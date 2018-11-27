#include <winsock2.h>
#include <assert.h>
#include <atomic>
#include <dplay8.h>
#include <iterator>
#include <memory>
#include <mutex>
#include <objbase.h>
#include <stdexcept>
#include <stdint.h>
#include <stdio.h>
#include <tuple>
#include <windows.h>
#include <ws2tcpip.h>

#include "COMAPIException.hpp"
#include "DirectPlay8Address.hpp"
#include "DirectPlay8Peer.hpp"
#include "Log.hpp"
#include "Messages.hpp"
#include "network.hpp"

#define UNIMPLEMENTED(fmt, ...) \
	log_printf("Unimplemented: " fmt, ## __VA_ARGS__); \
	return E_NOTIMPL;

#define RENEW_PEER_OR_RETURN() \
	peer = get_peer_by_peer_id(peer_id); \
	if(peer == NULL) \
	{ \
		return; \
	}

#define THREADS_PER_POOL     4
#define MAX_HANDLES_PER_POOL 16

/* Ephemeral port range as defined by IANA. */
static const int AUTO_PORT_MIN = 49152;
static const int AUTO_PORT_MAX = 65535;

DirectPlay8Peer::DirectPlay8Peer(std::atomic<unsigned int> *global_refcount):
	global_refcount(global_refcount),
	local_refcount(0),
	state(STATE_NEW),
	udp_socket(-1),
	listener_socket(-1),
	discovery_socket(-1),
	worker_pool(NULL),
	udp_sq(udp_socket_event)
{
	AddRef();
}

DirectPlay8Peer::~DirectPlay8Peer()
{
	if(state != STATE_NEW)
	{
		Close(DPNCLOSE_IMMEDIATE);
	}
}

HRESULT DirectPlay8Peer::QueryInterface(REFIID riid, void **ppvObject)
{
	if(riid == IID_IDirectPlay8Peer || riid == IID_IUnknown)
	{
		*((IUnknown**)(ppvObject)) = this;
		AddRef();
		
		return S_OK;
	}
	else{
		return E_NOINTERFACE;
	}
}

ULONG DirectPlay8Peer::AddRef(void)
{
	if(global_refcount != NULL)
	{
		++(*global_refcount);
	}
	
	return ++local_refcount;
}

ULONG DirectPlay8Peer::Release(void)
{
	std::atomic<unsigned int> *global_refcount = this->global_refcount;
	
	ULONG rc = --local_refcount;
	if(rc == 0)
	{
		delete this;
	}
	
	if(global_refcount != NULL)
	{
		--(*global_refcount);
	}
	
	return rc;
}

HRESULT DirectPlay8Peer::Initialize(PVOID CONST pvUserContext, CONST PFNDPNMESSAGEHANDLER pfn, CONST DWORD dwFlags)
{
	std::unique_lock<std::mutex> l(lock);
	
	if(state != STATE_NEW)
	{
		return DPNERR_ALREADYINITIALIZED;
	}
	
	WSADATA wd;
	if(WSAStartup(MAKEWORD(2,2), &wd) != 0)
	{
		log_printf("WSAStartup() failed");
		return DPNERR_GENERIC;
	}
	
	message_handler     = pfn;
	message_handler_ctx = pvUserContext;
	
	worker_pool = new HandleHandlingPool(THREADS_PER_POOL, MAX_HANDLES_PER_POOL);
	
	worker_pool->add_handle(udp_socket_event,   [this]() { handle_udp_socket_event();   });
	worker_pool->add_handle(other_socket_event, [this]() { handle_other_socket_event(); });
	worker_pool->add_handle(work_ready,         [this]() { handle_work(); });
	
	state = STATE_INITIALISED;
	
	return S_OK;
}

HRESULT DirectPlay8Peer::EnumServiceProviders(CONST GUID* CONST pguidServiceProvider, CONST GUID* CONST pguidApplication, DPN_SERVICE_PROVIDER_INFO* CONST pSPInfoBuffer, DWORD* CONST pcbEnumData, DWORD* CONST pcReturned, CONST DWORD dwFlags)
{
	static const DPN_SERVICE_PROVIDER_INFO IP_INFO  = { 0, CLSID_DP8SP_TCPIP, L"DirectPlay8 TCP/IP Service Provider", 0, 0 };
	static const DPN_SERVICE_PROVIDER_INFO IPX_INFO = { 0, CLSID_DP8SP_IPX,   L"DirectPlay8 IPX Service Provider",    0, 0 };
	
	std::unique_lock<std::mutex> l(lock);
	
	if(state == STATE_NEW)
	{
		return DPNERR_UNINITIALIZED;
	}
	
	if(pguidServiceProvider == NULL)
	{
		if(*pcbEnumData < (sizeof(DPN_SERVICE_PROVIDER_INFO) * 2))
		{
			*pcbEnumData = sizeof(DPN_SERVICE_PROVIDER_INFO) * 2;
			return DPNERR_BUFFERTOOSMALL;
		}
		
		pSPInfoBuffer[0] = IPX_INFO;
		pSPInfoBuffer[1] = IP_INFO;
		
		*pcbEnumData = sizeof(DPN_SERVICE_PROVIDER_INFO) * 2;
		*pcReturned  = 2;
		
		return S_OK;
	}
	else if(*pguidServiceProvider == CLSID_DP8SP_TCPIP || *pguidServiceProvider == CLSID_DP8SP_IPX)
	{
		if(*pcbEnumData < sizeof(DPN_SERVICE_PROVIDER_INFO))
		{
			*pcbEnumData = sizeof(DPN_SERVICE_PROVIDER_INFO);
			return DPNERR_BUFFERTOOSMALL;
		}
		
		if(*pguidServiceProvider == CLSID_DP8SP_TCPIP)
		{
			pSPInfoBuffer[0] = IP_INFO;
		}
		else if(*pguidServiceProvider == CLSID_DP8SP_IPX)
		{
			pSPInfoBuffer[0] = IPX_INFO;
		}
		
		*pcbEnumData = sizeof(DPN_SERVICE_PROVIDER_INFO);
		*pcReturned  = 1;
		
		return S_OK;
	}
	else{
		return DPNERR_DOESNOTEXIST;
	}
}

HRESULT DirectPlay8Peer::CancelAsyncOperation(CONST DPNHANDLE hAsyncHandle, CONST DWORD dwFlags)
{
	std::unique_lock<std::mutex> l(lock);
	
	if(dwFlags & DPNCANCEL_PLAYER_SENDS)
	{
		/* Cancel sends to player ID in hAsyncHandle */
		
		if(hAsyncHandle == local_player_id)
		{
			/* DirectX permits cancelling pending messages to the local player, but we
			 * don't queue loopback messages. So just do nothing.
			*/
			return S_OK;
		}
		
		Peer *peer = get_peer_by_player_id(hAsyncHandle);
		if(peer == NULL)
		{
			/* TODO: Is this the correct error? */
			return DPNERR_INVALIDPLAYER;
		}
		
		for(; peer != NULL; peer = get_peer_by_player_id(hAsyncHandle))
		{
			/* The DPNCANCEL_PLAYER_SENDS_* flags are horrible.
			 *
			 * Each priority-specific flag includes the DPNCANCEL_PLAYER_SENDS bit, so
			 * we need to check if ONLY the DPNCANCEL_PLAYER_SENDS bit is set, or if
			 * the priority-specific bit is set.
			*/
			
			DWORD send_flags = (dwFlags &
				( DPNCANCEL_PLAYER_SENDS_PRIORITY_LOW
				| DPNCANCEL_PLAYER_SENDS_PRIORITY_NORMAL
				| DPNCANCEL_PLAYER_SENDS_PRIORITY_HIGH));
			
			if(send_flags == DPNCANCEL_PLAYER_SENDS || (send_flags & DPNCANCEL_PLAYER_SENDS_PRIORITY_LOW) == DPNCANCEL_PLAYER_SENDS_PRIORITY_LOW)
			{
				SendQueue::SendOp *sqop = peer->sq.remove_queued_by_priority(SendQueue::SEND_PRI_LOW);
				if(sqop != NULL)
				{
					sqop->invoke_callback(l, DPNERR_USERCANCEL);
					delete sqop;
					continue;
				}
			}
			
			if(send_flags == DPNCANCEL_PLAYER_SENDS || (send_flags & DPNCANCEL_PLAYER_SENDS_PRIORITY_NORMAL) == DPNCANCEL_PLAYER_SENDS_PRIORITY_NORMAL)
			{
				SendQueue::SendOp *sqop = peer->sq.remove_queued_by_priority(SendQueue::SEND_PRI_MEDIUM);
				if(sqop != NULL)
				{
					sqop->invoke_callback(l, DPNERR_USERCANCEL);
					delete sqop;
					continue;
				}
			}
			
			if(send_flags == DPNCANCEL_PLAYER_SENDS || (send_flags & DPNCANCEL_PLAYER_SENDS_PRIORITY_HIGH) == DPNCANCEL_PLAYER_SENDS_PRIORITY_HIGH)
			{
				SendQueue::SendOp *sqop = peer->sq.remove_queued_by_priority(SendQueue::SEND_PRI_HIGH);
				if(sqop != NULL)
				{
					sqop->invoke_callback(l, DPNERR_USERCANCEL);
					delete sqop;
					continue;
				}
			}
			
			/* No more queued sends to cancel. */
			break;
		}
		
		return S_OK;
	}
	else if(dwFlags & (DPNCANCEL_ENUM | DPNCANCEL_CONNECT | DPNCANCEL_ALL_OPERATIONS))
	{
		/* Cancel all outstanding operations of one or more types. */
		
		if(dwFlags & (DPNCANCEL_ENUM | DPNCANCEL_ALL_OPERATIONS))
		{
			for(auto ei = async_host_enums.begin(); ei != async_host_enums.end(); ++ei)
			{
				ei->second.cancel();
			}
		}
		
		if(dwFlags & (DPNCANCEL_CONNECT | DPNCANCEL_ALL_OPERATIONS))
		{
			if((state == STATE_CONNECTING_TO_HOST || state == STATE_CONNECTING_TO_PEERS) && connect_handle != 0)
			{
				/* We have an ongoing asynchronous connection. */
				connect_fail(l, DPNERR_USERCANCEL, NULL, 0);
			}
		}
		
		if(dwFlags & DPNCANCEL_ALL_OPERATIONS)
		{
			for(auto p = peers.begin(); p != peers.end();)
			{
				Peer *peer = p->second;
				
				SendQueue::SendOp *sqop = peer->sq.remove_queued();
				
				if(sqop != NULL)
				{
					sqop->invoke_callback(l, DPNERR_USERCANCEL);
					delete sqop;
					
					/* Restart in case peers was munged. */
					p = peers.begin();
				}
				else{
					++p;
				}
			}
		}
		
		return S_OK;
	}
	else if((hAsyncHandle & AsyncHandleAllocator::TYPE_MASK) == AsyncHandleAllocator::TYPE_ENUM)
	{
		auto ei = async_host_enums.find(hAsyncHandle);
		if(ei == async_host_enums.end())
		{
			return DPNERR_INVALIDHANDLE;
		}
		
		/* TODO: Make successive cancels for the same handle before it is destroyed fail? */
		
		ei->second.cancel();
		return S_OK;
	}
	else if((hAsyncHandle & AsyncHandleAllocator::TYPE_MASK) == AsyncHandleAllocator::TYPE_CONNECT)
	{
		if(hAsyncHandle == connect_handle)
		{
			if(state == STATE_CONNECTING_TO_HOST || state == STATE_CONNECTING_TO_PEERS)
			{
				connect_fail(l, DPNERR_USERCANCEL, NULL, 0);
				return S_OK;
			}
			else{
				return DPNERR_CANNOTCANCEL;
			}
		}
		else{
			return DPNERR_INVALIDHANDLE;
		}
	}
	else if((hAsyncHandle & AsyncHandleAllocator::TYPE_MASK) == AsyncHandleAllocator::TYPE_SEND)
	{
		/* Search the send queues for a queued send with the handle. */
		
		SendQueue::SendOp *sqop = udp_sq.remove_queued_by_handle(hAsyncHandle);
		if(udp_sq.handle_is_pending(hAsyncHandle))
		{
			/* Cannot cancel once message has started sending. */
			return DPNERR_CANNOTCANCEL;
		}
		
		for(auto p = peers.begin(); p != peers.end() && sqop == NULL; ++p)
		{
			Peer *peer = p->second;
			
			sqop = peer->sq.remove_queued_by_handle(hAsyncHandle);
			if(peer->sq.handle_is_pending(hAsyncHandle))
			{
				/* Cannot cancel once message has started sending. */
				return DPNERR_CANNOTCANCEL;
			}
		}
		
		if(sqop != NULL)
		{
			/* Queued send was found, make it go away. */
			sqop->invoke_callback(l, DPNERR_USERCANCEL);
			delete sqop;
			
			return S_OK;
		}
		else{
			/* No pending send with that handle. */
			return DPNERR_INVALIDHANDLE;
		}
	}
	else{
		/* Unrecognised handle type. */
		return DPNERR_INVALIDHANDLE;
	}
}

HRESULT DirectPlay8Peer::Connect(CONST DPN_APPLICATION_DESC* CONST pdnAppDesc, IDirectPlay8Address* CONST pHostAddr, IDirectPlay8Address* CONST pDeviceInfo, CONST DPN_SECURITY_DESC* CONST pdnSecurity, CONST DPN_SECURITY_CREDENTIALS* CONST pdnCredentials, CONST void* CONST pvUserConnectData, CONST DWORD dwUserConnectDataSize, void* CONST pvPlayerContext, void* CONST pvAsyncContext, DPNHANDLE* CONST phAsyncHandle, CONST DWORD dwFlags)
{
	std::unique_lock<std::mutex> l(lock);
	
	switch(state)
	{
		case STATE_NEW:                 return DPNERR_UNINITIALIZED;
		case STATE_INITIALISED:         break;
		case STATE_HOSTING:             return DPNERR_HOSTING;
		case STATE_CONNECTING_TO_HOST:  return DPNERR_CONNECTING;
		case STATE_CONNECTING_TO_PEERS: return DPNERR_CONNECTING;
		case STATE_CONNECTED:           return DPNERR_ALREADYCONNECTED;
	}
	
	application_guid = pdnAppDesc->guidApplication;
	instance_guid    = pdnAppDesc->guidInstance;
	
	connect_req_data.clear();
	
	if(dwUserConnectDataSize > 0)
	{
		connect_req_data.reserve(dwUserConnectDataSize);
		connect_req_data.insert(connect_req_data.end(),
			(const unsigned char*)(pvUserConnectData),
			(const unsigned char*)(pvUserConnectData) + dwUserConnectDataSize);
	}
	
	local_player_ctx = pvPlayerContext;
	
	uint32_t l_ipaddr = htonl(INADDR_ANY);
	uint16_t l_port   = 0;
	
	/* TODO: Get bind address/port from pDeviceInfo, if specified. */
	
	/* Extract remote IP and port from pHostAddr. */
	
	uint32_t r_ipaddr;
	uint16_t r_port;
	
	GUID host_sp;
	if(pHostAddr->GetSP(&host_sp) != S_OK)
	{
		return DPNERR_INVALIDHOSTADDRESS;
	}
	
	wchar_t hostname_value[128];
	DWORD hostname_size = sizeof(hostname_value);
	DWORD hostname_type;
	
	if(pHostAddr->GetComponentByName(DPNA_KEY_HOSTNAME, hostname_value, &hostname_size, &hostname_type) == S_OK)
	{
		if(hostname_type != DPNA_DATATYPE_STRING)
		{
			return DPNERR_INVALIDHOSTADDRESS;
		}
		
		if(host_sp == CLSID_DP8SP_TCPIP)
		{
			struct in_addr hostname_addr;
			if(InetPtonW(AF_INET, hostname_value, &hostname_addr) == 1)
			{
				r_ipaddr = hostname_addr.s_addr;
			}
			else{
				return DPNERR_INVALIDHOSTADDRESS;
			}
		}
		else if(host_sp == CLSID_DP8SP_IPX)
		{
			unsigned ip;
			if(swscanf(hostname_value, L"00000000,0000%08X", &ip) != 1)
			{
				return DPNERR_INVALIDHOSTADDRESS;
			}
			
			r_ipaddr = htonl(ip);
		}
		else{
			return DPNERR_INVALIDHOSTADDRESS;
		}
	}
	else{
		return DPNERR_INVALIDHOSTADDRESS;
	}
	
	DWORD port_value;
	DWORD port_size = sizeof(port_value);
	DWORD port_type;
	
	if(pHostAddr->GetComponentByName(DPNA_KEY_PORT, &port_value, &port_size, &port_type) == S_OK)
	{
		if(port_type != DPNA_DATATYPE_DWORD || port_value > 65535)
		{
			return DPNERR_INVALIDHOSTADDRESS;
		}
		
		r_port = port_value;
	}
	else{
		return DPNERR_INVALIDHOSTADDRESS;
	}
	
	if(l_port == 0)
	{
		/* Start at a random point in the ephemeral port range and try each one, wrapping
		 * around when we reach the end.
		 *
		 * Gets the "random" point by querying the performance counter rather than calling
		 * rand() just in case the application relies on the RNG state.
		*/
		
		LARGE_INTEGER p_counter;
		QueryPerformanceCounter(&p_counter);
		
		int port_range = AUTO_PORT_MAX - AUTO_PORT_MIN;
		int base_port  = p_counter.QuadPart % port_range;
		
		for(int p = AUTO_PORT_MIN; p <= AUTO_PORT_MAX; ++p)
		{
			/* TODO: Only continue if creation failed due to address conflict. */
			
			int port = AUTO_PORT_MIN + ((base_port + p) % (port_range + 1));
			
			udp_socket = create_udp_socket(l_ipaddr, port);
			if(udp_socket == -1)
			{
				continue;
			}
			
			listener_socket = create_listener_socket(l_ipaddr, port);
			if(listener_socket == -1)
			{
				closesocket(udp_socket);
				udp_socket = -1;
				
				continue;
			}
			
			local_ip   = l_ipaddr;
			local_port = port;
			
			break;
		}
		
		if(udp_socket == -1)
		{
			return DPNERR_GENERIC;
		}
	}
	else{
		udp_socket = create_udp_socket(l_ipaddr, l_port);
		if(udp_socket == -1)
		{
			return DPNERR_GENERIC;
		}
		
		listener_socket = create_listener_socket(l_ipaddr, l_port);
		if(listener_socket == -1)
		{
			closesocket(udp_socket);
			udp_socket = -1;
			
			return DPNERR_GENERIC;
		}
		
		local_ip   = l_ipaddr;
		local_port = l_port;
	}
	
	connect_ctx    = pvAsyncContext;
	connect_handle = (dwFlags & DPNCONNECT_SYNC) ? 0 : handle_alloc.new_connect();
	
	state = STATE_CONNECTING_TO_HOST;
	
	if(!peer_connect(Peer::PS_CONNECTING_HOST, r_ipaddr, r_port))
	{
		closesocket(listener_socket);
		listener_socket = -1;
		
		closesocket(udp_socket);
		udp_socket = -1;
		
		return DPNERR_GENERIC;
	}
	
	if(WSAEventSelect(udp_socket, udp_socket_event, FD_READ | FD_WRITE) != 0
		|| WSAEventSelect(listener_socket, other_socket_event, FD_ACCEPT) != 0)
	{
		return DPNERR_GENERIC;
	}
	
	if(dwFlags & DPNCONNECT_SYNC)
	{
		connect_cv.wait(l, [this]() { return (state != STATE_CONNECTING_TO_HOST && state != STATE_CONNECTING_TO_PEERS && state != STATE_CONNECT_FAILED); });
		return connect_result;
	}
	else{
		*phAsyncHandle = connect_handle;
		return DPNSUCCESS_PENDING;
	}
}

HRESULT DirectPlay8Peer::SendTo(CONST DPNID dpnid, CONST DPN_BUFFER_DESC* CONST prgBufferDesc, CONST DWORD cBufferDesc, CONST DWORD dwTimeOut, void* CONST pvAsyncContext, DPNHANDLE* CONST phAsyncHandle, CONST DWORD dwFlags)
{
	std::unique_lock<std::mutex> l(lock);
	
	switch(state)
	{
		case STATE_NEW:                 return DPNERR_UNINITIALIZED;
		case STATE_INITIALISED:         return DPNERR_NOTREADY;
		case STATE_HOSTING:             break;
		case STATE_CONNECTING_TO_HOST:  break;
		case STATE_CONNECTING_TO_PEERS: break;
		case STATE_CONNECT_FAILED:      return DPNERR_NOCONNECTION;
		case STATE_CONNECTED:           break;
		case STATE_CLOSING:             return DPNERR_NOCONNECTION;
		case STATE_TERMINATED:          return DPNERR_NOCONNECTION;
	}
	
	if(dwFlags & DPNSEND_COMPLETEONPROCESS)
	{
		/* TODO: Implement DPNSEND_COMPLETEONPROCESS */
		return DPNERR_GENERIC;
	}
	
	std::vector<unsigned char> payload;
	
	for(DWORD i = 0; i < cBufferDesc; ++i)
	{
		payload.reserve(payload.size() + prgBufferDesc[i].dwBufferSize);
		payload.insert(payload.end(),
			(const unsigned char*)(prgBufferDesc[i].pBufferData),
			(const unsigned char*)(prgBufferDesc[i].pBufferData) + prgBufferDesc[i].dwBufferSize);
	}
	
	PacketSerialiser message(DPLITE_MSGID_MESSAGE);
	
	message.append_dword(local_player_id);
	message.append_data(payload.data(), payload.size());
	message.append_dword(dwFlags & (DPNSEND_GUARANTEED | DPNSEND_COALESCE | DPNSEND_COMPLETEONPROCESS));
	
	SendQueue::SendPriority priority = SendQueue::SEND_PRI_MEDIUM;
	if(dwFlags & DPNSEND_PRIORITY_HIGH)
	{
		priority = SendQueue::SEND_PRI_HIGH;
	}
	else if(dwFlags & DPNSEND_PRIORITY_LOW)
	{
		priority = SendQueue::SEND_PRI_LOW;
	}
	
	std::list<Peer*> send_to_peers;
	bool send_to_self = false;
	
	if(dpnid == DPNID_ALL_PLAYERS_GROUP)
	{
		if(!(dwFlags & DPNSEND_NOLOOPBACK))
		{
			send_to_self = true;
		}
		
		for(auto pi = peers.begin(); pi != peers.end(); ++pi)
		{
			if(pi->second->state == Peer::PS_CONNECTED)
			{
				send_to_peers.push_back(pi->second);
			}
		}
	}
	else{
		Peer *target_peer;
		Group *target_group;
		
		if(dpnid == local_player_id)
		{
			send_to_self = true;
		}
		else if((target_peer = get_peer_by_player_id(dpnid)) != NULL)
		{
			send_to_peers.push_back(target_peer);
		}
		else if((target_group = get_group_by_id(dpnid)) != NULL)
		{
			for(auto m = target_group->player_ids.begin(); m != target_group->player_ids.end(); ++m)
			{
				if(*m == local_player_id)
				{
					if(!(dwFlags & DPNSEND_NOLOOPBACK))
					{
						send_to_self = true;
					}
				}
				else{
					target_peer = get_peer_by_player_id(*m);
					assert(target_peer != NULL);
					
					send_to_peers.push_back(target_peer);
				}
			}
		}
		else{
			return DPNERR_INVALIDPLAYER;
		}
	}
	
	if(dwFlags & DPNSEND_SYNC)
	{
		unsigned int pending = send_to_peers.size();
		std::mutex d_mutex;
		std::condition_variable d_cv;
		HRESULT result = S_OK;
		
		for(auto pi = send_to_peers.begin(); pi != send_to_peers.end(); ++pi)
		{
			(*pi)->sq.send(priority, message, NULL,
				[&pending, &d_mutex, &d_cv, &result]
				(std::unique_lock<std::mutex> &l, HRESULT s_result)
				{
					if(s_result != S_OK && result == S_OK)
					{
						/* Error code from the first failure wins. */
						result = s_result;
					}
					
					std::unique_lock<std::mutex> dl(d_mutex);
					
					if(--pending == 0)
					{
						dl.unlock();
						d_cv.notify_one();
					}
				});
		}
		
		if(send_to_self)
		{
			/* TODO: Should the processing of this block a DPNSEND_SYNC send? */
			
			unsigned char *payload_copy = new unsigned char[payload.size()];
			memcpy(payload_copy, payload.data(), payload.size());
			
			DPNMSG_RECEIVE r;
			memset(&r, 0, sizeof(r));
			
			r.dwSize            = sizeof(r);
			r.dpnidSender       = local_player_id;
			r.pvPlayerContext   = local_player_ctx;
			r.pReceiveData      = payload_copy;
			r.dwReceiveDataSize = payload.size();
			r.hBufferHandle     = (DPNHANDLE)(payload_copy);
			r.dwReceiveFlags    = (dwFlags & DPNSEND_GUARANTEED ? DPNRECEIVE_GUARANTEED : 0)
			                    | (dwFlags & DPNSEND_COALESCE   ? DPNRECEIVE_COALESCED  : 0);
			
			l.unlock();
			
			HRESULT r_result = message_handler(message_handler_ctx, DPN_MSGID_RECEIVE, &r);
			if(r_result != DPNSUCCESS_PENDING)
			{
				delete[] payload_copy;
			}
		}
		else{
			l.unlock();
		}
		
		std::unique_lock<std::mutex> dl(d_mutex);
		d_cv.wait(dl, [&pending]() { return (pending == 0); });
		
		return result;
	}
	else{
		unsigned int *pending = new unsigned int(send_to_peers.size() + send_to_self);
		HRESULT      *result  = new HRESULT(S_OK);
		
		DPNHANDLE handle = handle_alloc.new_send();
		*phAsyncHandle   = handle;
		
		auto handle_send_complete =
			[this, pending, result, pvAsyncContext, dwFlags, prgBufferDesc, cBufferDesc, handle]
			(std::unique_lock<std::mutex> &l, HRESULT s_result)
		{
			if(s_result != S_OK && *result == S_OK)
			{
				/* Error code from the first failure wins. */
				*result = s_result;
			}
			
			if(--(*pending) == 0)
			{
				DPNMSG_SEND_COMPLETE sc;
				memset(&sc, 0, sizeof(sc));
				
				sc.dwSize = sizeof(sc);
				sc.hAsyncOp = handle;
				sc.pvUserContext = pvAsyncContext;
				sc.hResultCode   = *result;
				// sc.dwSendTime
				// sc.dwFirstFrameRTT
				// sc.dwFirstRetryCount
				sc.dwSendCompleteFlags = (dwFlags & DPNSEND_GUARANTEED ? DPNRECEIVE_GUARANTEED : 0)
				                       | (dwFlags & DPNSEND_COALESCE   ? DPNRECEIVE_COALESCED  : 0);
				
				if(dwFlags & DPNSEND_NOCOPY)
				{
					sc.pBuffers     = (DPN_BUFFER_DESC*)(prgBufferDesc);
					sc.dwNumBuffers = cBufferDesc;
				}
				
				delete result;
				delete pending;
				
				if(!(dwFlags & DPNSEND_NOCOMPLETE))
				{
					l.unlock();
					message_handler(message_handler_ctx, DPN_MSGID_SEND_COMPLETE, &sc);
					l.lock();
				}
			}
		};
		
		if(*pending == 0)
		{
			/* Horrible horrible hack to raise a DPNMSG_SEND_COMPLETE if there are no
			 * targets for the message.
			*/
			
			++(*pending);
			
			std::thread t([this, handle_send_complete]()
			{
				std::unique_lock<std::mutex> l(lock);
				handle_send_complete(l, S_OK);
			});
			
			t.detach();
		}
		
		for(auto pi = send_to_peers.begin(); pi != send_to_peers.end(); ++pi)
		{
			(*pi)->sq.send(priority, message, NULL, handle,
				[handle_send_complete]
				(std::unique_lock<std::mutex> &l, HRESULT s_result)
				{
					handle_send_complete(l, s_result);
				});
		}
		
		if(send_to_self)
		{
			size_t payload_size = payload.size();
			
			unsigned char *payload_copy = new unsigned char[payload_size];
			memcpy(payload_copy, payload.data(), payload_size);
			
			queue_work([this, payload_size, payload_copy, handle_send_complete, dwFlags]()
			{
				std::unique_lock<std::mutex> l(lock);
				
				DPNMSG_RECEIVE r;
				memset(&r, 0, sizeof(r));
				
				r.dwSize            = sizeof(r);
				r.dpnidSender       = local_player_id;
				r.pvPlayerContext   = local_player_ctx;
				r.pReceiveData      = payload_copy;
				r.dwReceiveDataSize = payload_size;
				r.hBufferHandle     = (DPNHANDLE)(payload_copy);
				r.dwReceiveFlags    = (dwFlags & DPNSEND_GUARANTEED ? DPNRECEIVE_GUARANTEED : 0)
				                    | (dwFlags & DPNSEND_COALESCE   ? DPNRECEIVE_COALESCED  : 0);
				
				l.unlock();
				HRESULT r_result = message_handler(message_handler_ctx, DPN_MSGID_RECEIVE, &r);
				l.lock();
				
				if(r_result != DPNSUCCESS_PENDING)
				{
					delete[] payload_copy;
				}
				
				handle_send_complete(l, S_OK);
			});
		}
		
		return DPNSUCCESS_PENDING;
	}
}

HRESULT DirectPlay8Peer::GetSendQueueInfo(CONST DPNID dpnid, DWORD* CONST pdwNumMsgs, DWORD* CONST pdwNumBytes, CONST DWORD dwFlags)
{
	UNIMPLEMENTED("DirectPlay8Peer::GetSendQueueInfo");
}

HRESULT DirectPlay8Peer::Host(CONST DPN_APPLICATION_DESC* CONST pdnAppDesc, IDirectPlay8Address **CONST prgpDeviceInfo, CONST DWORD cDeviceInfo, CONST DPN_SECURITY_DESC* CONST pdnSecurity, CONST DPN_SECURITY_CREDENTIALS* CONST pdnCredentials, void* CONST pvPlayerContext, CONST DWORD dwFlags)
{
	std::unique_lock<std::mutex> l(lock);
	
	switch(state)
	{
		case STATE_NEW:                 return DPNERR_UNINITIALIZED;
		case STATE_INITIALISED:         break;
		case STATE_HOSTING:             return DPNERR_ALREADYCONNECTED;
		case STATE_CONNECTING_TO_HOST:  return DPNERR_CONNECTING;
		case STATE_CONNECTING_TO_PEERS: return DPNERR_CONNECTING;
		case STATE_CONNECTED:           return DPNERR_ALREADYCONNECTED;
	}
	
	if(pdnAppDesc->dwSize != sizeof(DPN_APPLICATION_DESC))
	{
		return DPNERR_INVALIDPARAM;
	}
	
	if(pdnAppDesc->dwFlags & DPNSESSION_CLIENT_SERVER)
	{
		return DPNERR_INVALIDPARAM;
	}
	
	if(pdnAppDesc->dwFlags & DPNSESSION_MIGRATE_HOST)
	{
		/* Not supported yet. */
	}
	
	if(cDeviceInfo == 0)
	{
		return DPNERR_INVALIDPARAM;
	}
	
	/* Generate a random GUID for this session. */
	HRESULT guid_err = CoCreateGuid(&instance_guid);
	if(guid_err != S_OK)
	{
		return guid_err;
	}
	
	application_guid = pdnAppDesc->guidApplication;
	max_players      = pdnAppDesc->dwMaxPlayers;
	session_name     = (pdnAppDesc->pwszSessionName != NULL ? pdnAppDesc->pwszSessionName : L"(null)");
	
	if(pdnAppDesc->dwFlags & DPNSESSION_REQUIREPASSWORD)
	{
		password = pdnAppDesc->pwszPassword;
	}
	else{
		password.clear();
	}
	
	application_data.clear();
	
	if(pdnAppDesc->pvApplicationReservedData != NULL && pdnAppDesc->dwApplicationReservedDataSize > 0)
	{
		application_data.insert(application_data.begin(),
			(unsigned char*)(pdnAppDesc->pvApplicationReservedData),
			(unsigned char*)(pdnAppDesc->pvApplicationReservedData) + pdnAppDesc->dwApplicationReservedDataSize);
	}
	
	GUID     sp     = GUID_NULL;
	uint32_t ipaddr = htonl(INADDR_ANY);
	uint16_t port   = 0;
	
	for(DWORD i = 0; i < cDeviceInfo; ++i)
	{
		DirectPlay8Address *addr = (DirectPlay8Address*)(prgpDeviceInfo[i]);
		
		GUID this_sp;
		if(addr->GetSP(&this_sp) != S_OK)
		{
			return DPNERR_INVALIDDEVICEADDRESS;
		}
		
		if(sp != GUID_NULL && this_sp != sp)
		{
			/* Multiple service providers specified, don't support this yet. */
			return E_NOTIMPL;
		}
		
		if(this_sp != CLSID_DP8SP_TCPIP && this_sp != CLSID_DP8SP_IPX)
		{
			/* Only support TCP/IP and IPX addresses at this time. */
			return DPNERR_INVALIDDEVICEADDRESS;
		}
		
		sp = this_sp;
		
		DWORD addr_port_value;
		DWORD addr_port_size = sizeof(addr_port_value);
		DWORD addr_port_type;
		
		if(addr->GetComponentByName(DPNA_KEY_PORT, &addr_port_value, &addr_port_size, &addr_port_type) == S_OK
			&& addr_port_type == DPNA_DATATYPE_DWORD)
		{
			if(port != 0 && port != addr_port_value)
			{
				/* Multiple ports specified, don't support this yet. */
				return DPNERR_INVALIDPARAM;
			}
			else{
				port = addr_port_value;
			}
		}
	}
	
	service_provider = sp;
	
	if(port == 0)
	{
		for(int p = AUTO_PORT_MIN; p <= AUTO_PORT_MAX; ++p)
		{
			/* TODO: Only continue if creation failed due to address conflict. */
			
			udp_socket = create_udp_socket(ipaddr, p);
			if(udp_socket == -1)
			{
				continue;
			}
			
			listener_socket = create_listener_socket(ipaddr, p);
			if(listener_socket == -1)
			{
				closesocket(udp_socket);
				udp_socket = -1;
				
				continue;
			}
			
			local_ip   = ipaddr;
			local_port = p;
			
			break;
		}
		
		if(udp_socket == -1)
		{
			return DPNERR_GENERIC;
		}
	}
	else{
		udp_socket = create_udp_socket(ipaddr, port);
		if(udp_socket == -1)
		{
			return DPNERR_GENERIC;
		}
		
		listener_socket = create_listener_socket(ipaddr, port);
		if(listener_socket == -1)
		{
			closesocket(udp_socket);
			udp_socket = -1;
			
			return DPNERR_GENERIC;
		}
		
		local_ip   = ipaddr;
		local_port = port;
	}
	
	if(WSAEventSelect(udp_socket, udp_socket_event, FD_READ | FD_WRITE) != 0
		|| WSAEventSelect(listener_socket, other_socket_event, FD_ACCEPT) != 0)
	{
		return DPNERR_GENERIC;
	}
	
	if(!(pdnAppDesc->dwFlags & DPNSESSION_NODPNSVR))
	{
		discovery_socket = create_discovery_socket();
		
		if(discovery_socket == -1
			|| WSAEventSelect(discovery_socket, other_socket_event, FD_READ) != 0)
		{
			return DPNERR_GENERIC;
		}
	}
	
	next_player_id = 1;
	host_player_id = next_player_id++;
	
	local_player_id  = host_player_id;
	local_player_ctx = pvPlayerContext;
	
	state = STATE_HOSTING;
	
	/* Send DPNMSG_CREATE_PLAYER for local player. */
	dispatch_create_player(l, local_player_id, &local_player_ctx);
	
	return S_OK;
}

HRESULT DirectPlay8Peer::GetApplicationDesc(DPN_APPLICATION_DESC* CONST pAppDescBuffer, DWORD* CONST pcbDataSize, CONST DWORD dwFlags)
{
	std::unique_lock<std::mutex> l(lock);
	
	switch(state)
	{
		case STATE_NEW:                 return DPNERR_UNINITIALIZED;
		case STATE_HOSTING:             break;
		case STATE_CONNECTED:           break;
		case STATE_CONNECTING_TO_HOST:  return DPNERR_CONNECTING;
		case STATE_CONNECTING_TO_PEERS: return DPNERR_CONNECTING;
		default:                        return DPNERR_NOCONNECTION;
	}
	
	DWORD required_size = sizeof(DPN_APPLICATION_DESC)
		+ (session_name.length() + 1) * sizeof(wchar_t)
		+ (password.length() + !password.empty()) * sizeof(wchar_t)
		+ application_data.size();
	
	if(*pcbDataSize >= sizeof(DPN_APPLICATION_DESC) && pAppDescBuffer->dwSize != sizeof(DPN_APPLICATION_DESC))
	{
		return DPNERR_INVALIDPARAM;
	}
	
	if(*pcbDataSize >= required_size)
	{
		unsigned char *extra_at = (unsigned char*)(pAppDescBuffer + 1);
		
		pAppDescBuffer->dwFlags          = 0;
		pAppDescBuffer->guidInstance     = instance_guid;
		pAppDescBuffer->guidApplication  = application_guid;
		pAppDescBuffer->dwMaxPlayers     = max_players;
		pAppDescBuffer->dwCurrentPlayers = player_to_peer_id.size() + 1;
		
		wcscpy((wchar_t*)(extra_at), session_name.c_str());
		pAppDescBuffer->pwszSessionName = (wchar_t*)(extra_at);
		extra_at += (session_name.length() + 1) * sizeof(wchar_t);
		
		if(!password.empty())
		{
			wcscpy((wchar_t*)(extra_at), password.c_str());
			
			pAppDescBuffer->dwFlags |= DPNSESSION_REQUIREPASSWORD;
			pAppDescBuffer->pwszPassword = (wchar_t*)(extra_at);
			
			extra_at += (password.length() + 1) * sizeof(wchar_t);
		}
		else{
			pAppDescBuffer->pwszPassword = NULL;
		}
		
		pAppDescBuffer->pvReservedData     = NULL;
		pAppDescBuffer->dwReservedDataSize = 0;
		
		if(!application_data.empty())
		{
			memcpy(extra_at, application_data.data(), application_data.size());
			
			pAppDescBuffer->pvApplicationReservedData     = extra_at;
			pAppDescBuffer->dwApplicationReservedDataSize = application_data.size();
			
			extra_at += application_data.size();
		}
		else{
			pAppDescBuffer->pvApplicationReservedData     = NULL;
			pAppDescBuffer->dwApplicationReservedDataSize = 0;
		}
		
		return S_OK;
	}
	else{
		*pcbDataSize = required_size;
		return DPNERR_BUFFERTOOSMALL;
	}
}

HRESULT DirectPlay8Peer::SetApplicationDesc(CONST DPN_APPLICATION_DESC* CONST pad, CONST DWORD dwFlags)
{
	std::unique_lock<std::mutex> l(lock);
	
	switch(state)
	{
		case STATE_NEW:     return DPNERR_UNINITIALIZED;
		case STATE_HOSTING: break;
		default:            return DPNERR_NOTHOST;
	}
	
	if(pad->dwMaxPlayers > 0 && pad->dwMaxPlayers <= player_to_peer_id.size())
	{
		/* Can't set dwMaxPlayers below current player count. */
		return DPNERR_INVALIDPARAM;
	}
	
	max_players  = pad->dwMaxPlayers;
	session_name = pad->pwszSessionName;
	
	if(pad->dwFlags & DPNSESSION_REQUIREPASSWORD)
	{
		password = pad->pwszPassword;
	}
	else{
		password.clear();
	}
	
	application_data.clear();
	
	if(pad->pvApplicationReservedData != NULL && pad->dwApplicationReservedDataSize > 0)
	{
		application_data.insert(application_data.begin(),
			(unsigned char*)(pad->pvApplicationReservedData),
			(unsigned char*)(pad->pvApplicationReservedData) + pad->dwApplicationReservedDataSize);
	}
	
	/* Notify all peers of the new application description. We don't wait for confirmation
	 * from the other peers, they'll get it when they get it.
	*/
	
	PacketSerialiser appdesc(DPLITE_MSGID_APPDESC);
	
	appdesc.append_dword(max_players);
	appdesc.append_wstring(session_name);
	appdesc.append_wstring(password);
	appdesc.append_data(application_data.data(), application_data.size());
	
	for(auto pi = peers.begin(); pi != peers.end(); ++pi)
	{
		if(pi->second->state != Peer::PS_CONNECTED)
		{
			continue;
		}
		
		pi->second->sq.send(SendQueue::SEND_PRI_MEDIUM, appdesc, NULL,
			[](std::unique_lock<std::mutex> &l, HRESULT result) {});
	}
	
	/* And finally, notify ourself about it.
	 * TODO: Should this block the calling thread?
	*/
	
	l.unlock();
	message_handler(message_handler_ctx, DPN_MSGID_APPLICATION_DESC, NULL);
	
	return S_OK;
}

HRESULT DirectPlay8Peer::CreateGroup(CONST DPN_GROUP_INFO* CONST pdpnGroupInfo, void* CONST pvGroupContext, void* CONST pvAsyncContext, DPNHANDLE* CONST phAsyncHandle, CONST DWORD dwFlags)
{
	std::unique_lock<std::mutex> l(lock);
	
	switch(state)
	{
		case STATE_NEW:                 return DPNERR_UNINITIALIZED;
		case STATE_INITIALISED:         return DPNERR_NOCONNECTION;
		case STATE_HOSTING:             break;
		case STATE_CONNECTING_TO_HOST:  return DPNERR_CONNECTING;
		case STATE_CONNECTING_TO_PEERS: return DPNERR_CONNECTING;
		case STATE_CONNECT_FAILED:      return DPNERR_CONNECTING;
		case STATE_CONNECTED:           break;
		case STATE_CLOSING:             return DPNERR_CONNECTIONLOST;
		case STATE_TERMINATED:          return DPNERR_CONNECTIONLOST;
	}
	
	if(pdpnGroupInfo == NULL)
	{
		return DPNERR_INVALIDPARAM;
	}
	
	if(pdpnGroupInfo->dwGroupFlags & DPNGROUP_AUTODESTRUCT)
	{
		log_printf("DirectPlay8Peer::CreateGroup() called with DPNGROUP_AUTODESTRUCT");
		return E_NOTIMPL;
	}
	
	std::wstring group_name;
	if(pdpnGroupInfo->dwInfoFlags & DPNINFO_NAME)
	{
		group_name = pdpnGroupInfo->pwszName;
	}
	
	std::vector<unsigned char> group_data;
	if(pdpnGroupInfo->dwInfoFlags & DPNINFO_DATA)
	{
		group_data.reserve(pdpnGroupInfo->dwDataSize);
		group_data.insert(group_data.end(),
			(const unsigned char*)(pdpnGroupInfo->pvData),
			(const unsigned char*)(pdpnGroupInfo->pvData) + pdpnGroupInfo->dwDataSize);
	}
	
	DPNHANDLE async_handle;
	if(!(dwFlags & DPNCREATEGROUP_SYNC))
	{
		async_handle = handle_alloc.new_cgroup();
		
		if(phAsyncHandle != NULL)
		{
			*phAsyncHandle = async_handle;
		}
	}
	
	std::mutex *cg_lock = new std::mutex;
	int *pending = new int(1);
	
	std::condition_variable sync_cv;
	HRESULT sync_result = S_OK;
	
	auto complete =
		[cg_lock, pending, dwFlags, &sync_cv, &sync_result, async_handle, pvAsyncContext, this]
		(std::unique_lock<std::mutex> &l, HRESULT result)
	{
		std::unique_lock<std::mutex> cgl(*cg_lock);
		
		if(--(*pending) == 0)
		{
			if(dwFlags & DPNCREATEGROUP_SYNC)
			{
				sync_result = result;
				cgl.unlock();
				sync_cv.notify_one();
			}
			else{
				DPNMSG_ASYNC_OP_COMPLETE oc;
				memset(&oc, 0, sizeof(oc));
				
				oc.dwSize        = sizeof(oc);
				oc.hAsyncOp      = async_handle;
				oc.pvUserContext = pvAsyncContext;
				oc.hResultCode   = result;
				
				l.unlock();
				message_handler(message_handler_ctx, DPN_MSGID_ASYNC_OP_COMPLETE, &oc);
				l.lock();
				
				cgl.unlock();
				
				delete pending;
				delete cg_lock;
			}
		}
	};
	
	auto create_the_group =
		[this, group_name, group_data, pvGroupContext, cg_lock, pending, complete]
		(std::unique_lock<std::mutex> &l, DPNID group_id)
	{
		groups.emplace(
			std::piecewise_construct,
			std::forward_as_tuple(group_id),
			std::forward_as_tuple(group_name, group_data.data(), group_data.size(), pvGroupContext));
		
		PacketSerialiser group_create(DPLITE_MSGID_GROUP_CREATE);
		group_create.append_dword(group_id);
		group_create.append_wstring(group_name);
		group_create.append_data(group_data.data(), group_data.size());
		
		std::unique_lock<std::mutex> cgl(*cg_lock);
		
		for(auto p = peers.begin(); p != peers.end(); ++p)
		{
			Peer *peer = p->second;
			
			if(peer->state == Peer::PS_CONNECTED)
			{
				++(*pending);
				
				peer->sq.send(SendQueue::SEND_PRI_HIGH, group_create, NULL,
					[complete]
					(std::unique_lock<std::mutex> &l, HRESULT result)
					{
						if(result != S_OK)
						{
							log_printf("Failed to send DPLITE_MSGID_GROUP_CREATE, session may be out of sync!");
						}
						
						complete(l, S_OK);
					});
			}
		}
		
		cgl.unlock();
		
		/* Raise local DPNMSG_CREATE_GROUP.
		 * TODO: Do this in a properly managed worker thread.
		*/
		
		std::thread t([this, group_id, pvGroupContext, complete]()
		{
			std::unique_lock<std::mutex> l(lock);
			
			DPNMSG_CREATE_GROUP cg;
			memset(&cg, 0, sizeof(cg));
			
			cg.dwSize         = sizeof(cg);
			cg.dpnidGroup     = group_id;
			cg.dpnidOwner     = 0;
			cg.pvGroupContext = pvGroupContext;
			cg.pvOwnerContext = local_player_ctx;
			
			l.unlock();
			message_handler(message_handler_ctx, DPN_MSGID_CREATE_GROUP, &cg);
			l.lock();
			
			Group *group = get_group_by_id(group_id);
			if(group != NULL)
			{
				group->ctx = cg.pvGroupContext;
			}
			
			complete(l, S_OK);
		});
		
		t.detach();
	};
	
	if(local_player_id == host_player_id)
	{
		DPNID group_id = next_player_id++;
		create_the_group(l, group_id);
	}
	else{
		DPNID host_id = host_player_id;
		
		Peer *host = get_peer_by_player_id(host_id);
		assert(host != NULL);
		
		DWORD ack_id = host->alloc_ack_id();
		
		PacketSerialiser group_allocate(DPLITE_MSGID_GROUP_ALLOCATE);
		group_allocate.append_dword(ack_id);
		
		host->sq.send(SendQueue::SEND_PRI_HIGH, group_allocate, NULL,
			[this, ack_id, host_id, create_the_group, complete]
			(std::unique_lock<std::mutex> &l, HRESULT result)
			{
				if(result == S_OK)
				{
					Peer *host = get_peer_by_player_id(host_id);
					assert(host != NULL);
					
					host->register_ack(ack_id,
						[create_the_group, complete]
						(std::unique_lock<std::mutex> &l, DWORD result, const void *data, size_t data_size)
						{
							if(result == S_OK && data_size == sizeof(DPNID))
							{
								DPNID group_id = *(DPNID*)(data);
								create_the_group(l, group_id);
							}
							else{
								complete(l, result);
							}
						});
				}
				else{
					complete(l, result);
				}
			});
	}
	
	if(dwFlags & DPNCREATEGROUP_SYNC)
	{
		l.unlock();
		
		std::unique_lock<std::mutex> cgl(*cg_lock);
		sync_cv.wait(cgl, [&pending]() { return (*pending == 0); });
		cgl.unlock();
		
		delete pending;
		delete cg_lock;
		
		return sync_result;
	}
	else{
		return DPNSUCCESS_PENDING;
	}
}

HRESULT DirectPlay8Peer::DestroyGroup(CONST DPNID idGroup, PVOID CONST pvAsyncContext, DPNHANDLE* CONST phAsyncHandle, CONST DWORD dwFlags)
{
	std::unique_lock<std::mutex> l(lock);
	
	switch(state)
	{
		case STATE_NEW:                 return DPNERR_UNINITIALIZED;
		case STATE_INITIALISED:         return DPNERR_NOCONNECTION;
		case STATE_HOSTING:             break;
		case STATE_CONNECTING_TO_HOST:  return DPNERR_CONNECTING;
		case STATE_CONNECTING_TO_PEERS: return DPNERR_CONNECTING;
		case STATE_CONNECT_FAILED:      return DPNERR_CONNECTING;
		case STATE_CONNECTED:           break;
		case STATE_CLOSING:             return DPNERR_CONNECTIONLOST;
		case STATE_TERMINATED:          return DPNERR_CONNECTIONLOST;
	}
	
	Group *group = get_group_by_id(idGroup);
	if(group == NULL)
	{
		return DPNERR_INVALIDGROUP;
	}
	
	if(destroyed_groups.find(idGroup) != destroyed_groups.end())
	{
		/* The group is there, but we're already destroying it. */
		return DPNERR_INVALIDGROUP;
	}
	
	DPNHANDLE async_handle;
	if(!(dwFlags & DPNDESTROYGROUP_SYNC))
	{
		async_handle = handle_alloc.new_dgroup();
		
		if(phAsyncHandle != NULL)
		{
			*phAsyncHandle = async_handle;
		}
	}
	
	destroyed_groups.insert(idGroup);
	
	/* Send DPLITE_MSGID_GROUP_DESTROY to each PS_CONNECTED peer. */
	
	PacketSerialiser group_destroy(DPLITE_MSGID_GROUP_DESTROY);
	group_destroy.append_dword(idGroup);
	
	int *pending = new int(1);
	std::condition_variable cv;
	
	auto complete =
		[this, pending, dwFlags, &cv, async_handle, pvAsyncContext]
		(std::unique_lock<std::mutex> &l)
	{
		if(--(*pending) == 0)
		{
			if(dwFlags & DPNDESTROYGROUP_SYNC)
			{
				cv.notify_one();
			}
			else{
				DPNMSG_ASYNC_OP_COMPLETE oc;
				memset(&oc, 0, sizeof(oc));
				
				oc.dwSize        = sizeof(oc);
				oc.hAsyncOp      = async_handle;
				oc.pvUserContext = pvAsyncContext;
				oc.hResultCode   = S_OK;
				
				l.unlock();
				message_handler(message_handler_ctx, DPN_MSGID_ASYNC_OP_COMPLETE, &oc);
				l.lock();
				
				delete pending;
			}
		}
	};
	
	for(auto p = peers.begin(); p != peers.end(); ++p)
	{
		Peer *peer = p->second;
		
		if(peer->state == Peer::PS_CONNECTED)
		{
			++(*pending);
			
			peer->sq.send(SendQueue::SEND_PRI_HIGH, group_destroy, NULL,
				[complete]
				(std::unique_lock<std::mutex> &l, HRESULT result)
				{
					if(result != S_OK)
					{
						log_printf("Failed to send DPLITE_MSGID_GROUP_DESTROY, session may be out of sync!");
					}
					
					complete(l);
				});
		}
	}
	
	/* Raise local DPNMSG_DESTROY_GROUP and then destroy the group.
	 * TODO: Do this in a properly managed worker thread.
	*/
	
	std::thread t([this, idGroup, complete]()
	{
		std::unique_lock<std::mutex> l(lock);
		
		Group *group = get_group_by_id(idGroup);
		if(group != NULL)
		{
			DPNMSG_DESTROY_GROUP dg;
			memset(&dg, 0, sizeof(dg));
			
			dg.dwSize         = sizeof(dg);
			dg.dpnidGroup     = idGroup;
			dg.pvGroupContext = group->ctx;
			dg.dwReason       = DPNDESTROYGROUPREASON_NORMAL;
			
			l.unlock();
			message_handler(message_handler_ctx, DPN_MSGID_DESTROY_GROUP, &dg);
			l.lock();
			
			groups.erase(idGroup);
		}
		
		complete(l);
	});
	
	t.detach();
	
	if(dwFlags & DPNDESTROYGROUP_SYNC)
	{
		cv.wait(l, [&pending]() { return (*pending == 0); });
		delete pending;
		
		return S_OK;
	}
	else{
		return DPNSUCCESS_PENDING;
	}
}

HRESULT DirectPlay8Peer::AddPlayerToGroup(CONST DPNID idGroup, CONST DPNID idClient, PVOID CONST pvAsyncContext, DPNHANDLE* CONST phAsyncHandle, CONST DWORD dwFlags)
{
	std::unique_lock<std::mutex> l(lock);
	
	switch(state)
	{
		case STATE_NEW:                 return DPNERR_UNINITIALIZED;
		case STATE_INITIALISED:         return DPNERR_NOCONNECTION;
		case STATE_HOSTING:             break;
		case STATE_CONNECTING_TO_HOST:  return DPNERR_CONNECTING;
		case STATE_CONNECTING_TO_PEERS: return DPNERR_CONNECTING;
		case STATE_CONNECT_FAILED:      return DPNERR_CONNECTING;
		case STATE_CONNECTED:           break;
		case STATE_CLOSING:             return DPNERR_CONNECTIONLOST;
		case STATE_TERMINATED:          return DPNERR_CONNECTIONLOST;
	}
	
	Group *group = get_group_by_id(idGroup);
	if(group == NULL)
	{
		return DPNERR_INVALIDGROUP;
	}
	
	if(destroyed_groups.find(idGroup) != destroyed_groups.end())
	{
		/* The group is there, but it is being destroyed. */
		return DPNERR_INVALIDGROUP;
	}
	
	if(group->player_ids.find(idClient) != group->player_ids.end())
	{
		/* Already in group. */
		return DPNERR_PLAYERALREADYINGROUP;
	}
	
	DPNHANDLE async_handle;
	if(!(dwFlags & DPNADDPLAYERTOGROUP_SYNC))
	{
		async_handle = handle_alloc.new_apgroup();
		
		if(phAsyncHandle != NULL)
		{
			*phAsyncHandle = async_handle;
		}
	}
	
	int *pending = new int(1);
	std::condition_variable cv;
	HRESULT s_result = S_OK;
	
	auto complete =
		[this, pending, dwFlags, &cv, &s_result, async_handle, pvAsyncContext]
		(std::unique_lock<std::mutex> &l, HRESULT result)
	{
		if(--(*pending) == 0)
		{
			if(dwFlags & DPNADDPLAYERTOGROUP_SYNC)
			{
				s_result = result;
				cv.notify_one();
			}
			else{
				DPNMSG_ASYNC_OP_COMPLETE oc;
				memset(&oc, 0, sizeof(oc));
				
				oc.dwSize        = sizeof(oc);
				oc.hAsyncOp      = async_handle;
				oc.pvUserContext = pvAsyncContext;
				oc.hResultCode   = result;
				
				l.unlock();
				message_handler(message_handler_ctx, DPN_MSGID_ASYNC_OP_COMPLETE, &oc);
				l.lock();
				
				delete pending;
			}
		}
	};
	
	if(idClient == local_player_id)
	{
		/* Adding ourself to the group. Notify everyone else in the session. */
		
		PacketSerialiser group_joined(DPLITE_MSGID_GROUP_JOINED);
		group_joined.append_dword(idGroup);
		group_joined.append_wstring(group->name);
		group_joined.append_data(group->data.data(), group->data.size());
		
		for(auto p = peers.begin(); p != peers.end(); ++p)
		{
			Peer *peer = p->second;
			
			if(peer->state == Peer::PS_CONNECTED)
			{
				++(*pending);
				peer->sq.send(SendQueue::SEND_PRI_HIGH, group_joined, NULL,
					[complete]
					(std::unique_lock<std::mutex> &l, HRESULT result)
					{
						if(result != S_OK)
						{
							log_printf("Failed to send DPLITE_MSGID_GROUP_JOINED, session may be out of sync!");
						}
						
						complete(l, S_OK);
					});
			}
		}
		
		/* And actually update the group and tell the application. */
		
		group->player_ids.insert(local_player_id);
		
		void *group_ctx = group->ctx;
		std::thread t([this, idGroup, group_ctx, complete]()
		{
			DPNMSG_ADD_PLAYER_TO_GROUP ap;
			memset(&ap, 0, sizeof(ap));
			
			ap.dwSize          = sizeof(ap);
			ap.dpnidGroup      = idGroup;
			ap.pvGroupContext  = group_ctx;
			ap.dpnidPlayer     = local_player_id;
			ap.pvPlayerContext = local_player_ctx;
			
			message_handler(message_handler_ctx, DPN_MSGID_ADD_PLAYER_TO_GROUP, &ap);
			
			std::unique_lock<std::mutex> l(lock);
			complete(l, S_OK);
		});
		
		t.detach();
	}
	else{
		Peer *peer = get_peer_by_player_id(idClient);
		if(peer == NULL)
		{
			return DPNERR_INVALIDPLAYER;
		}
		
		/* We know this exists because get_peer_by_peer_id() just succeeded. */
		unsigned int peer_id = player_to_peer_id[idClient];
		
		/* Adding a remote peer to a group, all we do is send it a
		 * request to put itself into the group.
		*/
		
		DWORD ack_id = peer->alloc_ack_id();
		
		PacketSerialiser group_join(DPLITE_MSGID_GROUP_JOIN);
		group_join.append_dword(idGroup);
		group_join.append_dword(ack_id);
		group_join.append_wstring(group->name);
		group_join.append_data(group->data.data(), group->data.size());
		
		peer->sq.send(SendQueue::SEND_PRI_HIGH, group_join, NULL,
			[this, peer_id, ack_id, complete]
			(std::unique_lock<std::mutex> &l, HRESULT result)
			{
				if(result == S_OK)
				{
					/* We'll only get called with S_OK immediately after a
					 * send completes, so the peer must exist at this point.
					*/
					Peer *peer = get_peer_by_peer_id(peer_id);
					assert(peer != NULL);
					
					peer->register_ack(ack_id, [complete](std::unique_lock<std::mutex> &l, HRESULT result)
					{
						complete(l, result);
					});
				}
				else{
					complete(l, result);
				}
			});
	}
	
	if(dwFlags & DPNADDPLAYERTOGROUP_SYNC)
	{
		cv.wait(l, [&pending]() { return (*pending == 0); });
		delete pending;
		
		return s_result;
	}
	else{
		return DPNSUCCESS_PENDING;
	}
}

HRESULT DirectPlay8Peer::RemovePlayerFromGroup(CONST DPNID idGroup, CONST DPNID idClient, PVOID CONST pvAsyncContext, DPNHANDLE* CONST phAsyncHandle, CONST DWORD dwFlags)
{
	std::unique_lock<std::mutex> l(lock);
	
	switch(state)
	{
		case STATE_NEW:                 return DPNERR_UNINITIALIZED;
		case STATE_INITIALISED:         return DPNERR_NOCONNECTION;
		case STATE_HOSTING:             break;
		case STATE_CONNECTING_TO_HOST:  return DPNERR_CONNECTING;
		case STATE_CONNECTING_TO_PEERS: return DPNERR_CONNECTING;
		case STATE_CONNECT_FAILED:      return DPNERR_CONNECTING;
		case STATE_CONNECTED:           break;
		case STATE_CLOSING:             return DPNERR_CONNECTIONLOST;
		case STATE_TERMINATED:          return DPNERR_CONNECTIONLOST;
	}
	
	Group *group = get_group_by_id(idGroup);
	if(group == NULL)
	{
		return DPNERR_INVALIDGROUP;
	}
	
	if(group->player_ids.find(idClient) == group->player_ids.end())
	{
		/* Not in group. */
		return DPNERR_PLAYERNOTINGROUP;
	}
	
	DPNHANDLE async_handle;
	if(!(dwFlags & DPNREMOVEPLAYERFROMGROUP_SYNC))
	{
		async_handle = handle_alloc.new_rpgroup();
		
		if(phAsyncHandle != NULL)
		{
			*phAsyncHandle = async_handle;
		}
	}
	
	int *pending = new int(1);
	std::condition_variable cv;
	HRESULT s_result = S_OK;
	
	auto complete =
		[this, pending, dwFlags, &cv, &s_result, async_handle, pvAsyncContext]
		(std::unique_lock<std::mutex> &l, HRESULT result)
	{
		if(--(*pending) == 0)
		{
			if(dwFlags & DPNREMOVEPLAYERFROMGROUP_SYNC)
			{
				s_result = result;
				cv.notify_one();
			}
			else{
				DPNMSG_ASYNC_OP_COMPLETE oc;
				memset(&oc, 0, sizeof(oc));
				
				oc.dwSize        = sizeof(oc);
				oc.hAsyncOp      = async_handle;
				oc.pvUserContext = pvAsyncContext;
				oc.hResultCode   = result;
				
				l.unlock();
				message_handler(message_handler_ctx, DPN_MSGID_ASYNC_OP_COMPLETE, &oc);
				l.lock();
				
				delete pending;
			}
		}
	};
	
	if(idClient == local_player_id)
	{
		/* Removing ourself from the group. Notify everyone else in the session. */
		
		PacketSerialiser group_left(DPLITE_MSGID_GROUP_LEFT);
		group_left.append_dword(idGroup);
		
		for(auto p = peers.begin(); p != peers.end(); ++p)
		{
			Peer *peer = p->second;
			
			if(peer->state == Peer::PS_CONNECTED)
			{
				++(*pending);
				peer->sq.send(SendQueue::SEND_PRI_HIGH, group_left, NULL,
					[complete]
					(std::unique_lock<std::mutex> &l, HRESULT result)
					{
						if(result != S_OK)
						{
							log_printf("Failed to send DPLITE_MSGID_GROUP_LEFT, session may be out of sync!");
						}
						
						complete(l, S_OK);
					});
			}
		}
		
		/* And actually update the group and tell the application. */
		
		group->player_ids.erase(local_player_id);
		
		void *group_ctx = group->ctx;
		std::thread t([this, idGroup, group_ctx, complete]()
		{
			DPNMSG_REMOVE_PLAYER_FROM_GROUP rp;
			memset(&rp, 0, sizeof(rp));
			
			rp.dwSize          = sizeof(rp);
			rp.dpnidGroup      = idGroup;
			rp.pvGroupContext  = group_ctx;
			rp.dpnidPlayer     = local_player_id;
			rp.pvPlayerContext = local_player_ctx;
			
			message_handler(message_handler_ctx, DPN_MSGID_REMOVE_PLAYER_FROM_GROUP, &rp);
			
			std::unique_lock<std::mutex> l(lock);
			complete(l, S_OK);
		});
		
		t.detach();
	}
	else{
		Peer *peer = get_peer_by_player_id(idClient);
		if(peer == NULL)
		{
			return DPNERR_INVALIDPLAYER;
		}
		
		/* We know this exists because get_peer_by_peer_id() just succeeded. */
		unsigned int peer_id = player_to_peer_id[idClient];
		
		/* Adding a remote peer to a group, all we do is send it a
		 * request to put itself into the group.
		*/
		
		DWORD ack_id = peer->alloc_ack_id();
		
		PacketSerialiser group_leave(DPLITE_MSGID_GROUP_LEAVE);
		group_leave.append_dword(idGroup);
		group_leave.append_dword(ack_id);
		
		peer->sq.send(SendQueue::SEND_PRI_HIGH, group_leave, NULL,
			[this, peer_id, ack_id, complete]
			(std::unique_lock<std::mutex> &l, HRESULT result)
			{
				if(result == S_OK)
				{
					/* We'll only get called with S_OK immediately after a
					 * send completes, so the peer must exist at this point.
					*/
					Peer *peer = get_peer_by_peer_id(peer_id);
					assert(peer != NULL);
					
					peer->register_ack(ack_id, [complete](std::unique_lock<std::mutex> &l, HRESULT result)
					{
						complete(l, result);
					});
				}
				else{
					complete(l, result);
				}
			});
	}
	
	if(dwFlags & DPNREMOVEPLAYERFROMGROUP_SYNC)
	{
		cv.wait(l, [&pending]() { return (*pending == 0); });
		delete pending;
		
		return s_result;
	}
	else{
		return DPNSUCCESS_PENDING;
	}
}

HRESULT DirectPlay8Peer::SetGroupInfo(CONST DPNID dpnid, DPN_GROUP_INFO* CONST pdpnGroupInfo,PVOID CONST pvAsyncContext, DPNHANDLE* CONST phAsyncHandle, CONST DWORD dwFlags)
{
	UNIMPLEMENTED("DirectPlay8Peer::SetGroupInfo");
}

HRESULT DirectPlay8Peer::GetGroupInfo(CONST DPNID dpnid, DPN_GROUP_INFO* CONST pdpnGroupInfo, DWORD* CONST pdwSize, CONST DWORD dwFlags)
{
	std::unique_lock<std::mutex> l(lock);
	
	switch(state)
	{
		case STATE_NEW:                 return DPNERR_UNINITIALIZED;
		case STATE_INITIALISED:         return DPNERR_NOCONNECTION;
		case STATE_HOSTING:             break;
		case STATE_CONNECTING_TO_HOST:  break;
		case STATE_CONNECTING_TO_PEERS: break;
		case STATE_CONNECT_FAILED:      break;
		case STATE_CONNECTED:           break;
		case STATE_CLOSING:             break;
		case STATE_TERMINATED:          break;
	}
	
	Group *group = get_group_by_id(dpnid);
	if(group == NULL)
	{
		return DPNERR_INVALIDGROUP;
	}
	
	size_t min_buffer_size = sizeof(DPN_GROUP_INFO);
	
	if(!group->name.empty())
	{
		min_buffer_size += (group->name.length() + 1) * sizeof(wchar_t);
	}
	
	if(!group->data.empty())
	{
		min_buffer_size += group->data.size();
	}
	
	if(*pdwSize < min_buffer_size)
	{
		*pdwSize = min_buffer_size;
		return DPNERR_BUFFERTOOSMALL;
	}
	
	if(pdpnGroupInfo->dwSize != sizeof(DPN_GROUP_INFO))
	{
		return DPNERR_INVALIDPARAM;
	}
	
	unsigned char *extra_at = (unsigned char*)(pdpnGroupInfo + 1);
	
	pdpnGroupInfo->dwInfoFlags = DPNINFO_NAME | DPNINFO_DATA;
	
	if(!group->name.empty())
	{
		pdpnGroupInfo->pwszName = wcscpy((wchar_t*)(extra_at), group->name.c_str());
		extra_at += (group->name.length() + 1) * sizeof(wchar_t);
	}
	else{
		pdpnGroupInfo->pwszName = NULL;
	}
	
	if(!group->data.empty())
	{
		pdpnGroupInfo->pvData = memcpy(extra_at, group->data.data(), group->data.size());
		pdpnGroupInfo->dwDataSize = group->data.size();
		extra_at += group->data.size();
	}
	else{
		pdpnGroupInfo->pvData = NULL;
		pdpnGroupInfo->dwDataSize = 0;
	}
	
	pdpnGroupInfo->dwGroupFlags = 0;
	
	return S_OK;
}

HRESULT DirectPlay8Peer::EnumPlayersAndGroups(DPNID* CONST prgdpnid, DWORD* CONST pcdpnid, CONST DWORD dwFlags)
{
	std::unique_lock<std::mutex> l(lock);
	
	switch(state)
	{
		case STATE_NEW:                 return DPNERR_UNINITIALIZED;
		case STATE_INITIALISED:         return DPNERR_NOCONNECTION;
		case STATE_HOSTING:             break;
		case STATE_CONNECTING_TO_HOST:  return DPNERR_CONNECTING;
		case STATE_CONNECTING_TO_PEERS: return DPNERR_CONNECTING;
		case STATE_CONNECT_FAILED:      return DPNERR_CONNECTING;
		case STATE_CONNECTED:           break;
		case STATE_CLOSING:             return DPNERR_CONNECTIONLOST;
		case STATE_TERMINATED:          return DPNERR_CONNECTIONLOST;
	}
	
	DWORD num_results = 0;
	
	if(dwFlags & DPNENUM_PLAYERS)
	{
		++num_results; /* For local peer. */
		
		for(auto p = peers.begin(); p != peers.end(); ++p)
		{
			Peer *peer = p->second;
			
			if(peer->state == Peer::PS_CONNECTED)
			{
				++num_results;
			}
		}
	}
	
	if(dwFlags & DPNENUM_GROUPS)
	{
		num_results += groups.size();
	}
	
	if(*pcdpnid < num_results)
	{
		*pcdpnid = num_results;
		return DPNERR_BUFFERTOOSMALL;
	}
	
	DWORD next_idx = 0;
	
	if(dwFlags & DPNENUM_PLAYERS)
	{
		prgdpnid[next_idx++] = local_player_id;
		
		for(auto p = peers.begin(); p != peers.end(); ++p)
		{
			Peer *peer = p->second;
			
			if(peer->state == Peer::PS_CONNECTED)
			{
				prgdpnid[next_idx++] = peer->player_id;
			}
		}
	}
	
	if(dwFlags & DPNENUM_GROUPS)
	{
		for(auto g = groups.begin(); g != groups.end(); ++g)
		{
			DPNID group_id = g->first;
			prgdpnid[next_idx++] = group_id;
		}
	}
	
	assert(next_idx == num_results);
	*pcdpnid = next_idx;
	
	return S_OK;
}

HRESULT DirectPlay8Peer::EnumGroupMembers(CONST DPNID dpnid, DPNID* CONST prgdpnid, DWORD* CONST pcdpnid, CONST DWORD dwFlags)
{
	std::unique_lock<std::mutex> l(lock);
	
	switch(state)
	{
		case STATE_NEW:                 return DPNERR_UNINITIALIZED;
		case STATE_INITIALISED:         return DPNERR_NOCONNECTION;
		case STATE_HOSTING:             break;
		case STATE_CONNECTING_TO_HOST:  break;
		case STATE_CONNECTING_TO_PEERS: break;
		case STATE_CONNECT_FAILED:      return DPNERR_CONNECTIONLOST;
		case STATE_CONNECTED:           break;
		case STATE_CLOSING:             return DPNERR_CONNECTIONLOST;
		case STATE_TERMINATED:          return DPNERR_CONNECTIONLOST;
	}
	
	auto g = groups.find(dpnid);
	if(g == groups.end())
	{
		return DPNERR_INVALIDGROUP;
	}
	
	Group &group = g->second;
	
	if(*pcdpnid < group.player_ids.size())
	{
		*pcdpnid = group.player_ids.size();
		return DPNERR_BUFFERTOOSMALL;
	}
	
	DWORD next_idx = 0;
	
	for(auto p = group.player_ids.begin(); p != group.player_ids.end(); ++p)
	{
		prgdpnid[next_idx++] = *p;
	}
	
	*pcdpnid = next_idx;
	
	return S_OK;
}

HRESULT DirectPlay8Peer::SetPeerInfo(CONST DPN_PLAYER_INFO* CONST pdpnPlayerInfo, PVOID CONST pvAsyncContext, DPNHANDLE* CONST phAsyncHandle, CONST DWORD dwFlags)
{
	std::unique_lock<std::mutex> l(lock);
	
	if(pdpnPlayerInfo->dwSize != sizeof(DPN_PLAYER_INFO))
	{
		return DPNERR_INVALIDPARAM;
	}
	
	if(pdpnPlayerInfo->dwInfoFlags & DPNINFO_NAME)
	{
		if(pdpnPlayerInfo->pwszName == NULL)
		{
			local_player_name.clear();
		}
		else{
			local_player_name = pdpnPlayerInfo->pwszName;
		}
	}
	
	if(pdpnPlayerInfo->dwInfoFlags & DPNINFO_DATA)
	{
		local_player_data.clear();
		
		if(pdpnPlayerInfo->pvData != NULL && pdpnPlayerInfo->dwDataSize > 0)
		{
			local_player_data.reserve(pdpnPlayerInfo->dwDataSize);
			local_player_data.insert(
				local_player_data.end(),
				(const unsigned char*)(pdpnPlayerInfo->pvData),
				(const unsigned char*)(pdpnPlayerInfo->pvData) + pdpnPlayerInfo->dwDataSize);
		}
	}
	
	/* If called before creating or joining a session, SetPeerInfo() returns S_OK and
	 * doesn't fire any local messages.
	*/
	if(state != STATE_HOSTING && state != STATE_CONNECTED)
	{
		return S_OK;
	}
	
	std::function<void(std::unique_lock<std::mutex>&, HRESULT)> op_finished_cb;
	
	unsigned int sync_pending = 1;
	unsigned int *pending = &sync_pending;
	
	std::condition_variable sync_cv;
	HRESULT sync_result = S_OK;
	
	DPNHANDLE async_handle = handle_alloc.new_pinfo();
	HRESULT *async_result = NULL;
	
	if(dwFlags & DPNSETPEERINFO_SYNC)
	{
		op_finished_cb = [this, pending, &sync_result, &sync_cv](std::unique_lock<std::mutex> &l, HRESULT result)
		{
			if(result != S_OK && sync_result == S_OK)
			{
				/* Error code from the first failure wins. */
				sync_result = result;
			}
			
			if(--(*pending) == 0)
			{
				sync_cv.notify_one();
			}
		};
	}
	else{
		if(phAsyncHandle != NULL)
		{
			*phAsyncHandle = async_handle;
		}
		
		pending = new unsigned int(1);
		async_result = new HRESULT(S_OK);
		
		op_finished_cb = [this, pending, async_result, async_handle, pvAsyncContext](std::unique_lock<std::mutex> &l, HRESULT result)
		{
			if(result != S_OK && *async_result == S_OK)
			{
				/* Error code from the first failure wins. */
				*async_result = result;
			}
			
			if(--(*pending) == 0)
			{
				DPNMSG_ASYNC_OP_COMPLETE oc;
				memset(&oc, 0, sizeof(oc));
				
				oc.dwSize        = sizeof(oc);
				oc.hAsyncOp      = async_handle;
				oc.pvUserContext = pvAsyncContext;
				oc.hResultCode   = *async_result;
				
				delete async_result;
				delete pending;
				
				l.unlock();
				message_handler(message_handler_ctx, DPN_MSGID_ASYNC_OP_COMPLETE, &oc);
				l.lock();
			}
		};
	}
	
	for(auto pi = peers.begin(); pi != peers.end(); ++pi)
	{
		unsigned int peer_id = pi->first;
		Peer *peer = pi->second;
		
		if(peer->state != Peer::PS_CONNECTED)
		{
			continue;
		}
		
		DWORD ack_id = peer->alloc_ack_id();
		
		PacketSerialiser playerinfo(DPLITE_MSGID_PLAYERINFO);
		
		playerinfo.append_dword(local_player_id);
		playerinfo.append_wstring(local_player_name);
		playerinfo.append_data(local_player_data.data(), local_player_data.size());
		playerinfo.append_dword(ack_id);
		
		pi->second->sq.send(SendQueue::SEND_PRI_MEDIUM, playerinfo, NULL,
			[this, op_finished_cb, peer_id, ack_id]
			(std::unique_lock<std::mutex> &l, HRESULT result)
			{
				if(result == S_OK)
				{
					/* DPLITE_MSGID_PLAYERINFO has been sent, register the op
					 * to handle the response.
					*/
					Peer *peer = get_peer_by_peer_id(peer_id);
					assert(peer != NULL);
					
					peer->register_ack(ack_id, [op_finished_cb](std::unique_lock<std::mutex> &l, HRESULT result)
					{
						op_finished_cb(l, result);
					});
				}
				else{
					op_finished_cb(l, result);
				}
			});
		
		++(*pending);
	}
	
	{
		/* Notify the local instance it just changed its own peer info... which the
		 * official DirectX does for some reason.
		*/
		
		DPNMSG_PEER_INFO pi;
		memset(&pi, 0, sizeof(pi));
		
		pi.dwSize          = sizeof(DPNMSG_PEER_INFO);
		pi.dpnidPeer       = local_player_id;
		pi.pvPlayerContext = local_player_ctx;
		
		l.unlock();
		message_handler(message_handler_ctx, DPN_MSGID_PEER_INFO, &pi);
		l.lock();
		
		op_finished_cb(l, S_OK);
	}
	
	if(dwFlags & DPNSETPEERINFO_SYNC)
	{
		sync_cv.wait(l, [pending]() { return (*pending == 0); });
		return sync_result;
	}
	else{
		return DPNSUCCESS_PENDING;
	}
}

HRESULT DirectPlay8Peer::GetPeerInfo(CONST DPNID dpnid, DPN_PLAYER_INFO* CONST pdpnPlayerInfo, DWORD* CONST pdwSize, CONST DWORD dwFlags)
{
	std::unique_lock<std::mutex> l(lock);
	
	std::wstring *name;
	std::vector<unsigned char> *data;
	
	if(dpnid == local_player_id)
	{
		name = &local_player_name;
		data = &local_player_data;
	}
	else{
		Peer *peer = get_peer_by_player_id(dpnid);
		if(peer == NULL)
		{
			return DPNERR_INVALIDPLAYER;
		}
		
		name = &(peer->player_name);
		data = &(peer->player_data);
	}
	
	size_t needed_buf_size = sizeof(DPN_PLAYER_INFO) + data->size() + ((name->length() + !name->empty()) * sizeof(wchar_t));
	
	if(pdpnPlayerInfo != NULL && *pdwSize >= sizeof(DPN_PLAYER_INFO) && pdpnPlayerInfo->dwSize != sizeof(DPN_PLAYER_INFO))
	{
		return DPNERR_INVALIDFLAGS;
	}
	
	if(pdpnPlayerInfo == NULL || *pdwSize < needed_buf_size)
	{
		*pdwSize = needed_buf_size;
		return DPNERR_BUFFERTOOSMALL;
	}
	
	unsigned char *data_out_ptr = (unsigned char*)(pdpnPlayerInfo + 1);
	
	pdpnPlayerInfo->dwInfoFlags   = DPNINFO_NAME | DPNINFO_DATA;
	pdpnPlayerInfo->dwPlayerFlags = 0;
	
	if(!name->empty())
	{
		size_t ws_size = (name->length() + 1) * sizeof(wchar_t);
		memcpy(data_out_ptr, name->c_str(), ws_size);
		
		pdpnPlayerInfo->pwszName = (wchar_t*)(data_out_ptr);
		
		data_out_ptr += ws_size;
	}
	else{
		pdpnPlayerInfo->pwszName = NULL;
	}
	
	if(!data->empty())
	{
		memcpy(data_out_ptr, data->data(), data->size());
		
		pdpnPlayerInfo->pvData     = data_out_ptr;
		pdpnPlayerInfo->dwDataSize = data->size();
		
		data_out_ptr += data->size();
	}
	else{
		pdpnPlayerInfo->pvData     = NULL;
		pdpnPlayerInfo->dwDataSize = 0;
	}
	
	if(dpnid == local_player_id)
	{
		pdpnPlayerInfo->dwPlayerFlags |= DPNPLAYER_LOCAL;
	}
	
	if(dpnid == host_player_id)
	{
		pdpnPlayerInfo->dwPlayerFlags |= DPNPLAYER_HOST;
	}
	
	return S_OK;
}

HRESULT DirectPlay8Peer::GetPeerAddress(CONST DPNID dpnid, IDirectPlay8Address** CONST pAddress, CONST DWORD dwFlags)
{
	std::unique_lock<std::mutex> l(lock);
	
	Peer *peer = get_peer_by_player_id(dpnid);
	if(peer == NULL)
	{
		return DPNERR_INVALIDPLAYER;
	}
	
	struct sockaddr_in sa;
	sa.sin_family      = AF_INET;
	sa.sin_addr.s_addr = peer->ip;
	sa.sin_port        = htons(peer->port);
	
	*pAddress = DirectPlay8Address::create_host_address(global_refcount, service_provider, (struct sockaddr*)(&sa));
	
	return S_OK;
}

HRESULT DirectPlay8Peer::GetLocalHostAddresses(IDirectPlay8Address** CONST prgpAddress, DWORD* CONST pcAddress, CONST DWORD dwFlags)
{
	std::unique_lock<std::mutex> l(lock);
	
	switch(state)
	{
		case STATE_NEW:                 return DPNERR_UNINITIALIZED;
		case STATE_INITIALISED:         return DPNERR_NOCONNECTION;
		case STATE_HOSTING:             break;
		case STATE_CONNECTING_TO_HOST:  return DPNERR_NOCONNECTION;
		case STATE_CONNECTING_TO_PEERS: return DPNERR_NOCONNECTION;
		case STATE_CONNECT_FAILED:      return DPNERR_NOCONNECTION;
		case STATE_CONNECTED:           return DPNERR_NOTHOST;
		case STATE_CLOSING:             return DPNERR_NOCONNECTION;
		case STATE_TERMINATED:          return DPNERR_NOCONNECTION;
	}
	
	if(dwFlags & DPNGETLOCALHOSTADDRESSES_COMBINED)
	{
		/* TODO: Implement DPNGETLOCALHOSTADDRESSES_COMBINED */
		return E_NOTIMPL;
	}
	
	/* We don't support binding to specific interfaces yet, so we just return all the IPv4
	 * addresses assigned to the system.
	*/
	
	std::list<SystemNetworkInterface> interfaces = get_network_interfaces();
	std::list<struct sockaddr_in> local_addrs;
	
	for(auto i = interfaces.begin(); i != interfaces.end(); ++i)
	{
		for(auto a = i->unicast_addrs.begin(); a != i->unicast_addrs.end(); ++a)
		{
			if(a->ss_family == AF_INET)
			{
				struct sockaddr_in sa_v4 = *(struct sockaddr_in*)(&(*a));
				sa_v4.sin_port = htons(local_port);
				
				local_addrs.push_back(sa_v4);
			}
		}
	}
	
	if(*pcAddress < local_addrs.size())
	{
		*pcAddress = local_addrs.size();
		return DPNERR_BUFFERTOOSMALL;
	}
	
	IDirectPlay8Address **dest = prgpAddress;
	for(auto a = local_addrs.begin(); a != local_addrs.end(); ++a)
	{
		*dest = DirectPlay8Address::create_host_address(global_refcount, service_provider, (struct sockaddr*)(&(*a)));
		++dest;
	}
	
	*pcAddress = local_addrs.size();
	
	return S_OK;
}

HRESULT DirectPlay8Peer::Close(CONST DWORD dwFlags)
{
	std::unique_lock<std::mutex> l(lock);
	
	bool was_connected = false;
	bool was_hosting   = false;
	
	switch(state)
	{
		case STATE_NEW:                 return DPNERR_UNINITIALIZED;
		case STATE_INITIALISED:         break;
		case STATE_HOSTING:             was_hosting = true; break;
		case STATE_CONNECTING_TO_HOST:  break;
		case STATE_CONNECTING_TO_PEERS: break;
		case STATE_CONNECT_FAILED:      break;
		case STATE_CONNECTED:           was_connected = true; break;
		case STATE_CLOSING:             return DPNERR_ALREADYCLOSING;
		case STATE_TERMINATED:          break;
	}
	
	/* Signal all asynchronous EnumHosts() calls to complete. */
	for(auto ei = async_host_enums.begin(); ei != async_host_enums.end(); ++ei)
	{
		ei->second.cancel();
	}
	
	close_main_sockets();
	
	if(state == STATE_CONNECTING_TO_HOST || state == STATE_CONNECTING_TO_PEERS)
	{
		/* connect_fail() will change the state to STATE_CONNECT_FAILED, then finally to
		 * STATE_INITIALISED before it returns, this doesn't matter as we return it to
		 * STATE_CLOSING before the lock is released again.
		*/
		connect_fail(l, DPNERR_NOCONNECTION, NULL, 0);
	}
	
	state = STATE_CLOSING;
	
	/* When Close() is called as a host, the DPNMSG_DESTROY_PLAYER for the local player comes
	 * before the ones for the peers, when called as a non-host, it comes after.
	 *
	 * Do not question the ways of DirectX.
	*/
	
	if(was_hosting)
	{
		/* Raise a DPNMSG_DESTROY_PLAYER for ourself. */
		dispatch_destroy_player(l, local_player_id, local_player_ctx, DPNDESTROYPLAYERREASON_NORMAL);
	}
	
	if(dwFlags & DPNCLOSE_IMMEDIATE)
	{
		peer_destroy_all(l, DPNERR_USERCANCEL, DPNDESTROYPLAYERREASON_NORMAL);
	}
	else{
		/* Initiate graceful shutdown of all peers. */
		peer_shutdown_all(l, DPNERR_USERCANCEL, DPNDESTROYPLAYERREASON_NORMAL);
		
		/* Wait for remaining peers to finish disconnecting. */
		peer_destroyed.wait(l, [this]() { return peers.empty(); });
	}
	
	if(was_connected)
	{
		/* Raise a DPNMSG_DESTROY_PLAYER for ourself. */
		dispatch_destroy_player(l, local_player_id, local_player_ctx, DPNDESTROYPLAYERREASON_NORMAL);
	}
	
	group_destroy_all(l, DPNDESTROYGROUPREASON_NORMAL);
	
	/* Wait for outstanding EnumHosts() calls. */
	host_enum_completed.wait(l, [this]() { return async_host_enums.empty() && sync_host_enums.empty(); });
	
	/* We need to release the lock while the worker_pool destructor runs so that any worker
	 * threads waiting for it can finish. No other thread should mess with it while we are in
	 * STATE_CLOSING and we have no open sockets.
	*/
	l.unlock();
	delete worker_pool;
	l.lock();
	worker_pool = NULL;
	
	destroyed_groups.clear();
	
	WSACleanup();
	
	state = STATE_NEW;
	
	return S_OK;
}

HRESULT DirectPlay8Peer::EnumHosts(PDPN_APPLICATION_DESC CONST pApplicationDesc, IDirectPlay8Address* CONST pAddrHost, IDirectPlay8Address* CONST pDeviceInfo,PVOID CONST pUserEnumData, CONST DWORD dwUserEnumDataSize, CONST DWORD dwEnumCount, CONST DWORD dwRetryInterval, CONST DWORD dwTimeOut,PVOID CONST pvUserContext, DPNHANDLE* CONST pAsyncHandle, CONST DWORD dwFlags)
{
	std::unique_lock<std::mutex> l(lock);
	
	if(state == STATE_NEW)
	{
		return DPNERR_UNINITIALIZED;
	}
	
	try {
		if(dwFlags & DPNENUMHOSTS_SYNC)
		{
			HRESULT result;
			
			sync_host_enums.emplace_front(
				global_refcount,
				message_handler, message_handler_ctx,
				pApplicationDesc, pAddrHost, pDeviceInfo, pUserEnumData, dwUserEnumDataSize,
				dwEnumCount, dwRetryInterval, dwTimeOut, pvUserContext,
				
				[&result](HRESULT r)
				{
					result = r;
				});
			
			std::list<HostEnumerator>::iterator he = sync_host_enums.begin();
			
			l.unlock();
			he->wait();
			l.lock();
			
			sync_host_enums.erase(he);
			host_enum_completed.notify_all();
			
			return result;
		}
		else{
			DPNHANDLE handle = handle_alloc.new_enum();
			
			*pAsyncHandle = handle;
			
			async_host_enums.emplace(
				std::piecewise_construct,
				std::forward_as_tuple(handle),
				std::forward_as_tuple(
					global_refcount,
					message_handler, message_handler_ctx,
					pApplicationDesc, pAddrHost, pDeviceInfo, pUserEnumData, dwUserEnumDataSize,
					dwEnumCount, dwRetryInterval, dwTimeOut, pvUserContext,
					
					[this, handle, pvUserContext](HRESULT r)
					{
						DPNMSG_ASYNC_OP_COMPLETE oc;
						memset(&oc, 0, sizeof(oc));
						
						oc.dwSize        = sizeof(oc);
						oc.hAsyncOp      = handle;
						oc.pvUserContext = pvUserContext;
						oc.hResultCode   = r;
						
						message_handler(message_handler_ctx, DPN_MSGID_ASYNC_OP_COMPLETE, &oc);
						
						std::unique_lock<std::mutex> l(lock);
						async_host_enums.erase(handle);
						
						host_enum_completed.notify_all();
					}));
			
			return DPNSUCCESS_PENDING;
		}
	}
	catch(const COMAPIException &e)
	{
		return e.result();
	}
	catch(...)
	{
		return DPNERR_GENERIC;
	}
}

HRESULT DirectPlay8Peer::DestroyPeer(CONST DPNID dpnidClient, CONST void* CONST pvDestroyData, CONST DWORD dwDestroyDataSize, CONST DWORD dwFlags)
{
	std::unique_lock<std::mutex> l(lock);
	
	switch(state)
	{
		case STATE_NEW:                 return DPNERR_UNINITIALIZED;
		case STATE_INITIALISED:         return DPNERR_NOCONNECTION;
		case STATE_HOSTING:             break;
		case STATE_CONNECTING_TO_HOST:  return DPNERR_CONNECTING;
		case STATE_CONNECTING_TO_PEERS: return DPNERR_CONNECTING;
		case STATE_CONNECT_FAILED:      return DPNERR_CONNECTING;
		case STATE_CONNECTED:           return DPNERR_NOTHOST;
		case STATE_CLOSING:             return DPNERR_CONNECTIONLOST;
		case STATE_TERMINATED:          return DPNERR_HOSTTERMINATEDSESSION;
	}
	
	if(dpnidClient == local_player_id)
	{
		/* Can't destroy the local peer? */
		return DPNERR_INVALIDPARAM;
	}
	
	Peer *peer = get_peer_by_player_id(dpnidClient);
	if(peer == NULL)
	{
		return DPNERR_INVALIDPLAYER;
	}
	
	/* dpnidClient must be present in player_to_peer_id for the above to have succeeded. */
	unsigned int peer_id = player_to_peer_id[dpnidClient];
	
	PacketSerialiser destroy_peer_base(DPLITE_MSGID_DESTROY_PEER);
	destroy_peer_base.append_dword(peer->player_id);
	
	PacketSerialiser destroy_peer_full(DPLITE_MSGID_DESTROY_PEER);
	destroy_peer_full.append_dword(peer->player_id);
	destroy_peer_full.append_data(pvDestroyData, dwDestroyDataSize);
	
	/* Notify the peer we are destroying it and initiate the connection shutdown. */
	
	peer->sq.send(SendQueue::SEND_PRI_HIGH, destroy_peer_full, NULL, [](std::unique_lock<std::mutex> &l, HRESULT result) {});
	peer_shutdown(l, peer_id, DPNERR_HOSTTERMINATEDSESSION, DPNDESTROYPLAYERREASON_HOSTDESTROYEDPLAYER);
	
	/* Notify the other peers, in case the other peer is malfunctioning and doesn't remove
	 * itself from the session gracefully.
	*/
	
	for(auto p = peers.begin(); p != peers.end(); ++p)
	{
		Peer *o_peer = p->second;
		
		if(o_peer->state != Peer::PS_CONNECTED)
		{
			continue;
		}
		
		o_peer->sq.send(SendQueue::SEND_PRI_HIGH, destroy_peer_base, NULL, [](std::unique_lock<std::mutex> &l, HRESULT result) {});
	}
	
	return S_OK;
}

HRESULT DirectPlay8Peer::ReturnBuffer(CONST DPNHANDLE hBufferHandle, CONST DWORD dwFlags)
{
	unsigned char *buffer = (unsigned char*)(hBufferHandle);
	delete[] buffer;
	
	return S_OK;
}

HRESULT DirectPlay8Peer::GetPlayerContext(CONST DPNID dpnid,PVOID* CONST ppvPlayerContext, CONST DWORD dwFlags)
{
	std::unique_lock<std::mutex> l(lock);
	
	switch(state)
	{
		case STATE_NEW:                 return DPNERR_UNINITIALIZED;
		case STATE_INITIALISED:         return DPNERR_NOTREADY;
		case STATE_HOSTING:             break;
		case STATE_CONNECTING_TO_HOST:  return DPNERR_NOTREADY;
		case STATE_CONNECTING_TO_PEERS: return DPNERR_NOTREADY;
		case STATE_CONNECT_FAILED:      return DPNERR_NOTREADY;
		case STATE_CONNECTED:           break;
	}
	
	if(dpnid == local_player_id)
	{
		*ppvPlayerContext = local_player_ctx;
		return S_OK;
	}
	
	Peer *peer = get_peer_by_player_id(dpnid);
	if(peer != NULL)
	{
		*ppvPlayerContext = peer->player_ctx;
		return S_OK;
	}
	
	return DPNERR_INVALIDPLAYER;
}

HRESULT DirectPlay8Peer::GetGroupContext(CONST DPNID dpnid,PVOID* CONST ppvGroupContext, CONST DWORD dwFlags)
{
	std::unique_lock<std::mutex> l(lock);
	
	switch(state)
	{
		case STATE_NEW:                 return DPNERR_UNINITIALIZED;
		case STATE_INITIALISED:         return DPNERR_NOCONNECTION;
		case STATE_HOSTING:             break;
		case STATE_CONNECTING_TO_HOST:  break;
		case STATE_CONNECTING_TO_PEERS: break;
		case STATE_CONNECT_FAILED:      return DPNERR_CONNECTING;
		case STATE_CONNECTED:           break;
		case STATE_CLOSING:             return DPNERR_CONNECTIONLOST;
		case STATE_TERMINATED:          return DPNERR_CONNECTIONLOST;
	}
	
	Group *group = get_group_by_id(dpnid);
	if(group != NULL)
	{
		*ppvGroupContext = group->ctx;
		return S_OK;
	}
	else{
		return DPNERR_INVALIDGROUP;
	}
}

HRESULT DirectPlay8Peer::GetCaps(DPN_CAPS* CONST pdpCaps, CONST DWORD dwFlags)
{
	std::unique_lock<std::mutex> l(lock);
	
	if(state == STATE_NEW)
	{
		return DPNERR_UNINITIALIZED;
	}
	
	l.unlock();
	
	if(pdpCaps->dwSize == sizeof(DPN_CAPS))
	{
		pdpCaps->dwFlags                   = 0;
		pdpCaps->dwConnectTimeout          = 200;
		pdpCaps->dwConnectRetries          = 14;
		pdpCaps->dwTimeoutUntilKeepAlive   = 25000;
		
		return S_OK;
	}
	else if(pdpCaps->dwSize == sizeof(DPN_CAPS_EX))
	{
		DPN_CAPS_EX *pdpCapsEx = (DPN_CAPS_EX*)(pdpCaps);
		
		pdpCapsEx->dwFlags                   = 0;
		pdpCapsEx->dwConnectTimeout          = 200;
		pdpCapsEx->dwConnectRetries          = 14;
		pdpCapsEx->dwTimeoutUntilKeepAlive   = 25000;
		pdpCapsEx->dwMaxRecvMsgSize          = 0xFFFFFFFF;
		pdpCapsEx->dwNumSendRetries          = 10;
		pdpCapsEx->dwMaxSendRetryInterval    = 5000;
		pdpCapsEx->dwDropThresholdRate       = 7;
		pdpCapsEx->dwThrottleRate            = 25;
		pdpCapsEx->dwNumHardDisconnectSends  = 3;
		pdpCapsEx->dwMaxHardDisconnectPeriod = 500;
		
		return S_OK;
	}
	else{
		return DPNERR_INVALIDPARAM;
	}
}

HRESULT DirectPlay8Peer::SetCaps(CONST DPN_CAPS* CONST pdpCaps, CONST DWORD dwFlags)
{
	std::unique_lock<std::mutex> l(lock);
	
	if(state == STATE_NEW)
	{
		return DPNERR_UNINITIALIZED;
	}
	
	l.unlock();
	
	if(pdpCaps->dwSize == sizeof(DPN_CAPS) || pdpCaps->dwSize == sizeof(DPN_CAPS_EX))
	{
		/* Our protocol doesn't have all the tunables the official DirectPlay does... so
		 * just say everything was accepted.
		 *
		 * TODO: Store new values for future GetSPCaps() calls?
		*/
		return S_OK;
	}
	else{
		return DPNERR_INVALIDPARAM;
	}
}

HRESULT DirectPlay8Peer::SetSPCaps(CONST GUID* CONST pguidSP, CONST DPN_SP_CAPS* CONST pdpspCaps, CONST DWORD dwFlags )
{
	std::unique_lock<std::mutex> l(lock);
	
	if(state == STATE_NEW)
	{
		return DPNERR_UNINITIALIZED;
	}
	
	l.unlock();
	
	if(pdpspCaps->dwSize != sizeof(DPN_SP_CAPS))
	{
		return DPNERR_INVALIDPARAM;
	}
	
	if(*pguidSP != CLSID_DP8SP_TCPIP && *pguidSP != CLSID_DP8SP_IPX)
	{
		return DPNERR_DOESNOTEXIST;
	}
	
	/* From the DirectX 9.0 SDK:
	 *
	 * Currently, only the dwSystemBufferSize member can be set by this call. The dwNumThreads
	 * member is for legacy support. Microsoft DirectX 9.0 applications should use the
	 * IDirectPlay8ThreadPool::SetThreadCount method to set the number of threads. The other
	 * members of the DPN_SP_CAPS structure are get-only or ignored.
	*/
	
	return S_OK;
}

HRESULT DirectPlay8Peer::GetSPCaps(CONST GUID* CONST pguidSP, DPN_SP_CAPS* CONST pdpspCaps, CONST DWORD dwFlags)
{
	std::unique_lock<std::mutex> l(lock);
	
	if(state == STATE_NEW)
	{
		return DPNERR_UNINITIALIZED;
	}
	
	l.unlock();
	
	if(pdpspCaps->dwSize != sizeof(DPN_SP_CAPS))
	{
		return DPNERR_INVALIDPARAM;
	}
	
	if(*pguidSP != CLSID_DP8SP_TCPIP && *pguidSP != CLSID_DP8SP_IPX)
	{
		return DPNERR_DOESNOTEXIST;
	}
	
	/* TCP/IP and IPX both have the same values in DirectX. */
	
	pdpspCaps->dwFlags = DPNSPCAPS_SUPPORTSDPNSRV
	                   | DPNSPCAPS_SUPPORTSBROADCAST
	                   | DPNSPCAPS_SUPPORTSALLADAPTERS
	                   | DPNSPCAPS_SUPPORTSTHREADPOOL;
	
	pdpspCaps->dwNumThreads               = 3;
	pdpspCaps->dwDefaultEnumCount         = DEFAULT_ENUM_COUNT;
	pdpspCaps->dwDefaultEnumRetryInterval = DEFAULT_ENUM_INTERVAL;
	pdpspCaps->dwDefaultEnumTimeout       = DEFAULT_ENUM_TIMEOUT;
	pdpspCaps->dwMaxEnumPayloadSize       = 983;
	pdpspCaps->dwBuffersPerThread         = 1;
	pdpspCaps->dwSystemBufferSize         = 8192;
	
	return S_OK;
}

HRESULT DirectPlay8Peer::GetConnectionInfo(CONST DPNID dpnid, DPN_CONNECTION_INFO* CONST pdpConnectionInfo, CONST DWORD dwFlags)
{
	UNIMPLEMENTED("DirectPlay8Peer::GetConnectionInfo");
}

HRESULT DirectPlay8Peer::RegisterLobby(CONST DPNHANDLE dpnHandle, struct IDirectPlay8LobbiedApplication* CONST pIDP8LobbiedApplication, CONST DWORD dwFlags)
{
	UNIMPLEMENTED("DirectPlay8Peer::RegisterLobby");
}

HRESULT DirectPlay8Peer::TerminateSession(void* CONST pvTerminateData, CONST DWORD dwTerminateDataSize, CONST DWORD dwFlags)
{
	std::unique_lock<std::mutex> l(lock);
	
	switch(state)
	{
		case STATE_NEW:                 return DPNERR_UNINITIALIZED;
		case STATE_INITIALISED:         return DPNERR_NOCONNECTION;
		case STATE_HOSTING:             break;
		case STATE_CONNECTING_TO_HOST:  return DPNERR_CONNECTING;
		case STATE_CONNECTING_TO_PEERS: return DPNERR_CONNECTING;
		case STATE_CONNECT_FAILED:      return DPNERR_CONNECTING;
		case STATE_CONNECTED:           return DPNERR_NOTHOST;
		case STATE_CLOSING:             return DPNERR_CONNECTIONLOST;
		case STATE_TERMINATED:          return DPNERR_HOSTTERMINATEDSESSION;
	}
	
	close_main_sockets();
	
	/* First, we iterate over all the peers.
	 *
	 * For connected peers: Notify them of the impending doom, and add them to closing_peers
	 * so we can raise a DPNMSG_DESTROY_PLAYER later.
	 *
	 * For other peers: Add them to destroy_peers, to be destroyed later.
	 *
	 * We defer these actions until later to ensure nothing can change the state of the peers
	 * underneath us, as dealing with peers potentially changing state while this runs would
	 * be rather horrible.
	*/
	
	PacketSerialiser terminate_session(DPLITE_MSGID_TERMINATE_SESSION);
	terminate_session.append_data(pvTerminateData, dwTerminateDataSize);
	
	std::list< std::pair<DPNID, void*> > closing_peers;
	std::list<unsigned int> destroy_peers;
	
	for(auto pi = peers.begin(); pi != peers.end(); ++pi)
	{
		unsigned int peer_id = pi->first;
		Peer *peer = pi->second;
		
		if(peer->state == Peer::PS_CONNECTED)
		{
			peer->sq.send(SendQueue::SEND_PRI_HIGH, terminate_session, NULL, [](std::unique_lock<std::mutex> &l, HRESULT result){});
			peer->state = Peer::PS_CLOSING;
			
			closing_peers.push_back(std::make_pair(peer->player_id, peer->player_ctx));
		}
		else if(peer->state == Peer::PS_CLOSING)
		{
			/* Do nothing. We're waiting for this peer to go away. */
		}
		else{
			destroy_peers.push_back(peer_id);
		}
	}
	
	state = STATE_TERMINATED;
	
	/* Raise DPNMSG_TERMINATE_SESSION. */
	
	{
		DPNMSG_TERMINATE_SESSION ts;
		memset(&ts, 0, sizeof(ts));
		
		ts.dwSize              = sizeof(DPNMSG_TERMINATE_SESSION);
		ts.hResultCode         = DPNERR_HOSTTERMINATEDSESSION;
		ts.pvTerminateData     = pvTerminateData;
		ts.dwTerminateDataSize = dwTerminateDataSize;
		
		l.unlock();
		message_handler(message_handler_ctx, DPN_MSGID_TERMINATE_SESSION, &ts);
		l.lock();
	}
	
	/* Raise a DPNMSG_DESTROY_PLAYER for ourself. */
	dispatch_destroy_player(l, local_player_id, local_player_ctx, DPNDESTROYPLAYERREASON_SESSIONTERMINATED);
	
	/* Raise a DPNMSG_DESTROY_PLAYER for each connected peer. */
	
	for(auto cp = closing_peers.begin(); cp != closing_peers.end(); ++cp)
	{
		dispatch_destroy_player(l, cp->first, cp->second, DPNDESTROYPLAYERREASON_SESSIONTERMINATED);
	}
	
	group_destroy_all(l, DPNDESTROYGROUPREASON_NORMAL);
	
	/* Destroy any peers which weren't fully connected. */
	
	for(auto dp = destroy_peers.begin(); dp != destroy_peers.end();)
	{
		unsigned int peer_id = *dp;
		peer_destroy(l, *dp, DPNERR_USERCANCEL, DPNDESTROYPLAYERREASON_NORMAL);
	}
	
	return S_OK;
}

DirectPlay8Peer::Peer *DirectPlay8Peer::get_peer_by_peer_id(unsigned int peer_id)
{
	auto pi = peers.find(peer_id);
	if(pi != peers.end())
	{
		return pi->second;
	}
	else{
		return NULL;
	}
}

DirectPlay8Peer::Peer *DirectPlay8Peer::get_peer_by_player_id(DPNID player_id)
{
	auto i = player_to_peer_id.find(player_id);
	if(i != player_to_peer_id.end())
	{
		return get_peer_by_peer_id(i->second);
	}
	else{
		return NULL;
	}
}

DirectPlay8Peer::Group *DirectPlay8Peer::get_group_by_id(DPNID group_id)
{
	auto gi = groups.find(group_id);
	if(gi != groups.end())
	{
		return &(gi->second);
	}
	else{
		return NULL;
	}
}

void DirectPlay8Peer::handle_udp_socket_event()
{
	std::unique_lock<std::mutex> l(lock);
	
	if(udp_socket == -1)
	{
		return;
	}
	
	struct sockaddr_in from_addr;
	int fa_len = sizeof(from_addr);
	
	unsigned char recv_buf[MAX_PACKET_SIZE];
	
	int r = recvfrom(udp_socket, (char*)(recv_buf), sizeof(recv_buf), 0, (struct sockaddr*)(&from_addr), &fa_len);
	if(r > 0)
	{
		/* Process message */
		std::unique_ptr<PacketDeserialiser> pd;
		
		try {
			pd.reset(new PacketDeserialiser(recv_buf, r));
		}
		catch(const PacketDeserialiser::Error &e)
		{
			/* Malformed packet received */
			return;
		}
		
		switch(pd->packet_type())
		{
			case DPLITE_MSGID_HOST_ENUM_REQUEST:
			{
				handle_host_enum_request(l, *pd, &from_addr);
				break;
			}
			
			default:
			{
				char s_ip[16];
				inet_ntop(AF_INET, &(from_addr.sin_addr), s_ip, sizeof(s_ip));
				
				log_printf(
					"Unexpected message type %u received on udp_socket from %s",
					(unsigned)(pd->packet_type()), s_ip);
				
				break;
			}
		}
	}
	
	io_udp_send(l);
}

void DirectPlay8Peer::handle_other_socket_event()
{
	std::unique_lock<std::mutex> l(lock);
	
	if(discovery_socket != -1)
	{
		struct sockaddr_in from_addr;
		int fa_len = sizeof(from_addr);
		
		unsigned char recv_buf[MAX_PACKET_SIZE];
		
		int r = recvfrom(discovery_socket, (char*)(recv_buf), sizeof(recv_buf), 0, (struct sockaddr*)(&from_addr), &fa_len);
		if(r > 0)
		{
			/* Process message */
			std::unique_ptr<PacketDeserialiser> pd;
			
			try {
				pd.reset(new PacketDeserialiser(recv_buf, r));
			}
			catch(const PacketDeserialiser::Error &e)
			{
				/* Malformed packet received */
				return;
			}
			
			switch(pd->packet_type())
			{
				case DPLITE_MSGID_HOST_ENUM_REQUEST:
				{
					handle_host_enum_request(l, *pd, &from_addr);
					break;
				}
				
				default:
					char s_ip[16];
					inet_ntop(AF_INET, &(from_addr.sin_addr), s_ip, sizeof(s_ip));
					
					log_printf(
						"Unexpected message type %u received on discovery_socket from %s",
						(unsigned)(pd->packet_type()), s_ip);
					
					break;
			}
		}
	}
	
	peer_accept(l);
}

void DirectPlay8Peer::queue_work(const std::function<void()> &work)
{
	work_queue.push(work);
	SetEvent(work_ready);
}

void DirectPlay8Peer::handle_work()
{
	std::unique_lock<std::mutex> l(lock);
	
	if(!work_queue.empty())
	{
		std::function<void()> work = work_queue.front();
		work_queue.pop();
		
		if(!work_queue.empty())
		{
			/* Wake up another thread, in case we are heavily loaded and the pool isn't
			 * keeping up with the events from queue_work()
			*/
			SetEvent(work_ready);
		}
		
		l.unlock();
		
		work();
	}
}

void DirectPlay8Peer::io_peer_triggered(unsigned int peer_id)
{
	std::unique_lock<std::mutex> l(lock);
	
	Peer *peer = get_peer_by_peer_id(peer_id);
	if(peer == NULL)
	{
		/* The peer has gone away since this event was raised. Discard. */
		return;
	}
	
	if(peer->state == Peer::PS_CONNECTING_HOST)
	{
		assert(state == STATE_CONNECTING_TO_HOST);
		io_peer_connected(l, peer_id);
	}
	else if(peer->state == Peer::PS_CONNECTING_PEER)
	{
		assert(state == STATE_CONNECTING_TO_PEERS);
		io_peer_connected(l, peer_id);
	}
	else{
		io_peer_send(l, peer_id);
		io_peer_recv(l, peer_id);
	}
}

void DirectPlay8Peer::io_udp_send(std::unique_lock<std::mutex> &l)
{
	SendQueue::SendOp *sqop;
	
	while(udp_socket != -1 && (sqop = udp_sq.get_pending()) != NULL)
	{
		std::pair<const void*, size_t>            data = sqop->get_data();
		std::pair<const struct sockaddr*, size_t> addr = sqop->get_dest_addr();
		
		int s = sendto(udp_socket, (const char*)(data.first), data.second, 0, addr.first, addr.second);
		if(s == -1)
		{
			DWORD err = WSAGetLastError();
			
			if(err == WSAEWOULDBLOCK)
			{
				/* Socket send buffer is full. */
				return;
			}
			else{
				/* TODO: LOG ME */
			}
		}
		
		udp_sq.pop_pending(sqop);
		
		/* Wake up another worker to continue dealing with this socket in case we wind up
		 * blocking for a long time in application code within the callback.
		*/
		SetEvent(udp_socket_event);
		
		/* TODO: More specific error codes */
		sqop->invoke_callback(l, (s < 0 ? DPNERR_GENERIC : S_OK));
		
		delete sqop;
	}
}

void DirectPlay8Peer::io_peer_connected(std::unique_lock<std::mutex> &l, unsigned int peer_id)
{
	Peer *peer = get_peer_by_peer_id(peer_id);
	assert(peer != NULL);
	
	int error;
	int esize = sizeof(error);
	
	if(getsockopt(peer->sock, SOL_SOCKET, SO_ERROR, (char*)(&error), &esize) != 0)
	{
		log_printf("getsockopt(level = SOL_SOCKET, optname = SO_ERROR) failed");
		connect_fail(l, DPNERR_GENERIC, NULL, 0);
	}
	
	if(error == 0)
	{
		/* TCP connection established. */
		
		if(peer->state == Peer::PS_CONNECTING_HOST)
		{
			PacketSerialiser connect_host(DPLITE_MSGID_CONNECT_HOST);
			
			if(instance_guid != GUID_NULL)
			{
				connect_host.append_guid(instance_guid);
			}
			else{
				connect_host.append_null();
			}
			
			connect_host.append_guid(application_guid);
			
			if(!password.empty())
			{
				connect_host.append_wstring(password);
			}
			else{
				connect_host.append_null();
			}
			
			if(!connect_req_data.empty())
			{
				connect_host.append_data(connect_req_data.data(), connect_req_data.size());
			}
			else{
				connect_host.append_null();
			}
			
			connect_host.append_wstring(local_player_name);
			connect_host.append_data(local_player_data.data(), local_player_data.size());
			
			peer->sq.send(SendQueue::SEND_PRI_MEDIUM,
				connect_host,
				NULL,
				[](std::unique_lock<std::mutex> &l, HRESULT result){});
			
			peer->state = Peer::PS_REQUESTING_HOST;
		}
		else if(peer->state == Peer::PS_CONNECTING_PEER)
		{
			PacketSerialiser connect_peer(DPLITE_MSGID_CONNECT_PEER);
			
			connect_peer.append_guid(instance_guid);
			connect_peer.append_guid(application_guid);
			connect_peer.append_wstring(password);
			
			connect_peer.append_dword(local_player_id);
			connect_peer.append_wstring(local_player_name);
			connect_peer.append_data(local_player_data.data(), local_player_data.size());
			
			peer->sq.send(SendQueue::SEND_PRI_HIGH,
				connect_peer,
				NULL,
				[](std::unique_lock<std::mutex> &l, HRESULT result){});
			
			peer->state = Peer::PS_REQUESTING_PEER;
		}
	}
	else{
		/* TCP connection failed. */
		
		if(peer->state == Peer::PS_CONNECTING_HOST)
		{
			connect_fail(l, DPNERR_NOCONNECTION, NULL, 0);
		}
		else if(peer->state == Peer::PS_CONNECTING_PEER)
		{
			connect_fail(l, DPNERR_PLAYERNOTREACHABLE, NULL, 0);
		}
	}
}

void DirectPlay8Peer::io_peer_send(std::unique_lock<std::mutex> &l, unsigned int peer_id)
{
	Peer *peer;
	SendQueue::SendOp *sqop;
	
	while((peer = get_peer_by_peer_id(peer_id)) != NULL)
	{
		if((sqop = peer->sq.get_pending()) != NULL)
		{
			std::pair<const void*, size_t> d = sqop->get_pending_data();
			
			int s = send(peer->sock, (const char*)(d.first), d.second, 0);
			
			if(s < 0)
			{
				DWORD err = WSAGetLastError();
				
				if(err == WSAEWOULDBLOCK)
				{
					/* Send buffer full. Try again later. */
					break;
				}
				else{
					/* Write error. */
					
					log_printf("Write error on peer %u: %s", peer_id, win_strerror(err).c_str());
					log_printf("Closing connection");
					
					peer_destroy(l, peer_id, DPNERR_CONNECTIONLOST, DPNDESTROYPLAYERREASON_CONNECTIONLOST);
					return;
				}
			}
			
			sqop->inc_sent_data(s);
			
			if(s == d.second)
			{
				peer->sq.pop_pending(sqop);
				
				if(peer->sq.get_pending() != NULL)
				{
					/* There is another message in the send queue.
					 *
					 * Wake another worker to dispatch it in case we have to
					 * block within the application for a while.
					*/
					SetEvent(peer->event);
				}
				
				sqop->invoke_callback(l, S_OK);
				delete sqop;
			}
		}
		else{
			if(peer->state == Peer::PS_CLOSING && peer->send_open)
			{
				/* Peer is in a closing state and send queue has been cleared.
				 *
				 * Begin a graceful shutdown from our end to ensure the peer
				 * receives any informational messages about the close. It will do
				 * a hard close once it receives our EOF.
				*/
				
				if(shutdown(peer->sock, SD_SEND) != 0)
				{
					DWORD err = WSAGetLastError();
					log_printf(
						"shutdown(SD_SEND) on peer %u failed: %s",
						peer_id, win_strerror(err));
					
					peer_destroy(l, peer_id, DPNERR_CONNECTIONLOST, DPNDESTROYPLAYERREASON_CONNECTIONLOST);
					return;
				}
				
				peer->send_open = false;
			}
			
			break;
		}
	}
}

void DirectPlay8Peer::io_peer_recv(std::unique_lock<std::mutex> &l, unsigned int peer_id)
{
	Peer *peer;
	
	bool rb_claimed = false;
	
	while((peer = get_peer_by_peer_id(peer_id)) != NULL)
	{
		if(!rb_claimed && peer->recv_busy)
		{
			/* Another thread is already processing data from this socket.
			 *
			 * Only one thread at a time is allowed to handle reads, even when the
			 * other thread is in the application callback, so that the order of
			 * messages is preserved.
			*/
			return;
		}
		
		int r = recv(peer->sock, (char*)(peer->recv_buf) + peer->recv_buf_cur, sizeof(peer->recv_buf) - peer->recv_buf_cur, 0);
		DWORD err = WSAGetLastError();
		
		if(r < 0 && err == WSAEWOULDBLOCK)
		{
			/* Nothing to read. */
			break;
		}
		
		if(!rb_claimed)
		{
			/* No other thread is processing data from this peer, we shall
			 * claim the throne and temporarily disable FD_READ events from it
			 * to avoid other workers spinning against the recv_busy lock.
			*/
			
			peer->recv_busy = true;
			rb_claimed      = true;
			
			peer->disable_events(FD_READ | FD_CLOSE);
		}
		
		if(r == 0)
		{
			/* When the remote end initiates a graceful close, it will no longer
			 * process anything we send it. Just close the connection.
			*/
			
			peer_destroy(l, peer_id, DPNERR_CONNECTIONLOST, DPNDESTROYPLAYERREASON_NORMAL);
			return;
		}
		else if(r < 0)
		{
			/* Read error. */
			
			log_printf("Read error on peer %u: %s", peer_id, win_strerror(err).c_str());
			log_printf("Closing connection");
			
			peer_destroy(l, peer_id, DPNERR_CONNECTIONLOST, DPNDESTROYPLAYERREASON_CONNECTIONLOST);
			return;
		}
		
		if(peer->state == Peer::PS_CLOSING)
		{
			/* When a peer is in PS_CLOSING, we keep the socket open until the send queue has
			 * been flushed and discard anything we read until we get EOF.
			*/
			
			continue;
		}
		
		peer->recv_buf_cur += r;
		
		while(peer->recv_buf_cur >= sizeof(TLVChunk))
		{
			TLVChunk *header = (TLVChunk*)(peer->recv_buf);
			size_t full_packet_size = sizeof(TLVChunk) + header->value_length;
			
			if(full_packet_size > MAX_PACKET_SIZE)
			{
				/* Malformed packet received - TCP stream invalid! */
				
				log_printf(
					"Received over-size packet from peer %u, dropping connection",
					peer_id);
				
				peer_destroy(l, peer_id, DPNERR_CONNECTIONLOST, DPNDESTROYPLAYERREASON_CONNECTIONLOST);
				return;
			}
			
			if(peer->recv_buf_cur >= full_packet_size)
			{
				/* Process message */
				std::unique_ptr<PacketDeserialiser> pd;
				
				try {
					pd.reset(new PacketDeserialiser(peer->recv_buf, full_packet_size));
				}
				catch(const PacketDeserialiser::Error &e)
				{
					/* Malformed packet received - TCP stream invalid! */
					
					log_printf(
						"Received malformed packet (%s) from peer %u, dropping connection",
						e.what(), peer_id);
					
					peer_destroy(l, peer_id, DPNERR_CONNECTIONLOST, DPNDESTROYPLAYERREASON_CONNECTIONLOST);
					return;
				}
				
				switch(pd->packet_type())
				{
					case DPLITE_MSGID_CONNECT_HOST:
					{
						handle_host_connect_request(l, peer_id, *pd);
						break;
					}
					
					case DPLITE_MSGID_CONNECT_HOST_OK:
					{
						handle_host_connect_ok(l, peer_id, *pd);
						break;
					}
					
					case DPLITE_MSGID_CONNECT_HOST_FAIL:
					{
						handle_host_connect_fail(l, peer_id, *pd);
						break;
					}
					
					case DPLITE_MSGID_MESSAGE:
					{
						handle_message(l, *pd);
						break;
					}
					
					case DPLITE_MSGID_PLAYERINFO:
					{
						handle_playerinfo(l, peer_id, *pd);
						break;
					}
					
					case DPLITE_MSGID_ACK:
					{
						handle_ack(l, peer_id, *pd);
						break;
					}
					
					case DPLITE_MSGID_APPDESC:
					{
						handle_appdesc(l, peer_id, *pd);
						break;
					}
					
					case DPLITE_MSGID_CONNECT_PEER:
					{
						handle_connect_peer(l, peer_id, *pd);
						break;
					}
					
					case DPLITE_MSGID_CONNECT_PEER_OK:
					{
						handle_connect_peer_ok(l, peer_id, *pd);
						break;
					}
					
					case DPLITE_MSGID_CONNECT_PEER_FAIL:
					{
						handle_connect_peer_fail(l, peer_id, *pd);
						break;
					}
					
					case DPLITE_MSGID_DESTROY_PEER:
					{
						handle_destroy_peer(l, peer_id, *pd);
						break;
					}
					
					case DPLITE_MSGID_TERMINATE_SESSION:
					{
						handle_terminate_session(l, peer_id, *pd);
						break;
					}
					
					case DPLITE_MSGID_GROUP_ALLOCATE:
					{
						handle_group_allocate(l, peer_id, *pd);
						break;
					}
					
					case DPLITE_MSGID_GROUP_CREATE:
					{
						handle_group_create(l, peer_id, *pd);
						break;
					}
					
					case DPLITE_MSGID_GROUP_DESTROY:
					{
						handle_group_destroy(l, peer_id, *pd);
						break;
					}
					
					case DPLITE_MSGID_GROUP_JOIN:
					{
						handle_group_join(l, peer_id, *pd);
						break;
					}
					
					case DPLITE_MSGID_GROUP_JOINED:
					{
						handle_group_joined(l, peer_id, *pd);
						break;
					}
					
					case DPLITE_MSGID_GROUP_LEAVE:
					{
						handle_group_leave(l, peer_id, *pd);
						break;
					}
					
					case DPLITE_MSGID_GROUP_LEFT:
					{
						handle_group_left(l, peer_id, *pd);
						break;
					}
					
					default:
						log_printf(
							"Unexpected message type %u received from peer %u",
							(unsigned)(pd->packet_type()), peer_id);
						break;
				}
				
				RENEW_PEER_OR_RETURN();
				
				/* Message at the front of the buffer has been dealt with, shift any
				 * remaining data beyond it to the front and truncate it.
				*/
				
				memmove(peer->recv_buf, peer->recv_buf + full_packet_size,
					peer->recv_buf_cur - full_packet_size);
				peer->recv_buf_cur -= full_packet_size;
			}
			else{
				/* Haven't read the full message yet. */
				break;
			}
		}
	}
	
	if(peer != NULL && rb_claimed)
	{
		peer->enable_events(FD_READ | FD_CLOSE);
		peer->recv_busy = false;
	}
}

void DirectPlay8Peer::peer_accept(std::unique_lock<std::mutex> &l)
{
	if(listener_socket == -1)
	{
		return;
	}
	
	struct sockaddr_in addr;
	int addrlen = sizeof(addr);
	
	int newfd = accept(listener_socket, (struct sockaddr*)(&addr), &addrlen);
	if(newfd == -1)
	{
		DWORD err = WSAGetLastError();
		
		if(err == WSAEWOULDBLOCK)
		{
			return;
		}
		else{
			/* Hopefully this is temporary and doesn't go into a tight loop... */
			log_printf("Incoming connection failed: %s", win_strerror(err).c_str());
			return;
		}
	}
	
	struct linger li;
	li.l_onoff = 0;
	li.l_linger = 0;
	
	if(setsockopt(newfd, SOL_SOCKET, SO_LINGER, (char*)(&li), sizeof(li)) != 0)
	{
		DWORD err = WSAGetLastError();
		log_printf("Failed to set SO_LINGER parameters on accepted connection: %s", win_strerror(err).c_str());
		
		/* Not fatal, since this probably won't matter in production. */
	}
	
	u_long non_blocking = 1;
	if(ioctlsocket(newfd, FIONBIO, &non_blocking) != 0)
	{
		DWORD err = WSAGetLastError();
		log_printf("Failed to set accepted connection to non-blocking mode: %s", win_strerror(err).c_str());
		log_printf("Closing connection");
		
		closesocket(newfd);
		return;
	}
	
	/* Set SO_LINGER so that closesocket() does a hard close, immediately removing the socket
	 * address from the connection table.
	 *
	 * If this isn't done, then we are able to immediately bind() new sockets to the same
	 * local address (as we may when the port isn't specified), but then outgoing connections
	 * made from it will fail with WSAEADDRINUSE until the background close completes.
	*/
	
	struct linger no_linger;
	no_linger.l_onoff  = 1;
	no_linger.l_linger = 0;
	
	if(setsockopt(newfd, SOL_SOCKET, SO_LINGER, (char*)(&no_linger), sizeof(no_linger)) != 0)
	{
		DWORD err = WSAGetLastError();
		log_printf("Failed to set SO_LINGER on accepted connection: %s", win_strerror(err).c_str());
	}
	
	unsigned int peer_id = next_peer_id++;
	Peer *peer = new Peer(Peer::PS_ACCEPTED, newfd, addr.sin_addr.s_addr, ntohs(addr.sin_port));
	
	if(!peer->enable_events(FD_READ | FD_WRITE | FD_CLOSE))
	{
		log_printf("WSAEventSelect() failed, dropping peer");
		
		closesocket(peer->sock);
		delete peer;
		
		return;
	}
	
	peers.insert(std::make_pair(peer_id, peer));
	
	worker_pool->add_handle(peer->event, [this, peer_id]() { io_peer_triggered(peer_id); });
}

bool DirectPlay8Peer::peer_connect(Peer::PeerState initial_state, uint32_t remote_ip, uint16_t remote_port, DPNID player_id)
{
	int p_sock = create_client_socket(local_ip, local_port);
	if(p_sock == -1)
	{
		return false;
	}
	
	unsigned int peer_id = next_peer_id++;
	Peer *peer = new Peer(initial_state, p_sock, remote_ip, remote_port);
	
	peer->player_id = player_id;
	
	if(!peer->enable_events(FD_CONNECT | FD_READ | FD_WRITE | FD_CLOSE))
	{
		closesocket(peer->sock);
		delete peer;
		
		return false;
	}
	
	struct sockaddr_in r_addr;
	r_addr.sin_family      = AF_INET;
	r_addr.sin_addr.s_addr = remote_ip;
	r_addr.sin_port        = htons(remote_port);
	
	if(connect(peer->sock, (struct sockaddr*)(&r_addr), sizeof(r_addr)) != -1 || WSAGetLastError() != WSAEWOULDBLOCK)
	{
		closesocket(peer->sock);
		delete peer;
		
		return false;
	}
	
	peers.insert(std::make_pair(peer_id, peer));
	
	worker_pool->add_handle(peer->event, [this, peer_id]() { io_peer_triggered(peer_id); });
	
	return true;
}

void DirectPlay8Peer::peer_destroy(std::unique_lock<std::mutex> &l, unsigned int peer_id, HRESULT outstanding_op_result, DWORD destroy_player_reason)
{
	Peer *peer = get_peer_by_peer_id(peer_id);
	assert(peer != NULL);
	
	/* Cancel any outstanding sends and notify the callbacks. */
	
	for(SendQueue::SendOp *sqop; (sqop = peer->sq.get_pending()) != NULL;)
	{
		peer->sq.pop_pending(sqop);
		
		sqop->invoke_callback(l, outstanding_op_result);
		delete sqop;
		
		RENEW_PEER_OR_RETURN();
	}
	
	/* Fail any outstanding acks and notify the callbacks. */
	
	while(!peer->pending_acks.empty())
	{
		auto ai = peer->pending_acks.begin();
		
		std::function<void(std::unique_lock<std::mutex>&, HRESULT, const void*, size_t)> callback = ai->second;
		peer->pending_acks.erase(ai);
		
		callback(l, outstanding_op_result, NULL, 0);
		
		RENEW_PEER_OR_RETURN();
	}
	
	if(peer->state == Peer::PS_CONNECTED)
	{
		DPNID killed_player_id = peer->player_id;
		
		/* Bodge to prevent io_peer_send() initiating a graceful shutdown
		 * while the application is handling the DPNMSG_DESTROY_PLAYER.
		*/
		peer->send_open = false;
		
		peer->state = Peer::PS_CLOSING;
		
		dispatch_destroy_player(l, peer->player_id, peer->player_ctx, destroy_player_reason);
		
		player_to_peer_id.erase(killed_player_id);
		
		if(state == STATE_CONNECTED && killed_player_id == host_player_id)
		{
			/* The connection to the host has been lost. We need to raise a
			 * DPNMSG_TERMINATE_SESSION and dump all the other peers. We don't return
			 * to STATE_INITIALISED because the application is still expected to call
			 * IDirectPlay8Peer::Close() after receiving DPNMSG_TERMINATE_SESSION.
			 *
			 * TODO: Implement host migration here.
			*/
			
			DPNMSG_TERMINATE_SESSION ts;
			memset(&ts, 0, sizeof(ts));
			
			ts.dwSize              = sizeof(DPNMSG_TERMINATE_SESSION);
			ts.hResultCode         = outstanding_op_result;
			ts.pvTerminateData     = (void*)(NULL);
			ts.dwTerminateDataSize = 0;
			
			state = STATE_TERMINATED;
			
			l.unlock();
			message_handler(message_handler_ctx, DPN_MSGID_TERMINATE_SESSION, &ts);
			l.lock();
			
			dispatch_destroy_player(l, local_player_id, local_player_ctx, DPNDESTROYPLAYERREASON_NORMAL);
			
			while(!peers.empty())
			{
				auto peer_id = peers.begin()->first;
				peer_destroy(l, peer_id, outstanding_op_result, destroy_player_reason);
			}
			
			group_destroy_all(l, DPNDESTROYGROUPREASON_NORMAL);
		}
		
		RENEW_PEER_OR_RETURN();
	}
	else if(state == STATE_CONNECTING_TO_HOST && (peer->state == Peer::PS_CONNECTING_HOST || peer->state == Peer::PS_REQUESTING_HOST))
	{
		connect_fail(l, outstanding_op_result, NULL, 0);
		RENEW_PEER_OR_RETURN();
	}
	else if(state == STATE_CONNECTING_TO_PEERS && (peer->state == Peer::PS_CONNECTING_PEER || peer->state == Peer::PS_REQUESTING_PEER))
	{
		connect_fail(l, DPNERR_PLAYERNOTREACHABLE, NULL, 0);
		RENEW_PEER_OR_RETURN();
	}
	
	worker_pool->remove_handle(peer->event);
	
	closesocket(peer->sock);
	
	peers.erase(peer_id);
	delete peer;
	
	peer_destroyed.notify_all();
}

void DirectPlay8Peer::peer_destroy_all(std::unique_lock<std::mutex> &l, HRESULT outstanding_op_result, DWORD destroy_player_reason)
{
	while(!peers.empty())
	{
		unsigned int peer_id = peers.begin()->first;
		peer_destroy(l, peer_id, outstanding_op_result, destroy_player_reason);
	}
}

void DirectPlay8Peer::peer_shutdown(std::unique_lock<std::mutex> &l, unsigned int peer_id, HRESULT outstanding_op_result, DWORD destroy_player_reason)
{
	Peer *peer = get_peer_by_peer_id(peer_id);
	assert(peer != NULL);
	
	if(peer->state == Peer::PS_CONNECTED)
	{
		/* Peer is a fully connected player, initiate a graceful shutdown and raise a
		 * DPNMSG_DESTROY_PLAYER message.
		*/
		
		peer->state = Peer::PS_CLOSING;
		SetEvent(peer->event);
		
		dispatch_destroy_player(l, peer->player_id, peer->player_ctx, destroy_player_reason);
		
		player_to_peer_id.erase(peer_id);
	}
	else if(peer->state == Peer::PS_CLOSING)
	{
		/* We're waiting for this peer to go away. Do nothing. */
	}
	else{
		/* Peer is not a fully fledged player, just destroy it. */
		peer_destroy(l, peer_id, outstanding_op_result, destroy_player_reason);
	}
}

void DirectPlay8Peer::peer_shutdown_all(std::unique_lock<std::mutex> &l, HRESULT outstanding_op_result, DWORD destroy_player_reason)
{
	for(auto p = peers.begin(); p != peers.end();)
	{
		unsigned int peer_id = p->first;
		Peer *peer = p->second;
		
		if(peer->state == Peer::PS_CLOSING)
		{
			/* Peer is already shutting down. Do nothing. */
			++p;
		}
		else{
			/* Gracefully shutdown or destroy the peer as appropriate. Restart the
			 * loop as any iterator into peers may have been invalidated within the
			 * peer_shutdown() call.
			*/
			
			peer_shutdown(l, peer_id, outstanding_op_result, destroy_player_reason);
			p = peers.begin();
			
			#ifndef NDEBUG
			peer = get_peer_by_peer_id(peer_id);
			if(peer != NULL)
			{
				assert(peer->state == Peer::PS_CLOSING);
			}
			#endif
		}
	}
}

void DirectPlay8Peer::group_destroy_all(std::unique_lock<std::mutex> &l, DWORD dwReason)
{
	for(std::map<DPNID, Group>::iterator g; (g = groups.begin()) != groups.end();)
	{
		DPNID group_id = g->first;
		Group *group   = &(g->second);
		
		if(destroyed_groups.find(group_id) == destroyed_groups.end())
		{
			destroyed_groups.insert(group_id);
			dispatch_destroy_group(l, group_id, group->ctx, dwReason);
		}
		
		groups.erase(group_id);
	}
}

void DirectPlay8Peer::close_main_sockets()
{
	if(discovery_socket != -1)
	{
		closesocket(discovery_socket);
		discovery_socket = -1;
	}
	
	if(listener_socket != -1)
	{
		closesocket(listener_socket);
		listener_socket = -1;
	}
	
	if(udp_socket != -1)
	{
		closesocket(udp_socket);
		udp_socket = -1;
	}
}

void DirectPlay8Peer::handle_host_enum_request(std::unique_lock<std::mutex> &l, const PacketDeserialiser &pd, const struct sockaddr_in *from_addr)
{
	if(state != STATE_HOSTING)
	{
		return;
	}
	
	if(!pd.is_null(0))
	{
		GUID r_application_guid = pd.get_guid(0);
		
		if(application_guid != r_application_guid)
		{
			/* This isn't the application you're looking for.
			 * It can go about its business.
			*/
			return;
		}
	}
	
	DPNMSG_ENUM_HOSTS_QUERY ehq;
	memset(&ehq, 0, sizeof(ehq));
	
	DirectPlay8Address *sender_address = DirectPlay8Address::create_host_address(
		global_refcount, service_provider, (struct sockaddr*)(from_addr));
	
	ehq.dwSize = sizeof(ehq);
	ehq.pAddressSender = sender_address;
	ehq.pAddressDevice = NULL; // TODO
	
	if(!pd.is_null(1))
	{
		std::pair<const void*, size_t> data = pd.get_data(1);
		
		ehq.pvReceivedData     = (void*)(data.first); /* TODO: Make a non-const copy? */
		ehq.dwReceivedDataSize = data.second;
	}
	
	ehq.dwMaxResponseDataSize = 9999; // TODO
	
	DWORD req_tick = pd.get_dword(2);
	
	l.unlock();
	HRESULT ehq_result = message_handler(message_handler_ctx, DPN_MSGID_ENUM_HOSTS_QUERY, &ehq);
	l.lock();
	
	sender_address->Release();
	
	std::vector<unsigned char> response_data_buffer;
	if(ehq.dwResponseDataSize > 0)
	{
		response_data_buffer.reserve(ehq.dwResponseDataSize);
		response_data_buffer.insert(response_data_buffer.end(),
			(const unsigned char*)(ehq.pvResponseData),
			(const unsigned char*)(ehq.pvResponseData) + ehq.dwResponseDataSize);
		
		DPNMSG_RETURN_BUFFER rb;
		memset(&rb, 0, sizeof(rb));
		
		rb.dwSize        = sizeof(rb);
		rb.hResultCode   = S_OK;
		rb.pvBuffer      = ehq.pvResponseData;
		rb.pvUserContext = ehq.pvResponseContext;
		
		l.unlock();
		message_handler(message_handler_ctx, DPN_MSGID_RETURN_BUFFER, &rb);
		l.lock();
		
		ehq.pvResponseData = response_data_buffer.data();
	}
	
	if(state != STATE_HOSTING)
	{
		return;
	}
	
	if(ehq_result == DPN_OK)
	{
		PacketSerialiser host_enum_response(DPLITE_MSGID_HOST_ENUM_RESPONSE);
		
		host_enum_response.append_dword(password.empty() ? 0 : DPNSESSION_REQUIREPASSWORD);
		host_enum_response.append_guid(instance_guid);
		host_enum_response.append_guid(application_guid);
		host_enum_response.append_dword(max_players);
		host_enum_response.append_dword(player_to_peer_id.size() + 1);
		host_enum_response.append_wstring(session_name);
		
		if(!application_data.empty())
		{
			host_enum_response.append_data(application_data.data(), application_data.size());
		}
		else{
			host_enum_response.append_null();
		}
		
		if(ehq.dwResponseDataSize > 0)
		{
			host_enum_response.append_data(ehq.pvResponseData, ehq.dwResponseDataSize);
		}
		else{
			host_enum_response.append_null();
		}
		
		host_enum_response.append_dword(req_tick);
		
		udp_sq.send(SendQueue::SEND_PRI_MEDIUM,
			host_enum_response,
			from_addr,
			[](std::unique_lock<std::mutex> &l, HRESULT result){});
	}
	else{
		/* Application rejected the DPNMSG_ENUM_HOSTS_QUERY message. */
	}
}

void DirectPlay8Peer::handle_host_connect_request(std::unique_lock<std::mutex> &l, unsigned int peer_id, const PacketDeserialiser &pd)
{
	Peer *peer = get_peer_by_peer_id(peer_id);
	assert(peer != NULL);
	
	if(peer->state != Peer::PS_ACCEPTED)
	{
		log_printf("Received unexpected DPLITE_MSGID_CONNECT_HOST from peer %u, in state %u",
			peer_id, (unsigned)(peer->state));
		return;
	}
	
	auto send_fail = [&peer](DWORD error, void *data, size_t data_size)
	{
		PacketSerialiser connect_host_fail(DPLITE_MSGID_CONNECT_HOST_FAIL);
		connect_host_fail.append_dword(error);
		
		if(data_size > 0)
		{
			connect_host_fail.append_data(data, data_size);
		}
		else{
			connect_host_fail.append_null();
		}
		
		peer->sq.send(SendQueue::SEND_PRI_MEDIUM,
			connect_host_fail,
			NULL,
			[](std::unique_lock<std::mutex> &l, HRESULT result) {});
		
		peer->state = Peer::PS_CLOSING;
	};
	
	if(state != STATE_HOSTING)
	{
		send_fail(DPNERR_NOTHOST, NULL, 0);
		return;
	}
	
	if(!pd.is_null(0) && pd.get_guid(0) != instance_guid)
	{
		send_fail(DPNERR_INVALIDINSTANCE, NULL, 0);
		return;
	}
	
	if(pd.get_guid(1) != application_guid)
	{
		send_fail(DPNERR_INVALIDAPPLICATION, NULL, 0);
		return;
	}
	
	std::wstring req_password = pd.is_null(2) ? L"" : pd.get_wstring(2);
	
	if(req_password != password)
	{
		send_fail(DPNERR_INVALIDPASSWORD, NULL, 0);
		return;
	}
	
	peer->player_name = pd.get_wstring(4);
	
	peer->player_data.clear();
	
	std::pair<const void*, size_t> player_data = pd.get_data(5);
	
	peer->player_data.reserve(player_data.second);
	peer->player_data.insert(peer->player_data.end(),
		(const unsigned char*)(player_data.first),
		(const unsigned char*)(player_data.first) + player_data.second);
	
	DPNMSG_INDICATE_CONNECT ic;
	memset(&ic, 0, sizeof(ic));
	
	ic.dwSize = sizeof(ic);
	
	if(!pd.is_null(3))
	{
		std::pair<const void*, size_t> d = pd.get_data(3);
		
		ic.pvUserConnectData     = (void*)(d.first); /* TODO: Take non-const copy? */
		ic.dwUserConnectDataSize = d.second;
	}
	
	struct sockaddr_in peer_sa;
	peer_sa.sin_family      = AF_INET;
	peer_sa.sin_addr.s_addr = peer->ip;
	peer_sa.sin_port        = htons(peer->port);
	
	DirectPlay8Address *peer_address = DirectPlay8Address::create_host_address(
		global_refcount, service_provider, (struct sockaddr*)(&peer_sa));
	
	ic.pAddressPlayer = peer_address;
	ic.pAddressDevice = NULL; /* TODO */
	
	peer->state = Peer::PS_INDICATING;
	
	l.unlock();
	HRESULT ic_result = message_handler(message_handler_ctx, DPN_MSGID_INDICATE_CONNECT, &ic);
	l.lock();
	
	peer_address->Release();
	
	std::vector<unsigned char> reply_data_buffer;
	if(ic.dwReplyDataSize > 0)
	{
		reply_data_buffer.reserve(ic.dwReplyDataSize);
		reply_data_buffer.insert(reply_data_buffer.end(),
			(const unsigned char*)(ic.pvReplyData),
			(const unsigned char*)(ic.pvReplyData) + ic.dwReplyDataSize);
		
		DPNMSG_RETURN_BUFFER rb;
		memset(&rb, 0, sizeof(rb));
		
		rb.dwSize        = sizeof(rb);
		rb.hResultCode   = S_OK; /* TODO: Size check */
		rb.pvBuffer      = ic.pvReplyData;
		rb.pvUserContext = ic.pvReplyContext;
		
		l.unlock();
		message_handler(message_handler_ctx, DPN_MSGID_RETURN_BUFFER, &rb);
		l.lock();
		
		ic.pvReplyData = reply_data_buffer.data();
	}
	
	RENEW_PEER_OR_RETURN();
	
	if(ic_result == DPN_OK)
	{
		/* Connection accepted by application. */
		
		peer->player_id  = next_player_id++;
		peer->player_ctx = ic.pvPlayerContext;
		
		player_to_peer_id[peer->player_id] = peer_id;
		
		peer->state = Peer::PS_CONNECTED;
		
		/* Send DPLITE_MSGID_GROUP_DESTROY for each destroyed group. */
		
		for(auto di = destroyed_groups.begin(); di != destroyed_groups.end(); ++di)
		{
			PacketSerialiser group_destroy(DPLITE_MSGID_GROUP_DESTROY);
			group_destroy.append_dword(*di);
			
			peer->sq.send(SendQueue::SEND_PRI_HIGH, group_destroy, NULL,
				[](std::unique_lock<std::mutex> &l, HRESULT result) {});
		}
		
		/* Send DPLITE_MSGID_GROUP_CREATE for each group. */
		
		std::set<DPNID> member_group_ids;
		
		for(auto gi = groups.begin(); gi != groups.end(); ++gi)
		{
			DPNID group_id = gi->first;
			Group *group   = &(gi->second);
			
			if(destroyed_groups.find(group_id) != destroyed_groups.end())
			{
				/* Group is being destroyed. */
				continue;
			}
			
			PacketSerialiser group_create(DPLITE_MSGID_GROUP_CREATE);
			group_create.append_dword(group_id);
			group_create.append_wstring(group->name);
			group_create.append_data(group->data.data(), group->data.size());
			
			peer->sq.send(SendQueue::SEND_PRI_HIGH, group_create, NULL,
				[](std::unique_lock<std::mutex> &l, HRESULT result) {});
			
			if(group->player_ids.find(local_player_id) != group->player_ids.end())
			{
				member_group_ids.insert(group_id);
			}
		}
		
		/* Send DPLITE_MSGID_CONNECT_HOST_OK. */
		
		PacketSerialiser connect_host_ok(DPLITE_MSGID_CONNECT_HOST_OK);
		connect_host_ok.append_guid(instance_guid);
		connect_host_ok.append_dword(host_player_id);
		connect_host_ok.append_dword(peer->player_id);
		
		connect_host_ok.append_dword(player_to_peer_id.size() - 1);
		
		for(auto pi = peers.begin(); pi != peers.end(); ++pi)
		{
			Peer *pip = pi->second;
			
			if(pip != peer && pip->state == Peer::PS_CONNECTED)
			{
				connect_host_ok.append_dword(pip->player_id);
				connect_host_ok.append_dword(pip->ip);
				connect_host_ok.append_dword(pip->port);
			}
		}
		
		if(ic.dwReplyDataSize > 0)
		{
			connect_host_ok.append_data(ic.pvReplyData, ic.dwReplyDataSize);
		}
		else{
			connect_host_ok.append_null();
		}
		
		connect_host_ok.append_wstring(local_player_name);
		connect_host_ok.append_data(local_player_data.data(), local_player_data.size());
		
		connect_host_ok.append_dword(max_players);
		connect_host_ok.append_wstring(session_name);
		connect_host_ok.append_wstring(password);
		connect_host_ok.append_data(application_data.data(), application_data.size());
		
		connect_host_ok.append_dword(member_group_ids.size());
		
		for(auto i = member_group_ids.begin(); i != member_group_ids.end(); ++i)
		{
			connect_host_ok.append_dword(*i);
		}
		
		peer->sq.send(SendQueue::SEND_PRI_MEDIUM,
			connect_host_ok,
			NULL,
			[](std::unique_lock<std::mutex> &l, HRESULT result) {});
		
		DPNMSG_CREATE_PLAYER cp;
		memset(&cp, 0, sizeof(cp));
		
		cp.dwSize          = sizeof(cp);
		cp.dpnidPlayer     = peer->player_id;
		cp.pvPlayerContext = peer->player_ctx;
		
		l.unlock();
		message_handler(message_handler_ctx, DPN_MSGID_CREATE_PLAYER, &cp);
		l.lock();
		
		RENEW_PEER_OR_RETURN();
		
		peer->player_ctx = cp.pvPlayerContext;
	}
	else{
		/* Connection rejected by application. */
		send_fail(DPNERR_HOSTREJECTEDCONNECTION, ic.pvReplyData, ic.dwReplyDataSize);
	}
}

/* Successful response to DPLITE_MSGID_CONNECT_HOST from host.
 *
 * GUID        - Instance GUID
 * DWORD       - Player ID of current host
 * DWORD       - Player ID assigned to receiving client
 * DWORD       - Number of other peers (total - 2)
 *
 * For each peer:
 *   DWORD - Player ID
 *   DWORD - IPv4 address (network byte order)
 *   DWORD - Port (host byte order)
 *
 * DATA | NULL - Response data
*/

void DirectPlay8Peer::handle_host_connect_ok(std::unique_lock<std::mutex> &l, unsigned int peer_id, const PacketDeserialiser &pd)
{
	Peer *peer = get_peer_by_peer_id(peer_id);
	assert(peer != NULL);
	
	if(peer->state != Peer::PS_REQUESTING_HOST)
	{
		log_printf("Received unexpected DPLITE_MSGID_CONNECT_HOST_OK from peer %u, in state %u",
			peer_id, (unsigned)(peer->state));
		return;
	}
	
	assert(state == STATE_CONNECTING_TO_HOST);
	
	instance_guid = pd.get_guid(0);
	
	host_player_id = pd.get_dword(1);
	
	peer->player_id = host_player_id;
	player_to_peer_id[peer->player_id] = peer_id;
	
	local_player_id = pd.get_dword(2);
	
	DWORD n_other_peers = pd.get_dword(3);
	
	for(DWORD n = 0; n < n_other_peers; ++n)
	{
		/* Spinning through the loop so we blow up early if malformed. */
		pd.get_dword(4 + (n * 3));
		pd.get_dword(5 + (n * 3));
		pd.get_dword(6 + (n * 3));
	}
	
	int after_peers_base = 4 + (n_other_peers * 3);
	
	connect_reply_data.clear();
	
	if(!pd.is_null(after_peers_base + 0))
	{
		std::pair<const void*, size_t> d = pd.get_data(after_peers_base + 0);
		
		if(d.second > 0)
		{
			connect_reply_data.reserve(d.second);
			connect_reply_data.insert(connect_reply_data.end(),
				(unsigned const char*)(d.first),
				(unsigned const char*)(d.first) + d.second);
		}
	}
	
	peer->player_name = pd.get_wstring(after_peers_base + 1);
	
	peer->player_data.clear();
	
	std::pair<const void*, size_t> player_data = pd.get_data(after_peers_base + 2);
	
	peer->player_data.reserve(player_data.second);
	peer->player_data.insert(peer->player_data.end(),
		(const unsigned char*)(player_data.first),
		(const unsigned char*)(player_data.first) + player_data.second);
	
	max_players  = pd.get_dword(after_peers_base + 3);
	session_name = pd.get_wstring(after_peers_base + 4);
	password     = pd.get_wstring(after_peers_base + 5);
	
	std::pair<const void*, size_t> application_data = pd.get_data(after_peers_base + 6);
	
	DWORD peer_group_count = pd.get_dword(after_peers_base + 7);
	std::set<DPNID> peer_groups;
	
	for(DWORD i = 0; i < peer_group_count; ++i)
	{
		peer_groups.insert(pd.get_dword(after_peers_base + 8 + i));
	}
	
	this->application_data.clear();
	this->application_data.insert(this->application_data.end(),
		(const unsigned char*)(application_data.first),
		(const unsigned char*)(application_data.first) + application_data.second);
	
	peer->state = Peer::PS_CONNECTED;
	
	state = STATE_CONNECTING_TO_PEERS;
	
	{
		DPNMSG_CREATE_PLAYER cp;
		memset(&cp, 0, sizeof(cp));
		
		cp.dwSize          = sizeof(cp);
		cp.dpnidPlayer     = local_player_id;
		cp.pvPlayerContext = local_player_ctx;
		
		l.unlock();
		message_handler(message_handler_ctx, DPN_MSGID_CREATE_PLAYER, &cp);
		l.lock();
		
		local_player_ctx = cp.pvPlayerContext;
	}
	
	{
		DPNMSG_CREATE_PLAYER cp;
		memset(&cp, 0, sizeof(cp));
		
		cp.dwSize          = sizeof(cp);
		cp.dpnidPlayer     = peer->player_id;
		cp.pvPlayerContext = NULL;
		
		l.unlock();
		message_handler(message_handler_ctx, DPN_MSGID_CREATE_PLAYER, &cp);
		l.lock();
		
		RENEW_PEER_OR_RETURN();
		
		peer->player_ctx = cp.pvPlayerContext;
	}
	
	for(auto g = peer_groups.begin(); g != peer_groups.end(); ++g)
	{
		DPNID group_id = *g;
		
		if(destroyed_groups.find(group_id) != destroyed_groups.end())
		{
			/* Group is already in the process of being destroyed. */
			continue;
		}
		
		Group *group = get_group_by_id(group_id);
		if(group == NULL)
		{
			/* Unknown group ID... very rarely normal. */
			continue;
		}
		
		if(group->player_ids.find(peer->player_id) != group->player_ids.end())
		{
			/* Already in group... somehow?! */
			continue;
		}
		
		group->player_ids.insert(peer->player_id);
		
		DPNMSG_ADD_PLAYER_TO_GROUP ap;
		memset(&ap, 0, sizeof(ap));
		
		ap.dwSize          = sizeof(ap);
		ap.dpnidGroup      = group_id;
		ap.pvGroupContext  = group->ctx;
		ap.dpnidPlayer     = peer->player_id;
		ap.pvPlayerContext = peer->player_ctx;
		
		l.unlock();
		message_handler(message_handler_ctx, DPN_MSGID_ADD_PLAYER_TO_GROUP, &ap);
		l.lock();
		
		RENEW_PEER_OR_RETURN();
	}
	
	for(DWORD n = 0; n < n_other_peers; ++n)
	{
		DPNID    player_id     = pd.get_dword(4 + (n * 3));
		uint32_t player_ipaddr = pd.get_dword(5 + (n * 3));
		uint16_t player_port   = pd.get_dword(6 + (n * 3));
		
		if(!peer_connect(Peer::PS_CONNECTING_PEER, player_ipaddr, player_port, player_id))
		{
			connect_fail(l, DPNERR_PLAYERNOTREACHABLE, NULL, 0);
			return;
		}
	}
	
	connect_check(l);
}

void DirectPlay8Peer::handle_host_connect_fail(std::unique_lock<std::mutex> &l, unsigned int peer_id, const PacketDeserialiser &pd)
{
	Peer *peer = get_peer_by_peer_id(peer_id);
	assert(peer != NULL);
	
	if(peer->state != Peer::PS_REQUESTING_HOST)
	{
		log_printf("Received unexpected DPLITE_MSGID_CONNECT_HOST_FAIL from peer %u, in state %u",
			peer_id, (unsigned)(peer->state));
		return;
	}
	
	assert(state == STATE_CONNECTING_TO_HOST);
	
	DWORD       hResultCode                = DPNERR_GENERIC;
	const void *pvApplicationReplyData     = NULL;
	DWORD       dwApplicationReplyDataSize = 0;
	
	try {
		hResultCode = pd.get_dword(0);
		
		if(!pd.is_null(1))
		{
			std::pair<const void*, size_t> d = pd.get_data(1);
			
			if(d.second > 0)
			{
				pvApplicationReplyData     = d.first;
				dwApplicationReplyDataSize = d.second;
			}
		}
	}
	catch(const PacketDeserialiser::Error &e)
	{
		log_printf("Received invalid DPLITE_MSGID_CONNECT_HOST_FAIL from peer %u: %s",
			peer_id, e.what());
	}
	
	connect_fail(l, hResultCode, pvApplicationReplyData, dwApplicationReplyDataSize);
}

void DirectPlay8Peer::handle_connect_peer(std::unique_lock<std::mutex> &l, unsigned int peer_id, const PacketDeserialiser &pd)
{
	Peer *peer = get_peer_by_peer_id(peer_id);
	assert(peer != NULL);
	
	if(peer->state != Peer::PS_ACCEPTED)
	{
		log_printf("Received unexpected DPLITE_MSGID_CONNECT_PEER from peer %u, in state %u",
			peer_id, (unsigned)(peer->state));
		return;
	}
	
	auto send_fail = [&peer](DWORD error)
	{
		PacketSerialiser connect_peer_fail(DPLITE_MSGID_CONNECT_PEER_FAIL);
		connect_peer_fail.append_dword(error);
		
		peer->sq.send(SendQueue::SEND_PRI_HIGH,
			connect_peer_fail,
			NULL,
			[](std::unique_lock<std::mutex> &l, HRESULT result) {});
		
		peer->state = Peer::PS_CLOSING;
	};
	
	if(state != STATE_CONNECTED)
	{
		send_fail(DPNERR_GENERIC);
		return;
	}
	
	if(pd.get_guid(0) != instance_guid)
	{
		send_fail(DPNERR_INVALIDINSTANCE);
		return;
	}
	
	if(pd.get_guid(1) != application_guid)
	{
		send_fail(DPNERR_INVALIDAPPLICATION);
		return;
	}
	
	if(pd.get_wstring(2) != password)
	{
		send_fail(DPNERR_INVALIDPASSWORD);
		return;
	}
	
	peer->player_id = pd.get_dword(3);
	peer->player_name = pd.get_wstring(4);
	
	peer->player_data.clear();
	
	std::pair<const void*, size_t> player_data = pd.get_data(5);
	
	peer->player_data.reserve(player_data.second);
	peer->player_data.insert(peer->player_data.end(),
		(const unsigned char*)(player_data.first),
		(const unsigned char*)(player_data.first) + player_data.second);
	
	if(player_to_peer_id.find(peer->player_id) != player_to_peer_id.end())
	{
		log_printf("Rejected DPLITE_MSGID_CONNECT_PEER with already-known Player ID %u", (unsigned)(peer->player_id));
		
		send_fail(DPNERR_ALREADYCONNECTED);
		return;
	}
	
	player_to_peer_id[peer->player_id] = peer_id;
	
	peer->state = Peer::PS_CONNECTED;
	
	/* Send DPLITE_MSGID_GROUP_DESTROY for each destroyed group. */
	
	for(auto di = destroyed_groups.begin(); di != destroyed_groups.end(); ++di)
	{
		PacketSerialiser group_destroy(DPLITE_MSGID_GROUP_DESTROY);
		group_destroy.append_dword(*di);
		
		peer->sq.send(SendQueue::SEND_PRI_HIGH, group_destroy, NULL,
			[](std::unique_lock<std::mutex> &l, HRESULT result) {});
	}
	
	/* Send DPLITE_MSGID_GROUP_CREATE for each group. */
	
	std::set<DPNID> member_group_ids;
	
	for(auto gi = groups.begin(); gi != groups.end(); ++gi)
	{
		DPNID group_id = gi->first;
		Group *group   = &(gi->second);
		
		if(destroyed_groups.find(group_id) != destroyed_groups.end())
		{
			/* Group is being destroyed. */
			continue;
		}
		
		PacketSerialiser group_create(DPLITE_MSGID_GROUP_CREATE);
		group_create.append_dword(group_id);
		group_create.append_wstring(group->name);
		group_create.append_data(group->data.data(), group->data.size());
		
		peer->sq.send(SendQueue::SEND_PRI_HIGH, group_create, NULL,
			[](std::unique_lock<std::mutex> &l, HRESULT result) {});
		
		if(group->player_ids.find(local_player_id) != group->player_ids.end())
		{
			member_group_ids.insert(group_id);
		}
	}
	
	/* Send DPLITE_MSGID_CONNECT_PEER_OK. */
	
	PacketSerialiser connect_peer_ok(DPLITE_MSGID_CONNECT_PEER_OK);
	connect_peer_ok.append_wstring(local_player_name);
	connect_peer_ok.append_data(local_player_data.data(), local_player_data.size());
	
	connect_peer_ok.append_dword(member_group_ids.size());
	
	for(auto i = member_group_ids.begin(); i != member_group_ids.end(); ++i)
	{
		connect_peer_ok.append_dword(*i);
	}
	
	peer->sq.send(SendQueue::SEND_PRI_HIGH,
		connect_peer_ok,
		NULL,
		[](std::unique_lock<std::mutex> &l, HRESULT result) {});
	
	DPNMSG_CREATE_PLAYER cp;
	memset(&cp, 0, sizeof(cp));
	
	cp.dwSize          = sizeof(cp);
	cp.dpnidPlayer     = peer->player_id;
	cp.pvPlayerContext = NULL;
	
	l.unlock();
	message_handler(message_handler_ctx, DPN_MSGID_CREATE_PLAYER, &cp);
	l.lock();
	
	RENEW_PEER_OR_RETURN();
	
	peer->player_ctx = cp.pvPlayerContext;
}

void DirectPlay8Peer::handle_connect_peer_ok(std::unique_lock<std::mutex> &l, unsigned int peer_id, const PacketDeserialiser &pd)
{
	Peer *peer = get_peer_by_peer_id(peer_id);
	assert(peer != NULL);
	
	if(peer->state != Peer::PS_REQUESTING_PEER)
	{
		log_printf("Received unexpected DPLITE_MSGID_CONNECT_PEER_OK from peer %u, in state %u",
			peer_id, (unsigned)(peer->state));
		return;
	}
	
	assert(state == STATE_CONNECTING_TO_PEERS);
	
	peer->player_name = pd.get_wstring(0);
	
	peer->player_data.clear();
	
	std::pair<const void*, size_t> player_data = pd.get_data(1);
	
	peer->player_data.reserve(player_data.second);
	peer->player_data.insert(peer->player_data.end(),
		(const unsigned char*)(player_data.first),
		(const unsigned char*)(player_data.first) + player_data.second);
	
	DWORD peer_group_count = pd.get_dword(2);
	std::set<DPNID> peer_groups;
	
	for(DWORD i = 0; i < peer_group_count; ++i)
	{
		peer_groups.insert(pd.get_dword(3 + i));
	}
	
	peer->state = Peer::PS_CONNECTED;
	
	/* player_id initialised in handling of DPLITE_MSGID_CONNECT_HOST_OK. */
	player_to_peer_id[peer->player_id] = peer_id;
	
	{
		DPNMSG_CREATE_PLAYER cp;
		memset(&cp, 0, sizeof(cp));
		
		cp.dwSize          = sizeof(cp);
		cp.dpnidPlayer     = peer->player_id;
		cp.pvPlayerContext = NULL;
		
		l.unlock();
		message_handler(message_handler_ctx, DPN_MSGID_CREATE_PLAYER, &cp);
		l.lock();
		
		RENEW_PEER_OR_RETURN();
		
		peer->player_ctx = cp.pvPlayerContext;
	}
	
	for(auto g = peer_groups.begin(); g != peer_groups.end(); ++g)
	{
		DPNID group_id = *g;
		
		if(destroyed_groups.find(group_id) != destroyed_groups.end())
		{
			/* Group is already in the process of being destroyed. */
			continue;
		}
		
		Group *group = get_group_by_id(group_id);
		if(group == NULL)
		{
			/* Unknown group ID... very rarely normal. */
			continue;
		}
		
		if(group->player_ids.find(peer->player_id) != group->player_ids.end())
		{
			/* Already in group... somehow?! */
			continue;
		}
		
		group->player_ids.insert(peer->player_id);
		
		DPNMSG_ADD_PLAYER_TO_GROUP ap;
		memset(&ap, 0, sizeof(ap));
		
		ap.dwSize          = sizeof(ap);
		ap.dpnidGroup      = group_id;
		ap.pvGroupContext  = group->ctx;
		ap.dpnidPlayer     = peer->player_id;
		ap.pvPlayerContext = peer->player_ctx;
		
		l.unlock();
		message_handler(message_handler_ctx, DPN_MSGID_ADD_PLAYER_TO_GROUP, &ap);
		l.lock();
		
		RENEW_PEER_OR_RETURN();
	}
	
	connect_check(l);
}

void DirectPlay8Peer::handle_connect_peer_fail(std::unique_lock<std::mutex> &l, unsigned int peer_id, const PacketDeserialiser &pd)
{
	Peer *peer = get_peer_by_peer_id(peer_id);
	assert(peer != NULL);
	
	if(peer->state != Peer::PS_REQUESTING_PEER)
	{
		log_printf("Received unexpected DPLITE_MSGID_CONNECT_PEER_FAIL from peer %u, in state %u",
			peer_id, (unsigned)(peer->state));
		return;
	}
	
	assert(state == STATE_CONNECTING_TO_PEERS);
	
	DWORD hResultCode = DPNERR_GENERIC;
	
	try {
		hResultCode = pd.get_dword(0);
	}
	catch(const PacketDeserialiser::Error &e)
	{
		log_printf("Received invalid DPLITE_MSGID_CONNECT_PEER_FAIL from peer %u: %s",
			peer_id, e.what());
	}
	
	connect_fail(l, DPNERR_PLAYERNOTREACHABLE, NULL, 0);
}

void DirectPlay8Peer::handle_message(std::unique_lock<std::mutex> &l, const PacketDeserialiser &pd)
{
	try {
		DWORD from_player_id = pd.get_dword(0);
		std::pair<const void*, size_t> payload = pd.get_data(1);
		DWORD flags = pd.get_dword(2);
		
		Peer *peer = get_peer_by_player_id(from_player_id);
		if(peer == NULL)
		{
			return;
		}
		
		unsigned char *payload_copy = new unsigned char[payload.second];
		memcpy(payload_copy, payload.first, payload.second);
		
		DPNMSG_RECEIVE r;
		memset(&r, 0, sizeof(r));
		
		static_assert(sizeof(DPNHANDLE) >= sizeof(unsigned char*),
			"DPNHANDLE must be large enough to take a pointer");
		
		r.dwSize            = sizeof(r);
		r.dpnidSender       = from_player_id;
		r.pvPlayerContext   = peer->player_ctx;
		r.pReceiveData      = payload_copy;
		r.dwReceiveDataSize = payload.second;
		r.hBufferHandle     = (DPNHANDLE)(payload_copy);
		// r.dwReceiveFlags
		
		l.unlock();
		HRESULT r_result = message_handler(message_handler_ctx, DPN_MSGID_RECEIVE, &r);
		l.lock();
		
		if(r_result != DPNSUCCESS_PENDING)
		{
			delete[] payload_copy;
		}
	}
	catch(const PacketDeserialiser::Error &e)
	{
		log_printf("Received invalid DPLITE_MSGID_MESSAGE: %s", e.what());
	}
}

void DirectPlay8Peer::handle_playerinfo(std::unique_lock<std::mutex> &l, unsigned int peer_id, const PacketDeserialiser &pd)
{
	Peer *peer = get_peer_by_peer_id(peer_id);
	assert(peer != NULL);
	
	try {
		DPNID player_id = pd.get_dword(0);
		std::wstring name = pd.get_wstring(1);
		std::pair<const void*, size_t> data = pd.get_data(2);
		DWORD ack_id = pd.get_dword(3);
		
		if(peer->state != Peer::PS_CONNECTED)
		{
			log_printf("Received unexpected DPLITE_MSGID_PLAYERINFO from peer %u, in state %u",
				peer_id, (unsigned)(peer->state));
			return;
		}
		
		if(player_id != peer->player_id)
		{
			log_printf("Received unexpected DPLITE_MSGID_PLAYERINFO from peer %u for player %u",
				peer_id, (unsigned)(player_id));
			return;
		}
		
		peer->player_name = name;
		
		peer->player_data.clear();
		peer->player_data.insert(peer->player_data.end(),
			(const unsigned char*)(data.first),
			(const unsigned char*)(data.first) + data.second);
		
		/* TODO: Should we send DPLITE_MSGID_ACK before or after the callback
		 * completes?
		*/
		
		PacketSerialiser ack(DPLITE_MSGID_ACK);
		ack.append_dword(ack_id);
		ack.append_dword(S_OK);
		ack.append_data(NULL, 0);
		
		peer->sq.send(SendQueue::SEND_PRI_HIGH, ack, NULL,
			[](std::unique_lock<std::mutex> &l, HRESULT s_result) {});
		
		DPNMSG_PEER_INFO pi;
		memset(&pi, 0, sizeof(pi));
		
		pi.dwSize          = sizeof(pi);
		pi.dpnidPeer       = peer->player_id;
		pi.pvPlayerContext = peer->player_ctx;
		
		l.unlock();
		message_handler(message_handler_ctx, DPN_MSGID_PEER_INFO, &pi);
		l.lock();
	}
	catch(const PacketDeserialiser::Error &e)
	{
		log_printf("Received invalid DPLITE_MSGID_PLAYERINFO from peer %u: %s",
			peer_id, e.what());
	}
}

void DirectPlay8Peer::handle_ack(std::unique_lock<std::mutex> &l, unsigned int peer_id, const PacketDeserialiser &pd)
{
	Peer *peer = get_peer_by_peer_id(peer_id);
	assert(peer != NULL);
	
	try {
		DWORD                          ack_id = pd.get_dword(0);
		HRESULT                        result = pd.get_dword(1);
		std::pair<const void*, size_t> data   = pd.get_data(2);
		
		auto ai = peer->pending_acks.find(ack_id);
		if(ai == peer->pending_acks.end())
		{
			log_printf("Received DPLITE_MSGID_CONNECT_HOST_FAIL with unknown ID %u from peer %u: %s",
				(unsigned)(ack_id), peer_id);
			return;
		}
		
		std::function<void(std::unique_lock<std::mutex>&, HRESULT, const void*, size_t)> callback = ai->second;
		peer->pending_acks.erase(ai);
		
		callback(l, result, data.first, data.second);
	}
	catch(const PacketDeserialiser::Error &e)
	{
		log_printf("Received invalid DPLITE_MSGID_ACK from peer %u: %s",
			peer_id, e.what());
	}
}

void DirectPlay8Peer::handle_appdesc(std::unique_lock<std::mutex> &l, unsigned int peer_id, const PacketDeserialiser &pd)
{
	Peer *peer = get_peer_by_peer_id(peer_id);
	assert(peer != NULL);
	
	try {
		DWORD                          max_players      = pd.get_dword(0);
		std::wstring                   session_name     = pd.get_wstring(1);
		std::wstring                   password         = pd.get_wstring(2);
		std::pair<const void*, size_t> application_data = pd.get_data(3);
		
		if(peer->state != Peer::PS_CONNECTED)
		{
			log_printf("Received unexpected DPLITE_MSGID_APPDESC from peer %u, in state %u",
				peer_id, (unsigned)(peer->state));
			return;
		}
		
		/* host_player_id must be initialised by this point, as the host is always the
		 * first peer to enter state PS_CONNECTED, initialising it in the process.
		*/
		
		if(peer->player_id != host_player_id)
		{
			log_printf("Received unexpected DPLITE_MSGID_CONNECT_HOST_FAIL from non-host peer %u",
				peer_id);
			return;
		}
		
		this->max_players  = max_players;
		this->session_name = session_name;
		this->password     = password;
		
		this->application_data.clear();
		this->application_data.insert(this->application_data.end(),
			(const unsigned char*)(application_data.first),
			(const unsigned char*)(application_data.first) + application_data.second);
		
		/* DPN_MSGID_APPLICATION_DESC has no accompanying structure.
		 * The application must call GetApplicationDesc() to obtain the
		 * new data.
		*/
		
		l.unlock();
		message_handler(message_handler_ctx, DPN_MSGID_APPLICATION_DESC, NULL);
		l.lock();
	}
	catch(const PacketDeserialiser::Error &e)
	{
		log_printf("Received invalid DPLITE_MSGID_APPDESC from peer %u: %s",
			peer_id, e.what());
	}
}

void DirectPlay8Peer::handle_destroy_peer(std::unique_lock<std::mutex> &l, unsigned int peer_id, const PacketDeserialiser &pd)
{
	Peer *peer = get_peer_by_peer_id(peer_id);
	assert(peer != NULL);
	
	try {
		DWORD destroy_player_id = pd.get_dword(0);
		
		if(peer->state != Peer::PS_CONNECTED)
		{
			log_printf("Received unexpected DPLITE_MSGID_DESTROY_PEER from peer %u, in state %u",
				peer_id, (unsigned)(peer->state));
			return;
		}
		
		/* host_player_id must be initialised by this point, as the host is always the
		 * first peer to enter state PS_CONNECTED, initialising it in the process.
		*/
		
		if(peer->player_id != host_player_id && peer->player_id != destroy_player_id)
		{
			log_printf("Received unexpected DPLITE_MSGID_DESTROY_PEER from non-host peer %u",
				peer_id);
			return;
		}
		
		if(destroy_player_id == local_player_id)
		{
			/* The host called DestroyPeer() on US! */
			
			std::pair<const void*, size_t> terminate_data = pd.get_data(1);
			
			DPNMSG_TERMINATE_SESSION ts;
			memset(&ts, 0, sizeof(ts));
			
			ts.dwSize              = sizeof(DPNMSG_TERMINATE_SESSION);
			ts.hResultCode         = DPNERR_HOSTTERMINATEDSESSION;
			ts.pvTerminateData     = (void*)(terminate_data.first); /* TODO: Make non-const copy? */
			ts.dwTerminateDataSize = terminate_data.second;
			
			state = STATE_TERMINATED;
			
			l.unlock();
			message_handler(message_handler_ctx, DPN_MSGID_TERMINATE_SESSION, &ts);
			l.lock();
			
			dispatch_destroy_player(l, local_player_id, local_player_ctx, DPNDESTROYPLAYERREASON_SESSIONTERMINATED);
			
			/* Forward destroyed player notification to all peers so they may raise
			 * DPNMSG_DESTROY_PLAYER with the correct dwReason.
			*/
			
			for(auto pi = peers.begin(); pi != peers.end(); ++pi)
			{
				unsigned int peer_id = pi->first;
				Peer *peer = pi->second;
				
				if(peer->state == Peer::PS_CONNECTED)
				{
					PacketSerialiser destroy_peer(DPLITE_MSGID_DESTROY_PEER);
					destroy_peer.append_dword(local_player_id);
					
					peer->sq.send(SendQueue::SEND_PRI_HIGH, destroy_peer, NULL, [](std::unique_lock<std::mutex> &l, HRESULT result) {});
				}
			}
			
			peer_shutdown_all(l, DPNERR_HOSTTERMINATEDSESSION, DPNDESTROYPLAYERREASON_SESSIONTERMINATED);
			
			group_destroy_all(l, DPNDESTROYGROUPREASON_SESSIONTERMINATED);
		}
		else{
			/* The host called DestroyPeer() on another peer in the session.
			 *
			 * Reciving an unknown player ID here is normal; the host notifies all
			 * peers of the DestroyPeer() and whichever end processes it first will
			 * close the connection.
			*/
			
			auto destroy_peer_id = player_to_peer_id.find(destroy_player_id);
			if(destroy_peer_id != player_to_peer_id.end())
			{
				peer_destroy(l, destroy_peer_id->second, DPNERR_CONNECTIONLOST, DPNDESTROYPLAYERREASON_HOSTDESTROYEDPLAYER);
			}
		}
	}
	catch(const PacketDeserialiser::Error &e)
	{
		log_printf("Received invalid DPLITE_MSGID_DESTROY_PEER from peer %u: %s",
			peer_id, e.what());
	}
}

void DirectPlay8Peer::handle_terminate_session(std::unique_lock<std::mutex> &l, unsigned int peer_id, const PacketDeserialiser &pd)
{
	Peer *peer = get_peer_by_peer_id(peer_id);
	assert(peer != NULL);
	
	try {
		std::pair<const void*, size_t> terminate_data = pd.get_data(0);
		
		if(peer->state != Peer::PS_CONNECTED)
		{
			log_printf("Received unexpected DPLITE_MSGID_TERMINATE_SESSION from peer %u, in state %u",
				peer_id, (unsigned)(peer->state));
			return;
		}
		
		/* host_player_id must be initialised by this point, as the host is always the
		 * first peer to enter state PS_CONNECTED, initialising it in the process.
		*/
		
		if(peer->player_id != host_player_id)
		{
			log_printf("Received unexpected DPLITE_MSGID_TERMINATE_SESSION from non-host peer %u",
				peer_id);
			return;
		}
		
		state = STATE_TERMINATED;
		
		DPNMSG_TERMINATE_SESSION ts;
		memset(&ts, 0, sizeof(ts));
		
		ts.dwSize              = sizeof(DPNMSG_TERMINATE_SESSION);
		ts.hResultCode         = DPNERR_HOSTTERMINATEDSESSION;
		ts.pvTerminateData     = (void*)(terminate_data.first); /* TODO: Make non-const copy? */
		ts.dwTerminateDataSize = terminate_data.second;
		
		l.unlock();
		message_handler(message_handler_ctx, DPN_MSGID_TERMINATE_SESSION, &ts);
		l.lock();
		
		dispatch_destroy_player(l, local_player_id, local_player_ctx, DPNDESTROYPLAYERREASON_SESSIONTERMINATED);
		
		peer_shutdown_all(l, DPNERR_HOSTTERMINATEDSESSION, DPNDESTROYPLAYERREASON_SESSIONTERMINATED);
		
		group_destroy_all(l, DPNDESTROYGROUPREASON_SESSIONTERMINATED);
	}
	catch(const PacketDeserialiser::Error &e)
	{
		log_printf("Received invalid DPLITE_MSGID_TERMINATE_SESSION from peer %u: %s",
			peer_id, e.what());
	}
}

void DirectPlay8Peer::handle_group_allocate(std::unique_lock<std::mutex> &l, unsigned int peer_id, const PacketDeserialiser &pd)
{
	Peer *peer = get_peer_by_peer_id(peer_id);
	assert(peer != NULL);
	
	try {
		DWORD ack_id = pd.get_dword(0);
		
		if(peer->state != Peer::PS_CONNECTED)
		{
			log_printf("Received unexpected DPLITE_MSGID_GROUP_ALLOCATE from peer %u, in state %u",
				peer_id, (unsigned)(peer->state));
			return;
		}
		
		DPNID group_id = next_player_id++;
		
		peer->send_ack(ack_id, S_OK, &group_id, sizeof(group_id));
	}
	catch(const PacketDeserialiser::Error &e)
	{
		log_printf("Received invalid DPLITE_MSGID_GROUP_ALLOCATE from peer %u: %s",
			peer_id, e.what());
	}
}

void DirectPlay8Peer::handle_group_create(std::unique_lock<std::mutex> &l, unsigned int peer_id, const PacketDeserialiser &pd)
{
	Peer *peer = get_peer_by_peer_id(peer_id);
	assert(peer != NULL);
	
	try {
		DPNID                          group_id   = pd.get_dword(0);
		std::wstring                   group_name = pd.get_wstring(1);
		std::pair<const void*, size_t> group_data = pd.get_data(2);
		
		if(peer->state != Peer::PS_CONNECTED
			&& peer->state != Peer::PS_REQUESTING_HOST
			&& peer->state != Peer::PS_REQUESTING_PEER)
		{
			log_printf("Received unexpected DPLITE_MSGID_GROUP_CREATE from peer %u, in state %u",
				peer_id, (unsigned)(peer->state));
			return;
		}
		
		if(groups.find(group_id) != groups.end())
		{
			/* Group already exists, probably normal. */
			return;
		}
		
		if(destroyed_groups.find(group_id) != destroyed_groups.end())
		{
			/* Group has already been destroyed. */
			return;
		}
		
		groups.emplace(
				std::piecewise_construct,
				std::forward_as_tuple(group_id),
				std::forward_as_tuple(group_name, group_data.first, group_data.second));
		
		/* Raise DPNMSG_CREATE_GROUP for the new group. */
		
		DPNMSG_CREATE_GROUP cg;
		memset(&cg, 0, sizeof(cg));
		
		cg.dwSize         = sizeof(cg);
		cg.dpnidGroup     = group_id;
		cg.dpnidOwner     = 0;
		cg.pvGroupContext = NULL;
		cg.pvOwnerContext = peer->player_ctx;
		
		l.unlock();
		message_handler(message_handler_ctx, DPN_MSGID_CREATE_GROUP, &cg);
		l.lock();
		
		Group *group = get_group_by_id(group_id);
		if(group != NULL)
		{
			group->ctx = cg.pvGroupContext;
		}
	}
	catch(const PacketDeserialiser::Error &e)
	{
		log_printf("Received invalid DPLITE_MSGID_TERMINATE_SESSION from peer %u: %s",
			peer_id, e.what());
	}
}

void DirectPlay8Peer::handle_group_destroy(std::unique_lock<std::mutex> &l, unsigned int peer_id, const PacketDeserialiser &pd)
{
	Peer *peer = get_peer_by_peer_id(peer_id);
	assert(peer != NULL);
	
	try {
		DPNID group_id = pd.get_dword(0);
		
		if(peer->state != Peer::PS_CONNECTED
			&& peer->state != Peer::PS_REQUESTING_HOST
			&& peer->state != Peer::PS_REQUESTING_PEER)
		{
			log_printf("Received unexpected DPLITE_MSGID_GROUP_DESTROY from peer %u, in state %u",
				peer_id, (unsigned)(peer->state));
			return;
		}
		
		if(destroyed_groups.find(group_id) != destroyed_groups.end())
		{
			/* Group is already in the process of being destroyed. */
			return;
		}
		
		destroyed_groups.insert(group_id);
		
		Group *group = get_group_by_id(group_id);
		if(group != NULL)
		{
			DPNMSG_DESTROY_GROUP dg;
			memset(&dg, 0, sizeof(dg));
			
			dg.dwSize         = sizeof(dg);
			dg.dpnidGroup     = group_id;
			dg.pvGroupContext = group->ctx;
			dg.dwReason       = DPNDESTROYGROUPREASON_NORMAL;
			
			l.unlock();
			message_handler(message_handler_ctx, DPN_MSGID_DESTROY_GROUP, &dg);
			l.lock();
			
			groups.erase(group_id);
		}
	}
	catch(const PacketDeserialiser::Error &e)
	{
		log_printf("Received invalid DPLITE_MSGID_GROUP_DESTROY from peer %u: %s",
			peer_id, e.what());
	}
}

void DirectPlay8Peer::handle_group_join(std::unique_lock<std::mutex> &l, unsigned int peer_id, const PacketDeserialiser &pd)
{
	Peer *peer = get_peer_by_peer_id(peer_id);
	assert(peer != NULL);
	
	try {
		DPNID                          group_id   = pd.get_dword(0);
		DWORD                          ack_id     = pd.get_dword(1);
		std::wstring                   group_name = pd.get_wstring(2);
		std::pair<const void*, size_t> group_data = pd.get_data(3);
		
		if(peer->state != Peer::PS_CONNECTED)
		{
			log_printf("Received unexpected DPLITE_MSGID_GROUP_JOIN from peer %u, in state %u",
				peer_id, (unsigned)(peer->state));
			return;
		}
		
		if(destroyed_groups.find(group_id) != destroyed_groups.end())
		{
			/* Group is already in the process of being destroyed. */
			peer->send_ack(ack_id, DPNERR_INVALIDGROUP);
			return;
		}
		
		Group *group = get_group_by_id(group_id);
		if(group == NULL)
		{
			/* Unknown group ID... we must create it! */
			
			groups.emplace(
				std::piecewise_construct,
				std::forward_as_tuple(group_id),
				std::forward_as_tuple(group_name, group_data.first, group_data.second));
			
			/* Raise DPNMSG_CREATE_GROUP for the new group. */
			
			DPNMSG_CREATE_GROUP cg;
			memset(&cg, 0, sizeof(cg));
			
			cg.dwSize         = sizeof(cg);
			cg.dpnidGroup     = group_id;
			cg.dpnidOwner     = 0;
			cg.pvGroupContext = NULL;
			cg.pvOwnerContext = peer->player_ctx;
			
			l.unlock();
			message_handler(message_handler_ctx, DPN_MSGID_CREATE_GROUP, &cg);
			l.lock();
			
			Group *group = get_group_by_id(group_id);
			if(group == NULL)
			{
				/* Group got destroyed already?! */
				peer->send_ack(ack_id, DPNERR_INVALIDGROUP);
				return;
			}
			
			group->ctx = cg.pvGroupContext;
		}
		
		if(group->player_ids.find(local_player_id) != group->player_ids.end())
		{
			/* Already in group. */
			peer->send_ack(ack_id, DPNERR_PLAYERALREADYINGROUP);
			return;
		}
		
		PacketSerialiser group_joined(DPLITE_MSGID_GROUP_JOINED);
		group_joined.append_dword(group_id);
		group_joined.append_wstring(group->name);
		group_joined.append_data(group->data.data(), group->data.size());
		
		for(auto p = peers.begin(); p != peers.end(); ++p)
		{
			Peer *peer = p->second;
			
			if(peer->state == Peer::PS_CONNECTED)
			{
				peer->sq.send(SendQueue::SEND_PRI_HIGH, group_joined, NULL,
					[](std::unique_lock<std::mutex> &l, HRESULT result) {});
			}
		}
		
		/* If the peer hasn't gone away, ack it. */
		peer = get_peer_by_peer_id(peer_id);
		if(peer != NULL)
		{
			peer->send_ack(ack_id, S_OK);
		}
		
		group->player_ids.insert(local_player_id);
		
		DPNMSG_ADD_PLAYER_TO_GROUP ap;
		memset(&ap, 0, sizeof(ap));
		
		ap.dwSize          = sizeof(ap);
		ap.dpnidGroup      = group_id;
		ap.pvGroupContext  = group->ctx;
		ap.dpnidPlayer     = local_player_id;
		ap.pvPlayerContext = local_player_ctx;
		
		l.unlock();
		message_handler(message_handler_ctx, DPN_MSGID_ADD_PLAYER_TO_GROUP, &ap);
		l.lock();
	}
	catch(const PacketDeserialiser::Error &e)
	{
		log_printf("Received invalid DPLITE_MSGID_GROUP_JOIN from peer %u: %s",
			peer_id, e.what());
	}
}

void DirectPlay8Peer::handle_group_joined(std::unique_lock<std::mutex> &l, unsigned int peer_id, const PacketDeserialiser &pd)
{
	Peer *peer = get_peer_by_peer_id(peer_id);
	assert(peer != NULL);
	
	try {
		DPNID                          group_id   = pd.get_dword(0);
		std::wstring                   group_name = pd.get_wstring(1);
		std::pair<const void*, size_t> group_data = pd.get_data(2);
		
		if(peer->state != Peer::PS_CONNECTED)
		{
			log_printf("Received unexpected DPLITE_MSGID_GROUP_JOINED from peer %u, in state %u",
				peer_id, (unsigned)(peer->state));
			return;
		}
		
		if(destroyed_groups.find(group_id) != destroyed_groups.end())
		{
			/* Group is already in the process of being destroyed. */
			return;
		}
		
		Group *group = get_group_by_id(group_id);
		if(group == NULL)
		{
			/* Unknown group ID... we must create it! */
			
			groups.emplace(
				std::piecewise_construct,
				std::forward_as_tuple(group_id),
				std::forward_as_tuple(group_name, group_data.first, group_data.second));
			
			/* Raise DPNMSG_CREATE_GROUP for the new group. */
			
			DPNMSG_CREATE_GROUP cg;
			memset(&cg, 0, sizeof(cg));
			
			cg.dwSize         = sizeof(cg);
			cg.dpnidGroup     = group_id;
			cg.dpnidOwner     = 0;
			cg.pvGroupContext = NULL;
			cg.pvOwnerContext = peer->player_ctx;
			
			l.unlock();
			message_handler(message_handler_ctx, DPN_MSGID_CREATE_GROUP, &cg);
			l.lock();
			
			Group *group = get_group_by_id(group_id);
			if(group == NULL)
			{
				/* Group got destroyed already?! */
				return;
			}
			
			group->ctx = cg.pvGroupContext;
		}
		
		if(group->player_ids.find(peer->player_id) != group->player_ids.end())
		{
			/* Already in group. */
			log_printf("Received DPLITE_MSGID_GROUP_JOINED from peer %u for group %u, but it is already in group",
				peer_id, (unsigned)(group_id));
			return;
		}
		
		group->player_ids.insert(peer->player_id);
		
		DPNMSG_ADD_PLAYER_TO_GROUP ap;
		memset(&ap, 0, sizeof(ap));
		
		ap.dwSize          = sizeof(ap);
		ap.dpnidGroup      = group_id;
		ap.pvGroupContext  = group->ctx;
		ap.dpnidPlayer     = peer->player_id;
		ap.pvPlayerContext = peer->player_ctx;
		
		l.unlock();
		message_handler(message_handler_ctx, DPN_MSGID_ADD_PLAYER_TO_GROUP, &ap);
		l.lock();
	}
	catch(const PacketDeserialiser::Error &e)
	{
		log_printf("Received invalid DPLITE_MSGID_GROUP_JOINED from peer %u: %s",
			peer_id, e.what());
	}
}

void DirectPlay8Peer::handle_group_leave(std::unique_lock<std::mutex> &l, unsigned int peer_id, const PacketDeserialiser &pd)
{
	Peer *peer = get_peer_by_peer_id(peer_id);
	assert(peer != NULL);
	
	try {
		DPNID group_id = pd.get_dword(0);
		DWORD ack_id   = pd.get_dword(1);
		
		if(peer->state != Peer::PS_CONNECTED)
		{
			log_printf("Received unexpected DPLITE_MSGID_GROUP_LEAVE from peer %u, in state %u",
				peer_id, (unsigned)(peer->state));
			return;
		}
		
		if(destroyed_groups.find(group_id) != destroyed_groups.end())
		{
			/* Group is already in the process of being destroyed. */
			peer->send_ack(ack_id, DPNERR_INVALIDGROUP);
			return;
		}
		
		Group *group = get_group_by_id(group_id);
		if(group == NULL)
		{
			/* Unknown group ID, probably normal. */
			return;
		}
		
		if(group->player_ids.find(local_player_id) == group->player_ids.end())
		{
			/* Already in group. */
			peer->send_ack(ack_id, DPNERR_PLAYERNOTINGROUP);
			return;
		}
		
		PacketSerialiser group_left(DPLITE_MSGID_GROUP_LEFT);
		group_left.append_dword(group_id);
		
		for(auto p = peers.begin(); p != peers.end(); ++p)
		{
			Peer *peer = p->second;
			
			if(peer->state == Peer::PS_CONNECTED)
			{
				peer->sq.send(SendQueue::SEND_PRI_HIGH, group_left, NULL,
					[](std::unique_lock<std::mutex> &l, HRESULT result) {});
			}
		}
		
		/* If the peer hasn't gone away, ack it. */
		peer = get_peer_by_peer_id(peer_id);
		if(peer != NULL)
		{
			peer->send_ack(ack_id, S_OK);
		}
		
		group->player_ids.erase(local_player_id);
		
		DPNMSG_REMOVE_PLAYER_FROM_GROUP rp;
		memset(&rp, 0, sizeof(rp));
		
		rp.dwSize          = sizeof(rp);
		rp.dpnidGroup      = group_id;
		rp.pvGroupContext  = group->ctx;
		rp.dpnidPlayer     = local_player_id;
		rp.pvPlayerContext = local_player_ctx;
		
		l.unlock();
		message_handler(message_handler_ctx, DPN_MSGID_REMOVE_PLAYER_FROM_GROUP, &rp);
		l.lock();
	}
	catch(const PacketDeserialiser::Error &e)
	{
		log_printf("Received invalid DPLITE_MSGID_GROUP_LEAVE from peer %u: %s",
			peer_id, e.what());
	}
}

void DirectPlay8Peer::handle_group_left(std::unique_lock<std::mutex> &l, unsigned int peer_id, const PacketDeserialiser &pd)
{
	Peer *peer = get_peer_by_peer_id(peer_id);
	assert(peer != NULL);
	
	try {
		DPNID group_id = pd.get_dword(0);
		
		if(peer->state != Peer::PS_CONNECTED)
		{
			log_printf("Received unexpected DPLITE_MSGID_GROUP_LEFT from peer %u, in state %u",
				peer_id, (unsigned)(peer->state));
			return;
		}
		
		if(destroyed_groups.find(group_id) != destroyed_groups.end())
		{
			/* Group is already in the process of being destroyed. */
			return;
		}
		
		Group *group = get_group_by_id(group_id);
		if(group == NULL)
		{
			/* Unknown group ID, probably normal. */
			return;
		}
		
		if(group->player_ids.find(peer->player_id) == group->player_ids.end())
		{
			/* Already in group. */
			log_printf("Received DPLITE_MSGID_GROUP_LEFT from peer %u for group %u, but it isn't in group",
				peer_id, (unsigned)(group_id));
			return;
		}
		
		group->player_ids.erase(peer->player_id);
		
		DPNMSG_REMOVE_PLAYER_FROM_GROUP rp;
		memset(&rp, 0, sizeof(rp));
		
		rp.dwSize          = sizeof(rp);
		rp.dpnidGroup      = group_id;
		rp.pvGroupContext  = group->ctx;
		rp.dpnidPlayer     = peer->player_id;
		rp.pvPlayerContext = peer->player_ctx;
		
		l.unlock();
		message_handler(message_handler_ctx, DPN_MSGID_REMOVE_PLAYER_FROM_GROUP, &rp);
		l.lock();
	}
	catch(const PacketDeserialiser::Error &e)
	{
		log_printf("Received invalid DPLITE_MSGID_GROUP_LEFT from peer %u: %s",
			peer_id, e.what());
	}
}

/* Check if we have finished connecting and should enter STATE_CONNECTED.
 *
 * This is called after processing either of:
 *
 * DPLITE_MSGID_CONNECT_HOST_OK
 * DPLITE_MSGID_CONNECT_PEER_OK
 *
 * If there are no outgoing connections still outstanding, then we have
 * successfully connected to every peer in the session at the point the server
 * accepted us and we should proceed.
*/
void DirectPlay8Peer::connect_check(std::unique_lock<std::mutex> &l)
{
	assert(state == STATE_CONNECTING_TO_HOST || state == STATE_CONNECTING_TO_PEERS);
	
	/* Search for any outgoing connections we have initiated that haven't
	 * completed or failed yet.
	*/
	for(auto p = peers.begin(); p != peers.end(); ++p)
	{
		switch(p->second->state)
		{
			case Peer::PS_CONNECTING_HOST:
			case Peer::PS_REQUESTING_HOST:
			case Peer::PS_CONNECTING_PEER:
			case Peer::PS_REQUESTING_PEER:
				return;
				
			default:
				break;
		}
	}
	
	/* No outgoing connections in progress, proceed to STATE_CONNECTED! */
	
	state = STATE_CONNECTED;
	
	DPNMSG_CONNECT_COMPLETE cc;
	memset(&cc, 0, sizeof(cc));
	
	cc.dwSize        = sizeof(cc);
	cc.hAsyncOp      = connect_handle;
	cc.pvUserContext = connect_ctx;
	cc.hResultCode   = S_OK;
	cc.dpnidLocal    = local_player_id;
	
	if(!connect_reply_data.empty())
	{
		cc.pvApplicationReplyData     = connect_reply_data.data();
		cc.dwApplicationReplyDataSize = connect_reply_data.size();
	}
	
	l.unlock();
	message_handler(message_handler_ctx, DPN_MSGID_CONNECT_COMPLETE, &cc);
	l.lock();
	
	/* Signal the pending synchronous Connect() call (if any) to return. */
	
	connect_result = S_OK;
	connect_cv.notify_all();
}

/* Fail a pending Connect operation and return to STATE_INITIALISED.
 * ...
*/
void DirectPlay8Peer::connect_fail(std::unique_lock<std::mutex> &l, HRESULT hResultCode, const void *pvApplicationReplyData, DWORD dwApplicationReplyDataSize)
{
	assert(state == STATE_CONNECTING_TO_HOST || state == STATE_CONNECTING_TO_PEERS);
	
	State old_state = state;
	
	state = STATE_CONNECT_FAILED;
	
	close_main_sockets();
	peer_destroy_all(l, DPNERR_GENERIC, DPNDESTROYPLAYERREASON_CONNECTIONLOST);
	
	if(old_state == STATE_CONNECTING_TO_PEERS)
	{
		/* If we were connecting to peers, then we had gotten far enough to have
		 * raised DPNMSG_CREATE_PLAYER for the local player. Undo it.
		*/
		
		dispatch_destroy_player(l, local_player_id, local_player_ctx, DPNDESTROYPLAYERREASON_NORMAL);
	}
	
	DPNMSG_CONNECT_COMPLETE cc;
	memset(&cc, 0, sizeof(cc));
	
	cc.dwSize        = sizeof(cc);
	cc.hAsyncOp      = connect_handle;
	cc.pvUserContext = connect_ctx;
	
	cc.hResultCode                = hResultCode;
	cc.pvApplicationReplyData     = (void*)(pvApplicationReplyData); /* TODO: Take non-const copy? */
	cc.dwApplicationReplyDataSize = dwApplicationReplyDataSize;
	
	l.unlock();
	message_handler(message_handler_ctx, DPN_MSGID_CONNECT_COMPLETE, &cc);
	l.lock();
	
	/* Signal the pending synchronous Connect() call (if any) to return. */
	
	connect_result = hResultCode;
	state = STATE_INITIALISED;
	connect_cv.notify_all();
}

HRESULT DirectPlay8Peer::dispatch_message(std::unique_lock<std::mutex> &l, DWORD dwMessageType, PVOID pvMessage)
{
	l.unlock();
	HRESULT result = message_handler(message_handler_ctx, dwMessageType, pvMessage);
	l.lock();
	
	return result;
}

HRESULT DirectPlay8Peer::dispatch_create_player(std::unique_lock<std::mutex> &l, DPNID dpnidPlayer, void **ppvPlayerContext)
{
	DPNMSG_CREATE_PLAYER cp;
	memset(&cp, 0, sizeof(cp));
	
	cp.dwSize          = sizeof(cp);
	cp.dpnidPlayer     = dpnidPlayer;
	cp.pvPlayerContext = *ppvPlayerContext;
	
	HRESULT result = dispatch_message(l, DPN_MSGID_CREATE_PLAYER, &cp);
	
	*ppvPlayerContext = cp.pvPlayerContext;
	
	return result;
}

HRESULT DirectPlay8Peer::dispatch_destroy_player(std::unique_lock<std::mutex> &l, DPNID dpnidPlayer, void *pvPlayerContext, DWORD dwReason)
{
	/* HACK: Remove the player ID from any groups it is still in. */
	for(auto g = groups.begin(); g != groups.end();)
	{
		DPNID group_id = g->first;
		Group *group = &(g->second);
		
		if(group->player_ids.find(dpnidPlayer) != group->player_ids.end())
		{
			group->player_ids.erase(dpnidPlayer);
			
			DPNMSG_REMOVE_PLAYER_FROM_GROUP rp;
			memset(&rp, 0, sizeof(rp));
			
			rp.dwSize          = sizeof(rp);
			rp.dpnidGroup      = group_id;
			rp.pvGroupContext  = group->ctx;
			rp.dpnidPlayer     = dpnidPlayer;
			rp.pvPlayerContext = pvPlayerContext;
			
			l.unlock();
			message_handler(message_handler_ctx, DPN_MSGID_REMOVE_PLAYER_FROM_GROUP, &rp);
			l.lock();
			
			g = groups.begin();
		}
		else{
			++g;
		}
	}
	
	DPNMSG_DESTROY_PLAYER dp;
	memset(&dp, 0, sizeof(dp));
	
	dp.dwSize          = sizeof(DPNMSG_DESTROY_PLAYER);
	dp.dpnidPlayer     = dpnidPlayer;
	dp.pvPlayerContext = pvPlayerContext;
	dp.dwReason        = dwReason;
	
	return dispatch_message(l, DPN_MSGID_DESTROY_PLAYER, &dp);
}

HRESULT DirectPlay8Peer::dispatch_destroy_group(std::unique_lock<std::mutex> &l, DPNID dpnidGroup, void *pvGroupContext, DWORD dwReason)
{
	DPNMSG_DESTROY_GROUP dg;
	
	dg.dwSize         = sizeof(dg);
	dg.dpnidGroup     = dpnidGroup;
	dg.pvGroupContext = pvGroupContext;
	dg.dwReason       = dwReason;
	
	return dispatch_message(l, DPN_MSGID_DESTROY_GROUP, &dg);
}

DirectPlay8Peer::Peer::Peer(enum PeerState state, int sock, uint32_t ip, uint16_t port):
	state(state), sock(sock), ip(ip), port(port), recv_busy(false), recv_buf_cur(0), events(0), sq(event), send_open(true), next_ack_id(1)
{}

bool DirectPlay8Peer::Peer::enable_events(long events)
{
	if(WSAEventSelect(sock, event, (this->events | events)) != 0)
	{
		DWORD err = WSAGetLastError();
		log_printf("WSAEventSelect() error: ", win_strerror(err).c_str());
		
		return false;
	}
	
	this->events |= events;
	
	return true;
}

bool DirectPlay8Peer::Peer::disable_events(long events)
{
	if(WSAEventSelect(sock, event, (this->events & ~events)) != 0)
	{
		DWORD err = WSAGetLastError();
		log_printf("WSAEventSelect() error: ", win_strerror(err).c_str());
		
		return false;
	}
	
	this->events &= ~events;
	
	return true;
}

DWORD DirectPlay8Peer::Peer::alloc_ack_id()
{
	DWORD id = next_ack_id++;
	if(next_ack_id == 0)
	{
		++next_ack_id;
	}
	
	return id;
}

void DirectPlay8Peer::Peer::register_ack(DWORD id, const std::function<void(std::unique_lock<std::mutex>&, HRESULT)> &callback)
{
	register_ack(id, [callback](std::unique_lock<std::mutex> &l, HRESULT result, const void *data, size_t data_size)
	{
		callback(l, result);
	});
}

void DirectPlay8Peer::Peer::register_ack(DWORD id, const std::function<void(std::unique_lock<std::mutex>&, HRESULT, const void*, size_t)> &callback)
{
	assert(pending_acks.find(id) == pending_acks.end());
	pending_acks.emplace(id, callback);
}

void DirectPlay8Peer::Peer::send_ack(DWORD ack_id, HRESULT result, const void *data, size_t data_size)
{
	PacketSerialiser ack(DPLITE_MSGID_ACK);
	ack.append_dword(ack_id);
	ack.append_dword(result);
	ack.append_data(data, data_size);
	
	sq.send(SendQueue::SEND_PRI_HIGH, ack, NULL,
		[](std::unique_lock<std::mutex> &l, HRESULT result) {});
}

DirectPlay8Peer::Group::Group(const std::wstring &name, const void *data, size_t data_size, void *ctx):
	name(name),
	data((const unsigned char*)(data), (const unsigned char*)(data) + data_size),
	ctx(ctx)
{}

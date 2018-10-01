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
	worker_pool(THREADS_PER_POOL, MAX_HANDLES_PER_POOL),
	udp_sq(udp_socket_event)
{
	worker_pool.add_handle(udp_socket_event,   [this]() { handle_udp_socket_event();   });
	worker_pool.add_handle(other_socket_event, [this]() { handle_other_socket_event(); });
	
	AddRef();
}

DirectPlay8Peer::~DirectPlay8Peer()
{
	if(state != STATE_NEW)
	{
		Close(0);
	}
	
	worker_pool.remove_handle(other_socket_event);
	worker_pool.remove_handle(udp_socket_event);
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
	if(state != STATE_NEW)
	{
		return DPNERR_ALREADYINITIALIZED;
	}
	
	message_handler     = pfn;
	message_handler_ctx = pvUserContext;
	
	WSADATA wd;
	if(WSAStartup(MAKEWORD(2,2), &wd) != 0)
	{
		log_printf("WSAStartup() failed");
		return DPNERR_GENERIC;
	}
	
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
	if(dwFlags & DPNCANCEL_PLAYER_SENDS)
	{
		/* Cancel sends to player ID in hAsyncHandle */
		UNIMPLEMENTED("DirectPlay8Peer::CancelAsyncOperation");
	}
	else if(dwFlags & (DPNCANCEL_ENUM | DPNCANCEL_CONNECT | DPNCANCEL_ALL_OPERATIONS))
	{
		/* Cancel all outstanding operations of one or more types. */
		
		if(dwFlags & (DPNCANCEL_ENUM | DPNCANCEL_ALL_OPERATIONS))
		{
			std::unique_lock<std::mutex> l(lock);
			
			for(auto ei = host_enums.begin(); ei != host_enums.end(); ++ei)
			{
				ei->second.cancel();
			}
		}
		
		if(dwFlags & (DPNCANCEL_CONNECT | DPNCANCEL_ALL_OPERATIONS))
		{
			/* TODO: Cancel in-progress connect. */
		}
		
		if(dwFlags & DPNCANCEL_ALL_OPERATIONS)
		{
			/* TODO: Cancel all sends */
		}
		
		return S_OK;
	}
	else if((hAsyncHandle & AsyncHandleAllocator::TYPE_MASK) == AsyncHandleAllocator::TYPE_ENUM)
	{
		std::unique_lock<std::mutex> l(lock);
		
		auto ei = host_enums.find(hAsyncHandle);
		if(ei == host_enums.end())
		{
			return DPNERR_INVALIDHANDLE;
		}
		
		/* TODO: Make successive cancels for the same handle before it is destroyed fail? */
		
		ei->second.cancel();
		return S_OK;
	}
	else if((hAsyncHandle & AsyncHandleAllocator::TYPE_MASK) == AsyncHandleAllocator::TYPE_CONNECT)
	{
		UNIMPLEMENTED("DirectPlay8Peer::CancelAsyncOperation");
	}
	else if((hAsyncHandle & AsyncHandleAllocator::TYPE_MASK) == AsyncHandleAllocator::TYPE_SEND)
	{
		UNIMPLEMENTED("DirectPlay8Peer::CancelAsyncOperation");
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
		case STATE_NEW:         return DPNERR_UNINITIALIZED;
		case STATE_INITIALISED: break;
		case STATE_HOSTING:     return DPNERR_HOSTING;
		case STATE_CONNECTING:  return DPNERR_CONNECTING;
		case STATE_CONNECTED:   return DPNERR_ALREADYCONNECTED;
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
	
	uint32_t r_ipaddr;
	uint16_t r_port;
	
	{
		wchar_t buf[128];
		DWORD bsize = sizeof(buf);
		DWORD type;
		
		pHostAddr->GetComponentByName(DPNA_KEY_HOSTNAME, buf, &bsize, &type);
		InetPtonW(AF_INET, buf, &r_ipaddr);
	}
	
	{
		DWORD buf;
		DWORD bsize = sizeof(buf);
		DWORD type;
		
		pHostAddr->GetComponentByName(DPNA_KEY_PORT, &buf, &bsize, &type);
		r_port = buf;
	}
	
	if(l_port == 0)
	{
		for(int p = AUTO_PORT_MIN; p <= AUTO_PORT_MAX; ++p)
		{
			/* TODO: Only continue if creation failed due to address conflict. */
			
			udp_socket = create_udp_socket(l_ipaddr, p);
			if(udp_socket == -1)
			{
				continue;
			}
			
			listener_socket = create_listener_socket(l_ipaddr, p);
			if(listener_socket == -1)
			{
				closesocket(udp_socket);
				udp_socket = -1;
				
				continue;
			}
			
			local_ip   = l_ipaddr;
			local_port = p;
			
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
	
	state = STATE_CONNECTING;
	
	if(!peer_connect(Peer::PS_CONNECTING_HOST, r_ipaddr, r_port))
	{
		closesocket(listener_socket);
		listener_socket = -1;
		
		closesocket(udp_socket);
		udp_socket = -1;
		
		return DPNERR_GENERIC;
	}
	
	if(dwFlags & DPNCONNECT_SYNC)
	{
		connect_cv.wait(l, [this]() { return (state != STATE_CONNECTING && state != STATE_CONNECT_FAILED); });
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
		case STATE_NEW:            return DPNERR_UNINITIALIZED;
		case STATE_INITIALISED:    return DPNERR_NOTREADY;
		case STATE_HOSTING:        break;
		case STATE_CONNECTING:     return DPNERR_NOTREADY;
		case STATE_CONNECT_FAILED: return DPNERR_NOTREADY;
		case STATE_CONNECTED:      break;
	}
	
	if(dwFlags & DPNSEND_COMPLETEONPROCESS)
	{
		/* Not implemented yet. */
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
		if(dpnid == local_player_id)
		{
			send_to_self = true;
		}
		else{
			Peer *target_peer = get_peer_by_player_id(dpnid);
			if(target_peer == NULL)
			{
				return DPNERR_INVALIDPLAYER;
			}
			
			send_to_peers.push_back(target_peer);
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
		
		auto handle_send_complete =
			[this, pending, result, pvAsyncContext, dwFlags, prgBufferDesc, cBufferDesc]
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
				// sc.hAsyncOp
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
				
				l.unlock();
				message_handler(message_handler_ctx, DPN_MSGID_SEND_COMPLETE, &sc);
				l.lock();
			}
		};
		
		*phAsyncHandle = 0;
		
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
			(*pi)->sq.send(priority, message, NULL,
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
			
			/* TODO: Do this in a properly managed worker thread. */
			
			std::thread t([this, payload_size, payload_copy, handle_send_complete, dwFlags]()
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
			
			t.detach();
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
	switch(state)
	{
		case STATE_NEW:         return DPNERR_UNINITIALIZED;
		case STATE_INITIALISED: break;
		case STATE_HOSTING:     return DPNERR_ALREADYCONNECTED;
		case STATE_CONNECTING:  return DPNERR_CONNECTING;
		case STATE_CONNECTED:   return DPNERR_ALREADYCONNECTED;
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
	
	/* Generate a random GUID for this session. */
	HRESULT guid_err = CoCreateGuid(&instance_guid);
	if(guid_err != S_OK)
	{
		return guid_err;
	}
	
	application_guid = pdnAppDesc->guidApplication;
	max_players      = pdnAppDesc->dwMaxPlayers;
	session_name     = pdnAppDesc->pwszSessionName;
	
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
	
	uint32_t ipaddr = htonl(INADDR_ANY);
	uint16_t port   = 0;
	
	for(DWORD i = 0; i < cDeviceInfo; ++i)
	{
		DirectPlay8Address *addr = (DirectPlay8Address*)(prgpDeviceInfo[i]);
		
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
	
	DPNMSG_CREATE_PLAYER cp;
	memset(&cp, 0, sizeof(cp));
	
	cp.dwSize          = sizeof(cp);
	cp.dpnidPlayer     = local_player_id;
	cp.pvPlayerContext = local_player_ctx;
	
	message_handler(message_handler_ctx, DPN_MSGID_CREATE_PLAYER, &cp);
	
	local_player_ctx = cp.pvPlayerContext;
	
	return S_OK;
}

HRESULT DirectPlay8Peer::GetApplicationDesc(DPN_APPLICATION_DESC* CONST pAppDescBuffer, DWORD* CONST pcbDataSize, CONST DWORD dwFlags)
{
	std::unique_lock<std::mutex> l(lock);
	
	switch(state)
	{
		case STATE_NEW:        return DPNERR_UNINITIALIZED;
		case STATE_HOSTING:    break;
		case STATE_CONNECTED:  break;
		case STATE_CONNECTING: return DPNERR_CONNECTING;
		default:               return DPNERR_NOCONNECTION;
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
	UNIMPLEMENTED("DirectPlay8Peer::CreateGroup");
}

HRESULT DirectPlay8Peer::DestroyGroup(CONST DPNID idGroup, PVOID CONST pvAsyncContext, DPNHANDLE* CONST phAsyncHandle, CONST DWORD dwFlags)
{
	UNIMPLEMENTED("DirectPlay8Peer::DestroyGroup");
}

HRESULT DirectPlay8Peer::AddPlayerToGroup(CONST DPNID idGroup, CONST DPNID idClient, PVOID CONST pvAsyncContext, DPNHANDLE* CONST phAsyncHandle, CONST DWORD dwFlags)
{
	UNIMPLEMENTED("DirectPlay8Peer::AddPlayerToGroup");
}

HRESULT DirectPlay8Peer::RemovePlayerFromGroup(CONST DPNID idGroup, CONST DPNID idClient, PVOID CONST pvAsyncContext, DPNHANDLE* CONST phAsyncHandle, CONST DWORD dwFlags)
{
	UNIMPLEMENTED("DirectPlay8Peer::RemovePlayerFromGroup");
}

HRESULT DirectPlay8Peer::SetGroupInfo(CONST DPNID dpnid, DPN_GROUP_INFO* CONST pdpnGroupInfo,PVOID CONST pvAsyncContext, DPNHANDLE* CONST phAsyncHandle, CONST DWORD dwFlags)
{
	UNIMPLEMENTED("DirectPlay8Peer::SetGroupInfo");
}

HRESULT DirectPlay8Peer::GetGroupInfo(CONST DPNID dpnid, DPN_GROUP_INFO* CONST pdpnGroupInfo, DWORD* CONST pdwSize, CONST DWORD dwFlags)
{
	UNIMPLEMENTED("DirectPlay8Peer::GetGroupInfo");
}

HRESULT DirectPlay8Peer::EnumPlayersAndGroups(DPNID* CONST prgdpnid, DWORD* CONST pcdpnid, CONST DWORD dwFlags)
{
	UNIMPLEMENTED("DirectPlay8Peer::EnumPlayersAndGroups");
}

HRESULT DirectPlay8Peer::EnumGroupMembers(CONST DPNID dpnid, DPNID* CONST prgdpnid, DWORD* CONST pcdpnid, CONST DWORD dwFlags)
{
	UNIMPLEMENTED("DirectPlay8Peer::EnumGroupMembers");
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
	
	unsigned int sync_pending = 0;
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
		
		pending = new unsigned int(0);
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
	
	/* Bodge to dispatch a DPNMSG_ASYNC_OP_COMPLETE if we didn't have any
	 * peers to notify. None of the send callbacks could have touched
	 * pending by this point since we haven't released the lock.
	*/
	bool no_peers_to_notify = (*pending == 0);
	
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
	}
	
	if(dwFlags & DPNSETPEERINFO_SYNC)
	{
		sync_cv.wait(l, [pending]() { return (*pending == 0); });
		return sync_result;
	}
	else{
		if(no_peers_to_notify)
		{
			++(*pending);
			op_finished_cb(l, S_OK);
		}
		
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
	UNIMPLEMENTED("DirectPlay8Peer::GetPeerAddress");
}

HRESULT DirectPlay8Peer::GetLocalHostAddresses(IDirectPlay8Address** CONST prgpAddress, DWORD* CONST pcAddress, CONST DWORD dwFlags)
{
	UNIMPLEMENTED("DirectPlay8Peer::GetLocalHostAddresses");
}

HRESULT DirectPlay8Peer::Close(CONST DWORD dwFlags)
{
	if(state == STATE_NEW)
	{
		return DPNERR_UNINITIALIZED;
	}
	
	CancelAsyncOperation(0, DPNCANCEL_ALL_OPERATIONS);
	
	/* TODO: Wait properly. */
	
	while(1)
	{
		std::unique_lock<std::mutex> l(lock);
		if(host_enums.empty())
		{
			break;
		}
		
		Sleep(50);
	}
	
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
	
	WSACleanup();
	
	state = STATE_NEW;
	
	return S_OK;
}

HRESULT DirectPlay8Peer::EnumHosts(PDPN_APPLICATION_DESC CONST pApplicationDesc, IDirectPlay8Address* CONST pAddrHost, IDirectPlay8Address* CONST pDeviceInfo,PVOID CONST pUserEnumData, CONST DWORD dwUserEnumDataSize, CONST DWORD dwEnumCount, CONST DWORD dwRetryInterval, CONST DWORD dwTimeOut,PVOID CONST pvUserContext, DPNHANDLE* CONST pAsyncHandle, CONST DWORD dwFlags)
{
	if(state == STATE_NEW)
	{
		return DPNERR_UNINITIALIZED;
	}
	
	try {
		if(dwFlags & DPNENUMHOSTS_SYNC)
		{
			HRESULT result;
			
			HostEnumerator he(
				global_refcount,
				message_handler, message_handler_ctx,
				pApplicationDesc, pAddrHost, pDeviceInfo, pUserEnumData, dwUserEnumDataSize,
				dwEnumCount, dwRetryInterval, dwTimeOut, pvUserContext,
				
				[&result](HRESULT r)
				{
					result = r;
				});
			
			he.wait();
			
			return result;
		}
		else{
			DPNHANDLE handle = handle_alloc.new_enum();
			
			*pAsyncHandle = handle;
			
			std::unique_lock<std::mutex> l(lock);
			
			host_enums.emplace(
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
						host_enums.erase(handle);
						l.unlock();
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
	UNIMPLEMENTED("DirectPlay8Peer::DestroyPeer");
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
		case STATE_NEW:            return DPNERR_UNINITIALIZED;
		case STATE_INITIALISED:    return DPNERR_NOTREADY;
		case STATE_HOSTING:        break;
		case STATE_CONNECTING:     return DPNERR_NOTREADY;
		case STATE_CONNECT_FAILED: return DPNERR_NOTREADY;
		case STATE_CONNECTED:      break;
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
	UNIMPLEMENTED("DirectPlay8Peer::GetGroupContext");
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
	UNIMPLEMENTED("DirectPlay8Peer::TerminateSession");
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

void DirectPlay8Peer::handle_udp_socket_event()
{
	std::unique_lock<std::mutex> l(lock);
	
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
				/* TODO: Log "unrecognised packet type" */
				break;
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
					/* TODO: Log "unrecognised packet type" */
					break;
			}
		}
	}
	
	peer_accept(l);
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
	
	if(peer->state == Peer::PS_CONNECTING_HOST || peer->state == Peer::PS_CONNECTING_PEER)
	{
		assert(state == STATE_CONNECTING);
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
			/* TODO: Send DPLITE_MSGID_CONNECT_PEER message. */
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
	
	while((peer = get_peer_by_peer_id(peer_id)) != NULL && (sqop = peer->sq.get_pending()) != NULL)
	{
		std::pair<const void*, size_t> d = sqop->get_pending_data();
		
		int s = send(peer->sock, (const char*)(d.first), d.second, 0);
		if(s < 0)
		{
			DWORD err = WSAGetLastError();
			
			if(err == WSAEWOULDBLOCK)
			{
				break;
			}
			else{
				/* TODO */
				return;
			}
		}
		
		sqop->inc_sent_data(s);
		
		if(s == d.second)
		{
			peer->sq.pop_pending(sqop);
			
			/* TODO: Poke event, send/recv mutexes per TCP socket. */
			
			sqop->invoke_callback(l, S_OK);
			
			delete sqop;
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
		
		peer->recv_busy = true;
		rb_claimed      = true;
		
		/* TODO: Mask read events to avoid workers spinning. */
		
		int r = recv(peer->sock, (char*)(peer->recv_buf) + peer->recv_buf_cur, sizeof(peer->recv_buf) - peer->recv_buf_cur, 0);
		if(r == 0)
		{
			/* Connection closed */
			/* TODO */
			break;
		}
		else if(r == -1)
		{
			DWORD err = WSAGetLastError();
			
			if(err == WSAEWOULDBLOCK)
			{
				/* Nothing to read */
				break;
			}
			else{
				/* Read error. */
				/* TODO */
				break;
			}
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
				/* TODO */
				abort();
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
					/* TODO */
					abort();
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
					
					default:
						log_printf(
							"Unexpected message type %u received from peer %u\n",
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
	
	if(peer != NULL)
	{
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
			/* TODO */
			abort();
		}
	}
	
	u_long non_blocking = 1;
	if(ioctlsocket(newfd, FIONBIO, &non_blocking) != 0)
	{
		closesocket(newfd);
		return;
	}
	
	unsigned int peer_id = next_peer_id++;
	Peer *peer = new Peer(Peer::PS_ACCEPTED, newfd, addr.sin_addr.s_addr, ntohs(addr.sin_port));
	
	peers.insert(std::make_pair(peer_id, peer));
	
	if(WSAEventSelect(peer->sock, peer->event, FD_READ | FD_WRITE | FD_CLOSE) != 0)
	{
		/* TODO */
	}
	
	worker_pool.add_handle(peer->event, [this, peer_id]() { io_peer_triggered(peer_id); });
}

bool DirectPlay8Peer::peer_connect(Peer::PeerState initial_state, uint32_t remote_ip, uint16_t remote_port)
{
	int p_sock = create_client_socket(local_ip, local_port);
	if(p_sock == -1)
	{
		return false;
	}
	
	unsigned int peer_id = next_peer_id++;
	Peer *peer = new Peer(initial_state, p_sock, remote_ip, remote_port);
	
	if(WSAEventSelect(peer->sock, peer->event, FD_CONNECT | FD_READ | FD_WRITE | FD_CLOSE) != 0)
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
	
	worker_pool.add_handle(peer->event, [this, peer_id]() { io_peer_triggered(peer_id); });
	
	return true;
}

void DirectPlay8Peer::peer_destroy(std::unique_lock<std::mutex> &l, unsigned int peer_id, HRESULT outstanding_op_result)
{
	Peer *peer;
	
	while((peer = get_peer_by_peer_id(peer_id)) != NULL)
	{
		/* Cancel any outstanding sends and notify the callbacks. */
		
		SendQueue::SendOp *sqop;
		if((sqop = peer->sq.get_pending()) != NULL)
		{
			peer->sq.pop_pending(sqop);
			
			sqop->invoke_callback(l, outstanding_op_result);
			
			delete sqop;
			
			/* Return to the start in case the peer has gone away while the lock was
			 * released to run the callback.
			*/
			continue;
		}
		
		/* Fail any outstanding acks and notify the callbacks. */
		
		if(!peer->pending_acks.empty())
		{
			auto ai = peer->pending_acks.begin();
			
			std::function<void(std::unique_lock<std::mutex>&, HRESULT)> callback = ai->second;
			peer->pending_acks.erase(ai);
			
			callback(l, outstanding_op_result);
			
			/* Return to the start in case the peer has gone away while the lock was
			 * released to run the callback.
			*/
			continue;
		}
		
		if(peer->state == Peer::PS_CONNECTED)
		{
			/* TODO: DPN_MSGID_DESTROY_PLAYER? */
			player_to_peer_id.erase(peer->player_id);
		}
		
		worker_pool.remove_handle(peer->event);
		
		closesocket(peer->sock);
		
		peers.erase(peer_id);
		delete peer;
		
		break;
	}
}

/* Immediately close all sockets and erase all peers. */
void DirectPlay8Peer::close_everything_now(std::unique_lock<std::mutex> &l)
{
	while(!peers.empty())
	{
		peer_destroy(l, peers.begin()->first, DPNERR_GENERIC);
	}
	
	if(discovery_socket != -1)
	{
		WSAEventSelect(discovery_socket, other_socket_event, 0);
		
		closesocket(discovery_socket);
		discovery_socket = -1;
	}
	
	if(listener_socket != -1)
	{
		WSAEventSelect(listener_socket, other_socket_event, 0);
		
		closesocket(listener_socket);
		listener_socket = -1;
	}
	
	if(udp_socket != -1)
	{
		WSAEventSelect(udp_socket, udp_socket_event, 0);
		
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
	
	ehq.dwSize = sizeof(ehq);
	ehq.pAddressSender = NULL; // TODO
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
		log_printf("Received unexpected DPLITE_MSGID_CONNECT_HOST from peer %u, in state %u\n",
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
	
	ic.pAddressPlayer = NULL; /* TODO */
	ic.pAddressDevice = NULL; /* TODO */
	
	peer->state = Peer::PS_INDICATING;
	
	l.unlock();
	HRESULT ic_result = message_handler(message_handler_ctx, DPN_MSGID_INDICATE_CONNECT, &ic);
	l.lock();
	
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
		
		PacketSerialiser connect_host_ok(DPLITE_MSGID_CONNECT_HOST_OK);
		connect_host_ok.append_guid(instance_guid);
		connect_host_ok.append_dword(host_player_id);
		connect_host_ok.append_dword(peer->player_id);
		
		connect_host_ok.append_dword(0); /* TODO: Other peers */
		
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
		/* TODO: LOG ME */
		return;
	}
	
	assert(state == STATE_CONNECTING);
	
	instance_guid = pd.get_guid(0);
	
	host_player_id = pd.get_dword(1);
	
	peer->player_id = host_player_id;
	player_to_peer_id[peer->player_id] = peer_id;
	
	local_player_id = pd.get_dword(2);
	
	DWORD n_other_peers = pd.get_dword(3);
	
	for(DWORD n = 0; n < n_other_peers; ++n)
	{
		DPNID    player_id     = pd.get_dword(4 + (n * 3));
		uint32_t player_ipaddr = pd.get_dword(5 + (n * 3));
		uint16_t player_port   = pd.get_dword(6 + (n * 3));
		
		/* TODO: Setup connections to other peers. */
		abort();
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
	
	this->application_data.clear();
	this->application_data.insert(this->application_data.end(),
		(const unsigned char*)(application_data.first),
		(const unsigned char*)(application_data.first) + application_data.second);
	
	peer->state = Peer::PS_CONNECTED;
	
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
	
	connect_check(l);
}

void DirectPlay8Peer::handle_host_connect_fail(std::unique_lock<std::mutex> &l, unsigned int peer_id, const PacketDeserialiser &pd)
{
	Peer *peer = get_peer_by_peer_id(peer_id);
	assert(peer != NULL);
	
	if(peer->state != Peer::PS_REQUESTING_HOST)
	{
		log_printf("Received unexpected DPLITE_MSGID_CONNECT_HOST_FAIL from peer %u, in state %u\n",
			peer_id, (unsigned)(peer->state));
		return;
	}
	
	assert(state == STATE_CONNECTING);
	
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
		log_printf("Received invalid DPLITE_MSGID_CONNECT_HOST_FAIL from peer %u: %s\n",
			peer_id, e.what());
	}
	
	connect_fail(l, hResultCode, pvApplicationReplyData, dwApplicationReplyDataSize);
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
		log_printf("Received invalid DPLITE_MSGID_MESSAGE: %s\n", e.what());
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
		
		if(peer->state != Peer::PS_CONNECTED || player_id != peer->player_id)
		{
			/* TODO: LOG ME */
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
		log_printf("Received invalid DPLITE_MSGID_PLAYERINFO from peer %u: %s\n",
			peer_id, e.what());
	}
}

void DirectPlay8Peer::handle_ack(std::unique_lock<std::mutex> &l, unsigned int peer_id, const PacketDeserialiser &pd)
{
	Peer *peer = get_peer_by_peer_id(peer_id);
	assert(peer != NULL);
	
	try {
		DWORD ack_id = pd.get_dword(0);
		HRESULT result = pd.get_dword(1);
		
		auto ai = peer->pending_acks.find(ack_id);
		if(ai == peer->pending_acks.end())
		{
			log_printf("Received DPLITE_MSGID_CONNECT_HOST_FAIL with unknown ID %u from peer %u: %s\n",
				(unsigned)(ack_id), peer_id);
			return;
		}
		
		std::function<void(std::unique_lock<std::mutex>&, HRESULT)> callback = ai->second;
		peer->pending_acks.erase(ai);
		
		callback(l, result);
	}
	catch(const PacketDeserialiser::Error &e)
	{
		log_printf("Received invalid DPLITE_MSGID_ACK from peer %u: %s\n",
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
			log_printf("Received unexpected DPLITE_MSGID_APPDESC from peer %u, in state %u\n",
				peer_id, (unsigned)(peer->state));
			return;
		}
		
		/* host_player_id must be initialised by this point, as the host is always the
		 * first peer to enter state PS_CONNECTED, initialising it in the process.
		*/
		
		if(peer->player_id != host_player_id)
		{
			log_printf("Received unexpected DPLITE_MSGID_CONNECT_HOST_FAIL from non-host peer %u\n",
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
		log_printf("Received invalid DPLITE_MSGID_APPDESC from peer %u: %s\n",
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
	assert(state == STATE_CONNECTING);
	
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
	assert(state == STATE_CONNECTING);
	
	close_everything_now(l);
	
	state = STATE_CONNECT_FAILED;
	
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

DirectPlay8Peer::Peer::Peer(enum PeerState state, int sock, uint32_t ip, uint16_t port):
	state(state), sock(sock), ip(ip), port(port), recv_busy(false), recv_buf_cur(0), sq(event), next_ack_id(1)
{}

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
	assert(pending_acks.find(id) == pending_acks.end());
	pending_acks.emplace(id, callback);
}

#include <winsock2.h>
#include <atomic>
#include <dplay8.h>
#include <iterator>
#include <memory>
#include <objbase.h>
#include <stdexcept>
#include <stdint.h>
#include <stdio.h>
#include <tuple>
#include <windows.h>

#include "COMAPIException.hpp"
#include "DirectPlay8Address.hpp"
#include "DirectPlay8Peer.hpp"
#include "Messages.hpp"
#include "network.hpp"

#define UNIMPLEMENTED(fmt, ...) \
	fprintf(stderr, "Unimplemented method: " fmt "\n", ## __VA_ARGS__); \
	return E_NOTIMPL;

DirectPlay8Peer::DirectPlay8Peer(std::atomic<unsigned int> *global_refcount):
	global_refcount(global_refcount),
	local_refcount(0),
	state(STATE_NEW),
	udp_socket(-1),
	listener_socket(-1),
	discovery_socket(-1)
{
	io_event = CreateEvent(NULL, FALSE, FALSE, NULL);
	if(io_event == NULL)
	{
		throw std::runtime_error("Cannot create event object");
	}
	
	AddRef();
}

DirectPlay8Peer::~DirectPlay8Peer()
{
	if(state != STATE_NEW)
	{
		Close(0);
	}
	
	CloseHandle(io_event);
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
		/* TODO */
		return DPNERR_GENERIC;
	}
	
	state = STATE_INITIALISED;
	
	return S_OK;
}

HRESULT DirectPlay8Peer::EnumServiceProviders(CONST GUID* CONST pguidServiceProvider, CONST GUID* CONST pguidApplication, DPN_SERVICE_PROVIDER_INFO* CONST pSPInfoBuffer, DWORD* CONST pcbEnumData, DWORD* CONST pcReturned, CONST DWORD dwFlags)
{
	UNIMPLEMENTED("DirectPlay8Peer::EnumServiceProviders");
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
	UNIMPLEMENTED("DirectPlay8Peer::Connect");
}

HRESULT DirectPlay8Peer::SendTo(CONST DPNID dpnid, CONST DPN_BUFFER_DESC* CONST prgBufferDesc, CONST DWORD cBufferDesc, CONST DWORD dwTimeOut, void* CONST pvAsyncContext, DPNHANDLE* CONST phAsyncHandle, CONST DWORD dwFlags)
{
	UNIMPLEMENTED("DirectPlay8Peer::SendTo");
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
		/* Ephemeral port range as defined by IANA. */
		const int AUTO_PORT_MIN = 49152;
		const int AUTO_PORT_MAX = 65535;
		
		for(int p = AUTO_PORT_MIN; p <= AUTO_PORT_MAX; ++p)
		{
			/* TODO: Only continue if creation failed due to address conflict. */
			
			udp_socket = create_udp_socket(ipaddr, port);
			if(udp_socket == -1)
			{
				continue;
			}
			
			listener_socket = create_listener_socket(ipaddr, port);
			if(listener_socket == -1)
			{
				closesocket(udp_socket);
				udp_socket = -1;
				
				continue;
			}
			
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
	}
	
	if(WSAEventSelect(udp_socket, io_event, FD_READ | FD_WRITE) != 0
		|| WSAEventSelect(listener_socket, io_event, FD_ACCEPT) != 0)
	{
		return DPNERR_GENERIC;
	}
	
	if(!(pdnAppDesc->dwFlags & DPNSESSION_NODPNSVR))
	{
		discovery_socket = create_discovery_socket();
		
		if(discovery_socket == -1
			|| WSAEventSelect(discovery_socket, io_event, FD_READ) != 0)
		{
			return DPNERR_GENERIC;
		}
	}
	
	io_run    = true;
	io_thread = std::thread(&DirectPlay8Peer::io_main, this);
	
	state = STATE_HOSTING;
	
	return S_OK;
}

HRESULT DirectPlay8Peer::GetApplicationDesc(DPN_APPLICATION_DESC* CONST pAppDescBuffer, DWORD* CONST pcbDataSize, CONST DWORD dwFlags)
{
	if(state != STATE_HOSTING && state != STATE_CONNECTED)
	{
		return DPNERR_NOCONNECTION;
	}
	
	DWORD required_size = sizeof(DPN_APPLICATION_DESC)
		+ (password.length() + !password.empty()) * sizeof(wchar_t)
		+ application_data.size();
	
	if(pAppDescBuffer != NULL && *pcbDataSize >= required_size)
	{
		unsigned char *extra_at = (unsigned char*)(pAppDescBuffer);
		
		pAppDescBuffer->dwSize = sizeof(*pAppDescBuffer);
		pAppDescBuffer->dwFlags = 0;
		pAppDescBuffer->guidInstance     = instance_guid;
		pAppDescBuffer->guidApplication  = application_guid;
		pAppDescBuffer->dwMaxPlayers     = max_players;
		pAppDescBuffer->dwCurrentPlayers = other_player_ids.size() + 1;
		
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
		*pcbDataSize = sizeof(*pAppDescBuffer);
		return DPNERR_BUFFERTOOSMALL;
	}
}

HRESULT DirectPlay8Peer::SetApplicationDesc(CONST DPN_APPLICATION_DESC* CONST pad, CONST DWORD dwFlags)
{
	if(state == STATE_HOSTING)
	{
		if(pad->dwMaxPlayers > 0 && pad->dwMaxPlayers <= other_player_ids.size())
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
		
		/* TODO: Notify peers */
		
		return S_OK;
	}
	else{
		return DPNERR_NOTHOST;
	}
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
	UNIMPLEMENTED("DirectPlay8Peer::SetPeerInfo(%p, %p, %p, %u)", pdpnPlayerInfo, pvAsyncContext, phAsyncHandle, (unsigned)(dwFlags));
}

HRESULT DirectPlay8Peer::GetPeerInfo(CONST DPNID dpnid, DPN_PLAYER_INFO* CONST pdpnPlayerInfo, DWORD* CONST pdwSize, CONST DWORD dwFlags)
{
	UNIMPLEMENTED("DirectPlay8Peer::GetPeerInfo");
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
	
	if(state == STATE_HOSTING || state == STATE_CONNECTED)
	{
		io_run = false;
		SetEvent(io_event);
		
		io_thread.join();
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
	
	/* TODO: Clean sockets etc up. */
	
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
	UNIMPLEMENTED("DirectPlay8Peer::ReturnBuffer");
}

HRESULT DirectPlay8Peer::GetPlayerContext(CONST DPNID dpnid,PVOID* CONST ppvPlayerContext, CONST DWORD dwFlags)
{
	UNIMPLEMENTED("DirectPlay8Peer::GetPlayerContext");
}

HRESULT DirectPlay8Peer::GetGroupContext(CONST DPNID dpnid,PVOID* CONST ppvGroupContext, CONST DWORD dwFlags)
{
	UNIMPLEMENTED("DirectPlay8Peer::GetGroupContext");
}

HRESULT DirectPlay8Peer::GetCaps(DPN_CAPS* CONST pdpCaps, CONST DWORD dwFlags)
{
	UNIMPLEMENTED("DirectPlay8Peer::GetCaps");
}

HRESULT DirectPlay8Peer::SetCaps(CONST DPN_CAPS* CONST pdpCaps, CONST DWORD dwFlags)
{
	UNIMPLEMENTED("DirectPlay8Peer::SetCaps");
}

HRESULT DirectPlay8Peer::SetSPCaps(CONST GUID* CONST pguidSP, CONST DPN_SP_CAPS* CONST pdpspCaps, CONST DWORD dwFlags )
{
	UNIMPLEMENTED("DirectPlay8Peer::SetSPCaps");
}

HRESULT DirectPlay8Peer::GetSPCaps(CONST GUID* CONST pguidSP, DPN_SP_CAPS* CONST pdpspCaps, CONST DWORD dwFlags)
{
	UNIMPLEMENTED("DirectPlay8Peer::GetSPCaps");
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

void DirectPlay8Peer::io_main()
{
	while(io_run)
	{
		WaitForSingleObject(io_event, INFINITE);
		
		io_udp_recv(udp_socket);
		io_udp_send(udp_socket, udp_sq);
		
		if(discovery_socket != -1)
		{
			io_udp_recv(discovery_socket);
			io_udp_send(discovery_socket, discovery_sq);
		}
		
		io_listener_accept(listener_socket);
		
		std::unique_lock<std::mutex> l(lock);
		
		for(auto p = peers.begin(); p != peers.end();)
		{
			auto next_p = std::next(p);
			
			if(!io_tcp_recv(&*p) || !io_tcp_send(&*p))
			{
				/* TODO: Complete outstanding sends (failed), drop player */
				closesocket(p->sock);
				peers.erase(p);
			}
			
			p = next_p;
		}
	}
}

void DirectPlay8Peer::io_udp_recv(int sock)
{
	struct sockaddr_in from_addr;
	int fa_len = sizeof(from_addr);
	
	int r = recvfrom(sock, (char*)(recv_buf), sizeof(recv_buf), 0, (struct sockaddr*)(&from_addr), &fa_len);
	if(r <= 0)
	{
		return;
	}
	
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
			if(state == STATE_HOSTING)
			{
				handle_host_enum_request(*pd, &from_addr);
			}
			
			break;
		}
		
		default:
			/* TODO: Log "unrecognised packet type" */
			break;
	}
}

void DirectPlay8Peer::io_udp_send(int sock, SendQueue &sq)
{
	SendQueue::Buffer *sqb;
	
	while((sqb = sq.get_next()) != NULL)
	{
		std::pair<const void*, size_t>               data = sqb->get_data();
		std::pair<const struct sockaddr*, int> addr = sqb->get_dest_addr();
		
		int s = sendto(sock, (const char*)(data.first), data.second, 0, addr.first, addr.second);
		if(s == -1)
		{
			DWORD err = WSAGetLastError();
			
			if(err != WSAEWOULDBLOCK)
			{
				/* TODO: More specific error codes */
				sq.complete(sqb, DPNERR_GENERIC);
			}
			
			break;
		}
		
		sq.complete(sqb, S_OK);
	}
}

void DirectPlay8Peer::io_listener_accept(int sock)
{
	struct sockaddr_in addr;
	int addrlen = sizeof(addr);
	
	int newfd = accept(sock, (struct sockaddr*)(&addr), &addrlen);
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
	
	peers.emplace_back(newfd, addr.sin_addr.s_addr, ntohs(addr.sin_port));
}

bool DirectPlay8Peer::io_tcp_recv(Player *player)
{
	int r = recv(player->sock, (char*)(player->recv_buf) + player->recv_buf_cur, sizeof(player->recv_buf) - player->recv_buf_cur, 0);
	if(r == 0)
	{
		/* Connection closed */
		return false;
	}
	else if(r == -1)
	{
		DWORD err = WSAGetLastError();
		
		if(err == WSAEWOULDBLOCK)
		{
			/* Nothing to read */
			return true;
		}
		else{
			/* Read error. */
			return false;
		}
	}
	
	player->recv_buf_cur += r;
	
	if(player->recv_buf_cur >= sizeof(TLVChunk))
	{
		TLVChunk *header = (TLVChunk*)(player->recv_buf);
		size_t full_packet_size = sizeof(TLVChunk) + header->value_length;
		
		if(full_packet_size > MAX_PACKET_SIZE)
		{
			/* Malformed packet received */
			return false;
		}
		
		if(player->recv_buf_cur >= full_packet_size)
		{
			/* Process message */
			std::unique_ptr<PacketDeserialiser> pd;
			
			try {
				pd.reset(new PacketDeserialiser(player->recv_buf, full_packet_size));
			}
			catch(const PacketDeserialiser::Error &e)
			{
				/* Malformed packet received */
				return false;
			}
			
			/* Message at the front of the buffer has been dealt with, shift any
			 * remaining data beyond it to the front and truncate it.
			*/
			
			memmove(player->recv_buf, player->recv_buf + full_packet_size,
				player->recv_buf_cur - full_packet_size);
			player->recv_buf_cur -= full_packet_size;
		}
	}
	
	return true;
}

bool DirectPlay8Peer::io_tcp_send(Player *player)
{
	SendQueue::Buffer *sqb = player->sq.get_next();
	
	while(player->send_buf != NULL || sqb != NULL)
	{
		if(player->sqb == NULL)
		{
			std::pair<const void*, size_t> sqb_data = sqb->get_data();
			
			player->send_buf    = (const unsigned char*)(sqb_data.first);
			player->send_remain = sqb_data.second;
		}
		
		int s = send(player->sock, (const char*)(player->send_buf), player->send_remain, 0);
		if(s < 0)
		{
			DWORD err = WSAGetLastError();
			
			if(err == WSAEWOULDBLOCK)
			{
				break;
			}
			else{
				/* TODO: Better error codes */
				player->sq.complete(sqb, DPNERR_GENERIC);
				return false;
			}
		}
		
		if((size_t)(s) == player->send_remain)
		{
			player->send_buf = NULL;
			
			player->sq.complete(sqb, S_OK);
			sqb = player->sq.get_next();
		}
		else{
			player->send_buf    += s;
			player->send_remain -= s;
		}
	}
	
	return true;
}

class SQB_TODO: public SendQueue::Buffer
{
	private:
		PFNDPNMESSAGEHANDLER message_handler;
		PVOID message_handler_ctx;
		
		DPNMSG_ENUM_HOSTS_QUERY query;
		
	public:
		SQB_TODO(const void *data, size_t data_size, const struct sockaddr_in *dest_addr,
			PFNDPNMESSAGEHANDLER message_handler, PVOID message_handler_ctx,
			DPNMSG_ENUM_HOSTS_QUERY query):
			Buffer(data, data_size, (const struct sockaddr*)(dest_addr), sizeof(*dest_addr)),
			message_handler(message_handler),
			message_handler_ctx(message_handler_ctx),
			query(query) {}
			
		virtual void complete(HRESULT result) override
		{
			if(query.pvResponseData != NULL)
			{
				DPNMSG_RETURN_BUFFER rb;
				memset(&rb, 0, sizeof(rb));
				
				rb.dwSize = sizeof(rb);
				rb.hResultCode = result;
				rb.pvBuffer = query.pvResponseData;
				rb.pvUserContext = query.pvResponseContext;
				
				message_handler(message_handler_ctx, DPN_MSGID_RETURN_BUFFER, &rb);
			}
		}
};

void DirectPlay8Peer::handle_host_enum_request(const PacketDeserialiser &pd, const struct sockaddr_in *from_addr)
{
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
	
	DPNMSG_ENUM_HOSTS_QUERY query;
	memset(&query, 0, sizeof(query));
	
	query.dwSize = sizeof(query);
	query.pAddressSender = NULL; // TODO
	query.pAddressDevice = NULL; // TODO
	
	if(!pd.is_null(1))
	{
		std::pair<const void*, size_t> data = pd.get_data(1);
		
		query.pvReceivedData     = (void*)(data.first); /* TODO: Make a non-const copy? */
		query.dwReceivedDataSize = data.second;
	}
	
	query.dwMaxResponseDataSize = 9999; // TODO
	
	DWORD req_tick = pd.get_dword(2);
	
	if(message_handler(message_handler_ctx, DPN_MSGID_ENUM_HOSTS_QUERY, &query) == DPN_OK)
	{
		PacketSerialiser ps(DPLITE_MSGID_HOST_ENUM_RESPONSE);
		
		ps.append_dword(password.empty() ? 0 : DPNSESSION_REQUIREPASSWORD);
		ps.append_guid(instance_guid);
		ps.append_guid(application_guid);
		ps.append_dword(max_players);
		ps.append_dword(other_player_ids.size() + 1);
		ps.append_wstring(session_name);
		
		if(!application_data.empty())
		{
			ps.append_data(application_data.data(), application_data.size());
		}
		else{
			ps.append_null();
		}
		
		if(query.pvResponseData != NULL && query.dwResponseDataSize != 0)
		{
			ps.append_data(query.pvResponseData, query.dwResponseDataSize);
		}
		else{
			ps.append_null();
		}
		
		ps.append_dword(req_tick);
		
		std::pair<const void*, size_t> raw_pkt = ps.raw_packet();
		
		udp_sq.send(SendQueue::SEND_PRI_MEDIUM, new SQB_TODO(raw_pkt.first, raw_pkt.second, from_addr,
			message_handler, message_handler_ctx, query));
	}
	else{
		/* Application rejected the DPNMSG_ENUM_HOSTS_QUERY message. */
	}
}

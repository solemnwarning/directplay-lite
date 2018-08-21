#include <atomic>
#include <dplay8.h>
#include <objbase.h>
#include <stdio.h>
#include <windows.h>

#include "DirectPlay8Peer.hpp"

#define UNIMPLEMENTED(fmt, ...) \
	fprintf(stderr, "Unimplemented method: " fmt "\n", ## __VA_ARGS__); \
	return E_NOTIMPL;

DirectPlay8Peer::DirectPlay8Peer(std::atomic<unsigned int> *global_refcount):
	global_refcount(global_refcount),
	local_refcount(0)
{
	AddRef();
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
	UNIMPLEMENTED("DirectPlay8Peer::Initialize");
}

HRESULT DirectPlay8Peer::EnumServiceProviders(CONST GUID* CONST pguidServiceProvider, CONST GUID* CONST pguidApplication, DPN_SERVICE_PROVIDER_INFO* CONST pSPInfoBuffer, DWORD* CONST pcbEnumData, DWORD* CONST pcReturned, CONST DWORD dwFlags)
{
	UNIMPLEMENTED("DirectPlay8Peer::EnumServiceProviders");
}

HRESULT DirectPlay8Peer::CancelAsyncOperation(CONST DPNHANDLE hAsyncHandle, CONST DWORD dwFlags)
{
	UNIMPLEMENTED("DirectPlay8Peer::CancelAsyncOperation");
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
	UNIMPLEMENTED("DirectPlay8Peer::Host");
}

HRESULT DirectPlay8Peer::GetApplicationDesc(DPN_APPLICATION_DESC* CONST pAppDescBuffer, DWORD* CONST pcbDataSize, CONST DWORD dwFlags)
{
	UNIMPLEMENTED("DirectPlay8Peer::GetApplicationDesc");
}

HRESULT DirectPlay8Peer::SetApplicationDesc(CONST DPN_APPLICATION_DESC* CONST pad, CONST DWORD dwFlags)
{
	UNIMPLEMENTED("DirectPlay8Peer::SetApplicationDesc");
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

HRESULT DirectPlay8Peer::SetPeerInfo(CONST DPN_PLAYER_INFO* CONST pdpnPlayerInfo,PVOID CONST pvAsyncContext, DPNHANDLE* CONST phAsyncHandle, CONST DWORD dwFlags)
{
	UNIMPLEMENTED("DirectPlay8Peer::SetPeerInfo");
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
	UNIMPLEMENTED("DirectPlay8Peer::Close");
}

HRESULT DirectPlay8Peer::EnumHosts(PDPN_APPLICATION_DESC CONST pApplicationDesc, IDirectPlay8Address* CONST pAddrHost, IDirectPlay8Address* CONST pDeviceInfo,PVOID CONST pUserEnumData, CONST DWORD dwUserEnumDataSize, CONST DWORD dwEnumCount, CONST DWORD dwRetryInterval, CONST DWORD dwTimeOut,PVOID CONST pvUserContext, DPNHANDLE* CONST pAsyncHandle, CONST DWORD dwFlags)
{
	UNIMPLEMENTED("DirectPlay8Peer::EnumHosts");
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

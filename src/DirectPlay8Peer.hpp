#ifndef DPLITE_DIRECTPLAY8PEER_HPP
#define DPLITE_DIRECTPLAY8PEER_HPP

#include <atomic>
#include <dplay8.h>
#include <map>
#include <objbase.h>
#include <stdint.h>

class DirectPlay8Peer: public IDirectPlay8Peer
{
	private:
		std::atomic<unsigned int> * const global_refcount;
		ULONG local_refcount;
		
		PFNDPNMESSAGEHANDLER message_handler;
		PVOID message_handler_ctx;
		
		enum {
			STATE_DISCONNECTED,
			STATE_HOSTING,
			STATE_CONNECTED,
		} state;
		
		GUID instance_guid;
		GUID application_guid;
		DWORD max_players;
		std::wstring session_name;
		std::wstring password;
		std::vector<unsigned char> application_data;
		
		int udp_socket;        /* UDP socket, used for general send/recv operations. */
		int listener_socket;   /* TCP listener socket. */
		int discovery_socket;  /* Discovery UDP sockets, RECIEVES broadcasts only. */
		
		struct Player
		{
			/* This is the TCP socket to the peer, we may have connected to it, or it
			 * may have connected to us depending who joined the session first, that
			 * doesn't really matter.
			*/
			int sock;
			
			uint32_t ip;    /* IPv4 address, network byte order. */
			uint16_t port;  /* Port, host byte order. */
		};
		
		std::map<DPNID, Player> peers;
		
	public:
		DirectPlay8Peer(std::atomic<unsigned int> *global_refcount);
		virtual ~DirectPlay8Peer();
		
		/* IUnknown */
		virtual HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void **ppvObject) override;
		virtual ULONG STDMETHODCALLTYPE AddRef(void) override;
		virtual ULONG STDMETHODCALLTYPE Release(void) override;
		
		/* IDirectPlay8Peer */
		virtual HRESULT STDMETHODCALLTYPE Initialize(PVOID CONST pvUserContext, CONST PFNDPNMESSAGEHANDLER pfn, CONST DWORD dwFlags) override;
		virtual HRESULT STDMETHODCALLTYPE EnumServiceProviders(CONST GUID* CONST pguidServiceProvider, CONST GUID* CONST pguidApplication, DPN_SERVICE_PROVIDER_INFO* CONST pSPInfoBuffer, DWORD* CONST pcbEnumData, DWORD* CONST pcReturned, CONST DWORD dwFlags) override;
		virtual HRESULT STDMETHODCALLTYPE CancelAsyncOperation(CONST DPNHANDLE hAsyncHandle, CONST DWORD dwFlags) override;
		virtual HRESULT STDMETHODCALLTYPE Connect(CONST DPN_APPLICATION_DESC* CONST pdnAppDesc, IDirectPlay8Address* CONST pHostAddr, IDirectPlay8Address* CONST pDeviceInfo, CONST DPN_SECURITY_DESC* CONST pdnSecurity, CONST DPN_SECURITY_CREDENTIALS* CONST pdnCredentials, CONST void* CONST pvUserConnectData, CONST DWORD dwUserConnectDataSize, void* CONST pvPlayerContext, void* CONST pvAsyncContext, DPNHANDLE* CONST phAsyncHandle, CONST DWORD dwFlags) override;
		virtual HRESULT STDMETHODCALLTYPE SendTo(CONST DPNID dpnid, CONST DPN_BUFFER_DESC* CONST prgBufferDesc, CONST DWORD cBufferDesc, CONST DWORD dwTimeOut, void* CONST pvAsyncContext, DPNHANDLE* CONST phAsyncHandle, CONST DWORD dwFlags) override;
		virtual HRESULT STDMETHODCALLTYPE GetSendQueueInfo(CONST DPNID dpnid, DWORD* CONST pdwNumMsgs, DWORD* CONST pdwNumBytes, CONST DWORD dwFlags) override;
		virtual HRESULT STDMETHODCALLTYPE Host(CONST DPN_APPLICATION_DESC* CONST pdnAppDesc, IDirectPlay8Address **CONST prgpDeviceInfo, CONST DWORD cDeviceInfo, CONST DPN_SECURITY_DESC* CONST pdnSecurity, CONST DPN_SECURITY_CREDENTIALS* CONST pdnCredentials, void* CONST pvPlayerContext, CONST DWORD dwFlags) override;
		virtual HRESULT STDMETHODCALLTYPE GetApplicationDesc(DPN_APPLICATION_DESC* CONST pAppDescBuffer, DWORD* CONST pcbDataSize, CONST DWORD dwFlags) override;
		virtual HRESULT STDMETHODCALLTYPE SetApplicationDesc(CONST DPN_APPLICATION_DESC* CONST pad, CONST DWORD dwFlags) override;
		virtual HRESULT STDMETHODCALLTYPE CreateGroup(CONST DPN_GROUP_INFO* CONST pdpnGroupInfo, void* CONST pvGroupContext, void* CONST pvAsyncContext, DPNHANDLE* CONST phAsyncHandle, CONST DWORD dwFlags) override;
		virtual HRESULT STDMETHODCALLTYPE DestroyGroup(CONST DPNID idGroup, PVOID CONST pvAsyncContext, DPNHANDLE* CONST phAsyncHandle, CONST DWORD dwFlags) override;
		virtual HRESULT STDMETHODCALLTYPE AddPlayerToGroup(CONST DPNID idGroup, CONST DPNID idClient, PVOID CONST pvAsyncContext, DPNHANDLE* CONST phAsyncHandle, CONST DWORD dwFlags) override;
		virtual HRESULT STDMETHODCALLTYPE RemovePlayerFromGroup(CONST DPNID idGroup, CONST DPNID idClient, PVOID CONST pvAsyncContext, DPNHANDLE* CONST phAsyncHandle, CONST DWORD dwFlags) override;
		virtual HRESULT STDMETHODCALLTYPE SetGroupInfo(CONST DPNID dpnid, DPN_GROUP_INFO* CONST pdpnGroupInfo,PVOID CONST pvAsyncContext, DPNHANDLE* CONST phAsyncHandle, CONST DWORD dwFlags) override;
		virtual HRESULT STDMETHODCALLTYPE GetGroupInfo(CONST DPNID dpnid, DPN_GROUP_INFO* CONST pdpnGroupInfo, DWORD* CONST pdwSize, CONST DWORD dwFlags) override;
		virtual HRESULT STDMETHODCALLTYPE EnumPlayersAndGroups(DPNID* CONST prgdpnid, DWORD* CONST pcdpnid, CONST DWORD dwFlags) override;
		virtual HRESULT STDMETHODCALLTYPE EnumGroupMembers(CONST DPNID dpnid, DPNID* CONST prgdpnid, DWORD* CONST pcdpnid, CONST DWORD dwFlags) override;
		virtual HRESULT STDMETHODCALLTYPE SetPeerInfo(CONST DPN_PLAYER_INFO* CONST pdpnPlayerInfo,PVOID CONST pvAsyncContext, DPNHANDLE* CONST phAsyncHandle, CONST DWORD dwFlags) override;
		virtual HRESULT STDMETHODCALLTYPE GetPeerInfo(CONST DPNID dpnid, DPN_PLAYER_INFO* CONST pdpnPlayerInfo, DWORD* CONST pdwSize, CONST DWORD dwFlags) override;
		virtual HRESULT STDMETHODCALLTYPE GetPeerAddress(CONST DPNID dpnid, IDirectPlay8Address** CONST pAddress, CONST DWORD dwFlags) override;
		virtual HRESULT STDMETHODCALLTYPE GetLocalHostAddresses(IDirectPlay8Address** CONST prgpAddress, DWORD* CONST pcAddress, CONST DWORD dwFlags) override;
		virtual HRESULT STDMETHODCALLTYPE Close(CONST DWORD dwFlags) override;
		virtual HRESULT STDMETHODCALLTYPE EnumHosts(PDPN_APPLICATION_DESC CONST pApplicationDesc, IDirectPlay8Address* CONST pAddrHost, IDirectPlay8Address* CONST pDeviceInfo,PVOID CONST pUserEnumData, CONST DWORD dwUserEnumDataSize, CONST DWORD dwEnumCount, CONST DWORD dwRetryInterval, CONST DWORD dwTimeOut,PVOID CONST pvUserContext, DPNHANDLE* CONST pAsyncHandle, CONST DWORD dwFlags) override;
		virtual HRESULT STDMETHODCALLTYPE DestroyPeer(CONST DPNID dpnidClient, CONST void* CONST pvDestroyData, CONST DWORD dwDestroyDataSize, CONST DWORD dwFlags) override;
		virtual HRESULT STDMETHODCALLTYPE ReturnBuffer(CONST DPNHANDLE hBufferHandle, CONST DWORD dwFlags) override;
		virtual HRESULT STDMETHODCALLTYPE GetPlayerContext(CONST DPNID dpnid,PVOID* CONST ppvPlayerContext, CONST DWORD dwFlags) override;
		virtual HRESULT STDMETHODCALLTYPE GetGroupContext(CONST DPNID dpnid,PVOID* CONST ppvGroupContext, CONST DWORD dwFlags) override;
		virtual HRESULT STDMETHODCALLTYPE GetCaps(DPN_CAPS* CONST pdpCaps, CONST DWORD dwFlags) override;
		virtual HRESULT STDMETHODCALLTYPE SetCaps(CONST DPN_CAPS* CONST pdpCaps, CONST DWORD dwFlags) override;
		virtual HRESULT STDMETHODCALLTYPE SetSPCaps(CONST GUID* CONST pguidSP, CONST DPN_SP_CAPS* CONST pdpspCaps, CONST DWORD dwFlags ) override;
		virtual HRESULT STDMETHODCALLTYPE GetSPCaps(CONST GUID* CONST pguidSP, DPN_SP_CAPS* CONST pdpspCaps, CONST DWORD dwFlags) override;
		virtual HRESULT STDMETHODCALLTYPE GetConnectionInfo(CONST DPNID dpnid, DPN_CONNECTION_INFO* CONST pdpConnectionInfo, CONST DWORD dwFlags) override;
		virtual HRESULT STDMETHODCALLTYPE RegisterLobby(CONST DPNHANDLE dpnHandle, struct IDirectPlay8LobbiedApplication* CONST pIDP8LobbiedApplication, CONST DWORD dwFlags) override;
		virtual HRESULT STDMETHODCALLTYPE TerminateSession(void* CONST pvTerminateData, CONST DWORD dwTerminateDataSize, CONST DWORD dwFlags) override;
};

#endif /* !DPLITE_DIRECTPLAY8PEER_HPP */

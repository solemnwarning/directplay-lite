#ifndef DPLITE_DIRECTPLAY8PEER_HPP
#define DPLITE_DIRECTPLAY8PEER_HPP

#include <winsock2.h>
#include <atomic>
#include <dplay8.h>
#include <map>
#include <mutex>
#include <objbase.h>
#include <stdint.h>
#include <windows.h>

#include "AsyncHandleAllocator.hpp"
#include "EventObject.hpp"
#include "HandleHandlingPool.hpp"
#include "HostEnumerator.hpp"
#include "network.hpp"
#include "packet.hpp"
#include "SendQueue.hpp"

class DirectPlay8Peer: public IDirectPlay8Peer
{
	private:
		std::atomic<unsigned int> * const global_refcount;
		ULONG local_refcount;
		
		PFNDPNMESSAGEHANDLER message_handler;
		PVOID message_handler_ctx;
		
		enum {
			STATE_NEW,
			STATE_INITIALISED,
			STATE_HOSTING,
			STATE_CONNECTING,
			STATE_CONNECT_FAILED,
			STATE_CONNECTED,
			STATE_CLOSING,
		} state;
		
		AsyncHandleAllocator handle_alloc;
		
		std::map<DPNHANDLE, HostEnumerator> host_enums;
		std::condition_variable host_enum_completed;
		
		GUID instance_guid;
		GUID application_guid;
		DWORD max_players;
		std::wstring session_name;
		std::wstring password;
		std::vector<unsigned char> application_data;
		
		GUID service_provider;
		
		/* Local IP and port for all our sockets, except discovery_socket. */
		uint32_t local_ip;
		uint16_t local_port;
		
		int udp_socket;        /* UDP socket, used for non-guaranteed send/recv operations. */
		int listener_socket;   /* TCP listener socket. */
		int discovery_socket;  /* Discovery UDP sockets, RECIEVES broadcasts only. */
		
		EventObject udp_socket_event;
		EventObject other_socket_event;
		
		HandleHandlingPool *worker_pool;
		
		SendQueue udp_sq;
		
		struct Peer
		{
			enum PeerState {
				/* Peer has connected to us, we're waiting for the initial message from it. */
				PS_ACCEPTED,
				
				/* We have started connecting to the host, waiting for async connect() to complete. */
				PS_CONNECTING_HOST,
				
				/* TCP connection to host open, waiting for response to DPLITE_MSGID_CONNECT_HOST. */
				PS_REQUESTING_HOST,
				
				/* We have started connecting to a peer, waiting for async connect() to complete. */
				PS_CONNECTING_PEER,
				
				/* TCP connection to peer open, waiting for response to DPLITE_MSGID_CONNECT_PEER. */
				PS_REQUESTING_PEER,
				
				/* We are the host and the peer has sent DPLITE_MSGID_CONNECT_HOST, we are waiting
				 * for the application to process DPN_MSGID_INDICATE_CONNECT before we either add the
				 * player to the session or reject it.
				*/
				PS_INDICATING,
				
				/* This is a fully-fledged peer. */
				PS_CONNECTED,
				
				/* This peer is closing down. Discard any future messages received from it, but flush
				 * anything waiting to be sent and keep the player DPNID valid until after the
				 * application has processed the DPN_MSGID_DESTROY_PLAYER message and any outstanding
				 * operations have completed or been cancelled.
				*/
				PS_CLOSING,
			};
			
			enum PeerState state;
			
			/* This is the TCP socket to the peer, we may have connected to it, or it
			 * may have connected to us depending who joined the session first, that
			 * doesn't really matter.
			*/
			int sock;
			
			uint32_t ip;    /* IPv4 address, network byte order. */
			uint16_t port;  /* TCP and UDP port, host byte order. */
			
			DPNID player_id;  /* Player ID, not initialised before state PS_CONNECTED. */
			void *player_ctx; /* Player context, not initialised before state PS_CONNECTED. */
			
			std::wstring player_name;
			std::vector<unsigned char> player_data;
			
			bool recv_busy;
			unsigned char recv_buf[MAX_PACKET_SIZE];
			size_t recv_buf_cur;
			
			EventObject event;
			long events;
			
			SendQueue sq;
			bool send_open;
			
			/* Some messages require confirmation of success/failure from the other
			 * peer. Each of these is assigned a rolling (per peer) ID, the callback
			 * associated to which is called when we get a DPLITE_MSGID_ACK.
			 */
			DWORD next_ack_id;
			std::map< DWORD, std::function<void(std::unique_lock<std::mutex>&, HRESULT)> > pending_acks;
			
			Peer(enum PeerState state, int sock, uint32_t ip, uint16_t port);
			
			bool enable_events(long events);
			bool disable_events(long events);
			
			DWORD alloc_ack_id();
			void register_ack(DWORD id, const std::function<void(std::unique_lock<std::mutex>&, HRESULT)> &callback);
		};
		
		DPNID local_player_id;
		void *local_player_ctx;
		
		/* Local player name and data used by GetPeerInfo() and SetPeerInfo() */
		std::wstring local_player_name;
		std::vector<unsigned char> local_player_data;
		
		DPNID next_player_id;
		DPNID host_player_id;
		
		unsigned int next_peer_id;
		std::map<unsigned int, Peer*> peers;
		std::condition_variable peer_destroyed;
		
		std::map<DPNID, unsigned int> player_to_peer_id;
		
		/* Serialises access to everything.
		 *
		 * All methods and event handlers hold this lock while executing. They will
		 * temporarily release it when executing the application message handler, after
		 * which they must reclaim it and check for any changes to the state which may
		 * affect them, such as a peer being destroyed.
		*/
		std::mutex lock;
		
		std::condition_variable connect_cv;
		
		void *connect_ctx;
		DPNHANDLE connect_handle;
		std::vector<unsigned char> connect_req_data;
		
		HRESULT connect_result;
		std::vector<unsigned char> connect_reply_data;
		
		Peer *get_peer_by_peer_id(unsigned int peer_id);
		Peer *get_peer_by_player_id(DPNID player_id);
		
		void handle_udp_socket_event();
		void io_udp_send(std::unique_lock<std::mutex> &l);
		void handle_other_socket_event();
		
		void io_peer_triggered(unsigned int peer_id);
		void io_peer_connected(std::unique_lock<std::mutex> &l, unsigned int peer_id);
		void io_peer_send(std::unique_lock<std::mutex> &l, unsigned int peer_id);
		void io_peer_recv(std::unique_lock<std::mutex> &l, unsigned int peer_id);
		
		void peer_accept(std::unique_lock<std::mutex> &l);
		bool peer_connect(Peer::PeerState initial_state, uint32_t remote_ip, uint16_t remote_port, DPNID player_id = 0);
		void peer_destroy(std::unique_lock<std::mutex> &l, unsigned int peer_id, HRESULT outstanding_op_result, DWORD destroy_player_reason);
		
		void close_everything_now(std::unique_lock<std::mutex> &l, HRESULT outstanding_op_result, DWORD destroy_player_reason);
		
		void handle_host_enum_request(std::unique_lock<std::mutex> &l, const PacketDeserialiser &pd, const struct sockaddr_in *from_addr);
		void handle_host_connect_request(std::unique_lock<std::mutex> &l, unsigned int peer_id, const PacketDeserialiser &pd);
		void handle_host_connect_ok(std::unique_lock<std::mutex> &l, unsigned int peer_id, const PacketDeserialiser &pd);
		void handle_host_connect_fail(std::unique_lock<std::mutex> &l, unsigned int peer_id, const PacketDeserialiser &pd);
		void handle_connect_peer(std::unique_lock<std::mutex> &l, unsigned int peer_id, const PacketDeserialiser &pd);
		void handle_connect_peer_ok(std::unique_lock<std::mutex> &l, unsigned int peer_id, const PacketDeserialiser &pd);
		void handle_connect_peer_fail(std::unique_lock<std::mutex> &l, unsigned int peer_id, const PacketDeserialiser &pd);
		void handle_message(std::unique_lock<std::mutex> &l, const PacketDeserialiser &pd);
		void handle_playerinfo(std::unique_lock<std::mutex> &l, unsigned int peer_id, const PacketDeserialiser &pd);
		void handle_ack(std::unique_lock<std::mutex> &l, unsigned int peer_id, const PacketDeserialiser &pd);
		void handle_appdesc(std::unique_lock<std::mutex> &l, unsigned int peer_id, const PacketDeserialiser &pd);
		
		void connect_check(std::unique_lock<std::mutex> &l);
		void connect_fail(std::unique_lock<std::mutex> &l, HRESULT hResultCode, const void *pvApplicationReplyData, DWORD dwApplicationReplyDataSize);
		
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

#ifndef DPLITE_HOSTENUMERATOR_HPP
#define DPLITE_HOSTENUMERATOR_HPP

#include <winsock2.h>
#include <dplay8.h>
#include <functional>
#include <thread>
#include <vector>
#include <windows.h>

#include "network.hpp"

#define DEFAULT_ENUM_COUNT    3
#define DEFAULT_ENUM_INTERVAL 1000
#define DEFAULT_ENUM_TIMEOUT  1000

class HostEnumerator
{
	private:
		/* No copy c'tor. */
		HostEnumerator(const HostEnumerator&) = delete;
		
		/* Pointer to the global refcount (if in use), for instantiating DirectPlay8Address objects. */
		std::atomic<unsigned int> * const global_refcount;
		
		/* DirectPlay message handler and context value */
		PFNDPNMESSAGEHANDLER message_handler;
		PVOID message_handler_ctx;
		
		std::function<void(HRESULT)> complete_cb;
		
		struct sockaddr_in send_addr;
		
		GUID application_guid;                 /* GUID of application to search for, or GUID_NULL */
		std::vector<unsigned char> user_data;  /* Data to include in request. */
		
		DWORD tx_remain;   /* Number of remaining requests to transmit, may be INFINITE. */
		DWORD tx_interval; /* Number of milliseconds to wait between transmits. */
		DWORD rx_timeout;  /* Number of milliseconds to wait for replies to a request. */
		
		void *user_context; /* DPNMSG_ENUM_HOSTS_REPONSE.pvUserContext */
		
		DWORD next_tx_at;
		DWORD stop_at;
		
		int sock;
		HANDLE wake_thread;
		std::thread *thread;
		bool req_cancel;
		
		unsigned char recv_buf[MAX_PACKET_SIZE];
		
		void main();
		void handle_packet(const void *data, size_t size, struct sockaddr_in *from_addr);
		
	public:
		HostEnumerator(
			std::atomic<unsigned int> * const global_refcount,
			
			PFNDPNMESSAGEHANDLER message_handler,
			PVOID message_handler_ctx,
			
			PDPN_APPLICATION_DESC const pApplicationDesc,
			IDirectPlay8Address *const pdpaddrHost,
			IDirectPlay8Address *const pdpaddrDeviceInfo,
			PVOID const pvUserEnumData,
			const DWORD dwUserEnumDataSize,
			const DWORD dwEnumCount,
			const DWORD dwRetryInterval,
			const DWORD dwTimeOut,
			PVOID const pvUserContext,
			
			std::function<void(HRESULT)> complete_cb
		);
		
		~HostEnumerator();
		
		void cancel();
		void wait();
};

#endif /* !DPLITE_HOSTENUMERATOR_HPP */

#ifndef DPLITE_SENDQUEUE_HPP
#define DPLITE_SENDQUEUE_HPP

#include <winsock2.h>

#include <functional>
#include <dplay8.h>
#include <list>
#include <mutex>
#include <set>
#include <stdlib.h>
#include <utility>
#include <vector>
#include <windows.h>

#include "packet.hpp"

class SendQueue
{
	public:
		enum SendPriority {
			SEND_PRI_LOW = 1,
			SEND_PRI_MEDIUM = 2,
			SEND_PRI_HIGH = 4,
		};
		
		class SendOp
		{
			private:
				std::vector<unsigned char> data;
				size_t sent_data;
				
				struct sockaddr_storage dest_addr;
				size_t dest_addr_size;
				
				std::function<void(std::unique_lock<std::mutex>&, HRESULT)> callback;
				
			public:
				const DPNHANDLE async_handle;
				
				SendOp(
					const void *data, size_t data_size,
					const struct sockaddr *dest_addr, size_t dest_addr_size,
					DPNHANDLE async_handle,
					const std::function<void(std::unique_lock<std::mutex>&, HRESULT)> &callback);
				
				std::pair<const void*, size_t> get_data() const;
				std::pair<const struct sockaddr*, size_t> get_dest_addr() const;
				
				void inc_sent_data(size_t sent);
				std::pair<const void*, size_t> get_pending_data() const;
				
				void invoke_callback(std::unique_lock<std::mutex> &l, HRESULT result) const;
		};
		
	private:
		std::list<SendOp*> low_queue;
		std::list<SendOp*> medium_queue;
		std::list<SendOp*> high_queue;
		
		SendOp *current;
		
		HANDLE signal_on_queue;
		
	public:
		SendQueue(HANDLE signal_on_queue): current(NULL), signal_on_queue(signal_on_queue) {}
		
		/* No copy c'tor. */
		SendQueue(const SendQueue &src) = delete;
		
		void send(SendPriority priority, const PacketSerialiser &ps, const struct sockaddr_in *dest_addr, const std::function<void(std::unique_lock<std::mutex>&, HRESULT)> &callback);
		void send(SendPriority priority, const PacketSerialiser &ps, const struct sockaddr_in *dest_addr, DPNHANDLE async_handle, const std::function<void(std::unique_lock<std::mutex>&, HRESULT)> &callback);
		
		SendOp *get_pending();
		void pop_pending(SendOp *op);
		
		SendOp *remove_queued();
		SendOp *remove_queued_by_handle(DPNHANDLE async_handle);
		SendOp *remove_queued_by_priority(SendPriority priority);
		bool handle_is_pending(DPNHANDLE async_handle);
};

#endif /* !DPLITE_SENDQUEUE_HPP */

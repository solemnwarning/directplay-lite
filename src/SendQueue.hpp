#ifndef DPLITE_SENDQUEUE_HPP
#define DPLITE_SENDQUEUE_HPP

#include <winsock2.h>

#include <list>
#include <set>
#include <stdlib.h>
#include <utility>
#include <vector>

class SendQueue
{
	public:
		enum SendPriority {
			SEND_PRI_LOW = 1,
			SEND_PRI_MEDIUM = 2,
			SEND_PRI_HIGH = 4,
		};
		
		class Buffer {
			private:
				std::vector<unsigned char> data;
				
				struct sockaddr_storage dest_addr;
				int dest_addr_len;
				
			protected:
				Buffer(const void *data, size_t data_size, const struct sockaddr *dest_addr = NULL, int dest_addr_len = 0);
				
			public:
				virtual ~Buffer();
				
				std::pair<const void*, size_t> get_data();
				
				std::pair<const struct sockaddr*, int> get_dest_addr();
				
				virtual void complete(HRESULT result) = 0;
		};
		
	private:
		std::list<Buffer*> low_queue;
		std::list<Buffer*> medium_queue;
		std::list<Buffer*> high_queue;
		
		Buffer *current;
		
	public:
		SendQueue(): current(NULL) {}
		
		/* No copy c'tor. */
		SendQueue(const SendQueue &src) = delete;
		
		void send(SendPriority priority, Buffer *buffer);
		
		Buffer *get_next();
		void complete(Buffer *buffer, HRESULT result);
};

#endif /* !DPLITE_SENDQUEUE_HPP */

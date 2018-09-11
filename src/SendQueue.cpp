#include <assert.h>

#include "SendQueue.hpp"

void SendQueue::send(SendPriority priority, Buffer *buffer)
{
	switch(priority)
	{
		case SEND_PRI_LOW:
			low_queue.push_back(buffer);
			break;
			
		case SEND_PRI_MEDIUM:
			medium_queue.push_back(buffer);
			break;
			
		case SEND_PRI_HIGH:
			high_queue.push_back(buffer);
			break;
	}
}

SendQueue::Buffer *SendQueue::get_next()
{
	if(current != NULL)
	{
		return current;
	}
	
	if(!high_queue.empty())
	{
		current = high_queue.front();
		high_queue.pop_front();
	}
	else if(!medium_queue.empty())
	{
		current = medium_queue.front();
		medium_queue.pop_front();
	}
	else if(!low_queue.empty())
	{
		current = low_queue.front();
		low_queue.pop_front();
	}
	
	return current;
}

void SendQueue::complete(SendQueue::Buffer *buffer, HRESULT result)
{
	assert(buffer == current);
	
	current = NULL;
	
	buffer->complete(result);
	delete buffer;
}

SendQueue::Buffer::Buffer(const void *data, size_t data_size, const struct sockaddr *dest_addr, int dest_addr_len):
	data((const unsigned char*)(data), (const unsigned char*)(data) + data_size)
{
	assert((size_t)(dest_addr_len) <= sizeof(this->dest_addr));
	
	memcpy(&(this->dest_addr), dest_addr, dest_addr_len);
	this->dest_addr_len = dest_addr_len;
}

SendQueue::Buffer::~Buffer() {}

std::pair<const void*, size_t> SendQueue::Buffer::get_data()
{
	return std::make_pair<const void*, size_t>(data.data(), data.size());
}

std::pair<const struct sockaddr*, int> SendQueue::Buffer::get_dest_addr()
{
	return std::make_pair<const struct sockaddr*, int>((struct sockaddr*)(&dest_addr), (int)(dest_addr_len));
}

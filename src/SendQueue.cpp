#include <assert.h>
#include <winsock2.h>
#include <windows.h>

#include "SendQueue.hpp"

void SendQueue::send(SendPriority priority, const PacketSerialiser &ps, const struct sockaddr_in *dest_addr, const std::function<void(std::unique_lock<std::mutex>&, HRESULT)> &callback)
{
	std::pair<const void*, size_t> data = ps.raw_packet();
	
	SendOp *op = new SendOp(
		data.first, data.second,
		(const struct sockaddr*)(dest_addr), (dest_addr != NULL ? sizeof(*dest_addr) : 0),
		callback);
	
	switch(priority)
	{
		case SEND_PRI_LOW:
			low_queue.push_back(op);
			break;
			
		case SEND_PRI_MEDIUM:
			medium_queue.push_back(op);
			break;
			
		case SEND_PRI_HIGH:
			high_queue.push_back(op);
			break;
	}
	
	SetEvent(signal_on_queue);
}

SendQueue::SendOp *SendQueue::get_pending()
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

void SendQueue::pop_pending(SendQueue::SendOp *op)
{
	assert(op == current);
	current = NULL;
}

SendQueue::SendOp::SendOp(const void *data, size_t data_size,
	const struct sockaddr *dest_addr, size_t dest_addr_size,
	const std::function<void(std::unique_lock<std::mutex>&, HRESULT)> &callback):
	
	data((const unsigned char*)(data), (const unsigned char*)(data) + data_size),
	sent_data(0),
	callback(callback)
{
	assert((size_t)(dest_addr_size) <= sizeof(this->dest_addr));
	
	memcpy(&(this->dest_addr), dest_addr, dest_addr_size);
	this->dest_addr_size = dest_addr_size;
}

std::pair<const void*, size_t> SendQueue::SendOp::get_data() const
{
	return std::make_pair<const void*, size_t>(data.data(), data.size());
}

std::pair<const struct sockaddr*, size_t> SendQueue::SendOp::get_dest_addr() const
{
	return std::make_pair((const struct sockaddr*)(&dest_addr), dest_addr_size);
}

void SendQueue::SendOp::inc_sent_data(size_t sent)
{
	sent_data += sent;
	assert(sent_data <= data.size());
}

std::pair<const void*, size_t> SendQueue::SendOp::get_pending_data() const
{
	return std::make_pair<const void*, size_t>(data.data() + sent_data, data.size() - sent_data);
}

void SendQueue::SendOp::invoke_callback(std::unique_lock<std::mutex> &l, HRESULT result) const
{
	callback(l, result);
}

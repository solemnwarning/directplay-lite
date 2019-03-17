/* DirectPlay Lite
 * Copyright (C) 2018 Daniel Collins
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
*/

#include <assert.h>
#include <winsock2.h>
#include <windows.h>

#include "SendQueue.hpp"

void SendQueue::send(SendPriority priority, const PacketSerialiser &ps,
	const struct sockaddr_in *dest_addr,
	const std::function<void(std::unique_lock<std::mutex>&, HRESULT)> &callback)
{
	send(priority, ps, dest_addr, 0, callback);
}

void SendQueue::send(SendPriority priority, const PacketSerialiser &ps,
	const struct sockaddr_in *dest_addr, DPNHANDLE async_handle,
	const std::function<void(std::unique_lock<std::mutex>&, HRESULT)> &callback)
{
	std::pair<const void*, size_t> data = ps.raw_packet();
	
	SendOp *op = new SendOp(
		data.first, data.second,
		(const struct sockaddr*)(dest_addr), (dest_addr != NULL ? sizeof(*dest_addr) : 0),
		async_handle,
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

/* NOTE: The remove_queued() family of methods will ONLY return SendOps which
 * have a nonzero async_handle. This is for cancelling application-created SendOps
 * without also aborting internal ones.
*/

SendQueue::SendOp *SendQueue::remove_queued()
{
	std::list<SendOp*> *queues[] = { &high_queue, &medium_queue, &low_queue };
	
	for(int i = 0; i < 3; ++i)
	{
		for(auto it = queues[i]->begin(); it != queues[i]->end(); ++it)
		{
			SendOp *op = *it;
			
			if(op->async_handle != 0)
			{
				queues[i]->erase(it);
				return op;
			}
		}
	}
	
	return NULL;
}

SendQueue::SendOp *SendQueue::remove_queued_by_handle(DPNHANDLE async_handle)
{
	std::list<SendOp*> *queues[] = { &low_queue, &medium_queue, &high_queue };
	
	for(int i = 0; i < 3; ++i)
	{
		for(auto it = queues[i]->begin(); it != queues[i]->end(); ++it)
		{
			SendOp *op = *it;
			
			if(op->async_handle != 0 && op->async_handle == async_handle)
			{
				queues[i]->erase(it);
				return op;
			}
		}
	}
	
	return NULL;
}

SendQueue::SendOp *SendQueue::remove_queued_by_priority(SendPriority priority)
{
	std::list<SendOp*> *queue;
	
	switch(priority)
	{
		case SEND_PRI_LOW:
			queue = &low_queue;
			break;
			
		case SEND_PRI_MEDIUM:
			queue = &medium_queue;
			break;
			
		case SEND_PRI_HIGH:
			queue = &high_queue;
			break;
	}
	
	for(auto it = queue->begin(); it != queue->end(); ++it)
	{
		SendOp *op = *it;
		
		if(op->async_handle != 0)
		{
			queue->erase(it);
			return op;
		}
	}
	
	return NULL;
}

bool SendQueue::handle_is_pending(DPNHANDLE async_handle)
{
	return (current != NULL && current->async_handle == async_handle);
}

SendQueue::SendOp::SendOp(const void *data, size_t data_size,
	const struct sockaddr *dest_addr, size_t dest_addr_size,
	DPNHANDLE async_handle,
	const std::function<void(std::unique_lock<std::mutex>&, HRESULT)> &callback):
	
	data((const unsigned char*)(data), (const unsigned char*)(data) + data_size),
	sent_data(0),
	async_handle(async_handle),
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

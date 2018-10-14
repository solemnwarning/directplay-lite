#include <dplay8.h>

#include "AsyncHandleAllocator.hpp"

AsyncHandleAllocator::AsyncHandleAllocator():
	next_enum_id(1),
	next_connect_id(1),
	next_send_id(1),
	next_pinfo_id(1) {}

DPNHANDLE AsyncHandleAllocator::new_enum()
{
	DPNHANDLE handle = next_enum_id++ | TYPE_ENUM;
	
	next_enum_id &= ~TYPE_MASK;
	if(next_enum_id == 0)
	{
		next_enum_id = 1;
	}
	
	return handle;
}

DPNHANDLE AsyncHandleAllocator::new_connect()
{
	DPNHANDLE handle = next_connect_id++ | TYPE_CONNECT;
	
	next_connect_id &= ~TYPE_MASK;
	if(next_connect_id == 0)
	{
		next_connect_id = 1;
	}
	
	return handle;
}

DPNHANDLE AsyncHandleAllocator::new_send()
{
	DPNHANDLE handle = next_send_id++ | TYPE_SEND;
	
	next_send_id &= ~TYPE_MASK;
	if(next_send_id == 0)
	{
		next_send_id = 1;
	}
	
	return handle;
}

DPNHANDLE AsyncHandleAllocator::new_pinfo()
{
	DPNHANDLE handle = next_pinfo_id++ | TYPE_PINFO;
	
	next_pinfo_id &= ~TYPE_MASK;
	if(next_pinfo_id == 0)
	{
		next_pinfo_id = 1;
	}
	
	return handle;
}

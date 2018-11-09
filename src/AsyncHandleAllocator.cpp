#include <dplay8.h>

#include "AsyncHandleAllocator.hpp"

AsyncHandleAllocator::AsyncHandleAllocator():
	next_enum_id(1),
	next_connect_id(1),
	next_send_id(1),
	next_pinfo_id(1),
	next_cgroup_id(1),
	next_dgroup_id(1),
	next_apgroup_id(1),
	next_rpgroup_id(1) {}

static DPNHANDLE new_XXX(DPNHANDLE *next, DPNHANDLE type)
{
	DPNHANDLE handle = (*next)++ | type;
	
	(*next) &= ~AsyncHandleAllocator::TYPE_MASK;
	if((*next) == 0)
	{
		(*next) = 1;
	}
	
	return handle;
}

DPNHANDLE AsyncHandleAllocator::new_enum()
{
	return new_XXX(&next_enum_id, TYPE_ENUM);
}

DPNHANDLE AsyncHandleAllocator::new_connect()
{
	return new_XXX(&next_connect_id, TYPE_CONNECT);
}

DPNHANDLE AsyncHandleAllocator::new_send()
{
	return new_XXX(&next_send_id, TYPE_SEND);
}

DPNHANDLE AsyncHandleAllocator::new_pinfo()
{
	return new_XXX(&next_pinfo_id, TYPE_PINFO);
}

DPNHANDLE AsyncHandleAllocator::new_cgroup()
{
	return new_XXX(&next_cgroup_id, TYPE_CGROUP);
}

DPNHANDLE AsyncHandleAllocator::new_dgroup()
{
	return new_XXX(&next_dgroup_id, TYPE_DGROUP);
}

DPNHANDLE AsyncHandleAllocator::new_apgroup()
{
	return new_XXX(&next_apgroup_id, TYPE_APGROUP);
}

DPNHANDLE AsyncHandleAllocator::new_rpgroup()
{
	return new_XXX(&next_rpgroup_id, TYPE_RPGROUP);
}

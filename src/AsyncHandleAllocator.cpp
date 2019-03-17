/* DirectPlay Lite
 * Copyright (C) 2018 Daniel Collins <solemnwarning@solemnwarning.net>
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

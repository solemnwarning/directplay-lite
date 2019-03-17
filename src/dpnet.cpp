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

#include <winsock2.h>
#include <atomic>
#include <dplay8.h>
#include <objbase.h>
#include <windows.h>

#include "DirectPlay8Address.hpp"
#include "DirectPlay8Peer.hpp"
#include "Factory.hpp"

/* Sum of refcounts of all created COM objects. */
static std::atomic<unsigned int> global_refcount;

BOOL WINAPI DllMain(HINSTANCE hinstDLL, DWORD fdwReason, LPVOID lpvReserved)
{
	if(fdwReason == DLL_PROCESS_ATTACH)
	{
		global_refcount = 0;
	}
	
	return TRUE;
}

HRESULT CALLBACK DllCanUnloadNow()
{
	return (global_refcount == 0 ? S_OK : S_FALSE);
}

HRESULT CALLBACK DllGetClassObject(REFCLSID rclsid, REFIID riid, LPVOID *ppv)
{
	if(riid != IID_IClassFactory && riid != IID_IUnknown)
	{
		return CLASS_E_CLASSNOTAVAILABLE;
	}
	
	if(rclsid == CLSID_DirectPlay8Address)
	{
		*((IUnknown**)(ppv)) = new Factory<DirectPlay8Address, IID_IDirectPlay8Address>(&global_refcount);
		return S_OK;
	}
	else if(rclsid == CLSID_DirectPlay8Peer)
	{
		*((IUnknown**)(ppv)) = new Factory<DirectPlay8Peer, IID_IDirectPlay8Peer>(&global_refcount);
		return S_OK;
	}
	else{
		return CLASS_E_CLASSNOTAVAILABLE;
	}
}

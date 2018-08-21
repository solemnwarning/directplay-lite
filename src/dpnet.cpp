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

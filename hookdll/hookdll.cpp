#include <winsock2.h>
#include <MinHook.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>

#include "../src/DirectPlay8Address.hpp"
#include "../src/DirectPlay8Peer.hpp"
#include "../src/Factory.hpp"

static HMODULE dll_handle = NULL;
static unsigned int coinit_depth = 0;

static DWORD DirectPlay8Address_cookie;
static DWORD DirectPlay8Peer_cookie;

static HRESULT (__stdcall *real_CoInitialize)(LPVOID) = NULL;
static HRESULT (__stdcall *real_CoInitializeEx)(LPVOID, DWORD) = NULL;
static void    (__stdcall *real_CoUninitialize)() = NULL;

static HRESULT __stdcall hook_CoInitialize(LPVOID pvReserved);
static HRESULT __stdcall hook_CoInitializeEx(LPVOID pvReserved, DWORD dwCoInit);
static void    __stdcall hook_CoUninitialize();

template<typename CLASS, const CLSID &CLASS_ID, const IID &INTERFACE_ID> void register_class(DWORD *cookie);

extern "C" BOOL WINAPI DllMain(HINSTANCE hinstDLL, DWORD fdwReason, LPVOID lpvReserved)
{
	if(fdwReason == DLL_PROCESS_ATTACH)
	{
		if(MH_Initialize() != MH_OK)
		{
			/* TODO: LOG ME */
			return FALSE;
		}
		
		if(MH_CreateHook(&CoInitialize, &hook_CoInitialize, (LPVOID*)(&real_CoInitialize)) != MH_OK
			|| MH_EnableHook(&CoInitialize) != MH_OK
			|| MH_CreateHook(&CoInitializeEx, &hook_CoInitializeEx, (LPVOID*)(&real_CoInitializeEx)) != MH_OK
			|| MH_EnableHook(&CoInitializeEx) != MH_OK
			|| MH_CreateHook(&CoUninitialize, &hook_CoUninitialize, (LPVOID*)(&real_CoUninitialize)) != MH_OK
			|| MH_EnableHook(&CoUninitialize) != MH_OK)
		{
			/* TODO: LOG ME */
			abort();
		}
	}
	else if(fdwReason == DLL_PROCESS_DETACH && lpvReserved == NULL)
	{
		if(MH_RemoveHook(&CoUninitialize) != MH_OK
			|| MH_RemoveHook(&CoInitializeEx) != MH_OK 
			|| MH_RemoveHook(&CoInitialize) != MH_OK
			|| MH_Uninitialize() != MH_OK)
		{
			/* TODO: LOG ME */
			abort();
		}
	}
	
	return TRUE;
}

extern "C" void* __stdcall find_sym(const char *dll_name, const char *sym_name)
{
	if(dll_handle == NULL)
	{
		char path[512];
		GetSystemDirectory(path, sizeof(path));
		
		if(strlen(path) + strlen(dll_name) + 2 > sizeof(path))
		{
			abort();
		}
		
		strcat(path, "\\");
		strcat(path, dll_name);
		
		dll_handle = LoadLibrary(path);
		if(dll_handle == NULL)
		{
			/* TODO: LOG ME */
			abort();
		}
	}
	
	void *sym_addr = GetProcAddress(dll_handle, sym_name);
	if(sym_addr == NULL)
	{
		/* TODO: LOG ME */
		abort();
	}
	
	return sym_addr;
}

static HRESULT __stdcall hook_CoInitialize(LPVOID pvReserved)
{
	HRESULT res = real_CoInitialize(pvReserved);
	if((res == S_OK || res == S_FALSE) && ++coinit_depth == 1)
	{
		/* Register COM classes. */
		
		register_class<DirectPlay8Address, CLSID_DirectPlay8Address, IID_IDirectPlay8Address>(&DirectPlay8Address_cookie);
		register_class<DirectPlay8Peer,    CLSID_DirectPlay8Peer,    IID_IDirectPlay8Peer>   (&DirectPlay8Peer_cookie);
	}
	
	return res;
}

static HRESULT __stdcall hook_CoInitializeEx(LPVOID pvReserved, DWORD dwCoInit)
{
	HRESULT res = real_CoInitializeEx(pvReserved, dwCoInit);
	if((res == S_OK || res == S_FALSE) && ++coinit_depth == 1)
	{
		/* Register COM classes. */
		
		register_class<DirectPlay8Address, CLSID_DirectPlay8Address, IID_IDirectPlay8Address>(&DirectPlay8Address_cookie);
		register_class<DirectPlay8Peer,    CLSID_DirectPlay8Peer,    IID_IDirectPlay8Peer>   (&DirectPlay8Peer_cookie);
	}
	
	return res;
}

static void __stdcall hook_CoUninitialize()
{
	if(--coinit_depth == 0)
	{
		/* Unregister COM classes. */
		
		CoRevokeClassObject(DirectPlay8Peer_cookie);
		CoRevokeClassObject(DirectPlay8Address_cookie);
	}
	
	real_CoUninitialize();
}

template<typename CLASS, const CLSID &CLASS_ID, const IID &INTERFACE_ID> void register_class(DWORD *cookie)
{
	IClassFactory *factory = new Factory<CLASS, INTERFACE_ID>(NULL);
	
	if(CoRegisterClassObject(CLASS_ID, factory, CLSCTX_INPROC_SERVER, REGCLS_MULTIPLEUSE, cookie) != 0)
	{
		/* TODO: LOG ME */
		abort();
	}
	
	/* CoRegisterClassObject() calls AddRef(), release our reference to the factory. */
	factory->Release();
}

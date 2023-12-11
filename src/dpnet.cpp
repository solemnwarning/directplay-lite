/* DirectPlay Lite
 * Copyright (C) 2018-2023 Daniel Collins <solemnwarning@solemnwarning.net>
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
#include <olectl.h>
#include <windows.h>

#include "DirectPlay8Address.hpp"
#include "DirectPlay8Peer.hpp"
#include "Factory.hpp"

struct DllClass
{
	GUID clsid;
	const char *desc;

	CreateFactoryInstanceFunc CreateFactoryInstance;
};

DllClass DLL_CLASSES[] = {
	{ CLSID_DirectPlay8Address, "DirectPlay8Address Object", &Factory<DirectPlay8Address, IID_IDirectPlay8Address>::CreateFactoryInstance },
	{ CLSID_DirectPlay8Peer,    "DirectPlay8Peer Object",    &Factory<DirectPlay8Peer,    IID_IDirectPlay8Peer   >::CreateFactoryInstance },
};

size_t NUM_DLL_CLASSES = sizeof(DLL_CLASSES) / sizeof(*DLL_CLASSES);

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

	for (size_t i = 0; i < NUM_DLL_CLASSES; ++i)
	{
		if (rclsid == DLL_CLASSES[i].clsid)
		{
			return DLL_CLASSES[i].CreateFactoryInstance(ppv, &global_refcount);
		}
	}
	
	return CLASS_E_CLASSNOTAVAILABLE;
}

HRESULT CALLBACK DllRegisterServer()
{
	HRESULT status = S_OK;

	HMODULE this_dll;
	if (!GetModuleHandleEx(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, (TCHAR*)(&DllRegisterServer), &this_dll))
	{
		return E_UNEXPECTED;
	}

	char this_dll_path[MAX_PATH];
	if (GetModuleFileNameA(this_dll, this_dll_path, sizeof(this_dll_path)) != ERROR_SUCCESS)
	{
		return E_UNEXPECTED;
	}

	for (size_t i = 0; i < NUM_DLL_CLASSES; ++i)
	{
		OLECHAR *clsid_string;
		HRESULT status2 = StringFromCLSID(DLL_CLASSES[i].clsid, &clsid_string);
		if (status2 == E_OUTOFMEMORY)
		{
			status = E_OUTOFMEMORY;
			continue;
		}
		else if (status2 != S_OK)
		{
			status = SELFREG_E_CLASS;
			continue;
		}

		char clsid_path[72];
		snprintf(clsid_path, sizeof(clsid_path), "SOFTWARE\\Classes\\CLSID\\%ls", clsid_string);

		HKEY clsid_key;
		if (RegCreateKeyExA(
			HKEY_LOCAL_MACHINE,
			clsid_path,
			0,
			NULL,
			0,
			KEY_READ | KEY_WRITE,
			NULL,
			&clsid_key,
			NULL) == ERROR_SUCCESS)
		{
			if (RegSetValueExA(clsid_key, NULL, 0, REG_SZ, (BYTE*)(DLL_CLASSES[i].desc), strlen(DLL_CLASSES[i].desc) + 1) != ERROR_SUCCESS)
			{
				status = SELFREG_E_CLASS;
			}

			HKEY ips32_key;
			if (RegCreateKeyExA(
				clsid_key,
				"InprocServer32",
				0,
				NULL,
				0,
				KEY_READ | KEY_WRITE,
				NULL,
				&ips32_key,
				NULL) == ERROR_SUCCESS)
			{
				if (RegSetValueExA(clsid_key, NULL, 0, REG_SZ, (BYTE*)(this_dll_path), strlen(this_dll_path) + 1) != ERROR_SUCCESS)
				{
					status = SELFREG_E_CLASS;
				}

				if (RegSetValueExA(clsid_key, "ThreadingModel", 0, REG_SZ, (BYTE*)("Both"), strlen("Both") + 1) != ERROR_SUCCESS)
				{
					status = SELFREG_E_CLASS;
				}

				RegCloseKey(ips32_key);
			}
			else {
				status = SELFREG_E_CLASS;
			}

			RegCloseKey(clsid_key);
		}
		else {
			status = SELFREG_E_CLASS;
		}

		CoTaskMemFree(clsid_string);
	}

	return status;
}

HRESULT CALLBACK DllUnregisterServer()
{
	HRESULT status = S_OK;

	for (size_t i = 0; i < NUM_DLL_CLASSES; ++i)
	{
		OLECHAR *clsid_string;
		HRESULT status2 = StringFromCLSID(DLL_CLASSES[i].clsid, &clsid_string);
		if (status2 == E_OUTOFMEMORY)
		{
			status = E_OUTOFMEMORY;
			continue;
		}
		else if (status2 != S_OK)
		{
			status = SELFREG_E_CLASS;
			continue;
		}

		char clsid_path[72];
		snprintf(clsid_path, sizeof(clsid_path), "SOFTWARE\\Classes\\CLSID\\%ls", clsid_string);

		/* From the DllUnregisterServer() documentation on MSDN:
		 *
		 * > The server must not disturb any entries that it did not create which currently exist
		 * > for its object classes. For example, between registration and unregistration, the user
		 * > may have specified a Treat As relationship between this class and another. In that
		 * > case, unregistration can remove all entries except the TreatAs key and any others that
		 * > were not explicitly created in DllRegisterServer. The registry functions specifically
		 * > disallow the deletion of an entire populated tree in the registry. The server can
		 * > attempt, as the last step, to remove the CLSID key, but if other entries still exist,
		 * > the key will remain.
		 *
		 * And that is why we don't check whether RegDeleteKey() succeeds below.
		*/

		HKEY clsid_key;
		DWORD err = RegOpenKeyExA(
			HKEY_LOCAL_MACHINE,
			clsid_path,
			0,
			KEY_READ | KEY_WRITE,
			&clsid_key);

		if (err == ERROR_SUCCESS)
		{
			err = RegDeleteValueA(clsid_key, NULL);
			if (err != ERROR_SUCCESS && err != ERROR_FILE_NOT_FOUND)
			{
				status = SELFREG_E_CLASS;
			}

			HKEY ips32_key;
			err = RegOpenKeyExA(
				clsid_key,
				"InprocServer32",
				0,
				KEY_READ | KEY_WRITE,
				&ips32_key);

			if (err == ERROR_SUCCESS)
			{
				err = RegDeleteValueA(ips32_key, NULL);
				if (err != ERROR_SUCCESS && err != ERROR_FILE_NOT_FOUND)
				{
					status = SELFREG_E_CLASS;
				}

				err = RegDeleteValueA(ips32_key, "ThreadingModel");
				if (err != ERROR_SUCCESS && err != ERROR_FILE_NOT_FOUND)
				{
					status = SELFREG_E_CLASS;
				}

				RegCloseKey(ips32_key);
				RegDeleteKeyA(clsid_key, "InprocServer32");
			}

			RegCloseKey(clsid_key);
			RegDeleteKeyA(HKEY_LOCAL_MACHINE, clsid_path);
		}
		else if (err != ERROR_FILE_NOT_FOUND)
		{
			status = SELFREG_E_CLASS;
		}

		CoTaskMemFree(clsid_string);
	}

	return status;
}

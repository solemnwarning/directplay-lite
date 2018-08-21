#include <atomic>
#include <dplay8.h>
#include <objbase.h>
#include <stdio.h>
#include <windows.h>

#include "DirectPlay8Address.hpp"

#define UNIMPLEMENTED(fmt, ...) \
	fprintf(stderr, "Unimplemented method: " fmt "\n", ## __VA_ARGS__); \
	return E_NOTIMPL;

DirectPlay8Address::DirectPlay8Address(std::atomic<unsigned int> *global_refcount):
	global_refcount(global_refcount),
	local_refcount(0)
{
	AddRef();
}

HRESULT DirectPlay8Address::QueryInterface(REFIID riid, void **ppvObject)
{
	if(riid == IID_IDirectPlay8Address || riid == IID_IUnknown)
	{
		*((IUnknown**)(ppvObject)) = this;
		AddRef();
		
		return S_OK;
	}
	else{
		return E_NOINTERFACE;
	}
}

ULONG DirectPlay8Address::AddRef(void)
{
	if(global_refcount != NULL)
	{
		++(*global_refcount);
	}
	
	return ++local_refcount;
}

ULONG DirectPlay8Address::Release(void)
{
	std::atomic<unsigned int> *global_refcount = this->global_refcount;
	
	ULONG rc = --local_refcount;
	if(rc == 0)
	{
		delete this;
	}
	
	if(global_refcount != NULL)
	{
		--(*global_refcount);
	}
	
	return rc;
}

HRESULT DirectPlay8Address::BuildFromURLW(WCHAR* pwszSourceURL)
{
	UNIMPLEMENTED("DirectPlay8Address::BuildFromURLW");
}

HRESULT DirectPlay8Address::BuildFromURLA(CHAR* pszSourceURL)
{
	UNIMPLEMENTED("DirectPlay8Address::BuildFromURLA");
}

HRESULT DirectPlay8Address::Duplicate(PDIRECTPLAY8ADDRESS* ppdpaNewAddress)
{
	UNIMPLEMENTED("DirectPlay8Address::Duplicate");
}

HRESULT DirectPlay8Address::SetEqual(PDIRECTPLAY8ADDRESS pdpaAddress)
{
	UNIMPLEMENTED("DirectPlay8Address::SetEqual");
}

HRESULT DirectPlay8Address::IsEqual(PDIRECTPLAY8ADDRESS pdpaAddress)
{
	UNIMPLEMENTED("DirectPlay8Address::IsEqual");
}

HRESULT DirectPlay8Address::Clear()
{
	UNIMPLEMENTED("DirectPlay8Address::Clear");
}

HRESULT DirectPlay8Address::GetURLW(WCHAR* pwszURL, PDWORD pdwNumChars)
{
	UNIMPLEMENTED("DirectPlay8Address::GetURLW");
}

HRESULT DirectPlay8Address::GetURLA(CHAR* pszURL, PDWORD pdwNumChars)
{
	UNIMPLEMENTED("DirectPlay8Address::GetURLA");
}

HRESULT DirectPlay8Address::GetSP(GUID* pguidSP)
{
	UNIMPLEMENTED("DirectPlay8Address::GetSP");
}

HRESULT DirectPlay8Address::GetUserData(LPVOID pvUserData, PDWORD pdwBufferSize)
{
	UNIMPLEMENTED("DirectPlay8Address::GetUserData");
}

HRESULT DirectPlay8Address::SetSP(CONST GUID* CONST pguidSP)
{
	UNIMPLEMENTED("DirectPlay8Address::SetSP");
}

HRESULT DirectPlay8Address::SetUserData(CONST void* CONST pvUserData, CONST DWORD dwDataSize)
{
	UNIMPLEMENTED("DirectPlay8Address::SetUserData");
}

HRESULT DirectPlay8Address::GetNumComponents(PDWORD pdwNumComponents)
{
	UNIMPLEMENTED("DirectPlay8Address::GetNumComponents");
}

HRESULT DirectPlay8Address::GetComponentByName(CONST WCHAR* CONST pwszName, LPVOID pvBuffer, PDWORD pdwBufferSize, PDWORD pdwDataType)
{
	UNIMPLEMENTED("DirectPlay8Address::GetComponentByName");
}

HRESULT DirectPlay8Address::GetComponentByIndex(CONST DWORD dwComponentID, WCHAR* pwszName, PDWORD pdwNameLen, void* pvBuffer, PDWORD pdwBufferSize, PDWORD pdwDataType)
{
	UNIMPLEMENTED("DirectPlay8Address::GetComponentByIndex");
}

HRESULT DirectPlay8Address::AddComponent(CONST WCHAR* CONST pwszName, CONST void* CONST lpvData, CONST DWORD dwDataSize, CONST DWORD dwDataType)
{
	UNIMPLEMENTED("DirectPlay8Address::AddComponent");
}

HRESULT DirectPlay8Address::GetDevice(GUID* pDevGuid)
{
	UNIMPLEMENTED("DirectPlay8Address::GetDevice");
}

HRESULT DirectPlay8Address::SetDevice(CONST GUID* CONST devGuid)
{
	UNIMPLEMENTED("DirectPlay8Address::SetDevice");
}

HRESULT DirectPlay8Address::BuildFromDirectPlay4Address(LPVOID pvAddress, DWORD dwDataSize)
{
	UNIMPLEMENTED("DirectPlay8Address::BuildFromDirectPlay4Address");
}

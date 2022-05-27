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
#include <assert.h>
#include <atomic>
#include <dplay8.h>
#include <objbase.h>
#include <stdio.h>
#include <string.h>
#include <wchar.h>
#include <windows.h>
#include <ws2tcpip.h>

#include "DirectPlay8Address.hpp"
#include "Log.hpp"

#define UNIMPLEMENTED(fmt, ...) \
	log_printf("Unimplemented method: " fmt, ## __VA_ARGS__); \
	return E_NOTIMPL;

DirectPlay8Address::DirectPlay8Address(std::atomic<unsigned int> *global_refcount):
	global_refcount(global_refcount),
	local_refcount(0)
{
	AddRef();
}

DirectPlay8Address::DirectPlay8Address(const DirectPlay8Address &src):
	global_refcount(src.global_refcount),
	local_refcount(0),
	user_data(src.user_data)
{
	replace_components(src.components);
	AddRef();
}

DirectPlay8Address::~DirectPlay8Address()
{
	clear_components();
}

/* Construct a DirectPlay8Address which represents a host address.
 *
 * service_provider must be CLSID_DP8SP_TCPIP or CLSID_DP8SP_IPX.
 * sa must be an IPv4 address with a valid IP and port.
*/
DirectPlay8Address *DirectPlay8Address::create_host_address(std::atomic<unsigned int> *global_refcount, GUID service_provider, const struct sockaddr *sa)
{
	assert(service_provider == CLSID_DP8SP_TCPIP || service_provider == CLSID_DP8SP_IPX);
	assert(sa->sa_family == AF_INET);
	
	const struct sockaddr_in *sa_v4 = (const struct sockaddr_in*)(sa);
	
	DirectPlay8Address *address = new DirectPlay8Address(global_refcount);
	address->SetSP(&service_provider);
	
	if(service_provider == CLSID_DP8SP_TCPIP)
	{
		/* TCP/IP service provider, just stick the IP in the hostname field. */
		
		char hostname[16];
		inet_ntop(AF_INET, (void*)(&(sa_v4->sin_addr)), hostname, sizeof(hostname));
		
		address->AddComponent(DPNA_KEY_HOSTNAME,
			hostname, strlen(hostname) + 1, DPNA_DATATYPE_STRING_ANSI);
	}
	else if(service_provider == CLSID_DP8SP_IPX)
	{
		/* IPX service provider.
		 *
		 * Network address is all zeros.
		 * Host address is IP address preceeded by two zero bytes.
		*/
		
		uint32_t ipaddr_he = ntohl(sa_v4->sin_addr.s_addr);
		
		char hostname[32];
		snprintf(hostname, sizeof(hostname), "00000000,0000%08X", (unsigned)(ipaddr_he));
		
		address->AddComponent(DPNA_KEY_HOSTNAME,
			hostname, strlen(hostname) + 1, DPNA_DATATYPE_STRING_ANSI);
	}
	
	DWORD port_dw = ntohs(sa_v4->sin_port);
	
	address->AddComponent(DPNA_KEY_PORT, &port_dw, sizeof(port_dw), DPNA_DATATYPE_DWORD);
	
	return address;
}

void DirectPlay8Address::clear_components()
{
	for(auto c = components.begin(); c != components.end(); ++c)
	{
		delete *c;
	}
	
	components.clear();
}

void DirectPlay8Address::replace_components(const std::vector<Component*> src)
{
	clear_components();
	
	components.reserve(src.size());
	for(auto c = src.begin(); c != src.end(); ++c)
	{
		components.push_back((*c)->clone());
	}
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
	*ppdpaNewAddress = new DirectPlay8Address(*this);
	return S_OK;
}

HRESULT DirectPlay8Address::SetEqual(PDIRECTPLAY8ADDRESS pdpaAddress)
{
	replace_components(((DirectPlay8Address*)(pdpaAddress))->components);
	user_data = ((DirectPlay8Address*)(pdpaAddress))->user_data;
	
	return S_OK;
}

HRESULT DirectPlay8Address::IsEqual(PDIRECTPLAY8ADDRESS pdpaAddress)
{
	UNIMPLEMENTED("DirectPlay8Address::IsEqual");
}

HRESULT DirectPlay8Address::Clear()
{
	clear_components();
	user_data.clear();
	
	return S_OK;
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
	GUID buf;
	DWORD buf_size = sizeof(buf);
	DWORD type;
	
	HRESULT res = GetComponentByName(DPNA_KEY_PROVIDER, &buf, &buf_size, &type);
	
	if(res == S_OK && type == DPNA_DATATYPE_GUID)
	{
		*pguidSP = buf;
		return S_OK;
	}
	else if(res == S_OK)
	{
		return DPNERR_GENERIC;
	}
	else{
		return res;
	}
}

HRESULT DirectPlay8Address::GetUserData(LPVOID pvUserData, PDWORD pdwBufferSize)
{
	if(user_data.empty())
	{
		return DPNERR_DOESNOTEXIST;
	}
	
	if(pvUserData != NULL && *pdwBufferSize >= user_data.size())
	{
		memcpy(pvUserData, user_data.data(), user_data.size());
		
		/* BUG: DirectPlay is supposed to set pdwBufferSize to the size of the data passed to
		 * SetUserData(), the DirectX implementation doesn't, so we don't.
		*/
		// *pdwBufferSize = user_data.size();
		
		return S_OK;
	}
	else{
		*pdwBufferSize = user_data.size();
		return DPNERR_BUFFERTOOSMALL;
	}
}

HRESULT DirectPlay8Address::SetSP(CONST GUID* CONST pguidSP)
{
	return AddComponent(DPNA_KEY_PROVIDER, pguidSP, sizeof(*pguidSP), DPNA_DATATYPE_GUID);
}

HRESULT DirectPlay8Address::SetUserData(CONST void* CONST pvUserData, CONST DWORD dwDataSize)
{
	user_data.clear();
	
	if(pvUserData != NULL && dwDataSize > 0)
	{
		user_data.insert(user_data.end(), (unsigned char*)(pvUserData),(unsigned char*)(pvUserData) + dwDataSize);
	}
	
	return S_OK;
}

HRESULT DirectPlay8Address::GetNumComponents(PDWORD pdwNumComponents)
{
	*pdwNumComponents = components.size();
	return S_OK;
}

HRESULT DirectPlay8Address::GetComponentByName(CONST WCHAR* CONST pwszName, LPVOID pvBuffer, PDWORD pdwBufferSize, PDWORD pdwDataType)
{
	for(auto c = components.begin(); c != components.end(); ++c)
	{
		if((*c)->name == pwszName)
		{
			return (*c)->get_component(pvBuffer, pdwBufferSize, pdwDataType);
		}
	}
	
	return DPNERR_DOESNOTEXIST;
}

HRESULT DirectPlay8Address::GetComponentByIndex(CONST DWORD dwComponentID, WCHAR* pwszName, PDWORD pdwNameLen, void* pvBuffer, PDWORD pdwBufferSize, PDWORD pdwDataType)
{
	if(dwComponentID >= components.size())
	{
		return DPNERR_DOESNOTEXIST;
	}
	
	Component *c = components[dwComponentID];
	
	HRESULT res = c->get_component(pvBuffer, pdwBufferSize, pdwDataType);
	
	if(*pdwNameLen > c->name.length() && pwszName != NULL)
	{
		wcscpy(pwszName, c->name.c_str());
		*pdwNameLen = c->name.length() + 1;
	}
	else{
		*pdwNameLen = c->name.length() + 1;
		res = DPNERR_BUFFERTOOSMALL;
	}
	
	return res;
}

HRESULT DirectPlay8Address::AddComponent(CONST WCHAR* CONST pwszName, CONST void* CONST lpvData, CONST DWORD dwDataSize, CONST DWORD dwDataType)
{
	auto existing_component = components.begin();
	for(; existing_component != components.end(); ++existing_component)
	{
		if((*existing_component)->name == pwszName)
		{
			break;
		}
	}
	
	Component *new_component;
	
	switch(dwDataType)
	{
		case DPNA_DATATYPE_STRING:
			new_component = new StringComponentW(pwszName, (const wchar_t*)(lpvData), dwDataSize);
			break;
			
		case DPNA_DATATYPE_STRING_ANSI:
		{
			/* DirectX converts ANSI strings to wide on input. */
			
			int wide_char_count = MultiByteToWideChar(CP_ACP, 0, (const char*)(lpvData), dwDataSize, NULL, 0);
			if(wide_char_count == 0)
			{
				return DPNERR_INVALIDPARAM;
			}
			
			std::vector<wchar_t> wide_char_buf(wide_char_count);
			
			wide_char_count = MultiByteToWideChar(CP_ACP, 0, (const char*)(lpvData), dwDataSize, wide_char_buf.data(), wide_char_count);
			if(wide_char_count == 0)
			{
				return DPNERR_INVALIDPARAM;
			}
			
			new_component = new StringComponentW(pwszName, wide_char_buf.data(), wide_char_count * sizeof(wchar_t));
			break;
		}
		
		case DPNA_DATATYPE_DWORD:
			if(dwDataSize != sizeof(DWORD))
			{
				return DPNERR_INVALIDPARAM;
			}
			
			new_component = new DWORDComponent(pwszName, *((DWORD*)(lpvData)));
			break;
			
		case DPNA_DATATYPE_GUID:
			if(dwDataSize != sizeof(GUID))
			{
				return DPNERR_INVALIDPARAM;
			}
			
			new_component = new GUIDComponent(pwszName, *((GUID*)(lpvData)));
			break;
			
		default:
			UNIMPLEMENTED("DirectPlay8Address::AddComponent() with dwDataType %u", (unsigned)(dwDataType));
	}
	
	if(existing_component != components.end())
	{
		delete *existing_component;
		*existing_component = new_component;
	}
	else{
		if(wcscmp(pwszName, DPNA_KEY_PROVIDER) == 0)
		{
			/* Provider is always the first component. */
			components.insert(components.begin(), new_component);
		}
		else{
			components.push_back(new_component);
		}
	}
	
	return S_OK;
}

HRESULT DirectPlay8Address::GetDevice(GUID* pDevGuid)
{
	GUID buf;
	DWORD buf_size = sizeof(buf);
	DWORD type;
	
	HRESULT res = GetComponentByName(DPNA_KEY_DEVICE, &buf, &buf_size, &type);
	
	if(res == S_OK && type == DPNA_DATATYPE_GUID)
	{
		*pDevGuid = buf;
		return S_OK;
	}
	else if(res == S_OK)
	{
		return DPNERR_GENERIC;
	}
	else{
		return res;
	}
}

HRESULT DirectPlay8Address::SetDevice(CONST GUID* CONST devGuid)
{
	return AddComponent(DPNA_KEY_DEVICE, devGuid, sizeof(*devGuid), DPNA_DATATYPE_GUID);
}

HRESULT DirectPlay8Address::BuildFromDirectPlay4Address(LPVOID pvAddress, DWORD dwDataSize)
{
	UNIMPLEMENTED("DirectPlay8Address::BuildFromDirectPlay4Address");
}

DirectPlay8Address::Component::Component(const std::wstring &name, DWORD type):
	name(name),
	type(type) {}

DirectPlay8Address::Component::~Component() {}

DirectPlay8Address::StringComponentW::StringComponentW(const std::wstring &name, const wchar_t *lpvData, DWORD dwDataSize):
	Component(name, DPNA_DATATYPE_STRING), value(lpvData, dwDataSize / sizeof(wchar_t)) {}

DirectPlay8Address::Component *DirectPlay8Address::StringComponentW::clone()
{
	return new DirectPlay8Address::StringComponentW(name, value.data(), value.length());
}

HRESULT DirectPlay8Address::StringComponentW::get_component(LPVOID pvBuffer, PDWORD pdwBufferSize, PDWORD pdwDataType)
{
	*pdwDataType = DPNA_DATATYPE_STRING;
	
	if(*pdwBufferSize >= (value.length() * sizeof(wchar_t)) && pvBuffer != NULL)
	{
		wcsncpy((wchar_t*)(pvBuffer), value.data(), value.length());
		*pdwBufferSize = value.length() * sizeof(wchar_t);
		return S_OK;
	}
	else{
		*pdwBufferSize = value.length() * sizeof(wchar_t);
		return DPNERR_BUFFERTOOSMALL;
	}
}

DirectPlay8Address::DWORDComponent::DWORDComponent(const std::wstring &name, DWORD value):
	Component(name, DPNA_DATATYPE_DWORD), value(value) {}

DirectPlay8Address::Component *DirectPlay8Address::DWORDComponent::clone()
{
	return new DirectPlay8Address::DWORDComponent(name, value);
}

HRESULT DirectPlay8Address::DWORDComponent::get_component(LPVOID pvBuffer, PDWORD pdwBufferSize, PDWORD pdwDataType)
{
	*pdwDataType = DPNA_DATATYPE_DWORD;
	
	if(*pdwBufferSize >= sizeof(DWORD) && pvBuffer != NULL)
	{
		*((DWORD*)(pvBuffer)) = value;
		*pdwBufferSize = sizeof(DWORD);
		return S_OK;
	}
	else{
		*pdwBufferSize = sizeof(DWORD);
		return DPNERR_BUFFERTOOSMALL;
	}
}

DirectPlay8Address::GUIDComponent::GUIDComponent(const std::wstring &name, GUID value):
	Component(name, DPNA_DATATYPE_GUID), value(value) {}


DirectPlay8Address::Component *DirectPlay8Address::GUIDComponent::clone()
{
	return new DirectPlay8Address::GUIDComponent(name, value);
}

HRESULT DirectPlay8Address::GUIDComponent::get_component(LPVOID pvBuffer, PDWORD pdwBufferSize, PDWORD pdwDataType)
{
	*pdwDataType = DPNA_DATATYPE_GUID;
	
	if(*pdwBufferSize >= sizeof(GUID) && pvBuffer != NULL)
	{
		*((GUID*)(pvBuffer)) = value;
		*pdwBufferSize = sizeof(GUID);
		return S_OK;
	}
	else{
		*pdwBufferSize = sizeof(GUID);
		return DPNERR_BUFFERTOOSMALL;
	}
}

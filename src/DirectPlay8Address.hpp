#ifndef DPLITE_DIRECTPLAY8ADDRESS_HPP
#define DPLITE_DIRECTPLAY8ADDRESS_HPP

#include <atomic>
#include <dplay8.h>
#include <objbase.h>
#include <string>
#include <vector>

class DirectPlay8Address: public IDirectPlay8Address
{
	private:
		class Component
		{
			public:
				const std::wstring name;
				const DWORD type;
				
				virtual ~Component();
				
				virtual Component *clone() = 0;
				virtual HRESULT get_component(LPVOID pvBuffer, PDWORD pdwBufferSize, PDWORD pdwDataType) = 0;
			
			protected:
				Component(const std::wstring &name, DWORD type);
		};
		
		class StringComponentW: public Component
		{
			public:
				const std::wstring value;
				
				StringComponentW(const std::wstring &name, const wchar_t *lpvData, DWORD dwDataSize);
				virtual Component *clone();
				virtual HRESULT get_component(LPVOID pvBuffer, PDWORD pdwBufferSize, PDWORD pdwDataType);
		};
		
		class DWORDComponent: public Component
		{
			public:
				const DWORD value;
				
				DWORDComponent(const std::wstring &name, DWORD value);
				virtual Component *clone();
				virtual HRESULT get_component(LPVOID pvBuffer, PDWORD pdwBufferSize, PDWORD pdwDataType);
		};
		
		class GUIDComponent: public Component
		{
			public:
				const GUID value;
				
				GUIDComponent(const std::wstring &name, GUID value);
				virtual Component *clone();
				virtual HRESULT get_component(LPVOID pvBuffer, PDWORD pdwBufferSize, PDWORD pdwDataType);
		};
		
		std::atomic<unsigned int> * const global_refcount;
		ULONG local_refcount;
		
		std::vector<Component*> components;
		std::vector<unsigned char> user_data;
		
		void clear_components();
		void replace_components(const std::vector<Component*> src);
		
	public:
		DirectPlay8Address(std::atomic<unsigned int> *global_refcount);
		DirectPlay8Address(const DirectPlay8Address &src);
		virtual ~DirectPlay8Address();
		
		/* IUnknown */
		virtual HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void **ppvObject) override;
		virtual ULONG STDMETHODCALLTYPE AddRef() override;
		virtual ULONG STDMETHODCALLTYPE Release() override;
		
		/* IDirectPlay8Address */
		virtual HRESULT STDMETHODCALLTYPE BuildFromURLW(WCHAR* pwszSourceURL) override;
		virtual HRESULT STDMETHODCALLTYPE BuildFromURLA(CHAR* pszSourceURL) override;
		virtual HRESULT STDMETHODCALLTYPE Duplicate(PDIRECTPLAY8ADDRESS* ppdpaNewAddress) override;
		virtual HRESULT STDMETHODCALLTYPE SetEqual(PDIRECTPLAY8ADDRESS pdpaAddress) override;
		virtual HRESULT STDMETHODCALLTYPE IsEqual(PDIRECTPLAY8ADDRESS pdpaAddress) override;
		virtual HRESULT STDMETHODCALLTYPE Clear() override;
		virtual HRESULT STDMETHODCALLTYPE GetURLW(WCHAR* pwszURL, PDWORD pdwNumChars) override;
		virtual HRESULT STDMETHODCALLTYPE GetURLA(CHAR* pszURL, PDWORD pdwNumChars) override;
		virtual HRESULT STDMETHODCALLTYPE GetSP(GUID* pguidSP) override;
		virtual HRESULT STDMETHODCALLTYPE GetUserData(LPVOID pvUserData, PDWORD pdwBufferSize) override;
		virtual HRESULT STDMETHODCALLTYPE SetSP(CONST GUID* CONST pguidSP) override;
		virtual HRESULT STDMETHODCALLTYPE SetUserData(CONST void* CONST pvUserData, CONST DWORD dwDataSize) override;
		virtual HRESULT STDMETHODCALLTYPE GetNumComponents(PDWORD pdwNumComponents) override;
		virtual HRESULT STDMETHODCALLTYPE GetComponentByName(CONST WCHAR* CONST pwszName, LPVOID pvBuffer, PDWORD pdwBufferSize, PDWORD pdwDataType) override;
		virtual HRESULT STDMETHODCALLTYPE GetComponentByIndex(CONST DWORD dwComponentID, WCHAR* pwszName, PDWORD pdwNameLen, void* pvBuffer, PDWORD pdwBufferSize, PDWORD pdwDataType) override;
		virtual HRESULT STDMETHODCALLTYPE AddComponent(CONST WCHAR* CONST pwszName, CONST void* CONST lpvData, CONST DWORD dwDataSize, CONST DWORD dwDataType) override;
		virtual HRESULT STDMETHODCALLTYPE GetDevice(GUID* pDevGuid) override;
		virtual HRESULT STDMETHODCALLTYPE SetDevice(CONST GUID* CONST devGuid) override;
		virtual HRESULT STDMETHODCALLTYPE BuildFromDPADDRESS(LPVOID pvAddress, DWORD dwDataSize) override;
};

#endif /* DPLITE_DIRECTPLAY8ADDRESS_HPP */

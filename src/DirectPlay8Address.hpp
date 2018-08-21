#ifndef DPLITE_DIRECTPLAY8ADDRESS_HPP
#define DPLITE_DIRECTPLAY8ADDRESS_HPP

#include <atomic>
#include <dplay8.h>
#include <objbase.h>

class DirectPlay8Address: public IDirectPlay8Address
{
	private:
		std::atomic<unsigned int> * const global_refcount;
		ULONG local_refcount;
	
	public:
		DirectPlay8Address(std::atomic<unsigned int> *global_refcount);
		virtual ~DirectPlay8Address() {}
		
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
		virtual HRESULT STDMETHODCALLTYPE BuildFromDirectPlay4Address(LPVOID pvAddress, DWORD dwDataSize) override;
};

#endif /* DPLITE_DIRECTPLAY8ADDRESS_HPP */

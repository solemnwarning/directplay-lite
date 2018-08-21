#ifndef DPLITE_FACTORY_HPP
#define DPLITE_FACTORY_HPP

#include <atomic>

template<typename T, const IID &IMPLEMENTS> class Factory: public IClassFactory
{
	private:
		std::atomic<unsigned int> * const global_refcount;
		ULONG local_refcount;
		
	public:
		Factory(std::atomic<unsigned int> *global_refcount):
			global_refcount(global_refcount),
			local_refcount(0)
		{
			AddRef();
		}
		
		virtual ~Factory() {}
		
		virtual HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void **ppvObject) override
		{
			if(riid == IID_IClassFactory || riid == IID_IUnknown)
			{
				*((IUnknown**)(ppvObject)) = this;
				AddRef();
				
				return S_OK;
			}
			else{
				return E_NOINTERFACE;
			}
		}
		
		virtual ULONG STDMETHODCALLTYPE AddRef(void) override
		{
			if(global_refcount != NULL)
			{
				++(*global_refcount);
			}
			
			return ++local_refcount;
		}
		
		virtual ULONG STDMETHODCALLTYPE Release(void) override
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
		
		virtual HRESULT STDMETHODCALLTYPE CreateInstance(IUnknown *pUnkOuter, const IID &riid, void **ppvObject) override
		{
			if(pUnkOuter != NULL)
			{
				return CLASS_E_NOAGGREGATION;
			}
			
			if(riid == IMPLEMENTS || riid == IID_IUnknown)
			{
				*((IUnknown**)(ppvObject)) = new T(global_refcount);
				return S_OK;
			}
			else{
				return E_NOINTERFACE;
			}
		}
		
		virtual HRESULT STDMETHODCALLTYPE LockServer(WINBOOL) override
		{
			return S_OK;
		}
};

#endif /* !DPLITE_FACTORY_HPP */

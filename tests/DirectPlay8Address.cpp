#include <gtest/gtest.h>

#include "../src/DirectPlay8Address.hpp"

// #define INSTANTIATE_FROM_COM

class DirectPlay8AddressInitial: public ::testing::Test {
	private:
		DirectPlay8Address *addr;
		
	protected:
		IDirectPlay8Address *idp8;
		
		DirectPlay8AddressInitial()
		{
			#ifdef INSTANTIATE_FROM_COM
			CoInitialize(NULL);
			CoCreateInstance(CLSID_DirectPlay8Address, NULL, CLSCTX_INPROC_SERVER, IID_IDirectPlay8Address, (void**)(&idp8));
			#else
			addr = new DirectPlay8Address(NULL);
			idp8 = addr;
			#endif
		}
		
		~DirectPlay8AddressInitial()
		{
			#ifdef INSTANTIATE_FROM_COM
			idp8->Release();
			CoUninitialize();
			#else
			delete addr;
			#endif
		}
};

TEST_F(DirectPlay8AddressInitial, HasNoComponents)
{
	DWORD num;
	ASSERT_EQ(idp8->GetNumComponents(&num), S_OK);
	EXPECT_EQ(num, (DWORD)(0));
}

TEST_F(DirectPlay8AddressInitial, GetSP)
{
	GUID vbuf;
	ASSERT_EQ(idp8->GetSP(&vbuf), DPNERR_DOESNOTEXIST);
}

TEST_F(DirectPlay8AddressInitial, GetDevice)
{
	GUID vbuf;
	ASSERT_EQ(idp8->GetDevice(&vbuf), DPNERR_DOESNOTEXIST);
}

TEST_F(DirectPlay8AddressInitial, GetUserData)
{
	unsigned char buf[256];
	DWORD size = sizeof(buf);
	
	ASSERT_EQ(idp8->GetUserData(buf, &size), DPNERR_DOESNOTEXIST);
}

// TEST_F(DirectPlay8AddressInitial, GetURLW)
// {
// 	wchar_t buf[256] = { 0xFF };
// 	DWORD size = 256;
// 	
// 	ASSERT_EQ(idp8->GetURLW(buf, &size), S_OK);
// 	
// 	const wchar_t EXPECT[] = L"x-directplay:/";
// 	const DWORD EXPECT_CHARS = sizeof(EXPECT) / sizeof(wchar_t);
// 	
// 	EXPECT_EQ(size, EXPECT_CHARS);
// 	EXPECT_EQ(std::wstring(buf, size), std::wstring(EXPECT, EXPECT_CHARS));
// }

// TEST_F(DirectPlay8AddressInitial, GetURLA)
// {
// 	char buf[256] = { 0x7F };
// 	DWORD size = 256;
// 	
// 	ASSERT_EQ(idp8->GetURLA(buf, &size), S_OK);
// 	
// 	const char EXPECT[] = "x-directplay:/";
// 	const DWORD EXPECT_CHARS = sizeof(EXPECT);
// 	
// 	EXPECT_EQ(size, EXPECT_CHARS);
// 	EXPECT_EQ(std::string(buf, size), std::string(EXPECT, EXPECT_CHARS));
// }

class DirectPlay8AddressWithWStringComponent: public DirectPlay8AddressInitial
{
	protected:
		const wchar_t *REFKEY = L"key";
		const DWORD REFKSIZE = 4;
		
		const wchar_t *REFVAL = L"wide string value";
		const DWORD REFVSIZE = 18 * sizeof(wchar_t);
		
		wchar_t kbuf[256];
		unsigned char vbuf[256];
		
		virtual void SetUp() override
		{
			memset(kbuf, 0xFF, sizeof(kbuf));
			memset(vbuf, 0xFF, sizeof(vbuf));
			
			ASSERT_EQ(idp8->AddComponent(REFKEY, REFVAL, REFVSIZE, DPNA_DATATYPE_STRING), S_OK);
		}
};

TEST_F(DirectPlay8AddressWithWStringComponent, HasOneComponent)
{
	DWORD num;
	ASSERT_EQ(idp8->GetNumComponents(&num), S_OK);
	EXPECT_EQ(num, (DWORD)(1));
}

TEST_F(DirectPlay8AddressWithWStringComponent, ComponentByIndexNameSizeZero)
{
	DWORD ksize = 0;
	DWORD vsize = REFVSIZE;
	DWORD type;
	
	ASSERT_EQ(idp8->GetComponentByIndex(0, (wchar_t*)(kbuf), &ksize, (void*)(vbuf), &vsize, &type), DPNERR_BUFFERTOOSMALL);
	
	EXPECT_EQ(ksize, REFKSIZE);
	EXPECT_EQ(vsize, REFVSIZE);
}

TEST_F(DirectPlay8AddressWithWStringComponent, ComponentByIndexBufferSizeZero)
{
	DWORD ksize = REFKSIZE;
	DWORD vsize = 0;
	DWORD type;
	
	ASSERT_EQ(idp8->GetComponentByIndex(0, (wchar_t*)(kbuf), &ksize, (void*)(vbuf), &vsize, &type), DPNERR_BUFFERTOOSMALL);
	
	EXPECT_EQ(ksize, REFKSIZE);
	EXPECT_EQ(vsize, REFVSIZE);
}

TEST_F(DirectPlay8AddressWithWStringComponent, ComponentByIndexNameSizeSmall)
{
	DWORD ksize = REFKSIZE - 1;
	DWORD vsize = REFVSIZE;
	DWORD type;
	
	ASSERT_EQ(idp8->GetComponentByIndex(0, (wchar_t*)(kbuf), &ksize, (void*)(vbuf), &vsize, &type), DPNERR_BUFFERTOOSMALL);
	
	EXPECT_EQ(ksize, REFKSIZE);
	EXPECT_EQ(vsize, REFVSIZE);
}

TEST_F(DirectPlay8AddressWithWStringComponent, ComponentByIndexBufferSizeSmall)
{
	DWORD ksize = REFKSIZE;
	DWORD vsize = REFVSIZE - 1;
	DWORD type;
	
	ASSERT_EQ(idp8->GetComponentByIndex(0, (wchar_t*)(kbuf), &ksize, (void*)(vbuf), &vsize, &type), DPNERR_BUFFERTOOSMALL);
	
	EXPECT_EQ(ksize, REFKSIZE);
	EXPECT_EQ(vsize, REFVSIZE);
}

TEST_F(DirectPlay8AddressWithWStringComponent, ComponentByIndexSizeExact)
{
	DWORD ksize = REFKSIZE;
	DWORD vsize = REFVSIZE;
	DWORD type;
	
	ASSERT_EQ(idp8->GetComponentByIndex(0, (wchar_t*)(kbuf), &ksize, (void*)(vbuf), &vsize, &type), S_OK);
	
	EXPECT_EQ(ksize, REFKSIZE);
	EXPECT_EQ(vsize, REFVSIZE);
	EXPECT_EQ(type, (DWORD)(DPNA_DATATYPE_STRING));
	
	EXPECT_EQ(std::wstring((const wchar_t*)(kbuf), ksize), std::wstring(REFKEY, REFKSIZE));
	
	EXPECT_EQ(std::wstring((const wchar_t*)(vbuf), (vsize / sizeof(wchar_t))),
		std::wstring(REFVAL, (REFVSIZE / sizeof(wchar_t))));
}

TEST_F(DirectPlay8AddressWithWStringComponent, ComponentByIndexSizeBig)
{
	DWORD ksize = REFKSIZE * 2;
	DWORD vsize = REFVSIZE * 2;
	DWORD type;
	
	ASSERT_EQ(idp8->GetComponentByIndex(0, (wchar_t*)(kbuf), &ksize, (void*)(vbuf), &vsize, &type), S_OK);
	
	EXPECT_EQ(ksize, REFKSIZE);
	EXPECT_EQ(vsize, REFVSIZE);
	EXPECT_EQ(type, (DWORD)(DPNA_DATATYPE_STRING));
	
	EXPECT_EQ(std::wstring((const wchar_t*)(kbuf), ksize), std::wstring(REFKEY, REFKSIZE));
	
	EXPECT_EQ(std::wstring((const wchar_t*)(vbuf), (vsize / sizeof(wchar_t))),
		std::wstring(REFVAL, (REFVSIZE / sizeof(wchar_t))));
}

TEST_F(DirectPlay8AddressWithWStringComponent, ComponentByIndexWrongIndex)
{
	DWORD ksize = sizeof(kbuf);
	DWORD vsize = sizeof(vbuf);
	DWORD type;
	
	ASSERT_EQ(idp8->GetComponentByIndex(1, (wchar_t*)(kbuf), &ksize, (void*)(vbuf), &vsize, &type), DPNERR_DOESNOTEXIST);
}

TEST_F(DirectPlay8AddressWithWStringComponent, ComponentByNameBufferSizeZero)
{
	DWORD vsize = 0;
	DWORD type;
	
	ASSERT_EQ(idp8->GetComponentByName(REFKEY, NULL, &vsize, &type), DPNERR_BUFFERTOOSMALL);
	
	EXPECT_EQ(vsize, REFVSIZE);
}

TEST_F(DirectPlay8AddressWithWStringComponent, ComponentByNameBufferSizeSmall)
{
	DWORD vsize = REFVSIZE - 1;
	DWORD type;
	
	ASSERT_EQ(idp8->GetComponentByName(REFKEY, (void*)(vbuf), &vsize, &type), DPNERR_BUFFERTOOSMALL);
	
	EXPECT_EQ(vsize, REFVSIZE);
}

TEST_F(DirectPlay8AddressWithWStringComponent, ComponentByNameBufferSizeExact)
{
	DWORD vsize = REFVSIZE;
	DWORD type;
	
	ASSERT_EQ(idp8->GetComponentByName(REFKEY, (void*)(vbuf), &vsize, &type), S_OK);
	
	EXPECT_EQ(vsize, REFVSIZE);
	EXPECT_EQ(type, (DWORD)(DPNA_DATATYPE_STRING));
	
	EXPECT_EQ(std::wstring((const wchar_t*)(vbuf), (vsize / sizeof(wchar_t))),
		std::wstring(REFVAL, (REFVSIZE / sizeof(wchar_t))));
}

TEST_F(DirectPlay8AddressWithWStringComponent, ComponentByNameBufferSizeBig)
{
	DWORD vsize = REFVSIZE * 2;
	DWORD type;
	
	ASSERT_EQ(idp8->GetComponentByName(REFKEY, (void*)(vbuf), &vsize, &type), S_OK);
	
	EXPECT_EQ(vsize, REFVSIZE);
	EXPECT_EQ(type, (DWORD)(DPNA_DATATYPE_STRING));
	
	EXPECT_EQ(std::wstring((const wchar_t*)(vbuf), (vsize / sizeof(wchar_t))),
		std::wstring(REFVAL, (REFVSIZE / sizeof(wchar_t))));
}

TEST_F(DirectPlay8AddressWithWStringComponent, ComponentByNameWrongName)
{
	DWORD vsize = sizeof(vbuf);
	DWORD type;
	
	ASSERT_EQ(idp8->GetComponentByName(L"wrongkey", (void*)(vbuf), &vsize, &type), DPNERR_DOESNOTEXIST);
}

TEST_F(DirectPlay8AddressWithWStringComponent, Clear)
{
	ASSERT_EQ(idp8->Clear(), S_OK);
	
	{
		DWORD num;
		ASSERT_EQ(idp8->GetNumComponents(&num), S_OK);
		ASSERT_EQ(num, (DWORD)(0));
	}
	
	{
		DWORD vsize = sizeof(vbuf);
		DWORD type;
		
		EXPECT_EQ(idp8->GetComponentByName(REFKEY, (void*)(vbuf), &vsize, &type), DPNERR_DOESNOTEXIST);
	}
	
	{
		DWORD ksize = sizeof(kbuf);
		DWORD vsize = sizeof(vbuf);
		DWORD type;
		
		ASSERT_EQ(idp8->GetComponentByIndex(0, (wchar_t*)(kbuf), &ksize, (void*)(vbuf), &vsize, &type), DPNERR_DOESNOTEXIST);
	}
}

// TEST_F(DirectPlay8AddressWithWStringComponent, GetURLW)
// {
// 	wchar_t buf[256] = { 0xFF };
// 	DWORD size = 256;
// 	
// 	ASSERT_EQ(idp8->GetURLW(buf, &size), S_OK);
// 	
// 	const wchar_t EXPECT[] = L"x-directplay:/key=wide%20string%20value";
// 	const DWORD EXPECT_CHARS = sizeof(EXPECT) / sizeof(wchar_t);
// 	
// 	EXPECT_EQ(size, EXPECT_CHARS);
// 	EXPECT_EQ(std::wstring(buf, size), std::wstring(EXPECT, EXPECT_CHARS));
// }

class DirectPlay8AddressWithAStringComponent: public DirectPlay8AddressInitial
{
	protected:
		const wchar_t *REFKEY = L"key";
		
		const char *AREFVAL = "ASCII string value";
		const DWORD AREFVSIZE = 19;
		
		const wchar_t *WREFVAL = L"ASCII string value";
		const DWORD WREFVSIZE = 38;
		
		unsigned char vbuf[256];
		
		virtual void SetUp() override
		{
			memset(vbuf, 0xFF, sizeof(vbuf));
			
			ASSERT_EQ(idp8->AddComponent(REFKEY, AREFVAL, AREFVSIZE, DPNA_DATATYPE_STRING_ANSI), S_OK);
		}
};

TEST_F(DirectPlay8AddressWithAStringComponent, HasOneComponent)
{
	DWORD num;
	ASSERT_EQ(idp8->GetNumComponents(&num), S_OK);
	EXPECT_EQ(num, (DWORD)(1));
}

TEST_F(DirectPlay8AddressWithAStringComponent, ComponentByNameBufferSizeZero)
{
	DWORD vsize = 0;
	DWORD type;
	
	ASSERT_EQ(idp8->GetComponentByName(REFKEY, NULL, &vsize, &type), DPNERR_BUFFERTOOSMALL);
	
	EXPECT_EQ(vsize, WREFVSIZE);
}

TEST_F(DirectPlay8AddressWithAStringComponent, ComponentByNameBufferSizeSmall)
{
	DWORD vsize = WREFVSIZE - 1;
	DWORD type;
	
	ASSERT_EQ(idp8->GetComponentByName(REFKEY, (void*)(vbuf), &vsize, &type), DPNERR_BUFFERTOOSMALL);
	
	EXPECT_EQ(vsize, WREFVSIZE);
}

TEST_F(DirectPlay8AddressWithAStringComponent, ComponentByNameBufferSizeExact)
{
	DWORD vsize = WREFVSIZE;
	DWORD type;
	
	ASSERT_EQ(idp8->GetComponentByName(REFKEY, (void*)(vbuf), &vsize, &type), S_OK);
	
	EXPECT_EQ(vsize, WREFVSIZE);
	EXPECT_EQ(type, (DWORD)(DPNA_DATATYPE_STRING));
	
	EXPECT_EQ(std::wstring((const wchar_t*)(vbuf), (vsize / sizeof(wchar_t))),
		std::wstring(WREFVAL, (WREFVSIZE / sizeof(wchar_t))));
}

TEST_F(DirectPlay8AddressWithAStringComponent, ComponentByNameBufferSizeBig)
{
	DWORD vsize = WREFVSIZE * 2;
	DWORD type;
	
	ASSERT_EQ(idp8->GetComponentByName(REFKEY, (void*)(vbuf), &vsize, &type), S_OK);
	
	EXPECT_EQ(vsize, WREFVSIZE);
	EXPECT_EQ(type, (DWORD)(DPNA_DATATYPE_STRING));
	
	EXPECT_EQ(std::wstring((const wchar_t*)(vbuf), (vsize / sizeof(wchar_t))),
		std::wstring(WREFVAL, (WREFVSIZE / sizeof(wchar_t))));
}

// TEST_F(DirectPlay8AddressWithAStringComponent, GetURLW)
// {
// 	wchar_t buf[256] = { 0xFF };
// 	DWORD size = 256;
// 	
// 	ASSERT_EQ(idp8->GetURLW(buf, &size), S_OK);
// 	
// 	const wchar_t EXPECT[] = L"x-directplay:/key=ASCII%20string%20value";
// 	const DWORD EXPECT_CHARS = sizeof(EXPECT) / sizeof(wchar_t);
// 	
// 	EXPECT_EQ(size, EXPECT_CHARS);
// 	EXPECT_EQ(std::wstring(buf, size), std::wstring(EXPECT, EXPECT_CHARS));
// }

class DirectPlay8AddressWithDWORDComponent: public DirectPlay8AddressInitial
{
	protected:
		const wchar_t *REFKEY = L"key";
		
		const DWORD REFVAL = 0x0EA7BEEF;
		const DWORD REFVSIZE = sizeof(REFVAL);
		
		unsigned char vbuf[256];
		
		virtual void SetUp() override
		{
			memset(vbuf, 0xFF, sizeof(vbuf));
			
			ASSERT_EQ(idp8->AddComponent(REFKEY, &REFVAL, REFVSIZE, DPNA_DATATYPE_DWORD), S_OK);
		}
};

TEST_F(DirectPlay8AddressWithDWORDComponent, HasOneComponent)
{
	DWORD num;
	ASSERT_EQ(idp8->GetNumComponents(&num), S_OK);
	EXPECT_EQ(num, (DWORD)(1));
}

TEST_F(DirectPlay8AddressWithDWORDComponent, ComponentByNameBufferSizeZero)
{
	DWORD vsize = 0;
	DWORD type;
	
	ASSERT_EQ(idp8->GetComponentByName(REFKEY, NULL, &vsize, &type), DPNERR_BUFFERTOOSMALL);
	
	EXPECT_EQ(vsize, REFVSIZE);
}

TEST_F(DirectPlay8AddressWithDWORDComponent, ComponentByNameBufferSizeSmall)
{
	DWORD vsize = REFVSIZE - 1;
	DWORD type;
	
	ASSERT_EQ(idp8->GetComponentByName(REFKEY, (void*)(vbuf), &vsize, &type), DPNERR_BUFFERTOOSMALL);
	
	EXPECT_EQ(vsize, REFVSIZE);
}

TEST_F(DirectPlay8AddressWithDWORDComponent, ComponentByNameBufferSizeExact)
{
	DWORD vsize = REFVSIZE;
	DWORD type;
	
	ASSERT_EQ(idp8->GetComponentByName(REFKEY, (void*)(vbuf), &vsize, &type), S_OK);
	
	EXPECT_EQ(vsize, REFVSIZE);
	EXPECT_EQ(type, (DWORD)(DPNA_DATATYPE_DWORD));
	
	EXPECT_EQ(*(DWORD*)(vbuf), REFVAL);
}

TEST_F(DirectPlay8AddressWithDWORDComponent, ComponentByNameBufferSizeBig)
{
	DWORD vsize = REFVSIZE * 2;
	DWORD type;
	
	ASSERT_EQ(idp8->GetComponentByName(REFKEY, (void*)(vbuf), &vsize, &type), S_OK);
	
	EXPECT_EQ(vsize, REFVSIZE);
	EXPECT_EQ(type, (DWORD)(DPNA_DATATYPE_DWORD));
	
	EXPECT_EQ(*(DWORD*)(vbuf), REFVAL);
}

// TEST_F(DirectPlay8AddressWithDWORDComponent, GetURLW)
// {
// 	wchar_t buf[256] = { 0xFF };
// 	DWORD size = 256;
// 	
// 	ASSERT_EQ(idp8->GetURLW(buf, &size), S_OK);
// 	
// 	const wchar_t EXPECT[] = L"x-directplay:/key=245874415";
// 	const DWORD EXPECT_CHARS = sizeof(EXPECT) / sizeof(wchar_t);
// 	
// 	EXPECT_EQ(size, EXPECT_CHARS);
// 	EXPECT_EQ(std::wstring(buf, size), std::wstring(EXPECT, EXPECT_CHARS));
// }

class DirectPlay8AddressWithGUIDComponent: public DirectPlay8AddressInitial
{
	protected:
		const wchar_t *REFKEY = L"key";
		
		const GUID REFVAL = CLSID_DirectPlay8Address;
		const DWORD REFVSIZE = sizeof(REFVAL);
		
		unsigned char vbuf[256];
		
		virtual void SetUp() override
		{
			memset(vbuf, 0xFF, sizeof(vbuf));
			
			ASSERT_EQ(idp8->AddComponent(REFKEY, &REFVAL, REFVSIZE, DPNA_DATATYPE_GUID), S_OK);
		}
};

TEST_F(DirectPlay8AddressWithGUIDComponent, HasOneComponent)
{
	DWORD num;
	ASSERT_EQ(idp8->GetNumComponents(&num), S_OK);
	EXPECT_EQ(num, (DWORD)(1));
}

TEST_F(DirectPlay8AddressWithGUIDComponent, ComponentByNameBufferSizeZero)
{
	DWORD vsize = 0;
	DWORD type;
	
	ASSERT_EQ(idp8->GetComponentByName(REFKEY, NULL, &vsize, &type), DPNERR_BUFFERTOOSMALL);
	
	EXPECT_EQ(vsize, REFVSIZE);
}

TEST_F(DirectPlay8AddressWithGUIDComponent, ComponentByNameBufferSizeSmall)
{
	DWORD vsize = REFVSIZE - 1;
	DWORD type;
	
	ASSERT_EQ(idp8->GetComponentByName(REFKEY, (void*)(vbuf), &vsize, &type), DPNERR_BUFFERTOOSMALL);
	
	EXPECT_EQ(vsize, REFVSIZE);
}

TEST_F(DirectPlay8AddressWithGUIDComponent, ComponentByNameBufferSizeExact)
{
	DWORD vsize = REFVSIZE;
	DWORD type;
	
	ASSERT_EQ(idp8->GetComponentByName(REFKEY, (void*)(vbuf), &vsize, &type), S_OK);
	
	EXPECT_EQ(vsize, REFVSIZE);
	EXPECT_EQ(type, (DWORD)(DPNA_DATATYPE_GUID));
	
	EXPECT_EQ(*(GUID*)(vbuf), REFVAL);
}

TEST_F(DirectPlay8AddressWithGUIDComponent, ComponentByNameBufferSizeBig)
{
	DWORD vsize = REFVSIZE * 2;
	DWORD type;
	
	ASSERT_EQ(idp8->GetComponentByName(REFKEY, (void*)(vbuf), &vsize, &type), S_OK);
	
	EXPECT_EQ(vsize, REFVSIZE);
	EXPECT_EQ(type, (DWORD)(DPNA_DATATYPE_GUID));
	
	EXPECT_EQ(*(GUID*)(vbuf), REFVAL);
}

// TEST_F(DirectPlay8AddressWithGUIDComponent, GetURLW)
// {
// 	wchar_t buf[256] = { 0xFF };
// 	DWORD size = 256;
// 	
// 	ASSERT_EQ(idp8->GetURLW(buf, &size), S_OK);
// 	
// 	const wchar_t EXPECT[] = L"x-directplay:/key=%7B934A9523-A3CA-4BC5-ADA0-D6D95D979421%7D";
// 	const DWORD EXPECT_CHARS = sizeof(EXPECT) / sizeof(wchar_t);
// 	
// 	EXPECT_EQ(size, EXPECT_CHARS);
// 	EXPECT_EQ(std::wstring(buf, size), std::wstring(EXPECT, EXPECT_CHARS));
// }

class DirectPlay8AddressSetSP: public DirectPlay8AddressInitial
{
	protected:
		virtual void SetUp() override
		{
			ASSERT_EQ(idp8->SetSP(&CLSID_DP8SP_TCPIP), S_OK);
		}
};

TEST_F(DirectPlay8AddressSetSP, GetSP)
{
	GUID vbuf;
	ASSERT_EQ(idp8->GetSP(&vbuf), S_OK);
	EXPECT_EQ(vbuf, CLSID_DP8SP_TCPIP);
}

TEST_F(DirectPlay8AddressSetSP, GetNumComponents)
{
	DWORD num;
	ASSERT_EQ(idp8->GetNumComponents(&num), S_OK);
	EXPECT_EQ(num, (DWORD)(1));
}

TEST_F(DirectPlay8AddressSetSP, GetComponentByName)
{
	GUID vbuf;
	DWORD vsize = sizeof(vbuf);
	DWORD type;
	
	ASSERT_EQ(idp8->GetComponentByName(DPNA_KEY_PROVIDER, (void*)(&vbuf), &vsize, &type), S_OK);
	
	ASSERT_EQ(vsize, sizeof(vbuf));
	EXPECT_EQ(vbuf, CLSID_DP8SP_TCPIP);
}

class DirectPlay8AddressSetSPTwice: public DirectPlay8AddressInitial
{
	protected:
		virtual void SetUp() override
		{
			ASSERT_EQ(idp8->SetSP(&CLSID_DP8SP_TCPIP),  S_OK);
			ASSERT_EQ(idp8->SetSP(&CLSID_DP8SP_SERIAL), S_OK);
		}
};

TEST_F(DirectPlay8AddressSetSPTwice, GetSP)
{
	GUID vbuf;
	ASSERT_EQ(idp8->GetSP(&vbuf), S_OK);
	EXPECT_EQ(vbuf, CLSID_DP8SP_SERIAL);
}

TEST_F(DirectPlay8AddressSetSPTwice, GetNumComponents)
{
	DWORD num;
	ASSERT_EQ(idp8->GetNumComponents(&num), S_OK);
	EXPECT_EQ(num, (DWORD)(1));
}

TEST_F(DirectPlay8AddressSetSPTwice, GetComponentByName)
{
	GUID vbuf;
	DWORD vsize = sizeof(vbuf);
	DWORD type;
	
	ASSERT_EQ(idp8->GetComponentByName(DPNA_KEY_PROVIDER, (void*)(&vbuf), &vsize, &type), S_OK);
	
	ASSERT_EQ(vsize, sizeof(vbuf));
	EXPECT_EQ(vbuf, CLSID_DP8SP_SERIAL);
}

class DirectPlay8AddressSetDevice: public DirectPlay8AddressInitial
{
	protected:
		virtual void SetUp() override
		{
			/* Interface GUIDs are system-dependant, but it doesn't need to be real. */
			ASSERT_EQ(idp8->SetDevice(&CLSID_DP8SP_BLUETOOTH), S_OK);
		}
};

TEST_F(DirectPlay8AddressSetDevice, GetDevice)
{
	GUID vbuf;
	ASSERT_EQ(idp8->GetDevice(&vbuf), S_OK);
	EXPECT_EQ(vbuf, CLSID_DP8SP_BLUETOOTH);
}

TEST_F(DirectPlay8AddressSetDevice, GetNumComponents)
{
	DWORD num;
	ASSERT_EQ(idp8->GetNumComponents(&num), S_OK);
	EXPECT_EQ(num, (DWORD)(1));
}

TEST_F(DirectPlay8AddressSetDevice, GetComponentByName)
{
	GUID vbuf;
	DWORD vsize = sizeof(vbuf);
	DWORD type;
	
	ASSERT_EQ(idp8->GetComponentByName(DPNA_KEY_DEVICE, (void*)(&vbuf), &vsize, &type), S_OK);
	
	ASSERT_EQ(vsize, sizeof(vbuf));
	EXPECT_EQ(vbuf, CLSID_DP8SP_BLUETOOTH);
}

// TEST_F(DirectPlay8AddressSetDevice, GetURLW)
// {
// 	wchar_t buf[256] = { 0xFF };
// 	DWORD size = 256;
// 	
// 	ASSERT_EQ(idp8->GetURLW(buf, &size), S_OK);
// 	
// 	const wchar_t EXPECT[] = L"x-directplay:/device=%7B995513AF-3027-4B9A-956E-C772B3F78006%7D";
// 	const DWORD EXPECT_CHARS = sizeof(EXPECT) / sizeof(wchar_t);
// 	
// 	EXPECT_EQ(size, EXPECT_CHARS);
// 	EXPECT_EQ(std::wstring(buf, size), std::wstring(EXPECT, EXPECT_CHARS));
// }

class DirectPlay8AddressSetUserData: public DirectPlay8AddressInitial
{
	protected:
		const unsigned char REFDATA[22] = { 0x00, 0x01, 0x02, 0x03, '0', '1', '2', '3', '4', '5', '6', '7', '8', '9', 'A', 'B', 'C', 'D', 'E', 'F', 0xFF, 0xFE };
		static const DWORD  REFSIZE     = 22;
		
		virtual void SetUp() override
		{
			/* Interface GUIDs are system-dependant, but it doesn't need to be real. */
			ASSERT_EQ(idp8->SetUserData(REFDATA, REFSIZE), S_OK);
		}
};

TEST_F(DirectPlay8AddressSetUserData, GetUserDataSizeZero)
{
	DWORD size = 0;
	
	ASSERT_EQ(idp8->GetUserData(NULL, &size), DPNERR_BUFFERTOOSMALL);
	ASSERT_EQ(size, REFSIZE);
}

TEST_F(DirectPlay8AddressSetUserData, GetUserDataSizeSmall)
{
	unsigned char buf[REFSIZE - 1];
	DWORD size = sizeof(buf);
	
	ASSERT_EQ(idp8->GetUserData(buf, &size), DPNERR_BUFFERTOOSMALL);
	ASSERT_EQ(size, REFSIZE);
}

TEST_F(DirectPlay8AddressSetUserData, GetUserDataSizeExact)
{
	unsigned char buf[REFSIZE];
	DWORD size = sizeof(buf);
	
	ASSERT_EQ(idp8->GetUserData(buf, &size), S_OK);
	
	ASSERT_EQ(size, REFSIZE);
	EXPECT_EQ(std::string((char*)(buf), REFSIZE), std::string((char*)(REFDATA), REFSIZE));
}

TEST_F(DirectPlay8AddressSetUserData, GetUserDataSizeBig)
{
	unsigned char buf[256];
	DWORD size = sizeof(buf);
	
	ASSERT_EQ(idp8->GetUserData(buf, &size), S_OK);
	
	/* BUG: DirectPlay is supposed to set pdwBufferSize to the size of the data passed to
	 * SetUserData(), the DirectX implementation doesn't, so we don't.
	*/
	// ASSERT_EQ(size, REFSIZE);
	EXPECT_EQ(size, sizeof(buf));
	
	EXPECT_EQ(std::string((char*)(buf), REFSIZE), std::string((char*)(REFDATA), REFSIZE));
}

TEST_F(DirectPlay8AddressSetUserData, GetNumComponents)
{
	DWORD num;
	ASSERT_EQ(idp8->GetNumComponents(&num), S_OK);
	EXPECT_EQ(num, (DWORD)(0));
}

// TEST_F(DirectPlay8AddressSetUserData, GetURLW)
// {
// 	wchar_t buf[256] = { 0xFF };
// 	DWORD size = 256;
// 	
// 	ASSERT_EQ(idp8->GetURLW(buf, &size), S_OK);
// 	
// 	/* BUG: This doesn't match the URL scheme described by the DirectX SDK documentation, but
// 	 * it matches what DirectX produces.
// 	*/
// 	
// 	const wchar_t EXPECT[] = L"x-directplay:/%00%01%02%030123456789ABCDEF%FF%FE\0";
// 	const DWORD EXPECT_CHARS = sizeof(EXPECT) / sizeof(wchar_t);
// 	
// 	EXPECT_EQ(size, EXPECT_CHARS);
// 	EXPECT_EQ(std::wstring(buf, size), std::wstring(EXPECT, EXPECT_CHARS));
// }

TEST_F(DirectPlay8AddressSetUserData, Clear)
{
	ASSERT_EQ(idp8->Clear(), S_OK);
	
	unsigned char buf[256];
	DWORD size = sizeof(buf);
	
	ASSERT_EQ(idp8->GetUserData(buf, &size), DPNERR_DOESNOTEXIST);
}

class DirectPlay8AddressIPConstructed: public DirectPlay8AddressInitial
{
	protected:
		virtual void SetUp() override
		{
			ASSERT_EQ(idp8->SetDevice(&CLSID_DP8SP_BLUETOOTH), S_OK);
			ASSERT_EQ(idp8->SetSP(&CLSID_DP8SP_TCPIP), S_OK);
			
			DWORD port  = 1234;
			ASSERT_EQ(idp8->AddComponent(DPNA_KEY_PORT, &port, sizeof(port), DPNA_DATATYPE_DWORD), S_OK);
			
			const char user_data[] = "user data";
			ASSERT_EQ(idp8->SetUserData(user_data, strlen(user_data)), S_OK);
		}
};

TEST_F(DirectPlay8AddressIPConstructed, GetNumComponents)
{
	DWORD num;
	ASSERT_EQ(idp8->GetNumComponents(&num), S_OK);
	EXPECT_EQ(num, (DWORD)(3));
}

TEST_F(DirectPlay8AddressIPConstructed, GetComponentByIndex0)
{
	wchar_t kbuf[64];
	unsigned char vbuf[64];
	
	DWORD ksize = 64;
	DWORD vsize = 64;
	DWORD type;
	
	ASSERT_EQ(idp8->GetComponentByIndex(0, kbuf, &ksize, vbuf, &vsize, &type), S_OK);
	
	ASSERT_EQ(std::wstring(kbuf, ksize), std::wstring(L"provider", 9));
	
	EXPECT_EQ(type, (DWORD)(DPNA_DATATYPE_GUID));
	
	EXPECT_EQ(vsize, sizeof(GUID));
	EXPECT_EQ(*(GUID*)(vbuf), CLSID_DP8SP_TCPIP);
}

TEST_F(DirectPlay8AddressIPConstructed, GetComponentByIndex1)
{
	wchar_t kbuf[64];
	unsigned char vbuf[64];
	
	DWORD ksize = 64;
	DWORD vsize = 64;
	DWORD type;
	
	ASSERT_EQ(idp8->GetComponentByIndex(1, kbuf, &ksize, vbuf, &vsize, &type), S_OK);
	
	ASSERT_EQ(std::wstring(kbuf, ksize), std::wstring(L"device", 7));
	
	EXPECT_EQ(type, (DWORD)(DPNA_DATATYPE_GUID));
	
	EXPECT_EQ(vsize, sizeof(GUID));
	EXPECT_EQ(*(GUID*)(vbuf), CLSID_DP8SP_BLUETOOTH);
}

TEST_F(DirectPlay8AddressIPConstructed, GetComponentByIndex2)
{
	wchar_t kbuf[64];
	unsigned char vbuf[64];
	
	DWORD ksize = 64;
	DWORD vsize = 64;
	DWORD type;
	
	ASSERT_EQ(idp8->GetComponentByIndex(2, kbuf, &ksize, vbuf, &vsize, &type), S_OK);
	
	ASSERT_EQ(std::wstring(kbuf, ksize), std::wstring(L"port", 5));
	
	EXPECT_EQ(type, (DWORD)(DPNA_DATATYPE_DWORD));
	
	EXPECT_EQ(vsize, sizeof(DWORD));
	EXPECT_EQ(*(DWORD*)(vbuf), (DWORD)(1234));
}

// TEST_F(DirectPlay8AddressIPConstructed, GetURLW)
// {
// 	wchar_t buf[256] = { 0xFF };
// 	DWORD size = 256;
// 	
// 	ASSERT_EQ(idp8->GetURLW(buf, &size), S_OK);
// 	
// 	/* BUG: This doesn't match the URL scheme described by the DirectX SDK documentation, but
// 	 * it matches what DirectX produces.
// 	*/
// 	
// 	const wchar_t EXPECT[] = L"x-directplay:/provider=%7BEBFE7BA0-628D-11D2-AE0F-006097B01411%7D;device=%7B995513AF-3027-4B9A-956E-C772B3F78006%7D;port=1234user%20data\0";
// 	const DWORD EXPECT_CHARS = sizeof(EXPECT) / sizeof(wchar_t);
// 	
// 	EXPECT_EQ(size, EXPECT_CHARS);
// 	EXPECT_EQ(std::wstring(buf, size), std::wstring(EXPECT, EXPECT_CHARS));
// }

// TEST_F(DirectPlay8AddressIPConstructed, GetURLA)
// {
// 	char buf[256] = { 0x7F };
// 	DWORD size = 256;
// 	
// 	ASSERT_EQ(idp8->GetURLA(buf, &size), S_OK);
// 	
// 	/* BUG: This doesn't match the URL scheme described by the DirectX SDK documentation, but
// 	 * it matches what DirectX produces.
// 	*/
// 	
// 	const char EXPECT[] = "x-directplay:/provider=%7BEBFE7BA0-628D-11D2-AE0F-006097B01411%7D;device=%7B995513AF-3027-4B9A-956E-C772B3F78006%7D;port=1234user%20data\0";
// 	const DWORD EXPECT_CHARS = sizeof(EXPECT);
// 	
// 	EXPECT_EQ(size, EXPECT_CHARS);
// 	EXPECT_EQ(std::string(buf, size), std::string(EXPECT, EXPECT_CHARS));
// }

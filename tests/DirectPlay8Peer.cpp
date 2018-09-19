#include <winsock2.h>
#include <array>
#include <functional>
#include <gtest/gtest.h>
#include <stdexcept>

#include "../src/DirectPlay8Address.hpp"
#include "../src/DirectPlay8Peer.hpp"

// #define INSTANTIATE_FROM_COM

#define PORT 42895

static const GUID APP_GUID_1 = { 0xa6133957, 0x6f42, 0x46ce, { 0xa9, 0x88, 0x22, 0xf7, 0x79, 0x47, 0x08, 0x16 } };
static const GUID APP_GUID_2 = { 0x5917faae, 0x7ab0, 0x42d2, { 0xae, 0x13, 0x9c, 0x54, 0x1b, 0x7f, 0xb5, 0xab } };

static HRESULT CALLBACK callback_shim(PVOID pvUserContext, DWORD dwMessageType, PVOID pMessage)
{
	std::function<HRESULT(DWORD,PVOID)> *callback = (std::function<HRESULT(DWORD,PVOID)>*)(pvUserContext);
	return (*callback)(dwMessageType, pMessage);
}

class IDP8PeerInstance
{
	public:
		IDirectPlay8Peer *instance;
		
		IDP8PeerInstance()
		{
			#ifdef INSTANTIATE_FROM_COM
			CoInitialize(NULL);
			CoCreateInstance(CLSID_DirectPlay8Peer, NULL, CLSCTX_INPROC_SERVER, IID_IDirectPlay8Peer, (void**)(&instance));
			#else
			instance = new DirectPlay8Peer(NULL);
			#endif
		}
		
		~IDP8PeerInstance()
		{
			#ifdef INSTANTIATE_FROM_COM
			instance->Release();
			CoUninitialize();
			#else
			instance->Release();
			#endif
		}
		
		IDirectPlay8Peer &operator*()
		{
			return *instance;
		}
		
		IDirectPlay8Peer *operator->()
		{
			return instance;
		}
};

class IDP8AddressInstance
{
	public:
		IDirectPlay8Address *instance;
		
		IDP8AddressInstance()
		{
			#ifdef INSTANTIATE_FROM_COM
			CoInitialize(NULL);
			CoCreateInstance(CLSID_DirectPlay8Address, NULL, CLSCTX_INPROC_SERVER, IID_IDirectPlay8Address, (void**)(&instance));
			#else
			instance = new DirectPlay8Address(NULL);
			#endif
		}
		
		IDP8AddressInstance(const wchar_t *hostname, DWORD port): IDP8AddressInstance()
		{
			if(instance->SetSP(&CLSID_DP8SP_TCPIP) != S_OK
				|| instance->AddComponent(DPNA_KEY_HOSTNAME, hostname, ((wcslen(hostname) + 1) * sizeof(wchar_t)), DPNA_DATATYPE_STRING) != S_OK
				|| instance->AddComponent(DPNA_KEY_PORT, &port, sizeof(DWORD), DPNA_DATATYPE_DWORD) != S_OK)
			{
				throw std::runtime_error("Address setup failed");
			}
		}
		
		~IDP8AddressInstance()
		{
			#ifdef INSTANTIATE_FROM_COM
			instance->Release();
			CoUninitialize();
			#else
			instance->Release();
			#endif
		}
		
		IDirectPlay8Address &operator*()
		{
			return *instance;
		}
		
		IDirectPlay8Address *operator->()
		{
			return instance;
		}
		
		operator IDirectPlay8Address*() const
		{
			return instance;
		}
};

/* Wrapper around a DirectPlay8Peer which hosts a session. */
struct SessionHost
{
	std::function<HRESULT(DWORD,PVOID)> cb;
	IDP8PeerInstance dp8p;
	
	SessionHost(
		GUID application_guid,
		const wchar_t *session_description,
		std::function<HRESULT(DWORD,PVOID)> cb =
			[](DWORD dwMessageType, PVOID pMessage)
			{
				return DPN_OK;
			}):
		cb(cb)
	{
		if(dp8p->Initialize(&(this->cb), &callback_shim, 0) != S_OK)
		{
			throw std::runtime_error("DirectPlay8Peer::Initialize failed");
		}
		
		{
			DPN_APPLICATION_DESC app_desc;
			memset(&app_desc, 0, sizeof(app_desc));
			
			app_desc.dwSize = sizeof(app_desc);
			app_desc.guidApplication = application_guid;
			app_desc.pwszSessionName = (wchar_t*)(session_description);
			
			if(dp8p->Host(&app_desc, NULL, 0, NULL, NULL, (void*)(0xB00), 0) != S_OK)
			{
				throw std::runtime_error("DirectPlay8Peer::Host failed");
			}
		}
	}
	
	SessionHost(
		GUID application_guid,
		const wchar_t *session_description,
		DWORD port,
		std::function<HRESULT(DWORD,PVOID)> cb =
			[](DWORD dwMessageType, PVOID pMessage)
			{
				return DPN_OK;
			}):
		cb(cb)
	{
		if(dp8p->Initialize(&(this->cb), &callback_shim, 0) != S_OK)
		{
			throw std::runtime_error("DirectPlay8Peer::Initialize failed");
		}
		
		{
			DPN_APPLICATION_DESC app_desc;
			memset(&app_desc, 0, sizeof(app_desc));
			
			app_desc.dwSize = sizeof(app_desc);
			app_desc.guidApplication = application_guid;
			app_desc.pwszSessionName = (wchar_t*)(session_description);
			
			IDP8AddressInstance addr;
			
			if(addr->SetSP(&CLSID_DP8SP_TCPIP) != S_OK
				|| addr->AddComponent(DPNA_KEY_PORT, &port, sizeof(DWORD), DPNA_DATATYPE_DWORD) != S_OK)
			{
				throw std::runtime_error("Address setup failed");
			}
			
			if(dp8p->Host(&app_desc, &(addr.instance), 1, NULL, NULL, (void*)(0xB00), 0) != S_OK)
			{
				throw std::runtime_error("DirectPlay8Peer::Host failed");
			}
		}
	}
	
	IDirectPlay8Peer *operator->()
	{
		return dp8p.instance;
	}
};

struct FoundSession
{
	GUID application_guid;
	std::wstring session_description;
	
	FoundSession(GUID application_guid, const std::wstring &session_description):
		application_guid(application_guid),
		session_description(session_description) {}
	
	bool operator==(const FoundSession &rhs) const
	{
		return application_guid == rhs.application_guid
			&& session_description == rhs.session_description;
	}
};

struct CompareGUID {
	bool operator()(const GUID &a, const GUID &b) const
	{
		return memcmp(&a, &b, sizeof(GUID)) < 0;
	}
};

static void EXPECT_SESSIONS(std::map<GUID, FoundSession, CompareGUID> got, const FoundSession *expect_begin, const FoundSession *expect_end)
{
	std::list<FoundSession> expect(expect_begin, expect_end);
	
	for(auto gi = got.begin(); gi != got.end();)
	{
		for(auto ei = expect.begin(); ei != expect.end();)
		{
			if(gi->second == *ei)
			{
				ei = expect.erase(ei);
				gi = got.erase(gi);
				goto NEXT_GI;
			}
			else{
				++ei;
			}
		}
		
		++gi;
		NEXT_GI:
		{}
	}
	
	for(auto gi = got.begin(); gi != got.end(); ++gi)
	{
		wchar_t application_guid_s[128];
		StringFromGUID2(gi->second.application_guid, application_guid_s, 128);
		
		ADD_FAILURE() << "Extra session:" << std::endl
			<< "  application_guid    = " << application_guid_s << std::endl
			<< "  session_description = " << gi->second.session_description;
	}
	
	for(auto ei = expect.begin(); ei != expect.end(); ++ei)
	{
		wchar_t application_guid_s[128];
		StringFromGUID2(ei->application_guid, application_guid_s, 128);
		
		ADD_FAILURE() << "Missing session:" << std::endl
			<< "  application_guid    = " << application_guid_s << std::endl
			<< "  session_description = " << ei->session_description;
	}
	
	if(got.empty() && expect.empty())
	{
		SUCCEED();
	}
}

TEST(DirectPlay8Peer, EnumHostsSync)
{
	SessionHost a1s1(APP_GUID_1, L"Application 1 Session 1");
	SessionHost a1s2(APP_GUID_1, L"Application 1 Session 2");
	SessionHost a2s1(APP_GUID_2, L"Application 2 Session 1");
	
	std::map<GUID, FoundSession, CompareGUID> sessions;
	
	bool got_async_op_complete = false;
	
	std::function<HRESULT(DWORD,PVOID)> client_cb =
		[&sessions, &got_async_op_complete]
		(DWORD dwMessageType, PVOID pMessage)
	{
		if(dwMessageType == DPN_MSGID_ENUM_HOSTS_RESPONSE)
		{
			DPNMSG_ENUM_HOSTS_RESPONSE *ehr = (DPNMSG_ENUM_HOSTS_RESPONSE*)(pMessage);
			
			EXPECT_EQ(ehr->dwSize,        sizeof(DPNMSG_ENUM_HOSTS_RESPONSE));
			EXPECT_EQ(ehr->pvUserContext, (void*)(0xBEEF));
			
			sessions.emplace(
				ehr->pApplicationDescription->guidInstance,
				FoundSession(
					ehr->pApplicationDescription->guidApplication,
					ehr->pApplicationDescription->pwszSessionName));
		}
		else if(dwMessageType == DPN_MSGID_ASYNC_OP_COMPLETE)
		{
			got_async_op_complete = true;
		}
		
		return DPN_OK;
	};
	
	IDP8PeerInstance client;
	
	ASSERT_EQ(client->Initialize(&client_cb, &callback_shim, 0), S_OK);
	
	DWORD start = GetTickCount();
	
	ASSERT_EQ(client->EnumHosts(
		NULL,              /* pApplicationDesc */
		NULL,              /* pdpaddrHost */
		NULL,              /* pdpaddrDeviceInfo */
		NULL,              /* pvUserEnumData */
		0,                 /* dwUserEnumDataSize */
		3,                 /* dwEnumCount */
		500,               /* dwRetryInterval */
		500,               /* dwTimeOut*/
		(void*)(0xBEEF),   /* pvUserContext */
		NULL,              /* pAsyncHandle */
		DPNENUMHOSTS_SYNC  /* dwFlags */
	), S_OK);
	
	DWORD end = GetTickCount();
	
	FoundSession expect_sessions[] = {
		FoundSession(APP_GUID_1, L"Application 1 Session 1"),
		FoundSession(APP_GUID_1, L"Application 1 Session 2"),
		FoundSession(APP_GUID_2, L"Application 2 Session 1"),
	};
	
	EXPECT_SESSIONS(sessions, expect_sessions, expect_sessions + 3);
	
	DWORD enum_time_ms = end - start;
	EXPECT_TRUE((enum_time_ms >= 1250) && (enum_time_ms <= 1750));
	
	EXPECT_FALSE(got_async_op_complete);
}

TEST(DirectPlay8Peer, EnumHostsAsync)
{
	SessionHost a1s1(APP_GUID_1, L"Application 1 Session 1");
	SessionHost a1s2(APP_GUID_1, L"Application 1 Session 2");
	SessionHost a2s1(APP_GUID_2, L"Application 2 Session 1");
	
	std::map<GUID, FoundSession, CompareGUID> sessions;
	
	bool got_async_op_complete = false;
	DWORD got_async_op_complete_at;
	DPNHANDLE async_handle;
	
	std::function<HRESULT(DWORD,PVOID)> callback =
		[&sessions, &got_async_op_complete, &got_async_op_complete_at, &async_handle]
		(DWORD dwMessageType, PVOID pMessage)
	{
		if(dwMessageType == DPN_MSGID_ENUM_HOSTS_RESPONSE)
		{
			DPNMSG_ENUM_HOSTS_RESPONSE *ehr = (DPNMSG_ENUM_HOSTS_RESPONSE*)(pMessage);
			
			EXPECT_EQ(ehr->dwSize,        sizeof(DPNMSG_ENUM_HOSTS_RESPONSE));
			EXPECT_EQ(ehr->pvUserContext, (void*)(0xABCD));
			
			sessions.emplace(
				ehr->pApplicationDescription->guidInstance,
				FoundSession(
					ehr->pApplicationDescription->guidApplication,
					ehr->pApplicationDescription->pwszSessionName));
		}
		else if(dwMessageType == DPN_MSGID_ASYNC_OP_COMPLETE)
		{
			got_async_op_complete_at = GetTickCount();
			
			/* We shouldn't get DPNMSG_ASYNC_OP_COMPLETE multiple times. */
			EXPECT_FALSE(got_async_op_complete);
			
			DPNMSG_ASYNC_OP_COMPLETE *oc = (DPNMSG_ASYNC_OP_COMPLETE*)(pMessage);
			
			EXPECT_EQ(oc->dwSize,        sizeof(DPNMSG_ASYNC_OP_COMPLETE));
			EXPECT_EQ(oc->hAsyncOp,      async_handle);
			EXPECT_EQ(oc->pvUserContext, (void*)(0xABCD));
			EXPECT_EQ(oc->hResultCode,   S_OK);
			
			got_async_op_complete = true;
		}
		
		return DPN_OK;
	};
	
	IDP8PeerInstance client;
	
	ASSERT_EQ(client->Initialize(&callback, &callback_shim, 0), S_OK);
	
	DWORD start = GetTickCount();
	
	ASSERT_EQ(client->EnumHosts(
		NULL,              /* pApplicationDesc */
		NULL,              /* pdpaddrHost */
		NULL,              /* pdpaddrDeviceInfo */
		NULL,              /* pvUserEnumData */
		0,                 /* dwUserEnumDataSize */
		3,                 /* dwEnumCount */
		500,               /* dwRetryInterval */
		500,               /* dwTimeOut*/
		(void*)(0xABCD),   /* pvUserContext */
		&async_handle,     /* pAsyncHandle */
		0                  /* dwFlags */
	), DPNSUCCESS_PENDING);
	
	Sleep(3000);
	
	FoundSession expect_sessions[] = {
		FoundSession(APP_GUID_1, L"Application 1 Session 1"),
		FoundSession(APP_GUID_1, L"Application 1 Session 2"),
		FoundSession(APP_GUID_2, L"Application 2 Session 1"),
	};
	
	EXPECT_SESSIONS(sessions, expect_sessions, expect_sessions + 3);
	
	EXPECT_TRUE(got_async_op_complete);
	
	if(got_async_op_complete)
	{
		DWORD enum_time_ms = got_async_op_complete_at - start;
		EXPECT_TRUE((enum_time_ms >= 1250) && (enum_time_ms <= 1750));
	}
}

TEST(DirectPlay8Peer, EnumHostsAsyncCancelByHandle)
{
	bool got_async_op_complete = false;
	DWORD got_async_op_complete_at;
	DPNHANDLE async_handle;
	
	std::function<HRESULT(DWORD,PVOID)> callback =
		[&got_async_op_complete, &got_async_op_complete_at, &async_handle]
		(DWORD dwMessageType, PVOID pMessage)
	{
		if(dwMessageType == DPN_MSGID_ASYNC_OP_COMPLETE)
		{
			got_async_op_complete_at = GetTickCount();
			
			/* We shouldn't get DPNMSG_ASYNC_OP_COMPLETE multiple times. */
			EXPECT_FALSE(got_async_op_complete);
			
			DPNMSG_ASYNC_OP_COMPLETE *oc = (DPNMSG_ASYNC_OP_COMPLETE*)(pMessage);
			
			EXPECT_EQ(oc->dwSize,        sizeof(DPNMSG_ASYNC_OP_COMPLETE));
			EXPECT_EQ(oc->hAsyncOp,      async_handle);
			EXPECT_EQ(oc->pvUserContext, (void*)(0xABCD));
			EXPECT_EQ(oc->hResultCode,   DPNERR_USERCANCEL);
			
			got_async_op_complete = true;
		}
		
		return DPN_OK;
	};
	
	IDP8PeerInstance client;
	
	ASSERT_EQ(client->Initialize(&callback, &callback_shim, 0), S_OK);
	
	DWORD start = GetTickCount();
	
	ASSERT_EQ(client->EnumHosts(
		NULL,              /* pApplicationDesc */
		NULL,              /* pdpaddrHost */
		NULL,              /* pdpaddrDeviceInfo */
		NULL,              /* pvUserEnumData */
		0,                 /* dwUserEnumDataSize */
		3,                 /* dwEnumCount */
		500,               /* dwRetryInterval */
		500,               /* dwTimeOut*/
		(void*)(0xABCD),   /* pvUserContext */
		&async_handle,     /* pAsyncHandle */
		0                  /* dwFlags */
	), DPNSUCCESS_PENDING);
	
	ASSERT_EQ(client->CancelAsyncOperation(async_handle, 0), S_OK);
	
	Sleep(500);
	
	EXPECT_TRUE(got_async_op_complete);
	
	if(got_async_op_complete)
	{
		DWORD enum_time_ms = got_async_op_complete_at - start;
		EXPECT_TRUE(enum_time_ms <= 250);
	}
}

TEST(DirectPlay8Peer, EnumHostsAsyncCancelAllEnums)
{
	bool got_async_op_complete = false;
	DWORD got_async_op_complete_at;
	DPNHANDLE async_handle;
	
	std::function<HRESULT(DWORD,PVOID)> callback =
		[&got_async_op_complete, &got_async_op_complete_at, &async_handle]
		(DWORD dwMessageType, PVOID pMessage)
	{
		if(dwMessageType == DPN_MSGID_ASYNC_OP_COMPLETE)
		{
			got_async_op_complete_at = GetTickCount();
			
			/* We shouldn't get DPNMSG_ASYNC_OP_COMPLETE multiple times. */
			EXPECT_FALSE(got_async_op_complete);
			
			DPNMSG_ASYNC_OP_COMPLETE *oc = (DPNMSG_ASYNC_OP_COMPLETE*)(pMessage);
			
			EXPECT_EQ(oc->dwSize,        sizeof(DPNMSG_ASYNC_OP_COMPLETE));
			EXPECT_EQ(oc->hAsyncOp,      async_handle);
			EXPECT_EQ(oc->pvUserContext, (void*)(0xABCD));
			EXPECT_EQ(oc->hResultCode,   DPNERR_USERCANCEL);
			
			got_async_op_complete = true;
		}
		
		return DPN_OK;
	};
	
	IDP8PeerInstance client;
	
	ASSERT_EQ(client->Initialize(&callback, &callback_shim, 0), S_OK);
	
	DWORD start = GetTickCount();
	
	ASSERT_EQ(client->EnumHosts(
		NULL,              /* pApplicationDesc */
		NULL,              /* pdpaddrHost */
		NULL,              /* pdpaddrDeviceInfo */
		NULL,              /* pvUserEnumData */
		0,                 /* dwUserEnumDataSize */
		3,                 /* dwEnumCount */
		500,               /* dwRetryInterval */
		500,               /* dwTimeOut*/
		(void*)(0xABCD),   /* pvUserContext */
		&async_handle,     /* pAsyncHandle */
		0                  /* dwFlags */
	), DPNSUCCESS_PENDING);
	
	ASSERT_EQ(client->CancelAsyncOperation(0, DPNCANCEL_ENUM), S_OK);
	
	Sleep(500);
	
	EXPECT_TRUE(got_async_op_complete);
	
	if(got_async_op_complete)
	{
		DWORD enum_time_ms = got_async_op_complete_at - start;
		EXPECT_TRUE(enum_time_ms <= 250);
	}
}

TEST(DirectPlay8Peer, EnumHostsAsyncCancelAllOperations)
{
	bool got_async_op_complete = false;
	DWORD got_async_op_complete_at;
	DPNHANDLE async_handle;
	
	std::function<HRESULT(DWORD,PVOID)> callback =
		[&got_async_op_complete, &got_async_op_complete_at, &async_handle]
		(DWORD dwMessageType, PVOID pMessage)
	{
		if(dwMessageType == DPN_MSGID_ASYNC_OP_COMPLETE)
		{
			got_async_op_complete_at = GetTickCount();
			
			/* We shouldn't get DPNMSG_ASYNC_OP_COMPLETE multiple times. */
			EXPECT_FALSE(got_async_op_complete);
			
			DPNMSG_ASYNC_OP_COMPLETE *oc = (DPNMSG_ASYNC_OP_COMPLETE*)(pMessage);
			
			EXPECT_EQ(oc->dwSize,        sizeof(DPNMSG_ASYNC_OP_COMPLETE));
			EXPECT_EQ(oc->hAsyncOp,      async_handle);
			EXPECT_EQ(oc->pvUserContext, (void*)(0xABCD));
			EXPECT_EQ(oc->hResultCode,   DPNERR_USERCANCEL);
			
			got_async_op_complete = true;
		}
		
		return DPN_OK;
	};
	
	IDP8PeerInstance client;
	
	ASSERT_EQ(client->Initialize(&callback, &callback_shim, 0), S_OK);
	
	DWORD start = GetTickCount();
	
	ASSERT_EQ(client->EnumHosts(
		NULL,              /* pApplicationDesc */
		NULL,              /* pdpaddrHost */
		NULL,              /* pdpaddrDeviceInfo */
		NULL,              /* pvUserEnumData */
		0,                 /* dwUserEnumDataSize */
		3,                 /* dwEnumCount */
		500,               /* dwRetryInterval */
		500,               /* dwTimeOut*/
		(void*)(0xABCD),   /* pvUserContext */
		&async_handle,     /* pAsyncHandle */
		0                  /* dwFlags */
	), DPNSUCCESS_PENDING);
	
	ASSERT_EQ(client->CancelAsyncOperation(0, DPNCANCEL_ALL_OPERATIONS), S_OK);
	
	Sleep(500);
	
	EXPECT_TRUE(got_async_op_complete);
	
	if(got_async_op_complete)
	{
		DWORD enum_time_ms = got_async_op_complete_at - start;
		EXPECT_TRUE(enum_time_ms <= 250);
	}
}

TEST(DirectPlay8Peer, EnumHostsAsyncCancelByClose)
{
	bool got_async_op_complete = false;
	DWORD got_async_op_complete_at;
	DPNHANDLE async_handle;
	
	std::function<HRESULT(DWORD,PVOID)> callback =
		[&got_async_op_complete, &got_async_op_complete_at, &async_handle]
		(DWORD dwMessageType, PVOID pMessage)
	{
		if(dwMessageType == DPN_MSGID_ASYNC_OP_COMPLETE)
		{
			got_async_op_complete_at = GetTickCount();
			
			/* We shouldn't get DPNMSG_ASYNC_OP_COMPLETE multiple times. */
			EXPECT_FALSE(got_async_op_complete);
			
			DPNMSG_ASYNC_OP_COMPLETE *oc = (DPNMSG_ASYNC_OP_COMPLETE*)(pMessage);
			
			EXPECT_EQ(oc->dwSize,        sizeof(DPNMSG_ASYNC_OP_COMPLETE));
			EXPECT_EQ(oc->hAsyncOp,      async_handle);
			EXPECT_EQ(oc->pvUserContext, (void*)(0xABCD));
			EXPECT_EQ(oc->hResultCode,   DPNERR_USERCANCEL);
			
			got_async_op_complete = true;
		}
		
		return DPN_OK;
	};
	
	IDP8PeerInstance client;
	
	ASSERT_EQ(client->Initialize(&callback, &callback_shim, 0), S_OK);
	
	DWORD start = GetTickCount();
	
	ASSERT_EQ(client->EnumHosts(
		NULL,              /* pApplicationDesc */
		NULL,              /* pdpaddrHost */
		NULL,              /* pdpaddrDeviceInfo */
		NULL,              /* pvUserEnumData */
		0,                 /* dwUserEnumDataSize */
		3,                 /* dwEnumCount */
		500,               /* dwRetryInterval */
		500,               /* dwTimeOut*/
		(void*)(0xABCD),   /* pvUserContext */
		&async_handle,     /* pAsyncHandle */
		0                  /* dwFlags */
	), DPNSUCCESS_PENDING);
	
	ASSERT_EQ(client->Close(0), S_OK);
	
	EXPECT_TRUE(got_async_op_complete);
	
	if(got_async_op_complete)
	{
		DWORD enum_time_ms = got_async_op_complete_at - start;
		EXPECT_TRUE(enum_time_ms <= 250);
	}
}

TEST(DirectPlay8Peer, EnumHostsFilterByApplicationGUID)
{
	bool right_app_got_host_enum_query = false;
	bool wrong_app_got_host_enum_query = false;
	
	SessionHost a1s1(APP_GUID_1, L"Application 1 Session 1",
		[&wrong_app_got_host_enum_query]
		(DWORD dwMessageType, PVOID pMessage)
		{
			if(dwMessageType == DPN_MSGID_ENUM_HOSTS_QUERY)
			{
				wrong_app_got_host_enum_query = true;
			}
			
			return DPN_OK;
		});
	
	SessionHost a1s2(APP_GUID_1, L"Application 1 Session 2",
		[&wrong_app_got_host_enum_query]
		(DWORD dwMessageType, PVOID pMessage)
		{
			if(dwMessageType == DPN_MSGID_ENUM_HOSTS_QUERY)
			{
				wrong_app_got_host_enum_query = true;
			}
			
			return DPN_OK;
		});
	
	SessionHost a2s1(APP_GUID_2, L"Application 2 Session 1",
		[&right_app_got_host_enum_query]
		(DWORD dwMessageType, PVOID pMessage)
		{
			if(dwMessageType == DPN_MSGID_ENUM_HOSTS_QUERY)
			{
				DPNMSG_ENUM_HOSTS_QUERY *ehq = (DPNMSG_ENUM_HOSTS_QUERY*)(pMessage);
				
				EXPECT_EQ(ehq->dwSize, sizeof(DPNMSG_ENUM_HOSTS_QUERY));
				
				/* TODO: Check pAddressSender, pAddressDevice */
				
				EXPECT_EQ(ehq->pvReceivedData, nullptr);
				EXPECT_EQ(ehq->dwReceivedDataSize, 0);
				
				EXPECT_EQ(ehq->pvResponseData, nullptr);
				EXPECT_EQ(ehq->dwResponseDataSize, 0);
				
				right_app_got_host_enum_query = true;
			}
			
			return DPN_OK;
		});
	
	std::map<GUID, FoundSession, CompareGUID> sessions;
	
	std::function<HRESULT(DWORD,PVOID)> client_cb =
		[&sessions]
		(DWORD dwMessageType, PVOID pMessage)
	{
		if(dwMessageType == DPN_MSGID_ENUM_HOSTS_RESPONSE)
		{
			DPNMSG_ENUM_HOSTS_RESPONSE *ehr = (DPNMSG_ENUM_HOSTS_RESPONSE*)(pMessage);
			
			EXPECT_EQ(ehr->dwSize,        sizeof(DPNMSG_ENUM_HOSTS_RESPONSE));
			EXPECT_EQ(ehr->pvUserContext, (void*)(0xBEEF));
			
			sessions.emplace(
				ehr->pApplicationDescription->guidInstance,
				FoundSession(
					ehr->pApplicationDescription->guidApplication,
					ehr->pApplicationDescription->pwszSessionName));
		}
		
		return DPN_OK;
	};
	
	IDP8PeerInstance client;
	
	ASSERT_EQ(client->Initialize(&client_cb, &callback_shim, 0), S_OK);
	
	DPN_APPLICATION_DESC app_desc;
	memset(&app_desc, 0, sizeof(app_desc));
	
	app_desc.dwSize          = sizeof(app_desc);
	app_desc.guidApplication = APP_GUID_2;
	
	ASSERT_EQ(client->EnumHosts(
		&app_desc,         /* pApplicationDesc */
		NULL,              /* pdpaddrHost */
		NULL,              /* pdpaddrDeviceInfo */
		NULL,              /* pvUserEnumData */
		0,                 /* dwUserEnumDataSize */
		3,                 /* dwEnumCount */
		500,               /* dwRetryInterval */
		500,               /* dwTimeOut*/
		(void*)(0xBEEF),   /* pvUserContext */
		NULL,              /* pAsyncHandle */
		DPNENUMHOSTS_SYNC  /* dwFlags */
	), S_OK);
	
	FoundSession expect_sessions[] = {
		FoundSession(APP_GUID_2, L"Application 2 Session 1"),
	};
	
	EXPECT_SESSIONS(sessions, expect_sessions, expect_sessions + 1);
	
	EXPECT_TRUE(right_app_got_host_enum_query);
	EXPECT_FALSE(wrong_app_got_host_enum_query);
}

TEST(DirectPlay8Peer, EnumHostsFilterByNULLApplicationGUID)
{
	SessionHost a1s1(APP_GUID_1, L"Application 1 Session 1");
	SessionHost a1s2(APP_GUID_1, L"Application 1 Session 2");
	SessionHost a2s1(APP_GUID_2, L"Application 2 Session 1");
	
	std::map<GUID, FoundSession, CompareGUID> sessions;
	
	std::function<HRESULT(DWORD,PVOID)> client_cb =
		[&sessions]
		(DWORD dwMessageType, PVOID pMessage)
	{
		if(dwMessageType == DPN_MSGID_ENUM_HOSTS_RESPONSE)
		{
			DPNMSG_ENUM_HOSTS_RESPONSE *ehr = (DPNMSG_ENUM_HOSTS_RESPONSE*)(pMessage);
			
			EXPECT_EQ(ehr->dwSize,        sizeof(DPNMSG_ENUM_HOSTS_RESPONSE));
			EXPECT_EQ(ehr->pvUserContext, (void*)(0xBEEF));
			
			sessions.emplace(
				ehr->pApplicationDescription->guidInstance,
				FoundSession(
					ehr->pApplicationDescription->guidApplication,
					ehr->pApplicationDescription->pwszSessionName));
		}
		
		return DPN_OK;
	};
	
	IDP8PeerInstance client;
	
	ASSERT_EQ(client->Initialize(&client_cb, &callback_shim, 0), S_OK);
	
	DPN_APPLICATION_DESC app_desc;
	memset(&app_desc, 0, sizeof(app_desc));
	
	app_desc.dwSize          = sizeof(app_desc);
	app_desc.guidApplication = GUID_NULL;
	
	ASSERT_EQ(client->EnumHosts(
		&app_desc,         /* pApplicationDesc */
		NULL,              /* pdpaddrHost */
		NULL,              /* pdpaddrDeviceInfo */
		NULL,              /* pvUserEnumData */
		0,                 /* dwUserEnumDataSize */
		3,                 /* dwEnumCount */
		500,               /* dwRetryInterval */
		500,               /* dwTimeOut*/
		(void*)(0xBEEF),   /* pvUserContext */
		NULL,              /* pAsyncHandle */
		DPNENUMHOSTS_SYNC  /* dwFlags */
	), S_OK);
	
	FoundSession expect_sessions[] = {
		FoundSession(APP_GUID_1, L"Application 1 Session 1"),
		FoundSession(APP_GUID_1, L"Application 1 Session 2"),
		FoundSession(APP_GUID_2, L"Application 2 Session 1"),
	};
	
	EXPECT_SESSIONS(sessions, expect_sessions, expect_sessions + 3);
}

TEST(DirectPlay8Peer, EnumHostsDataInQuery)
{
	static const unsigned char DATA[] = { 0x00, 0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF };
	
	SessionHost a1s1(APP_GUID_1, L"Application 1 Session 1",
		[]
		(DWORD dwMessageType, PVOID pMessage)
		{
			if(dwMessageType == DPN_MSGID_ENUM_HOSTS_QUERY)
			{
				DPNMSG_ENUM_HOSTS_QUERY *ehq = (DPNMSG_ENUM_HOSTS_QUERY*)(pMessage);
				
				EXPECT_EQ(ehq->dwSize, sizeof(DPNMSG_ENUM_HOSTS_QUERY));
				
				std::vector<unsigned char> got_data(
					(const unsigned char*)(ehq->pvReceivedData),
					(const unsigned char*)(ehq->pvReceivedData) + ehq->dwReceivedDataSize);
				
				std::vector<unsigned char> expect_data(DATA, DATA + sizeof(DATA));
				
				EXPECT_EQ(got_data, expect_data);
				
				EXPECT_EQ(ehq->pvResponseData, nullptr);
				EXPECT_EQ(ehq->dwResponseDataSize, 0);
			}
			
			return DPN_OK;
		});
	
	std::map<GUID, FoundSession, CompareGUID> sessions;
	
	std::function<HRESULT(DWORD,PVOID)> client_cb =
		[&sessions]
		(DWORD dwMessageType, PVOID pMessage)
	{
		if(dwMessageType == DPN_MSGID_ENUM_HOSTS_RESPONSE)
		{
			DPNMSG_ENUM_HOSTS_RESPONSE *ehr = (DPNMSG_ENUM_HOSTS_RESPONSE*)(pMessage);
			
			EXPECT_EQ(ehr->dwSize,        sizeof(DPNMSG_ENUM_HOSTS_RESPONSE));
			EXPECT_EQ(ehr->pvUserContext, (void*)(0xBEEF));
			
			EXPECT_EQ(ehr->pvResponseData, nullptr);
			EXPECT_EQ(ehr->dwResponseDataSize, 0);
			
			sessions.emplace(
				ehr->pApplicationDescription->guidInstance,
				FoundSession(
					ehr->pApplicationDescription->guidApplication,
					ehr->pApplicationDescription->pwszSessionName));
		}
		
		return DPN_OK;
	};
	
	IDP8PeerInstance client;
	
	ASSERT_EQ(client->Initialize(&client_cb, &callback_shim, 0), S_OK);
	
	ASSERT_EQ(client->EnumHosts(
		NULL,              /* pApplicationDesc */
		NULL,              /* pdpaddrHost */
		NULL,              /* pdpaddrDeviceInfo */
		(void*)(DATA),     /* pvUserEnumData */
		sizeof(DATA),      /* dwUserEnumDataSize */
		3,                 /* dwEnumCount */
		500,               /* dwRetryInterval */
		500,               /* dwTimeOut*/
		(void*)(0xBEEF),   /* pvUserContext */
		NULL,              /* pAsyncHandle */
		DPNENUMHOSTS_SYNC  /* dwFlags */
	), S_OK);
	
	FoundSession expect_sessions[] = {
		FoundSession(APP_GUID_1, L"Application 1 Session 1"),
	};
	
	EXPECT_SESSIONS(sessions, expect_sessions, expect_sessions + 1);
}

TEST(DirectPlay8Peer, EnumHostsDataInResponse)
{
	static const unsigned char DATA[] = { 0x00, 0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF };
	
	bool got_return_buffer = false;
	
	SessionHost a1s1(APP_GUID_1, L"Application 1 Session 1",
		[&got_return_buffer]
		(DWORD dwMessageType, PVOID pMessage)
		{
			if(dwMessageType == DPN_MSGID_ENUM_HOSTS_QUERY)
			{
				DPNMSG_ENUM_HOSTS_QUERY *ehq = (DPNMSG_ENUM_HOSTS_QUERY*)(pMessage);
				
				EXPECT_EQ(ehq->dwSize, sizeof(DPNMSG_ENUM_HOSTS_QUERY));
				
				EXPECT_EQ(ehq->pvReceivedData, nullptr);
				EXPECT_EQ(ehq->dwReceivedDataSize, 0);
				
				EXPECT_EQ(ehq->pvResponseData, nullptr);
				EXPECT_EQ(ehq->dwResponseDataSize, 0);
				
				ehq->pvResponseData     = (void*)(DATA);
				ehq->dwResponseDataSize = sizeof(DATA);
				ehq->pvResponseContext  = (void*)(0x1234);
			}
			else if(dwMessageType == DPN_MSGID_RETURN_BUFFER)
			{
				DPNMSG_RETURN_BUFFER *rb = (DPNMSG_RETURN_BUFFER*)(pMessage);
				
				EXPECT_EQ(rb->dwSize,        sizeof(*rb));
				EXPECT_EQ(rb->hResultCode,   S_OK);
				EXPECT_EQ(rb->pvBuffer,      DATA);
				EXPECT_EQ(rb->pvUserContext, (void*)(0x1234));
				
				got_return_buffer = true;
			}
			
			return DPN_OK;
		});
	
	std::map<GUID, FoundSession, CompareGUID> sessions;
	
	std::function<HRESULT(DWORD,PVOID)> client_cb =
		[&sessions]
		(DWORD dwMessageType, PVOID pMessage)
	{
		if(dwMessageType == DPN_MSGID_ENUM_HOSTS_RESPONSE)
		{
			DPNMSG_ENUM_HOSTS_RESPONSE *ehr = (DPNMSG_ENUM_HOSTS_RESPONSE*)(pMessage);
			
			EXPECT_EQ(ehr->dwSize,        sizeof(DPNMSG_ENUM_HOSTS_RESPONSE));
			EXPECT_EQ(ehr->pvUserContext, (void*)(0xBEEF));
			
			std::vector<unsigned char> got_data(
				(const unsigned char*)(ehr->pvResponseData),
				(const unsigned char*)(ehr->pvResponseData) + ehr->dwResponseDataSize);
			
			std::vector<unsigned char> expect_data(DATA, DATA + sizeof(DATA));
			
			EXPECT_EQ(got_data, expect_data);
			
			sessions.emplace(
				ehr->pApplicationDescription->guidInstance,
				FoundSession(
					ehr->pApplicationDescription->guidApplication,
					ehr->pApplicationDescription->pwszSessionName));
		}
		
		return DPN_OK;
	};
	
	IDP8PeerInstance client;
	
	ASSERT_EQ(client->Initialize(&client_cb, &callback_shim, 0), S_OK);
	
	ASSERT_EQ(client->EnumHosts(
		NULL,              /* pApplicationDesc */
		NULL,              /* pdpaddrHost */
		NULL,              /* pdpaddrDeviceInfo */
		NULL,              /* pvUserEnumData */
		0,                 /* dwUserEnumDataSize */
		3,                 /* dwEnumCount */
		500,               /* dwRetryInterval */
		500,               /* dwTimeOut*/
		(void*)(0xBEEF),   /* pvUserContext */
		NULL,              /* pAsyncHandle */
		DPNENUMHOSTS_SYNC  /* dwFlags */
	), S_OK);
	
	FoundSession expect_sessions[] = {
		FoundSession(APP_GUID_1, L"Application 1 Session 1"),
	};
	
	EXPECT_SESSIONS(sessions, expect_sessions, expect_sessions + 1);
	
	EXPECT_TRUE(got_return_buffer);
}

/* TODO: Test enumerating a session directly. */

TEST(DirectPlay8Peer, ConnectSync)
{
	std::atomic<bool> testing(true);
	
	std::atomic<int> host_seq(0), p1_seq(0);
	DPNID host_player_id = -1, p1_player_id = -1;
	
	SessionHost host(APP_GUID_1, L"Session 1", PORT,
		[&testing, &host_seq, &host_player_id, &p1_player_id]
		(DWORD dwMessageType, PVOID pMessage)
		{
			if(!testing)
			{
				return DPN_OK;
			}
			
			int seq = ++host_seq;
			
			switch(seq)
			{
				case 1:
					EXPECT_EQ(dwMessageType, DPN_MSGID_CREATE_PLAYER);
					
					if(dwMessageType == DPN_MSGID_CREATE_PLAYER)
					{
						DPNMSG_CREATE_PLAYER *cp = (DPNMSG_CREATE_PLAYER*)(pMessage);
						host_player_id = cp->dpnidPlayer;
						
						EXPECT_EQ(cp->dwSize,          sizeof(DPNMSG_CREATE_PLAYER));
						EXPECT_EQ(cp->pvPlayerContext, (void*)(0xB00));
						
						cp->pvPlayerContext = (void*)(0xB00B00);
					}
					
					break;
					
				case 2:
					EXPECT_EQ(dwMessageType, DPN_MSGID_INDICATE_CONNECT);
					
					if(dwMessageType == DPN_MSGID_INDICATE_CONNECT)
					{
						DPNMSG_INDICATE_CONNECT *ic = (DPNMSG_INDICATE_CONNECT*)(pMessage);
						
						EXPECT_EQ(ic->dwSize, sizeof(DPNMSG_INDICATE_CONNECT));
						
						EXPECT_EQ(ic->pvUserConnectData,     (void*)(NULL));
						EXPECT_EQ(ic->dwUserConnectDataSize, 0);
						
						EXPECT_EQ(ic->pvReplyData,     (void*)(NULL));
						EXPECT_EQ(ic->dwReplyDataSize, 0);
						
						EXPECT_EQ(ic->pvReplyContext,  (void*)(NULL));
						EXPECT_EQ(ic->pvPlayerContext, (void*)(NULL));
						
						/* TODO: Check pAddressPlayer, pAddressDevice */
						
						ic->pvPlayerContext = (void*)(0xB441);
					}
					
					break;
					
				case 3:
					EXPECT_EQ(dwMessageType, DPN_MSGID_CREATE_PLAYER);
					
					if(dwMessageType == DPN_MSGID_CREATE_PLAYER)
					{
						DPNMSG_CREATE_PLAYER *cp = (DPNMSG_CREATE_PLAYER*)(pMessage);
						p1_player_id = cp->dpnidPlayer;
						
						EXPECT_EQ(cp->dwSize,          sizeof(DPNMSG_CREATE_PLAYER));
						EXPECT_EQ(cp->pvPlayerContext, (void*)(0xB441));
						
						cp->pvPlayerContext = (void*)(0xFEED);
					}
					
					break;
				
				default:
					ADD_FAILURE() << "Unexpected message of type " << dwMessageType <<", sequence " << seq;
					break;
			}
			
			return DPN_OK;
		});
	
	/* Give the host instance a moment to settle. */
	Sleep(1000);
	
	DPNID p1_cp_dpnidPlayer = -1, p1_cc_dpnidLocal = -1;
	
	std::function<HRESULT(DWORD,PVOID)> p1_cb =
		[&testing, &p1_seq, &host_player_id, &p1_player_id, &p1_cp_dpnidPlayer, &p1_cc_dpnidLocal]
		(DWORD dwMessageType, PVOID pMessage)
		{
			if(!testing)
			{
				return DPN_OK;
			}
			
			int seq = ++p1_seq;
			
			switch(seq)
			{
				case 1:
					EXPECT_EQ(dwMessageType, DPN_MSGID_CREATE_PLAYER);
					
					if(dwMessageType == DPN_MSGID_CREATE_PLAYER)
					{
						DPNMSG_CREATE_PLAYER *cp = (DPNMSG_CREATE_PLAYER*)(pMessage);
						p1_cp_dpnidPlayer = cp->dpnidPlayer;
						
						EXPECT_EQ(cp->dwSize,          sizeof(DPNMSG_CREATE_PLAYER));
						EXPECT_EQ(cp->pvPlayerContext, (void*)(0xBCDE));
						
						cp->pvPlayerContext = (void*)(0xCDEF);
					}
					
					break;
					
				case 2:
					EXPECT_EQ(dwMessageType, DPN_MSGID_CREATE_PLAYER);
					
					if(dwMessageType == DPN_MSGID_CREATE_PLAYER)
					{
						DPNMSG_CREATE_PLAYER *cp = (DPNMSG_CREATE_PLAYER*)(pMessage);
						
						EXPECT_EQ(cp->dwSize,          sizeof(DPNMSG_CREATE_PLAYER));
						EXPECT_EQ(cp->dpnidPlayer,     host_player_id);
						EXPECT_EQ(cp->pvPlayerContext, (void*)(0));
						
						cp->pvPlayerContext = (void*)(0xBAA);
					}
					
					break;
					
				case 3:
					EXPECT_EQ(dwMessageType, DPN_MSGID_CONNECT_COMPLETE);
					
					if(dwMessageType == DPN_MSGID_CONNECT_COMPLETE)
					{
						DPNMSG_CONNECT_COMPLETE *cc = (DPNMSG_CONNECT_COMPLETE*)(pMessage);
						
						EXPECT_EQ(cc->dwSize,      sizeof(DPNMSG_CONNECT_COMPLETE));
						EXPECT_EQ(cc->hAsyncOp,    0);
						EXPECT_EQ(cc->hResultCode, S_OK);
						
						EXPECT_EQ(cc->pvApplicationReplyData,     (PVOID)(NULL));
						EXPECT_EQ(cc->dwApplicationReplyDataSize, 0);
						
						p1_cc_dpnidLocal = cc->dpnidLocal;
					}
					
					break;
					
				default:
					ADD_FAILURE() << "Unexpected message of type " << dwMessageType <<", sequence " << seq;
					break;
			}
			
			return DPN_OK;
		};
	
	IDP8PeerInstance p1;
	
	ASSERT_EQ(p1->Initialize(&p1_cb, &callback_shim, 0), S_OK);
	
	DPN_APPLICATION_DESC connect_to_app;
	memset(&connect_to_app, 0, sizeof(connect_to_app));
	
	connect_to_app.dwSize = sizeof(connect_to_app);
	connect_to_app.guidApplication = APP_GUID_1;
	
	IDP8AddressInstance connect_to_addr(L"127.0.0.1", PORT);
	
	EXPECT_EQ(p1->Connect(
		&connect_to_app,  /* pdnAppDesc */
		connect_to_addr,  /* pHostAddr */
		NULL,             /* pDeviceInfo */
		NULL,             /* pdnSecurity */
		NULL,             /* pdnCredentials */
		NULL,             /* pvUserConnectData */
		0,                /* dwUserConnectDataSize */
		(void*)(0xBCDE),  /* pvPlayerContext */
		NULL,             /* pvAsyncContext */
		NULL,             /* phAsyncHandle */
		DPNCONNECT_SYNC   /* dwFlags */
	), S_OK);
	
	/* Give the host instance a moment to settle. */
	Sleep(1000);
	
	testing = false;
	
	EXPECT_EQ(host_seq, 3);
	EXPECT_EQ(p1_seq, 3);
	
	EXPECT_EQ(p1_cp_dpnidPlayer, p1_player_id);
	EXPECT_EQ(p1_cc_dpnidLocal,  p1_player_id);
}

TEST(DirectPlay8Peer, ConnectSyncFail)
{
	SessionHost host(APP_GUID_2, L"Session 1", PORT,
		[]
		(DWORD dwMessageType, PVOID pMessage)
		{
			return DPN_OK;
		});
	
	std::atomic<int> p1_seq(0);
	
	std::function<HRESULT(DWORD,PVOID)> p1_cb =
		[&p1_seq]
		(DWORD dwMessageType, PVOID pMessage)
		{
			int seq = ++p1_seq;
			
			switch(seq)
			{
				case 1:
					EXPECT_EQ(dwMessageType, DPN_MSGID_CONNECT_COMPLETE);
					
					if(dwMessageType == DPN_MSGID_CONNECT_COMPLETE)
					{
						DPNMSG_CONNECT_COMPLETE *cc = (DPNMSG_CONNECT_COMPLETE*)(pMessage);
						
						EXPECT_EQ(cc->dwSize,      sizeof(DPNMSG_CONNECT_COMPLETE));
						EXPECT_EQ(cc->hAsyncOp,    0);
						EXPECT_NE(cc->hResultCode, S_OK);
						
						EXPECT_EQ(cc->pvApplicationReplyData,     (PVOID)(NULL));
						EXPECT_EQ(cc->dwApplicationReplyDataSize, 0);
					}
					
					break;
					
				default:
					ADD_FAILURE() << "Unexpected message of type " << dwMessageType <<", sequence " << seq;
					break;
			}
			
			return DPN_OK;
		};
	
	IDP8PeerInstance p1;
	
	ASSERT_EQ(p1->Initialize(&p1_cb, &callback_shim, 0), S_OK);
	
	DPN_APPLICATION_DESC connect_to_app;
	memset(&connect_to_app, 0, sizeof(connect_to_app));
	
	connect_to_app.dwSize = sizeof(connect_to_app);
	connect_to_app.guidApplication = APP_GUID_1;
	
	IDP8AddressInstance connect_to_addr(L"127.0.0.1", PORT);
	
	EXPECT_NE(p1->Connect(
		&connect_to_app,  /* pdnAppDesc */
		connect_to_addr,  /* pHostAddr */
		NULL,             /* pDeviceInfo */
		NULL,             /* pdnSecurity */
		NULL,             /* pdnCredentials */
		NULL,             /* pvUserConnectData */
		0,                /* dwUserConnectDataSize */
		NULL,             /* pvPlayerContext */
		NULL,             /* pvAsyncContext */
		NULL,             /* phAsyncHandle */
		DPNCONNECT_SYNC   /* dwFlags */
	), S_OK);
	
	EXPECT_EQ(p1_seq, 1);
}

TEST(DirectPlay8Peer, ConnectAsync)
{
	std::atomic<bool> testing(true);
	
	std::atomic<int> host_seq(0), p1_seq(0);
	DPNID host_player_id = -1, p1_player_id = -1;
	
	SessionHost host(APP_GUID_1, L"Session 1", PORT,
		[&testing, &host_seq, &host_player_id, &p1_player_id]
		(DWORD dwMessageType, PVOID pMessage)
		{
			if(!testing)
			{
				return DPN_OK;
			}
			
			int seq = ++host_seq;
			
			switch(seq)
			{
				case 1:
					EXPECT_EQ(dwMessageType, DPN_MSGID_CREATE_PLAYER);
					
					if(dwMessageType == DPN_MSGID_CREATE_PLAYER)
					{
						DPNMSG_CREATE_PLAYER *cp = (DPNMSG_CREATE_PLAYER*)(pMessage);
						host_player_id = cp->dpnidPlayer;
						
						EXPECT_EQ(cp->dwSize,          sizeof(DPNMSG_CREATE_PLAYER));
						EXPECT_EQ(cp->pvPlayerContext, (void*)(0xB00));
						
						cp->pvPlayerContext = (void*)(0xB00B00);
					}
					
					break;
					
				case 2:
					EXPECT_EQ(dwMessageType, DPN_MSGID_INDICATE_CONNECT);
					
					if(dwMessageType == DPN_MSGID_INDICATE_CONNECT)
					{
						DPNMSG_INDICATE_CONNECT *ic = (DPNMSG_INDICATE_CONNECT*)(pMessage);
						
						EXPECT_EQ(ic->dwSize, sizeof(DPNMSG_INDICATE_CONNECT));
						
						EXPECT_EQ(ic->pvUserConnectData,     (void*)(NULL));
						EXPECT_EQ(ic->dwUserConnectDataSize, 0);
						
						EXPECT_EQ(ic->pvReplyData,     (void*)(NULL));
						EXPECT_EQ(ic->dwReplyDataSize, 0);
						
						EXPECT_EQ(ic->pvReplyContext,  (void*)(NULL));
						EXPECT_EQ(ic->pvPlayerContext, (void*)(NULL));
						
						/* TODO: Check pAddressPlayer, pAddressDevice */
						
						ic->pvPlayerContext = (void*)(0xB441);
					}
					
					break;
					
				case 3:
					EXPECT_EQ(dwMessageType, DPN_MSGID_CREATE_PLAYER);
					
					if(dwMessageType == DPN_MSGID_CREATE_PLAYER)
					{
						DPNMSG_CREATE_PLAYER *cp = (DPNMSG_CREATE_PLAYER*)(pMessage);
						p1_player_id = cp->dpnidPlayer;
						
						EXPECT_EQ(cp->dwSize,          sizeof(DPNMSG_CREATE_PLAYER));
						EXPECT_EQ(cp->pvPlayerContext, (void*)(0xB441));
						
						cp->pvPlayerContext = (void*)(0xFEED);
					}
					
					break;
				
				default:
					ADD_FAILURE() << "Unexpected message of type " << dwMessageType <<", sequence " << seq;
					break;
			}
			
			return DPN_OK;
		});
	
	/* Give the host instance a moment to settle. */
	Sleep(1000);
	
	DPNID p1_cp_dpnidPlayer = -1, p1_cc_dpnidLocal = -1;
	DPNHANDLE p1_connect_handle;
	
	std::function<HRESULT(DWORD,PVOID)> p1_cb =
		[&testing, &p1_seq, &host_player_id, &p1_player_id, &p1_cp_dpnidPlayer, &p1_connect_handle, &p1_cc_dpnidLocal]
		(DWORD dwMessageType, PVOID pMessage)
		{
			if(!testing)
			{
				return DPN_OK;
			}
			
			int seq = ++p1_seq;
			
			switch(seq)
			{
				case 1:
					EXPECT_EQ(dwMessageType, DPN_MSGID_CREATE_PLAYER);
					
					if(dwMessageType == DPN_MSGID_CREATE_PLAYER)
					{
						DPNMSG_CREATE_PLAYER *cp = (DPNMSG_CREATE_PLAYER*)(pMessage);
						p1_cp_dpnidPlayer = cp->dpnidPlayer;
						
						EXPECT_EQ(cp->dwSize,          sizeof(DPNMSG_CREATE_PLAYER));
						EXPECT_EQ(cp->pvPlayerContext, (void*)(0xBCDE));
						
						cp->pvPlayerContext = (void*)(0xCDEF);
					}
					
					break;
					
				case 2:
					EXPECT_EQ(dwMessageType, DPN_MSGID_CREATE_PLAYER);
					
					if(dwMessageType == DPN_MSGID_CREATE_PLAYER)
					{
						DPNMSG_CREATE_PLAYER *cp = (DPNMSG_CREATE_PLAYER*)(pMessage);
						
						EXPECT_EQ(cp->dwSize,          sizeof(DPNMSG_CREATE_PLAYER));
						EXPECT_EQ(cp->dpnidPlayer,     host_player_id);
						EXPECT_EQ(cp->pvPlayerContext, (void*)(0));
						
						cp->pvPlayerContext = (void*)(0xBAA);
					}
					
					break;
					
				case 3:
					EXPECT_EQ(dwMessageType, DPN_MSGID_CONNECT_COMPLETE);
					
					if(dwMessageType == DPN_MSGID_CONNECT_COMPLETE)
					{
						DPNMSG_CONNECT_COMPLETE *cc = (DPNMSG_CONNECT_COMPLETE*)(pMessage);
						
						EXPECT_EQ(cc->dwSize,        sizeof(DPNMSG_CONNECT_COMPLETE));
						EXPECT_EQ(cc->hAsyncOp,      p1_connect_handle);
						EXPECT_EQ(cc->pvUserContext, (void*)(0xABCD));
						EXPECT_EQ(cc->hResultCode,   S_OK);
						
						EXPECT_EQ(cc->pvApplicationReplyData,     (PVOID)(NULL));
						EXPECT_EQ(cc->dwApplicationReplyDataSize, 0);
						
						p1_cc_dpnidLocal = cc->dpnidLocal;
					}
					
					break;
					
				default:
					ADD_FAILURE() << "Unexpected message of type " << dwMessageType <<", sequence " << seq;
					break;
			}
			
			return DPN_OK;
		};
	
	IDP8PeerInstance p1;
	
	ASSERT_EQ(p1->Initialize(&p1_cb, &callback_shim, 0), S_OK);
	
	DPN_APPLICATION_DESC connect_to_app;
	memset(&connect_to_app, 0, sizeof(connect_to_app));
	
	connect_to_app.dwSize = sizeof(connect_to_app);
	connect_to_app.guidApplication = APP_GUID_1;
	
	IDP8AddressInstance connect_to_addr(L"127.0.0.1", PORT);
	
	EXPECT_EQ(p1->Connect(
		&connect_to_app,     /* pdnAppDesc */
		connect_to_addr,     /* pHostAddr */
		NULL,                /* pDeviceInfo */
		NULL,                /* pdnSecurity */
		NULL,                /* pdnCredentials */
		NULL,                /* pvUserConnectData */
		0,                   /* dwUserConnectDataSize */
		(void*)(0xBCDE),     /* pvPlayerContext */
		(void*)(0xABCD),     /* pvAsyncContext */
		&p1_connect_handle,  /* phAsyncHandle */
		0                    /* dwFlags */
	), DPNSUCCESS_PENDING);
	
	/* Give the connect a chance to complete. */
	Sleep(5000);
	
	testing = false;
	
	EXPECT_EQ(host_seq, 3);
	EXPECT_EQ(p1_seq, 3);
	
	EXPECT_EQ(p1_cp_dpnidPlayer, p1_player_id);
	EXPECT_EQ(p1_cc_dpnidLocal,  p1_player_id);
	
	void *host_host_player_ctx;
	EXPECT_EQ(host->GetPlayerContext(host_player_id, &host_host_player_ctx, 0), S_OK);
	EXPECT_EQ(host_host_player_ctx, (void*)(0xB00B00));
	
	void *host_p1_player_ctx;
	EXPECT_EQ(host->GetPlayerContext(p1_player_id, &host_p1_player_ctx, 0), S_OK);
	EXPECT_EQ(host_p1_player_ctx, (void*)(0xFEED));
	
	void *p1_host_player_ctx;
	EXPECT_EQ(p1->GetPlayerContext(host_player_id, &p1_host_player_ctx, 0), S_OK);
	EXPECT_EQ(p1_host_player_ctx, (void*)(0xBAA));
	
	void *p1_p1_player_ctx;
	EXPECT_EQ(p1->GetPlayerContext(p1_player_id, &p1_p1_player_ctx, 0), S_OK);
	EXPECT_EQ(p1_p1_player_ctx, (void*)(0xCDEF));
}

TEST(DirectPlay8Peer, ConnectAsyncFail)
{
	SessionHost host(APP_GUID_2, L"Session 1", PORT,
		[]
		(DWORD dwMessageType, PVOID pMessage)
		{
			return DPN_OK;
		});
	
	std::atomic<int> p1_seq(0);
	
	DPNHANDLE p1_connect_handle;
	
	std::function<HRESULT(DWORD,PVOID)> p1_cb =
		[&p1_seq, &p1_connect_handle]
		(DWORD dwMessageType, PVOID pMessage)
		{
			int seq = ++p1_seq;
			
			switch(seq)
			{
				case 1:
					EXPECT_EQ(dwMessageType, DPN_MSGID_CONNECT_COMPLETE);
					
					if(dwMessageType == DPN_MSGID_CONNECT_COMPLETE)
					{
						DPNMSG_CONNECT_COMPLETE *cc = (DPNMSG_CONNECT_COMPLETE*)(pMessage);
						
						EXPECT_EQ(cc->dwSize,        sizeof(DPNMSG_CONNECT_COMPLETE));
						EXPECT_EQ(cc->hAsyncOp,      p1_connect_handle);
						EXPECT_EQ(cc->pvUserContext, (void*)(0xABCD));
						EXPECT_NE(cc->hResultCode,   S_OK);
						
						EXPECT_EQ(cc->pvApplicationReplyData,     (PVOID)(NULL));
						EXPECT_EQ(cc->dwApplicationReplyDataSize, 0);
					}
					
					break;
					
				default:
					ADD_FAILURE() << "Unexpected message of type " << dwMessageType <<", sequence " << seq;
					break;
			}
			
			return DPN_OK;
		};
	
	IDP8PeerInstance p1;
	
	ASSERT_EQ(p1->Initialize(&p1_cb, &callback_shim, 0), S_OK);
	
	DPN_APPLICATION_DESC connect_to_app;
	memset(&connect_to_app, 0, sizeof(connect_to_app));
	
	connect_to_app.dwSize = sizeof(connect_to_app);
	connect_to_app.guidApplication = APP_GUID_1;
	
	IDP8AddressInstance connect_to_addr(L"127.0.0.1", PORT);
	
	EXPECT_EQ(p1->Connect(
		&connect_to_app,     /* pdnAppDesc */
		connect_to_addr,     /* pHostAddr */
		NULL,                /* pDeviceInfo */
		NULL,                /* pdnSecurity */
		NULL,                /* pdnCredentials */
		NULL,                /* pvUserConnectData */
		0,                   /* dwUserConnectDataSize */
		NULL,                /* pvPlayerContext */
		(void*)(0xABCD),     /* pvAsyncContext */
		&p1_connect_handle,  /* phAsyncHandle */
		0                    /* dwFlags */
	), DPNSUCCESS_PENDING);
	
	/* Give the connect a chance to complete. */
	Sleep(5000);
	
	EXPECT_EQ(p1_seq, 1);
}

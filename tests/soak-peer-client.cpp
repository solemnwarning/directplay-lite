/* DirectPlay Lite
 * Copyright (C) 2018 Daniel Collins
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
#include <algorithm>
#include <assert.h>
#include <atomic>
#include <dplay8.h>
#include <mutex>
#include <objbase.h>
#include <psapi.h>
#include <stdarg.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <time.h>
#include <vector>
#include <windows.h>

/* Durations specified in milliseconds. */
#define STATS_INTERVAL (30 * 1000)

static const int MESSAGE_SIZES[] = { 17, 33, 257, 1025, 4097, 8193, 16385, 70000 };

static const GUID APP_GUID = { 0x8723c2c6, 0x0b89, 0x4ea0, { 0xad, 0xe8, 0xec, 0x53, 0x66, 0x51, 0x68, 0x9f } };

struct phase {
	int phase_duration_ms;
	int reconstruct_interval_ms;
	int reinitialise_interval_ms;
	int concurrent_messages;
};

static const struct phase PHASES[] = {
	/* Generate statistics with varying numbers of concurrent SendTo() operations. */
	{ 300000, 999999999, 999999999, 1 },
	{ 300000, 999999999, 999999999, 2 },
	{ 300000, 999999999, 999999999, 4 },
	{ 300000, 999999999, 999999999, 8 },
	{ 300000, 999999999, 999999999, 16 },
	{ 300000, 999999999, 999999999, 32 },
	{ 300000, 999999999, 999999999, 64 },
	{ 300000, 999999999, 999999999, 128 },
	
	/* Long running IDirectPlay8Peer instance and session. */
	{ 3600000, 999999999, 999999999, 2 },
	
	/* Long running IDirectPlay8Peer instance, frequent session reconnects. */
	{ 3600000, 999999999, 60000, 2 },
	
	/* Frequently recreated IDirectPlay8Peer instance. */
	{ 3600000, 60000, 999999999, 2 },
};

struct message_header {
	int64_t timestamp_us;
	int64_t msg_size_idx;
};

static int64_t pc_freq;
static int64_t now_ms();
static int64_t now_us();

static int64_t start_time;
static int64_t usage_time;
static IDirectPlay8Peer *instance;
static bool disconnected;
static const struct phase *phase;

static std::atomic<uint64_t> msg_num_complete;
static std::atomic<uint64_t> msg_total_send_us;
static std::atomic<uint64_t> msg_total_rtt_us;
static std::atomic<uint64_t> msg_total_recv_bytes;
static std::atomic<int64_t> msg_stats_start;

static HRESULT CALLBACK callback(PVOID pvUserContext, DWORD dwMessageType, PVOID pMessage);
static void print_stats();
static void timed_fprintf(FILE *fh, const char *fmt, ...);

#define timed_printf(...) timed_fprintf(stdout, __VA_ARGS__)

int main(int argc, char **argv)
{
	{
		LARGE_INTEGER li;
		QueryPerformanceFrequency(&li);
		pc_freq = li.QuadPart;
	}
	
	HRESULT res = CoInitialize(NULL);
	if(res != S_OK)
	{
		timed_fprintf(stderr, "CoInitialize failed with HRESULT %08x", (unsigned)(res));
		return 1;
	}
	
	start_time = now_ms();
	msg_num_complete = 0;
	
	for(int i = 0; i < (sizeof(PHASES) / sizeof(*PHASES)); ++i)
	{
		phase = &(PHASES[i]);
		
		int64_t phase_end_ms = now_ms() + phase->phase_duration_ms;
		
		while(now_ms() < phase_end_ms)
		{
			timed_printf("Constructing DirectPlay8Peer instance...");
			
			res = CoCreateInstance(CLSID_DirectPlay8Peer, NULL, CLSCTX_INPROC_SERVER, IID_IDirectPlay8Peer, (void**)(&instance));
			if(res != S_OK)
			{
				timed_fprintf(stderr, "Failed to construct DirectPlay8Peer instance (HRESULT %08x)", (unsigned)(res));
				return 1;
			}
			
			int64_t construct_time = now_ms();
			int64_t destruct_time  = construct_time + phase->reconstruct_interval_ms;
			
			while(now_ms() < destruct_time && now_ms() < phase_end_ms)
			{
				timed_printf("Initialising DirectPlay8Peer instance...");
				
				disconnected = false;
				msg_num_complete = 0;
				msg_total_send_us = 0;
				msg_total_rtt_us = 0;
				msg_total_recv_bytes = 0;
				
				int64_t initialise_time = now_ms();
				int64_t close_time = now_ms() + phase->reinitialise_interval_ms;
				
				res = instance->Initialize(NULL, &callback, 0);
				if(res != S_OK)
				{
					timed_fprintf(stderr, "IDirectPlay8Peer::Initialize failed with HRESULT %08x", (unsigned)(res));
					return 1;
				}
				
				bool connected = false;
				
				while(!disconnected && now_ms() < close_time && now_ms() < destruct_time && now_ms() < phase_end_ms)
				{
					if(!connected)
					{
						timed_printf("Enumerating sessions...");
						
						DPN_APPLICATION_DESC app_desc;
						memset(&app_desc, 0, sizeof(app_desc));
						
						app_desc.dwSize = sizeof(app_desc);
						app_desc.guidApplication = APP_GUID;
						
						IDirectPlay8Address *enum_address;
						res = CoCreateInstance(CLSID_DirectPlay8Address, NULL, CLSCTX_INPROC_SERVER, IID_IDirectPlay8Address, (void**)(&enum_address));
						if(res != S_OK)
						{
							timed_fprintf(stderr, "Failed to construct DirectPlay8Address instance (HRESULT %08x)", (unsigned)(res));
							return 1;
						}
						
						res = enum_address->SetSP(&CLSID_DP8SP_TCPIP);
						if(res != S_OK)
						{
							timed_fprintf(stderr, "IDirectPlay8Address::SetSP failed with HRESULT %08x", (unsigned)(res));
							return 1;
						}
						
						IDirectPlay8Address *connect_address = NULL;
						
						res = instance->EnumHosts(&app_desc, NULL, enum_address, NULL, 0, 0, 0, 0, &connect_address, NULL, DPNENUMHOSTS_SYNC);
						if(res != S_OK)
						{
							timed_fprintf(stderr, "IDirectPlay8Peer::EnumHosts failed with HRESULT %08x", (unsigned)(res));
							return 1;
						}
						
						enum_address->Release();
						
						if(connect_address == NULL)
						{
							continue;
						}
						
						timed_printf("Connecting to session...");
						
						res = instance->Connect(
							&app_desc,         /* pdnAppDesc */
							connect_address,   /* pHostAddr */
							NULL,              /* pDeviceInfo */
							NULL,              /* pdnSecurity */
							NULL,              /* pdnCredentials */
							NULL,              /* pvUserConnectData */
							0,                 /* dwUserConnectDataSize */
							(void*)(0xAAAA),   /* pvPlayerContext */
							NULL,              /* pvAsyncContext */
							NULL,              /* phAsyncHandle */
							DPNCONNECT_SYNC);  /* dwFlags */
						if(res != S_OK)
						{
							timed_fprintf(stderr, "IDirectPlay8Peer::Connect failed with HRESULT %08x", (unsigned)(res));
							continue;
						}
						
						connected = true;
						msg_stats_start = now_ms();
						
						connect_address->Release();
					}
					
					int64_t sleep_until = std::min({ usage_time, close_time, destruct_time, phase_end_ms });
					int64_t sleep_for   = sleep_until - now_ms();
					
					if(sleep_for > 0)
					{
						Sleep(sleep_for);
					}
					
					if(now_ms() >= usage_time)
					{
						print_stats();
					}
				}
				
				timed_printf("Closing DirectPlay8Peer instance...");
				
				/* Alternate between hard/soft closes. */
				
				static bool hard_close = false;
				res = instance->Close(hard_close ? DPNCLOSE_IMMEDIATE : 0);
				if(res != S_OK)
				{
					timed_fprintf(stderr, "IDirectPlay8Peer::Close() failed with HRESULT %08x", (unsigned int)(res));
					return 1;
				}
				
				hard_close = !hard_close;
			}
			
			timed_printf("Destroying DirectPlay8Peer instance...");
			
			instance->Release();
			instance = NULL;
		}
	}
	
	CoUninitialize();
	
	return 0;
}

static int64_t now_ms()
{
	return now_us() / 1000;
}

static int64_t now_us()
{
	LARGE_INTEGER ticks;
	QueryPerformanceCounter(&ticks);
	
	return ticks.QuadPart / (pc_freq / 1000000);
}

static HRESULT CALLBACK callback(PVOID pvUserContext, DWORD dwMessageType, PVOID pMessage)
{
	/* Pool of int64_t values used to pass the start time of any asyncronous SendTo() operation
	 * back into the callback to time how long it took to complete.
	*/
	static const int SEND_BEGIN_SIZE = 1024;
	static int64_t send_begin_buf[SEND_BEGIN_SIZE];
	static std::atomic<unsigned> send_begin_idx;
	
	switch(dwMessageType)
	{
		case DPN_MSGID_ENUM_HOSTS_RESPONSE:
		{
			DPNMSG_ENUM_HOSTS_RESPONSE *ehr = (DPNMSG_ENUM_HOSTS_RESPONSE*)(pMessage);
			IDirectPlay8Address **connect_address = (IDirectPlay8Address**)(ehr->pvUserContext);
			
			ehr->pAddressSender->AddRef();
			*connect_address = ehr->pAddressSender;
			
			break;
		}
		
		case DPN_MSGID_TERMINATE_SESSION:
		{
			timed_printf("Lost connection to session");
			disconnected = true;
			
			break;
		}
		
		case DPN_MSGID_CREATE_PLAYER:
		{
			DPNMSG_CREATE_PLAYER *cp = (DPNMSG_CREATE_PLAYER*)(pMessage);
			
			if(cp->pvPlayerContext == (void*)(0xAAAA))
			{
				/* Ignore our own player. */
				break;
			}
			
			timed_printf("New player ID: %u", (unsigned)(cp->dpnidPlayer));
			
			for(int i = 0; i < phase->concurrent_messages; ++i)
			{
				int this_msg_size_idx = 0;
				std::vector<unsigned char> buf(MESSAGE_SIZES[this_msg_size_idx]);
				
				assert(buf.size() >= sizeof(struct message_header));
				struct message_header *header = (struct message_header*)(buf.data());
				
				int64_t now = now_us();
				
				header->timestamp_us = now;
				header->msg_size_idx = this_msg_size_idx;
				
				DPN_BUFFER_DESC bd = { buf.size(), (BYTE*)(buf.data()) };
				
				int64_t *now_p = &(send_begin_buf[++send_begin_idx % SEND_BEGIN_SIZE]);
				*now_p = now;
				
				DPNHANDLE s_handle;
				HRESULT res = instance->SendTo(cp->dpnidPlayer, &bd, 1, 0, now_p, &s_handle, DPNSEND_GUARANTEED);
				if(res != DPNSUCCESS_PENDING)
				{
					timed_fprintf(stderr, "IDirectPlay8Peer::SendTo() failed with HRESULT %08x", (unsigned int)(res));
				}
			}
			
			break;
		}
		
		case DPN_MSGID_DESTROY_PLAYER:
		{
			DPNMSG_DESTROY_PLAYER *dp = (DPNMSG_DESTROY_PLAYER*)(pMessage);
			timed_printf("Destroyed player ID: %u", (unsigned)(dp->dpnidPlayer));
			
			break;
		}
		
		case DPN_MSGID_SEND_COMPLETE:
		{
			DPNMSG_SEND_COMPLETE *sc = (DPNMSG_SEND_COMPLETE*)(pMessage);
			
			int64_t now  = now_us();
			int64_t then = *(int64_t*)(sc->pvUserContext);
			
			msg_total_send_us += now - then;
			
			break;
		}
		
		case DPN_MSGID_RECEIVE:
		{
			DPNMSG_RECEIVE *r = (DPNMSG_RECEIVE*)(pMessage);
			
			int64_t now = now_us();
			
			assert(r->dwReceiveDataSize >= sizeof(struct message_header));
			struct message_header *r_header = (struct message_header*)(r->pReceiveData);
			
			int64_t rtt = now - r_header->timestamp_us;
			assert(rtt >= 0);
			
			msg_total_rtt_us += rtt;
			msg_total_recv_bytes += r->dwReceiveDataSize;
			++msg_num_complete;
			
			{
				int this_msg_size_idx = r_header->msg_size_idx + 1;
				if(this_msg_size_idx >= sizeof(MESSAGE_SIZES) / sizeof(*MESSAGE_SIZES))
				{
					this_msg_size_idx = 0;
				}
				
				std::vector<unsigned char> buf(MESSAGE_SIZES[this_msg_size_idx]);
				
				assert(buf.size() >= sizeof(struct message_header));
				struct message_header *header = (struct message_header*)(buf.data());
				
				int64_t now = now_us();
				
				header->timestamp_us = now;
				header->msg_size_idx = this_msg_size_idx;
				
				DPN_BUFFER_DESC bd = { buf.size(), (BYTE*)(buf.data()) };
				
				int64_t *now_p = &(send_begin_buf[++send_begin_idx % SEND_BEGIN_SIZE]);
				*now_p = now;
				
				DPNHANDLE s_handle;
				HRESULT res = instance->SendTo(r->dpnidSender, &bd, 1, 0, now_p, &s_handle, DPNSEND_GUARANTEED);
				if(res != DPNSUCCESS_PENDING)
				{
					timed_fprintf(stderr, "IDirectPlay8Peer::SendTo() failed with HRESULT %08x", (unsigned int)(res));
				}
			}
			
			break;
		}
		
		default:
		{
			break;
		}
	}
	
	return S_OK;
}

static void print_stats()
{
	PROCESS_MEMORY_COUNTERS_EX mc;
	mc.cb = sizeof(mc);
	
	GetProcessMemoryInfo(GetCurrentProcess(), (PROCESS_MEMORY_COUNTERS*)(&mc), sizeof(mc));
	
	timed_printf("memory usage = %u KiB", (unsigned)(mc.PrivateUsage / 1024));
	
	/* Take copies of the msg_XXX variables so another thread can't change them under us.
	 * Slight inaccuracies from the variables going out of sync as they are copied is fine, but
	 * we don't want any division by zero errors.
	*/
	
	uint64_t l_msg_num_complete     = msg_num_complete;
	uint64_t l_msg_total_send_us    = msg_total_send_us;
	uint64_t l_msg_total_rtt_us     = msg_total_rtt_us;
	uint64_t l_msg_total_recv_bytes = msg_total_recv_bytes;
	int64_t  l_msg_stats_start      = msg_stats_start;
	
	int stats_period_s = (now_ms() - l_msg_stats_start) / 1000;
	
	if(l_msg_num_complete > 0 && stats_period_s > 0)
	{
		/* Reset the stats so they don't smooth out to be almost static over a long run. */
		
		msg_num_complete     = 0;
		msg_total_send_us    = 0;
		msg_total_rtt_us     = 0;
		msg_total_recv_bytes = 0;
		msg_stats_start      = now_ms();
		
		unsigned send_avg = l_msg_total_send_us / l_msg_num_complete;
		unsigned rtt_avg  = l_msg_total_rtt_us / l_msg_num_complete;
		unsigned bps      = l_msg_total_recv_bytes / stats_period_s;
		
		timed_printf("concurrent = %d, msg/sec = %u, send us = %u, rtt us = %u, KiB/s = %u",
			phase->concurrent_messages, (unsigned)(l_msg_num_complete / stats_period_s), send_avg, rtt_avg, (bps / 1024));
	}
	
	usage_time = now_ms() + STATS_INTERVAL;
}

static void timed_fprintf(FILE *fh, const char *fmt, ...)
{
	static std::mutex lock;
	std::unique_lock<std::mutex> l(lock);
	
	int64_t now = now_ms();
	fprintf(fh, "[T+%06u.%03us] ",
		(unsigned)((now - start_time) / 1000),
		(unsigned)((now - start_time) % 1000));
	
	va_list argv;
	va_start(argv, fmt);
	vfprintf(fh, fmt, argv);
	va_end(argv);
	
	fprintf(fh, "\n");
}

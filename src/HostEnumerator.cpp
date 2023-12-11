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

#include <algorithm>
#include <memory>
#include <stdio.h>
#include <ws2tcpip.h>

#include "COMAPIException.hpp"
#include "DirectPlay8Address.hpp"
#include "HostEnumerator.hpp"
#include "Messages.hpp"
#include "packet.hpp"

// const GUID GUID_NULL = { 0, 0, 0, { 0, 0, 0, 0, 0, 0, 0, 0 } };

HostEnumerator::HostEnumerator(
	std::atomic<unsigned int> * const global_refcount,
	
	PFNDPNMESSAGEHANDLER message_handler,
	PVOID message_handler_ctx,
	
	PDPN_APPLICATION_DESC const pApplicationDesc,
	IDirectPlay8Address *const pdpaddrHost,
	IDirectPlay8Address *const pdpaddrDeviceInfo,
	PVOID const pvUserEnumData,
	const DWORD dwUserEnumDataSize,
	const DWORD dwEnumCount,
	const DWORD dwRetryInterval,
	const DWORD dwTimeOut,
	PVOID const pvUserContext,
	
	std::function<void(HRESULT)> complete_cb):
	
	global_refcount(global_refcount),
	message_handler(message_handler),
	message_handler_ctx(message_handler_ctx),
	complete_cb(complete_cb),
	user_context(pvUserContext),
	next_tx_at(0),
	req_cancel(false)
{
	if(pdpaddrDeviceInfo == NULL)
	{
		throw COMAPIException(DPNERR_INVALIDPARAM);
	}
	
	if(pdpaddrDeviceInfo->GetSP(&service_provider) != S_OK)
	{
		throw COMAPIException(DPNERR_INVALIDDEVICEADDRESS);
	}
	
	memset(&send_addr, 0, sizeof(send_addr));
	send_addr.sin_family      = AF_INET;
	send_addr.sin_addr.s_addr = htonl(INADDR_BROADCAST);
	send_addr.sin_port        = htons(DISCOVERY_PORT);
	
	if(pdpaddrHost != NULL)
	{
		GUID host_sp;
		if(pdpaddrHost->GetSP(&host_sp) != S_OK)
		{
			throw COMAPIException(DPNERR_INVALIDHOSTADDRESS);
		}
		
		if(host_sp != service_provider)
		{
			/* Service Provider in host address must match device address. */
			throw COMAPIException(DPNERR_INVALIDPARAM);
		}
		
		/* Hostname component overrides discovery address, if provided. */
		
		wchar_t hostname_value[128];
		DWORD hostname_size = sizeof(hostname_value);
		DWORD hostname_type;
		
		if(pdpaddrHost->GetComponentByName(DPNA_KEY_HOSTNAME, hostname_value, &hostname_size, &hostname_type) == S_OK)
		{
			if(hostname_type != DPNA_DATATYPE_STRING)
			{
				throw COMAPIException(DPNERR_INVALIDHOSTADDRESS);
			}
			
			if(host_sp == CLSID_DP8SP_TCPIP)
			{
				struct in_addr hostname_addr;
				if(InetPtonW(AF_INET, hostname_value, &hostname_addr) == 1)
				{
					send_addr.sin_addr = hostname_addr;
				}
				else{
					throw COMAPIException(DPNERR_INVALIDHOSTADDRESS);
				}
			}
			else if(host_sp == CLSID_DP8SP_IPX)
			{
				unsigned ip;
				if(swscanf(hostname_value, L"00000000,0000%08X", &ip) != 1)
				{
					throw COMAPIException(DPNERR_INVALIDHOSTADDRESS);
				}
				
				send_addr.sin_addr.s_addr = htonl(ip);
			}
		}
		
		/* Port component overrides discovery port, if provided. */
		
		DWORD port_value;
		DWORD port_size = sizeof(port_value);
		DWORD port_type;
		
		if(pdpaddrHost->GetComponentByName(DPNA_KEY_PORT, &port_value, &port_size, &port_type) == S_OK)
		{
			if(port_type != DPNA_DATATYPE_DWORD || port_value > 65535)
			{
				throw COMAPIException(DPNERR_INVALIDHOSTADDRESS);
			}
			
			send_addr.sin_port = htons((uint16_t)(port_value));
		}
	}
	
	if(pApplicationDesc != NULL)
	{
		application_guid = pApplicationDesc->guidApplication;
	}
	else{
		application_guid = GUID_NULL;
	}
	
	if(pvUserEnumData != NULL && dwUserEnumDataSize > 0)
	{
		user_data.insert(
			user_data.end(),
			(const unsigned char*)(pvUserEnumData),
			(const unsigned char*)(pvUserEnumData) + dwUserEnumDataSize);
	}
	
	tx_remain   = (dwEnumCount     == 0) ? DEFAULT_ENUM_COUNT    : dwEnumCount;
	tx_interval = (dwRetryInterval == 0) ? DEFAULT_ENUM_INTERVAL : dwRetryInterval;
	rx_timeout  = (dwTimeOut       == 0) ? DEFAULT_ENUM_TIMEOUT  : dwTimeOut;
	
	/* TODO: Bind to interface in pdpaddrDeviceInfo, if provided. */
	
	sock = create_udp_socket(0, 0);
	if(sock == -1)
	{
		throw std::runtime_error("Cannot create UDP socket");
	}
	
	wake_thread = CreateEvent(NULL, FALSE, FALSE, NULL);
	if(wake_thread == NULL)
	{
		closesocket(sock);
		throw std::runtime_error("Cannot create wake_thread object");
	}
	
	if(WSAEventSelect(sock, wake_thread, FD_READ))
	{
		CloseHandle(wake_thread);
		closesocket(sock);
		throw std::runtime_error("Cannot WSAEventSelect");
	}
	
	thread = new std::thread(&HostEnumerator::main, this);
}

HostEnumerator::~HostEnumerator()
{
	cancel();
	
	if(thread->joinable())
	{
		if(thread->get_id() == std::this_thread::get_id())
		{
			thread->detach();
		}
		else{
			thread->join();
		}
	}
	
	delete thread;
	
	CloseHandle(wake_thread);
}

void HostEnumerator::main()
{
	while(!req_cancel)
	{
		DWORD now = GetTickCount();
		
		if(tx_remain > 0 && now >= next_tx_at)
		{
			PacketSerialiser ps(DPLITE_MSGID_HOST_ENUM_REQUEST);
			
			if(application_guid != GUID_NULL)
			{
				ps.append_guid(application_guid);
			}
			else{
				ps.append_null();
			}
			
			if(!user_data.empty())
			{
				ps.append_data(user_data.data(), user_data.size());
			}
			else{
				ps.append_null();
			}
			
			ps.append_dword(now);
			
			std::pair<const void*, size_t> raw = ps.raw_packet();
			
			sendto(sock, (const char*)(raw.first), raw.second, 0,
				(struct sockaddr*)(&send_addr), sizeof(send_addr));
			
			next_tx_at = now + tx_interval;
			--tx_remain;
			
			if(rx_timeout != INFINITE)
			{
				stop_at = now + rx_timeout;
			}
		}
		
		struct sockaddr_in from_addr;
		int addrlen = sizeof(from_addr);
		
		int r = recvfrom(sock, (char*)(recv_buf), sizeof(recv_buf), 0, (struct sockaddr*)(&from_addr), &addrlen);
		if(r > 0)
		{
			handle_packet(recv_buf, r, &from_addr);
		}
		
		if(tx_remain == 0 && stop_at > 0 && now >= stop_at)
		{
			/* No more requests to transmit and the wait for replies from the last one
			 * has timed out.
			*/
			break;
		}
		
		DWORD timeout = INFINITE;
		if(tx_remain > 0) { timeout = std::min((next_tx_at - now), timeout); }
		if(stop_at   > 0) { timeout = std::min((stop_at - now),    timeout); }
		
		WaitForSingleObject(wake_thread, timeout);
	}
	
	if(req_cancel)
	{
		complete_cb(DPNERR_USERCANCEL);
	}
	else{
		complete_cb(S_OK);
	}
}

void HostEnumerator::handle_packet(const void *data, size_t size, struct sockaddr_in *from_addr)
{
	std::unique_ptr<PacketDeserialiser> pd;
	
	try {
		pd.reset(new PacketDeserialiser(data, size));
	}
	catch(const PacketDeserialiser::Error &)
	{
		/* Malformed packet received */
		return;
	}
	
	if(pd->packet_type() != DPLITE_MSGID_HOST_ENUM_RESPONSE)
	{
		/* Unexpected packet type. */
		return;
	}
	
	DPN_APPLICATION_DESC app_desc;
	memset(&app_desc, 0, sizeof(app_desc));
	std::wstring app_desc_pwszSessionName;
	
	const void *response_data = NULL;
	size_t response_data_size = 0;
	
	DWORD request_tick_count;
	
	try {
		app_desc.dwSize = sizeof(app_desc);
		
		app_desc.dwFlags          = pd->get_dword(0);
		app_desc.guidInstance     = pd->get_guid(1);
		app_desc.guidApplication  = pd->get_guid(2);
		app_desc.dwMaxPlayers     = pd->get_dword(3);
		app_desc.dwCurrentPlayers = pd->get_dword(4);
		
		app_desc_pwszSessionName = pd->get_wstring(5);
		app_desc.pwszSessionName = (wchar_t*)(app_desc_pwszSessionName.c_str());
		
		if(!pd->is_null(6))
		{
			std::pair<const void*, size_t> app_data = pd->get_data(6);
			app_desc.pvApplicationReservedData     = (void*)(app_data.first);
			app_desc.dwApplicationReservedDataSize = app_data.second;
		}
		
		if(!pd->is_null(7))
		{
			std::pair<const void*, size_t> r_data = pd->get_data(7);
			response_data      = r_data.first;
			response_data_size = r_data.second;
		}
		
		request_tick_count = pd->get_dword(8);
	}
	catch(const PacketDeserialiser::Error &)
	{
		/* Malformed packet received */
		return;
	}
	
	/* Build a DirectPlay8Address with the host/port where the response came from - thats the main
	 * port for the host.
	*/
	
	IDirectPlay8Address *sender_address = DirectPlay8Address::create_host_address(
		global_refcount, service_provider, (struct sockaddr*)(from_addr));
	
	/* Build a DirectPlay8Address with the interface we received the response on.
	 * TODO: Actually do this.
	*/
	
	IDirectPlay8Address *device_address = new DirectPlay8Address(global_refcount);
	device_address->SetSP(&service_provider);
	
	DPNMSG_ENUM_HOSTS_RESPONSE message;
	memset(&message, 0, sizeof(message));
	
	message.dwSize                  = sizeof(message);
	message.pAddressSender          = sender_address;
	message.pAddressDevice          = device_address;
	message.pApplicationDescription = &app_desc;
	message.pvResponseData          = (void*)(response_data);
	message.dwResponseDataSize      = response_data_size;
	message.pvUserContext           = user_context;
	message.dwRoundTripLatencyMS    = GetTickCount() - request_tick_count;
	
	message_handler(message_handler_ctx, DPN_MSGID_ENUM_HOSTS_RESPONSE, &message);
	
	device_address->Release();
	sender_address->Release();
}

void HostEnumerator::cancel()
{
	req_cancel = true;
	SetEvent(wake_thread);
}

void HostEnumerator::wait()
{
	if(thread->joinable())
	{
		thread->join();
	}
}

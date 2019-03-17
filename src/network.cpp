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
#include <iphlpapi.h>
#include <windows.h>
#include <ws2tcpip.h>
#include <stdint.h>
#include <stdio.h>
#include <vector>

#include "network.hpp"
#include "log.hpp"

int create_udp_socket(uint32_t ipaddr, uint16_t port)
{
	int sock = socket(AF_INET, SOCK_DGRAM, 0);
	if(sock == -1)
	{
		return -1;
	}
	
	u_long non_blocking = 1;
	if(ioctlsocket(sock, FIONBIO, &non_blocking) != 0)
	{
		closesocket(sock);
		return -1;
	}
	
	BOOL broadcast = TRUE;
	if(setsockopt(sock, SOL_SOCKET, SO_BROADCAST, (char*)(&broadcast), sizeof(BOOL)) == -1)
	{
		closesocket(sock);
		return -1;
	}
	
	struct sockaddr_in addr;
	addr.sin_family      = AF_INET;
	addr.sin_addr.s_addr = ipaddr;
	addr.sin_port        = htons(port);
	
	if(bind(sock, (struct sockaddr*)(&addr), sizeof(addr)) == -1)
	{
		closesocket(sock);
		return -1;
	}
	
	return sock;
}

int create_listener_socket(uint32_t ipaddr, uint16_t port)
{
	int sock = socket(AF_INET, SOCK_STREAM, 0);
	if(sock == -1)
	{
		return -1;
	}
	
	u_long non_blocking = 1;
	if(ioctlsocket(sock, FIONBIO, &non_blocking) != 0)
	{
		closesocket(sock);
		return -1;
	}
	
	BOOL reuse = TRUE;
	if(setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, (char*)(&reuse), sizeof(BOOL)) == -1)
	{
		closesocket(sock);
		return -1;
	}
	
	struct sockaddr_in addr;
	addr.sin_family      = AF_INET;
	addr.sin_addr.s_addr = ipaddr;
	addr.sin_port        = htons(port);
	
	if(bind(sock, (struct sockaddr*)(&addr), sizeof(addr)) == -1)
	{
		closesocket(sock);
		return -1;
	}
	
	if(listen(sock, LISTEN_QUEUE_SIZE) == -1)
	{
		closesocket(sock);
		return -1;
	}
	
	return sock;
}

int create_client_socket(uint32_t local_ipaddr, uint16_t local_port)
{
	int sock = socket(AF_INET, SOCK_STREAM, 0);
	if(sock == -1)
	{
		return -1;
	}
	
	u_long non_blocking = 1;
	if(ioctlsocket(sock, FIONBIO, &non_blocking) != 0)
	{
		closesocket(sock);
		return -1;
	}
	
	BOOL reuse = TRUE;
	if(setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, (char*)(&reuse), sizeof(BOOL)) == -1)
	{
		closesocket(sock);
		return -1;
	}
	
	/* Set SO_LINGER so that closesocket() does a hard close, immediately removing the socket
	 * address from the connection table.
	 *
	 * If this isn't done, then we are able to immediately bind() new sockets to the same
	 * local address (as we may when the port isn't specified), but then outgoing connections
	 * made from it will fail with WSAEADDRINUSE until the background close completes.
	*/
	
	struct linger no_linger;
	no_linger.l_onoff  = 1;
	no_linger.l_linger = 0;
	
	if(setsockopt(sock, SOL_SOCKET, SO_LINGER, (char*)(&no_linger), sizeof(no_linger)) == -1)
	{
		closesocket(sock);
		return -1;
	}
	
	struct sockaddr_in l_addr;
	l_addr.sin_family      = AF_INET;
	l_addr.sin_addr.s_addr = local_ipaddr;
	l_addr.sin_port        = htons(local_port);
	
	if(bind(sock, (struct sockaddr*)(&l_addr), sizeof(l_addr)) == -1)
	{
		closesocket(sock);
		return -1;
	}
	
	return sock;
}

int create_discovery_socket()
{
	int sock = socket(AF_INET, SOCK_DGRAM, 0);
	if(sock == -1)
	{
		return -1;
	}
	
	u_long non_blocking = 1;
	if(ioctlsocket(sock, FIONBIO, &non_blocking) != 0)
	{
		closesocket(sock);
		return -1;
	}
	
	BOOL reuse = TRUE;
	if(setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, (char*)(&reuse), sizeof(BOOL)) == -1)
	{
		closesocket(sock);
		return -1;
	}
	
	struct sockaddr_in addr;
	addr.sin_family      = AF_INET;
	addr.sin_addr.s_addr = htonl(INADDR_ANY);
	addr.sin_port        = htons(DISCOVERY_PORT);
	
	if(bind(sock, (struct sockaddr*)(&addr), sizeof(addr)) == -1)
	{
		closesocket(sock);
		return -1;
	}
	
	return sock;
}

std::list<SystemNetworkInterface> get_network_interfaces()
{
	std::vector<unsigned char> buf;
	
	while(1)
	{
		ULONG size = buf.size();
		ULONG err = GetAdaptersAddresses(AF_UNSPEC, 0, NULL, (IP_ADAPTER_ADDRESSES*)(buf.data()), &size);
		
		if(err == ERROR_SUCCESS)
		{
			break;
		}
		else if(err == ERROR_NO_DATA)
		{
			return std::list<SystemNetworkInterface>();
		}
		else if(err == ERROR_BUFFER_OVERFLOW)
		{
			buf.resize(size);
		}
		else{
			log_printf("GetAdaptersAddresses: %s", win_strerror(err).c_str());
			return std::list<SystemNetworkInterface>();
		}
	}
	
	std::list<SystemNetworkInterface> interfaces;
	
	for(IP_ADAPTER_ADDRESSES *ipaa = (IP_ADAPTER_ADDRESSES*)(buf.data()); ipaa != NULL; ipaa = ipaa->Next)
	{
		if(ipaa->IfType == IF_TYPE_SOFTWARE_LOOPBACK)
		{
			continue;
		}
		
		SystemNetworkInterface iface;
		
		iface.friendly_name = ipaa->FriendlyName;
		
		for(IP_ADAPTER_UNICAST_ADDRESS_LH *uc = ipaa->FirstUnicastAddress; uc != NULL; uc = uc->Next)
		{
			if(uc->Address.iSockaddrLength > sizeof(struct sockaddr_storage))
			{
				log_printf("Ignoring oversize address (family = %u, size = %u)",
					(unsigned)(uc->Address.lpSockaddr->sa_family),
					(unsigned)(uc->Address.iSockaddrLength));
				continue;
			}
			
			struct sockaddr_storage ss;
			memset(&ss, 0, sizeof(ss));
			memcpy(&ss, uc->Address.lpSockaddr, uc->Address.iSockaddrLength);
			
			iface.unicast_addrs.push_back(ss);
		}
		
		interfaces.push_back(iface);
	}
	
	return interfaces;
}

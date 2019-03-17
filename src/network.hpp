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

#ifndef DPLITE_NETWORK_HPP
#define DPLITE_NETWORK_HPP

#include <winsock2.h>
#include <list>
#include <stdint.h>
#include <string>

#define DISCOVERY_PORT    6073
#define DEFAULT_HOST_PORT 6072
#define LISTEN_QUEUE_SIZE 16
#define MAX_PACKET_SIZE   (256 * 1024)

struct SystemNetworkInterface {
	std::wstring friendly_name;
	
	std::list<struct sockaddr_storage> unicast_addrs;
};

int create_udp_socket(uint32_t ipaddr, uint16_t port);
int create_listener_socket(uint32_t ipaddr, uint16_t port);
int create_client_socket(uint32_t local_ipaddr, uint16_t local_port);
int create_discovery_socket();
std::list<SystemNetworkInterface> get_network_interfaces();

#endif /* !DPLITE_NETWORK_HPP */

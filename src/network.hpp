#ifndef DPLITE_NETWORK_HPP
#define DPLITE_NETWORK_HPP

#include <winsock2.h>
#include <list>
#include <stdint.h>
#include <string>

#define DISCOVERY_PORT    6073
#define DEFAULT_HOST_PORT 6072
#define LISTEN_QUEUE_SIZE 16
#define MAX_PACKET_SIZE   (60 * 1024)

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

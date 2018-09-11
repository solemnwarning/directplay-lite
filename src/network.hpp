#ifndef DPLITE_NETWORK_HPP
#define DPLITE_NETWORK_HPP

#include <stdint.h>

#define DISCOVERY_PORT    6073
#define DEFAULT_HOST_PORT 6072
#define LISTEN_QUEUE_SIZE 16
#define MAX_PACKET_SIZE   (60 * 1024)

int create_udp_socket(uint32_t ipaddr, uint16_t port);
int create_listener_socket(uint32_t ipaddr, uint16_t port);
int create_discovery_socket();

#endif /* !DPLITE_NETWORK_HPP */

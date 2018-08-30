#include <winsock2.h>
#include <windows.h>
#include <stdint.h>

#include "network.hpp"

int create_udp_socket(uint32_t ipaddr, uint16_t port)
{
	int sock = socket(AF_INET, SOCK_DGRAM, 0);
	if(sock == -1)
	{
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
	int sock = socket(AF_INET, SOCK_DGRAM, 0);
	if(sock == -1)
	{
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

int create_discovery_socket()
{
	int sock = socket(AF_INET, SOCK_DGRAM, 0);
	if(sock == -1)
	{
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

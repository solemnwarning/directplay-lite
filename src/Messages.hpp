#ifndef DPLITE_MESSAGES_HPP
#define DPLITE_MESSAGES_HPP

#define DPLITE_MSGID_HOST_ENUM_REQUEST 1

/* EnumHosts() request message.
 *
 * GUID        - Application GUID, NULL to search for any
 * DATA | NULL - User data
 * DWORD       - Current tick count, to be returned, for latency measurement
*/

#define DPLITE_MSGID_HOST_ENUM_RESPONSE 2

/* EnumHosts() response message.
 *
 * DWORD       - DPN_APPLICATION_DESC.dwFlags
 * GUID        - DPN_APPLICATION_DESC.guidInstance
 * GUID        - DPN_APPLICATION_DESC.guidApplication
 * DWORD       - DPN_APPLICATION_DESC.dwMaxPlayers
 * DWORD       - DPN_APPLICATION_DESC.dwCurrentPlayers
 * WSTRING     - DPN_APPLICATION_DESC.pwszSessionName
 * DATA | NULL - DPN_APPLICATION_DESC.pvApplicationReservedData
 *
 * DATA | NULL - DPN_MSGID_ENUM_HOSTS_RESPONSE.pvResponseData
 * DWORD       - Tick count from DPLITE_MSGID_HOST_ENUM_REQUEST
*/

#define DPLITE_MSGID_CONNECT_HOST 3

/* Initial Connect() request to host.
 *
 * GUID | NULL    - Instance GUID
 * GUID           - Application GUID
 * WSTRING | NULL - Password
 * DATA | NULL    - Request data
*/

#define DPLITE_MSGID_CONNECT_HOST_OK 4

/* Successful response to DPLITE_MSGID_CONNECT_HOST from host.
 *
 * GUID        - Instance GUID
 * DWORD       - Player ID of current host
 * DWORD       - Player ID assigned to receiving client
 * DWORD       - Number of other peers (total - 2)
 *
 * For each peer:
 *   DWORD - Player ID
 *   DWORD - IPv4 address (network byte order)
 *   DWORD - Port (host byte order)
 *
 * DATA | NULL - Response data
*/

#define DPLITE_MSGID_CONNECT_HOST_FAIL 5

/* Negative response to DPLITE_MSGID_CONNECT_HOST from host.
 * Host will close the connection after sending this.
 *
 * DWORD       - Error code (DPNERR_HOSTREJECTEDCONNECTION, DPNERR_INVALIDAPPLICATION, etc)
 * DATA | NULL - Response data
*/

#define DPLITE_MSGID_MESSAGE 6

/* Message sent via SendTo() by application.
 *
 * DWORD - Player ID of sender
 * DATA  - Message payload
 * DWORD - Flags (DPNSEND_GUARANTEED, DPNSEND_COALESCE, DPNSEND_COMPLETEONPROCESS)
*/

#endif /* !DPLITE_MESSAGES_HPP */

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

#endif /* !DPLITE_MESSAGES_HPP */

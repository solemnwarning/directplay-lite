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
 * WSTRING - Player name (empty = none)
 * DATA    - Player data (empty = none)
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
 * WSTRING - Host player name (empty = none)
 * DATA    - Host player data (empty = none)
 * DWORD   - DPN_APPLICATION_DESC.dwMaxPlayers
 * WSTRING - DPN_APPLICATION_DESC.pwszSessionName
 * WSTRING - DPN_APPLICATION_DESC.pwszPassword
 * DATA    - DPN_APPLICATION_DESC.pvApplicationReservedData
 * DWORD   - Number of groups the host player is a member of
 *
 * For each group:
 *   DWORD - Group ID
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

#define DPLITE_MSGID_PLAYERINFO 7

/* Player info has been updated by the peer using the SetPeerInfo() method.
 *
 * DWORD   - Player ID, will always be that of the sending peer.
 * WSTRING - Player name (empty = none)
 * DATA    - Player data (empty = none)
 * DWORD   - Operation ID to return in DPLITE_MSGID_OP_COMPLETE
*/

#define DPLITE_MSGID_ACK 8

/* The peer has completed processing a message.
 *
 * DWORD - Ack ID, should be unique to the peer, for a time.
 * DWORD - Result code (normally S_OK)
 * DATA  - Response payload
*/

#define DPLITE_MSGID_APPDESC 9

/* The host has modified the session's application description using SetApplicationDesc()
 *
 * DWORD   - DPN_APPLICATION_DESC.dwMaxPlayers
 * WSTRING - DPN_APPLICATION_DESC.pwszSessionName
 * WSTRING - DPN_APPLICATION_DESC.pwszPassword
 * DATA    - DPN_APPLICATION_DESC.pvApplicationReservedData
*/

#define DPLITE_MSGID_CONNECT_PEER 10

/* Initial connect request to a follow non-host peer.
 *
 * GUID    - Instance GUID
 * GUID    - Application GUID
 * WSTRING - Password
 * DWORD   - Player ID
 * WSTRING - Player name (empty = none)
 * DATA    - Player data (empty = none)
*/

#define DPLITE_MSGID_CONNECT_PEER_OK 11

/* Successful response to DPLITE_MSGID_CONNECT_PEER.
 *
 * WSTRING - Player name (empty = none)
 * DATA    - Player data (empty = none)
 * DWORD   - Number of groups the player is a member of
 *
 * For each group:
 *   DWORD - Group ID
*/

#define DPLITE_MSGID_CONNECT_PEER_FAIL 12

/* Negative response to DPLITE_MSGID_CONNECT_PEER.
 * Peer will close the connection after sending this.
 *
 * DWORD - Error code (DPNERR_HOSTREJECTEDCONNECTION, DPNERR_INVALIDAPPLICATION, etc)
*/

#define DPLITE_MSGID_DESTROY_PEER 13

/* Peer has been destroyed by the host calling IDirectPlay8Peer::DestroyPeer()
 *
 * Sent from host to peer being terminated, to all other peers and from the terminated peer to each
 * peer when it closes the connection.
 *
 * The victim peer must remove itself from the session after receiving this message. All other
 * peers will eject it when they receive notifcation from the server.
 *
 * DWORD - Player ID of peer being destroyed
 * DATA  - DPNMSG_TERMINATE_SESSION.pvTerminateData (only from host to victim)
*/

#define DPLITE_MSGID_TERMINATE_SESSION 14

/* Host is destroying the session. This message is sent from only the host and to all peers in the
 * session simultaneously.
 *
 * DATA - pvTerminateData passed to IDirectPlay8Peer::TerminateSession()
*/

#define DPLITE_MSGID_GROUP_ALLOCATE 15

/* DPLITE_MSGID_GROUP_ALLOCATE
 * A non-host peer is requesting a unique group ID.
 *
 * All player/group IDs are allocated by the host peer, so any other peer must
 * use this message to allocate an ID from the host.
 *
 * The host will respond with a DPLITE_MSGID_ACK with the ID to use encoded as
 * a DPNID in the payload.
 *
 * DWORD - Ack ID
*/

#define DPLITE_MSGID_GROUP_CREATE 16

/* DPLITE_MSGID_GROUP_CREATE
 * A group has been created.
 *
 * This is sent from the peer which called CreateGroup() to all other peers.
 *
 * DWORD   - New group ID
 * WSTRING - Group name (empty = none)
 * DATA    - Group data (empty = none)
*/

#define DPLITE_MSGID_GROUP_DESTROY 17

/* DPLITE_MSGID_GROUP_DESTROY
 * A group has been destroyed.
 *
 * This is sent from the peer which called DestroyGroup() to all other peers.
 * Once this message has been received, the group ID is permenantly unavailable
 * to be used in the session, this is to ensure a group isn't re-created by
 * receiving a message about a group from a different peer that was sent while
 * the group still existed after this was processed.
 *
 * DWORD - Group ID
*/

#define DPLITE_MSGID_GROUP_JOIN 18

/* DPLITE_MSGID_GROUP_JOIN
 * The peer that receives this message is being instructed to join a group.
 *
 * After processing and accepting this message, a peer should send a DPLITE_MSGID_GROUP_JOINED
 * message to all peers. This ensures any AddPlayerToGroup() or RemovePlayerFromGroup() calls for
 * a peer are serialised by that peer, so the session will become consistent if peers start
 * calling them in rapid sucession.
 *
 * Only after sending DPLITE_MSGID_GROUP_JOINED, the peer should respond to the sender of the
 * DPLITE_MSGID_GROUP_JOIN message with a DPLITE_MSGID_ACK.
 *
 * If the group isn't known to the receiver of this message (including being destroyed), that
 * peer should locally instantiate the group. This is to allow for Peer A creating a group, then
 * Peer B is informed of it and adds Peer C to it before the DPLITE_MSGID_GROUP_CREATE message
 * from Peer A arrives at Peer C.
 *
 * DWORD   - Group ID
 * DWORD   - Ack ID
 * WSTRING - Group name (empty = none)
 * DATA    - Group data (empty = none)
*/

#define DPLITE_MSGID_GROUP_JOINED 19

/* DPLITE_MSGID_GROUP_JOINED
 * The peer sending this message has joined a group.
 *
 * If the group isn't known to the receiver of this message (including being destroyed), that
 * peer should locally instantiate the group. This is to allow for Peer A creating a group, then
 * Peer B is informed of it and adds Peer C to it before the DPLITE_MSGID_GROUP_CREATE message
 * from Peer A arrives at Peer C.
 *
 * DWORD   - Group ID
 * WSTRING - Group name (empty = none)
 * DATA    - Group data (empty = none)
*/

#define DPLITE_MSGID_GROUP_LEAVE 20

/* DPLITE_MSGID_GROUP_LEAVE
 * The peer that receives this message is being instructed to leave a group.
 *
 * After processing and accepting this message, a peer should send a DPLITE_MSGID_GROUP_LEFT
 * message to all peers. This ensures any AddPlayerToGroup() or RemovePlayerFromGroup() calls for
 * a peer are serialised by that peer, so the session will become consistent if peers start
 * calling them in rapid sucession.
 *
 * Only after sending DPLITE_MSGID_GROUP_LEFT, the peer should respond to the sender of the
 * DPLITE_MSGID_GROUP_JOIN message with a DPLITE_MSGID_ACK.
 *
 * DWORD - Group ID
 * DWORD - Ack ID
*/

#define DPLITE_MSGID_GROUP_LEFT 21

/* DPLITE_MSGID_GROUP_LEFT
 * The peer sending this message has left a group.
 *
 * DWORD   - Group ID
*/

#endif /* !DPLITE_MESSAGES_HPP */

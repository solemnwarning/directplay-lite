#ifndef DPLITE_ASYNCHANDLEALLOCATOR_HPP
#define DPLITE_ASYNCHANDLEALLOCATOR_HPP

#include <dplay8.h>

/* There is an instance of this class in each DirectPlay8Peer/etc instance to allocate DPNHANDLEs
 * for async operations.
 *
 * Handles are allocated sequentially, they aren't currently tracked, but I doubt anyone will ever
 * have enough running at once to wrap around and conflict.
 *
 * The handle's type is encoded in the high bits so CancelAsyncOperation() can know where it needs
 * to look rather than having to search through each type of async task.
 *
 * 0x00000000 and 0xFFFFFFFF are both impossible values as they have significance to some parts of
 * DirectPlay.
*/

class AsyncHandleAllocator
{
	private:
		DPNHANDLE next_enum_id;
		DPNHANDLE next_connect_id;
		DPNHANDLE next_send_id;
		
	public:
		static const DPNHANDLE TYPE_MASK    = 0xC0000000;
		
		static const DPNHANDLE TYPE_ENUM    = 0x00000000;
		static const DPNHANDLE TYPE_CONNECT = 0x40000000;
		static const DPNHANDLE TYPE_SEND    = 0x80000000;
		
		AsyncHandleAllocator();
		
		DPNHANDLE new_enum();
		DPNHANDLE new_connect();
		DPNHANDLE new_send();
};

#endif /* !DPLITE_ASYNCHANDLEALLOCATOR_HPP */

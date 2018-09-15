#include <winsock2.h>
#include <windows.h>
#include <stdexcept>

#include "EventObject.hpp"

EventObject::EventObject(BOOL bManualReset, BOOL bInitialState)
{
	handle = CreateEvent(NULL, bManualReset, bInitialState, NULL);
	if(handle == NULL)
	{
		throw std::runtime_error("Unable to create event object");
	}
}

EventObject::~EventObject()
{
	CloseHandle(handle);
}

EventObject::operator HANDLE() const
{
	return handle;
}

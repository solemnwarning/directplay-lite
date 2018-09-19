#ifndef DPLITE_EVENTOBJECT_HPP
#define DPLITE_EVENTOBJECT_HPP

#include <winsock2.h>
#include <windows.h>

class EventObject
{
	private:
		/* No copy c'tor. */
		EventObject(const EventObject&) = delete;
		
		HANDLE handle;
	
	public:
		EventObject(BOOL bManualReset = FALSE, BOOL bInitialState = FALSE);
		~EventObject();
		
		operator HANDLE() const;
};

#endif /* !DPLITE_EVENTOBJECT_HPP */

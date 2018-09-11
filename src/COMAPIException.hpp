#ifndef DPLITE_COMAPIEXCEPTION_HPP
#define DPLITE_COMAPIEXCEPTION_HPP

#include <winsock2.h>
#include <exception>
#include <windows.h>

class COMAPIException: public std::exception
{
	private:
		const HRESULT hr;
		char what_s[64];
		
	public:
		COMAPIException(HRESULT result);
		virtual ~COMAPIException();
		
		HRESULT result() const noexcept;
		virtual const char *what() const noexcept;
};

#endif /* !DPLITE_COMAPIEXCEPTION_HPP */

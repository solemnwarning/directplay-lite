#include <stdio.h>

#include "COMAPIException.hpp"

COMAPIException::COMAPIException(HRESULT result):
	hr(result)
{
	snprintf(what_s, sizeof(what_s), "COMAPIException, HRESULT %08X", (unsigned)(result));
}

COMAPIException::~COMAPIException() {}

HRESULT COMAPIException::result() const noexcept
{
	return hr;
}

const char *COMAPIException::what() const noexcept
{
	return what_s;
}

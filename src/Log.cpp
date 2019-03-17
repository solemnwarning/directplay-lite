/* DirectPlay Lite
 * Copyright (C) 2018 Daniel Collins
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

#include <winsock2.h>
#include <mutex>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <windows.h>

#include "Log.hpp"

static std::mutex lock;
static bool initialised = false;
static FILE *log_fh = NULL;
static bool trace_enabled = false;

static void _log_init()
{
	if(initialised)
	{
		return;
	}
	
	const char *log_name = getenv("DPLITE_LOG");
	if(log_name != NULL)
	{
		log_fh = fopen(log_name, "a");
		if(log_fh != NULL)
		{
			setbuf(log_fh, NULL);
		}
	}
	
	const char *t = getenv("DPLITE_TRACE");
	trace_enabled = (t && atoi(t) != 0);
	
	initialised = true;
}

static void _log_fini()
{
	initialised = false;
	
	trace_enabled = false;
	
	if(log_fh != NULL)
	{
		fclose(log_fh);
		log_fh = NULL;
	}
}

void log_init()
{
	std::unique_lock<std::mutex> l(lock);
	_log_init();
}

void log_fini()
{
	std::unique_lock<std::mutex> l(lock);
	_log_fini();
}

bool log_trace_enabled()
{
	std::unique_lock<std::mutex> l(lock);
	_log_init();
	
	return trace_enabled;
}

void log_printf(const char *fmt, ...)
{
	std::unique_lock<std::mutex> l(lock);
	_log_init();
	
	if(log_fh != NULL)
	{
		fprintf(log_fh, "[thread=%u time=%u] ",
			(unsigned)(GetCurrentThreadId()), (unsigned)(GetTickCount()));
		
		va_list argv;
		va_start(argv, fmt);
		vfprintf(log_fh, fmt, argv);
		va_end(argv);
		
		fprintf(log_fh, "\n");
	}
}

/* Convert a windows error number to an error message */
std::string win_strerror(DWORD errnum)
{
	char buf[256];
	memset(buf, '\0', sizeof(buf));
	
	FormatMessage(FORMAT_MESSAGE_FROM_SYSTEM, NULL, errnum, 0, buf, (sizeof(buf) - 1), NULL);
	buf[strcspn(buf, "\r\n")] = '\0';
	
	return buf;
}

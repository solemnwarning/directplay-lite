#ifndef DPLITE_LOG_HPP
#define DPLITE_LOG_HPP

#include <string>

void log_init();
void log_fini();
bool log_trace_enabled();

void log_printf(const char *fmt, ...);

std::string win_strerror(DWORD errnum);

#endif /* !DPLITE_LOG_HPP */

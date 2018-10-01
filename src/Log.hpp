#ifndef DPLITE_LOG_HPP
#define DPLITE_LOG_HPP

void log_init();
void log_fini();
bool log_trace_enabled();

void log_printf(const char *fmt, ...);

#endif /* !DPLITE_LOG_HPP */

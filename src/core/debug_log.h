#pragma once

#include <cstdarg>
#include <cstdio>

#if defined(_DEBUG) || !defined(NDEBUG)
inline void DBG_PRINT(const char* fmt, ...)
{
	char buffer[2048];
	va_list ap;
	va_start(ap, fmt);
	std::vsnprintf(buffer, sizeof(buffer), fmt, ap);
	va_end(ap);
	// Print to console if available
	std::fprintf(stdout, "%s\n", buffer);
	std::fflush(stdout);
	// Also append to file to ensure visibility when running as GUI subsystem
	if (FILE* f = std::fopen("rasta_debug.log", "a")) {
		std::fprintf(f, "%s\n", buffer);
		std::fclose(f);
	}
}
#else
inline void DBG_PRINT(const char*, ...) {}
#endif



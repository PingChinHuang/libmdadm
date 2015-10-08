#include "test_utils.h"

string string_format(const char* fmt, ...)
{
	va_list args;
	char buf[64];
	va_start(args, fmt);
	vsnprintf(buf, 63, fmt, args);
	va_end(args);
	return buf;
}

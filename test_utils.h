#ifndef __TEST_UTILS_H__
#define __TEST_UTILS_H__

#ifndef NUUO
#include <string>

#include <stdio.h>
#include <syslog.h>
#include <stdarg.h>

using namespace std;

#define	WriteHWLog(facility, level, label, fmt, ...) \
	do {\
		printf("[%d][%s][%s][%s] " fmt, __LINE__, #facility, #level, label, ##__VA_ARGS__); \
	} while (0);

extern string string_format(const char* fmt, ...);
#endif

#endif

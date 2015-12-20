#include "test_utils.h"

#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>

#ifndef NUUO
string string_format(const char* fmt, ...)
{
	va_list args;
	char buf[64];
	va_start(args, fmt);
	vsnprintf(buf, 63, fmt, args);
	va_end(args);
	return buf;
}

bool CheckBlockDevice(const string& dev)
{
	struct stat sb;
	if (0 != stat(dev.c_str(), &sb)) {
		fprintf(stderr, "%s: %s, %s\n ", __func__, dev.c_str(), strerror(errno));
		return false;
	}

	if (S_ISBLK(sb.st_mode)) {
		 return true;
	}

	return false;
}

bool CheckDirectoryExist(const string& dev)
{
	struct stat sb;
	if (0 != stat(dev.c_str(), &sb)) {
		fprintf(stderr, "%s: %s, %s\n ", __func__, dev.c_str(), strerror(errno));
		return false;
	}

	if (S_ISDIR(sb.st_mode)) {
		 return true;
	}

	return false;
}

bool CheckFileExist(const string& dev)
{
	struct stat sb;
	if (0 != stat(dev.c_str(), &sb)) {
		fprintf(stderr, "%s: %s, %s\n ", __func__, dev.c_str(), strerror(errno));
		return false;
	}

	if (S_ISREG(sb.st_mode)) {
		 return true;
	}

	return false;
}

bool MakeDirectory(const string& pathname)
{
	int status = mkdir(pathname.c_str(), 0666);
	if (status < 0) {
		fprintf(stderr, "%s: %s, %s\n", __func__, pathname.c_str(), strerror(errno));
		return false;
	}

	return true;
}

off_t GetFileStorageSize(const string& pathname)
{
	struct stat st;
	if (stat(pathname.c_str(), &st) == 0)
		return st.st_size;

	fprintf(stderr, "%s: %s, %s\n", __func__, pathname.c_str(), strerror(errno));
	return -1;
}
#endif

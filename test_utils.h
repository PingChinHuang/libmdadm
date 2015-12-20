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
extern bool CheckBlockDevice(const string& dev);
extern bool CheckDirectoryExist(const string& dev);
extern bool MakeDirectory(const string& pathname);
extern bool CheckFileExist(const string& dev);
extern off_t GetFileStorageSize(const string& pathname);

class CriticalSection {
private:
	int m_iLock;

public:
	CriticalSection()
	: m_iLock(0)
	{}

	~CriticalSection() { m_iLock = 0; }

	void Lock() {
		 m_iLock++;
	}

	void Unlock() {
		 m_iLock--;
	}
};

class CriticalSectionLock {
private:
	CriticalSection *m_cs;
	CriticalSectionLock();
public:
	CriticalSectionLock(CriticalSection *cs)
	: m_cs(cs)
	{
		if (m_cs)
			m_cs->Lock();
	}
	~CriticalSectionLock() {
		if (m_cs)
			m_cs->Unlock();
	}
};
#endif

#endif

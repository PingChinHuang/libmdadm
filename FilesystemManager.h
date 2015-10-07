#ifndef __FILESYSTEM_MANAGER_H__
#define __FILESYSTEM_MANAGER_H__

#ifdef __cplusplus
extern "C"
{
#endif
#include "mke2fs.h" 
//#include "uuid/uuid.h"
//#include "e2p/e2p.h"
#include "blkid/blkid.h"
#ifdef __cplusplus
}
#endif

#include <string>

using namespace std;

#ifdef NUUO
#include "apr/apr_thread_worker.h"
#include "common/critical_section.h"
using namespace SYSUTILS_SPACE;

class FilesystemManager: public  AprThreadWorker
#else
class FilesystemManager 
#endif
{
private:
#ifdef NUUO
	CriticalSection m_csFormat;
	CriticalSection m_csMount;
#endif
	mke2fs_handle m_mkfsHandle;
	//fsck_handle m_fsckHandle;

	string m_strMountPoint;
	string m_strDevNode;
	string m_strUUID;
	string m_strFSType;
	int m_iFormatingState;
	int m_iFormatProgress;
	bool m_bFormat;
	bool m_bMount;

protected:
#ifdef NUUO
	void ThreadProc();
#endif

private:
	FilesystemManager() {}

	void InitializeMke2fsHandle();
	int blkid();

public:
	FilesystemManager(const string& dev);
	virtual ~FilesystemManager();

	bool Initialize();

	static void MakeFilesystemProgress(void *pData, int stat,
					   int current, int total);
	//static void CheckFilesystemProgress();

	bool Format(bool force = true);
	//bool Check();
	//bool Recovery();
	//void Status();

	bool Mount(const string& strMountPoint);
	bool Unmount();

	bool IsFormated();
	bool IsFormating(int& iFormatProgress);
	bool IsMounted(string& strMountPoint);
	void SetFormatInfo(bool format, int progress,
			   int stat);

	void GenerateUUIDFile();
	bool CreateDefaultFolders();

	void Dump();

	static bool dostat(const string& path, struct stat *st,
			   int do_lstat, int quiet);
	static bool IsMountPoint(const string& path);
};

#endif // __FILESYSTEM_MANAGER_H__

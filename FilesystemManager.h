#ifndef __FILESYSTEM_MANAGER_H__
#define __FILESYSTEM_MANAGER_H__

#ifdef __cplusplus
extern "C"
{
#endif
#include "libmke2fs/mke2fs.h" 
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
#include "test_utils.h"

class FilesystemManager 
#endif
{
private:
	CriticalSection m_csFormat;
	CriticalSection m_csMount;
	CriticalSection m_csInitialized;
	mke2fs_handle m_mkfsHandle;
	//fsck_handle m_fsckHandle;

	string m_strMountPoint;
	string m_strDevNode;
	string m_strUUID;
	string m_strFSType;
	int m_iVolumeNum;
	int m_iFormatingState;
	int m_iFormatProgress;
	bool m_bFormat;
	bool m_bMount;
	bool m_bInitialized;

protected:
#ifdef NUUO
	void ThreadProc();
#endif

private:
	void InitializeMke2fsHandle();
	bool Initialize();
	int blkid();

public:
	FilesystemManager();
	FilesystemManager(const string& dev);
	virtual ~FilesystemManager();

	static void MakeFilesystemProgress(void *pData, int stat,
					   int current, int total);
	//static void CheckFilesystemProgress();

	bool Format(bool force = true);
	//bool Check();
	//bool Recovery();
	//void Status();
	
	bool SetDeviceNode(const string &dev);
	void SetMountPoint(const string &mountpoint);
	void SetVolumeNum(const int &num);
	string GetVolumeNum(const int &num);
	string GetMountPoint();
	string GetMountPoint(const int &num);

	bool Mount(const string& strMountPoint);
	bool Mount();
	bool Unmount();

	bool IsFormated();
	bool IsFormating(int& progress, int& stat);
	bool IsMounted(string& strMountPoint);
	bool IsMounted(int& num);
	bool IsInitialied();
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

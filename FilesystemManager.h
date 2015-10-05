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
	CriticalSection m_csFormat;
	mke2fs_handle m_mkfsHandle;
	//fsck_handle m_fsckHandle;

	string m_strMountPoint;
	string m_strDevNode;
	//uuid_t m_uuid;
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

	void GenerateUUIDFile();
	bool CreateDefaultFolders();
	void InitializeMke2fsHandle();

public:
	FilesystemManager(const string& dev);
	virtual ~FilesystemManager();

	bool Initialize();

	void MakeFilesystemProgress(void *pData, int stat,
				    int current, int total);
	//static void CheckFilesystemProgress();

	bool Format();
	//bool Check();
	//bool Recovery();
	//void Status();

	bool Mount();
	bool Unmount();

	bool IsFormated();
	bool IsFormating(int& iFormatProgress);
	bool IsMounted(string& strMountPoint);
};

#endif // __FILESYSTEM_MANAGER_H__

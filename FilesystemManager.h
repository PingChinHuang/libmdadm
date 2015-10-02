#ifndef __FILESYSTEM_MANAGER_H__
#define __FILESYSTEM_MANAGER_H__

#ifdef __cplusplus
extern "C"
{
#endif
#include "mke2fs.h" 
#ifdef __cplusplus
}
#endif

#include "apr/apr_thread_worker.h"

#include <string>

using namespace std;

#ifdef NUUO
#include "apr/apr_thread_worker.h"
//#include "common/critical_section.h"
using namespace SYSUTILS_SPACE;

class FilesystemManager: public  AprThreadWorker
#else
class FilesystemManager 
#endif
{
private:
	mke2fs_handle m_mkfsHandle;
	//fsck_handle m_fsckHandle;

	int m_iFormatProgress;
	bool bForamt;
	bool bMount;

protected:
#ifdef NUUO
	void ThreadProc();
#endif

private:
	FilesystemManager();

	void GenerateUUIDFile();
	bool CreateDefaultFolders();

public:
	FilesystemManager(const string& dev);
	~FilesystemManager();

	static void MakeFilesystemProgress();
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

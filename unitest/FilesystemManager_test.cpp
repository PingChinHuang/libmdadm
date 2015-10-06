#include "FilesystemManager.h"

int main(int argc, char *argv[])
{
	FilesystemManager fs_mgr("/dev/md0");
//	fs_mgr.Initialize();
	fs_mgr.Dump();
	fs_mgr.GenerateUUIDFile();
	fs_mgr.CreateDefaultFolders();
	fs_mgr.Mount("/sources/MyCode/v3.1/build/utility/sysutils/raid_manager/unitest/test");
	fs_mgr.Dump();
	fs_mgr.Unmount();
	fs_mgr.Dump();


	if (FilesystemManager::IsMountPoint("/sources/MyCode/v3.1/build/utility/sysutils/raid_manager/unitest/test"))
		printf("Mount Point\n");
	else
		printf("Not Mount Point\n");

	return 0;
}

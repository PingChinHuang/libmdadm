#include "FilesystemManager.h"

#include "common/system.h"

int main(int argc, char *argv[])
{
	FilesystemManager fs_mgr("/dev/md0");
	fs_mgr.SpecifyMountPoint("/mnt/VOLUME0");
	fs_mgr.CreateThread();

	printf("Progress: ");
	int progress = 0, stat = WRITE_INODE_TABLES_UNKNOWN;
	FILE *pf = fopen("fs_test.log", "w+");
	while (fs_mgr.ThreadExists()) {
		if (fs_mgr.IsFormating(progress, stat)) {
			fprintf(pf, "%3d%%\n", progress);
			fflush(pf);
		}
		SleepMS(100);
//		printf("\b\b\b\b");
	}
	if (!fs_mgr.IsFormating(progress, stat) && stat == WRITE_INODE_TABLES_DONE) {
//		printf("\b\b\b\b");
		fprintf(pf, "Done:%3d%%\n", progress);
	}
	fclose(pf);
//	fs_mgr.Initialize();
//	fs_mgr.Dump();
//	fs_mgr.GenerateUUIDFile();
//	fs_mgr.CreateDefaultFolders();
//	fs_mgr.Mount("/sources/MyCode/v3.1/build/utility/sysutils/raid_manager/unitest/test");
//	fs_mgr.Dump();
//	fs_mgr.Unmount();
//	fs_mgr.Dump();


//	if (FilesystemManager::IsMountPoint("/sources/MyCode/v3.1/build/utility/sysutils/raid_manager/unitest/test"))
//		printf("Mount Point\n");
//	else
//		printf("Not Mount Point\n");

	return 0;
}

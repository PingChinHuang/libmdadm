#ifndef __MKE2FS_H__
#define __MKE2FS_H__

#include "ext2fs/ext2fs.h"

extern int mke2fs();
typedef void (*mke2fs_cb_func)(void *pData, int stat, int current, int total);

struct mke2fs_handle {
	ext2_filsys fs;
	char device_name[32];
	char *buf;
	void *pData;
	mke2fs_cb_func cb_func;
};

#endif // __MKE2FS_H__


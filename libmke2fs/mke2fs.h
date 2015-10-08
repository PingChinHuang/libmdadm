#ifndef __MKE2FS_H__
#define __MKE2FS_H__

#include "ext2fs/ext2fs.h"
#include "mke2fs_err.h"

enum {
	WRITE_INODE_TABLES_UNKNOWN = 0,
	WRITE_INODE_TABLES_INIT,
	WRITE_INODE_TABLES_WRITING,
	WRITE_INODE_TABLES_WRITING_DONE,
	WRITE_INODE_TABLES_DONE,
	WRITE_INODE_TABLES_ERROR,
};

struct extended_opt {
	__u16 stride;
	__u32 stripe_width;
	__u16 desc_size;
	__u16 mmp_update_interval;
	int test_fs;
	int discard;	
};

struct e2fs_cfg {
	double reserved_ratio;
	unsigned long long num_inodes;
	unsigned long flex_bg_size;
	__u32 blocks_per_group;
	__u32 creator_os;
	int blocksize;
	int cluster_size;
	int direct_io;	
	int force;
	int cflag;
	int verbose;
	int quiet;
	int r_opt;
	int super_only;
	int inode_ratio;
	int inode_size;
	int journal_size;
	int noaction;
	char bad_blocks_filename[512];
	char fs_features[256];
	char usage_types[256];
	char mount_dir[64];
	char fs_uuid[64];
	char fs_type[16];
	char volume_label[16];

	struct extended_opt ext_opts;
};

typedef void (*mke2fs_cb_func)(void *pData, int stat, int current, int total);

struct mke2fs_handle {
	//ext2_filsys fs;
	struct e2fs_cfg cfg;
	char device_name[32];
	char *buf;
	void *pData;
	mke2fs_cb_func cb_func;
};

extern int mke2fs(struct mke2fs_handle *handle);

#endif // __MKE2FS_H__


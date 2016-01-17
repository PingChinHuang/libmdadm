#ifndef __E2FSCK_LIB_H__
#define __E2FSCK_LIB_H__

#include "ext2fs/ext2fs.h"
#include "e2fsck_err.h"

//enum {
//};

struct e2fsck_extended_opt {
	int fragcheck;
	int discard;
	int nodiscard;
	int journal_only;
};

struct e2fsck_cfg {
	blk64_t use_superblock;
	int specified_superblock;
	int superblock_size;
	int progress_fd;
	int optimize_dir;
	int timing_statistics; /* 1: OPT_TIME 2: OPT_TIME2 */
	int badblock_check; /* 1: OPT_CHECKBLOCKS 2: OPT_CHECKBLOCKS + OPT_WRITECHECK*/
	int use_ext_journal;
	char ext_journal[256];
	int debug;
	int force;
	int flush;
	int verbose;
	struct e2fsck_extended_opt ext_opts;
};

typedef void (*e2fsck_cb_func)(void *pData, int pass, float percent);

struct e2fsck_handle {
	//ext2_filsys fs;
	struct e2fsck_cfg cfg;
	char device_name[32];
	char *buf;
	void *pData;
	e2fsck_cb_func cb_func;
};

extern int e2fsck(struct e2fsck_handle *handle);

#endif // __E2FSCK_H__


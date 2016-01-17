#include <unistd.h>
#include "e2fsck_lib.h"

static int bDone = 0;
static void callback(void *pData, int pass, float percent)
{
	 printf("pass %d: %%%f\n", pass, percent);
	 if (pass == -1)
		bDone = 1;
}

int main(int argc, char *argv[])
{
	struct e2fsck_handle h;
	strcpy(h.device_name, argv[1]);
	h.buf = NULL;
	h.pData = NULL;
	h.cb_func = callback;
	h.cfg.progress_fd = 0;
	h.cfg.specified_superblock = 0;
	h.cfg.optimize_dir = 1;
	h.cfg.timing_statistics = 2;
	h.cfg.badblock_check = 0;
	h.cfg.use_ext_journal = 0;
	h.cfg.debug = 0;
	h.cfg.force = 1;
	h.cfg.flush = 0;
	h.cfg.verbose = 0;
	h.cfg.ext_opts.fragcheck = 1;
	h.cfg.ext_opts.discard = 0;
	h.cfg.ext_opts.nodiscard = 0;
	h.cfg.ext_opts.journal_only = 0;

	e2fsck(&h);

	while (!bDone) {
		 sleep(3);
	}
	return 0;
}

#include "e2fsck_lib.h"

int main(int argc, char *argv[])
{
	struct e2fsck_handle h;
	strcpy(h.device_name, argv[1]);
	h.buf = NULL;
	h.pData = NULL;
	h.cb_func = NULL;
	h.cfg.progress_fd = 0;
	h.cfg.specified_superblock = 0;
	h.cfg.optimize_dir = 1;
	h.cfg.timing_statistics = 2;
	h.cfg.badblock_check = 0;
	h.cfg.use_ext_journal = 0;
	h.cfg.debug = 1;
	h.cfg.force = 1;
	h.cfg.flush = 0;
	h.cfg.verbose = 1;
	h.cfg.ext_opts.fragcheck = 1;
	h.cfg.ext_opts.discard = 0;
	h.cfg.ext_opts.nodiscard = 0;
	h.cfg.ext_opts.journal_only = 0;

	e2fsck(&h);
	return 0;
}

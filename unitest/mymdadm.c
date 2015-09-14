#include "mdadm.h"

int main(int argc, char *argv[])
{
	struct shape s;
	struct context c;
	struct mddev_dev* devlist = NULL;
	struct mddev_dev** devlistend = &devlist;
	struct mddev_dev* dv = NULL;
	char sdb[16];
	char sda[16];
	int ret = SUCCESS;

	strcpy(sdb, "/dev/sda");
	strcpy(sda, "/dev/sdx");

	dv = malloc(sizeof(struct mddev_dev));
	if (dv == NULL) {
		printf("Fail to allocate memory.\n");
		exit(1);
	}

	dv->devname = sdb;
	dv->disposition = 0;
	dv->writemostly = 0;
	dv->used = 0;
	dv->next = NULL;
	*devlistend = dv;
	devlistend = &dv->next;

#if 0
	dv = malloc(sizeof(struct mddev_dev));
	if (dv == NULL) {
		printf("Fail to allocate memory.\n");
		exit(1);
	}

	dv->devname = sda;
	dv->disposition = 0;
	dv->writemostly = 0;
	dv->used = 0;
	dv->next = NULL;
	*devlistend = dv;
	devlistend = &dv->next;
#endif

	memset(&s, 0x00, sizeof(struct shape));
	memset(&c, 0x00, sizeof(struct context));

	s.raiddisks = 1;
	s.level = 0;
	s.layout = UnSet;
	s.bitmap_chunk = UnSet;
	s.assume_clean = 0;

	c.force = 1;
	c.delay = DEFAULT_BITMAP_DELAY;
	c.runstop = 1;
	c.verbose = 0;
	c.brief = 1;

	//ret = Create(NULL, "/dev/md1", "\0", NULL, s.raiddisks, devlist, &s, &c, INVALID_SECTORS);
	//printf("ret = %d\n", ret);

	c.brief = 0;
	Detail("/dev/md1", &c);


	for (dv = devlist; dv; dv = dv->next) {
		printf("%s\n", dv->devname);
		free(dv);
	}


	return 0;
}

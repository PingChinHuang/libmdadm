#include "mdadm.h"

int main(int argc, char *argv[])
{
	struct shape s;
	struct context c;
	struct mddev_dev* devlist = NULL;
	struct mddev_dev** devlistend = &devlist;
	struct mddev_dev* dv = NULL;
	//char sdb[16];
	//char sda[16];
	int ret = SUCCESS, i = 0;
	int level = 0;
	struct array_detail ad;
	time_t atime;

	//strcpy(sdb, "/dev/sda");
	//strcpy(sda, "/dev/sdx");

	for (i = 2; i < argc; i ++) {
		dv = malloc(sizeof(struct mddev_dev));
		if (dv == NULL) {
			printf("Fail to allocate memory.\n");
			exit(1);
		}
		dv->devname = argv[i];
		dv->disposition = 0;
		dv->writemostly = 0;
		dv->used = 0;
		dv->next = NULL;
		*devlistend = dv;
		devlistend = &dv->next;
	}

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

	sscanf(argv[1], "%d", &level);

	s.raiddisks = 4;
	s.level = level;
	s.layout = UnSet;
	s.bitmap_chunk = UnSet;
	s.assume_clean = 0;

	c.force = 1;
	c.delay = DEFAULT_BITMAP_DELAY;
	c.runstop = 1;
	c.verbose = 1;
	c.brief = 1;

	ret = Create(NULL, "/dev/md1", "\0", NULL, s.raiddisks, devlist, &s, &c, INVALID_SECTORS);
	printf("ret = %d\n", ret);

	c.brief = 0;
	//Detail("/dev/md1", &c);


	for (dv = devlist; dv; dv = dv->next) {
		printf("%s\n", dv->devname);
		free(dv);
	}

#if 0
	ret = Detail_ToArrayDetail("/dev/md1", &c, &ad);
	printf("done\n");
	printf("RAID State: %s\n", ad.strArrayState);
	printf("RAID Device Name: %s\n", ad.strArrayDevName);
	printf("RAID Level: %s\n", ad.strRaidLevel);
	printf("RAID Layout: %s\n", ad.strRaidLayout);
	printf("RAID Size: %s\n", ad.strArraySize);
	printf("RAID UsedSize: %s\n", ad.strUsedSize);
	atime = ad.arrayInfo.ctime;
	printf("RAID Creation Time: %.24s\n", ctime(&atime));
	atime = ad.arrayInfo.utime;
	printf("RAID Update Time: %.24s\n", ctime(&atime));

	for (i = 0; i < ad.arrayInfo.raid_disks; i++) {
		printf("Disk Device Name: %s\n", ad.arrayDisks[i].strDevName);
		printf("Disk State: %s\n", ad.arrayDisks[i].strState);
		printf("Disk Number: %d\n", ad.arrayDisks[i].diskInfo.number);
		printf("Disk Major: %d Minor: %d\n", ad.arrayDisks[i].diskInfo.major, ad.arrayDisks[i].diskInfo.minor);
	}
#endif
#if 0
	int fd = open_mddev("/dev/md0", 1);
	ret = Manage_stop("/dev/md0", fd, 1, 0); 
	printf("%d\n", ret);
	close(fd);
#endif
	return 0;
}

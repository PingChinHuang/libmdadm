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

#if 0
	ret = Create(NULL, "/dev/md1", "\0", NULL, s.raiddisks, devlist, &s, &c, INVALID_SECTORS);
	printf("ret = %d\n", ret);
#endif

#if 0

	c.brief = 0;
	//Detail("/dev/md1", &c);
#endif

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
	struct mddev_ident ident;
#if 0
	ident.uuid_set = 1;
	//ident.uuid[0] = -2110487005;
	//ident.uuid[1] = -466931330;
	//ident.uuid[2] =  138953740;
	//ident.uuid[3] =  -785669806;
	char uuid[128];
	snprintf(uuid, 127,"5f3e55da4f7e42f9759f8e52902172a8");
	uuid[32] = '\0';
	parse_uuid(uuid, ident.uuid);
#endif
#if 0
	ident.super_minor = UnSet;
	ident.level = UnSet;
	ident.raid_disks = UnSet;
	ident.spare_disks = UnSet;
	ident.bitmap_fd = -1;
	ret = Assemble(NULL, "/dev/md1", &ident, devlist, &c); 
#endif
#if 0
	int fd = open_mddev("/dev/md1", 1);
	if (fd == -2)
		return 0;
	
	for (dv = devlist; dv; dv = dv->next)
		dv->disposition = 'a';	
	ret = Manage_subdevs("/dev/md1", fd, devlist, c.verbose, 0, 0, c.force);
	close(fd);
#endif
#if 0
	ret = Kill("/dev/sdb", NULL, c.force, c.verbose, 0);
#endif
#if 0
	struct query_result result;
	Query_ToQueryResult("/dev/md1", &result);
	if (result.bIsMD) {
		if (result.bHasMDError)
			printf("%s\n", result.strMDError);
		else if (result.bIsMDActive && result.bHasMDDetail)
			printf("%s: Raid Disks %d, Spare Disks %d, Raid Level %d(%s), Size %s\n", result.strMDDevName, result.iMDRaidDiskNum, result.iMDSpareDiskNum, result.iMDRaidLevel, result.strMDLevel, result.strMDSize);
	} else
		printf("%s: Disk Number %d, Activity %s, MD %s, Raid Disks %d, Raid Level %d(%s)\n", result.strDiskDevName, result.iDiskNumber, result.strDiskActivity, result.strMDDevName, result.iMDRaidDiskNum, result.iMDRaidLevel, result.strMDLevel);
#endif


	struct examine_result result;
	ret = Examine_ToResult(devlist, &c, NULL, &result);
//	for (result = list; result; result = result->next) {
	if (result.bIsValid) {
		printf("State %c, DevName %s, raid lv: %u, raid disk num: %u\n",
			result.cState, result.strDevName, result.uRaidLevel, result.uRaidDiskNum);
		for (i = 0; i < 16; i++) {
			printf("%02X ", result.arrayUUID[i]);

		}
		printf("\n");
	}
//	}

	printf("ret = %d\n", ret);

	for (dv = devlist; dv; dv = dv->next) {
		printf("%s\n", dv->devname);
		free(dv);
	}
	return 0;
}

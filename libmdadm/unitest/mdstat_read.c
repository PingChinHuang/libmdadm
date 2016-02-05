#include "mdadm.h"

int main(int argc, char *argv[])
{
	struct mdstat_ent *ms = mdstat_read(0, 0);
	struct mdstat_ent *e = ms;
	for(; e; e = e->next) {
		struct dev_member *dev = e->members;
		printf("dev %s, devname %s, level %s, pattern %s, raid_disks %d, devcnt %d \n", e->dev, e->devnm, e->level, e->pattern, e->raid_disks, e->devcnt);
		printf("dev:\n");
		for (; dev; dev = dev->next) {
			printf("%s ", dev->name);
		}
		printf("\n");
	}

	free_mdstat(ms);

	e = mdstat_by_component(argv[1]);
	if (e) {
		printf("%s belongs to %s\n", argv[1], e->dev);
	} else
		printf("%s belongs to nothing\n", argv[1]);

	free_mdstat(e);


	return 0;
}

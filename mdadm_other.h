#ifndef __MDADM_OTHER_H__
#define __MDAMM_OTHER_H__

#define MAX_DISK_NUM 64

struct array_disk_info {
	mdu_disk_info_t diskInfo;
	char strState[256];
	char strDevName[64];
};

struct array_detail {
	mdu_array_info_t arrayInfo;
	struct array_disk_info arrayDisks[MAX_DISK_NUM];
	
	char strArrayState[128];
	char strArrayDevName[32];
	char strRaidLevel[16];
	char strRaidLayout[32];
	char strArraySize[32];
	char strUsedSize[32];
	char strRebuildOperation[16];
	unsigned long long ullArraySize;
	unsigned long long ullUsedSize;
	int uuid[4];
	int bIsSuperBlockPersistent;
	int bInactive;
	int bIsRebuilding;
	int iRebuildProgress; /* -1 if no resync */

	char strContainer[32];
	char strMember[32];

	/* Reshape Info */
	int bReshapeActive;
	int iDeltaDisks;
	int iRaidNewLevel;
	int iNewChunkSize; /* Byte */
	unsigned long long ullReshapeProgress;
	char strRaidNewLayout[32];
};

#endif

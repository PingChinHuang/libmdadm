#ifndef __MDADM_OTHER_H__
#define __MDAMM_OTHER_H__

#define MAX_DISK_NUM 64

/* Just defined for future use */
enum {
	RAID_STATUS_BIT_FUNCTIONAL = 0,
	RAID_STATUS_BIT_DEGRADE,
	RAID_STATUS_BIT_FAILED,
	RAID_STATUS_BIT_RECOVERING,
	RAID_STATUS_BIT_UNMOUNTED,
	RAID_STATUS_BIT_UNFORMATED,
	RAID_STATUS_BIT_FILESYS_ERR,
};

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

struct query_result {
	int bIsMD;
	int bIsMDActive;
	int bHasMDDetail;
	int bHasMDError;
	int iMDRaidDiskNum;	// Available for MD, Disk
	int iMDSpareDiskNum;	
	int iMDRaidLevel;	// Available for MD, Disk
	char strMDSize[32];
	char strMDLevel[16];	// Available for MD, Disk
	char strMDDevName[32];	// Available for MD, Disk
	char strMDError[128];

	int iDiskNumber;
	char strDiskActivity[32];
	char strDiskDevName[32];
};

struct examine_result {
	char cState; /* Active: 'A', Spare: 'S', Replacement: 'R' */
	char strDevName[32];
	int arrayUUID[4];
	unsigned uRaidLevel;
	unsigned uRaidDiskNum;
	unsigned bHasBadblock;
	unsigned bSBChkSumError;
	unsigned bIsValid;
	unsigned uChkSum;
	unsigned uExpectedChkSum;
	unsigned uDevRole;
	struct examine_result* next;
};

#endif

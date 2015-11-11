#ifndef __RAIDMANAGER_H__
#define __RAIDMANAGER_H__

#ifdef __cplusplus
extern "C" {
#endif
#include "mdadm.h"
#ifdef __cplusplus
}
#endif

#include "FilesystemManager.h"

#ifdef NUUO
#include "common/critical_section.h"
#include "common/nusyslog.h"
#include "common/smart_pointer.h"
using namespace SYSUTILS_SPACE;
#else
#include "test_utils.h"
#endif

#include <string>
#include <vector>
#include <iterator>
#include <memory>

#include <stdint.h>
#include <sys/stat.h>
#include <unistd.h>

using namespace std;

enum eDiskType {
	DISK_TYPE_UNKNOWN,
	DISK_TYPE_LOCAL,
	DISK_TYPE_ISCSI,
	DISK_TYPE_NFS
};

struct RAIDDiskInfo {
	string		m_strState;
	string		m_strDevName;
	string		m_strSoftLinkName;
	int32_t		m_RaidUUID[4]; // Get after Examine()
	int32_t		m_iState;
	int32_t		m_iNumber;
	int32_t		m_iRaidDiskNum;
	eDiskType	m_diskType;
	bool		m_bHasMDSB;

	RAIDDiskInfo()
	: m_strState("")
	, m_strDevName("")
	, m_strSoftLinkName("")
	, m_iState(0)
	, m_iNumber(0)
	, m_iRaidDiskNum(0)
	, m_diskType(DISK_TYPE_UNKNOWN)
	, m_bHasMDSB(false)
	{
		for (int i = 0; i < 4; i++) {
			m_RaidUUID[i] = 0;
		}
	}

	~RAIDDiskInfo() {}

	RAIDDiskInfo& operator=(const struct array_disk_info& rhs)
	{
		m_strState = rhs.strState;
		m_strDevName = rhs.strDevName;
		m_iState = rhs.diskInfo.state;
		m_iNumber = rhs.diskInfo.number;
		m_iRaidDiskNum = rhs.diskInfo.raid_disk;
		return *this;
	}

	void Dump()
	{
		printf("Link: %s, Device: %s, State: %s, Order: %d",
			m_strSoftLinkName.c_str(), m_strDevName.c_str(),
			m_strState.c_str(), m_iNumber);
		switch (m_diskType) {
		case DISK_TYPE_UNKNOWN:
			printf(", Type: Unknown\n");
			break;
		case DISK_TYPE_LOCAL:
			printf(", Type: Local\n");
			break;
		case DISK_TYPE_ISCSI:
			printf(", Type: iSCSI\n");
			break;
		case DISK_TYPE_NFS:
			printf(", Type: NFS\n");
			break;
		}
	}

	void HandleDevName(const string& name)
	{
		struct stat s;
		if (lstat(name.c_str(), &s) >= 0) {
			if (S_ISLNK(s.st_mode) == 1) {
				char buf[128];
				int len = 0;
				if ((len = readlink(name.c_str(), buf, sizeof(buf) - 1)) >= 0) {
					buf[len] = '\0';
					m_strSoftLinkName = name;
					m_strDevName = "/dev/";
					m_strDevName += buf;
					return;
				}
			}
		}
		m_strDevName = name;
		m_strSoftLinkName = name;
	}

	RAIDDiskInfo& operator=(const RAIDDiskInfo& rhs)
	{
		if (this == &rhs)
			return *this;
		m_strSoftLinkName = rhs.m_strSoftLinkName;
		m_strState = rhs.m_strState;
		m_strDevName = rhs.m_strDevName;
		m_iState = rhs.m_iState;
		m_iNumber = rhs.m_iNumber;
		m_iRaidDiskNum = rhs.m_iRaidDiskNum;
		m_bHasMDSB = rhs.m_bHasMDSB;
		m_diskType = rhs.m_diskType;
		return *this;
	}

	bool operator==(const RAIDDiskInfo& rhs) const
	{
		return (m_strDevName == rhs.m_strDevName);
	}

	bool operator==(const string& rhs) const
	{
		return (rhs == m_strDevName || rhs == m_strSoftLinkName);
	}
};

struct RAIDInfo {
	smart_ptr<FilesystemManager> m_fsMgr;
	vector<RAIDDiskInfo>	m_vDiskList;
	string			m_strDevNodeName;
	string			m_strState;
	string			m_strLayout;
	string			m_strRebuildingOperation;
	time_t			m_CreationTime;
	time_t			m_UpdateTime;
	uint32_t		m_UUID[4];
	int64_t			m_ullTotalCapacity;
	int64_t			m_ullUsedSize;
	int32_t			m_iRAIDLevel;
	int32_t			m_iTotalDiskNum;
	int32_t			m_iRAIDDiskNum;
	int32_t			m_iActiveDiskNum;
	int32_t			m_iWorkingDiskNum;
	int32_t			m_iFailedDiskNum;
	int32_t			m_iSpareDiskNum;
	int32_t			m_iState;
	int32_t			m_iChunkSize;
	int32_t			m_iRebuildingProgress;
	int32_t			m_iMDNum;
	bool			m_bSuperBlockPersistent;
	bool			m_bInactive;
	bool			m_bRebuilding;
	
	RAIDInfo()
	: m_fsMgr(NULL)
	, m_strDevNodeName("")
	, m_strState("")
	, m_strLayout("")
	, m_strRebuildingOperation("")
	, m_CreationTime(0)
	, m_UpdateTime(0)
	, m_ullTotalCapacity(0ull)
	, m_ullUsedSize(0ull)
	, m_iRAIDLevel(UnSet)
	, m_iTotalDiskNum(0)
	, m_iRAIDDiskNum(0)
	, m_iActiveDiskNum(0)
	, m_iWorkingDiskNum(0)
	, m_iFailedDiskNum(0)
	, m_iSpareDiskNum(0)
	, m_iState(0)
	, m_iChunkSize(512)
	, m_iRebuildingProgress(0)
	, m_iMDNum(-1)
	, m_bSuperBlockPersistent(false)
	, m_bInactive(false)
	, m_bRebuilding(false)
	{
		for (int i = 0; i < 4; i ++)
			m_UUID[i] = 0;
	}

	~RAIDInfo() {
		m_fsMgr = NULL;
	}

	bool InitializeFSManager() {
		try {
			m_fsMgr = new FilesystemManager;
			if (!m_fsMgr->SetDeviceNode(m_strDevNodeName)) {
				WriteHWLog(LOG_LOCAL0, LOG_ERR, "RAIDInfo",
					   "Iniitialize FilesystemManager failed.");
				return false;
			}
		} catch (bad_alloc&) {
			m_fsMgr = NULL;
			WriteHWLog(LOG_LOCAL0, LOG_ERR, "RAIDInfo",
				   "Allocate memory failed.");
			return false;
		}

		return true;
	}	

	RAIDInfo& operator=(const struct array_detail& rhs)
	{
		m_vDiskList.clear();
		for (int i = 0; i < rhs.arrayInfo.nr_disks; i++) {
			RAIDDiskInfo info;
			if (rhs.arrayDisks[i].diskInfo.major == 0 &&
			    rhs.arrayDisks[i].diskInfo.minor == 0)
				continue;
			
			info = rhs.arrayDisks[i];
			m_vDiskList.push_back(info);
		}
		
		m_strState = rhs.strArrayState;
		m_strLayout = rhs.strRaidLayout;
		m_strRebuildingOperation = rhs.strRebuildOperation;
		m_strDevNodeName = rhs.strArrayDevName;
		memcpy(m_UUID, rhs.uuid, sizeof(m_UUID));
		m_ullTotalCapacity = rhs.ullArraySize;
		m_ullUsedSize = rhs.ullUsedSize;
		m_iRAIDLevel = rhs.arrayInfo.level;
		m_iTotalDiskNum = rhs.arrayInfo.nr_disks;
		m_iRAIDDiskNum = rhs.arrayInfo.raid_disks;
		m_iActiveDiskNum = rhs.arrayInfo.active_disks;
		m_iWorkingDiskNum = rhs.arrayInfo.working_disks;
		m_iFailedDiskNum = rhs.arrayInfo.failed_disks;
		m_iSpareDiskNum = rhs.arrayInfo.spare_disks;
		m_iState = rhs.arrayInfo.state;
		m_iChunkSize = rhs.arrayInfo.chunk_size;
		m_iRebuildingProgress = rhs.iRebuildProgress;
		m_bRebuilding = rhs.bIsRebuilding == 1 ? true : false;
		m_bInactive = rhs.bInactive == 1 ? true : false;
		m_bSuperBlockPersistent = rhs.bIsSuperBlockPersistent == 1 ? true : false;
		m_CreationTime = rhs.arrayInfo.ctime;
		m_UpdateTime = rhs.arrayInfo.utime;

		return *this;
	}

	RAIDInfo& operator=(const RAIDInfo& rhs)
	{
		if (this == &rhs)
			return *this;

		m_vDiskList.clear();
		for (size_t i = 0; i < rhs.m_vDiskList.size(); i++) {
			m_vDiskList.push_back(rhs.m_vDiskList[i]);
		}
		
		m_strState = rhs.m_strState;
		m_strLayout = rhs.m_strLayout;
		m_strRebuildingOperation = rhs.m_strRebuildingOperation;
		m_strDevNodeName = rhs.m_strDevNodeName;
		memcpy(m_UUID, rhs.m_UUID, sizeof(m_UUID));
		m_ullTotalCapacity = rhs.m_ullTotalCapacity;
		m_ullUsedSize = rhs.m_ullUsedSize;
		m_iRAIDLevel = rhs.m_iRAIDLevel;
		m_iTotalDiskNum = rhs.m_iTotalDiskNum;
		m_iRAIDDiskNum = rhs.m_iRAIDDiskNum;
		m_iActiveDiskNum = rhs.m_iActiveDiskNum;
		m_iWorkingDiskNum = rhs.m_iWorkingDiskNum;
		m_iFailedDiskNum = rhs.m_iFailedDiskNum;
		m_iSpareDiskNum = rhs.m_iSpareDiskNum;
		m_iState = rhs.m_iState;
		m_iChunkSize = rhs.m_iChunkSize;
		m_iRebuildingProgress = rhs.m_iRebuildingProgress;
		m_bRebuilding = rhs.m_bRebuilding;
		m_bInactive = rhs.m_bInactive;
		m_bSuperBlockPersistent = rhs.m_bSuperBlockPersistent;
		m_CreationTime = rhs.m_CreationTime;
		m_UpdateTime = rhs.m_UpdateTime;
		m_fsMgr = rhs.m_fsMgr;
		m_iMDNum = rhs.m_iMDNum;

		return *this;
	}

	bool operator==(const RAIDInfo& rhs) const
	{
		return (m_strDevNodeName == rhs.m_strDevNodeName &&
			0 == memcmp(rhs.m_UUID, m_UUID, sizeof(m_UUID)));
	}

	bool operator==(const string& rhs) const
	{
		return (m_strDevNodeName == rhs);
	}

	bool IsRAIDStatusChanged(const RAIDInfo& previous) {
		return !(m_UpdateTime == previous.m_UpdateTime &&
			 m_iFailedDiskNum == previous.m_iFailedDiskNum &&
			 m_iWorkingDiskNum == previous.m_iWorkingDiskNum &&
			 m_iSpareDiskNum == previous.m_iSpareDiskNum);
	}

	void Dump()
	{
		printf("Device: %s\n"
			"\tState: %s, Level: %d, Chunk Size: %d\n"
			"\tTotal Capacity: %llu\n\tUsed Capacity: %llu\n"
			"\tTotal Disks: %d (R: %d, A: %d, W: %d, F: %d, S: %d)\n"
			"\tCreatetion Time: %.24s\n\tUpdate Time: %.24s\n"
			"\tRebuilding: %s (%d%%)\n\n",
			m_strDevNodeName.c_str(), m_strState.c_str(),
			m_iRAIDLevel, m_iChunkSize,
			m_ullTotalCapacity, m_ullUsedSize,
			m_iTotalDiskNum, m_iRAIDDiskNum, m_iActiveDiskNum,
			m_iWorkingDiskNum, m_iFailedDiskNum, m_iSpareDiskNum,
			ctime(&m_CreationTime), ctime(&m_UpdateTime),
			m_bRebuilding?"Yes":"No", (m_iRebuildingProgress < 0)?100:m_iRebuildingProgress
			);
		
		if (m_fsMgr.get() != NULL)
			m_fsMgr->Dump();
		printf("\n");

		for (size_t i =0 ; i < m_vDiskList.size(); i++) {
			printf("\t");
			m_vDiskList[i].Dump();
		}
		printf("\n");
	}
};

class RAIDManager {
private:
	vector<RAIDInfo> m_vRAIDInfoList;
	vector<RAIDDiskInfo> m_vRAIDDiskList;
	
#ifdef NUUO
	CriticalSection m_csRAIDInfoList;
	CriticalSection m_csRAIDDiskList;
	CriticalSection m_csUsedMD;
	CriticalSection m_csUsedVolume;
#endif

	bool m_bUsedMD[128];
	bool m_bUsedVolume[128];

private:
	vector<RAIDInfo>::iterator SearchDiskBelong2RAID(RAIDDiskInfo& devInfo);

	void InitializeShape(struct shape& s, int raiddisks, int level, int chunk = 512, int bitmap_chunk = UnSet, char* bitmap_file = NULL);
#ifdef DEBUG
	void InitializeContext(struct context& c, int force = 1, int runstop = 1, int verbose = 1);
#else
	void InitializeContext(struct context& c, int force = 1, int runstop = 1, int verbose = 0);
#endif
	void InitializeMDDevIdent(struct mddev_ident& ident, int uuid_set, const int uuid[4], int bitmap_fd = -1, char* bitmap_file = NULL);
	struct mddev_dev* InitializeDevList(vector<string>& devNameList, int disposition = 0);
	struct mddev_dev* InitializeDevList(const string& replace, const string& with);
	void FreeDevList(struct mddev_dev* devlist);
	int OpenMDDev(const string& mddev);

	int CreateRAID(const int& mdnum, string& mddev, vector<string>& vDevList, int level);
	int CreateRAID(const string& mddev, vector<string>& vDevList, int level);
	int AssembleRAID(const int& mdnum, string& mddev, const int uuid[4]);
	int AssembleRAID(const string& mddev, const int uuid[4]);
	int AssembleRAID(const int& mdnum, string& mddev, vector<string>& vDevList);
	int AssembleRAID(const string& mddev, vector<string>& vDevList);
	int GetFreeMDNum();
	void FreeMDNum(int n);
	void SetMDNum(int n);
	int GetFreeVolumeNum();
	void FreeVolumeNum(int n);
	void SetVolumeNum(int n);

	void UpdateRAIDDiskList(vector<RAIDDiskInfo>& vRAIDDiskInfoList);
	bool ManageRAIDSubdevs(const string& mddev, vector<string>& vDevList, int operation);
	vector<RAIDInfo>::iterator IsMDDevInRAIDInfoList(const string &mddev);
	vector<RAIDInfo>::iterator IsMDDevInRAIDInfoList(const string &mddev, RAIDInfo& info);
	bool IsDiskExistInRAIDDiskList(const string& dev);
	bool IsDiskExistInRAIDDiskList(vector<string>& vDevList);
	string GenerateMDDevName(int num);
	int GenerateVolumeName(string& name);

public:
	RAIDManager();
	~RAIDManager();

	bool AddDisk(const string& dev, const eDiskType &type);
	bool RemoveDisk(const string& dev);

	bool CreateRAID(vector<string>& vDevList, int level, string& strMDName);
	bool AssembleRAID(const int uuid[4], string& strMDName);
	bool AssembleRAID(vector<string>& vDevList, string& strMDName);
	bool RemoveMDDisks(const string& mddev, vector<string>& vDevList);
	bool MarkFaultyMDDisks(const string& mddev, vector<string>& vDevList);
	bool AddMDDisks(const string& mddev, vector<string>& vDevList);
	bool ReaddMDDisks(const string& mddev, vector<string>& vDevList);
	bool ReplaceMDDisk(const string& mddev, const string& replace, const string& with);
	bool DeleteRAID(const string& mddev);
	bool StopRAID(const string& mddev);

	bool GetRAIDInfo(const string& mddev, RAIDInfo& info);
	void GetRAIDInfo(vector<RAIDInfo>& list);
	void GetDisksInfo(vector<RAIDDiskInfo> &list);
	bool GetDisksInfo(const string& dev, RAIDDiskInfo &info);

	bool UpdateRAIDInfo(); // May need for periodically update.
	bool UpdateRAIDInfo(const string& mddev, int mdnum = -1);
	bool UpdateRAIDInfo(const int uuid[4]);

	bool CheckFileSystem();
	bool DoFileSystemRecovery();
	bool GetFileSystemStatus();

	bool Format(const string& mddev);
	bool Mount(const string& mddev);
	bool Unmount(const string& mddev);
	bool GetFormatProgress(const string& mddev,
			       int& stat, int& progress);
	bool IsMounted(const string& mddev, int &num);
	bool IsFormated(const string& mddev);

	void Dump();
}; 


#endif

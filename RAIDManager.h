#ifndef __RAIDMANAGER_H__
#define __RAIDMANAGER_H__

#ifdef __cplusplus
extern "C" {
#endif
#include "mdadm.h"
#ifdef __cplusplus
}
#endif

#ifdef NUUO
#include "common/critical_section.h"
using namespace SYSUTILS_SPACE;
#endif

#include <string>
#include <vector>
#include <iterator>

#include <stdint.h>

using namespace std;

struct RAIDDiskInfo {
	string		m_strState;
	string		m_strDevName;
	string		m_strSoftLinkName;
	int32_t		m_RaidUUID[4]; // Get after Examine()
	int32_t		m_iState;
	int32_t		m_iNumber;
	int32_t		m_iRaidDisk;

	RAIDDiskInfo()
	: m_strState("")
	, m_strDevName("")
	, m_iState(0)
	, m_iNumber(0)
	, m_iRaidDisk(0)
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
		m_iRaidDisk = rhs.diskInfo.raid_disk;
		return *this;
	}

	RAIDDiskInfo& operator=(const RAIDDiskInfo& rhs)
	{
		if (this == &rhs)
			return *this;

		m_strState = rhs.m_strState;
		m_strDevName = rhs.m_strDevName;
		m_iState = rhs.m_iState;
		m_iNumber = rhs.m_iNumber;
		m_iRaidDisk = rhs.m_iRaidDisk;
		return *this;
	}

	bool operator==(const RAIDDiskInfo& rhs) const
	{
		return (m_strState == rhs.m_strState &&
			m_strDevName == rhs.m_strDevName &&
			m_iState == rhs.m_iState &&
			m_iNumber == rhs.m_iNumber &&
			m_iRaidDisk == rhs.m_iRaidDisk);
	}
};

struct RAIDInfo {
	vector<RAIDDiskInfo>	m_vDiskList;
	string			m_strVolumeName;
	string			m_strDevNodeName;
	string			m_strMountPoint;
	string			m_strState;
	string			m_strLayout;
	string			m_strRebuildingOperation;
	time_t			m_CreationTime;
	time_t			m_UpdateTime;
	uint32_t		m_UUID[4];
	int64_t			m_ullTotalCapacity;
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
	int32_t			m_iFormatProgress;
	bool			m_bSuperBlockPersistent;
	bool			m_bInactive;
	bool			m_bRebuilding;
	bool			m_bFormat;
	bool			m_bMount;
	
	RAIDInfo()
	: m_strVolumeName("")
	, m_strDevNodeName("")
	, m_strMountPoint("")
	, m_ullTotalCapacity(0ull)
	, m_iRAIDLevel(UnSet)
	, m_iTotalDiskNum(0)
	{
		for (int i = 0; i < 4; i ++)
			m_UUID[i] = 0;
	}

	~RAIDInfo() {}

	RAIDInfo& operator=(const struct array_detail& rhs)
	{
		m_vDiskList.clear();
		for (int i = 0; i < MAX_DISK_NUM; i++) {
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
		m_strVolumeName = rhs.m_strVolumeName;
		m_strMountPoint = rhs.m_strMountPoint;
		memcpy(m_UUID, rhs.m_UUID, sizeof(m_UUID));
		m_ullTotalCapacity = rhs.m_ullTotalCapacity;
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
		m_bFormat = rhs.m_bFormat;
		m_bMount = rhs.m_bMount;
		m_iFormatProgress = rhs.m_iFormatProgress;

		return *this;
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
#endif

	bool m_bRAIDInfoListUpdating;
	bool m_bUsedMD[128];

private:
	vector<RAIDInfo>::iterator SearchDiskBelong2RAID(const string& dev, RAIDDiskInfo& devInfo);

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

	int CreateRAID(const string& mddev, vector<string>& vDevList, int level);
	int AssembleRAID(const string& mddev, const int uuid[4]);
	int AssembleRAID(const string& mddev, vector<string>& vDevList);
	int GetFreeMDNum();
	void FreeMDNum(int n);
	void SetMDNum(int n);

	void UpdateRAIDDiskList(vector<RAIDDiskInfo>& vRAIDDiskInfoList);
	bool ManageRAIDSubdevs(const string& mddev, vector<string>& vDevList, int operation);
	vector<RAIDInfo>::iterator IsMDDevInRAIDInfoList(const string &mddev);
	vector<RAIDInfo>::iterator IsMDDevInRAIDInfoList(const string &mddev, RAIDInfo& info);
	bool IsDiskExistInRAIDDiskList(const string& dev);
	bool IsDiskExistInRAIDDiskList(vector<string>& vDevList);
	int GenerateMDDevName(string& name);

public:
	RAIDManager();
	~RAIDManager();

	bool AddRAIDDisk(const string& dev);
	bool RemoveRAIDDisk(const string& dev);

	bool CreateRAID(vector<string>& vDevList, int level, string& strMDName);
	bool AssembleRAID(const int uuid[4], string& strMDName);
	bool AssembleRAID(vector<string>& vDevList, string& strMDName);
	bool RemoveDisks(const string& mddev, vector<string>& vDevList);
	bool MarkFaultyDisks(const string& mddev, vector<string>& vDevList);
	bool AddDisks(const string& mddev, vector<string>& vDevList);
	bool ReaddDisks(const string& mddev, vector<string>& vDevList);
	bool ReplaceDisk(const string& mddev, const string& replace, const string& with);
	bool DeleteRAID(const string& mddev);
	bool StopRAID(const string& mddev);

	bool GetRAIDInfo(const string& mddev, RAIDInfo& info);
	void GetRAIDInfo(vector<RAIDInfo>& list);

	bool UpdateRAIDInfo(); // May need for periodically update.
	bool UpdateRAIDInfo(const string& mddev);
	bool UpdateRAIDInfo(const int uuid[4]);

	bool CheckFileSystem();
	bool DoFileSystemRecovery();
	bool GetFileSystemStatus();

	bool Format();
	bool Mount();
	bool Unmount();
}; 


#endif

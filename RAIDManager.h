#ifndef __RAIDMANAGER_H__
#define __RAIDMANAGER_H__

#ifdef __cplusplus
extern "C" {
#endif
#include "libmdadm/mdadm.h"
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
#include <map>

#include <stdint.h>
#include <sys/stat.h>
#include <unistd.h>
#include <scsi/sg_cmds.h>
#include <scsi/sg_unaligned.h>

using namespace std;

enum eDiskType {
	DISK_TYPE_UNKNOWN = -1,
	DISK_TYPE_SATA,
	DISK_TYPE_ESATA,
	DISK_TYPE_ISCSI,
	DISK_TYPE_NFS
};

struct SGSerialNoPage {
	uint8_t m_bytePQPDT; /* bit 5-7: Peripheral Qualifier, bit 0-4: Peripheral Device Type */
	uint8_t m_bytePageCode;
	uint8_t m_byteReservied;
	uint8_t m_bytePageLength;
	uint8_t m_bytePageSN[32];
};

struct SGReadCapacity10 {
	uint8_t m_LogicalBlockAddr[4];
	uint8_t m_BlockLength[4];
};

struct MiscDiskInfo {
	string m_strSymLink;
	string m_strMDDev;
	eDiskType m_diskType;

	MiscDiskInfo()
	: m_strSymLink("")
	, m_strMDDev("")
	, m_diskType(DISK_TYPE_UNKNOWN)
	{
	}

	MiscDiskInfo(const string& link, eDiskType type)
	: m_strSymLink(link)
	, m_strMDDev("")
	, m_diskType(type)
	{
	}

	~MiscDiskInfo()
	{
	}

	MiscDiskInfo& operator=(const MiscDiskInfo& rhs)
	{
		if (this == &rhs)
			return *this;

		m_strSymLink = rhs.m_strSymLink;
		m_diskType = rhs.m_diskType;
		m_strMDDev = rhs.m_strMDDev;
	}

	void Dump()
	{
		printf("Soft Link: %s, In RAID: %s",
			   m_strSymLink.c_str(), m_strMDDev.c_str());
		switch (m_diskType) {
		case DISK_TYPE_UNKNOWN:
			printf(", Type: Unknown\n");
			break;
		case DISK_TYPE_SATA:
			printf(", Type: SATA\n");
			break;
		case DISK_TYPE_ESATA:
			printf(", Type: ESATA\n");
			break;
		case DISK_TYPE_ISCSI:
			printf(", Type: iSCSI\n");
			break;
		case DISK_TYPE_NFS:
			printf(", Type: NFS\n");
			break;
		}
	}
};

struct RAIDDiskInfo {
	MiscDiskInfo m_miscInfo;
	string		m_strState;
	string		m_strDevName;			/* Device node */
	string		m_strVendor;
	string		m_strModel;
	string		m_strFirmwareVersion;
	string		m_strSerialNum;
	int64_t		m_llCapacity;
	int32_t		m_RaidUUID[4];			/* Get after Examine(). */
	int32_t		m_iState;
	int32_t		m_iRaidDiskNum;
	int32_t		m_iMajor;				/* For confirming whether disk is valid or not. */
	int32_t		m_iMinor;				/* For confirming whether disk is valid or not. */
	bool		m_bHasMDSB;

	RAIDDiskInfo()
	: m_strState("")
	, m_strDevName("")
	, m_strVendor("")
	, m_strModel("")
	, m_strFirmwareVersion("")
	, m_strSerialNum("")
	, m_llCapacity(0ll)
	, m_iState(0)
	, m_iRaidDiskNum(0)
	, m_iMajor(0)
	, m_iMinor(0)
	, m_bHasMDSB(false)
	{
		memset(m_RaidUUID, 0x00, sizeof(m_RaidUUID));
	}

	~RAIDDiskInfo() {}

	RAIDDiskInfo& operator=(const struct array_disk_info& rhs)
	{
		m_strState = rhs.strState;
		m_strDevName = rhs.strDevName;
		m_iState = rhs.diskInfo.state;
		//m_iNumber = rhs.diskInfo.number;
		m_iRaidDiskNum = rhs.diskInfo.raid_disk;
		m_iMajor = rhs.diskInfo.major;
		m_iMinor = rhs.diskInfo.minor;
		SetHDDVendorInfomation();
		return *this;
	}

	void Dump()
	{
		printf("Device: %s, State: %s, MD Super Block: %s, Major: %d, Minor: %d\n\t",
			   m_strDevName.c_str(), m_strState.c_str(), m_bHasMDSB ? "Yes" : "No",
			   m_iMajor, m_iMinor);
		m_miscInfo.Dump();
	}

	void SetHDDVendorInfomation() {
		if (m_strDevName.empty())
			return;

		struct sg_simple_inquiry_resp resp;
		struct SGSerialNoPage sg_sn_resp;
		struct SGReadCapacity10 sg_readcap10_resp;
		int sg_fd = sg_cmds_open_device(m_strDevName.c_str(), 1, 1);

		if (sg_fd < 0)
			return;

		if (0 == sg_simple_inquiry(sg_fd, &resp, 0, 0)) {
			m_strVendor = resp.vendor;
			m_strModel = resp.product;
			m_strFirmwareVersion = resp.revision;
		} else {
			/*WriteHWLog(LOG_LOCAL1, LOG_DEBUG, "DiskInfo",
					   "Cannot get %s's vendor information.",
					   m_strDevName.c_str());*/
		}

		if (0 == sg_ll_inquiry(sg_fd, 0 ,1 , 0x80, 
							   (void*)&sg_sn_resp,
							   sizeof(struct SGSerialNoPage),
							   0, 0)) {
			m_strSerialNum = (char*)sg_sn_resp.m_bytePageSN;	
		} else {
			/*WriteHWLog(LOG_LOCAL1, LOG_DEBUG, "DiskInfo",
					   "Cannot get %s's serial number.",
					   m_strDevName.c_str());*/
		}

		if (0 == sg_ll_readcap_10(sg_fd, 0, 1,
								  &sg_readcap10_resp,
								  sizeof(struct SGReadCapacity10),
								  0, 0)) {
			uint32_t last_blk_addr = 0, block_size = 0;
			last_blk_addr = sg_get_unaligned_be32(&(sg_readcap10_resp.m_LogicalBlockAddr));
			if (0xffffffff != last_blk_addr) {
				block_size = sg_get_unaligned_be32(&(sg_readcap10_resp.m_BlockLength));
				m_llCapacity = (last_blk_addr + 1) * block_size;
			}
		} else {
			/*WriteHWLog(LOG_LOCAL1, LOG_DEBUG, "DiskInfo",
					   "Cannot get %s's capacity.",
					   m_strDevName.c_str());*/
		}

		sg_cmds_close_device(sg_fd);	
	}

	/*void HandleDevName(const string& name)
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
					SetHDDVendorInfomation();
					return;
				}
			}
		}
		m_strDevName = name;
		m_strSoftLinkName = name;
		SetHDDVendorInfomation();
	}*/

	RAIDDiskInfo& operator=(const RAIDDiskInfo& rhs)
	{
		if (this == &rhs)
			return *this;
		m_strState = rhs.m_strState;
		m_strDevName = rhs.m_strDevName;
		m_iState = rhs.m_iState;
		m_iRaidDiskNum = rhs.m_iRaidDiskNum;
		m_bHasMDSB = rhs.m_bHasMDSB;
		m_iMajor = rhs.m_iMajor;
		m_iMinor = rhs.m_iMinor;
		SetHDDVendorInfomation();
		return *this;
	}

	bool operator==(const RAIDDiskInfo& rhs) const
	{
		return (m_strDevName == rhs.m_strDevName);
	}

	bool operator==(const string& rhs) const
	{
		return (rhs == m_strDevName);
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
	, m_iRebuildingProgress(-1)
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

	bool IsDiskHaveMDSuperBlock(const string& dev, examine_result &result, int &err);

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
	bool GetDisksInfoBySymLink(const string& link, RAIDDiskInfo &info);

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

	static string GetDeviceNodeBySymLink(const string& symlink);
}; 


#endif

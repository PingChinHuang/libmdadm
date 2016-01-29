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
#include "common/semaphore.h"
#include "apr/apr_thread_worker.h"
#include "debugMsg/Debug.h"
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
#include <libudev.h>
#include <stdarg.h>
#include <atasmart.h>

using namespace std;

enum eDiskType {
	DISK_TYPE_UNKNOWN = -1,
	DISK_TYPE_SATA,
	DISK_TYPE_ESATA,
	DISK_TYPE_ISCSI,
	DISK_TYPE_NFS
};

enum eCBEvent {
	CB_EVENT_INITIAL		= 1,
	CB_EVENT_MOUNT			= 1 << 1, 
	CB_EVENT_FORMATING		= 1 << 2, 
	CB_EVENT_FORMATED		= 1 << 3, 
	CB_EVENT_DELRAID_DONE	= 1 << 4, 
	CB_EVENT_REMDISK_DONE	= 1 << 5, 
};

typedef void (*raidmgr_event_cb)(void *, uint64_t);

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

struct SGReadCapacity16 {
	uint8_t m_LogicalBlockAddr[8];
	uint8_t m_BlockLength[4];
	uint8_t m_ProtectionInfo; // bit 0: PROT_EN, bit 1:3: P_TYPE, bit 4:7: reserved
	uint8_t m_Reserved[19];
};

struct DiskProfile {
	string m_strSysName;
	string m_strDevPath;
	string m_strSymLink;
	string m_strMDDev;
	string m_strVendor;
	string m_strModel;
	string m_strFWVer;
	string m_strSerialNum;
	string m_strSMARTOverall;
	uint64_t m_llCapacity;
	uint64_t m_ullSMARTBadSectors;
	uint64_t m_ullSMARTTemp;
	eDiskType m_diskType;
	int m_iBay;
	bool m_bSMARTSupport;

	DiskProfile()
	: m_strSysName("")
	, m_strDevPath("")
	, m_strSymLink("")
	, m_strMDDev("")
	, m_strVendor("")
	, m_strModel("")
	, m_strFWVer("")
	, m_strSerialNum("")
	, m_strSMARTOverall("")
	, m_llCapacity(0ull)
	, m_ullSMARTBadSectors(0ull)
	, m_ullSMARTTemp(0ull)
	, m_diskType(DISK_TYPE_UNKNOWN)
	, m_iBay(-1)
	, m_bSMARTSupport(false)
	{
	}

	DiskProfile(string dev)
	: m_strSysName(dev)
	, m_strDevPath("")
	, m_strSymLink("")
	, m_strMDDev("")
	, m_strVendor("")
	, m_strModel("")
	, m_strFWVer("")
	, m_strSerialNum("")
	, m_strSMARTOverall("")
	, m_llCapacity(0ull)
	, m_ullSMARTBadSectors(0ull)
	, m_ullSMARTTemp(0ull)
	, m_diskType(DISK_TYPE_UNKNOWN)
	, m_iBay(-1)
	, m_bSMARTSupport(false)
	{
		SetUDEVInformation();
		SetDiskVendorInfomation();
		GetDevCapacity();
		ReadMDStat();
		SetSMARTInfo();
		//Dump();
	}

	~DiskProfile()
	{
	}

	DiskProfile& operator=(const DiskProfile& rhs)
	{
		if (this == &rhs)
			return *this;
		m_strSysName = rhs.m_strSysName;
		m_strDevPath = rhs.m_strDevPath;
		m_strSymLink = rhs.m_strSymLink;
		m_diskType = rhs.m_diskType;
		m_strMDDev = rhs.m_strMDDev;
		m_iBay = rhs.m_iBay;
		m_strVendor = rhs.m_strVendor;
		m_strModel = rhs.m_strModel;
		m_strFWVer = rhs.m_strFWVer;
		m_strSerialNum = rhs.m_strSerialNum;
		m_strSMARTOverall = rhs.m_strSMARTOverall;
		m_llCapacity = rhs.m_llCapacity;
		m_ullSMARTBadSectors = rhs.m_ullSMARTBadSectors;
		m_ullSMARTTemp = rhs.m_ullSMARTTemp;
		m_bSMARTSupport = rhs.m_bSMARTSupport;
		return *this;
	}

	bool operator==(const DiskProfile& rhs)
	{
		return (m_strSysName == rhs.m_strSysName &&
				m_strDevPath == rhs.m_strDevPath &&
				m_strSymLink == rhs.m_strSymLink);
	}

	void SetUDEVInformation()
	{
		if (m_strSysName.empty())
			return;

		struct udev *udev;
		struct udev_device *dev;
		udev = udev_new();
		if (!udev) {
			TRACE("can't create udev\n");
			return;
		}

		dev = udev_device_new_from_subsystem_sysname(udev, "block", m_strSysName.c_str());
		if (dev) {
			int ret = 0;

			m_strDevPath = udev_device_get_devnode(dev);
			m_strSymLink = udev_device_get_property_value(dev, "ID_SYMLINK");

			if (!m_strSymLink.empty()) {
				if (m_strSymLink.find("nuuo_sata") != string::npos) {
					m_diskType = DISK_TYPE_SATA;
					ret = sscanf(m_strSymLink.c_str(), "nuuo_sata%d", &m_iBay);
					if (ret < 1 || ret == EOF || m_iBay > 127 || m_iBay < 0) {
						m_iBay = -1;
					}
				} else if (m_strSymLink.find("nuuo_esata") != string::npos) {
					m_diskType = DISK_TYPE_ESATA;
					ret = sscanf(m_strSymLink.c_str(), "nuuo_esata%d", &m_iBay);
					if (ret < 1 || ret == EOF || m_iBay > 127 || m_iBay < 0) {
						m_iBay = -1;
					}
				} else if (m_strSymLink.find("nuuo_iscsi") != string::npos) {
					m_diskType = DISK_TYPE_ISCSI;
					ret = sscanf(m_strSymLink.c_str(), "nuuo_iscsi%d", &m_iBay);
					if (ret < 1 || ret == EOF) {
						m_iBay = -1;
					}
					
					/* iSCSI disk use its target name as serial number. */
					m_strSerialNum = udev_device_get_property_value(dev, "ID_PATH");
				} else {
					TRACE("Unknown SYMLINK\n");
				}
			}
		} else {
			TRACE("can't found block device %s.\n", m_strSysName.c_str());
		}
	
		udev_device_unref(dev);
		udev_unref(udev);
		return;
	}

	void SetDiskVendorInfomation()
	{
		if (m_strDevPath.empty())
			return;

		struct sg_simple_inquiry_resp resp;
		struct SGSerialNoPage sg_sn_resp;
		int sg_fd = sg_cmds_open_device(m_strDevPath.c_str(), 1, 1);

		if (sg_fd < 0)
			return;

		if (0 == sg_simple_inquiry(sg_fd, &resp, 0, 0)) {
			m_strVendor = resp.vendor;
			m_strModel = resp.product;
			m_strFWVer = resp.revision;
		} else {
			/*WriteHWLog(LOG_LOCAL1, LOG_DEBUG, "DiskInfo",
					   "Cannot get %s's vendor information.",
					   m_strDevName.c_str());*/
		}

		if (m_diskType != DISK_TYPE_ISCSI &&
			0 == sg_ll_inquiry(sg_fd, 0 ,1 , 0x80, 
							   (void*)&sg_sn_resp,
							   sizeof(struct SGSerialNoPage),
							   0, 0)) {
			m_strSerialNum = (char*)sg_sn_resp.m_bytePageSN;	
		} else {
			/*WriteHWLog(LOG_LOCAL1, LOG_DEBUG, "DiskInfo",
					   "Cannot get %s's serial number.",
					   m_strDevName.c_str());*/
		}

		sg_cmds_close_device(sg_fd);	
	}

	void GetDevCapacity()
	{
		int fd = open(m_strDevPath.c_str(), O_RDONLY);
		if (fd < 0) {
			TRACE("Cannot open %s, %s\n", m_strDevPath.c_str(), strerror(errno));
		}
	
		if (0 == get_dev_size(fd, (char*)m_strDevPath.c_str(), &m_llCapacity))
			m_llCapacity = 0;

		close(fd);
	}

	void ReadMDStat()
	{
		struct mdstat_ent *ms = mdstat_read(0, 0);
		struct mdstat_ent *e = ms;
		e = mdstat_by_component((char*)m_strSysName.c_str());
		if (e)
			m_strMDDev = e->dev;
		else
			m_strMDDev = "";

		free_mdstat(e);
	}

	void SetSMARTInfo()
	{
		SkDisk *d = NULL;
		SkBool available;
		SkSmartOverall overall;

		if (sk_disk_open(m_strDevPath.c_str(), &d) < 0) {
			 TRACE("Fail to open S.M.A.R.T. device %s.\n",
					 m_strDevPath.c_str());
			 return;
		}

		if (sk_disk_smart_is_available(d, &available) < 0) {
			 //TRACE("Fail to query %s whether S.M.A.R.T. is available.\n",
					 //m_strDevPath.c_str());
			m_bSMARTSupport = false;
			m_strSMARTOverall.clear();
			m_ullSMARTTemp = 0ull;
			m_ullSMARTBadSectors = 0ull;
			 goto get_smart_info_fail;
		}
		m_bSMARTSupport = available ? true : false;

		if (sk_disk_smart_read_data(d) < 0) {
			 TRACE("Fail to read %s's S.M.A.R.T. data.\n",
					 m_strDevPath.c_str());
			 goto get_smart_info_fail;
		}

		if (sk_disk_smart_get_overall(d, &overall) < 0) {
			 TRACE("Fail to get %s's S.M.A.R.T. overall status.\n",
					 m_strDevPath.c_str());
		}
		m_strSMARTOverall = sk_smart_overall_to_string(overall);

		if (sk_disk_smart_get_bad(d, &m_ullSMARTBadSectors) < 0) {
			 TRACE("Fail to get %s's bad sectors information.\n",
					 m_strDevPath.c_str());
		}

		if (sk_disk_smart_get_temperature(d, &m_ullSMARTTemp) < 0) {
			 TRACE("Fail to get %s's temperature.\n",
					 m_strDevPath.c_str());
		}
		m_ullSMARTTemp = (m_ullSMARTTemp / 1000) - 273; /* Covert to Celsius */

get_smart_info_fail:
		sk_disk_free(d);
	}

	bool RunSMARTSelfTest(SkSmartSelfTest type)
	{
		SkDisk *d = NULL;
		if (sk_disk_open(m_strDevPath.c_str(), &d) < 0) {
			TRACE("Fail to open S.M.A.R.T. device %s.\n",
				 m_strDevPath.c_str());
			return false;
		}
		
		if (sk_disk_smart_self_test(d, type) < 0) {
			TRACE("Fail to start %s's S.M.A.R.T. test.\n",
					 m_strDevPath.c_str());
			sk_disk_free(d);
			return false;
		}

		sk_disk_free(d);
		return true;
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

		printf("\tS.M.A.R.T: %s, Status %s, Bad Sectors: %llu, Temperature: %llu C\n\n",
				m_bSMARTSupport ? "Support" : "Not Support",
				m_strSMARTOverall.c_str(), m_ullSMARTBadSectors,
				m_ullSMARTTemp
			  );
	}
};

struct RAIDDiskInfo {
	DiskProfile m_diskProfile;
	string		m_strState;
	string		m_strDevPath;			/* Device node */
	int32_t		m_RaidUUID[4];			/* Get after Examine(). */
	int32_t		m_iState;
	int32_t		m_iRaidDiskNum;
	int32_t		m_iMajor;				/* For confirming whether disk is valid or not. */
	int32_t		m_iMinor;				/* For confirming whether disk is valid or not. */

	RAIDDiskInfo()
	: m_strState("")
	, m_strDevPath("")
	, m_iState(0)
	, m_iRaidDiskNum(0)
	, m_iMajor(0)
	, m_iMinor(0)
	{
		memset(m_RaidUUID, 0x00, sizeof(m_RaidUUID));
	}

	~RAIDDiskInfo() {}

	RAIDDiskInfo& operator=(const struct array_disk_info& rhs)
	{
		m_strState = rhs.strState;
		m_strDevPath = rhs.strDevName;
		m_iState = rhs.diskInfo.state;
		m_iRaidDiskNum = rhs.diskInfo.raid_disk;
		m_iMajor = rhs.diskInfo.major;
		m_iMinor = rhs.diskInfo.minor;
		return *this;
	}

	void Dump()
	{
		printf("Device: %s, State: %s, Major: %d, Minor: %d\n\t",
			   m_strDevPath.c_str(), m_strState.c_str(), 
			   m_iMajor, m_iMinor);
		m_diskProfile.Dump();
	}

	RAIDDiskInfo& operator=(const RAIDDiskInfo& rhs)
	{
		if (this == &rhs)
			return *this;
		m_diskProfile = rhs.m_diskProfile;
		m_strState = rhs.m_strState;
		m_strDevPath = rhs.m_strDevPath;
		m_iState = rhs.m_iState;
		m_iRaidDiskNum = rhs.m_iRaidDiskNum;
		m_iMajor = rhs.m_iMajor;
		m_iMinor = rhs.m_iMinor;
		return *this;
	}

	bool operator==(const RAIDDiskInfo& rhs) const
	{
		return (m_strDevPath == rhs.m_strDevPath);
	}

	bool operator==(const string& rhs) const
	{
		return (rhs == m_strDevPath);
	}

	bool operator==(const DiskProfile& profile) const
	{
		return (m_strDevPath == profile.m_strDevPath);
	}
};

struct MDProfile {
#ifdef NUUO
	smart_ptr<FilesystemManager> m_fsMgr;
#else
	shared_ptr<FilesystemManager> m_fsMgr;
#endif
	vector<string> m_vMembers;
	string m_strSysName;
	string m_strDevPath;
	int m_iRaidDisks; 
	int m_iDevCount;
	int m_iMDNum;
	bool m_bInMDStat;
	int m_uuid[4];

	MDProfile()
	: m_fsMgr(NULL)
	, m_strSysName("")
	, m_strDevPath("")
	, m_iRaidDisks(0)
	, m_iDevCount(0)
	, m_iMDNum(-1)
	, m_bInMDStat(false)
	{
		m_vMembers.clear();
	}

	MDProfile(string dev)
	: m_fsMgr(NULL)
	, m_strSysName(dev)
	, m_strDevPath("")
	, m_iRaidDisks(0)
	, m_iDevCount(0)
	, m_iMDNum(-1)
	, m_bInMDStat(false)
	{
		m_vMembers.clear();
		SetUDEVInformation();
		ReadMDStat();
		InitializeFSManager();
	}

	~MDProfile()
	{
		m_vMembers.clear();
		m_fsMgr = NULL;
	}

	MDProfile& operator=(MDProfile& rhs)
	{
		if (this == &rhs)
			return *this;

		m_vMembers.clear();
		for (size_t i = 0; i < rhs.m_vMembers.size(); i++)
			m_vMembers.push_back(rhs.m_vMembers[i]);
		m_strSysName = rhs.m_strSysName;
		m_strDevPath = rhs.m_strDevPath;
		m_iRaidDisks = rhs.m_iRaidDisks;
		m_iDevCount = rhs.m_iDevCount;
		m_iMDNum = rhs.m_iMDNum;
		m_fsMgr = rhs.m_fsMgr;
		return *this;
	}

	bool operator==(MDProfile& rhs)
	{
		return (m_strSysName == rhs.m_strSysName);
	}

	void SetUDEVInformation()
	{
		if (m_strSysName.empty())
			return;

		struct udev *udev;
		struct udev_device *dev;
		udev = udev_new();
		if (!udev) {
			TRACE("can't create udev\n");
			return;
		}

		dev = udev_device_new_from_subsystem_sysname(udev, "block", m_strSysName.c_str());
		if (dev) {
			int ret = 0;

			m_strDevPath = udev_device_get_devnode(dev);

			if (!m_strDevPath.empty()) {
				ret = sscanf(m_strDevPath.c_str(), "/dev/md%d", &m_iMDNum);
				if (ret < 1 || ret == EOF || m_iMDNum > 127 || m_iMDNum < 0) {
					m_iMDNum = -1;
				}
			}
		}

		udev_device_unref(dev);
		udev_unref(udev);
		return;
	}

	void ReadMDStat()
	{
		struct mdstat_ent *ms = mdstat_read(0, 0);
		struct mdstat_ent *e = ms;
		m_vMembers.clear();
		for (; e; e = e->next) {
			if (m_strSysName == e->dev) {
				mdstat_ent::dev_member *dev = e->members;
				for (; dev; dev = dev->next) {
					m_vMembers.push_back(dev->name);
				}

				m_iRaidDisks = e->raid_disks;
				m_iDevCount = e->devcnt;
				m_bInMDStat = true;
				break;
			}
		}
		free_mdstat(ms);
	}

	bool InitializeFSManager() {
		try {
#ifdef NUUO
			m_fsMgr = new FilesystemManager(m_strDevPath);
#else
			m_fsMgr = shared_ptr<FilesystemManager>(new FilesystemManager(m_strDevPath));
#endif
			if (!m_fsMgr->IsInitialized()) {
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

	void Dump() {
		char buf[128];
		printf("MD Name: %s\n\tDev Node: %s, RAID Disks: (%d/%d)\n\t%s\n\t",
				m_strSysName.c_str(), m_strDevPath.c_str(), m_iDevCount, m_iRaidDisks,
				__fname_from_uuid(m_uuid, 0, buf, ':'));
		if (m_fsMgr.get()) {
			printf("Status: Format(%s), Mount(%s on %s)",
					m_fsMgr->IsFormated()?"Yes":"No",
					m_fsMgr->IsMounted()?"Yes":"No", m_fsMgr->GetMountPoint().c_str());
		}
		printf("\n");
	}
};

struct RAIDInfo {
	vector<RAIDDiskInfo>	m_vDiskList;
	string			m_strSysName;
	string			m_strDevPath;
	string			m_strState;
	string			m_strLayout;
	string			m_strRebuildingOperation;
	string			m_strMountPoint;
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
	int32_t			m_iFormatingState;
	int32_t			m_iFormatProgress;
	bool			m_bFormat;
	bool			m_bMount;
	bool			m_bSuperBlockPersistent;
	bool			m_bInactive;
	bool			m_bRebuilding;
	
	RAIDInfo()
	: m_strSysName("")
	, m_strDevPath("")
	, m_strState("")
	, m_strLayout("")
	, m_strRebuildingOperation("")
	, m_strMountPoint("")
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
	, m_iFormatingState(-1)
	, m_iFormatProgress(-1)
	, m_bFormat(false)
	, m_bMount(false)
	, m_bSuperBlockPersistent(false)
	, m_bInactive(false)
	, m_bRebuilding(false)
	{
		for (int i = 0; i < 4; i ++)
			m_UUID[i] = 0;
	}

	~RAIDInfo() {
	}

	RAIDInfo& operator=(const struct array_detail& rhs)
	{
		m_vDiskList.clear();
		for (unsigned i = 0; i < rhs.uDiskCounter; i++) {
			RAIDDiskInfo info;
			if (rhs.arrayDisks[i].diskInfo.major != 8)
				continue;
			
			info = rhs.arrayDisks[i];
			m_vDiskList.push_back(info);
		}
		
		m_strState = rhs.strArrayState;
		m_strLayout = rhs.strRaidLayout;
		m_strRebuildingOperation = rhs.strRebuildOperation;
		m_strDevPath = rhs.strArrayDevName;
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
		m_bRebuilding = (rhs.bIsRebuilding == 1) ? true : false;
		m_bInactive = (rhs.bInactive == 1) ? true : false;
		m_bSuperBlockPersistent = (rhs.bIsSuperBlockPersistent == 1) ? true : false;
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
	
		m_strSysName = rhs.m_strSysName;	
		m_strState = rhs.m_strState;
		m_strLayout = rhs.m_strLayout;
		m_strRebuildingOperation = rhs.m_strRebuildingOperation;
		m_strDevPath = rhs.m_strDevPath;
		m_strMountPoint = rhs.m_strMountPoint;
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
		m_iMDNum = rhs.m_iMDNum;
		m_iFormatProgress = rhs.m_iFormatProgress;
		m_iFormatingState = rhs.m_iFormatingState;
		m_bMount = rhs.m_bMount;
		m_bFormat = rhs.m_bFormat;

		return *this;
	}

	bool operator==(const RAIDInfo& rhs) const
	{
		return (m_strSysName == rhs.m_strSysName &&
			0 == memcmp(rhs.m_UUID, m_UUID, sizeof(m_UUID)));
	}

	bool operator==(const string& rhs) const
	{
		return (m_strSysName == rhs);
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
			"\tTotal Capacity: %lld\n\tUsed Capacity: %lld\n"
			"\tTotal Disks: %d (R: %d, A: %d, W: %d, F: %d, S: %d)\n"
			"\tCreatetion Time: %.24s\n\tUpdate Time: %.24s\n"
			"\tRebuilding: %s (%d%%)\n\tMounted: %s\n\tFormated: %s\n"
			"\tFormating Progress: %d\n\n",
			m_strDevPath.c_str(), m_strState.c_str(),
			m_iRAIDLevel, m_iChunkSize,
			m_ullTotalCapacity, m_ullUsedSize,
			m_iTotalDiskNum, m_iRAIDDiskNum, m_iActiveDiskNum,
			m_iWorkingDiskNum, m_iFailedDiskNum, m_iSpareDiskNum,
			ctime(&m_CreationTime), ctime(&m_UpdateTime),
			m_bRebuilding?"Yes":"No", (m_iRebuildingProgress < 0)?100:m_iRebuildingProgress,
			m_bMount?"Yes":"No", m_bFormat?"Yes":"No",
			m_iFormatingState
			);
		
		for (size_t i =0 ; i < m_vDiskList.size(); i++) {
			printf("\t");
			m_vDiskList[i].Dump();
		}
		printf("\n");
	}
};

class RAIDManager : public AprThreadWorker {
private:
	enum {
		eTC_STOP,
	};

private:
	map<string, MDProfile> m_mapMDProfiles; /* /dev/mdX, profile */
	map<string, DiskProfile> m_mapDiskProfiles; /* /dev/sdX, profile */
	string m_strLastError;
	bool m_bUsedMD[128];
	bool m_bUsedVolume[128];
	
	CriticalSection m_csMDProfiles;
	CriticalSection m_csDiskProfiles;
	CriticalSection m_csUsedMD;
	CriticalSection m_csUsedVolume;
	CriticalSection m_csLastError;
	CriticalSection m_csCBEvent;
	Semaphore m_semAssemble;
	AprCond m_NotifyChange;

	CriticalSection m_csCallback;
	void* m_pCallbackData;
	raidmgr_event_cb m_cb;

	uint64_t m_u64CBEvent;

private:
	vector<RAIDInfo>::iterator SearchDiskBelong2RAID(RAIDDiskInfo& devInfo);

	bool Initialize();

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
	int OpenMDDev(const string& mddev_path);

	int CreateRAID(const string& mddev, vector<string>& vDevList, int level);

	bool AssembleRAID(const int uuid[4], string &mddev);
	int AssembleRAID(const string& mddev, const int uuid[4]);

	int GetFreeMDNum();
	void FreeMDNum(int n);
	void SetMDNum(int n);

	int GetFreeVolumeNum();
	void FreeVolumeNum(int n);
	void SetVolumeNum(int n);

	void UpdateRAIDDiskList(vector<RAIDDiskInfo>& vRAIDDiskInfoList, const string& mddev);
	bool ManageRAIDSubdevs(const string& mddev, vector<string>& vDevList, int operation);
	vector<RAIDInfo>::iterator IsMDDevInRAIDInfoList(const string &mddev);
	vector<RAIDInfo>::iterator IsMDDevInRAIDInfoList(const string &mddev, RAIDInfo& info);
	bool IsDiskExistInRAIDDiskList(const string& dev);
	bool IsDiskExistInRAIDDiskList(vector<string>& vDevList);
	string GenerateMDSysName(int num);

	bool StopRAID(const string& mddev);

	int QueryMDSuperBlockInDisk(const string& dev_path, examine_result &result);
	bool QueryMDDetail(const string& mddev_path, array_detail &ad);
	bool GenerateRAIDInfo(const MDProfile &profile, RAIDInfo& info);
	string GetDeviceNodeBySymLink(const string& symlink);

	void NotifyChange();

	void EventCallback(uint64_t event);

protected:
	void ThreadProc();

public:
	RAIDManager();
	~RAIDManager();

	void RegisterCB(void* pData, raidmgr_event_cb cb);
	void DeregisterCB();

	bool AddDisk(const string& dev);
	bool RemoveDisk(const string& dev);

	bool CreateRAID(vector<string>& vDevList, int level);

	bool RemoveMDDisks(const string& mddev, vector<string>& vDevList);
	bool MarkFaultyMDDisks(const string& mddev, vector<string>& vDevList);
	bool AddMDDisks(const string& mddev, vector<string>& vDevList);
	bool ReaddMDDisks(const string& mddev, vector<string>& vDevList);
	bool ReplaceMDDisk(const string& mddev, const string& replace, const string& with);

	bool DeleteRAID(const string& mddev);

	bool GetRAIDInfo(const string& mddev, RAIDInfo& info);
	void GetRAIDInfo(vector<RAIDInfo>& list);
	void GetFreeDisksInfo(vector<RAIDDiskInfo> &list);
	bool GetFreeDiskInfo(const string& symlink, RAIDDiskInfo &info);

	void SetLastError(const char* fmt, ...);
	void GetLastError(string &log);

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

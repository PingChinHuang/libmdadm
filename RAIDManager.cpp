#include "RAIDManager.h"

#ifdef NUUO
#include "common/file.h"
#include "common/string.h"
#include "common/directory_traverse.h"
#endif

#define LOG_LABEL "RAIDManager"
#define RAIDMANAGER_MONITOR_INTERVAL 3000 /* ms */

RAIDManager::RAIDManager()
{
	for (int i = 0; i < 128; i++)
		m_bUsedMD[i] = false;
	for (int i = 0; i < 128; i++)
		m_bUsedVolume[i] = false;

	CriticalSectionLock cs_MDProfiles(&m_csMDProfiles);
	CriticalSectionLock cs_DiskProfiles(&m_csDiskProfiles);
	Initialize();

	CriticalSectionLock cs_NotifyChange(&m_csNotifyChange);
	try {
		m_pNotifyChange = new AprCond(false);
		CreateThread();
	} catch (bad_alloc&) {
		m_pNotifyChange = NULL;
		WriteHWLog(LOG_LOCAL0, LOG_ERR, LOG_LABEL,
			   "Allocate memory failed.");
	}
}

RAIDManager::~RAIDManager()
{
	if (ThreadExists()) {
		uint32_t result;
		CallWorker(eTC_STOP, &result);
		if (result == 0) {
			CriticalSectionLock cs_NotifyChange(&m_csNotifyChange);
			delete m_pNotifyChange;
			m_pNotifyChange = NULL;
		}
	}
}

bool RAIDManager::Initialize()
{
	struct udev *udev;
	struct udev_enumerate *enumerate;
	struct udev_list_entry *devices, *dev_list_entry;
	struct udev_device *dev;
	
	udev = udev_new();
	if (!udev) {
		printf("can't create udev\n");
		return false;
	}

	enumerate = udev_enumerate_new(udev);
	udev_enumerate_add_match_subsystem(enumerate, "block");
	udev_enumerate_scan_devices(enumerate);
	devices = udev_enumerate_get_list_entry(enumerate);
	udev_list_entry_foreach(dev_list_entry, devices) {
		string strSysName;
		string strMajor;
		int iMajor = 0;
		const char *path = NULL;

		path = udev_list_entry_get_name(dev_list_entry);
		dev = udev_device_new_from_syspath(udev, path);

		strSysName = udev_device_get_sysname(dev);
		strMajor = udev_device_get_property_value(dev, "MAJOR");

		iMajor = str_to_i32(strMajor);
		switch(iMajor) {
		case 8: /* Disk */
		{
			DiskProfile profile(strSysName);
			m_mapDiskProfiles[strSysName] = profile;
			break;
		}
		case 9: /* MD */
		{
			MDProfile profile(strSysName);
			m_mapMDProfiles[strSysName] = profile;
			SetMDNum(m_mapMDProfiles[strSysName].m_iMDNum);

			if (NULL == m_mapMDProfiles[strSysName].m_fsMgr->get()) /* Retry */
				m_mapMDProfiles[strSysName].InitializeFSManager();
			
			if (m_mapMDProfiles[strSysName].m_fsMgr->get()) {
				if (m_mapMDProfiles[strSysName].m_fsMgr->IsInitialized()) {
					SetVolumeNum(m_mapMDProfiles[strSysName].m_fsMgr->m_iVolumeNum);
				} else {
					m_mapMDProfiles[strSysName].m_fsMgr->Initialize(); /* Retry */
					SetVolumeNum(m_mapMDProfiles[strSysName].m_fsMgr->m_iVolumeNum);
				}
			} else {
				printf("[%s] FilesystemManager initialization retried failed.\n");
			}
			break;
		}
		default:;
		}

		udev_device_unref(dev);
	}
	udev_enumerate_unref(enumerate);
	udev_unref(udev);

	/* TODO:
	 * Is it necessary to check all the MD devices in the list, and
	 * to set m_strMDDev of a DiskProfile since the disk is occupied
	 * by a MD device during this initial stage?
	 */
}

int RAIDManager::GetFreeMDNum()
{
	CriticalSectionLock cs_md(&m_csUsedMD);
	for (int i = 0 ; i < 128; i++) {
		if (!m_bUsedMD[i]) {
			m_bUsedMD[i] = true; // Pre-allocated, if you don't use it or any error happened, you have to free.
			return i;
		}
	}

	return -1; // No valid number
}

void RAIDManager::FreeMDNum(int n)
{
	if (n < 0 || n > 127)
		return;

	CriticalSectionLock cs_md(&m_csUsedMD);
	m_bUsedMD[n] = false;
}

void RAIDManager::SetMDNum(int n)
{
	if (n < 0 || n > 127)
		return;

	CriticalSectionLock cs_md(&m_csUsedMD);
	m_bUsedMD[n] = true;
}

int RAIDManager::GetFormerVolumeNum(int n)
{
	CriticalSectionLock cs_md(&m_csUsedVolume);
	/* Check requested Volume number */
	if (m_bUsedVolume[n]) {
		for (int i = 0; i < 128; i++) {
			if (!m_bUsedMD[i]) {
				m_bUsedMD[i] = true;
				return i;
			}
		}

		return -1;
	} else {
		m_bUsedVolume[n] = true;
		return n;
	}
}

int RAIDManager::GetFreeVolumeNum()
{
	CriticalSectionLock cs_md(&m_csUsedVolume);
	for (int i = 0 ; i < 128; i++) {
		if (!m_bUsedVolume[i]) {
			m_bUsedVolume[i] = true; // Pre-allocated, if you don't use it or any error happened, you have to free.
			return i;
		}
	}

	return -1; // No valid number
}

void RAIDManager::FreeVolumeNum(int n)
{
	if (n < 0 || n > 127)
		return;

	CriticalSectionLock cs_md(&m_csUsedVolume);
	m_bUsedVolume[n] = false;
}

void RAIDManager::SetVolumeNum(int n)
{
	if (n < 0 || n > 127)
		return;

	CriticalSectionLock cs_md(&m_csUsedVolume);
	m_bUsedVolume[n] = true;
}

bool RAIDManager::IsDiskHaveMDSuperBlock(const string& dev, examine_result &result, int &err)
{
	vector<string> vDevList;
	struct mddev_dev* devlist = NULL;
	struct context c;

	vDevList.push_back(dev);
	if (NULL == (devlist = InitializeDevList(vDevList))) {
		err = EXAMINE_MEM_ALLOC_FAIL;
		return false;
	}

	InitializeContext(c);
	err = Examine_ToResult(devlist, &c, NULL, &result);
	FreeDevList(devlist);

	return !(err == EXAMINE_NO_MD_SUPERBLOCK);
}

bool RAIDManager::AddDisk(const string& dev)
{
	/*0. dev is empty -> return false*/
	if (dev.empty())
		return false;

	/*1. Check device node exists or not (SYSUTILS::CheckBlockDevice)
		1.1 Yes -> 3 
		1.2 No -> return false*/
	if (!CheckBlockDevice(dev)) {
		WriteHWLog(LOG_LOCAL0, LOG_ERR, LOG_LABEL,
			   "%s is not a block device.\n", dev.c_str());
		return false;
	}

	m_csDiskProfiles.Lock();
	DiskProfile profile(dev);
	m_mapDiskProfiles[dev] = profile;
	m_csDiskProfiles.Unlock();

	CriticalSectionLock cs_NotifyChange(&m_csNotifyChange);
	if (NULL == m_pNotifyChange)
		m_pNotifyChange->set();

	return true;
}

bool RAIDManager::RemoveDisk(const string& dev)
{
	/* 0. dev is empty -> return false */
	if (dev.empty())
		return false;

	m_csDiskProfiles.Lock();
	map<string, DiskProfile>::iterator it;
	it = m_mapDiskProfiles.find(dev);
	m_mapDiskProfiles.erase(it);
	m_csDiskProfiles.Unlock();

	CriticalSectionLock cs_NotifyChange(&m_csNotifyChange);
	if (NULL == m_pNotifyChange)
		m_pNotifyChange->set();

	return true;
}

bool RAIDManager::GetRAIDDetail(const string& mddev,
								array_detail &ad)
{
	struct context c;
	int ret = SUCCESS;

	InitializeContext(c);
	ret = Detail_ToArrayDetail(mddev.c_str(), &c, &ad);
	if (ret != SUCCESS) {
		WriteHWLog(LOG_LOCAL1, LOG_DEBUG, LOG_LABEL,
				"[%d] Detail Error Code %s: (%d)\n",
				__LINE__, mddev.c_str(), ret);
		return false;
	}

	return true;
}

bool RAIDManager::IsRAIDAbnormal(const RAIDInfo &info)
{
	int num = -1;

	if (info.m_iTotalDiskNum == 0)
		goto raid_abnormal;

	switch (info.m_iRAIDLevel) {
	case 0:
	case 1:
	case 5:
	case 6:
	case 10:
	case LEVEL_MULTIPATH:
	case LEVEL_LINEAR:
		if (info.m_strState.find("FAILED") != string::npos)
			goto raid_abnormal;
		break;
	default:
		WriteHWLog(LOG_LOCAL1, LOG_DEBUG, LOG_LABEL,
				   "[%s] Unknown RAID Level.\n",
				   info.m_strDevNodeName.c_str());
		return true;
	}

	/* Try to mount the volume if it has been formated */
	if (info.m_fsMgr->IsFormated()) {
		if (!info.m_fsMgr->IsMounted(num)) {
			string strMountPoint;
			if (num < 0) {
				num = GetFreeVolumeNum();
			} else {
				num = GetFormerVolumeNum(num);
			}

			if (num < 0) {
				/*
				 * Although mount operation is failed,
				 * this volume is in normal state. So,
				 * we return false.
				 */
				WriteHWLog(LOG_LOCAL0, LOG_ERR, LOG_LABEL,
					   "Exceed maximal volume limitation.\n");
				return false;
			}

			info.m_fsMgr->SetVolumeNum(num);
			info.m_fsMgr->IsMounted(strMountPoint); // Get mount point name
			if (!info.m_fsMgr->Mount(strMountPoint)) {
				FreeVolumeNum(num);
			}
		}
	}

	return false;

raid_abnormal:
	/* Try to unmount the volume. */
	if (info.m_fsMgr->IsMounted(num)) {
		info.m_fsMgr->Unmount();
		FreeVolumeNum(num);
	}
	return true;
}

void RAIDManager::InitializeShape(struct shape& s, int raiddisks, int level, int chunk, int bitmap_chunk, char* bitmap_file)
{
	memset(&s, 0x00, sizeof(struct shape));
	s.raiddisks = raiddisks;
	s.level = level;
	s.layout = UnSet;
	s.bitmap_chunk = bitmap_chunk;
	s.chunk = chunk;
	s.assume_clean = 0;
	s.bitmap_file = bitmap_file;
}

void RAIDManager::InitializeContext(struct context& c, int force, int runstop, int verbose)
{
	memset(&c, 0x00, sizeof(struct context));
	c.force = force;
	c.delay = DEFAULT_BITMAP_DELAY;
	c.runstop = runstop;
	c.verbose = verbose;
	c.brief = 0;
}

void RAIDManager::InitializeMDDevIdent(struct mddev_ident& ident, int uuid_set, const int uuid[4], int bitmap_fd, char* bitmap_file)
{
	memset(&ident, 0x00, sizeof(struct mddev_ident));
	ident.uuid_set = uuid_set;
	ident.super_minor = UnSet;
	ident.level = UnSet;
	ident.raid_disks = UnSet;
	ident.spare_disks = UnSet;
	ident.bitmap_fd = bitmap_fd;
	ident.bitmap_file = bitmap_file;
	if (uuid_set) {
		memcpy(ident.uuid, uuid, sizeof(int) * 4);
		for (int i = 0; i < 4; i++) {
			ident.uuid[i] = __be32_to_cpu(ident.uuid[i]);
		}
	}
}

struct mddev_dev* RAIDManager::InitializeDevList(const string& replace, const string& with)
{
	struct mddev_dev* devlist = NULL;
	struct mddev_dev** devlistend = &devlist;
	struct mddev_dev* dv = NULL;

	if (replace.empty() || with.empty())
		return NULL;

	devlist = NULL;
	dv = (struct mddev_dev*)malloc(sizeof(struct mddev_dev));
	if (dv == NULL) {
		WriteHWLog(LOG_LOCAL1, LOG_DEBUG, LOG_LABEL,
			   "[%d] Fail to allocate memory.\n", __LINE__);
		return NULL;
	}
	dv->devname = (char*)replace.c_str();
	dv->disposition = 'R';
	dv->writemostly = 0;
	dv->used = 0;
	dv->next = NULL;
	*devlistend = dv;
	devlistend = &dv->next;

	dv = (struct mddev_dev*)malloc(sizeof(struct mddev_dev));
	if (dv == NULL) {
		FreeDevList(devlist);
		WriteHWLog(LOG_LOCAL1, LOG_DEBUG, LOG_LABEL,
			   "[%d] Fail to allocate memory.\n", __LINE__);
		return NULL;
	}
	dv->devname = (char*)with.c_str();
	dv->disposition = 'w';
	dv->writemostly = 0;
	dv->used = 0;
	dv->next = NULL;
	*devlistend = dv;
	devlistend = &dv->next;

	return devlist;
}

struct mddev_dev* RAIDManager::InitializeDevList(vector<string>& devNameList, int disposition)
{
	struct mddev_dev* devlist = NULL;
	struct mddev_dev** devlistend = &devlist;
	struct mddev_dev* dv = NULL;

	if (devNameList.empty())
		return NULL;

	for (size_t i = 0; i < devNameList.size(); i ++) {
		dv = (struct mddev_dev*)malloc(sizeof(struct mddev_dev));
		if (dv == NULL) {
			FreeDevList(devlist);
			WriteHWLog(LOG_LOCAL1, LOG_DEBUG, LOG_LABEL,
				   "[%d] Fail to allocate memory.\n", __LINE__);
			return NULL;
		}
		dv->devname = (char*)devNameList[i].c_str();
		dv->disposition = disposition;
		dv->writemostly = 0;
		dv->used = 0;
		dv->next = NULL;
		*devlistend = dv;
		devlistend = &dv->next;
	}

	return devlist;
}

void RAIDManager::FreeDevList(struct mddev_dev* devlist)
{
	struct mddev_dev* dv = NULL;
	for (dv = devlist; dv; dv = dv->next) {
		free(dv);
	}
	devlist = NULL;
}

int RAIDManager::OpenMDDev(const string& mddev)
{
	int fd = open_mddev((char*)mddev.c_str(), 1);
	if (fd < 0) {
		WriteHWLog(LOG_LOCAL0, LOG_ERR, LOG_LABEL,
			   "Fail to open MD device. %s\n", mddev.c_str());
	}

	return fd;
}

string RAIDManager::GenerateMDDevName(int num)
{
	return string_format("/dev/md%d", num);
}

int RAIDManager::GenerateVolumeName(string& name)
{
	int iFreeVolume = GetFreeVolumeNum();
	if (iFreeVolume < 0) {
		WriteHWLog(LOG_LOCAL0, LOG_ERR, LOG_LABEL,
		   	   "No free volume number.\n");
		return -1;
	}
	name = string_format("/mnt/VOLUME%d", iFreeVolume + 1);
	WriteHWLog(LOG_LOCAL0, LOG_INFO, LOG_LABEL,
		   "Volume Name: %s\n", name.c_str()); 
	return iFreeVolume;
}

bool RAIDManager::CreateRAID(vector<string>& vDevList, int level, string& strMDName)
{
	int ret = SUCCESS;

	do {
		int freeMD = GetFreeMDNum();
		if (freeMD < 0) {
			WriteHWLog(LOG_LOCAL0, LOG_ERR, LOG_LABEL,
			   	   "No free MD number.\n");
			return -1;
		}

		ret = CreateRAID(freeMD, strMDName, vDevList, level);
		if (ret == SUCCESS) {
			WriteHWLog(LOG_LOCAL0, LOG_INFO, LOG_LABEL,
			   	   "%s created successfully.\n", strMDName.c_str());
			return true;
		} else if (ret == CREATE_MDDEV_INUSE) {
			SetMDNum(freeMD);
			// next loop retry
		} else {
			FreeMDNum(freeMD);	
		}
	} while (ret == CREATE_MDDEV_INUSE);

	WriteHWLog(LOG_LOCAL1, LOG_DEBUG, LOG_LABEL,
		   "[%d] Create Error Code: (%d)\n", __LINE__, ret);
	return false;
}

int RAIDManager::CreateRAID(const int& mdnum, string& mddev, vector<string>& vDevList, int level)
{
	/* 1. Check mddev
		empty string -> return false; */
	if (mdnum < 0)
		return CREATE_MDNUM_ILLEGAL;

	mddev = GenerateMDDevName(mdnum);
	/*	2. Check devList
			no device (empty vector) > return false*/
	if (vDevList.empty())
		return CREATE_RAID_DEVS_NOT_ENOUGH;


	/*	3.[CS] mddev exist in m_vRAIDInfoList or not
			Yes -> return false */
	
	vector<RAIDInfo>::iterator it = IsMDDevInRAIDInfoList(mddev);
	if (it != m_vRAIDInfoList.end()) {
		WriteHWLog(LOG_LOCAL1, LOG_DEBUG, LOG_LABEL,
	   		   "[%d] Create Error Code: (%d)\n", __LINE__, CREATE_MDDEV_INUSE);

		return CREATE_MDDEV_INUSE;
	}

	/*
		[CS Start] Protect m_vRAIDDiskList
		3. Check whether devices in devList exist in m_vRAIDDiskList or not.
		Any device does not exist in m_vRAIDDiskList -> return false
		[CS End]
	*/
	if (!IsDiskExistInRAIDDiskList(vDevList)) {
		WriteHWLog(LOG_LOCAL1, LOG_DEBUG, LOG_LABEL,
			   "[%d] Create Error Code: (%d)\n", __LINE__, CREATE_RAID_DEVS_NOT_EXIST_IN_LIST);
		return CREATE_RAID_DEVS_NOT_EXIST_IN_LIST;
	}

	/*	4. InitializeShape(s, vDevList.size(), level, 512) */
	struct shape s;
	InitializeShape(s, vDevList.size(), level, 512);

	/*	5. InitializeContext(c) */
	struct context c;
	InitializeContext(c);

	/*
		6. InitializeDevList(devlist, vDevList) 
			6.1 false -> FreeDevList(devlist) -> return false
	*/
	struct mddev_dev* devlist = InitializeDevList(vDevList);
	if (devlist == NULL)
		return false;

	/*
		7. ret = Create(NULL, mddev.c_str(), "\0", NULL, vDevList.size(), devlist, &s, &c, INVALID_SECTORS)
			7.1 ret != 0 -> Write HW Log ->9
			7.2 ret == 0 -> 8
	*/
	int ret = Create(NULL, (char*)mddev.c_str(), NULL, NULL, vDevList.size(), devlist, &s, &c, INVALID_SECTORS);
	if (ret != SUCCESS) {
		WriteHWLog(LOG_LOCAL0, LOG_ERR, LOG_LABEL,
		   	   "Fail to create volume %s: (%d)\n", mddev.c_str(), ret);
		return ret;
	}

	/*	8. UpdateRAIDInfo(mddev) */
	UpdateRAIDInfo(mddev, mdnum);

	/*	9. FreeDevList(devlist)
			9.1 ret !=  0 -> return false
			9.2 ret = 0 -> return true
	*/
	FreeDevList(devlist);
	return SUCCESS;
}

int RAIDManager::CreateRAID(const string& mddev, vector<string>& vDevList, int level)
{
	/* 1. Check mddev
		empty string -> return false; */
	if (mddev.empty())
		return CREATE_MDDEV_UNSET;

	/*	2. Check devList
			no device (empty vector) > return false*/
	if (vDevList.empty())
		return CREATE_RAID_DEVS_NOT_ENOUGH;


	/*	3.[CS] mddev exist in m_vRAIDInfoList or not
			Yes -> return false */
	
	vector<RAIDInfo>::iterator it = IsMDDevInRAIDInfoList(mddev);
	if (it != m_vRAIDInfoList.end()) {
		WriteHWLog(LOG_LOCAL1, LOG_DEBUG, LOG_LABEL,
	   		   "[%d] Create Error Code: (%d)\n", __LINE__, CREATE_MDDEV_INUSE);

		return CREATE_MDDEV_INUSE;
	}

	/*
		[CS Start] Protect m_vRAIDDiskList
		3. Check whether devices in devList exist in m_vRAIDDiskList or not.
		Any device does not exist in m_vRAIDDiskList -> return false
		[CS End]
	*/
	if (!IsDiskExistInRAIDDiskList(vDevList)) {
		WriteHWLog(LOG_LOCAL1, LOG_DEBUG, LOG_LABEL,
			   "[%d] Create Error Code: (%d)\n", __LINE__, CREATE_RAID_DEVS_NOT_EXIST_IN_LIST);
		return CREATE_RAID_DEVS_NOT_EXIST_IN_LIST;
	}

	/*	4. InitializeShape(s, vDevList.size(), level, 512) */
	struct shape s;
	InitializeShape(s, vDevList.size(), level, 512);

	/*	5. InitializeContext(c) */
	struct context c;
	InitializeContext(c);

	/*
		6. InitializeDevList(devlist, vDevList) 
			6.1 false -> FreeDevList(devlist) -> return false
	*/
	struct mddev_dev* devlist = InitializeDevList(vDevList);
	if (devlist == NULL)
		return false;

	/*
		7. ret = Create(NULL, mddev.c_str(), "\0", NULL, vDevList.size(), devlist, &s, &c, INVALID_SECTORS)
			7.1 ret != 0 -> Write HW Log ->9
			7.2 ret == 0 -> 8
	*/
	int ret = Create(NULL, (char*)mddev.c_str(), NULL, NULL, vDevList.size(), devlist, &s, &c, INVALID_SECTORS);
	if (ret != SUCCESS) {
		WriteHWLog(LOG_LOCAL0, LOG_ERR, LOG_LABEL,
		   	   "Fail to create volume %s: (%d)\n", mddev.c_str(), ret);
		return ret;
	}

	/*	8. UpdateRAIDInfo(mddev) */
	UpdateRAIDInfo(mddev);

	/*	9. FreeDevList(devlist)
			9.1 ret !=  0 -> return false
			9.2 ret = 0 -> return true
	*/
	FreeDevList(devlist);
	return SUCCESS;
}

bool RAIDManager::AssembleRAID(const int uuid[4], string& strMDName)
{
	int ret = SUCCESS;

	do {
		int freeMD = GetFreeMDNum();
		if (freeMD < 0) {
			WriteHWLog(LOG_LOCAL0, LOG_ERR, LOG_LABEL,
			   	   "No free MD number.\n");
			return false;
		}

		ret = AssembleRAID(freeMD, strMDName, uuid);
		if (ret == SUCCESS) {
			unsigned char* p_uuid = (unsigned char*) uuid;
			string strUUID("");
			for (int i = 0; i < 16; i++) {
				strUUID += string_format("%02X ", p_uuid[i]);
			}
			WriteHWLog(LOG_LOCAL1, LOG_DEBUG, LOG_LABEL,
				   "%swas assembled to %s.\n", strUUID.c_str(), strMDName.c_str());
			WriteHWLog(LOG_LOCAL0, LOG_INFO, LOG_LABEL,
			   	   "%s assembled successfully.\n", strMDName.c_str());
			return true;
		} else if (ret == ASSEMBLE_MD_ALREADY_ACTIVE) {
			return true;
		} else if (ret == ASSEMBLE_MDDEV_INUSE) {
			SetMDNum(freeMD);
			// next loop retry
		} else if (ret != ASSEMBLE_RAID_DEVS_NOT_ENOUGH) {
			FreeMDNum(freeMD);
		}
	} while (ret == ASSEMBLE_MDDEV_INUSE);

	WriteHWLog(LOG_LOCAL1, LOG_DEBUG, LOG_LABEL,
		   "[%d] Assemble Error Code: (%d)\n", __LINE__, ret);
	return false;
}

int RAIDManager::AssembleRAID(const int& mdnum, string& mddev, const int uuid[4])
{
	/* 1. Check mddev
		empty string -> return false; */
	if (mdnum < 0)
		return ASSEMBLE_MDNUM_ILLEGAL;

	mddev = GenerateMDDevName(mdnum);

	/*
		2. Check uuid
			NULL -> return false
	*/
	if (uuid == NULL)
		return ASSEMBLE_INVALID_UUID;

	/*
		3. [CS] Check mddev exists in m_vRAIDInfoList or not
			Yes -> return false
	*/
	RAIDInfo info;
	vector<RAIDInfo>::iterator it = IsMDDevInRAIDInfoList(mddev, info);
	if (it != m_vRAIDInfoList.end()) {
		int ret = ASSEMBLE_MDDEV_INUSE;
		if (0 == memcmp(uuid, info.m_UUID, sizeof(info.m_UUID))) {
			ret = ASSEMBLE_MD_ALREADY_ACTIVE;
		}
		WriteHWLog(LOG_LOCAL1, LOG_DEBUG, LOG_LABEL,
			   "[%d] Assemble Error Code: (%d)\n", __LINE__, ret);
		return ret;
	}

	/*
		4. InitializeMDDevIdent(ident, 1, str_uuid)
	*/
	struct mddev_ident ident;
	InitializeMDDevIdent(ident, 1, uuid);
	//unsigned char* p_uuid = (unsigned char*)ident.uuid;

	/*
		5. ret = Assemble(NULL, mddev.c_str(), &ident, NULL, &c);
			4.1 ret != 0 -> Write HW Log -> return false
	*/
	struct context c;
	InitializeContext(c);
	int ret = Assemble(NULL, (char*)mddev.c_str(), &ident, NULL, &c);
	if (ret != SUCCESS) {
		WriteHWLog(LOG_LOCAL0, LOG_ERR, LOG_LABEL,
			   "Fail to assemble volume %s: (%d)\n", mddev.c_str(), ret);
		if ( ret != ASSEMBLE_RAID_DEVS_NOT_ENOUGH)
			return ret;
	}

	/*
		6. return UpdateRAIDInfo(mddev)
	*/
	UpdateRAIDInfo(mddev, mdnum);
	return ret;
}

int RAIDManager::AssembleRAID(const string& mddev, const int uuid[4])
{
	/*
		1. Check mddev
			empty -> return false
	*/
	if (mddev.empty())
		return ASSEMBLE_MDDEV_UNSET;

	/*
		2. Check uuid
			NULL -> return false
	*/
	if (uuid == NULL)
		return ASSEMBLE_INVALID_UUID;

	/*
		3. [CS] Check mddev exists in m_vRAIDInfoList or not
			Yes -> return false
	*/
	RAIDInfo info;
	vector<RAIDInfo>::iterator it = IsMDDevInRAIDInfoList(mddev, info);
	if (it != m_vRAIDInfoList.end()) {
		int ret = ASSEMBLE_MDDEV_INUSE;
		if (0 == memcmp(uuid, info.m_UUID, sizeof(info.m_UUID))) {
			ret = ASSEMBLE_MD_ALREADY_ACTIVE;
		}

		WriteHWLog(LOG_LOCAL1, LOG_DEBUG, LOG_LABEL,
			   "[%d] Assemble Error Code: (%d)\n", __LINE__, ret);
		return ret;
	}

	/*
		4. InitializeMDDevIdent(ident, 1, str_uuid)
	*/
	struct mddev_ident ident;
	InitializeMDDevIdent(ident, 1, uuid);
	//unsigned char* p_uuid = (unsigned char*)ident.uuid;

	/*
		5. ret = Assemble(NULL, mddev.c_str(), &ident, NULL, &c);
			4.1 ret != 0 -> Write HW Log -> return false
	*/
	struct context c;
	InitializeContext(c);
	int ret = Assemble(NULL, (char*)mddev.c_str(), &ident, NULL, &c);
	if (ret != SUCCESS) {
		WriteHWLog(LOG_LOCAL0, LOG_ERR, LOG_LABEL,
			   "Fail to assemble volume %s: (%d)\n", mddev.c_str(), ret);
		if ( ret != ASSEMBLE_RAID_DEVS_NOT_ENOUGH)
			return ret;
	}

	/*
		6. return UpdateRAIDInfo(mddev)
	*/
	UpdateRAIDInfo(mddev);
	return ret;
}

bool RAIDManager::AssembleRAID(vector<string>& vDevList, string& strMDName)
{
	int ret = SUCCESS;

	do {
		int freeMD = GetFreeMDNum();
		if (freeMD < 0) {
			WriteHWLog(LOG_LOCAL0, LOG_ERR, LOG_LABEL,
			   	   "No free MD number.\n");
			return false;
		}

		ret = AssembleRAID(freeMD, strMDName, vDevList);
		if (ret == SUCCESS) {
			string strDevList("");
			for (size_t i = 0; i < vDevList.size(); i++) {
				strDevList += vDevList[i];
			}
			WriteHWLog(LOG_LOCAL1, LOG_DEBUG, LOG_LABEL,
				   "%swas assembled to %s.\n", strDevList.c_str(), strMDName.c_str());
			WriteHWLog(LOG_LOCAL0, LOG_INFO, LOG_LABEL,
			   	   "%s assembled successfully.\n", strMDName.c_str());
			return true;
		} else if (ret == ASSEMBLE_MDDEV_INUSE) {
			SetMDNum(freeMD);
			// next loop retry
		} else if (ret != ASSEMBLE_RAID_DEVS_NOT_ENOUGH) {
			FreeMDNum(freeMD);
		}
	} while (ret == ASSEMBLE_MDDEV_INUSE);

	WriteHWLog(LOG_LOCAL1, LOG_DEBUG, LOG_LABEL,
		   "[%d] Assemble Error Code: (%d)\n", __LINE__, ret);
	return false;
}

int RAIDManager::AssembleRAID(const int& mdnum, string& mddev, vector<string>& vDevList)
{
	/*
		1. Check mddev
			empty -> return false
	*/
	if (mdnum < 0)
		return ASSEMBLE_MDNUM_ILLEGAL;

	mddev = GenerateMDDevName(mdnum);

	/*
		2. Check vDevList 
			empty -> return false
	*/
	if (vDevList.empty())
		return ASSEMBLE_NO_DEVS_FOR_MD;

	/*
		3. [CS] Check mddev exists in m_vRAIDInfoList or not
			Yes -> return false
		
		FIXME: We only check that md device node exist or not,
			but maybe we also need to check whether the disks is already
			belong to this md device.
	*/
	vector<RAIDInfo>::iterator it = IsMDDevInRAIDInfoList(mddev);
	if (it != m_vRAIDInfoList.end()) {
		WriteHWLog(LOG_LOCAL1, LOG_DEBUG, LOG_LABEL,
			   "[%d] Assemble Error Code: (%d)\n", __LINE__, ASSEMBLE_MDDEV_INUSE);

		return ASSEMBLE_MDDEV_INUSE;
	}

	/*
		[CS Start] Protect m_vRAIDDiskList
		4. Check whether devices in devList exist in m_vRAIDDiskList or not.
			Any device does not exist in m_vRAIDDiskList -> return false
		[CS End]
	*/
	if (!IsDiskExistInRAIDDiskList(vDevList)) {
		WriteHWLog(LOG_LOCAL1, LOG_DEBUG, LOG_LABEL,
			   "[%d] Assemble Error Code: (%d)\n", __LINE__, ASSEMBLE_RAID_DEVS_NOT_EXIST_IN_LIST);
		return ASSEMBLE_RAID_DEVS_NOT_EXIST_IN_LIST;
	}

	/*
		5. InitializeMDDevIdent(ident, 0, "");
	*/
	struct mddev_ident ident;
	InitializeMDDevIdent(ident, 0, NULL);

	/*
		6. InitalizeDevList(devlist, vDevList);
			6.1 false -> FreeDevlist(devlist) -> return false
	*/
	struct mddev_dev* devlist = InitializeDevList(vDevList);
	if(devlist == NULL) {
		return ASSEMBLE_INITIALIZE_DEV_LIST_FAIL;
	}

	/*
		7. ret = Assemble(NULL, mddev.c_str(), &ident, devlist, &c);
			7.1 ret != 0 -> Write HW Log
		8. FreeDevList(devlist)
	*/
	struct context c;
	InitializeContext(c);
	int ret = Assemble(NULL, (char*)mddev.c_str(), &ident, devlist, &c);
	FreeDevList(devlist);
	if (ret != 0) {
		WriteHWLog(LOG_LOCAL0, LOG_ERR, LOG_LABEL,
			   "Fail to assemble volume %s: (%d)\n", mddev.c_str(), ret);
		if ( ret != ASSEMBLE_RAID_DEVS_NOT_ENOUGH)
			return ret;
	}

	/*
		9.
		if (ret != 0)
			return false;
		else
			return UpdateRAIDInfo(mddev)
	*/
	UpdateRAIDInfo(mddev, mdnum);
	return ret;
}

int RAIDManager::AssembleRAID(const string& mddev, vector<string>& vDevList)
{
	/*
		1. Check mddev
			empty -> return false
	*/
	if (mddev.empty())
		return ASSEMBLE_MDDEV_UNSET;

	/*
		2. Check vDevList 
			empty -> return false
	*/
	if (vDevList.empty())
		return ASSEMBLE_NO_DEVS_FOR_MD;

	/*
		3. [CS] Check mddev exists in m_vRAIDInfoList or not
			Yes -> return false
		
		FIXME: We only check that md device node exist or not,
			but maybe we also need to check whether the disks is already
			belong to this md device.
	*/
	vector<RAIDInfo>::iterator it = IsMDDevInRAIDInfoList(mddev);
	if (it != m_vRAIDInfoList.end()) {
		WriteHWLog(LOG_LOCAL1, LOG_DEBUG, LOG_LABEL,
			   "[%d] Assemble Error Code: (%d)\n", __LINE__, ASSEMBLE_MDDEV_INUSE);

		return ASSEMBLE_MDDEV_INUSE;
	}

	/*
		[CS Start] Protect m_vRAIDDiskList
		4. Check whether devices in devList exist in m_vRAIDDiskList or not.
			Any device does not exist in m_vRAIDDiskList -> return false
		[CS End]
	*/
	if (!IsDiskExistInRAIDDiskList(vDevList)) {
		WriteHWLog(LOG_LOCAL1, LOG_DEBUG, LOG_LABEL,
			   "[%d] Assemble Error Code: (%d)\n", __LINE__, ASSEMBLE_RAID_DEVS_NOT_EXIST_IN_LIST);
		return ASSEMBLE_RAID_DEVS_NOT_EXIST_IN_LIST;
	}

	/*
		5. InitializeMDDevIdent(ident, 0, "");
	*/
	struct mddev_ident ident;
	InitializeMDDevIdent(ident, 0, NULL);

	/*
		6. InitalizeDevList(devlist, vDevList);
			6.1 false -> FreeDevlist(devlist) -> return false
	*/
	struct mddev_dev* devlist = InitializeDevList(vDevList);
	if(devlist == NULL) {
		return ASSEMBLE_INITIALIZE_DEV_LIST_FAIL;
	}

	/*
		7. ret = Assemble(NULL, mddev.c_str(), &ident, devlist, &c);
			7.1 ret != 0 -> Write HW Log
		8. FreeDevList(devlist)
	*/
	struct context c;
	InitializeContext(c);
	int ret = Assemble(NULL, (char*)mddev.c_str(), &ident, devlist, &c);
	FreeDevList(devlist);
	if (ret != 0) {
		WriteHWLog(LOG_LOCAL0, LOG_ERR, LOG_LABEL,
			   "Fail to assemble volume %s: (%d)\n", mddev.c_str(), ret);
		if ( ret != ASSEMBLE_RAID_DEVS_NOT_ENOUGH)
			return ret;
	}

	/*
		9.
		if (ret != 0)
			return false;
		else
			return UpdateRAIDInfo(mddev)
	*/
	UpdateRAIDInfo(mddev);
	return ret;
}

bool RAIDManager::ManageRAIDSubdevs(const string& mddev, vector<string>& vDevList, int operation)
{
	/*
		1. Check mddev
			empty -> return false
	*/
	if (mddev.empty())
		return false;

	/*
		2. Check vDevList 
			empty -> return false
	*/
	if (vDevList.empty())
		return false;

	/*
		3. [CS] Check mddev exists in m_vRAIDInfoList or not
			No -> return false
	*/
	vector<RAIDInfo>::iterator it = IsMDDevInRAIDInfoList(mddev);
	if (it == m_vRAIDInfoList.end()) {
		WriteHWLog(LOG_LOCAL0, LOG_ERR, LOG_LABEL,
			   "%s doesn't exist.\n", mddev.c_str());
		return false;
	}

/*
		[CS Start] Protect m_vRAIDDiskList
		4. Check whether devices in devList exist in m_vRAIDDiskList or not.
			Any device does not exist in m_vRAIDDiskList -> return false
		[CS End]
*/
	if (!IsDiskExistInRAIDDiskList(vDevList)) {
		WriteHWLog(LOG_LOCAL0, LOG_ERR, LOG_LABEL,
			   "Some disks doesn't belong to %s.\n", mddev.c_str());
		return false;
	}

/*
		5. fd = open_mddev(mddev.c_str(), 1)
			4.1 fd < 0 -> return false
			4.2 other ->6
*/
	int fd = OpenMDDev(mddev);
	if (fd < 0) 
		return false;

/*
		6.
			if (disposition == 'R')
				if vDevList.size != 2 close(fd);return false;
				InitializeDevListForReplace(devlist, vDevList[0], vDevList[1])
			else
				InitializeDevList(devlist, vDevList, operation);
			6.1 false -> FreeDevlist(devlist) -> return false
*/
	struct mddev_dev* devlist = NULL;
	switch (operation) {
	case 'R':
		if (vDevList.size() != 2) {
			close(fd);
			WriteHWLog(LOG_LOCAL1, LOG_DEBUG, LOG_LABEL,
				   "Replace can only accetp two devices. One for replace and another for new.\n");
			return false;
		}

		if (NULL == (devlist = InitializeDevList(vDevList[0], vDevList[1]))) {
			return false;
		}
		break;
	default:
		if (NULL == (devlist = InitializeDevList(vDevList, operation))) {
			return false;
		}
	}

/*
		7. InitializeContext(c)
		8. ret = Manage_subdevs(mddev.c_str(), fd, devlist, c.verbose, 0, NULL, c.force);
			ret != 0 -> Write HW Log
		9. close(fd)
		10. FreeDevList(devlist) 
*/
	struct context c;
	InitializeContext(c);
	int ret = Manage_subdevs((char*)mddev.c_str(), fd, devlist, c.verbose, 0, NULL, c.force);
	close(fd);
	FreeDevList(devlist);

/*
		11.
		if (ret != 0)
			return false;
		else
			return UpdateRAIDInfo(mddev)
	*/
	if (ret != 0) {
		WriteHWLog(LOG_LOCAL0, LOG_ERR, LOG_LABEL,
			   "Fail to manage volume %s: (%d)\n", mddev.c_str(), ret);
		return false;
	}

	UpdateRAIDInfo(mddev);
	return SUCCESS;
}

bool RAIDManager::RemoveMDDisks(const string& mddev, vector<string>& vDevList)
{
	return ManageRAIDSubdevs(mddev, vDevList, 'r');
}

bool RAIDManager::MarkFaultyMDDisks(const string& mddev, vector<string>& vDevList)
{
	return ManageRAIDSubdevs(mddev, vDevList, 'f');
}

bool RAIDManager::AddMDDisks(const string& mddev, vector<string>& vDevList)
{
	return ManageRAIDSubdevs(mddev, vDevList, 'a');
}

bool RAIDManager::ReaddMDDisks(const string& mddev, vector<string>& vDevList)
{
	return ManageRAIDSubdevs(mddev, vDevList, 'A');
}

bool RAIDManager::ReplaceMDDisk(const string& mddev, const string& replace, const string& with)
{
	if (replace.empty() || with.empty())
		return false;

	vector<string> vDevList;
	vDevList.push_back(replace);
	vDevList.push_back(with);

	return ManageRAIDSubdevs(mddev, vDevList, 'R');
}

bool RAIDManager::StopRAID(const string& mddev)
{
	/*
		1. Check mddev
			empty -> return false
	*/
	if (mddev.empty())
		return false;

	/*
		2. If mddev exist in m_vRAIdInfoList
			No -> return true;
			Yes -> 3
	*/
	vector<RAIDInfo>::iterator it = IsMDDevInRAIDInfoList(mddev);
	if (it == m_vRAIDInfoList.end()) {
		WriteHWLog(LOG_LOCAL0, LOG_WARNING, LOG_LABEL,
			   "%s doesn't exist.\n", mddev.c_str());
		return true;
	}

	/* Do nothing if the MD device is formatting */
	int stat = WRITE_INODE_TABLES_UNKNOWN, progress = -1;
	if (GetFormatProgress(mddev, stat, progress)) {
		WriteHWLog(LOG_LOCAL0, LOG_INFO, LOG_LABEL,
			   "Fail to stop volume %s. It is formatting. [%d/%d%%]\n", mddev.c_str(), stat, progress);
		return false;
	}

	Unmount(mddev);

	/*
		3. fd = open_mddev(mddev.c_str(), 1);
			fd < 0 -> return false;
	*/
	int fd = OpenMDDev(mddev);
	if (fd < 0) 
		return false;

	/*
		4. ret = Manage_stop(mddev.c_str(), fd, 1, 0);
			ret != 0 -> close(fd) -> close(fd);return false;
			ret == 0 -> 5
		5. close(fd)
	*/
	struct context c;
	int ret = SUCCESS;

	InitializeContext(c);
	ret = Manage_stop((char*)mddev.c_str(), fd, c.verbose, 0);
	close(fd);

	if (ret != SUCCESS) {
		WriteHWLog(LOG_LOCAL0, LOG_ERR, LOG_LABEL,
			   "Fail to stop volume %s: %d\n", mddev.c_str(), ret);
		return false;
	}

	/*
		6. Remove mddev from m_vRAIDInfoList
	*/
	CriticalSectionLock cs(&m_csRAIDInfoList);
	// If MD device is stopped, we can free MD number immediately.
	for (size_t i = 0; i < it->m_vDiskList.size(); i++) {
		CriticalSectionLock csSymLinkTable(&m_csSymLinkTable);
		m_mapSymLinkTable[it->m_vDiskList[i].m_strDevName].m_strMDDev = "";
		m_mapSymLinkTable[it->m_vDiskList[i].m_strDevName].Dump();
	}

	FreeMDNum(it->m_iMDNum);
	m_vRAIDInfoList.erase(it);
	return true;
}

bool RAIDManager::DeleteRAID(const string& mddev)
{
	/*
	   Keep a copy for clearing disk's superblock later.
	   And we also can bypass following procedures if
	   the md device does not exist in the list.
	 */
	RAIDInfo info;
	vector<RAIDInfo>::iterator it = IsMDDevInRAIDInfoList(mddev, info);
	if (it == m_vRAIDInfoList.end()) {
		WriteHWLog(LOG_LOCAL0, LOG_INFO, LOG_LABEL,
			   "Delete volume %s\n", mddev.c_str());
		return true;
	}

	if (!StopRAID(mddev))
		return false;
#if 0
	/*
		1. Check mddev
			empty -> return false
	*/
	if (mddev.empty())
		return false;

	/*
		2. If mddev exist in m_vRAIdInfoList
			No -> return true;
			Yes -> 3
	*/
	vector<RAIDInfo>::iterator it = IsMDDevInRAIDInfoList(mddev);
	if (it == m_vRAIDInfoList.end()) {
		WriteHWLog(LOG_LOCAL0, LOG_INFO, LOG_LABEL,
			   "Delete volume %s\n", mddev.c_str());
		return true;
	}

	/* Do nothing if the MD device is formatting */
	int stat = WRITE_INODE_TABLES_UNKNOWN, progress = -1;
	if (GetFormatProgress(mddev, stat, progress)) {
		WriteHWLog(LOG_LOCAL0, LOG_INFO, LOG_LABEL,
			   "Fail to delete volume %s. It is formatting. [%d/%d%%]\n", mddev.c_str(), stat, progress);
		return false;
	}

	Unmount(mddev);

	/*
		3. fd = open_mddev(mddev.c_str(), 1);
			fd < 0 -> return false;
	*/
	int fd = OpenMDDev(mddev);
	if (fd < 0) 
		return false;

	/*
		4. ret = Manage_stop(mddev.c_str(), fd, 1, 0);
			ret != 0 -> close(fd) -> close(fd);return false;
			ret == 0 -> 5
		5. close(fd)
	*/
	struct context c;
	int ret = SUCCESS;

	InitializeContext(c);
	ret = Manage_stop((char*)mddev.c_str(), fd, c.verbose, 0);
	close(fd);

	if (ret != SUCCESS) {
		WriteHWLog(LOG_LOCAL0, LOG_ERR, LOG_LABEL,
			   "Fail to stop volume %s: %d\n", mddev.c_str(), ret);
		return false;
	}
	/*
		6. Remove mddev from m_vRAIDInfoList, keep a RAID disks list copy for later use.
	*/
	RAIDInfo info = *it;
#ifdef NUUO
	m_csRAIDInfoList.Lock();
#endif
	// If MD device is stopped, we can free MD number immediately.
	FreeMDNum(info.m_iMDNum);
	m_vRAIDInfoList.erase(it);
#ifdef NUUO
	m_csRAIDInfoList.Unlock();
#endif
#endif

	/*
		7. InitializeContext(c)
		8. For all raid disks in mddev: ret = Kill(devname, NULL, c.force, c.verbose, 0);
			Write HW Log if ret != 0 
	*/
	struct context c;
	int ret = SUCCESS;

	InitializeContext(c);
	for (size_t i = 0; i < info.m_vDiskList.size(); i++) {
		ret = Kill((char*)info.m_vDiskList[i].m_strDevName.c_str(), NULL, c.force, c.verbose, 0);
		if (ret != SUCCESS) {
			WriteHWLog(LOG_LOCAL1, LOG_DEBUG, LOG_LABEL,
				   "Kill Error Code %s: %d\n", info.m_vDiskList[i].m_strDevName.c_str(), ret);
		}

		/*
			Check for unknown superblock type which
			cannot be cleared by Kill().
			Hope that this special case won't happen.
		*/
		struct examine_result result;
		int ret = SUCCESS;
			
		/* force to clear superblock */
		if (IsDiskHaveMDSuperBlock(info.m_vDiskList[i].m_strDevName.c_str(), result, ret)) {
			WriteHWLog(LOG_LOCAL0, LOG_WARNING, LOG_LABEL,
				   "Force to clear superblock of %s. [%d]\n",
				   info.m_vDiskList[i].m_strDevName.c_str(), ret);

			string cmd = string_format("dd if=/dev/zero of=%s bs=512 count=1", info.m_vDiskList[i].m_strDevName.c_str());
			system(cmd.c_str());
		}

	//	CriticalSectionLock csSymLinkTable(&m_csSymLinkTable);
	//	m_mapSymLinkTable[info.m_vDiskList[i].m_strDevName].m_strMDDev = "";
		WriteHWLog(LOG_LOCAL1, LOG_WARNING, LOG_LABEL,
			   "%s's superblock is cleared\n", info.m_vDiskList[i].m_strDevName.c_str());
	}

	return true;
}

bool RAIDManager::GetRAIDInfo(const string& mddev, RAIDInfo& info)
{
	/*
		1. Check mddev
			empty -> return false
	*/
	if (mddev.empty())
		return false;
	/*
		[CS Start] protect m_vRAIDInfoList
		2. Look for mddev in m_vRAIDInfoList
			2.1 Exist -> Copy to info -> return true (found)
		[CS End]
		3. return false (not found)
	*/
	CriticalSectionLock cs(&m_csRAIDInfoList);
	vector<RAIDInfo>::iterator it = m_vRAIDInfoList.begin();
	while (it != m_vRAIDInfoList.end()) {
		if (*it == mddev) {
			break;
		}

		it++;
	}

	if (it == m_vRAIDInfoList.end()) {
		WriteHWLog(LOG_LOCAL1, LOG_DEBUG, LOG_LABEL,
			   "%s doesn't exist.\n", mddev.c_str());
		return false;
	}

	for (size_t i = 0; i < it->m_vDiskList.size(); i++) {
		CriticalSectionLock csSymLinkTable(&m_csSymLinkTable);
		it->m_vDiskList[i].m_miscInfo = m_mapSymLinkTable[it->m_vDiskList[i].m_strDevName]; 
	}
	info = *it;
	return true;
}

void RAIDManager::GetRAIDInfo(vector<RAIDInfo>& list)
{
	list.clear();

	CriticalSectionLock cs(&m_csRAIDInfoList);
	vector<RAIDInfo>::iterator it = m_vRAIDInfoList.begin();
	while (it != m_vRAIDInfoList.end()) {
		for (size_t i = 0; i < it->m_vDiskList.size(); i++) {
			CriticalSectionLock csSymLinkTable(&m_csSymLinkTable);
			it->m_vDiskList[i].m_miscInfo = m_mapSymLinkTable[it->m_vDiskList[i].m_strDevName]; 
		}

		list.push_back(*it);
		it++;
	}
}

void RAIDManager::GetDisksInfo(vector<RAIDDiskInfo> &list)
{
	list.clear();

	CriticalSectionLock cs(&m_csRAIDDiskList);
	vector<RAIDDiskInfo>::iterator it = m_vRAIDDiskList.begin();
	while(it != m_vRAIDDiskList.end()) {
		RAIDDiskInfo info = *it;
		CriticalSectionLock csSymLinkTable(&m_csSymLinkTable);
		info.m_miscInfo = m_mapSymLinkTable[it->m_strDevName];
		info.Dump();
		list.push_back(info);
		it++;
	}
}

bool RAIDManager::GetDisksInfo(const string& dev, RAIDDiskInfo &info)
{
	if (dev.empty()) {
		return false;
	}

	string strDevNode = GetDeviceNodeBySymLink(dev);

	CriticalSectionLock csRAIDDiskList(&m_csRAIDDiskList);
	vector<RAIDDiskInfo>::iterator it = m_vRAIDDiskList.begin();
	while(it != m_vRAIDDiskList.end()) {
		if (*it == strDevNode) {
			info = *it;
			CriticalSectionLock csSymLinkTable(&m_csSymLinkTable);
			info.m_miscInfo = m_mapSymLinkTable[strDevNode]; 
			return true;
		}
		it++;
	}

	return false;
}

bool RAIDManager::Format(const string& mddev)
{
	if (mddev.empty()) {
		WriteHWLog(LOG_LOCAL0, LOG_ERR, LOG_LABEL,
			   "[%d] Unknown MD device.\n", __LINE__);
		return false;
	}

	RAIDInfo info;
	if (IsMDDevInRAIDInfoList(mddev, info) == m_vRAIDInfoList.end()) {
		WriteHWLog(LOG_LOCAL0, LOG_INFO, LOG_LABEL,
			   "%s doesn't exist.\n", mddev.c_str());
		return false;
	}

	if (info.m_fsMgr.get() == NULL) {
		WriteHWLog(LOG_LOCAL0, LOG_ERR, LOG_LABEL,
			   "FilesystemManager is not initialized for %s\n",
			   mddev.c_str());
		return false;
	}

	int num = GetFreeVolumeNum();
	if (num < 0) {
		WriteHWLog(LOG_LOCAL0, LOG_ERR, LOG_LABEL,
			   "Exceed maximal volume limitation.\n");
		return false;
	}

	info.m_fsMgr->SetVolumeNum(num);
#ifdef NUUO
	info.m_fsMgr->CreateThread();
#endif

	return true;
}

bool RAIDManager::Mount(const string& mddev)
{
	if (mddev.empty()) {
		WriteHWLog(LOG_LOCAL0, LOG_ERR, LOG_LABEL,
			   "[%d] Unknown MD device.\n", __LINE__);
		return false;
	}

	RAIDInfo info;
	if (IsMDDevInRAIDInfoList(mddev, info) == m_vRAIDInfoList.end()) {
		WriteHWLog(LOG_LOCAL0, LOG_INFO, LOG_LABEL,
			   "%s doesn't exist.\n", mddev.c_str());
		return false;
	}

	if (info.m_fsMgr.get() == NULL) {
		WriteHWLog(LOG_LOCAL0, LOG_ERR, LOG_LABEL,
			   "FilesystemManager is not initialized for %s\n",
			   mddev.c_str());
		return false;
	}

	if (!info.m_fsMgr->IsFormated()) {
		WriteHWLog(LOG_LOCAL0, LOG_ERR, LOG_LABEL,
			   "%s is not formated.\n",
			   mddev.c_str());
		return false;
	}

	int num = -1;
	if (!info.m_fsMgr->IsMounted(num)) {
		string strMountPoint;
		if (num < 0) {
			num = GetFreeVolumeNum();
		} else {
			num = GetFormerVolumeNum(num);
		}

		if (num < 0) {
			WriteHWLog(LOG_LOCAL0, LOG_ERR, LOG_LABEL,
				   "Exceed maximal volume limitation.\n");
			return false;
		}
		info.m_fsMgr->SetVolumeNum(num);
		info.m_fsMgr->IsMounted(strMountPoint); // Get mount point name
		if (!info.m_fsMgr->Mount(strMountPoint)) {
			FreeVolumeNum(num);
			return false;
		}
	}

	// In case of some necessary files and folders do not exist.
	info.m_fsMgr->GenerateUUIDFile();
	info.m_fsMgr->CreateDefaultFolders();

	return true;
}

bool RAIDManager::Unmount(const string& mddev)
{
	if (mddev.empty()) {
		WriteHWLog(LOG_LOCAL0, LOG_ERR, LOG_LABEL,
			   "[%d] Unknown MD device.\n", __LINE__);
		return false;
	}

	RAIDInfo info;
	if (IsMDDevInRAIDInfoList(mddev, info) == m_vRAIDInfoList.end()) {
		WriteHWLog(LOG_LOCAL0, LOG_INFO, LOG_LABEL,
			   "%s doesn't exist.\n", mddev.c_str());
		return false;
	}

	if (info.m_fsMgr.get() == NULL) {
		WriteHWLog(LOG_LOCAL0, LOG_ERR, LOG_LABEL,
			   "FilesystemManager is not initialized for %s\n",
			   mddev.c_str());
		return false;
	}

	int num = -1;
	if (info.m_fsMgr->IsMounted(num)) {
		if (info.m_fsMgr->Unmount()) {
			FreeVolumeNum(num);
			return true;
		} else {
			// FIXME: Should I free volume num...
			FreeVolumeNum(num);
			return false;
		}
	}

	return true;
}

bool RAIDManager::GetFormatProgress(const string& mddev,
				    int& stat, int& progress)
{
	if (mddev.empty()) {
		WriteHWLog(LOG_LOCAL0, LOG_ERR, LOG_LABEL,
			   "[%d] Unknown MD device.\n", __LINE__);
		return false;
	}

	RAIDInfo info;
	if (IsMDDevInRAIDInfoList(mddev, info) == m_vRAIDInfoList.end()) {
		WriteHWLog(LOG_LOCAL0, LOG_INFO, LOG_LABEL,
			   "%s doesn't exist.\n", mddev.c_str());
		return false;
	}

	if (info.m_fsMgr.get() == NULL) {
		WriteHWLog(LOG_LOCAL0, LOG_ERR, LOG_LABEL,
			   "FilesystemManager is not initialized for %s\n",
			   mddev.c_str());
		return false;
	}

	return info.m_fsMgr->IsFormating(progress, stat);
}

bool RAIDManager::IsMounted(const string& mddev, int &num)
{
	if (mddev.empty()) {
		WriteHWLog(LOG_LOCAL0, LOG_ERR, LOG_LABEL,
			   "[%d] Unknown MD device.\n", __LINE__);
		return false;
	}

	RAIDInfo info;
	if (IsMDDevInRAIDInfoList(mddev, info) == m_vRAIDInfoList.end()) {
		WriteHWLog(LOG_LOCAL0, LOG_INFO, LOG_LABEL,
			   "%s doesn't exist.\n", mddev.c_str());
		return false;
	}

	if (info.m_fsMgr.get() == NULL) {
		WriteHWLog(LOG_LOCAL0, LOG_ERR, LOG_LABEL,
			   "FilesystemManager is not initialized for %s\n",
			   mddev.c_str());
		return false;
	}

	return info.m_fsMgr->IsMounted(num);
}

bool RAIDManager::IsFormated(const string& mddev)
{
	if (mddev.empty()) {
		WriteHWLog(LOG_LOCAL0, LOG_ERR, LOG_LABEL,
			   "[%d] Unknown MD device.\n", __LINE__);
		return false;
	}

	RAIDInfo info;
	if (IsMDDevInRAIDInfoList(mddev, info) == m_vRAIDInfoList.end()) {
		WriteHWLog(LOG_LOCAL0, LOG_INFO, LOG_LABEL,
			   "%s doesn't exist.\n", mddev.c_str());
		return false;
	}

	if (info.m_fsMgr.get() == NULL) {
		WriteHWLog(LOG_LOCAL0, LOG_ERR, LOG_LABEL,
			   "FilesystemManager is not initialized for %s\n",
			   mddev.c_str());
		return false;
	}
	
	return info.m_fsMgr->IsFormated();
}

void RAIDManager::Dump()
{
	printf("Used MD Number: ");
	for (int i = 0; i < 128; i ++)
		if (m_bUsedMD[i])
			printf("%d ", i);
	printf("\n");
	printf("Used Volume Number: ");
	for (int i = 0; i < 128; i ++)
		if (m_bUsedVolume[i])
			printf("%d ", i + 1);
	printf("\n");
	for (size_t i = 0; i < m_vRAIDDiskList.size(); i++) {
		m_vRAIDDiskList[i].Dump();
	}
}

string RAIDManager::GetDeviceNodeBySymLink(const string& symlink)
{
	/* Return symlink directly if we cannot found corresponding device node. */
	struct stat s;
	if (lstat(symlink.c_str(), &s) >= 0) {
		if (S_ISLNK(s.st_mode) == 1) {
			char buf[128];
			int len = 0;
			if ((len = readlink(symlink.c_str(), buf, sizeof(buf) - 1)) >= 0) {
				string strDevNode = "/dev/";
				buf[len] = '\0';
				strDevNode += buf;
				return strDevNode;
			}
		}
	}

	return symlink;
}

void RAIDManager::ThreadProc()
{
	uint32_t uMessage = (uint32_t) eTC_STOP;

	while (1) {
		if (CheckRequest(&uMessage)) {
			switch (uMessage) {
			case eTC_STOP:
				Reply(0);
				return;
			default:
				Reply(-1);
				break;
			}
		}

		/*
		 * TODO: Monitor
		 */

		/*
		 * Check current disk status;
		 */
		m_csDiskProfiles.Lock();
		map::<string, DiskProfile>::iterator it_disk = m_mapDiskProfiles.begin();
		while (it_disk != m_mapDiskProfiles.end()) {
			struct udev *udev = NULL;
			struct udev_device *dev = NULL;
			udev = udev_new();
			if (!udev) {
				printf("can't create udev\n");
				return;
			}

			dev = udev_device_new_from_subsystem_sysname(udev, "block", it_disk->first.c_str());
			if (NULL == dev) {
				it_disk = m_mapDiskProfiles.erase(it_disk);
			} else {
				DiskProfile profile(it_disk->first);
				if (profile != it_disk->second) {
					ASSERT2(0, "[%s] System name is the same, but other disk information is different.\n");
					it_disk->second = profile;
				}

				it disk++;
			}

			udev_device_unref(dev);
			udev_unref(udev);
		}

		m_csMDProfiles.Lock();

		map::<string, MDProfile>::iterator it_md = m_mapMDProfiles.begin();
		while (it_md != m_mapMDProfiles.end()) {
			/*
			 * Check MD exists or not.
			 */
			struct udev *udev = NULL;
			struct udev_device *dev = NULL;
			udev = udev_new();
			if (!udev) {
				printf("can't create udev\n");
				return;
			}

			dev = udev_device_new_from_subsystem_sysname(udev, "block", it_md->first.c_str());
			if (NULL == dev) {
				FreeMDNum(it->m_iMDNum);

				it->m_fsMgr->Unmount();
				FreeVolumeNum(it->m_fsMgr->GetVolumeNum());

				/*
				 * TODO: Need to let disk member call ReadMDStat to
				 * update its m_strMDDev?
				 */

				it_md = m_mapDiskProfiles.erase(it_md);
				udev_unref(udev);
				continue;
			}

			udev_device_unref(dev);
			udev_unref(udev);

			it_md->ReadMDStat(); /* Update new members */
			vector<string>::iterator it_member = it_md->m_vMembers.begin();
			while (it_member != it_md->m_vMembers.end()) {
				/*
				 * Don't use m_mapDiskProfiles[*it_member],
				 * It the element does not exist, it will create an empty one,
				 * and the result will always has the disk in the disk list.
				 *
				 * We use this to handle the MD device created by iSCSI disk.
				 * iSCSI disk lost connection won't reflect on the MD device.
				 * So, we have to do the this check manually remove the missing
				 * disks in MD's member list.
				 */

				it_disk = m_mapDiskProfiles.find(*it_member);
				if (it_disk == m_mapDiskProfiles.end()) {
					it_member = it_md->m_vMembers.erase(it_member);
				} else {
					it_member++;
				}
			}

			/*
			 * TODO: Check potential malfunctional stauts of MD device and
			 * do corresponding actions to protect the volume.
			 *
			 * Unmount, Stop MD Device, or .....
			 */
			 

			it_md++;
		}

		m_csMDProfiles.Unlock();

		m_csDiskProfiles.Unlock();
		

		if (m_pNotifyChange == NULL) {
			SleepMS(RAIDMANAGER_MONITOR_INTERVAL);
		} else {
			m_pNotifyChange->timedwait(RAIDMANAGER_MONITOR_INTERVAL);
		}
	}
}

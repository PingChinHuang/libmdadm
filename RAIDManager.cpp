#include "RAIDManager.h"

#ifdef NUUO
#include "common/file.h"
#include "common/string.h"
#include "common/directory_traverse.h"
#include "common/system.h"
#include "common/time.h"
#include "debugMsg/Debug.h"
#endif

#define LOG_LABEL "RAIDManager"
#define RAIDMANAGER_MONITOR_INTERVAL 10000 /* ms */

RAIDManager::RAIDManager()
{
	for (int i = 0; i < 128; i++)
		m_bUsedMD[i] = false;
	for (int i = 0; i < 128; i++)
		m_bUsedVolume[i] = false;

	CriticalSectionLock cs_DiskProfiles(&m_csDiskProfiles);
	CriticalSectionLock cs_MDProfiles(&m_csMDProfiles);
	Initialize();

	m_semAssemble.Post();

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
			if (profile.m_strSymLink != "nuuo_satadom") 
				m_mapDiskProfiles[strSysName] = profile;
			break;
		}
		case 9: /* MD */
		{
			MDProfile profile(strSysName);
			m_mapMDProfiles[strSysName] = profile;
			SetMDNum(m_mapMDProfiles[strSysName].m_iMDNum);

			if (NULL == m_mapMDProfiles[strSysName].m_fsMgr.get()) /* Retry */
				m_mapMDProfiles[strSysName].InitializeFSManager();
			
			if (m_mapMDProfiles[strSysName].m_fsMgr.get()) {
				if (!m_mapMDProfiles[strSysName].m_fsMgr->IsInitialized()) {
					if (!m_mapMDProfiles[strSysName].m_fsMgr->Initialize()) /* Retry */
					   continue;
				}

				int num = m_mapMDProfiles[strSysName].m_fsMgr->GetVolumeNum();
				if (num < 0) {
					/* Pre-allocated a volume number to this MD. */
					m_mapMDProfiles[strSysName].m_fsMgr->SetVolumeNum(GetFreeVolumeNum());
				} else
					SetVolumeNum(m_mapMDProfiles[strSysName].m_fsMgr->GetVolumeNum());
			} else {
				printf("[%s] FilesystemManager initialization retried failed.\n",
					   strSysName.c_str());
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

	WriteHWLog(LOG_LOCAL0, LOG_ERR, LOG_LABEL,
			   "No free MD number.\n");
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

int RAIDManager::GetFreeVolumeNum()
{
	CriticalSectionLock cs_md(&m_csUsedVolume);
	for (int i = 0 ; i < 128; i++) {
		if (!m_bUsedVolume[i]) {
			m_bUsedVolume[i] = true; // Pre-allocated, if you don't use it or any error happened, you have to free.
			return i;
		}
	}

	WriteHWLog(LOG_LOCAL0, LOG_ERR, LOG_LABEL,
			   "Exceed maximal volume limitation.\n");
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

int RAIDManager::QueryMDSuperBlockInDisk(const string& dev_path, examine_result &result)
{
	vector<string> vDevList;
	struct mddev_dev* devlist = NULL;
	struct context c;
	int err = 0;

	vDevList.push_back(dev_path);
	if (NULL == (devlist = InitializeDevList(vDevList))) {
		err = EXAMINE_MEM_ALLOC_FAIL;
		return err;
	}

	InitializeContext(c);
	err = Examine_ToResult(devlist, &c, NULL, &result);
	FreeDevList(devlist);

	return err;
}

void RAIDManager::NotifyChange()
{
	CriticalSectionLock cs_NotifyChange(&m_csNotifyChange);
	if (NULL == m_pNotifyChange)
		m_pNotifyChange->set();
}

bool RAIDManager::AddDisk(const string& dev)
{
	if (dev.empty())
		return false;

	m_csDiskProfiles.Lock();
	DiskProfile profile(dev);
	m_mapDiskProfiles[dev] = profile;
	m_csDiskProfiles.Unlock();
	m_semAssemble.Post();
	NotifyChange();

	return true;
}

bool RAIDManager::RemoveDisk(const string& dev)
{
	if (dev.empty())
		return false;

	m_csDiskProfiles.Lock();
	map<string, DiskProfile>::iterator it;
	it = m_mapDiskProfiles.find(dev);
	m_mapDiskProfiles.erase(it);
	m_csDiskProfiles.Unlock();
	NotifyChange();

	return true;
}

bool RAIDManager::QueryMDDetail(const string& mddev_path,
								array_detail &ad)
{
	struct context c;
	int ret = SUCCESS;

	InitializeContext(c);
	ret = Detail_ToArrayDetail(mddev_path.c_str(), &c, &ad);
	if (ret != SUCCESS) {
		WriteHWLog(LOG_LOCAL1, LOG_DEBUG, LOG_LABEL,
				"[%d] Detail Error Code %s: (%d)\n",
				__LINE__, mddev_path.c_str(), ret);
		return false;
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

int RAIDManager::OpenMDDev(const string& mddev_path)
{
	int fd = open_mddev((char*)mddev_path.c_str(), 1);
	if (fd < 0) {
		WriteHWLog(LOG_LOCAL0, LOG_ERR, LOG_LABEL,
			   "Fail to open MD device. %s\n", mddev_path.c_str());
	}

	return fd;
}

string RAIDManager::GenerateMDSysName(int num)
{
	return string_format("md%d", num);
}

bool RAIDManager::CreateRAID(vector<string>& vDevList, int level)
{
	int ret = SUCCESS;
	bool bRetry = false;
	int freeMD = GetFreeMDNum();
	if (freeMD < 0) return false;

	do {
		/* [CS] m_csDiskProfiles
		 * TODO: Check disks use for creating MD first. 
		 * 1. Disks' size are all the same?
		 * 2. Mix iSCSI and local disks?
		 * 3. Use iSCSI to create a non-RAID0 MD
		 */

		CriticalSectionLock cs_md(&m_csMDProfiles);
		ret = CreateRAID(GenerateMDSysName(freeMD), vDevList, level);
		if (ret == SUCCESS) {
			WriteHWLog(LOG_LOCAL0, LOG_INFO, LOG_LABEL,
			   	   "%s created successfully.\n",
				   GenerateMDSysName(freeMD).c_str());

			MDProfile profile(GenerateMDSysName(freeMD));
			/* Run a thread to format the volume */
			if (profile.m_fsMgr.get() != NULL)
				profile.m_fsMgr->CreateThread();

			m_mapMDProfiles[GenerateMDSysName(freeMD)] = profile;
			NotifyChange();
			return true;
		} else if (ret == CREATE_MDDEV_INUSE) {
			/* MD number is occupied, get another one. */
			freeMD = GetFreeMDNum();
			if (freeMD < 0) return false;
			// next loop retry
		} else {
			if (bRetry) {
				WriteHWLog(LOG_LOCAL0, LOG_ERR, LOG_LABEL,
						   "Retry to create %s failed.\n",
						   GenerateMDSysName(freeMD).c_str());
				FreeMDNum(freeMD);
				bRetry = false; 
			} else
				bRetry = true;
		}
	} while (ret == CREATE_MDDEV_INUSE || bRetry);

	WriteHWLog(LOG_LOCAL1, LOG_DEBUG, LOG_LABEL,
		   "[%d] Create Error Code: (%d)\n", __LINE__, ret);
	return false;
}

int RAIDManager::CreateRAID(const string& mddev, vector<string>& vDevList, int level)
{
	if (mddev.empty())
		return CREATE_MDDEV_UNSET;

	if (vDevList.empty())
		return CREATE_RAID_DEVS_NOT_ENOUGH;

	map<string, MDProfile>::iterator it = m_mapMDProfiles.find(mddev);	
	if (it != m_mapMDProfiles.end()) {
		WriteHWLog(LOG_LOCAL1, LOG_DEBUG, LOG_LABEL,
				   "[%d] Create Error Code: (%d)\n", __LINE__,
				   CREATE_MDDEV_INUSE);

		return CREATE_MDDEV_INUSE;
	}

	struct shape s;
	InitializeShape(s, vDevList.size(), level, 512);

	struct context c;
	InitializeContext(c);

	vector<string> vDevPathList;
	for (size_t i = 0; i < vDevList.size(); i++) {
		vDevPathList.push_back(GetDeviceNodeBySymLink("/dev/" + vDevList[i]));
	}

	struct mddev_dev* devlist = InitializeDevList(vDevPathList);
	if (devlist == NULL)
		return false;

	int ret = Create(NULL, (char*)string_format("/dev/%s", mddev.c_str()).c_str(),
					 NULL, NULL, vDevPathList.size(), devlist, &s, &c, INVALID_SECTORS);
	if (ret != SUCCESS) {
		WriteHWLog(LOG_LOCAL0, LOG_ERR, LOG_LABEL,
				   "Fail to create volume %s: (%d)\n",
				   mddev.c_str(), ret);
		return ret;
	}

	FreeDevList(devlist);
	return SUCCESS;
}

bool RAIDManager::AssembleRAID(const int uuid[4], string& mddev)
{
	int ret = SUCCESS;
	bool bRetry = false;
	int freeMD = GetFreeMDNum();
	if (freeMD < 0) 
		return false;

	do {
		mddev = GenerateMDSysName(freeMD);
		ret = AssembleRAID(mddev, uuid);
		if (ret == SUCCESS) {
			unsigned char* p_uuid = (unsigned char*) uuid;
			string strUUID("");
			for (int i = 0; i < 16; i++) {
				strUUID += string_format("%02X ", p_uuid[i]);
			}
			WriteHWLog(LOG_LOCAL0, LOG_INFO, LOG_LABEL,
				   "%swas assembled to %s.\n", strUUID.c_str(), mddev.c_str());

			MDProfile profile(mddev);
			m_mapMDProfiles[mddev] = profile;
			return true;
		} else if (ret == ASSEMBLE_MD_ALREADY_ACTIVE) {
			return true;
		} else if (ret == ASSEMBLE_MDDEV_INUSE) {
			freeMD = GetFreeMDNum();
			if (freeMD < 0) 
				return false;
			// next loop retry
		} else if (ret != ASSEMBLE_RAID_DEVS_NOT_ENOUGH) {
			if (bRetry) {
				unsigned char* p_uuid = (unsigned char*) uuid;
				string strUUID("");
				for (int i = 0; i < 16; i++) {
					strUUID += string_format("%02X ", p_uuid[i]);
				}
				WriteHWLog(LOG_LOCAL0, LOG_ERR, LOG_LABEL,
						   "Retry to assemble %s failed.\n",
						   strUUID.c_str());
				FreeMDNum(freeMD);
				bRetry = false; 
			} else
				bRetry = true;	
		}
	} while (ret == ASSEMBLE_MDDEV_INUSE || bRetry);

	WriteHWLog(LOG_LOCAL1, LOG_DEBUG, LOG_LABEL,
		   "[%d] Assemble Error Code: (%d)\n", __LINE__, ret);
	return false;
}

int RAIDManager::AssembleRAID(const string& mddev, const int uuid[4])
{
	if (mddev.empty())
		return ASSEMBLE_MDDEV_UNSET;

	if (uuid == NULL)
		return ASSEMBLE_INVALID_UUID;

	map<string, MDProfile>::iterator it = m_mapMDProfiles.find(mddev);
	if (it != m_mapMDProfiles.end()) {
		WriteHWLog(LOG_LOCAL1, LOG_DEBUG, LOG_LABEL,
				   "[%d] Assemble Error Code: (%d)\n",
				   __LINE__, ASSEMBLE_MDDEV_INUSE);
		return ASSEMBLE_MDDEV_INUSE;
	}

	struct mddev_ident ident;
	InitializeMDDevIdent(ident, 1, uuid);

	struct context c;
	InitializeContext(c);
	int ret = Assemble(NULL, (char*)string_format("/dev/%s", mddev.c_str()).c_str(),
						&ident, NULL, &c);
	if (ret != SUCCESS) {
		unsigned char* p_uuid = (unsigned char*) uuid;
		string strUUID("");
		for (int i = 0; i < 16; i++) {
			strUUID += string_format("%02X ", p_uuid[i]);
		}
		WriteHWLog(LOG_LOCAL0, LOG_ERR, LOG_LABEL,
					"Fail to assemble volume %s to %s: (%d)\n",
					strUUID.c_str(), mddev.c_str(), ret);
	}

	return ret;
}

bool RAIDManager::ManageRAIDSubdevs(const string& mddev, vector<string>& vDevList, int operation)
{
	if (mddev.empty())
		return false;

	if (vDevList.empty())
		return false;

	CriticalSectionLock cs_md(&m_csMDProfiles);
	map<string, MDProfile>::iterator it = m_mapMDProfiles.find(mddev);
	if (it == m_mapMDProfiles.end()) {
		WriteHWLog(LOG_LOCAL0, LOG_ERR, LOG_LABEL,
			   "%s doesn't exist.\n", mddev.c_str());
		return false;
	}

	int fd = OpenMDDev(it->second.m_strDevPath.c_str());
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

	vector<string> vDevPathList;
	for (size_t i = 0; i < vDevList.size(); i++) {
		vDevPathList.push_back(GetDeviceNodeBySymLink("/dev/" + vDevList[i]));
		printf("%s.\n", vDevPathList[i].c_str());
	}

	switch (operation) {
	case 'R':
		if (vDevPathList.size() != 2) {
			close(fd);
			WriteHWLog(LOG_LOCAL1, LOG_DEBUG, LOG_LABEL,
				   "Replace can only accetp two devices. One for replace and another for new.\n");
			return false;
		}

		if (NULL == (devlist = InitializeDevList(vDevPathList[0], vDevPathList[1]))) {
			return false;
		}
		break;
	default:
		if (NULL == (devlist = InitializeDevList(vDevPathList, operation))) {
			return false;
		}
	}

	struct context c;
	InitializeContext(c);
	int ret = Manage_subdevs((char*)it->second.m_strDevPath.c_str(), fd, devlist, c.verbose, 0, NULL, c.force);
	close(fd);
	FreeDevList(devlist);

	if (ret != 0) {
		WriteHWLog(LOG_LOCAL0, LOG_ERR, LOG_LABEL,
			   "Fail to manage volume %s: (%d)\n", mddev.c_str(), ret);
		return false;
	}

	NotifyChange();

	return true;
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
	if (mddev.empty())
		return false;

	CriticalSectionLock cs_md(&m_csMDProfiles);
	map<string, MDProfile>::iterator it = m_mapMDProfiles.find(mddev);
	if (it == m_mapMDProfiles.end()) {
		WriteHWLog(LOG_LOCAL0, LOG_ERR, LOG_LABEL,
			   "%s doesn't exist.\n", mddev.c_str());
		return true;
	}

	/* Do nothing if the MD device is formatting */
	int stat = WRITE_INODE_TABLES_UNKNOWN, progress = -1;
	if (it->second.m_fsMgr.get() && it->second.m_fsMgr->IsFormating(progress, stat)) {
		WriteHWLog(LOG_LOCAL0, LOG_INFO, LOG_LABEL,
				   "Fail to stop volume %s. It is formatting. [%d/%d%%]\n",
				   mddev.c_str(), stat, progress);
		return false;
	}

	it->second.m_fsMgr->Unmount();
	FreeVolumeNum(it->second.m_fsMgr->GetVolumeNum());

	int fd = OpenMDDev(it->second.m_strDevPath.c_str());
	if (fd < 0) 
		return false;

	struct context c;
	int ret = SUCCESS;

	InitializeContext(c);
	ret = Manage_stop((char*)it->second.m_strDevPath.c_str(), fd, c.verbose, 0);
	close(fd);

	if (ret != SUCCESS) {
		WriteHWLog(LOG_LOCAL0, LOG_ERR, LOG_LABEL,
				   "Fail to stop RAID %s: %d\n",
				   mddev.c_str(), ret);
		return false;
	}

	FreeMDNum(it->second.m_iMDNum);
	m_mapMDProfiles.erase(it);

	return true;
}

bool RAIDManager::DeleteRAID(const string& mddev)
{
	array_detail ad;
	struct context c;
	bool bQueryDetailOK = false; 
	int ret = SUCCESS;
	InitializeContext(c);

	/* Get MD's UUID */
	bQueryDetailOK = QueryMDDetail("/dev/" + mddev, ad);

	CriticalSectionLock cs(&m_csDiskProfiles);
	if (!StopRAID(mddev))
		return false;

	map<string, DiskProfile>::iterator it = m_mapDiskProfiles.begin();
	while (it != m_mapDiskProfiles.end()) {
		struct examine_result er;
		ret = QueryMDSuperBlockInDisk(it->second.m_strDevPath, er); /*Get Array UUID in SB */

		if (mddev == it->second.m_strMDDev ||
			(ret == SUCCESS && bQueryDetailOK &&
			 0 == memcmp(ad.uuid, er.arrayUUID, sizeof(ad.uuid))))
		{
			it->second.ReadMDStat(); // m_strMDDev should be updated to an empty string.
			ret = Kill((char*)it->second.m_strDevPath.c_str(), NULL, c.force, c.verbose, 0); // Clear MD Super block.
			if (ret != SUCCESS) {
				WriteHWLog(LOG_LOCAL1, LOG_DEBUG, LOG_LABEL,
						   "Kill Error Code %s: %d\n",
						   it->first.c_str(), ret);
			}

			/* force to clear superblock */
			if ((ret = QueryMDSuperBlockInDisk(it->second.m_strDevPath, er)) != EXAMINE_NO_MD_SUPERBLOCK) {
				WriteHWLog(LOG_LOCAL0, LOG_WARNING, LOG_LABEL,
						   "Force to clear superblock of %s. [%d]\n",
						   it->first.c_str(), ret);

				string cmd = string_format("dd if=/dev/zero of=%s bs=512 count=1",
										   it->second.m_strDevPath.c_str());
				system(cmd.c_str());
			}
			WriteHWLog(LOG_LOCAL1, LOG_WARNING, LOG_LABEL,
						"%s's superblock is cleared\n",
				   it->first.c_str());
		}

		WriteHWLog(LOG_LOCAL1, LOG_WARNING, LOG_LABEL,
				   "%s's superblock is cleared\n",
				   it->first.c_str());
		it++;
	}

	return true;
}

bool RAIDManager::GenerateRAIDInfo(const MDProfile &profile, RAIDInfo& info)
{
	array_detail ad;
	if (!QueryMDDetail(profile.m_strDevPath, ad))
		return false;

	info = ad;
	info.m_strSysName = profile.m_strSysName;
	if (profile.m_fsMgr.get()) {
		info.m_bMount = profile.m_fsMgr->IsMounted();
		info.m_bFormat = profile.m_fsMgr->IsFormated();

		if (!profile.m_fsMgr->IsFormating(info.m_iFormatingState, info.m_iFormatProgress)) {
			 info.m_iFormatProgress = -1;
		}

		info.m_strMountPoint = profile.m_fsMgr->GetMountPoint();
	}

	for (size_t i = 0; i < info.m_vDiskList.size(); i++) {
		char csDiskSysName[8];
		sscanf(info.m_vDiskList[i].m_strDevPath.c_str(),
			   "/dev/%7[^/\n\t ]", csDiskSysName);
		printf("Search for disk %s's profile.\n", csDiskSysName);
		map<string, DiskProfile>::iterator it_disk = m_mapDiskProfiles.find(csDiskSysName);
		if (it_disk != m_mapDiskProfiles.end()) {
			info.m_vDiskList[i].m_diskProfile = it_disk->second;	
		}
	}
	
	return true;
}

bool RAIDManager::GetRAIDInfo(const string& mddev, RAIDInfo& info)
{
	if (mddev.empty())
		return false;

	CriticalSectionLock cs_disk(&m_csDiskProfiles);
	CriticalSectionLock cs_md(&m_csMDProfiles);
	map<string, MDProfile>::iterator it = m_mapMDProfiles.find(mddev);
	if (it == m_mapMDProfiles.end()) {
		WriteHWLog(LOG_LOCAL0, LOG_ERR, LOG_LABEL,
				   "%s doesn't exist.\n", mddev.c_str());
		return false;
	}

	if (!GenerateRAIDInfo(it->second, info))
		return false;

	return true;
}

void RAIDManager::GetRAIDInfo(vector<RAIDInfo>& list)
{
	list.clear();

	CriticalSectionLock cs_disk(&m_csDiskProfiles);
	CriticalSectionLock cs_md(&m_csMDProfiles);
	map<string, MDProfile>::iterator it = m_mapMDProfiles.begin();	
	while (it != m_mapMDProfiles.end()) {
		RAIDInfo info;
		if (GenerateRAIDInfo(it->second, info))
			list.push_back(info);
		it++;
	}
}

void RAIDManager::GetFreeDisksInfo(vector<RAIDDiskInfo> &list)
{
	list.clear();

	CriticalSectionLock cs(&m_csDiskProfiles);
	map<string, DiskProfile>::iterator it = m_mapDiskProfiles.begin();
	while(it != m_mapDiskProfiles.end()) {
		if (it->second.m_strMDDev.empty()) {
			RAIDDiskInfo info;
			info.m_strDevPath = it->second.m_strDevPath;
			info.m_diskProfile = it->second;
			list.push_back(info);
		}
		it++;
	}
}

bool RAIDManager::Format(const string& mddev)
{
	if (mddev.empty()) {
		WriteHWLog(LOG_LOCAL0, LOG_ERR, LOG_LABEL,
			   "[%d] Unknown MD device.\n", __LINE__);
		return false;
	}

	CriticalSectionLock cs_md(&m_csMDProfiles);
	map<string, MDProfile>::iterator it = m_mapMDProfiles.find(mddev);
	if (it == m_mapMDProfiles.end()) {
		WriteHWLog(LOG_LOCAL0, LOG_ERR, LOG_LABEL,
			   "%s doesn't exist.\n", mddev.c_str());
		return false;
	}

	if (it->second.m_fsMgr.get() == NULL) {
		WriteHWLog(LOG_LOCAL0, LOG_ERR, LOG_LABEL,
			   "FilesystemManager is not initialized for %s\n",
			   mddev.c_str());
		return false;
	}

	if (it->second.m_fsMgr->GetVolumeNum() < 0) {
		int num = GetFreeVolumeNum();
		it->second.m_fsMgr->SetVolumeNum(num);
	}

#ifdef NUUO
	it->second.m_fsMgr->CreateThread();
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

	CriticalSectionLock cs_md(&m_csMDProfiles);
	map<string, MDProfile>::iterator it = m_mapMDProfiles.find(mddev);
	if (it == m_mapMDProfiles.end()) {
		WriteHWLog(LOG_LOCAL0, LOG_ERR, LOG_LABEL,
			   "%s doesn't exist.\n", mddev.c_str());
		return false;
	}

	if (it->second.m_fsMgr.get() == NULL) {
		WriteHWLog(LOG_LOCAL0, LOG_ERR, LOG_LABEL,
			   "FilesystemManager is not initialized for %s\n",
			   mddev.c_str());
		return false;
	}

	if (!it->second.m_fsMgr->IsFormated()) {
		WriteHWLog(LOG_LOCAL0, LOG_ERR, LOG_LABEL,
			   "%s is not formated.\n",
			   mddev.c_str());
		return false;
	}

	int num = -1;
	if (!it->second.m_fsMgr->IsMounted(num)) {
		if (num < 0) {
			num = GetFreeVolumeNum();
			if (num < 0) return false;

			it->second.m_fsMgr->SetVolumeNum(num);
			if (!it->second.m_fsMgr->Mount()) {
				return false;
			}
		}
	}

	// In case of some necessary files and folders do not exist.
	it->second.m_fsMgr->GenerateUUIDFile();
	it->second.m_fsMgr->CreateDefaultFolders();

	return true;
}

bool RAIDManager::Unmount(const string& mddev)
{
	if (mddev.empty()) {
		WriteHWLog(LOG_LOCAL0, LOG_ERR, LOG_LABEL,
			   "[%d] Unknown MD device.\n", __LINE__);
		return false;
	}

	CriticalSectionLock cs_md(&m_csMDProfiles);
	map<string, MDProfile>::iterator it = m_mapMDProfiles.find(mddev);
	if (it == m_mapMDProfiles.end()) {
		WriteHWLog(LOG_LOCAL0, LOG_ERR, LOG_LABEL,
			   "%s doesn't exist.\n", mddev.c_str());
		return false;
	}

	if (it->second.m_fsMgr.get() == NULL) {
		WriteHWLog(LOG_LOCAL0, LOG_ERR, LOG_LABEL,
			   "FilesystemManager is not initialized for %s\n",
			   mddev.c_str());
		return false;
	}

	if (it->second.m_fsMgr->IsMounted()) {
		if (!it->second.m_fsMgr->Unmount()) {
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

	CriticalSectionLock cs_md(&m_csMDProfiles);
	map<string, MDProfile>::iterator it = m_mapMDProfiles.find(mddev);
	if (it == m_mapMDProfiles.end()) {
		WriteHWLog(LOG_LOCAL0, LOG_ERR, LOG_LABEL,
			   "%s doesn't exist.\n", mddev.c_str());
		return false;
	}

	if (it->second.m_fsMgr.get() == NULL) {
		WriteHWLog(LOG_LOCAL0, LOG_ERR, LOG_LABEL,
			   "FilesystemManager is not initialized for %s\n",
			   mddev.c_str());
		return false;
	}

	return it->second.m_fsMgr->IsFormating(progress, stat);
}

bool RAIDManager::IsMounted(const string& mddev, int &num)
{
	if (mddev.empty()) {
		WriteHWLog(LOG_LOCAL0, LOG_ERR, LOG_LABEL,
			   "[%d] Unknown MD device.\n", __LINE__);
		return false;
	}

	CriticalSectionLock cs_md(&m_csMDProfiles);
	map<string, MDProfile>::iterator it = m_mapMDProfiles.find(mddev);
	if (it == m_mapMDProfiles.end()) {
		WriteHWLog(LOG_LOCAL0, LOG_ERR, LOG_LABEL,
			   "%s doesn't exist.\n", mddev.c_str());
		return false;
	}

	if (it->second.m_fsMgr.get() == NULL) {
		WriteHWLog(LOG_LOCAL0, LOG_ERR, LOG_LABEL,
			   "FilesystemManager is not initialized for %s\n",
			   mddev.c_str());
		return false;
	}

	return it->second.m_fsMgr->IsMounted(num);
}

bool RAIDManager::IsFormated(const string& mddev)
{
	if (mddev.empty()) {
		WriteHWLog(LOG_LOCAL0, LOG_ERR, LOG_LABEL,
			   "[%d] Unknown MD device.\n", __LINE__);
		return false;
	}

	CriticalSectionLock cs_md(&m_csMDProfiles);
	map<string, MDProfile>::iterator it = m_mapMDProfiles.find(mddev);
	if (it == m_mapMDProfiles.end()) {
		WriteHWLog(LOG_LOCAL0, LOG_ERR, LOG_LABEL,
			   "%s doesn't exist.\n", mddev.c_str());
		return false;
	}

	if (it->second.m_fsMgr.get() == NULL) {
		WriteHWLog(LOG_LOCAL0, LOG_ERR, LOG_LABEL,
			   "FilesystemManager is not initialized for %s\n",
			   mddev.c_str());
		return false;
	}
	
	return it->second.m_fsMgr->IsFormated();
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

	map<string, MDProfile>::iterator it = m_mapMDProfiles.begin();
	if (it != m_mapMDProfiles.end()) {
		it->second.Dump();
	}
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
		map<string, DiskProfile>::iterator it_disk = m_mapDiskProfiles.begin();
		while (it_disk != m_mapDiskProfiles.end()) {
			struct udev *udev = NULL;
			struct udev_device *dev = NULL;
			udev = udev_new();
			if (!udev) {
				printf("can't create udev\n");
				it_disk++;
				continue;
			}

			dev = udev_device_new_from_subsystem_sysname(udev, "block", it_disk->first.c_str());
			if (NULL == dev) {
				m_mapDiskProfiles.erase(it_disk++);
			} else {
				DiskProfile profile(it_disk->first);
				if (!(profile == it_disk->second)) {
					ASSERT2(0, "[%s] System name is the same, but other disk information is different.\n", it_disk->first.c_str());
				}

				/* update new status */
				it_disk->second = profile;
				it_disk++;
			}

			udev_device_unref(dev);
			udev_unref(udev);
		}

		m_csMDProfiles.Lock();

		map<string, MDProfile>::iterator it_md = m_mapMDProfiles.begin();
		while (it_md != m_mapMDProfiles.end()) {
			/*
			 * Check MD exists or not.
			 */
			vector<string>::iterator it_member;;
			struct udev *udev = NULL;
			struct udev_device *dev = NULL;
			array_detail ad;
			char avail[128];
			int disk_enough = 0;

			udev = udev_new();
			if (!udev) {
				printf("can't create udev\n");
				goto md_check_done;
			}

			dev = udev_device_new_from_subsystem_sysname(udev, "block", it_md->first.c_str());
			if (NULL == dev) { /* MD device doesn't  exist. */
				FreeMDNum(it_md->second.m_iMDNum);

				if (it_md->second.m_fsMgr.get()) {
					it_md->second.m_fsMgr->Unmount();
					FreeVolumeNum(it_md->second.m_fsMgr->GetVolumeNum());
				}

				/*
				 * TODO: Need to let disk member call ReadMDStat to
				 * update its m_strMDDev?
				 */

				m_mapMDProfiles.erase(it_md++);
				udev_unref(udev);
				continue;
			}

			udev_device_unref(dev);
			udev_unref(udev);

			it_md->second.ReadMDStat(); /* Update new members */

			it_member = it_md->second.m_vMembers.begin();
			while (it_member != it_md->second.m_vMembers.end()) {
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
					it_member = it_md->second.m_vMembers.erase(it_member);
				} else {
					it_member++;
				}
			}

			/*
			 * Check potential malfunctional stauts of MD device and
			 * do corresponding actions to protect the volume.
			 *
			 * Unmount, Stop MD Device, or .....
			 */
			it_md->second.m_iDevCount = it_md->second.m_vMembers.size(); /* Update actual member number */
			
			if (!QueryMDDetail(it_md->second.m_strDevPath, ad)) {
				 ASSERT2(0, "Cannot query %s's detail information.", it_md->first.c_str());
				 goto md_check_done;
			}

			/*
			 * MD device has already lost some disks, we have to check
			 * whehter the remaining is enough for MD device to work.
			 *
			 * We have to create this avail array according to the RAID disks' order,
			 * because it will affect the result of RAID10.
			 */
			for (int i = 0; i < ad.arrayInfo.raid_disks; i++) {
				bool bFound = false;
				avail[i] = 0;

				/* We can see this case when iSCSI disk diconnected. */
				if (strlen(ad.arrayDisks[i].strDevName) == 0) {
					printf("MD device %s has disks without any information.\n",
							ad.strArrayDevName);
					continue;
				}

				for (size_t j = 0; j < it_md->second.m_vMembers.size(); j++) {
					if (it_md->second.m_vMembers[j] == ad.arrayDisks[i].strDevName) {
						bFound = true;
						break;
					}
				}

				/* Disk is already missing, but MD device does not know it. */
				if (!bFound) {
					continue;
				}

				if (ad.arrayDisks[i].diskInfo.state & (1 << MD_DISK_SYNC)) {
					avail[i] = 1;
				}
			}

			/*
			 * enough is a utility function to check a MD device has
			 * enough RAID disks to work or not.
			 * 1: means MD device still can work with some missing disks.
			 * 0: means MD device should not keep on working.
			 */
			disk_enough = enough(ad.arrayInfo.level,
								 ad.arrayInfo.raid_disks,
								 ad.arrayInfo.layout,
								 1, avail);

			if (it_md->second.m_iDevCount == ad.arrayInfo.raid_disks ||
				disk_enough == 1) {
				/* Check format and mount status and mount volume if it is necessary. */
				int num = -1;

				if (it_md->second.m_fsMgr.get() == NULL) {
					if(!it_md->second.InitializeFSManager()) { /* Re-initialize */
						printf("%s has no valid filesystem manager.\n",
								it_md->second.m_strSysName.c_str());
						goto md_check_done;
					}
				}

				if (it_md->second.m_fsMgr->IsFormated()) {
					if (!it_md->second.m_fsMgr->IsMounted(num)) {
						if (num == -1) {
							num = GetFreeVolumeNum();
							if (num == -1) {
								goto md_check_done;
							} else {
								/* 
								 * Volume number is assigned until 
								 * MD device is stopped or deleted.
								 */
								it_md->second.m_fsMgr->SetVolumeNum(num);
							}
						}

						if (it_md->second.m_fsMgr->Mount()) {
							it_md->second.m_fsMgr->GenerateUUIDFile();
							it_md->second.m_fsMgr->CreateDefaultFolders();
						}
					}
				} else {
					num = it_md->second.m_fsMgr->GetVolumeNum();
					if (num == -1) {
						/* 
						 * We don't have to care about whether volume num is legal or not,
						 * because this volume is not formated yet. We can get volume number
						 * again when it is formated successfully and ready to be mounted.
						 */
						num = GetFreeVolumeNum();
						it_md->second.m_fsMgr->SetVolumeNum(num); /* Pre-allocated */
					}
				}

				goto md_check_done;
			} else if (0 == disk_enough) {
				/*
				 * Umount the volume, but we don't stop MD device.
				 * We have to leave this status to user to decide
				 * which actions they want to do. Delete this RAID
				 * or modify it to add new disks.
				 */
				if (it_md->second.m_fsMgr.get() == NULL)
					goto md_check_done;

				if (it_md->second.m_fsMgr->IsMounted()) {
					if (it_md->second.m_fsMgr->Unmount()) {
						WriteHWLog(LOG_LOCAL0, LOG_ERR, LOG_LABEL,
								   "Unmount %s due to RAID doesn't have enough disks\n",
								   it_md->first.c_str());
					}
				}	
			}

md_check_done:
			it_md++;
		}

		if (m_semAssemble.Wait(0)) {
			it_disk = m_mapDiskProfiles.begin();
			while (it_disk != m_mapDiskProfiles.end()) {
				/* The disk doesn't belong to and MD device.
				 * Maybe MD device is not assembled yet.*/
				it_disk->second.ReadMDStat();
				if (it_disk->second.m_strMDDev.empty()) {
					examine_result er;
					int err = 0;
					if ((err = QueryMDSuperBlockInDisk(it_disk->second.m_strDevPath, er)) == SUCCESS) {
						// TODO: Check MD before ASSEMBLE
						string mddev;
						if (AssembleRAID(er.arrayUUID, mddev)) {
							if (m_mapMDProfiles[mddev].m_fsMgr.get() == NULL) {
								m_mapMDProfiles[mddev].InitializeFSManager();
								if (m_mapMDProfiles[mddev].m_fsMgr.get() == NULL) {
									it_disk++;
									continue;				
								}
							}
						
							int num = -1;
							if (m_mapMDProfiles[mddev].m_fsMgr->IsFormated()) {
								if (!m_mapMDProfiles[mddev].m_fsMgr->IsMounted(num)) {
									if (num == -1) {
										num = GetFreeVolumeNum();
										if (num == -1) {
											;
										} else {
											m_mapMDProfiles[mddev].m_fsMgr->SetVolumeNum(num);
											if (m_mapMDProfiles[mddev].m_fsMgr->Mount()) {
												m_mapMDProfiles[mddev].m_fsMgr->GenerateUUIDFile();
												m_mapMDProfiles[mddev].m_fsMgr->CreateDefaultFolders();
											}
										}
									} else {
										if (m_mapMDProfiles[mddev].m_fsMgr->Mount()) {
											m_mapMDProfiles[mddev].m_fsMgr->GenerateUUIDFile();
											m_mapMDProfiles[mddev].m_fsMgr->CreateDefaultFolders();
										}
									}
								}
							} else {
								num = GetFreeVolumeNum();
								m_mapMDProfiles[mddev].m_fsMgr->SetVolumeNum(num);
							}

							it_disk->second.ReadMDStat();
						}	
					} else if (err != EXAMINE_NO_MD_SUPERBLOCK) {
						WriteHWLog(LOG_LOCAL1, LOG_DEBUG, LOG_LABEL,
								   "[%d] Fail to examine %s.\n", __LINE__,
								   it_disk->first.c_str());
					}
				}

				it_disk++;	
			}
		}

		m_csMDProfiles.Unlock();
		m_csDiskProfiles.Unlock();

		SYSTEMTIME time;
		if (m_pNotifyChange == NULL) {
			SleepMS(RAIDMANAGER_MONITOR_INTERVAL);
			time = UTCTime::GetCurrentSystemTime();
			printf("[%u:%u:%u.%u]Timeout for next round check.\n",
					time.wHour, time.wMinute, time.wSecond, time.wMilliseconds);
		} else {
			m_pNotifyChange->timedwait(RAIDMANAGER_MONITOR_INTERVAL);
			time = UTCTime::GetCurrentSystemTime();
			printf("[%u:%u:%u.%u]Got notification or timeout for next round check.\n",
					time.wHour, time.wMinute, time.wSecond, time.wMilliseconds);
		}
	}
}

void RAIDManager::GetLastError(string &log)
{
	 CriticalSectionLock cs(&m_csLastError);
	 log = m_strLastError;
}

string RAIDManager::GetDeviceNodeBySymLink(const string& symlink)
{
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

#include "RAIDManager.h"

#include "common/file.h"
#include "common/string.h"

RAIDManager::RAIDManager()
: m_bRAIDInfoListUpdating(false)
{

}

RAIDManager::~RAIDManager()
{

}

bool RAIDManager::AddRAIDDisk(const string& dev)
{
	/*0. dev is empty -> return false*/
	if (dev.empty())
		return false;

	/*1. Check device node exists or not (SYSUTILS::CheckBlockDevice)
		1.1 Yes -> 2
		1.2 No -> return false*/
	if (!CheckBlockDevice(dev))
		return false;
	
	/*2. ret = Examine_ToResult() to check MD superblock
		2.1 Has MD superblock -> 3
		2.2 ret == EXAMINE_NO_MD_SUPERBLOCK -> 3*/
	vector<string> vDevList;
	struct examine_result result;
	struct mddev_dev* devlist = NULL;
	struct context c;
	int ret = SUCCESS;

	if (!InitializeDevList(devlist, vDevList)) {
		FreeDevList(devlist); // For Safety;
		return false;
	}
	InitializeContext(c);
	ret = Examine_ToResult(devlist, &c, NULL, &result);

	/*[CS Start] Protect m_vRAIDDiskList*/
	m_csRAIDDiskList.Lock();
	/*3. Exist in m_vRAIDDiskList?
		3.1 Yes
			Come from 2.2 return true
			Come from 2.1 -> 4
		3.2 No -> Push into m_vRAIDDiskLisit
			Come from 2.2 return true
			Come from 2.1 -> 4*/
	RAIDDiskInfo info;
	vector<RAIDDiskInfo>::iterator it = m_vRAIDDiskList.begin();
	while (it != m_vRAIDDiskList.end()) {
		if (it->m_strDevName == dev) {
			info.m_strDevName = result.strDevName;
			info.m_iNumber = result.uDevRole;
			info.m_iRaidDisk = result.uRaidDiskNum;
			memcpy(info.m_RaidUUID, result.arrayUUID, sizeof(int) * 4);
			m_vRAIDDiskList.push_back(info);
			break;
		}
		it ++;
	}
	/*[CS End]*/
	m_csRAIDDiskList.Unlock();

	if (ret == EXAMINE_NO_MD_SUPERBLOCK) {
		return true;
	} else if (ret > 0) {
		// TODO: Write HW Log
		return false;
	}

	/*4. SearchDiskBelong2RAID()
		4.1 Has Active RAID in m_vRAIDInfoList 
			4.1.1 Disk is active/spare in RAID -> return true
			4.1.2 Disk is faulty in RAID -> RemoveDiskFromRAID() -> AddDiskIntoRAID() -> 6
			4.1.3 ReaddDiskIntoRAID() -> 6
		4.2 No Active RAID in m_vRAIDInfoList -> 5
	*/
	string mddev;
	vector<RAIDInfo>::iterator raid_it = SearchDiskBelong2RAID(dev, info);
	if (raid_it != m_vRAIDInfoList.end()) { // 4.1
		if ((info.m_iState & (1 << MD_DISK_ACTIVE)) ||
		    (info.m_iState & ((1 << MD_DISK_ACTIVE) | (1 << MD_DISK_SYNC) | (1 << MD_DISK_REMOVED) | (1 << MD_DISK_FAULTY))) == 0
		) {
			// 4.1.1
			return true;
		} else if (info.m_iState & (1 << MD_DISK_FAULTY)) {
			// 4.1.2
			vector<string> vDevList;
			vDevList.push_back(dev);
			ret = RemoveDisksFromRAID(raid_it->m_strDevNodeName, vDevList);
			if (ret == SUCCESS) {
				ret = AddDisksIntoRAID(raid_it->m_strDevNodeName, vDevList);
				if (ret != SUCCESS) {
					// TODO: Write Log
					// We still need to upate raid info list because remove was done successfully.
				}
			} else {
				// TODO: Write Log
				return true; // Treat it as normal, and should be solved by manually. We don't need to update list.
			}
		} else {
			// 4.1.3
			vector<string> vDevList;
			vDevList.push_back(dev);
			ret = ReaddDisksIntoRAID(raid_it->m_strDevNodeName, vDevList);
			if (ret != SUCCESS) {
				// TODO: Write Log
				return true;
			}
		}
	} else { // 4.2
			/*	5. Check whether there are enough disk for assembling.
				5.1 enough -> AssembleByRAIDUUID() -> 6
				5.2 not enough -> return true	*/
		int counter = 1; // count disk has the same uuid. Initial value is 1 for this newly added disk.
		CriticalSectionLock cs_disk(&m_csRAIDDiskList);
		for (size_t i = 0; i < m_vRAIDDiskList.size(); i++) {
			if (info.m_strDevName == m_vRAIDDiskList[i].m_strDevName) // Bypass itself
				continue;

			// The list include other disks which has the same array id of this newly added disk.
			if (0 == memcmp(info.m_RaidUUID, m_vRAIDDiskList[i].m_RaidUUID, sizeof(int) * 4))
				counter++;
		}
		
		if (counter >= info.m_iRaidDisk) {
			int freeMD = 0;
			CriticalSectionLock cs_md(&m_csUsedMD);
			for (; freeMD < 128; freeMD++) {
				if (!m_bUsedMD[freeMD]) {
					break;
				}
			}

			if (freeMD >= 128) {
				// TODO: HW Log, exceed maxmimal raid device number.
				return true;
			}

			mddev = string_format("/dev/md%d", freeMD);
			ret = AssembleByRAIDUUID(mddev, info.m_RaidUUID);
			if (ret != SUCCESS) {
				// TODO: HW Log
				return true; // Treat it as normal, and should be solved by manually. We don't need to update list.
			}
		}
	}

	/* 6.  UpdateRAIDInfo(mddev) */
	UpdateRAIDInfo(mddev);
	return true;
}

vector<RAIDInfo>::iterator RAIDManager::SearchDiskBelong2RAID(const string& dev, RAIDDiskInfo& devInfo)
{
	/* 0. dev is empty -> return false */
	if (dev.empty())
		return m_vRAIDInfoList.end();

	/*
		[CS Start] Protect m_vRAIDInfoList
		1. Search dev in m_vRAIDInfoList.vDiskList
			1.1 Exist a RAID return its iterator
			1.2 Not exist a RAID return m_vRAIDInfoList.end()
		[CS End]		
	*/
	CriticalSectionLock cs(&m_csRAIDInfoList);
	vector<RAIDInfo>::iterator it = m_vRAIDInfoList.begin();
	while (it != m_vRAIDInfoList.end()) {
		vector<RAIDDiskInfo>::iterator it_disk = it->m_vDiskList.begin();
		while (it_disk != it->m_vDiskList.end()) {
			if (it_disk->m_strDevName == dev) {
				devInfo = *it_disk;
				return it;
			}
		}

		it ++;
	}

	return it;
}

bool RAIDManager::RemoveRAIDDisk(const string& dev)
{
	/*
		When a disk is removed, it will be marked faulty.
		And just keep this status and it should be updated
		after UpdateRAIDInfo finished.
	*/

	/* 0. dev is empty -> return false */
	if (dev.empty())
		return false;

	/*
		[CS Start] Protect m_vRAIDDiskList
		1. Exist in m_vRAIDDiskList?
			1.1 Yes -> Remove from m_vRAIDDiskList -> 2
			1.2 No -> return true;
		[CS End]
	*/
	m_csRAIDDiskList.Lock();
	vector<RAIDDiskInfo>::iterator it = m_vRAIDDiskList.begin();
	int uuid[4];
	while (it != m_vRAIDDiskList.end()) {
		if (it->m_strDevName == dev) {
			memcpy(uuid, it->m_RaidUUID, sizeof(int) * 4);
			break;
		}
		it++;
	}

	if (it == m_vRAIDDiskList.end()) {
		m_csRAIDDiskList.Unlock();
		return true;
	}

	m_vRAIDDiskList.erase(it);
	m_csRAIDDiskList.Unlock();

	/* 2. UpdateRAIDInfo(uuid) */
	UpdateRAIDInfo(uuid);
	return true;
}

bool RAIDManager::UpdateRAIDInfo(const string& mddev)
{
	/*
		Update RAID object by mddev name.
		
		0. mddev is empty -> return false

		[CS Start] Protect m_vRAIDInfoList
		1. Detail_ToArrayDetail(mddev)
		2. Erase old one and push new one
		[CS End]
	*/
}

bool RAIDManager::UpdateRAIDInfo()
{
	/*
		Update all RAID objects in m_vRAIDInfoList
		1. For loop to get m_strDevNodeName
		2. UpdateRAIDInfo(m_strDevNodeName)
	*/
}

bool RAIDManager::UpdateRAIDInfo(const int uuid[4])
{
	/*
		Update RAID objec by UUID.
		1. For loop to look up m_strDevNodeName corresponding to the uuid.
		2. UpdateRAIDInfo(m_strDevNodeName)
	*/
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
	ident.uuid_set = uuid_set;
	ident.super_minor = UnSet;
	ident.level = UnSet;
	ident.raid_disks = UnSet;
	ident.spare_disks = UnSet;
	ident.bitmap_fd = bitmap_fd;
	ident.bitmap_file = bitmap_file;
	if (uuid_set)
		memcpy(ident.uuid, uuid, sizeof(int) * 4);
}

bool RAIDManager::InitializeDevListForReplace(struct mddev_dev* devlist, const string& replace, const string& with)
{
	struct mddev_dev** devlistend = &devlist;
	struct mddev_dev* dv = NULL;

	devlist = NULL;
	dv = (struct mddev_dev*)malloc(sizeof(struct mddev_dev));
	if (dv == NULL) {
		// TODO: Write HW logs.
		return false;
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
		// TODO: Write HW logs.
		return false;
	}
	dv->devname = (char*)with.c_str();
	dv->disposition = 'w';
	dv->writemostly = 0;
	dv->used = 0;
	dv->next = NULL;
	*devlistend = dv;
	devlistend = &dv->next;

	return true;
}

bool RAIDManager::InitializeDevList(struct mddev_dev* devlist, const vector<string>& devNameList, int disposition)
{
	struct mddev_dev** devlistend = &devlist;
	struct mddev_dev* dv = NULL;

	devlist = NULL;
	for (size_t i = 0; i < devNameList.size(); i ++) {
		dv = (struct mddev_dev*)malloc(sizeof(struct mddev_dev));
		if (dv == NULL) {
			// TODO: Write HW logs.
			return false;
		}
		dv->devname = (char*)devNameList[i].c_str();
		dv->disposition = 0;
		dv->writemostly = 0;
		dv->used = 0;
		dv->next = NULL;
		*devlistend = dv;
		devlistend = &dv->next;
	}

	return true;
}

void RAIDManager::FreeDevList(struct mddev_dev* devlist)
{
	struct mddev_dev* dv = NULL;
	for (dv = devlist; dv; dv = dv->next) {
		free(dv);
	}
	devlist = NULL;
}

bool RAIDManager::CreateRAID(const string& mddev, const vector<string>& vDevList, int level)
{
	/*
		1. Check mddev
			empty string -> return false;
		2. Check devList
			no device (empty vector) > return false
		3.[CS] mddev exist in m_vRAIDInfoList or not
			Yes -> return false 

		[CS Start] Protect m_vRAIDDiskList
		3. Check whether devices in devList exist in m_vRAIDDiskList or not.
			Any device does not exist in m_vRAIDDiskList -> return false
		[CS End]

		4. InitializeShape(s, vDevList.size(), level, 512)
		5. InitializeContext(c)
		6. InitializeDevList(devlist, vDevList)
			6.1 false -> FreeDevList(devlist) -> return false
		7. ret = Create(NULL, mddev.c_str(), "\0", NULL, vDevList.size(), devlist, &s, &c, INVALID_SECTORS)
			7.1 ret != 0 -> Write HW Log ->9
			7.2 ret == 0 -> 8
		8. UpdateRAIDInfo(mddev)
		9. FreeDevList(devlist)
			9.1 ret !=  0 -> return false
			9.2 ret = 0 -> return true
	*/
}

bool RAIDManager::AssembleByRAIDUUID(const string& mddev, const int uuid[4])
{
	/*
		1. Check mddev
			empty -> return false
		2. Check uuid
			NULL -> return false
		3. [CS] Check mddev exists in m_vRAIDInfoList or not
			Yes -> return false

		4. InitializeMDDevIdent(ident, 1, str_uuid)
		5. ret = Assemble(NULL, mddev.c_str(), &ident, NULL, &c);
			4.1 ret != 0 -> Write HW Log -> return false
		6. return UpdateRAIDInfo(mddev)
	*/	
}

bool RAIDManager::AssembleByRAIDDisks(const string& mddev, const vector<string>& vDevList)
{
	/*
		1. Check mddev
			empty -> return false
		2. Check vDevList 
			empty -> return false
		3. [CS] Check mddev exists in m_vRAIDInfoList or not
			Yes -> return false

		[CS Start] Protect m_vRAIDDiskList
		4. Check whether devices in devList exist in m_vRAIDDiskList or not.
			Any device does not exist in m_vRAIDDiskList -> return false
		[CS End]

		5. InitializeMDDevIdent(ident, 0, "");
		6. InitalizeDevList(devlist, vDevList);
			6.1 false -> FreeDevlist(devlist) -> return false
		7. ret = Assemble(NULL, mddev.c_str(), &ident, devlist, &c);
			7.1 ret != 0 -> Write HW Log
		8. FreeDevList(devlist)
		
		9.
		if (ret != 0)
			return false;
		else
			return UpdateRAIDInfo(mddev)
	*/	

}

bool RAIDManager::ManageRAIDSubdevs(const string& mddev, const vector<string>& vDevList, int operation)
{
	/*
		1. Check mddev
			empty -> return false
		2. Check vDevList 
			empty -> return false
		3. [CS] Check mddev exists in m_vRAIDInfoList or not
			No -> return false

		[CS Start] Protect m_vRAIDDiskList
		4. Check whether devices in devList exist in m_vRAIDDiskList or not.
			Any device does not exist in m_vRAIDDiskList -> return false
		[CS End]
	
		5. fd = open_mddev(mddev.c_str(), 1)
			4.1 fd < 0 -> return false
			4.2 other ->6

		6.
			if (disposition == 'R')
				if vDevList.size != 2 close(fd);return false;
				InitializeDevListForReplace(devlist, vDevList[0], vDevList[1])
			else
				InitializeDevList(devlist, vDevList, operation);
			6.1 false -> FreeDevlist(devlist) -> return false
		7. InitializeContext(c)
		8. ret = Manage_subdevs(mddev.c_str(), fd, devlist, c.verbose, 0, NULL, c.force);
			ret != 0 -> Write HW Log
		9. close(fd)
		10. FreeDevList(devlist) 

		11.
		if (ret != 0)
			return false;
		else
			return UpdateRAIDInfo(mddev)
	*/
}

bool RAIDManager::RemoveDisksFromRAID(const string& mddev, const vector<string>& vDevList)
{
	return ManageRAIDSubdevs(mddev, vDevList, 'r');
}

bool RAIDManager::MarkFaultyDisksInRAID(const string& mddev, const vector<string>& vDevList)
{
	return ManageRAIDSubdevs(mddev, vDevList, 'f');
}

bool RAIDManager::AddDisksIntoRAID(const string& mddev, const vector<string>& vDevList)
{
	return ManageRAIDSubdevs(mddev, vDevList, 'a');
}

bool RAIDManager::ReaddDisksIntoRAID(const string& mddev, const vector<string>& vDevList)
{
	return ManageRAIDSubdevs(mddev, vDevList, 'A');
}

bool RAIDManager::ReplaceDisksInRAID(const string& mddev, const string& replace, const string& with)
{
	if (replace.empty() || with.empty())
		return false;

	vector<string> vDevList;
	vDevList.push_back(replace);
	vDevList.push_back(with);

	return ManageRAIDSubdevs(mddev, vDevList, 'R');
}

bool RAIDManager::DeleteRAID(const string& mddev)
{
	/*
		1. Check mddev
			empty -> return false
		2. If mddev exist in m_vRAIdInfoList
			No -> return true;
			Yes -> 3
		3. fd = open_mddev(mddev.c_str(), 1);
			fd < 0 -> return false;
		4. ret = Manage_stop(mddev.c_str(), fd, 1, 0);
			ret != 0 -> close(fd) -> close(fd);return false;
			ret == 0 -> 5
		5. close(fd)
		6. Remove mddev from m_vRAIDInfoList, keep a RAID disks list copy for later use.
		7. InitializeContext(c)
		8. For all raid disks in mddev: ret = Kill(devname, NULL, c.force, c.verbose, 0);
			Write HW Log if ret != 0 
	*/
}

bool RAIDManager::GetRAIDInfo(const string& mddev, RAIDInfo& info)
{
	/*
		1. Check mddev
			empty -> return false
		[CS Start] protect m_vRAIDInfoList
		2. Look for mddev in m_vRAIDInfoList
			2.1 Exist -> Copy to info -> return true (found)
		[CS End]
		3. return false (not found)
	*/
}

void RAIDManager::GetRAIDInfo(vector<RAIDInfo>& list)
{

}

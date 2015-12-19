#include "RAIDManager.h"

#ifdef NUUO
#include "common/file.h"
#include "common/string.h"
#include "common/directory_traverse.h"
#endif

#define LOG_LABEL "RAIDManager"

RAIDManager::RAIDManager()
{
	for (int i = 0; i < 128; i++)
		m_bUsedMD[i] = false;
	for (int i = 0; i < 128; i++)
		m_bUsedVolume[i] = false;

#ifdef NUUO
	DirectoryTraverse dt("/dev");

	/*
		Check running MD devices and add into list.
	*/
	while (dt.Next()) {
		if (dt.GetPathName().find("/dev/md") != string::npos) {
			if (CheckBlockDevice(dt.GetPathName())) {
				int num = 0;
				int ret = sscanf(dt.GetPathName().c_str(), "/dev/md%d", &num);
				if (ret < 1 || ret == EOF || num > 127 || num < 0)
					continue;
	
				/*
					Check /dev/mdX to make sure that it is an active device.
					If it is an active one, we can add it into list.
				*/
				if (UpdateRAIDInfo(dt.GetPathName(), num)) {
					m_bUsedMD[num] = true;
				}

				int volumeNum;
				if (IsFormated(dt.GetPathName())) {
					if (IsMounted(dt.GetPathName(), volumeNum)) {
						m_bUsedVolume[volumeNum] = true;
					} else {
						Mount(dt.GetPathName());
					}
				}
			}
		}
	}
	dt.Reset();

	/* 
		Because adding MD device into list will also add the disk into 
		the m_vRAIDDiskList. So, Clear the list first for following procedures
		to add disk into list, or the soft link information will be lost.
	*/
	//m_vRAIDDiskList.clear();

	/*
		Check active HDD and add into list.
	*/
	while (dt.Next()) {
		if (dt.GetFlags() & DirectoryTraverse::FLAG_SYMBOLIC) {
			if (dt.GetPathName().find("/dev/nuuo_sata") !=
			    string::npos) {
				if (dt.GetPathName() != "/dev/nuuo_satadom") {
					AddDiskSymLink(dt.GetPathName(), DISK_TYPE_SATA);
					AddDisk(dt.GetPathName()); 
				}
			} else if (dt.GetPathName().find("/dev/nuuo_esata") != string::npos) {
				AddDiskSymLink(dt.GetPathName(), DISK_TYPE_ESATA);
				AddDisk(dt.GetPathName());
			} else if (dt.GetPathName().find("/dev/nuuo_iscsi") != string::npos) {
				AddDiskSymLink(dt.GetPathName(), DISK_TYPE_ISCSI);
				AddDisk(dt.GetPathName());
			}
		}
	}
	dt.Close();
#endif
}

RAIDManager::~RAIDManager()
{

}

int RAIDManager::GetFreeMDNum()
{
#ifdef NUUO
	CriticalSectionLock cs_md(&m_csUsedMD);
#endif
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

#ifdef NUUO
	CriticalSectionLock cs_md(&m_csUsedMD);
#endif
	m_bUsedMD[n] = false;
}

void RAIDManager::SetMDNum(int n)
{
	if (n < 0 || n > 127)
		return;

#ifdef NUUO
	CriticalSectionLock cs_md(&m_csUsedMD);
#endif
	m_bUsedMD[n] = true;
}

int RAIDManager::GetFreeVolumeNum()
{
#ifdef NUUO
	CriticalSectionLock cs_md(&m_csUsedVolume);
#endif
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

#ifdef NUUO
	CriticalSectionLock cs_md(&m_csUsedVolume);
#endif
	m_bUsedVolume[n] = false;
}

void RAIDManager::SetVolumeNum(int n)
{
	if (n < 0 || n > 127)
		return;

#ifdef NUUO
	CriticalSectionLock cs_md(&m_csUsedVolume);
#endif
	m_bUsedVolume[n] = true;
}

vector<RAIDInfo>::iterator RAIDManager::IsMDDevInRAIDInfoList(const string &mddev, RAIDInfo& info)
{
#ifdef NUUO
	CriticalSectionLock cs(&m_csRAIDInfoList);
#endif
	vector<RAIDInfo>::iterator it = m_vRAIDInfoList.begin();
	while (it != m_vRAIDInfoList.end()) {
		if (*it == mddev) {
			info = *it;
			break;
		}

		it ++;
	}

	return it;
}

vector<RAIDInfo>::iterator RAIDManager::IsMDDevInRAIDInfoList(const string &mddev)
{
	RAIDInfo info;
	return IsMDDevInRAIDInfoList(mddev, info);
}

bool RAIDManager::IsDiskExistInRAIDDiskList(const string& dev)
{
#ifdef NUUO
	CriticalSectionLock cs(&m_csRAIDDiskList);
#endif
	vector<RAIDDiskInfo>::iterator it_disk = m_vRAIDDiskList.begin();
	while (it_disk != m_vRAIDDiskList.end()) {
		// Compare both soft link name and actual device node name.
		// In case of soft link name is not the same, but the disk
		// actually is in the list....
		string strDevNode = GetDeviceNodeBySymLink(dev); 
		if (*it_disk == strDevNode) {
			return true;
		}
		it_disk++;
	}

	return false;
}

bool RAIDManager::IsDiskExistInRAIDDiskList(vector<string>& vDevList)
{
	vector<string>::iterator it_devlist = vDevList.begin();
	while (it_devlist != vDevList.end()) {
		if(!IsDiskExistInRAIDDiskList(*it_devlist)) {
			return false; // Some device in the list is not in m_vRAIDDiskList.
		}

		it_devlist++;
	}

	return true;
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

bool RAIDManager::AddDiskSymLink(const string& symlink, eDiskType type)
{
	if (symlink.empty())
		return false;

#ifdef NUUO
	CriticalSectionLock cs(&m_csSymLinkTable);
#endif

	string strDevNode = GetDeviceNodeBySymLink(symlink);
	MiscDiskInfo info(symlink, type);
	info.Dump();
	m_mapSymLinkTable[strDevNode] = info;
	return true;
}

bool RAIDManager::RemoveDiskSymLink(const string& symlink)
{
	if (symlink.empty())
		return false;

	string strDevNode = GetDeviceNodeBySymLink(symlink);

#ifdef NUUO
	CriticalSectionLock cs(&m_csSymLinkTable);
#endif

	map<string, MiscDiskInfo>::iterator it = m_mapSymLinkTable.find(strDevNode);
	if (it != m_mapSymLinkTable.end())
		m_mapSymLinkTable.erase(it);

	return true;
}

bool RAIDManager::AddDisk(const string& dev)
{
	/*0. dev is empty -> return false*/
	if (dev.empty())
		return false;

	/*1. Check device node exists or not (SYSUTILS::CheckBlockDevice)
		1.1 Yes -> 3 
		1.2 No -> return false*/
#ifdef NUUO
	if (!CheckBlockDevice(dev)) {
		WriteHWLog(LOG_LOCAL0, LOG_ERR, LOG_LABEL,
			   "%s is not a block device.\n", dev.c_str());
		return false;
	}
#endif

	/*[CS Start] Protect m_vRAIDDiskList*/
	/*3. Exist in m_vRAIDDiskList?
		3.1 Yes
			Come from 2.2 return true
			Come from 2.1 -> 4
		3.2 No -> Push into m_vRAIDDiskLisit
			Come from 2.2 return true
			Come from 2.1 -> 4*/
	struct examine_result result;
	int ret = SUCCESS;
	RAIDDiskInfo info;
	bool bExist = IsDiskExistInRAIDDiskList(dev);

	info.m_bHasMDSB = IsDiskHaveMDSuperBlock(dev, result, ret);
	info.m_strDevName = GetDeviceNodeBySymLink(dev);
	info.SetHDDVendorInfomation();
	info.m_iRaidDiskNum = result.uRaidDiskNum;
	memcpy(info.m_RaidUUID, result.arrayUUID, sizeof(int) * 4);

	// FIXME: Maybe the critical section should protect following code since checking the disk existence. 
	if (!bExist) {
#ifdef NUUO
		CriticalSectionLock cs(&m_csRAIDDiskList);
#endif
		m_vRAIDDiskList.push_back(info);
		WriteHWLog(LOG_LOCAL0, LOG_INFO, LOG_LABEL,
			   "%s added successfully .\n", dev.c_str());
	} 

	/*[CS End]*/

	if (!info.m_bHasMDSB) {
		WriteHWLog(LOG_LOCAL0, LOG_INFO, LOG_LABEL,
			   "%s has no MD superblock .\n", dev.c_str());
		return true;
	} else if (ret > 0) {
		WriteHWLog(LOG_LOCAL1, LOG_DEBUG, LOG_LABEL,
			   "[%d] Examine Error Code %s: (%d)\n", __LINE__,
			   dev.c_str(), ret);
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
	vector<RAIDInfo>::iterator raid_it = SearchDiskBelong2RAID(info);
	if (raid_it != m_vRAIDInfoList.end()) { // 4.1
		vector<string> vDevList;
		vDevList.push_back(dev);
		
		if (info.m_iState & (1 << MD_DISK_ACTIVE)) {
			;
		} else {
			ret = ReaddMDDisks(raid_it->m_strDevNodeName, vDevList);
			if (ret != SUCCESS) {
				WriteHWLog(LOG_LOCAL1, LOG_DEBUG, LOG_LABEL,
					   "[%d] Manage Error Code %s: (%d)\n",
					   __LINE__,
				  	 raid_it->m_strDevNodeName.c_str(), ret);
				// We still need to upate raid info list because remove was done successfully.
			}
		}
	} else { // 4.2
			/*	5. Check whether there are enough disk for assembling.
				5.1 enough -> AssembleByRAIDUUID() -> 6
				5.2 not enough -> return true	*/
		int counter = 1; // count disk has the same uuid. Initial value is 1 for this newly added disk.
#ifdef NUUO
		m_csRAIDDiskList.Lock();
#endif
		for (size_t i = 0; i < m_vRAIDDiskList.size(); i++) {
			if (info.m_strDevName == m_vRAIDDiskList[i].m_strDevName) // Bypass itself
				continue;

			// The list include other disks which has the same array id of this newly added disk.
			if (0 == memcmp(info.m_RaidUUID, m_vRAIDDiskList[i].m_RaidUUID, sizeof(int) * 4))
				counter++;
		}
#ifdef NUUO
		m_csRAIDDiskList.Unlock();
#endif
		
		if (counter >= info.m_iRaidDiskNum) {
			if (AssembleRAID(info.m_RaidUUID, mddev)) {
				/* Don't format the MD device for safety.
				   To prevent from unnecessary format and
				   then leading to data lost.
				*/
				int num = -1;
				if (IsFormated(mddev) && !IsMounted(mddev, num))
					Mount(mddev);
				return true;
			}
		}
	}

	/* 6.  UpdateRAIDInfo(mddev) */
	UpdateRAIDInfo(mddev);
	return true;
}

vector<RAIDInfo>::iterator RAIDManager::SearchDiskBelong2RAID(RAIDDiskInfo& info)
{
	/* 0. dev is empty -> return false */
	if (info.m_strDevName.empty())
		return m_vRAIDInfoList.end();

	/*
		[CS Start] Protect m_vRAIDInfoList
		1. Search dev in m_vRAIDInfoList.vDiskList
			1.1 Exist a RAID return its iterator
			1.2 Not exist a RAID return m_vRAIDInfoList.end()
		[CS End]		
	*/

#ifdef NUUO
	CriticalSectionLock cs(&m_csRAIDInfoList);
#endif
	vector<RAIDInfo>::iterator it = m_vRAIDInfoList.begin();
	while (it != m_vRAIDInfoList.end()) {
#if 1
		if (0 != memcmp(it->m_UUID, info.m_RaidUUID, sizeof(info.m_RaidUUID))) {
			it ++;
			continue;
		} else {
			vector<RAIDDiskInfo>::iterator it_disk = it->m_vDiskList.begin();
			while (it_disk != it->m_vDiskList.end()) {
				if (*it_disk == info.m_strDevName) {
					info = *it_disk;
					break;
				}

				it_disk++;
			}
			return it;
		}
#endif

		it ++;
	}

	return it;
}

bool RAIDManager::RemoveDisk(const string& dev)
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
		If disk is belong to the RAID, it need to be mark faulty and
		removed before it is removed from RAID disk list.

		Note: It is not necessary, because when user remove the disk,
		      It should set faulty and remove first. Once the disk is
                      removed without marking faulty and removing, it will
		      be impossible to do, because it is already set removed
		      by the MD driver.
			
		      Umount the volume is not a neccessary procedure,
		      because if the volume still can work, it is always
		      workable, even if it has a removed disk.
		      Howerver, if it cannot be used, we will know it 
		      is in a critical status.
	*/

#if 1 
	RAIDDiskInfo info;
	vector<RAIDInfo>::iterator raid_it = SearchDiskBelong2RAID(dev, info);
	if (raid_it != m_vRAIDInfoList.end()) {
		int num = 0;
		if (IsMounted(raid_it->m_strDevNodeName, num)) {
			if (Unmount(raid_it->m_strDevNodeName))
				return false;
		}

		vector<string> vDevList;
		vDevList.push_back(dev);
		if (!MarkFaultyMDDisks(raid_it->m_strDevNodeName, vDevList))
			return false;

		if (!RemoveMDDisks(raid_it->m_strDevNodeName, vDevList))
			return false;
	}
#endif

	/*
		[CS Start] Protect m_vRAIDDiskList
		1. Exist in m_vRAIDDiskList?
			1.1 Yes -> Remove from m_vRAIDDiskList -> 2
			1.2 No -> return true;
		[CS End]
	*/
#ifdef NUUO
	m_csRAIDDiskList.Lock();
#endif
	vector<RAIDDiskInfo>::iterator it = m_vRAIDDiskList.begin();
	int uuid[4];
	while (it != m_vRAIDDiskList.end()) {
		if (*it == dev) {
			memcpy(uuid, it->m_RaidUUID, sizeof(int) * 4);
			break;
		}
		it++;
	}

	if (it == m_vRAIDDiskList.end()) {
#ifdef NUUO
		m_csRAIDDiskList.Unlock();
#endif
		WriteHWLog(LOG_LOCAL0, LOG_INFO, LOG_LABEL,
			   "%s removed successfully .\n", dev.c_str());
		return true;
	}

	m_vRAIDDiskList.erase(it);
#ifdef NUUO
	m_csRAIDDiskList.Unlock();
#endif

	/*
	   It is no reason to mount. The reason is explained in former
	   comment.
	*/
#if 0
	if (raid_it != m_vRAIDInfoList.end()) {
		/* Try to remount MD after removing the disk */
		Mount(raid_it->m_strDevNodeName);
	}
#endif

	/* 2. UpdateRAIDInfo(uuid) */
	UpdateRAIDInfo(uuid);
	WriteHWLog(LOG_LOCAL0, LOG_INFO, LOG_LABEL,
		   "%s removed successfully .\n", dev.c_str());
	return true;
}

void RAIDManager::UpdateRAIDDiskList(vector<RAIDDiskInfo>& vRAIDDiskInfoList, const string& mddev)
{
	if (vRAIDDiskInfoList.empty())
		return;

	vector<RAIDDiskInfo>::iterator it = vRAIDDiskInfoList.begin();
	while (it != vRAIDDiskInfoList.end()) {
		bool bExist = false;
#ifdef NUUO
		CriticalSectionLock cs(&m_csRAIDDiskList);
#endif
		vector<RAIDDiskInfo>::iterator it_all = m_vRAIDDiskList.begin();
		while(it_all != m_vRAIDDiskList.end()) {
			if (*it == *it_all) {
				examine_result result;
				int ret = SUCCESS;

				it->m_bHasMDSB = IsDiskHaveMDSuperBlock(it->m_strDevName, result, ret);
				*it_all = *it;
				bExist = true;
				break;
			}

			it_all++;
		}

		if (!bExist) {
			// Should not be here.....
			// If we are here, it means this disk is not added before creating the RAID volume.
			// And we will not have the soft link information about the disk....
			// But we still push it into list, look up for a way to get the soft link name.
			m_vRAIDDiskList.push_back(*it);
		}

#ifdef NUUO
		CriticalSectionLock cs(&m_mapSymLinkTable);
#endif
		m_mapSymLinkTable[it->m_strDevName].m_strMDDev = mddev; 
		it++;
	}

#ifdef NUUO
	CriticalSectionLock cs(&m_mapSymLinkTable);
#endif
	map<string, MiscDiskInfo>::iterator it_symLinkTab = m_mapSymLinkTable.begin();
	while (it_symLinkTab != m_mapSymLinkTable.end()) {
		if (it_symLinkTab->second.m_strMDDev != mddev) {
			it_symLinkTab++;
			continue;
		}

		bool bFound = false;
		for (size_t i = 0; i < vRAIDDiskInfoList.size(); i++) {
			if (it_symLinkTab->first == vRAIDDiskInfoList[i].m_strDevName) {
				bFound = true;
				break;
			}
		}
		
		if (!bFound)
			it_symLinkTab->second.m_strMDDev = ""; // This MD device doesn't have this disk now.

		it_symLinkTab++;		
	}
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
	if (info.m_iTotalDiskNum == 0)
		return true;

	switch (info.m_iRAIDLevel) {
	case 0:
	case 1:
	case 5:
	case 6:
	case 10:
	case LEVEL_MULTIPATH:
	case LEVEL_LINEAR:
		if (info.m_strState.find("FAILED") != string::npos)
			return true;
		break;
	default:
		WriteHWLog(LOG_LOCAL1, LOG_DEBUG, LOG_LABEL,
				   "[%s] Unknown RAID Level.\n",
				   info.m_strDevNodeName);
		return true;
	}

	return false;
}

bool RAIDManager::UpdateRAIDInfo(const string& mddev, int mdnum)
{
	/* Update RAID object by mddev name.
	   If the request mddev does not exist in the list,
	   push the info into the list.*/

	/* 0. dev is empty -> return false */
	if (mddev.empty())
		return false;
		
	/*
		[CS Start] Protect m_vRAIDInfoList
		1. Detail_ToArrayDetail(mddev)
		2. Erase old one and push new one
		[CS End]
	*/
	struct array_detail ad;
	RAIDInfo info;
	bool bGetDetailSuccess = false;

	bGetDetailSuccess = GetRAIDDetail(mddev, ad);

#ifdef NUUO
	CriticalSectionLock cs(&m_csRAIDInfoList);
#endif
	vector<RAIDInfo>::iterator it = m_vRAIDInfoList.begin();
	while (it != m_vRAIDInfoList.end()) {
		if (*it == mddev) {
			if (!bGetDetailSuccess) {
				 it->m_strState = "INVALID";
				 return false;
			}

			*it = ad; // Keep some fixed information like mount point, volumne name
			UpdateRAIDDiskList(it->m_vDiskList, mddev);

			if (IsRAIDAbnormal(*it)) {
				/* Try to unmount the volume. */
				if (IsMounted(mddev)) {
					Unmount(mddev);
				}

				/* Obviously a useless MD device, remove it from the list. */
				if (ad.arrayInfo.nr_disks == 0) {
					m_vRAIDInfoList.erase(it);
					WriteHWLog(LOG_LOCAL1, LOG_DEBUG, LOG_LABEL,
							   "[%d] %s's disk number is zero. Remove from the list.\n",
							   __LINE__, mddev.c_str());
				}
			}

			return true;
		}

		it ++;
	}

	if (ad.arrayInfo.nr_disks == 0) {
		WriteHWLog(LOG_LOCAL1, LOG_DEBUG, LOG_LABEL,
				   "[%d] %s's disk number is. Ignore it.\n",
				   __LINE__, mddev.c_str());
		return true;
	}
	
	info = ad; // Update new information.
	info.m_iMDNum = mdnum;
	info.InitializeFSManager();
	UpdateRAIDDiskList(info.m_vDiskList, mddev);
	m_vRAIDInfoList.push_back(info);
	return true;
}

bool RAIDManager::UpdateRAIDInfo()
{
	/*
		Update all RAID objects in m_vRAIDInfoList,
		and we don't add new RAID into list by this method.
		1. For loop to get m_strDevNodeName
		2. UpdateRAIDInfo(m_strDevNodeName)
	*/

#ifdef NUUO
	CriticalSectionLock cs(&m_csRAIDInfoList);
#endif
	if (m_vRAIDInfoList.empty())
		return true;

	vector<RAIDInfo>::iterator it = m_vRAIDInfoList.begin();
	while (it != m_vRAIDInfoList.end()) {
		struct array_detail ad;

		if (!GetRAIDDetail(it->m_strDevNodeName, ad)) {
			/* 
			 * Cannot get RAID detail information.
			 * In order to easily debug, assign "INVALID"
			 * state to notify users.
			 */
			it->m_strState = "INVALID"; 
			continue;
		}

		*it = ad; // Keep some fixed information like mount point, volumne name
		UpdateRAIDDiskList(it->m_vDiskList, it->m_strDevNodeName); // For update m_mapSymLinkTable, this step should be done before remove the RAID device from the list.

		if (IsRAIDAbnormal(*it)) {
			if (IsMounted(it->m_strDevNodeName)) {
				Unmount(it->m_strDevNodeName);
			}
		}

		if (ad.arrayInfo.nr_disks == 0) {
			it = m_vRAIDInfoList.erase(it);
			WriteHWLog(LOG_LOCAL1, LOG_DEBUG, LOG_LABEL,
					   "[%d] %s's disk number is zero. Remove from the list.\n",
					   __LINE__, mddev.c_str());
		} else {
			it ++;
		}
	}

	return true;
}

bool RAIDManager::UpdateRAIDInfo(const int uuid[4])
{
	/*
		Update RAID objec by UUID.
		and we don't add new RAID into list by this method.
		1. For loop to look up m_strDevNodeName corresponding to the uuid.
		2. UpdateRAIDInfo(m_strDevNodeName)
	*/
	if (uuid == NULL)
		return false;

#ifdef NUUO
	CriticalSectionLock cs(&m_csRAIDInfoList);
#endif
	if (m_vRAIDInfoList.empty())
		return true;

	vector<RAIDInfo>::iterator it = m_vRAIDInfoList.begin();
	while (it != m_vRAIDInfoList.end()) {
		if (0 == memcmp(uuid, it->m_UUID, sizeof(int) * 4)) {
			struct array_detail ad;
			
			if (!GetRAIDDetail(it->m_strDevNodeName, ad)) {
				it->m_strState = "INVALID";
				return false; 
			}

			*it = ad;
			UpdateRAIDDiskList(it->m_vDiskList, it->m_strDevNodeName);
			if (IsRAIDAbnormal(*it)) {
				/* Try to unmount the volume. */
				if (IsMounted(mddev)) {
					Unmount(mddev);
				}

				/* Obviously a useless MD device, remove it from the list. */
				if (ad.arrayInfo.nr_disks == 0) {
					m_vRAIDInfoList.erase(it);
					WriteHWLog(LOG_LOCAL1, LOG_DEBUG, LOG_LABEL,
							   "[%d] %s's disk number is zero. Remove from the list.\n",
							   __LINE__, mddev.c_str());
				}
			}

			return true;
		}

		it++;
	}

	unsigned char* p_uuid = (unsigned char*) uuid;
	string strUUID("");
	for (int i = 0; i < 16; i++) {
		strUUID += string_format("%02X ", p_uuid[i]);
	}
	WriteHWLog(LOG_LOCAL1, LOG_DEBUG, LOG_LABEL, "MD device corresponding to %swas not found.\n", strUUID.c_str());
	return false;
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
#ifdef NUUO
	CriticalSectionLock cs(&m_csRAIDInfoList);
#endif	
	// If MD device is stopped, we can free MD number immediately.
	for (size_t i = 0; i < it->m_vDiskList.size(); i++) {
		CriticalSectionLock csSymLinkTable(&m_csSymLinkTable);
		m_mapSymLinkTable[it->m_vDiskList[i].m_strDevName].m_strMDDev = "";
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
#ifdef NUUO
	CriticalSectionLock cs(&m_csRAIDInfoList);
#endif
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
#ifdef NUUO
	CriticalSectionLock cs(&m_csRAIDInfoList);
#endif
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

#ifdef NUUO
	CriticalSectionLock cs(&m_csRAIDDiskList);
#endif
	vector<RAIDDiskInfo>::iterator it = m_vRAIDDiskList.begin();
	while(it != m_vRAIDDiskList.end()) {
		RAIDDiskInfo info = *it;
		CriticalSectionLock csSymLinkTable(&m_csSymLinkTable);
		info.m_miscInfo = m_mapSymLinkTable[it->m_strDevName]; 
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

#ifdef NUUO
	CriticalSectionLock csRAIDDiskList(&m_csRAIDDiskList);
#endif
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
	info.m_fsMgr->CreateThread();

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
			int num = GetFreeVolumeNum();
			if (num < 0) {
				WriteHWLog(LOG_LOCAL0, LOG_ERR, LOG_LABEL,
					   "Exceed maximal volume limitation.\n");
				return false;
			}
			info.m_fsMgr->SetVolumeNum(num);
		}	

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

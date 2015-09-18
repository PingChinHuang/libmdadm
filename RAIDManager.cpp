#include "RAIDManager.h"

RAIDManager::RAIDManager()
{

}

RAIDManager::~RAIDManager()
{

}

bool RAIDManager::AddRAIDDisk(const string& dev)
{
	/*
		1. Check device node exists or not
			1.1 Yes -> 2
			1.2 No -> return false
		[CS Start] Protect m_vRAIDDiskList
		2. Examine() to check MD superblock
			2.1 Has MD superblock -> 3
			2.2 No MD superblock -> 3
		3. Exist in m_vRAIDDiskList?
			3.1 Yes
				Come from 2.2 return true
				Come from 2.1 -> 4
			3.2 No -> Push into m_vRAIDDiskLisit
				Come from 2.2 return true
				Come from 2.1 -> 4
		[CS End]

		[CS Start] Protect m_vRAIDInfoList
		4. SearchDiskBelong2RAID()
			4.1 Has Active RAID in m_vRAIDInfoList 
				4.1.1 Disk is active/spare in RAID -> return true
				4.1.2 Disk is faulty in RAID -> RemoveDiskFromRAID() -> ReaddDiskIntoRAID() -> 6
				4.1.3 ReaddDiskIntoRAID() -> 6
			4.2 No Active RAID in m_vRAIDInfoList -> 5
		
		5. Check whether there are enough disk for assembling.
			5.1 enough -> AssembleByRAIDUUID() -> 6
			5.2 not enough -> return true

		6.  UpdateRAIDInfo(mddev)
		[CS End]
	*/
}

bool RAIDManager::RemoveRAIDDisk(const string& dev)
{

}

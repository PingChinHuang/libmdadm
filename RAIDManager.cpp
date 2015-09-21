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
	*/
}

vector<RAIDInfo>::iterator RAIDManager::SearchDiskBelong2RAID(const string& dev)
{
	/*
		[CS Start] Protect m_vRAIDInfoList
		1. Search dev in m_vRAIDInfoList.vDiskList
			1.1 Exist a RAID return its iterator
			1.2 Not exist a RAID return m_vRAIDInfoList.end()
		[CS End]		
	*/
}

bool RAIDManager::RemoveRAIDDisk(const string& dev)
{
	/*
		When a disk is removed, it will be marked faulty.
		And just keep this status and it should be updated
		after UpdateRAIDInfo finished.


		[CS Start] Protect m_vRAIDDiskList
		1. Exist in m_vRAIDDiskList?
			1.1 Yes -> Remove from m_vRAIDDiskList -> 2
			1.2 No -> return true;
		[CS End]

		2. UpdateRAIDInfo(uuid)
	*/
}

bool RAIDManager::UpdateRAIDInfo(const string& mddev)
{
	/*
		Update RAID object by mddev name.
		[CS Start] Protect m_vRAIDInfoList
		1. Detail(mddev)
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

void RAIDManager::InitializeMDDevIdent(struct mddev_ident& ident, const string& str_uuid, int uuid[4], int bitmap_fd, char* bitmap_file)
{
	ident.uuid_set = uuid_set;
	parse_uuid(str_uuid.c_str(), ident.uuid);
	ident.super_minor = UnSet;
	ident.level = UnSet;
	ident.raid_disks = UnSet;
	ident.spare_disks = UnSet;
	ident.bitmap_fd = bitmap_fd;
	ident.bitmap_file = bitmap_file;
}

void RAIDManager::InitializeDevList(struct mddev_dev* devlist, const vector<string>& devNameList, int disposition)
{
	struct mddev_dev** devlistend = &devlist;
	struct mddev_dev* dv = NULL;

	devlist = NULL;
	for (size_t i = 0; i < devNameList.size(); i ++) {
		dv = malloc(sizeof(struct mddev_dev));
		if (dv == NULL) {
			// TODO: Write HW logs.
			return;
		}
		dv->devname = devNameList[i].c_str();
		dv->disposition = 0;
		dv->writemostly = 0;
		dv->used = 0;
		dv->next = NULL;
		*devlistend = dv;
		devlistend = &dv->next;
	}
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

		[CS Start] Protect m_vRAIDDiskList
		3. Check whether devices in devList exist in m_vRAIDDiskList or not.
			Any device does not exist in m_vRAIDDiskList -> return false
		[CS End]

		4. InitializeShape(s, vDevList.size(), level, 512)
		5. InitializeContext(c)
		6. InitializeDevList(devlist, vDevList)
		7. ret = Create(NULL, mddev.c_str(), "\0", NULL, vDevList.size(), devlist, &s, &c, INVALID_SECTORS)
			7.1 ret != 0 -> Write HW Log ->9
			7.2 ret == 0 -> 8
		8. UpdateRAIDInfo(mddev)
		9. FreeDevList(devlist)
			9.1 ret !=  0 -> return false
			9.2 ret = 0 -> return true
	*/
}

bool RAIDManager::AssembleByRAIDUUID(const string& mddev, const string& str_uuid)
{
	/*
		1. Check mddev
			empty -> return false
		2. Check uuid
			NULL -> return false

		3. InitializeMDDevIdent(ident, 1, str_uuid)
		4. ret = Assemble(NULL, mddev.c_str(), &ident, NULL, &c);
			4.1 ret != 0 -> Write HW Log -> return false
		5. return UpdateRAIDInfo(mddev)
	*/	
}

bool RAIDManager::AssembleByRAIDDisks(const string& mddev, const vector<string>& vDevList)
{
	/*
		1. Check mddev
			empty -> return false
		2. Check vDevList 
			empty -> return false

		[CS Start] Protect m_vRAIDDiskList
		3. Check whether devices in devList exist in m_vRAIDDiskList or not.
			Any device does not exist in m_vRAIDDiskList -> return false
		[CS End]

		4. InitializeMDDevIdent(ident, 0, "");
		5. InitalizeDevList(devlist, vDevList);
		6. ret = Assemble(NULL, mddev.c_str(), &ident, devlist, &c);
			6.1 ret != 0 -> Write HW Log
		7. FreeDevList(devlist)
		
		8.
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

		[CS Start] Protect m_vRAIDDiskList
		3. Check whether devices in devList exist in m_vRAIDDiskList or not.
			Any device does not exist in m_vRAIDDiskList -> return false
		[CS End]
	
		4. fd = open_mddev(mddev.c_str(), 1)
			4.1 fd < 0 -> return false
			4.2 other ->5

		5. InitializeDevList(devlist, vDevList, operation);
		6. InitializeContext(c)
		7. ret = Manage_subdevs(mddev.c_str(), fd, devlist, c.verbose, 0, 0, c.force);
			ret != 0 -> Write HW Log
		8. close(fd)
		9. FreeDevList(devlist) 

		10.
		if (ret != 0)
			return false;
		else
			return UpdateRAIDInfo(mddev)
	*/
}

bool RAIDManager::RemoveDisksFromRAID(const string& mddev, const vector<string>& vDevList)
{

}

bool RAIDManager::MarkFaultyDisksInRAID()
{

}

bool RAIDManager::AddDisksIntoRAID()
{

}

bool RAIDManager::ReaddDisksIntoRAID()
{

}

bool RAIDManager::ReplaceDisksInRAID()
{

}

bool RAIDManager::DeleteRAID()
{

}



#include "FilesystemManager.h"

#include "common/file.h"

#include <algorithm>
#include <sys/mount.h>
#include <sys/stat.h>
#include <errno.h>

FilesystemManager::FilesystemManager(const string& dev)
: m_strMountPoint("")
, m_strDevNode(dev)
, m_iFormatingState(WRITE_INODE_TABLES_UNKNOWN)
, m_iFormatProgress(0)
, m_bFormat(false)
, m_bMount(false)
{
	Initialize();
}

FilesystemManager::~FilesystemManager()
{

}

bool FilesystemManager::Initialize()
{
	if (blkid() != 0)
		return false;

	return true;
}

#ifdef NUUO
void FilesystemManager::ThreadProc()
{

}
#endif

bool FilesystemManager::Format()
{

}

bool FilesystemManager::Mount(const string& strMountPoint)
{
	if (strMountPoint.empty())
		return false;

	if (m_strDevNode.empty())
		return false;

#ifdef NUUO
	m_csFormat.Lock();
#endif
	if (!m_bFormat)
		return false;
#ifdef NUUO
	m_csFormat.Unlock();
#endif

#ifdef NUUO
	if (!CheckBlockDevice(m_strDevNode.c_str()))
		return false;

	CriticalSectionLock cs_mount(&m_csMount);
#endif

	if (m_bMount &&
	    !m_strMountPoint.empty() && 
	    m_strMountPoint == strMountPoint &&
#ifdef NUUO
	    CheckDirectoryExist(m_strMountPoint) &&
	    ROOTFS_PROTECT(m_strMountPoint.c_str())
#endif
	) {
		return true;
	}

	if (m_bMount &&
	    !m_strMountPoint.empty() &&
	    m_strMountPoint != strMountPoint) {
		// Filesystem has already mounted,
		// why request to mount again with another mount point?
		return false;
	}

#ifdef NUUO	
	if (!CheckDirectoryExist(strMountPoint)) {
		if (!MakeDirectory(strMountPoint))
			return false;	
	} else if (IsMountPoint(strMountPoint)) {
		// If the directoy exists and it is a mount point
		// do nothing and return false for safety.
		return false;
	}
#endif

	if (mount(m_strDevNode.c_str(), strMountPoint.c_str(),
		  m_strFSType.c_str(), 0, "") < 0) {
		return false;
	}

	m_bMount = true;
	m_strMountPoint = strMountPoint;

	return true; 
}

bool FilesystemManager::Unmount()
{
#ifdef NUUO
	CriticalSectionLock cs(&m_csMount);
#endif

	if (!m_bMount)
		return true;

	if (m_strMountPoint.empty())
		return true;

#ifdef NUUO
	if (!CheckDirectoryExist(m_strMountPoint))
		return true;
#endif
	
	if (!IsMountPoint(m_strMountPoint))
		return true;	

	if (umount2(m_strMountPoint.c_str(), UMOUNT_NOFOLLOW | MNT_DETACH) < 0) {
		fprintf(stderr, "%s, %d, %s\n", __func__, __LINE__, strerror(errno));
		return false;
	}

	m_bMount = false;
	m_strMountPoint = "";

	return true;
}

bool FilesystemManager::IsFormated()
{
#ifdef NUUO
	CriticalSectionLock cs(&m_csFormat);
#endif
	bool ret = m_bFormat;
	return ret;
}

bool FilesystemManager::IsFormating(int& iFormatProgress)
{
#ifdef NUUO
	CriticalSectionLock cs(&m_csFormat);
#endif
	iFormatProgress = m_iFormatProgress;
	if (m_iFormatingState == WRITE_INODE_TABLES_WRITING ||
	    m_iFormatingState == WRITE_INODE_TABLES_INIT)
		return true;

	return false;
}

bool FilesystemManager::IsMounted(string& strMountPoint)
{
#ifdef NUUO
	CriticalSectionLock cs(&m_csMount);
#endif
	strMountPoint = m_strMountPoint;
	return m_bMount;
}

void FilesystemManager::MakeFilesystemProgress(void *pData, int stat,
					       int current, int total)
{
#ifdef NUUO
	CriticalSectionLock cs(&m_csFormat);
#endif
	m_iFormatingState = stat;

	switch (stat) {
	case WRITE_INODE_TABLES_INIT:
		m_bFormat = false;
		m_iFormatProgress = 0;
		break;
	case WRITE_INODE_TABLES_WRITING:
		m_bFormat = false;
		m_iFormatProgress = (int)((double)current / (double)total * 100);
		break;
	case WRITE_INODE_TABLES_DONE:
		m_bFormat = true;
		m_iFormatProgress = 100;
		break;
	case WRITE_INODE_TABLES_ERROR:
		m_bFormat = false;
		m_iFormatProgress = 0;
		break;
	}	
}

void FilesystemManager::GenerateUUIDFile()
{
#ifdef NUUO
	CriticalSectionLock cs_mount(&m_csMount);
#endif

	if (!m_bMount || 
	    m_strMountPoint.empty() ||
#ifdef NUUO
	    !CheckDirectoryExist(m_strMountPoint) ||
	    !ROOTFS_PROTECT(m_strMountPoint.c_str())
#endif
	)
		return;

	string strUUIDFileName = m_strMountPoint + "/uuid";
#ifdef NUUO
	if (CheckFileExist(strUUIDFileName) &&
	    GetFileStorageSize(strUUIDFileName) == 32)
		return;
#endif

#ifdef NUUO
	CriticalSectionLock cs_format(&m_csFormat);
#endif
	if (m_strUUID.empty())
		return;
	
	FILE *pf = fopen(strUUIDFileName.c_str(), "w+");
	if (pf) {
		fprintf(pf, "%s", m_strUUID.c_str());
		fclose(pf);	
	}
}

bool FilesystemManager::CreateDefaultFolders()
{
#ifdef NUUO
	CriticalSectionLock cs(&m_csMount);
#endif

	if (!m_bMount || 
	    m_strMountPoint.empty() ||
#ifdef NUUO
	    !CheckDirectoryExist(m_strMountPoint) ||
	    !ROOTFS_PROTECT(m_strMountPoint.c_str())
#endif
	)
		return false;

	string strFolderName = m_strMountPoint + "/VIDEODATA";
#ifdef NUUO	
	if (!CheckDirectoryExist(strFolderName)) {
		if (!MakeDirectory(strFolderName))
			return false;	
	}
#endif

	strFolderName = m_strMountPoint + "/LOG";
#ifdef NUUO
	if (!CheckDirectoryExist(strFolderName)) {
		if (!MakeDirectory(strFolderName))
			return false;	
	}
#endif

	return true;	
}

int FilesystemManager::blkid()
{
	blkid_cache cache = NULL;
	int retval = 0;

	if (retval = blkid_get_cache(&cache, NULL) < 0) {
		return MKE2FS_FAIL_TO_GET_BLKID_CACHE;
	}

#ifdef NUUO
	m_csFormat.Lock();
#endif
	blkid_dev dev = blkid_get_dev(cache, m_strDevNode.c_str(), BLKID_DEV_NORMAL);
	string devname = blkid_dev_devname(dev);
#ifdef NUUO
	if (!CheckBlockDevice(devname.c_str()))
		return MKE2FS_NOT_BLOCK_DEV;
#endif

	blkid_tag_iterate iter;
	const char* type = NULL;
	const char* value = NULL;
	iter = blkid_tag_iterate_begin(dev);
	while (blkid_tag_next(iter, &type, &value) == 0) {
		if (!strcmp(type, "UUID")) {
			m_strUUID = value;
			m_strUUID.erase(std::remove(m_strUUID.begin(), m_strUUID.end(), '-'), m_strUUID.end()); // Remove '-'
		} 
		if (!strcmp(type, "TYPE")) {
			m_strFSType = value;
			if (m_strFSType == "ext4" ||
			    m_strFSType == "ext3")
				m_bFormat = true;
		} if (!strcmp(type, "LABEL"))
			continue;
	}
	blkid_tag_iterate_end(iter);
#ifdef NUUO
	m_csFormat.Unlock();
#endif

	char mtpt[256];
	int mount_flags = 0;
	mtpt[0] = '\0';
	retval = ext2fs_check_mount_point(m_strDevNode.c_str(), &mount_flags,
					  mtpt, sizeof(mtpt));
	if (retval == 0) {
#ifdef NUUO
		CriticalSectionLock cs(&m_csMount);
#endif
		m_strMountPoint = mtpt;
		if (mount_flags & EXT2_MF_MOUNTED) {
			m_bMount = true;
		} else if (mount_flags & EXT2_MF_BUSY) {
			m_bMount = true;
		} else {
			m_bMount = false;
		}
	} else {
		return MKE2FS_CHECK_MOUNT_POINT_FAIL;
	}

	return 0;
}

void FilesystemManager::Dump()
{
	fprintf(stderr, "Mount Point: %s\nDevice Node: %s\n"
		"UUID: %s\nFS Type: %s\n"
		"Format: %s\nMount: %s\n",
		m_strMountPoint.c_str(), m_strDevNode.c_str(),
		m_strUUID.c_str(), m_strFSType.c_str(),
		m_bFormat?"Yes":"No", m_bMount?"Mounted":"Not mounted");
}

bool FilesystemManager::dostat(const string& path, struct stat *st,
			      int do_lstat, int quiet)
{
	int n = 0;
	
	if (do_lstat)
		n = lstat(path.c_str(), st);
	else
		n = stat(path.c_str(), st);

	if (n != 0) {
		if (!quiet)
			fprintf(stderr, "mountpoint: %s: %s\n",
				path.c_str(), strerror(errno));
		return false;
	}

	return true;
}

bool FilesystemManager::IsMountPoint(const string& path)
{
	struct stat st, st2;
	if (!dostat(path, &st, 1, 0))
		return false;

	if (!dostat(path + "/..", &st2, 1, 0))
		return false;

	if ((st.st_dev != st2.st_dev) ||
	    (st.st_dev == st2.st_dev && st.st_ino == st2.st_ino)) {
		return true;
	}

	return false;
}

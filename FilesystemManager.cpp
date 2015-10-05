#include "FilesystemManager.h"

FilesystemManager::FilesystemManager(const string& dev)
: m_strMountPoint("")
, m_strDevNode(dev)
, m_iFormatingState(WRITE_INODE_TABLES_UNKNOWN)
, m_iFormatProgress(0)
, m_bFormat(false)
, m_bMount(false)
{

}

FilesystemManager::~FilesystemManager()
{

}

bool FilesystemManager::Initialize()
{
/*	ext2_filsys fs;
	int flags = EXT2_FLAG_JOURNAL_DEV_OK |
		    EXT2_FLAG_SOFTSUPP_FEATURES |
		    EXT2_FLAG_64BITS;
	int retval = 0;

#ifdef NUUO
	CriticalSectionLock cs(&m_csFormat);
#endif

	retval = ext2fs_open(m_strDevNode.c_str(), flags,
			     0, 0, unix_io_manager, &fs);
	if (retval) {
		return false;
	}
	
	ext2fs_close(fs);*/



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

bool FilesystemManager::Mount()
{

}

bool FilesystemManager::Unmount()
{

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

}

bool FilesystemManager::CreateDefaultFolders()
{

}

int FilesystemManager::blkid()
{
	blkid_cache cache = NULL;
	int retval = 0;
	if (retval = blkid_get_cache(&cache, NULL) < 0) {
		return retval;
	}

	blkid_dev dev = blkid_get_dev(cache, m_strDevNode.c_str(), BLKID_DEV_NORMAL);
	m_strDevNode = blkid_dev_devname(dev);
	if (access(m_strDevNode.c_str(), F_OK))
		return retval;

	blkid_tag_iterate iter;
	const char* type = NULL;
	const char* value = NULL;
	iter = blkid_tag_iterate_begin(dev);
	while (blkid_tag_next(iter, &type, &value) == 0) {
		if (!strcmp(type, "UUID"))
			m_strUUID = value;
		if (!strcmp(type, "TYPE"))
			m_strFSType = value;
		if (!strcmp(type, "LABEL"))
			continue;
	}
	blkid_tag_iterate_end(iter);

	char mtpt[256];

	int mount_flags = 0;
	retval = ext2fs_check_mount_point(m_strDevNode.c_str(), &mount_flags,
					  mtpt, sizeof(mtpt));
	if (retval == 0) {
		m_strMountPoint = mtpt;
		if (mount_flags & EXT2_MF_MOUNTED) {
			m_bMount = true;
		} else if (mount_flags & EXT2_MF_BUSY) {
			m_bMount = true;
		} else {
			m_bMount = false;
		}
	}

	return 0;
}



#include "FilesystemManager.h"

#ifdef NUUO
#include "common/file.h"
#include "common/nusyslog.h"
#include "common/string.h"
#include "common/system.h"
#else
#include "test_utils.h"
#endif

#include <algorithm>
#include <sys/mount.h>
#include <sys/stat.h>
#include <errno.h>

#define LOG_LABEL "FSManager"

FilesystemManager::FilesystemManager(const string& dev)
: m_strMountPoint("")
, m_strDevNode(dev)
, m_strUUID("")
, m_strFSType("")
, m_iVolumeNum(-1)
, m_iFormatingState(WRITE_INODE_TABLES_UNKNOWN)
, m_iFormatProgress(0)
, m_bFormat(false)
, m_bMount(false)
{
	Initialize();
}

FilesystemManager::FilesystemManager()
: m_strMountPoint("")
, m_strDevNode("")
, m_strUUID("")
, m_strFSType("")
, m_iVolumeNum(-1)
, m_iFormatingState(WRITE_INODE_TABLES_UNKNOWN)
, m_iFormatProgress(0)
, m_bFormat(false)
, m_bMount(false)
{
}

FilesystemManager::~FilesystemManager()
{

}

bool FilesystemManager::SetDeviceNode(const string &dev)
{
	m_strDevNode = dev;
	return Initialize();
}

bool FilesystemManager::Initialize()
{
	if (m_strDevNode.empty()) {
		WriteHWLog(LOG_LOCAL0, LOG_ERR, LOG_LABEL,
		 	   "Device is no specified.");
		return false;
	}
	
#ifdef NUUO
	if (!CheckBlockDevice(m_strDevNode.c_str())) {
		WriteHWLog(LOG_LOCAL0, LOG_ERR, LOG_LABEL,
		 	   "%s is not a block device.",
		   	   m_strDevNode.c_str());
		return false;
	}
#endif

	if (blkid() != 0) {
		WriteHWLog(LOG_LOCAL0, LOG_ERR, LOG_LABEL,
		 	   "%s initialize failed.",
			   m_strDevNode.c_str());
		return false;
	}

	return true;
}

void FilesystemManager::SetMountPoint(const string &mountpoint)
{
	m_strMountPoint = mountpoint;
}

#ifdef NUUO
void FilesystemManager::ThreadProc()
{
	if (!Format()) {
		return;
	}

	SleepMS(1000); // Wait for MD device's fs ready.

	if (!Mount(m_strMountPoint)) {
		return;
	}

	GenerateUUIDFile();
	CreateDefaultFolders();
	Dump();
}
#endif

bool FilesystemManager::Format(bool force)
{
	int ret = 0;
#ifdef NUUO
	CriticalSectionLock cs(&m_csFormat);
#endif
	if (m_bFormat && !force)
		goto format_done;	

	InitializeMke2fsHandle();
	ret = mke2fs(&m_mkfsHandle);
	if (0 != ret) {
		WriteHWLog(LOG_LOCAL0, LOG_ERR, LOG_LABEL,
			   "Fail to format %s [%d]\n", m_strDevNode.c_str(), ret);
		return false;
	}

format_done:
	WriteHWLog(LOG_LOCAL0, LOG_INFO, LOG_LABEL,
		   "Format %s successfully.\n", m_strDevNode.c_str());
	return true;
}

bool FilesystemManager::Mount(const string& strMountPoint)
{
	string strErrorLog;

	if (strMountPoint.empty()) {
		strErrorLog = "Invalid mount point.";
		goto mount_err;
	}

	if (m_strDevNode.empty()) {
		strErrorLog = "Invalid device.";
		goto mount_err;
	}

#ifdef NUUO
	m_csFormat.Lock();
#endif
	if (!m_bFormat) {
		strErrorLog = "The device isn't formated.";
#ifdef NUUO
		m_csFormat.Unlock();
#endif
		goto mount_err;
	}
#ifdef NUUO
	m_csFormat.Unlock();
#endif

#ifdef NUUO
	if (!CheckBlockDevice(m_strDevNode.c_str())) {
		strErrorLog = "The device is not a block device.";
		goto mount_err;
	}

	m_csMount.Lock();
#endif

	if (m_bMount
	    && !m_strMountPoint.empty()
	    && m_strMountPoint == strMountPoint
#ifdef NUUO
	    && CheckDirectoryExist(m_strMountPoint)
	    && ROOTFS_PROTECT(m_strMountPoint.c_str())
#endif
	) {
		goto mount_ok;
	}

	if (m_bMount &&
	    !m_strMountPoint.empty() &&
	    m_strMountPoint != strMountPoint) {
		// Filesystem has already mounted,
		// why request to mount again with another mount point?
		strErrorLog = "The device has already be mounted. Why request to mount again with a different mount point?";
		goto mount_err;
	}

#ifdef NUUO	
	if (!CheckDirectoryExist(strMountPoint)) {
		if (!MakeDirectory(strMountPoint)) {
			strErrorLog = "Fail to create mount point.";
			goto mount_err;	
		}
	} else
#endif
	if (IsMountPoint(strMountPoint)) {
		// If the directoy exists and it is a mount point
		// do nothing and return false for safety.
		strErrorLog = "The mount point is using by another device..";
		goto mount_err;
	}

	if (mount(m_strDevNode.c_str(), strMountPoint.c_str(),
		  m_strFSType.c_str(), 0, "") < 0) {
		strErrorLog = string_format("Fail to mount %s to %s. (%s)",
					  m_strDevNode.c_str(),
					  m_strMountPoint.c_str(),
					  strerror(errno));
		goto mount_err;
	}

mount_ok:
	m_bMount = true;
	m_strMountPoint = strMountPoint;
	m_csMount.Unlock();
	WriteHWLog(LOG_LOCAL0, LOG_INFO, LOG_LABEL,
		   "%s has be mounted to %s successfully.",
		   m_strDevNode.c_str(),
		   m_strMountPoint.c_str());

	return true;

mount_err:
	m_csMount.Unlock();
	WriteHWLog(LOG_LOCAL0, LOG_ERR, LOG_LABEL,
		   "%s", strErrorLog.c_str());
	return false; 
}

bool FilesystemManager::Unmount()
{
#ifdef NUUO
	CriticalSectionLock cs(&m_csMount);
#endif

	if (!m_bMount)
		goto unmount_done;

	if (m_strMountPoint.empty())
		goto unmount_done;

#ifdef NUUO
	if (!CheckDirectoryExist(m_strMountPoint))
		goto unmount_done;
#endif
	
	if (!IsMountPoint(m_strMountPoint))
		goto unmount_done;	

	if (umount2(m_strMountPoint.c_str(), UMOUNT_NOFOLLOW | MNT_DETACH) < 0) {
		WriteHWLog(LOG_LOCAL0, LOG_ERR, LOG_LABEL, "Fail to unmount %s (%s)\n",
			   m_strMountPoint.c_str(), strerror(errno));
		return false;
	}

	// TODO: Mount point should be deleted when unmounting successfully.
	// Maybe it cannot be removed due to using by other processes.
	// This should be handle first.

unmount_done:
	WriteHWLog(LOG_LOCAL0, LOG_INFO, LOG_LABEL,
		   "%s has be unmounted successfully.",
		   m_strMountPoint.c_str());
	m_bMount = false;
	m_strMountPoint = "";

	return true;
}

bool FilesystemManager::IsFormated()
{
	return m_bFormat;
}

bool FilesystemManager::IsFormating(int& progress, int& stat)
{
	progress = m_iFormatProgress;
	stat = m_iFormatingState;
	if (m_iFormatingState == WRITE_INODE_TABLES_WRITING ||
	    m_iFormatingState == WRITE_INODE_TABLES_WRITING_DONE ||
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

bool FilesystemManager::IsMounted(int& num)
{
#ifdef NUUO
	CriticalSectionLock cs(&m_csMount);
#endif
	num = m_iVolumeNum;
	return m_bMount;
}

void FilesystemManager::SetFormatInfo(bool format, int progress, int stat)
{
	m_bFormat = format;
	m_iFormatProgress = progress;
	m_iFormatingState = stat;
}

void FilesystemManager::MakeFilesystemProgress(void *pData, int stat,
					       int current, int total)
{
	if (pData == NULL)
		return;
	FilesystemManager *pFSMgr = (FilesystemManager*) pData; 

	switch (stat) {
	case WRITE_INODE_TABLES_INIT:
		pFSMgr->SetFormatInfo(false, 0, stat);
		break;
	case WRITE_INODE_TABLES_WRITING:
	case WRITE_INODE_TABLES_WRITING_DONE:
		pFSMgr->SetFormatInfo(false, (int)((double)current / (double)total * 100), stat);
		break;
	case WRITE_INODE_TABLES_DONE:
		pFSMgr->SetFormatInfo(true, 100, stat);
		break;
	case WRITE_INODE_TABLES_ERROR:
		pFSMgr->SetFormatInfo(false, 0, stat);
		break;
	}	
}

void FilesystemManager::GenerateUUIDFile()
{
#ifdef NUUO
	CriticalSectionLock cs_mount(&m_csMount);
#endif

	if (!m_bMount || m_strMountPoint.empty() 
#ifdef NUUO
	    || !CheckDirectoryExist(m_strMountPoint) ||
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

	if (!m_bMount || m_strMountPoint.empty()
#ifdef NUUO
	    || !CheckDirectoryExist(m_strMountPoint) ||
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
		WriteHWLog(LOG_LOCAL1, LOG_ERR, LOG_LABEL,
			   "[%d] Fail to get blkid cache.", __LINE__);
		return MKE2FS_FAIL_TO_GET_BLKID_CACHE;
	}

#ifdef NUUO
	m_csFormat.Lock();
#endif
	blkid_dev dev = blkid_get_dev(cache, m_strDevNode.c_str(), BLKID_DEV_NORMAL);
	const char *devname = blkid_dev_devname(dev);
#ifdef NUUO
	if (!CheckBlockDevice(m_strDevNode)) {
		WriteHWLog(LOG_LOCAL1, LOG_ERR, LOG_LABEL,
			   "[%d] %s is not a block device.", __LINE__, m_strDevNode.c_str());
		return MKE2FS_NOT_BLOCK_DEV;
	}
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
		int ret = 0;
		m_strMountPoint = mtpt;
		if (mount_flags & EXT2_MF_MOUNTED) {
			m_bMount = true;
		} else if (mount_flags & EXT2_MF_BUSY) {
			m_bMount = true;
		} else {
			m_bMount = false;
		}

#ifdef NUUO
		/*
			If mount point pattern is not "/mnt/VOLUMEX",
			just unmount it and reset the related data..
		*/
		if (m_bMount) {
			ret = sscanf(mtpt, "/mnt/VOLUME%d", &m_iVolumeNum);
			if (ret < 0 || ret == EOF || m_iVolumeNum > 128 ||
			    m_iVolumeNum < 1) {
				m_iVolumeNum = -1;
				if (umount2(m_strMountPoint.c_str(),
					    UMOUNT_NOFOLLOW | MNT_DETACH) < 0) {
					WriteHWLog(LOG_LOCAL0, LOG_ERR, LOG_LABEL, "Fail to unmount %s (%s)\n",
			   			   m_strMountPoint.c_str(),
						   strerror(errno));
				} else {
					m_bMount = false;
					m_strMountPoint = "";
				}
			} else {
				/*
					Volume Number is start from 0.
					But the mount point name is start from 1.
				*/
				m_iVolumeNum--;
			}
		}
#endif
	} else {
		WriteHWLog(LOG_LOCAL1, LOG_ERR, LOG_LABEL,
			   "[%d] Fail to check mount point.", __LINE__);
		return MKE2FS_CHECK_MOUNT_POINT_FAIL;
	}

	return 0;
}

void FilesystemManager::Dump()
{
	fprintf(stdout, "\tMount Point: %s(%s), Filesystem: %s(%s)\n"
		"\tUUID %s\n",
		m_strMountPoint.c_str(), m_bMount?"Mounted":"Not mounted",
		m_strFSType.c_str(), m_bFormat?"Formated":"Not Formated",
		m_strUUID.c_str());
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

void FilesystemManager::InitializeMke2fsHandle()
{
	memset(&m_mkfsHandle.cfg, 0x00, sizeof(struct e2fs_cfg));
	strncpy(m_mkfsHandle.device_name, m_strDevNode.c_str(),
		sizeof(m_mkfsHandle.device_name));
	m_mkfsHandle.cb_func = MakeFilesystemProgress;
	m_mkfsHandle.buf = NULL;
	m_mkfsHandle.pData = this;

	m_mkfsHandle.cfg.blocksize = 4096;
	m_mkfsHandle.cfg.reserved_ratio = 1;
	m_mkfsHandle.cfg.r_opt = -1;
	m_mkfsHandle.cfg.force = 1;
	m_mkfsHandle.cfg.quiet = 1;
#ifdef DEBUG
	m_mkfsHandle.cfg.verbose = 1;
#else
	m_mkfsHandle.cfg.verbose = 0;
#endif
	m_mkfsHandle.cfg.creator_os = EXT2_OS_LINUX;
	strncpy(m_mkfsHandle.cfg.fs_type, "ext4", strlen("ext4"));
}

void FilesystemManager::SetVolumeNum(const int &num)
{
	m_iVolumeNum = num;
	m_strMountPoint = string_format("/mnt/VOLUME%d", m_iVolumeNum + 1);
}

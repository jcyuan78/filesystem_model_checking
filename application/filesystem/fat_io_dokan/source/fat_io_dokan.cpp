// fat_io_dokan.cpp : Defines the exported functions for the DLL application.
//

#include "stdafx.h"
//extern "C" {
#include "../../fat_io_lib/fat_io_lib.h"
//}
#include "fat_io_dokan.h"

LOCAL_LOGGER_ENABLE(L"fat.fs", LOGGER_LEVEL_NOTICE);


//jcvos::CStaticInstance<CFSFatIo>	g_fs;

//int media_read(uint32 sector, uint8 * buffer, uint32 sector_count)
//{
//	bool br = g_fs.MediaRead(sector, buffer, sector_count);
//	return br;
//}
//
//int media_write(uint32 sector, uint8 * buffer, uint32 sector_count)
//{
//	bool br = g_fs.MediaWrite(sector, buffer, sector_count);
//	return br;
//}
//
//int media_sync(void)
//{
//	bool br = g_fs.Sync();
//	return br;
//}


CFSFatIo::CFSFatIo(void)
{
	memset(&m_fs, 0, sizeof(fatfs));
//	m_fs.m_disk_io = NULL;
}

CFSFatIo::~CFSFatIo(void)
{
	//JCASSERT(m_dev == NULL);
//	RELEASE(m_dev);
//	fl_shutdown();
	delete m_fs.m_disk_io;
}

bool CFSFatIo::ConnectToDevice(IVirtualDisk * dev)
{
	if (m_fs.m_disk_io != NULL) THROW_ERROR(ERR_APP, L"device has already connected");
	if (dev == NULL) THROW_ERROR(ERR_APP, L"device cannot be null");
	//m_disk_io = new disk_if(dev);
	m_fs.m_disk_io = new disk_if(dev);
	//m_dev = dev;
	//m_dev->AddRef();
	//m_disk_io.
	return true;
}

void CFSFatIo::Disconnect(void)
{
	//RELEASE(m_dev);
	delete m_fs.m_disk_io;
	m_fs.m_disk_io = NULL;
}

bool CFSFatIo::Mount(IVirtualDisk * dev)
{
	ConnectToDevice(dev);
	int ir;
	fl_init(&m_fs);
	//ir = fl_attach_media(&m_fs, m_disk_io);
	ir = fatfs_init(&m_fs);
	if (ir!=FAT_INIT_OK)
	{
		fl_shutdown(&m_fs);
		Disconnect();
		LOG_ERROR(L"[err] failed on initial, res=%d", ir);
		return false;
	}
	m_fs.m_filelib_valid = 1;
	return true;
}

void CFSFatIo::Unmount(void)
{	// 清理FS，关闭所有已打开的文件，回收资源
	fl_shutdown(&m_fs);
//	RELEASE(m_dev);
	Disconnect();
}

bool CFSFatIo::DokanGetDiskSpace(ULONGLONG & free_bytes, ULONGLONG & total_bytes, ULONGLONG & total_free_bytes)
{
	//JCASSERT(m_fs);
	uint32 free_cluster = fatfs_count_free_clusters(&m_fs)-1024;
	free_bytes = free_cluster * m_fs.sectors_per_cluster * SECTOR_SIZE;
	total_free_bytes = free_bytes;
	total_bytes = m_fs.m_disk_io->m_disk->GetCapacity();
	return true;
}

bool CFSFatIo::GetVolumnInfo(std::wstring & vol_name,
	DWORD & sn, DWORD & max_fn_len, DWORD & fs_flag, std::wstring & fs_name)
{
	//JCASSERT(m_fs);
	vol_name = L"TEST";
	sn = 0;
	fs_name = L"fat";
	return true;
}

bool CFSFatIo::DokanCreateFile(IFileInfo *& file, const std::wstring & _fn,
	ACCESS_MASK access_mask, DWORD attr, FsCreateDisposition disp, ULONG share, ULONG opt, bool isdir)
{
	JCASSERT(file==NULL);

	std::wstring fn = L"C:";
	fn += _fn;

	std::string str_fn;
	jcvos::UnicodeToUtf8(str_fn, fn);
	char mode[10] = { 0 };
	if (access_mask & GENERIC_WRITE)
	{
		mode[0] = 'w', mode[1]=0;
		if (access_mask & GENERIC_READ) mode[1] = '+', mode[2]=0;
	}
//	else if (access_mask & GENERIC_READ || access_mask & GENERIC_EXECUTE) mode[0] = 'r', mode[1]=0;
	else { mode[0] = 'r', mode[1] = 0; }
	LOG_DEBUG(L"mode=%S", mode);

	jcvos::auto_interface<CFileInfoFatIo> ff(jcvos::CDynamicInstance<CFileInfoFatIo>::Create());
	ff->Init(&m_fs);
	if (isdir || *(fn.rbegin()) == '\\')
	{	// 处理目录，尝试打开目录
		ff->m_isdir = true;
		FL_DIR * dd = fl_opendir(&m_fs, str_fn.c_str(), &ff->m_dir);

		if (dd)
		{
			if (disp == IFileSystem::FS_CREATE_NEW)
			{
				LOG_ERROR_EX(1128, L"dir %s has already existed while CREATE_NEW", _fn.c_str());
				return false;
			}
		}
		else
		{
			if (disp == IFileSystem::FS_OPEN_EXISTING)
			{
				LOG_ERROR_EX(1136, L"dir %s does not existed while OPEN_EXISTING", _fn.c_str());
				return false;
			}
			else
			{
				int ir = fl_createdirectory(&m_fs, str_fn.c_str());
				if (ir == 0)
				{
					LOG_ERROR_EX(1143, L"failed on creating dir: %s", _fn.c_str());
					return false;
				}
				dd = fl_opendir(&m_fs, str_fn.c_str(), &ff->m_dir);
				if (!dd)
				{
					LOG_ERROR_EX(1143, L"failed on open created dir: %s", _fn.c_str());
					return false;
				}
			}
		}
	}
	else
	{
		ff->m_isdir = false;
		ff->m_fn = fn;
		// try to open file
		void *f = fl_fopen(&m_fs, str_fn.c_str(), mode);
		// case 1: create file
		if (f)
		{
			ff->m_file = reinterpret_cast<FL_FILE*>(f);
			ff.detach(file);
			return true;
		}

		// retry for create
		LOG_DEBUG(L"mode=%S, open fail", mode);
		if (disp == IFileSystem::FS_CREATE_NEW || disp == IFileSystem::FS_CREATE_ALWAYS 
			|| disp== IFileSystem::FS_OPEN_ALWAYS)
		{
			f = fl_fopen(&m_fs, str_fn.c_str(), "a+");
			if (f)
			{
				ff->m_file = reinterpret_cast<FL_FILE*>(f);
				ff.detach(file);
				return true;
			}
			LOG_ERROR(L"failed on creating file %s", fn.c_str());
			return false;
		}
		else if (disp == IFileSystem::FS_OPEN_EXISTING)
		{	// maybe file has already opened, try again
			f = fl_fopen(&m_fs, str_fn.c_str(), mode);
			if (f)
			{
				ff->m_file = reinterpret_cast<FL_FILE*>(f);
				ff.detach(file);
				return true;
			}
			LOG_ERROR(L"[err] failed on open existing file %s", fn.c_str());
			return false;
		}
		return false;
	}
	ff.detach(file);
	return true;
}

bool CFSFatIo::DokanDeleteFile(const std::wstring & fn, IFileInfo * file, bool isdir)
{
	std::string str_fn;
	jcvos::UnicodeToUtf8(str_fn, fn);
	int ir = fl_remove(&m_fs, str_fn.c_str());
	if (ir) return true;
	else return false;
}

bool CFSFatIo::MakeFileSystem(IVirtualDisk * dev, UINT32 volume_secs, const std::wstring & volume_name)
{
	ConnectToDevice(dev);
	fl_init(&m_fs);
	std::string str_vol;
	jcvos::UnicodeToUtf8(str_vol, volume_name);
	//pre_attach_media(media_read, media_write, media_sync);

	bool br = true;
	int ir = fl_format(&m_fs, volume_secs, str_vol.c_str());
	if (ir == 0)
	{
		LOG_ERROR(L"[err] failed on formatting disk.");
	}
	fl_shutdown(&m_fs);
	Disconnect();
	return br;
}

IFileSystem::FsCheckResult CFSFatIo::FileSystemCheck(IVirtualDisk* dev, bool repair)
{
	ConnectToDevice(dev);
	fl_init(&m_fs);
	IFileSystem::FsCheckResult res = IFileSystem::CheckNoError;
	int ir = fl_fsck(&m_fs, repair);
	if (!ir)
	{
		LOG_ERROR(L"[err] fs check failed");
		res = IFileSystem::CheckFailed;
	}
	fl_shutdown(&m_fs);
	Disconnect();
	LOG_NOTICE(L"no error was found");
	return res;
}

bool CFSFatIo::Sync(void)
{
	bool br = m_fs.m_disk_io->m_disk->FlushData(0, 0);
	return br;
}
//
//bool CFSFatIo::MediaRead(UINT sector, void * buffer, size_t sector_count)
//{
//	bool br = m_dev->ReadSectors(buffer, sector, sector_count);
//	return br;
//}
//
//bool CFSFatIo::MediaWrite(UINT sector, void * buffer, size_t sector_count)
//{
//	bool br = m_dev->WriteSectors(buffer, sector, sector_count);
//	return br;
//}





///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// -- CFileInfo

void CFileInfoFatIo::Cleanup(void)
{
	if (!m_isdir)
	{
		if (m_file)	fl_fclose(m_fs, m_file);
		m_file = NULL;
	}
}

void CFileInfoFatIo::CloseFile(void)
{
	if (m_isdir)
	{
		fl_closedir(m_fs, &m_dir);
	}
	else
	{
		if (m_file)	fl_fclose(m_fs, m_file);
		m_file = NULL;
	}
}

bool CFileInfoFatIo::DokanReadFile(LPVOID buf, DWORD len, DWORD & read, LONGLONG offset)
{
	JCASSERT(m_file);
	int ir = fl_fseek(m_fs, m_file, (long)offset, SEEK_SET);
	ir = fl_fread(m_fs, buf, len, 1, m_file);
	read = ir;
	return (ir!=0);
}

bool CFileInfoFatIo::DokanWriteFile(LPCVOID buf, DWORD len, DWORD & written, LONGLONG offset)
{
	JCASSERT(m_file);
	int ir = fl_fseek(m_fs, m_file, (long)offset, SEEK_SET);
	void * bb = (void*)buf;
	ir = fl_fwrite(m_fs, bb, len, 1, m_file);
	written = ir;
	if (ir != 0) return true;
	else	return false;
}

bool CFileInfoFatIo::EnumerateFiles(EnumFileListener * listener) const
{
	JCASSERT(listener);
	if (!m_isdir) return false;
	struct fs_dir_ent dirent;
	FL_DIR * dir = const_cast<FL_DIR*>(&m_dir);
	while (fl_readdir(m_fs, dir, &dirent) == 0)
	{
#if FATFS_INC_TIME_DATE_SUPPORT
		int d, m, y, h, mn, s;
		fatfs_convert_from_fat_time(dirent.write_time, &h, &m, &s);
		fatfs_convert_from_fat_date(dirent.write_date, &d, &mn, &y);
		FAT_PRINTF(("%02d/%02d/%04d  %02d:%02d      ", d, mn, y, h, m));
#endif
		std::wstring fn;
		jcvos::Utf8ToUnicode(fn, dirent.filename);
		BY_HANDLE_FILE_INFORMATION finfo;
		memset(&finfo, 0, sizeof(BY_HANDLE_FILE_INFORMATION));
		finfo.nFileSizeLow = dirent.size;
		finfo.nFileIndexLow = dirent.cluster;
		if (dirent.is_dir) finfo.dwFileAttributes |= FILE_ATTRIBUTE_DIRECTORY;

		if (!listener->EnumFileCallback(fn, 0, 0, &finfo))	break;
	}
	return true;
}

bool CFileInfoFatIo::GetFileInformation(LPBY_HANDLE_FILE_INFORMATION fileinfo) const
{
	JCASSERT(fileinfo);
	memset(fileinfo, 0, sizeof(BY_HANDLE_FILE_INFORMATION));
	if (m_isdir)
	{ 
		fileinfo->dwFileAttributes |= FILE_ATTRIBUTE_DIRECTORY;
		fileinfo->nFileIndexLow = m_dir.cluster;
	}
	else
	{
		JCASSERT(m_file);
		if (m_file->flags & FILE_ATTR_READ_ONLY)	fileinfo->dwFileAttributes |= FILE_ATTRIBUTE_READONLY;
		if (m_file->flags & FILE_ATTR_HIDDEN)		fileinfo->dwFileAttributes |= FILE_ATTRIBUTE_HIDDEN;
		if (m_file->flags & FILE_ATTR_SYSTEM)		fileinfo->dwFileAttributes |= FILE_ATTRIBUTE_SYSTEM;
		if (m_file->flags & FILE_ATTR_DIRECTORY)	fileinfo->dwFileAttributes |= FILE_ATTRIBUTE_DIRECTORY;
		if (m_file->flags & FILE_ATTR_ARCHIVE)	fileinfo->dwFileAttributes |= FILE_ATTRIBUTE_ARCHIVE;

		fileinfo->nFileSizeLow = m_file->filelength;
		fileinfo->nFileIndexLow = m_file->startcluster;
		fileinfo->nNumberOfLinks = 1;
	}
	return true;
}

bool CFileInfoFatIo::SetEndOfFile(LONGLONG new_size)
{
	return true;
}

bool CFileInfoFatIo::FlushFile(void)
{
	JCASSERT(m_file);
	int ir = fl_fflush(m_fs, m_file);
	return (ir==0);
}

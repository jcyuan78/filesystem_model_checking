// fat_io_dokan.cpp : Defines the exported functions for the DLL application.
//

#include "stdafx.h"
extern "C" {
#include "../../fat_io_lib/fat_io_lib.h"
}
#include "fat_io_dokan.h"

LOCAL_LOGGER_ENABLE(L"fat.fs", LOGGER_LEVEL_DEBUGINFO);


jcvos::CStaticInstance<CFSFatIo>	g_fs;

int media_read(uint32 sector, uint8 * buffer, uint32 sector_count)
{
	bool br = g_fs.MediaRead(sector, buffer, sector_count);
	return br;
}

int media_write(uint32 sector, uint8 * buffer, uint32 sector_count)
{
	bool br = g_fs.MediaWrite(sector, buffer, sector_count);
	return br;
}

CFSFatIo::CFSFatIo(void) : m_fs(NULL), m_dev(NULL)
{
	fl_init();
}

CFSFatIo::~CFSFatIo(void)
{
	RELEASE(m_dev);
	fl_shutdown();
}

bool CFSFatIo::ConnectToDevice(IVirtualDisk * dev)
{
	JCASSERT(dev);

	m_dev = dev;
	m_dev->AddRef();
//	fl_init();
	return true;
}

void CFSFatIo::Disconnect(void)
{
	RELEASE(m_dev);
//	fl_shutdown();
}

bool CFSFatIo::Mount(void)
{
	int ir;
	ir = fl_attach_media(media_read, media_write);
	if (ir!=FAT_INIT_OK)
	{
		LOG_ERROR(L"[err] failed on attach to media, res=%d", ir);
		return false;
	}
	m_fs = fl_get_fs();
	if (!m_fs)
	{
		LOG_ERROR(L"[err] failed on getting fs");
		return false;
	}

	return true;
}

bool CFSFatIo::DokanGetDiskSpace(ULONGLONG & free_bytes, ULONGLONG & total_bytes, ULONGLONG & total_free_bytes)
{
	JCASSERT(m_fs);
	uint32 free_cluster = fatfs_count_free_clusters(m_fs)-1024;
	free_bytes = free_cluster * m_fs->sectors_per_cluster * SECTOR_SIZE;
	total_free_bytes = free_bytes;
	total_bytes = m_dev->GetCapacity();
	return true;
}

bool CFSFatIo::GetVolumnInfo(std::wstring & vol_name,
	DWORD & sn, DWORD & max_fn_len, DWORD & fs_flag, std::wstring & fs_name)
{
	JCASSERT(m_fs);
	vol_name = L"TEST";
	sn = 0;
	fs_name = L"fat";
	return true;
}

bool CFSFatIo::DokanCreateFile(IFileInfo *& file, const std::wstring & _fn,
	ACCESS_MASK access_mask, DWORD attr, DWORD disp, ULONG share, ULONG opt, bool isdir)
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
	if (isdir || *(fn.rbegin()) == '\\')
	{	// 处理目录，尝试打开目录
		ff->m_isdir = true;
		FL_DIR * dd = fl_opendir(str_fn.c_str(), &ff->m_dir);

		if (dd)
		{
			if (disp == CREATE_NEW)
			{
				LOG_ERROR_EX(1128, L"dir %s has already existed while CREATE_NEW", _fn.c_str());
				return false;
			}
		}
		else
		{
			if (disp == OPEN_EXISTING)
			{
				LOG_ERROR_EX(1136, L"dir %s does not existed while OPEN_EXISTING", _fn.c_str());
				return false;
			}
			else
			{
				int ir = fl_createdirectory(str_fn.c_str());
				if (ir == 0)
				{
					LOG_ERROR_EX(1143, L"failed on creating dir: %s", _fn.c_str());
					return false;
				}
				dd = fl_opendir(str_fn.c_str(), &ff->m_dir);
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
		void *f = fl_fopen(str_fn.c_str(), mode);
		// case 1: create file
		if (f)
		{
			ff->m_file = reinterpret_cast<FL_FILE*>(f);
			ff.detach(file);
			return true;
		}

		// retry for create
		LOG_DEBUG(L"mode=%S, open fail", mode);
		if (disp == CREATE_NEW || disp == CREATE_ALWAYS || disp==OPEN_ALWAYS)
		{
			f = fl_fopen(str_fn.c_str(), "a+");
			if (f)
			{
				ff->m_file = reinterpret_cast<FL_FILE*>(f);
				ff.detach(file);
				return true;
			}
			LOG_ERROR(L"failed on creating file %s", fn.c_str());
			return false;
		}
		else if (disp == OPEN_EXISTING)
		{	// maybe file has already opened, try again
			f = fl_fopen(str_fn.c_str(), mode);
			if (f)
			{
				ff->m_file = reinterpret_cast<FL_FILE*>(f);
				ff.detach(file);
				return true;
			}
			LOG_ERROR(L"failed on open existing file %s", fn.c_str());
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
	int ir = fl_remove(str_fn.c_str());
	if (ir) return true;
	else return false;
}

bool CFSFatIo::MakeFileSystem(UINT32 volume_secs, const std::wstring & volume_name)
{
	JCASSERT(m_dev);
	std::string str_vol;
	jcvos::UnicodeToUtf8(str_vol, volume_name);
	pre_attach_media(media_read, media_write);

	int ir = fl_format(volume_secs, str_vol.c_str());
	if (ir == 0)
	{
		LOG_ERROR(L"[err] failed on formatting disk.");
		return false;
	}
	return true;
}

IFileSystem::FsCheckResult CFSFatIo::FileSystemCheck(bool repair)
{
	int ir = fl_fsck(repair);
	if (!ir)
	{
		LOG_ERROR(L"[err] fs check failed");
		return IFileSystem::CheckFailed;
	}
	LOG_NOTICE(L"no error was found");
	return IFileSystem::CheckNoError;
}

bool CFSFatIo::MediaRead(UINT sector, void * buffer, size_t sector_count)
{
	bool br = m_dev->ReadSectors(buffer, sector, sector_count);
	return br;
}

bool CFSFatIo::MediaWrite(UINT sector, void * buffer, size_t sector_count)
{
	bool br = m_dev->WriteSectors(buffer, sector, sector_count);
	return br;
}



///////////////////////////////////////////////////////////////////////////////
// -- CFileInfo

void CFileInfoFatIo::Cleanup(void)
{
	if (!m_isdir)
	{
		if (m_file)	fl_fclose(m_file);
		m_file = NULL;
	}
}

void CFileInfoFatIo::CloseFile(void)
{
	if (m_isdir)
	{
		fl_closedir(&m_dir);
	}
	else
	{
		if (m_file)	fl_fclose(m_file);
		m_file = NULL;
	}
}

bool CFileInfoFatIo::DokanReadFile(LPVOID buf, DWORD len, DWORD & read, LONGLONG offset)
{
	JCASSERT(m_file);
	int ir = fl_fseek(m_file, (long)offset, SEEK_SET);
	ir = fl_fread(buf, len, 1, m_file);
	read = ir;
	return (ir!=0);
}

bool CFileInfoFatIo::DokanWriteFile(LPCVOID buf, DWORD len, DWORD & written, LONGLONG offset)
{
	JCASSERT(m_file);
	int ir = fl_fseek(m_file, (long)offset, SEEK_SET);
	void * bb = (void*)buf;
	ir = fl_fwrite(bb, len, 1, m_file);
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
	while (fl_readdir(dir, &dirent) == 0)
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
	int ir = fl_fflush(m_file);
	return (ir==0);
}

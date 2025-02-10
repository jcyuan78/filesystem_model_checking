#pragma once
#include <dokanfs-lib.h>
class CFileInfoFatIo : public IFileInfo
{
public:
//	CFileInfoFatIo(struct fatfs * fs) :/* m_dir(NULL),*/ m_file(NULL), m_fs(fs) {};
	CFileInfoFatIo(void) :/* m_dir(NULL),*/ m_file(NULL), m_fs(NULL) {};
	void Init(struct fatfs* fs) { m_fs = fs; };

public:
	virtual void Cleanup(void);
	virtual void CloseFile(void);
	virtual bool DokanReadFile(LPVOID buf, DWORD len, DWORD & read, LONGLONG offset);
	virtual bool DokanWriteFile(LPCVOID buf, DWORD len, DWORD & written, LONGLONG offset);

	virtual bool LockFile(LONGLONG offset, LONGLONG len) { return false; };
	virtual bool UnlockFile(LONGLONG offset, LONGLONG len) { return false; };
	virtual bool EnumerateFiles(EnumFileListener * listener) const;

	virtual bool GetFileInformation(LPBY_HANDLE_FILE_INFORMATION fileinfo) const;
	virtual std::wstring GetFileName(void) const { return m_fn; }

	// for dir only
	virtual bool IsDirectory(void) const { return false; }
	virtual bool SetAllocationSize(LONGLONG size) { return false; }
	virtual bool SetEndOfFile(LONGLONG);
	virtual void DokanSetFileAttributes(DWORD attr) {}
	//virtual void SetFileSecurity(void) {};
	virtual bool DokanGetFileSecurity(SECURITY_INFORMATION psinfo, PSECURITY_DESCRIPTOR psdesc, ULONG & buf_size) {	return false;}
	virtual bool DokanSetFileSecurity(PSECURITY_INFORMATION psinfo, PSECURITY_DESCRIPTOR psdesc, ULONG buf_size) { return false; }
	virtual void SetFileTime(const FILETIME * ct, const FILETIME * at, const FILETIME* mt) {}
	virtual bool FlushFile(void);

	virtual void GetParent(IFileInfo * & parent) {};
	virtual void ClearData(void) {};
	virtual bool OpenChild(IFileInfo * &file, const wchar_t * fn) const { return false; }
	virtual bool CreateChild(IFileInfo * &file, const wchar_t * fn, bool dir) { return false; }

public:
//	void SetFile(void * file) { JCASSERT(file); m_file = reinterpret_cast<FL_FILE*>(file); }
public:
	struct fatfs* m_fs;
	FL_DIR	m_dir;
	bool m_isdir;
	FL_FILE * m_file;
	std::wstring m_fn;
};

class CFSFatIo : public IFileSystem
{
public:
	CFSFatIo(void);
	~CFSFatIo(void);

public:
	virtual ULONG GetFileSystemOption(void) const { return 0; };
	virtual bool Mount(IVirtualDisk * dev);
	virtual void Unmount(void);
	// Volume Size：sector单位
	virtual bool MakeFileSystem(IVirtualDisk * dev, UINT32 volume_secs, const std::wstring & volume_name);
	virtual FsCheckResult FileSystemCheck(IVirtualDisk * dev, bool repair);

	virtual bool DokanGetDiskSpace(ULONGLONG &free_bytes, ULONGLONG &total_bytes, ULONGLONG &total_free_bytes);
	virtual bool GetVolumnInfo(std::wstring & vol_name,
		DWORD & sn, DWORD & max_fn_len, DWORD & fs_flag,
		std::wstring & fs_name);

	// file attribute (attr) and create disposition (disp) is in user mode 
	virtual bool DokanCreateFile(IFileInfo * &file, const std::wstring & fn,
		ACCESS_MASK access_mask, DWORD attr, FsCreateDisposition disp,
		ULONG share, ULONG opt, bool isdir);
	virtual bool DokanDeleteFile(const std::wstring & fn, IFileInfo * file, bool isdir);
	virtual void FindStreams(void) {};
	virtual bool DokanMoveFile(const std::wstring & src_fn, const std::wstring & dst_fn, bool replace, IFileInfo * file) { return true; }


	virtual bool MakeDir(const std::wstring& dir) { JCASSERT(0); return false; }

	virtual bool HardLink(const std::wstring &src, const std::wstring & dst) { JCASSERT(0); return false; }
	virtual bool Unlink(const std::wstring& fn) { JCASSERT(0); return false; }
	virtual bool Sync(void);

	// local
//public:
//	bool MediaRead(UINT sector, void * buffer, size_t sector_count);
//	bool MediaWrite(UINT sector, void * buffer, size_t sector_count);

protected:
	virtual bool ConnectToDevice(IVirtualDisk * dev);
	virtual void Disconnect(void);

protected:
	struct fatfs	m_fs;
	//struct disk_if  *m_disk_io = NULL;
	//FL_FILE         m_files[FATFS_MAX_OPEN_FILES];
	//int             m_filelib_init = 0;
	//int             m_filelib_valid = 0;
	//struct fat_list m_open_file_list;
	//struct fat_list m_free_file_list;


//	IVirtualDisk * m_dev;
};

class CFatIoFactory : public IFsFactory
{
public:
	virtual bool CreateFileSystem(IFileSystem * & fs, const std::wstring & fs_name);
	//virtual bool CreateVirtualDisk(IVirtualDisk * & dev, const std::wstring & fn, size_t secs);
	virtual bool CreateVirtualDisk(IVirtualDisk * & dev, const boost::property_tree::wptree & prop, bool create);

};


//extern jcvos::CStaticInstance<CFSFatIo>	g_fs;

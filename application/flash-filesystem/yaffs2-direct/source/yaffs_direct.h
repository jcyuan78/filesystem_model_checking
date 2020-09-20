#pragma once

#include <dokanfs-lib.h>
extern "C"
{
#include "../yaffs-guts/yaffsfs.h"
}

class CYaffsDirect : public CDokanFsBase
{
public:
	CYaffsDirect();
	virtual ~CYaffsDirect();

public:
	virtual void GetRoot(IFileInfo * & root);
	virtual void Lock(void) {};
	virtual void Unlock(void) {};

public:
	// file attribute (attr) and create disposition (disp) is in user mode 
	virtual bool DokanCreateFile(IFileInfo * &file, const std::wstring & fn,
		ACCESS_MASK access_mask, DWORD attr, DWORD disp, ULONG share, ULONG opt, bool isdir);
	virtual ULONG GetFileSystemOption(void) const { return 0; }
	virtual bool ConnectToDevice(IVirtualDisk * dev);
	virtual void Disconnect(void);
	virtual bool Mount(void);
	
	virtual void Unmount(void);

	virtual bool DokanGetDiskSpace(ULONGLONG &free_bytes, ULONGLONG &total_bytes, ULONGLONG &total_free_bytes);

	virtual bool GetVolumnInfo(std::wstring & vol_name,
		DWORD & sn, DWORD & max_fn_len, DWORD & fs_flag,
		std::wstring & fs_name) {
		return false;
	}

	virtual bool DokanDeleteFile(const std::wstring & fn, IFileInfo * file, bool isdir);
	//virtual void FindFiles(void);
	virtual void FindStreams(void) {}
	virtual bool DokanMoveFile(const std::wstring & src_fn, const std::wstring & dst_fn, bool replace, IFileInfo * file) { return false; };
	virtual bool MakeFileSystem(UINT32 volume_size, const std::wstring & volume_name);

	virtual bool FileSystemCheck(bool repair) { return false; }

protected:
	yaffs_obj * FindDir(yaffs_obj * start_dir, const YCHAR * path, const YCHAR *& name/*, bool & isdir*/);

protected:
	INandDriver * m_driver;
	yaffs_dev* m_yaffs_dev;
	yaffs_obj* m_root;
};


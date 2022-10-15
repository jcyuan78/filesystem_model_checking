#pragma once
#include "ifilesystem.h"
#include <boost/property_tree/json_parser.hpp>

class CDokanFsBase : public IFileSystem
{
public:
	CDokanFsBase();
	virtual ~CDokanFsBase();

// override for dokan base
public:
	virtual void GetRoot(IFileInfo * & root) = 0;
protected:
	bool OpenFileDir(IFileInfo * & f, wchar_t * path, size_t path_len);


// implement for IFileSystem
public:
	virtual ULONG GetFileSystemOption(void) const = 0;
	virtual bool ConnectToDevice(IVirtualDisk * dev) = 0;
	virtual void Disconnect(void) = 0;
	virtual bool Mount(void) = 0;
	virtual void Unmount(void) = 0;

	virtual bool DokanGetDiskSpace(ULONGLONG &free_bytes, ULONGLONG &total_bytes, ULONGLONG &total_free_bytes) = 0;
	virtual bool GetVolumnInfo(std::wstring & vol_name,
		DWORD & sn, DWORD & max_fn_len, DWORD & fs_flag,
		std::wstring & fs_name) = 0;

	// file attribute (attr) and create disposition (disp) is in user mode 
	virtual NTSTATUS DokanCreateFile(IFileInfo * &file, const std::wstring & fn, ACCESS_MASK access_mask, DWORD attr, DWORD disp, ULONG share, ULONG opt, bool isdir);
	//virtual bool OpenFile(IFileInfo * & file, UINT32 f_inode) = 0;

	virtual NTSTATUS DokanDeleteFile(const std::wstring & fn, IFileInfo * file, bool isdir) = 0;
	//virtual void FindFiles(void) = 0;
	virtual void FindStreams(void) = 0;
	virtual NTSTATUS DokanMoveFile(const std::wstring & src_fn, const std::wstring & dst_fn, bool replace, IFileInfo * file) = 0;

	virtual bool MakeFileSystem(/*IVirtualDevice * dev,*/ UINT32 volume_size, const std::wstring & volume_name) = 0;

// help functions
public:
	static int LoadDisk(IVirtualDisk*& disk, const std::wstring& working_dir, const boost::property_tree::wptree& config);
	static int LoadFilesystem(IFileSystem*& fs, IVirtualDisk* disk, const std::wstring& working_dir, const boost::property_tree::wptree& config);
	static int LoadFilesystemByConfig(IFileSystem*& fs, IVirtualDisk*& disk, const std::wstring& working_dir, const boost::property_tree::wptree & config);
};


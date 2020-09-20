#pragma once

//#include <stdext.h>
#include <ntstatus.h>
#include <boost/property_tree/ptree.hpp>
//#include   <boost/property_tree/json_parser.hpp>

enum FS_OPTION
{
	FS_OPTION_WRITE_PROTECT =8,		// DOKAN_OPTION_WRITE_PROTECT
};

class EnumFileListener
{
public:
	virtual bool EnumFileCallback(const std::wstring & fn, 
		UINT32 ino, UINT32 entry, // entry 在父目录中的位置
		BY_HANDLE_FILE_INFORMATION *) = 0;
};



class IVirtualDisk : public IJCInterface
{
public:
	struct HEALTH_INFO
	{
		UINT32 empty_block;
		UINT32 pure_spare;
		UINT32 host_write;			// sector
		UINT32 host_read;
		UINT32 media_write;			// sector
		UINT32 media_read;
	};
public:
	virtual size_t GetCapacity(void) = 0;		// in sector
	virtual bool ReadSectors(void * buf, UINT lba, size_t secs) = 0;
	virtual bool WriteSectors(void * buf, UINT lba, size_t secs) = 0;
	virtual bool Trim(UINT lba, size_t secs) = 0;
	virtual bool FlushData(UINT lba, size_t secs) = 0;
	virtual void SetSectorSize(UINT size) = 0;
	virtual void CopyFrom(IVirtualDisk * dev) = 0;
	// 将当前的image保存到文件中.
	virtual bool SaveSnapshot(const std::wstring &fn) = 0;

	virtual bool GetHealthInfo(HEALTH_INFO & info) = 0;
};

// 从IVirtualDisk继承，便于Factory的实现
class INandDriver : public IVirtualDisk
{
public:
	// NAND FLASH的地址表示，可以是Block:Page，或者是Page_in_nand的形式。由具体实现负责解释，
	typedef UINT32 FAddress;
	enum ECC_RESULT 
	{
		ECC_RESULT_UNKNOWN = 0,
		ECC_RESULT_NO_ERROR=1,
		ECC_RESULT_FIXED=2,
		ECC_RESULT_UNFIXED=3
	};
	struct NAND_DEVICE_INFO
	{
		size_t data_size;
		size_t spare_size;
		size_t page_size;
		size_t page_num;
		size_t block_num;
	};

public:
	// 获取flash的相关配置，包括page大小，每个block的page数量，block数量等
	virtual void GetFlashId(NAND_DEVICE_INFO & info) = 0;
	virtual bool WriteChunk(FAddress page, const BYTE *data, size_t data_len) = 0;
	virtual bool ReadChunk(FAddress nand_chunk, BYTE *data, size_t data_len, ECC_RESULT &ecc_result) = 0;

	// 用于兼容Yaffs
	virtual bool WriteChunk(int nand_chunk, const BYTE *data, size_t data_len, 
		const BYTE *oob, size_t oob_len) = 0;
	virtual bool ReadChunk(int nand_chunk, BYTE *data, size_t data_len, 
		BYTE *oob, size_t oob_len, ECC_RESULT &ecc_result) = 0;

	// (*drv_mark_bad_fn)
	virtual bool MarkBad(int block_no) = 0;
	virtual bool Erase(int block_no) = 0;
	virtual bool CheckBad(int block_no) = 0;
	// (*drv_check_bad_fn)

	virtual size_t GetLogNumber(void) const = 0;
	virtual bool BackLog(size_t num) = 0;
	virtual void ResetLog(void) = 0;

	// (*drv_initialise_fn)
	//virtual bool Initialize() = 0;
	//// (*drv_deinitialise_fn)
	//virtual void DeInitialize() = 0;
	//virtual void QueryInitBlockState(int blk, yaffs_block_state &state, UINT32 &sn) = 0;
};

class IFileInfo : public IJCInterface
{

public:
	virtual void Cleanup(void) = 0;
	virtual void CloseFile(void) = 0;
	virtual bool DokanReadFile(LPVOID buf, DWORD len, DWORD & read, LONGLONG offset) = 0;
	virtual bool DokanWriteFile(const void * buf, DWORD len, DWORD & written, LONGLONG offset) = 0;

	virtual bool LockFile(LONGLONG offset, LONGLONG len) = 0;
	virtual bool UnlockFile(LONGLONG offset, LONGLONG len) = 0;
	virtual bool EnumerateFiles(EnumFileListener * listener) const =0;

	virtual bool GetFileInformation(LPBY_HANDLE_FILE_INFORMATION fileinfo) const = 0;
	virtual std::wstring GetFileName(void) const = 0;

	virtual bool DokanGetFileSecurity(SECURITY_INFORMATION psinfo, PSECURITY_DESCRIPTOR psdesc, ULONG & buf_size) = 0;
	virtual bool DokanSetFileSecurity(PSECURITY_INFORMATION psinfo, PSECURITY_DESCRIPTOR psdesc, ULONG buf_size) = 0;
	// for dir only
	virtual bool IsDirectory(void) const = 0;
	virtual bool SetAllocationSize(LONGLONG size) =0;
	virtual bool SetEndOfFile(LONGLONG) =0;
	virtual void DokanSetFileAttributes(DWORD attr) =0;

	virtual void SetFileTime(const FILETIME * ct, const FILETIME * at, const FILETIME* mt) = 0;
	virtual bool FlushFile(void) = 0;

	virtual void GetParent(IFileInfo * & parent) = 0;

	// 删除所有给文件分配的空间。如果是目录，删除目录下的所有文件。
	virtual void ClearData(void) = 0;

	virtual bool OpenChild(IFileInfo * &file, const wchar_t * fn) const = 0;
	virtual bool CreateChild(IFileInfo * &file, const wchar_t * fn, bool dir) = 0;

//// for tester
//public:
//	virtual bool ConnectToDevice(IVirtualDevice * dev) = 0;
//	virtual void Disconnect(void) = 0;
};

class IFileSystem : public IJCInterface
{
public:
	virtual ULONG GetFileSystemOption(void) const = 0;
	virtual bool ConnectToDevice(IVirtualDisk * dev) = 0;
	virtual void Disconnect(void) = 0;
	virtual bool Mount(void) = 0;
	virtual void Unmount(void) = 0;

	virtual bool DokanGetDiskSpace(ULONGLONG &free_bytes, ULONGLONG &total_bytes, ULONGLONG &total_free_bytes)=0;
	virtual bool GetVolumnInfo(std::wstring & vol_name, 
		DWORD & sn, DWORD & max_fn_len, DWORD & fs_flag,
		std::wstring & fs_name) = 0;

	// file attribute (attr) and create disposition (disp) is in user mode 
	virtual bool DokanCreateFile(IFileInfo * &file, const std::wstring & fn, 
		ACCESS_MASK access_mask, DWORD attr, DWORD disp, 
		ULONG share, ULONG opt, bool isdir) = 0;
	//virtual bool OpenFile(IFileInfo * & file, UINT32 f_inode) = 0;

	virtual bool DokanDeleteFile(const std::wstring & fn, IFileInfo * file, bool isdir) =0;
	//virtual void FindFiles(void) = 0;
	virtual void FindStreams(void) = 0;
	virtual bool DokanMoveFile(const std::wstring & src_fn, const std::wstring & dst_fn, bool replace, IFileInfo * file)=0;

	virtual bool MakeFileSystem(UINT32 volume_size, const std::wstring & volume_name) = 0;
	virtual bool FileSystemCheck(bool repair) = 0;
};

class IFsFactory : public IJCInterface
{
public:
	virtual bool CreateFileSystem(IFileSystem * & fs, const std::wstring & fs_name) = 0;
	virtual bool CreateVirtualDisk(IVirtualDisk * & dev, const std::wstring & fn, size_t secs) = 0;
	virtual bool CreateVirtualDisk(IVirtualDisk * & dev, const boost::property_tree::wptree & prop, bool create) = 0;
};


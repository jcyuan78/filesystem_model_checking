///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
#pragma once

#include <stdext.h>
#include <boost/property_tree/ptree.hpp>
#include <vector>
#include "reference_fs.h"
#include "config.h"

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// == pages  ==
#define INVALID_BLK		(0xFFFFFFFF)
#define NID_IN_USE		(0xFFFFFFF0)
#define INVALID_FID		(0xFFFF)
#define BLOCK_SIZE		(512)			// 块的大小


enum ERROR_CODE
{
	ERR_OK = 0,
	ERR_NO_OPERATION,	// 测试过程中，正常情况下，当前操作无法被执行。
	ERR_GENERAL,
	OK_ALREADY_EXIST,	// 文件或者目录已经存在，但是结果返回true
	ERR_CREATE_EXIST,	// 对于已经存在的文件，重复创建。
	ERR_CREATE,			// 文件或者目录不存在，但是创建失败
	ERR_OPEN_FILE,		// 试图打开一个已经存在的文件是出错
	ERR_GET_INFOMATION,	// 获取File Informaiton时出错
	ERR_DELETE_FILE,	// 删除文件时出错
	ERR_DELETE_DIR,		// 删除目录时出错
	ERR_NON_EMPTY,		// 删除文件夹时，文件夹非空
	ERR_READ_FILE,		// 读文件时出错
	ERR_WRONG_FILE_SIZE,	// 
	ERR_WRONG_FILE_DATA,
//	ERR_UNKNOWN,
	ERR_PENDING,		// 测试还在进行中

	ERR_DENTRY_FULL,	// 文件夹的dentry已经满了
	ERR_MAX_OPEN_FILE,	// 打开的文件超过数量
	ERR_PARENT_NOT_EXIST,	//打开文件或者创建文件时，父目录不存在
	ERR_WRONG_PATH,	// 文件名格式不对，要求从\\开始

	ERR_VERIFY_FILE_LEN,		// 文件比较时，长度不对

	ERR_WRONG_BLOCK_TYPE,		// block的类型不符
	ERR_WRONG_FILE_TYPE, 
	ERR_SIT_MISMATCH,
	ERR_PHY_ADDR_MISMATCH,
	ERR_INVALID_INDEX,

	ERR_DEAD_NID,
	ERR_LOST_NID,
	ERR_DOUBLED_NID,
	ERR_INVALID_NID,		// 不合法的nid, 或者不存在的nid/fid

	ERR_DEAD_BLK,
	ERR_LOST_BLK,
	ERR_DOUBLED_BLK,
	ERR_INVALID_BLK,		// 不合法的physical block id

	ERR_UNKNOWN,
};
//typedef UINT FSIZE;

//typedef DWORD FID;
typedef DWORD NID;		// node id
typedef DWORD PHY_BLK;
typedef DWORD LBLK_T;
typedef UINT	PAGE_INDEX;

enum BLK_TEMP
{
	BT_COLD_DATA = 0, BT_COLD_NODE = 1,
	BT_WARM_DATA = 2, BT_WARM_NODE = 3,
	BT_HOT__DATA = 4, BT_HOT__NODE = 5,
	BT_TEMP_NR
};

struct FILE_DATA
{
	NID		fid;
	UINT	offset;
	UINT	ver;
};

/// <summary>
/// 描述文件系统的运行状态。通过特殊（$health）文件读取
/// </summary>
struct FsHealthInfo
{
	UINT m_seg_nr;	// 总的segment数量
	UINT m_blk_nr;	// 总的block数量
	UINT m_logical_blk_nr;			// 逻辑块总是。makefs时申请的逻辑块数量
	UINT m_free_blk;		// 空闲逻辑块数量。不是一个精确数值，初始值为允许的逻辑饱和度。当过量写的时候，可能导致负数。

	LONG64 m_total_host_write;	// 以块为单位，host的写入总量。（快的大小由根据文件系统调整，一般为4KB）
	LONG64 m_total_media_write;	// 写入介质的数据总量，以block为单位
	//LONG64 m_media_write_node;
	//LONG64 m_media_write_data;

	UINT m_logical_saturation;	// 逻辑饱和度。被写过的逻辑块数量，不包括metadata
	UINT m_physical_saturation;	// 物理饱和度。有效的物理块数量，

	UINT m_node_nr;		// nid, direct node的总数
	UINT m_used_node;	// 被使用的node总数
	UINT m_file_num, m_dir_num;		// 文件数量和目录数量
};


struct FS_INFO
{
	FSIZE total_seg, free_seg;
	FSIZE total_blks;
	FSIZE used_blks;	// 逻辑饱和度（有效block数量）
	FSIZE free_blks;
	FSIZE physical_blks;	// 物理饱和度

	UINT max_file_nr;	// 最大支持的文件数量
	UINT dir_nr, file_nr;		// 目录数量和文件数量
	LONG64 total_host_write;
	LONG64 total_media_write;

	UINT max_opened_file;	// 最大打开的文件数量

	UINT total_page_nr;
	UINT free_page_nr;
	UINT total_data_nr;
	UINT free_data_nr;
};

struct GC_TRACE
{
	NID fid;
	UINT offset;
	PHY_BLK org_phy;
	PHY_BLK new_phy;
};

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// == interface  ==


class IFsSimulator //: public IJCInterface
{
public:
	virtual ~IFsSimulator(void) {}
	// 文件系统初始化
	virtual bool Initialzie(const boost::property_tree::wptree& config, const std::wstring & log_path) = 0;
		// 获取文件系统的配置
	virtual void GetConfig(boost::property_tree::wptree& config, const std::wstring& config_name) = 0;
	virtual bool Mount(void) = 0;
	virtual bool Unmount(void) = 0;
	virtual bool Reset(UINT rollback) = 0;
	virtual ERROR_CODE fsck(bool fix) =0;


	// 文件系统基本操作
	virtual ERROR_CODE  FileCreate(NID & fid, const std::string& fn) = 0;
	virtual ERROR_CODE  DirCreate(NID & fid, const std::string& fn) = 0;
	virtual ERROR_CODE  FileOpen(NID & fid, const std::string& fn, bool delete_on_close = false) = 0;
	virtual void FileClose(NID fid) = 0;
	// 设置和获取文件大小，以sector为单位。
	virtual void SetFileSize(NID fid, FSIZE secs) = 0;
	virtual FSIZE GetFileSize(NID fid) = 0;

	virtual void FileWrite(NID fid, FSIZE offset, FSIZE secs) = 0;
	// 返回读取到的 page数量
	virtual size_t FileRead(FILE_DATA blks[], NID fid, FSIZE offset, FSIZE secs) = 0;
	virtual void FileTruncate(NID fid, FSIZE offset, FSIZE secs) = 0;
	// delete 根据文件名删除文件。FileRemove()根据ID，删除文件相关内容，但是不删除path map
	virtual void FileDelete(const std::string & fn) = 0;	
	virtual ERROR_CODE DirDelete(const std::string& fn) = 0;
	virtual void FileFlush(NID fid) = 0;
	// 文件能够支持的最大长度（block单位）
	virtual DWORD MaxFileSize(void) const = 0;
	virtual void GetFsInfo(FS_INFO& space_info) = 0;
	
	// 测试支持
	// virtual bool CopyFrom(IFsSimulator* src) = 0;
	virtual void Clone(IFsSimulator*& dst) = 0;
	virtual void CopyFrom(const IFsSimulator* src) = 0;
	virtual void GetHealthInfo(FsHealthInfo& info) const = 0;
	// 用于调试，不需要打开文件。size：文件大小。node block：包括inode在内，index block数量；data_blk：实际占用block数量
	virtual void GetFileInfo(NID fid, FSIZE& size, FSIZE& node_blk, FSIZE& data_blk) =0;

	// 对storage，用于storage相关测试
	virtual UINT GetCacheNum(void) = 0;

	// for debug

	virtual void DumpSegments(const std::wstring& fn, bool sanity_check) = 0;
	virtual void DumpSegmentBlocks(const std::wstring& fn) = 0;
	virtual void DumpFileMap(FILE* out, NID fid) = 0;
	virtual void DumpAllFileMap(const std::wstring& fn) = 0;
	virtual void DumpBlockWAF(const std::wstring& fn) = 0;
	virtual size_t DumpFileIndex(NID index[], size_t buf_size, NID fid) = 0;

	virtual void GetGcTrace(std::vector<GC_TRACE>&) = 0;
};

class CFsException : public jcvos::CJCException
{
public:
	CFsException(ERROR_CODE code, const wchar_t * func, int line, const wchar_t* msg, ...);
	static const wchar_t* ErrCodeToString(ERROR_CODE code);
};



#define THROW_FS_ERROR(code, ...)   {		\
		FS_BREAK	\
		CFsException err(code, __STR2WSTR__(__FUNCTION__), __LINE__, __VA_ARGS__); \
        /*LogException(__STR2WSTR__(__FUNCTION__), __LINE__, err);*/	\
        throw err; }
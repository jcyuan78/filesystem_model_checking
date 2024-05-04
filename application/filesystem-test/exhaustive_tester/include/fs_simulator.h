///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
#pragma once

#include <stdext.h>
#include <boost/property_tree/ptree.hpp>
#include <vector>
#include "reference_fs.h"

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// == pages  ==
#define INVALID_BLK		0xFFFFFFFF
#define BLOCK_SIZE				(512)			// 块的大小

//typedef UINT FSIZE;

typedef DWORD FID;
typedef DWORD NID;		// node id
typedef DWORD PHY_BLK;
typedef DWORD LBLK_T;

enum BLK_TEMP
{
	BT_COLD_DATA = 0, BT_COLD_NODE = 1,
	BT_WARM_DATA = 2, BT_WARM_NODE = 3,
	BT_HOT__DATA = 4, BT_HOT__NODE = 5,
	BT_TEMP_NR
};


class CPageInfo
{
public:
	void Init();

public:
	PHY_BLK phy_blk = INVALID_BLK;	// page所在物理位置
	// 标记page的温度，当page被写入SSD时更新。这个温度不是实际分配到温度，所有算法下都相同。仅用于统计。
	BLK_TEMP ttemp;
	//在文件中的位置
	NID	inode;
	LBLK_T offset = INVALID_BLK;
	// 数据(对于inode 或者 direct node)
	UINT data_index;	// 指向数据缓存的索引号
	bool dirty = false;
	enum PAGE_TYPE { PAGE_DATA, PAGE_NODE } type;
public:
	// 用于性能统计
	UINT host_write = 0;
	UINT media_write = 0;
};


/// <summary>
/// 描述文件系统的运行状态。通过特殊（$health）文件读取
/// </summary>
struct FsHealthInfo
{
	UINT m_seg_nr;	// 总的segment数量
	UINT m_blk_nr;	// 总的block数量
	UINT m_logical_blk_nr;			// 逻辑块总是。makefs时申请的逻辑块数量
//	UINT m_free_seg;	// free segment：空闲的物理segmeng，用于GC的判断
//	int m_free_blk;		// 空闲逻辑块数量。不是一个精确数值，初始值为允许的逻辑饱和度。当过量写的时候，可能导致负数。

	LONG64 m_total_host_write;	// 以块为单位，host的写入总量。（快的大小由根据文件系统调整，一般为4KB）
	LONG64 m_total_media_write;	// 写入介质的数据总量，以block为单位
	LONG64 m_media_write_node;
	LONG64 m_media_write_data;

	UINT m_logical_saturation;	// 逻辑饱和度。被写过的逻辑块数量，不包括metadata
	UINT m_physical_saturation;	// 物理饱和度。有效的物理块数量，

	UINT m_node_nr;		// inode, direct node的总数
	UINT m_used_node;	// 被使用的node总数
};


struct FsSpaceInfo
{
	FSIZE total_blks;
	FSIZE used_blks;
	FSIZE free_blks;

	FSIZE max_file_nr;
	FSIZE created_files;
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
	virtual bool Reset(void) = 0;

	// 文件系统基本操作
	virtual FID  FileCreate(const std::wstring& fn) = 0;
	virtual FID  DirCreate(const std::wstring& fn) = 0;
	virtual void FileOpen(FID fid, bool delete_on_close = false) = 0;
	virtual FID  FileOpen(const std::wstring& fn, bool delete_on_close = false) = 0;
	virtual void FileClose(FID fid) = 0;
	// 设置和获取文件大小，以sector为单位。
	virtual void SetFileSize(FID fid, FSIZE secs) = 0;
	virtual FSIZE GetFileSize(FID fid) = 0;

	virtual void FileWrite(FID fid, FSIZE offset, FSIZE secs) = 0;
	virtual void FileRead(std::vector<CPageInfo*>& blks, FID fid, FSIZE offset, FSIZE secs) = 0;
	virtual void FileTruncate(FID fid, FSIZE offset, FSIZE secs) = 0;
//	virtual void FileDelete(FID fid) = 0;
	// delete 根据文件名删除文件。FileRemove()根据ID，删除文件相关内容，但是不删除path map
	virtual void FileDelete(const std::wstring & fn) = 0;		
	virtual void FileFlush(FID fid) = 0;
	// 文件能够支持的最大长度（block单位）
	virtual DWORD MaxFileSize(void) const = 0;
	virtual void GetSpaceInfo(FsSpaceInfo& space_info) = 0;
	
	// 测试支持
	// virtual bool CopyFrom(IFsSimulator* src) = 0;
	virtual void Clone(IFsSimulator*& dst) = 0;
	virtual void GetHealthInfo(FsHealthInfo& info) const = 0;

	virtual void DumpSegments(const std::wstring& fn, bool sanity_check) = 0;
	virtual void DumpSegmentBlocks(const std::wstring& fn) = 0;
	virtual void DumpFileMap(FILE* out, FID fid) = 0;
	virtual void DumpAllFileMap(const std::wstring& fn) = 0;
//	virtual void SetLogFolder(const std::wstring& fn) = 0;
	virtual void DumpBlockWAF(const std::wstring& fn) = 0;


};
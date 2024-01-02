///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
#pragma once

#include <boost/property_tree/ptree.hpp>
#include <vector>


#define SECTOR_PER_BLOCK		(8)
#define SECTOR_PER_BLOCK_BIT	(3)

#define BLOCK_PER_SEG	512
#define BITMAP_SIZE		16			// 512 blocks / 32 bit
#define GC_THREAD_START		3
#define GC_THREAD_END		5

#define INVALID_BLK		0xFFFFFFFF

#define MAX_TABLE_SIZE			1024
#define START_OF_DIRECTORY_NODE	64
#define FID_ROOT	0
#define MAX_INDEX_LEVEL		3
#define LEVEL1_OFFSET			64
#define LEVEL2_OFFSET			96
#define INDEX_SIZE				1024
#define INDEX_SIZE_BIT			10


// == configurations
#define _SANITY_CHECK
#define HEAP_ALGORITHM

typedef DWORD SEG_T;
typedef DWORD BLK_T;
typedef DWORD PHY_BLK;
typedef DWORD LBLK_T;
typedef DWORD FID;

enum BLK_TEMP
{
	BT_COLD_DATA = 0, BT_COLD_NODE = 1,
	BT_WARM_DATA = 2, BT_WARM_NODE = 3,
	BT_HOT__DATA = 4, BT_HOT__NODE = 5,
	BT_TEMP_NR
};


/// <summary>
/// 描述文件系统的运行状态。通过特殊（$health）文件读取
/// </summary>
struct FsHealthInfo
{
	UINT m_seg_nr;	// 总的segment数量
	UINT m_blk_nr;	// 做的block数量
	UINT m_logical_blk_nr;			// 逻辑块总是。makefs时申请的逻辑块数量
	UINT m_free_seg, m_free_blk;	// 空闲segment和block数量

	LONG64 m_total_host_write;	// 以块为单位，host的写入总量。（快的大小由根据文件系统调整，一般为4KB）
	LONG64 m_total_media_write;	// 写入介质的数据总量，以block为单位
	LONG64 m_media_write_node;
	LONG64 m_media_write_data;

	UINT m_logical_saturation;	// 逻辑饱和度。被写过的逻辑块数量，不包括metadata
	UINT m_physical_saturation;	// 物理饱和度。有效的物理块数量，

	UINT m_node_nr;		// inode, direct node的总数
	UINT m_used_node;	// 被使用的node总数
};

class CPageInfoBase
{
public:
	UINT host_write = 0;
	UINT media_write = 0;
};

/// ///////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// == File system


class CLfsInterface
{
public:
	virtual ~CLfsInterface(void) {}
public:
	virtual bool Initialzie(const boost::property_tree::wptree& config) = 0;
	virtual FID  FileCreate(const std::wstring& fn) = 0;
	virtual void SetFileSize(FID fid, size_t secs) = 0;
	virtual void FileWrite(FID fid, size_t offset, size_t secs) = 0;
	virtual void FileRead(std::vector<CPageInfoBase*>& blks, FID fid, size_t offset, size_t secs) = 0;
	virtual void FileTruncate(FID fid) = 0;
	virtual void FileDelete(FID fid) = 0;
	virtual void FileFlush(FID fid) = 0;

	// 文件能够支持的最大长度（block单位）
	virtual DWORD MaxFileSize(void) const = 0;
	virtual void FileOpen(FID fid, bool delete_on_close = false) = 0;
	virtual void FileClose(FID fid) = 0;
	virtual void GetHealthInfo(FsHealthInfo& info) const = 0;
	virtual void DumpSegments(const std::wstring& fn, bool sanity_check) = 0;
	virtual void DumpSegmentBlocks(const std::wstring& fn) = 0;
	virtual void DumpFileMap(FILE* out, FID fid) = 0;
	virtual void DumpAllFileMap(const std::wstring& fn) = 0;
	virtual void SetLogFolder(const std::wstring& fn) = 0;
	virtual void DumpBlockWAF(const std::wstring& fn) = 0;

	// 获取文件系统的配置
	virtual void GetConfig(boost::property_tree::wptree& config, const std::wstring& config_name) = 0;
};

class inode_info;




class CLfsBase : public CLfsInterface
{
public:
	CLfsBase(void)
	{	// 初始化m_level_to_offset
		m_level_to_offset[0] = LEVEL1_OFFSET;
		m_level_to_offset[1] = m_level_to_offset[0] + (LEVEL2_OFFSET - LEVEL1_OFFSET) * INDEX_SIZE;
		m_level_to_offset[2] = m_level_to_offset[1] + (MAX_TABLE_SIZE - LEVEL2_OFFSET) * INDEX_SIZE * INDEX_SIZE;

		memset(&m_health_info, 0, sizeof(FsHealthInfo));
	}

	virtual ~CLfsBase(void)
	{
		if (m_log_invalid_trace) fclose(m_log_invalid_trace);
		if (m_log_write_trace) fclose(m_log_write_trace);
		if (m_log_fs_trace) fclose(m_log_fs_trace);
		if (m_gc_trace) fclose(m_gc_trace);
	}

public:
	virtual DWORD MaxFileSize(void) const;
	virtual void GetHealthInfo(FsHealthInfo& info) const
	{
		memcpy_s(&info, sizeof(FsHealthInfo), &m_health_info, sizeof(FsHealthInfo));
	}

protected:

	virtual void SetLogFolder(const std::wstring& fn);

protected:
	FsHealthInfo m_health_info;

	std::wstring m_log_fn;
	FILE* m_log_invalid_trace = nullptr;
	
	FILE* m_log_write_trace = nullptr;		// 文件：trace_fs.csv
	// 用于记录文件系统层次的写入
	FILE* m_log_fs_trace = nullptr;			// 文件: fs_trace.csv;
	FILE* m_gc_trace = nullptr;

	LBLK_T m_level_to_offset[MAX_INDEX_LEVEL];

};
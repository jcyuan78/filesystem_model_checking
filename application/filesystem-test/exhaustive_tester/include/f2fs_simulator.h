///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "fs_simulator.h"

#include "f2fs_segment.h"

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// == 参数配置  ==
#define MAX_INDEX_LEVEL			(3)
#define INDEX_SIZE				(128)
#define MAX_TABLE_SIZE			(128)

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// == inode and index node  ==
// 用于记录一个实际的inode
struct INODE
{
//	wchar_t fn[MAX_FILENAME_LEN];	// 文件名
	UINT blk_num;		// 文件占用的块数
	UINT file_size;		// 文件长度，字节单位
	UINT ref_count;		
	bool delete_on_close;
	CPageAllocator::INDEX index_table[MAX_TABLE_SIZE];
};

struct INDEX_NODE
{
	CPageAllocator::INDEX index_table[MAX_TABLE_SIZE];
};

#define NODE_NEXT_FREE m_nid
struct NODE_INFO
{
public:
	enum NODE_TYPE { NODE_FREE, NODE_INODE, NODE_INDEX, } m_type;
	NID m_nid;
	NID m_parent_node;
	UINT valid_data;
	CPageAllocator::INDEX page_id;		// 这个node对应的page的id
	union {
		INODE inode;
		INDEX_NODE index;
	};
};

// 管理Buffer，模拟文件系统的页缓存。
template <typename BLOCK_TYPE>
class CBufferManager
{
public:
	typedef UINT _INDEX;
public:
	CBufferManager(void) {}
	void Init(void) {
		// 构建free list
		for (UINT ii = 1; ii < MAX_NODE_NUM; ++ii)
		{
			//m_free_blk[ii] = ii;
			m_data_buffer[ii].NODE_NEXT_FREE = ii + 1;
		}
		m_data_buffer[MAX_NODE_NUM - 1].NODE_NEXT_FREE = INVALID_BLK;
		m_data_buffer[0].NODE_NEXT_FREE = 0;	// 保留第0号node
		m_free_ptr = 1;
		m_used_nr = 1;
//		m_free_head = 1;
	}
	_INDEX get_block(void)
	{
		if (m_used_nr >= MAX_NODE_NUM) THROW_ERROR(ERR_APP, L"no free block buffer");
		_INDEX index = m_free_ptr;
		m_free_ptr = m_data_buffer[index].NODE_NEXT_FREE;
		m_used_nr++;
//		UINT index = m_free_blk[m_free_head++];
		return index;
	}
	void put_block(_INDEX index)
	{
//		if (m_free_head <= 1) THROW_ERROR(ERR_APP, L"free block does not match");
//		m_free_head--;
//		m_free_blk[m_free_head] = index;
		m_data_buffer[index].NODE_NEXT_FREE = (NID)m_free_ptr;
		m_free_ptr = index;
		m_used_nr--;
	}
public:
	BLOCK_TYPE m_data_buffer[MAX_NODE_NUM];
protected:
	//UINT m_free_blk[MAX_NODE_NUM];
	_INDEX m_free_ptr;
	UINT m_used_nr;
};


// 用于描述 inode中index table的映射关系
class CIndexPath
{
public:
	CIndexPath(NODE_INFO* inode)
	{
		level = -1;
		memset(offset, 0, sizeof(offset));
		memset(node, 0, sizeof(node));
		memset(page, 0, sizeof(page));
		node[0] = inode;
	}
public:
	int level = -1;
	int offset[MAX_INDEX_LEVEL];
	CPageInfo* page[MAX_INDEX_LEVEL];
	NODE_INFO* node[MAX_INDEX_LEVEL];
};


///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// == file system  ==


class CF2fsSimulator : public IFsSimulator
{
public:
	CF2fsSimulator(void) {};
	virtual ~CF2fsSimulator(void);

public:
	virtual bool Initialzie(const boost::property_tree::wptree& config, const std::wstring & log_path);
	virtual FID  FileCreate(const std::wstring& fn);
	virtual FID  DirCreate(const std::wstring& fn);

	virtual void SetFileSize(FID fid, FSIZE secs) {}
	virtual void FileWrite(FID fid, FSIZE offset, FSIZE secs);
	virtual void FileRead(std::vector<CPageInfo*>& blks, FID fid, FSIZE offset, FSIZE secs);
	// 可以truncate部分文件
	virtual void FileTruncate(FID fid, FSIZE offset, FSIZE secs);
	virtual void FileDelete(const std::wstring & fn);
	virtual void FileFlush(FID fid);
	virtual void FileClose(FID fid);
	virtual void FileOpen(FID fid, bool delete_on_close = false);
	virtual FSIZE GetFileSize(FID fid);
	virtual FID  FileOpen(const std::wstring& fn, bool delete_on_close = false);

	virtual bool Mount(void) {	return false;	}
	virtual bool Unmount(void) { return false; }
	virtual bool Reset(void) { return false; }

	virtual void Clone(IFsSimulator*& dst);

	virtual void DumpSegments(const std::wstring& fn, bool sanity_check);
	virtual void DumpSegmentBlocks(const std::wstring& fn);
	virtual void DumpFileMap(FILE* out, FID fid) { DumpFileMap_merge(out, fid); }

	virtual void DumpAllFileMap(const std::wstring& fn);
	virtual void DumpBlockWAF(const std::wstring& fn);

	virtual void GetConfig(boost::property_tree::wptree& config, const std::wstring& config_name);
	virtual DWORD MaxFileSize(void) const { return 10000; }

//	virtual void SetLogFolder(const std::wstring& fn);
	virtual void GetSpaceInfo(FsSpaceInfo& space_info);
	virtual void GetHealthInfo(FsHealthInfo& info) const {};

#ifdef ENABLE_FS_TRACE
	void fs_trace(const char* op, FID fid, DWORD start_blk, DWORD blk_nr);
#else
	void fs_trace(const char* op, FID fid, DWORD start_blk, DWORD blk_nr) {}
#endif

protected:
	friend class CF2fsSegmentManager;

	void FileRemove(FID fid);
	// 计算block的实际温度，不考虑目前使用的算法。
	BLK_TEMP GetBlockTemp(CPageInfo* page);
	// 根据当前的算法计算block的温度。
	BLK_TEMP GetAlgorithmBlockTemp(CPageInfo* page, BLK_TEMP temp);
	// 在输出file map时，不合并连续的物理block
	void DumpFileMap_no_merge(FILE* out, FID fid);
	bool InvalidBlock(const char* reason, PHY_BLK phy_blk);
	void OffsetToIndex(CIndexPath& path, LBLK_T offset, bool alloc);
	// 将index_path移动到下一个block位置。用于加速offset到index的转化。如果index block发生变化，则清除path, 需要重新计算。
	void NextOffset(CIndexPath& path);
	// 检查inode中的所有index block和inode block，如果没有在磁盘上，测保存
	void UpdateInode(NODE_INFO& inode, const char* caller = "");

	// 在输出file map时，并连续的物理block
	void DumpFileMap_merge(FILE* out, FID fid);

	void InitInode(NODE_INFO* node, NID nid, CPageAllocator::INDEX page_id, CPageInfo * page)
	{
		memset(node, 0xFF, sizeof(NODE_INFO));
		node->m_nid = nid;
		node->m_type = NODE_INFO::NODE_INODE;
		node->valid_data = 0;
		node->page_id = page_id;
//		node->m_parent_node = INVALID_BLK;
		node->inode.blk_num = 0;
		node->inode.file_size = 0;
		node->inode.ref_count = 0;
		node->inode.delete_on_close = false;

		page->data_index = nid;
		page->inode = nid;
		page->dirty = true;
	}

	void InitIndexNode(NODE_INFO* node, NID nid, NID parent, CPageAllocator::INDEX page_id, CPageInfo * page)
	{
		memset(node, 0xFF, sizeof(NODE_INFO));
		node->m_nid = nid;
		node->m_type = NODE_INFO::NODE_INDEX;
		node->m_parent_node = parent;
		node->valid_data = 0;
		node->page_id = page_id;

		page->data_index = nid;
		page->inode = parent;
		page->dirty = true;
	}

	inline NODE_INFO& get_inode(FID fid)
	{
		if (fid >= MAX_NODE_NUM) THROW_ERROR(ERR_APP, L"invalid fid number: %d", fid);
		return m_block_buf.m_data_buffer[fid];
	}


	// 文件系统状态变量（Clone时，需要复制的变量）
protected:
	// 模拟磁盘layout
	CF2fsSegmentManager m_segments;
	FsHealthInfo m_health_info;

protected:
	// 文件系统数据
	CPageAllocator m_pages;
//	CInodeManager_ m_inodes;
	CBufferManager<NODE_INFO> m_block_buf;

protected:
	std::map<std::wstring, FID> m_path_map;
//	LBLK_T m_level_to_offset[MAX_INDEX_LEVEL];
	int m_multihead_cnt = 0;
	UINT m_node_blks = 0;	//使用的inode或index node的数量

	// data for log
	size_t m_write_count = 0;
//	SEG_T m_gc_th_low, m_gc_th_hi;
	// 对于删除文件的写入量统计。
	//UINT64 m_truncated_host_write[BT_TEMP_NR];
	//UINT64 m_truncated_media_write[BT_TEMP_NR];


protected:
	// log和统计信息
	std::wstring m_log_path;
#ifdef ENABLE_FS_TRACE
	FILE* m_log_invalid_trace = nullptr;
	FILE* m_log_write_trace = nullptr;		// 文件：trace_fs.csv
	// 用于记录文件系统层次的写入
	FILE* m_log_fs_trace = nullptr;			// 文件: fs_trace.csv;
	FILE* m_gc_trace = nullptr;
	FILE* m_inode_trace = nullptr;
#endif

	UINT m_file_num = 0;						// 总的有效文件数量；
	int m_free_blks = 0;
};


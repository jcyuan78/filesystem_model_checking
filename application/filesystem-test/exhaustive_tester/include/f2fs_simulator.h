///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "fs_simulator.h"

#include "f2fs_segment.h"

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// == 参数配置  ==
#define MAX_INDEX_LEVEL			(3)
#define INDEX_SIZE				(128)
#define MAX_TABLE_SIZE			(128)

#define NR_DENTRY_IN_BLOCK		(4)
#define FN_SLOT_LEN				(4)
#define MAX_DENTRY_LEVEL		(4)

#define ROOT_FID				(0)

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// == inode and index node  ==
// 用于记录一个实际的inode
struct INODE
{
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

struct NODE_INFO
{
public:
//	enum NODE_TYPE { NODE_FREE, NODE_INODE, NODE_INDEX, } m_type;
	NID m_nid;
	NID m_parent_node;
	UINT valid_data;
	CPageAllocator::INDEX page_id;		// 这个node对应的page的id
	union {
		INODE inode;
		INDEX_NODE index;
	};
};

struct DENTRY
{
	FID ino;
	WORD hash;
	WORD name_len;
	BYTE file_type;
};

struct DENTRY_BLOCK
{
	DWORD bitmap;
	char filenames[NR_DENTRY_IN_BLOCK][FN_SLOT_LEN];
	DENTRY dentries[NR_DENTRY_IN_BLOCK];
};

#define NODE_NEXT_FREE node.m_nid
struct BLOCK_DATA
{
	enum BLOCK_TYPE {BLOCK_FREE, BLOCK_INODE, BLOCK_INDEX, BLOCK_DENTRY} m_type;
	union
	{
		NODE_INFO node;
		DENTRY_BLOCK dentry;
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
	// 返回 root的 NODE
	BLOCK_TYPE * Init(void) {
		// 构建free list
		for (UINT ii = 1; ii < MAX_NODE_NUM; ++ii)
		{
			m_data_buffer[ii].NODE_NEXT_FREE = ii + 1;
			m_data_buffer[ii].m_type = BLOCK_DATA::BLOCK_FREE;
		}
		m_data_buffer[MAX_NODE_NUM - 1].NODE_NEXT_FREE = INVALID_BLK;
		m_data_buffer[0].NODE_NEXT_FREE = 0;	// 保留第0号node
		m_free_ptr = 1;
		m_used_nr = 1;
		// FID = 0表示root
		return m_data_buffer + 0;
	}
	_INDEX get_block(void)
	{
		if (m_used_nr >= MAX_NODE_NUM) THROW_ERROR(ERR_APP, L"no free block buffer");
		_INDEX index = m_free_ptr;
		m_free_ptr = m_data_buffer[index].NODE_NEXT_FREE;
		m_used_nr++;
		return index;
	}
	void put_block(_INDEX index)
	{
		m_data_buffer[index].m_type = BLOCK_DATA::BLOCK_FREE;
		m_data_buffer[index].NODE_NEXT_FREE = (NID)m_free_ptr;
		m_free_ptr = index;
		m_used_nr--;
	}
public:
	BLOCK_TYPE m_data_buffer[MAX_NODE_NUM];
protected:
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
	virtual void FileWrite(FID fid, FSIZE offset, FSIZE len);
	virtual void FileRead(std::vector<CPageInfo*>& blks, FID fid, FSIZE offset, FSIZE secs);
	// 可以truncate部分文件
	virtual void FileTruncate(FID fid, FSIZE offset, FSIZE secs);
	virtual void FileDelete(const std::wstring & fn);
	virtual void FileFlush(FID fid);
	virtual void FileClose(FID fid);
	//virtual void FileOpen(FID fid, bool delete_on_close = false);
	virtual FSIZE GetFileSize(FID fid);
	virtual FID  FileOpen(const std::wstring& fn, bool delete_on_close = false);

	virtual bool Mount(void) {	return false;	}
	virtual bool Unmount(void) { return false; }
	virtual bool Reset(void) { return false; }

	virtual void Clone(IFsSimulator*& dst);
	virtual void CopyFrom(const IFsSimulator* src);


	virtual void DumpSegments(const std::wstring& fn, bool sanity_check);
	virtual void DumpSegmentBlocks(const std::wstring& fn);
	virtual void DumpFileMap(FILE* out, FID fid) { DumpFileMap_merge(out, fid); }

	virtual void DumpAllFileMap(const std::wstring& fn);
	virtual void DumpBlockWAF(const std::wstring& fn);

	virtual void GetConfig(boost::property_tree::wptree& config, const std::wstring& config_name);
	// 返回最大支持文件大小的block数量
	virtual DWORD MaxFileSize(void) const { return MAX_TABLE_SIZE* MAX_TABLE_SIZE-10; }

//	virtual void SetLogFolder(const std::wstring& fn);
	virtual void GetFsInfo(FS_INFO& space_info);
	virtual void GetHealthInfo(FsHealthInfo& info) const {};

#ifdef ENABLE_FS_TRACE
	void fs_trace(const char* op, FID fid, DWORD start_blk, DWORD blk_nr);
#else
	void fs_trace(const char* op, FID fid, DWORD start_blk, DWORD blk_nr) {}
#endif

protected:
	friend class CF2fsSegmentManager;

	NODE_INFO * FileOpenInternal(const char* fn);
	// 在父目录中查找文件fn, 找到返回fid，找不到返回invalid_blk
	FID FindFile(NODE_INFO& parent, const char* fn);

	// pages: out 读取到的page_id，调用者申请内存
	void FileReadInternal(CPageAllocator::INDEX *pages, NODE_INFO & inode, FSIZE start_blk, FSIZE end_blk);
	void FileWriteInternal(NODE_INFO& inode, FSIZE start_blk, FSIZE end_blk, CPageAllocator::INDEX * pages);

	inline WORD FileNameHash(const char* fn)
	{
		size_t seed = 0;
		const char* p = fn;
		while (*p != 0) {
			boost::hash_combine(seed, *p); p++;
		}
		return ( (WORD)(seed & 0xFFFF) );
	}

	void AddChild(NODE_INFO* parent, const char* fn, FID inode);
	void RemoveChild(NODE_INFO* parent, WORD hash, FID fid);
	FID InternalCreatreFile(const std::wstring& fn, bool is_dir);
	void FileRemove(NODE_INFO * inode);
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

	void InitInode(BLOCK_DATA* block, NID nid, NID parent, CPageAllocator::INDEX page_id, CPageInfo * page)
	{
		memset(block, 0xFF, sizeof(BLOCK_DATA));
		block->m_type = BLOCK_DATA::BLOCK_INODE;

		NODE_INFO* node = &(block->node);
		node->m_nid = nid;
		node->valid_data = 0;
		node->page_id = page_id;
		node->m_parent_node = parent;

		node->inode.blk_num = 0;
		node->inode.file_size = 0;
		node->inode.ref_count = 0;
		node->inode.delete_on_close = false;

		page->data_index = nid;
		page->inode = nid;
		page->dirty = true;
	}

	void InitIndexNode(BLOCK_DATA* block, NID nid, NID parent, CPageAllocator::INDEX page_id, CPageInfo * page)
	{
		memset(block, 0xFF, sizeof(BLOCK_DATA));
		block->m_type = BLOCK_DATA::BLOCK_INDEX;

		NODE_INFO* node = &(block->node);
		node->m_nid = nid;
		node->m_parent_node = parent;
		node->valid_data = 0;
		node->page_id = page_id;

		page->data_index = nid;
		page->inode = parent;
		page->dirty = true;
	}

	void InitDentry(BLOCK_DATA* block)
	{
		memset(block, 0xFF, sizeof(BLOCK_DATA));
		block->m_type = BLOCK_DATA::BLOCK_DENTRY;
		DENTRY_BLOCK& entries = block->dentry;
		entries.bitmap = 0;

	}

	BLOCK_DATA* AllocateDentryBlock(CPageAllocator::INDEX & page_id);

	inline NODE_INFO& get_inode(FID fid)
	{
		if (fid >= MAX_NODE_NUM) THROW_ERROR(ERR_APP, L"invalid fid number: %d", fid);
		BLOCK_DATA & block = m_block_buf.m_data_buffer[fid];
		if (block.m_type != BLOCK_DATA::BLOCK_INDEX && block.m_type != BLOCK_DATA::BLOCK_INODE) {
			THROW_ERROR(ERR_APP, L"block[%d] is not a node block", fid);
		}
		return block.node;
	}

	inline BLOCK_DATA& get_block(FID fid)
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
	typedef CBufferManager<BLOCK_DATA>	BLOCK_BUF_TYPE;
	BLOCK_BUF_TYPE m_block_buf;

protected:
	int m_multihead_cnt = 0;
	UINT m_node_blks = 0;	//使用的inode或index node的数量

	// data for log
//	size_t m_write_count = 0;
	// 对于删除文件的写入量统计。

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

	//UINT m_file_num = 0;						// 总的有效文件数量；
	//int m_free_blks = 0;
};


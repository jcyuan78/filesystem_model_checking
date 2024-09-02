///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "fs_simulator.h"
#include "config.h"

#include "f2fs_segment.h"

#include "storage.h"
#include "blocks.h"
#include "pages.h"

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// == 参数配置  ==



#define ROOT_FID				(0)

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////



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

class CNodeAddressTable
{
public:
	CNodeAddressTable(CF2fsSimulator* fs);
	NID Init(PHY_BLK root);
	void build_free(void);
	void CopyFrom(const CNodeAddressTable* src);
public:
	bool Load(void);
	void Sync(void);
	void Reset(void);

	NID get_node(void);
	void put_node(NID node);
	PHY_BLK get_phy_blk(NID nid);
	void set_phy_blk(NID nid, PHY_BLK phy_blk);
public:
	PAGE_INDEX node_catch[NODE_NR];
protected:
	PHY_BLK nat[NODE_NR];
	NID next_scan;			// 从这个位置开始搜索下一个free
	UINT free_nr;
	CPageAllocator* m_pages;
	CStorage* m_storage;
};

// 用于缓存已经打开的文件，inode的page
struct OPENED_FILE
{
	NID ino;
	PAGE_INDEX ipage;
};


///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// == file system  ==
class CF2fsSimulator : public IFsSimulator
{
public:
	CF2fsSimulator(void);
	virtual ~CF2fsSimulator(void);
	virtual void Clone(IFsSimulator*& dst);
	virtual void CopyFrom(const IFsSimulator* src);
protected:
	void InternalCopyFrom(const CF2fsSimulator* src);

public:
	virtual bool Initialzie(const boost::property_tree::wptree& config, const std::wstring & log_path);
	virtual NID  FileCreate(const std::string& fn);
	virtual NID  DirCreate(const std::string& fn);

	virtual void SetFileSize(NID fid, FSIZE secs) {}
	virtual void FileWrite(NID fid, FSIZE offset, FSIZE len);
//	virtual size_t FileRead(CPageInfo* blks[], NID fid, FSIZE offset, FSIZE secs);
	virtual size_t FileRead(FILE_DATA blks[], NID fid, FSIZE offset, FSIZE secs);
	// 可以truncate部分文件
	virtual void FileTruncate(NID fid, FSIZE offset, FSIZE secs);
	virtual void FileDelete(const std::string & fn);
	virtual void FileFlush(NID fid);
	virtual void FileClose(NID fid);
	//virtual void FileOpen(NID fid, bool delete_on_close = false);
	virtual FSIZE GetFileSize(NID fid);
	virtual NID  FileOpen(const std::string& fn, bool delete_on_close = false);

	virtual bool Mount(void);
	virtual bool Unmount(void);
	virtual bool Reset(void);

	virtual void DumpSegments(const std::wstring& fn, bool sanity_check);
	virtual void DumpSegmentBlocks(const std::wstring& fn);
	virtual void DumpFileMap(FILE* out, NID fid) { DumpFileMap_merge(out, fid); }

	virtual void DumpAllFileMap(const std::wstring& fn);
	virtual void DumpBlockWAF(const std::wstring& fn);
	virtual size_t DumpFileIndex(NID index[], size_t buf_size, NID fid);

	virtual void GetGcTrace(std::vector<GC_TRACE>& gc) { 
#ifdef GC_TRACE
		gc = m_segments.gc_trace; 
#endif
	};

	virtual void GetConfig(boost::property_tree::wptree& config, const std::wstring& config_name);
	// 返回最大支持文件大小的block数量
	virtual DWORD MaxFileSize(void) const { return MAX_FILE_BLKS -10; }

//	virtual void SetLogFolder(const std::wstring& fn);
	virtual void GetFsInfo(FS_INFO& space_info);
	virtual void GetHealthInfo(FsHealthInfo& info) const {};

#ifdef ENABLE_FS_TRACE
	void fs_trace(const char* op, NID fid, DWORD start_blk, DWORD blk_nr);
#else
	void fs_trace(const char* op, NID fid, DWORD start_blk, DWORD blk_nr) {}
#endif

protected:
	friend class CF2fsSegmentManager;
	friend class CStorage;
	friend class CPageAllocator;
	friend class CNodeAddressTable;

	// 
//	NODE_INFO * FileOpenInternal(const char* fn, CPageInfo * inode_page, PPAGE * parent = nullptr);
	// 找到文件fn(全路径）的NID，parent返回NID所在父目录的inode的page
	NID FileOpenInternal(char* fn, CPageInfo* &parent);
	void FileCloseInternal(OPENED_FILE* file);
	// 在父目录中查找文件fn, 找到返回fid，找不到返回invalid_blk
	NID FindFile(NODE_INFO& parent, const char* fn);

	OPENED_FILE* FindOpenFile(NID fid);
	OPENED_FILE* AddFileToOpenList(NID fid, CPageInfo* page);

	void InitOpenList(void);

	// 关闭打开的inode，释放被inode缓存的page
	void CloseInode(CPageInfo* &ipage);

	// pages: out 读取到的page_id，调用者申请内存
//	void FileReadInternal(CPageAllocator::INDEX *pages, NODE_INFO & nid, FSIZE start_blk, FSIZE end_blk);
	void FileReadInternal(CPageInfo * pages[], NODE_INFO& inode, FSIZE start_blk, FSIZE end_blk);

//	void FileWriteInternal(NODE_INFO& nid, FSIZE start_blk, FSIZE end_blk, CPageAllocator::INDEX * pages);
	void FileWriteInternal(NODE_INFO& inode, FSIZE start_blk, FSIZE end_blk, CPageInfo* pages[]);

	void FileTruncateInternal(CPageInfo * ipage, LBLK_T start_blk, LBLK_T end_blk);

	inline WORD FileNameHash(const char* fn)
	{
		size_t seed = 0;
		const char* p = fn;
		while (*p != 0) {
			boost::hash_combine(seed, *p); p++;
		}
		return ( (WORD)(seed & 0xFFFF) );
	}

	// 将文件添加到parent中，parent:需要添加的目录文件的inode，nid：子文件的inode id
	void AddChild(NODE_INFO* parent, const char* fn, NID nid);
//	void RemoveChild(NODE_INFO* parent, WORD hash, NID fid);
	// 从parent中将fid移除
	void Unlink(NID fid, CPageInfo* parent);
	NID InternalCreatreFile(const std::string& fn, bool is_dir);
//	void FileRemove(NODE_INFO * inode);

	void FileRemove(CPageInfo* &inode_page);
	// 计算block的实际温度，不考虑目前使用的算法。
	BLK_TEMP GetBlockTemp(CPageInfo* page);
	// 根据当前的算法计算block的温度。
	BLK_TEMP GetAlgorithmBlockTemp(CPageInfo* page, BLK_TEMP temp);
	// 在输出file map时，不合并连续的物理block
	void DumpFileMap_no_merge(FILE* out, NID fid);
	bool InvalidBlock(const char* reason, PHY_BLK phy_blk);
	void OffsetToIndex(CIndexPath& path, LBLK_T offset, bool alloc);
	// 将index_path移动到下一个block位置。用于加速offset到index的转化。如果index block发生变化，则清除path, 需要重新计算。
	void NextOffset(CIndexPath& path);
	// 检查inode中的所有index block和inode block，如果没有在磁盘上，测保存, 返回inode的phy blk
	//PHY_BLK UpdateInode(NODE_INFO& nid, const char* caller = "");
	PHY_BLK UpdateInode(CPageInfo * ipage, const char* caller = "");

	// 在输出file map时，并连续的物理block
	void DumpFileMap_merge(FILE* out, NID fid);

	void InitInode(BLOCK_DATA* block, CPageInfo * page)
	{
		memset(block, 0xFF, sizeof(BLOCK_DATA));
		block->m_type = BLOCK_DATA::BLOCK_INODE;

		NODE_INFO* node = &(block->node);
		node->m_nid = m_nat.get_node();
		if (node->m_nid == INVALID_BLK) THROW_ERROR(ERR_APP, L"node is full");

		node->valid_data = 0;
		node->page_id = page->page_id;
		node->m_ino = node->m_nid;

		node->inode.blk_num = 0;
		node->inode.file_size = 0;
		node->inode.ref_count = 0;
		node->inode.nlink = 0;
//		node->inode.delete_on_close = false;

		page->nid = node->m_nid;
		page->offset = INVALID_BLK;
		page->dirty = true;
	}

	void InitIndexNode(BLOCK_DATA* block, NID parent, CPageInfo * page)
	{
		memset(block, 0xFF, sizeof(BLOCK_DATA));
		block->m_type = BLOCK_DATA::BLOCK_INDEX;

		NODE_INFO* node = &(block->node);
		node->m_nid = m_nat.get_node();
		if (node->m_nid == INVALID_BLK) THROW_ERROR(ERR_APP, L"node is full");

		node->m_ino = parent;
		node->valid_data = 0;
		node->page_id = page->page_id;

//		page->nid = parent;
		page->nid = node->m_nid;
		page->offset = INVALID_BLK;
		page->dirty = true;
	}

	void InitDentry(BLOCK_DATA* block)
	{
		memset(block, 0xFF, sizeof(BLOCK_DATA));
		block->m_type = BLOCK_DATA::BLOCK_DENTRY;
		DENTRY_BLOCK& entries = block->dentry;
		entries.bitmap = 0;

	}

	// 更新NAT，nid：node id，phy_blk：node新的物理地址
	void UpdateNat(NID nid, PHY_BLK phy_blk);
	// 更新index table中，block的物理地址，nid：index table的node id，offset：data block在index中的offset，phy_blk：data block新的物理地址
	void UpdateIndex(NID nid, UINT offset, PHY_BLK phy_blk);

	CPageInfo* AllocateDentryBlock();
	// 读取Node，并保存在page中。page会自动cache，不需要释放
	NODE_INFO& ReadNode(NID fid, CPageInfo * &page);
	// 文件系统状态变量（Clone时，需要复制的变量）
protected:
	// 模拟磁盘layout
	CF2fsSegmentManager m_segments;
	CNodeAddressTable m_nat;
	CStorage m_storage;
	FsHealthInfo m_health_info;


protected:
	// OS的数据缓存。m_pages模拟OS的页缓存，m_block_buf为page提供数据。由于文件数据不需要实际数据，可以省略。
	CPageAllocator m_pages;
	OPENED_FILE m_open_files[MAX_OPEN_FILE];
	UINT m_free_ptr;	// 空闲列表的头指针
	UINT m_open_nr;		// 打开的文件数量

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


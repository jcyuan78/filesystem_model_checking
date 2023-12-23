///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "../include/fs_comm.h"
#include "lfs_simulator.h"

#include "../include/f2fs_segment.h"

class CPageInfo;

class CNodeInfoBase
{
public:
	enum NODE_TYPE {
		NODE_FREE, NODE_INODE, NODE_INDEX,
	};

	CNodeInfoBase(NODE_TYPE type, FID id, CPageInfo * page, CNodeInfoBase * p) 
		: m_type(type), data_page(page), parent(p), m_fid(id)
	{
		memset(data, 0, MAX_TABLE_SIZE);
		//m_fid = next_fid++;
	}
	virtual ~CNodeInfoBase(void) {}

public:
	NODE_TYPE m_type;		// 0： inode, 1: direct index, 2: indirect index;
	FID m_fid;				// INVALID_BLK表示此inode未被使用
	CPageInfo* data_page;
	CNodeInfoBase* parent;

	size_t valid_data=0, data_nr= MAX_TABLE_SIZE;
	CPageInfo* data[MAX_TABLE_SIZE];
};

class CInodeInfo : public CNodeInfoBase
{
public:
	CInodeInfo(FID id, CPageInfo* page) : CNodeInfoBase(NODE_INODE, id, page, nullptr) {}
public:
	std::wstring m_fn;
	size_t m_blks;		// 文件大小：block单位
	UINT m_ref_count = 0;
	bool m_delete_on_close;
	DWORD m_host_write;

	//size_t m_valid_blk;
//public:
//	virtual CPageInfo* GetDataPage(int index) = 0;
};

class CDirectInfo : public CNodeInfoBase
{
public:
	CDirectInfo(FID id, CPageInfo*page, CNodeInfoBase * parent) : CNodeInfoBase(NODE_INDEX, id, page, parent) {}
public:
	//size_t m_valid_blk;
	//CPageInfo data[INDEX_SIZE];
//public:
//	virtual CPageInfo* GetDataPage(int index) = 0;
};

class CPageInfo : public CPageInfoBase
{
public:

public:
	PHY_BLK phy_blk = INVALID_BLK;	// page所在物理位置
	BLK_TEMP temp;	// page的温度
	//在文件中的位置
	CInodeInfo* inode = nullptr;
	LBLK_T offset = INVALID_BLK;
	// 数据(对于inode 或者 direct node)
	CNodeInfoBase* data = nullptr;
	bool dirty = false;
	// 统计
	//UINT host_write = 0, media_write = 0;
	enum PAGE_TYPE {PAGE_DATA, PAGE_NODE} type;
};

class CIndexPath
{
public:
	CIndexPath(CInodeInfo* inode)
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
	CNodeInfoBase* node[MAX_INDEX_LEVEL];
};

class CInodeManager_;

//
//class CF2fsSegmentManager : public CSegmentManagerBase<CPageInfo *>
//{
//public:
//	virtual ~CF2fsSegmentManager(void);
//	typedef CPageInfo* _BLK_TYPE;
//public:
//	// 写入data block到segment, file_index 文件id, blk：文件中的相对block，temp温度
//	void CheckGarbageCollection(void)
//	{
//		if (m_free_nr < m_gc_lo) GarbageCollection();
//	}
//	virtual PHY_BLK WriteBlockToSeg(const _BLK_TYPE &lblk, BLK_TEMP temp);
//	virtual void GarbageCollection(void);
//	void DumpSegmentBlocks(const std::wstring& fn);
//	virtual bool InitSegmentManager(SEG_T segment_nr, SEG_T gc_lo, SEG_T gc_hi, int init_val=0);
//
//	friend class CF2fsSimulator;
//	FILE* m_gc_trace;
//protected:
//
//protected:
//	CInodeManager_* m_inodes = nullptr;
//
//};

class CInodeManager_
{
public:
	CInodeManager_(void) { m_nodes.push_back(nullptr); };
	~CInodeManager_(void) {};
public:
	CNodeInfoBase* allocate_inode(CNodeInfoBase::NODE_TYPE type, CNodeInfoBase* parent)
	{
		CPageInfo* page = new CPageInfo;
		page->type = CPageInfo::PAGE_NODE;
		page->temp = BT_HOT__NODE;

		CNodeInfoBase* node = nullptr;
		FID fid = (FID)m_nodes.size();
		if (type == CNodeInfoBase::NODE_INODE)
		{
			CInodeInfo* _node = new CInodeInfo(fid, page);
			node = static_cast<CNodeInfoBase*>(_node);
			page->inode = _node;
		}
		else
		{
			CDirectInfo* _node = new CDirectInfo(fid, page, parent);
			node = static_cast<CNodeInfoBase*>(_node);
			page->inode = dynamic_cast<CInodeInfo*>(parent); JCASSERT(page->inode);
		}
		page->data = node;
		page->dirty = true;
		m_nodes.push_back(node);
		return node;
	}
	void free_inode(FID nid)
	{
		CNodeInfoBase* node = m_nodes.at(nid);
		delete node->data_page;
		delete node;
		m_nodes.at(nid) = nullptr;
	}
	CNodeInfoBase* get_node(FID nid) { return m_nodes.at(nid); }

//	FID allocate_index_block(void);
	FID get_node_nr(void) const { return (FID)(m_nodes.size()); }

public:
	//void init_node(inode_info& node, int type, FID file_index);
//	& get_index_block(FID nid);

protected:
	std::list<FID> m_free_list;
	//	std::vector<inode_info> m_inodes;
	//	std::vector<inode_info*> m_node_buffer;
	std::vector<CNodeInfoBase*> m_nodes;
	size_t m_node_nr;
	size_t m_used_nr, m_free_nr;

//	FID next_fid;
};

class CF2fsSimulator : public CLfsBase
{
public:
	CF2fsSimulator(void) {};
	virtual ~CF2fsSimulator(void);

public:
	virtual bool Initialzie(const boost::property_tree::wptree& config);
	virtual FID  FileCreate(const std::wstring& fn);
	virtual void SetFileSize(FID fid, size_t secs) {}
	virtual void FileWrite(FID fid, size_t offset, size_t secs);
	virtual void FileRead(std::vector<CPageInfoBase*>& blks, FID fid, size_t offset, size_t secs);
	virtual void FileTruncate(FID fid);
	virtual void FileDelete(FID fid);
	virtual void FileFlush(FID fid);
	virtual void FileClose(FID fid);
	virtual void FileOpen(FID fid, bool delete_on_close = false);

//	virtual DWORD MaxFileSize(void) const;
//	virtual void FileOpen(FID fid, bool delete_on_close = false);
//	virtual void FileClose(FID fid);
//	virtual void GetHealthInfo(FsHealthInfo& info) const
//	{
//		memcpy_s(&info, sizeof(FsHealthInfo), &m_health_info, sizeof(FsHealthInfo));
//	}
	virtual void DumpSegments(const std::wstring& fn, bool sanity_check);
	virtual void DumpSegmentBlocks(const std::wstring& fn);
//	virtual void DumpAllFileMap(const std::wstring& fn);
	//	virtual void DumpFileMap(FILE* out, FID file_index) { DumpFileMap_no_merge(out, file_index); }
	virtual void DumpFileMap(FILE* out, FID fid) { DumpFileMap_merge(out, fid); }
//	virtual void SetLogFolder(const std::wstring& fn);

	virtual void DumpAllFileMap(const std::wstring& fn);

	BLK_TEMP GetBlockTemp(CPageInfo* page);

protected:
	// 在输出file map时，并连续的物理block
	void DumpFileMap_merge(FILE* out, FID fid);

	// 在输出file map时，不合并连续的物理block
	void DumpFileMap_no_merge(FILE* out, FID fid);

	bool InvalidBlock(const char* reason, PHY_BLK phy_blk);
	void OffsetToIndex(CIndexPath& path, LBLK_T offset, bool alloc);

protected:
	// 根据inode，计算data block的温度
//	BLK_TEMP get_temperature(CInodeInfo* inode, LBLK_T offset) { return BT_HOT__DATA; }
	// 将逻辑block转换成在index层中的位置（从0层到最底层的路径）
	//  alloc = true时，当遇到index block不存在时则创建，否则返回null
//	void InitIndexPath(index_path& path, inode_info* inode);
	// 将index_path移动到下一个block位置。用于加速offset到index的转化。如果index block发生变化，则清除path, 需要重新计算。
	void NextOffset(CIndexPath& path);
	// 返回ipath中指示的index的位置，用于更新。如果index block为空，则创建index block
	//PHY_BLK* GetPhyBlockForWrite(index_path& path);
	// 返回direct node以及ipath所指向的offset

//	PHY_BLK SyncInode(inode_info& inode);
	PHY_BLK SyncInode(CPageInfo * page);
	// 检查inode中的所有index block和inode block，如果没有在磁盘上，测保存
	void UpdateInode(CInodeInfo* inode);


protected:
	CInodeManager_ m_inodes;
	// 模拟磁盘
	CF2fsSegmentManager m_segments;

	//LBLK_T m_logic_blks;		// 逻辑大小
	//LBLK_T m_physical_blks;
	LBLK_T m_level_to_offset[MAX_INDEX_LEVEL];

	//FsHealthInfo m_health_info;
#ifdef _SANITY_CHECK
	// 用于检查P2L的表格
#endif
	// data for log
	//DWORD m_last_host_write = 0, m_last_media_write = 0;
//	std::wstring m_log_fn;
//	FILE* m_log_invalid_trace = nullptr;
//	FILE* m_log_write_trace = nullptr;
//	FILE* m_gc_trace = nullptr;
	size_t m_write_count = 0;
	SEG_T m_gc_th_low, m_gc_th_hi;
};
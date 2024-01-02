///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "../include/fs_comm.h"
#include "lfs_simulator.h"

#include "../include/f2fs_segment.h"

class CPageInfo;

// 实现在磁盘上存储的Node Block，包括Inode, Index Node, Indirect index node等。
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

// 用于记录一个实际的inode
class CInodeInfo : public CNodeInfoBase
{
public:
	CInodeInfo(FID id, CPageInfo* page) : CNodeInfoBase(NODE_INODE, id, page, nullptr) 
	{
		m_host_write = 0;
		m_media_write = 0;
	}
public:
	std::wstring m_fn;
	size_t m_blks;		// 文件大小：block单位
	UINT m_ref_count = 0;
	bool m_delete_on_close;
	DWORD m_host_write;
	DWORD m_media_write;

	// 按照逻辑地址，记录每个块的写入次数，host的写入次数以及media的写入次数。
	std::vector<int> m_host_write_count;
	std::vector<int> m_total_write_count;

};

class CDirectInfo : public CNodeInfoBase
{
public:
	CDirectInfo(FID id, CPageInfo*page, CNodeInfoBase * parent) : CNodeInfoBase(NODE_INDEX, id, page, parent) {}
public:
};

class CPageInfo : public CPageInfoBase
{
public:

public:
	PHY_BLK phy_blk = INVALID_BLK;	// page所在物理位置
	// 标记page的温度，当page被写入SSD时更新。这个温度不是实际分配到温度，所有算法下都相同。仅用于统计。
	BLK_TEMP ttemp;
	//在文件中的位置
	CInodeInfo* inode = nullptr;
	LBLK_T offset = INVALID_BLK;
	// 数据(对于inode 或者 direct node)
	CNodeInfoBase* data = nullptr;
	bool dirty = false;
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
//		page->temp = BT_HOT__NODE;

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

	virtual void DumpSegments(const std::wstring& fn, bool sanity_check);
	virtual void DumpSegmentBlocks(const std::wstring& fn);
	virtual void DumpFileMap(FILE* out, FID fid) { DumpFileMap_merge(out, fid); }

	virtual void DumpAllFileMap(const std::wstring& fn);
	virtual void DumpBlockWAF(const std::wstring& fn);


	virtual void GetConfig(boost::property_tree::wptree& config, const std::wstring& config_name);

	virtual void SetLogFolder(const std::wstring& fn);


	// 计算block的实际温度，不考虑目前使用的算法。
	BLK_TEMP GetBlockTemp(CPageInfo* page);
	// 根据当前的算法计算block的温度。
	BLK_TEMP GetAlgorithmBlockTemp(CPageInfo* page, BLK_TEMP temp);

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

	// 检查inode中的所有index block和inode block，如果没有在磁盘上，测保存
	void UpdateInode(CInodeInfo* inode, const char* caller="");


protected:
	CInodeManager_ m_inodes;
	// 模拟磁盘
	CF2fsSegmentManager m_segments;
	LBLK_T m_level_to_offset[MAX_INDEX_LEVEL];
	// 
	int m_multihead_cnt=0;


#ifdef _SANITY_CHECK
	// 用于检查P2L的表格
#endif
	// data for log
	size_t m_write_count = 0;
	SEG_T m_gc_th_low, m_gc_th_hi;
	// 对于删除文件的写入量统计。
	UINT64 m_truncated_host_write[BT_TEMP_NR];
	UINT64 m_truncated_media_write[BT_TEMP_NR];

	FILE* m_inode_trace = nullptr;
};
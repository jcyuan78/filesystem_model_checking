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
	NODE_TYPE m_type;		// 0�� inode, 1: direct index, 2: indirect index;
	FID m_fid;				// INVALID_BLK��ʾ��inodeδ��ʹ��
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
	size_t m_blks;		// �ļ���С��block��λ
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
	PHY_BLK phy_blk = INVALID_BLK;	// page��������λ��
	BLK_TEMP temp;	// page���¶�
	//���ļ��е�λ��
	CInodeInfo* inode = nullptr;
	LBLK_T offset = INVALID_BLK;
	// ����(����inode ���� direct node)
	CNodeInfoBase* data = nullptr;
	bool dirty = false;
	// ͳ��
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
//	// д��data block��segment, file_index �ļ�id, blk���ļ��е����block��temp�¶�
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
	// �����file mapʱ��������������block
	void DumpFileMap_merge(FILE* out, FID fid);

	// �����file mapʱ�����ϲ�����������block
	void DumpFileMap_no_merge(FILE* out, FID fid);

	bool InvalidBlock(const char* reason, PHY_BLK phy_blk);
	void OffsetToIndex(CIndexPath& path, LBLK_T offset, bool alloc);

protected:
	// ����inode������data block���¶�
//	BLK_TEMP get_temperature(CInodeInfo* inode, LBLK_T offset) { return BT_HOT__DATA; }
	// ���߼�blockת������index���е�λ�ã���0�㵽��ײ��·����
	//  alloc = trueʱ��������index block������ʱ�򴴽������򷵻�null
//	void InitIndexPath(index_path& path, inode_info* inode);
	// ��index_path�ƶ�����һ��blockλ�á����ڼ���offset��index��ת�������index block�����仯�������path, ��Ҫ���¼��㡣
	void NextOffset(CIndexPath& path);
	// ����ipath��ָʾ��index��λ�ã����ڸ��¡����index blockΪ�գ��򴴽�index block
	//PHY_BLK* GetPhyBlockForWrite(index_path& path);
	// ����direct node�Լ�ipath��ָ���offset

//	PHY_BLK SyncInode(inode_info& inode);
	PHY_BLK SyncInode(CPageInfo * page);
	// ���inode�е�����index block��inode block�����û���ڴ����ϣ��Ᵽ��
	void UpdateInode(CInodeInfo* inode);


protected:
	CInodeManager_ m_inodes;
	// ģ�����
	CF2fsSegmentManager m_segments;

	//LBLK_T m_logic_blks;		// �߼���С
	//LBLK_T m_physical_blks;
	LBLK_T m_level_to_offset[MAX_INDEX_LEVEL];

	//FsHealthInfo m_health_info;
#ifdef _SANITY_CHECK
	// ���ڼ��P2L�ı��
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
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "../include/fs_comm.h"
#include "lfs_simulator.h"

#include "../include/f2fs_segment.h"

class CPageInfo;

// ʵ���ڴ����ϴ洢��Node Block������Inode, Index Node, Indirect index node�ȡ�
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

// ���ڼ�¼һ��ʵ�ʵ�inode
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
	size_t m_blks;		// �ļ���С��block��λ
	UINT m_ref_count = 0;
	bool m_delete_on_close;
	DWORD m_host_write;
	DWORD m_media_write;

	// �����߼���ַ����¼ÿ�����д�������host��д������Լ�media��д�������
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
	PHY_BLK phy_blk = INVALID_BLK;	// page��������λ��
	// ���page���¶ȣ���page��д��SSDʱ���¡�����¶Ȳ���ʵ�ʷ��䵽�¶ȣ������㷨�¶���ͬ��������ͳ�ơ�
	BLK_TEMP ttemp;
	//���ļ��е�λ��
	CInodeInfo* inode = nullptr;
	LBLK_T offset = INVALID_BLK;
	// ����(����inode ���� direct node)
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


	// ����block��ʵ���¶ȣ�������Ŀǰʹ�õ��㷨��
	BLK_TEMP GetBlockTemp(CPageInfo* page);
	// ���ݵ�ǰ���㷨����block���¶ȡ�
	BLK_TEMP GetAlgorithmBlockTemp(CPageInfo* page, BLK_TEMP temp);

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

	// ���inode�е�����index block��inode block�����û���ڴ����ϣ��Ᵽ��
	void UpdateInode(CInodeInfo* inode, const char* caller="");


protected:
	CInodeManager_ m_inodes;
	// ģ�����
	CF2fsSegmentManager m_segments;
	LBLK_T m_level_to_offset[MAX_INDEX_LEVEL];
	// 
	int m_multihead_cnt=0;


#ifdef _SANITY_CHECK
	// ���ڼ��P2L�ı��
#endif
	// data for log
	size_t m_write_count = 0;
	SEG_T m_gc_th_low, m_gc_th_hi;
	// ����ɾ���ļ���д����ͳ�ơ�
	UINT64 m_truncated_host_write[BT_TEMP_NR];
	UINT64 m_truncated_media_write[BT_TEMP_NR];

	FILE* m_inode_trace = nullptr;
};
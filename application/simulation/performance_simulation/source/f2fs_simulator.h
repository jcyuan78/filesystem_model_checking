///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "../include/fs_comm.h"
#include "lfs_simulator.h"

#include "../include/f2fs_segment.h"

#include "../include/pages.h"

#define MAX_NODE_NUM	10240

// ʵ���ڴ����ϴ洢��Node Block������Inode, Index Node, Indirect index node�ȡ�
class CNodeInfoBase
{
public:
	enum NODE_TYPE {
		NODE_FREE, NODE_INODE, NODE_INDEX,
	};

	CNodeInfoBase(void) {}

	void Init(NODE_TYPE type, FID id, CPageInfo* page, CNodeInfoBase* p)
	{
		m_type = type;
		m_fid = id;
		data_page = page;
		parent = p;
		memset(data, 0, MAX_TABLE_SIZE);
		valid_data = 0;
		data_nr = MAX_TABLE_SIZE;
	}

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
	CInodeInfo(void) {}
	void Init(FID id, CPageInfo* page)
	{
		__super::Init(NODE_INODE, id, page, nullptr);
		m_blks = 0;		// �ļ���С��block��λ
		m_ref_count = 0;
		m_delete_on_close = false;
	}
public:
	std::wstring m_fn;
	size_t m_blks=0;		// �ļ���С��block��λ
	UINT m_ref_count = 0;
	bool m_delete_on_close=false;
};

class CDirectInfo : public CNodeInfoBase
{
public:
	CDirectInfo(void) {}
	void Init(FID id, CPageInfo* page, CNodeInfoBase* parent)
	{
		__super::Init(NODE_INDEX, id, page, parent);
	}
public:
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

class CInodeManager_
{
public:
	CInodeManager_(void);// { m_nodes.push_back(nullptr); };
	~CInodeManager_(void);// {};
public:
	CNodeInfoBase* allocate_inode(CNodeInfoBase::NODE_TYPE type, CNodeInfoBase* parent, CPageInfo* page);
	CPageInfo* free_inode(FID nid);

	CNodeInfoBase* get_node(FID nid) { return m_nodes[nid]; }
	FID get_node_nr(void) const { return (FID)(m_node_nr); }

protected:
	typedef CNodeInfoBase* PNODE;
	PNODE m_nodes[MAX_NODE_NUM*2];
	size_t m_node_nr;

	// ����node buffer
	CInodeInfo m_inode_buffer[MAX_NODE_NUM];
	CInodeInfo* m_free_inodes[MAX_NODE_NUM];
	size_t m_inode_head;

	CDirectInfo m_dnode_buffer[MAX_NODE_NUM];
	CDirectInfo* m_free_dnodes[MAX_NODE_NUM];
	size_t m_dnode_head;
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

	CPageManager m_pages;
};
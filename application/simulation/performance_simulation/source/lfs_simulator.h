///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "segment_manager.h"
#include <list>

typedef DWORD BLK_ID;



class CPageInfo;

// ����ƣ�ʹ�õ�����2��index
class inode_info
{
public:
	//inode_info(FID file_index);
	inode_info(void) :m_fid(INVALID_BLK) {}
	~inode_info(void) {}
	void init_index(FID fid, inode_info* parent);
	void init_inode(FID fid);
	enum NODE_TYPE {
		NODE_FREE, NODE_INODE, NODE_INDEX,
	};

public:
	
	bool m_dirty = false;
	NODE_TYPE m_type;		// 0�� inode, 1: direct index, 2: indirect index;
	PHY_BLK m_phy_blk;		// inode �������ڵ�phy block��INVALID_BLK��ʾ��δ����physical block��
	FID m_fid;				// INVALID_BLK��ʾ��inodeδ��ʹ��
	inode_info* m_parent = nullptr;
	int m_parent_offset;	// �ڸ��ڵ��е�ƫ����
	std::wstring m_fn;

	CPageInfo* data_page;

	union
	{
		struct
		{
			//size_t m_size;		// �ļ���С���ֽڵ�λ
			size_t m_blks;		// �ļ���С��block��λ
			inode_info* m_direct[MAX_TABLE_SIZE];
			UINT m_ref_count;
			bool m_delete_on_close;
			DWORD m_host_write;
		} inode_blk;
		struct
		{
			size_t m_valid_blk;
			PHY_BLK m_index[INDEX_SIZE];
		} index_blk;
	};
};

// ��ʾһ����offset��physical block��·��
class index_path
{
public:
	int level = -1;
	int offset[MAX_INDEX_LEVEL];
	inode_info* node[MAX_INDEX_LEVEL];
};



class CInodeManager
{
public:
	CInodeManager(void);
	~CInodeManager(void);
public:
	inode_info* allocate_inode(inode_info::NODE_TYPE type, inode_info* parent);
	void free_inode(FID nid);
	inode_info* get_node(FID nid);

	FID allocate_index_block(void);
	FID get_node_nr(void) const { return (FID)(m_nodes.size()); }
	virtual void DumpBlockWAF(const std::wstring& fn) {};

public:
	//void init_node(inode_info& node, int type, FID file_index);
//	& get_index_block(FID nid);

protected:
	std::list<FID> m_free_list;
	//	std::vector<inode_info> m_inodes;
	//	std::vector<inode_info*> m_node_buffer;
	std::vector<inode_info*> m_nodes;
	size_t m_node_nr;
	size_t m_used_nr, m_free_nr;
};

// ָ��index table����Ϣ
class CIndexInfo
{
	LBLK_T	lblk;	// �ļ���logical block�����Ե�ַ
	PHY_BLK* table;
	size_t offset;
	int level;
};



class CSingleLogSimulator : public CLfsBase
{
public:
	CSingleLogSimulator(void) ;
	~CSingleLogSimulator(void);

public:
	virtual bool Initialzie(const boost::property_tree::wptree& config);
	virtual FID  FileCreate(const std::wstring &fn );
	virtual void SetFileSize(FID fid, size_t secs) {}
	virtual void FileWrite(FID fid, size_t offset, size_t secs);
	virtual void FileRead(std::vector<CPageInfoBase*>&blks, FID fid, size_t offset, size_t secs);
	virtual void FileTruncate(FID fid);
	virtual void FileDelete(FID fid);
	virtual void FileFlush(FID fid);
	virtual void FileClose(FID fid);
	virtual void FileOpen(FID fid, bool delete_on_close = false);

	virtual void DumpSegments(const std::wstring& fn, bool sanity_check);
	virtual void DumpSegmentBlocks(const std::wstring& fn);
//	virtual void DumpFileMap(FILE* out, FID file_index) { DumpFileMap_no_merge(out, file_index); }
	virtual void DumpFileMap(FILE* out, FID fid) { DumpFileMap_merge(out, fid); }

	void OffsetToIndex(index_path& path, LBLK_T offset, bool alloc);
	virtual void DumpAllFileMap(const std::wstring& fn);

	virtual void GetConfig(boost::property_tree::wptree& config, const std::wstring& config_name) {}
	virtual void DumpBlockWAF(const std::wstring& fn) {}


protected:
	// �����file mapʱ��������������block
	void DumpFileMap_merge(FILE* out, FID fid);

	// �����file mapʱ�����ϲ�����������block
	void DumpFileMap_no_merge(FILE* out, FID fid);

protected:
	// ����inode������data block���¶�
	BLK_TEMP get_temperature(inode_info& inode, LBLK_T offset) { return BT_HOT__DATA; }
	// ����ipath��ָʾ��index��λ�ã����ڸ��¡����index blockΪ�գ��򴴽�index block


//	PHY_BLK WriteInode(inode_info& inode);
	PHY_BLK WriteInode(LFS_BLOCK_INFO& ipage);
	void UpdateInode(inode_info* inode);

	// ���߼�blockת������index���е�λ�ã���0�㵽��ײ��·����
	//  alloc = trueʱ��������index block������ʱ�򴴽������򷵻�null
	// ��index_path�ƶ�����һ��blockλ�á����ڼ���offset��index��ת�������index block�����仯�������path, ��Ҫ���¼��㡣
	void NextOffset(index_path& path);
	void InitIndexPath(index_path& path, inode_info* inode);


protected:
	// ģ�����
	CLfsSegmentManager m_segments;
	CInodeManager m_inodes;

	//LBLK_T m_logic_blks;		// �߼���С
	//LBLK_T m_physical_blks;

#ifdef _SANITY_CHECK
	// ���ڼ��P2L�ı��
#endif
	// data for log
	DWORD m_last_host_write = 0, m_last_media_write = 0;
	size_t m_write_count = 0;
	SEG_T m_gc_th_low, m_gc_th_hi;
};


class ITester
{
public:
	virtual void Config(const boost::property_tree::wptree& pt, const std::wstring& root)=0;
	virtual int PrepareTest(void)=0;
	virtual int RunTest(void)=0;
	virtual int FinishTest(void)=0;
	virtual void ShowTestFailure(FILE* log) = 0;
};
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "segment_manager.h"
#include <list>

typedef DWORD BLK_ID;



class CPageInfo;

// 简化设计，使用单纯的2级index
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
	NODE_TYPE m_type;		// 0： inode, 1: direct index, 2: indirect index;
	PHY_BLK m_phy_blk;		// inode 本身所在的phy block，INVALID_BLK表示还未分配physical block。
	FID m_fid;				// INVALID_BLK表示此inode未被使用
	inode_info* m_parent = nullptr;
	int m_parent_offset;	// 在父节点中得偏移量
	std::wstring m_fn;

	CPageInfo* data_page;

	union
	{
		struct
		{
			//size_t m_size;		// 文件大小：字节单位
			size_t m_blks;		// 文件大小：block单位
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

// 表示一个从offset到physical block的路径
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

// 指向index table的信息
class CIndexInfo
{
	LBLK_T	lblk;	// 文件中logical block的线性地址
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
	// 在输出file map时，并连续的物理block
	void DumpFileMap_merge(FILE* out, FID fid);

	// 在输出file map时，不合并连续的物理block
	void DumpFileMap_no_merge(FILE* out, FID fid);

protected:
	// 根据inode，计算data block的温度
	BLK_TEMP get_temperature(inode_info& inode, LBLK_T offset) { return BT_HOT__DATA; }
	// 返回ipath中指示的index的位置，用于更新。如果index block为空，则创建index block


//	PHY_BLK WriteInode(inode_info& inode);
	PHY_BLK WriteInode(LFS_BLOCK_INFO& ipage);
	void UpdateInode(inode_info* inode);

	// 将逻辑block转换成在index层中的位置（从0层到最底层的路径）
	//  alloc = true时，当遇到index block不存在时则创建，否则返回null
	// 将index_path移动到下一个block位置。用于加速offset到index的转化。如果index block发生变化，则清除path, 需要重新计算。
	void NextOffset(index_path& path);
	void InitIndexPath(index_path& path, inode_info* inode);


protected:
	// 模拟磁盘
	CLfsSegmentManager m_segments;
	CInodeManager m_inodes;

	//LBLK_T m_logic_blks;		// 逻辑大小
	//LBLK_T m_physical_blks;

#ifdef _SANITY_CHECK
	// 用于检查P2L的表格
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
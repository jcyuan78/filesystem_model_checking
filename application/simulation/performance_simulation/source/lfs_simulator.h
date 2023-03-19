///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
#pragma once

#include <boost/property_tree/ptree.hpp>
#include "segment_manager.h"
#include <list>
#include <vector>

typedef DWORD BLK_ID;

#define MAX_TABLE_SIZE			1024
#define START_OF_DIRECTORY_NODE	64
#define FID_ROOT	0
#define MAX_INDEX_LEVEL		3
#define LEVEL1_OFFSET			64
#define LEVEL2_OFFSET			96
#define INDEX_SIZE				1024
#define INDEX_SIZE_BIT			10




// 简化设计，使用单纯的2级index

class inode_info
{
public:
	//inode_info(FID fid);
	inode_info(void) {}
	~inode_info(void) {}
	void init_index(FID fid, inode_info* parent);
	void init_inode(FID fid);
	enum NODE_TYPE {
		NODE_FREE, NODE_INODE, NODE_INDEX,
	};

public:
	
	bool m_dirty = false;
	NODE_TYPE m_type;		// 0： inode, 1: direct index, 2: indirect index;
	PHY_BLK m_phy_blk;	// inode 本身所在的phy block，0xFF表示文件无效。
	FID m_fid;
	inode_info* m_parent = nullptr;
	int m_parent_offset;	// 在父节点中得偏移量
	std::wstring m_fn;

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
	//FID index_node[MAX_INDEX_LEVEL];
	inode_info* node[MAX_INDEX_LEVEL];
};

class CLfsInterface
{
public:
	virtual ~CLfsInterface(void) {}
public:
	virtual bool Initialzie(const boost::property_tree::wptree & config ) = 0;
	virtual FID  FileCreate(const std::wstring & fn) = 0;
	virtual void SetFileSize(FID fid, size_t secs) = 0;
	virtual void FileWrite(FID fid, size_t offset, size_t secs) = 0;
	virtual void FileRead(FID fid, size_t offset, size_t secs) = 0;
	virtual void FileTruncate(FID fid) = 0;
	virtual void FileDelete(FID fid) = 0;
	virtual void FileFlush(FID fid) = 0;

	// 文件能够支持的最大长度（block单位）
	virtual DWORD MaxFileSize(void) const = 0;
	virtual void FileOpen(FID fid, bool delete_on_close = false) =0;
	virtual void FileClose(FID fid) =0;
	virtual void GetHealthInfo(FsHealthInfo& info) const = 0;
	virtual void DumpSegments(const std::wstring& fn, bool sanity_check) = 0;
	virtual void DumpSegmentBlocks(const std::wstring& fn) = 0;
//	virtual void DumpSegmentSumm(const std::wstring& fn, bool sanity_check) =0;
	virtual void DumpFileMap(FILE* out, FID fid) =0;
	virtual void DumpAllFileMap(const std::wstring &fn) =0;

};

class CLfsBase : public CLfsInterface
{
public:


};

class CInodeManager
{
public:
	CInodeManager(void);
	~CInodeManager(void);
public:
	inode_info * allocate_inode(inode_info::NODE_TYPE type, inode_info * parent);
	void free_inode(FID nid);
	inode_info * get_node(FID nid);

	FID allocate_index_block(void);
	FID get_node_nr(void) const { return (FID)(m_nodes.size()); }

public:
	//void init_node(inode_info& node, int type, FID fid);
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
	virtual void FileRead(FID fid, size_t offset, size_t secs);
	virtual void FileTruncate(FID fid);
	virtual void FileDelete(FID fid);
	virtual void FileFlush(FID fid);

	virtual DWORD MaxFileSize(void) const;
	virtual void FileOpen(FID fid, bool delete_on_close = false);
	virtual void FileClose(FID fid);
	virtual void GetHealthInfo(FsHealthInfo& info) const
	{
		memcpy_s(&info, sizeof(FsHealthInfo), &m_health_info, sizeof(FsHealthInfo));
	}
	virtual void DumpSegments(const std::wstring& fn, bool sanity_check);
	virtual void DumpSegmentBlocks(const std::wstring& fn);
	virtual void DumpAllFileMap(const std::wstring& fn);
//	virtual void DumpFileMap(FILE* out, FID fid) { DumpFileMap_no_merge(out, fid); }
	virtual void DumpFileMap(FILE* out, FID fid) { DumpFileMap_merge(out, fid); }

protected:
	// 在输出file map时，并连续的物理block
	void DumpFileMap_merge(FILE* out, FID fid);

	// 在输出file map时，不合并连续的物理block
	void DumpFileMap_no_merge(FILE* out, FID fid);

protected:
	// 根据inode，计算data block的温度
	BLK_TEMP get_temperature(inode_info& inode, LBLK_T offset) { return BT_HOT__DATA; }
	// 将逻辑block转换成在index层中的位置（从0层到最底层的路径）
	//  alloc = true时，当遇到index block不存在时则创建，否则返回null
	void OffsetToIndex(index_path& path, LBLK_T offset, bool alloc);
	// 将index_path移动到下一个block位置。用于加速offset到index的转化。如果index block发生变化，则清除path, 需要重新计算。
	void NextOffset(index_path& path);
	// 返回ipath中指示的index的位置，用于更新。如果index block为空，则创建index block
	//PHY_BLK* GetPhyBlockForWrite(index_path& path);
	// 返回direct node以及ipath所指向的offset
	//inode_info* GetDirectNodeForWrite(int &offset, index_path& path);
	//inode_info* GetDirectNodeForRead(int &offset, index_path& path);
	PHY_BLK WriteInode(inode_info& inode);
	void InitIndexPath(index_path& path, inode_info* inode);

protected:
	CInodeManager m_inodes;
	// 模拟磁盘
	CLfsSegmentManager m_segments;

	//LBLK_T m_logic_blks;		// 逻辑大小
	//LBLK_T m_physical_blks;
	LBLK_T m_level_to_offset[MAX_INDEX_LEVEL];

	FsHealthInfo m_health_info;
#ifdef _SANITY_CHECK
	// 用于检查P2L的表格
#endif
};
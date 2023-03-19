///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
#pragma once

#include <boost/property_tree/ptree.hpp>
#include "segment_manager.h"
#include <list>
#include <vector>

typedef DWORD BLK_ID;

#define MAX_TABLE_SIZE			128
#define START_OF_DIRECTORY_NODE	64
#define FID_ROOT	0
#define MAX_INDEX_LEVEL		3
#define LEVEL1_OFFSET			64
#define LEVEL2_OFFSET			96
#define INDEX_SIZE				128
#define INDEX_SIZE_BIT			7



class inode_info
{
public:
	bool m_dirty = false;
	int m_type = 0;		// 0： inode, 1: direct index, 2: indirect index;
	PHY_BLK m_phy_blk;	// inode 本身所在的phy block，0xFF表示文件无效。
	FID m_fid;
	inode_info* m_parent = nullptr;

	union
	{
		struct
		{
			size_t m_size;		// 文件大小：字节单位
			size_t m_blks;		// 文件大小：block单位
			PHY_BLK m_index[MAX_TABLE_SIZE];	// 如果时direct/indirect node，则指向node id.
			UINT m_ref_count;
			bool m_delete_on_close;
		} inode_blk;
		struct
		{
//			FID m_parant;
			PHY_BLK m_index[INDEX_SIZE];
		} index_blk;
	};
};

//class index_blk_info
//{
//public:
//};

// 表示一个从offset到physical block的路径
class index_path
{
public:
	int level = -1;
	int offset[MAX_INDEX_LEVEL];
	FID index_node[MAX_INDEX_LEVEL];
	inode_info* node[MAX_INDEX_LEVEL];
};

class CLfsInterface
{
public:
	virtual ~CLfsInterface(void) {}
public:
	virtual bool Initialzie(const boost::property_tree::wptree & config ) = 0;
	virtual FID  FileCreate(void) = 0;
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
};

class CLfsBase : public CLfsInterface
{
public:


};

class CInodeManager
{
public:
	CInodeManager(void);
	~CInodeManager(void) {};
public:
	FID allocate_inode(void);
	void free_inode(FID nid);
	inode_info& get_node(FID nid);

	FID allocate_index_block(void);
//	& get_index_block(FID nid);

protected:
	std::list<FID> m_free_list;
	std::vector<inode_info> m_inodes;
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
	~CSingleLogSimulator(void) {};

public:
	virtual bool Initialzie(const boost::property_tree::wptree& config);
	virtual FID  FileCreate(void);
	virtual void SetFileSize(FID fid, size_t secs) {}
	virtual void FileWrite(FID fid, size_t offset, size_t secs);
	virtual void FileRead(FID fid, size_t offset, size_t secs) {}
	virtual void FileTruncate(FID fid);
	virtual void FileDelete(FID fid);
	virtual void FileFlush(FID fid);

	virtual DWORD MaxFileSize(void) const;
	virtual void FileOpen(FID fid, bool delete_on_close = false);
	virtual void FileClose(FID fid);

protected:
	// 根据inode，计算data block的温度
	BLK_TEMP get_temperature(inode_info& inode, LBLK_T offset) { return BT_HOT__DATA; }
	void OffsetToIndex(index_path& path, LBLK_T offset);
	// 将index_path移动到下一个block位置。用于加速offset到index的转化。如果index block发生变化，则清除path, 需要重新计算。
	void NextOffset(index_path& path);
	PHY_BLK* GetPhyBlock(index_path& path);
	PHY_BLK WriteInode(inode_info& inode);

protected:
	CInodeManager m_inodes;
	// 模拟磁盘
	CLfsSegmentManager m_segments;

	LBLK_T m_logic_blks;		// 逻辑大小
	LBLK_T m_physical_blks;
	LBLK_T m_level_to_offset[MAX_INDEX_LEVEL];
};
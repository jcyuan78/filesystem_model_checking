///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// node.h ：定义各种node 类型的数据结构
#pragma once

#include "config.h"
#include "fs_simulator.h"
#include "f2fs_segment.h"

// == nid and index node  ==
enum F2FS_FILE_TYPE {
	F2FS_FILE_REG, F2FS_FILE_DIR, F2FS_FILE_UNKNOWN,
};
// 用于记录一个实际的inode
struct INODE
{
	UINT blk_num;		// 文件占用的有效块数。可能小于文件长度，排除空洞。
	UINT file_size;		// 文件长度，字节单位。文件实际块数通过file_size计算获得。
	F2FS_FILE_TYPE file_type;
	UINT ref_count;
	UINT nlink;			// 文件连接数
	_NID index[INDEX_TABLE_SIZE];		// 磁盘数据，
	//		index[]		index_buf[]
	//		null		null			：在文件中，这个index block不存在。不需要回写
	//		null		valid			：这个index block新建，还未存盘。需要回写
	//		valid		null			：这个index block未被读取。不需要回写
	//		valid		valid			：这个index block已经被读取。是否修改，是否回写要看page->dirty
};

struct INDEX_NODE
{
	UINT valid_data;
	PHY_BLK index[INDEX_SIZE];
};

// 包含 nid 和 index node
struct NODE_INFO
{
public:
	_NID m_nid;				// 表示这个node的id
	_NID m_ino;				// 表示这个node的所在的inode
	PAGE_INDEX page_id;		// 这个node对应的page的id
	union {
		INODE inode;
		INDEX_NODE index;
	};
};

// == dentry ==
// dentry的数据结构，以及块在dentry中测存储

struct DENTRY
{
	_NID ino;
	WORD hash;
	WORD name_len;
	BYTE file_type;
};

struct DENTRY_BLOCK
{
	DWORD bitmap;
	char filenames[DENTRY_PER_BLOCK][FN_SLOT_LEN];
	DENTRY dentries[DENTRY_PER_BLOCK];
};

// == metadata blocks ==
struct NAT_BLOCK
{
	PHY_BLK nat[NAT_ENTRY_PER_BLK];
};


#define NODE_NEXT_FREE node.m_nid

struct BLOCK_DATA
{
	enum BLOCK_TYPE { 
		BLOCK_FREE=-1, BLOCK_INODE, BLOCK_INDEX, BLOCK_DENTRY, BLOCK_SIT, BLOCK_NAT, 
		BLOCK_FILE_DATA, BLOCK_CKPT_HEADER, BLOCK_CKPT_NAT_JOURNAL, BLOCK_CKPT_SIT_JOURNAL } m_type;
	union
	{
		NODE_INFO		node;
		DENTRY_BLOCK	dentry;
		SIT_BLOCK		sit;
		NAT_BLOCK		nat;
		SUMMARY_BLOCK	ssa;
		FILE_DATA		file;
		CKPT_HEAD		ckpt_header;
		CKPT_NAT_JOURNAL	ckpt_nat_nournal;
		CKPT_SIT_JOURNAL	ckpt_sit_nournal;
	};
};


// 管理Buffer，模拟文件系统的页缓存。
template <typename BLOCK_TYPE>
class _CBufferManager
{
public:
	typedef UINT _INDEX;
public:
	_CBufferManager(void) {}
	// 返回 root的 NODE
	BLOCK_TYPE* Init(void) {
		// 构建free list
		for (UINT ii = 1; ii < BLOCK_BUF_SIZE; ++ii)
		{
			m_data_buffer[ii].NODE_NEXT_FREE = ii + 1;
			m_data_buffer[ii].m_type = BLOCK_DATA::BLOCK_FREE;
		}
		m_data_buffer[BLOCK_BUF_SIZE - 1].NODE_NEXT_FREE = INVALID_BLK;
		m_data_buffer[0].NODE_NEXT_FREE = 0;	// 保留第0号node
		m_free_ptr = 1;
		m_used_nr = 1;
		// FID = 0表示root
		return m_data_buffer + 0;
	}
	_INDEX get_block(void)
	{
		if (m_used_nr >= BLOCK_BUF_SIZE) THROW_ERROR(ERR_APP, L"no free block buffer");
		_INDEX index = m_free_ptr;
		m_free_ptr = m_data_buffer[index].NODE_NEXT_FREE;
		m_used_nr++;
		return index;
	}
	void put_block(_INDEX index)
	{
		m_data_buffer[index].m_type = BLOCK_DATA::BLOCK_FREE;
		m_data_buffer[index].NODE_NEXT_FREE = (_NID)m_free_ptr;
		m_free_ptr = index;
		m_used_nr--;
	}
	BLOCK_TYPE& get_block_data(_INDEX blk)
	{
		if (blk >= BLOCK_BUF_SIZE) THROW_ERROR(ERR_APP, L"invalid blk number: %d", blk);
		return m_data_buffer[blk];
	}
public:
	BLOCK_TYPE m_data_buffer[BLOCK_BUF_SIZE];
protected:
	friend class CPageAllocator;
	_INDEX m_free_ptr;
	UINT m_used_nr;
};

typedef _CBufferManager<BLOCK_DATA>	CBufferManager;

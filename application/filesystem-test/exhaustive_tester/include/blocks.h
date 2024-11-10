///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// node.h ���������node ���͵����ݽṹ
#pragma once

#include "config.h"
#include "fs_simulator.h"
#include "f2fs_segment.h"

// == nid and index node  ==
enum F2FS_FILE_TYPE {
	F2FS_FILE_REG, F2FS_FILE_DIR, F2FS_FILE_UNKNOWN,
};
// ���ڼ�¼һ��ʵ�ʵ�inode
struct INODE
{
	UINT blk_num;		// �ļ�ռ�õ���Ч����������С���ļ����ȣ��ų��ն���
	UINT file_size;		// �ļ����ȣ��ֽڵ�λ���ļ�ʵ�ʿ���ͨ��file_size�����á�
	F2FS_FILE_TYPE file_type;
	UINT ref_count;
	UINT nlink;			// �ļ�������
	NID index[INDEX_TABLE_SIZE];		// �������ݣ�
	//		index[]		index_buf[]
	//		null		null			�����ļ��У����index block�����ڡ�����Ҫ��д
	//		null		valid			�����index block�½�����δ���̡���Ҫ��д
	//		valid		null			�����index blockδ����ȡ������Ҫ��д
	//		valid		valid			�����index block�Ѿ�����ȡ���Ƿ��޸ģ��Ƿ��дҪ��page->dirty
};

struct INDEX_NODE
{
	UINT valid_data;
	PHY_BLK index[INDEX_SIZE];
};

// ���� nid �� index node
struct NODE_INFO
{
public:
	NID m_nid;				// ��ʾ���node��id
	NID m_ino;				// ��ʾ���node�����ڵ�inode
//	UINT valid_data;		// �ӽṹ�У��ж�����Ч��block
	PAGE_INDEX page_id;		// ���node��Ӧ��page��id
	union {
		INODE inode;
		INDEX_NODE index;
	};
};

// == dentry ==
// dentry�����ݽṹ���Լ�����dentry�в�洢

struct DENTRY
{
	NID ino;
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
	enum BLOCK_TYPE { BLOCK_FREE, BLOCK_INODE, BLOCK_INDEX, BLOCK_DENTRY, BLOCK_SIT, BLOCK_NAT, BLOCK_FILE_DATA } m_type;
	union
	{
		NODE_INFO		node;
		DENTRY_BLOCK	dentry;
		SIT_BLOCK		sit;
		NAT_BLOCK		nat;
		SUMMARY_BLOCK	ssa;
		FILE_DATA		file;
	};
};


// ����Buffer��ģ���ļ�ϵͳ��ҳ���档
template <typename BLOCK_TYPE>
class _CBufferManager
{
public:
	typedef UINT _INDEX;
public:
	_CBufferManager(void) {}
	// ���� root�� NODE
	BLOCK_TYPE* Init(void) {
		// ����free list
		for (UINT ii = 1; ii < BLOCK_BUF_SIZE; ++ii)
		{
			m_data_buffer[ii].NODE_NEXT_FREE = ii + 1;
			m_data_buffer[ii].m_type = BLOCK_DATA::BLOCK_FREE;
		}
		m_data_buffer[BLOCK_BUF_SIZE - 1].NODE_NEXT_FREE = INVALID_BLK;
		m_data_buffer[0].NODE_NEXT_FREE = 0;	// ������0��node
		m_free_ptr = 1;
		m_used_nr = 1;
		// FID = 0��ʾroot
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
		m_data_buffer[index].NODE_NEXT_FREE = (NID)m_free_ptr;
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

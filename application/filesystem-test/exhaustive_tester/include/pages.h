///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// pages.h: ����page��block data buffer��������ģ��Linux�Ļ������
#pragma once

#include "config.h"
#include "fs_simulator.h"
#include "blocks.h"

#define PAGE_NEXT_FREE	data_index

struct BLOCK_DATA;

class CPageInfo
{
public:
	void Init();

public:
	PHY_BLK phy_blk = INVALID_BLK;	// page��������λ��
	// ���page���¶ȣ���page��д��SSDʱ���¡�����¶Ȳ���ʵ�ʷ��䵽�¶ȣ������㷨�¶���ͬ��������ͳ�ơ�
	BLK_TEMP ttemp;
	// nid��offset��һ���ʾpage���߼���ַ���ڸ��ڵ��е�λ�ã�����Ҫ����GCʱ���¸��ڵ�����ݡ������������ڵ��� WriteBlockToSeg() ǰ����
	// ��pageָ��node block��inode, index��ʱ���丸�ڵ�ΪNAT�� nid��ʾnode id��offset =INVALID_BLK��ʾָ��node
	// ��pageָ��data block��data, dentry)�ȡ��丸�ڵ�Ϊinode/index��nid��ʾ���ڵ��nid��offset��ʾ�ڸ��ڵ��е�λ�á�
	NID	nid;						// ���page���ڵ�node��nid������node block������Ҫ���¸��ڵ㣬����NAT����
	LBLK_T offset = INVALID_BLK;	// ���page��һ��node��offset=INVALID_BLK
	bool dirty = false;
	PAGE_INDEX page_id;
public:
	// ��������ͳ��
	UINT host_write = 0;
	friend class CPageAllocator;
protected:
	// ����(����inode ���� direct node)
	UINT data_index;	// ָ�����ݻ����������
};

typedef CPageInfo* PPAGE;

class CPageAllocator
{
public:
//	typedef UINT INDEX;
	CPageAllocator(CF2fsSimulator* fs);
	~CPageAllocator(void);

	void CopyFrom(const CPageAllocator* src);
	void Reset(void);

public:
	void Init(size_t page_nr);
	PAGE_INDEX allocate(void);
	// ����һ��page��data:true��ͬʱ�������ݣ�false����Ҫ����
	CPageInfo* allocate(bool data);
	void free(PAGE_INDEX index);
	void free(CPageInfo* page) {
		free(page->page_id);
	}

	BLOCK_DATA * get_data(CPageInfo* page);

	inline CPageInfo* page(PAGE_INDEX index) {
		return (index >= m_page_nr) ? nullptr : (m_pages + index);
	}

// ��ȡ�����Ϣ
public:
	UINT total_page_nr(void) { return MAX_PAGE_NUM; }
	UINT total_data_nr(void) { return BLOCK_BUF_SIZE; }
	UINT free_page_nr(void) { return MAX_PAGE_NUM - m_used_nr; }
	UINT free_data_nr(void) { return BLOCK_BUF_SIZE - m_buffer.m_used_nr; }

protected:
	CPageInfo m_pages[MAX_PAGE_NUM];
	PAGE_INDEX m_free_ptr, m_used_nr;
	UINT m_page_nr;
protected:
	CBufferManager m_buffer;
};
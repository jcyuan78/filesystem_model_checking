///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "fs_comm.h"
#include "vector"

class CInodeInfo;
class CNodeInfoBase;

class CPageInfo : public CPageInfoBase
{
public:
	void Init();

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
	enum PAGE_TYPE { PAGE_DATA, PAGE_NODE } type;
};

class CPageManager
{
public:
	CPageManager(void);
	~CPageManager(void);
public:
	void Init(size_t page_nr);
	CPageInfo* get_page(void);
	void put_page(CPageInfo*);

protected:
	std::vector<CPageInfo*> m_buffers;
	typedef CPageInfo* PPAGE;
	PPAGE* m_free_pages;
	size_t m_page_nr;
	size_t m_head_free;
};
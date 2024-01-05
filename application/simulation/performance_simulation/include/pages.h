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
	PHY_BLK phy_blk = INVALID_BLK;	// page所在物理位置
	// 标记page的温度，当page被写入SSD时更新。这个温度不是实际分配到温度，所有算法下都相同。仅用于统计。
	BLK_TEMP ttemp;
	//在文件中的位置
	CInodeInfo* inode = nullptr;
	LBLK_T offset = INVALID_BLK;
	// 数据(对于inode 或者 direct node)
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
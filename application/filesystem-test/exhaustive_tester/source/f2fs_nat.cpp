///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
#include "pch.h"
#include "../include/f2fs_simulator.h"
//#include <boost/unordered_set.hpp>

LOCAL_LOGGER_ENABLE(L"simulator.f2fs.nat", LOGGER_LEVEL_DEBUGINFO);

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
CNodeAddressTable::CNodeAddressTable(CF2fsSimulator* fs)
	: m_pages(&fs->m_pages), m_storage(&fs->m_storage)
{
}
NID CNodeAddressTable::Init(PHY_BLK root)
{
	memset(nat, 0xFF, sizeof(PHY_BLK) * NODE_NR);
	memset(node_catch, 0xFF, sizeof(PAGE_INDEX) * NODE_NR);
	memset(dirty, 0xFF, sizeof(DWORD) * NAT_BLK_NR);
	build_free();
	Sync();			// 将NAT写入磁盘
	return 0;
}

void CNodeAddressTable::CopyFrom(const CNodeAddressTable* src)
{
	memcpy_s(nat, sizeof(PHY_BLK) * NODE_NR, src->nat, sizeof(PHY_BLK) * NODE_NR);
	memcpy_s(node_catch, sizeof(PAGE_INDEX) * NODE_NR, src->node_catch, sizeof(PAGE_INDEX) * NODE_NR);
	memcpy_s(dirty, sizeof(DWORD) * NAT_BLK_NR, src->dirty, sizeof(DWORD) * NAT_BLK_NR);
	next_scan = src->next_scan;
	free_nr = src->free_nr;
}

bool CNodeAddressTable::Load(void)
{
	UINT lba = NAT_START_BLK;
	for (NID nid = 0; nid < NODE_NR; )
	{
		CPageInfo* page = m_pages->allocate(true);
		m_storage->BlockRead(lba, page);
		BLOCK_DATA* data = m_pages->get_data(page);
		memcpy_s(nat + nid, sizeof(PHY_BLK) * NAT_ENTRY_PER_BLK, &data->nat, sizeof(PHY_BLK) * NAT_ENTRY_PER_BLK);
		nid += NAT_ENTRY_PER_BLK;
		lba++;
	}
	build_free();
	memset(dirty, 0, sizeof(DWORD) * NAT_BLK_NR);
	return true;
}

void CNodeAddressTable::Sync(void)
{
//	UINT lba = NAT_START_BLK;
	UINT nat_blk = 0;
	for (NID nid = 0; nid < NODE_NR; )
	{
		if (dirty[nat_blk] != 0)
		{
			CPageInfo* page = m_pages->allocate(true);
			BLOCK_DATA* data = m_pages->get_data(page);
			memcpy_s(&data->nat, sizeof(PHY_BLK) * NAT_ENTRY_PER_BLK, nat + nid, sizeof(PHY_BLK) * NAT_ENTRY_PER_BLK);
			LOG_DEBUG(L"Save NAT table start nid=%d", nid);
			m_storage->BlockWrite(nat_blk + NAT_START_BLK, page);
			dirty[nat_blk] = 0;
		}
		nid += NAT_ENTRY_PER_BLK;
		//lba++;
		nat_blk++;
	}
}

void CNodeAddressTable::Reset(void)
{
	memset(nat, 0xFF, sizeof(PHY_BLK) * NODE_NR);
	memset(node_catch, 0xFF, sizeof(PAGE_INDEX) * NODE_NR);
	memset(dirty, 0, sizeof(DWORD) * NAT_BLK_NR);
	next_scan = 0;
	free_nr = 0;
}

void CNodeAddressTable::build_free(void)
{
	for (next_scan = 0; next_scan < NODE_NR; next_scan++)
	{
		if (nat[next_scan] == INVALID_BLK) break;
	}
	if (next_scan >= NODE_NR) THROW_ERROR(ERR_APP, L"no valid node");
	for (int ii = 0; ii < NODE_NR; ++ii)
	{
		if (nat[ii] == INVALID_BLK) free_nr++;
	}
}

NID CNodeAddressTable::get_node(void)
{
	NID cur = next_scan;
	if (free_nr == 0) THROW_ERROR(ERR_APP, L"remained node=0");
	while (1)
	{
		if (nat[next_scan] == INVALID_BLK)
		{
			free_nr--;
			//			LOG_DEBUG(L"allocate nid: nid=%d, remain=%d", next_scan, free_nr);
			nat[next_scan] = NID_IN_USE;	// 避免分配以后，node没有写入磁盘之前，被再次分配，用NID_IN_USE标志已使用。
			set_dirty(next_scan);
			// TODO 需要确认f2fs真实系统中如何实现。
			return next_scan++;
		}
		next_scan++;
		if (next_scan >= NODE_NR)
			next_scan = 0;
		if (next_scan == cur)
		{
			THROW_ERROR(ERR_APP, L"node run out");
			LOG_ERROR(L"no valid node");
			return INVALID_BLK;
		}
	}
}

void CNodeAddressTable::put_node(NID nid)
{
	free_nr++;
	//	LOG_DEBUG(L"free nid: nid=%d, remain=%d", nid, free_nr);
	if (nid >= NODE_NR) THROW_ERROR(ERR_USER, L"wrong node id:%d", nid);
	nat[nid] = INVALID_BLK;
	set_dirty(nid);
}

PHY_BLK CNodeAddressTable::get_phy_blk(NID nid)
{
	if (nid >= NODE_NR) THROW_ERROR(ERR_APP, L"invalid node id %d", nid);
	return nat[nid];
}

void CNodeAddressTable::set_phy_blk(NID nid, PHY_BLK phy_blk)
{
	if (nid >= NODE_NR) THROW_ERROR(ERR_APP, L"invalid node id %d", nid);
	nat[nid] = phy_blk;
	set_dirty(nid);

}

void CNodeAddressTable::set_dirty(NID nid)
{
	UINT blk = nid / NAT_ENTRY_PER_BLK;
	UINT offset = nid % NAT_ENTRY_PER_BLK;
	DWORD mask = (1 << offset);
	dirty[blk] |= mask;
}


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
	if (!build_free()) {
		THROW_FS_ERROR(ERR_NODE_FULL, L"no valid node during initializing");
	}
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

bool CNodeAddressTable::Load(CKPT_BLOCK& checkpoint)
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
	// 从checkpoint的journal中恢复
	if (checkpoint.nat_journal_nr > JOURNAL_NR) THROW_FS_ERROR(ERR_INVALID_CHECKPOINT, L"NAT journal size is too large: %d", checkpoint.nat_journal_nr);
	for (UINT ii = 0; ii < checkpoint.nat_journal_nr; ++ii)
	{
		NID nid = checkpoint.nat_journals[ii].nid;
		nat[nid] = checkpoint.nat_journals[ii].phy_blk;
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

int CNodeAddressTable::build_free(void)
{
	next_scan = INVALID_BLK;
	free_nr = 0;
	for (int ii = 0; ii < NODE_NR; ++ii)
	{
		if (nat[ii] == INVALID_BLK) {
			free_nr++;
			if (next_scan == INVALID_BLK) next_scan = ii;
		}
	}
//	if (free_nr == 0)
//	{
//		next_scan = INVALID_BLK;
//		return 0;
//	}
//
//	for (next_scan = 0; next_scan < NODE_NR; next_scan++)
//	{
//		if (nat[next_scan] == INVALID_BLK) break;
//	}
//	if (next_scan >= NODE_NR)
//	{
//		THROW_ERROR(ERR_APP, L"no valid node");
////		LOG_ERROR(L"no valid node");
////		free_nr = 0;
////		return 0;
//	}
	return free_nr;
}

NID CNodeAddressTable::get_node(void)
{
	NID cur = next_scan;
	if (free_nr == 0) {
//		THROW_ERROR(ERR_APP, L"remained node=0");
		LOG_ERROR(L"no valid node, free_nr==0");
		return INVALID_BLK;
	}
	if (next_scan >= NODE_NR)		next_scan = 0;
	while (1)
	{
		if (nat[next_scan] == INVALID_BLK)
		{
			free_nr--;
			//			LOG_DEBUG(L"allocate_index nid: nid=%d, remain=%d", next_scan, free_nr);
			nat[next_scan] = NID_IN_USE;	// 避免分配以后，node没有写入磁盘之前，被再次分配，用NID_IN_USE标志已使用。
			set_dirty(next_scan);
			// TODO 需要确认f2fs真实系统中如何实现。
			return next_scan++;
		}
		next_scan++;
		if (next_scan >= NODE_NR)		next_scan = 0;
		if (next_scan == cur)
		{
//			THROW_ERROR(ERR_APP, L"node run out");
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
	if (nid >= NODE_NR) THROW_FS_ERROR(ERR_INVALID_NID, L"invalid node id %d", nid);
	return nat[nid];
}

void CNodeAddressTable::set_phy_blk(NID nid, PHY_BLK phy_blk)
{
	if (nid >= NODE_NR) THROW_ERROR(ERR_APP, L"invalid node id %d", nid);
	nat[nid] = phy_blk;
	set_dirty(nid);

}

void CNodeAddressTable::f2fs_flush_nat_entries(CKPT_BLOCK& checkpoint)
{
	// 将journal中的nat写入磁盘
	CPageInfo* pages[NAT_BLK_NR];
	memset(pages, 0, sizeof(pages));
	for (UINT ii = 0; ii < checkpoint.nat_journal_nr; ++ii)
	{
		// 计算nid所在的block
		NID nid = checkpoint.nat_journals[ii].nid;
		UINT nat_blk_id = nid / NAT_ENTRY_PER_BLK;
		UINT offset = nid % NAT_ENTRY_PER_BLK;
		// cache nat page
		NAT_BLOCK* nat_blk = nullptr;
		if (pages[nat_blk_id] == nullptr)
		{	// 读取page
			pages[nat_blk_id] = m_pages->allocate(true);
			m_storage->BlockRead(nat_blk_id + NAT_START_BLK, pages[nat_blk_id]);
		}
		// 更新 nat page
		nat_blk = &m_pages->get_data(pages[nat_blk_id])->nat;
		nat_blk->nat[offset] = checkpoint.nat_journals[ii].phy_blk;
	}
	// 将更新的page写入nat block
	for (UINT blk = 0; blk < NAT_BLK_NR; blk++)
	{
		if (!pages[blk]) continue;
//		LOG_TRACK(L"write_nat", L"write nat[%d] to lba %d", blk * NAT_ENTRY_PER_BLK, blk + NAT_START_BLK);
		m_storage->BlockWrite(blk + NAT_START_BLK, pages[blk]);
		m_pages->free(pages[blk]);
		pages[blk] = nullptr;
	}
	checkpoint.nat_journal_nr = 0;
	// 将nat写入journal中。
	for (NID nid = 0; nid < NODE_NR; nid++) 
	{
		if (is_dirty(nid))
		{	// nid添加到journal中，检查nid是否已经在journal中
//			JCASSERT(checkpoint.nat_journal_nr < JOURNAL_NR);
			if (checkpoint.sit_journal_nr >= JOURNAL_NR) THROW_FS_ERROR(ERR_JOURNAL_OVERFLOW, L"too many sit journal");
			int index = checkpoint.nat_journal_nr;
			checkpoint.nat_journals[index].nid = nid;
			checkpoint.nat_journals[index].phy_blk = nat[nid];
			clear_dirty(nid);
			checkpoint.nat_journal_nr++;
		}
	}
}

//void CNodeAddressTable::f2fs_flush_nat_entries(CKPT_BLOCK& checkpoint)
//{
//	// 将journal中的nat写入磁盘
//	CPageInfo* pages[NAT_BLK_NR];
//	memset(pages, 0, sizeof(pages));
//	for (int ii = 0; ii < checkpoint.nat_journal_nr; ++ii)
//	{
//		// 计算nid所在的block
//		NID nid = checkpoint.nat_journals[ii].nid;
//		UINT nat_blk_id = nid / NAT_ENTRY_PER_BLK;
//		UINT offset = nid % NAT_ENTRY_PER_BLK;
//		// cache nat page
//		NAT_BLOCK* nat_blk = nullptr;
//		if (pages[nat_blk_id] == nullptr)
//		{	// 读取page
//			pages[nat_blk_id] = m_pages->allocate(true);
//			m_storage->BlockRead(nat_blk_id + NAT_START_BLK, pages[nat_blk_id]);
//			nat_blk = &m_pages->get_data(pages[nat_blk_id])->nat;
//			NID start_nid = nat_blk_id * NAT_ENTRY_PER_BLK;
//			memcpy_s(nat_blk, sizeof(NAT_BLOCK), nat + start_nid, sizeof(NAT_BLOCK));
//		}
//		// 更新 nat page
//		nat_blk = &m_pages->get_data(pages[nat_blk_id])->nat;
//		if (!is_dirty(nid))
//		{	// 如果is_dirty，说明这个nid在journal以后被更新过。
//			nat_blk->nat[offset] = checkpoint.nat_journals[ii].phy_blk;
//		}
//	}
//	// 将更新的page写入nat block
//	for (UINT blk = 0; blk < NAT_BLK_NR; blk++)
//	{
//		if (!pages[blk]) continue;
//		m_storage->BlockWrite(blk + NAT_START_BLK, pages[blk]);
//		m_pages->free(pages[blk]);
//		pages[blk] = nullptr;
//		dirty[blk] = 0;
//	}
//	checkpoint.nat_journal_nr = 0;
//	// 将nat写入journal中。
//	for (NID nid = 0; nid < NODE_NR; nid++)
//	{
//		if (is_dirty(nid))
//		{	// nid添加到journal中，检查nid是否已经在journal中
//			int index = 0;
//			for (index = 0; index < checkpoint.nat_journal_nr; ++index)
//			{
//				if (checkpoint.nat_journals[index].nid == nid) break;
//			}
//			if (checkpoint.nat_journal_nr <= index) checkpoint.nat_journal_nr = index + 1;
//			checkpoint.nat_journals[index].nid = nid;
//			checkpoint.nat_journals[index].phy_blk = nat[nid];
//			clear_dirty(nid);
//		}
//	}
//}


void CNodeAddressTable::set_dirty(NID nid)
{
	UINT blk = nid / NAT_ENTRY_PER_BLK;
	UINT offset = nid % NAT_ENTRY_PER_BLK;
	DWORD mask = (1 << offset);
	dirty[blk] |= mask;
}

void CNodeAddressTable::clear_dirty(NID nid)
{
	UINT blk = nid / NAT_ENTRY_PER_BLK;
	UINT offset = nid % NAT_ENTRY_PER_BLK;
	DWORD mask = (1 << offset);
	dirty[blk] &= (~mask);
}

DWORD CNodeAddressTable::is_dirty(NID nid)
{
	UINT blk = nid / NAT_ENTRY_PER_BLK;
	UINT offset = nid % NAT_ENTRY_PER_BLK;
	DWORD mask = (1 << offset);

	return dirty[blk] & mask;
}



///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
#include "pch.h"
#include "../include/f2fs_simulator.h"
//#include <boost/unordered_set.hpp>

LOCAL_LOGGER_ENABLE(L"simulator.f2fs.nat", LOGGER_LEVEL_DEBUGINFO);

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
CNodeAddressTable::CNodeAddressTable(CF2fsSimulator* fs)
	: m_pages(&fs->m_pages), m_storage(&fs->m_storage), m_fs(fs)
{
	fs->m_health_info.m_node_nr = NODE_NR;
}

NID CNodeAddressTable::Init(PHY_BLK root)
{
	memset(nat, 0xFF, sizeof(PHY_BLK) * NODE_NR);
	memset(node_cache, 0xFF, sizeof(PAGE_INDEX) * NODE_NR);
	memset(dirty, 0xFF, sizeof(DWORD) * NAT_BLK_NR);
	if (!build_free()) {
		THROW_FS_ERROR(ERR_NO_SPACE, L"no valid node during initializing");
	}
	Sync();			// 将NAT写入磁盘
	return 0;
}

void CNodeAddressTable::CopyFrom(const CNodeAddressTable* src)
{
	memcpy_s(nat, sizeof(PHY_BLK) * NODE_NR, src->nat, sizeof(PHY_BLK) * NODE_NR);
	memcpy_s(node_cache, sizeof(PAGE_INDEX) * NODE_NR, src->node_cache, sizeof(PAGE_INDEX) * NODE_NR);
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
	memset(node_cache, 0xFF, sizeof(PAGE_INDEX) * NODE_NR);
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
		if (is_invalid(nat[ii])) {
			free_nr++;
			if (is_invalid(next_scan) ) next_scan = ii;
		}
	}
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
		if (is_invalid(nat[next_scan]) )
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
	if (nid >= NODE_NR) THROW_ERROR(ERR_USER, L"wrong node id:%d", nid);
	PHY_BLK blk = nat[nid];
	if (is_valid(blk) && blk != NID_IN_USE) {
		m_fs->m_segments.InvalidBlock(blk);
	}
	// decache
	if (is_valid(node_cache[nid]))
	{
		CPageInfo* page = m_pages->page(node_cache[nid]);
		m_pages->free(page);
		node_cache[nid] = INVALID_BLK;
	}
	nat[nid] = INVALID_BLK;
	free_nr++;
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

void CNodeAddressTable::f2fs_out_nat_journal(NAT_JOURNAL_ENTRY* journal, UINT &journal_nr)
{
	// 将journal中的nat写入磁盘
	CPageInfo* pages[NAT_BLK_NR];
	memset(pages, 0, sizeof(pages));
	for (UINT ii = 0; ii < journal_nr; ++ii)
	{
		// 计算nid所在的block
		NID nid = journal[ii].nid;
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
		nat_blk->nat[offset] = journal[ii].phy_blk;
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
	journal_nr = 0;
}

void CNodeAddressTable::f2fs_flush_nat_entries(CKPT_BLOCK& checkpoint)
{
	f2fs_out_nat_journal(checkpoint.nat_journals, checkpoint.nat_journal_nr);
//	checkpoint.nat_journal_nr = 0;
	// 将nat写入journal中。
	for (NID nid = 0; nid < NODE_NR; nid++) 
	{
		if (is_dirty(nid))
		{	// nid添加到journal中，检查nid是否已经在journal中
			if (checkpoint.nat_journal_nr >= JOURNAL_NR) {
				f2fs_out_nat_journal(checkpoint.nat_journals, checkpoint.nat_journal_nr);
				m_fs->m_health_info.nat_journal_overflow++;
//				checkpoint.nat_journal_nr = 0;
				LOG_ERROR(L"too many nat journal");
//				THROW_FS_ERROR(ERR_JOURNAL_OVERFLOW, L"too many nat journal");
			}
			int index = checkpoint.nat_journal_nr;
			checkpoint.nat_journals[index].nid = nid;
			checkpoint.nat_journals[index].phy_blk = nat[nid];
			clear_dirty(nid);
			checkpoint.nat_journal_nr++;
		}
	}
}



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

UINT CNodeAddressTable::get_dirty_node_nr(void)
{
	UINT dirty_nr = 0;
	for (UINT ii = 0; ii < NODE_NR; ++ii) {
		PAGE_INDEX pid = node_cache[ii];
		if (is_invalid(pid)) continue;
		CPageInfo* page = m_pages->page(pid);
		if (page && page->dirty) dirty_nr++;
	}
	return dirty_nr;
}



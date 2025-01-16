///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
#include "pch.h"
#include "../include/f2fs_segment.h"
#include "../include/f2fs_simulator.h"

LOCAL_LOGGER_ENABLE(L"segment", LOGGER_LEVEL_DEBUGINFO);

#define SORTING HEAP


CF2fsSegmentManager::CF2fsSegmentManager(CF2fsSimulator* fs)
{
	m_fs = fs;
	m_pages = &m_fs->m_pages;
	m_storage = &m_fs->m_storage;
	m_health_info = &m_fs->m_health_info;
	m_checkpoint = &m_fs->m_checkpoint;
}

bool CF2fsSegmentManager::InitSegmentManager(SEG_T segment_nr, SEG_T gc_lo, SEG_T gc_hi)
{
	// 初始化，如果blk_map指向block_id，初始化为0xFF，如果指向指针，初始化为0
	memset(m_segments, 0xFF, sizeof(SegmentInfo) * MAIN_SEG_NR);
	for (SEG_T ii = 0; ii < MAIN_SEG_NR; ++ii)
	{
		m_segments[ii].valid_blk_nr = 0;
		memset(m_segments[ii].valid_bmp, 0, sizeof(SegmentInfo::valid_bmp));
	}
	memset(m_dirty_map, 0xFF, sizeof(m_dirty_map));
	// 构建free链表
	build_free_link();
	memset(m_cur_segs, 0xFF, sizeof(m_cur_segs));
	m_gc_lo = gc_lo, m_gc_hi = gc_hi;
	memset(&m_data_cache, 0xFF, sizeof(m_data_cache));
	return true;
}

void CF2fsSegmentManager::CopyFrom(const CF2fsSegmentManager& src/*, CF2fsSimulator* fs*/)
{
	memcpy_s(m_segments, sizeof(m_segments), src.m_segments, sizeof(m_segments) );
	memcpy_s(m_cur_segs, sizeof(m_cur_segs), src.m_cur_segs, sizeof(m_cur_segs) );
	memcpy_s(m_dirty_map, sizeof(m_dirty_map), src.m_dirty_map, sizeof(m_dirty_map));
	m_free_nr = src.m_free_nr;
	m_free_tail = src.m_free_tail;
	m_free_head = src.m_free_head;
	m_used_blk_nr = src.m_used_blk_nr;
	m_gc_lo = src.m_gc_lo;
	m_gc_hi = src.m_gc_hi;
	memset(&m_data_cache, 0xFF, sizeof(m_data_cache));
}

void CF2fsSegmentManager::Reset(void)
{
	memset(m_segments, 0xFF, sizeof(SegmentInfo) * MAIN_SEG_NR);
	memset(m_dirty_map, 0, sizeof(m_dirty_map));
	memset(m_cur_segs, 0xFF, sizeof(m_cur_segs) );
	m_free_nr = 0;
	m_free_head = m_free_tail = INVALID_BLK;
	memset(&m_data_cache, 0xFF, sizeof(m_data_cache));
}

void CF2fsSegmentManager::f2fs_out_sit_journal(SIT_JOURNAL_ENTRY* journal, UINT &journal_nr)
{
	// 将journal中的sit写入磁盘
	CPageInfo* pages[SIT_BLK_NR];
	memset(pages, 0, sizeof(pages));
	for (UINT ii = 0; ii < journal_nr; ++ii)
	{
		//计算segment在sit中的位置
		SEG_T seg_no = journal[ii].seg_no;
		UINT sit_blk_id = seg_no / SIT_ENTRY_PER_BLK;
		UINT offset = seg_no % SIT_ENTRY_PER_BLK;
		// cache sit page
		SIT_BLOCK* sit_blk = nullptr;
		if (pages[sit_blk_id] == nullptr)
		{
			pages[sit_blk_id] = m_pages->allocate(true);
			m_storage->BlockRead(sit_blk_id + SIT_START_BLK, pages[sit_blk_id]);
		}
		//更新sit page
		sit_blk = &m_pages->get_data(pages[sit_blk_id])->sit;
		memcpy_s(&sit_blk->sit_entries[offset], sizeof(SEG_INFO), &journal[ii].seg_info, sizeof(SEG_INFO));
	}

	// 将更新的page写入sit block
	for (UINT blk = 0; blk < SIT_BLK_NR; ++blk)
	{
		if (pages[blk] == nullptr) continue;
		m_storage->BlockWrite(blk + SIT_START_BLK, pages[blk]);
		m_pages->free(pages[blk]);
		pages[blk] = nullptr;
	}
	journal_nr = 0;
}

void CF2fsSegmentManager::f2fs_flush_sit_entries(CKPT_BLOCK& checkpoint)
{
	f2fs_out_sit_journal(checkpoint.sit_journals, checkpoint.sit_journal_nr);
	// 将sit更新写入journal中
	for (SEG_T seg = 0; seg < MAIN_SEG_NR; seg++)
	{
		if (is_dirty(seg))
		{	// seg添加到journal中
			if (checkpoint.sit_journal_nr >= JOURNAL_NR)
			{
				f2fs_out_sit_journal(checkpoint.sit_journals, checkpoint.sit_journal_nr);
				m_fs->m_health_info.sit_journal_overflow++;
				LOG_ERROR(L"[err] too many sit journal.");
			}
			int index = checkpoint.sit_journal_nr;
			checkpoint.sit_journals[index].seg_no = seg;
			fill_seg_info(&checkpoint.sit_journals[index].seg_info, seg);
			clear_dirty(seg);
			checkpoint.sit_journal_nr++;
		}
	}
}

void CF2fsSegmentManager::SyncSIT(void)
{
	// 保存SIT
	UINT blk = 0;
	size_t bmp_size = sizeof(DWORD) * BITMAP_SIZE;
	CPageInfo* page = m_pages->allocate(true);
	for (SEG_T seg_id = 0; seg_id < MAIN_SEG_NR; ++blk)
	{
		if (m_dirty_map[blk]) {
			BLOCK_DATA* data = m_pages->get_data(page);
			SEG_INFO* seg_info = data->sit.sit_entries;
			LOG_DEBUG_(1, L"save SIT, seg id = %d", seg_id);

			int copy_nr = SIT_ENTRY_PER_BLK;
			if (seg_id + copy_nr > MAIN_SEG_NR) copy_nr = MAIN_SEG_NR - seg_id;
			for (int ii = 0; ii < copy_nr; ++ii, ++seg_id)		{
				fill_seg_info(&seg_info[ii], seg_id);
			}
			m_storage->BlockWrite(blk + SIT_START_BLK, page);
		}
		else seg_id += SIT_ENTRY_PER_BLK;
	}
	m_pages->free(page);
}

void CF2fsSegmentManager::SyncSSA(void)
{
	CPageInfo* page = m_pages->allocate(true);
	// 考虑到一个summary block包含一个segmet的信息。
	for (SEG_T seg_id = 0; seg_id < MAIN_SEG_NR; ++seg_id)
	{
		if (is_dirty(seg_id)) {
			SUMMARY_BLOCK& sum = m_pages->get_data(page)->ssa;
			for (UINT bb = 0; bb < BLOCK_PER_SEG; ++bb)
			{
				sum.entries[bb].nid = m_segments[seg_id].nids[bb];
				sum.entries[bb].offset = m_segments[seg_id].offset[bb];
			}
			m_storage->BlockWrite(seg_id+SSA_START_BLK, page);
		}
	}
	m_pages->free(page);
}

void CF2fsSegmentManager::fill_seg_info(SEG_INFO* seg_info, SEG_T seg)
{
	memcpy_s(seg_info->valid_bmp, bmp_size, m_segments[seg].valid_bmp, bmp_size);
	seg_info->valid_blk_nr = m_segments[seg].valid_blk_nr;
	seg_info->seg_temp = m_segments[seg].seg_temp;
}

void CF2fsSegmentManager::read_seg_info(SEG_INFO* seg_info, SEG_T seg)
{
	memcpy_s(m_segments[seg].valid_bmp, bmp_size, seg_info->valid_bmp, bmp_size);
	m_segments[seg].valid_blk_nr = seg_info->valid_blk_nr;
	m_segments[seg].seg_temp = seg_info->seg_temp;
}

bool CF2fsSegmentManager::Load(CKPT_BLOCK& checkpoint)
{
	// 读取SIT
	UINT lba = SIT_START_BLK;
	SEG_T seg_id = 0;
	CPageInfo* page = m_pages->allocate(true);
	for (; seg_id < MAIN_SEG_NR; ++lba)
	{
		m_storage->BlockRead(lba, page);
		BLOCK_DATA* data = m_pages->get_data(page);
		SEG_INFO* seg_info = data->sit.sit_entries;

		SEG_T copy_nr = SIT_ENTRY_PER_BLK;
		if (seg_id + copy_nr > MAIN_SEG_NR) copy_nr = MAIN_SEG_NR - seg_id;
		for (SEG_T ii = 0; ii < copy_nr; ++ii, ++seg_id)
		{
			read_seg_info(&seg_info[ii], seg_id);
		}
	}
	// 读取cur_seg
	size_t curseg_size = sizeof(CURSEG_INFO) * BT_TEMP_NR;
	memcpy_s(m_cur_segs, curseg_size, checkpoint.cur_segs, curseg_size);

	// 从journal中恢复最新改动
	if (checkpoint.sit_journal_nr > JOURNAL_NR) {
		THROW_FS_ERROR(ERR_INVALID_CHECKPOINT, L"SIT journal size is too large: %d", checkpoint.sit_journal_nr);
	}
	for (UINT ii = 0; ii < checkpoint.sit_journal_nr; ++ii)
	{
		SEG_T seg_id = checkpoint.sit_journals[ii].seg_no;
		read_seg_info(&checkpoint.sit_journals[ii].seg_info, seg_id);
	}
	
	lba = SSA_START_BLK;
	// 考虑到一个summary block包含一个segmet的信息。
	for (SEG_T seg_id = 0; seg_id < MAIN_SEG_NR; ++seg_id, ++lba)
	{
		m_storage->BlockRead(lba, page);
		SUMMARY_BLOCK& sum = m_pages->get_data(page)->ssa;
		for (UINT bb = 0; bb < BLOCK_PER_SEG; ++bb)
		{
			m_segments[seg_id].nids[bb] = sum.entries[bb].nid;
			m_segments[seg_id].offset[bb] = sum.entries[bb].offset;
		}
	}
	m_pages->free(page);
	memset(m_dirty_map, 0, sizeof(m_dirty_map));
	// 构建free block chain
	build_free_link();
	return true;
}

void CF2fsSegmentManager::build_free_link(void)
{
	m_free_tail = INVALID_BLK, m_free_head = INVALID_BLK;
	m_free_nr = 0;
	m_used_blk_nr = 0;

	for (SEG_T ii = 0; ii < MAIN_SEG_NR; ++ii)
	{	// 对于mount或者初始化，只要valid block number ==0即使这个segment没有被清除，也可作为free segment使用。
		SegmentInfo & seg  = m_segments[ii];
		m_used_blk_nr += seg.valid_blk_nr;
		if (seg.valid_blk_nr == 0 && m_cur_segs[seg.seg_temp].seg_no != ii)
		{
			seg.seg_temp = BT_TEMP_NR;
			free_en_queue(ii);
		}
		else
		{	// free_next指针指向下一个free的segment，INVALID_BLK被使用。
			seg.free_next = INVALID_BLK;
		}
	}
	LOG_DEBUG_(1, L"[free_seg]:build free_nr=%d, tail=%d, head=%d", m_free_nr, m_free_tail, m_free_head);
}

// 查找一个空的segment
SEG_T CF2fsSegmentManager::AllocSegment(BLK_TEMP temp, bool by_gc, bool force)
{
	if (m_free_nr == 0 || is_invalid(m_free_head)|| is_invalid(m_free_tail) )
	{
		THROW_ERROR(ERR_USER, L"no engouh empty segment");
		return INVALID_BLK;
	}

	if (m_free_nr <= RESERVED_SEG && !by_gc)
	{	// segment不够，尝试回收空间
		ERROR_CODE ir = GarbageCollection(m_fs);
		LOG_DEBUG_(1, L"[data_cache] called GC, free seg = %d", m_free_nr);
		if (m_free_nr <= 1 || ir != ERR_OK) {
			THROW_FS_ERROR(ERR_NO_SPACE, L"[err] gc error or no segment, error=%d, free=%d, force=%d", ir, m_free_nr, force);
		}
		else if (!force && m_free_nr <= RESERVED_SEG) {
			LOG_ERROR(L"[err] no enough segment for write, free=%d, force=%d", m_free_nr, force);
			return INVALID_BLK;
		}
	}
	// de-queue
	SEG_T new_seg = free_de_queue();
	LOG_DEBUG_(1, L"[free_seg]:allocate, seg=%d, free_nr=%d, tail=%d, head=%d", new_seg, m_free_nr, m_free_tail, m_free_head);

	// Initial Segment
	SegmentInfo& seg = m_segments[new_seg];
	seg.valid_blk_nr = 0;
	seg.seg_temp = temp;
//	seg.cur_blk = 0;
	memset(seg.valid_bmp, 0, sizeof(SegmentInfo::valid_bmp));
	memset(seg.nids, 0xFF, sizeof(SegmentInfo::nids));
	memset(seg.offset, 0xFF, sizeof(SegmentInfo::offset));
	set_dirty(new_seg);
	return new_seg;
}

// 回收一个segment
void CF2fsSegmentManager::FreeSegment(SEG_T seg_id)
{
	SegmentInfo& seg = m_segments[seg_id];
	// 保留erase count
	seg.seg_temp = BT_TEMP_NR;
	// en-queue
	free_en_queue(seg_id);
	LOG_DEBUG_(1, L"[free_seg]:free, seg=%d, free_nr=%d, tail=%d, head=%d", seg_id, m_free_nr, m_free_tail, m_free_head);
//	seg.cur_blk = 0;
	// 将seg放入free list中；
	set_dirty(seg_id);
}

void CF2fsSegmentManager::free_en_queue(SEG_T ii)
{
	if (is_valid(m_free_head)) {
		// 原来队列不空
		m_segments[ii].free_next = m_segments[m_free_head].free_next;
		m_segments[m_free_head].free_next = ii;
		m_free_head = ii;
	}
	else {	// 原来队列为空
		JCASSERT(is_invalid(m_free_tail));
		m_segments[ii].free_next = ii;
		m_free_head = ii;
		m_free_tail = ii;
	}
	m_free_nr++;
}

SEG_T CF2fsSegmentManager::free_de_queue(void)
{
	if (is_invalid(m_free_head) || is_invalid(m_free_tail)) {
		THROW_ERROR(ERR_APP, L"free que is empty, free_nr=%d, tail=%d, head=%d", m_free_nr, m_free_tail, m_free_head);
	}
	SEG_T new_seg = m_free_tail;
	if (m_free_tail == m_free_head) {
		m_free_tail = INVALID_BLK;
		m_free_head = INVALID_BLK;
	}
	else {
		m_free_tail = m_segments[new_seg].free_next;
		m_segments[m_free_head].free_next = m_free_tail;
	}
	m_segments[new_seg].free_next = INVALID_BLK;
	m_free_nr--;
	return new_seg;
}



void CF2fsSegmentManager::GetBlockInfo(NID& nid, WORD& offset, PHY_BLK phy_blk)
{
	SEG_T seg_id;
	BLK_T blk_id;
	BlockToSeg(seg_id, blk_id, phy_blk);
	SegmentInfo& seg = m_segments[seg_id];
	if (test_bitmap(seg.valid_bmp, blk_id) == 0)
	{	// 对于invalid block
		nid = INVALID_BLK;
		offset = INVALID_BLK;
	}
	else
	{
		nid = seg.nids[blk_id];
		offset = seg.offset[blk_id];
	}
}

void CF2fsSegmentManager::SetBlockInfo(NID nid, WORD offset, PHY_BLK phy_blk)
{
	SEG_T seg_id;
	BLK_T blk_id;
	BlockToSeg(seg_id, blk_id, phy_blk);
	SegmentInfo& seg = m_segments[seg_id];
	seg.nids[blk_id] = nid;
	seg.offset[blk_id] = offset;
	set_dirty(seg_id);
}

PHY_BLK CF2fsSegmentManager::WriteBlockToSeg(CPageInfo* page, bool force, bool by_gc)
{
	//LOG_STACK_TRACE()
	// 计算page的温度，（实际温度，用于统计）
	page->ttemp = m_fs->GetBlockTemp(page);
	BLK_TEMP temp = m_fs->GetAlgorithmBlockTemp(page, page->ttemp);

	// 由于GC的trigger移到AllocSegment()中，有可能遇到GC 正要写入的page的情况。这会导致page->phy_blk地址改变。重复invalid同一个物理地址。
	// 对策：对于正要写入的page cache。GC时会检查page是否已经在cache中了。如果在，则跳过GC。
	if (!by_gc) {
//		JCASSERT(is_invalid(m_data_cache.nid) && is_invalid(m_data_cache.offset));
		m_data_cache.nid = page->nid;
		m_data_cache.offset = page->offset;
		m_data_cache.page = page;
		LOG_DEBUG_(1, L"[data_cache] set data cache, nid=%d,offset=%d", m_data_cache.nid, m_data_cache.offset);
	}

	// 按照温度（算法温度）给page分配segment。
	CURSEG_INFO& curseg = m_cur_segs[temp];
	if (is_invalid(curseg.seg_no)) {
		SEG_T seg_no = AllocSegment(temp, by_gc, force);
		if (is_invalid(seg_no))
		{
			m_data_cache.nid = INVALID_BLK;
			m_data_cache.offset = INVALID_BLK;
			m_data_cache.page = nullptr;
			LOG_DEBUG_(1, L"[data_cache] clear data cache");
			LOG_ERROR(L"[err] no enough segment for write");
			return INVALID_BLK;
		}
		curseg.seg_no = seg_no;
		curseg.blk_offset = 0;
	}
	SegmentInfo& seg = m_segments[curseg.seg_no];
	BLK_T blk_id = curseg.blk_offset;
	set_bitmap(seg.valid_bmp, blk_id);
	UINT lba = seg_to_lba(curseg.seg_no, blk_id);
	m_storage->BlockWrite(lba, page);

	seg.nids[blk_id] = page->nid;
	seg.offset[blk_id] = (WORD)page->offset;

	seg.valid_blk_nr++;
	m_used_blk_nr++;
	//	seg.cur_blk++;
	curseg.blk_offset++;
	// segment在分配时设置dirty
	set_dirty(curseg.seg_no);

	InterlockedIncrement64(&m_health_info->m_total_media_write);
	InterlockedDecrement16((SHORT*)(& m_health_info->m_free_blk));
	InterlockedIncrement16((SHORT*)(& m_health_info->m_physical_saturation));

	PHY_BLK src_phy_blk = page->phy_blk;
	if (is_valid(src_phy_blk))
	{
		SEG_T src_seg_id; BLK_T src_blk_id;
		BlockToSeg(src_seg_id, src_blk_id, src_phy_blk);
		InvalidBlock(src_seg_id, src_blk_id);
	}

	SEG_T tar_seg = curseg.seg_no;
	BLK_T tar_blk = blk_id;
	PHY_BLK phy_blk = PhyBlock(tar_seg, tar_blk);
	page->phy_blk = phy_blk;

	// 由于WriteBlockToSeg()会在GC中被调用，目前的GC算法针对segment进行GC，GC后并不能清楚dirty标志。
	if (!by_gc) page->dirty = false;

	if (curseg.blk_offset >= BLOCK_PER_SEG)
	{	// 当前segment已经写满
		curseg.seg_no = INVALID_BLK;
	}
	//clear data cache
	if (!by_gc) {
		m_data_cache.nid = INVALID_BLK;
		m_data_cache.offset = INVALID_BLK;
		m_data_cache.page = nullptr;
		LOG_DEBUG_(1, L"[data_cache] clear data cache");

	}
	return  phy_blk;
}

#define SORTING HEAP

ERROR_CODE CF2fsSegmentManager::GarbageCollection(CF2fsSimulator* fs)
{
	LOG_STACK_TRACE();
	m_fs->fs_trace("GC-IN", 0, 0, 0);

#if (SORTING == HEAP)
	GcPoolHeap<64, SegmentInfo > pool(m_segments);

#elif (SORTING == QSORT)
	GcPoolQuick<64, SegmentInfo >& pool = *m_gc_pool;
	pool.init();
#endif
	for (SEG_T ss = 0; ss < MAIN_SEG_NR; ss++)
	{
		SegmentInfo* seg = m_segments + ss;
		// 当valid block为0时，直接回收。seg_temp表示block是否已经被回收。
		if (seg->valid_blk_nr == 0 && is_invalid(seg->free_next)) {
			if (seg->valid_bmp[0] != 0)	THROW_ERROR(ERR_USER, L"try to free a non-empty segment, seg=%d", ss);
			FreeSegment(ss);
			continue;
		}
//		if (seg->cur_blk < BLOCK_PER_SEG) continue;  // 跳过未写满的segment
		if (m_cur_segs[seg->seg_temp].seg_no == ss) continue;
		pool.Push(seg);
	}
	pool.Sort();
	SEG_T free_before_gc = m_free_nr;
	SEG_T claimed_seg = 0;

	LONG64 media_write_before = m_health_info->m_total_media_write;
	LONG64 host_write_before = m_health_info->m_total_host_write;
	UINT media_write_count = 0;

	CPageInfo _page;
	CPageInfo* page = &_page;
	while (m_free_nr < m_gc_hi)
	{
		SegmentInfo* src_seg = pool.Pop();
		if (src_seg == nullptr) break;
		SEG_T src_seg_id = (SEG_T)(src_seg - m_segments);
		UINT valid_blk = src_seg->valid_blk_nr;
		
		m_fs->m_health_info.gc_count++;
		for (BLK_T bb = 0; bb < BLOCK_PER_SEG; ++bb)
		{
			if (test_bitmap(src_seg->valid_bmp, bb) == 0) continue;
			NID nid = src_seg->nids[bb];		// block 所在的inode
			WORD offset = src_seg->offset[bb];
			// 如果有效，检查block的nid和offset
			PHY_BLK org_phy = PhyBlock(src_seg_id, bb);
			UINT lba = phyblk_to_lba(org_phy);
			// 读取page
			// page 是否为node，且已经cache了，将cache 的page强制刷新
			// <TODO> 在F2FS中，还要判断L2P的physical地址是否匹配，不匹配的跳过GC。
			//		但是如果跳过的话，会导致block无法释放。（此处仅检查L2P是否匹配）

			CPageInfo* node_page = nullptr;
			m_fs->ReadNode(nid, node_page);
			if ( is_invalid(offset) )
			{	// node block, page 被cache住
				PHY_BLK l2p = m_fs->m_nat.get_phy_blk(nid);
				if (l2p != org_phy) {
					THROW_FS_ERROR(ERR_PHY_ADDR_MISMATCH, L"phy_blk [%d] node=%d, L2P=%d mismatch", org_phy, nid, l2p);
				}

				PHY_BLK phy_blk = WriteBlockToSeg(node_page, true, true);
				node_page->dirty = false;
				m_fs->m_nat.set_phy_blk(nid, phy_blk);
				LOG_DEBUG_(1, L"[gc] move (node=%d) from %d to %d", nid, org_phy, phy_blk);
			}
			else
			{
				if (m_data_cache.nid == nid && m_data_cache.offset == offset) {
					// 此page已经在write cache中。马上会被写入，因此不需要在搬移。
					continue;
				}
				BLOCK_DATA * node_blk = m_pages->get_data(node_page);
				if (node_blk->m_type != BLOCK_DATA::BLOCK_INDEX) {
					THROW_FS_ERROR(ERR_WRONG_BLOCK_TYPE, L"node [%d] expected INDEX BLOCK, wrong type=%d", 
						nid, node_blk->m_type);
				}
				if (offset > INDEX_SIZE) {
					THROW_FS_ERROR(ERR_INVALID_INDEX, L"data block [%d,%d], phy=%d, offset over range",	nid, offset, org_phy);
				}
				PHY_BLK l2p = node_blk->node.index.index[offset];
				if (l2p != org_phy) {
					THROW_FS_ERROR(ERR_PHY_ADDR_MISMATCH, L"data block [%d, %d] phy=%d, L2P=%d mismatch", 
						nid, offset, org_phy, l2p);
				}

				m_storage->BlockRead(lba, page);
				// 写入page	// 注意：GC不应该改变block的温度，这里做一个检查
				page->phy_blk = org_phy;
				page->nid = nid;
				page->offset = offset;
				PHY_BLK phy_blk = WriteBlockToSeg(page, true, true);
				m_fs->UpdateIndex(nid, offset, phy_blk);
				LOG_DEBUG_(1, L"[gc] move data(%d,%d) from %d to %d", nid, offset, org_phy, phy_blk);
			}
			// Write Block To Segment就会触发invalid 旧的block，当原segment中的所有block都无效了，会free. 此处不用再次invalid
			// invalid original blk
			media_write_count++;
			// for debug
#ifdef GC_TRACE
			gc_trace.emplace_back();
			GC_TRACE& gc= gc_trace.back();
			gc.fid = nid;
			gc.offset = offset;		
			gc.org_phy = org_phy;
			gc.new_phy = phy_blk;
#endif
			// 当valid block为0时，source segment已经被回收，因此其nid和offset不可信。
			valid_blk--;
			if (valid_blk == 0) break;
		}
		claimed_seg++;
	}

#ifdef ENABLE_FS_TRACE
	if (m_gc_trace)
	{
#if (GC_TRACE_TYPE==1)
		fprintf_s(m_gc_trace, "%d,%d,%d,%d,%d,%d\n",
			free_before_gc, m_free_nr, m_free_nr - free_before_gc, claimed_seg,
			(UINT)(m_health_info->m_total_media_write - media_write_before), media_write_count);
#elif (GC_TRACE_TYPE==2)
		//		fprintf_s(m_gc_trace, "%d,%lld,%lld,%d,%d,%d,%d\n", m_gc_pool->sort_count,m_gc_pool->low,m_gc_pool->high, m_gc_pool->min_val, m_gc_pool->max_val,m_gc_pool->pth, claimed_seg);
#endif
	}
#endif
	m_fs->fs_trace("GC-OUT", 0, 0, 0);
	return ERR_OK;
}

#define FLUSH_SEGMENT_BLOCK_OUT(_blk)	\
	if (is_valid(pre_fid)) {	\
		fprintf_s(log, "%d,%d,%d,1,DATA,%d,%d\n", ss, start_blk, (_blk-start_blk), pre_fid, start_lblk);	\
		pre_fid = INVALID_BLK; start_blk = _blk; start_lblk = 0; pre_lblk = 0; }

bool CF2fsSegmentManager::InvalidBlock(PHY_BLK phy_blk)
{
	if (is_invalid(phy_blk) ) return false;
	SEG_T seg_id; BLK_T blk_id;
	BlockToSeg(seg_id, blk_id, phy_blk);
	return InvalidBlock(seg_id, blk_id);
}

bool CF2fsSegmentManager::InvalidBlock(SEG_T seg_id, BLK_T blk_id)
{
	bool free_seg = false;
	JCASSERT(seg_id < MAIN_SEG_NR);
	SegmentInfo& seg = m_segments[seg_id];
	// block的有效性判断以SIT为主
	if (test_bitmap(seg.valid_bmp, blk_id) == 0) {
		THROW_FS_ERROR(ERR_DOUBLED_BLK, L"double invalid phy block, seg=%d, blk=%d,", seg_id, blk_id);
	}

	seg.valid_blk_nr--;
	m_used_blk_nr--;
	clear_bitmap(seg.valid_bmp, blk_id);
	LOG_DEBUG_(1, L"[invalid block] seg=%d, blk=%d, nid=%d, offset=%d, valid_blk=%d,", 
		seg_id, blk_id, seg.nids[blk_id], seg.offset[blk_id], seg.valid_blk_nr);

	seg.nids[blk_id] = INVALID_BLK;
	seg.offset[blk_id] = INVALID_BLK;
	set_dirty(seg_id);

	// 不回收当前的segment
	if (seg.valid_blk_nr == 0 // && seg.cur_blk >= BLOCK_PER_SEG)
		&& m_cur_segs[seg.seg_temp].seg_no != seg_id)
	{
		SEG_T seg_id =SegId(&seg);
#if 1		// for debug only, 检查是否所有block都invalid
		if (seg.valid_bmp[0] != 0)	THROW_ERROR(ERR_USER, L"try to free a non-empty segment, seg=%d", seg_id);
#endif
		FreeSegment(seg_id);
		free_seg = true;
	}
	InterlockedIncrement16((SHORT*)(& m_health_info->m_free_blk));
	InterlockedDecrement16((SHORT*)(& m_health_info->m_physical_saturation));
	return free_seg;
}

DWORD CF2fsSegmentManager::is_dirty(SEG_T seg_id)
{
	UINT sit_blk = seg_id / SIT_ENTRY_PER_BLK;
	UINT offset = seg_id % SIT_ENTRY_PER_BLK;
	DWORD mask = (1 << offset);
	return (m_dirty_map[sit_blk] & mask);
}

void CF2fsSegmentManager::set_dirty(SEG_T seg_id)
{
	UINT sit_blk = seg_id / SIT_ENTRY_PER_BLK;
	UINT offset = seg_id % SIT_ENTRY_PER_BLK;
	DWORD mask = (1 << offset);
	m_dirty_map[sit_blk] |= mask;
}

void CF2fsSegmentManager::clear_dirty(SEG_T seg_id)
{
	UINT sit_blk = seg_id / SIT_ENTRY_PER_BLK;
	UINT offset = seg_id % SIT_ENTRY_PER_BLK;
	DWORD mask = (1 << offset);
	m_dirty_map[sit_blk] &= (~mask);
}



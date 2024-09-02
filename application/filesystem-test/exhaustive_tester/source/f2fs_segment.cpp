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
}

bool CF2fsSegmentManager::InitSegmentManager(SEG_T segment_nr, SEG_T gc_lo, SEG_T gc_hi)
{
	// ��ʼ�������blk_mapָ��block_id����ʼ��Ϊ0xFF�����ָ��ָ�룬��ʼ��Ϊ0
	memset(m_segments, 0xFF, sizeof(SegmentInfo) * MAIN_SEG_NR);
	for (SEG_T ii = 0; ii < MAIN_SEG_NR; ++ii)
	{
		m_segments[ii].seg_id = ii;
	}
	memset(m_dirty_map, 0, sizeof(m_dirty_map));
	// ����free����
	build_free_link();
	memset(m_cur_segs, 0xFF, sizeof(SEG_T) * BT_TEMP_NR);
	m_gc_lo = gc_lo, m_gc_hi = gc_hi;

#ifdef ENABLE_FS_TRACE
	if (m_gc_trace)
	{
		// free_before: GGǰ��free segment��free_after: GC���free segment, reclaimed_seg: GC�ͷŵ�segment����
		// src_seg: GC��ʹ�õ�segment��������released��ȽϿ�������GCЧ��)
		// ͨ��GCǰ��health.media_write����õ���media write block����
		// GC�������ۼӵ�media write block������������ֻ�Ǽ��㷽����ͬ����������ֵӦ����ͬ��
#if (GC_TRACE_TYPE==1)
		fprintf_s(m_gc_trace, "free_before,free_after,released_seg,src_sge,media_write,media_write_gc\n");
#elif (GC_TRACE_TYPE ==2)
		fprintf_s(m_gc_trace, "sort_cnt,low,high,min_vb,max_vb,sort_th,gc_secs\n");
#endif
	}
#endif
	return true;
}

void CF2fsSegmentManager::build_free_link(void)
{
	SEG_T* free_ptr = &m_free_ptr;
	m_free_ptr = INVALID_BLK;
	m_free_nr = 0;

	for (SEG_T ii = 0; ii < MAIN_SEG_NR; ++ii)
	{
		if (m_segments[ii].valid_blk_nr == INVALID_BLK)
		{
			*free_ptr = ii;
			free_ptr = &(m_segments[ii].SEG_NEXT_FREE);
			m_free_nr++;
		}
	}
}


void CF2fsSegmentManager::CopyFrom(const CF2fsSegmentManager& src/*, CF2fsSimulator* fs*/)
{
	memcpy_s(m_segments, sizeof(m_segments), src.m_segments, sizeof(m_segments) );
	memcpy_s(m_cur_segs, sizeof(m_cur_segs), src.m_cur_segs, sizeof(m_cur_segs) );
	memcpy_s(m_dirty_map, sizeof(m_dirty_map), src.m_dirty_map, sizeof(m_dirty_map));
	m_free_nr = src.m_free_nr;
	m_free_ptr = src.m_free_ptr;
	m_gc_lo = src.m_gc_lo;
	m_gc_hi = src.m_gc_hi;
}

void CF2fsSegmentManager::Reset(void)
{
	memset(m_segments, 0xFF, sizeof(SegmentInfo) * MAIN_SEG_NR);
	memset(m_dirty_map, 0, sizeof(m_dirty_map));
	memset(m_cur_segs, 0xFF, sizeof(SEG_T) * BT_TEMP_NR);
	m_free_nr = 0;
	m_free_ptr = INVALID_BLK;
}

void CF2fsSegmentManager::Sync(void)
{
	// ����SIT
	UINT lba = SIT_START_BLK;
	size_t bmp_size = sizeof(DWORD) * BITMAP_SIZE;
	CPageInfo* page = m_pages->allocate(true);
	for (SEG_T seg_id = 0; seg_id < MAIN_SEG_NR; ++lba)
	{
		BLOCK_DATA* data = m_pages->get_data(page);
		SEG_INFO *seg_info = data->sit.sit_entries;

		int copy_nr = SIT_ENTRY_PER_BLK;
		if (seg_id + copy_nr > MAIN_SEG_NR) copy_nr = MAIN_SEG_NR - seg_id;
		for (int ii = 0; ii < copy_nr; ++ii, ++seg_id)
		{
			memcpy_s(seg_info[ii].valid_bmp, bmp_size, m_segments[seg_id].valid_bmp, bmp_size);
			seg_info[ii].valid_blk_nr = m_segments[seg_id].valid_blk_nr;
			seg_info[ii].cur_blk = m_segments[seg_id].cur_blk;
		}
		m_storage->BlockWrite(lba, page);
	}

	lba = SSA_START_BLK;
	// ���ǵ�һ��summary block����һ��segmet����Ϣ��
	for (SEG_T seg_id = 0; seg_id < MAIN_SEG_NR; ++seg_id, ++lba)
	{
		SUMMARY_BLOCK& sum = m_pages->get_data(page)->ssa;
		for (UINT bb = 0; bb<BLOCK_PER_SEG; ++bb)
		{
			sum.entries[bb].nid = m_segments[seg_id].nids[bb];
			sum.entries[bb].offset = m_segments[seg_id].offset[bb];
		}
		m_storage->BlockWrite(lba, page);
	}
	m_pages->free(page);
}

bool CF2fsSegmentManager::Load(void)
{
	// ��ȡSIT
	UINT lba = SIT_START_BLK;
	size_t bmp_size = sizeof(DWORD) * BITMAP_SIZE;
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
			memcpy_s(m_segments[seg_id].valid_bmp, bmp_size, seg_info[ii].valid_bmp, bmp_size);
			m_segments[seg_id].valid_blk_nr = seg_info[ii].valid_blk_nr;
			m_segments[seg_id].cur_blk = seg_info[ii].cur_blk;
			m_segments[seg_id].seg_id = seg_id;
		}
	}
	
	lba = SSA_START_BLK;
	// ���ǵ�һ��summary block����һ��segmet����Ϣ��
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
	// ����free block chain
	build_free_link();
	return true;
}

// ����һ���յ�segment
SEG_T CF2fsSegmentManager::AllocSegment(BLK_TEMP temp)
{
	if (m_free_nr == 0 || m_free_ptr == INVALID_BLK)
	{
		THROW_ERROR(ERR_USER, L"no engouh empty segment");
		return INVALID_BLK;
	}
	SEG_T new_seg = m_free_ptr;
	m_free_ptr = m_segments[new_seg].SEG_NEXT_FREE;
	m_free_nr--;
	LOG_DEBUG(L"SEG_MNG: allocate segment: %d", new_seg);
	// Initial Segment
	SegmentInfo& seg = m_segments[new_seg];
	seg.valid_blk_nr = 0;
	seg.seg_temp = temp;
	seg.cur_blk = 0;
	seg.seg_id = new_seg;
	memset(seg.valid_bmp, 0, sizeof(SegmentInfo::valid_bmp));
	memset(seg.nids, 0xFF, sizeof(SegmentInfo::nids));
	memset(seg.offset, 0xFF, sizeof(SegmentInfo::offset));
	return new_seg;
}

// ����һ��segment
void CF2fsSegmentManager::FreeSegment(SEG_T seg_id)
{
	SegmentInfo& seg = m_segments[seg_id];
	// ����erase count
	seg.seg_temp = BT_TEMP_NR;
	seg.cur_blk = 0;
	// ��seg����free list�У�
	seg.SEG_NEXT_FREE = m_free_ptr;
	m_free_ptr = seg_id;
	m_free_nr++;
	LOG_DEBUG(L"SEG_MNG: free segment: %d", seg_id);
}

bool CF2fsSegmentManager::InvalidBlock(PHY_BLK phy_blk)
{
	if (phy_blk == INVALID_BLK) return false;
	SEG_T seg_id; BLK_T blk_id;
	BlockToSeg(seg_id, blk_id, phy_blk);
	return InvalidBlock(seg_id, blk_id);
}

PHY_BLK CF2fsSegmentManager::WriteBlockToSeg(CPageInfo * page, bool by_gc)
{
	// ����page���¶ȣ���ʵ���¶ȣ�����ͳ�ƣ�
	page->ttemp = m_fs->GetBlockTemp(page);
	BLK_TEMP temp = m_fs->GetAlgorithmBlockTemp(page, page->ttemp);

	// �����¶ȣ��㷨�¶ȣ���page����segment��
	SEG_T& cur_seg_id = m_cur_segs[temp];
	if (cur_seg_id == INVALID_BLK) cur_seg_id = AllocSegment(temp);
	SegmentInfo& seg = m_segments[cur_seg_id];

	BLK_T blk_id = seg.cur_blk;
	UINT lba = seg_to_lba(cur_seg_id, blk_id);
	m_storage->BlockWrite(lba, page);

	seg.nids[blk_id] = page->nid;
	seg.offset[blk_id] = (WORD) page->offset;

	seg.valid_blk_nr++;
	seg.cur_blk++;

	InterlockedIncrement64(&m_health_info->m_total_media_write);
	InterlockedDecrement(&m_health_info->m_free_blk);
	InterlockedIncrement(&m_health_info->m_physical_saturation);

	PHY_BLK src_phy_blk = page->phy_blk;
	LOG_DEBUG(L"[write block] seg=%d, blk=%d, valid_blk=%d, org_pblk=%04X,", cur_seg_id, blk_id, seg.valid_blk_nr, src_phy_blk);
	if (src_phy_blk != INVALID_BLK)
	{
		SEG_T src_seg_id; BLK_T src_blk_id;
		BlockToSeg(src_seg_id, src_blk_id, src_phy_blk);
		InvalidBlock(src_seg_id, src_blk_id);
//		InvalidBlock(src_phy_blk);
	}

	SEG_T tar_seg = cur_seg_id;
	BLK_T tar_blk = blk_id;
	PHY_BLK phy_blk = PhyBlock(tar_seg, tar_blk);
	page->phy_blk = phy_blk;

	// ����WriteBlockToSeg()����GC�б����ã�Ŀǰ��GC�㷨���segment����GC��GC�󲢲������dirty��־��
	if (!by_gc) page->dirty = false;

	if (seg.cur_blk >= BLOCK_PER_SEG)
	{	// ��ǰsegment�Ѿ�д��
		cur_seg_id = INVALID_BLK;
	}
	return  phy_blk;
}

#define SORTING HEAP

void CF2fsSegmentManager::GarbageCollection(CF2fsSimulator* fs)
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
		if (seg->cur_blk < BLOCK_PER_SEG) continue;  // ����δд����segment
		pool.Push(seg);
	}
	pool.Sort();
	SEG_T free_before_gc = m_free_nr;
	SEG_T claimed_seg = 0;

	LONG64 media_write_before = m_health_info->m_total_media_write;
	LONG64 host_write_before = m_health_info->m_total_host_write;
	UINT media_write_count = 0;
	while (m_free_nr < m_gc_hi)
	{
		SegmentInfo* src_seg = pool.Pop();
		if (src_seg == nullptr)
		{	// GC pool�е�block����ȡ�꣬�����block�����գ�����ʱֹͣGC�����򱨴�
			if (m_free_nr >= m_gc_lo) break;
			THROW_ERROR(ERR_APP, L"cannot find segment which has invalid block");
		}
		SEG_T src_seg_id = (SEG_T)(src_seg - m_segments);
		UINT valid_blk = src_seg->valid_blk_nr;
//		LOG_DEBUG(L"GC: src segment=%d / %d, valid_blk = %d", src_seg_id, src_seg->seg_id, valid_blk);
		
		CPageInfo* page = m_pages->allocate(true);
		for (BLK_T bb = 0; bb < BLOCK_PER_SEG; ++bb)
		{
			NID nid = src_seg->nids[bb];		// block ���ڵ�inode
			if (nid == INVALID_BLK) continue;	//���segment�У����block�Ƿ���Ч
			WORD offset = src_seg->offset[bb];

			// �����Ч�����block��nid��offset
			PHY_BLK org_phy = PhyBlock(src_seg->seg_id, bb);
			UINT lba = phyblk_to_lba(org_phy);
			// ��ȡpage
			if (page == nullptr) THROW_ERROR(ERR_USER, L"no enough pages");
			// page �Ƿ�Ϊnode�����Ѿ�cache�ˣ���cache ��pageǿ��ˢ��
			if ((offset >= INVALID_FID) && (m_fs->m_nat.node_catch[nid] != INVALID_BLK))
			{	// node block, page ��cacheס
				CPageInfo* _page = m_pages->page(m_fs->m_nat.node_catch[nid]);
				PHY_BLK phy_blk = WriteBlockToSeg(_page, true);
				_page->dirty = false;
				m_fs->m_nat.set_phy_blk(nid, phy_blk);
			}
			else
			{
				m_storage->BlockRead(lba, page);

				// д��page	// ע�⣺GC��Ӧ�øı�block���¶ȣ�������һ�����
				page->phy_blk = org_phy;
				page->nid = nid;
				page->offset = offset;

				int _seg_id = (int)(src_seg - m_segments);
				//			LOG_DEBUG(L"GC: nid=%03d, offset=%03d, phy=(%02X / %02X, %02X):0x%04X => ??", 
				//				nid, offset, _seg_id, src_seg->seg_id, bb, org_phy);
				PHY_BLK phy_blk = WriteBlockToSeg(page, true);

				// ������û�м�¼�¶���Ϣ����ʱ���ԱȽ�
#if 0
				BLK_TEMP after_temp = page->ttemp;
				if (before_temp != after_temp)
				{
					THROW_ERROR(ERR_APP, L"page temperature mismatch, before=%S(%d), after=%S(%d)",
						BLK_TEMP_NAME[before_temp], before_temp, BLK_TEMP_NAME[after_temp], after_temp);
			}
#endif
				LOG_DEBUG(L"GC:  new phy=0x%04X", phy_blk);
				// ����inode ���� nat
				if (offset >= INVALID_FID)
				{	// node block�� ����nat // <TODO> ���F2FS��˭�������L2P��write()����������write()�ĵ����ߣ�
					m_fs->UpdateNat(nid, phy_blk);
				}
				else
				{
					m_fs->UpdateIndex(nid, offset, phy_blk);
				}
			}
			// Write Block To Segment�ͻᴥ��invalid �ɵ�block����ԭsegment�е�����block����Ч�ˣ���free. �˴������ٴ�invalid
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
			// ��valid blockΪ0ʱ��source segment�Ѿ������գ������nid��offset�����š�
			valid_blk--;
			if (valid_blk == 0) break;
		}
		m_pages->free(page);
		claimed_seg++;
	}
	//LOG_TRACK(L"gc", L"SUM,free_before=%d,free_after=%d,released_seg=%d,src_nr=%d,host_write=%lld,%lld,media_write=%lld,%lld,delta=%d,media_write=%d",
	//	free_before_gc, m_free_nr, m_free_nr - free_before_gc, claimed_seg,
	//	host_write_before, m_health_info->m_total_host_write, media_write_before, m_health_info->m_total_media_write,
	//	(UINT)(m_health_info->m_total_media_write - media_write_before), media_write_count);

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
}

#define FLUSH_SEGMENT_BLOCK_OUT(_blk)	\
	if (pre_fid != INVALID_BLK) {	\
		fprintf_s(log, "%d,%d,%d,1,DATA,%d,%d\n", ss, start_blk, (_blk-start_blk), pre_fid, start_lblk);	\
		pre_fid = INVALID_BLK; start_blk = _blk; start_lblk = 0; pre_lblk = 0; }



bool CF2fsSegmentManager::InvalidBlock(SEG_T seg_id, BLK_T blk_id)
{
	bool free_seg = false;
	JCASSERT(seg_id < MAIN_SEG_NR);
	SegmentInfo& seg = m_segments[seg_id];
	if (seg.nids[blk_id] == INVALID_BLK) THROW_ERROR(ERR_USER, L"double invalid phy block, seg=%d, blk=%d,", seg_id, blk_id);
	seg.valid_blk_nr--;
	seg.nids[blk_id] = INVALID_BLK;
	seg.offset[blk_id] = INVALID_FID;

	LOG_DEBUG(L"[invalid block] seg=%d, blk=%d, valid_blk=%d,", seg_id, blk_id, seg.valid_blk_nr);

	if (seg.valid_blk_nr == 0 && seg.cur_blk >= BLOCK_PER_SEG)
	{
#if 1		// for debug only, ����Ƿ�����block��invalid
		for (BLK_T bb = 0; bb < BLOCK_PER_SEG; ++bb)
		{
			if (seg.nids[bb] != INVALID_BLK) THROW_ERROR(ERR_USER, L"try to free a non-empty segment, seg=%d", seg.seg_id);
		}
#endif
		FreeSegment(seg_id);
		free_seg = true;
	}
	InterlockedIncrement(&m_health_info->m_free_blk);
	InterlockedDecrement(&m_health_info->m_physical_saturation);
	return free_seg;
}


void CF2fsSegmentManager::DumpSegmentBlocks(const std::wstring& fn)
{
	// for merge
	NID pre_fid = INVALID_BLK;
	LBLK_T start_lblk = 0, pre_lblk = 0;
	BLK_T start_blk = 0;

	FILE* log = nullptr;
	_wfopen_s(&log, fn.c_str(), L"w+");
	if (log == nullptr) THROW_ERROR(ERR_APP, L"failed on opening file %s", fn.c_str());
	fprintf_s(log, "seg,blk,blk_num,valid,type,fid,lblk\n");
	for (SEG_T ss = 0; ss < MAIN_SEG_NR; ++ss)
	{
		SegmentInfo& seg = m_segments[ss];
		for (BLK_T bb = 0; bb < BLOCK_PER_SEG; ++bb)
		{
			NID nid = seg.nids[bb];	// block ���ڵ�inode
			// ���segment�У����block�Ƿ���Ч��
			if (nid == INVALID_BLK) continue;
			// �����Ч�����block���ڵ�inode��offset
			WORD offset = seg.offset[bb];
			// ��ȡpage
			UINT lba = seg_to_lba(ss, bb);
			CPageInfo* page = m_pages->allocate(true);
			m_storage->BlockRead(lba, page);
			page->phy_blk = PhyBlock(ss, bb);

			// д��page
			PHY_BLK new_blk = WriteBlockToSeg(page, true);
			// ����inode
			if (offset != 0xFFFF)
			{	// ����һ��data block��Ҫ����node block�еĵ�ַ


			}
			else
			{	// ����һ��node block������nat
				m_fs->UpdateNat(nid, new_blk);
			}
//			CPageAllocator::INDEX _pblk = seg.blk_map[bb];
//			UINT lba = seg.start_lba + bb;

//			CPageInfo* blk = m_pages->page(_pblk);
			//if (page == nullptr || page->nid == INVALID_BLK)
			//{	// invalid block�������
			//	FLUSH_SEGMENT_BLOCK_OUT(bb);
			//	continue;
			//}
			if (page->offset == INVALID_BLK)
			{	// node
				FLUSH_SEGMENT_BLOCK_OUT(bb);
				fprintf_s(log, "%d,%d,1,NODE,%d,xxxx\n", ss, bb, page->nid);
				continue;
			}
			// data block
			//if (pre_fid == page->nid && (pre_lblk + 1) == page->offset)
			//{	// merge
			//	pre_lblk++;
			//}
			//else
			//{	// reflush
			//	FLUSH_SEGMENT_BLOCK_OUT(bb);
			//	pre_fid = page->nid;
			//	start_blk = bb;
			//	start_lblk = page->offset;
			//	pre_lblk = page->offset;
			//}
		}
//		FLUSH_SEGMENT_BLOCK_OUT(BLOCK_PER_SEG);
	}
	fclose(log);
}



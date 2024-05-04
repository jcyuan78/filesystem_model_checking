///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
#include "pch.h"
#include "../include/f2fs_segment.h"
#include "../include/f2fs_simulator.h"

LOCAL_LOGGER_ENABLE(L"segment", LOGGER_LEVEL_DEBUGINFO);

#define SORTING HEAP


//PHY_BLK CF2fsSegmentManager::WriteBlockToSeg(const CF2fsSegmentManager::_BLK_TYPE& lblk, BLK_TEMP temp)
PHY_BLK CF2fsSegmentManager::WriteBlockToSeg(CPageAllocator::INDEX _pp, bool by_gc)
{
	// 计算page的温度，（实际温度，用于统计）
	CPageInfo* page = m_pages->page(_pp);
	page->ttemp = m_fs->GetBlockTemp(page);
	BLK_TEMP temp = m_fs->GetAlgorithmBlockTemp(page, page->ttemp);

	// 按照温度（算法温度）给page分配segment。

	SEG_T& cur_seg_id = m_cur_segs[temp];
	if (cur_seg_id == INVALID_BLK) cur_seg_id = AllocSegment(temp);
	//	seg_id = m_cur_seg;
	SEG_INFO& seg = m_segments[cur_seg_id];

	BLK_T blk_id = seg.cur_blk;
	page->media_write++;
	seg.blk_map[blk_id] = _pp;

	seg.valid_blk_nr++;
	seg.cur_blk++;

	InterlockedIncrement64(&m_health_info->m_total_media_write);
	InterlockedDecrement((UINT*)(&m_fs->m_free_blks));
	InterlockedIncrement(&m_health_info->m_physical_saturation);

	if (page->type == CPageInfo::PAGE_NODE) m_health_info->m_media_write_node++;
	else m_health_info->m_media_write_data++;

	SEG_T tar_seg = cur_seg_id;
	BLK_T tar_blk = blk_id;
	PHY_BLK old_blk = page->phy_blk;
	PHY_BLK phy_blk = PhyBlock(tar_seg, tar_blk);
	page->phy_blk = phy_blk;
	if (old_blk != INVALID_BLK) InvalidBlock(old_blk);

	// 由于WriteBlockToSeg()会在GC中被调用，目前的GC算法针对segment进行GC，GC后并不能清楚dirty标志。
	if (!by_gc) page->dirty = false;

	if (seg.cur_blk >= BLOCK_PER_SEG)
	{	// 当前segment已经写满
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
	GcPoolHeap<64, SEG_INFO > pool(m_segments);

#elif (SORTING == QSORT)
	GcPoolQuick<64, SEG_INFO >& pool = *m_gc_pool;
	pool.init();
#endif
	for (SEG_T ss = 0; ss < m_seg_nr; ss++)
	{
		SEG_INFO* seg = m_segments + ss;
		if (seg->cur_blk < BLOCK_PER_SEG) continue;  // 跳过未写满的segment
//		if (seg->valid_blk_nr == 0) LOG_DEBUG(L"0 valid segmen, seg id=%d", ss);
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
		SEG_INFO* src_seg = pool.Pop();
		if (src_seg == nullptr)
		{
			// GC pool中的block都以取完，如果有block被回收，则暂时停止GC。否则报错
			if (m_free_nr >= m_gc_lo) break;
			THROW_ERROR(ERR_APP, L"cannot find segment which has invalid block");
		}
		for (BLK_T bb = 0; bb < BLOCK_PER_SEG; ++bb)
		{
			CPageAllocator::INDEX _pblk = src_seg->blk_map[bb];
			CPageInfo* blk = m_pages->page(_pblk);
			if (blk == nullptr) continue;
			JCASSERT(blk->inode);
			// 注意：GC不应该改变block的温度，这里做一个检查
			BLK_TEMP before_temp = blk->ttemp;
			WriteBlockToSeg(_pblk, true);
			BLK_TEMP after_temp = blk->ttemp;
			JCASSERT(before_temp == after_temp);

			media_write_count++;
		}
		claimed_seg++;
	}
	LOG_TRACK(L"gc", L"SUM,free_before=%d,free_after=%d,released_seg=%d,src_nr=%d,host_write=%lld,%lld,media_write=%lld,%lld,delta=%d,media_write=%d",
		free_before_gc, m_free_nr, m_free_nr - free_before_gc, claimed_seg,
		host_write_before, m_health_info->m_total_host_write, media_write_before, m_health_info->m_total_media_write,
		(UINT)(m_health_info->m_total_media_write - media_write_before), media_write_count);

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

void CF2fsSegmentManager::DumpSegmentBlocks(const std::wstring& fn)
{
	// for merge
	FID pre_fid = INVALID_BLK;
	LBLK_T start_lblk = 0, pre_lblk = 0;
	BLK_T start_blk = 0;

	FILE* log = nullptr;
	_wfopen_s(&log, fn.c_str(), L"w+");
	if (log == nullptr) THROW_ERROR(ERR_APP, L"failed on opening file %s", fn.c_str());
	fprintf_s(log, "seg,blk,blk_num,valid,type,fid,lblk\n");
	for (SEG_T ss = 0; ss < m_seg_nr; ++ss)
	{
		SEG_INFO& seg = m_segments[ss];
		for (BLK_T bb = 0; bb < BLOCK_PER_SEG; ++bb)
		{
			CPageAllocator::INDEX _pblk = seg.blk_map[bb];
			CPageInfo* blk = m_pages->page(_pblk);
			if (blk == nullptr || blk->inode == INVALID_BLK)
			{	// invalid block：不输出
				FLUSH_SEGMENT_BLOCK_OUT(bb);
				continue;
			}
			if (blk->offset == INVALID_BLK)
			{	// node
				FLUSH_SEGMENT_BLOCK_OUT(bb);
				fprintf_s(log, "%d,%d,1,NODE,%d,xxxx\n", ss, bb, blk->inode);
				continue;
			}
			// data block
			if (pre_fid == blk->inode && (pre_lblk + 1) == blk->offset)
			{	// merge
				pre_lblk++;
			}
			else
			{	// reflush
				FLUSH_SEGMENT_BLOCK_OUT(bb);
				pre_fid = blk->inode;
				start_blk = bb;
				start_lblk = blk->offset;
				pre_lblk = blk->offset;
			}
		}
		FLUSH_SEGMENT_BLOCK_OUT(BLOCK_PER_SEG);
	}
	fclose(log);
}

bool CF2fsSegmentManager::InvalidBlock(SEG_T seg_id, BLK_T blk_id)
{
	bool free_seg = false;
	JCASSERT(seg_id < m_seg_nr);
	SEG_INFO& seg = m_segments[seg_id];
	seg.blk_map[blk_id] = INVALID_BLK;
	seg.valid_blk_nr--;
	if (seg.valid_blk_nr == 0 && seg.cur_blk >= BLOCK_PER_SEG)
	{
		FreeSegment(seg_id);
		free_seg = true;
	}
	InterlockedIncrement((UINT*)(&m_fs->m_free_blks));
	InterlockedDecrement(&m_health_info->m_physical_saturation);
	return free_seg;
}



bool CF2fsSegmentManager::InitSegmentManager(CF2fsSimulator* fs, SEG_T segment_nr, SEG_T gc_lo, SEG_T gc_hi)
{
	//InitSegmentManagerBase(fs, segment_nr, gc_lo, gc_hi, 0);
	m_fs = fs;
	m_pages = & m_fs->m_pages;
	m_health_info = & m_fs->m_health_info;

	m_seg_nr = SEG_NUM;
	//m_segments = new SEG_INFO[m_seg_nr];
	//m_free_segs = new SEG_T[m_seg_nr];

	// 初始化，如果blk_map指向block_id，初始化为0xFF，如果指向指针，初始化为0
	// 构建free链表
	memset(m_segments, 0, sizeof(SEG_INFO) * m_seg_nr);
	for (SEG_T ii = 0; ii < m_seg_nr; ++ii)
	{
		m_segments[ii].valid_blk_nr = ii+1;
		m_segments[ii].cur_blk = 0;
		m_segments[ii].seg_temp = BT_TEMP_NR;
//		m_free_segs[ii] = (DWORD)ii;
	}
	m_segments[m_seg_nr - 1].valid_blk_nr = INVALID_BLK;
	m_free_ptr = 0;
	m_free_nr = m_seg_nr;

	memset(m_cur_segs, 0xFF, sizeof(SEG_T) * BT_TEMP_NR);

//	m_free_head = 0;
//	m_free_tail = m_free_nr - 1;
//	m_health_info->m_free_seg = m_free_nr;

	m_gc_lo = gc_lo, m_gc_hi = gc_hi;

#ifdef ENABLE_FS_TRACE
	if (m_gc_trace)
	{
		// free_before: GG前的free segment，free_after: GC后的free segment, reclaimed_seg: GC释放的segment数量
		// src_seg: GC中使用的segment数量（和released相比较可以体现GC效率)
		// 通过GC前后health.media_write计算得到的media write block数量
		// GC过程中累加的media write block数量（这两者只是计算方法不同，理论上数值应该相同）
#if (GC_TRACE_TYPE==1)
		fprintf_s(m_gc_trace, "free_before,free_after,released_seg,src_sge,media_write,media_write_gc\n");
#elif (GC_TRACE_TYPE ==2)
		fprintf_s(m_gc_trace, "sort_cnt,low,high,min_vb,max_vb,sort_th,gc_secs\n");
#endif
	}
#endif

//	m_gc_pool = new GcPoolQuick<64, SEG_INFO >(m_seg_nr);
	return true;
}

void CF2fsSegmentManager::CopyFrom(const CF2fsSegmentManager& src, CF2fsSimulator* fs)
{
	memcpy_s(m_segments, sizeof(SEG_INFO)*SEG_NUM, src.m_segments, sizeof(SEG_INFO) * SEG_NUM);
//	memcpy_s(m_free_segs, sizeof(SEG_T)*SEG_NUM, src.m_free_segs, sizeof(SEG_T)*SEG_NUM);
//	m_free_head = src.m_free_head;
//	m_free_tail = src.m_free_tail;
	memcpy_s(m_cur_segs, sizeof(SEG_T)* BT_TEMP_NR, src.m_cur_segs, sizeof(SEG_T) * BT_TEMP_NR);
	m_seg_nr = src.m_seg_nr;
	m_free_nr = src.m_free_nr;
	m_free_ptr = src.m_free_ptr;
	m_gc_lo = src.m_gc_lo;
	m_gc_hi = src.m_gc_hi;

	m_fs = fs;
	m_pages = &fs->m_pages;
	m_health_info = &fs->m_health_info;
}

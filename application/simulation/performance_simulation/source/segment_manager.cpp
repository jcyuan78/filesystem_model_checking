///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#include "pch.h"
#include "segment_manager.h"
#include "lfs_simulator.h"

LOCAL_LOGGER_ENABLE(L"simulator.segment", LOGGER_LEVEL_DEBUGINFO);

#define HEAP_ALGORITHM

template <> void TypedInvalidBlock<LBLK_T>(LBLK_T& blk)
{
	blk = INVALID_BLK;
}

template <> void TypedInvalidBlock<LFS_BLOCK_INFO>(LFS_BLOCK_INFO& blk)
{
	blk.nid = INVALID_BLK;
	blk.offset = INVALID_BLK;
	blk.parent = nullptr;
	blk.parent_offset = 0;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// ==== Lfs Segment Manager

CLfsSegmentManager::~CLfsSegmentManager(void)
{
	LOG_STACK_TRACE();
}

PHY_BLK CLfsSegmentManager::WriteBlockToSeg(const LFS_BLOCK_INFO& lblk, BLK_TEMP temp)
{
	JCASSERT(temp < BT_TEMP_NR);
	SEG_T& cur_seg_id = m_cur_segs[temp];
	if (cur_seg_id == INVALID_BLK) cur_seg_id = AllocSegment(temp);
	//	seg_id = m_cur_seg;
	SEG_INFO<LFS_BLOCK_INFO>& seg = m_segments[cur_seg_id];
	BLK_T blk_id = seg.cur_blk;
	//seg.blk_map[blk_id].nid = file_index;
	//seg.blk_map[blk_id].offset = blk;
	seg.blk_map[blk_id] = lblk;
	seg.blk_map[blk_id].media_write++;

	seg.valid_blk_nr++;
	seg.cur_blk++;

	InterlockedIncrement64(&m_health->m_total_media_write);
	InterlockedDecrement(&m_health->m_free_blk);
	InterlockedIncrement(&m_health->m_physical_saturation);

	if (lblk.offset == INVALID_BLK) m_health->m_media_write_node++;
	else m_health->m_media_write_data++;

	SEG_T tar_seg = cur_seg_id;
	BLK_T tar_blk = blk_id;

	if (seg.cur_blk >= BLOCK_PER_SEG)
	{	// 当前segment已经写满
		cur_seg_id = INVALID_BLK;
	}
	return  PhyBlock(tar_seg, tar_blk);
}

void CLfsSegmentManager::GarbageCollection(void)
{
	LOG_STACK_TRACE();
	GcPool<64, SEG_INFO<LFS_BLOCK_INFO> > pool(m_segments);
	for (SEG_T ss = 0; ss < m_seg_nr; ss++)
	{
		SEG_INFO<LFS_BLOCK_INFO>& seg = m_segments[ss];
		if (seg.cur_blk < BLOCK_PER_SEG) continue;  // 跳过未写满的segment
		pool.Push(ss);
	}

	pool.LargeToSmall();
	SEG_T free_before_gc = m_free_nr;
	SEG_T claimed_seg = 0;

	LONG64 media_write_before = m_health->m_total_media_write;
	LONG64 host_write_before = m_health->m_total_host_write;
	UINT media_write_count = 0;
	while (m_free_nr < m_gc_hi)
	{
		SEG_T min_seg = pool.Pop();
		SEG_INFO<LFS_BLOCK_INFO>& src_seg = m_segments[min_seg];
		if (min_seg == INVALID_BLK)
		{
			// GC pool中的block都以取完，如果有block被回收，则暂时停止GC。否则报错
			if (m_free_nr > m_gc_lo) break;
			THROW_ERROR(ERR_APP, L"cannot find segment which has invalid block");
		}
		for (BLK_T bb = 0; bb < BLOCK_PER_SEG; ++bb)
		{
			LFS_BLOCK_INFO& blk = src_seg.blk_map[bb];
			if (blk.nid == INVALID_BLK) continue;
			PHY_BLK* parent_entry = nullptr;
			if (blk.parent && blk.parent->m_type == inode_info::NODE_INDEX)
			{
				parent_entry = &(blk.parent->index_blk.m_index[blk.parent_offset]);
			}

			PHY_BLK old_phy = PhyBlock(min_seg, bb);
			//LOG_TRACK(L"gc", L"MOVE,fid=%d,blk=%d,src=%X,src_seg=%d,src_blk=%d", blk.nid, blk.offset, old_phy, min_seg, bb);
			PHY_BLK new_blk = MoveBlock(min_seg, bb, BT_HOT__DATA);
			media_write_count++;

			LFS_BLOCK_INFO& new_lblk = get_block(new_blk);
			if (new_lblk.offset == INVALID_BLK)
			{	// node block
				//LOG_TRACK(L"inode", L"MOVE,fid=%d,new_phy=%X,old_phy=%X", new_lblk.nid, new_blk, old_phy);
				inode_info * inode = m_inodes->get_node(new_lblk.nid);
				JCASSERT(inode);
				inode->m_phy_blk = new_blk;
			}
			else
			{	// data block

			}
			if (parent_entry) *parent_entry = new_blk;
		}
		claimed_seg++;
	}
	LOG_TRACK(L"gc", L"SUM,free_before=%d,free_after=%d,released_seg=%d,src_nr=%d,host_write=%lld,%lld,media_write=%lld,%lld,delta=%d,media_write=%d",
		free_before_gc, m_free_nr, m_free_nr - free_before_gc, claimed_seg, 
		host_write_before, m_health->m_total_host_write, media_write_before, m_health->m_total_media_write,
		(UINT)(m_health->m_total_media_write-media_write_before), media_write_count);
	if (m_gc_trace)
	{
		//fprintf_s(m_gc_trace, "free_before,free_after,reclaimed,src_sge,media_write\n");
		fprintf_s(m_gc_trace, "%d,%d,%d,%d,%d,%d\n",
			free_before_gc, m_free_nr, m_free_nr - free_before_gc, claimed_seg,
			(UINT)(m_health->m_total_media_write - media_write_before), media_write_count);
	}
}

#define FLUSH_SEGMENT_BLOCK_OUT(_blk)	\
	if (pre_fid != INVALID_BLK) {	\
		fprintf_s(log, "%d,%d,%d,1,DATA,%d,%d\n", ss, start_blk, (_blk-start_blk), pre_fid, start_lblk);	\
		pre_fid = INVALID_BLK; start_blk = _blk; start_lblk = 0; pre_lblk = 0; }

void CLfsSegmentManager::DumpSegmentBlocks(const std::wstring& fn)
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
		SEG_INFO<LFS_BLOCK_INFO>& seg = m_segments[ss];
		for (BLK_T bb = 0; bb < BLOCK_PER_SEG; ++bb)
		{
			LFS_BLOCK_INFO& blk = seg.blk_map[bb];
			if (blk.nid == INVALID_BLK)
			{	// invalid block：不输出
				FLUSH_SEGMENT_BLOCK_OUT(bb);
				continue;
			}
			if (blk.offset == INVALID_BLK)
			{	// node
				FLUSH_SEGMENT_BLOCK_OUT(bb);
				fprintf_s(log, "%d,%d,1,NODE,%d,xxxx\n", ss,bb,blk.nid);
				continue;
			}
			// data block
			if (pre_fid == blk.nid && (pre_lblk + 1) == blk.offset)
			{	// merge
				pre_lblk++;
			}
			else
			{	// reflush
				FLUSH_SEGMENT_BLOCK_OUT(bb);
				pre_fid = blk.nid;
				start_blk = bb;
				start_lblk = blk.offset;
				pre_lblk = blk.offset;
			}
		}
		FLUSH_SEGMENT_BLOCK_OUT(BLOCK_PER_SEG);

	}
	fclose(log);
}

bool CLfsSegmentManager::InitSegmentManager(SEG_T segment_nr, SEG_T gc_lo, SEG_T gc_hi, int init_val)
{
	CSegmentManagerBase<LFS_BLOCK_INFO>::InitSegmentManager(segment_nr, gc_lo, gc_hi, 0xFFFFFFFF);
	if (m_gc_trace)
	{
		// free_before: GG前的free segment，free_after: GC后的free segment, reclaimed_seg: GC释放的segment数量
		// src_seg: GC中使用的segment数量（和released相比较可以体现GC效率)
		// 通过GC前后health.media_write计算得到的media write block数量
		// GC过程中累加的media write block数量（这两者只是计算方法不同，理论上数值应该相同）
		fprintf_s(m_gc_trace, "free_before,free_after,released_seg,src_sge,media_write,media_write_gc\n");
	}
	return true;
}


///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#include "pch.h"
#include "segment_manager.h"

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
	//seg.blk_map[blk_id].nid = fid;
	//seg.blk_map[blk_id].offset = blk;
	seg.blk_map[blk_id] = lblk;

	seg.valid_blk_nr++;
	seg.cur_blk++;

	InterlockedIncrement64(&m_health->m_total_media_write);
	InterlockedDecrement(&m_health->m_free_blk);
	InterlockedIncrement(&m_health->m_physical_saturation);

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
	GcPool<32, LFS_BLOCK_INFO> pool(m_segments);
	for (SEG_T ss = 0; ss < m_seg_nr; ss++)
	{
		SEG_INFO<LFS_BLOCK_INFO>& seg = m_segments[ss];
		if (seg.cur_blk < BLOCK_PER_SEG) continue;  // 跳过未写满的segment
		pool.Push(ss);
	}

	pool.LargeToSmall();

	while (m_free_nr < GC_THREAD_END)
	{
		SEG_T min_seg = pool.Pop();
		SEG_INFO<LFS_BLOCK_INFO>& src_seg = m_segments[min_seg];
		if (min_seg == INVALID_BLK) THROW_ERROR(ERR_APP, L"cannot find segment has invalid block");
		for (BLK_T bb = 0; bb < BLOCK_PER_SEG; ++bb)
		{
			LFS_BLOCK_INFO& blk = src_seg.blk_map[bb];
			if (blk.nid == INVALID_BLK) continue;
			MoveBlock(min_seg, bb, BT_HOT__DATA);
		}
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


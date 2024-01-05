///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
#include "pch.h"
#include "ssd_simulator.h"


LOCAL_LOGGER_ENABLE(L"simulator.ssd", LOGGER_LEVEL_DEBUGINFO);



// == Single Head Segment Manager

CSingleHeadSM::CSingleHeadSM(size_t seg_nr, size_t cap)
{
	//m_seg_nr = (DWORD)seg_nr;
	//m_segments = new SEG_INFO[seg_nr];
	//m_free_segs = new DWORD[seg_nr];
	//// 初始化
	//memset(m_segments, 0xFF, sizeof(SEG_INFO)*m_seg_nr);
	//for (size_t ii = 0; ii < m_seg_nr; ++ii)
	//{
	//	m_segments[ii].valid_blk_nr = 0;
	//	m_segments[ii].cur_blk = 0;
	//	m_free_segs[ii] = (DWORD)ii;
	//}
	//m_cur_seg = INVALID_BLK;

	//m_free_nr = m_seg_nr;
	//m_free_head = 0;
	//m_free_tail = m_free_nr - 1;

	m_blk_nr = cap / 8;
	//m_l2p_map = new DWORD[m_blk_nr];
	m_l2p_map = new LBLOCK_INFO[m_blk_nr];
	memset(m_l2p_map, 0xFF, sizeof(LBLOCK_INFO) * m_blk_nr);

	m_total_host_write = 0;
	m_total_media_write = 0;
//	m_free_blk = seg_nr * BLOCK_PER_SEG;
	m_next_update = 0;
}

CSingleHeadSM::~CSingleHeadSM(void)
{
	delete[] m_l2p_map;
	if (m_log_invalid_trace)
	{
		fclose(m_log_invalid_trace);
	}
}

bool CSingleHeadSM::Initialize(const boost::property_tree::wptree& config)
{
	m_blk_nr = config.get<size_t>(L"sectors") / 8;
	SEG_T seg_nr = (SEG_T)((float)m_blk_nr * config.get<float>(L"over_provision") / BLOCK_PER_SEG);
	m_segment.InitSegmentManager(seg_nr, 3, 5);

	LOG_DEBUG(L"logic blk_nr=%d, logical seg_nr=%d, phy seg_n=%dr", m_blk_nr, m_blk_nr / BLOCK_PER_SEG, seg_nr);

	//m_cur_seg = INVALID_BLK;

	//	m_blk_nr = cap / 8;
		//m_l2p_map = new DWORD[m_blk_nr];
	m_l2p_map = new LBLOCK_INFO[m_blk_nr];
	memset(m_l2p_map, 0xFF, sizeof(LBLOCK_INFO) * m_blk_nr);

	m_total_host_write = 0;
	m_total_media_write = 0;
	//m_free_blk = seg_nr * BLOCK_PER_SEG;
	m_next_update = 0;

	return true;
}

void CSingleHeadSM::SetLogFile(const std::wstring& fn)
{
	_wfopen_s(&m_log_invalid_trace, fn.c_str(), L"w+");
	if (m_log_invalid_trace == nullptr) THROW_ERROR(ERR_APP, L"failed on create log file %s", fn.c_str());
	fprintf_s(m_log_invalid_trace, "Event,LBlock,BlockNr,HostWrite,MediaWrite,WAF,RunningWAF,FreeSeg,FreeBlock\n");
}

bool CSingleHeadSM::WriteSector(size_t lba, size_t secs, BLK_TEMP temp)
{
	LBLK_T start_blk, end_blk;
	LbaToBlock(start_blk, end_blk, lba, secs);

	JCASSERT(end_blk <= m_blk_nr);
	LOG_DEBUG_(1, L"write blocks: %X ~ %X", start_blk, end_blk);

	static size_t last_media_write = 0, last_host_write = 0;

	// 分配phy block
	for (; start_blk < end_blk; start_blk++)
	{
		// 旧的block
		LBLOCK_INFO& lblk_info = m_l2p_map[start_blk];
		PHY_BLK old_blk = lblk_info.phy_blk;
		if (old_blk == INVALID_BLK)
		{
			lblk_info.host_write = 0;
			lblk_info.total_write = 0;
		}

		// 分配新的block
		PHY_BLK phy_blk = m_segment.WriteBlockToSeg(start_blk, BT_HOT__DATA);
		// 记录新的block
		lblk_info.total_write++;
		m_total_media_write++;

		lblk_info.phy_blk = phy_blk;
		lblk_info.temp = temp;
		lblk_info.host_write++;

		// 旧的block无效
		if (old_blk != INVALID_BLK) 	{	m_segment.InvalidBlock(old_blk);	}

		m_total_host_write++;
	}
	// check GC
	GarbageCollection();

	if (m_total_host_write >= m_next_update)
	{
		wprintf_s(L"host_write=%lld, media_write=%lld, WAF=%.1f, free_seg=%d, free_blk=%d\n", m_total_host_write, m_total_media_write, (double)m_total_media_write / (double)m_total_host_write, m_segment.get_free_nr(), m_segment.get_free_nr() * BLOCK_PER_SEG);

		size_t running_host_write = m_total_host_write - last_host_write;

		fprintf_s(m_log_invalid_trace, "WriteSector,%lld,%lld,%lld,%lld,%.1f,%.1f,%d, %d\n", lba / 8, end_blk - lba / 8, m_total_host_write, m_total_media_write,
			/*累计WAF*/(double)m_total_media_write / (double)m_total_host_write,
			/*实时WAF*/(running_host_write) ? ((double)(m_total_media_write - last_media_write) / (double)running_host_write) : 0,
			m_segment.get_free_nr(), m_segment.get_free_nr() * BLOCK_PER_SEG);
		last_host_write = m_total_host_write;
		last_media_write = m_total_media_write;
		m_next_update += 4096;
	}

	return true;
}

void CSingleHeadSM::DumpL2PMap(const std::wstring& fn)
{
	FILE* file = nullptr;
	_wfopen_s(&file, fn.c_str(), L"w+");

	fprintf_s(file, "LogicBlock,Segment,Block,Temp,TotalWrite,HostWrite,BlockWAF\n");
	for (DWORD bb = 0; bb < m_blk_nr; ++bb)
	{
		SEG_T seg_id;
		BLK_T blk_id;
		LBLOCK_INFO& lblk_info = m_l2p_map[bb];
		BlockToSeg(seg_id, blk_id, lblk_info.phy_blk);
		fprintf_s(file, "%d,%d,%d,%d,%d,%d,%.2f\n", bb, seg_id, blk_id, lblk_info.temp, lblk_info.total_write, lblk_info.host_write, (double)lblk_info.total_write / (double)lblk_info.host_write);
		// Sanity Check
		SEG_INFO<LBLK_T>& seg = m_segment.get_segment(seg_id);
		DWORD p2l = seg.blk_map[blk_id];
		if (p2l != bb)
		{
			THROW_ERROR(ERR_APP, L"L2P mismatch, lblk=%X, seg=%X, blk=%X, p2l=%X", bb, seg_id, blk_id, p2l);
		}
	}
	fclose(file);
}

void CSingleHeadSM::CheckingColdDataBySeg(const std::wstring& fn)
{
	FILE* file = nullptr;
	_wfopen_s(&file, fn.c_str(), L"w+");
	fprintf_s(file, "Segment,Valid_Block,Hot_Block,Cold_Block,Invalid_Block\n");
	for (SEG_T ss = 0; ss < m_segment.get_seg_nr(); ++ss)
	{
		SEG_INFO<LBLK_T>& seg = m_segment.get_segment(ss);	// m_segments[ss];
		int cold_blk = 0, hot_blk = 0;
		for (BLK_T bb = 0; bb < BLOCK_PER_SEG; ++bb)
		{
			DWORD lblk = seg.blk_map[bb];
			if (lblk == INVALID_BLK)continue;
			if (lblk < m_blk_nr / 2) hot_blk++;
			else cold_blk++;
			// Sanity Check
			DWORD phy_blk = PhyBlock(ss, bb);
			if (m_l2p_map[lblk].phy_blk != phy_blk)
			{
				THROW_ERROR(ERR_APP, L"P2L mismatch, lblk=%X, seg=%X, blk=%X, phy_blk=%X", lblk, ss, bb, phy_blk);
			}
		}
		fprintf_s(file, "%d,%d,%d,%d,%d\n", ss, seg.valid_blk_nr, hot_blk, cold_blk, BLOCK_PER_SEG - seg.valid_blk_nr);
	}

}

void CSingleHeadSM::GarbageCollection(void)
{
	// check GC
	if (m_segment.get_free_nr() > GC_THREAD_START) return;
	LOG_STACK_TRACE();
#ifdef HEAP_ALGORITHM
	// 按照segment的valid blk nr堆排序
	GcPool<32, SEG_INFO<LBLK_T> > pool(m_segment.m_segments);
	for (SEG_T ss = 0; ss < m_segment.get_seg_nr(); ss++)
	{
		SEG_INFO<LBLK_T>& seg = m_segment.get_segment(ss);
		if (seg.cur_blk < BLOCK_PER_SEG) continue;  // 跳过未写满的segment
		pool.Push(ss);
	}
	//	pool.ShowHeap(1);
	pool.LargeToSmall();
	//	pool.ShowHeap(0);
#endif

	while (m_segment.get_free_nr() <= GC_THREAD_END)
	{
		// GC 一个segment
#ifdef HEAP_ALGORITHM
		SEG_T min_seg = pool.Pop();
#else
		// (1) 查找segment
		// 
		SEG_T min_seg = INVALID_BLK;
		DWORD min_valid = BLOCK_PER_SEG;
		for (SEG_T ss = 0; ss < m_segment.get_seg_nr(); ss++)
		{
			//LOG_DEBUG(L"segment: %X, cur_blk=%X, valid_nr=%d", ss, m_segments[ss].cur_blk, m_segments[ss].valid_blk_nr);
			if (m_segments[ss].cur_blk < BLOCK_PER_SEG || ss == m_cur_seg) continue;
			if (m_segments[ss].valid_blk_nr < min_valid)
			{
				//LOG_DEBUG(L"found invalid block, seg=%X, valid_nr=%d", ss, m_segments[ss].valid_blk_nr);
				min_seg = ss;
				min_valid = m_segments[ss].valid_blk_nr;
			}
		}
#endif

		SEG_INFO<LBLK_T>& src_seg = m_segment.get_segment(min_seg);	// m_segments[min_seg];

		LOG_DEBUG(L"do gc, total_seg=%d, free_nr=%d, src_seg=%d, valid_blk=%d", m_segment.get_seg_nr(), m_segment.get_free_nr(), min_seg, src_seg.valid_blk_nr);
		if (min_seg == INVALID_BLK) THROW_ERROR(ERR_APP, L"cannot find segment has invalid block");
		// 将segment中的block移动到m_cur_seg中
		for (BLK_T bb = 0; bb < BLOCK_PER_SEG; bb++)
		{
			if (src_seg.blk_map[bb] == INVALID_BLK) continue;
			MoveBlock(min_seg, bb, BT_HOT__DATA);
		}
	}
}

PHY_BLK CSsdSegmentManager::WriteBlockToSeg(const LBLK_T& lblk, BLK_TEMP temp)
{
	JCASSERT(temp < BT_TEMP_NR);
	SEG_T& cur_seg_id = m_cur_segs[temp];
	if (cur_seg_id == INVALID_BLK) cur_seg_id = AllocSegment(temp);
	//	seg_id = m_cur_seg;
	SEG_INFO<LBLK_T>& seg = m_segments[cur_seg_id];
	BLK_T blk_id = seg.cur_blk;
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

void CSingleHeadSM::MoveBlock(/*SEG_T& tar_seg,*/ SEG_T src_seg, BLK_T src_blk, BLK_TEMP temp)
{
	//PHY_BLK m_segment.MoveBlock(src_seg, src_blk, temp);


#ifdef _SANITY_CHECK
	SEG_INFO<LBLK_T>& seg = m_segment.get_segment(src_seg);	//m_segments[src_seg];
	LBLK_T lblk = seg.blk_map[src_blk];
	// 一致性检查
	PHY_BLK _bb = PhyBlock(src_seg, src_blk);
	if (m_l2p_map[lblk].phy_blk != _bb) THROW_ERROR(ERR_APP, L"P2L mismatch, lblk=%X, seg=%X, blk=%X, phy_blk=%X", lblk, src_seg, src_blk, _bb);
#endif
	PHY_BLK tar = m_segment.MoveBlock(src_seg, src_blk, temp);
	// 新的segment中写入 
//	PHY_BLK tar = m_segment.WriteBlockToSeg(tar_seg, lblk, temp);
	m_l2p_map[lblk].total_write++;
	m_total_media_write++;

	m_l2p_map[lblk].phy_blk = tar;// PhyBlock(tar_seg, tar_blk);
	// 无效旧的block
//	m_segment.InvalidBlock(src_seg, src_blk);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// == Multi Head Segment Manager

bool CMultiHeadSM::Initialize(const boost::property_tree::wptree& config)
{
	CSingleHeadSM::Initialize(config);
	//	m_thread_nr = config.get<size_t>(L"thread_nr");

	//	m_cur_segs = new SEG_T[m_thread_nr];
//	memset(m_cur_segs, 0xFF, sizeof(SEG_T) * BT_TEMP_NR);
	std::wstring policy = config.get<std::wstring>(L"temp_policy");
	if (policy == L"by_host") m_temp_policy = BY_HOST;
	else if (policy == L"by_count") m_temp_policy = BY_COUNT;

	return true;
}

CMultiHeadSM::~CMultiHeadSM(void)
{
	//	delete m_cur_segs;
}


bool CMultiHeadSM::WriteSector(size_t lba, size_t secs, BLK_TEMP temp)
{
	// lba => block
	//size_t end_lba = lba + secs;
	//size_t start_blk = lba / 8;
	//size_t end_blk = ROUND_UP_DIV(end_lba, 8);
	LBLK_T start_blk, end_blk;
	LbaToBlock(start_blk, end_blk, lba, secs);
	JCASSERT(end_blk <= m_blk_nr);
	LOG_DEBUG_(1, L"write blocks: %X ~ %X", start_blk, end_blk);
	static size_t last_media_write = 0, last_host_write = 0;


	// 分配phy block
	for (; start_blk < end_blk; start_blk++)
	{
		// 旧的block
		LBLOCK_INFO& lblk_info = m_l2p_map[start_blk];
		PHY_BLK old_blk = lblk_info.phy_blk;
		if (old_blk == INVALID_BLK)
		{
			lblk_info.host_write = 0;
			lblk_info.total_write = 0;
		}
		// 决定数据的温度
		BLK_TEMP temp_ix = TemperaturePolicy(start_blk, temp);
		// 分配新的block
		//SEG_T seg_id;
		PHY_BLK phy_blk = m_segment.WriteBlockToSeg(start_blk, temp_ix);
		lblk_info.total_write++;
		m_total_media_write++;

		// 记录新的block
		lblk_info.phy_blk = phy_blk; //PhyBlock(seg_id, blk_id);
		lblk_info.temp = temp;
		lblk_info.host_write++;

		// 旧的block无效
		if (old_blk != INVALID_BLK)
		{
			//SEG_T seg_id;
			//BLK_T blk_id;

			//BlockToSeg(seg_id, blk_id, old_blk);
			m_segment.InvalidBlock(old_blk);
		}
		m_total_host_write++;
	}
	// check GC
	GarbageCollection();

	if (m_total_host_write >= m_next_update)
	{
		wprintf_s(L"host_write=%lld, media_write=%lld, WAF=%.1f, free_seg=%d, free_blk=%d\n", m_total_host_write, m_total_media_write, (double)m_total_media_write / (double)m_total_host_write, m_segment.get_free_nr(), m_segment.get_free_nr() * BLOCK_PER_SEG);

		size_t running_host_write = m_total_host_write - last_host_write;

		fprintf_s(m_log_invalid_trace, "WriteSector,%lld,%lld,%lld,%lld,%.1f,%.1f,%d,%d\n", lba / 8, end_blk - lba / 8, m_total_host_write, m_total_media_write,
			/*累计WAF*/(double)m_total_media_write / (double)m_total_host_write,
			/*实时WAF*/(running_host_write) ? ((double)(m_total_media_write - last_media_write) / (double)running_host_write) : 0,
			m_segment.get_free_nr(), m_segment.get_free_nr() * BLOCK_PER_SEG);
		last_host_write = m_total_host_write;
		last_media_write = m_total_media_write;

		m_next_update += 4096;
	}

	return true;
}

BLK_TEMP CMultiHeadSM::TemperaturePolicy(LBLK_T lblk, BLK_TEMP temp)
{
	if (m_temp_policy == BY_HOST)
	{
		if (temp == BT_COLD_DATA) return BT_COLD_DATA;
		else return BT_HOT__DATA;
	}
	else if (m_temp_policy == BY_COUNT)
	{
		int count = m_l2p_map[lblk].host_write;
		if (count > 5) return BT_HOT__DATA;
		else return BT_COLD_DATA;
	}
	return BT_HOT__DATA;

}

void CMultiHeadSM::GarbageCollection(void)
{
	// check GC
	if (m_segment.get_free_nr() > GC_THREAD_START) return;
	LOG_STACK_TRACE();
#ifdef HEAP_ALGORITHM
	// 按照segment的valid blk nr堆排序
	GcPool<32, SEG_INFO<LBLK_T> > pool(m_segment.m_segments);
	for (SEG_T ss = 0; ss < m_segment.get_seg_nr(); ss++)
	{
		SEG_INFO<LBLK_T>& seg = m_segment.get_segment(ss);
		if (seg.cur_blk < BLOCK_PER_SEG) continue;  // 跳过未写满的segment
		pool.Push(ss);
	}
	pool.LargeToSmall();
#endif

	while (m_segment.get_free_nr() <= GC_THREAD_END)
	{
		// GC 一个segment
#ifdef HEAP_ALGORITHM
		SEG_T min_seg = pool.Pop();
#else
		// (1) 查找segment
		// 
		SEG_T min_seg = INVALID_BLK;
		DWORD min_valid = BLOCK_PER_SEG;
		for (SEG_T ss = 0; ss < m_segment.get_seg_nr(); ss++)
		{
			//LOG_DEBUG(L"segment: %X, cur_blk=%X, valid_nr=%d", ss, m_segments[ss].cur_blk, m_segments[ss].valid_blk_nr);
			if (m_segments[ss].cur_blk < BLOCK_PER_SEG || ss == m_cur_seg) continue;
			if (m_segments[ss].valid_blk_nr < min_valid)
			{
				//LOG_DEBUG(L"found invalid block, seg=%X, valid_nr=%d", ss, m_segments[ss].valid_blk_nr);
				min_seg = ss;
				min_valid = m_segments[ss].valid_blk_nr;
			}
		}
#endif

		if (min_seg == INVALID_BLK) THROW_ERROR(ERR_APP, L"cannot find segment has invalid block");
		// 将segment中的block移动到m_cur_seg中
		SEG_INFO<LBLK_T>& src_seg = m_segment.get_segment(min_seg);	// m_segments[min_seg];
		LOG_DEBUG(L"do gc, total_seg=%d, free_nr=%d, src_seg=%d, valid_blk=%d, src_temp=%d", 
			m_segment.get_seg_nr(), m_segment.get_free_nr(), min_seg, src_seg.valid_blk_nr,	src_seg.seg_temp);
		for (BLK_T bb = 0; bb < BLOCK_PER_SEG; bb++)
		{
			LBLK_T lblk = src_seg.blk_map[bb];
			if (lblk == INVALID_BLK) continue;
			LBLOCK_INFO& lblk_info = m_l2p_map[lblk];
			BLK_TEMP temp_ix = TemperaturePolicy(lblk, lblk_info.temp);

			MoveBlock(/*m_cur_segs[temp_ix],*/ min_seg, bb, temp_ix);
			//LOG_DEBUG(L"move logic block, lblk=%d, temp=%d, src_seg=%d, src_blk=%d, dst_blk=%d",
			//	lblk, temp_ix, min_seg, bb, m_cur_segs[temp_ix]);
		}
	}

	// 按照cold和hot统计每个block的valid data. 统计每个segment含有的cold, hot和invalid的比例
	size_t cold_seg_nr = 0, hot_seg_nr = 0, free_seg_nr = 0;
	for (SEG_T ss = 0; ss < m_segment.get_seg_nr(); ss++)
	{
		SEG_INFO<LBLK_T>& seg = m_segment.get_segment(ss);	// m_segments[ss];
		size_t cold_blk_nr = 0, hot_blk_nr = 0, invalid_blk_nr = 0;
		for (BLK_T bb = 0; bb < BLOCK_PER_SEG; ++bb)
		{
			DWORD lblk = seg.blk_map[bb];
			if (lblk == INVALID_BLK)
			{
				invalid_blk_nr++;
				continue;
			}
			//			if (m_l2p_map[lblk].temp == BT_HOT__DATA) hot_blk_nr++;
			if (lblk < m_blk_nr / 2) hot_blk_nr++;
			else cold_blk_nr++;

		}
		LOG_DEBUG(L"seg=%d, temp=%d, cold=%lld, hot=%lld, invalid=%lld, cur_blk=%d", ss, seg.seg_temp, cold_blk_nr, hot_blk_nr, invalid_blk_nr, seg.cur_blk);
		if (seg.seg_temp == BT_HOT__DATA) hot_seg_nr++;
		else if (seg.seg_temp == BT_COLD_DATA) cold_seg_nr++;
		else free_seg_nr++;
	}
	LOG_DEBUG(L"cold_seg_nr=%lld, hot_seg_nr=%lld, free_seg_nr=%lld", cold_seg_nr, hot_seg_nr, free_seg_nr);
}

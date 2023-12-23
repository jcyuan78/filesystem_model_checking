///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "fs_comm.h"

class CPageInfo;
class CInodeManager_;
class CF2fsSimulator;

//template <> void TypedInvalidBlock<CPageInfo*>(CPageInfo*& blk)
//{
//	blk = nullptr;
//}

//template <typename BLOCK_TYPE>
class CF2fsSegmentManager
{
public:
	typedef CPageInfo* _BLK_TYPE;

	CF2fsSegmentManager(void) { }
	virtual ~CF2fsSegmentManager(void)
	{
		delete[] m_free_segs;
		delete[] m_segments;
	}
public:
	// 查找一个空的segment
	SEG_T AllocSegment(BLK_TEMP temp)
	{
		if ((m_free_head == m_free_tail) || m_free_nr == 0)
		{
			THROW_ERROR(ERR_APP, L"no enough free segment");
		}
		SEG_T new_seg = m_free_segs[m_free_head];
		m_free_head++;
		if (m_free_head >= m_seg_nr) m_free_head = 0;
		m_free_nr--;
		InterlockedDecrement(&m_health->m_free_seg);

		m_segments[new_seg].seg_temp = temp;
		return new_seg;
	}

	// 回收一个segment
	void FreeSegment(SEG_T seg_id)
	{
		m_free_tail++;
		if (m_free_tail >= m_seg_nr) m_free_tail = 0;
		if (m_free_tail == m_free_head) { THROW_ERROR(ERR_APP, L"free buffer full"); }
		SEG_INFO<CPageInfo *>& seg = m_segments[seg_id];
		memset(&seg, 0, sizeof(SEG_INFO<CPageInfo *>));

		seg.valid_blk_nr = 0;
		seg.cur_blk = 0;

		m_free_segs[m_free_tail] = seg_id;
		m_free_nr++;
		InterlockedIncrement(&m_health->m_free_seg);
	}

	bool InvalidBlock(PHY_BLK phy_blk)
	{
		if (phy_blk == INVALID_BLK) return false;
		SEG_T seg_id; BLK_T blk_id;
		BlockToSeg(seg_id, blk_id, phy_blk);
		return InvalidBlock(seg_id, blk_id);
	}

	bool InvalidBlock(SEG_T seg_id, BLK_T blk_id)
	{
		bool free_seg = false;
		JCASSERT(seg_id < m_seg_nr);
		SEG_INFO<CPageInfo *>& seg = m_segments[seg_id];
//		TypedInvalidBlock<CPageInfo *>(seg.blk_map[blk_id]);
		seg.blk_map[blk_id] = nullptr;
		seg.valid_blk_nr--;
		if (seg.valid_blk_nr == 0 && seg.cur_blk >= BLOCK_PER_SEG)
		{
			FreeSegment(seg_id);
			free_seg = true;
		}
		InterlockedIncrement(&m_health->m_free_blk);
		InterlockedDecrement(&m_health->m_physical_saturation);
		return free_seg;
	}
	virtual bool InitSegmentManager(SEG_T segment_nr, SEG_T gc_lo, SEG_T gc_hi, int init = 0);

	bool InitSegmentManagerBase(SEG_T segment_nr, SEG_T gc_lo, SEG_T gc_hi, int init_val = 0)
	{
		m_seg_nr = segment_nr;
		m_segments = new SEG_INFO<CPageInfo *>[m_seg_nr];
		m_free_segs = new SEG_T[m_seg_nr];
		// 初始化，如果blk_map指向block_id，初始化为0xFF，如果指向指针，初始化为0
		memset(m_segments, 0, sizeof(SEG_INFO<CPageInfo *>) * m_seg_nr);
		for (size_t ii = 0; ii < m_seg_nr; ++ii)
		{
			m_segments[ii].valid_blk_nr = 0;
			m_segments[ii].cur_blk = 0;
			m_free_segs[ii] = (DWORD)ii;
		}
		memset(m_cur_segs, 0xFF, sizeof(SEG_T) * BT_TEMP_NR);

		m_free_nr = m_seg_nr;
		m_free_head = 0;
		m_free_tail = m_free_nr - 1;
		m_health->m_free_seg = m_free_nr;

		m_gc_lo = gc_lo, m_gc_hi = gc_hi;
		return true;
	}


	// 将src_seg, src_blk中的block，移动到temp相关的当前segment，返回目标(segment,block)
	PHY_BLK MoveBlock(SEG_T src_seg, BLK_T src_blk, BLK_TEMP temp)
	{
		SEG_INFO<CPageInfo *>& seg = m_segments[src_seg];
		CPageInfo *& lblk = seg.blk_map[src_blk];
		// 新的segment中写入 
		PHY_BLK tar = WriteBlockToSeg(lblk, temp);
		// 无效旧的block
		InvalidBlock(src_seg, src_blk);
		return tar;
	}

	SEG_T get_seg_nr(void) const { return m_seg_nr; }
	SEG_T get_free_nr(void) const { return m_free_nr; }
	SEG_INFO<CPageInfo *>& get_segment(SEG_T id) const
	{
		JCASSERT(id < m_seg_nr);
		return m_segments[id];
	}

	CPageInfo *& get_block(PHY_BLK phy_blk)
	{
		SEG_T seg_id; BLK_T blk_id;
		BlockToSeg(seg_id, blk_id, phy_blk);
		SEG_INFO<CPageInfo *>& seg = m_segments[seg_id];
		return seg.blk_map[blk_id];
	}

	void SetHealth(FsHealthInfo* health) { m_health = health; } 

	// 写入data block到segment, file_index 文件id, blk：文件中的相对block，temp温度
	void CheckGarbageCollection(CF2fsSimulator* fs)
	{
		if (m_free_nr < m_gc_lo) GarbageCollection(fs);
	}
	virtual PHY_BLK WriteBlockToSeg(const _BLK_TYPE & lblk, BLK_TEMP temp);
	virtual void GarbageCollection(CF2fsSimulator * fs);
	void DumpSegmentBlocks(const std::wstring& fn);

	friend class CF2fsSimulator;
	FILE* m_gc_trace;

protected:
	CInodeManager_* m_inodes = nullptr;

public:	// 临时措施，需要考虑如何处理GcPool。(1)将GC作为算法器放入segment management中，(2)提供获取GcPool的接口
	SEG_INFO<CPageInfo *>* m_segments = nullptr;

protected:
	friend class CSingleLogSimulator;
	SEG_T m_cur_segs[BT_TEMP_NR];
	SEG_T m_seg_nr = 0, m_free_nr = 0;
	FsHealthInfo* m_health = nullptr;
	SEG_T m_gc_lo, m_gc_hi;
private:
	SEG_T m_free_head = 0, m_free_tail = 0;
	SEG_T* m_free_segs = nullptr;
};

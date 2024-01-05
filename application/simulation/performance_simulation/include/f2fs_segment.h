///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "fs_comm.h"

class CPageInfo;
class CInodeManager_;
class CF2fsSimulator;


class CF2fsSegmentManager
{
public:
	typedef CPageInfo* _BLK_TYPE;

	CF2fsSegmentManager(void) { }
	virtual ~CF2fsSegmentManager(void)
	{
		delete[] m_free_segs;
		delete[] m_segments;
		delete m_gc_pool;
	}
public:
	// ����һ���յ�segment
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
		InterlockedDecrement(&m_health_info->m_free_seg);

		m_segments[new_seg].seg_temp = temp;
		m_segments[new_seg].erase_count++;
		return new_seg;
	}

	// ����һ��segment
	void FreeSegment(SEG_T seg_id)
	{
		m_free_tail++;
		if (m_free_tail >= m_seg_nr) m_free_tail = 0;
		if (m_free_tail == m_free_head) { THROW_ERROR(ERR_APP, L"free buffer full"); }
		SEG_INFO<CPageInfo *>& seg = m_segments[seg_id];
		// ����erase count
		DWORD erase_cnt = seg.erase_count;
		memset(&seg, 0, sizeof(SEG_INFO<CPageInfo *>));
		seg.erase_count = erase_cnt;
		seg.seg_temp = BT_TEMP_NR;

		seg.valid_blk_nr = 0;
		seg.cur_blk = 0;

		m_free_segs[m_free_tail] = seg_id;
		m_free_nr++;
		InterlockedIncrement(&m_health_info->m_free_seg);
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
		seg.blk_map[blk_id] = nullptr;
		seg.valid_blk_nr--;
		if (seg.valid_blk_nr == 0 && seg.cur_blk >= BLOCK_PER_SEG)
		{
			FreeSegment(seg_id);
			free_seg = true;
		}
		InterlockedIncrement(&m_health_info->m_free_blk);
		InterlockedDecrement(&m_health_info->m_physical_saturation);
		return free_seg;
	}
	virtual bool InitSegmentManager(CF2fsSimulator* fs, SEG_T segment_nr, SEG_T gc_lo, SEG_T gc_hi, int init = 0);

	// ��src_seg, src_blk�е�block���ƶ���temp��صĵ�ǰsegment������Ŀ��(segment,block)
//	PHY_BLK MoveBlock(SEG_T src_seg, BLK_T src_blk, BLK_TEMP temp);

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

	void SetHealth(FsHealthInfo* health) { m_health_info = health; } 

	// д��data block��segment, file_index �ļ�id, blk���ļ��е����block��temp�¶�
	void CheckGarbageCollection(CF2fsSimulator* fs)
	{
		if (m_free_nr < m_gc_lo) GarbageCollection(fs);
	}
//	virtual PHY_BLK WriteBlockToSeg(const _BLK_TYPE & lblk, BLK_TEMP temp);
	// ��pageд�����
	virtual PHY_BLK WriteBlockToSeg(CPageInfo * page, bool by_gc=false);

	virtual void GarbageCollection(CF2fsSimulator * fs);
	void DumpSegmentBlocks(const std::wstring& fn);

	friend class CF2fsSimulator;
	FILE* m_gc_trace;

protected:
	CInodeManager_* m_inodes = nullptr;

public:	// ��ʱ��ʩ����Ҫ������δ���GcPool��(1)��GC��Ϊ�㷨������segment management�У�(2)�ṩ��ȡGcPool�Ľӿ�
	SEG_INFO<CPageInfo *>* m_segments = nullptr;

protected:
	CF2fsSimulator* m_fs;

	friend class CSingleLogSimulator;
	SEG_T m_cur_segs[BT_TEMP_NR];
	SEG_T m_seg_nr = 0, m_free_nr = 0;
	FsHealthInfo* m_health_info = nullptr;
	SEG_T m_gc_lo, m_gc_hi;

	GcPoolQuick<64, SEG_INFO<CPageInfo*> > *m_gc_pool=nullptr;

private:
	SEG_T m_free_head = 0, m_free_tail = 0;
	SEG_T* m_free_segs = nullptr;
};

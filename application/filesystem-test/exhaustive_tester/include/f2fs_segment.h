///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
#pragma once
#include "fs_simulator.h"
#include "config.h"

class CF2fsSimulator;
class CPageInfo;

//#define ENABLE_FS_TRACE

// [change] 2024.05.03: ��free segment�Ĺ����Ϊ����ʾ�����ڱ���free seg���飬ͨ��һ��free��ָ��ָ���һ��free��segment, segment��valid_blk��Ϊָ����һ��free segment��ָ�롣

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// == page info and page allocator ==

typedef void* DATA_BLK;

class CStorage;
class CPageAllocator;


// segment info��һ��segment����Ϣ
struct SEG_INFO
{
public:
	// block�����storage�У�
	DWORD valid_bmp[BITMAP_SIZE];
	// ��segment free��ʱ����Ϊfree�����ָ��ʹ�á�valid_blk_nr == -1 ��ʾblockΪfree�������ٷ���
	DWORD valid_blk_nr;		
//	DWORD cur_blk;			// ���Է������һ��block, 0:��ʾ���segmentδ��ʹ�ã�BLOCK_PER_SEG����ʾ�Ѿ���������������ǰsegment
							// ��block freeʱ�� cur_blk��ʾfreeָ��
	BLK_TEMP seg_temp;		// ָʾsegment���¶ȣ�����GC��
};

struct SIT_BLOCK
{
	SEG_INFO sit_entries[SIT_ENTRY_PER_BLK];
};

struct SUMMARY
{
	NID nid;
	WORD offset;
};

struct SUMMARY_BLOCK
{
	SUMMARY entries[SUMMARY_PER_BLK];
};

struct CURSEG_INFO {
	SEG_T seg_no;
	BLK_T blk_offset;
};

struct NAT_JOURNAL_ENTRY {
	NID nid;
	PHY_BLK phy_blk;
};

struct SIT_JOURNAL_ENTRY {
	SEG_T seg_no;
	SEG_INFO seg_info;
};

struct CKPT_CURSEG
{
	CURSEG_INFO cur_segs[BT_TEMP_NR];
	UINT nat_journal_nr, sit_journal_nr;
};
struct CKPT_NAT_JOURNAL
{
	NAT_JOURNAL_ENTRY nat_journals[JOURNAL_NR];
};

struct CKPT_SIT_JOURNAL
{
	SIT_JOURNAL_ENTRY sit_journals[JOURNAL_NR/SIT_JOURNAL_BLK];
};

struct CKPT_BLOCK
{
	CURSEG_INFO cur_segs[BT_TEMP_NR];
	UINT nat_journal_nr, sit_journal_nr;
	NAT_JOURNAL_ENTRY nat_journals[JOURNAL_NR];
	SIT_JOURNAL_ENTRY sit_journals[JOURNAL_NR];
};

// Segment Info���ڴ����ݽṹ��
//	free segment�ı�ʾ��valid_blk_nr == 0��
//	free link: cur_blk
//#define SEG_NEXT_FREE	cur_blk

class SegmentInfo
{
public:
	DWORD valid_bmp[BITMAP_SIZE];
	UINT valid_blk_nr;	// ��segment free��ʱ����Ϊfree�����ָ��ʹ�á�valid_blk_nr == -1 ��ʾblockΪfree�������ٷ���
//	DWORD cur_blk;		// ���Է������һ��block, 0:��ʾ���segmentδ��ʹ�ã�BLOCK_PER_SEG����ʾ�Ѿ���������������ǰsegment
	// ��block freeʱ�� cur_blk��ʾfreeָ��
	BLK_TEMP seg_temp;	// ָʾsegment���¶ȣ�����GC��
	NID		nids[BLOCK_PER_SEG];
	WORD	offset[BLOCK_PER_SEG];
	SEG_T	free_next;	// ����free�����˫��ָ��
};

inline DWORD OffsetToBlock(LBLK_T& start_blk, LBLK_T& end_blk, FSIZE start_lba, FSIZE secs)
{
	// lba => block
	FSIZE end_pos = start_lba + secs;
	start_blk = (LBLK_T)(start_lba / BLOCK_SIZE);
	end_blk = (LBLK_T)ROUND_UP_DIV(end_pos, BLOCK_SIZE);
	DWORD blk_nr = end_blk - start_blk;
	return blk_nr;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// == segment manager ==
template <int N, typename SEG_TYPE >
class GcPoolHeap
{
public:
	GcPoolHeap(SEG_TYPE* s) {}
public:
	// ��󶥶��з���segment
	void Push(SEG_TYPE* seg)
	{
		if (large_len < (N - 1)) { large_add(seg); }
		else if (seg->valid_blk_nr < large_heap[0]->valid_blk_nr) { large_heapify(seg); }
	}
	// ���󶥶��е���������С����
	void Sort(void)
	{
		// ��large heap�дӴ�Сȡ��������small_heap�У�
		small_len = large_len;
		for (; ;)
		{
			large_len--;
			small_heap[large_len] = large_heap[0];
			if (large_len <= 0)	break;
			large_heapify(large_heap[large_len]);
		}
		pop_ptr = 0;
	}
	// ��С������ȡ����СԪ��
	SEG_TYPE* Pop(void)
	{
		if (pop_ptr >= small_len) return nullptr;
		SEG_TYPE* pop = small_heap[pop_ptr++];
		return pop;
	}

	int Size(void) const { return small_heap; }

	void ShowHeap(int heap_id)
	{
		SEG_TYPE** heap;
		int size;
		if (heap_id == 0)	heap = small_heap, size = small_len;
		else heap = large_heap, size = large_len;

		for (int ii = 0; ii < size; ++ii)
		{
			SEG_TYPE* seg = heap[ii];
			wprintf_s(L"%02d ", (seg) ? seg->valid_blk_nr : -1);
		}
		wprintf_s(L"\n");
	}

protected:
	void large_add(SEG_TYPE* key)
	{
		int cur = large_len;
		DWORD key_val = key->valid_blk_nr;
		while (cur > 0)
		{	// �Ƚϸ��ڵ�
			int pp = (cur - 1) >> 1;
			if (key_val <= large_heap[pp]->valid_blk_nr) break;
			// ����
			large_heap[cur] = large_heap[pp];
			cur = pp;
		}
		large_heap[cur] = key;
		large_len++;
	}

	void large_heapify(SEG_TYPE* key)
	{
		DWORD key_val = key->valid_blk_nr;
		int cur = 0;
		while (1)
		{
			int left = cur * 2 + 1, right = left + 1;
			int largest = cur;
			DWORD largest_val = key_val;
			DWORD left_val;
			if (left < large_len && (left_val = large_heap[left]->valid_blk_nr) > largest_val)
			{
				largest = left; largest_val = left_val;
			}
			if (right < large_len && large_heap[right]->valid_blk_nr > largest_val)
			{
				largest = right;
			}
			if (largest == cur) break;
			large_heap[cur] = large_heap[largest];		// ��ʱlargest=left����right��keyС��left����right
			cur = largest;
		}
		//		JCASSERT(cur < large_len);
		large_heap[cur] = key;
	}

protected:
	int large_len = 0, small_len = 0;		// �ѵ���Ч����
	SEG_TYPE* large_heap[N]; //����ѡ��һ��������segmeng
	SEG_TYPE* small_heap[N];	// ��С��������������ѡ��ΪGCԴ
	int pop_ptr;
};

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// == segment manager : �ڴ��е�segment���� ==
class CF2fsSegmentManager
{
public:
	CF2fsSegmentManager(CF2fsSimulator* fs);
	~CF2fsSegmentManager(void)	{}
	void CopyFrom(const CF2fsSegmentManager& src);
	bool InitSegmentManager(SEG_T segment_nr, SEG_T gc_lo, SEG_T gc_hi);
	void Reset(void);

public:
	inline static UINT seg_to_lba(SEG_T seg, SEG_T blk) {
		return ((MAIN_SEG_OFFSET + seg) * BLOCK_PER_SEG + blk);
	}

	inline static void BlockToSeg(SEG_T& seg_id, BLK_T& blk_id, PHY_BLK phy_blk) {
		if (is_invalid(phy_blk) ) { seg_id = INVALID_BLK;				blk_id = INVALID_BLK; }
		else { seg_id = phy_blk / BLOCK_PER_SEG;	blk_id = phy_blk % BLOCK_PER_SEG; }
	}

	inline static PHY_BLK PhyBlock(SEG_T seg_id, BLK_T blk_id) {
		return seg_id * BLOCK_PER_SEG + blk_id;
	}

	inline static UINT phyblk_to_lba(PHY_BLK phy_blk) {
		return phy_blk + MAIN_SEG_OFFSET * BLOCK_PER_SEG;
	}

	inline static void set_bitmap(DWORD* bmp, BLK_T blk) {
		DWORD mask = (1 << blk);
		bmp[0] = bmp[0] | mask;
	}

	inline static void clear_bitmap(DWORD* bmp, BLK_T blk) {
		DWORD mask = (1 << blk);
		bmp[0] = bmp[0] & (~mask);
	}

	inline static DWORD test_bitmap(const DWORD* bmp, BLK_T blk) {
		DWORD mask = (1 << blk);
		return bmp[0] & mask;
	}

	static const size_t bmp_size = sizeof(DWORD) * BITMAP_SIZE;


public:
	// ����Ҫ�����ݱ��浽Storage
	void SyncSIT(void);
	void SyncSSA(void);
	void f2fs_flush_sit_entries(CKPT_BLOCK& checkpoint);
	void f2fs_out_sit_journal(SIT_JOURNAL_ENTRY* journal, UINT &journal_nr);
	void fill_seg_info(SEG_INFO* seg_info, SEG_T seg);
	void read_seg_info(SEG_INFO* seg_info, SEG_T seg);
	// ��storage�ж�ȡ 
	bool Load(CKPT_BLOCK & checkpoint);

protected:
	// ����һ���յ�segment: force������ʹ�ñ�������
	SEG_T AllocSegment(BLK_TEMP temp, bool by_gc, bool force);
	// ����һ��segment
	void FreeSegment(SEG_T seg_id);
	// ��segment ii��ӵ�free queue (head)��
	void free_en_queue(SEG_T ii);
	SEG_T free_de_queue(void);
	void build_free_link(void);

public:
	void reset_dirty_map(void) {
		memset(m_dirty_map, 0, sizeof(m_dirty_map));
	}
public:

	bool InvalidBlock(PHY_BLK phy_blk);
	bool InvalidBlock(SEG_T seg_id, BLK_T blk_id);

	// ��src_seg, src_blk�е�block���ƶ���temp��صĵ�ǰsegment������Ŀ��(segment,block)
	SEG_T get_seg_nr(void) const { return MAIN_SEG_NR; }
	SEG_T get_free_nr(void) const { return m_free_nr; }
	PHY_BLK get_free_blk_nr(void) const { return MAIN_SEG_NR * BLOCK_PER_SEG - m_used_blk_nr; }

	const SegmentInfo& get_segment(SEG_T id) const	{
		JCASSERT(id < MAIN_SEG_NR);
		return m_segments[id];
	}

	inline UINT get_valid_blk_nr(SEG_T seg)	{
		return m_segments[seg].valid_blk_nr;
	}

	DWORD is_blk_valid(SEG_T seg_id, BLK_T blk)	{
		return test_bitmap(m_segments[seg_id].valid_bmp, blk);
	}

	void GetBlockInfo(NID& nid, WORD& offset, PHY_BLK phy_blk);
	void SetBlockInfo(NID nid, WORD offset, PHY_BLK phy_blk);

	// д��data block��segment, file_index �ļ�id, blk���ļ��е����block��temp�¶�
	//void CheckGarbageCollection(CF2fsSimulator* fs)	{
	//	if (m_free_nr < m_gc_lo) GarbageCollection(fs);
	//}
	// ��pageд�����
	PHY_BLK WriteBlockToSeg(CPageInfo * page, bool force, bool by_gc=false);

	ERROR_CODE GarbageCollection(CF2fsSimulator * fs);
	inline SEG_T SegId(SegmentInfo* seg) const {return (SEG_T)(seg - m_segments);}

	friend class CF2fsSimulator;
#ifdef ENABLE_FS_TRACE
	FILE* m_gc_trace;
#endif

public:
	DWORD is_dirty(SEG_T seg_id);
	void set_dirty(SEG_T seg_id);
	void clear_dirty(SEG_T seg_id);

	// һ��������������Ҫ�����
protected:	// ��ʱ��ʩ����Ҫ������δ���GcPool��(1)��GC��Ϊ�㷨������segment management�У�(2)�ṩ��ȡGcPool�Ľӿ�
	SegmentInfo m_segments[MAIN_SEG_NR];
protected:
//	SEG_T m_cur_segs[BT_TEMP_NR];
	CURSEG_INFO m_cur_segs[BT_TEMP_NR];
	SEG_T m_gc_lo, m_gc_hi;
	// SIT entry��dirty��־��һ��bit��ʾһ��SIT entry��һ��DWORD��ʾһ��SIT block��
	DWORD m_dirty_map[SIT_BLK_NR];

protected:
	// free
	SEG_T m_free_tail, m_free_head, m_free_nr; // headָ�������������en-queue; tailָ������ͷ������de-queue
	PHY_BLK	m_used_blk_nr;

protected:
	CF2fsSimulator* m_fs;
	CStorage* m_storage;
	// system cache manager
	CPageAllocator* m_pages;
	CKPT_BLOCK*		m_checkpoint;

protected:
	FsHealthInfo* m_health_info = nullptr;
	struct DATA_PAGE_CACHE {
		NID nid;
		UINT offset;
		CPageInfo* page;
	} m_data_cache;
};

const char* BLK_TEMP_NAME[];

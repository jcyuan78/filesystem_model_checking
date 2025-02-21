///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
#pragma once
#include "fs_simulator.h"
#include "config.h"

class CF2fsSimulator;
class CPageInfo;

//#define ENABLE_FS_TRACE

// [change] 2024.05.03: 将free segment的管理改为连表示。不在保留free seg数组，通过一个free的指针指向第一个free的segment, segment的valid_blk作为指向下一个free segment的指针。

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// == page info and page allocator ==

typedef void* DATA_BLK;

class CStorage;
class CPageAllocator;


// segment info：一个segment的信息
struct SEG_INFO
{
public:
	// block存放在storage中，
	DWORD valid_bmp[BITMAP_SIZE];
	// 当segment free的时候，作为free链表的指针使用。valid_blk_nr == -1 表示block为free，可以再分配
	DWORD valid_blk_nr;		
//	DWORD cur_blk;			// 可以分配的下一个block, 0:表示这个segment未被使用，BLOCK_PER_SEG：表示已经填满，其他：当前segment
							// 当block free时， cur_blk表示free指针
	BLK_TEMP seg_temp;		// 指示segment的温度，用于GC和
};

struct SIT_BLOCK
{
	SEG_INFO sit_entries[SIT_ENTRY_PER_BLK];
};

struct SUMMARY
{
	_NID nid;
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
	_NID nid;
	PHY_BLK phy_blk;
};

struct SIT_JOURNAL_ENTRY {
	SEG_T seg_no;
	SEG_INFO seg_info;
};

struct CKPT_HEAD
{
	CURSEG_INFO cur_segs[BT_TEMP_NR];
	UINT nat_journal_nr, sit_journal_nr;
	DWORD sit_ver_bitmap;
	DWORD nat_ver_bitmap;
	DWORD ver_open, ver_close;
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
	//CURSEG_INFO cur_segs[BT_TEMP_NR];
	//UINT nat_journal_nr, sit_journal_nr;
	//DWORD sit_ver_bitmap;
	//DWORD nat_ver_bitmap;
	CKPT_HEAD header;
	NAT_JOURNAL_ENTRY nat_journals[JOURNAL_NR];
	SIT_JOURNAL_ENTRY sit_journals[JOURNAL_NR];
};

// Segment Info的内存数据结构：
//	free segment的表示：valid_blk_nr == 0；
//	free link: cur_blk
//#define SEG_NEXT_FREE	cur_blk

class SegmentInfo
{
public:
	DWORD		valid_bmp[BITMAP_SIZE];
	UINT		valid_blk_nr;	// 当segment free的时候，作为free链表的指针使用。valid_blk_nr == -1 表示block为free，可以再分配
	BLK_TEMP	seg_temp;	// 指示segment的温度，用于GC和
	_NID		nids[BLOCK_PER_SEG];
	WORD		offset[BLOCK_PER_SEG];
	SEG_T		free_next;	// 构成free链表的双向指针
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
	// 想大顶堆中放入segment
	void Push(SEG_TYPE* seg)
	{
		if (large_len < (N - 1)) { large_add(seg); }
		else if (seg->valid_blk_nr < large_heap[0]->valid_blk_nr) { large_heapify(seg); }
	}
	// 将大顶堆中的数据移入小顶堆
	void Sort(void)
	{
		// 从large heap中从大到小取出，放入small_heap中；
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
	// 从小顶堆中取出最小元素
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
		{	// 比较父节点
			int pp = (cur - 1) >> 1;
			if (key_val <= large_heap[pp]->valid_blk_nr) break;
			// 交换
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
			large_heap[cur] = large_heap[largest];		// 此时largest=left或者right，key小于left或者right
			cur = largest;
		}
		//		JCASSERT(cur < large_len);
		large_heap[cur] = key;
	}

protected:
	int large_len = 0, small_len = 0;		// 堆的有效长度
	SEG_TYPE* large_heap[N]; //用于选出一定数量的segmeng
	SEG_TYPE* small_heap[N];	// 按小顶堆排序，用于挑选作为GC源
	int pop_ptr;
};

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// == segment manager : 内存中的segment管理 ==
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
	// 将必要的数据保存到Storage
	void SyncSIT(void);
	void SyncSSA(void);
	void f2fs_flush_sit_entries(CKPT_BLOCK& checkpoint);
	void f2fs_out_sit_journal(/*SIT_JOURNAL_ENTRY* journal, UINT &journal_nr, */CPageInfo ** sit_pages, CKPT_BLOCK & checkpoint);
	LBLK_T get_sit_next_block(UINT sit_blk, CKPT_BLOCK & checkpoint);
	LBLK_T get_sit_block(UINT sit_blk, const CKPT_BLOCK & checkpoint);
	void fill_seg_info(SEG_INFO* seg_info, SEG_T seg);
	void read_seg_info(SEG_INFO* seg_info, SEG_T seg);
	// 从storage中读取 
	bool Load(CKPT_BLOCK & checkpoint);
	void DumpSegments(FILE * out);

protected:
	// 查找一个空的segment: force：可以使用保留区域
	SEG_T AllocSegment(BLK_TEMP temp, bool by_gc, bool force);
	// 回收一个segment
	void FreeSegment(SEG_T seg_id);
	// 将segment ii添加到free queue (head)中
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

	// 将src_seg, src_blk中的block，移动到temp相关的当前segment，返回目标(segment,block)
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

	void GetBlockInfo(_NID& nid, WORD& offset, PHY_BLK phy_blk);
	void SetBlockInfo(_NID nid, WORD offset, PHY_BLK phy_blk);

	// 写入data block到segment, file_index 文件id, blk：文件中的相对block，temp温度
	//void CheckGarbageCollection(CF2fsSimulator* fs)	{
	//	if (m_free_nr < m_gc_lo) GarbageCollection(fs);
	//}
	// 将page写入磁盘
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

	// 一下两段数据是需要保存的
protected:	// 临时措施，需要考虑如何处理GcPool。(1)将GC作为算法器放入segment management中，(2)提供获取GcPool的接口
	SegmentInfo m_segments[MAIN_SEG_NR];
protected:
//	SEG_T m_cur_segs[BT_TEMP_NR];
	CURSEG_INFO m_cur_segs[BT_TEMP_NR];
	SEG_T m_gc_lo, m_gc_hi;
	// SIT entry的dirty标志，一个bit表示一个SIT entry。一个DWORD表示一个SIT block。
	DWORD m_dirty_map[SIT_BLK_NR];

protected:
	// free
	SEG_T m_free_tail, m_free_head, m_free_nr; // head指向链表最后，用于en-queue; tail指向链表头，用于de-queue
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
		_NID nid;
		UINT offset;
		CPageInfo* page;
	} m_data_cache;
};

const char* BLK_TEMP_NAME[];

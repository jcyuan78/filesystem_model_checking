///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
#pragma once
#include "fs_simulator.h"

class CF2fsSimulator;

//#define ENABLE_FS_TRACE

// [change] 2024.05.03: 将free segment的管理改为连表示。不在保留free seg数组，通过一个free的指针指向第一个free的segment, segment的valid_blk作为指向下一个free segment的指针。

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// == page info and page allocator ==

typedef DWORD SEG_T;
typedef DWORD BLK_T;
typedef DWORD FID;
typedef DWORD NID;		// node id
typedef void* DATA_BLK;

//#define MAX_TABLE_SIZE			128
#define MAX_FILENAME_LEN		8
#define MAX_NODE_NUM			4096

//#define SECTOR_PER_BLOCK		(8)
//#define SECTOR_PER_BLOCK_BIT	(3)

#define BLOCK_PER_SEG			(64)			// 一个segment有多少块
#define BITMAP_SIZE				(16)			// 512 blocks / 32 bit
#define SEG_NUM					(512)
#define MAX_PAGE_NUM (SEG_NUM * BLOCK_PER_SEG)

#define PAGE_NEXT_FREE	data_index

class CPageAllocator
{
public:
	typedef UINT INDEX;
	CPageAllocator(void);
	~CPageAllocator(void);

public:
	void Init(size_t page_nr);
	INDEX get_page(void);
	void put_page(INDEX index);

	inline CPageInfo* page(INDEX index) {
		return (index>=m_page_nr)?nullptr:(m_pages + index); }

protected:
	CPageInfo m_pages[MAX_PAGE_NUM];
	INDEX m_free_ptr, m_used_nr;
	UINT m_page_nr;
};

// segment info：一个segment的信息
#define SEG_NEXT_FREE	valid_blk_nr
class SEG_INFO
{
public:
	CPageAllocator::INDEX blk_map[BLOCK_PER_SEG];
	DWORD valid_bmp[BITMAP_SIZE];
	DWORD valid_blk_nr;		// 当segment free的时候，作为free链表的指针使用。
	DWORD cur_blk;			// 可以分配的下一个block, 0:表示这个segment未被使用，BLOCK_PER_SEG：表示已经填满，其他：当前segment
	BLK_TEMP seg_temp;		// 指示segment的温度，用于GC和
	DWORD erase_count;
};

inline void BlockToSeg(SEG_T& seg_id, BLK_T& blk_id, PHY_BLK phy_blk)
{
	if (phy_blk == INVALID_BLK)
	{
		seg_id = INVALID_BLK;
		blk_id = INVALID_BLK;
	}
	else
	{
		seg_id = phy_blk / BLOCK_PER_SEG;
		blk_id = phy_blk % BLOCK_PER_SEG;
	}
}

inline PHY_BLK PhyBlock(SEG_T seg_id, BLK_T blk_id)
{
	return seg_id * BLOCK_PER_SEG + blk_id;
}

inline DWORD OffsetToBlock(LBLK_T& start_blk, LBLK_T& end_blk, UINT start_lba, UINT secs)
{
	// lba => block
	UINT end_lba = start_lba + secs;
	start_blk = (LBLK_T)(start_lba / BLOCK_SIZE);
	end_blk = (LBLK_T)ROUND_UP_DIV(end_lba, BLOCK_SIZE);
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
// == segment manager ==


class CF2fsSegmentManager
{
public:

	CF2fsSegmentManager(void) { }
	virtual ~CF2fsSegmentManager(void)
	{
		//delete[] m_free_segs;
		//delete[] m_segments;
	}
	void CopyFrom(const CF2fsSegmentManager& src, CF2fsSimulator * fs);
	
public:
	// 查找一个空的segment
	SEG_T AllocSegment(BLK_TEMP temp)
	{
		if (m_free_nr == 0 || m_free_ptr == INVALID_BLK) { THROW_ERROR(ERR_APP, L"no enough free segment"); }
		SEG_T new_seg = m_free_ptr;
		m_free_ptr = m_segments[new_seg].SEG_NEXT_FREE;
		m_free_nr--;

		m_segments[new_seg].valid_blk_nr = 0;
		m_segments[new_seg].seg_temp = temp;
		m_segments[new_seg].erase_count++;
		return new_seg;
	}

	// 回收一个segment
	void FreeSegment(SEG_T seg_id)
	{
		SEG_INFO& seg = m_segments[seg_id];

		// 保留erase count
		DWORD erase_cnt = seg.erase_count;
		memset(&seg, 0, sizeof(SEG_INFO));
		seg.erase_count = erase_cnt;
		seg.seg_temp = BT_TEMP_NR;
		seg.cur_blk = 0;

		// 将seg放入free list中；
		seg.SEG_NEXT_FREE = m_free_ptr;
		m_free_ptr = seg_id;

		m_free_nr++;
	}

	bool InvalidBlock(PHY_BLK phy_blk)
	{
		if (phy_blk == INVALID_BLK) return false;
		SEG_T seg_id; BLK_T blk_id;
		BlockToSeg(seg_id, blk_id, phy_blk);
		return InvalidBlock(seg_id, blk_id);
	}

	bool InvalidBlock(SEG_T seg_id, BLK_T blk_id);

	virtual bool InitSegmentManager(CF2fsSimulator* fs, SEG_T segment_nr, SEG_T gc_lo, SEG_T gc_hi/*, int init = 0*/);

	// 将src_seg, src_blk中的block，移动到temp相关的当前segment，返回目标(segment,block)

	SEG_T get_seg_nr(void) const { return m_seg_nr; }
	SEG_T get_free_nr(void) const { return m_free_nr; }
	const SEG_INFO& get_segment(SEG_T id) const
	{
		JCASSERT(id < m_seg_nr);
		return m_segments[id];
	}

	CPageAllocator::INDEX get_block(PHY_BLK phy_blk)
	{
		SEG_T seg_id; BLK_T blk_id;
		BlockToSeg(seg_id, blk_id, phy_blk);
		SEG_INFO& seg = m_segments[seg_id];
		return seg.blk_map[blk_id];
	}

	// 写入data block到segment, file_index 文件id, blk：文件中的相对block，temp温度
	void CheckGarbageCollection(CF2fsSimulator* fs)
	{
		if (m_free_nr < m_gc_lo) GarbageCollection(fs);
	}
	// 将page写入磁盘
	virtual PHY_BLK WriteBlockToSeg(CPageAllocator::INDEX page, bool by_gc=false);

	virtual void GarbageCollection(CF2fsSimulator * fs);
	void DumpSegmentBlocks(const std::wstring& fn);

	friend class CF2fsSimulator;
#ifdef ENABLE_FS_TRACE
	FILE* m_gc_trace;
#endif

public:	// 临时措施，需要考虑如何处理GcPool。(1)将GC作为算法器放入segment management中，(2)提供获取GcPool的接口
	SEG_INFO m_segments[SEG_NUM];
protected:
	// free
	SEG_T m_free_nr, m_free_ptr;

protected:
	CF2fsSimulator* m_fs;
	CPageAllocator* m_pages;

	SEG_T m_cur_segs[BT_TEMP_NR];
	SEG_T m_seg_nr = 0;
	SEG_T m_gc_lo, m_gc_hi;

private:
	FsHealthInfo* m_health_info = nullptr;
};

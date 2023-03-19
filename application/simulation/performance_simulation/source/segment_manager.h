///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
#pragma once

#include <boost/property_tree/ptree.hpp>

#define SECTOR_PER_BLOCK		(8)
#define SECTOR_PER_BLOCK_BIT	(3)

#define BLOCK_PER_SEG	512
#define BITMAP_SIZE		16			// 512 blocks / 32 bit
#define GC_THREAD_START		3
#define GC_THREAD_END		5

#define INVALID_BLK		0xFFFFFFFF


// == configurations
#define _SANITY_CHECK
#define HEAP_ALGORITHM

typedef DWORD SEG_T;
typedef DWORD BLK_T;
typedef DWORD PHY_BLK;
typedef DWORD LBLK_T;
typedef DWORD FID;

/// <summary>
/// 描述文件系统的运行状态。通过特殊（$health）文件读取
/// </summary>
struct FsHealthInfo
{
	UINT m_seg_nr;	// 总的segment数量
	UINT m_blk_nr;	// 做的block数量
	UINT m_logical_blk_nr;			// 逻辑块总是。makefs时申请的逻辑块数量
	UINT m_free_seg, m_free_blk;	// 空闲segment和block数量

	LONG64 m_total_host_write;	// 以块为单位，host的写入总量。（快的大小由根据文件系统调整，一般为4KB）
	LONG64 m_total_media_write;	// 写入介质的数据总量，以block为单位

	UINT m_logical_saturation;	// 逻辑饱和度。被写过的逻辑块数量，不包括metadata
	UINT m_physical_saturation;	// 物理饱和度。有效的物理块数量，

	UINT m_node_nr;		// inode, direct node的总数
	UINT m_used_node;	// 被使用的node总数
};

enum BLK_TEMP
{
	BT_COLD_DATA = 0, BT_COLD_NODE = 1, 
	BT_WARM_DATA = 2, BT_WARM_NODE = 3, 
	BT_HOT__DATA = 4, BT_HOT__NODE = 5,
	BT_TEMP_NR
};

// segment info：一个segment的信息
template <typename BLOCK_TYPE>
class SEG_INFO
{
public:
	BLOCK_TYPE blk_map[BLOCK_PER_SEG];
	DWORD valid_bmp[BITMAP_SIZE];
	DWORD valid_blk_nr;	
	DWORD cur_blk;		// 可以分配的下一个block, 0:表示这个segment未被使用，BLOCK_PER_SEG：表示已经填满，其他：当前segment
	BLK_TEMP seg_temp;	// 指示segment的温度，用于GC和
};

template <typename BLOCK_TYPE> void TypedInvalidBlock(BLOCK_TYPE& blk);

class LBLOCK_INFO
{
public:
	PHY_BLK phy_blk;
	int host_write, total_write;	// host的改写次数；总改写次数（包括GC）
	BLK_TEMP temp;
};

template <int N, typename BLOCK_TYPE >
class GcPool
{
public:
	//typedef DWORD INDEX_TYPE;
	GcPool(SEG_INFO<BLOCK_TYPE>* s) : segs(s) {}
public:
	// 想大顶堆中放入segment
	void Push(SEG_T id)
	{
		if (large_len < (N-1))
		{
			large_heap[large_len] = id;
			large_add(large_len);
			large_len++;
		}
		else
		{
			SEG_INFO<BLOCK_TYPE>& new_seg = segs[id];
			SEG_INFO<BLOCK_TYPE> & max_seg = segs[large_heap[0]] ;
			if (new_seg.valid_blk_nr >= max_seg.valid_blk_nr) return;
			large_heap[0] = id;
			large_heapify();
		}
	}
	// 将大顶堆中的数据移入小顶堆
	void LargeToSmall(void)
	{
		small_len = 0;
		for (int ii = N - 2; ii >= 0; --ii)
		{
			small_heap[small_len] = large_heap[ii];
			small_add(small_len);
			small_len++;
			//wprintf_s(L"")
//			ShowHeap(small_heap, small_len);
		}
	}
	// 从小顶堆中取出最小元素
	SEG_T Pop(void)
	{
		if (small_len == 0) return INVALID_BLK;

		SEG_T pop = small_heap[0];
		small_len--;
		small_heap[0] = small_heap[small_len];
		small_heapify();
		return pop;
	}
	int Size(void) const { return small_heap; }

	void ShowHeap(int heap_id)
	{
		SEG_T* heap;
		int size;
		if (heap_id == 0)	heap = small_heap, size = small_len;
		else heap = large_heap, size = large_len;

		for (int ii = 0; ii < size; ++ii)
		{
			SEG_INFO<BLOCK_TYPE>& seg = segs[heap[ii]];
			wprintf_s(L"%02d ", seg.valid_blk_nr);
		}
		wprintf_s(L"\n");
	}

protected:

	inline void swap(SEG_T& a, SEG_T& b) {	SEG_T c = a; a = b; b = c;}

	void large_add(int cur)
	{
		while (cur > 0)
		{	// 比较父节点
			int pp = (cur - 1) / 2;
			SEG_INFO<BLOCK_TYPE>& ch = segs[large_heap[cur] ];
			SEG_INFO<BLOCK_TYPE>& fa = segs[large_heap[pp] ];
			if (ch.valid_blk_nr <= fa.valid_blk_nr) break;
			// 交换
			swap(large_heap[cur], large_heap[pp]);
			cur = pp;
		}
	}

	void small_add(int cur)
	{
		while (cur > 0)
		{	// 比较父节点
			int pp = (cur - 1) / 2;
			SEG_INFO<BLOCK_TYPE>& ch = segs[small_heap[cur]];
			SEG_INFO<BLOCK_TYPE>& fa = segs[small_heap[pp]];
			if (ch.valid_blk_nr >= fa.valid_blk_nr) break;
			// 交换
			swap(small_heap[cur], small_heap[pp]);
			cur = pp;
		}
	}

	void large_heapify(void)
	{
		int cur = 0;
		while (1)
		{
			int left = cur * 2 + 1, right = left + 1;
			int largest = cur;
			if (left < large_len && segs[large_heap[left]].valid_blk_nr > segs[large_heap[largest]].valid_blk_nr) 
				largest = left;
			if (right < large_len && segs[large_heap[right]].valid_blk_nr > segs[large_heap[largest]].valid_blk_nr)
				largest = right;
			if (largest == cur) break;
			swap(large_heap[largest], large_heap[cur]);
			cur = largest;
		}
	}

	void small_heapify(void)
	{
		int cur = 0;
		while (1)
		{
			int left = cur * 2 + 1, right = left + 1;
			int largest = cur;
			if (left < small_len && segs[small_heap[left]].valid_blk_nr < segs[small_heap[largest]].valid_blk_nr)
				largest = left;
			if (right < small_len && segs[small_heap[right]].valid_blk_nr < segs[small_heap[largest]].valid_blk_nr) 
				largest = right;
			if (largest == cur) break;
			swap(small_heap[largest], small_heap[cur]);
			cur = largest;
		}
	}

protected:
	int large_len=0, small_len=0;		// 堆的有效长度
	SEG_INFO<BLOCK_TYPE> * segs;
	SEG_T large_heap[N]; //用于选出一定数量的segmeng
	SEG_T small_heap[N];	// 按小顶堆排序，用于挑选作为GC源
};

template <typename BLOCK_TYPE>
class CSegmentManagerBase
{
public:
	CSegmentManagerBase(void) { }
	virtual ~CSegmentManagerBase(void)
	{
		delete[] m_free_segs;
		delete[] m_segments;
	}
public:
	//virtual bool Initialize(const boost::property_tree::wptree& config) = 0;
	//virtual void SetLogFile(const std::wstring& fn) = 0;
	//virtual bool WriteSector(size_t lba, size_t secs, BLK_TEMP temp) = 0;
	//virtual void DumpL2PMap(const std::wstring& fn) = 0;
	//virtual void CheckingColdDataBySeg(const std::wstring& fn) = 0;
	//virtual size_t GetLBlockNr(void) const = 0;
	virtual void GarbageCollection(void) = 0;
	virtual PHY_BLK WriteBlockToSeg(const BLOCK_TYPE& lblk, BLK_TEMP temp) = 0;

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
		if (m_free_tail == m_free_head)
		{
			THROW_ERROR(ERR_APP, L"free buffer full");
		}
		SEG_INFO<BLOCK_TYPE>& seg = m_segments[seg_id];
		memset(&seg, 0xFF, sizeof(SEG_INFO<BLOCK_TYPE>));
		seg.valid_blk_nr = 0;
		seg.cur_blk = 0;

		m_free_segs[m_free_tail] = seg_id;
		m_free_nr++;
		InterlockedIncrement(&m_health->m_free_seg);
	}

	void InvalidBlock(PHY_BLK phy_blk)
	{
		if (phy_blk == INVALID_BLK) return;
		SEG_T seg_id; BLK_T blk_id;
		BlockToSeg(seg_id, blk_id, phy_blk);
		InvalidBlock(seg_id, blk_id);
	}

	void InvalidBlock(SEG_T seg_id, BLK_T blk_id)
	{
		JCASSERT(seg_id < m_seg_nr);
		SEG_INFO<BLOCK_TYPE>& seg = m_segments[seg_id];
		TypedInvalidBlock<BLOCK_TYPE>(seg.blk_map[blk_id]);
		seg.valid_blk_nr--;
		if (seg.valid_blk_nr == 0 && seg.cur_blk >= BLOCK_PER_SEG)
		{
			FreeSegment(seg_id);
		}
		InterlockedIncrement(&m_health->m_free_blk);
		InterlockedDecrement(&m_health->m_physical_saturation);
	}

	bool InitSegmentManager(SEG_T segment_nr)
	{
		m_seg_nr = segment_nr;
		m_segments = new SEG_INFO<BLOCK_TYPE>[m_seg_nr];
		m_free_segs = new SEG_T[m_seg_nr];
		// 初始化
		memset(m_segments, 0xFF, sizeof(SEG_INFO<BLOCK_TYPE>) * m_seg_nr);
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
		return true;
	}


	// 将src_seg, src_blk中的block，移动到temp相关的当前segment，返回目标(segment,block)
	PHY_BLK MoveBlock(SEG_T src_seg, BLK_T src_blk, BLK_TEMP temp)
	{
		SEG_INFO<BLOCK_TYPE>& seg = m_segments[src_seg];
		BLOCK_TYPE & lblk = seg.blk_map[src_blk];
		// 新的segment中写入 
		PHY_BLK tar = WriteBlockToSeg(lblk, temp);
		// 无效旧的block
		InvalidBlock(src_seg, src_blk);
		return tar;
	}

	SEG_T get_seg_nr(void) const { return m_seg_nr; }
	SEG_T get_free_nr(void) const { return m_free_nr; }
	SEG_INFO<BLOCK_TYPE>& get_segment(SEG_T id) const
	{
		JCASSERT(id < m_seg_nr);
		return m_segments[id]; 
	}

	BLOCK_TYPE& get_block(PHY_BLK phy_blk)
	{
		SEG_T seg_id; BLK_T blk_id;
		BlockToSeg(seg_id, blk_id, phy_blk);
		SEG_INFO<BLOCK_TYPE> & seg = m_segments[seg_id];
		return seg.blk_map[blk_id];
	}

	void SetHealth(FsHealthInfo* health) { m_health = health; }

public:	// 临时措施，需要考虑如何处理GcPool。(1)将GC作为算法器放入segment management中，(2)提供获取GcPool的接口
	SEG_INFO<BLOCK_TYPE>* m_segments = nullptr;

protected:
	SEG_T m_seg_nr=0, m_free_nr=0;
	SEG_T m_cur_segs[BT_TEMP_NR];
	FsHealthInfo* m_health = nullptr;

private:
	SEG_T m_free_head=0, m_free_tail=0;
	SEG_T* m_free_segs = nullptr;
};


// 描述LFS中的一个block。
//	模拟实际文件系统中的一个物理block。对于data block，不关心其中的数据，只保留他的逻辑地址。对于NODE，由链接指示物理数据。
class LFS_BLOCK_INFO
{
public:
	LFS_BLOCK_INFO(FID f, LBLK_T l) : nid(f), offset(l) {}
	LFS_BLOCK_INFO(void): nid(INVALID_BLK), offset(INVALID_BLK), host_write(0), media_write(0){}
public:
	FID nid;	// 文件ID或者node id, 0xFF时无效
	LBLK_T offset;	// 文件中的相对block，0xFF时，表示指向node
	UINT host_write;
	UINT media_write;
};

class CLfsSegmentManager : public CSegmentManagerBase<LFS_BLOCK_INFO>
{
public:
	virtual ~CLfsSegmentManager(void);
public:
	// 写入data block到segment, fid 文件id, blk：文件中的相对block，temp温度
	PHY_BLK WriteBlockToSeg(FID fid, LBLK_T blk, BLK_TEMP temp) {
		PHY_BLK phy_blk = WriteBlockToSeg(LFS_BLOCK_INFO(fid, blk), temp);
		if (m_free_nr < GC_THREAD_START) GarbageCollection();
		return phy_blk;
	}
	virtual PHY_BLK WriteBlockToSeg(const LFS_BLOCK_INFO & lblk, BLK_TEMP temp);
	virtual void GarbageCollection(void);
	void DumpSegmentBlocks(const std::wstring& fn);

protected:

};

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// == 辅助函数 ==

inline DWORD LbaToBlock(LBLK_T &start_blk, LBLK_T &end_blk, size_t start_lba, size_t secs)
{
	// lba => block
	size_t end_lba = start_lba + secs;
	start_blk = (LBLK_T)(start_lba / 8);
	end_blk = (LBLK_T) ROUND_UP_DIV(end_lba, 8);
	DWORD blk_nr = end_blk - start_blk;
	return blk_nr;
}

inline PHY_BLK PhyBlock(SEG_T seg_id, BLK_T blk_id)
{
	return seg_id * BLOCK_PER_SEG + blk_id; 
}

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

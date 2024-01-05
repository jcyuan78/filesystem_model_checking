///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
#pragma once

#include <boost/property_tree/ptree.hpp>

#include "../include/fs_comm.h"





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
	DWORD erase_count;
};

template <typename BLOCK_TYPE> void TypedInvalidBlock(BLOCK_TYPE& blk);

class LBLOCK_INFO
{
public:
	PHY_BLK phy_blk;
	int host_write, total_write;	// host的改写次数；总改写次数（包括GC）
	BLK_TEMP temp;
};


template <int N, typename SEG_TYPE >
class GcPool
{
public:
	GcPool(SEG_TYPE* s) : segs(s) {}
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
			SEG_TYPE& new_seg = segs[id];
			SEG_TYPE & max_seg = segs[large_heap[0]] ;
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
			SEG_TYPE& seg = segs[heap[ii]];
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
			SEG_TYPE& ch = segs[large_heap[cur] ];
			SEG_TYPE& fa = segs[large_heap[pp] ];
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
			SEG_TYPE& ch = segs[small_heap[cur]];
			SEG_TYPE& fa = segs[small_heap[pp]];
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
	SEG_TYPE * segs;
	SEG_T large_heap[N]; //用于选出一定数量的segmeng
	SEG_T small_heap[N];	// 按小顶堆排序，用于挑选作为GC源
};


/// ///////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// <summary>
/// 基于指针的Heap排序
/// </summary>
/// <typeparam name="BLOCK_TYPE"></typeparam>

template <int N, typename SEG_TYPE >
class GcPoolHeap
{
public:
	GcPoolHeap(SEG_TYPE* s) /*: segs(s)*/ {}
public:
	// 想大顶堆中放入segment
	void Push(SEG_TYPE* seg)
	{
		if (large_len < (N-1) )	{ large_add(seg);	}
		else if (seg->valid_blk_nr < large_heap[0]->valid_blk_nr) {large_heapify(seg);	}
	}
#if 0
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
//			wprintf_s(L"move %d to small: ", small_heap[large_len]->valid_blk_nr);
//			ShowHeap(1);
		}
//		wprintf_s(L"move %d to small\n", small_heap[0]->valid_blk_nr);
		pop_ptr = 0;
	}
	// 从小顶堆中取出最小元素
	SEG_TYPE * Pop(void)
	{
		if (pop_ptr >= small_len) return nullptr;
		SEG_TYPE* pop = small_heap[pop_ptr++];
		return pop;
	}
#else
	// 将 large_heap重新按照从小到大排序到small_heap中
	void Sort(void)
	{
		small_len = 0;
		for (int ii = N - 2; ii >= 0; --ii)
		{
			small_heap[small_len] = large_heap[ii];
			small_add(small_len);
			small_len++;
		}
	}
	// 从小顶堆中取出最小元素
	SEG_TYPE* Pop(void)
	{
		if (pop_ptr >= small_len) return nullptr;

		SEG_TYPE* pop = small_heap[0];
		small_len--;
		small_heap[0] = small_heap[small_len];
		small_heapify();
		return pop;
	}
#endif

	int Size(void) const { return small_heap; }

	void ShowHeap(int heap_id)
	{
		SEG_TYPE** heap;
		int size;
		if (heap_id == 0)	heap = small_heap, size = small_len;
		else heap = large_heap, size = large_len;

		for (int ii = 0; ii < size; ++ii)
		{
			SEG_TYPE * seg = heap[ii];
			wprintf_s(L"%02d ", (seg)?seg->valid_blk_nr:-1);
		}
		wprintf_s(L"\n");
	}

protected:

	inline void swap(SEG_TYPE * & a, SEG_TYPE * & b) { SEG_TYPE* c = a; a = b; b = c; }

	void large_add(SEG_TYPE * key)
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

	void small_add(int cur)
	{
		while (cur > 0)
		{	// 比较父节点
			int pp = (cur - 1) / 2;
			SEG_TYPE& ch = *small_heap[cur];
			SEG_TYPE& fa = *small_heap[pp];
			if (ch.valid_blk_nr >= fa.valid_blk_nr) break;
			// 交换
			swap(small_heap[cur], small_heap[pp]);
			cur = pp;
		}
	}

	void large_heapify(SEG_TYPE * key)
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

	void small_heapify(void)
	{
		int cur = 0;
		while (1)
		{
			int left = cur * 2 + 1, right = left + 1;
			int largest = cur;
			if (left < small_len && (*small_heap[left]).valid_blk_nr < (*small_heap[largest]).valid_blk_nr)
				largest = left;
			if (right < small_len && (*small_heap[right]).valid_blk_nr < (*small_heap[largest]).valid_blk_nr)
				largest = right;
			if (largest == cur) break;
			swap(small_heap[largest], small_heap[cur]);
			cur = largest;
		}
	}

protected:
	int large_len = 0, small_len = 0;		// 堆的有效长度
//	SEG_TYPE* segs;
	SEG_TYPE* large_heap[N]; //用于选出一定数量的segmeng
	SEG_TYPE* small_heap[N];	// 按小顶堆排序，用于挑选作为GC源
	int pop_ptr;
};

/// ///////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// <summary>
/// 利用quick排序查找GC对象
/// </summary>
/// <typeparam name="BLOCK_TYPE"></typeparam>


template <int N, typename SEG_TYPE >
class GcPoolQuick
{
public:
	typedef SEG_TYPE* PSEG;
	GcPoolQuick(size_t seg_nr) 
	{
		pth = 500;
		buf_size = seg_nr;
		buf = new PSEG[seg_nr];
		memset(buf, 0, sizeof(PSEG) * seg_nr);
	};
	void init(void)
	{
		low = 0, high = buf_size - 1;
		sort_count = 0;
		data_size = 0;
	}

	void Push(SEG_TYPE* seg)
	{
		// 复制seg，完成第一次分类；
//		DWORD pivot = 12;
		if (seg->valid_blk_nr >= pth)
		{
			buf[high] = seg;
			high--;
		}
		else
		{
			buf[low] = seg;
			low++;
		}
		data_size++;
	}

	void Sort2(SEG_TYPE * segs)
	{
/*
		DWORD pivot = 12;

		// 复制seg，完成第一次分类；
		size_t left = 0, right = buf_size - 1;
		for (size_t ii = 0; ii < buf_size; ++ii)
		{
			//JCASSERT(left < right);
			SEG_TYPE* seg = segs + ii;
			if (seg->valid_blk_nr >= pivot)
			{
				buf[right] = seg;
				right--;
			}
			else
			{
				buf[left] = seg;
				left++;
			}
		}
		//		wprintf_s(L"left=%lld, right=%lld\n", left, right);
		//		out_data();

		data_size = buf_size;
		if (left >= N)
		{
			partial_qsort(0, left - 1);
		}
		else
		{
			partial_qsort(0, left - 1);
			// 将高端与左边对其
			partial_qsort(left, data_size - 1);
		}
		pop_ptr = 0;
*/
	}


	void Sort(void)
	{
//		wprintf_s(L"start sorting: low part:%lld, high part:%lld\n", low, high);
		if (low > 0) partial_qsort(0, low-1);
		if (low < N )
		{
			// 将高端与左边对其
			if (high >= low)
			{
				size_t tt = low;
				for (size_t ii = high + 1; ii < buf_size; ++ii)		buf[tt++] = buf[ii];
				data_size = tt;
			}
			else data_size = buf_size;

			partial_qsort(low, data_size - 1);
		}
		pop_ptr = 0;
		min_val = buf[0] ? buf[0]->valid_blk_nr : 0;
		max_val = buf[N - 1] ? buf[N - 1]->valid_blk_nr : 0;
//		wprintf_s(L"min value=%d, max value=%d\n", buf[0]->valid_blk_nr, buf[N-1]?buf[N-1]->valid_blk_nr:0);
	}
	~GcPoolQuick(void)
	{
		delete[] buf;
	}


	SEG_TYPE* Pop(void)
	{
		if (pop_ptr >= N) return nullptr;
		SEG_TYPE* seg = buf[pop_ptr++];
		return seg;
	}
	PSEG* get_buf(void)
	{
		return buf;
	}
	size_t get_seg_nr(void) { return data_size; }

protected:
	void out_data(size_t start=0, size_t end=0)
	{
		if (end == 0) end = buf_size;
		wprintf_s(L"data (%lld - %lld): ", start, end-1);
		for (size_t ii = start; ii < end; ++ii)
		{
			wprintf_s(L"%d, ", buf[ii]->valid_blk_nr);
		}
		wprintf_s(L"\n");
	}

#if 0
	void partial_qsort(size_t left, size_t right, DWORD max, DWORD min)
	{

		if (left < right)
		{
			sort_count++;
			if (left + 1 == right)
			{
				if (buf[left]->valid_blk_nr > buf[right]->valid_blk_nr)
				{
					SEG_TYPE* p = buf[left];
					buf[left] = buf[right];
					buf[right] = p;
				}
			}
			else
			{
				//			wprintf_s(L"sort from %lld to %lld\n", left, right);
				DWORD pivolt = 12;
				if (pivolt > max || pivolt < min)
				{
					pivolt = buf[left]->valid_blk_nr;
				}
				size_t pi = partition(left, right, pivolt);

				partial_qsort(left, pi, min, pivolt);


				if (pi >= N)
				{
				}
				else
				{
//					partial_qsort(left, pi);
					partial_qsort(pi + 1, right, pivolt, max);
				}
				//			out_data(left, hight);
			}
		}
	}
	size_t partition(size_t left, size_t right, DWORD pivot)
	{
		size_t ll = left, rr = right;
		//		SEG_TYPE* key = buf[left];
		//		DWORD pivot = 12;
		while (left < right)
		{
			while (buf[right]->valid_blk_nr >= pivot && right > left) right--;
			while (buf[left]->valid_blk_nr < pivot && right > left) left++;
			if (right > left)
			{
				SEG_TYPE* p = buf[left];
				buf[left] = buf[right];
				buf[right] = p;
			}
			//			wprintf_s(L"key=%d, left=%lld, right=%lld\n", pivot, left, right);
			//			out_data(ll, rr+1);
		}
		return left;
	}
#else

	void partial_qsort(size_t left, size_t right)
	{
		if (left < right)
		{
			sort_count++;
			if (left + 1 == right)
			{
				if (buf[left]->valid_blk_nr > buf[right]->valid_blk_nr)
				{
					SEG_TYPE* p = buf[left];
					buf[left] = buf[right];
					buf[right] = p;
				}
			}
			else
			{
				//			wprintf_s(L"sort from %lld to %lld\n", left, right);
				size_t pi = partition(left, right);
				wprintf_s(L"sorting: %lld (%d) to %lld (%d), pivot: %lld (%d)\n", 
					left, buf[left]->valid_blk_nr, right, buf[right]->valid_blk_nr, pi, buf[pi]->valid_blk_nr);
				partial_qsort(left, pi);
				if (pi < N)
				{
					partial_qsort(pi + 1, right);
				}
				//			out_data(left, hight);
			}
		}
	}
	size_t partition(size_t left, size_t right)
	{
		size_t ll = left, rr = right;
		SEG_TYPE* key = buf[left];
		DWORD pivot = key->valid_blk_nr;
		while (left < right)
		{
			while (buf[right]->valid_blk_nr >= pivot && right > left) right--;
			if (right > left)
			{
				buf[left] = buf[right]; left++;
				buf[right] = key;
			}
			while (buf[left]->valid_blk_nr < pivot && right > left) left++;
			if (right > left)
			{
				buf[right] = buf[left]; right--;
				buf[left] = key;
			}
	//			if (right > left) buf[left] = key;
	//			wprintf_s(L"key=%d, left=%lld, right=%lld\n", pivot, left, right);
	//			out_data(ll, rr+1);
		}
		return left;
	}

#endif

protected:
	PSEG* buf;
	size_t buf_size, data_size;
	size_t pop_ptr;


public:
// 用于性能测试
	size_t low, high;
	int sort_count=0;		// 调用qsort次数
	DWORD min_val, max_val;
	DWORD pth;
};


/// ///////////////////////////////////////////////////////////////////////////////////////////////////////////////////

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
		if (m_free_tail == m_free_head)	{	THROW_ERROR(ERR_APP, L"free buffer full");	}
		SEG_INFO<BLOCK_TYPE>& seg = m_segments[seg_id];
		memset(&seg, 0xFF, sizeof(SEG_INFO<BLOCK_TYPE>));
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
		SEG_INFO<BLOCK_TYPE>& seg = m_segments[seg_id];
		TypedInvalidBlock<BLOCK_TYPE>(seg.blk_map[blk_id]);
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

	virtual bool InitSegmentManager(SEG_T segment_nr, SEG_T gc_lo, SEG_T gc_hi, int init_val=0xFFFFFFFF)
	{
		m_seg_nr = segment_nr;
		m_segments = new SEG_INFO<BLOCK_TYPE>[m_seg_nr];
		m_free_segs = new SEG_T[m_seg_nr];
		// 初始化，如果blk_map指向block_id，初始化为0xFF，如果指向指针，初始化为0
		memset(m_segments, init_val, sizeof(SEG_INFO<BLOCK_TYPE>) * m_seg_nr);
		//memset(m_segments, 0x00, sizeof(SEG_INFO<BLOCK_TYPE>) * m_seg_nr);
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
	friend class CSingleLogSimulator;
	SEG_T m_cur_segs[BT_TEMP_NR];
	SEG_T m_seg_nr=0, m_free_nr=0;
	FsHealthInfo* m_health = nullptr;
	SEG_T m_gc_lo, m_gc_hi;
private:
	SEG_T m_free_head=0, m_free_tail=0;
	SEG_T* m_free_segs = nullptr;
};

class inode_info;

// 描述LFS中的一个block。
//	模拟实际文件系统中的一个物理block。对于data block，不关心其中的数据，只保留他的逻辑地址。对于NODE，由链接指示物理数据。
class LFS_BLOCK_INFO : public CPageInfoBase
{
public:
	LFS_BLOCK_INFO(FID f, LBLK_T l) : nid(f), offset(l) {}
	LFS_BLOCK_INFO(void): nid(INVALID_BLK), offset(INVALID_BLK){}
public:
	FID nid;	// 文件ID或者node id, 0xFF时无效
	LBLK_T offset;	// 文件中的相对block，0xFF时，表示指向node
	inode_info* parent = nullptr;		// 父节点指针	
	UINT parent_offset = INVALID_BLK;			// 父节点表中的位移
};

class CInodeManager;

class CLfsSegmentManager : public CSegmentManagerBase<LFS_BLOCK_INFO>
{
public:
	virtual ~CLfsSegmentManager(void);
public:
	// 写入data block到segment, file_index 文件id, blk：文件中的相对block，temp温度
	//PHY_BLK WriteBlockToSeg(FID fid, LBLK_T blk, BLK_TEMP temp) {
	//	PHY_BLK phy_blk = WriteBlockToSeg(LFS_BLOCK_INFO(fid, blk), temp);
	//	return phy_blk;
	//}
	void CheckGarbageCollection(void)
	{
		if (m_free_nr < m_gc_lo) GarbageCollection();
	}
	virtual PHY_BLK WriteBlockToSeg(const LFS_BLOCK_INFO & lblk, BLK_TEMP temp);
	virtual void GarbageCollection(void);
	void DumpSegmentBlocks(const std::wstring& fn);
	virtual bool InitSegmentManager(SEG_T segment_nr, SEG_T gc_lo, SEG_T gc_hi, int init_val = 0xFFFFFFFF);


	friend class CSingleLogSimulator;
	FILE* m_gc_trace;
protected:
	CInodeManager* m_inodes = nullptr;

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

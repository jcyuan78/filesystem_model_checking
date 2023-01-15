///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
#pragma once

// ==== types ====
typedef size_t loff_t;

typedef UINT8	u8;
typedef UINT8	__u8;

typedef UINT16	u16;
typedef UINT16	__u16;

typedef UINT32	u32;
typedef UINT32	__u32;
typedef UINT32	__le32;
typedef INT32	__s32;

typedef UINT64	u64;
typedef UINT64	__le64;
typedef UINT64	__u64;
typedef INT64	s64;
typedef INT64	__s64;

typedef UINT64	sector_t;
typedef INT64	ssize_t;
typedef UINT64	blkcnt_t;
typedef unsigned long	pgoff_t;
typedef UINT64	time64_t;
typedef UINT32	fmode_t;
typedef UINT32	errseq_t;
typedef unsigned int gfp_t;
typedef UINT32	projid_t;

typedef unsigned short	umode_t;
typedef INT64	ktime_t;
typedef time64_t	timespec64;
//typedef unsigned /*__bitwise*/ xa_mark_t;
typedef UINT32 xa_mark_t;

//#define jiffies  0
#define jiffies (jcvos::GetTimeStamp())
#define time_after(a,b)		((long)((b) - (a)) < 0)
#define time_before(a,b)	time_after(b,a)
//#define HZ 1
//#define umode_t UINT32

#define __init
#define __user
#define unlikely(x)		(x)
#define likely(x)		(x)


//<YUAN> Copied from include/linux/kernel.h。
// 注意：此处的round_up()和round_down()仅去掉余数部分，相当于[x/y+1]*y和
// [x/y]*y。实际并没有缩小。并且此处的y要求是2的正幂次。另外，在f2fs format中使用道的round_up是需要缩小y倍的。
// 定义ROUND_UP()和ROUND_DOWN()表示需要缩小的情况

/* This looks more complex than it should be. But we need to get the type for the ~ right in round_down (it needs to
be as wide as the result!), and we want to evaluate the macro arguments just once each. */

/** round_down - round down to next specified power of 2
 * @x: the value to round
 * @y: multiple to round down to (must be a power of 2)
 *
 * Rounds @x down to next multiple of @y (which must be a power of 2). To perform arbitrary rounding down, use rounddown() below. */

//<YUAN> my defined
template <typename T> inline T DIV_ROUND_UP(T x, T y) { return (x + y - 1) / y; }

/* fls - find last set bit in word
 * @x: the word to search
 *
 * This is defined in a similar way as the libc and compiler builtin ffs, but returns the position of the most significant set bit.
 * fls(value) returns 0 if value is 0 or the position of the last set bit if value is nonzero. The last (most significant) bit is at position 32. */
// 找到做高位的 1
template <typename T> inline T roundup_pow_of_two(T x)
{
	static const int bits = sizeof(T) * 8;
	if (x == 0) return 0;
	T xx = x-1;
	int bitpos = 0;
	for (bitpos = 0; xx > 0; bitpos++, xx >>= 1);
//	bitpos++;
	return ((T)1) << bitpos;
}

/* generic data direction definitions */
#define READ			0
#define WRITE			1




#define BITS_PER_BYTE		(8)
//#define BITS_PER_LONG		(sizeof(long) *BITS_PER_BYTE)
static const int BITS_PER_LONG = (sizeof(long) * BITS_PER_BYTE);

//#ifdef CONFIG_64BIT
//#define BITS_PER_LONG	64
//#else
//#define BITS_PER_LONG	32
//#endif

template <typename T1>
size_t BITS_PER_T(T1 v = 0) { return sizeof(T1) * BITS_PER_BYTE; }

#define BITS_PER_TYPE(type)	(sizeof(type) * BITS_PER_BYTE)
#define BITS_TO_LONGS(nr)	DIV_ROUND_UP<long>(nr, BITS_PER_LONG)
#define BITS_TO_U64(nr)		DIV_ROUND_UP<UINT64>(nr, BITS_PER_TYPE(u64))
#define BITS_TO_U32(nr)		DIV_ROUND_UP<UINT32>(nr, BITS_PER_TYPE(u32))
#define BITS_TO_BYTES(nr)	DIV_ROUND_UP<char>(nr, BITS_PER_TYPE(char))
/* PAGE_SHIFT determines the page size */
#define PAGE_SHIFT		12
#define PAGE_SIZE		(1 << PAGE_SHIFT)
#define PAGE_MASK		(~(PAGE_SIZE-1))
#define SECTOR_PER_PAGE	8


/* assumes size > 256 */
inline unsigned int blksize_bits(unsigned int size)
{
	unsigned int bits = 8;
	do
	{
		bits++;
		size >>= 1;
	} while (size > 256);
	return bits;
}

#include "synchronize.h"

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// ==== 数据结构 ====
// -- radix_tree_root
//<YUAN> TODO 优化并实现radix结构
typedef std::map<UINT32, void*>	radix_tree_root;

template <typename T>
inline T* radix_tree_lookup(radix_tree_root * tree, UINT32 n)
{
	auto it = tree->find(n);
	if (it == tree->end()) return NULL;
	return (T*)(it->second);
}

template <typename T>
inline int radix_tree_insert(radix_tree_root* tree, UINT32 n, T* entry)
{
	auto ir = tree->insert(std::make_pair(n, entry));
	if (!ir.second) return -1;
	return 0;
}

template <typename T>
inline T * radix_tree_delete(radix_tree_root* tree, UINT index)
{
	T* temp = NULL;
	auto it = tree->find(index);
	if (it != tree->end())
	{
		temp = (T*)it->second;
		tree->erase(it);
	}
	return temp;
}

inline int radix_tree_preload(gfp_t gfp_mask) { return 0; }
inline int radix_tree_preload_end(void) { return 0; }

/*	radix_tree_gang_lookup - perform multiple lookup on a radix tree
 *	@root:		radix tree root
 *	@results:	where the results of the lookup are placed
 *	@first_index:	start the lookup from this key
 *	@max_items:	place up to this many items at *results
 *
 *	Performs an index-ascending scan of the tree for present items.  Places them at *@results and returns the number 
    of items which were placed at *@results.
 *
 *	The implementation is naive.
 *
 *	Like radix_tree_lookup, radix_tree_gang_lookup may be called under rcu_read_lock. In this case, rather than the
    returned results being an atomic snapshot of the tree at a single point in time, the semantics of an RCU protected
	gang lookup are as though multiple radix_tree_lookups have been issued in individual locks, and resultsstored in 
	'results'. */
template <typename T>
unsigned int radix_tree_gang_lookup(const radix_tree_root* root, T** results, unsigned long first_index, 
	unsigned int max_items)
{
	//struct radix_tree_iter iter;
//	void __rcu** slot;
	unsigned int ret = 0;

	if (unlikely(!max_items)) return 0;

	for (auto it = root->begin(); it != root->end(); ++it)
//	radix_tree_for_each_slot(slot, root, &iter, first_index)
	{
		if (it->first < first_index) continue;
//		results[ret] = rcu_dereference_raw(*slot);
		results[ret] = reinterpret_cast<T*>(it->second);
		if (!results[ret])			continue;
		//if (radix_tree_is_internal_node(results[ret]))
		//{
		//	slot = radix_tree_iter_retry(&iter);
		//	continue;
		//}
		if (++ret == max_items) break;
	}

	return ret;
}

#define INIT_RADIX_TREE(x, f)

//struct rb_node
//{
//	unsigned long __rb_parent_color;
//	struct rb_node* rb_right;
//	struct rb_node* rb_left;
//};

//struct rb_root
//{
//	struct rb_node* rb_node;
//};

//struct rb_root_cached
//{
//	rb_root rb_root;
//	rb_node* rb_leftmost;
//};

// ==== defination
// 原定义在linux的 stat.h中。且一下数值以8禁止表示
//#define _S_IFMT   0xF000 // File type mask
#define S_IFSOCK	0xC000
#define S_IFLNK		0xA000
//#define _S_IFREG  0x8000 // Regular
#define S_IFBLK		0x6000
//#define _S_IFDIR  0x4000 // Directory
//#define _S_IFCHR  0x2000 // Character special
#define S_IFIFO		0x1000	// Pipe
#define S_ISUID		0x0800
#define S_ISGID		0x0400
#define S_ISVTX		0x0200
//#define _S_IREAD  0x0100 // Read permission, owner
//#define _S_IWRITE 0x0080 // Write permission, owner
//#define _S_IEXEC  0x0040 // Execute/search permission, owner

//			8进制		16进制
//S_IFMT	0170000		F000
//S_IFSOCK	0140000		C000
//S_IFLNK	0120000		A000
//S_IFREG	0100000		8000
//S_IFBLK	0060000		6000
//S_IFDIR	0040000		4000
//S_IFCHR	0020000		2000
//S_IFIFO	0010000		1000	
//S_ISUID	0004000		0800
//S_ISGID	0002000		0400
//S_ISVTX	0001000		0200

#define S_ISLNK(m)	(((m) & S_IFMT) == S_IFLNK)
#define S_ISREG(m)	(((m) & S_IFMT) == S_IFREG)
#define S_ISDIR(m)	(((m) & S_IFMT) == S_IFDIR)
#define S_ISCHR(m)	(((m) & S_IFMT) == S_IFCHR)
#define S_ISBLK(m)	(((m) & S_IFMT) == S_IFBLK)
#define S_ISFIFO(m)	(((m) & S_IFMT) == S_IFIFO)
#define S_ISSOCK(m)	(((m) & S_IFMT) == S_IFSOCK)

#define S_IRWXU 0x00700
#define S_IRUSR 0x00400
#define S_IWUSR 0x00200
#define S_IXUSR 0x00100

#define S_IRWXG 0x00070
#define S_IRGRP 0x00040
#define S_IWGRP 0x00020
#define S_IXGRP 0x00010

#define S_IRWXO 0x00007
#define S_IROTH 0x00004
#define S_IWOTH 0x00002
#define S_IXOTH 0x00001

// ==== error codes

#define ENOMEDIUM	129	/* No medium found */
#define EMEDIUMTYPE	130	/* Wrong medium type */
#ifndef ECANCELED 
#define	ECANCELED	105	/* Operation Cancelled */	//和Windows定义保持一致
#endif
#define	ENOKEY		132	/* Required key not available */
#define	EKEYEXPIRED	133	/* Key has expired */
#define	EKEYREVOKED	134	/* Key has been revoked */
#define	EKEYREJECTED	135	/* Key was rejected by service */
#ifndef ESTALE
#define ESTALE                        10070L
#endif

/*
 * These should never be seen by user programs.  To return one of ERESTART*
 * codes, signal_pending() MUST be set.  Note that ptrace can observe these
 * at syscall exit tracing, but they will never be left for the debugged user
 * process to see.
 */
#define ERESTARTSYS	512
#define ERESTARTNOINTR	513
#define ERESTARTNOHAND	514	/* restart if no handler.. */
#define ENOIOCTLCMD	515	/* No ioctl command */
#define ERESTART_RESTARTBLOCK 516 /* restart by calling sys_restart_syscall */
#define EPROBE_DEFER	517	/* Driver requests probe retry */
#define EOPENSTALE	518	/* open found a stale dentry */
#define ENOPARAM	519	/* Parameter not supported */

 /* Defined for the NFSv3 protocol */
#define EBADHANDLE	521	/* Illegal NFS file handle */
#define ENOTSYNC	522	/* Update synchronization mismatch */
#define EBADCOOKIE	523	/* Cookie is stale */
#define ENOTSUPP	524	/* Operation is not supported */
#define ETOOSMALL	525	/* Buffer or request is too small */
#define ESERVERFAULT	526	/* An untranslatable error occurred */
#define EBADTYPE	527	/* Type not supported by server */
#define EJUKEBOX	528	/* Request initiated, but will not complete before timeout */
#define EIOCBQUEUED	529	/* iocb queued, will get completion event */
#define ERECALLCONFLICT	530	/* conflict with recalled state */

#define MAX_ERRNO	(4095)

template <typename T> inline T* ERR_PTR(INT64 e) { return reinterpret_cast<T*>(e); }

inline UINT32 prandom_u32(void)
{
	int a1 = rand(), a2 = rand();
	return MAKELONG(a1, a2);
}

// ==== bit operation: defined in "bitops.h"

//inline bool constant_test_bit(int nr, const void* addr)
//{
//	const u32* p = (const u32*)addr;
//	return ((1UL << (nr & 31)) & (p[nr >> 5])) != 0;
//}


// __test_bit(), __clear_bit(), __set_bit()，以__开头的函数，处理bit数组。即输入参数可以超过一个word的长度。
// test_bit(), clear_bit(), set_bit()，没有__开头的函数，处理一个word的bit。




#define test_and_set_bit_lock test_and_set_bit













//inline void set_bit(int nr, void* addr)
//{
//	u32* p = (u32*)addr;
//	p[nr >> 5] |= (1UL << (nr & 31));
//}

/**
 * __set_bit - Set a bit in memory
 * @nr: the bit to set
 * @addr: the address to start counting from
 *
 * Unlike set_bit(), this function is non-atomic and may be reordered. If it's called on the same region of memory
   simultaneously, the effect may be that only one operation succeeds.  */
inline void __set_bit(int nr, volatile void* addr)
{
	*((__u32*)addr + (nr >> 5)) |= (1 << (nr & 31));
}

inline void __clear_bit(int nr, void* addr)
{
	u32* p = (u32*)addr;
	p[nr >> 5] &= ~(1UL << (nr & 31));
}

inline bool __test_bit(int nr, const void* addr)
{
	const u32* p = (const u32*)addr;
	return ((1UL << (nr & 31)) & (p[nr >> 5])) != 0;
}

/* __test_and_clear_bit - Clear a bit and return its old value
 * @nr: Bit to clear
 * @addr: Address to count from
 *
 * This operation is non-atomic and can be reordered. If two examples of this operation race, one can appear to succeed but actually fail.  You must protect multiple accesses with a lock. */
static inline int __test_and_clear_bit(int nr, volatile void* addr)
{
	__u32* p = (__u32*)addr + (nr >> 5);
	__u32 m = 1 << (nr & 31);
	int oldbitset = (*p & m) != 0;

	*p &= ~m;
	return oldbitset;
}

inline int __test_and_set_bit(int nr, volatile void* addr)
{
	__u32* p = (__u32*)addr + (nr >> 5);
	__u32 m = 1 << (nr & 31);
	int oldbitset = (*p & m) != 0;
	*p |= m;
	return oldbitset;
}


template <int L, typename T> inline void set_bit_by_size(int nr, volatile T& addr);
template <int L, typename T> inline void set_bit_by_size<4, T>(int nr, volatile T& addr)
{
	InterlockedOr((long*)&addr, (((long)(1)) << nr));
}


template <typename T> inline void set_bit_(int nr, T& addr)
{
	JCASSERT(nr < sizeof(T) * 8);	//不满足这个条件应该换用__set_bit()函数
	addr |= (((T)(1)) << nr);
}

template <typename T> inline void set_bit(int nr, volatile T& addr)
{
//	InterlockedOr((long*)&addr, (((long)(1)) << nr));
	set_bit_by_size<sizeof(T)>(nr, addr);
}

template <typename T> inline void clear_bit_(int nr, T& addr)
{
	JCASSERT(nr < sizeof(T) * 8);	//不满足这个条件应该换用__clear_bit()函数
	addr &= ~((T)1 << (nr));
}

template <int L, typename T> inline void clear_bit_by_size(int nr, volatile T& addr);
template <int L, typename T> inline void clear_bit_by_size<4, T>(int nr, volatile T& addr)
{
	InterlockedAnd((long*)&addr, ~((long)1 << (nr)));
}


template <typename T> inline void clear_bit(int nr, volatile T& addr)
{
	clear_bit_by_size<sizeof(T)>(nr, addr);
}

template <typename T> inline bool test_bit(int nr, volatile T& addr)
{
	JCASSERT(nr < sizeof(T) * 8);	//不满足这个条件应该换用__clear_bit()函数
	return (((T)1 << (nr)) & addr) != 0;
}

/** test_and_clear_bit - Clear a bit and return its old value
 * @nr: Bit to clear
 * @addr: Address to count from */
template <typename T> inline int test_and_clear_bit(int nr, volatile T& addr)
{
	JCASSERT(nr < sizeof(T) * 8);	//不满足这个条件应该换用__clear_bit()函数
//	T old = addr;
	T mask = (T)1 << nr;
	//addr &= ~mask;
	T old = InterlockedAnd((long*)&addr, (~mask));
	return (old & mask) != 0;
}
/* test_and_set_bit - Set a bit and return its old value
 * @nr: Bit to set
 * @addr: Address to count from
 *
 * This operation is atomic and cannot be reordered. It also implies the acquisition side of the memory barrier. */
template <typename T> inline int test_and_set_bit(int nr, volatile T& addr)
{
	JCASSERT(nr < sizeof(T) * 8);	//不满足这个条件应该换用__clear_bit()函数
	T mask = ((T)1 << nr);
//	T old = addr;
//	addr = old | mask;
	T old = InterlockedOr((long*)&addr, mask);
	return (old & mask) != 0;
}

template <typename T> inline T set_mask_bits(T* ptr, T mask, T bits)
{
	const T mask__ = (mask), bits__ = (bits);
	T old__, new__;
	do
	{
		//old__ = READ_ONCE(*(ptr));
		old__ = *(ptr);
		new__ = (old__ & ~mask__) | bits__;
	} while (InterlockedCompareExchange(ptr, new__, old__) != old__);
	//} while (cmpxchg(ptr, old__, new__) != old__);
	return old__;
}

//<YUAN> Intel 平台上字节顺序一致，简化处理
#define __set_bit_le(x, y)		__set_bit(x, y)
#define __clear_bit_le(x, y)	__clear_bit(x, y)


// ==== block device: defined in "linux/blkdev.h"
#define bdev_is_zoned(x)	false
#define bdev_read_only(x)	false
//inline bool bdev_is_zoned(block_device* bdev)
//{
//	struct request_queue* q = bdev->bd_isk->queue;
//	if (q && (q->limits.zoned == BLK_ZONED_HA || q->limits.zoned == BLK_ZONED_MA))
//		return true;
//	return false;
//}

template <typename T1, typename T2> T1* container_of(T2 * ptr, size_t offset)
{
	T1* c = (T1*)((BYTE*)(ptr)-offset);
	return c;
}

/* Bits in mapping->flags. */ // <YUAN> from linux/pagemap.h
enum mapping_flags
{
	AS_EIO = 0,	/* IO error on async write */
	AS_ENOSPC = 1,	/* ENOSPC on async write */
	AS_MM_ALL_LOCKS = 2,	/* under mm_take_all_locks() */
	AS_UNEVICTABLE = 3,	/* e.g., ramdisk, SHM_LOCK */
	AS_EXITING = 4, 	/* final truncate in progress */
	/* writeback related tags are not used */
	AS_NO_WRITEBACK_TAGS = 5,
	AS_THP_SUPPORT = 6,	/* THPs supported */
};

#define WARN_ON(x)	JCASSERT(!(x))
#define IS_ERR(x)	((INT64)(x)<=0)
#define PTR_ERR(x)	(int)((uintptr_t)x)
#define BUG_ON(x)	JCASSERT(!(x))
#define IS_ERR_VALUE(x) unlikely((UINT64)(void *)(x) >= (UINT64)-MAX_ERRNO)

static inline bool IS_ERR_OR_NULL(const void* ptr)
{
	return unlikely(!ptr) || IS_ERR_VALUE((UINT64)ptr);
}

static inline int PTR_ERR_OR_ZERO(const void* ptr)
{
	if (IS_ERR(ptr))		return PTR_ERR(ptr);
	else		return 0;
}


#define UNSUPPORT_1(tt)	{JCASSERT(0); return (tt)(-EOPNOTSUPP);}
#define UNSUPPORT_0		{JCASSERT(0); }


#define rcu_assign_pointer(p, v)	(p=v)

// ==== from include/uapi/linux/ioprio.h

/*
 * Gives us 8 prio classes with 13-bits of data for each class
 */
#define IOPRIO_CLASS_SHIFT	13
#define IOPRIO_CLASS_MASK	0x07
#define IOPRIO_PRIO_MASK	((1UL << IOPRIO_CLASS_SHIFT) - 1)

#define IOPRIO_PRIO_CLASS(ioprio)	\
	(((ioprio) >> IOPRIO_CLASS_SHIFT) & IOPRIO_CLASS_MASK)
#define IOPRIO_PRIO_DATA(ioprio)	((ioprio) & IOPRIO_PRIO_MASK)
#define IOPRIO_PRIO_VALUE(class_type, data)	\
	((((class_type) & IOPRIO_CLASS_MASK) << IOPRIO_CLASS_SHIFT) | \
	 ((data) & IOPRIO_PRIO_MASK))

 /*
  * These are the io priority groups as implemented by the BFQ and mq-deadline
  * schedulers. RT is the realtime class, it always gets premium service. For
  * ATA disks supporting NCQ IO priority, RT class IOs will be processed using
  * high priority NCQ commands. BE is the best-effort scheduling class, the
  * default for any process. IDLE is the idle scheduling class, it is only
  * served when no one else is using the disk.
  */
enum
{
	IOPRIO_CLASS_NONE,
	IOPRIO_CLASS_RT,
	IOPRIO_CLASS_BE,
	IOPRIO_CLASS_IDLE,
};

/*
 * The RT and BE priority classes both support up to 8 priority levels.
 */
#define IOPRIO_NR_LEVELS	8
#define IOPRIO_BE_NR		IOPRIO_NR_LEVELS

enum
{
	IOPRIO_WHO_PROCESS = 1,
	IOPRIO_WHO_PGRP,
	IOPRIO_WHO_USER,
};

/*
 * Fallback BE priority level.
 */
#define IOPRIO_NORM	4
#define IOPRIO_BE_NORM	IOPRIO_NORM

// ==== config
#define CONFIG_BLOCK
#define CONFIG_UNICODE
#define CONFIG_64BIT
#define WANT_PAGE_VIRTUAL
#define CONFIG_GROUP_WRITEBACK
#define CONFIG_IDLE_PAGE_TRACKING
//#define CONFIG_FS_VERITY
#define CONFIG_BASE_SMALL		(1)

#define CONFIG_NUMA

//#define CONFIG_TINY_RCU


template <typename T>
inline void ZeroInit(T& d) { memset(&d, 0, sizeof(T)); }


/* Used in tsk->state: */
#define TASK_RUNNING				0x0000
#define TASK_INTERRUPTIBLE			0x0001
#define TASK_UNINTERRUPTIBLE		0x0002
#define __TASK_STOPPED				0x0004
#define __TASK_TRACED				0x0008
/* Used in tsk->exit_state: */
#define EXIT_DEAD					0x0010
#define EXIT_ZOMBIE					0x0020
#define EXIT_TRACE			(EXIT_ZOMBIE | EXIT_DEAD)
/* Used in tsk->state again: */
#define TASK_PARKED					0x0040
#define TASK_DEAD					0x0080
#define TASK_WAKEKILL				0x0100
#define TASK_WAKING					0x0200
#define TASK_NOLOAD					0x0400
#define TASK_NEW					0x0800
#define TASK_POSITIVE				0x1000		// 等待bit时，当bit为1时返回，否则bit为0时返回
#define TASK_STATE_MAX				0x2000

/* Convenience macros for the sake of set_current_state: */
#define TASK_KILLABLE			(TASK_WAKEKILL | TASK_UNINTERRUPTIBLE)
#define TASK_STOPPED			(TASK_WAKEKILL | __TASK_STOPPED)
#define TASK_TRACED			(TASK_WAKEKILL | __TASK_TRACED)

#define TASK_IDLE			(TASK_UNINTERRUPTIBLE | TASK_NOLOAD)

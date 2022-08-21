///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/* SPDX-License-Identifier: GPL-2.0 */
#pragma once

#include "linux_comm.h"
//#include <linux/mm_types_task.h>
//
//#include <linux/auxvec.h>
#include "list.h"
#include "address-space.h"
//#include "fs.h"
//#include <linux/spinlock.h>
//#include <linux/rbtree.h>
//#include <linux/rwsem.h>
//#include <linux/completion.h>
//#include <linux/cpumask.h>
//#include <linux/uprobes.h>
//#include <linux/page-flags-layout.h>
//#include <linux/workqueue.h>
//#include <linux/seqlock.h>
//
//#include <asm/mmu.h>

#ifndef AT_VECTOR_SIZE_ARCH
#define AT_VECTOR_SIZE_ARCH 0
#endif
#define AT_VECTOR_SIZE (2*(AT_VECTOR_SIZE_ARCH + AT_VECTOR_SIZE_BASE + 1))

#define INIT_PASID	0

class address_space;
struct mem_cgroup;


/* Each physical page in the system has a struct page associated with it to keep track of whatever it is we are using the page for at the moment. Note that we have no way to track which tasks are using a page, though if it is a pagecache page, rmap structures can tell us who is mapping it.
 *
 * If you allocate the page using alloc_pages(), you can use some of the space in struct page for your own purposes. The five words in the main union are available, except for bit 0 of the first word which must be kept clear.  Many users use this word to store a pointer to an object which is guaranteed to be aligned.  If you use the same storage as page->mapping, you must restore it to NULL before freeing the page.
 *
 * If your page will not be mapped to userspace, you can also use the four bytes in the mapcount union, but you must call page_mapcount_reset() before freeing it.
 *
 * If you want to use the refcount field, it must be used in such a way that other CPUs temporarily incrementing and then decrementing the refcount does not cause problems.  On receiving the page from alloc_pages(), the refcount will be positive.
 *
 * If you allocate pages of order > 0, you can use some of the fields in each subpage, but you may need to restore some of their values afterwards.
 *
 * SLUB uses cmpxchg_double() to atomically update its freelist and counters.  That requires that freelist & counters be adjacent and double-word aligned.  We align all struct pages to double-word boundaries, and ensure that 'freelist' is aligned within the struct. */
#ifdef CONFIG_HAVE_ALIGNED_STRUCT_PAGE
#define _struct_page_alignment	__aligned(2 * sizeof(unsigned long))
#else
#define _struct_page_alignment
#endif

struct page 
{
public:
	page(void);
	~page(void);

	void set_mark(xa_mark_t mark) { set_bit(mark, &m_mark); }
	bool is_marked(xa_mark_t mark) { return test_bit(mark, &m_mark); }
	void clear_mark(xa_mark_t mark) { clear_bit(mark, &m_mark); }
protected:
	UINT32 m_mark =0;
public:
	unsigned long flags;		/* Atomic flags, some possibly updated asynchronously */
	/* Five words (20/40 bytes) are available in this union.
	 * WARNING: bit 0 of the first word is used for PageTail(). That means the other users of this union MUST NOT use
	 the bit to avoid collision and false-positive PageTail().	 */
	union 
	{
		struct {	/* Page cache and anonymous pages */
			/** @lru: Pageout list, eg. active_list protected by lruvec->lru_lock.  Sometimes used as a generic list by the page owner.			 */
			list_head lru;
			/* See page-flags.h for PAGE_MAPPING_FLAGS */
			address_space *mapping;
			pgoff_t index;		/* Our offset within mapping. */
			/** @private: Mapping-private opaque data. Usually used for buffer_heads if PagePrivate. Used for p_entry_t if PageSwapCache. Indicates order in the buddy system if PageBuddy.	 */
			unsigned long private_data;
		};

		struct 
		{	/* page_pool used by netstack */
			/** @dma_addr: might require a 64-bit value on 32-bit architectures.	 */
			unsigned long dma_addr[2];
		};

		struct {	/* slab, slob and slub */
			union {
				struct list_head slab_list;
				struct {	/* Partial pages */
					struct page *next;
#ifdef CONFIG_64BIT
					int pages;	/* Nr of pages left */
					int pobjects;	/* Approximate count */
#else
					short int pages;
					short int pobjects;
#endif
				};
			};
			struct kmem_cache *slab_cache; /* not slob */
			/* Double-word boundary */
			void *freelist;		/* first free object */
			union {
				void *s_mem;	/* slab: first object */
				unsigned long counters;		/* SLUB */
				struct {			/* SLUB */
					unsigned inuse:16;
					unsigned objects:15;
					unsigned frozen:1;
				};
			};
		};
		struct {	/* Tail pages of compound page */
			unsigned long compound_head;	/* Bit zero is set */

			/* First tail page only */
			unsigned char compound_dtor;
			unsigned char compound_order;
			atomic_t compound_mapcount;
			unsigned int compound_nr; /* 1 << compound_order */
		};
		struct {	/* Second tail page of compound page */
			unsigned long _compound_pad_1;	/* compound_head */
			atomic_t hpage_pinned_refcount;
			/* For both global and memcg */
			struct list_head deferred_list;
		};
#if 0 //TODO
		struct {	/* Page table pages */
			unsigned long _pt_pad_1;	/* compound_head */
			pgtable_t pmd_huge_pte; /* protected by page->ptl */
			unsigned long _pt_pad_2;	/* mapping */
			union {
				struct mm_struct *pt_mm; /* x86 pgds only */
				atomic_t pt_frag_refcount; /* powerpc */
			};
#if ALLOC_SPLIT_PTLOCKS
			spinlock_t *ptl;
#else
			spinlock_t ptl;
#endif
		};
#endif //TODO
		struct {	/* ZONE_DEVICE pages */
			/** @pgmap: Points to the hosting device page map. */
			struct dev_pagemap *pgmap;
			void *zone_device_data;
			/* ZONE_DEVICE private pages are counted as being mapped so the next 3 words hold the mapping, index, and private fields from the source anonymous or page cache page while the page is migrated to device private memory. ZONE_DEVICE MEMORY_DEVICE_FS_DAX pages alsouse the mapping, index, and private fields when pmem backed DAX files are mapped. */
		};
		/** @rcu_head: You can use this to free a page by RCU. */
//		struct rcu_head rcu_head;
	};

	union {		/* This union is 4 bytes in size. */
	/* If the page can be mapped to userspace, encodes the number of times this page is referenced by a page table. */
		atomic_t _mapcount;
		/* If the page is neither PageSlab nor mappable to userspace, the value stored here may help determine what this page is used for.  See page-flags.h for a list of page types which are currently stored here. */
		unsigned int page_type;
		unsigned int active;	/* SLAB */
		int units;				/* SLOB */
	};

public:
	inline void ref_add(int count) { atomic_add(count, &_refcount); }
	inline atomic_t ref_count(void) { return atomic_read(&_refcount); }

	inline void get_page(void) 
	{
		//page = compound_head(page);
	/* Getting a normal page or the head of a compound page requires to already have an elevated page->_refcount.	 */
		//	VM_BUG_ON_PAGE(page_ref_zero_or_close_to_overflow(page), page);
		//	page_ref_inc(page);
		JCASSERT(_refcount > 0);
		atomic_inc(&_refcount); 
	}
	void put_page(void);

protected:
	/* Usage count. *DO NOT USE DIRECTLY*. See page_ref.h */
	atomic_t _refcount;
	inline int put_page_testzero(void);
	void inline __put_page(void)
	{
#if 0
		if (is_zone_device_page(ppage))
		{
			put_dev_pagemap(ppage->pgmap);
			/* The page belongs to the device that created pgmap. Do not return it to page allocator. */
			return;
	}
		if (unlikely(PageCompound(ppage)))		__put_compound_page(ppage);
		else		__put_single_page(ppage);
#endif
		//	LOG_DEBUG(L"page=0x%llX, add=0x%llX, ref=%d", ppage, ppage->virtual_add, ppage->_refcount);

		delete this;
}

public:

#ifdef CONFIG_MEMCG
	unsigned long memcg_data;
#endif

	/* On machines where all RAM is mapped into kernel address space, we can simply calculate the virtual address. On machines with highmem some memory is mapped into kernel virtual memory dynamically, so we need a place to store		that address. Note that this field could be 16 bits on x86 ... ;)
	Architectures with slow multiplication can define  WANT_PAGE_VIRTUAL in asm/page.h */
#if defined(WANT_PAGE_VIRTUAL)
	void *virtual_add;			/* Kernel virtual address (NULL if not kmapped, ie. highmem) */
#endif /* WANT_PAGE_VIRTUAL */

#ifdef _DEBUG
	//保存自动分配的地址，以检查时候有变化
	void* back_add;
	//debug 信息
	UINT32	m_block_addr;			// page所对应的ondisk地址
	std::wstring m_type;			// page的类型：inode，data，其他
	std::wstring m_description;		
#endif

#ifdef LAST_CPUPID_NOT_IN_PAGE_FLAGS
	int _last_cpupid;
#endif
};

class CPageManager
{

};


// ==== page ====

//inline void get_page(page* pp)
//{
////	page = compound_head(page);
//	/* Getting a normal page or the head of a compound page requires to already have an elevated page->_refcount.	 */
////	VM_BUG_ON_PAGE(page_ref_zero_or_close_to_overflow(page), page);
////	page_ref_inc(page);
//	atomic_inc(&pp->_refcount);
//}

inline address_space* page_mapping(page* pp)
{
	return pp->mapping;
}

/*
 * Return the pagecache index of the passed page.  Regular pagecache pages use ->index whereas swapcache pages use 
   swp_offset(->private) */
static inline pgoff_t page_index(struct page* page)
{
	//if (unlikely(PageSwapCache(page)))
	//	return __page_file_index(page);
	return page->index;
}

#if 0

static inline atomic_t *compound_mapcount_ptr(struct page *page)
{
	return &page[1].compound_mapcount;
}

static inline atomic_t *compound_pincount_ptr(struct page *page)
{
	return &page[2].hpage_pinned_refcount;
}
#endif

/* Used for sizing the vmemmap region on some architectures */
#define STRUCT_PAGE_MAX_SHIFT	(order_base_2(sizeof(struct page)))

#define PAGE_FRAG_CACHE_MAX_SIZE	__ALIGN_MASK(32768, ~PAGE_MASK)
#define PAGE_FRAG_CACHE_MAX_ORDER	get_order(PAGE_FRAG_CACHE_MAX_SIZE)

#define page_private(page)		((page)->private_data)

static inline void set_page_private(struct page *page, unsigned long private_data)
{
	page->private_data = private_data;
}
#if 0
struct page_frag_cache {
	void * va;
#if (PAGE_SIZE < PAGE_FRAG_CACHE_MAX_SIZE)
	__u16 offset;
	__u16 size;
#else
	__u32 offset;
#endif
	/* we maintain a pagecount bias, so that we dont dirty cache line
	 * containing page->_refcount every time we allocate a fragment.
	 */
	unsigned int		pagecnt_bias;
	bool pfmemalloc;
};

typedef unsigned long vm_flags_t;

/*
 * A region containing a mapping of a non-memory backed file under NOMMU
 * conditions.  These are held in a global tree and are pinned by the VMAs that
 * map parts of them.
 */
struct vm_region {
	struct rb_node	vm_rb;		/* link in global region tree */
	vm_flags_t	vm_flags;	/* VMA vm_flags */
	unsigned long	vm_start;	/* start address of region */
	unsigned long	vm_end;		/* region initialised to here */
	unsigned long	vm_top;		/* region allocated to here */
	unsigned long	vm_pgoff;	/* the offset in vm_file corresponding to vm_start */
	struct file	*vm_file;	/* the backing file or NULL */

	int		vm_usage;	/* region usage count (access under nommu_region_sem) */
	bool		vm_icache_flushed : 1; /* true if the icache has been flushed for
						* this region */
};

#ifdef CONFIG_USERFAULTFD
#define NULL_VM_UFFD_CTX ((struct vm_userfaultfd_ctx) { NULL, })
struct vm_userfaultfd_ctx {
	struct userfaultfd_ctx *ctx;
};
#else /* CONFIG_USERFAULTFD */
#define NULL_VM_UFFD_CTX ((struct vm_userfaultfd_ctx) {})
struct vm_userfaultfd_ctx {};
#endif /* CONFIG_USERFAULTFD */

/*
 * This struct describes a virtual memory area. There is one of these
 * per VM-area/task. A VM area is any part of the process virtual memory
 * space that has a special rule for the page-fault handlers (ie a shared
 * library, the executable area etc).
 */
struct vm_area_struct {
	/* The first cache line has the info for VMA tree walking. */

	unsigned long vm_start;		/* Our start address within vm_mm. */
	unsigned long vm_end;		/* The first byte after our end address
					   within vm_mm. */

	/* linked list of VM areas per task, sorted by address */
	struct vm_area_struct *vm_next, *vm_prev;

	struct rb_node vm_rb;

	/*
	 * Largest free memory gap in bytes to the left of this VMA.
	 * Either between this VMA and vma->vm_prev, or between one of the
	 * VMAs below us in the VMA rbtree and its ->vm_prev. This helps
	 * get_unmapped_area find a free area of the right size.
	 */
	unsigned long rb_subtree_gap;

	/* Second cache line starts here. */

	struct mm_struct *vm_mm;	/* The address space we belong to. */

	/*
	 * Access permissions of this VMA.
	 * See vmf_insert_mixed_prot() for discussion.
	 */
	pgprot_t vm_page_prot;
	unsigned long vm_flags;		/* Flags, see mm.h. */

	/*
	 * For areas with an address space and backing store,
	 * linkage into the address_space->i_mmap interval tree.
	 */
	struct {
		struct rb_node rb;
		unsigned long rb_subtree_last;
	} shared;

	/*
	 * A file's MAP_PRIVATE vma can be in both i_mmap tree and anon_vma
	 * list, after a COW of one of the file pages.	A MAP_SHARED vma
	 * can only be in the i_mmap tree.  An anonymous MAP_PRIVATE, stack
	 * or brk vma (with NULL file) can only be in an anon_vma list.
	 */
	struct list_head anon_vma_chain; /* Serialized by mmap_lock &
					  * page_table_lock */
	struct anon_vma *anon_vma;	/* Serialized by page_table_lock */

	/* Function pointers to deal with this struct. */
	const struct vm_operations_struct *vm_ops;

	/* Information about our backing store: */
	unsigned long vm_pgoff;		/* Offset (within vm_file) in PAGE_SIZE
					   units */
	struct file * vm_file;		/* File we map to (can be NULL). */
	void * vm_private_data;		/* was vm_pte (shared mem) */

#ifdef CONFIG_SWAP
	atomic_long_t swap_readahead_info;
#endif
#ifndef CONFIG_MMU
	struct vm_region *vm_region;	/* NOMMU mapping region */
#endif
#ifdef CONFIG_NUMA
	struct mempolicy *vm_policy;	/* NUMA policy for the VMA */
#endif
	struct vm_userfaultfd_ctx vm_userfaultfd_ctx;
} __randomize_layout;

struct core_thread {
	struct task_struct *task;
	struct core_thread *next;
};

struct core_state {
	atomic_t nr_threads;
	struct core_thread dumper;
	struct completion startup;
};

struct kioctx_table;
struct mm_struct {
	struct {
		struct vm_area_struct *mmap;		/* list of VMAs */
		struct rb_root mm_rb;
		u64 vmacache_seqnum;                   /* per-thread vmacache */
#ifdef CONFIG_MMU
		unsigned long (*get_unmapped_area) (struct file *filp,
				unsigned long addr, unsigned long len,
				unsigned long pgoff, unsigned long flags);
#endif
		unsigned long mmap_base;	/* base of mmap area */
		unsigned long mmap_legacy_base;	/* base of mmap area in bottom-up allocations */
#ifdef CONFIG_HAVE_ARCH_COMPAT_MMAP_BASES
		/* Base adresses for compatible mmap() */
		unsigned long mmap_compat_base;
		unsigned long mmap_compat_legacy_base;
#endif
		unsigned long task_size;	/* size of task vm space */
		unsigned long highest_vm_end;	/* highest vma end address */
		pgd_t * pgd;

#ifdef CONFIG_MEMBARRIER
		/**
		 * @membarrier_state: Flags controlling membarrier behavior.
		 *
		 * This field is close to @pgd to hopefully fit in the same
		 * cache-line, which needs to be touched by switch_mm().
		 */
		atomic_t membarrier_state;
#endif

		/**
		 * @mm_users: The number of users including userspace.
		 *
		 * Use mmget()/mmget_not_zero()/mmput() to modify. When this
		 * drops to 0 (i.e. when the task exits and there are no other
		 * temporary reference holders), we also release a reference on
		 * @mm_count (which may then free the &struct mm_struct if
		 * @mm_count also drops to 0).
		 */
		atomic_t mm_users;

		/**
		 * @mm_count: The number of references to &struct mm_struct
		 * (@mm_users count as 1).
		 *
		 * Use mmgrab()/mmdrop() to modify. When this drops to 0, the
		 * &struct mm_struct is freed.
		 */
		atomic_t mm_count;

		/**
		 * @has_pinned: Whether this mm has pinned any pages.  This can
		 * be either replaced in the future by @pinned_vm when it
		 * becomes stable, or grow into a counter on its own. We're
		 * aggresive on this bit now - even if the pinned pages were
		 * unpinned later on, we'll still keep this bit set for the
		 * lifecycle of this mm just for simplicity.
		 */
		atomic_t has_pinned;

#ifdef CONFIG_MMU
		atomic_long_t pgtables_bytes;	/* PTE page table pages */
#endif
		int map_count;			/* number of VMAs */

		spinlock_t page_table_lock; /* Protects page tables and some
					     * counters
					     */
		/*
		 * With some kernel config, the current mmap_lock's offset
		 * inside 'mm_struct' is at 0x120, which is very optimal, as
		 * its two hot fields 'count' and 'owner' sit in 2 different
		 * cachelines,  and when mmap_lock is highly contended, both
		 * of the 2 fields will be accessed frequently, current layout
		 * will help to reduce cache bouncing.
		 *
		 * So please be careful with adding new fields before
		 * mmap_lock, which can easily push the 2 fields into one
		 * cacheline.
		 */
		struct rw_semaphore mmap_lock;

		struct list_head mmlist; /* List of maybe swapped mm's.	These
					  * are globally strung together off
					  * init_mm.mmlist, and are protected
					  * by mmlist_lock
					  */


		unsigned long hiwater_rss; /* High-watermark of RSS usage */
		unsigned long hiwater_vm;  /* High-water virtual memory usage */

		unsigned long total_vm;	   /* Total pages mapped */
		unsigned long locked_vm;   /* Pages that have PG_mlocked set */
		atomic64_t    pinned_vm;   /* Refcount permanently increased */
		unsigned long data_vm;	   /* VM_WRITE & ~VM_SHARED & ~VM_STACK */
		unsigned long exec_vm;	   /* VM_EXEC & ~VM_WRITE & ~VM_STACK */
		unsigned long stack_vm;	   /* VM_STACK */
		unsigned long def_flags;

		/**
		 * @write_protect_seq: Locked when any thread is write
		 * protecting pages mapped by this mm to enforce a later COW,
		 * for instance during page table copying for fork().
		 */
		seqcount_t write_protect_seq;

		spinlock_t arg_lock; /* protect the below fields */

		unsigned long start_code, end_code, start_data, end_data;
		unsigned long start_brk, brk, start_stack;
		unsigned long arg_start, arg_end, env_start, env_end;

		unsigned long saved_auxv[AT_VECTOR_SIZE]; /* for /proc/PID/auxv */

		/*
		 * Special counters, in some configurations protected by the
		 * page_table_lock, in other configurations by being atomic.
		 */
		struct mm_rss_stat rss_stat;

		struct linux_binfmt *binfmt;

		/* Architecture-specific MM context */
		mm_context_t context;

		unsigned long flags; /* Must use atomic bitops to access */

		struct core_state *core_state; /* coredumping support */

#ifdef CONFIG_AIO
		spinlock_t			ioctx_lock;
		struct kioctx_table __rcu	*ioctx_table;
#endif
#ifdef CONFIG_MEMCG
		/*
		 * "owner" points to a task that is regarded as the canonical
		 * user/owner of this mm. All of the following must be true in
		 * order for it to be changed:
		 *
		 * current == mm->owner
		 * current->mm != mm
		 * new_owner->mm == mm
		 * new_owner->alloc_lock is held
		 */
		struct task_struct __rcu *owner;
#endif
		struct user_namespace *user_ns;

		/* store ref to file /proc/<pid>/exe symlink points to */
		struct file __rcu *exe_file;
#ifdef CONFIG_MMU_NOTIFIER
		struct mmu_notifier_subscriptions *notifier_subscriptions;
#endif
#if defined(CONFIG_TRANSPARENT_HUGEPAGE) && !USE_SPLIT_PMD_PTLOCKS
		pgtable_t pmd_huge_pte; /* protected by page_table_lock */
#endif
#ifdef CONFIG_NUMA_BALANCING
		/*
		 * numa_next_scan is the next time that the PTEs will be marked
		 * pte_numa. NUMA hinting faults will gather statistics and
		 * migrate pages to new nodes if necessary.
		 */
		unsigned long numa_next_scan;

		/* Restart point for scanning and setting pte_numa */
		unsigned long numa_scan_offset;

		/* numa_scan_seq prevents two threads setting pte_numa */
		int numa_scan_seq;
#endif
		/*
		 * An operation with batched TLB flushing is going on. Anything
		 * that can move process memory needs to flush the TLB when
		 * moving a PROT_NONE or PROT_NUMA mapped page.
		 */
		atomic_t tlb_flush_pending;
#ifdef CONFIG_ARCH_WANT_BATCHED_UNMAP_TLB_FLUSH
		/* See flush_tlb_batched_pending() */
		bool tlb_flush_batched;
#endif
		struct uprobes_state uprobes_state;
#ifdef CONFIG_HUGETLB_PAGE
		atomic_long_t hugetlb_usage;
#endif
		struct work_struct async_put_work;

#ifdef CONFIG_IOMMU_SUPPORT
		u32 pasid;
#endif
	} __randomize_layout;

	/*
	 * The mm_cpumask needs to be at the end of mm_struct, because it
	 * is dynamically sized based on nr_cpu_ids.
	 */
	unsigned long cpu_bitmap[];
};

extern struct mm_struct init_mm;

/* Pointer magic because the dynamic array size confuses some compilers. */
static inline void mm_init_cpumask(struct mm_struct *mm)
{
	unsigned long cpu_bitmap = (unsigned long)mm;

	cpu_bitmap += offsetof(struct mm_struct, cpu_bitmap);
	cpumask_clear((struct cpumask *)cpu_bitmap);
}

/* Future-safe accessor for struct mm_struct's cpu_vm_mask. */
static inline cpumask_t *mm_cpumask(struct mm_struct *mm)
{
	return (struct cpumask *)&mm->cpu_bitmap;
}

struct mmu_gather;
extern void tlb_gather_mmu(struct mmu_gather *tlb, struct mm_struct *mm);
extern void tlb_gather_mmu_fullmm(struct mmu_gather *tlb, struct mm_struct *mm);
extern void tlb_finish_mmu(struct mmu_gather *tlb);

static inline void init_tlb_flush_pending(struct mm_struct *mm)
{
	atomic_set(&mm->tlb_flush_pending, 0);
}

static inline void inc_tlb_flush_pending(struct mm_struct *mm)
{
	atomic_inc(&mm->tlb_flush_pending);
	/*
	 * The only time this value is relevant is when there are indeed pages
	 * to flush. And we'll only flush pages after changing them, which
	 * requires the PTL.
	 *
	 * So the ordering here is:
	 *
	 *	atomic_inc(&mm->tlb_flush_pending);
	 *	spin_lock(&ptl);
	 *	...
	 *	set_pte_at();
	 *	spin_unlock(&ptl);
	 *
	 *				spin_lock(&ptl)
	 *				mm_tlb_flush_pending();
	 *				....
	 *				spin_unlock(&ptl);
	 *
	 *	flush_tlb_range();
	 *	atomic_dec(&mm->tlb_flush_pending);
	 *
	 * Where the increment if constrained by the PTL unlock, it thus
	 * ensures that the increment is visible if the PTE modification is
	 * visible. After all, if there is no PTE modification, nobody cares
	 * about TLB flushes either.
	 *
	 * This very much relies on users (mm_tlb_flush_pending() and
	 * mm_tlb_flush_nested()) only caring about _specific_ PTEs (and
	 * therefore specific PTLs), because with SPLIT_PTE_PTLOCKS and RCpc
	 * locks (PPC) the unlock of one doesn't order against the lock of
	 * another PTL.
	 *
	 * The decrement is ordered by the flush_tlb_range(), such that
	 * mm_tlb_flush_pending() will not return false unless all flushes have
	 * completed.
	 */
}

static inline void dec_tlb_flush_pending(struct mm_struct *mm)
{
	/*
	 * See inc_tlb_flush_pending().
	 *
	 * This cannot be smp_mb__before_atomic() because smp_mb() simply does
	 * not order against TLB invalidate completion, which is what we need.
	 *
	 * Therefore we must rely on tlb_flush_*() to guarantee order.
	 */
	atomic_dec(&mm->tlb_flush_pending);
}

static inline bool mm_tlb_flush_pending(struct mm_struct *mm)
{
	/*
	 * Must be called after having acquired the PTL; orders against that
	 * PTLs release and therefore ensures that if we observe the modified
	 * PTE we must also observe the increment from inc_tlb_flush_pending().
	 *
	 * That is, it only guarantees to return true if there is a flush
	 * pending for _this_ PTL.
	 */
	return atomic_read(&mm->tlb_flush_pending);
}

static inline bool mm_tlb_flush_nested(struct mm_struct *mm)
{
	/*
	 * Similar to mm_tlb_flush_pending(), we must have acquired the PTL
	 * for which there is a TLB flush pending in order to guarantee
	 * we've seen both that PTE modification and the increment.
	 *
	 * (no requirement on actually still holding the PTL, that is irrelevant)
	 */
	return atomic_read(&mm->tlb_flush_pending) > 1;
}

struct vm_fault;

/**
 * typedef vm_fault_t - Return type for page fault handlers.
 *
 * Page fault handlers return a bitmask of %VM_FAULT values.
 */
typedef __bitwise unsigned int vm_fault_t;

/**
 * enum vm_fault_reason - Page fault handlers return a bitmask of
 * these values to tell the core VM what happened when handling the
 * fault. Used to decide whether a process gets delivered SIGBUS or
 * just gets major/minor fault counters bumped up.
 *
 * @VM_FAULT_OOM:		Out Of Memory
 * @VM_FAULT_SIGBUS:		Bad access
 * @VM_FAULT_MAJOR:		Page read from storage
 * @VM_FAULT_WRITE:		Special case for get_user_pages
 * @VM_FAULT_HWPOISON:		Hit poisoned small page
 * @VM_FAULT_HWPOISON_LARGE:	Hit poisoned large page. Index encoded
 *				in upper bits
 * @VM_FAULT_SIGSEGV:		segmentation fault
 * @VM_FAULT_NOPAGE:		->fault installed the pte, not return page
 * @VM_FAULT_LOCKED:		->fault locked the returned page
 * @VM_FAULT_RETRY:		->fault blocked, must retry
 * @VM_FAULT_FALLBACK:		huge page fault failed, fall back to small
 * @VM_FAULT_DONE_COW:		->fault has fully handled COW
 * @VM_FAULT_NEEDDSYNC:		->fault did not modify page tables and needs
 *				fsync() to complete (for synchronous page faults
 *				in DAX)
 * @VM_FAULT_HINDEX_MASK:	mask HINDEX value
 *
 */
enum vm_fault_reason {
	VM_FAULT_OOM            = (__force vm_fault_t)0x000001,
	VM_FAULT_SIGBUS         = (__force vm_fault_t)0x000002,
	VM_FAULT_MAJOR          = (__force vm_fault_t)0x000004,
	VM_FAULT_WRITE          = (__force vm_fault_t)0x000008,
	VM_FAULT_HWPOISON       = (__force vm_fault_t)0x000010,
	VM_FAULT_HWPOISON_LARGE = (__force vm_fault_t)0x000020,
	VM_FAULT_SIGSEGV        = (__force vm_fault_t)0x000040,
	VM_FAULT_NOPAGE         = (__force vm_fault_t)0x000100,
	VM_FAULT_LOCKED         = (__force vm_fault_t)0x000200,
	VM_FAULT_RETRY          = (__force vm_fault_t)0x000400,
	VM_FAULT_FALLBACK       = (__force vm_fault_t)0x000800,
	VM_FAULT_DONE_COW       = (__force vm_fault_t)0x001000,
	VM_FAULT_NEEDDSYNC      = (__force vm_fault_t)0x002000,
	VM_FAULT_HINDEX_MASK    = (__force vm_fault_t)0x0f0000,
};

/* Encode hstate index for a hwpoisoned large page */
#define VM_FAULT_SET_HINDEX(x) ((__force vm_fault_t)((x) << 16))
#define VM_FAULT_GET_HINDEX(x) (((__force unsigned int)(x) >> 16) & 0xf)

#define VM_FAULT_ERROR (VM_FAULT_OOM | VM_FAULT_SIGBUS |	\
			VM_FAULT_SIGSEGV | VM_FAULT_HWPOISON |	\
			VM_FAULT_HWPOISON_LARGE | VM_FAULT_FALLBACK)

#define VM_FAULT_RESULT_TRACE \
	{ VM_FAULT_OOM,                 "OOM" },	\
	{ VM_FAULT_SIGBUS,              "SIGBUS" },	\
	{ VM_FAULT_MAJOR,               "MAJOR" },	\
	{ VM_FAULT_WRITE,               "WRITE" },	\
	{ VM_FAULT_HWPOISON,            "HWPOISON" },	\
	{ VM_FAULT_HWPOISON_LARGE,      "HWPOISON_LARGE" },	\
	{ VM_FAULT_SIGSEGV,             "SIGSEGV" },	\
	{ VM_FAULT_NOPAGE,              "NOPAGE" },	\
	{ VM_FAULT_LOCKED,              "LOCKED" },	\
	{ VM_FAULT_RETRY,               "RETRY" },	\
	{ VM_FAULT_FALLBACK,            "FALLBACK" },	\
	{ VM_FAULT_DONE_COW,            "DONE_COW" },	\
	{ VM_FAULT_NEEDDSYNC,           "NEEDDSYNC" }

struct vm_special_mapping {
	const char *name;	/* The name, e.g. "[vdso]". */

	/*
	 * If .fault is not provided, this points to a
	 * NULL-terminated array of pages that back the special mapping.
	 *
	 * This must not be NULL unless .fault is provided.
	 */
	struct page **pages;

	/*
	 * If non-NULL, then this is called to resolve page faults
	 * on the special mapping.  If used, .pages is not checked.
	 */
	vm_fault_t (*fault)(const struct vm_special_mapping *sm,
				struct vm_area_struct *vma,
				struct vm_fault *vmf);

	int (*mremap)(const struct vm_special_mapping *sm,
		     struct vm_area_struct *new_vma);
};

enum tlb_flush_reason {
	TLB_FLUSH_ON_TASK_SWITCH,
	TLB_REMOTE_SHOOTDOWN,
	TLB_LOCAL_SHOOTDOWN,
	TLB_LOCAL_MM_SHOOTDOWN,
	TLB_REMOTE_SEND_IPI,
	NR_TLB_FLUSH_REASONS,
};

 /*
  * A swap entry has to fit into a "unsigned long", as the entry is hidden
  * in the "index" field of the swapper address space.
  */
typedef struct {
	unsigned long val;
} swp_entry_t;



#endif //TODO

//<YUAN> define page related functions
void unlock_page(struct page* page);



//void put_page(page* page);


template<typename T>
inline T* page_address(page* pp)
{
	return (T*)(pp->virtual_add);
}


#define FGP_ACCESSED		0x00000001
#define FGP_LOCK		0x00000002
#define FGP_CREAT		0x00000004
#define FGP_WRITE		0x00000008
#define FGP_NOFS		0x00000010
#define FGP_NOWAIT		0x00000020
#define FGP_FOR_MMAP		0x00000040
#define FGP_HEAD		0x00000080
#define FGP_ENTRY		0x00000100
#define FGP_LAST		0x00000200

#ifdef _DEBUG
// 调试输出FGP的选项
static const wchar_t* FGP_STRING[] = {
	L"ACCESSED", L"LOCK", L"CREATE", L"WRITE", L"NO_FS", L"NO_WAIT", L"FOR_MMAP",
	L"HEAD", L"ENTRY"};
inline std::wstring FGP2String(int fgp_flags)
{
	std::wstring str_flags;
	for (int ii = 0, mask = 1; mask < FGP_LAST; mask <<= 1, ++ii)
	{
		if (fgp_flags & mask) 
		{
			str_flags += FGP_STRING[ii]; 
			str_flags += L", "; 
		}
	}
	return str_flags;
}

#endif

#ifdef _DEBUG
//调试输出 page tag
static const wchar_t* PAGE_TAG_STRING[] = {L"DIRTY", L"WRITEBACK", L"TOWRITE",};
inline std::wstring PageTag2String(xa_mark_t mark)
{
	std::wstring str_tag;
	for (int ii = 0, mask=1; ii < 3; ii++, mask<<=1)
	{
		if (mark & mask)
		{
			str_tag += PAGE_TAG_STRING[ii];
			str_tag += L", ";
		}
	}
	return str_tag;
}
#endif

page* grab_cache_page(address_space* mapping, pgoff_t index);
page* pagecache_get_page(address_space* mapping, pgoff_t index, int fgp_flag=0, gfp_t gfp_mask=0);
//page* grab_cache_page_write_begin(address_space * mapping, pgoff_t index, int flag);
page* find_get_page(address_space* mapping, pgoff_t index);
inline page* find_lock_page(address_space* mapping, pgoff_t index)
{
	return pagecache_get_page(mapping, index, FGP_LOCK, 0);
}

inline void zero_user(page* pp, loff_t start, loff_t end)
{
	BYTE* buf = page_address<BYTE>(pp);
	memset(buf + start, 0, end - start);
}


// <YUAN> from pagemap.h
/* This is non-atomic.  Only to be used before the mapping is activated. Probably needs a barrier... */
void mapping_set_gfp_mask(address_space* m, gfp_t mask);

//<YUAN> from linux/bitmat.h
static inline void bitmap_zero(unsigned long* dst, unsigned int nbits)
{
	unsigned int len = BITS_TO_LONGS(nbits) * sizeof(unsigned long);
	memset(dst, 0, len);
}
static inline void bitmap_fill(unsigned long* dst, unsigned int nbits)
{
	unsigned int len = BITS_TO_LONGS(nbits) * sizeof(unsigned long);
	memset(dst, 0xff, len);
}

//<YUAN> in filemap.c
inline void lock_page(page*)
{
	//struct page* page = compound_head(__page);
	//wait_queue_head_t* q = page_waitqueue(page);
	//wait_on_page_bit_common(q, page, PG_locked, TASK_UNINTERRUPTIBLE, EXCLUSIVE);
}

class page_auto_lock
{
public:
	page_auto_lock(page& pp) { m_page = &pp; }
	void lock(void) { lock_page(m_page); }
	void unlock(void) { unlock_page(m_page); }

protected:
	page* m_page;

};


/* Dirty a page.
 *
 * For pages with a mapping this should be done under the page lock for the benefit of asynchronous memory errors who
 prefer a consistent dirty state. This rule can be broken in some special cases, but should be better not to. */
//inline int set_page_dirty(page* page);

/*	Various page->flags bits :
* PG_reserved is set for special pages.The "struct page" of such a page should in general not be touched(e.g.set dirty)
except by its owner. Pages marked as PG_reserved include :

	* -Pages part of the kernel image(including vDSO) and similar(e.g.BIOS, initrd, HW tables)
	* -Pages reserved or allocated early during boot(before the page allocator was initialized).This includes(depending 
		on the architecture) the initial vmemmap, initial page tables, crashkernel, elfcorehdr, and much much more.Once
		(if ever) freed, PG_reserved is clearedand they will be given to the page allocator.
	* -Pages falling into physical memory gaps - not IORESOURCE_SYSRAM.Trying to read / write these pages might end
		badly.Don't touch!
	* -The zero page(s)
	* -Pages not added to the page allocator when onlining a section because
	* they were excluded via the online_page_callback() or because they are
	* PG_hwpoison.
	* -Pages allocated in the context of kexec / kdump(loaded kernel image,
		*control pages, vmcoreinfo)
	* -MMIO / DMA pages.Some architectures don't allow to ioremap pages that are
	* not marked PG_reserved(as they might be in use by somebody else who does
		* not respect the caching strategy).
	* -Pages part of an offline section(struct pages of offline sections should
		* not be trusted as they will be initialized when first onlined).
	* -MCA pages on ia64
	* -Pages holding CPU notes for POWER Firmware Assisted Dump
	* -Device memory(e.g.PMEM, DAX, HMM)
	* Some PG_reserved pages will be excluded from the hibernation image.
	* PG_reserved does in general not hinder anybody from dumping or swapping
	* andis no longer required for remap_pfn_range().ioremap might require it.
	* Consequently, PG_reserved for a page mapped into user space can indicate
	* the zero page, the vDSO, MMIO pages or device memory.
	*
	*The PG_private bitflag is set on pagecache pages if they contain filesystem
	* specific data(which is normally at page->private).It can be used by
	* private allocations for its own usage.
	*
	* During initiation of disk I / O, PG_locked is set.This bit is set before I / O
	* andcleared when writeback _starts_ or when read _completes_.PG_writeback
	* is set before writeback starts and cleared when it finishes.
	*
	*PG_locked also pins a page in pagecache, and blocks truncation of the file
	* while it is held.
	*
	* page_waitqueue(page) is a wait queue of all tasks waiting for the page
	* to become unlocked.
	*
	* PG_swapbacked is set when a page uses swap as a backing storage.This are
	* usually PageAnon or shmem pages but please note that even anonymous pages
	* might lose their PG_swapbacked flag when they simply can be dropped(e.g.as
		* a result of MADV_FREE).
	*
	*PG_uptodate tells whether the page's contents is valid.  When a read
	* completes, the page becomes uptodate, unless a disk I / O error happened.
	*
	*PG_referenced, PG_reclaim are used for page reclaim for anonymousand
	* file - backed pagecache(see mm / vmscan.c).
	*
	*PG_error is set to indicate that an I / O error occurred on this page.
	*
	*PG_arch_1 is an architecture specific page state bit.The generic code
	* guarantees that this bit is cleared for a page when it first is entered into
	* the page cache.
	*
	* PG_hwpoison indicates that a page got corrupted in hardwareand contains
	* data with incorrect ECC bits that triggered a machine check.Accessing is
	* not safe since it may cause another machine check.Don't touch!
	* /

/* Don't use the pageflags directly.  Use the PageFoo macros.
 * The page flags field is split into two parts, the main flags area which extends from the low bits upwards, and the 
 fields area which extends from the high bits downwards.
 *
 *  | FIELD | ... | FLAGS |
 *  N-1           ^       0
 *               (NR_PAGEFLAGS)
 * The fields area is reserved for fields mapping zone, node (for NUMA) and SPARSEMEM section (for variants of 
 SPARSEMEM that require section ids like SPARSEMEM_EXTREME with !SPARSEMEM_VMEMMAP).*/
enum pageflags
{
	PG_locked,		/* Page is locked. Don't touch. */
	PG_referenced,
	PG_uptodate,
	PG_dirty,
	PG_lru,
	PG_active,
	PG_workingset,
	PG_waiters,		/* Page has waiters, check its waitqueue. Must be bit #7 and in the same byte as "PG_locked" */
	PG_error,
	PG_slab,
	PG_owner_priv_1,	/* Owner use. If pagecache, fs may use*/
	PG_arch_1,
	PG_reserved,
	PG_private,		/* If pagecache, has fs-private data */
	PG_private_2,		/* If pagecache, has fs aux data */
	PG_writeback,		/* Page is under writeback */
	PG_head,		/* A head page */
	PG_mappedtodisk,	/* Has blocks allocated on-disk */
	PG_reclaim,		/* To be reclaimed asap */
	PG_swapbacked,		/* Page is backed by RAM/swap */
	PG_unevictable,		/* Page is "unevictable"  */
#ifdef CONFIG_MMU
	PG_mlocked,		/* Page is vma mlocked */
#endif
#ifdef CONFIG_ARCH_USES_PG_UNCACHED
	PG_uncached,		/* Page has been mapped as uncached */
#endif
#ifdef CONFIG_MEMORY_FAILURE
	PG_hwpoison,		/* hardware poisoned page. Don't touch */
#endif
#if defined(CONFIG_IDLE_PAGE_TRACKING) && defined(CONFIG_64BIT)
	PG_young,
	PG_idle,
#endif
#ifdef CONFIG_64BIT
	PG_arch_2,
#endif
#ifdef CONFIG_KASAN_HW_TAGS
	PG_skip_kasan_poison,
#endif
	__NR_PAGEFLAGS,

	/* Filesystems */
	PG_checked = PG_owner_priv_1,

	/* SwapBacked */
	PG_swapcache = PG_owner_priv_1,	/* Swap page: swp_entry_t in private */

	/* Two page bits are conscripted by FS-Cache to maintain local caching
	 * state.  These bits are set on pages belonging to the netfs's inodes
	 * when those inodes are being locally cached.
	 */
	 PG_fscache = PG_private_2,	/* page backed by cache */

	 /* XEN */
	 /* Pinned in Xen as a read-only pagetable page. */
	 PG_pinned = PG_owner_priv_1,
	 /* Pinned as part of domain save (see xen_mm_pin_all()). */
	 PG_savepinned = PG_dirty,
	 /* Has a grant mapping of another (foreign) domain's page. */
	 PG_foreign = PG_owner_priv_1,
	 /* Remapped by swiotlb-xen. */
	 PG_xen_remapped = PG_owner_priv_1,

	 /* SLOB */
	 PG_slob_free = PG_private,

	 /* Compound pages. Stored in first tail page's flags */
	 PG_double_map = PG_workingset,

	 /* non-lru isolated movable page */
	 PG_isolated = PG_reclaim,

	 /* Only valid for buddy pages. Used to track pages that are reported */
	 PG_reported = PG_uptodate,
};

//<YUAN> 源代码在page-flags.h中，262行，通过#define Page##uname()的方式实现

// read data to page

//-- Active
inline bool PageActive(page* pp) {	return test_bit(PG_active, &pp->flags);}
inline void SetPageActive(page* pp) { set_bit(PG_active, &pp->flags); }
inline void ClearPageActive(page* pp) { clear_bit(PG_active, &pp->flags); }


//-- Checked
inline bool PageChecked(page* pp) { return test_bit(PG_checked, &pp->flags); }
inline void SetPageChecked(page* pp) { set_bit(PG_checked, &pp->flags); }
inline void ClearPageChecked(page* pp) { clear_bit(PG_checked, &pp->flags); }

//-- Dirty
inline bool PageDirty(page* pp) { return test_bit(PG_dirty, &pp->flags); }
inline bool TestSetPageDirty(page* pp) { return test_and_set_bit(PG_dirty, &pp->flags); }
inline bool TestClearPageDirty(page* pp) { return test_and_clear_bit(PG_dirty, &pp->flags); }
//-- Error
inline bool PageError(page* page_ptr) { return test_bit(PG_error, &page_ptr->flags); }
inline void SetPageError(page* page_ptr) { set_bit(PG_error, &page_ptr->flags); }
inline void ClearPageError(page* page_ptr) { clear_bit(PG_error, &page_ptr->flags); }
inline bool TestClearPageError(page* pp) { return test_and_clear_bit(PG_error, &pp->flags); }


//-- Idle
inline bool PageIdle(page* pp) { return test_bit(PG_idle, &pp->flags); }
inline bool page_is_idle(page* pp) { return PageIdle(pp); }
inline void ClearPageIdle(page* pp) { clear_bit(PG_idle, &pp->flags); }
inline void clear_page_idle(page* pp)	{	ClearPageIdle(pp);}


//-- Locked
inline bool PageLocked(page* pp) { return test_bit(PG_locked, &pp->flags); }
inline void __SetPageLocked(page* pp) { set_bit(PG_locked, &pp->flags); }
inline void __ClearPageLocked(page* pp) { clear_bit(PG_locked, &pp->flags); }

inline bool PageLRU(page* pp) { return test_bit(PG_lru, &pp->flags); }
inline void SetPageLRU(page* pp) { set_bit(PG_lru, &pp->flags); }
inline void ClearPageLRU(page* pp) { clear_bit(PG_lru, &pp->flags); }


inline bool PageSwapCache(page* pp) { return test_bit(PG_swapcache, &pp->flags); }
inline bool PageMappedToDisk(page* pp) { return test_bit(PG_mappedtodisk, &pp->flags); }
inline void SetPageMappedToDisk(page* pp) { set_bit(PG_mappedtodisk, &pp->flags); }

inline bool PageReclaim(page* pp) { return test_bit(PG_reclaim, &pp->flags); }
inline void ClearPageReclaim(page* pp) { clear_bit(PG_reclaim, &pp->flags); }
//-- Private
inline bool PagePrivate(page* pp) { return test_bit(PG_private, &pp->flags); }
inline void SetPagePrivate(page* pp) { set_bit(PG_private, &pp->flags); }
inline void ClearPagePrivate(page* pp) { clear_bit(PG_private, &pp->flags); }

inline bool PageHead(page* pp) { return test_bit(PG_head, &pp->flags); }
inline bool PageTransHuge(page* pp) { return PageHead(pp); }

//-- Reference
inline bool PageReferenced(page* pp) { return test_bit(PG_referenced, &pp->flags); }
inline void SetPageReferenced(page* pp) { set_bit(PG_referenced, &pp->flags); }
inline void ClearPageReferenced(page* pp) { clear_bit(PG_referenced, &pp->flags); }


//-- Readahead / Reclaim
inline bool PageReadahead(page* pp) { return test_bit(PG_reclaim, &pp->flags); }
inline void SetPageReadahead(page* pp) {	set_bit(PG_reclaim, &pp->flags); }
inline void ClearPageReadahead(page* pp) { clear_bit(PG_reclaim, &pp->flags); }

inline void __SetPageReferenced(page* pp) { set_bit(PG_referenced, &pp->flags); }

//-- Unevictable
inline bool PageUnevictable(page* pp) {	return test_bit(PG_unevictable, &pp->flags); }
inline void ClearPageUnevictable(page* pp) { clear_bit(PG_unevictable, &pp->flags); }

 
//-- Uptodate
inline int PageUptodate(page* page_ptr) { return test_bit(PG_uptodate, &page_ptr->flags); }
inline void ClearPageUptodate(page* page_ptr) { clear_bit(PG_uptodate, &page_ptr->flags); }
inline void SetPageUptodate(page* page_ptr) { set_bit(PG_uptodate, &page_ptr->flags); }

//-- Waiters
inline bool PageWaiters(page* pp) { return test_bit(PG_waiters, &pp->flags); }

//-- Writeback
inline bool PageWriteback(page* pp) { return test_bit(PG_writeback, &pp->flags); }
inline bool TestSetPageWriteback(page* pp) { return test_and_set_bit(PG_writeback, &pp->flags); }
inline bool TestClearPageWriteback(page* pp) { return test_and_clear_bit(PG_writeback, &pp->flags); }

int __set_page_dirty_nobuffers(struct page* page);

int clear_page_dirty_for_io(struct page* page);
int set_page_writeback(page* pp);


//<YUAN> from gfp.h
/*
 * In case of changes, please don't forget to update
 * include/trace/events/mmflags.h and tools/perf/builtin-kmem.c
 */

struct vm_area_struct;

/*
 * In case of changes, please don't forget to update
 * include/trace/events/mmflags.h and tools/perf/builtin-kmem.c
 */

 /* Plain integer GFP bitmasks. Do not use this directly. */
#define ___GFP_DMA		0x01u
#define ___GFP_HIGHMEM		0x02u
#define ___GFP_DMA32		0x04u
#define ___GFP_MOVABLE		0x08u
#define ___GFP_RECLAIMABLE	0x10u
#define ___GFP_HIGH		0x20u
#define ___GFP_IO		0x40u
#define ___GFP_FS		0x80u
#define ___GFP_ZERO		0x100u
#define ___GFP_ATOMIC		0x200u
#define ___GFP_DIRECT_RECLAIM	0x400u
#define ___GFP_KSWAPD_RECLAIM	0x800u
#define ___GFP_WRITE		0x1000u
#define ___GFP_NOWARN		0x2000u
#define ___GFP_RETRY_MAYFAIL	0x4000u
#define ___GFP_NOFAIL		0x8000u
#define ___GFP_NORETRY		0x10000u
#define ___GFP_MEMALLOC		0x20000u
#define ___GFP_COMP		0x40000u
#define ___GFP_NOMEMALLOC	0x80000u
#define ___GFP_HARDWALL		0x100000u
#define ___GFP_THISNODE		0x200000u
#define ___GFP_ACCOUNT		0x400000u
#ifdef CONFIG_LOCKDEP
#define ___GFP_NOLOCKDEP	0x800000u
#else
#define ___GFP_NOLOCKDEP	0
#endif
/* If the above are modified, __GFP_BITS_SHIFT may need updating */

/*
 * Physical address zone modifiers (see linux/mmzone.h - low four bits)
 *
 * Do not put any conditional on these. If necessary modify the definitions
 * without the underscores and use them consistently. The definitions here may
 * be used in bit comparisons.
 */
#define __GFP_DMA	((__force gfp_t)___GFP_DMA)
#define __GFP_HIGHMEM	((__force gfp_t)___GFP_HIGHMEM)
#define __GFP_DMA32	((__force gfp_t)___GFP_DMA32)
#define __GFP_MOVABLE	((__force gfp_t)___GFP_MOVABLE)  /* ZONE_MOVABLE allowed */
#define GFP_ZONEMASK	(__GFP_DMA|__GFP_HIGHMEM|__GFP_DMA32|__GFP_MOVABLE)

 /**
  * DOC: Page mobility and placement hints
  *
  * Page mobility and placement hints
  * ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
  *
  * These flags provide hints about how mobile the page is. Pages with similar
  * mobility are placed within the same pageblocks to minimise problems due
  * to external fragmentation.
  *
  * %__GFP_MOVABLE (also a zone modifier) indicates that the page can be
  * moved by page migration during memory compaction or can be reclaimed.
  *
  * %__GFP_RECLAIMABLE is used for slab allocations that specify
  * SLAB_RECLAIM_ACCOUNT and whose pages can be freed via shrinkers.
  *
  * %__GFP_WRITE indicates the caller intends to dirty the page. Where possible,
  * these pages will be spread between local zones to avoid all the dirty
  * pages being in one zone (fair zone allocation policy).
  *
  * %__GFP_HARDWALL enforces the cpuset memory allocation policy.
  *
  * %__GFP_THISNODE forces the allocation to be satisfied from the requested
  * node with no fallbacks or placement policy enforcements.
  *
  * %__GFP_ACCOUNT causes the allocation to be accounted to kmemcg.
  */
#define __GFP_RECLAIMABLE	(___GFP_RECLAIMABLE)
#define __GFP_WRITE			(___GFP_WRITE)
#define __GFP_HARDWALL		(___GFP_HARDWALL)
#define __GFP_THISNODE		(___GFP_THISNODE)
#define __GFP_ACCOUNT		(___GFP_ACCOUNT)

 /* Plain integer GFP bitmasks. Do not use this directly. */
#define ___GFP_DMA		0x01u
#define ___GFP_HIGHMEM		0x02u
#define ___GFP_DMA32		0x04u
#define ___GFP_MOVABLE		0x08u
#define ___GFP_RECLAIMABLE	0x10u
#define ___GFP_HIGH		0x20u
#define ___GFP_IO		0x40u
#define ___GFP_FS		0x80u
#define ___GFP_ZERO		0x100u
#define ___GFP_ATOMIC		0x200u
#define ___GFP_DIRECT_RECLAIM	0x400u
#define ___GFP_KSWAPD_RECLAIM	0x800u
#define ___GFP_WRITE		0x1000u
#define ___GFP_NOWARN		0x2000u
#define ___GFP_RETRY_MAYFAIL	0x4000u
#define ___GFP_NOFAIL		0x8000u
#define ___GFP_NORETRY		0x10000u
#define ___GFP_MEMALLOC		0x20000u
#define ___GFP_COMP		0x40000u
#define ___GFP_NOMEMALLOC	0x80000u
#define ___GFP_HARDWALL		0x100000u
#define ___GFP_THISNODE		0x200000u
#define ___GFP_ACCOUNT		0x400000u
#define ___GFP_ZEROTAGS		0x800000u
#define ___GFP_SKIP_KASAN_POISON	0x1000000u
#ifdef CONFIG_LOCKDEP
#define ___GFP_NOLOCKDEP	0x2000000u
#else
#define ___GFP_NOLOCKDEP	0
#endif
/* If the above are modified, __GFP_BITS_SHIFT may need updating */

/**
 * DOC: Reclaim modifiers
 *
 * Reclaim modifiers
 * ~~~~~~~~~~~~~~~~~
 * Please note that all the following flags are only applicable to sleepable
 * allocations (e.g. %GFP_NOWAIT and %GFP_ATOMIC will ignore them).
 *
 * %__GFP_IO can start physical IO.
 *
 * %__GFP_FS can call down to the low-level FS. Clearing the flag avoids the
 * allocator recursing into the filesystem which might already be holding
 * locks.
 *
 * %__GFP_DIRECT_RECLAIM indicates that the caller may enter direct reclaim.
 * This flag can be cleared to avoid unnecessary delays when a fallback
 * option is available.
 *
 * %__GFP_KSWAPD_RECLAIM indicates that the caller wants to wake kswapd when
 * the low watermark is reached and have it reclaim pages until the high
 * watermark is reached. A caller may wish to clear this flag when fallback
 * options are available and the reclaim is likely to disrupt the system. The
 * canonical example is THP allocation where a fallback is cheap but
 * reclaim/compaction may cause indirect stalls.
 *
 * %__GFP_RECLAIM is shorthand to allow/forbid both direct and kswapd reclaim.
 *
 * The default allocator behavior depends on the request size. We have a concept
 * of so called costly allocations (with order > %PAGE_ALLOC_COSTLY_ORDER).
 * !costly allocations are too essential to fail so they are implicitly
 * non-failing by default (with some exceptions like OOM victims might fail so
 * the caller still has to check for failures) while costly requests try to be
 * not disruptive and back off even without invoking the OOM killer.
 * The following three modifiers might be used to override some of these
 * implicit rules
 *
 * %__GFP_NORETRY: The VM implementation will try only very lightweight
 * memory direct reclaim to get some memory under memory pressure (thus
 * it can sleep). It will avoid disruptive actions like OOM killer. The
 * caller must handle the failure which is quite likely to happen under
 * heavy memory pressure. The flag is suitable when failure can easily be
 * handled at small cost, such as reduced throughput
 *
 * %__GFP_RETRY_MAYFAIL: The VM implementation will retry memory reclaim
 * procedures that have previously failed if there is some indication
 * that progress has been made else where.  It can wait for other
 * tasks to attempt high level approaches to freeing memory such as
 * compaction (which removes fragmentation) and page-out.
 * There is still a definite limit to the number of retries, but it is
 * a larger limit than with %__GFP_NORETRY.
 * Allocations with this flag may fail, but only when there is
 * genuinely little unused memory. While these allocations do not
 * directly trigger the OOM killer, their failure indicates that
 * the system is likely to need to use the OOM killer soon.  The
 * caller must handle failure, but can reasonably do so by failing
 * a higher-level request, or completing it only in a much less
 * efficient manner.
 * If the allocation does fail, and the caller is in a position to
 * free some non-essential memory, doing so could benefit the system
 * as a whole.
 *
 * %__GFP_NOFAIL: The VM implementation _must_ retry infinitely: the caller
 * cannot handle allocation failures. The allocation could block
 * indefinitely but will never return with failure. Testing for
 * failure is pointless.
 * New users should be evaluated carefully (and the flag should be
 * used only when there is no reasonable failure policy) but it is
 * definitely preferable to use the flag rather than opencode endless
 * loop around allocator.
 * Using this flag for costly allocations is _highly_ discouraged.
 */

#define __GFP_IO	((/*__force*/ gfp_t)___GFP_IO)
#define __GFP_FS	((/*__force*/ gfp_t)___GFP_FS)
#define __GFP_DIRECT_RECLAIM	((gfp_t)___GFP_DIRECT_RECLAIM) /* Caller can reclaim */
#define __GFP_KSWAPD_RECLAIM	((gfp_t)___GFP_KSWAPD_RECLAIM) /* kswapd can wake */
#define __GFP_RECLAIM ((gfp_t)(___GFP_DIRECT_RECLAIM|___GFP_KSWAPD_RECLAIM))
#define __GFP_RETRY_MAYFAIL	((gfp_t)___GFP_RETRY_MAYFAIL)
#define __GFP_NOFAIL	((gfp_t)___GFP_NOFAIL)
#define __GFP_NORETRY	((gfp_t)___GFP_NORETRY)

	/**
	 * DOC: Useful GFP flag combinations
	 *
	 * Useful GFP flag combinations
	 * ~~~~~~~~~~~~~~~~~~~~~~~~~~~~
	 *
	 * Useful GFP flag combinations that are commonly used. It is recommended
	 * that subsystems start with one of these combinations and then set/clear
	 * %__GFP_FOO flags as necessary.
	 *
	 * %GFP_ATOMIC users can not sleep and need the allocation to succeed. A lower
	 * watermark is applied to allow access to "atomic reserves".
	 * The current implementation doesn't support NMI and few other strict
	 * non-preemptive contexts (e.g. raw_spin_lock). The same applies to %GFP_NOWAIT.
	 *
	 * %GFP_KERNEL is typical for kernel-internal allocations. The caller requires
	 * %ZONE_NORMAL or a lower zone for direct access but can direct reclaim.
	 *
	 * %GFP_KERNEL_ACCOUNT is the same as GFP_KERNEL, except the allocation is
	 * accounted to kmemcg.
	 *
	 * %GFP_NOWAIT is for kernel allocations that should not stall for direct
	 * reclaim, start physical IO or use any filesystem callback.
	 *
	 * %GFP_NOIO will use direct reclaim to discard clean pages or slab pages
	 * that do not require the starting of any physical IO.
	 * Please try to avoid using this flag directly and instead use
	 * memalloc_noio_{save,restore} to mark the whole scope which cannot
	 * perform any IO with a short explanation why. All allocation requests
	 * will inherit GFP_NOIO implicitly.
	 *
	 * %GFP_NOFS will use direct reclaim but will not use any filesystem interfaces.
	 * Please try to avoid using this flag directly and instead use
	 * memalloc_nofs_{save,restore} to mark the whole scope which cannot/shouldn't
	 * recurse into the FS layer with a short explanation why. All allocation
	 * requests will inherit GFP_NOFS implicitly.
	 *
	 * %GFP_USER is for userspace allocations that also need to be directly
	 * accessibly by the kernel or hardware. It is typically used by hardware
	 * for buffers that are mapped to userspace (e.g. graphics) that hardware
	 * still must DMA to. cpuset limits are enforced for these allocations.
	 *
	 * %GFP_DMA exists for historical reasons and should be avoided where possible.
	 * The flags indicates that the caller requires that the lowest zone be
	 * used (%ZONE_DMA or 16M on x86-64). Ideally, this would be removed but
	 * it would require careful auditing as some users really require it and
	 * others use the flag to avoid lowmem reserves in %ZONE_DMA and treat the
	 * lowest zone as a type of emergency reserve.
	 *
	 * %GFP_DMA32 is similar to %GFP_DMA except that the caller requires a 32-bit
	 * address.
	 *
	 * %GFP_HIGHUSER is for userspace allocations that may be mapped to userspace,
	 * do not need to be directly accessible by the kernel but that cannot
	 * move once in use. An example may be a hardware allocation that maps
	 * data directly into userspace but has no addressing limitations.
	 *
	 * %GFP_HIGHUSER_MOVABLE is for userspace allocations that the kernel does not
	 * need direct access to but can use kmap() when access is required. They
	 * are expected to be movable via page reclaim or page migration. Typically,
	 * pages on the LRU would also be allocated with %GFP_HIGHUSER_MOVABLE.
	 *
	 * %GFP_TRANSHUGE and %GFP_TRANSHUGE_LIGHT are used for THP allocations. They
	 * are compound allocations that will generally fail quickly if memory is not
	 * available and will not wake kswapd/kcompactd on failure. The _LIGHT
	 * version does not attempt reclaim/compaction at all and is by default used
	 * in page fault path, while the non-light is used by khugepaged.
	 */
#define GFP_ATOMIC	(__GFP_HIGH|__GFP_ATOMIC|__GFP_KSWAPD_RECLAIM)
#define GFP_KERNEL	(__GFP_RECLAIM | __GFP_IO | __GFP_FS)
#define GFP_KERNEL_ACCOUNT (GFP_KERNEL | __GFP_ACCOUNT)
#define GFP_NOWAIT	(__GFP_KSWAPD_RECLAIM)
#define GFP_NOIO	(__GFP_RECLAIM)
#define GFP_NOFS	(__GFP_RECLAIM | __GFP_IO)
#define GFP_USER	(__GFP_RECLAIM | __GFP_IO | __GFP_FS | __GFP_HARDWALL)
#define GFP_DMA		__GFP_DMA
#define GFP_DMA32	__GFP_DMA32
#define GFP_HIGHUSER	(GFP_USER | __GFP_HIGHMEM)
#define GFP_HIGHUSER_MOVABLE	(GFP_HIGHUSER | __GFP_MOVABLE | \
			 __GFP_SKIP_KASAN_POISON)
#define GFP_TRANSHUGE_LIGHT	((GFP_HIGHUSER_MOVABLE | __GFP_COMP | \
			 __GFP_NOMEMALLOC | __GFP_NOWARN) & ~__GFP_RECLAIM)
#define GFP_TRANSHUGE	(GFP_TRANSHUGE_LIGHT | __GFP_DIRECT_RECLAIM)

	 /**
	  * DOC: Action modifiers
	  *
	  * Action modifiers
	  * ~~~~~~~~~~~~~~~~
	  *
	  * %__GFP_NOWARN suppresses allocation failure reports.
	  *
	  * %__GFP_COMP address compound page metadata.
	  *
	  * %__GFP_ZERO returns a zeroed page on success.
	  *
	  * %__GFP_ZEROTAGS returns a page with zeroed memory tags on success, if
	  * __GFP_ZERO is set.
	  *
	  * %__GFP_SKIP_KASAN_POISON returns a page which does not need to be poisoned
	  * on deallocation. Typically used for userspace pages. Currently only has an
	  * effect in HW tags mode.
	  */
#define __GFP_NOWARN	((gfp_t)___GFP_NOWARN)
#define __GFP_COMP	((gfp_t)___GFP_COMP)
#define __GFP_ZERO	(( gfp_t)___GFP_ZERO)
#define __GFP_ZEROTAGS	((gfp_t)___GFP_ZEROTAGS)
#define __GFP_SKIP_KASAN_POISON	((gfp_t)___GFP_SKIP_KASAN_POISON)


// ==== xarray ====

// ==== pagemap.h ====
/* Return true if the page was successfully locked */
static inline int trylock_page(struct page* page)
{
	//page = compound_head(page);
	return (likely(!test_and_set_bit_lock(PG_locked, &page->flags)));
}

/* 15 pointers + header align the pagevec structure to a power of two */
#define PAGEVEC_SIZE	15

struct pagevec
{
	unsigned char nr;
	bool percpu_pvec_drained;
	struct page* pages[PAGEVEC_SIZE];
};

void __pagevec_release(pagevec* pvec);
void release_pages(page** pages, int nr);
void free_unref_page_list(list_head* list);


static inline unsigned pagevec_count(pagevec* pvec)
{
	return pvec->nr;
}

static inline void pagevec_init(pagevec* pvec)
{
	pvec->nr = 0;
	pvec->percpu_pvec_drained = false;
}

static inline void pagevec_reinit(pagevec* pvec)
{
	pvec->nr = 0;
}



static inline void pagevec_release(pagevec* pvec)
{
#if 1 //<TODO>
	if (pagevec_count(pvec))		__pagevec_release(pvec);
#else
	JCASSERT(0);
#endif
}




static inline int mapping_use_writeback_tags(address_space* mapping)
{
	return !test_bit(AS_NO_WRITEBACK_TAGS, &mapping->flags);
}

extern unsigned find_lock_entries(address_space* mapping, pgoff_t start, pgoff_t end, pagevec* pvec, pgoff_t* indices);

/* pagevec_remove_exceptionals - pagevec exceptionals pruning
 * @pvec:	The pagevec to prune
 *
 * find_get_entries() fills both pages and XArray value entries (aka exceptional entries) into the pagevec.  
   This function prunes all exceptionals from @pvec without leaving holes, so that it can be passed on to 
   page-only pagevec operations. */
inline void pagevec_remove_exceptionals(struct pagevec* pvec)
{
	UINT32 jj=0;
	for (UINT32 i = 0; i < pagevec_count(pvec); i++)
	{
		struct page* page = pvec->pages[i];
		if (!xa_is_value(page))
			pvec->pages[jj++] = page;
	}
	pvec->nr = jj;
}

static inline unsigned pagevec_space(struct pagevec* pvec)
{
	return PAGEVEC_SIZE - pvec->nr;
}

/* Add a page to a pagevec.  Returns the number of slots still available. */
static inline unsigned pagevec_add(struct pagevec* pvec, struct page* page)
{
	pvec->pages[pvec->nr++] = page;
	return pagevec_space(pvec);
}

/**memalloc_nofs_save - Marks implicit GFP_NOFS allocation scope.
 *
 * This functions marks the beginning of the GFP_NOFS allocation scope. All further allocations will implicitly
 drop __GFP_FS flag and so they are safe for the FS critical section from the allocation recursion point of view. 
 Use memalloc_nofs_restore to end the scope with flags returned by this function.
 *
 * This function is safe to be used from any context. */
static inline unsigned int memalloc_nofs_save(void)
{
	//unsigned int flags = current->flags & PF_MEMALLOC_NOFS;
	//current->flags |= PF_MEMALLOC_NOFS;
	//return flags;
	return 0;
}

/**
 * memalloc_nofs_restore - Ends the implicit GFP_NOFS scope.
 * @flags: Flags to restore.
 *
 * Ends the implicit GFP_NOFS scope started by memalloc_nofs_save function. Always make sure that the given flags 
 is the return value from the pairing memalloc_nofs_save call. */
static inline void memalloc_nofs_restore(unsigned int flags)
{
//	current->flags = (current->flags & ~PF_MEMALLOC_NOFS) | flags;
}

static inline bool blk_cgroup_congested(void) { return false; }

inline int PageHuge(page* pp) { return false; }

#define offset_in_page(p)	((unsigned long)(p) & ~PAGE_MASK)

static inline unsigned int thp_order(struct page* page)
{
	//	VM_BUG_ON_PGFLAGS(PageTail(page), page);
	return 0;
}

/**
 * thp_size - Size of a transparent huge page.
 * @page: Head page of a transparent huge page.
 *
 * Return: Number of bytes in this page.
 */
static inline unsigned long thp_size(struct page* page)
{
	return PAGE_SIZE << thp_order(page);
}

void truncate_pagecache(struct inode* inode, loff_t newsize);
void truncate_inode_pages_final(address_space* mapping);


/**
 * DOC: Watermark modifiers
 *
 * Watermark modifiers -- controls access to emergency reserves
 * ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
 *
 * %__GFP_HIGH indicates that the caller is high-priority and that granting the request is necessary before the system can make forward progress. For example, creating an IO context to clean pages.
 *
 * %__GFP_ATOMIC indicates that the caller cannot reclaim or sleep and is
 * high priority. Users are typically interrupt handlers. This may be
 * used in conjunction with %__GFP_HIGH
 *
 * %__GFP_MEMALLOC allows access to all memory. This should only be used when
 * the caller guarantees the allocation will allow more memory to be freed
 * very shortly e.g. process exiting or swapping. Users either should
 * be the MM or co-ordinating closely with the VM (e.g. swap over NFS).
 * Users of this flag have to be extremely careful to not deplete the reserve
 * completely and implement a throttling mechanism which controls the
 * consumption of the reserve based on the amount of freed memory.
 * Usage of a pre-allocated pool (e.g. mempool) should be always considered
 * before using this flag.
 *
 * %__GFP_NOMEMALLOC is used to explicitly forbid access to emergency reserves.
 * This takes precedence over the %__GFP_MEMALLOC flag if both are set.
 */
#define __GFP_ATOMIC	((gfp_t)___GFP_ATOMIC)
#define __GFP_HIGH	((gfp_t)___GFP_HIGH)
#define __GFP_MEMALLOC	((gfp_t)___GFP_MEMALLOC)
#define __GFP_NOMEMALLOC ((gfp_t)___GFP_NOMEMALLOC)

/*
 * The set of flags that only affect watermark checking and reclaim
 * behaviour. This is used by the MM to obey the caller constraints
 * about IO, FS and watermark checking while ignoring placement
 * hints such as HIGHMEM usage.
 */
#define GFP_RECLAIM_MASK (__GFP_RECLAIM|__GFP_HIGH|__GFP_IO|__GFP_FS|\
			__GFP_NOWARN|__GFP_RETRY_MAYFAIL|__GFP_NOFAIL|\
			__GFP_NORETRY|__GFP_MEMALLOC|__GFP_NOMEMALLOC|\
			__GFP_ATOMIC)


static inline unsigned int compound_order(struct page* page)
{
	//if (!PageHead(page))	return 0;
	//return page[1].compound_order;
	return 0;
}

/* Returns the number of pages in this potentially compound page. */
static inline unsigned long compound_nr(struct page* page)
{
	//if (!PageHead(page))	return 1;
	//return page[1].compound_nr;
	return 1;
}

 /* Returns the number of bytes in this potentially compound page. */
static inline unsigned long page_size(struct page* page)
{
	return PAGE_SIZE << compound_order(page);
}
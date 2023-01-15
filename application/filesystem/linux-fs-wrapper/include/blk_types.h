///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
#pragma once
/* SPDX-License-Identifier: GPL-2.0 */
/* Block data types and constants.  Directly include this file only to break include dependency loop. */
#include "linux_comm.h"
#include <dokanfs-lib.h>
#include "mm_types.h"

struct bio_set;
struct bio;
struct bio_integrity_payload;
struct io_context;
struct cgroup_subsys_state;
typedef void (bio_end_io_t) (struct bio *);
struct bio_crypt_ctx;

typedef IVirtualDisk block_device;
class CBioSet;

#define bdev_whole(_bdev)			((_bdev)->bd_disk->part0)

#define dev_to_bdev(device)			container_of((device), block_device, bd_device)

#define bdev_kobj(_bdev) 			(&((_bdev)->bd_device.kobj))

/* Block error status values.  See block/blk-core:blk_errors for the details. Alpha cannot write a byte atomically, so we need to use 32-bit value. */
#if defined(CONFIG_ALPHA) && !defined(__alpha_bwx__)
typedef u32 __bitwise blk_status_t;
#else
typedef u8 blk_status_t;
#endif
#define	BLK_STS_OK 0
#define BLK_STS_NOTSUPP		(/*(__force blk_status_t)*/1)
#define BLK_STS_TIMEOUT		(/*(__force blk_status_t)*/2)
#define BLK_STS_NOSPC		(/*(__force blk_status_t)*/3)
#define BLK_STS_TRANSPORT	(/*(__force blk_status_t)*/4)
#define BLK_STS_TARGET		(/*(__force blk_status_t)*/5)
#define BLK_STS_NEXUS		(/*(__force blk_status_t)*/6)
#define BLK_STS_MEDIUM		(/*(__force blk_status_t)*/7)
#define BLK_STS_PROTECTION	(/*(__force blk_status_t)*/8)
#define BLK_STS_RESOURCE	(/*(__force blk_status_t)*/9)
#define BLK_STS_IOERR		(/*(__force blk_status_t)*/10)

/* hack for device mapper, don't use elsewhere: */
#define BLK_STS_DM_REQUEUE    (/*(__force blk_status_t)*/11)

#define BLK_STS_AGAIN		(/*(__force blk_status_t)*/12)


/* BLK_STS_DEV_RESOURCE is returned from the driver to the block layer if device related resources are unavailable, but 
 the driver can guarantee that the queue will be rerun in the future once resources become  available again. This is
 typically the case for device specific resources that are consumed for IO. If the driver fails allocating these 
 resources, we know that inflight (or pending) IO will free these resource upon completion.
 *
 * This is different from BLK_STS_RESOURCE in that it explicitly references a device specific resource. For resources of
 wider scope, allocation failure can happen without having pending IO. This means that we can't rely on request completions 
 freeing these resources, as IO may not be in flight. Examples of that are kernel memory allocations, DMA mappings, or any
 other system wide resources. */
#define BLK_STS_DEV_RESOURCE	(/*(__force blk_status_t)*/13)

/*
 * BLK_STS_ZONE_RESOURCE is returned from the driver to the block layer if zone
 * related resources are unavailable, but the driver can guarantee the queue
 * will be rerun in the future once the resources become available again.
 *
 * This is different from BLK_STS_DEV_RESOURCE in that it explicitly references
 * a zone specific resource and IO to a different zone on the same device could
 * still be served. Examples of that are zones that are write-locked, but a read
 * to the same zone could be served.
 */
#define BLK_STS_ZONE_RESOURCE	(/*(__force blk_status_t)*/14)

/*
 * BLK_STS_ZONE_OPEN_RESOURCE is returned from the driver in the completion
 * path if the device returns a status indicating that too many zone resources
 * are currently open. The same command should be successful if resubmitted
 * after the number of open zones decreases below the device's limits, which is
 * reported in the request_queue's max_open_zones.
 */
#define BLK_STS_ZONE_OPEN_RESOURCE	(/*(__force blk_status_t)*/15)

/*
 * BLK_STS_ZONE_ACTIVE_RESOURCE is returned from the driver in the completion
 * path if the device returns a status indicating that too many zone resources
 * are currently active. The same command should be successful if resubmitted
 * after the number of active zones decreases below the device's limits, which
 * is reported in the request_queue's max_active_zones.
 */
#define BLK_STS_ZONE_ACTIVE_RESOURCE	(/*(__force blk_status_t)*/16)
#if 0

/**
 * blk_path_error - returns true if error may be path related
 * @error: status the request was completed with
 *
 * Description:
 *     This classifies block error status into non-retryable errors and ones
 *     that may be successful if retried on a failover path.
 *
 * Return:
 *     %false - retrying failover path will not help
 *     %true  - may succeed if retried
 */
static inline bool blk_path_error(blk_status_t error)
{
	switch (error) {
	case BLK_STS_NOTSUPP:
	case BLK_STS_NOSPC:
	case BLK_STS_TARGET:
	case BLK_STS_NEXUS:
	case BLK_STS_MEDIUM:
	case BLK_STS_PROTECTION:
		return false;
	}

	/* Anything else could be a path failure, so should be retried */
	return true;
}

/*
 * From most significant bit:
 * 1 bit: reserved for other usage, see below
 * 12 bits: original size of bio
 * 51 bits: issue time of bio
 */
#define BIO_ISSUE_RES_BITS      1
#define BIO_ISSUE_SIZE_BITS     12
#define BIO_ISSUE_RES_SHIFT     (64 - BIO_ISSUE_RES_BITS)
#define BIO_ISSUE_SIZE_SHIFT    (BIO_ISSUE_RES_SHIFT - BIO_ISSUE_SIZE_BITS)
#define BIO_ISSUE_TIME_MASK     ((1ULL << BIO_ISSUE_SIZE_SHIFT) - 1)
#define BIO_ISSUE_SIZE_MASK     \
	(((1ULL << BIO_ISSUE_SIZE_BITS) - 1) << BIO_ISSUE_SIZE_SHIFT)
#define BIO_ISSUE_RES_MASK      (~((1ULL << BIO_ISSUE_RES_SHIFT) - 1))

/* Reserved bit for blk-throtl */
#define BIO_ISSUE_THROTL_SKIP_LATENCY (1ULL << 63)

struct bio_issue {
	u64 value;
};

static inline u64 __bio_issue_time(u64 time)
{
	return time & BIO_ISSUE_TIME_MASK;
}

static inline u64 bio_issue_time(struct bio_issue *issue)
{
	return __bio_issue_time(issue->value);
}

static inline sector_t bio_issue_size(struct bio_issue *issue)
{
	return ((issue->value & BIO_ISSUE_SIZE_MASK) >> BIO_ISSUE_SIZE_SHIFT);
}

static inline void bio_issue_init(struct bio_issue *issue,
				       sector_t size)
{
	size &= (1ULL << BIO_ISSUE_SIZE_BITS) - 1;
	issue->value = ((issue->value & BIO_ISSUE_RES_MASK) |
			(ktime_get_ns() & BIO_ISSUE_TIME_MASK) |
			((u64)size << BIO_ISSUE_SIZE_SHIFT));
}
#endif


//<YUAN> from include/linux/bvec.h

/** struct bio_vec - a contiguous range of physical memory addresses
 * @bv_page:   First page associated with the address range.
 * @bv_len:    Number of bytes in the address range.
 * @bv_offset: Start of the address range relative to the start of @bv_page.
 *
 * The following holds for a bvec if n * PAGE_SIZE < bv_offset + bv_len:	nth_page(@bv_page, n) == @bv_page + n
 * This holds because page_is_mergeable() checks the above property. */
struct bio_vec
{
	page* bv_page;
	unsigned int	bv_len;
	unsigned int	bv_offset;
};

struct bvec_iter
{
	sector_t		bi_sector;		/* device address in 512 byte sectors */
	size_t			bi_size;		/* residual I/O count */	//<YUAN> in byte;
	unsigned int	bi_idx;			/* current index into bvl_vec */
	unsigned int    bi_bvec_done;	/* number of bytes completed in current bvec */
};

struct bvec_iter_all
{
	struct bio_vec	bv;
	int		idx;
	unsigned	done;
};

class CIoCompleteCtrl;

//<YUAN> end of bvec.h

//#define BIO_INLINE_VECS 4
#define BIO_INLINE_VECS		64


/* main unit of I/O for the block layer and lower layers (ie drivers and stacking drivers) */
struct bio 
{
	struct bio			*bi_next;	/* request queue link */
	block_device		*bi_bdev;
	unsigned int		bi_opf;		/* bottom bits req flags, top bits REQ_OP. Use accessors. */
	unsigned short		bi_flags;	/* BIO_* below */
	unsigned short		bi_ioprio;
	unsigned short		bi_write_hint;
	blk_status_t		bi_status;
	atomic_t			__bi_remaining;
	struct bvec_iter	bi_iter;
	bio_end_io_t		*bi_end_io;
	void				*bi_private;
#ifdef CONFIG_BLK_CGROUP
	/* Represents the association of the css and request_queue for the bio. If a bio goes direct to device, it will
	 not have a blkg as it will not have a request_queue associated with it.  The reference is put on release of the bio.
	 */
	struct blkcg_gq		*bi_blkg;
	struct bio_issue	bi_issue;
#ifdef CONFIG_BLK_CGROUP_IOCOST
	u64			bi_iocost_cost;
#endif
#endif

#ifdef CONFIG_BLK_INLINE_ENCRYPTION
	struct bio_crypt_ctx	*bi_crypt_context;
#endif

	union {
#if defined(CONFIG_BLK_DEV_INTEGRITY)
		struct bio_integrity_payload *bi_integrity; /* data integrity */
#endif
	};

	unsigned short		bi_vcnt;	/* how many bio_vec's */

	/* Everything starting with bi_max_vecs will be preserved by bio_reset() */
	unsigned short		bi_max_vecs;	/* max bvl_vecs we can hold */
	atomic_t			__bi_cnt;	/* pin count */
	struct bio_vec		*bi_io_vec;	/* the actual vec list */
//	struct bio_set		*bi_pool;
//	CBioSet* bi_pool;
	CIoCompleteCtrl* bi_pool;
	/* We can inline a number of vecs at the end of the bio, to avoid  double allocations for a small number of bio_vecs. This member MUST obviously be kept at the very end of the bio. */
	bio_vec		bi_inline_vecs[BIO_INLINE_VECS];
//	OVERLAPPED  m_overlapped;	// 用于异步调用	
	OVERLAPPED* m_ol;
	BYTE* m_buf;		// 异步调用必须要保持buffer
};

#define BIO_RESET_BYTES		offsetof(struct bio, bi_max_vecs)

/* bio flags */
enum {
	BIO_NO_PAGE_REF,	/* don't put release vec pages */
	BIO_CLONED,		/* doesn't own data */
	BIO_BOUNCED,		/* bio is a bounce bio */
	BIO_WORKINGSET,		/* contains userspace workingset pages */
	BIO_QUIET,		/* Make BIO Quiet */
	BIO_CHAIN,		/* chained bio, ->bi_remaining in effect */
	BIO_REFFED,		/* bio has elevated ->bi_cnt */
	BIO_THROTTLED,		/* This bio has already been subjected to
				 * throttling rules. Don't do it again. */
	BIO_TRACE_COMPLETION,	/* bio_endio() should trace the final completion
				 * of this bio. */
	BIO_CGROUP_ACCT,	/* has been accounted to a cgroup */
	BIO_TRACKED,		/* set if bio goes through the rq_qos path */
	BIO_REMAPPED,
	BIO_ZONE_WRITE_LOCKED,	/* Owns a zoned device zone write lock */
	BIO_FLAG_LAST
};

typedef __u32 blk_mq_req_flags_t;


/*Operations and flags common to the bio and request structures. We use 8 bits for encoding the operation, and the remaining 24 for flags.
 * The least significant bit of the operation number indicates the data transfer direction:
 *
 *   - if the least significant bit is set transfers are TO the device
 *   - if the least significant bit is not set transfers are FROM the device
 *
 * If a operation does not transfer data the least significant bit has no meaning. */
#define REQ_OP_BITS	8
#define REQ_OP_MASK	((1 << REQ_OP_BITS) - 1)
#define REQ_FLAG_BITS	24

enum req_opf {
	/* read sectors from the device */
	REQ_OP_READ		= 0,
	/* write sectors to the device */
	REQ_OP_WRITE		= 1,
	/* flush the volatile write cache */
	REQ_OP_FLUSH		= 2,
	/* discard sectors */
	REQ_OP_DISCARD		= 3,
	/* securely erase sectors */
	REQ_OP_SECURE_ERASE	= 5,
	/* write the same sector many times */
	REQ_OP_WRITE_SAME	= 7,
	/* write the zero filled sector many times */
	REQ_OP_WRITE_ZEROES	= 9,
	/* Open a zone */
	REQ_OP_ZONE_OPEN	= 10,
	/* Close a zone */
	REQ_OP_ZONE_CLOSE	= 11,
	/* Transition a zone to full */
	REQ_OP_ZONE_FINISH	= 12,
	/* write data at the current zone write pointer */
	REQ_OP_ZONE_APPEND	= 13,
	/* reset a zone write pointer */
	REQ_OP_ZONE_RESET	= 15,
	/* reset all the zone present on the device */
	REQ_OP_ZONE_RESET_ALL	= 17,

	/* SCSI passthrough using struct scsi_request */
	REQ_OP_SCSI_IN		= 32,
	REQ_OP_SCSI_OUT		= 33,
	/* Driver private requests */
	REQ_OP_DRV_IN		= 34,
	REQ_OP_DRV_OUT		= 35,

	REQ_OP_LAST,
};

#ifdef _DEBUG
static const wchar_t* str_req_opf[] = {
	L"OP_READ_",			L"OP_WRITE", 			L"OP_FLUSH",		L"OP_DISCARD",		// 0
	L"UNKOWN",				L"OP_SECURE_ERASE",		L"UNKOWN",			L"OP_WRITE_SAME",	// 4
	L"UNKOWN",				L"OP_WRITE_ZEROES",		L"OP_ZONE_OPEN",	L"OP_ZONE_CLOSE",	// 8
	L"OP_ZONE_FINISH",		L"OP_ZONE_APPEND",		L"UNKOWN",			L"OP_ZONE_RESET",	// 12
	L"UNKOWN",				L"OP_ZONE_RESET_ALL",	L"UNKOWN",			L"UNKOWN",					// 16
	L"UNKOWN",				L"UNKOWN",				L"UNKOWN",			L"UNKOWN",					// 20
	L"UNKOWN",				L"UNKOWN",				L"UNKOWN",			L"UNKOWN",					// 24
	L"UNKOWN",				L"UNKOWN",				L"UNKOWN",			L"UNKOWN",					// 28
	L"OP_SCSI_IN",			L"OP_SCSI_OUT",			L"OP_DRV_IN",		L"OP_DRV_OUT",		// 32
	L"OP_LAST",
};



inline const wchar_t* DebugOutReq(int op)
{
	if (op >= REQ_OP_LAST) return L"OUT OF SCOPE";
	return str_req_opf[op];
}
#define TRACK_BIO_IO(bbio, ev, ...)		LOG_TRACK(L"bio.io", L",bio=%p,op=%s,blk=0x%X,blks=%lld," ev, bbio, \
	str_req_opf[bbio->bi_opf & 0xFF], bbio->bi_iter.bi_sector >> 3, bbio->bi_iter.bi_size >> 12, __VA_ARGS__)
#else
#define TRACK_BIO_IO(bbio, ev, ...)		LOG_TRACK(L"bio.io", L",bio=%p,op=%d,blk=0x%X,blks=%lld," ev, bbio, \
	bbio->bi_opf & 0xFF, bbio->bi_iter.bi_sector >> 3, bbio->bi_iter.bi_size >> 12, __VA_ARGS__)


#endif

enum req_flag_bits {
	__REQ_FAILFAST_DEV =	/* no driver retries of device errors */
		REQ_OP_BITS,
	__REQ_FAILFAST_TRANSPORT, /* no driver retries of transport errors */
	__REQ_FAILFAST_DRIVER,	/* no driver retries of driver errors */
	__REQ_SYNC,		/* request is sync (sync write or read) */
	__REQ_META,		/* metadata io request */
	__REQ_PRIO,		/* boost priority in cfq */
	__REQ_NOMERGE,		/* don't touch this for merging */
	__REQ_IDLE,		/* anticipate more IO after this one */
	__REQ_INTEGRITY,	/* I/O includes block integrity payload */
	__REQ_FUA,		/* forced unit access */
	__REQ_PREFLUSH,		/* request for cache flush */
	__REQ_RAHEAD,		/* read ahead, can fail anytime */
	__REQ_BACKGROUND,	/* background IO */
	__REQ_NOWAIT,           /* Don't wait if request will block */
	/* When a shared kthread needs to issue a bio for a cgroup, doing so synchronously can lead to priority inversions as the kthread can be trapped waiting for that cgroup.  CGROUP_PUNT flag makes submit_bio() punt the actual issuing to a dedicated per-blkcg work item to avoid such priority inversions. */
	__REQ_CGROUP_PUNT,

	/* command specific flags for REQ_OP_WRITE_ZEROES: */
	__REQ_NOUNMAP,		/* do not free blocks when zeroing */

	__REQ_HIPRI,

	/* for driver use */
	__REQ_DRV,
	__REQ_SWAP,		/* swapping request. */
	__REQ_NR_BITS,		/* stops here */
};

#define REQ_FAILFAST_DEV	(1ULL << __REQ_FAILFAST_DEV)
#define REQ_FAILFAST_TRANSPORT	(1ULL << __REQ_FAILFAST_TRANSPORT)
#define REQ_FAILFAST_DRIVER	(1ULL << __REQ_FAILFAST_DRIVER)
#define REQ_SYNC		(1ULL << __REQ_SYNC)
#define REQ_META		(1ULL << __REQ_META)
#define REQ_PRIO		(1ULL << __REQ_PRIO)
#define REQ_NOMERGE		(1ULL << __REQ_NOMERGE)
#define REQ_IDLE		(1ULL << __REQ_IDLE)
#define REQ_INTEGRITY		(1ULL << __REQ_INTEGRITY)
#define REQ_FUA			(1ULL << __REQ_FUA)
#define REQ_PREFLUSH		(1ULL << __REQ_PREFLUSH)
#define REQ_RAHEAD		(1ULL << __REQ_RAHEAD)
#define REQ_BACKGROUND		(1ULL << __REQ_BACKGROUND)
#define REQ_NOWAIT		(1ULL << __REQ_NOWAIT)
#define REQ_CGROUP_PUNT		(1ULL << __REQ_CGROUP_PUNT)

#define REQ_NOUNMAP		(1ULL << __REQ_NOUNMAP)
#define REQ_HIPRI		(1ULL << __REQ_HIPRI)

#define REQ_DRV			(1ULL << __REQ_DRV)
#define REQ_SWAP		(1ULL << __REQ_SWAP)

#define REQ_FAILFAST_MASK \
	(REQ_FAILFAST_DEV | REQ_FAILFAST_TRANSPORT | REQ_FAILFAST_DRIVER)

#define REQ_NOMERGE_FLAGS \
	(REQ_NOMERGE | REQ_PREFLUSH | REQ_FUA)

enum stat_group {
	STAT_READ,
	STAT_WRITE,
	STAT_DISCARD,
	STAT_FLUSH,

	NR_STAT_GROUPS
};

#define bio_op(bio) 	((bio)->bi_opf & REQ_OP_MASK)
#define req_op(req) 	((req)->cmd_flags & REQ_OP_MASK)

/* obsolete, don't use in new code */
static inline void bio_set_op_attrs(struct bio *bio, unsigned op, unsigned op_flags)
{
	bio->bi_opf = op | op_flags;
}
#if 0

static inline bool op_is_write(unsigned int op)
{
	return (op & 1);
}

/*
 * Check if the bio or request is one that needs special treatment in the
 * flush state machine.
 */
static inline bool op_is_flush(unsigned int op)
{
	return op & (REQ_FUA | REQ_PREFLUSH);
}

/*
 * Reads are always treated as synchronous, as are requests with the FUA or
 * PREFLUSH flag.  Other operations may be marked as synchronous using the
 * REQ_SYNC flag.
 */
static inline bool op_is_sync(unsigned int op)
{
	return (op & REQ_OP_MASK) == REQ_OP_READ ||
		(op & (REQ_SYNC | REQ_FUA | REQ_PREFLUSH));
}

static inline bool op_is_discard(unsigned int op)
{
	return (op & REQ_OP_MASK) == REQ_OP_DISCARD;
}

/*
 * Check if a bio or request operation is a zone management operation, with
 * the exception of REQ_OP_ZONE_RESET_ALL which is treated as a special case
 * due to its different handling in the block layer and device response in
 * case of command failure.
 */
static inline bool op_is_zone_mgmt(enum req_opf op)
{
	switch (op & REQ_OP_MASK) {
	case REQ_OP_ZONE_RESET:
	case REQ_OP_ZONE_OPEN:
	case REQ_OP_ZONE_CLOSE:
	case REQ_OP_ZONE_FINISH:
		return true;
	default:
		return false;
	}
}

static inline int op_stat_group(unsigned int op)
{
	if (op_is_discard(op))
		return STAT_DISCARD;
	return op_is_write(op);
}

typedef unsigned int blk_qc_t;
#define BLK_QC_T_NONE		-1U
#define BLK_QC_T_SHIFT		16
#define BLK_QC_T_INTERNAL	(1U << 31)

static inline bool blk_qc_t_valid(blk_qc_t cookie)
{
	return cookie != BLK_QC_T_NONE;
}

static inline unsigned int blk_qc_t_to_queue_num(blk_qc_t cookie)
{
	return (cookie & ~BLK_QC_T_INTERNAL) >> BLK_QC_T_SHIFT;
}

static inline unsigned int blk_qc_t_to_tag(blk_qc_t cookie)
{
	return cookie & ((1u << BLK_QC_T_SHIFT) - 1);
}

static inline bool blk_qc_t_is_internal(blk_qc_t cookie)
{
	return (cookie & BLK_QC_T_INTERNAL) != 0;
}

struct blk_rq_stat {
	u64 mean;
	u64 min;
	u64 max;
	u32 nr_samples;
	u64 batch;
};

#endif //TODO

void __bio_add_page(bio* bio, page* page, unsigned int len, unsigned int off);

////<YUAN> from block/bio.c
///* __bio_add_page - add page(s) to a bio in a new segment
// * @bio: destination bio
// * @page: start page to add
// * @len: length of the data to add, may cross pages
// * @off: offset of the data relative to @page, may cross pages
// *
// * Add the data at @page + @off to @bio as a new bvec.  The caller must ensure that @bio has space for another bvec.*/
//inline void __bio_add_page(bio* bio, page* page, unsigned int len, unsigned int off)
//{
//	bio_vec* bv = &bio->bi_io_vec[bio->bi_vcnt];
//
//	//WARN_ON_ONCE(bio_flagged(bio, BIO_CLONED));
//	//WARN_ON_ONCE(bio_full(bio, len));
//
//	bv->bv_page = page;
//	bv->bv_offset = off;
//	bv->bv_len = len;
//
//	bio->bi_iter.bi_size += len;
//	bio->bi_vcnt++;
//
//	if (!bio_flagged(bio, BIO_WORKINGSET) && unlikely(PageWorkingset(page)))
//		bio_set_flag(bio, BIO_WORKINGSET);
//}

//
///*	bio_add_page	-	attempt to add page(s) to bio
// *	@bio: destination bio
// *	@page: start page to add
// *	@len: vec entry length, may cross pages
// *	@offset: vec entry offset relative to @page, may cross pages
// *
// *	Attempt to add page(s) to the bio_vec maplist. This will only fail	if either bio->bi_vcnt == bio->bi_max_vecs or it's a cloned bio. */
//// offset表示在page bio_vec在page中的偏移量
//inline int bio_add_page(bio* bio, page* page, unsigned int len, unsigned int offset)
//{
//	//bool same_page = false;
//	//__bio_add_page(bio, page, len, offset);
//	//return len;
//
//	bool same_page = false;
//
//	if (!__bio_try_merge_page(bio, page, len, offset, &same_page)) {
//		if (bio_full(bio, len))
//			return 0;
//		__bio_add_page(bio, page, len, offset);
//	}
//	return len;
//}

//<YUAN> from include/linux/bio.h
static inline void bio_set_flag(struct bio* bio, unsigned int bit)
{
	bio->bi_flags |= (1U << bit);
}

static inline void bio_clear_flag(struct bio* bio, unsigned int bit)
{
	bio->bi_flags &= ~(1U << bit);
}

inline void bio_set_dev(bio * bio, block_device * bdev)
{
	bio_clear_flag(bio, BIO_REMAPPED);
	if ((bio)->bi_bdev != (bdev))	bio_clear_flag(bio, BIO_THROTTLED);
	(bio)->bi_bdev = (bdev);	
#if 0 //DO NOTHING
	bio_associate_blkg(bio);	
#endif
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// == bio set
void bio_init(bio* bio, bio_vec* table, unsigned short max_vecs);

#if 0
class CBioSet
{
public:
	//<YUAN> from block/bio.c, 简化设计，new，并且初始化bio
	/** bio_alloc_bioset - alloc_obj a bio for I/O
	 * @gfp_mask:   the GFP_* mask given to the slab allocator
	 * @nr_iovecs:	number of iovecs to pre-alloc_obj
	 * @bs:		the bio_set to alloc_obj from.
	 *
	 * Allocate a bio from the mempools in @bs.
	 *
	 * If %__GFP_DIRECT_RECLAIM is set then bio_alloc will always be able to alloc_obj a bio.  This is due to the mempool
	 guarantees.  To make this work, callers must never alloc_obj more than 1 bio at a time from the general pool. 
	 Callers that need to alloc_obj more than 1 bio must always submit the previously allocated bio for IO before
	 attempting to alloc_obj a new one. Failure to do so can cause deadlocks under memory pressure.
	 *
	 * Note that when running under submit_bio_noacct() (i.e. any block driver), bios are not submitted until after you
	 return - see the code in submit_bio_noacct() that converts recursion into iteration, to prevent stack overflows.
	 *
	 * This would normally mean allocating multiple bios under submit_bio_noacct() would be susceptible to deadlocks, 
	 but we have deadlock avoidance code that resubmits any blocked bios from a rescuer thread.
	 *
	 * However, we do not guarantee forward progress for allocations from other mempools. Doing multiple allocations from 
	 the same mempool under submit_bio_noacct() should be avoided - instead, use bio_set's front_pad for per bio 
	 allocations.
	 *
	 * Returns: Pointer to new bio on success, NULL on failure. */
	bio* bio_alloc_bioset(gfp_t gfp_mask, unsigned short nr_iovecs/*, void * bio_set*/);
	void bio_put(bio* bb);
};

inline void bio_put(bio* bio)
{
	if (bio && bio->bi_pool) bio->bi_pool->bio_put(bio);
}

#endif
static inline bio_vec* bvec_init_iter_all(bvec_iter_all* iter_all)
{
	iter_all->done = 0;
	iter_all->idx = 0;
	return &iter_all->bv;
}

void bio_put(bio* bio);

static inline void bvec_advance(const struct bio_vec* bvec, struct bvec_iter_all* iter_all)
{
	struct bio_vec* bv = &iter_all->bv;
	if (iter_all->done)
	{
		bv->bv_page++;
		bv->bv_offset = 0;
	}
	else
	{
		bv->bv_page = bvec->bv_page + (bvec->bv_offset >> PAGE_SHIFT);
		bv->bv_offset = bvec->bv_offset & ~PAGE_MASK;
	}
	bv->bv_len = min((unsigned int)(PAGE_SIZE - bv->bv_offset), (unsigned int)(bvec->bv_len - iter_all->done));
	iter_all->done += bv->bv_len;

	if (iter_all->done == bvec->bv_len)
	{
		iter_all->idx++;
		iter_all->done = 0;
	}
}

static inline bool bio_next_segment(const bio* bio, bvec_iter_all* iter)
{
	if (iter->idx >= bio->bi_vcnt)	return false;
	bvec_advance(&bio->bi_io_vec[iter->idx], iter);
	return true;
}

#define BIO_MAX_VECS		256U

static inline unsigned int bio_max_segs(unsigned int nr_segs)
{
	return min(nr_segs, BIO_MAX_VECS);
}

/* drivers should _never_ use the all version - the bio may have been split before it got to the driver and the driver won't own all of it*/
#define bio_for_each_segment_all(bvl, bio, iter) 	for (bvl = bvec_init_iter_all(&iter); bio_next_segment((bio), &iter); )

// <YUAN> from block/bio.c

 /**
  * bio_reset - reinitialize a bio
  * @bio:	bio to reset
  *
  * Description:
  *   After calling bio_reset(), @bio will be in the same state as a freshly
  *   allocated bio returned bio bio_alloc_bioset() - the only fields that are
  *   preserved are the ones that are initialized by bio_alloc_bioset(). See
  *   comment in struct bio.
  */
//void bio_reset(struct bio* bio)
//{
//	bio_uninit(bio);
//	memset(bio, 0, BIO_RESET_BYTES);
//	atomic_set(&bio->__bi_remaining, 1);
//}
//
//static struct bio* __bio_chain_endio(struct bio* bio)
//{
//	struct bio* parent = bio->bi_private;
//
//	if (bio->bi_status && !parent->bi_status)
//		parent->bi_status = bio->bi_status;
//	bio_put(bio);
//	return parent;
//}
//
inline void bio_chain_endio(struct bio* bio)
{
	JCASSERT(0);
//	bio_endio(__bio_chain_endio(bio));
}


 /**
  * bio_chain - chain bio completions
  * @bio: the target bio
  * @parent: the parent bio of @bio
  *
  * The caller won't have a bi_end_io called when @bio completes - instead,
  * @parent's bi_end_io won't be called until both @parent and @bio have
  * completed; the chained bio will also be freed when it completes.
  *
  * The caller must not set bi_private or bi_end_io in @bio.
  */
inline void bio_chain(struct bio* bio, struct bio* parent)
{
	JCASSERT(!(bio->bi_private || bio->bi_end_io));

	bio->bi_private = parent;
	bio->bi_end_io = bio_chain_endio;
	//bio_inc_remaining(parent);
	bio_set_flag(parent, BIO_CHAIN);
	atomic_inc(&parent->__bi_remaining);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
#include "pch.h"
#include <dokanfs-lib.h>
#include "../include/blk_types.h"

LOCAL_LOGGER_ENABLE(L"linuxfs.bio", LOGGER_LEVEL_DEBUGINFO);

#if 0
bio* CBioSet::bio_alloc_bioset(gfp_t gfp_mask, unsigned short nr_iovecs)
{
	gfp_t saved_gfp = gfp_mask;

	/* should not use nobvec bioset for nr_iovecs > 0 */
//	if (WARN_ON_ONCE(!mempool_initialized(&bs->bvec_pool) && nr_iovecs > 0))	return NULL;

	/* submit_bio_noacct() converts recursion to iteration; this means if we're running beneath it, any bios we alloc_obj and submit will not be submitted (and thus freed) until after we return.
	 *
	 * This exposes us to a potential deadlock if we alloc_obj multiple bios from the same bio_set() while running underneath submit_bio_noacct(). If we were to alloc_obj multiple bios (say a stacking block driver that was splitting bios), we would deadlock if we exhausted the mempool's reserve.
	 *
	 * We solve this, and guarantee forward progress, with a rescuer workqueue per bio_set. If we go to alloc_obj and there are bios on current->bio_list, we first try the allocation without __GFP_DIRECT_RECLAIM; if that fails, we punt those bios we would be blocking to the rescuer workqueue before we retry with the original gfp_flags.	 */
	bio* ptr_bio = new bio;
	if (!ptr_bio)	THROW_ERROR(ERR_MEM, L"failed on allocating bio");
	memset(ptr_bio, 0, sizeof(bio));

	//if (current->bio_list && (!bio_list_empty(&current->bio_list[0]) || !bio_list_empty(&current->bio_list[1])) 
	//	&& bs->rescue_workqueue)
	//	gfp_mask &= ~__GFP_DIRECT_RECLAIM;
	//p = mempool_alloc(&bs->bio_pool, gfp_mask);
	//if (!p && gfp_mask != saved_gfp)
	//{
	//	punt_bios_to_rescuer(bs);
	//	gfp_mask = saved_gfp;
	//	p = mempool_alloc(&bs->bio_pool, gfp_mask);
	//}
	//if (unlikely(!p))	return NULL;
	//bio = p + bs->front_pad;

	// nr_iovecs=bio������page����
	if (nr_iovecs > BIO_INLINE_VECS)
	{
		bio_vec* bvl = new bio_vec[nr_iovecs];
		bio_init(ptr_bio, bvl, nr_iovecs);
	}
	else if (nr_iovecs)
	{
		bio_init(ptr_bio, ptr_bio->bi_inline_vecs, BIO_INLINE_VECS);
	}
	else
	{
		bio_init(ptr_bio, NULL, 0);
	}

	ptr_bio->bi_pool = this;
	return ptr_bio;
}

void CBioSet::bio_put(bio* bb)
{
	LOG_TRACK(L"bio", L"bio=%p, delete bio", bb);
	if (bb->bi_io_vec != bb->bi_inline_vecs) delete[] (bb->bi_io_vec);
	delete bb;
}
#endif


/* Users of this function have their own bio allocation. Subsequently, they must remember to pair any call to 
bio_init() with bio_uninit() when IO has completed, or when the bio is released. */
void bio_init(bio* bio, bio_vec* table, unsigned short max_vecs)
{
	bio->bi_next = NULL;
	bio->bi_bdev = NULL;
	bio->bi_opf = 0;
	bio->bi_flags = 0;
	bio->bi_ioprio = 0;
	bio->bi_write_hint = 0;
	bio->bi_status = 0;
	bio->bi_iter.bi_sector = 0;
	bio->bi_iter.bi_size = 0;
	bio->bi_iter.bi_idx = 0;
	bio->bi_iter.bi_bvec_done = 0;
	bio->bi_end_io = NULL;
	bio->bi_private = NULL;
#ifdef CONFIG_BLK_CGROUP
	bio->bi_blkg = NULL;
	bio->bi_issue.value = 0;
#ifdef CONFIG_BLK_CGROUP_IOCOST
	bio->bi_iocost_cost = 0;
#endif
#endif
#ifdef CONFIG_BLK_INLINE_ENCRYPTION
	bio->bi_crypt_context = NULL;
#endif
#ifdef CONFIG_BLK_DEV_INTEGRITY
	bio->bi_integrity = NULL;
#endif
	bio->bi_vcnt = 0;

	atomic_set(&bio->__bi_remaining, 1);
	atomic_set(&bio->__bi_cnt, 1);

	bio->bi_max_vecs = max_vecs;
	bio->bi_io_vec = table;
	bio->bi_pool = NULL;
}

/** DECLARE_COMPLETION - declare and initialize a completion structure
 * @work:  identifier for the completion structure
 *
 * This macro declares and initializes a completion structure. Generally used for static declarations. You should use the _ONSTACK variant for automatic variables. */
#define DECLARE_COMPLETION(work) 	struct completion work = COMPLETION_INITIALIZER(work)

/* Lockdep needs to run a non-constant initializer for on-stack completions - so we use the _ONSTACK() variant for those that are on the kernel stack: */

 /** DECLARE_COMPLETION_ONSTACK - declare and initialize a completion structure
  * @work:  identifier for the completion structure
  *
  * This macro declares and initializes a completion structure on the kernel stack. */
#ifdef CONFIG_LOCKDEP
# define DECLARE_COMPLETION_ONSTACK(work) \
	struct completion work = COMPLETION_INITIALIZER_ONSTACK(work)
# define DECLARE_COMPLETION_ONSTACK_MAP(work, map) \
	struct completion work = COMPLETION_INITIALIZER_ONSTACK_MAP(work, map)
#else
# define DECLARE_COMPLETION_ONSTACK(work) DECLARE_COMPLETION(work)
# define DECLARE_COMPLETION_ONSTACK_MAP(work, map) DECLARE_COMPLETION(work)
#endif


// from bio.c
/* bio_endio - end I/O on a bio
* @bio:	bio
*
* Description:
*   bio_endio() will end I/O on the whole bio. bio_endio() is the preferred way to end I/O on a bio. No one should call bi_end_io() directly on a bio unless they own it and thus know that it has an end_io function.
*   bio_endio() can be called several times on a bio that has been chained using bio_chain().  The ->bi_end_io() function will only be called the last time. */
void bio_endio(struct bio* bio)
{
//again:
	//if (!bio_remaining_done(bio))	return;
	//if (!bio_integrity_endio(bio))	return;

	//if (bio->bi_bdev) rq_qos_done_bio(bio->bi_bdev->bd_disk->queue, bio);

	//if (bio->bi_bdev && bio_flagged(bio, BIO_TRACE_COMPLETION))
	//{
	//	trace_block_bio_complete(bio->bi_bdev->bd_disk->queue, bio);
	//	bio_clear_flag(bio, BIO_TRACE_COMPLETION);
	//}

	/* Need to have a real endio function for chained bios, otherwise various corner cases will break (like stacking block devices that save/restore bi_end_io) - however, we want to avoid unbounded recursion and blowing the stack. Tail call optimization would handle this, but compiling with frame pointers also disables gcc's sibling call optimization. */
	//if (bio->bi_end_io == bio_chain_endio) 
	//{
	//	bio = __bio_chain_endio(bio);
	//	goto again;
	//}

	//blk_throtl_bio_endio(bio);
	///* release cgroup info */
	//bio_uninit(bio);
	if (bio->bi_end_io)
		bio->bi_end_io(bio);
}
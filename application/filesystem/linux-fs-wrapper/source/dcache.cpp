///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
#include "pch.h"
// SPDX-License-Identifier: GPL-2.0-only
/*
 * fs/dcache.c
 *
 * Complete reimplementation
 * (C) 1997 Thomas Schoebel-Theuer,
 * with heavy changes by Linus Torvalds
 */

/*
 * Notes on the allocation strategy:
 *
 * The dcache is a master of the icache - whenever a dcache entry
 * exists, the inode will always exist. "iput()" is done either when
 * the dcache entry is deleted or garbage collected.
 */

#include <list>
#include "../include/linux_comm.h"
#include "../include/fs.h"
#include "../include/dcache.h"

//#include <linux/ratelimit.h>
//#include <linux/string.h>
//#include <linux/mm.h>
//#include <linux/fs.h>
//#include <linux/fscrypt.h>
//#include <linux/fsnotify.h>
//#include <linux/slab.h>
//#include <linux/init.h>
//#include <linux/hash.h>
//#include <linux/cache.h>
//#include <linux/export.h>
//#include <linux/security.h>
//#include <linux/seqlock.h>
//#include <linux/memblock.h>
//#include <linux/bit_spinlock.h>
//#include <linux/rculist_bl.h>
//#include <linux/list_lru.h>
#include "../include/list.h"
#include "../include/list_bl.h"
//#include "internal.h"
//#include "mount.h"
LOCAL_LOGGER_ENABLE(L"f2fs.dcache", LOGGER_LEVEL_DEBUGINFO);

/*
 * Usage:
 * dcache->d_inode->i_lock protects:
 *   - i_dentry, d_u.d_alias, d_inode of aliases
 * dcache_hash_bucket lock protects:
 *   - the dcache hash table
 * s_roots bl list spinlock protects:
 *   - the s_roots list (see __d_drop)
 * dentry->d_sb->s_dentry_lru_lock protects:
 *   - the dcache lru lists and counters
 * d_lock protects:
 *   - d_flags
 *   - d_name
 *   - d_lru
 *   - d_count
 *   - d_unhashed()
 *   - d_parent and d_subdirs
 *   - childrens' d_child and d_parent
 *   - d_u.d_alias, d_inode
 *
 * Ordering:
 * dentry->d_inode->i_lock
 *   dentry->d_lock
 *     dentry->d_sb->s_dentry_lru_lock
 *     dcache_hash_bucket lock
 *     s_roots lock
 *
 * If there is an ancestor relationship:
 * dentry->d_parent->...->d_parent->d_lock
 *   ...
 *     dentry->d_parent->d_lock
 *       dentry->d_lock
 *
 * If no ancestor relationship:
 * arbitrary, since it's serialized on rename_lock
 */
#if 0 //<TODO>
int sysctl_vfs_cache_pressure __read_mostly = 100;
EXPORT_SYMBOL_GPL(sysctl_vfs_cache_pressure);

__cacheline_aligned_in_smp DEFINE_SEQLOCK(rename_lock);

EXPORT_SYMBOL(rename_lock);

static struct kmem_cache *dentry_cache __read_mostly;

#endif

static void dentry_free(dentry* );
static void d_shrink_del(dentry*);
static void dentry_unlink_inode(dentry*);
static void d_lru_del(dentry*);
//static void d_shrink_add(dentry*, list_head* list);
static inline void __d_set_inode_and_type(dentry* dentry, inode* inode, unsigned type_flags);

// 全局变量移植到super_block中
//seqlock_t rename_lock;

const qstr empty_name(L"");
//EXPORT_SYMBOL(empty_name);
//const qstr slash_name = QSTR_INIT("/", 1);
const qstr slash_name(L"\\");
//EXPORT_SYMBOL(slash_name);
const qstr dotdot_name(L"..");
//EXPORT_SYMBOL(dotdot_name);

dentry* d_ancestor(dentry* p1, dentry* p2);


/*
 * This is the single most critical data structure when it comes
 * to the dcache: the hashtable for lookups. Somebody should try
 * to make this good - I've just made it work.
 *
 * This hash-function tries to avoid losing too many bits of hash
 * information, yet avoid using a prime hash-size or similar.
 */

static unsigned int d_hash_shift /*__read_mostly*/;

static struct hlist_bl_head *dentry_hashtable /*__read_mostly*/;

static inline hlist_bl_head *d_hash(UINT64 hash)
{
	return dentry_hashtable + (hash >> d_hash_shift);
}

#define IN_LOOKUP_SHIFT 10
static struct hlist_bl_head in_lookup_hashtable[1 << IN_LOOKUP_SHIFT];

static inline struct hlist_bl_head *in_lookup_hash(const struct dentry *parent, UINT64 hash)
{
#if 0
	hash += (unsigned long) parent / L1_CACHE_BYTES;
	return in_lookup_hashtable + hash_32(hash, IN_LOOKUP_SHIFT);
#else
	JCASSERT(0);
	return 0;
#endif
}
#if 0 //TODO


/* Statistics gathering. */
struct dentry_stat_t dentry_stat = {
	.age_limit = 45,
};

static DEFINE_PER_CPU(long, nr_dentry);
static DEFINE_PER_CPU(long, nr_dentry_unused);
static DEFINE_PER_CPU(long, nr_dentry_negative);

#if defined(CONFIG_SYSCTL) && defined(CONFIG_PROC_FS)

/*
 * Here we resort to our own counters instead of using generic per-cpu counters
 * for consistency with what the vfs inode code does. We are expected to harvest
 * better code and performance by having our own specialized counters.
 *
 * Please note that the loop is done over all possible CPUs, not over all online
 * CPUs. The reason for this is that we don't want to play games with CPUs going
 * on and off. If one of them goes off, we will just keep their counters.
 *
 * glommer: See cffbc8a for details, and if you ever intend to change this,
 * please update all vfs counters to match.
 */
static long get_nr_dentry(void)
{
	int i;
	long sum = 0;
	for_each_possible_cpu(i)
		sum += per_cpu(nr_dentry, i);
	return sum < 0 ? 0 : sum;
}

static long get_nr_dentry_unused(void)
{
	int i;
	long sum = 0;
	for_each_possible_cpu(i)
		sum += per_cpu(nr_dentry_unused, i);
	return sum < 0 ? 0 : sum;
}

static long get_nr_dentry_negative(void)
{
	int i;
	long sum = 0;

	for_each_possible_cpu(i)
		sum += per_cpu(nr_dentry_negative, i);
	return sum < 0 ? 0 : sum;
}

int proc_nr_dentry(struct ctl_table *table, int write, void *buffer,
		   size_t *lenp, loff_t *ppos)
{
	dentry_stat.nr_dentry = get_nr_dentry();
	dentry_stat.nr_unused = get_nr_dentry_unused();
	dentry_stat.nr_negative = get_nr_dentry_negative();
	return proc_doulongvec_minmax(table, write, buffer, lenp, ppos);
}
#endif

/*
 * Compare 2 name strings, return 0 if they match, otherwise non-zero.
 * The strings are both count bytes long, and count is non-zero.
 */
#ifdef CONFIG_DCACHE_WORD_ACCESS

#include <asm/word-at-a-time.h>
/*
 * NOTE! 'cs' and 'scount' come from a dentry, so it has a
 * aligned allocation for this particular component. We don't
 * strictly need the load_unaligned_zeropad() safety, but it
 * doesn't hurt either.
 *
 * In contrast, 'ct' and 'tcount' can be from a pathname, and do
 * need the careful unaligned handling.
 */
static inline int dentry_string_cmp(const unsigned char *cs, const unsigned char *ct, unsigned tcount)
{
	unsigned long a,b,mask;

	for (;;) {
		a = read_word_at_a_time(cs);
		b = load_unaligned_zeropad(ct);
		if (tcount < sizeof(unsigned long))
			break;
		if (unlikely(a != b))
			return 1;
		cs += sizeof(unsigned long);
		ct += sizeof(unsigned long);
		tcount -= sizeof(unsigned long);
		if (!tcount)
			return 0;
	}
	mask = bytemask_from_count(tcount);
	return unlikely(!!((a ^ b) & mask));
}

#else

static inline int dentry_string_cmp(const unsigned char *cs, const unsigned char *ct, unsigned tcount)
{
	do {
		if (*cs != *ct)
			return 1;
		cs++;
		ct++;
		tcount--;
	} while (tcount);
	return 0;
}

#endif

static inline int dentry_cmp(const struct dentry *dentry, const unsigned char *ct, unsigned tcount)
{
	/*
	 * Be careful about RCU walk racing with rename:
	 * use 'READ_ONCE' to fetch the name pointer.
	 *
	 * NOTE! Even if a rename will mean that the length
	 * was not loaded atomically, we don't care. The
	 * RCU walk will check the sequence count eventually,
	 * and catch it. And we won't overrun the buffer,
	 * because we're reading the name pointer atomically,
	 * and a dentry name is guaranteed to be properly
	 * terminated with a NUL byte.
	 *
	 * End result: even if 'len' is wrong, we'll exit
	 * early because the data cannot match (there can
	 * be no NUL in the ct/tcount data)
	 */
	const unsigned char *cs = READ_ONCE(dentry->d_name.name);

	return dentry_string_cmp(cs, ct, tcount);
}
#endif

struct external_name 
{
	union {
		atomic_t count;
#if 0 //<TODO>
		struct rcu_head head;
#endif
	} u;
	char name[1];
};


//static inline external_name *to_external_name(dentry *entry)
//{
//	return container_of<external_name>(entry->d_name.name, offsetof( external_name, name[0]));
//}

#if 0

static void __d_free(rcu_head *head)
{
	struct dentry *dentry = container_of<dentry>(head, offsetof(dentry, (d_u.d_rcu)));
	kmem_cache_free(dentry_cache, dentry); 
}


static void __d_free_external(struct rcu_head *head)
{
	struct dentry *dentry = container_of(head, struct dentry, d_u.d_rcu);
	kfree(to_external_name(dentry));
	kmem_cache_free(dentry_cache, dentry);
}
#endif

//static inline int dname_external(const struct dentry *dentry)
//{
//	return dentry->d_name.name != dentry->d_iname;
//}
#if 0 //TODO

void take_dentry_name_snapshot(struct name_snapshot *name, struct dentry *dentry)
{
	spin_lock(&dentry->d_lock);
	name->name = dentry->d_name;
	if (unlikely(dname_external(dentry))) {
		atomic_inc(&to_external_name(dentry)->u.count);
	} else {
		memcpy(name->inline_name, dentry->d_iname,
		       dentry->d_name.len() + 1);
		name->name.name = name->inline_name;
	}
	spin_unlock(&dentry->d_lock);
}
EXPORT_SYMBOL(take_dentry_name_snapshot);

void release_dentry_name_snapshot(struct name_snapshot *name)
{
	if (unlikely(name->name.name != name->inline_name)) {
		struct external_name *p;
		p = container_of(name->name.name, struct external_name, name[0]);
		if (unlikely(atomic_dec_and_test(&p->u.count)))
			kfree_rcu(p, u.head);
	}
}
EXPORT_SYMBOL(release_dentry_name_snapshot);

#endif

static inline void __d_set_inode_and_type(dentry *dentry, inode *inode, unsigned type_flags)
{
	unsigned flags;

	dentry->d_inode = inode;
	flags = READ_ONCE(dentry->d_flags);
	flags &= ~(DCACHE_ENTRY_TYPE | DCACHE_FALLTHRU);
	flags |= type_flags;
//	smp_store_release(&dentry->d_flags, flags);
	InterlockedExchange(&dentry->d_flags, flags);
}

static inline void __d_clear_type_and_inode(dentry *ddentry)
{
	unsigned flags = READ_ONCE(ddentry->d_flags);

	flags &= ~(DCACHE_ENTRY_TYPE | DCACHE_FALLTHRU);
	WRITE_ONCE(ddentry->d_flags, flags);
	ddentry->d_inode = NULL;
//	if (ddentry->d_flags & DCACHE_LRU_LIST)		this_cpu_inc(nr_dentry_negative);
}

static void dentry_free(dentry *ddentry)
{
	WARN_ON(!hlist_unhashed(&ddentry->d_u.d_alias));
	ddentry->m_manager->free(ddentry);
#if 0
	if (unlikely(dname_external(ddentry))) 
	{
		struct external_name *p = to_external_name(ddentry);
		if (likely(atomic_dec_and_test(&p->u.count))) 
		{
			call_rcu(&ddentry->d_u.d_rcu, __d_free_external);
			return;
		}
	}
#endif
	/* if ddentry was never visible to RCU, immediate free is OK */
#if 0
	if (ddentry->d_flags & DCACHE_NORCU)		__d_free(&ddentry->d_u.d_rcu);
	else		call_rcu(&ddentry->d_u.d_rcu, __d_free);
#endif
}


/* Release the dentry's inode, using the filesystem d_iput() operation if defined.*/
static void dentry_unlink_inode(dentry * ddentry)
	//__releases(ddentry->d_lock)
	//__releases(dentry->d_inode->i_lock)
{
	inode *iinode = ddentry->d_inode;

	raw_write_seqcount_begin(&ddentry->d_seq);
	__d_clear_type_and_inode(ddentry);
	hlist_del_init(&ddentry->d_u.d_alias);
	raw_write_seqcount_end(&ddentry->d_seq);
	iinode->remove_alias(ddentry);
	spin_unlock(&ddentry->d_lock);
//	spin_unlock(&iinode->i_lock);

	iinode->unlock();

	if (!iinode->i_nlink)
	{
#if 0	//<NOT SUPPORT> fsnotify通知用户层文件系统的事件。这里不支持。
		fsnotify_inoderemove(iinode);
#endif
	}
	if (ddentry->d_op && ddentry->d_op->d_iput)
		ddentry->d_op->d_iput(ddentry, iinode);
	else	iput(iinode);
}


/* The DCACHE_LRU_LIST bit is set whenever the 'd_lru' entry is in use - which includes both the "real" per-superblock LRU list _and_ the DCACHE_SHRINK_LIST use.
 * The DCACHE_SHRINK_LIST bit is set whenever the dentry is on the shrink list (ie not on the superblock LRU list).
 * The per-cpu "nr_dentry_unused" counters are updated with the DCACHE_LRU_LIST bit.
 * The per-cpu "nr_dentry_negative" counters are only updated when deleted from or added to the per-superblock LRU list, not from/to the shrink list. That is to avoid an unneeded dec/inc pair when moving from LRU to shrink list in select_collect().
 * These helper functions make sure we always follow the rules. d_lock must be held by the caller. */

//#define D_FLAG_VERIFY(dentry,x) WARN_ON_ONCE(((dentry)->d_flags & (DCACHE_LRU_LIST | DCACHE_SHRINK_LIST)) != (x))

static void d_lru_add(dentry *ddentry)
{
//	D_FLAG_VERIFY(dentry, 0);
	ddentry->d_flags |= DCACHE_LRU_LIST;
//	this_cpu_inc(nr_dentry_unused);
//	if (d_is_negative(dentry)) this_cpu_inc(nr_dentry_negative);
	ddentry->d_sb->dentry_list_lru_add(ddentry);
//	WARN_ON_ONCE(!list_lru_add(&dentry->d_sb->s_dentry_lru, &dentry->d_lru));
}


static void d_lru_del(dentry *ddentry)
{
//	D_FLAG_VERIFY(ddentry, DCACHE_LRU_LIST);
	ddentry->d_flags &= ~DCACHE_LRU_LIST;
	//this_cpu_dec(nr_dentry_unused);
	//if (d_is_negative(ddentry))
	//	this_cpu_dec(nr_dentry_negative);
	//list_lru_del(&ddentry->d_sb->s_dentry_lru, &ddentry->d_lru);
	ddentry->d_sb->dentry_list_lru_del(ddentry);
//	WARN_ON_ONCE(!);
}

//static void d_shrink_del(dentry *ddentry)
//{
////	D_FLAG_VERIFY(dentry, DCACHE_SHRINK_LIST | DCACHE_LRU_LIST);
//	list_del_init(&ddentry->d_lru);
//	ddentry->d_flags &= ~(DCACHE_SHRINK_LIST | DCACHE_LRU_LIST);
////	this_cpu_dec(nr_dentry_unused);
//}
//
//static void d_shrink_add(dentry *ddentry, list_head *list)
//{
////	D_FLAG_VERIFY(dentry, 0);
//	list_add(&ddentry->d_lru, list);
//	ddentry->d_flags |= DCACHE_SHRINK_LIST | DCACHE_LRU_LIST;
////	this_cpu_inc(nr_dentry_unused);
//}

#if 0 //<TODO>

/*
 * These can only be called under the global LRU lock, ie during the
 * callback for freeing the LRU list. "isolate" removes it from the
 * LRU lists entirely, while shrink_move moves it to the indicated
 * private list.
 */
static void d_lru_isolate(struct list_lru_one *lru, struct dentry *dentry)
{
	D_FLAG_VERIFY(dentry, DCACHE_LRU_LIST);
	dentry->d_flags &= ~DCACHE_LRU_LIST;
	this_cpu_dec(nr_dentry_unused);
	if (d_is_negative(dentry))
		this_cpu_dec(nr_dentry_negative);
	list_lru_isolate(lru, &dentry->d_lru);
}

static void d_lru_shrink_move(struct list_lru_one *lru, struct dentry *dentry,
			      struct list_head *list)
{
	D_FLAG_VERIFY(dentry, DCACHE_LRU_LIST);
	dentry->d_flags |= DCACHE_SHRINK_LIST;
	if (d_is_negative(dentry))
		this_cpu_dec(nr_dentry_negative);
	list_lru_isolate_move(lru, &dentry->d_lru, list);
}
#endif

static void ___d_drop(struct dentry *dentry)
{
	struct hlist_bl_head *b;
	/* Hashed dentries are normally on the dentry hashtable, with the exception of those newly allocated by
	 * d_obtain_root, which are always IS_ROOT: */
	if (unlikely(IS_ROOT(dentry)))		b = &dentry->d_sb->s_roots;
	else		b = d_hash(dentry->d_name.hash);

	hlist_bl_lock(b);
	__hlist_bl_del(&dentry->d_hash);
	hlist_bl_unlock(b);
}

void __d_drop(struct dentry *dentry)
{
	if (!d_unhashed(dentry)) 
	{
		___d_drop(dentry);
		dentry->d_hash.pprev = NULL;
		write_seqcount_invalidate(&dentry->d_seq);
	}
}
//EXPORT_SYMBOL(__d_drop);

/** d_drop - drop a dentry
 * @dentry: dentry to drop
 *
 * d_drop() unhashes the entry from the parent dentry hashes, so that it won't be found through a VFS lookup any more. Note that this is different from deleting the dentry - d_delete will try to mark the dentry negative if possible, giving a successful _negative_ lookup, while d_drop will just make the cache lookup fail.
 *
 * d_drop() is used mainly for stuff that wants to invalidate a dentry for some reason (NFS timeouts or autofs deletes).
 *
 * __d_drop requires dentry->d_lock
 *
 * ___d_drop doesn't mark dentry as "unhashed"
 * (dentry->d_hash.pprev will be LIST_POISON2, not NULL). */
void d_drop(struct dentry *dentry)
{
	spin_lock(&dentry->d_lock);
	__d_drop(dentry);
	spin_unlock(&dentry->d_lock);
}
//EXPORT_SYMBOL(d_drop);


static inline void dentry_unlist(dentry *ddentry, dentry *parent)
{
	dentry *next;
	/* Inform d_walk() and shrink_dentry_list() that we are no longer attached to the dentry tree */
	ddentry->d_flags |= DCACHE_DENTRY_KILLED;
	if (unlikely(list_empty(&ddentry->d_child)))		return;
	__list_del_entry(&ddentry->d_child);
	/* Cursors can move around the list of children.  While we'd been a normal list member, it didn't matter - ->d_child.next would've been updated.  However, from now on it won't be and for the things like d_walk() it might end up with a nasty surprise. Normally d_walk() doesn't care about cursors moving around - ->d_lock on parent prevents that and since a cursor has no children of its own, we get through it without ever unlocking the parent. There is one exception, though - if we ascend from a child that gets killed as soon as we unlock it, the next sibling is found using the value left in its ->d_child.next.  And if _that_ pointed to a cursor, and cursor got moved (e.g. by lseek()) before d_walk() regains parent->d_lock, we'll end up skipping everything the cursor had been moved past.
	 * Solution: make sure that the pointer left behind in ->d_child.next points to something that won't be moving around.  I.e. skip the cursors. */
	while (ddentry->d_child.next != &parent->d_subdirs) {
		next = list_entry(ddentry->d_child.next, dentry, d_child);
		if (likely(!(next->d_flags & DCACHE_DENTRY_CURSOR)))
			break;
		ddentry->d_child.next = next->d_child.next;
	}
}


static void __dentry_kill(dentry *ddentry)
{
	dentry *parent = NULL;
	bool can_free = true;
	if (!IS_ROOT(ddentry))
		parent = ddentry->d_parent;

	/* The dentry is now unrecoverably dead to the world. */
//	lockref_mark_dead(&ddentry->d_lockref);
	ddentry->d_lockref.count = -128;

	/* inform the fs via d_prune that this dentry is about to be unhashed and destroyed. */
	if (ddentry->d_flags & DCACHE_OP_PRUNE)
		ddentry->d_op->d_prune(ddentry);

	if (ddentry->d_flags & DCACHE_LRU_LIST)
	{
		if (!(ddentry->d_flags & DCACHE_SHRINK_LIST))
			d_lru_del(ddentry);
	}
	/* if it was on the hash then remove it */
	__d_drop(ddentry);
	dentry_unlist(ddentry, parent);
	if (parent)
		spin_unlock(&parent->d_lock);
	if (ddentry->d_inode)		dentry_unlink_inode(ddentry);
	else		spin_unlock(&ddentry->d_lock);
//	this_cpu_dec(nr_dentry);
	if (ddentry->d_op && ddentry->d_op->d_release)
		ddentry->d_op->d_release(ddentry);

	spin_lock(&ddentry->d_lock);
	if (ddentry->d_flags & DCACHE_SHRINK_LIST) 
	{
		ddentry->d_flags |= DCACHE_MAY_FREE;
		can_free = false;
	}
	spin_unlock(&ddentry->d_lock);
	if (likely(can_free))
		dentry_free(ddentry);
//	cond_resched();
}



static dentry *__lock_parent(dentry *ddentry)
{
	dentry *parent;
//	rcu_read_lock();
	spin_unlock(&ddentry->d_lock);
again:
	parent = READ_ONCE(ddentry->d_parent);
	spin_lock(&parent->d_lock);
	/* We can't blindly lock dentry until we are sure that we won't violate the locking order. Any changes of dentry->d_parent must have been done with parent->d_lock held, so spin_lock() above is enough of a barrier for checking if it's still our child. */
	if (unlikely(parent != ddentry->d_parent)) 
	{
		spin_unlock(&parent->d_lock);
		goto again;
	}
//	rcu_read_unlock();
	if (parent != ddentry)		spin_lock_nested(&ddentry->d_lock, DENTRY_D_LOCK_NESTED);
	else		parent = NULL;
	return parent;
}

static inline struct dentry *lock_parent(struct dentry *dentry)
{
	struct dentry *parent = dentry->d_parent;
	if (IS_ROOT(dentry))
		return NULL;
	if (likely(spin_trylock(&parent->d_lock)))
		return parent;
	return __lock_parent(dentry);
}

static inline bool retain_dentry(dentry *ddentry)
{
	WARN_ON(d_in_lookup(ddentry));

	/* Unreachable? Get rid of it */
	if (unlikely(d_unhashed(ddentry)))		return false;

	if (unlikely(ddentry->d_flags & DCACHE_DISCONNECTED))
		return false;

	if (unlikely(ddentry->d_flags & DCACHE_OP_DELETE)) 
	{
		if (ddentry->d_op->d_delete(ddentry))
			return false;
	}

	if (unlikely(ddentry->d_flags & DCACHE_DONTCACHE))
		return false;

	/* retain; LRU fodder */
	ddentry->d_lockref.count--;
	if (unlikely(!(ddentry->d_flags & DCACHE_LRU_LIST)))		d_lru_add(ddentry);
	else if (unlikely(!(ddentry->d_flags & DCACHE_REFERENCED)))
		ddentry->d_flags |= DCACHE_REFERENCED;
	return true;
}

#if 0 //<TODO>

void d_mark_dontcache(struct inode *inode)
{
	struct dentry *de;

	spin_lock(&inode->i_lock);
	hlist_for_each_entry(de, &inode->i_dentry, d_u.d_alias) {
		spin_lock(&de->d_lock);
		de->d_flags |= DCACHE_DONTCACHE;
		spin_unlock(&de->d_lock);
	}
	inode->i_state |= I_DONTCACHE;
	spin_unlock(&inode->i_lock);
}
EXPORT_SYMBOL(d_mark_dontcache);

#endif //<TODO>

/*Finish off a dentry we've decided to kill. dentry->d_lock must be held, returns with it unlocked. Returns dentry requiring refcount drop, or NULL if we're done. */
static struct dentry *dentry_kill(struct dentry *dentry)
//	__releases(dentry->d_lock)
{
	inode *iinode = dentry->d_inode;
	struct dentry *parent = NULL;

	//if (iinode && unlikely(!spin_trylock(&iinode->i_lock)))
	if (iinode && unlikely(!iinode->trylock()))
			goto slow_positive;

	if (!IS_ROOT(dentry)) 
	{
		parent = dentry->d_parent;
		if (unlikely(!spin_trylock(&parent->d_lock))) 
		{
			parent = __lock_parent(dentry);
			if (likely(iinode || !dentry->d_inode))				goto got_locks;
			/* negative that became positive */
			if (parent) spin_unlock(&parent->d_lock);
			iinode = dentry->d_inode;
			goto slow_positive;
		}
	}
	__dentry_kill(dentry);
	return parent;

slow_positive:
	spin_unlock(&dentry->d_lock);
//	spin_lock(&iinode->i_lock);
//	iinode->lock();
	LOCK_INODE(iinode);
	spin_lock(&dentry->d_lock);
	parent = lock_parent(dentry);

got_locks:
	if (unlikely(dentry->d_lockref.count != 1)) 
	{
		dentry->d_lockref.count--;
	}
	else if (likely(!retain_dentry(dentry))) 
	{
		__dentry_kill(dentry);
		return parent;
	}
	/* we are keeping it, after all */
	if (iinode) iinode->unlock(); //		spin_unlock(&iinode->i_lock);
	if (parent)
		spin_unlock(&parent->d_lock);
	spin_unlock(&dentry->d_lock);
	return NULL;
}


/* Try to do a lockless dput(), and return whether that was successful.
 * If unsuccessful, we return false, having already taken the dentry lock.
 * The caller needs to hold the RCU read lock, so that the dentry is guaranteed to stay around even if the refcount goes down to zero! */
static inline bool fast_dput(struct dentry *dentry)
{
	int ret;
	unsigned int d_flags;

	/* If we have a d_op->d_delete() operation, we sould not let the dentry count go to zero, so use "put_or_lock". */
	if (unlikely(dentry->d_flags & DCACHE_OP_DELETE))
		return lockref_put_or_lock(&dentry->d_lockref);

	/* .. otherwise, we can try to just decrement the lockref optimistically.*/
	ret = lockref_put_return(&dentry->d_lockref);

	/* If the lockref_put_return() failed due to the lock being held by somebody else, the fast path has failed. We will need to get the lock, and then check the count again. */
	if (unlikely(ret < 0)) 
	{
		spin_lock(&dentry->d_lock);
		if (dentry->d_lockref.count > 1) 
		{
			dentry->d_lockref.count--;
			spin_unlock(&dentry->d_lock);
			return true;
		}
		return false;
	}

	/* If we weren't the last ref, we're done. */
	if (ret)	return true;

	/* Careful, careful. The reference count went down to zero, but we don't hold the dentry lock, so somebody else could get it again, and do another dput(), and we need to not race with that.
	 *
	 * However, there is a very special and common case where we don't care, because there is nothing to do: the dentry is still hashed, it does not have a 'delete' op, and it's referenced and already on the LRU list.
	 *
	 * NOTE! Since we aren't locked, these values are not "stable". However, it is sufficient that at some point after we dropped the reference the dentry was hashed and the flags had the proper value. Other dentry users may have re-gotten a reference to the dentry and change that, but our work is done - we can leave the dentry around with a zero refcount.
	 *
	 * Nevertheless, there are two cases that we should kill the dentry anyway.
	 * 1. free disconnected dentries as soon as their refcount reached zero.
	 * 2. free dentries if they should not be cached.	 */
//	smp_rmb();
	d_flags = READ_ONCE(dentry->d_flags);
	d_flags &= (DCACHE_REFERENCED | DCACHE_LRU_LIST | DCACHE_DISCONNECTED | DCACHE_DONTCACHE);

	/* Nothing to do? Dropping the reference was all we needed? */
	if (d_flags == (DCACHE_REFERENCED | DCACHE_LRU_LIST) && !d_unhashed(dentry))
		return true;

	/* Not the fast normal case? Get the lock. We've already decremented the refcount, but we'll need to re-check the situation after getting the lock.*/
	spin_lock(&dentry->d_lock);

	/* Did somebody else grab a reference to it in the meantime, and we're no longer the last user after all? Alternatively, somebody else could have killed it and marked it dead. Either way, we don't need to do anything else. */
	if (dentry->d_lockref.count) 
	{
		spin_unlock(&dentry->d_lock);
		return true;
	}

	/* Re-get the reference we optimistically dropped. We hold the lock, and we just tested that it was zero, so we can just set it to 1. */
	dentry->d_lockref.count = 1;
	return false;
}

/* This is dput
 * This is complicated by the fact that we do not want to put dentries that are no longer on any hash chain on the unused list: we'd much rather just get rid of them immediately.
 * However, that implies that we have to traverse the dentry tree upwards to the parents which might _also_ now be scheduled for deletion (it may have been only waiting for its last child to go away).
 * This tail recursion is done by hand as we don't want to depend on the compiler to always get this right (gcc generally doesn't). Real recursion would eat up our stack space. */

/* dput - release a dentry
 * @dentry: dentry to release 
 *
 * Release a dentry. This will drop the usage count and if appropriate call the dentry unlink method as well as removing it from the queues and releasing its resources. If the parent dentries were scheduled for release they too may now get deleted. */
void dput(dentry *ddentry)
{
	while (ddentry) 
	{ 
//		might_sleep();
//		rcu_read_lock();
		if (likely(fast_dput(ddentry))) 
		{
//			rcu_read_unlock();
			return;
		}

		/* Slow case: now with the dentry lock held */
//		rcu_read_unlock();

		if (likely(retain_dentry(ddentry))) 
		{
			spin_unlock(&ddentry->d_lock);
			return;
		}
		ddentry = dentry_kill(ddentry);
	}
}
//EXPORT_SYMBOL(dput);


static void __dput_to_list(dentry *ddentry, std::list<dentry*> & list)
{
	if (ddentry->d_flags & DCACHE_SHRINK_LIST) 
	{	/* let the owner of the list it's on deal with it */
		--ddentry->d_lockref.count;
	} 
	else
	{
		if (ddentry->d_flags & DCACHE_LRU_LIST)		d_lru_del(ddentry);
		if (!--ddentry->d_lockref.count) list.push_back(ddentry);
	}
}
#if 0 //TODO

void dput_to_list(struct dentry *dentry, struct list_head *list)
{
	rcu_read_lock();
	if (likely(fast_dput(dentry))) {
		rcu_read_unlock();
		return;
	}
	rcu_read_unlock();
	if (!retain_dentry(dentry))
		__dput_to_list(dentry, list);
	spin_unlock(&dentry->d_lock);
}
#endif

/* This must be called with d_lock held */
static inline void __dget_dlock(struct dentry *dentry)
{
	dentry->d_lockref.count++;
}


static inline void __dget(struct dentry *dentry)
{
	lockref_get(&dentry->d_lockref);
}

#if 0 //TODO
struct dentry *dget_parent(struct dentry *dentry)
{
	int gotref;
	struct dentry *ret;
	unsigned seq;

	/*
	 * Do optimistic parent lookup without any
	 * locking.
	 */
	rcu_read_lock();
	seq = raw_seqcount_begin(&dentry->d_seq);
	ret = READ_ONCE(dentry->d_parent);
	gotref = lockref_get_not_zero(&ret->d_lockref);
	rcu_read_unlock();
	if (likely(gotref)) {
		if (!read_seqcount_retry(&dentry->d_seq, seq))
			return ret;
		dput(ret);
	}

repeat:
	/*
	 * Don't need rcu_dereference because we re-check it was correct under
	 * the lock.
	 */
	rcu_read_lock();
	ret = dentry->d_parent;
	spin_lock(&ret->d_lock);
	if (unlikely(ret != dentry->d_parent)) {
		spin_unlock(&ret->d_lock);
		rcu_read_unlock();
		goto repeat;
	}
	rcu_read_unlock();
	BUG_ON(!ret->d_lockref.count);
	ret->d_lockref.count++;
	spin_unlock(&ret->d_lock);
	return ret;
}
EXPORT_SYMBOL(dget_parent);

#endif

dentry * __d_find_any_alias(inode *inode)
{
	dentry *alias;

	if (hlist_empty(&inode->i_dentry)) 	return NULL;
	alias = hlist_entry(inode->i_dentry.first, dentry, d_u.d_alias);
	__dget(alias);
	return alias;
}

#if 0 // TODO

/**
 * d_find_any_alias - find any alias for a given inode
 * @inode: inode to find an alias for
 *
 * If any aliases exist for the given inode, take and return a
 * reference for one of them.  If no aliases exist, return %NULL.
 */
struct dentry *d_find_any_alias(struct inode *inode)
{
	struct dentry *de;

	spin_lock(&inode->i_lock);
	de = __d_find_any_alias(inode);
	spin_unlock(&inode->i_lock);
	return de;
}
EXPORT_SYMBOL(d_find_any_alias);
#endif //<TODO>

static struct dentry *__d_find_alias(struct inode *inode)
{
	struct dentry *alias;

	if (S_ISDIR(inode->i_mode))
		return __d_find_any_alias(inode);

	hlist_for_each_entry(dentry, alias, &inode->i_dentry, d_u.d_alias) 
	{
		spin_lock(&alias->d_lock);
 		if (!d_unhashed(alias)) 
		{
			__dget_dlock(alias);
			spin_unlock(&alias->d_lock);
			return alias;
		}
		spin_unlock(&alias->d_lock);
	}
	return NULL;
}

/**
 * d_find_alias - grab a hashed alias of inode
 * @inode: inode in question
 *
 * If inode has a hashed alias, or is a directory and has any alias, acquire the reference to alias and return it. Otherwise return NULL. Notice that if inode is a directory there can be only one alias and it can be unhashed only if it has no children, or if it is the root of a filesystem, or if the directory was renamed and d_revalidate was the first vfs operation to notice.
 * If the inode has an IS_ROOT, DCACHE_DISCONNECTED alias, then prefer any other hashed alias over that one. */
struct dentry *d_find_alias(inode *iinode)
{
	struct dentry *de = NULL;

	if (!hlist_empty(&iinode->i_dentry)) 
	{
//		spin_lock(&iinode->i_lock);
//		iinode->lock();
		LOCK_INODE(iinode);
		de = __d_find_alias(iinode);
//		spin_unlock(&iinode->i_lock);
		iinode->unlock();
	}
	return de;
}
//EXPORT_SYMBOL(d_find_alias);
#if 0
/*
 *  Caller MUST be holding rcu_read_lock() and be guaranteed
 *  that inode won't get freed until rcu_read_unlock().
 */
struct dentry *d_find_alias_rcu(struct inode *inode)
{
	struct hlist_head *l = &inode->i_dentry;
	struct dentry *de = NULL;

	spin_lock(&inode->i_lock);
	// ->i_dentry and ->i_rcu are colocated, but the latter won't be
	// used without having I_FREEING set, which means no aliases left
	if (likely(!(inode->i_state & I_FREEING) && !hlist_empty(l))) {
		if (S_ISDIR(inode->i_mode)) {
			de = hlist_entry(l->first, struct dentry, d_u.d_alias);
		} else {
			hlist_for_each_entry(de, l, d_u.d_alias)
				if (!d_unhashed(de))
					break;
		}
	}
	spin_unlock(&inode->i_lock);
	return de;
}

/*
 *	Try to kill dentries associated with this inode.
 * WARNING: you must own a reference to inode.
 */
void d_prune_aliases(struct inode *inode)
{
	struct dentry *dentry;
restart:
	spin_lock(&inode->i_lock);
	hlist_for_each_entry(dentry, &inode->i_dentry, d_u.d_alias) {
		spin_lock(&dentry->d_lock);
		if (!dentry->d_lockref.count) {
			struct dentry *parent = lock_parent(dentry);
			if (likely(!dentry->d_lockref.count)) {
				__dentry_kill(dentry);
				dput(parent);
				goto restart;
			}
			if (parent)
				spin_unlock(&parent->d_lock);
		}
		spin_unlock(&dentry->d_lock);
	}
	spin_unlock(&inode->i_lock);
}
EXPORT_SYMBOL(d_prune_aliases);
#endif //<TODO>

/*
 * Lock a dentry from shrink list.
 * Called under rcu_read_lock() and dentry->d_lock; the former guarantees that nothing we access will be freed under us.
 * Note that dentry is *not* protected from concurrent dentry_kill(), d_delete(), etc.
 *
 * Return false if dentry has been disrupted or grabbed, leaving the caller to kick it off-list.  Otherwise, return true and have that dentry's inode and parent both locked. */
static bool shrink_lock_dentry(struct dentry *dentry)
{
	inode *iinode;
	struct dentry *parent;

	if (dentry->d_lockref.count)
		return false;

	iinode = dentry->d_inode;
	if (iinode && unlikely(!iinode->trylock()))
	{
		spin_unlock(&dentry->d_lock);
//		spin_lock(&iinode->i_lock);
//		iinode->lock();
		LOCK_INODE(iinode);

		spin_lock(&dentry->d_lock);
		if (unlikely(dentry->d_lockref.count))
			goto out;
		/* changed inode means that somebody had grabbed it */
		if (unlikely(iinode != dentry->d_inode))
			goto out;
	}

	parent = dentry->d_parent;
	if (IS_ROOT(dentry) || likely(spin_trylock(&parent->d_lock)))
		return true;

	spin_unlock(&dentry->d_lock);
	spin_lock(&parent->d_lock);
	if (unlikely(parent != dentry->d_parent)) {
		spin_unlock(&parent->d_lock);
		spin_lock(&dentry->d_lock);
		goto out;
	}
	spin_lock_nested(&dentry->d_lock, DENTRY_D_LOCK_NESTED);
	if (likely(!dentry->d_lockref.count))
		return true;
	spin_unlock(&parent->d_lock);
out:
	if (iinode) iinode->unlock();//		spin_unlock(&iinode->i_lock);
	return false;
}


void shrink_dentry_list(/*struct list_head *list*/std::list<dentry*> & list)
{
//	while (!list_empty(list)) {
	while (!list.empty())
	{
//		ddentry = list_entry(list->prev, struct dentry, d_lru);
		dentry * ddentry = list.front();
		spin_lock(&ddentry->d_lock);
//		rcu_read_lock();
		if (!shrink_lock_dentry(ddentry)) 
		{
			bool can_free = false;
//			rcu_read_unlock();
//			d_shrink_del(ddentry);
			list.remove(ddentry);
			if (ddentry->d_lockref.count < 0)		can_free = ddentry->d_flags & DCACHE_MAY_FREE;
			spin_unlock(&ddentry->d_lock);
			if (can_free)		dentry_free(ddentry);
			continue;
		}
//		rcu_read_unlock();
//		d_shrink_del(ddentry);
		list.remove(ddentry);
		dentry * parent = ddentry->d_parent;
		if (parent != ddentry)			__dput_to_list(parent, list);
		__dentry_kill(ddentry);
	}
}

#if 0 //<TODO>

static enum lru_status dentry_lru_isolate(struct list_head *item,
		struct list_lru_one *lru, spinlock_t *lru_lock, void *arg)
{
	struct list_head *freeable = arg;
	struct dentry	*dentry = container_of(item, struct dentry, d_lru);


	/*
	 * we are inverting the lru lock/dentry->d_lock here,
	 * so use a trylock. If we fail to get the lock, just skip
	 * it
	 */
	if (!spin_trylock(&dentry->d_lock))
		return LRU_SKIP;

	/*
	 * Referenced dentries are still in use. If they have active
	 * counts, just remove them from the LRU. Otherwise give them
	 * another pass through the LRU.
	 */
	if (dentry->d_lockref.count) {
		d_lru_isolate(lru, dentry);
		spin_unlock(&dentry->d_lock);
		return LRU_REMOVED;
	}

	if (dentry->d_flags & DCACHE_REFERENCED) {
		dentry->d_flags &= ~DCACHE_REFERENCED;
		spin_unlock(&dentry->d_lock);

		/*
		 * The list move itself will be made by the common LRU code. At
		 * this point, we've dropped the dentry->d_lock but keep the
		 * lru lock. This is safe to do, since every list movement is
		 * protected by the lru lock even if both locks are held.
		 *
		 * This is guaranteed by the fact that all LRU management
		 * functions are intermediated by the LRU API calls like
		 * list_lru_add and list_lru_del. List movement in this file
		 * only ever occur through this functions or through callbacks
		 * like this one, that are called from the LRU API.
		 *
		 * The only exceptions to this are functions like
		 * shrink_dentry_list, and code that first checks for the
		 * DCACHE_SHRINK_LIST flag.  Those are guaranteed to be
		 * operating only with stack provided lists after they are
		 * properly isolated from the main list.  It is thus, always a
		 * local access.
		 */
		return LRU_ROTATE;
	}

	d_lru_shrink_move(lru, dentry, freeable);
	spin_unlock(&dentry->d_lock);

	return LRU_REMOVED;
}

/**
 * prune_dcache_sb - shrink the dcache
 * @sb: superblock
 * @sc: shrink control, passed to list_lru_shrink_walk()
 *
 * Attempt to shrink the superblock dcache LRU by @sc->nr_to_scan entries. This
 * is done when we need more memory and called from the superblock shrinker
 * function.
 *
 * This function may fail to free any resources if all the dentries are in
 * use.
 */
long prune_dcache_sb(struct super_block *sb, struct shrink_control *sc)
{
	LIST_HEAD(dispose);
	long freed;

	freed = list_lru_shrink_walk(&sb->s_dentry_lru, sc,
				     dentry_lru_isolate, &dispose);
	shrink_dentry_list(&dispose);
	return freed;
}

static enum lru_status dentry_lru_isolate_shrink(struct list_head *item,
		struct list_lru_one *lru, spinlock_t *lru_lock, void *arg)
{
	struct list_head *freeable = arg;
	struct dentry	*dentry = container_of(item, struct dentry, d_lru);

	/*
	 * we are inverting the lru lock/dentry->d_lock here,
	 * so use a trylock. If we fail to get the lock, just skip
	 * it
	 */
	if (!spin_trylock(&dentry->d_lock))
		return LRU_SKIP;

	d_lru_shrink_move(lru, dentry, freeable);
	spin_unlock(&dentry->d_lock);

	return LRU_REMOVED;
}

#endif //<TODO>

/* shrink_dcache_sb - shrink dcache for a superblock
 * @sb: superblock
 *
 * Shrink the dcache for the specified super block. This is used to free the dcache before unmounting a file system. */
void shrink_dcache_sb(super_block *sb)
{
#if 0
	do 
	{
		LIST_HEAD(dispose);
		list_lru_walk(&sb->s_dentry_lru, dentry_lru_isolate_shrink, &dispose, 1024);
		shrink_dentry_list(&dispose);
	} while (list_lru_count(&sb->s_dentry_lru) > 0);
#else
	JCASSERT(0);
#endif
}
//EXPORT_SYMBOL(shrink_dcache_sb);

/**
 * enum d_walk_ret - action to talke during tree walk
 * @D_WALK_CONTINUE:	contrinue walk
 * @D_WALK_QUIT:	quit walk
 * @D_WALK_NORETRY:	quit when retry is needed
 * @D_WALK_SKIP:	skip this dentry and its children
 */
enum d_walk_ret {
	D_WALK_CONTINUE,
	D_WALK_QUIT,
	D_WALK_NORETRY,
	D_WALK_SKIP,
};


/**
 * d_walk - walk the dentry tree
 * @parent:	start of walk
 * @data:	data passed to @enter() and @finish()
 * @enter:	callback when first entering the dentry
 *
 * The @enter() callbacks are called with d_lock held. */
static void d_walk(dentry *parent, void *data, enum d_walk_ret (*enter)(void *, struct dentry *))
{
	dentry *this_parent;
	struct list_head *next;
	unsigned seq = 0;
	enum d_walk_ret ret;
	bool retry = true;

again:
//	read_seqbegin_or_lock(&rename_lock, &seq);
	this_parent = parent;
	spin_lock(&this_parent->d_lock);

	ret = enter(data, this_parent);
	switch (ret) {
	case D_WALK_CONTINUE:
		break;
	case D_WALK_QUIT:
	case D_WALK_SKIP:
		goto out_unlock;
	case D_WALK_NORETRY:
		retry = false;
		break;
	}
repeat:
	next = this_parent->d_subdirs.next;
resume:
	while (next != &this_parent->d_subdirs) 
	{
		list_head *tmp = next;
		dentry *ddentry = list_entry(tmp, dentry, d_child);
		next = tmp->next;

		if (unlikely(ddentry->d_flags & DCACHE_DENTRY_CURSOR))		continue;

		spin_lock_nested(&ddentry->d_lock, DENTRY_D_LOCK_NESTED);

		ret = enter(data, ddentry);
		switch (ret)
		{
		case D_WALK_CONTINUE:
			break;
		case D_WALK_QUIT:
			spin_unlock(&ddentry->d_lock);
			goto out_unlock;
		case D_WALK_NORETRY:
			retry = false;
			break;
		case D_WALK_SKIP:
			spin_unlock(&ddentry->d_lock);
			continue;
		}

		if (!list_empty(&ddentry->d_subdirs)) 
		{
			spin_unlock(&this_parent->d_lock);
//			spin_release(&dentry->d_lock.dep_map, _RET_IP_);
			this_parent = ddentry;
//			spin_acquire(&this_parent->d_lock.dep_map, 0, 1, _RET_IP_);
			goto repeat;
		}
		spin_unlock(&ddentry->d_lock);
	}
	/* All done at this level ... ascend and resume the search.	 */
//	rcu_read_lock();
ascend:
	if (this_parent != parent) {
		struct dentry *child = this_parent;
		this_parent = child->d_parent;

		spin_unlock(&child->d_lock);
		spin_lock(&this_parent->d_lock);

		/* might go back up the wrong parent if we have had a rename. */
//		if (need_seqretry(&rename_lock, seq)) goto rename_retry;
		/* go into the first sibling still alive */
		do 
		{
			next = child->d_child.next;
			if (next == &this_parent->d_subdirs)
				goto ascend;
			child = list_entry(next, struct dentry, d_child);
		} while (unlikely(child->d_flags & DCACHE_DENTRY_KILLED));
//		rcu_read_unlock();
		goto resume;
	}
//	if (need_seqretry(&rename_lock, seq))
//		goto rename_retry;
//	rcu_read_unlock();

out_unlock:
	spin_unlock(&this_parent->d_lock);
//	done_seqretry(&rename_lock, seq);
	return;

//rename_retry:
	spin_unlock(&this_parent->d_lock);
//	rcu_read_unlock();
	BUG_ON(seq & 1);
	if (!retry)
		return;
	seq = 1;
	goto again;
}

#if 0 //<TODO>

struct check_mount {
	struct vfsmount *mnt;
	unsigned int mounted;
};

static enum d_walk_ret path_check_mount(void *data, struct dentry *dentry)
{
	struct check_mount *info = data;
	struct path path = { .mnt = info->mnt, .dentry = dentry };

	if (likely(!d_mountpoint(dentry)))
		return D_WALK_CONTINUE;
	if (__path_is_mountpoint(&path)) {
		info->mounted = 1;
		return D_WALK_QUIT;
	}
	return D_WALK_CONTINUE;
}

/**
 * path_has_submounts - check for mounts over a dentry in the
 *                      current namespace.
 * @parent: path to check.
 *
 * Return true if the parent or its subdirectories contain
 * a mount point in the current namespace.
 */
int path_has_submounts(const struct path *parent)
{
	struct check_mount data = { .mnt = parent->mnt, .mounted = 0 };

	read_seqlock_excl(&mount_lock);
	d_walk(parent->dentry, &data, path_check_mount);
	read_sequnlock_excl(&mount_lock);

	return data.mounted;
}
EXPORT_SYMBOL(path_has_submounts);

/*
 * Called by mount code to set a mountpoint and check if the mountpoint is
 * reachable (e.g. NFS can unhash a directory dentry and then the complete
 * subtree can become unreachable).
 *
 * Only one of d_invalidate() and d_set_mounted() must succeed.  For
 * this reason take rename_lock and d_lock on dentry and ancestors.
 */
int d_set_mounted(struct dentry *dentry)
{
	struct dentry *p;
	int ret = -ENOENT;
	write_seqlock(&rename_lock);
	for (p = dentry->d_parent; !IS_ROOT(p); p = p->d_parent) {
		/* Need exclusion wrt. d_invalidate() */
		spin_lock(&p->d_lock);
		if (unlikely(d_unhashed(p))) {
			spin_unlock(&p->d_lock);
			goto out;
		}
		spin_unlock(&p->d_lock);
	}
	spin_lock(&dentry->d_lock);
	if (!d_unlinked(dentry)) {
		ret = -EBUSY;
		if (!d_mountpoint(dentry)) {
			dentry->d_flags |= DCACHE_MOUNTED;
			ret = 0;
		}
	}
 	spin_unlock(&dentry->d_lock);
out:
	write_sequnlock(&rename_lock);
	return ret;
}

#endif //<TODO>

/* Search the dentry child list of the specified parent, and move any unused dentries to the end of the unused list for prune_dcache(). We descend to the next level whenever the d_subdirs list is non-empty and continue searching.
 *
 * It returns zero iff there are no unused children, otherwise  it returns the number of children moved to the end of the unused list. This may not be the total number of unused children, because select_parent can drop the lock and return early due to latency constraints. */

struct select_data 
{
	dentry *start;
	union 
	{
		long found;
		dentry *victim;
	};
//	struct list_head dispose;
	std::list<dentry*> dispose;
};


static enum d_walk_ret select_collect(void *_data, struct dentry *dentry)
{
	select_data *data = (select_data*)_data;
	enum d_walk_ret ret = D_WALK_CONTINUE;

	if (data->start == dentry) goto out;

	if (dentry->d_flags & DCACHE_SHRINK_LIST) 
	{
		data->found++;
	}
	else
	{
		if (dentry->d_flags & DCACHE_LRU_LIST) d_lru_del(dentry);
		if (!dentry->d_lockref.count) 
		{
//			d_shrink_add(dentry, &data->dispose);
			data->dispose.push_back(dentry);
			data->found++;
		}
	}
	/* We can return to the caller if we have found some (this ensures forward progress). We'll be coming back to find the rest.	 */
//	if (!list_empty(&data->dispose))
	if (!data->dispose.empty()) ret = D_WALK_NORETRY;
//		ret = need_resched() ? D_WALK_QUIT : D_WALK_NORETRY;
out:
	return ret;
}

static enum d_walk_ret select_collect2(void *_data, struct dentry *dentry)
{
	select_data *data = (select_data*)_data;
	enum d_walk_ret ret = D_WALK_CONTINUE;

	if (data->start == dentry)
		goto out;

	if (dentry->d_flags & DCACHE_SHRINK_LIST) 
	{
		if (!dentry->d_lockref.count) 
		{
//			rcu_read_lock();
			data->victim = dentry;
			return D_WALK_QUIT;
		}
	} else {
		if (dentry->d_flags & DCACHE_LRU_LIST)		d_lru_del(dentry);
		if (!dentry->d_lockref.count)
			//			d_shrink_add(dentry, &data->dispose);
			data->dispose.push_back(dentry);
	}
	/* We can return to the caller if we have found some (this ensures forward progress). We'll be coming back to find the rest. */
	//if (!list_empty(&data->dispose))
	//	ret = need_resched() ? D_WALK_QUIT : D_WALK_NORETRY;
	if (!data->dispose.empty()) ret = D_WALK_NORETRY;
out:
	return ret;
}


/* shrink_dcache_parent - prune dcache
 * @parent: parent of entries to prune
 *
 * Prune the dcache to remove unused children of the parent dentry. */
void shrink_dcache_parent(dentry *parent)
{
	for (;;) 
	{
		select_data data;
		data.start = parent;

//		INIT_LIST_HEAD(&data.dispose);
		d_walk(parent, &data, select_collect);

//		if (!list_empty(&data.dispose)) 
		if (!data.dispose.empty())
		{
			shrink_dentry_list(data.dispose);
			continue;
		}

//		cond_resched();
		if (!data.found)		break;
		data.victim = NULL;
		d_walk(parent, &data, select_collect2);
		if (data.victim)
		{
			struct dentry *parent;
			spin_lock(&data.victim->d_lock);
			if (!shrink_lock_dentry(data.victim)) 
			{
				spin_unlock(&data.victim->d_lock);
//				rcu_read_unlock();
			}
			else
			{
//				rcu_read_unlock();
				parent = data.victim->d_parent;
				if (parent != data.victim) __dput_to_list(parent, data.dispose);
				__dentry_kill(data.victim);
			}
		}
		//if (!list_empty(&data.dispose))
		//	shrink_dentry_list(&data.dispose);
		if (!data.dispose.empty()) shrink_dentry_list(data.dispose);
	}
}
//EXPORT_SYMBOL(shrink_dcache_parent);

static enum d_walk_ret umount_check(void *_data, dentry *ddentry)
{
	/* it has busy descendents; complain about those instead */
	if (!list_empty(&ddentry->d_subdirs))	return D_WALK_CONTINUE;

	/* root with refcount 1 is fine */
	if (ddentry == _data && ddentry->d_lockref.count == 1)	return D_WALK_CONTINUE;

	LOG_ERROR(L"[Warning] Dentry %p{i=%lx} name=%S, still in use (%d)",
		ddentry, ddentry->d_inode ? ddentry->d_inode->i_ino : 0UL,
		ddentry->d_name.name.c_str(), ddentry->d_lockref.count);
	//LOG_ERROR(L"[Warning] Dentry %p{i=%lx,n=%pd} still in use (%d) [unmount of %s %s]\n",
	//	       ddentry,
	//	       ddentry->d_inode ? ddentry->d_inode->i_ino : 0UL,
	//	       ddentry,
	//	       ddentry->d_lockref.count,
//		       ddentry->d_sb->s_type->name,
//		       ddentry->d_sb->s_id
//				);
//	WARN_ON(1);
	return D_WALK_CONTINUE;
}

static void do_one_tree(dentry *ddentry)
{
	shrink_dcache_parent(ddentry);
	d_walk(ddentry, ddentry, umount_check);
	d_drop(ddentry);
	dput(ddentry);
}


/* destroy the dentries attached to a superblock on unmounting */
void shrink_dcache_for_umount(super_block *sb)
{
	dentry *ddentry;

//	WARN(down_read_trylock(&sb->s_umount), "s_umount should've been locked");
	down_read_trylock(&sb->s_umount);
	ddentry = sb->s_root;
	sb->s_root = NULL;
	do_one_tree(ddentry);

	while (!hlist_bl_empty(&sb->s_roots)) 
	{
		ddentry = dget(hlist_bl_entry(hlist_bl_first(&sb->s_roots), dentry, d_hash));
		do_one_tree(ddentry);
	}
}

static enum d_walk_ret find_submount(void *_data, dentry *ddentry)
{
	dentry **victim = reinterpret_cast<dentry**>(_data);
	if (d_mountpoint(ddentry))
	{
		__dget_dlock(ddentry);
		*victim = ddentry;
		return D_WALK_QUIT;
	}
	return D_WALK_CONTINUE;
}

// 来自于mount.h
static inline void detach_mounts(dentry* ddentry)
{
	if (!d_mountpoint(ddentry))	return;
#if 0
	__detach_mounts(ddentry);
#else
	JCASSERT(0);
#endif
}

/** d_invalidate - detach submounts, prune dcache, and drop
 * @dentry: dentry to invalidate (aka detach, prune and drop) */
void d_invalidate(dentry *ddentry)
{
	bool had_submounts = false;
	spin_lock(&ddentry->d_lock);
	if (d_unhashed(ddentry)) 
	{
		spin_unlock(&ddentry->d_lock);
		return;
	}
	__d_drop(ddentry);
	spin_unlock(&ddentry->d_lock);

	/* Negative dentries can be dropped without further checks */
	if (!ddentry->d_inode)	return;

	shrink_dcache_parent(ddentry);
	for (;;) 
	{
		dentry *victim = NULL;
		d_walk(ddentry, &victim, find_submount);
		if (!victim) 
		{
			if (had_submounts)		shrink_dcache_parent(ddentry);
			return;
		}
		had_submounts = true;
		detach_mounts(victim);
		dput(victim);
	}
}
//EXPORT_SYMBOL(d_invalidate);

/* __d_alloc	-	allocate a dcache entry
 * @sb: filesystem it will belong to
 * @name: qstr of the name
 *
 * Allocates a dentry. It returns %NULL if there is insufficient memory available. On a success the dentry is returned. The name passed in is copied and the copy passed in may be reused after this call. */
 
//static dentry *__d_alloc(super_block *sb, const qstr *name)
//{
//	dentry *ptr_dentry;
//	char *dname;
//	int err;
//
//	//ptr_dentry = kmem_cache_alloc(dentry_cache, GFP_KERNEL);
//	ptr_dentry = new dentry;
//	if (!ptr_dentry)	return NULL;
//
//	/* We guarantee that the inline name is always NUL-terminated. This way the memcpy() done by the name switching in rename will still always have a NUL at the end, even if we might be overwriting an internal NUL character */
//	ptr_dentry->d_iname[DNAME_INLINE_LEN-1] = 0;
//	if (unlikely(!name)) 
//	{
//		name = &slash_name;
////		dname = ptr_dentry->d_iname;
//		ptr_dentry->d_name = *name;
////		ptr_dentry->d_name.name = ptr_dentry->d_iname;
//	} 
//	else if (name->len() > DNAME_INLINE_LEN-1) 
//	{
//#if 0 //<TODO> 处理长文件名
//		size_t size = offsetof(struct external_name, name[1]);
//		external_name* p = (external_name*)  malloc(size + name->len());
//		//kmalloc(size + name->len,  GFP_KERNEL_ACCOUNT |	  __GFP_RECLAIMABLE);
//		if (!p) 
//		{	//kmem_cache_free(dentry_cache, ptr_dentry); 
//			delete ptr_dentry;
//			return NULL;
//		}
//		InterlockedExchange(&p->u.count, 1);
//		//atomic_set(&p->u.count, 1);
//		dname = p->name;
//#endif
//		ptr_dentry->d_name = *name;
//	} 
//	else  {	dname = ptr_dentry->d_iname;	}	
//
//	ptr_dentry->d_name = *name;
//	//ptr_dentry->d_name.len() = name->len();
//	//ptr_dentry->d_name.hash = name->_u._s.hash;
//	//memcpy(dname, name->name, name->len());
//	//dname[name->len()] = 0;
//
//	/* Make sure we always see the terminating NUL character */
////	smp_store_release(&ptr_dentry->d_name.name, dname); /* ^^^ */
//	//ptr_dentry->d_name.name = new char[name->len() + 1];
//	//strcpy_s(ptr_dentry->d_name.name, name->len()+1, dname);
//
//	ptr_dentry->d_lockref.count = 1;
//	ptr_dentry->d_flags = 0;
//	spin_lock_init(&ptr_dentry->d_lock);
//#if 0 //<TOOD>
//	seqcount_spinlock_init(&ptr_dentry->d_seq, &ptr_dentry->d_lock);
//#endif
//	ptr_dentry->d_inode = NULL;
//	ptr_dentry->d_parent = ptr_dentry;
//	ptr_dentry->d_sb = sb;
//	ptr_dentry->d_op = NULL;
//	ptr_dentry->d_fsdata = NULL;
//	INIT_HLIST_BL_NODE(&ptr_dentry->d_hash);
////	INIT_LIST_HEAD(&ptr_dentry->d_lru);
//	INIT_LIST_HEAD(&ptr_dentry->d_subdirs);
//	INIT_HLIST_NODE(&ptr_dentry->d_u.d_alias);
//	INIT_LIST_HEAD(&ptr_dentry->d_child);
//	d_set_d_op(ptr_dentry, ptr_dentry->d_sb->s_d_op);
//
//	if (ptr_dentry->d_op && ptr_dentry->d_op->d_init) 
//	{
//		err = ptr_dentry->d_op->d_init(ptr_dentry);
//		if (err) 
//		{
////			if (dname_external(ptr_dentry))			kfree(to_external_name(ptr_dentry));
////			kmem_cache_free(dentry_cache, ptr_dentry);
//			delete ptr_dentry;
//			return NULL;
//		}
//	}
//#if 0 //<TODO>
//	this_cpu_inc(nr_dentry);
//#endif
//	return ptr_dentry;
//}


/**
 * d_alloc	-	allocate a dcache entry
 * @parent: parent of entry to allocate
 * @name: qstr of the name
 *
 * Allocates a dentry. It returns %NULL if there is insufficient memory available. On a success the dentry is returned. The name passed in is copied and the copy passed in may be reused after this call. */
dentry *d_alloc(dentry * parent, const qstr &name)
{
	dentry *entry = parent->m_manager->__d_alloc(parent->d_sb, &name);
//	dentry *entry = __d_alloc(parent->d_sb, &name);
	if (!entry) return NULL;
	spin_lock(&parent->d_lock);
	/* don't need child lock because it is not subject to concurrency here */
	__dget_dlock(parent);
	entry->d_parent = parent;
	list_add(&entry->d_child, &parent->d_subdirs);
	spin_unlock(&parent->d_lock);

	return entry;
}
//EXPORT_SYMBOL(d_alloc);

//struct dentry *d_alloc_anon(struct super_block *sb)
//{
//	return __d_alloc(sb, NULL);
//}

#if 0


struct dentry *d_alloc_cursor(struct dentry * parent)
{
	struct dentry *dentry = d_alloc_anon(parent->d_sb);
	if (dentry) {
		dentry->d_flags |= DCACHE_DENTRY_CURSOR;
		dentry->d_parent = dget(parent);
	}
	return dentry;
}

/**
 * d_alloc_pseudo - allocate a dentry (for lookup-less filesystems)
 * @sb: the superblock
 * @name: qstr of the name
 *
 * For a filesystem that just pins its dentries in memory and never
 * performs lookups at all, return an unhashed IS_ROOT dentry.
 * This is used for pipes, sockets et.al. - the stuff that should
 * never be anyone's children or parents.  Unlike all other
 * dentries, these will not have RCU delay between dropping the
 * last reference and freeing them.
 *
 * The only user is alloc_file_pseudo() and that's what should
 * be considered a public interface.  Don't use directly.
 */
struct dentry *d_alloc_pseudo(struct super_block *sb, const struct qstr *name)
{
	struct dentry *dentry = __d_alloc(sb, name);
	if (likely(dentry))
		dentry->d_flags |= DCACHE_NORCU;
	return dentry;
}

struct dentry *d_alloc_name(struct dentry *parent, const char *name)
{
	struct qstr q;

	q.name = name;
	q._u.hash_len = hashlen_string(parent, name);
	return d_alloc(parent, &q);
}
EXPORT_SYMBOL(d_alloc_name);

void d_set_d_op(struct dentry *dentry, const struct dentry_operations *op)
{
	WARN_ON_ONCE(dentry->d_op);
	WARN_ON_ONCE(dentry->d_flags & (DCACHE_OP_HASH	|
				DCACHE_OP_COMPARE	|
				DCACHE_OP_REVALIDATE	|
				DCACHE_OP_WEAK_REVALIDATE	|
				DCACHE_OP_DELETE	|
				DCACHE_OP_REAL));
	dentry->d_op = op;
	if (!op)
		return;
	if (op->d_hash)
		dentry->d_flags |= DCACHE_OP_HASH;
	if (op->d_compare)
		dentry->d_flags |= DCACHE_OP_COMPARE;
	if (op->d_revalidate)
		dentry->d_flags |= DCACHE_OP_REVALIDATE;
	if (op->d_weak_revalidate)
		dentry->d_flags |= DCACHE_OP_WEAK_REVALIDATE;
	if (op->d_delete)
		dentry->d_flags |= DCACHE_OP_DELETE;
	if (op->d_prune)
		dentry->d_flags |= DCACHE_OP_PRUNE;
	if (op->d_real)
		dentry->d_flags |= DCACHE_OP_REAL;

}
EXPORT_SYMBOL(d_set_d_op);


/*
 * d_set_fallthru - Mark a dentry as falling through to a lower layer
 * @dentry - The dentry to mark
 *
 * Mark a dentry as falling through to the lower layer (as set with
 * d_pin_lower()).  This flag may be recorded on the medium.
 */
void d_set_fallthru(struct dentry *dentry)
{
	spin_lock(&dentry->d_lock);
	dentry->d_flags |= DCACHE_FALLTHRU;
	spin_unlock(&dentry->d_lock);
}
EXPORT_SYMBOL(d_set_fallthru);
#endif

static unsigned d_flags_for_inode(struct inode *inode)
{
	unsigned add_flags = DCACHE_REGULAR_TYPE;

	if (!inode)		return DCACHE_MISS_TYPE;

	if (S_ISDIR(inode->i_mode))
	{
		add_flags = DCACHE_DIRECTORY_TYPE;
		if (unlikely(!(inode->i_opflags & IOP_LOOKUP)))
		{
			//if (unlikely(!inode->i_op->lookup))		add_flags = DCACHE_AUTODIR_TYPE;
			if (false)								add_flags = DCACHE_AUTODIR_TYPE;
			else									inode->i_opflags |= IOP_LOOKUP;
		}
		goto type_determined;
	}

	if (unlikely(!(inode->i_opflags & IOP_NOFOLLOW))) 
	{
//		if (unlikely(inode->i_op->get_link)) 
		if (false)
		{
			add_flags = DCACHE_SYMLINK_TYPE;
			goto type_determined;
		}
		inode->i_opflags |= IOP_NOFOLLOW;
	}

	if (unlikely(!S_ISREG(inode->i_mode)))		add_flags = DCACHE_SPECIAL_TYPE;

type_determined:
	if (unlikely(IS_AUTOMOUNT(inode)))		add_flags |= DCACHE_NEED_AUTOMOUNT;
	return add_flags;
}

static void __d_instantiate(dentry* dentry, inode* inode)
{
	unsigned add_flags = d_flags_for_inode(inode);
	WARN_ON(d_in_lookup(dentry));

	spin_lock(&dentry->d_lock);
	/* Decrement negative dentry count if it was in the LRU list.	 */
	if (dentry->d_flags & DCACHE_LRU_LIST)
	{
//		InterlockedDecrement(nr_dentry_negative);
//		this_cpu_dec(nr_dentry_negative);
	}
	hlist_add_head(&dentry->d_u.d_alias, &inode->i_dentry);
	raw_write_seqcount_begin(&dentry->d_seq);
	__d_set_inode_and_type(dentry, inode, add_flags);
	raw_write_seqcount_end(&dentry->d_seq);
#if 0 //TODO
	fsnotify_update_flags(dentry);
#endif
	spin_unlock(&dentry->d_lock);
}

/* d_instantiate - fill in inode information for a dentry
 * @entry: dentry to complete
 * @inode: inode to attach to this dentry
 *
 * Fill in inode information in the entry.
 * This turns negative dentries into productive full members of society.
  * NOTE! This assumes that the inode count has been incremented (or otherwise set) by the caller to indicate that it is
  now in use by the dcache. */
 
void d_instantiate(dentry *entry, inode * iinode)
{
	JCASSERT(hlist_unhashed(&entry->d_u.d_alias));
	if (iinode) 
	{
#if 0 // TODO
		security_d_instantiate(entry, iinode);
#endif
//		spin_lock(&iinode->i_lock);
//		iinode->lock();
		LOCK_INODE(iinode);
		__d_instantiate(entry, iinode);
//		spin_unlock(&iinode->i_lock);
		iinode->unlock();
	}
}

/* This should be equivalent to d_instantiate() + unlock_new_inode(), with lockdep-related part of unlock_new_inode() 
   done before anything else.  Use that instead of open-coding d_instantiate()/ unlock_new_inode() combinations. */
void d_instantiate_new(dentry *entry, inode *iinode)
{
	BUG_ON(!hlist_unhashed(&entry->d_u.d_alias));
	BUG_ON(!iinode);
	lockdep_annotate_inode_mutex_key(iinode);
#if 0 //TODO
	security_d_instantiate(entry, iinode);
#endif
//	spin_lock(&iinode->i_lock);
//	iinode->lock();
	LOCK_INODE(iinode);
	__d_instantiate(entry, iinode);
	WARN_ON(!iinode->TestState(I_NEW));
	iinode->ClearStateNotify(I_NEW | I_CREATING);
#if 0 //TODO
	smp_mb();
#endif
//	wake_up_bit(&iinode->i_state, __I_NEW);
	//spin_unlock(&iinode->i_lock);
	iinode->unlock();
}
//EXPORT_SYMBOL(d_instantiate_new);

//dentry *d_make_root(inode *root_inode)
//{
//	dentry *res = NULL;
//
//	if (root_inode) 
//	{
//		res = d_alloc_anon(root_inode->i_sb);
//		if (res)		d_instantiate(res, root_inode);
//		else			iput(root_inode);
//	}
//	return res;
//}
//EXPORT_SYMBOL(d_make_root);
#if 0

static struct dentry *__d_instantiate_anon(struct dentry *dentry,
					   struct inode *inode,
					   bool disconnected)
{
	struct dentry *res;
	unsigned add_flags;

	security_d_instantiate(dentry, inode);
	spin_lock(&inode->i_lock);
	res = __d_find_any_alias(inode);
	if (res) {
		spin_unlock(&inode->i_lock);
		dput(dentry);
		goto out_iput;
	}

	/* attach a disconnected dentry */
	add_flags = d_flags_for_inode(inode);

	if (disconnected)
		add_flags |= DCACHE_DISCONNECTED;

	spin_lock(&dentry->d_lock);
	__d_set_inode_and_type(dentry, inode, add_flags);
	hlist_add_head(&dentry->d_u.d_alias, &inode->i_dentry);
	if (!disconnected) {
		hlist_bl_lock(&dentry->d_sb->s_roots);
		hlist_bl_add_head(&dentry->d_hash, &dentry->d_sb->s_roots);
		hlist_bl_unlock(&dentry->d_sb->s_roots);
	}
	spin_unlock(&dentry->d_lock);
	spin_unlock(&inode->i_lock);

	return dentry;

 out_iput:
	iput(inode);
	return res;
}

struct dentry *d_instantiate_anon(struct dentry *dentry, struct inode *inode)
{
	return __d_instantiate_anon(dentry, inode, true);
}
EXPORT_SYMBOL(d_instantiate_anon);

static struct dentry *__d_obtain_alias(struct inode *inode, bool disconnected)
{
	struct dentry *tmp;
	struct dentry *res;

	if (!inode)
		return ERR_PTR(-ESTALE);
	if (IS_ERR(inode))
		return ERR_CAST(inode);

	res = d_find_any_alias(inode);
	if (res)
		goto out_iput;

	tmp = d_alloc_anon(inode->i_sb);
	if (!tmp) {
		res = ERR_PTR(-ENOMEM);
		goto out_iput;
	}

	return __d_instantiate_anon(tmp, inode, disconnected);

out_iput:
	iput(inode);
	return res;
}

/**
 * d_obtain_alias - find or allocate a DISCONNECTED dentry for a given inode
 * @inode: inode to allocate the dentry for
 *
 * Obtain a dentry for an inode resulting from NFS filehandle conversion or
 * similar open by handle operations.  The returned dentry may be anonymous,
 * or may have a full name (if the inode was already in the cache).
 *
 * When called on a directory inode, we must ensure that the inode only ever
 * has one dentry.  If a dentry is found, that is returned instead of
 * allocating a new one.
 *
 * On successful return, the reference to the inode has been transferred
 * to the dentry.  In case of an error the reference on the inode is released.
 * To make it easier to use in export operations a %NULL or IS_ERR inode may
 * be passed in and the error will be propagated to the return value,
 * with a %NULL @inode replaced by ERR_PTR(-ESTALE).
 */
struct dentry *d_obtain_alias(struct inode *inode)
{
	return __d_obtain_alias(inode, true);
}
EXPORT_SYMBOL(d_obtain_alias);

/**
 * d_obtain_root - find or allocate a dentry for a given inode
 * @inode: inode to allocate the dentry for
 *
 * Obtain an IS_ROOT dentry for the root of a filesystem.
 *
 * We must ensure that directory inodes only ever have one dentry.  If a
 * dentry is found, that is returned instead of allocating a new one.
 *
 * On successful return, the reference to the inode has been transferred
 * to the dentry.  In case of an error the reference on the inode is
 * released.  A %NULL or IS_ERR inode may be passed in and will be the
 * error will be propagate to the return value, with a %NULL @inode
 * replaced by ERR_PTR(-ESTALE).
 */
struct dentry *d_obtain_root(struct inode *inode)
{
	return __d_obtain_alias(inode, false);
}
EXPORT_SYMBOL(d_obtain_root);

/**
 * d_add_ci - lookup or allocate new dentry with case-exact name
 * @inode:  the inode case-insensitive lookup has found
 * @dentry: the negative dentry that was passed to the parent's lookup func
 * @name:   the case-exact name to be associated with the returned dentry
 *
 * This is to avoid filling the dcache with case-insensitive names to the
 * same inode, only the actual correct case is stored in the dcache for
 * case-insensitive filesystems.
 *
 * For a case-insensitive lookup match and if the case-exact dentry
 * already exists in the dcache, use it and return it.
 *
 * If no entry exists with the exact case name, allocate new dentry with
 * the exact case, and return the spliced entry.
 */
struct dentry *d_add_ci(struct dentry *dentry, struct inode *inode,
			struct qstr *name)
{
	struct dentry *found, *res;

	/*
	 * First check if a dentry matching the name already exists,
	 * if not go ahead and create it now.
	 */
	found = d_hash_and_lookup(dentry->d_parent, name);
	if (found) {
		iput(inode);
		return found;
	}
	if (d_in_lookup(dentry)) {
		found = d_alloc_parallel(dentry->d_parent, name,
					dentry->d_wait);
		if (IS_ERR(found) || !d_in_lookup(found)) {
			iput(inode);
			return found;
		}
	} else {
		found = d_alloc(dentry->d_parent, name);
		if (!found) {
			iput(inode);
			return ERR_PTR(-ENOMEM);
		} 
	}
	res = d_splice_alias(inode, found);
	if (res) {
		dput(found);
		return res;
	}
	return found;
}
EXPORT_SYMBOL(d_add_ci);


static inline bool d_same_name(const struct dentry *dentry,
				const struct dentry *parent,
				const struct qstr *name)
{
	if (likely(!(parent->d_flags & DCACHE_OP_COMPARE))) {
		if (dentry->d_name.len() != name->len())
			return false;
		return dentry_cmp(dentry, name->name, name->len()) == 0;
	}
	return parent->d_op->d_compare(dentry,
				       dentry->d_name.len(), dentry->d_name.name,
				       name) == 0;
}

/**
 * __d_lookup_rcu - search for a dentry (racy, store-free)
 * @parent: parent dentry
 * @name: qstr of name we wish to find
 * @seqp: returns d_seq value at the point where the dentry was found
 * Returns: dentry, or NULL
 *
 * __d_lookup_rcu is the dcache lookup function for rcu-walk name
 * resolution (store-free path walking) design described in
 * Documentation/filesystems/path-lookup.txt.
 *
 * This is not to be used outside core vfs.
 *
 * __d_lookup_rcu must only be used in rcu-walk mode, ie. with vfsmount lock
 * held, and rcu_read_lock held. The returned dentry must not be stored into
 * without taking d_lock and checking d_seq sequence count against @seq
 * returned here.
 *
 * A refcount may be taken on the found dentry with the d_rcu_to_refcount
 * function.
 *
 * Alternatively, __d_lookup_rcu may be called again to look up the child of
 * the returned dentry, so long as its parent's seqlock is checked after the
 * child is looked up. Thus, an interlocking stepping of sequence lock checks
 * is formed, giving integrity down the path walk.
 *
 * NOTE! The caller *has* to check the resulting dentry against the sequence
 * number we've returned before using any of the resulting dentry state!
 */
struct dentry *__d_lookup_rcu(const struct dentry *parent,
				const struct qstr *name,
				unsigned *seqp)
{
	u64 hashlen = name->hash_len;
	const unsigned char *str = name->name;
	struct hlist_bl_head *b = d_hash(hashlen_hash(hashlen));
	struct hlist_bl_node *node;
	struct dentry *dentry;

	/*
	 * Note: There is significant duplication with __d_lookup_rcu which is
	 * required to prevent single threaded performance regressions
	 * especially on architectures where smp_rmb (in seqcounts) are costly.
	 * Keep the two functions in sync.
	 */

	/*
	 * The hash list is protected using RCU.
	 *
	 * Carefully use d_seq when comparing a candidate dentry, to avoid
	 * races with d_move().
	 *
	 * It is possible that concurrent renames can mess up our list
	 * walk here and result in missing our dentry, resulting in the
	 * false-negative result. d_lookup() protects against concurrent
	 * renames using rename_lock seqlock.
	 *
	 * See Documentation/filesystems/path-lookup.txt for more details.
	 */
	hlist_bl_for_each_entry_rcu(dentry, node, b, d_hash) {
		unsigned seq;

seqretry:
		/*
		 * The dentry sequence count protects us from concurrent
		 * renames, and thus protects parent and name fields.
		 *
		 * The caller must perform a seqcount check in order
		 * to do anything useful with the returned dentry.
		 *
		 * NOTE! We do a "raw" seqcount_begin here. That means that
		 * we don't wait for the sequence count to stabilize if it
		 * is in the middle of a sequence change. If we do the slow
		 * dentry compare, we will do seqretries until it is stable,
		 * and if we end up with a successful lookup, we actually
		 * want to exit RCU lookup anyway.
		 *
		 * Note that raw_seqcount_begin still *does* smp_rmb(), so
		 * we are still guaranteed NUL-termination of ->d_name.name.
		 */
		seq = raw_seqcount_begin(&dentry->d_seq);
		if (dentry->d_parent != parent)
			continue;
		if (d_unhashed(dentry))
			continue;

		if (unlikely(parent->d_flags & DCACHE_OP_COMPARE)) {
			int tlen;
			const char *tname;
			if (dentry->d_name.hash != hashlen_hash(hashlen))
				continue;
			tlen = dentry->d_name.len();
			tname = dentry->d_name.name;
			/* we want a consistent (name,len) pair */
			if (read_seqcount_retry(&dentry->d_seq, seq)) {
				cpu_relax();
				goto seqretry;
			}
			if (parent->d_op->d_compare(dentry,
						    tlen, tname, name) != 0)
				continue;
		} else {
			if (dentry->d_name._u.hash_len != hashlen)
				continue;
			if (dentry_cmp(dentry, str, hashlen_len(hashlen)) != 0)
				continue;
		}
		*seqp = seq;
		return dentry;
	}
	return NULL;
}

/**
 * d_lookup - search for a dentry
 * @parent: parent dentry
 * @name: qstr of name we wish to find
 * Returns: dentry, or NULL
 *
 * d_lookup searches the children of the parent dentry for the name in
 * question. If the dentry is found its reference count is incremented and the
 * dentry is returned. The caller must use dput to free the entry when it has
 * finished using it. %NULL is returned if the dentry does not exist.
 */
struct dentry *d_lookup(const struct dentry *parent, const struct qstr *name)
{
	struct dentry *dentry;
	unsigned seq;

	do {
		seq = read_seqbegin(&rename_lock);
		dentry = __d_lookup(parent, name);
		if (dentry)
			break;
	} while (read_seqretry(&rename_lock, seq));
	return dentry;
}
EXPORT_SYMBOL(d_lookup);

/**
 * __d_lookup - search for a dentry (racy)
 * @parent: parent dentry
 * @name: qstr of name we wish to find
 * Returns: dentry, or NULL
 *
 * __d_lookup is like d_lookup, however it may (rarely) return a
 * false-negative result due to unrelated rename activity.
 *
 * __d_lookup is slightly faster by avoiding rename_lock read seqlock,
 * however it must be used carefully, eg. with a following d_lookup in
 * the case of failure.
 *
 * __d_lookup callers must be commented.
 */
struct dentry *__d_lookup(const struct dentry *parent, const struct qstr *name)
{
	unsigned int hash = name->_u._s.hash;
	struct hlist_bl_head *b = d_hash(hash);
	struct hlist_bl_node *node;
	struct dentry *found = NULL;
	struct dentry *dentry;

	/*
	 * Note: There is significant duplication with __d_lookup_rcu which is
	 * required to prevent single threaded performance regressions
	 * especially on architectures where smp_rmb (in seqcounts) are costly.
	 * Keep the two functions in sync.
	 */

	/*
	 * The hash list is protected using RCU.
	 *
	 * Take d_lock when comparing a candidate dentry, to avoid races
	 * with d_move().
	 *
	 * It is possible that concurrent renames can mess up our list
	 * walk here and result in missing our dentry, resulting in the
	 * false-negative result. d_lookup() protects against concurrent
	 * renames using rename_lock seqlock.
	 *
	 * See Documentation/filesystems/path-lookup.txt for more details.
	 */
	rcu_read_lock();
	
	hlist_bl_for_each_entry_rcu(dentry, node, b, d_hash) {

		if (dentry->d_name.hash != hash)
			continue;

		spin_lock(&dentry->d_lock);
		if (dentry->d_parent != parent)
			goto next;
		if (d_unhashed(dentry))
			goto next;

		if (!d_same_name(dentry, parent, name))
			goto next;

		dentry->d_lockref.count++;
		found = dentry;
		spin_unlock(&dentry->d_lock);
		break;
next:
		spin_unlock(&dentry->d_lock);
 	}
 	rcu_read_unlock();

 	return found;
}

/**
 * d_hash_and_lookup - hash the qstr then search for a dentry
 * @dir: Directory to search in
 * @name: qstr of name we wish to find
 *
 * On lookup failure NULL is returned; on bad name - ERR_PTR(-error)
 */
struct dentry *d_hash_and_lookup(struct dentry *dir, struct qstr *name)
{
	/*
	 * Check for a fs-specific hash function. Note that we must
	 * calculate the standard hash first, as the d_op->d_hash()
	 * routine may choose to leave the hash value unchanged.
	 */
	name->_u._s.hash = full_name_hash(dir, name->name, name->len());
	if (dir->d_flags & DCACHE_OP_HASH) {
		int err = dir->d_op->d_hash(dir, name);
		if (unlikely(err < 0))
			return ERR_PTR(err);
	}
	return d_lookup(dir, name);
}
EXPORT_SYMBOL(d_hash_and_lookup);
#endif

/*
 * When a file is deleted, we have two options:
 * - turn this dentry into a negative dentry
 * - unhash this dentry and free it.
 *
 * Usually, we want to just turn this into a negative dentry, but if anybody else is currently using the dentry or the inode we can't do that and we fall back on removing it from the hash queues and waiting for it to be deleted later when it has no users */
 
/**
 * d_delete - delete a dentry
 * @dentry: The dentry to delete
 *
 * Turn the dentry into a negative dentry if possible, otherwise remove it from the hash queues so it can be deleted later */
 
void d_delete(struct dentry * dentry)
{
	inode *iinode = dentry->d_inode;

//	spin_lock(&iinode->i_lock);
//	iinode->lock();
	LOCK_INODE(iinode);
	spin_lock(&dentry->d_lock);
	/* Are we the only user? */
	if (dentry->d_lockref.count == 1) 
	{
		dentry->d_flags &= ~DCACHE_CANT_MOUNT;
		dentry_unlink_inode(dentry);
	} 
	else 
	{
		__d_drop(dentry);
		spin_unlock(&dentry->d_lock);
		//spin_unlock(&iinode->i_lock);
		iinode->unlock();
	}
}
//EXPORT_SYMBOL(d_delete);

static void __d_rehash(dentry *entry)
{
#if 0	//不处理全局的dentry hash，<TODO>移植到f2fs中处理。
	hlist_bl_head *b = d_hash(entry->d_name.hash);

	hlist_bl_lock(b);
	hlist_bl_add_head_rcu(&entry->d_hash, b);
	hlist_bl_unlock(b);
#endif
}

#if 0 //TODO

/**
 * d_rehash	- add an entry back to the hash
 * @entry: dentry to add to the hash
 *
 * Adds a dentry to the hash according to its name.
 */
 
void d_rehash(struct dentry * entry)
{
	spin_lock(&entry->d_lock);
	__d_rehash(entry);
	spin_unlock(&entry->d_lock);
}
EXPORT_SYMBOL(d_rehash);

#endif

static inline unsigned start_dir_add(inode *dir)
{
	for (;;)
	{
		unsigned n = dir->i_dir_seq;
		if (!(n & 1) && cmpxchg(&dir->i_dir_seq, n, n + 1) == n) return n;
//		InterlockedCompareExchange(&dir->i_dir_seq, n + 1, n);
//		cpu_relax();
	}
}


static inline void end_dir_add(struct inode *dir, unsigned n)
{
//	smp_store_release(&dir->i_dir_seq, n + 2);
	InterlockedExchange(&dir->i_dir_seq, n + 2);
}
#if 0 //TODO

static void d_wait_lookup(struct dentry *dentry)
{
	if (d_in_lookup(dentry)) {
		DECLARE_WAITQUEUE(wait, current);
		add_wait_queue(dentry->d_wait, &wait);
		do {
			set_current_state(TASK_UNINTERRUPTIBLE);
			spin_unlock(&dentry->d_lock);
			schedule();
			spin_lock(&dentry->d_lock);
		} while (d_in_lookup(dentry));
	}
}

struct dentry *d_alloc_parallel(struct dentry *parent,
				const struct qstr *name,
				wait_queue_head_t *wq)
{
	unsigned int hash = name->_u._s.hash;
	struct hlist_bl_head *b = in_lookup_hash(parent, hash);
	struct hlist_bl_node *node;
	struct dentry *new = d_alloc(parent, name);
	struct dentry *dentry;
	unsigned seq, r_seq, d_seq;

	if (unlikely(!new))
		return ERR_PTR(-ENOMEM);

retry:
	rcu_read_lock();
	seq = smp_load_acquire(&parent->d_inode->i_dir_seq);
	r_seq = read_seqbegin(&rename_lock);
	dentry = __d_lookup_rcu(parent, name, &d_seq);
	if (unlikely(dentry)) {
		if (!lockref_get_not_dead(&dentry->d_lockref)) {
			rcu_read_unlock();
			goto retry;
		}
		if (read_seqcount_retry(&dentry->d_seq, d_seq)) {
			rcu_read_unlock();
			dput(dentry);
			goto retry;
		}
		rcu_read_unlock();
		dput(new);
		return dentry;
	}
	if (unlikely(read_seqretry(&rename_lock, r_seq))) {
		rcu_read_unlock();
		goto retry;
	}

	if (unlikely(seq & 1)) {
		rcu_read_unlock();
		goto retry;
	}

	hlist_bl_lock(b);
	if (unlikely(READ_ONCE(parent->d_inode->i_dir_seq) != seq)) {
		hlist_bl_unlock(b);
		rcu_read_unlock();
		goto retry;
	}
	/*
	 * No changes for the parent since the beginning of d_lookup().
	 * Since all removals from the chain happen with hlist_bl_lock(),
	 * any potential in-lookup matches are going to stay here until
	 * we unlock the chain.  All fields are stable in everything
	 * we encounter.
	 */
	hlist_bl_for_each_entry(dentry, node, b, d_u.d_in_lookup_hash) {
		if (dentry->d_name.hash != hash)
			continue;
		if (dentry->d_parent != parent)
			continue;
		if (!d_same_name(dentry, parent, name))
			continue;
		hlist_bl_unlock(b);
		/* now we can try to grab a reference */
		if (!lockref_get_not_dead(&dentry->d_lockref)) {
			rcu_read_unlock();
			goto retry;
		}

		rcu_read_unlock();
		/*
		 * somebody is likely to be still doing lookup for it;
		 * wait for them to finish
		 */
		spin_lock(&dentry->d_lock);
		d_wait_lookup(dentry);
		/*
		 * it's not in-lookup anymore; in principle we should repeat
		 * everything from dcache lookup, but it's likely to be what
		 * d_lookup() would've found anyway.  If it is, just return it;
		 * otherwise we really have to repeat the whole thing.
		 */
		if (unlikely(dentry->d_name.hash != hash))
			goto mismatch;
		if (unlikely(dentry->d_parent != parent))
			goto mismatch;
		if (unlikely(d_unhashed(dentry)))
			goto mismatch;
		if (unlikely(!d_same_name(dentry, parent, name)))
			goto mismatch;
		/* OK, it *is* a hashed match; return it */
		spin_unlock(&dentry->d_lock);
		dput(new);
		return dentry;
	}
	rcu_read_unlock();
	/* we can't take ->d_lock here; it's OK, though. */
	new->d_flags |= DCACHE_PAR_LOOKUP;
	new->d_wait = wq;
	hlist_bl_add_head_rcu(&new->d_u.d_in_lookup_hash, b);
	hlist_bl_unlock(b);
	return new;
mismatch:
	spin_unlock(&dentry->d_lock);
	dput(dentry);
	goto retry;
}
EXPORT_SYMBOL(d_alloc_parallel);
#endif

void __d_lookup_done(struct dentry *dentry)
{
	struct hlist_bl_head *b = in_lookup_hash(dentry->d_parent, dentry->d_name.hash);
	hlist_bl_lock(b);
	dentry->d_flags &= ~DCACHE_PAR_LOOKUP;
	__hlist_bl_del(&dentry->d_u.d_in_lookup_hash);
#if 0 //TODO
	wake_up_all(dentry->d_wait);
	dentry->d_wait = NULL;
#endif
	hlist_bl_unlock(b);
	INIT_HLIST_NODE(&dentry->d_u.d_alias);
//	INIT_LIST_HEAD(&dentry->d_lru);
}
//EXPORT_SYMBOL(__d_lookup_done);



/* inode->i_lock held if inode is non-NULL */

static inline void __d_add(dentry *dentry, inode *iinode)
{
	inode *dir = NULL;
	unsigned n;
	spin_lock(&dentry->d_lock);
	if (unlikely(d_in_lookup(dentry))) 
	{
		dir = dentry->d_parent->d_inode;
		n = start_dir_add(dir);
		__d_lookup_done(dentry);
	}
	if (iinode) {
		unsigned add_flags = d_flags_for_inode(iinode);
		hlist_add_head(&dentry->d_u.d_alias, &iinode->i_dentry);
		raw_write_seqcount_begin(&dentry->d_seq);
		__d_set_inode_and_type(dentry, iinode, add_flags);
		raw_write_seqcount_end(&dentry->d_seq);
#if 0 //TODO
		fsnotify_update_flags(dentry);
#endif
	}
	__d_rehash(dentry);
	if (dir) end_dir_add(dir, n);
	spin_unlock(&dentry->d_lock);
	if (iinode) iinode->unlock();  //spin_unlock(&node->i_lock);
}

#if 0 //TODO

/**
 * d_add - add dentry to hash queues
 * @entry: dentry to add
 * @inode: The inode to attach to this dentry
 *
 * This adds the entry to the hash queues and initializes @inode.
 * The entry was actually filled in earlier during d_alloc().
 */

void d_add(struct dentry *entry, struct inode *inode)
{
	if (inode) {
		security_d_instantiate(entry, inode);
		spin_lock(&inode->i_lock);
	}
	__d_add(entry, inode);
}
EXPORT_SYMBOL(d_add);

/**
 * d_exact_alias - find and hash an exact unhashed alias
 * @entry: dentry to add
 * @inode: The inode to go with this dentry
 *
 * If an unhashed dentry with the same name/parent and desired
 * inode already exists, hash and return it.  Otherwise, return
 * NULL.
 *
 * Parent directory should be locked.
 */
struct dentry *d_exact_alias(struct dentry *entry, struct inode *inode)
{
	struct dentry *alias;
	unsigned int hash = entry->d_name.hash;

	spin_lock(&inode->i_lock);
	hlist_for_each_entry(alias, &inode->i_dentry, d_u.d_alias) {
		/*
		 * Don't need alias->d_lock here, because aliases with
		 * d_parent == entry->d_parent are not subject to name or
		 * parent changes, because the parent inode i_mutex is held.
		 */
		if (alias->d_name.hash != hash)
			continue;
		if (alias->d_parent != entry->d_parent)
			continue;
		if (!d_same_name(alias, entry->d_parent, &entry->d_name))
			continue;
		spin_lock(&alias->d_lock);
		if (!d_unhashed(alias)) {
			spin_unlock(&alias->d_lock);
			alias = NULL;
		} else {
			__dget_dlock(alias);
			__d_rehash(alias);
			spin_unlock(&alias->d_lock);
		}
		spin_unlock(&inode->i_lock);
		return alias;
	}
	spin_unlock(&inode->i_lock);
	return NULL;
}
EXPORT_SYMBOL(d_exact_alias);

#endif

template <class T> void swap(T &a, T &b)
{
	T c = a;
	a = b; b = c;
}



static void swap_names(dentry *entry, struct dentry *target)
{
//	if (unlikely(dname_external(target)))
//	{
//		if (unlikely(dname_external(dentry))) 
//		{	/** Both external: swap the pointers	 */
//			swap(target->d_name.name, dentry->d_name.name);
//		}
//		else
//		{	/* dentry:internal, target:external.  Steal target's storage and make target internal.	 */
//			memcpy(target->d_iname, dentry->d_name.name, dentry->d_name.len() + 1);
//			dentry->d_name.name = target->d_name.name;
//			target->d_name.name = target->d_iname;
//		}
//	}
//	else
//	{
//		if (unlikely(dname_external(dentry)))
//		{	/* dentry:external, target:internal.  Give dentry's storage to target and make dentry internal */
//			memcpy(dentry->d_iname, target->d_name.name, target->d_name.len() + 1);
//			target->d_name.name = dentry->d_name.name;
//			dentry->d_name.name = dentry->d_iname;
//		}
//		else
//		{	/* Both are internal.		 */
//			unsigned int i;
////			BUILD_BUG_ON(!IS_ALIGNED(DNAME_INLINE_LEN, sizeof(long)));
//			for (i = 0; i < DNAME_INLINE_LEN / sizeof(long); i++) 
//			{
//				swap(((long *) &dentry->d_iname)[i], ((long *) &target->d_iname)[i]);
//			}
//		}
//	}
//	swap(dentry->d_name._u.hash_len, target->d_name._u.hash_len);
	swap(entry->d_name, target->d_name);
}

static void copy_name(dentry *entry, dentry *target)
{
	//struct external_name *old_name = NULL;
	//if (unlikely(dname_external(dentry)))		old_name = to_external_name(dentry);
	//if (unlikely(dname_external(target)))
	//{
	//	atomic_inc(&to_external_name(target)->u.count);
	//	dentry->d_name = target->d_name;
	//}
	//else 
	//{
	//	memcpy(dentry->d_iname, target->d_name.name,				target->d_name.len() + 1);
	//	dentry->d_name.name = dentry->d_iname;
	//	dentry->d_name._u.hash_len = target->d_name._u.hash_len;
	//}
	//if (old_name && likely(atomic_dec_and_test(&old_name->u.count)));/*	kfree_rcu(old_name, u.head);*/
	entry->d_name = target->d_name;
}

/*
 * __d_move - move a dentry
 * @dentry: entry to move
 * @target: new dentry
 * @exchange: exchange the two dentries
 *
 * Update the dcache to reflect the move of a file name. Negative dcache entries should not be moved in this way. Caller must hold rename_lock, the i_mutex of the source and target directories, and the sb->s_vfs_rename_mutex if they differ. See lock_rename(). */
static void __d_move(dentry *entry, dentry *target, bool exchange)
{
	struct dentry *old_parent, *p;
	struct inode *dir = NULL;
	unsigned n;

	WARN_ON(!entry->d_inode);
	if ((entry == target))	return;

	BUG_ON(d_ancestor(target, entry));
	old_parent = entry->d_parent;
	p = d_ancestor(old_parent, target);
	if (IS_ROOT(entry))
	{
		BUG_ON(p);
		spin_lock(&target->d_parent->d_lock);
	} 
	else if (!p) 
	{	/* target is not a descendent of entry->d_parent */
		spin_lock(&target->d_parent->d_lock);
		spin_lock_nested(&old_parent->d_lock, DENTRY_D_LOCK_NESTED);
	}
	else 
	{
		BUG_ON(p == entry);
		spin_lock(&old_parent->d_lock);
		if (p != target) spin_lock_nested(&target->d_parent->d_lock, DENTRY_D_LOCK_NESTED);
	}
	spin_lock_nested(&entry->d_lock, 2);
	spin_lock_nested(&target->d_lock, 3);

	if (unlikely(d_in_lookup(target))) 
	{
		dir = target->d_parent->d_inode;
		n = start_dir_add(dir);
		__d_lookup_done(target);
	}

	write_seqcount_begin(&entry->d_seq);
	write_seqcount_begin_nested(&target->d_seq, DENTRY_D_LOCK_NESTED);

	/* unhash both */
	if (!d_unhashed(entry)) 	___d_drop(entry);
	if (!d_unhashed(target))
		___d_drop(target);

	/* ... and switch them in the tree */
	entry->d_parent = target->d_parent;
	if (!exchange) 
	{
		copy_name(entry, target);
		target->d_hash.pprev = NULL;
		entry->d_parent->d_lockref.count++;
		if (entry != old_parent) /* wasn't IS_ROOT */
			WARN_ON(!--old_parent->d_lockref.count);
	} else {
		target->d_parent = old_parent;
		swap_names(entry, target);
		list_move(&target->d_child, &target->d_parent->d_subdirs);
		__d_rehash(target);
#if 0 //TODO
		fsnotify_update_flags(target);
#endif
	}
	list_move(&entry->d_child, &entry->d_parent->d_subdirs);
	__d_rehash(entry);
#if 0 //TODO
	fsnotify_update_flags(entry);
	fscrypt_handle_d_move(entry);
#endif

	write_seqcount_end(&target->d_seq);
	write_seqcount_end(&entry->d_seq);

	if (dir)
		end_dir_add(dir, n);

	if (entry->d_parent != old_parent)
		spin_unlock(&entry->d_parent->d_lock);
	if (entry != old_parent)
		spin_unlock(&old_parent->d_lock);
	spin_unlock(&target->d_lock);
	spin_unlock(&entry->d_lock);
}

#if 0 //TODO

/*
 * d_move - move a dentry
 * @dentry: entry to move
 * @target: new dentry
 *
 * Update the dcache to reflect the move of a file name. Negative
 * dcache entries should not be moved in this way. See the locking
 * requirements for __d_move.
 */
void d_move(struct dentry *dentry, struct dentry *target)
{
	write_seqlock(&rename_lock);
	__d_move(dentry, target, false);
	write_sequnlock(&rename_lock);
}
EXPORT_SYMBOL(d_move);

/*
 * d_exchange - exchange two dentries
 * @dentry1: first dentry
 * @dentry2: second dentry
 */
void d_exchange(struct dentry *dentry1, struct dentry *dentry2)
{
	write_seqlock(&rename_lock);

	WARN_ON(!dentry1->d_inode);
	WARN_ON(!dentry2->d_inode);
	WARN_ON(IS_ROOT(dentry1));
	WARN_ON(IS_ROOT(dentry2));

	__d_move(dentry1, dentry2, true);

	write_sequnlock(&rename_lock);
}
#endif

/**
 * d_ancestor - search for an ancestor
 * @p1: ancestor dentry
 * @p2: child dentry
 *
 * Returns the ancestor dentry of p2 which is a child of p1, if p1 is an ancestor of p2, else NULL. */
dentry *d_ancestor(dentry *p1, dentry *p2)
{
	dentry *p;
	for (p = p2; !IS_ROOT(p); p = p->d_parent) 
	{
		if (p->d_parent == p1) return p;
	}
	return NULL;
}


/* This helper attempts to cope with remotely renamed directories
 * It assumes that the caller is already holding dentry->d_parent->d_inode->i_mutex, and rename_lock
 * Note: If ever the locking in lock_rename() changes, then please remember to update this too... */
static int __d_unalias(struct inode *inode, struct dentry *dentry, struct dentry *alias)
{
	mutex *m1 = NULL;
	rw_semaphore *m2 = NULL;
//	int ret = -ESTALE;
	int ret = -1;

	/* If alias and dentry share a parent, then no extra locks required */
	if (alias->d_parent == dentry->d_parent) goto out_unalias;

	/* See lock_rename() */
	if (!mutex_trylock(&dentry->d_sb->s_vfs_rename_mutex))  goto out_err;
	m1 = &dentry->d_sb->s_vfs_rename_mutex;
	if (!inode_trylock_shared(alias->d_parent->d_inode))	goto out_err;
	m2 = &alias->d_parent->d_inode->i_rwsem;
out_unalias:
	__d_move(alias, dentry, false);
	ret = 0;
out_err:
	if (m2) up_read(m2);
	if (m1) mutex_unlock(m1);
	return ret;
}

/* d_splice_alias - splice a disconnected dentry into the tree if one exists
 * @inode:  the inode which may have a disconnected dentry
 * @dentry: a negative dentry which we want to point to the inode.
 *
 * If inode is a directory and has an IS_ROOT alias, then d_move that in place of the given dentry and return it, else simply d_add the inode to the dentry and return NULL.
  * If a non-IS_ROOT directory is found, the filesystem is corrupt, and we should error out: directories can't have multiple aliases.
 * This is needed in the lookup routine of any filesystem that is exportable (via knfsd) so that we can build dcache paths to directories effectively.
 * If a dentry was found and moved, then it is returned.  Otherwise NULL is returned.  This matches the expected return value of ->lookup.
 * Cluster filesystems may call this function with a negative, hashed dentry. In that case, we know that the inode will be a regular file, and also this will only occur during atomic_open. So we need to check for the dentry being already hashed only in the final case. */
dentry *d_splice_alias(inode *iinode, dentry *entry)
{
//#define ERR_CAST(x) reinterpret_cast<dentry*>(x)
//#define ERR_PTR(x) reinterpret_cast<dentry*>(x)

	if (IS_ERR(iinode)) return ERR_PTR<dentry>(PTR_ERR(iinode));
//	BUG_ON(!d_unhashed(entry));
	if (!iinode)		goto out;

#if 0 //TODO
	security_d_instantiate(entry, iinode);
#endif
//	spin_lock(&iinode->i_lock);
//	iinode->lock();
	LOCK_INODE(iinode);
	if (S_ISDIR(iinode->i_mode))
	{
		dentry *new_entry = __d_find_any_alias(iinode);
		if (unlikely(new_entry)) 
		{	/* The reference to new ensures it remains an alias */
			//spin_unlock(&iinode->i_lock);
			iinode->unlock();
//			write_seqlock(&rename_lock);
			iinode->i_sb->lock_rename();
			if (unlikely(d_ancestor(new_entry, entry)))		// 检查new_entry是否是entry的祖先节点
			{	// new_entry是entry的祖先节点，则存在循环，报错
//				write_sequnlock(&rename_lock);
				iinode->i_sb->unlock_rename();
				dput(new_entry);
				new_entry = ERR_PTR<dentry>(-ELOOP);
				LOG_WARNING(L"VFS: Lookup of '%S' in %S %S would have caused loop", entry->d_name.name, 
					iinode->i_sb->s_type->name, iinode->i_sb->s_id);
				//pr_warn_ratelimited( "VFS: Lookup of '%s' in %s %s would have caused loop\n",
				//	entry->d_name.name, inode->i_sb->s_type->name, inode->i_sb->s_id);
			}
			else if (!IS_ROOT(new_entry)) 
			{	// new_entry 不是root
				dentry *old_parent = dget(new_entry->d_parent);
				int err = __d_unalias(iinode, entry, new_entry);
//				write_sequnlock(&rename_lock);
				iinode->i_sb->unlock_rename();

				if (err)
				{
					dput(new_entry);
					new_entry = ERR_PTR<dentry>(err);
				}
				dput(old_parent);
			} 
			else 
			{
				__d_move(new_entry, entry, false);
//				write_sequnlock(&rename_lock);
				iinode->i_sb->unlock_rename();
			}
			iput(iinode);
			return new_entry;
		}
	}
out:
	__d_add(entry, iinode);
	return NULL;
#undef ERR_CAST
}

//EXPORT_SYMBOL(d_splice_alias);

#if 0 //TODO
/*
 * Test whether new_dentry is a subdirectory of old_dentry.
 *
 * Trivially implemented using the dcache structure
 */

/**
 * is_subdir - is new dentry a subdirectory of old_dentry
 * @new_dentry: new dentry
 * @old_dentry: old dentry
 *
 * Returns true if new_dentry is a subdirectory of the parent (at any depth).
 * Returns false otherwise.
 * Caller must ensure that "new_dentry" is pinned before calling is_subdir()
 */
  
bool is_subdir(struct dentry *new_dentry, struct dentry *old_dentry)
{
	bool result;
	unsigned seq;

	if (new_dentry == old_dentry)
		return true;

	do {
		/* for restarting inner loop in case of seq retry */
		seq = read_seqbegin(&rename_lock);
		/*
		 * Need rcu_readlock to protect against the d_parent trashing
		 * due to d_move
		 */
		rcu_read_lock();
		if (d_ancestor(old_dentry, new_dentry))
			result = true;
		else
			result = false;
		rcu_read_unlock();
	} while (read_seqretry(&rename_lock, seq));

	return result;
}
EXPORT_SYMBOL(is_subdir);

static enum d_walk_ret d_genocide_kill(void *data, struct dentry *dentry)
{
	struct dentry *root = data;
	if (dentry != root) {
		if (d_unhashed(dentry) || !dentry->d_inode)
			return D_WALK_SKIP;

		if (!(dentry->d_flags & DCACHE_GENOCIDE)) {
			dentry->d_flags |= DCACHE_GENOCIDE;
			dentry->d_lockref.count--;
		}
	}
	return D_WALK_CONTINUE;
}

void d_genocide(struct dentry *parent)
{
	d_walk(parent, parent, d_genocide_kill);
}

EXPORT_SYMBOL(d_genocide);

void d_tmpfile(struct dentry *dentry, struct inode *inode)
{
	inode_dec_link_count(inode);
	BUG_ON(dentry->d_name.name != dentry->d_iname ||
		!hlist_unhashed(&dentry->d_u.d_alias) ||
		!d_unlinked(dentry));
	spin_lock(&dentry->d_parent->d_lock);
	spin_lock_nested(&dentry->d_lock, DENTRY_D_LOCK_NESTED);
	dentry->d_name.len() = sprintf(dentry->d_iname, "#%llu",
				(unsigned long long)inode->i_ino);
	spin_unlock(&dentry->d_lock);
	spin_unlock(&dentry->d_parent->d_lock);
	d_instantiate(dentry, inode);
}
EXPORT_SYMBOL(d_tmpfile);

static __initdata unsigned long dhash_entries;
static int __init set_dhash_entries(char *str)
{
	if (!str)
		return 0;
	dhash_entries = simple_strtoul(str, &str, 0);
	return 1;
}
__setup("dhash_entries=", set_dhash_entries);

static void __init dcache_init_early(void)
{
	/* If hashes are distributed across NUMA nodes, defer
	 * hash allocation until vmalloc space is available.
	 */
	if (hashdist)
		return;

	dentry_hashtable =
		alloc_large_system_hash("Dentry cache",
					sizeof(struct hlist_bl_head),
					dhash_entries,
					13,
					HASH_EARLY | HASH_ZERO,
					&d_hash_shift,
					NULL,
					0,
					0);
	d_hash_shift = 32 - d_hash_shift;
}

static void __init dcache_init(void)
{
	/*
	 * A constructor could be added for stable state like the lists,
	 * but it is probably not worth it because of the cache nature
	 * of the dcache.
	 */
	dentry_cache = KMEM_CACHE_USERCOPY(dentry,
		SLAB_RECLAIM_ACCOUNT|SLAB_PANIC|SLAB_MEM_SPREAD|SLAB_ACCOUNT,
		d_iname);

	/* Hash may have been set up in dcache_init_early */
	if (!hashdist)
		return;

	dentry_hashtable =
		alloc_large_system_hash("Dentry cache",
					sizeof(struct hlist_bl_head),
					dhash_entries,
					13,
					HASH_ZERO,
					&d_hash_shift,
					NULL,
					0,
					0);
	d_hash_shift = 32 - d_hash_shift;
}

/* SLAB cache for __getname() consumers */
struct kmem_cache *names_cachep __read_mostly;
EXPORT_SYMBOL(names_cachep);

void __init vfs_caches_init_early(void)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(in_lookup_hashtable); i++)
		INIT_HLIST_BL_HEAD(&in_lookup_hashtable[i]);

	dcache_init_early();
	inode_init_early();
}

void __init vfs_caches_init(void)
{
	names_cachep = kmem_cache_create_usercopy("names_cache", PATH_MAX, 0,
			SLAB_HWCACHE_ALIGN|SLAB_PANIC, 0, PATH_MAX, NULL);

	dcache_init();
	inode_init();
	files_init();
	files_maxfiles_init();
	mnt_init();
	bdev_cache_init();
	chrdev_init();
}


#endif


///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// ==== Dentry Manager ====
CDentryManager::CDentryManager(size_t init_size) : m_buf(nullptr)
{
	LOG_DEBUG(L"dentry buf size=%lld", init_size);
#if DENTRY_BUF_TYPE == 1
	m_buf_size = init_size;
	m_buf = new dentry[m_buf_size];
	if (m_buf == nullptr)	THROW_ERROR(ERR_MEM, L"failed on creating dentry buffer");
	for (size_t ii = 0; ii < m_buf_size; ii++)
	{
		m_free_list.push_back(m_buf + ii);
		m_tail++;
	}
	JCASSERT(m_free_list.size() == m_buf_size);
#elif DENTRY_BUF_TYPE == 2
	m_buf_size = init_size;
	m_buf = new dentry[m_buf_size];
	if (m_buf == nullptr)	THROW_ERROR(ERR_MEM, L"failed on creating dentry buffer");
	m_free = new PDENTRY[m_buf_size];
	for (size_t ii = 0; ii < m_buf_size; ii++)
	{
		m_free[ii] = m_buf + ii;
	}
	m_tail=m_buf_size-1;
	m_used = 0;
#else
	m_buf_size = 0;
#endif
}

CDentryManager::~CDentryManager(void)
{
	LOG_STACK_TRACE();
#if DENTRY_BUF_TYPE == 1
	LOG_DEBUG(L"release dentry buffer, free size=%d, allocated size=%d", m_free_list.size(), m_buf_size);
	if (m_free_list.size() < m_buf_size)
	{
		LOG_ERROR(L"[err] dentry memory leak happened, allocated=%d, reclaimed=%d", m_buf_size, m_free_list.size());
		JCASSERT(0);
#ifdef _DEBUG
		// 输出没有被回收的节点
		for (auto it = m_free_list.begin(); it != m_free_list.end(); ++it)
		{
			(*it)->m_manager = nullptr;
		}
		for (size_t ii = 0; ii < m_buf_size; ++ii)
		{
			if (m_buf[ii].m_manager != nullptr)
			{
				LOG_DEBUG(L"dentry: index=%d, name=%S, inode=%d, ref=%d", ii, m_buf[ii].d_name.name.c_str(),
					m_buf[ii].d_inode ? m_buf[ii].d_inode->i_ino : (-1), m_buf[ii].d_lockref.count);
			}
		}
		LOG_DEBUG(L"dump dentry leak completed");
#endif
	}
	delete[] m_buf;
#elif DENTRY_BUF_TYPE == 2

	delete[] m_free;
	delete[] m_buf;
#else

#endif
}

dentry* CDentryManager::__d_alloc(super_block* sb, const qstr* name)
{
	dentry* ptr_dentry;
	char* dname;
	int err;

	//ptr_dentry = kmem_cache_alloc(dentry_cache, GFP_KERNEL);
#if DENTRY_BUF_TYPE==1
	// 从free队列中查找
	if (m_free_list.empty()) THROW_ERROR(ERR_MEM, L"no enough buffer for dentry");
	lock();
	ptr_dentry = m_free_list.front();
	m_free_list.pop_front();
	m_head++;
	unlock();
	F_LOG_DEBUG(L"dentry", L"alloc: dentry=%p, index=%lld, free=%lld, head=%lld, tail=%lld ", ptr_dentry, ptr_dentry - m_buf, m_free_list.size(), m_head, m_tail);
#elif DENTRY_BUF_TYPE==2
	lock();
	if (m_tail == m_head)
	{
		unlock();
		THROW_ERROR(ERR_MEM, L"no enough buffer for dentry");
	}
	ptr_dentry = m_free[m_head];
	m_head++;
	if (m_head >= m_buf_size) m_head = 0;
	m_used++;
	unlock();
	LOG_DEBUG(L"dentry buf, used=%lld, empty=%lld", m_used, m_buf_size - m_used);
	F_LOG_DEBUG(L"dentry", L"alloc: dentry=%p, index=%lld, head=%lld, tail=%lld ", ptr_dentry, ptr_dentry - m_buf, m_head, m_tail);
#else

#endif
	JCASSERT(ptr_dentry);

	ptr_dentry->m_manager = this;

	//ptr_dentry = new dentry;
	//if (!ptr_dentry)	return NULL;

	/* We guarantee that the inline name is always NUL-terminated. This way the memcpy() done by the name switching in rename will still always have a NUL at the end, even if we might be overwriting an internal NUL character */
	ptr_dentry->d_iname[DNAME_INLINE_LEN - 1] = 0;
	if (unlikely(!name))
	{
		name = &slash_name;
//		ptr_dentry->d_name = slash_name;
	}
	else if (name->len() > DNAME_INLINE_LEN - 1)
	{
#if 0 //<TODO> 处理长文件名
		size_t size = offsetof(struct external_name, name[1]);
		external_name* p = (external_name*)malloc(size + name->len());
		//kmalloc(size + name->len,  GFP_KERNEL_ACCOUNT |	  __GFP_RECLAIMABLE);
		if (!p)
		{	//kmem_cache_free(dentry_cache, ptr_dentry); 
			delete ptr_dentry;
			return NULL;
		}
		InterlockedExchange(&p->u.count, 1);
		//atomic_set(&p->u.count, 1);
		dname = p->name;
#endif
		ptr_dentry->d_name = *name;
	}
	else { dname = ptr_dentry->d_iname; }

	ptr_dentry->d_name = *name;

	/* Make sure we always see the terminating NUL character */
	ptr_dentry->d_lockref.count = 1;
	ptr_dentry->d_flags = 0;
	spin_lock_init(&ptr_dentry->d_lock);
	seqcount_spinlock_init(&ptr_dentry->d_seq, &ptr_dentry->d_lock);
	ptr_dentry->d_inode = NULL;
	ptr_dentry->d_parent = ptr_dentry;
	ptr_dentry->d_sb = sb;
	ptr_dentry->d_op = NULL;
	ptr_dentry->d_fsdata = NULL;
	INIT_HLIST_BL_NODE(&ptr_dentry->d_hash);
	//	INIT_LIST_HEAD(&ptr_dentry->d_lru);
	INIT_LIST_HEAD(&ptr_dentry->d_subdirs);
	INIT_HLIST_NODE(&ptr_dentry->d_u.d_alias);
	INIT_LIST_HEAD(&ptr_dentry->d_child);
	d_set_d_op(ptr_dentry, ptr_dentry->d_sb->s_d_op);

	if (ptr_dentry->d_op && ptr_dentry->d_op->d_init)
	{
		err = ptr_dentry->d_op->d_init(ptr_dentry);
		if (err)
		{	// 回收dentry
			JCASSERT((UINT64)(ptr_dentry - m_buf) < m_buf_size);
#if DENTRY_BUF_TYPE == 1
			lock();
			m_free_list.push_back(ptr_dentry);
			m_tail++;
			unlock();
			F_LOG_DEBUG(L"dentry", L"reclaim: dentry due to error, dentry=%p, index=%lld, free=%lld, head=%lld, tail=%lld", ptr_dentry, ptr_dentry - m_buf, m_free_list.size(), m_head, m_tail);
#elif DENTRY_BUF_TYPE == 2
			lock();
			m_tail++;
			if (m_tail >= m_buf_size) m_tail = 0;
			if (m_head == m_tail)
			{
				unlock();
				THROW_ERROR(ERR_MEM, L"dentry buffer full");
			}
			m_free[m_tail] = ptr_dentry;
			m_used--;
			unlock();
			F_LOG_DEBUG(L"dentry", L"reclaim: dentry=%p, index=%lld, head=%lld, tail=%lld, inode=%p", ptr_dentry, ptr_dentry - m_buf, m_head, m_tail, ptr_dentry->d_inode);
#else

#endif
			return NULL;
		}
	}
#if 0 //<TODO>
	this_cpu_inc(nr_dentry);
#endif
	return ptr_dentry;
}


//dentry * CDentryManager::d_alloc_anon(super_block *sb)
//{
//	return __d_alloc(sb, NULL);
//}

dentry* CDentryManager::d_make_root(inode* root_inode)
{
	dentry* res = NULL;

	if (root_inode)
	{
//		res = d_alloc_anon(root_inode->i_sb);
		res = __d_alloc(root_inode->i_sb, NULL);
		if (res)		d_instantiate(res, root_inode);
		else			iput(root_inode);
	}
	return res;
}

void CDentryManager::free(dentry* ddentry)
{
	JCASSERT((UINT64)(ddentry - m_buf) < m_buf_size);
#if DENTRY_BUF_TYPE == 1
	lock();
	m_free_list.push_back(ddentry);
	m_tail++;
	unlock();
	F_LOG_DEBUG(L"dentry", L"reclaim: dentry=%p, index=%lld, free=%lld, head=%lld, tail=%lld, inode=%p", ddentry,  ddentry - m_buf, m_free_list.size(), m_head, m_tail, ddentry->d_inode);
#elif DENTRY_BUF_TYPE == 2
	lock();
	m_tail++;
	if (m_tail >= m_buf_size) m_tail = 0;
	if (m_head == m_tail)
	{
		unlock();
		THROW_ERROR(ERR_MEM, L"dentry buffer full");
	}
	m_free[m_tail] = ddentry;
	m_used--;
	unlock();
	F_LOG_DEBUG(L"dentry", L"reclaim: dentry=%p, index=%lld, head=%lld, tail=%lld, inode=%p", ddentry, ddentry - m_buf, m_head, m_tail, ddentry->d_inode);
#else

#endif
}

#ifdef _DEBUG
void dentry::dentry_trace(const wchar_t* func, int line)
{
	LOG_DEBUG(L"<call=%s> <line=%d> [d_trace] addr=%p, fn=%S, ref=%d, inode=%p", func, line, this, d_name.name.c_str(), d_lockref.count, d_inode);
}
#endif
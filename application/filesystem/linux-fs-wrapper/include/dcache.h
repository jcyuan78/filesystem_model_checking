﻿///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __LINUX_DCACHE_H
#define __LINUX_DCACHE_H

#include "linux_comm.h"
#include "list.h"
#include "list_bl.h"
#include <string>
#include <list>
#include "allocator.h"

//#define DEBUG_DENTRY

struct path;
struct vfsmount;

/* linux/include/linux/dcache.h
 *
 * Dirent cache data structures
 *
 * (C) Copyright 1997 Thomas Schoebel-Theuer, with heavy changes by Linus Torvalds */

#define IS_ROOT(x) ((x) == (x)->d_parent)

/* The hash is always the low bits of hash_len */
#ifdef __LITTLE_ENDIAN
 #define HASH_LEN_DECLARE u32 hash; u32 len
 #define bytemask_from_count(cnt)	(~(~0ul << (cnt)*8))
#else
 #define HASH_LEN_DECLARE u32 len; u32 hash
 #define bytemask_from_count(cnt)	(~(~0ul >> (cnt)*8))
#endif

/* "quick string" -- eases parameter passing, but more importantly saves "metadata" about the string (ie length and the hash). hash comes first so it snuggles against d_parent in the dentry. */
struct qstr 
{
	//qstr(const char* n, u32 l) { name = const_cast<char*>(n); _u._s.len = l; }
	//qstr(unsigned char* n, u32 l) { name = (char*)(n); _u._s.len = l; }
	qstr(const qstr& src) :name(src.name), hash(src.hash), hash_len(src.hash_len) {}
	qstr(const char* n, size_t l) : name(n), hash(0), hash_len(0) {}
	qstr(const wchar_t* n, size_t l) : hash(0), hash_len(0)
	{
		if (l == 0) l = wcslen(n);
		jcvos::UnicodeToUtf8(name, n, l);
	}
	qstr(const std::wstring& n) : hash(0), hash_len(0)
	{
		jcvos::UnicodeToUtf8(name, n);
	}
	//qstr(const wchar_t* n, size_t l=0): hash(0), hash_len(0) 
	//{
	//	jcvos::UnicodeToUtf8(name, n);
	//}
	qstr(void) {}
	inline size_t len(void) const { return name.size(); }

	std::string name;
	UINT64 hash;
	UINT64 hash_len;
};

//#define QSTR_INIT(n,l) { { { l } }, n }
#define QSTR_INIT(n,l)	(n,l)

extern const struct qstr empty_name;
extern const struct qstr slash_name;
extern const struct qstr dotdot_name;

struct dentry_stat_t {
	long nr_dentry;
	long nr_unused;
	long age_limit;		/* age in seconds */
	long want_pages;	/* pages requested by system */
	long nr_negative;	/* # of unused negative dentries */
	long dummy;		/* Reserved for future use */
};
extern struct dentry_stat_t dentry_stat;

/* Try to keep struct dentry aligned on 64 byte cachelines (this will give reasonable cacheline footprint with larger lines without the large memory footprint increase). */
#ifdef CONFIG_64BIT
# define DNAME_INLINE_LEN 32 /* 192 bytes */
#else
# ifdef CONFIG_SMP
#  define DNAME_INLINE_LEN 36 /* 128 bytes */
# else
#  define DNAME_INLINE_LEN 40 /* 128 bytes */
# endif
#endif

#define d_lock	d_lockref.lock

struct inode;
struct super_block;
struct dentry_operations;

class CDentryManager;

struct dentry
{
	/* RCU lookup touched fields */
	unsigned int d_flags;		/* protected by d_lock */
	seqcount_spinlock_t d_seq;	/* per dentry seqlock */
	hlist_bl_node d_hash;	/* lookup hash list */
	dentry* d_parent;	/* parent directory */
	qstr d_name;
	inode* d_inode;		/* Where the name belongs to - NULL is negative */
	char d_iname[DNAME_INLINE_LEN];	/* small names */

	/* Ref lookup also touches following */
	lockref d_lockref;	/* per-dentry lock and refcount */
	const dentry_operations* d_op;
	super_block* d_sb;	/* The root of the dentry tree */
	unsigned long d_time;		/* used by d_revalidate */
	void* d_fsdata;			/* fs-specific data */

	// lru队列移动到f2fs_super_info中
	list_head d_child;	/* child of parent list */
	list_head d_subdirs;	/* our children */
	/* d_alias and d_rcu can share memory */
	union
	{
		hlist_node d_alias;	/* inode alias list */
		hlist_bl_node d_in_lookup_hash;	/* only for in-lookup ones */
		// not support rcu
//		rcu_head d_rcu;
	} d_u;
public:
	dentry* get_parent(void) { return d_parent; }
	template <class T> T* get_inode(void) { return dynamic_cast<T*>(d_inode); }

public:
//	friend class CDentryManager;
//protected:
//	void init(Allocator<dentry>* manager) { m_manager = dynamic_cast<CDentryManager*>(manager); }
	CDentryManager* m_manager;

#ifdef DEBUG_DENTRY
	void dentry_trace(const wchar_t* func, int line);
#endif
};

//#define _STATIC_DENTRY_BUF

//#define DENTRY_BUF_TYPE		1			// buffer按静态分配，free以std::list形式
#define DENTRY_BUF_TYPE		2			// buffer按静态分配，free以数组FIFO形式
//#define DENTRY_BUF_TYPE		3			// buffer按动态分配等

// 用于dentry的内存分配，管理和缓存
//	基本思想：启动时预分配一定适量的dentry，避免频繁使用new/delete。需要的时候从free list中分配
class CDentryManager : public Allocator<dentry>
{
public:
	CDentryManager(size_t init_size);
	~CDentryManager(void);

protected:
	dentry* __d_alloc(super_block* sb, const qstr* name);
	void free(dentry* ddentry);
//	CRITICAL_SECTION m_list_lock;
//	inline void lock() { EnterCriticalSection(&m_list_lock); };
//	inline void unlock() { LeaveCriticalSection(&m_list_lock); };

public:
	friend dentry* d_alloc(dentry* parent, const qstr& name);
	friend void dentry_free(dentry* ddentry);
//	dentry* d_alloc_anon(super_block* sb);
	dentry* d_make_root(inode* root_inode);

//#ifdef _DEBUG	// 用于跟踪 dentry的申请和回收
//	size_t m_head = 0, m_tail = 0;
//#endif



protected:
	typedef dentry* PDENTRY;
#if DENTRY_BUF_TYPE == 1
	// 预先申请足够的dentry的缓存，仅从预申请缓存中分配
	dentry* m_buf;
	size_t m_buf_size;	// 总共申请的缓存数量
	std::list<dentry*> m_free_list;
#elif DENTRY_BUF_TYPE ==2
	dentry* m_buf;
	PDENTRY * m_free;
	size_t m_buf_size;	// 总共申请的缓存数量
	size_t m_head = 0, m_tail = 0, m_used=0;
#else
	// 按需申请dentry缓存，释放的缓存放回m_free_list中

#endif
};

//__randomize_layout;

/* dentry->d_lock spinlock nesting subclasses:
 *
 * 0: normal
 * 1: nested */
enum dentry_d_lock_class
{
	DENTRY_D_LOCK_NORMAL, /* implicitly used by plain spin_lock() APIs. */
	DENTRY_D_LOCK_NESTED
};

struct dentry_operations
{
	int (*d_revalidate)(struct dentry*, unsigned int);
	int (*d_weak_revalidate)(struct dentry*, unsigned int);
	int (*d_hash)(const struct dentry*, struct qstr*);
	int (*d_compare)(const struct dentry*, unsigned int, const char*, const struct qstr*);
	int (*d_delete)(const struct dentry*);
	int (*d_init)(struct dentry*);
	void (*d_release)(struct dentry*);
	void (*d_prune)(struct dentry*);
	void (*d_iput)(struct dentry*, struct inode*);
	char* (*d_dname)(struct dentry*, char*, int);
	struct vfsmount* (*d_automount)(struct path*);
	int (*d_manage)(const struct path*, bool);
	struct dentry* (*d_real)(struct dentry*, const struct inode*);
};
//____cacheline_aligned;

/* Locking rules for dentry_operations callbacks are to be found in
 * Documentation/filesystems/locking.rst. Keep it updated!
 *
 * FUrther descriptions are found in Documentation/filesystems/vfs.rst. Keep it updated too! */

/* d_flags entries */
#define DCACHE_OP_HASH			0x00000001
#define DCACHE_OP_COMPARE		0x00000002
#define DCACHE_OP_REVALIDATE		0x00000004
#define DCACHE_OP_DELETE		0x00000008
#define DCACHE_OP_PRUNE			0x00000010

#define	DCACHE_DISCONNECTED		0x00000020
     /* This dentry is possibly not currently connected to the dcache tree, in
      * which case its parent will either be itself, or will have this flag as
      * well.  nfsd will not use a dentry with this bit set, but will first
      * endeavour to clear the bit either by discovering that it is connected,
      * or by performing lookup operations.   Any filesystem which supports
      * nfsd_operations MUST have a lookup function which, if it finds a
      * directory inode with a DCACHE_DISCONNECTED dentry, will d_move that
      * dentry into place and return that dentry rather than the passed one,
      * typically using d_splice_alias. */

#define DCACHE_REFERENCED		0x00000040 /* Recently used, don't discard. */

#define DCACHE_DONTCACHE		0x00000080 /* Purge from memory on final dput() */

#define DCACHE_CANT_MOUNT		0x00000100
#define DCACHE_GENOCIDE			0x00000200
#define DCACHE_SHRINK_LIST		0x00000400

#define DCACHE_OP_WEAK_REVALIDATE	0x00000800

#define DCACHE_NFSFS_RENAMED		0x00001000
     /* this dentry has been "silly renamed" and has to be deleted on the last
      * dput() */
#define DCACHE_COOKIE			0x00002000 /* For use by dcookie subsystem */
#define DCACHE_FSNOTIFY_PARENT_WATCHED	0x00004000
     /* Parent inode is watched by some fsnotify listener */

#define DCACHE_DENTRY_KILLED		0x00008000

#define DCACHE_MOUNTED			0x00010000 /* is a mountpoint */
#define DCACHE_NEED_AUTOMOUNT		0x00020000 /* handle automount on this dir */
#define DCACHE_MANAGE_TRANSIT		0x00040000 /* manage transit from this dirent */
#define DCACHE_MANAGED_DENTRY \
	(DCACHE_MOUNTED|DCACHE_NEED_AUTOMOUNT|DCACHE_MANAGE_TRANSIT)

#define DCACHE_LRU_LIST			0x00080000

#define DCACHE_ENTRY_TYPE		0x00700000
#define DCACHE_MISS_TYPE		0x00000000 /* Negative dentry (maybe fallthru to nowhere) */
#define DCACHE_WHITEOUT_TYPE		0x00100000 /* Whiteout dentry (stop pathwalk) */
#define DCACHE_DIRECTORY_TYPE		0x00200000 /* Normal directory */
#define DCACHE_AUTODIR_TYPE		0x00300000 /* Lookupless directory (presumed automount) */
#define DCACHE_REGULAR_TYPE		0x00400000 /* Regular file type (or fallthru to such) */
#define DCACHE_SPECIAL_TYPE		0x00500000 /* Other file type (or fallthru to such) */
#define DCACHE_SYMLINK_TYPE		0x00600000 /* Symlink (or fallthru to such) */

#define DCACHE_MAY_FREE			0x00800000
#define DCACHE_FALLTHRU			0x01000000 /* Fall through to lower layer */
#define DCACHE_NOKEY_NAME		0x02000000 /* Encrypted name encoded without key */
#define DCACHE_OP_REAL			0x04000000

#define DCACHE_PAR_LOOKUP		0x10000000 /* being looked up (with parent locked shared) */
#define DCACHE_DENTRY_CURSOR		0x20000000
#define DCACHE_NORCU			0x40000000 /* No RCU delay for freeing */

//extern seqlock_t rename_lock;

/* These are the low-level FS interfaces to the dcache.. */
void d_instantiate(struct dentry*, struct inode*);
void d_instantiate_new(struct dentry *, struct inode *);
extern void d_delete(struct dentry *);
#if 0
extern struct dentry * d_instantiate_unique(struct dentry *, struct inode *);
extern struct dentry * d_instantiate_anon(struct dentry *, struct inode *);
extern void __d_drop(struct dentry *dentry);
extern void d_drop(struct dentry *dentry);
#endif

inline void d_set_d_op(struct dentry *dentry, const struct dentry_operations *op){}
/* alloc_obj/de-alloc_obj */
extern dentry * d_alloc(dentry *, const qstr &);

#if 0
extern struct dentry * d_alloc_anon(struct super_block *);
extern struct dentry * d_alloc_parallel(struct dentry *, const struct qstr *, wait_queue_head_t *);
#endif
//extern struct dentry * d_splice_alias(struct inode *, struct dentry *);
#if 0 //TODO
extern struct dentry * d_add_ci(struct dentry *, struct inode *, struct qstr *);
extern struct dentry * d_exact_alias(struct dentry *, struct inode *);
extern struct dentry *d_find_any_alias(struct inode *inode);
extern struct dentry * d_obtain_alias(struct inode *);
extern struct dentry * d_obtain_root(struct inode *);
#endif
void shrink_dcache_sb(super_block*);
extern void shrink_dcache_parent(dentry *);
extern void shrink_dcache_for_umount(super_block *);
extern void d_invalidate(dentry *);

/* only used at mount-time */
//dentry* d_make_root(inode*);

/* <clickety>-<click> the ramfs-type tree */
extern void d_genocide(struct dentry *);

extern void d_tmpfile(struct dentry *, struct inode *);

extern struct dentry *d_find_alias(struct inode *);
extern void d_prune_aliases(struct inode *);

extern struct dentry *d_find_alias_rcu(struct inode *);

/* test whether we have any submounts in a subdir tree */
extern int path_has_submounts(const struct path *);
#if 0

/*
 * This adds the entry to the hash queues.
 */
extern void d_rehash(struct dentry *);
 
extern void d_add(struct dentry *, struct inode *);

/* used for rename() and baskets */
extern void d_move(struct dentry *, struct dentry *);
extern void d_exchange(struct dentry *, struct dentry *);
extern struct dentry *d_ancestor(struct dentry *, struct dentry *);

/* appendix may either be NULL or be used for transname suffixes */
extern struct dentry *d_lookup(const struct dentry *, const struct qstr *);
extern struct dentry *d_hash_and_lookup(struct dentry *, struct qstr *);
extern struct dentry *__d_lookup(const struct dentry *, const struct qstr *);
extern struct dentry *__d_lookup_rcu(const struct dentry *parent,
				const struct qstr *name, unsigned *seq);

#if 0 //<TODO>
static inline unsigned d_count(const struct dentry *dentry)
{
	return dentry->d_lockref.count;
}
#endif

#if 0 //<TOOD>
/* helper function for dentry_operations.d_dname() members*/
extern __printf(4, 5) char *dynamic_dname(struct dentry *, char *, int, const char *, ...);
#endif

extern char *__d_path(const struct path *, const struct path *, char *, int);
extern char *d_absolute_path(const struct path *, char *, int);
extern char *d_path(const struct path *, char *, int);
extern char *dentry_path_raw(const struct dentry *, char *, int);
extern char *dentry_path(const struct dentry *, char *, int);
#endif 

/* Allocation counts.. */

/**
 *	dget, dget_dlock -	get a reference to a dentry
 *	@dentry: dentry to get a reference to
 *
 *	Given a dentry or %NULL pointer increment the reference count
 *	if appropriate and return the dentry. A dentry will not be 
 *	destroyed when it has references.
 */
#if 0 //<TODO>
static inline struct dentry *dget_dlock(struct dentry *dentry)
{
	if (dentry)	dentry->d_lockref.count++;
	return dentry;
}
#endif

static inline dentry *dget(dentry *dentry)
{
	if (dentry)	lockref_get(&dentry->d_lockref);
	return dentry;
}

extern struct dentry *dget_parent(struct dentry *dentry);

/**
 *	d_unhashed -	is dentry hashed
 *	@dentry: entry to check
 *
 *	Returns true if the dentry passed is not currently hashed.
 */
 
static inline int d_unhashed(const struct dentry *dentry)
{
	return hlist_bl_unhashed(&dentry->d_hash);
}

static inline int d_unlinked(const struct dentry *dentry)
{
	return d_unhashed(dentry) && !IS_ROOT(dentry);
}

static inline int cant_mount(const struct dentry *dentry)
{
	return (dentry->d_flags & DCACHE_CANT_MOUNT);
}

static inline void dont_mount(struct dentry *dentry)
{
	spin_lock(&dentry->d_lock);
	dentry->d_flags |= DCACHE_CANT_MOUNT;
	spin_unlock(&dentry->d_lock);
}

extern void __d_lookup_done(struct dentry *);

static inline int d_in_lookup(const struct dentry *dentry)
{
	return dentry->d_flags & DCACHE_PAR_LOOKUP;
}

static inline void d_lookup_done(struct dentry *dentry)
{
	if (unlikely(d_in_lookup(dentry))) 
	{
		spin_lock(&dentry->d_lock);
		__d_lookup_done(dentry);
		spin_unlock(&dentry->d_lock);
	}
}

extern void dput(struct dentry *);

static inline bool d_managed(const struct dentry *dentry)
{
	return dentry->d_flags & DCACHE_MANAGED_DENTRY;
}

static inline bool d_mountpoint(const struct dentry *dentry)
{
	return dentry->d_flags & DCACHE_MOUNTED;
}

/*
 * Directory cache entry type accessor functions.
 */
static inline unsigned __d_entry_type(const struct dentry *dentry)
{
	return dentry->d_flags & DCACHE_ENTRY_TYPE;
}

static inline bool d_is_miss(const struct dentry *dentry)
{
	return __d_entry_type(dentry) == DCACHE_MISS_TYPE;
}

static inline bool d_is_whiteout(const struct dentry *dentry)
{
	return __d_entry_type(dentry) == DCACHE_WHITEOUT_TYPE;
}

static inline bool d_can_lookup(const struct dentry *dentry)
{
	return __d_entry_type(dentry) == DCACHE_DIRECTORY_TYPE;
}

static inline bool d_is_autodir(const struct dentry *dentry)
{
	return __d_entry_type(dentry) == DCACHE_AUTODIR_TYPE;
}

static inline bool d_is_dir(const struct dentry *dentry)
{
	return d_can_lookup(dentry) || d_is_autodir(dentry);
}

static inline bool d_is_symlink(const struct dentry *dentry)
{
	return __d_entry_type(dentry) == DCACHE_SYMLINK_TYPE;
}

static inline bool d_is_reg(const struct dentry *dentry)
{
	return __d_entry_type(dentry) == DCACHE_REGULAR_TYPE;
}

static inline bool d_is_special(const struct dentry *dentry)
{
	return __d_entry_type(dentry) == DCACHE_SPECIAL_TYPE;
}

static inline bool d_is_file(const struct dentry *dentry)
{
	return d_is_reg(dentry) || d_is_special(dentry);
}

static inline bool d_is_negative(const struct dentry *dentry)
{
	// TODO: check d_is_whiteout(dentry) also.
	return d_is_miss(dentry);
}

static inline bool d_flags_negative(unsigned flags)
{
	return (flags & DCACHE_ENTRY_TYPE) == DCACHE_MISS_TYPE;
}

static inline bool d_is_positive(const struct dentry *dentry)
{
	return !d_is_negative(dentry);
}

/**
 * d_really_is_negative - Determine if a dentry is really negative (ignoring fallthroughs)
 * @dentry: The dentry in question
 *
 * Returns true if the dentry represents either an absent name or a name that
 * doesn't map to an inode (ie. ->d_inode is NULL).  The dentry could represent
 * a true miss, a whiteout that isn't represented by a 0,0 chardev or a
 * fallthrough marker in an opaque directory.
 *
 * Note!  (1) This should be used *only* by a filesystem to examine its own
 * dentries.  It should not be used to look at some other filesystem's
 * dentries.  (2) It should also be used in combination with d_inode() to get
 * the inode.  (3) The dentry may have something attached to ->d_lower and the
 * type field of the flags may be set to something other than miss or whiteout.
 */
static inline bool d_really_is_negative(const struct dentry *dentry)
{
	return dentry->d_inode == NULL;
}

/**
 * d_really_is_positive - Determine if a dentry is really positive (ignoring fallthroughs)
 * @dentry: The dentry in question
 *
 * Returns true if the dentry represents a name that maps to an inode
 * (ie. ->d_inode is not NULL).  The dentry might still represent a whiteout if
 * that is represented on medium as a 0,0 chardev.
 *
 * Note!  (1) This should be used *only* by a filesystem to examine its own
 * dentries.  It should not be used to look at some other filesystem's
 * dentries.  (2) It should also be used in combination with d_inode() to get
 * the inode.
 */
static inline bool d_really_is_positive(const struct dentry *dentry)
{
	return dentry->d_inode != NULL;
}

static inline int simple_positive(const struct dentry *dentry)
{
	return d_really_is_positive(dentry) && !d_unhashed(dentry);
}

extern void d_set_fallthru(struct dentry *dentry);

static inline bool d_is_fallthru(const struct dentry *dentry)
{
	return dentry->d_flags & DCACHE_FALLTHRU;
}


extern int sysctl_vfs_cache_pressure;

#if 0//<TODO>
static inline unsigned long vfs_pressure_ratio(unsigned long val)
{
	return mult_frac(val, sysctl_vfs_cache_pressure, 100);
}
#endif

/**
 * d_inode - Get the actual inode of this dentry
 * @dentry: The dentry to query
 *
 * This is the helper normal filesystems should use to get at their own inodes
 * in their own dentries and ignore the layering superimposed upon them.
 */
static inline struct inode *d_inode(const struct dentry *dentry)
{
	return dentry->d_inode;
}

/**
 * d_inode_rcu - Get the actual inode of this dentry with READ_ONCE()
 * @dentry: The dentry to query
 *
 * This is the helper normal filesystems should use to get at their own inodes
 * in their own dentries and ignore the layering superimposed upon them.
 */
static inline struct inode *d_inode_rcu(const struct dentry *dentry)
{
	return READ_ONCE(dentry->d_inode);
}

/**
 * d_backing_inode - Get upper or lower inode we should be using
 * @upper: The upper layer
 *
 * This is the helper that should be used to get at the inode that will be used
 * if this dentry were to be opened as a file.  The inode may be on the upper
 * dentry or it may be on a lower dentry pinned by the upper.
 *
 * Normal filesystems should not use this to access their own inodes.
 */
static inline struct inode *d_backing_inode(const struct dentry *upper)
{
	struct inode *inode = upper->d_inode;

	return inode;
}

/**
 * d_backing_dentry - Get upper or lower dentry we should be using
 * @upper: The upper layer
 *
 * This is the helper that should be used to get the dentry of the inode that
 * will be used if this dentry were opened as a file.  It may be the upper
 * dentry or it may be a lower dentry pinned by the upper.
 *
 * Normal filesystems should not use this to access their own dentries.
 */
static inline struct dentry *d_backing_dentry(struct dentry *upper)
{
	return upper;
}

/**
 * d_real - Return the real dentry
 * @dentry: the dentry to query
 * @inode: inode to select the dentry from multiple layers (can be NULL)
 *
 * If dentry is on a union/overlay, then return the underlying, real dentry.
 * Otherwise return the dentry itself.
 *
 * See also: Documentation/filesystems/vfs.rst
 */
static inline struct dentry *d_real(struct dentry *dentry, const struct inode *inode)
{
	if (unlikely(dentry->d_flags & DCACHE_OP_REAL) )
		return dentry->d_op->d_real(dentry, inode);
	else
		return dentry;
}

/**
 * d_real_inode - Return the real inode
 * @dentry: The dentry to query
 *
 * If dentry is on a union/overlay, then return the underlying, real inode.
 * Otherwise return d_inode().
 */
static inline struct inode *d_real_inode(const struct dentry *dentry)
{
	/* This usage of d_real() results in const dentry */
	return d_backing_inode(d_real((struct dentry *) dentry, NULL));
}

struct name_snapshot {
	struct qstr name;
	unsigned char inline_name[DNAME_INLINE_LEN];
};
void take_dentry_name_snapshot(struct name_snapshot *, struct dentry *);
void release_dentry_name_snapshot(struct name_snapshot *);

#endif	/* __LINUX_DCACHE_H */

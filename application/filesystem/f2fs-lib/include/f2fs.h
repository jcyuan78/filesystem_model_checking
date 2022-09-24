///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
#pragma once
/* SPDX-License-Identifier: GPL-2.0 */
/*
 * fs/f2fs/f2fs.h
 *
 * Copyright (c) 2012 Samsung Electronics Co., Ltd.
 *             http://www.samsung.com/
 */

//#include <linux/uio.h>
//#include <linux/types.h>
//#include <linux/page-flags.h>
//#include <linux/buffer_head.h>
//#include <linux/slab.h>
//#include <linux/crc32.h>
//#include <linux/magic.h>
//#include <linux/kobject.h>
//#include <linux/sched.h>
//#include <linux/cred.h>
//#include <linux/vmalloc.h>
//#include <linux/bio.h>
//#include <linux/blkdev.h>
//#include <linux/quotaops.h>
//#include <linux/part_stat.h>
//#include <crypto/hash.h>
//
//#include <linux/fscrypt.h>
//#include <linux/fsverity.h>
//

#include "config.h"
#include <linux-fs-wrapper.h>
#include "f2fs_fs.h"

#include "control-thread.h"


struct wait_queue_head_t
{
	CRITICAL_SECTION lock;
	list_head head;
};

#define ALIGN_DOWN(addrs, size)	(((addrs) / (size)) * (size))



#ifdef CONFIG_F2FS_CHECK_FS
#define f2fs_bug_on(sbi, condition)	BUG_ON(condition)
#else
#define f2fs_bug_on(sbi, condition)					\
	do {								\
		if (/*WARN_ON*/(condition))					\
			sbi->set_sbi_flag(SBI_NEED_FSCK);		\
	} while (0)
#endif

enum {
	FAULT_KMALLOC,
	FAULT_KVMALLOC,
	FAULT_PAGE_ALLOC,
	FAULT_PAGE_GET,
	FAULT_ALLOC_NID,
	FAULT_ORPHAN,
	FAULT_BLOCK,
	FAULT_DIR_DEPTH,
	FAULT_EVICT_INODE,
	FAULT_TRUNCATE,
	FAULT_READ_IO,
	FAULT_CHECKPOINT,
	FAULT_DISCARD,
	FAULT_WRITE_IO,
	FAULT_MAX,
};

#ifdef CONFIG_F2FS_FAULT_INJECTION
#define F2FS_ALL_FAULT_TYPE		((1 << FAULT_MAX) - 1)

struct f2fs_fault_info {
	atomic_t inject_ops;
	unsigned int inject_rate;
	unsigned int inject_type;
};

extern const char *f2fs_fault_name[FAULT_MAX];
#define IS_FAULT_SET(fi, type) ((fi)->inject_type & (1 << (type)))
#endif

/*
 * For mount options
 */
#define F2FS_MOUNT_DISABLE_ROLL_FORWARD	0x00000002
#define F2FS_MOUNT_DISCARD		0x00000004
#define F2FS_MOUNT_NOHEAP		0x00000008
#define F2FS_MOUNT_XATTR_USER		0x00000010
#define F2FS_MOUNT_POSIX_ACL		0x00000020
#define F2FS_MOUNT_DISABLE_EXT_IDENTIFY	0x00000040
#define F2FS_MOUNT_INLINE_XATTR		0x00000080
#define F2FS_MOUNT_INLINE_DATA		0x00000100
#define F2FS_MOUNT_INLINE_DENTRY	0x00000200
#define F2FS_MOUNT_FLUSH_MERGE		0x00000400
#define F2FS_MOUNT_NOBARRIER		0x00000800
#define F2FS_MOUNT_FASTBOOT		0x00001000
#define F2FS_MOUNT_EXTENT_CACHE		0x00002000
#define F2FS_MOUNT_DATA_FLUSH		0x00008000
#define F2FS_MOUNT_FAULT_INJECTION	0x00010000
#define F2FS_MOUNT_USRQUOTA		0x00080000
#define F2FS_MOUNT_GRPQUOTA		0x00100000
#define F2FS_MOUNT_PRJQUOTA		0x00200000
#define F2FS_MOUNT_QUOTA		0x00400000
#define F2FS_MOUNT_INLINE_XATTR_SIZE	0x00800000
#define F2FS_MOUNT_RESERVE_ROOT		0x01000000
#define F2FS_MOUNT_DISABLE_CHECKPOINT	0x02000000
#define F2FS_MOUNT_NORECOVERY		0x04000000
#define F2FS_MOUNT_ATGC			0x08000000
#define F2FS_MOUNT_MERGE_CHECKPOINT	0x10000000
#define	F2FS_MOUNT_GC_MERGE		0x20000000

#define F2FS_OPTION(sbi)	((sbi)->mount_opt)
#define clear_opt(sbi, option)	(F2FS_OPTION(sbi).opt &= ~F2FS_MOUNT_##option)
#define set_opt(sbi, option)	(F2FS_OPTION(sbi).opt |= F2FS_MOUNT_##option)
#define test_opt(sbi, option)	(F2FS_OPTION(sbi).opt & F2FS_MOUNT_##option)

//#define ver_after(a, b)	(typecheck(unsigned long long, a) &&		\
//		typecheck(unsigned long long, b) &&			\
//		((long long)((a) - (b)) > 0))
inline bool ver_after(unsigned long long a, unsigned long long b)
{
	return (long long)((a)-(b)) > 0;
}

typedef u32 block_t;	/* should not change u32, since it is the on-disk block address format, __le32.	 */
typedef u32 nid_t;

#define COMPRESS_EXT_NUM		16

struct f2fs_mount_info {
	unsigned int opt;
	int write_io_size_bits;		/* Write IO size bits */
	block_t root_reserved_blocks;	/* root reserved blocks */
	//kuid_t s_resuid;		/* reserved blocks for uid */
	//kgid_t s_resgid;		/* reserved blocks for gid */
	int active_logs;		/* # of active logs */
	int inline_xattr_size;		/* inline xattr size */
#ifdef CONFIG_F2FS_FAULT_INJECTION
	struct f2fs_fault_info fault_info;	/* For fault injection */
#endif
#ifdef CONFIG_QUOTA
	/* Names of quota files with journalled quota */
	char *s_qf_names[MAXQUOTAS];
	int s_jquota_fmt;			/* Format of quota to use */
#endif
	/* For which write hints are passed down to block layer */
	int whint_mode;
	int alloc_mode;			/* segment allocation policy */
	int fsync_mode;			/* fsync policy */
	int fs_mode;			/* fs mode: LFS or ADAPTIVE */
	int bggc_mode;			/* bggc mode: off, on or sync */
	//struct fscrypt_dummy_policy dummy_enc_policy; /* test dummy encryption */
	block_t unusable_cap_perc;	/* percentage for cap */
	block_t unusable_cap;		/* Amount of space allowed to be
					 * unusable when disabling checkpoint
					 */

	/* For compression */
	unsigned char compress_algorithm;	/* algorithm type */
	unsigned char compress_log_size;	/* cluster log size */
	unsigned char compress_level;		/* compress level */
	bool compress_chksum;			/* compressed data chksum */
	unsigned char compress_ext_cnt;		/* extension count */
	int compress_mode;			/* compression mode */
	wchar_t extensions[COMPRESS_EXT_NUM][F2FS_EXTENSION_LEN];	/* extensions */
};

#define F2FS_FEATURE_ENCRYPT		0x0001
#define F2FS_FEATURE_BLKZONED		0x0002
#define F2FS_FEATURE_ATOMIC_WRITE	0x0004
#define F2FS_FEATURE_EXTRA_ATTR		0x0008
#define F2FS_FEATURE_PRJQUOTA		0x0010
#define F2FS_FEATURE_INODE_CHKSUM	0x0020
#define F2FS_FEATURE_FLEXIBLE_INLINE_XATTR	0x0040
#define F2FS_FEATURE_QUOTA_INO		0x0080
#define F2FS_FEATURE_INODE_CRTIME	0x0100
#define F2FS_FEATURE_LOST_FOUND		0x0200
#define F2FS_FEATURE_VERITY			0x0400
#define F2FS_FEATURE_SB_CHKSUM		0x0800
#define F2FS_FEATURE_CASEFOLD		0x1000
#define F2FS_FEATURE_COMPRESSION	0x2000

#define __F2FS_HAS_FEATURE(raw_super, mask)				\
	((raw_super->feature & cpu_to_le32(mask)) != 0)
#define F2FS_HAS_FEATURE(sbi, mask)	__F2FS_HAS_FEATURE(sbi->raw_super, mask)
#define F2FS_SET_FEATURE(sbi, mask)					\
	(sbi->raw_super->feature |= cpu_to_le32(mask))
#define F2FS_CLEAR_FEATURE(sbi, mask)					\
	(sbi->raw_super->feature &= ~cpu_to_le32(mask))

/*
 * Default values for user and/or group using reserved blocks
 */
#define	F2FS_DEF_RESUID		0
#define	F2FS_DEF_RESGID		0

/*
 * For checkpoint manager
 */
enum {
	NAT_BITMAP,
	SIT_BITMAP
};

#define	CP_UMOUNT	0x00000001
#define	CP_FASTBOOT	0x00000002
#define	CP_SYNC		0x00000004
#define	CP_RECOVERY	0x00000008
#define	CP_DISCARD	0x00000010
#define CP_TRIMMED	0x00000020
#define CP_PAUSE	0x00000040
#define CP_RESIZE 	0x00000080

#define MAX_DISCARD_BLOCKS(sbi)		BLKS_PER_SEC(sbi)
#define DEF_MAX_DISCARD_REQUEST		8	/* issue 8 discards per round */
#define DEF_MIN_DISCARD_ISSUE_TIME	50	/* 50 ms, if exists */
#define DEF_MID_DISCARD_ISSUE_TIME	500	/* 500 ms, if device busy */
#define DEF_MAX_DISCARD_ISSUE_TIME	60000	/* 60 s, if no candidates */
#define DEF_DISCARD_URGENT_UTIL		80	/* do more discard over 80% */
#define DEF_CP_INTERVAL			60	/* 60 secs */
#define DEF_IDLE_INTERVAL		5	/* 5 secs */
#define DEF_DISABLE_INTERVAL		5	/* 5 secs */
#define DEF_DISABLE_QUICK_INTERVAL	1	/* 1 secs */
#define DEF_UMOUNT_DISCARD_TIMEOUT	5	/* 5 secs */

struct cp_control {
	int reason;
	__u64 trim_start;
	__u64 trim_end;
	__u64 trim_minlen;
};

/*
 * indicate meta/data type
 */
enum {
	META_CP,
	META_NAT,
	META_SIT,
	META_SSA,
	META_MAX,
	META_POR,
	DATA_GENERIC,		/* check range only */
	DATA_GENERIC_ENHANCE,	/* strong check on range and segment bitmap */
	DATA_GENERIC_ENHANCE_READ,	/*
					 * strong check on range and segment
					 * bitmap but no warning due to race
					 * condition of read on truncated area
					 * by extent_cache
					 */
	META_GENERIC,
};

/* for the list of ino */
enum {
	ORPHAN_INO,		/* for orphan ino list */
	APPEND_INO,		/* for append ino list */
	UPDATE_INO,		/* for update ino list */
	TRANS_DIR_INO,		/* for trasactions dir ino list */
	FLUSH_INO,		/* for multiple device flushing */
	MAX_INO_ENTRY,		/* max. list */
};

struct ino_entry {
	struct list_head list;		/* list head */
	nid_t ino;			/* inode number */
	unsigned int dirty_device;	/* dirty device bitmap */
};

/* for the list of inodes to be GCed */
struct inode_entry {
	struct list_head list;	/* list head */
	struct inode *inode;	/* vfs inode pointer */
};

struct fsync_node_entry {
	struct list_head list;	/* list head */
	struct page *page;	/* warm node page pointer */
	unsigned int seq_id;	/* sequence id */
};

struct ckpt_req 
{
public:
	ckpt_req(bool need_delete):m_need_delete(need_delete), ret(0), queue_time(0)
	{
		llnode.next = NULL;
		m_complete = CreateEvent(NULL, TRUE, FALSE, NULL);
		//memset(req, 0, sizeof(struct ckpt_req));
		//init_completion(&req->wait);
#if 1 //TODO
		queue_time = jcvos::GetTimeStamp(); // ktime_get();
#endif 
	}
	~ckpt_req(void) { CloseHandle(m_complete); }
public:
	void Complete(void)	{		SetEvent(m_complete);	}
	void WaitForComplete(void) { WaitForSingleObject(m_complete, INFINITE); }
public:
	llist_node llnode;			/* llist_node to be linked in wait queue */
	int ret;					/* return code of checkpoint */
	ktime_t queue_time;			/* request queued time */
	bool m_need_delete;			// 如果是用new创建的，需要delete

protected:
//	struct completion wait;		/* completion for checkpoint done */
	HANDLE m_complete;			// Complete以后Event Set
};

struct f2fs_sb_info;

struct ckpt_req_control : public CControlThread
{
public:
	//ckpt_req_control(void) {
	//	int dymmy = 0;
	//}
	ckpt_req_control(f2fs_sb_info * sbi);
	virtual ~ckpt_req_control(void);

public:
	void f2fs_init_ckpt_req_control(f2fs_sb_info* sbi);
	virtual DWORD Run(void) { return issue_checkpoint_thread(); }
	void WakeUp(void) { SetEvent(m_wait); }
//	DWORD WaitForIo(DWORD timeout) { return WaitForSingleObject(m_wait, timeout); }
	//<YUAN> request的队列处理私有化
	void AddRequest(ckpt_req* req);

public:
	DWORD issue_checkpoint_thread(void);
	void __checkpoint_and_complete_reqs(void);
//	int f2fs_issue_checkpoint(void);
	void flush_remained_ckpt_reqs(ckpt_req* wait_req);
	void f2fs_wait_on_all_pages(int type);
//	int f2fs_write_checkpoint(cp_control* cpc);
	int __write_checkpoint_sync();

protected:
//	struct task_struct *f2fs_issue_ckpt;	/* checkpoint task */
	int ckpt_thread_ioprio;			/* checkpoint merge thread ioprio */
#if 0
	wait_queue_head_t ckpt_wait_queue;	/* waiting queue for wake-up */
#endif
	atomic_t issued_ckpt;			/* # of actually issued ckpts */
	atomic_t total_ckpt;			/* # of total ckpts */
	CRITICAL_SECTION stat_lock;		/* lock for below checkpoint time stats */
	unsigned int cur_time;			/* cur wait time in msec for currently issued checkpoint */
	unsigned int peak_time;			/* peak wait time in msec until now */

	f2fs_sb_info* m_sbi;

protected:
	// 用event代替wait queue
	HANDLE m_wait;			//用于等待IO完成
	CRITICAL_SECTION m_req_list_lock;
	atomic_t queued_ckpt;			/* # of queued ckpts */
	llist_head issue_list;			/* list for command issue */

protected:
	//f2fs_checkpoint* m_ckpt = NULL;		/* raw checkpoint pointer */
	//int cur_cp_pack = 0;			/* remain current cp pack */
	//CRITICAL_SECTION cp_lock;			/* for flag in ckpt */
	//inode* meta_inode = NULL;		/* cache meta blocks */
	//rw_semaphore  cp_global_sem;	/* checkpoint procedure lock */
	//rw_semaphore  cp_rwsem;		/* blocking FS operations */
	//rw_semaphore  node_write;		/* locking node writes */
	//rw_semaphore  node_change;	/* locking node change */
};

#include "discard-control.h"

class f2fs_inode_info;

/* for the list of fsync inodes, used only during recovery */
struct fsync_inode_entry 
{
	list_head list;				/* list head */
	f2fs_inode_info *inode;		/* vfs inode pointer */
	block_t blkaddr;			/* block address locating the last fsync */
	block_t last_dentry;		/* block address locating the last dentry */
};

#define nats_in_cursum(jnl)		(le16_to_cpu((jnl)->n_nats))
#define sits_in_cursum(jnl)		(le16_to_cpu((jnl)->n_sits))

#define nat_in_journal(jnl, i)		((jnl)->nat_j.entries[i].ne)
#define nid_in_journal(jnl, i)		((jnl)->nat_j.entries[i].nid)
#define sit_in_journal(jnl, i)		((jnl)->sit_j.entries[i].se)
#define segno_in_journal(jnl, i)	((jnl)->sit_j.entries[i].segno)

#define MAX_NAT_JENTRIES(jnl)	(NAT_JOURNAL_ENTRIES - nats_in_cursum(jnl))
#define MAX_SIT_JENTRIES(jnl)	(SIT_JOURNAL_ENTRIES - sits_in_cursum(jnl))

static inline int update_nats_in_cursum(struct f2fs_journal *journal, int i)
{
	int before = nats_in_cursum(journal);
	journal->n_nats = cpu_to_le16(before + i);
	return before;
}

static inline int update_sits_in_cursum(struct f2fs_journal *journal, int i)
{
	int before = sits_in_cursum(journal);
	journal->n_sits = cpu_to_le16(before + i);
	return before;
}

static inline bool __has_cursum_space(f2fs_journal *journal, int size, int type)
{
	if (type == NAT_JOURNAL) return size <= MAX_NAT_JENTRIES(journal);
	return size <= MAX_SIT_JENTRIES(journal);
}

/* for inline stuff */
#define DEF_INLINE_RESERVED_SIZE	1

static inline int __get_extra_isize(f2fs_inode* inode)
{
	if (f2fs_has_extra_isize(inode))	return le16_to_cpu(inode->_u._s.i_extra_isize) / sizeof(__le32);
	return 0;
}

static inline int get_extra_isize(inode* inode);
static inline int get_inline_xattr_addrs(inode *inode);


inline size_t MAX_INLINE_DATA(inode* inode)
{
	return (sizeof(__le32) * (CUR_ADDRS_PER_INODE(inode) - get_inline_xattr_addrs(inode) - DEF_INLINE_RESERVED_SIZE));
}

inline size_t MAX_INLINE_DATA(f2fs_inode* node);
//{
//	return (sizeof(__le32) * (DEF_ADDRS_PER_INODE - get_inline_xattr_addrs(&node->i) - get_extra_isize(node) - DEF_INLINE_RESERVED_SIZE)));
//}

/* for inline dir */
#define NR_INLINE_DENTRY(inode)	(MAX_INLINE_DATA(inode) * BITS_PER_BYTE / \
				((SIZE_OF_DIR_ENTRY + F2FS_SLOT_LEN) * \
				BITS_PER_BYTE + 1))

#define INLINE_DENTRY_BITMAP_SIZE(inode) DIV_ROUND_UP<size_t>(NR_INLINE_DENTRY(inode), BITS_PER_BYTE)

#define INLINE_RESERVED_SIZE(inode)	(MAX_INLINE_DATA(inode) - \
				((SIZE_OF_DIR_ENTRY + F2FS_SLOT_LEN) * \
				NR_INLINE_DENTRY(inode) + \
				INLINE_DENTRY_BITMAP_SIZE(inode)))

/* For INODE and NODE manager */
/* for directory operations */
struct f2fs_filename 
{
	f2fs_filename(void):usr_fname(NULL)
	{
#ifdef CONFIG_FS_ENCRYPTION
		crypto_buf.name = NULL;
#endif
#ifdef CONFIG_UNICODE
		cf_name.name = NULL;
#endif

	}
	~f2fs_filename(void)
	{
#ifdef CONFIG_FS_ENCRYPTION
		//kfree(fname->crypto_buf.name);
		delete [] crypto_buf.name
		crypto_buf.name = NULL;
#endif
#ifdef CONFIG_UNICODE
		//kfree(fname->cf_name.name);
		delete[] cf_name.name;
		cf_name.name = NULL;
#endif

	}
	/* The filename the user specified.  This is NULL for some filesystem-internal operations, e.g. converting an inline directory to a non-inline one, or roll-forward recovering an encrypted dentry.	 */
	const qstr *usr_fname;
	/* The on-disk filename. For encrypted directories, this is encrypted. This may be NULL for lookups in an encrypted dir without the key.*/
	fscrypt_str disk_name;
	/* The dirhash of this filename */
	f2fs_hash_t hash;

#ifdef CONFIG_FS_ENCRYPTION
	/* For lookups in encrypted directories: either the buffer backing disk_name, or a buffer that holds the decoded 
	no-key name. */
	struct fscrypt_str crypto_buf;
#endif
#ifdef CONFIG_UNICODE
	/* For casefolded directories: the casefolded name, but it's left NULL if the original name is not valid Unicode, if the directory is both casefolded and encrypted and its encryption key is unavailable, or if the filesystem is doing an internal operation where usr_fname is also NULL.  In all these cases we fall back to treating the name as an opaque byte sequence. */
	fscrypt_str cf_name;
#endif
};

struct f2fs_dentry_ptr {
	struct inode *inode;
	void *bitmap;
	f2fs_dir_entry *dentry;
	union
	{
		__u8(*filename)[F2FS_SLOT_LEN];
		void* _f;
	};
//	wchar_t (*filename)[F2FS_SLOT_LEN];
	size_t max;
	size_t nr_bitmap;
};

static inline void make_dentry_ptr_block(inode *iinode, f2fs_dentry_ptr *d, f2fs_dentry_block *t)
{
	d->inode = iinode;
	d->max = NR_DENTRY_IN_BLOCK;
	d->nr_bitmap = SIZE_OF_DENTRY_BITMAP;
	d->bitmap = t->dentry_bitmap;
	d->dentry = t->dentry;
	d->filename = t->filename;
}

static inline void make_dentry_ptr_inline(inode *iinode, f2fs_dentry_ptr *d, void *t)
{
	size_t entry_cnt = NR_INLINE_DENTRY(iinode);
	size_t bitmap_size = INLINE_DENTRY_BITMAP_SIZE(iinode);
	size_t reserved_size = INLINE_RESERVED_SIZE(iinode);

	d->inode = iinode;
	d->max = entry_cnt;
	d->nr_bitmap = bitmap_size;
	d->bitmap = t;
	d->dentry = reinterpret_cast<f2fs_dir_entry*>((BYTE*)t + bitmap_size + reserved_size);
//	memcpy_s(d->filename, 8, (BYTE*)t + bitmap_size + reserved_size + SIZE_OF_DIR_ENTRY * entry_cnt, 8);
//	d->filename = reinterpret_cast<__u8**>(t) + bitmap_size + reserved_size + SIZE_OF_DIR_ENTRY * entry_cnt;
	d->_f = reinterpret_cast<BYTE*>(t) + bitmap_size + reserved_size + SIZE_OF_DIR_ENTRY * entry_cnt;
}
/*
 * XATTR_NODE_OFFSET stores xattrs to one node block per file keeping -1
 * as its node offset to distinguish from index node blocks.
 * But some bits are used to mark the node block.
 */
#define XATTR_NODE_OFFSET	((((unsigned int)-1) << OFFSET_BIT_SHIFT) \
				>> OFFSET_BIT_SHIFT)
enum {
	ALLOC_NODE,			/* allocate a new node page if needed */
	LOOKUP_NODE,			/* look up a node without readahead */
	LOOKUP_NODE_RA,			/*
					 * look up a node with readahead called
					 * by get_data_block.
					 */
};

#define DEFAULT_RETRY_IO_COUNT	8	/* maximum retry read IO count */

/* congestion wait timeout value, default: 20ms */
#define	DEFAULT_IO_TIMEOUT	(20)

/* maximum retry quota flush count */
#define DEFAULT_RETRY_QUOTA_FLUSH_COUNT		8

#define F2FS_LINK_MAX	0xffffffff	/* maximum link count per file */

#define MAX_DIR_RA_PAGES	4	/* maximum ra pages of dir */

/* for in-memory extent cache entry */
#define F2FS_MIN_EXTENT_LEN	64	/* minimum extent length */

/* number of extent info in extent cache we try to shrink */
#define EXTENT_CACHE_SHRINK_NUMBER	128

struct rb_entry 
{
	struct rb_node rb_node;		/* rb node located in rb-tree */
	union
	{
		struct 
		{
			unsigned int ofs;	/* start offset of the entry */
			unsigned int len;	/* length of the entry */
		} _s;
		unsigned long long key;		/* 64-bits key */
	} __packed;
};


struct extent_info {
	unsigned int fofs;		/* start offset in a file */
	unsigned int len;		/* length of the extent */
	u32 blk;			/* start block address of the extent */
};

struct extent_node {
	struct rb_node rb_node;		/* rb node located in rb-tree */
	struct extent_info ei;		/* extent info */
	struct list_head list;		/* node in global extent list of sbi */
	struct extent_tree *et;		/* extent tree pointer */
};

struct extent_tree 
{
	nid_t ino;			/* inode number */
	struct rb_root_cached root;	/* root of extent info rb-tree */
	struct extent_node *cached_en;	/* recently accessed extent node */
	struct extent_info largest;	/* largested extent info */
	struct list_head list;		/* to be used by sbi->zombie_list */
	rwlock_t lock;			/* protect extent info rb-tree */
	atomic_t node_cnt;		/* # of extent node in rb-tree*/
	bool largest_updated;		/* largest extent updated */
};

/*
 * This structure is taken from ext4_map_blocks.
 *
 * Note that, however, f2fs uses NEW and MAPPED flags for f2fs_map_blocks().
 */
#define F2FS_MAP_NEW		(1 << BH_New)
#define F2FS_MAP_MAPPED		(1 << BH_Mapped)
#define F2FS_MAP_UNWRITTEN	(1 << BH_Unwritten)
#define F2FS_MAP_FLAGS		(F2FS_MAP_NEW | F2FS_MAP_MAPPED | F2FS_MAP_UNWRITTEN)

/// <summary>
/// 一个从文件的逻辑地址到物理地址的映射。这个映射可以包含连续的多个block。
/// </summary>
struct f2fs_map_blocks
{
	block_t m_pblk;			// 物理block地址
	block_t m_lblk;			// 逻辑block的地址
	unsigned int m_len;		// block数量
	unsigned int m_flags;
	pgoff_t *m_next_pgofs;		/* point next possible non-hole pgofs */
	pgoff_t *m_next_extent;		/* point to next possible extent */
	int m_seg_type;
	bool m_may_create;		/* indicate it is from write path */
};

/* for flag in get_data_block */
enum {
	F2FS_GET_BLOCK_DEFAULT,
	F2FS_GET_BLOCK_FIEMAP,
	F2FS_GET_BLOCK_BMAP,
	F2FS_GET_BLOCK_DIO,
	F2FS_GET_BLOCK_PRE_DIO,
	F2FS_GET_BLOCK_PRE_AIO,
	F2FS_GET_BLOCK_PRECACHE,
};

/*
 * i_advise uses FADVISE_XXX_BIT. We can add additional hints later.
 */
#define FADVISE_COLD_BIT	0x01
#define FADVISE_LOST_PINO_BIT	0x02
#define FADVISE_ENCRYPT_BIT	0x04
#define FADVISE_ENC_NAME_BIT	0x08
#define FADVISE_KEEP_SIZE_BIT	0x10
#define FADVISE_HOT_BIT		0x20
#define FADVISE_VERITY_BIT	0x40

#define FADVISE_MODIFIABLE_BITS	(FADVISE_COLD_BIT | FADVISE_HOT_BIT)

#define file_is_cold(node)	is_file(node, FADVISE_COLD_BIT)
#define file_set_cold(node)	node->set_file(FADVISE_COLD_BIT)
#define file_clear_cold(node)	node->clear_file(FADVISE_COLD_BIT)

#define file_wrong_pino(node)	is_file(node, FADVISE_LOST_PINO_BIT)
#define file_lost_pino(node)	node->set_file(FADVISE_LOST_PINO_BIT)
#define file_got_pino(node)		node->clear_file(FADVISE_LOST_PINO_BIT)

#define file_is_encrypt(node)	is_file(node, FADVISE_ENCRYPT_BIT)
#define file_set_encrypt(node)	set_file(node, FADVISE_ENCRYPT_BIT)

#define file_enc_name(node)		is_file(node, FADVISE_ENC_NAME_BIT)
#define file_set_enc_name(node) node->set_file(FADVISE_ENC_NAME_BIT)

#define file_keep_isize(node)	is_file(node, FADVISE_KEEP_SIZE_BIT)
#define file_set_keep_isize(node) node->set_file(FADVISE_KEEP_SIZE_BIT)

#define file_is_hot(node)	is_file(node, FADVISE_HOT_BIT)
#define file_set_hot(node)	node->set_file(FADVISE_HOT_BIT)
#define file_clear_hot(node)	node->clear_file(FADVISE_HOT_BIT)

#define file_is_verity(node)	is_file(node, FADVISE_VERITY_BIT)
#define file_set_verity(node)	set_file(node, FADVISE_VERITY_BIT)

#define DEF_DIR_LEVEL		0

enum {
	GC_FAILURE_PIN,
	GC_FAILURE_ATOMIC,
	MAX_GC_FAILURE
};

/* used for f2fs_inode_info->flags */
enum {
	FI_NEW_INODE,		/* indicate newly allocated inode */
	FI_DIRTY_INODE,		/* indicate inode is dirty or not */
	FI_AUTO_RECOVER,	/* indicate inode is recoverable */
	FI_DIRTY_DIR,		/* indicate directory has dirty pages */
	FI_INC_LINK,		/* need to increment i_nlink */
	FI_ACL_MODE,		/* indicate acl mode */
	FI_NO_ALLOC,		/* should not allocate any blocks */
	FI_FREE_NID,		/* free allocated nide */
	FI_NO_EXTENT,		/* not to use the extent cache */
	FI_INLINE_XATTR,	/* used for inline xattr */
	FI_INLINE_DATA,		/* used for inline data*/
	FI_INLINE_DENTRY,	/* used for inline dentry */
	FI_APPEND_WRITE,	/* inode has appended data */
	FI_UPDATE_WRITE,	/* inode has in-place-update data */
	FI_NEED_IPU,		/* used for ipu per file */
	FI_ATOMIC_FILE,		/* indicate atomic file */
	FI_ATOMIC_COMMIT,	/* indicate the state of atomical committing */
	FI_VOLATILE_FILE,	/* indicate volatile file */
	FI_FIRST_BLOCK_WRITTEN,	/* indicate #0 data block was written */
	FI_DROP_CACHE,		/* drop dirty page cache */
	FI_DATA_EXIST,		/* indicate data exists */
	FI_INLINE_DOTS,		/* indicate inline dot dentries */
	FI_DO_DEFRAG,		/* indicate defragment is running */
	FI_DIRTY_FILE,		/* indicate regular/symlink has dirty pages */
	FI_NO_PREALLOC,		/* indicate skipped preallocated blocks */
	FI_HOT_DATA,		/* indicate file is hot */
	FI_EXTRA_ATTR,		/* indicate file has extra attribute */
	FI_PROJ_INHERIT,	/* indicate file inherits projectid */
	FI_PIN_FILE,		/* indicate file should not be gced */
	FI_ATOMIC_REVOKE_REQUEST, /* request to drop atomic data */
	FI_VERITY_IN_PROGRESS,	/* building fs-verity Merkle tree */
	FI_COMPRESSED_FILE,	/* indicate file's data can be compressed */
	FI_COMPRESS_CORRUPT,	/* indicate compressed cluster is corrupted */
	FI_MMAP_FILE,		/* indicate file was mmapped */
	FI_ENABLE_COMPRESS,	/* enable compression in "user" compression mode */
	FI_MAX,			/* max flag, never be used */
};

#include "f2fs-inode.h"


static inline void get_extent_info(struct extent_info *ext,	struct f2fs_extent *i_ext)
{
	ext->fofs = le32_to_cpu(i_ext->fofs);
	ext->blk = le32_to_cpu(i_ext->blk);
	ext->len = le32_to_cpu(i_ext->len);
}

static inline void set_raw_extent(extent_info *ext, f2fs_extent *i_ext)
{
	i_ext->fofs = cpu_to_le32(ext->fofs);
	i_ext->blk = cpu_to_le32(ext->blk);
	i_ext->len = cpu_to_le32(ext->len);
}

static inline void set_extent_info(struct extent_info *ei, unsigned int fofs,
						u32 blk, unsigned int len)
{
	ei->fofs = fofs;
	ei->blk = blk;
	ei->len = len;
}

static inline bool __is_discard_mergeable(struct discard_info *back,
			struct discard_info *front, unsigned int max_len)
{
	return (back->lstart + back->len == front->lstart) &&
		(back->len + front->len <= max_len);
}

static inline bool __is_discard_back_mergeable(struct discard_info *cur,
			struct discard_info *back, unsigned int max_len)
{
	return __is_discard_mergeable(back, cur, max_len);
}

static inline bool __is_discard_front_mergeable(struct discard_info *cur,
			struct discard_info *front, unsigned int max_len)
{
	return __is_discard_mergeable(cur, front, max_len);
}

static inline bool __is_extent_mergeable(struct extent_info *back,
						struct extent_info *front)
{
	return (back->fofs + back->len == front->fofs &&
			back->blk + back->len == front->blk);
}

static inline bool __is_back_mergeable(struct extent_info *cur,
						struct extent_info *back)
{
	return __is_extent_mergeable(back, cur);
}

static inline bool __is_front_mergeable(struct extent_info *cur,
						struct extent_info *front)
{
	return __is_extent_mergeable(cur, front);
}

//extern void f2fs_mark_inode_dirty_sync(struct inode *inode, bool sync);
static inline void __try_update_largest_extent(struct extent_tree *et, struct extent_node *en)
{
	if (en->ei.len > et->largest.len) {
		et->largest = en->ei;
		et->largest_updated = true;
	}
}

static inline f2fs_inode_info* F2FS_I(inode* inode)
{
	//	return container_of<f2fs_inode_info>(inode, offsetof(f2fs_inode_info, vfs_inode) );
	f2fs_inode_info* fi = dynamic_cast<f2fs_inode_info*>(inode);
	JCASSERT(fi);
	return fi;
}

#include "node-manager.h"

/* this structure is used as one of function parameters. all the information are dedicated to a given direct node
   block determined by the data offset in a file. */
struct dnode_of_data 
{
public:
	inline void set_new_dnode(inode* nn, page* ipage, page* npage, nid_t nid)
	{
		set_new_dnode(F2FS_I(nn), ipage, npage, nid);
	}
	inline void set_new_dnode(f2fs_inode_info* node, page* ipage, page* npage, nid_t _nid)
	{
		JCASSERT(node);
//		memset(this, 0, sizeof(dnode_of_data));
		inode = node;
		inode_page = ipage;
		node_page = npage;
		nid = _nid;
		ofs_in_node = 0;
		inode_page_locked = false;
		node_changed = false;
		cur_level = 0;
		max_level = 0;
		data_blkaddr = 0;
	}

public:

	f2fs_inode_info *inode;		/* vfs inode pointer */
	struct page *inode_page;	/* its inode page, NULL is possible */
	struct page *node_page;		/* cached direct node page */
	nid_t nid;			/* node id of the direct node block */
	unsigned int ofs_in_node;	/* data offset in the node page */
	bool inode_page_locked;		/* inode page is locked or not */
	bool node_changed;		/* is node block changed */
	char cur_level;			/* level of hole node page */
	char max_level;			/* level of current page located */
	block_t	data_blkaddr;		/* block address of the node block */
};

//static inline void set_new_dnode(struct dnode_of_data *dn, struct inode *inode,
//		struct page *ipage, struct page *npage, nid_t nid)
//{
//	memset(dn, 0, sizeof(*dn));
//	dn->inode = inode;
//	dn->inode_page = ipage;
//	dn->node_page = npage;
//	dn->nid = nid;
//}





#include "segment-manager.h"

/* For superblock */


/*
 * The below are the page types of bios used in submit_bio().
 * The available types are:
 * DATA			User data pages. It operates as async mode.
 * NODE			Node pages. It operates as async mode.
 * META			FS metadata pages such as SIT, NAT, CP.
 * NR_PAGE_TYPE		The number of page types.
 * META_FLUSH		Make sure the previous pages are written
 *			with waiting the bio's completion
 * ...			Only can be used with META.
 */
#define PAGE_TYPE_OF_BIO(type)	((type) > META ? META : (type))
enum page_type {
	DATA,
	NODE,
	META,
	NR_PAGE_TYPE,
	META_FLUSH,
	INMEM,		/* the below types are used by tracepoints only. */
	INMEM_DROP,
	INMEM_INVALIDATE,
	INMEM_REVOKE,
	IPU,
	OPU,
};

enum temp_type {
	HOT = 0,	/* must be zero for meta bio */
	WARM,
	COLD,
	NR_TEMP_TYPE,
};

enum need_lock_type {
	LOCK_REQ = 0,
	LOCK_DONE,
	LOCK_RETRY,
};

enum cp_reason_type {
	CP_NO_NEEDED,
	CP_NON_REGULAR,
	CP_COMPRESSED,
	CP_HARDLINK,
	CP_SB_NEED_CP,
	CP_WRONG_PINO,
	CP_NO_SPC_ROLL,
	CP_NODE_NEED_CP,
	CP_FASTBOOT_MODE,
	CP_SPEC_LOG_NUM,
	CP_RECOVER_DIR,
};

enum iostat_type {
	/* WRITE IO */
	APP_DIRECT_IO,			/* app direct write IOs */
	APP_BUFFERED_IO,		/* app buffered write IOs */
	APP_WRITE_IO,			/* app write IOs */
	APP_MAPPED_IO,			/* app mapped IOs */
	FS_DATA_IO,			/* data IOs from kworker/fsync/reclaimer */
	FS_NODE_IO,			/* node IOs from kworker/fsync/reclaimer */
	FS_META_IO,			/* meta IOs from kworker/reclaimer */
	FS_GC_DATA_IO,			/* data IOs from forground gc */
	FS_GC_NODE_IO,			/* node IOs from forground gc */
	FS_CP_DATA_IO,			/* data IOs from checkpoint */
	FS_CP_NODE_IO,			/* node IOs from checkpoint */
	FS_CP_META_IO,			/* meta IOs from checkpoint */

	/* READ IO */
	APP_DIRECT_READ_IO,		/* app direct read IOs */
	APP_BUFFERED_READ_IO,		/* app buffered read IOs */
	APP_READ_IO,			/* app read IOs */
	APP_MAPPED_READ_IO,		/* app mapped read IOs */
	FS_DATA_READ_IO,		/* data read IOs */
	FS_GDATA_READ_IO,		/* data read IOs from background gc */
	FS_CDATA_READ_IO,		/* compressed data read IOs */
	FS_NODE_READ_IO,		/* node read IOs */
	FS_META_READ_IO,		/* meta read IOs */

	/* other */
	FS_DISCARD,			/* discard */
	NR_IO_TYPE,
};

struct f2fs_io_info 
{
	struct f2fs_sb_info *sbi=0;	/* f2fs_sb_info pointer */
	nid_t ino=0;				/* inode number */
	enum page_type type;	/* contains DATA/NODE/META/META_FLUSH */
	enum temp_type temp;	/* contains HOT/WARM/COLD */
	int op=0;				/* contains REQ_OP_ */
	int op_flags=0;			/* req_flag_bits */
	block_t new_blkaddr=0;	/* new block address to be written */
	block_t old_blkaddr=0;	/* old block address before Cow */
	struct page *page=0;	/* page to be written */
	struct page *encrypted_page=0;	/* encrypted page */
	struct page *compressed_page=0;	/* compressed page */
	struct list_head list;		/* serialize IOs */
	bool submitted=0;		/* indicate IO submission */
	int need_lock=0;		/* indicate we need to lock cp_rwsem */
	bool in_list=0;		/* indicate fio is in io_list */
	bool is_por=0;		/* indicate IO is from recovery or not */
	bool retry=0;		/* need to reallocate block address */
	int compr_blocks=0;	/* # of compressed block addresses */
	bool encrypted=0;		/* indicate file is encrypted */
	enum iostat_type io_type;	/* io type */
	struct writeback_control *io_wbc=0; /* writeback control */
	struct bio **bio=0;		/* bio for ipu */
	sector_t *last_block=0;		/* last block number in bio */
	unsigned char version=0;		/* version of the node */
};

struct bio_entry {
	struct bio *bio;
	struct list_head list;
};

#define is_read_io(rw) ((rw) == READ)
struct f2fs_bio_info
{
	f2fs_sb_info *sbi;	/* f2fs superblock */
	struct bio *bio;		/* bios to merge */
	sector_t last_block_in_bio;	/* last block number */
	f2fs_io_info fio;	/* store buffered io info. */
	SRWLOCK /*rw_semaphore*/  io_rwsem;	/* blocking op for bio */
	CRITICAL_SECTION io_lock;		/* serialize DATA/NODE IOs */
	struct list_head io_list;	/* track fios */
	struct list_head bio_list;	/* bio entry list head */
	SRWLOCK /*rw_semaphore*/  bio_list_lock;	/* lock to protect bio entry list */
};

#define FDEV(i)				(sbi->devs[i])
#define RDEV(i)				(raw_super->devs[i])
struct f2fs_dev_info 
{
	//block_device *bdev;
	IVirtualDisk* m_disk;
	char path[MAX_PATH_LEN];
	unsigned int total_segments;
	block_t start_blk;
	block_t end_blk;
#ifdef CONFIG_BLK_DEV_ZONED
	unsigned int nr_blkz;		/* Total number of zones */
	unsigned long *blkz_seq;	/* Bitmap indicating sequential zones */
	block_t *zone_capacity_blocks;  /* Array of zone capacity in blks */
#endif
};




/* for inner inode cache management */
struct inode_management 
{
	radix_tree_root /*<ino_entry>*/ ino_root;		/* ino entry array */
	CRITICAL_SECTION ino_lock;		/* for ino entry lock */
	struct list_head ino_list;		/* inode list head */
	unsigned long ino_num;			/* number of entries */
};

/* for GC_AT */
struct atgc_management 
{
	bool atgc_enabled;			/* ATGC is enabled or not */
	struct rb_root_cached root;		/* root of victim rb-tree */
	struct list_head victim_list;		/* linked with all victim entries */
	unsigned int victim_count;		/* victim count in rb-tree */
	unsigned int candidate_ratio;		/* candidate ratio */
	unsigned int max_candidate_count;	/* max candidate count */
	unsigned int age_weight;		/* age weight, vblock_weight = 100 - age_weight */
	unsigned long long age_threshold;	/* age threshold */
};


/* For s_flag in struct f2fs_sb_info */
enum {
	SBI_IS_DIRTY,				/* dirty flag for checkpoint */
	SBI_IS_CLOSE,				/* specify unmounting */
	SBI_NEED_FSCK,				/* need fsck.f2fs to fix */
	SBI_POR_DOING,				/* recovery is doing or not */
	SBI_NEED_SB_WRITE,			/* need to recover superblock */
	SBI_NEED_CP,				/* need to checkpoint */
	SBI_IS_SHUTDOWN,			/* shutdown by ioctl */
	SBI_IS_RECOVERED,			/* recovered orphan/data */
	SBI_CP_DISABLED,			/* CP was disabled last mount */
	SBI_CP_DISABLED_QUICK,			/* CP was disabled quickly */
	SBI_QUOTA_NEED_FLUSH,			/* need to flush quota info in CP */
	SBI_QUOTA_SKIP_FLUSH,			/* skip flushing quota in current CP */
	SBI_QUOTA_NEED_REPAIR,			/* quota file may be corrupted */
	SBI_IS_RESIZEFS,			/* resizefs is in process */
};

enum {
	CP_TIME,
	REQ_TIME,
	DISCARD_TIME,
	GC_TIME,
	DISABLE_TIME,
	UMOUNT_DISCARD_TIMEOUT,
	MAX_TIME,
};

enum {
	GC_NORMAL,
	GC_IDLE_CB,
	GC_IDLE_GREEDY,
	GC_IDLE_AT,
	GC_URGENT_HIGH,
	GC_URGENT_LOW,
};

enum {
	BGGC_MODE_ON,		/* background gc is on */
	BGGC_MODE_OFF,		/* background gc is off */
	BGGC_MODE_SYNC,		/*
				 * background gc is on, migrating blocks
				 * like foreground gc
				 */
};

enum {
	FS_MODE_ADAPTIVE,	/* use both lfs/ssr allocation */
	FS_MODE_LFS,		/* use lfs allocation only */
};

enum {
	WHINT_MODE_OFF,		/* not pass down write hints */
	WHINT_MODE_USER,	/* try to pass down hints given by users */
	WHINT_MODE_FS,		/* pass down hints with F2FS policy */
};

enum {
	ALLOC_MODE_DEFAULT,	/* stay default */
	ALLOC_MODE_REUSE,	/* reuse segments as much as possible */
};

enum fsync_mode {
	FSYNC_MODE_POSIX,	/* fsync follows posix semantics */
	FSYNC_MODE_STRICT,	/* fsync behaves in line with ext4 */
	FSYNC_MODE_NOBARRIER,	/* fsync behaves nobarrier based on posix */
};

enum {
	COMPR_MODE_FS,		/*
				 * automatically compress compression
				 * enabled files
				 */
	COMPR_MODE_USER,	/*
				 * automatical compression is disabled.
				 * user can control the file compression
				 * using ioctls
				 */
};

/*
 * this value is set in page as a private data which indicate that
 * the page is atomically written, and it is in inmem_pages list.
 */
#define ATOMIC_WRITTEN_PAGE		((unsigned long)-1)
#define DUMMY_WRITTEN_PAGE		((unsigned long)-2)

#define IS_ATOMIC_WRITTEN_PAGE(page)			\
		(page_private(page) == ATOMIC_WRITTEN_PAGE)
#define IS_DUMMY_WRITTEN_PAGE(page)			\
		(page_private(page) == DUMMY_WRITTEN_PAGE)

/* For compression */
enum compress_algorithm_type {
	COMPRESS_LZO,
	COMPRESS_LZ4,
	COMPRESS_ZSTD,
	COMPRESS_LZORLE,
	COMPRESS_MAX,
};

enum compress_flag {
	COMPRESS_CHKSUM,
	COMPRESS_MAX_FLAG,
};

//#define COMPRESS_DATA_RESERVED_SIZE		4
//struct compress_data {
//	__le32 clen;			/* compressed data size */
//	__le32 chksum;			/* compressed data chksum */
//	__le32 reserved[COMPRESS_DATA_RESERVED_SIZE];	/* reserved */
//	u8 cdata[];			/* compressed data */
//};

#define COMPRESS_HEADER_SIZE	(sizeof(struct compress_data))

#define F2FS_COMPRESSED_PAGE_MAGIC	0xF5F2C000

#define	COMPRESS_LEVEL_OFFSET	8

/* compress context */
//struct compress_ctx {
//	struct inode *inode;		/* inode the context belong to */
//	pgoff_t cluster_idx;		/* cluster index number */
//	unsigned int cluster_size;	/* page count in cluster */
//	unsigned int log_cluster_size;	/* log of cluster size */
//	struct page **rpages;		/* pages store raw data in cluster */
//	unsigned int nr_rpages;		/* total page number in rpages */
//	struct page **cpages;		/* pages store compressed data in cluster */
//	unsigned int nr_cpages;		/* total page number in cpages */
//	void *rbuf;			/* virtual mapped address on rpages */
//	struct compress_data *cbuf;	/* virtual mapped address on cpages */
//	size_t rlen;			/* valid data length in rbuf */
//	size_t clen;			/* valid data length in cbuf */
//	void *private1;			/* payload buffer for specified compression algorithm */
//	void *private2;			/* extra payload buffer */
//};

/* compress context for write IO path */
struct compress_io_ctx {
	u32 magic;			/* magic number to indicate page is compressed */
	struct inode *inode;		/* inode the context belong to */
	struct page **rpages;		/* pages store raw data in cluster */
	unsigned int nr_rpages;		/* total page number in rpages */
	atomic_t pending_pages;		/* in-flight compressed page count */
};

#if 0
/* Context for decompressing one cluster on the read IO path */
struct decompress_io_ctx {
	u32 magic;			/* magic number to indicate page is compressed */
	struct inode *inode;		/* inode the context belong to */
	pgoff_t cluster_idx;		/* cluster index number */
	unsigned int cluster_size;	/* page count in cluster */
	unsigned int log_cluster_size;	/* log of cluster size */
	struct page **rpages;		/* pages store raw data in cluster */
	unsigned int nr_rpages;		/* total page number in rpages */
	struct page **cpages;		/* pages store compressed data in cluster */
	unsigned int nr_cpages;		/* total page number in cpages */
	struct page **tpages;		/* temp pages to pad holes in cluster */
	void *rbuf;			/* virtual mapped address on rpages */
	struct compress_data *cbuf;	/* virtual mapped address on cpages */
	size_t rlen;			/* valid data length in rbuf */
	size_t clen;			/* valid data length in cbuf */

	/*
	 * The number of compressed pages remaining to be read in this cluster.
	 * This is initially nr_cpages.  It is decremented by 1 each time a page
	 * has been read (or failed to be read).  When it reaches 0, the cluster
	 * is decompressed (or an error is reported).
	 *
	 * If an error occurs before all the pages have been submitted for I/O,
	 * then this will never reach 0.  In this case the I/O submitter is
	 * responsible for calling f2fs_decompress_end_io() instead.
	 */
	atomic_t remaining_pages;

	/*
	 * Number of references to this decompress_io_ctx.
	 *
	 * One reference is held for I/O completion.  This reference is dropped
	 * after the pagecache pages are updated and unlocked -- either after
	 * decompression (and verity if enabled), or after an error.
	 *
	 * In addition, each compressed page holds a reference while it is in a
	 * bio.  These references are necessary prevent compressed pages from
	 * being freed while they are still in a bio.
	 */
	refcount_t refcnt;

	bool failed;			/* IO error occurred before decompression? */
	bool need_verity;		/* need fs-verity verification after decompression? */
	void *private1;			/* payload buffer for specified decompression algorithm */
	void *private2;			/* extra payload buffer */
	struct work_struct verity_work;	/* work to verify the decompressed pages */
};

#endif

#define NULL_CLUSTER			((unsigned int)(~0))
#define MIN_COMPRESS_LOG_SIZE		2
#define MAX_COMPRESS_LOG_SIZE		8
#define MAX_COMPRESS_WINDOW_SIZE(log_size)	((PAGE_SIZE) << (log_size))

inline bool __is_set_ckpt_flags(f2fs_checkpoint* cp, unsigned int f)
{
	unsigned int ckpt_flags = le32_to_cpu(cp->ckpt_flags);
	return ckpt_flags & f;
}

#include "f2fs-super-block.h"

struct f2fs_private_dio {
	struct inode *inode;
	void *orig_private;
#if 0
	bio_end_io_t *orig_end_io;
#endif
	bool write;
};

#ifdef CONFIG_F2FS_FAULT_INJECTION
#define f2fs_show_injection_info(sbi, type)					\
	printk_ratelimited("%sF2FS-fs (%s) : inject %s in %s of %pS\n",	\
		KERN_INFO, sbi->s_id,				\
		f2fs_fault_name[type],					\
		__func__, __builtin_return_address(0))
static inline bool time_to_inject(struct f2fs_sb_info *sbi, int type)
{
	struct f2fs_fault_info *ffi = &F2FS_OPTION(sbi).fault_info;

	if (!ffi->inject_rate)
		return false;

	if (!IS_FAULT_SET(ffi, type))
		return false;

	atomic_inc(&ffi->inject_ops);
	if (atomic_read(&ffi->inject_ops) >= ffi->inject_rate) {
		atomic_set(&ffi->inject_ops, 0);
		return true;
	}
	return false;
}
#else
#define f2fs_show_injection_info(sbi, type) do { } while (0)
static inline bool time_to_inject(struct f2fs_sb_info *sbi, int type)
{
	return false;
}
#endif


//<YUAN> move to f2fs_sb_info member
//static inline void f2fs_update_time(struct f2fs_sb_info *sbi, int type)
//{
////	unsigned long now = jiffies;
//	LONGLONG now = jcvos::GetTimeStamp();
//	sbi->last_time[type] = now;
//
//	/* DISCARD_TIME and GC_TIME are based on REQ_TIME */
//	if (type == REQ_TIME) {
//		sbi->last_time[DISCARD_TIME] = now;
//		sbi->last_time[GC_TIME] = now;
//	}
//}

static inline bool f2fs_time_over(struct f2fs_sb_info *sbi, int type)
{
//	unsigned long interval = sbi->interval_time[type] * HZ;
	LONGLONG interval = sbi->interval_time[type];
	LONGLONG now = jcvos::GetTimeStamp();

	return time_after(now, sbi->last_time[type] + interval);
}
#if 0
#endif

static inline unsigned int f2fs_time_to_wait(struct f2fs_sb_info *sbi,	int type)
{
	unsigned int wait_ms = 0;
#if 0
	unsigned long interval = sbi->interval_time[type] * HZ;
	long delta;

	delta = (sbi->last_time[type] + interval) - jiffies;
	if (delta > 0)	wait_ms = jiffies_to_msecs(delta);
#else
	JCASSERT(0);
#endif
	return wait_ms;
}

/*
 * Inline functions
 */
//static inline u32 __f2fs_crc32(struct f2fs_sb_info *sbi, u32 crc, const void *address, unsigned int length)
//{
//	struct 
//	{
//		struct shash_desc shash;
//		char ctx[4];
//	} desc;
//	int err;
//
//	BUG_ON(crypto_shash_descsize(sbi->s_chksum_driver) != sizeof(desc.ctx));
//
//	desc.shash.tfm = sbi->s_chksum_driver;
//	*(u32 *)desc.ctx = crc;
//
//	err = crypto_shash_update(&desc.shash, address, length);
//	BUG_ON(err);
//
//	return *(u32 *)desc.ctx;
//}

//<YUAN> defined in libf2fs.cpp
//extern UINT32 f2fs_cal_crc32(UINT32 crc, const void* buf, int len);


static inline u32 f2fs_crc32(f2fs_sb_info *sbi, const void *address, size_t length)
{
//	return __f2fs_crc32(sbi, F2FS_SUPER_MAGIC, address, length);
	return f2fs_cal_crc32(F2FS_SUPER_MAGIC, address, length);
}

static inline bool f2fs_crc_valid(f2fs_sb_info *sbi, __u32 blk_crc, void *buf, size_t buf_size)
{
	return f2fs_crc32(sbi, buf, buf_size) == blk_crc;
}

static inline u32 f2fs_chksum(struct f2fs_sb_info *sbi, u32 crc, const void *address, unsigned int length)
{
	//return __f2fs_crc32(sbi, crc, address, length);
	return f2fs_cal_crc32(crc, address, length);
}



static inline struct f2fs_sb_info *F2FS_SB(super_block *sb)
{
	return dynamic_cast<f2fs_sb_info*>(sb);
}

static inline struct f2fs_sb_info *F2FS_I_SB(struct inode *inode)
{
	return F2FS_SB(inode->i_sb);
}

static inline struct f2fs_sb_info *F2FS_M_SB(address_space *mapping)
{
	return F2FS_I_SB(mapping->host);
}

static inline struct f2fs_sb_info *F2FS_P_SB(struct page *page)
{
//	return F2FS_M_SB(page_file_mapping(page));
	return F2FS_M_SB(page->mapping);
}







static inline struct f2fs_node *F2FS_NODE(struct page *page)
{
	//return (struct f2fs_node *)page_address(page);
	return (struct f2fs_node *)(page_address<f2fs_node>(page));
}

static inline f2fs_inode *F2FS_INODE(page *page)
{
//	return &((struct f2fs_node *)page_address(page))->i;
	return &((f2fs_node *)page_address<f2fs_node>(page))->i;
}

static inline struct f2fs_nm_info *NM_I(struct f2fs_sb_info *sbi)
{
	return (struct f2fs_nm_info *)(sbi->nm_info);
}






static inline struct free_segmap_info *FREE_I(struct f2fs_sb_info *sbi)
{
	return (struct free_segmap_info *)(sbi->sm_info->free_info);
}

static inline struct dirty_seglist_info *DIRTY_I(struct f2fs_sb_info *sbi)
{
	return (struct dirty_seglist_info *)(sbi->sm_info->dirty_info);
}

static inline address_space *META_MAPPING(struct f2fs_sb_info *sbi)
{
	return sbi->meta_inode->i_mapping;
}


static inline address_space *NODE_MAPPING(struct f2fs_sb_info *sbi)
{
	return sbi->node_inode->i_mapping;
}

// <YUAN> move to member
//static inline bool is_sbi_flag_set(f2fs_sb_info *sbi, unsigned int type)
//{
//	return test_bit(type, &sbi->s_flag);
//}

static inline void set_sbi_flag(struct f2fs_sb_info *sbi, unsigned int type)
{
	set_bit(type, &sbi->s_flag);
}

static inline void clear_sbi_flag(struct f2fs_sb_info *sbi, unsigned int type)
{
	clear_bit(type, &sbi->s_flag);
}

static inline unsigned long long cur_cp_version(struct f2fs_checkpoint *cp)
{
	return le64_to_cpu(cp->checkpoint_ver);
}

static inline unsigned long f2fs_qf_ino(struct super_block *sb, int type)
{
	if (type < F2FS_MAX_QUOTAS)
		return le32_to_cpu(F2FS_SB(sb)->raw_super->qf_ino[type]);
	return 0;
}

static inline __u64 cur_cp_crc(struct f2fs_checkpoint *cp)
{
	size_t crc_offset = le32_to_cpu(cp->checksum_offset);
	return le32_to_cpu(*((__le32 *)((unsigned char *)cp + crc_offset)));
}


#if 0
#endif

static inline void __set_ckpt_flags(f2fs_checkpoint *cp, unsigned int f)
{
	unsigned int ckpt_flags;
	ckpt_flags = le32_to_cpu(cp->ckpt_flags);
	ckpt_flags |= f;
	cp->ckpt_flags = cpu_to_le32(ckpt_flags);
}

static inline void set_ckpt_flags(f2fs_sb_info *sbi, unsigned int f)
{
//	unsigned long flags;
	spin_lock_irqsave(&sbi->cp_lock, flags);
	__set_ckpt_flags(sbi->F2FS_CKPT(), f);
	spin_unlock_irqrestore(&sbi->cp_lock, flags);
}

static inline void __clear_ckpt_flags(struct f2fs_checkpoint *cp, unsigned int f)
{
	unsigned int ckpt_flags;

	ckpt_flags = le32_to_cpu(cp->ckpt_flags);
	ckpt_flags &= (~f);
	cp->ckpt_flags = cpu_to_le32(ckpt_flags);
}

static inline void clear_ckpt_flags(struct f2fs_sb_info *sbi, unsigned int f)
{
//	unsigned long flags; <UNUSE>

	spin_lock_irqsave(&sbi->cp_lock, flags);
	__clear_ckpt_flags(sbi->F2FS_CKPT(), f);
	spin_unlock_irqrestore(&sbi->cp_lock, flags);
}

static inline void disable_nat_bits(f2fs_sb_info *sbi, bool lock)
{
//	unsigned long flags;	<UNUSE>
	unsigned char *nat_bits;

	/* In order to re-enable nat_bits we need to call fsck.f2fs by set_sbi_flag(sbi, SBI_NEED_FSCK). 
	But it may give huge cost, so let's rely on regular fsck or unclean shutdown. */

	if (lock)		spin_lock_irqsave(&sbi->cp_lock, flags);
	__clear_ckpt_flags(sbi->F2FS_CKPT(), CP_NAT_BITS_FLAG);
	nat_bits = NM_I(sbi)->nat_bits;
	NM_I(sbi)->nat_bits = NULL;
	if (lock) spin_unlock_irqrestore(&sbi->cp_lock, flags);

//	kvfree(nat_bits);
	delete[] nat_bits;
}
static inline bool enabled_nat_bits(f2fs_sb_info *sbi, cp_control *cpc)
{
	bool set = sbi->is_set_ckpt_flags(CP_NAT_BITS_FLAG);
	return (cpc) ? (cpc->reason & CP_UMOUNT) && set : set;
}



static inline int f2fs_trylock_op(struct f2fs_sb_info *sbi)
{
	return down_read_trylock(&sbi->cp_rwsem);
}



static inline void f2fs_lock_all(struct f2fs_sb_info *sbi)
{
	down_write(&sbi->cp_rwsem);
}

static inline void f2fs_unlock_all(struct f2fs_sb_info *sbi)
{
	up_write(&sbi->cp_rwsem);
}

//<YUAN> move to member 
//static inline int __get_cp_reason(struct f2fs_sb_info *sbi)
//{
//	int reason = CP_SYNC;
//
//	if (test_opt(sbi, FASTBOOT))
//		reason = CP_FASTBOOT;
//	if (sbi->is_sbi_flag_set( SBI_IS_CLOSE))
//		reason = CP_UMOUNT;
//	return reason;
//}

static inline bool __remain_node_summaries(int reason)
{
	return (reason & (CP_UMOUNT | CP_FASTBOOT));
}


/* Check whether the inode has blocks or not */
static inline int F2FS_HAS_BLOCKS(struct inode *inode)
{
	block_t xattr_block = F2FS_I(inode)->i_xattr_nid ? 1 : 0;

	return (inode->i_blocks >> F2FS_LOG_SECTORS_PER_BLOCK) > xattr_block;
}

static inline bool f2fs_has_xattr_block(unsigned int ofs)
{
	return ofs == XATTR_NODE_OFFSET;
}

static inline bool __allow_reserved_blocks(f2fs_sb_info *sbi, inode *inode, bool cap)
{
	if (!inode)		return true;
	if (!test_opt(sbi, RESERVE_ROOT))		return false;
	if (IS_NOQUOTA(inode))		return true;
	//if (uid_eq(F2FS_OPTION(sbi).s_resuid, current_fsuid()))		return true;
	//if (!gid_eq(F2FS_OPTION(sbi).s_resgid, GLOBAL_ROOT_GID) &&	in_group_p(F2FS_OPTION(sbi).s_resgid))		return true;
#if 0
	if (cap && capable(CAP_SYS_RESOURCE))		return true;
#endif
	return false;
}

static inline void f2fs_i_blocks_write(f2fs_inode_info*, block_t, bool, bool);

static inline int inc_valid_block_count(f2fs_sb_info *sbi, inode *inode, blkcnt_t *count)
{
	blkcnt_t diff = 0, release = 0;
	block_t avail_user_block_count;
	int ret;

	ret = dquot_reserve_block(inode, *count);
	if (ret)	return ret;

	if (time_to_inject(sbi, FAULT_BLOCK)) {
		f2fs_show_injection_info(sbi, FAULT_BLOCK);
		release = *count;
		goto release_quota;
	}

	/* let's increase this in prior to actual block count change in order for f2fs_sync_file to avoid data races when deciding checkpoint. */
	percpu_counter_add(&sbi->alloc_valid_block_count, (*count));

	spin_lock(&sbi->stat_lock);
	sbi->total_valid_block_count += (block_t)(*count);
	avail_user_block_count = sbi->user_block_count -
					sbi->current_reserved_blocks;

	if (!__allow_reserved_blocks(sbi, inode, true))
		avail_user_block_count -= F2FS_OPTION(sbi).root_reserved_blocks;
	if (unlikely(sbi->is_sbi_flag_set( SBI_CP_DISABLED))) {
		if (avail_user_block_count > sbi->unusable_block_count)
			avail_user_block_count -= sbi->unusable_block_count;
		else
			avail_user_block_count = 0;
	}
	if (unlikely(sbi->total_valid_block_count > avail_user_block_count)) {
		diff = sbi->total_valid_block_count - avail_user_block_count;
		if (diff > *count)
			diff = *count;
		*count -= diff;
		release = diff;
		sbi->total_valid_block_count -= boost::numeric_cast<block_t>(diff);
		if (!*count) 
		{
			spin_unlock(&sbi->stat_lock);
			goto enospc;
		}
	}
	spin_unlock(&sbi->stat_lock);

	if (unlikely(release))
	{
		percpu_counter_sub(&sbi->alloc_valid_block_count, release);
		dquot_release_reservation_block(inode, release);
	}
	f2fs_i_blocks_write(F2FS_I(inode), boost::numeric_cast<block_t>(*count), true, true);
	return 0;

enospc:
	percpu_counter_sub(&sbi->alloc_valid_block_count, release);
release_quota:
	dquot_release_reservation_block(inode, release);
	return -ENOSPC;
}
#if 0
__printf(2, 3)
void f2fs_printk(struct f2fs_sb_info *sbi, const char *fmt, ...);

#endif

//#define f2fs_err(sbi, fmt, ...)						\
//	f2fs_printk(sbi, KERN_ERR fmt, ##__VA_ARGS__)
#define f2fs_err(sbi, ...)		LOG_ERROR(__VA_ARGS__)		
#define f2fs_warn(sbi, fmt, ...)	LOG_WARNING(fmt, __VA_ARGS__)
//	f2fs_printk(sbi, KERN_WARNING fmt, ##__VA_ARGS__)
#define f2fs_notice(sbi, fmt, ...)	LOG_NOTICE(fmt, __VA_ARGS__)
	//f2fs_printk(sbi, KERN_NOTICE fmt, ##__VA_ARGS__)

#define f2fs_info(sbi, ...) LOG_DEBUG(__VA_ARGS__)
	//f2fs_printk(sbi, KERN_INFO fmt, ##__VA_ARGS__)
#define f2fs_debug(sbi, fmt, ...)	LOG_DEBUG(fmt, __VA_ARGS__)
//	f2fs_printk(sbi, KERN_DEBUG fmt, ##__VA_ARGS__)



//static inline void inc_page_count(struct f2fs_sb_info *sbi, int count_type)
//{
//	atomic_inc(&sbi->nr_pages[count_type]);
//
//	if (count_type == F2FS_DIRTY_DENTS ||
//			count_type == F2FS_DIRTY_NODES ||
//			count_type == F2FS_DIRTY_META ||
//			count_type == F2FS_DIRTY_QDATA ||
//			count_type == F2FS_DIRTY_IMETA)
//		set_sbi_flag(sbi, SBI_IS_DIRTY);
//}

static inline void inode_inc_dirty_pages(inode *iinode)
{
	atomic_inc(&F2FS_I(iinode)->dirty_pages);
	f2fs_sb_info* sbi = F2FS_I_SB(iinode);
	sbi->inc_page_count(S_ISDIR(iinode->i_mode) ? F2FS_DIRTY_DENTS : F2FS_DIRTY_DATA);
	if (IS_NOQUOTA(iinode)) 	sbi->inc_page_count(F2FS_DIRTY_QDATA);
}


//static inline void dec_page_count(f2fs_sb_info *sbi, int count_type)
//{
//	atomic_dec(&sbi->nr_pages[count_type]);
//}

static inline void inode_dec_dirty_pages(inode *iinode)
{
	if (!S_ISDIR(iinode->i_mode) && !S_ISREG(iinode->i_mode) && !S_ISLNK(iinode->i_mode))
		return;

	atomic_dec(&F2FS_I(iinode)->dirty_pages);
	f2fs_sb_info* sbi = F2FS_I_SB(iinode);
	sbi->dec_page_count(S_ISDIR(iinode->i_mode) ? 	F2FS_DIRTY_DENTS : F2FS_DIRTY_DATA);
	if (IS_NOQUOTA(iinode)) sbi->dec_page_count(F2FS_DIRTY_QDATA);
}

//static inline s64 get_pages(f2fs_sb_info *sbi, int count_type)
//{
//	return atomic_read(&sbi->nr_pages[count_type]);
//}


static inline int get_dirty_pages(struct inode *inode)
{
	return atomic_read(&F2FS_I(inode)->dirty_pages);
}


static inline int get_blocktype_secs(struct f2fs_sb_info *sbi, int block_type)
{
	unsigned int pages_per_sec = sbi->segs_per_sec * sbi->blocks_per_seg;
	unsigned int segs = boost::numeric_cast<UINT>((sbi->get_pages( block_type) + pages_per_sec - 1) >> sbi->log_blocks_per_seg);
	return segs / sbi->segs_per_sec;
}



#if 0

static inline block_t discard_blocks(struct f2fs_sb_info *sbi)
{
	return sbi->discard_blks;
}


#endif




static inline block_t __start_cp_next_addr(struct f2fs_sb_info *sbi)
{
	block_t start_addr = le32_to_cpu(sbi->F2FS_RAW_SUPER()->cp_blkaddr);

	if (sbi->cur_cp_pack == 1)
		start_addr += sbi->blocks_per_seg;
	return start_addr;
}

static inline void __set_cp_next_pack(struct f2fs_sb_info *sbi)
{
	sbi->cur_cp_pack = (sbi->cur_cp_pack == 1) ? 2 : 1;
}

static inline block_t __start_sum_addr(struct f2fs_sb_info *sbi)
{
	return le32_to_cpu(sbi->F2FS_CKPT()->cp_pack_start_sum);
}
//static inline int inc_valid_node_count(f2fs_sb_info *sbi, struct inode *inode, bool is_inode)
//{
////	LOG_DEBUG(L"increase valid node count, inode=0x%X", inode->i_ino);
//	block_t	valid_block_count;
//	unsigned int valid_node_count, user_block_count;
//	int err;
//	f2fs_inode_info* fi = F2FS_I(inode);
//
//	if (is_inode) 
//	{
//		if (inode) 
//		{
//			err = dquot_alloc_inode(inode);
//			if (err)	return err;
//		}
//	} else 
//	{
//		err = dquot_reserve_block(inode, 1);
//		if (err)	return err;
//	}
//
//	if (time_to_inject(sbi, FAULT_BLOCK))
//	{
//		f2fs_show_injection_info(sbi, FAULT_BLOCK);
//		goto enospc;
//	}
//
//	spin_lock(&sbi->stat_lock);
//
//	valid_block_count = sbi->total_valid_block_count +	sbi->current_reserved_blocks + 1;
//
//	if (!__allow_reserved_blocks(sbi, inode, false))
//		valid_block_count += F2FS_OPTION(sbi).root_reserved_blocks;
//	user_block_count = sbi->user_block_count;
//	if (unlikely(sbi->is_sbi_flag_set( SBI_CP_DISABLED)))
//		user_block_count -= sbi->unusable_block_count;
//
//	if (unlikely(valid_block_count > user_block_count)) 
//	{
//		spin_unlock(&sbi->stat_lock);
//		goto enospc;
//	}
//
//	valid_node_count = sbi->total_valid_node_count + 1;
//	if (unlikely(valid_node_count > sbi->total_node_count)) 
//	{
//		spin_unlock(&sbi->stat_lock);
//		goto enospc;
//	}
//
//	sbi->total_valid_node_count++;
//	sbi->total_valid_block_count++;
//	spin_unlock(&sbi->stat_lock);
//
//	if (inode) 
//	{
//		if (is_inode) fi->f2fs_mark_inode_dirty_sync( true);
//		else			f2fs_i_blocks_write(fi, 1, true, true);
//	}
//
//	percpu_counter_inc(&sbi->alloc_valid_block_count);
//	return 0;
//
//enospc:
//	if (is_inode)
//	{
//		if (inode)	dquot_free_inode(inode);
//	} 
//	else	{	dquot_release_reservation_block(inode, 1);	}
//	return -ENOSPC;
//}
//
//static inline void dec_valid_node_count(f2fs_sb_info *sbi, struct inode *inode, bool is_inode)
//{
////	LOG_DEBUG(L"decrease valid node count, inode=0x%X", inode->i_ino);
//
//	spin_lock(&sbi->stat_lock);
//
//	f2fs_bug_on(sbi, !sbi->total_valid_block_count);
//	f2fs_bug_on(sbi, !sbi->total_valid_node_count);
//
//	sbi->total_valid_node_count--;
//	sbi->total_valid_block_count--;
//	if (sbi->reserved_blocks &&	sbi->current_reserved_blocks < sbi->reserved_blocks)
//		sbi->current_reserved_blocks++;
//
//	spin_unlock(&sbi->stat_lock);
//
//	if (is_inode)
//	{
//		dquot_free_inode(inode);
//	}
//	else
//	{
//		if (unlikely(inode->i_blocks == 0)) {
//			//f2fs_warn(sbi, L"dec_valid_node_count: inconsistent i_blocks, ino:%lu, iblocks:%llu",
//			//	  inode->i_ino,
//			//	  (unsigned long long)inode->i_blocks);
//			sbi->set_sbi_flag(SBI_NEED_FSCK);
//			return;
//		}
//		f2fs_i_blocks_write(F2FS_I(inode), 1, false, true);
//	}
//}


static inline void inc_valid_inode_count(struct f2fs_sb_info *sbi)
{
	percpu_counter_inc(&sbi->total_valid_inode_count);
}

static inline void dec_valid_inode_count(struct f2fs_sb_info *sbi)
{
	percpu_counter_dec(&sbi->total_valid_inode_count);
}

//<YUAN> move to f2fs_sb_info member
//static inline s64 valid_inode_count(struct f2fs_sb_info *sbi)
//{
//	return percpu_counter_sum_positive(&sbi->total_valid_inode_count);
//}

static inline page *f2fs_grab_cache_page(address_space *mapping, pgoff_t index, bool for_write)
{
#if 0 //<YUAN> not support fault injection
	struct page *page;
	if (IS_ENABLED(CONFIG_F2FS_FAULT_INJECTION)) 
	{
		if (!for_write)		page = find_get_page_flags(mapping, index, FGP_LOCK | FGP_ACCESSED);
		else				page = find_lock_page(mapping, index);
		if (page)			return page;

		if (time_to_inject(F2FS_M_SB(mapping), FAULT_PAGE_ALLOC)) 
		{
			f2fs_show_injection_info(F2FS_M_SB(mapping), FAULT_PAGE_ALLOC);
			return NULL;
		}
	}
#endif

	if (!for_write)		return grab_cache_page(mapping, index);
	return grab_cache_page_write_begin(mapping, index, AOP_FLAG_NOFS);
}

static inline page *f2fs_pagecache_get_page( address_space *mapping, pgoff_t index,	int fgp_flags, gfp_t gfp_mask)
{
	if (time_to_inject(F2FS_M_SB(mapping), FAULT_PAGE_GET)) 
	{
		f2fs_show_injection_info(F2FS_M_SB(mapping), FAULT_PAGE_GET);
		return NULL;
	}
	return pagecache_get_page(mapping, index, fgp_flags, gfp_mask);
}

static inline void f2fs_copy_page(page *src, page *dst)
{
	char *src_kaddr = page_address<char>(src);
	char *dst_kaddr = page_address<char>(dst);

	memcpy(dst_kaddr, src_kaddr, PAGE_SIZE);
	//kunmap(dst);
	//kunmap(src);
}

static inline void f2fs_put_page(page *ppage, int unlock)
{
	if (!ppage)	return;

	if (unlock) 
	{
		f2fs_bug_on(F2FS_P_SB(ppage), !PageLocked(ppage));
		unlock_page(ppage);
	}
	ppage->put_page();
}

static inline void f2fs_put_dnode(struct dnode_of_data *dn)
{
	if (dn->node_page) f2fs_put_page(dn->node_page, 1);
	if (dn->inode_page && dn->node_page != dn->inode_page)
		f2fs_put_page(dn->inode_page, 0);
	dn->node_page = NULL;
	dn->inode_page = NULL;
}
#if 0

static inline struct kmem_cache *f2fs_kmem_cache_create(const char *name,
					size_t size)
{
	return kmem_cache_create(name, size, 0, SLAB_RECLAIM_ACCOUNT, NULL);
}
#endif

template <typename T>
static inline T *f2fs_kmem_cache_alloc(kmem_cache *cachep, gfp_t flags)
{
	//void *entry;
	//entry = kmem_cache_alloc(cachep, flags);
	//if (!entry)	entry = kmem_cache_alloc(cachep, flags | __GFP_NOFAIL);
	//return entry;
	return new T;
}

template <typename T> T* kmem_cache_alloc(kmem_cache* cahce, gfp_t flag)
{
	return new T;
}

template <typename T>
void kmem_cache_free(kmem_cache* free_nid_slab, T* e)
{
	delete e;
}


static inline bool is_inflight_io(struct f2fs_sb_info *sbi, int type)
{
	if (sbi->get_pages( F2FS_RD_DATA) || sbi->get_pages( F2FS_RD_NODE) ||
		sbi->get_pages( F2FS_RD_META) || sbi->get_pages( F2FS_WB_DATA) ||
		sbi->get_pages( F2FS_WB_CP_DATA) ||
		sbi->get_pages( F2FS_DIO_READ) ||
		sbi->get_pages( F2FS_DIO_WRITE))
		return true;

	if (type != DISCARD_TIME && sbi->SM_I() && sbi->SM_I()->dcc_info &&	sbi->SM_I()->dcc_info->atomic_read_queued())
		return true;

	if (sbi->SM_I() && sbi->SM_I()->fcc_info && sbi->SM_I()->fcc_info->atomic_read_queued())
		return true;
	return false;
}

static inline bool is_idle(struct f2fs_sb_info *sbi, int type)
{
	if (sbi->gc_mode == GC_URGENT_HIGH)
		return true;

	if (is_inflight_io(sbi, type))
		return false;

	if (sbi->gc_mode == GC_URGENT_LOW &&
			(type == DISCARD_TIME || type == GC_TIME))
		return true;

	return f2fs_time_over(sbi, type);
}

template <typename T>
static inline void f2fs_radix_tree_insert(radix_tree_root *root, unsigned long index, T *item)
{
	radix_tree_insert(root, index, item);
	//while (radix_tree_insert(root, index, item))
	//	cond_resched();
}

#define RAW_IS_INODE(p)	((p)->footer.nid == (p)->footer.ino)

static inline bool IS_INODE(page *page)
{
	f2fs_node *p = F2FS_NODE(page);
	return RAW_IS_INODE(p);
}

static inline int offset_in_addr(struct f2fs_inode *i)
{
	return (i->i_inline & F2FS_EXTRA_ATTR) ? (le16_to_cpu(i->_u._s.i_extra_isize) / sizeof(__le32)) : 0;
}

static inline __le32 *blkaddr_in_node(struct f2fs_node *node)
{
	return RAW_IS_INODE(node) ? node->i._u.i_addr : node->dn.addr;
}

static inline int f2fs_has_extra_attr(struct inode *inode);

static inline block_t data_blkaddr(struct inode *inode,	struct page *node_page, unsigned int offset)
{
	f2fs_node *raw_node;
	__le32 *addr_array;
	int base = 0;
	bool is_inode = IS_INODE(node_page);
	raw_node = F2FS_NODE(node_page);

	if (is_inode) 
	{
		if (!inode)	/* from GC path only */		base = offset_in_addr(&raw_node->i);
		else if (f2fs_has_extra_attr(inode))	base = get_extra_isize(inode);
	}
	addr_array = blkaddr_in_node(raw_node);
	return le32_to_cpu(addr_array[base + offset]);
}

static inline block_t f2fs_data_blkaddr(dnode_of_data *dn)
{
	return data_blkaddr(dn->inode, dn->node_page, dn->ofs_in_node);
}


static inline int f2fs_test_bit(unsigned int nr, BYTE *addr)
{
	int mask;
	addr += (nr >> 3);
	mask = 1 << (7 - (nr & 0x07));
	return mask & *addr;
}

static inline void f2fs_set_bit(unsigned int nr, char *addr)
{
	int mask;

	addr += (nr >> 3);
	mask = 1 << (7 - (nr & 0x07));
	*addr |= mask;
}

static inline void f2fs_clear_bit(unsigned int nr, char *addr)
{
	int mask;

	addr += (nr >> 3);
	mask = 1 << (7 - (nr & 0x07));
	*addr &= ~mask;
}

static inline int f2fs_test_and_set_bit(unsigned int nr, char *addr)
{
	int mask;
	int ret;

	addr += (nr >> 3);
	mask = 1 << (7 - (nr & 0x07));
	ret = mask & *addr;
	*addr |= mask;
	return ret;
}

static inline int f2fs_test_and_clear_bit(unsigned int nr, char *addr)
{
	int mask;
	int ret;

	addr += (nr >> 3);
	mask = 1 << (7 - (nr & 0x07));
	ret = mask & *addr;
	*addr &= ~mask;
	return ret;
}

static inline void f2fs_change_bit(unsigned int nr, BYTE *addr)
{
	int mask;

	addr += (nr >> 3);
	mask = 1 << (7 - (nr & 0x07));
	*addr ^= mask;
}


/*
 * On-disk inode flags (f2fs_inode::i_flags)
 */
#define F2FS_COMPR_FL			0x00000004 /* Compress file */
#define F2FS_SYNC_FL			0x00000008 /* Synchronous updates */
#define F2FS_IMMUTABLE_FL		0x00000010 /* Immutable file */
#define F2FS_APPEND_FL			0x00000020 /* writes to file may only append */
#define F2FS_NODUMP_FL			0x00000040 /* do not dump file */
#define F2FS_NOATIME_FL			0x00000080 /* do not update atime */
#define F2FS_NOCOMP_FL			0x00000400 /* Don't compress */
#define F2FS_INDEX_FL			0x00001000 /* hash-indexed directory */
#define F2FS_DIRSYNC_FL			0x00010000 /* dirsync behaviour (directories only) */
#define F2FS_PROJINHERIT_FL		0x20000000 /* Create with parents projid */
#define F2FS_CASEFOLD_FL		0x40000000 /* Casefolded file */

/* Flags that should be inherited by new inodes from their parent. */
#define F2FS_FL_INHERITED (F2FS_SYNC_FL | F2FS_NODUMP_FL | F2FS_NOATIME_FL | \
			   F2FS_DIRSYNC_FL | F2FS_PROJINHERIT_FL | \
			   F2FS_CASEFOLD_FL | F2FS_COMPR_FL | F2FS_NOCOMP_FL)

/* Flags that are appropriate for regular files (all but dir-specific ones). */
#define F2FS_REG_FLMASK		(~(F2FS_DIRSYNC_FL | F2FS_PROJINHERIT_FL | \
				F2FS_CASEFOLD_FL))

/* Flags that are appropriate for non-directories/regular files. */
#define F2FS_OTHER_FLMASK	(F2FS_NODUMP_FL | F2FS_NOATIME_FL)

static inline __u32 f2fs_mask_flags(umode_t mode, __u32 flags)
{
	if (S_ISDIR(mode))			return flags;
	else if (S_ISREG(mode))		return flags & F2FS_REG_FLMASK;
	else						return flags & F2FS_OTHER_FLMASK;
}

//static inline void __mark_inode_dirty_flag(inode *inode, int flag, bool set)
//{
//	switch (flag) 
//	{
//	case FI_INLINE_XATTR:
//	case FI_INLINE_DATA:
//	case FI_INLINE_DENTRY:
//	case FI_NEW_INODE:
//		if (set) return;
////		fallthrough;
//	case FI_DATA_EXIST:
//	case FI_INLINE_DOTS:
//	case FI_PIN_FILE:
//		F2FS_I(inode)->f2fs_mark_inode_dirty_sync( true);
//	}
//}


static inline void set_inode_flag(f2fs_inode_info *node, int flag)
{
	set_bit(flag, &node->flags);
	node->__mark_inode_dirty_flag(flag, true);
}

static inline int is_inode_flag_set(struct inode *inode, int flag)
{
	return test_bit(flag, &F2FS_I(inode)->flags);
}
static inline void clear_inode_flag(f2fs_inode_info *inode, int flag)
{
	clear_bit(flag, &inode->flags);
	inode->__mark_inode_dirty_flag(flag, false);
}

static inline bool f2fs_verity_in_progress(struct inode *inode)
{
	return /*IS_ENABLED(CONFIG_FS_VERITY) &&*/ is_inode_flag_set(inode, FI_VERITY_IN_PROGRESS);
}

static inline void set_acl_inode(f2fs_inode_info *inode, umode_t mode)
{
	F2FS_I(inode)->i_acl_mode = mode;
	inode->set_inode_flag(FI_ACL_MODE);
	inode->f2fs_mark_inode_dirty_sync( false);
}

//static inline void f2fs_i_links_write(struct inode *inode, bool inc)
//{
//	if (inc)
//		inc_nlink(inode);
//	else
//		drop_nlink(inode);
//	inode->f2fs_mark_inode_dirty_sync( true);
//}

static inline void f2fs_i_blocks_write(f2fs_inode_info *inode,	block_t diff, bool add, bool claim)
{
	bool clean = !is_inode_flag_set(inode, FI_DIRTY_INODE);
	bool recover = is_inode_flag_set(inode, FI_AUTO_RECOVER);

	/* add = 1, claim = 1 should be dquot_reserve_block in pair */
	if (add) 
	{
		if (claim)		dquot_claim_block(inode, diff);
		else			dquot_alloc_block_nofail(inode, diff);
	} 
	else {		dquot_free_block(inode, diff);	}

	inode->f2fs_mark_inode_dirty_sync( true);
	if (clean || recover)		inode->set_inode_flag(FI_AUTO_RECOVER);
}


//<YUAN> move to member
//static inline void f2fs_i_depth_write(struct inode *inode, unsigned int depth)
//{
//	F2FS_I(inode)->i_current_depth = depth;
//	inode->f2fs_mark_inode_dirty_sync( true);
//}

#if 0

static inline void f2fs_i_gc_failures_write(struct inode *inode,
					unsigned int count)
{
	F2FS_I(inode)->i_gc_failures[GC_FAILURE_PIN] = count;
	inode->f2fs_mark_inode_dirty_sync( true);
}
#endif

static inline void f2fs_i_xnid_write(f2fs_inode_info *inode, nid_t xnid)
{
	F2FS_I(inode)->i_xattr_nid = xnid;
	inode->f2fs_mark_inode_dirty_sync( true);
}

//static inline void f2fs_i_pino_write(struct inode *inode, nid_t pino)
//{
//	F2FS_I(inode)->i_pino = pino;
//	inode->f2fs_mark_inode_dirty_sync( true);
//}

static inline void get_inline_info(inode *inode, f2fs_inode *ri)
{
	f2fs_inode_info *fi = F2FS_I(inode);
	fi->get_inline_info(ri);
	//if (ri->i_inline & F2FS_INLINE_XATTR)		set_bit(FI_INLINE_XATTR, fi->flags);
	//if (ri->i_inline & F2FS_INLINE_DATA)		set_bit(FI_INLINE_DATA, fi->flags);
	//if (ri->i_inline & F2FS_INLINE_DENTRY)		set_bit(FI_INLINE_DENTRY, fi->flags);
	//if (ri->i_inline & F2FS_DATA_EXIST)			set_bit(FI_DATA_EXIST, fi->flags);
	//if (ri->i_inline & F2FS_INLINE_DOTS)		set_bit(FI_INLINE_DOTS, fi->flags);
	//if (ri->i_inline & F2FS_EXTRA_ATTR)			set_bit(FI_EXTRA_ATTR, fi->flags);
	//if (ri->i_inline & F2FS_PIN_FILE)			set_bit(FI_PIN_FILE, fi->flags);
}

static inline void set_raw_inline(struct inode *inode, struct f2fs_inode *ri)
{
	ri->i_inline = 0;

	if (is_inode_flag_set(inode, FI_INLINE_XATTR))		ri->i_inline |= F2FS_INLINE_XATTR;
	if (is_inode_flag_set(inode, FI_INLINE_DATA))		ri->i_inline |= F2FS_INLINE_DATA;
	if (is_inode_flag_set(inode, FI_INLINE_DENTRY))		ri->i_inline |= F2FS_INLINE_DENTRY;
	if (is_inode_flag_set(inode, FI_DATA_EXIST))		ri->i_inline |= F2FS_DATA_EXIST;
	if (is_inode_flag_set(inode, FI_INLINE_DOTS))		ri->i_inline |= F2FS_INLINE_DOTS;
	if (is_inode_flag_set(inode, FI_EXTRA_ATTR))		ri->i_inline |= F2FS_EXTRA_ATTR;
	if (is_inode_flag_set(inode, FI_PIN_FILE))			ri->i_inline |= F2FS_PIN_FILE;
}

static inline int f2fs_has_extra_attr(struct inode *inode)
{
	return is_inode_flag_set(inode, FI_EXTRA_ATTR);
}

static inline int f2fs_has_inline_xattr(struct inode *inode)
{
	return is_inode_flag_set(inode, FI_INLINE_XATTR);
}
#if 0
#endif

static inline int f2fs_compressed_file(struct inode *inode)
{
	return S_ISREG(inode->i_mode) && is_inode_flag_set(inode, FI_COMPRESSED_FILE);
}

static inline bool f2fs_need_compress_data(struct inode *inode)
{
	int compress_mode = F2FS_OPTION(F2FS_I_SB(inode)).compress_mode;

	if (!f2fs_compressed_file(inode))
		return false;

	if (compress_mode == COMPR_MODE_FS)
		return true;
	else if (compress_mode == COMPR_MODE_USER &&
			is_inode_flag_set(inode, FI_ENABLE_COMPRESS))
		return true;

	return false;
}

static inline unsigned int addrs_per_inode(struct inode *inode)
{
	unsigned int addrs = CUR_ADDRS_PER_INODE(inode) -	get_inline_xattr_addrs(inode);

	if (!f2fs_compressed_file(inode))		return addrs;
	return ALIGN_DOWN(addrs, F2FS_I(inode)->i_cluster_size);
}

static inline unsigned int addrs_per_block(inode *inode)
{
	if (!f2fs_compressed_file(inode))
		return DEF_ADDRS_PER_BLOCK;
	return ALIGN_DOWN(DEF_ADDRS_PER_BLOCK, F2FS_I(inode)->i_cluster_size);
}


static inline void *inline_xattr_addr(struct inode *inode, struct page *page)
{
	struct f2fs_inode *ri = F2FS_INODE(page);

	return (void *)&(ri->_u.i_addr[DEF_ADDRS_PER_INODE -
					get_inline_xattr_addrs(inode)]);
}

static inline int inline_xattr_size(struct inode *inode)
{
	if (f2fs_has_inline_xattr(inode))
		return get_inline_xattr_addrs(inode) * sizeof(__le32);
	return 0;
}

static inline int f2fs_has_inline_data(struct inode *inode)
{
	return is_inode_flag_set(inode, FI_INLINE_DATA);
}

static inline int f2fs_exist_data(struct inode *inode)
{
	return is_inode_flag_set(inode, FI_DATA_EXIST);
}

//<YUAN> move to member
//static inline int f2fs_has_inline_dots(inode *node)
//{
//	return is_inode_flag_set(node, FI_INLINE_DOTS);
//}
#if 0

static inline int f2fs_is_mmap_file(struct inode *inode)
{
	return is_inode_flag_set(inode, FI_MMAP_FILE);
}
#endif

static inline bool f2fs_is_pinned_file(struct inode *inode)
{
	return is_inode_flag_set(inode, FI_PIN_FILE);
}

static inline bool f2fs_is_atomic_file(struct inode *inode)
{
	return is_inode_flag_set(inode, FI_ATOMIC_FILE);
}
static inline bool f2fs_is_commit_atomic_write(struct inode *inode)
{
	return is_inode_flag_set(inode, FI_ATOMIC_COMMIT);
}

static inline bool f2fs_is_volatile_file(struct inode *inode)
{
	return is_inode_flag_set(inode, FI_VOLATILE_FILE);
}

static inline bool f2fs_is_first_block_written(struct inode *inode)
{
	return is_inode_flag_set(inode, FI_FIRST_BLOCK_WRITTEN);
}

static inline bool f2fs_is_drop_cache(struct inode *inode)
{
	return is_inode_flag_set(inode, FI_DROP_CACHE);
}

static inline void *inline_data_addr(inode *inode, struct page *page)
{
	struct f2fs_inode *ri = F2FS_INODE(page);
	int extra_size = get_extra_isize(inode);
	return (void *)&(ri->_u.i_addr[extra_size + DEF_INLINE_RESERVED_SIZE]);
}

//<YUAN> move to member
//static inline int f2fs_has_inline_dentry(inode *inode)
//{
//	return is_inode_flag_set(inode, FI_INLINE_DENTRY);
//}

static inline int is_file(inode *inode, int type)
{
	return F2FS_I(inode)->i_advise & type;
}

//<YUAN> move to member
//static inline void set_file(struct inode *inode, int type)
//{
//	F2FS_I(inode)->i_advise |= type;
//	inode->f2fs_mark_inode_dirty_sync( true);
//}

//static inline void clear_file(struct inode *inode, int type)
//{
//	F2FS_I(inode)->i_advise &= ~type;
//	inode->f2fs_mark_inode_dirty_sync( true);
//}

static inline bool f2fs_is_time_consistent(f2fs_inode_info *inode)
{
	//if (!timespec64_equal(F2FS_I(inode)->i_disk_time, &inode->i_atime))
	//	return false;
	//if (!timespec64_equal(F2FS_I(inode)->i_disk_time + 1, &inode->i_ctime))
	//	return false;
	//if (!timespec64_equal(F2FS_I(inode)->i_disk_time + 2, &inode->i_mtime))
	//	return false;
	//if (!timespec64_equal(F2FS_I(inode)->i_disk_time + 3, &F2FS_I(inode)->i_crtime))
	//	return false;
	if (!(inode->i_disk_time[0] == inode->i_atime)) return false;
	if (!(inode->i_disk_time[1] == inode->i_ctime)) return false;
	if (!(inode->i_disk_time[2] == inode->i_mtime)) return false;
	if (!(inode->i_disk_time[3] == inode->i_crtime)) return false;
	return true;
}

//static inline bool f2fs_skip_inode_update(f2fs_inode_info *inode, int dsync)
//{
//	bool ret;
//
//	if (dsync) {
//		struct f2fs_sb_info *sbi = F2FS_I_SB(inode);
//
//		spin_lock(&m_sbi->inode_lock[DIRTY_META]);
////		ret = list_empty(&F2FS_I(inode)->gdirty_list);
//		ret = m_sbi->list_empty(DIRTY_META);
//		spin_unlock(&m_sbi->inode_lock[DIRTY_META]);
//		return ret;
//	}
//	if (!is_inode_flag_set(inode, FI_AUTO_RECOVER) ||
//			file_keep_isize(inode) ||
//			i_size_read(inode) & ~PAGE_MASK)
//		return false;
//
//	if (!f2fs_is_time_consistent(inode))
//		return false;
//
//	spin_lock(&F2FS_I(inode)->i_size_lock);
//	ret = F2FS_I(inode)->last_disk_size == i_size_read(inode);
//	spin_unlock(&F2FS_I(inode)->i_size_lock);
//
//	return ret;
//}
inline bool f2fs_inode_info::f2fs_skip_inode_update(int dsync)
{
	bool ret;
	if (dsync) {
		//		struct f2fs_sb_info *sbi = F2FS_I_SB(inode);
		spin_lock(&m_sbi->inode_lock[DIRTY_META]);
		//		ret = list_empty(&F2FS_I(inode)->gdirty_list);
		ret = m_sbi->list_empty(DIRTY_META);
		spin_unlock(&m_sbi->inode_lock[DIRTY_META]);
		return ret;
	}
	if (!is_inode_flag_set(FI_AUTO_RECOVER) ||
		file_keep_isize(this) ||
		i_size_read(this) & ~PAGE_MASK)
		return false;

	if (!f2fs_is_time_consistent(this))
		return false;

	spin_lock(&i_size_lock);
	ret = last_disk_size == i_size_read(this);
	spin_unlock(&i_size_lock);

	return ret;
}


//static inline bool f2fs_readonly(struct super_block *sb)
//{
//	return sb_rdonly(sb);
//}

//<YUAN> move to f2fs_sb_info member
//static inline bool f2fs_cp_error(struct f2fs_sb_info *sbi)
//{
//	return sbi->is_set_ckpt_flags(CP_ERROR_FLAG);
//}

static inline bool is_dot_dotdot(const char *name, size_t len)
{
	if (len == 1 && name[0] == '.') return true;
	if (len == 2 && name[0] == '.' && name[1] == '.')	return true;
	return false;
}

static inline bool f2fs_may_extent_tree(struct inode *inode)
{
	struct f2fs_sb_info *sbi = F2FS_I_SB(inode);

	if (!test_opt(sbi, EXTENT_CACHE) ||
			is_inode_flag_set(inode, FI_NO_EXTENT) ||
			is_inode_flag_set(inode, FI_COMPRESSED_FILE))
		return false;

	/* for recovered files during mount do not create extents if shrinker is not registered. */
	if (list_empty(&sbi->s_list))
		return false;

	return S_ISREG(inode->i_mode);
}

template <typename T>
inline T *f2fs_kmalloc(f2fs_sb_info *sbi, size_t n/*, gfp_t flags*/)
{
	if (time_to_inject(sbi, FAULT_KMALLOC)) 
	{
		f2fs_show_injection_info(sbi, FAULT_KMALLOC);
		return NULL;
	}
	return new T[n];
}

//static inline void *f2fs_kzalloc(struct f2fs_sb_info *sbi, size_t size, gfp_t flags)
template <typename T>
inline T *f2fs_kzalloc(f2fs_sb_info *sbi, size_t n)
{
	T* t = f2fs_kmalloc<T>(sbi, n);
	if (t == nullptr) THROW_ERROR(ERR_APP, L"failed on creating array");
	memset(t, 0, sizeof(T) * n);
	return t;
	//return f2fs_kmalloc(sbi, size, flags | __GFP_ZERO);
}

// 申请 T类型的数组，数组大小n
template <typename T>
inline T *f2fs_kvmalloc(f2fs_sb_info *sbi,	size_t n/*size_t size, gfp_t flags*/)
{
	if (time_to_inject(sbi, FAULT_KVMALLOC)) 
	{
		f2fs_show_injection_info(sbi, FAULT_KVMALLOC);
		return NULL;
	}
	return new T[n];

//	return kvmalloc(size, flags);
}

// 申请 T类型的数组，数组大小n，并且清零
template <typename T>
inline T *f2fs_kvzalloc(f2fs_sb_info *sbi, size_t n/*, gfp_t flags*/)
{
	T * t = f2fs_kvmalloc<T>(sbi, n);
	memset(t, 0, sizeof(T) * n);
	return t;
}

template <typename T>
inline void f2fs_kvfree(T*& ptr)
{
	delete[] ptr;
	ptr = NULL;
}

template <typename T>
inline T* kmemdup(T* data, size_t size, gfp_t flats)
{
	T* new_buf = new T[size];
	memcpy_s(new_buf, sizeof(T) * size, data, sizeof(T) * size);
	return new_buf;
}

inline int get_extra_isize(f2fs_inode_info* iinode)
{
	return iinode->i_extra_isize / sizeof(__le32);
}

inline int get_extra_isize(f2fs_inode* node)
{
	return node->_u._s.i_extra_isize / sizeof(__le32);
}

static inline int get_extra_isize(inode *inode)
{
	return get_extra_isize(F2FS_I(inode));
//	->i_extra_isize / sizeof(__le32);
}

static inline int get_inline_xattr_addrs(struct inode *inode)
{
	return F2FS_I(inode)->i_inline_xattr_size;
}

//#define f2fs_get_inode_mode(i) \
//	((is_inode_flag_set(i, FI_ACL_MODE)) ? \
//	 (F2FS_I(i)->i_acl_mode) : ((i)->i_mode))

#define f2fs_get_inode_mode 	((is_inode_flag_set(this, FI_ACL_MODE)) ? (i_acl_mode) : (i_mode))

#define F2FS_OLD_ATTRIBUTE_SIZE	(offsetof(f2fs_inode, _u.i_addr))

//#define F2FS_FITS_IN_INODE(f2fs_inode, extra_isize, field)		\
//		((offsetof(typeof(*(f2fs_inode)), field) + sizeof((f2fs_inode)->field))			\
//		<= (F2FS_OLD_ATTRIBUTE_SIZE + (extra_isize)))

#define F2FS_FITS_IN_INODE(aa, extra_isize, field)		\
		((offsetof(f2fs_inode, field) + sizeof((aa)->field))	<= (F2FS_OLD_ATTRIBUTE_SIZE + (extra_isize)))

#define DEFAULT_IOSTAT_PERIOD_MS	3000
#define MIN_IOSTAT_PERIOD_MS		100
/* maximum period of iostat tracing is 1 day */
#define MAX_IOSTAT_PERIOD_MS		8640000

static inline void f2fs_reset_iostat(f2fs_sb_info *sbi)
{
	int i;
	spin_lock(&sbi->iostat_lock);
	for (i = 0; i < NR_IO_TYPE; i++) 
	{
		sbi->rw_iostat[i] = 0;
		sbi->prev_rw_iostat[i] = 0;
	}
	spin_unlock(&sbi->iostat_lock);
}

//inline  void f2fs_record_iostat(f2fs_sb_info *sbi) UNSUPPORT_0

void f2fs_record_iostat(f2fs_sb_info* sbi);


static inline void f2fs_update_iostat(f2fs_sb_info *sbi, enum iostat_type type, unsigned long long io_bytes)
{
	if (!sbi->iostat_enable)		return;
	spin_lock(&sbi->iostat_lock);
	sbi->rw_iostat[type] += io_bytes;

	if (type == APP_WRITE_IO || type == APP_DIRECT_IO)
		sbi->rw_iostat[APP_BUFFERED_IO] = sbi->rw_iostat[APP_WRITE_IO] - sbi->rw_iostat[APP_DIRECT_IO];

	if (type == APP_READ_IO || type == APP_DIRECT_READ_IO)
		sbi->rw_iostat[APP_BUFFERED_READ_IO] =	sbi->rw_iostat[APP_READ_IO] - sbi->rw_iostat[APP_DIRECT_READ_IO];
	spin_unlock(&sbi->iostat_lock);
	f2fs_record_iostat(sbi);
}

//#define __is_large_section(sbi)		((sbi)->segs_per_sec > 1)

#define __is_meta_io(fio) (PAGE_TYPE_OF_BIO((fio)->type) == META)
//bool f2fs_is_valid_blkaddr(struct f2fs_sb_info *sbi, block_t blkaddr, int type);

static inline void verify_blkaddr(struct f2fs_sb_info *sbi, block_t blkaddr, int type)
{
	if (!sbi->f2fs_is_valid_blkaddr(blkaddr, type)) 
	{
//		f2fs_err(sbi, L"invalid blkaddr: %u, type: %d, run fsck to fix.", blkaddr, type);
		f2fs_bug_on(sbi, 1);
	}
}

static inline bool __is_valid_data_blkaddr(block_t blkaddr)
{
	if (blkaddr == NEW_ADDR || blkaddr == NULL_ADDR || blkaddr == COMPRESS_ADDR)	return false;
	return true;
}

static inline void f2fs_set_page_private(page *page,	unsigned long data)
{
	if (PagePrivate(page))		return;

//<YUAN> attach_page_private展开
//	attach_page_private(page, (void *)data);
	set_page_private(page, data);
	SetPagePrivate(page);
}
static inline void f2fs_clear_page_private(page *ppage)
{
//<YUAN> detach_page_private展开
//	detach_page_private(ppage);
	if (!PagePrivate(ppage)) return;
	ClearPagePrivate(ppage);
	set_page_private(ppage, 0);
	ppage->put_page();
}
#if 0

/*
 * file.c
 */
int f2fs_sync_file(struct file *file, loff_t start, loff_t end, int datasync);
#endif
void f2fs_truncate_data_blocks(dnode_of_data *dn);
int f2fs_do_truncate_blocks(struct inode *inode, UINT64 from, bool lock);
int f2fs_truncate_blocks(struct inode *inode, UINT64 from, bool lock);
int f2fs_truncate(f2fs_inode_info *inode);
//int f2fs_getattr(user_namespace *mnt_userns, const path *path, kstat *stat, u32 request_mask, unsigned int flags);
//int f2fs_setattr(user_namespace *mnt_userns, dentry *dentry, iattr *attr);
#if 0
int f2fs_truncate_hole(struct inode *inode, pgoff_t pg_start, pgoff_t pg_end);
#endif
void f2fs_truncate_data_blocks_range(struct dnode_of_data *dn, int count);
#if 0
int f2fs_precache_extents(struct inode *inode);
int f2fs_fileattr_get(struct dentry *dentry, struct fileattr *fa);
int f2fs_fileattr_set(struct user_namespace *mnt_userns,
		      struct dentry *dentry, struct fileattr *fa);
long f2fs_ioctl(struct file *filp, unsigned int cmd, unsigned long arg);
long f2fs_compat_ioctl(struct file *file, unsigned int cmd, unsigned long arg);
int f2fs_transfer_project_quota(struct inode *inode, kprojid_t kprojid);
int f2fs_pin_file_control(struct inode *inode, bool inc);

#endif
/*
 * inode.c
 */
void f2fs_set_inode_flags(struct inode *inode);
bool f2fs_inode_chksum_verify(struct f2fs_sb_info* sbi, struct page* page);
void f2fs_inode_chksum_set(struct f2fs_sb_info* sbi, struct page* page);
// ino: inode number
//inode* f2fs_iget(struct super_block* sb, unsigned long ino);// { JCASSERT(0); return NULL; }
//inode *f2fs_iget_retry(f2fs_sb_info *sb, unsigned long ino);
int f2fs_try_to_free_nats(f2fs_sb_info* sbi, int nr_shrink);
//void f2fs_update_inode(struct inode *inode, struct page *node_page);
//void f2fs_update_inode_page(struct inode* inode);
//int f2fs_write_inode(struct inode *inode, struct writeback_control *wbc);
//void f2fs_evict_inode(struct inode *inode);
void f2fs_handle_failed_inode(struct inode *inode);

#if 0

/*
 * namei.c
 */
int f2fs_update_extension_list(struct f2fs_sb_info *sbi, const char *name,
							bool hot, bool set);
struct dentry *f2fs_get_parent(struct dentry *child);
#endif

/*
 * dir.c
 */
unsigned char f2fs_get_de_type(struct f2fs_dir_entry *de);
inline int f2fs_init_casefolded_name(const inode *dir, f2fs_filename *fname) UNSUPPORT_1(int);
#if 0
int f2fs_setup_filename(struct inode *dir, const struct qstr *iname,
			int lookup, struct f2fs_filename *fname);
#endif
int f2fs_prepare_lookup(struct inode *dir, struct dentry *dentry, struct f2fs_filename *fname);
#if 0
void f2fs_free_filename(struct f2fs_filename *fname);
struct f2fs_dir_entry *f2fs_find_target_dentry(const struct f2fs_dentry_ptr *d,
			const struct f2fs_filename *fname, int *max_slots);
int f2fs_fill_dentries(struct dir_context *ctx, struct f2fs_dentry_ptr *d,
			unsigned int start_pos, struct fscrypt_str *fstr);
void f2fs_do_make_empty_dir(struct inode *inode, struct inode *parent,
			struct f2fs_dentry_ptr *d);
struct page *f2fs_init_inode_metadata(struct inode *inode, struct inode *dir,
			const struct f2fs_filename *fname, struct page *dpage);
void f2fs_update_parent_metadata(struct inode *dir, struct inode *inode,
			unsigned int current_depth);
#endif
int f2fs_room_for_filename(const void *bitmap, int slots, int max_slots);

//<YUAN> move to member
// f2fs_dir_entry* __f2fs_find_entry(inode* dir, const f2fs_filename* fname, page** res_page);
void f2fs_drop_nlink(f2fs_inode_info *dir, f2fs_inode_info *inode);
struct f2fs_dir_entry *f2fs_find_entry(struct inode *dir,
			const struct qstr *child, struct page **res_page);
struct f2fs_dir_entry *f2fs_parent_dir(struct inode *dir, struct page **p);
ino_t f2fs_inode_by_name(struct inode *dir, const struct qstr *qstr,
			struct page **page);
void f2fs_set_link(struct inode *dir, struct f2fs_dir_entry *de,
			struct page *page, struct inode *inode);
bool f2fs_has_enough_room(struct inode *dir, struct page *ipage,
			  const struct f2fs_filename *fname);
void f2fs_update_dentry(nid_t ino, umode_t mode, f2fs_dentry_ptr *d, const fscrypt_str *name, f2fs_hash_t name_hash,
			unsigned int bit_pos);
#if 0
int f2fs_add_regular_entry(struct inode *dir, const struct f2fs_filename *fname,
			struct inode *inode, nid_t ino, umode_t mode);
#endif
inline int f2fs_add_dentry(inode *dir, const f2fs_filename *fname, inode *inode, nid_t ino, umode_t mode) UNSUPPORT_1(int);
//int f2fs_do_add_link(inode *dir, const qstr *name, struct inode *inode, nid_t ino, umode_t mode);
void f2fs_delete_entry(f2fs_dir_entry* dentry, page* page, f2fs_inode_info* dir, f2fs_inode_info* inode);
#if 0
int f2fs_do_tmpfile(struct inode *inode, struct inode *dir);
bool f2fs_empty_dir(struct inode *dir);
#endif

// 把inode和dentry添加到dentry的父节点中
//static inline int f2fs_add_link(dentry *entry, f2fs_inode_info *inode)
//{
//	if (fscrypt_is_nokey_name(entry))	return -ENOKEY;
//	f2fs_inode_info* fi = F2FS_I(d_inode(entry->d_parent));
//	Cf2fsDirInode* di = dynamic_cast<Cf2fsDirInode*>(fi);
//	if (di == NULL) THROW_ERROR(ERR_APP, L"only dir inode support this feature");
//	return di->f2fs_do_add_link( &entry->d_name, inode, inode->i_ino, inode->i_mode);
//}

//// ==== super.c */
//int f2fs_inode_dirtied(struct inode* inode, bool sync);
void f2fs_inode_synced(struct inode* inode);
int f2fs_enable_quota_files(struct f2fs_sb_info *sbi, bool rdonly);
int f2fs_quota_sync(super_block* sb, int type);
loff_t max_file_blocks(struct inode *inode);
void f2fs_quota_off_umount(struct super_block *sb);
inline int f2fs_commit_super(struct f2fs_sb_info *sbi, bool recover) { JCASSERT(0); return 0; }
//int f2fs_sync_fs(struct super_block* sb, int sync);
int f2fs_sanity_check_ckpt(struct f2fs_sb_info *sbi);

/*
 * hash.c
 */
void f2fs_hash_filename(const inode* dir, f2fs_filename* fname);

/* node.cpp */
struct node_info;

int f2fs_check_nid_range(struct f2fs_sb_info *sbi, nid_t nid);
bool f2fs_available_free_memory(struct f2fs_sb_info *sbi, int type);
//inline bool f2fs_in_warm_node_list(struct f2fs_sb_info* sbi, struct page* page) UNSUPPORT_1(bool);
void f2fs_init_fsync_node_info(f2fs_sb_info* sbi);
void f2fs_del_fsync_node_entry(struct f2fs_sb_info *sbi, struct page *page);
void f2fs_reset_fsync_node_info(struct f2fs_sb_info *sbi);
int f2fs_need_dentry_mark(struct f2fs_sb_info *sbi, nid_t nid);
bool f2fs_is_checkpointed_node(struct f2fs_sb_info *sbi, nid_t nid);
bool f2fs_need_inode_block_update(struct f2fs_sb_info* sbi, nid_t ino);
//int f2fs_get_node_info(f2fs_sb_info *sbi, nid_t nid, node_info *ni);
pgoff_t f2fs_get_next_page_offset(struct dnode_of_data *dn, pgoff_t pgofs);
/// <summary> 通过逻辑块index获取对应的物理块，放入dnode_of_data::data_blkaddr中
/// Caller should call f2fs_put_dnode(dn). Also, it should graband release a rwsem by calling f2fs_lock_op() and f2fs_unlock_op() only if mode is set with ALLOC_NODE.
/// </summary>
/// <param name="dn">[OUT]</param>
/// <param name="index">[IN]逻辑块</param>
/// <param name="mode"></param>
/// <returns></returns>
int f2fs_get_dnode_of_data(struct dnode_of_data *dn, pgoff_t index, int mode);
int f2fs_truncate_inode_blocks(inode* inode, pgoff_t from);
int f2fs_truncate_xattr_node(struct inode *inode);
int f2fs_wait_on_node_pages_writeback(struct f2fs_sb_info *sbi,
					unsigned int seq_id);
int f2fs_remove_inode_page(struct inode *inode);
//page *f2fs_new_inode_page(f2fs_inode_info *inode);
//page* f2fs_new_node_page(dnode_of_data* dn, unsigned int ofs);
inline void f2fs_ra_node_page(struct f2fs_sb_info *sbi, nid_t nid) UNSUPPORT_0
//struct page *f2fs_get_node_page(struct f2fs_sb_info *sbi, pgoff_t nid);
inline page *f2fs_get_node_page_ra(struct page *parent, int start) UNSUPPORT_1(page*);
int f2fs_move_node_page(struct page *node_page, int gc_type);
void f2fs_flush_inline_data(struct f2fs_sb_info* sbi);
int f2fs_fsync_node_pages(struct f2fs_sb_info *sbi, struct inode *inode,
			struct writeback_control *wbc, bool atomic,
			unsigned int *seq_id);
int f2fs_sync_node_pages(struct f2fs_sb_info *sbi,
			struct writeback_control *wbc,
			bool do_balance, enum iostat_type io_type);
//inline int f2fs_build_free_nids(struct f2fs_sb_info* sbi, bool sync, bool mount) UNSUPPORT_1(int);
//inline bool f2fs_alloc_nid(struct f2fs_sb_info *sbi, nid_t *nid)UNSUPPORT_1(bool);
//inline void f2fs_alloc_nid_done(struct f2fs_sb_info *sbi, nid_t nid);
inline void f2fs_alloc_nid_failed(struct f2fs_sb_info *sbi, nid_t nid)UNSUPPORT_0;
inline int f2fs_try_to_free_nids(struct f2fs_sb_info* sbi, int nr_shrink) UNSUPPORT_1(int);
int f2fs_recover_inline_xattr(struct inode *inode, struct page *page);
int f2fs_recover_xattr_data(struct inode *inode, struct page *page);
int f2fs_recover_inode_page(struct f2fs_sb_info *sbi, struct page *page);
int f2fs_restore_node_summary(struct f2fs_sb_info *sbi,
			unsigned int segno, struct f2fs_summary_block *sum);
int f2fs_flush_nat_entries(struct f2fs_sb_info *sbi, struct cp_control *cpc);
//inline int f2fs_build_node_manager(struct f2fs_sb_info *sbi) { JCASSERT(0); return 0; }
//void f2fs_destroy_node_manager(struct f2fs_sb_info *sbi);
int __init f2fs_create_node_manager_caches(void);
void f2fs_destroy_node_manager_caches(void);

/*
 * segment.c
 */
bool f2fs_need_SSR(struct f2fs_sb_info *sbi);
void f2fs_register_inmem_page(struct inode *inode, struct page *page);
void f2fs_drop_inmem_pages_all(struct f2fs_sb_info *sbi, bool gc_failure);
void f2fs_drop_inmem_pages(struct inode* inode);
void f2fs_drop_inmem_page(struct inode *inode, struct page *page);
int f2fs_commit_inmem_pages(struct inode *inode);
//void f2fs_balance_fs(struct f2fs_sb_info* sbi, bool need);
void f2fs_balance_fs_bg(struct f2fs_sb_info *sbi, bool from_bg);
//int f2fs_issue_flush(struct f2fs_sb_info *sbi, nid_t ino);
int f2fs_create_flush_cmd_control(struct f2fs_sb_info *sbi);
int f2fs_flush_device_cache(struct f2fs_sb_info *sbi);
//void f2fs_destroy_flush_cmd_control(struct f2fs_sb_info *sbi, bool free);
void f2fs_invalidate_blocks(struct f2fs_sb_info *sbi, block_t addr);
bool f2fs_is_checkpointed_data(struct f2fs_sb_info *sbi, block_t blkaddr);
void f2fs_drop_discard_cmd(struct f2fs_sb_info *sbi);
//void f2fs_stop_discard_thread(struct f2fs_sb_info *sbi);
//bool f2fs_issue_discard_timeout(struct f2fs_sb_info *sbi);
void f2fs_clear_prefree_segments(struct f2fs_sb_info *sbi, struct cp_control *cpc);
inline void f2fs_dirty_to_prefree(struct f2fs_sb_info *sbi) { JCASSERT(0); }//TODO
inline block_t f2fs_get_unusable_blocks(struct f2fs_sb_info *sbi) { JCASSERT(0); return 0; }
inline int f2fs_disable_cp_again(struct f2fs_sb_info* sbi, block_t unusable) { JCASSERT(0); return 0; }
void f2fs_release_discard_addrs(struct f2fs_sb_info *sbi);
//<YUAN> move to f2fs_sb_info member
//int f2fs_npages_for_summary_flush(struct f2fs_sb_info *sbi, bool for_ra);
bool f2fs_segment_has_free_slot(struct f2fs_sb_info* sbi, int segno);
void f2fs_init_inmem_curseg(f2fs_sb_info* sbi);
void f2fs_save_inmem_curseg(struct f2fs_sb_info *sbi);
void f2fs_restore_inmem_curseg(struct f2fs_sb_info *sbi);
void f2fs_get_new_segment(struct f2fs_sb_info *sbi,
			unsigned int *newseg, bool new_sec, int dir);
void f2fs_allocate_segment_for_resize(struct f2fs_sb_info *sbi, int type,
					unsigned int start, unsigned int end);
void f2fs_allocate_new_section(struct f2fs_sb_info *sbi, int type, bool force);
void f2fs_allocate_new_segments(struct f2fs_sb_info *sbi);
int f2fs_trim_fs(struct f2fs_sb_info *sbi, struct fstrim_range *range);
bool f2fs_exist_trim_candidates(struct f2fs_sb_info *sbi,
					struct cp_control *cpc);
struct page *f2fs_get_sum_page(struct f2fs_sb_info *sbi, unsigned int segno);
void f2fs_update_meta_page(struct f2fs_sb_info *sbi, void *src,
					block_t blk_addr);
//void f2fs_do_write_meta_page(struct f2fs_sb_info *sbi, struct page *page, enum iostat_type io_type);
void f2fs_do_write_node_page(unsigned int nid, struct f2fs_io_info* fio);
void f2fs_outplace_write_data(struct dnode_of_data *dn,
			struct f2fs_io_info *fio);
int f2fs_inplace_write_data(struct f2fs_io_info *fio);
inline void f2fs_do_replace_block(f2fs_sb_info* sbi, f2fs_summary* sum,
	block_t old_blkaddr, block_t new_blkaddr,
	bool recover_curseg, bool recover_newaddr,
	bool from_gc) UNSUPPORT_0;
void f2fs_replace_block(struct f2fs_sb_info *sbi, struct dnode_of_data *dn,
			block_t old_addr, block_t new_addr,
			unsigned char version, bool recover_curseg,
			bool recover_newaddr);
void f2fs_allocate_data_block(struct f2fs_sb_info *sbi, struct page *page,
			block_t old_blkaddr, block_t *new_blkaddr,
			struct f2fs_summary *sum, int type,
			struct f2fs_io_info *fio);
void f2fs_wait_on_page_writeback(page* page, enum page_type type, bool ordered, bool locked);
void f2fs_wait_on_block_writeback(struct inode *inode, block_t blkaddr);
void f2fs_wait_on_block_writeback_range(struct inode *inode, block_t blkaddr, block_t len);
void f2fs_write_data_summaries(struct f2fs_sb_info *sbi, block_t start_blk);
void f2fs_write_node_summaries(struct f2fs_sb_info *sbi, block_t start_blk);
int f2fs_lookup_journal_in_cursum(f2fs_journal* journal, int type, unsigned int val, int alloc);
//void f2fs_flush_sit_entries(f2fs_sb_info *sbi, cp_control *cpc);
int f2fs_fix_curseg_write_pointer(struct f2fs_sb_info *sbi);
int f2fs_check_write_pointer(struct f2fs_sb_info* sbi);
//inline int f2fs_build_segment_manager(struct f2fs_sb_info* sbi) { JCASSERT(0); return 0; }	//TODO
//void f2fs_destroy_segment_manager(struct f2fs_sb_info *sbi);
int __init f2fs_create_segment_manager_caches(void);
void f2fs_destroy_segment_manager_caches(void);
int f2fs_rw_hint_to_seg_type(enum rw_hint hint);
enum rw_hint f2fs_io_type_to_rw_hint(struct f2fs_sb_info *sbi,
			enum page_type type, enum temp_type temp);
unsigned int f2fs_usable_segs_in_sec(struct f2fs_sb_info *sbi,
			unsigned int segno);
unsigned int f2fs_usable_blks_in_seg(struct f2fs_sb_info *sbi,
			unsigned int segno);

/* checkpoint.c */
void f2fs_stop_checkpoint(struct f2fs_sb_info *sbi, bool end_io);
struct page *f2fs_grab_meta_page(struct f2fs_sb_info *sbi, pgoff_t index);
//struct page *f2fs_get_meta_page(struct f2fs_sb_info *sbi, pgoff_t index);
struct page *f2fs_get_meta_page_retry(struct f2fs_sb_info *sbi, pgoff_t index);
struct page *f2fs_get_tmp_page(struct f2fs_sb_info *sbi, pgoff_t index);
//bool f2fs_is_valid_blkaddr(struct f2fs_sb_info *sbi, block_t blkaddr, int type);
int f2fs_ra_meta_pages(struct f2fs_sb_info *sbi, block_t start, int nrpages, int type, bool sync);
void f2fs_ra_meta_pages_cond(struct f2fs_sb_info *sbi, pgoff_t index);
//long f2fs_sync_meta_pages(struct f2fs_sb_info *sbi, enum page_type type, long nr_to_write, enum iostat_type io_type);
void f2fs_add_ino_entry(struct f2fs_sb_info *sbi, nid_t ino, int type);
void f2fs_remove_ino_entry(struct f2fs_sb_info *sbi, nid_t ino, int type);
void f2fs_release_ino_entry(struct f2fs_sb_info *sbi, bool all);
bool f2fs_exist_written_data(struct f2fs_sb_info *sbi, nid_t ino, int mode);
void f2fs_set_dirty_device(struct f2fs_sb_info *sbi, nid_t ino,
					unsigned int devidx, int type);
bool f2fs_is_dirty_device(struct f2fs_sb_info *sbi, nid_t ino,
					unsigned int devidx, int type);
int f2fs_sync_inode_meta(struct f2fs_sb_info *sbi);
int f2fs_acquire_orphan_inode(struct f2fs_sb_info *sbi);
void f2fs_release_orphan_inode(f2fs_sb_info *sbi);
//void f2fs_add_orphan_inode(f2fs_inode_info *inode);
void f2fs_remove_orphan_inode(struct f2fs_sb_info *sbi, nid_t ino);
int f2fs_recover_orphan_inodes(struct f2fs_sb_info* sbi);
//int f2fs_get_valid_checkpoint(struct f2fs_sb_info *sbi);
void f2fs_update_dirty_page(struct inode *inode, struct page *page);
void f2fs_remove_dirty_inode(struct inode *inode);
int f2fs_sync_dirty_inodes(struct f2fs_sb_info* sbi, enum inode_type type);
//void f2fs_wait_on_all_pages(struct f2fs_sb_info *sbi, int type);
UINT64 f2fs_get_sectors_written(struct f2fs_sb_info* sbi);
//inline int f2fs_write_checkpoint(struct f2fs_sb_info *sbi, struct cp_control *cpc) { JCASSERT(0); return 0; }	// TODO

void f2fs_init_ino_entry_info(f2fs_sb_info* sbi);

int __init f2fs_create_checkpoint_caches(void);
void f2fs_destroy_checkpoint_caches(void);
int f2fs_issue_checkpoint(struct f2fs_sb_info *sbi);
//int f2fs_start_ckpt_thread(struct f2fs_sb_info* sbi);
//inline void f2fs_stop_ckpt_thread(struct f2fs_sb_info *sbi) { JCASSERT(0); }	// TODO
void f2fs_init_ckpt_req_control(f2fs_sb_info* sbi);

/*
 * data.c
 */
int __init f2fs_init_bioset(void);
void f2fs_destroy_bioset(void);
int f2fs_init_bio_entry_cache(void);
void f2fs_destroy_bio_entry_cache(void);
void f2fs_submit_bio(struct f2fs_sb_info *sbi,
				struct bio *bio, enum page_type type);
void f2fs_submit_merged_write(struct f2fs_sb_info *sbi, enum page_type type);
void f2fs_submit_merged_write_cond(f2fs_sb_info* sbi, inode* inode, struct page* page,
	nid_t ino, enum page_type type);

void f2fs_submit_merged_ipu_write(struct f2fs_sb_info *sbi,	struct bio **bio, struct page *page);
void f2fs_flush_merged_writes(struct f2fs_sb_info *sbi);
//int f2fs_submit_page_bio(f2fs_io_info *fio);
int f2fs_merge_page_bio(struct f2fs_io_info *fio);
//void f2fs_submit_page_write(struct f2fs_io_info *fio);
//IVirtualDisk *f2fs_target_device(f2fs_sb_info *sbi,	block_t blk_addr, bio *bio);
int f2fs_target_device_index(struct f2fs_sb_info *sbi, block_t blkaddr);
void f2fs_set_data_blkaddr(struct dnode_of_data *dn);
void f2fs_update_data_blkaddr(struct dnode_of_data *dn, block_t blkaddr);
int f2fs_reserve_new_blocks(struct dnode_of_data *dn, blkcnt_t count);
int f2fs_reserve_new_block(struct dnode_of_data *dn);
int f2fs_get_block(struct dnode_of_data *dn, pgoff_t index);
int f2fs_preallocate_blocks(struct kiocb *iocb, struct iov_iter *from);
int f2fs_reserve_block(struct dnode_of_data *dn, pgoff_t index);
//struct page *f2fs_get_read_data_page(struct inode *inode, pgoff_t index, int op_flags, bool for_write);
//struct page *f2fs_find_data_page(struct inode *inode, pgoff_t index);
//inline page* f2fs_get_lock_data_page(inode* inode, pgoff_t index, bool for_write);
//struct page *f2fs_get_new_data_page(struct inode *inode, struct page *ipage, pgoff_t index, bool new_i_size);
int f2fs_do_write_data_page(struct f2fs_io_info* fio);
void f2fs_do_map_lock(struct f2fs_sb_info *sbi, int flag, bool lock);

int f2fs_map_blocks(f2fs_inode_info *inode, struct f2fs_map_blocks *map, int create, int flag);
int f2fs_fiemap(struct inode *inode, struct fiemap_extent_info *fieinfo, UINT64 start, UINT64 len);
int f2fs_encrypt_one_page(struct f2fs_io_info *fio);
bool f2fs_should_update_inplace(struct inode *inode, struct f2fs_io_info *fio);
bool f2fs_should_update_outplace(struct inode *inode, struct f2fs_io_info *fio);
int f2fs_write_single_data_page(struct page* page, int* submitted,
	struct bio** bio, sector_t* last_block,
	struct writeback_control* wbc,
	enum iostat_type io_type, int compr_blocks, bool allow_balance);
//void f2fs_invalidate_page(struct page *page, unsigned int offset, unsigned int length);
#if 0
int f2fs_release_page(struct page *page, gfp_t wait);
#endif

#ifdef CONFIG_MIGRATION
int f2fs_migrate_page(address_space *mapping, struct page *newpage,
			struct page *page, enum migrate_mode mode);
#endif
bool f2fs_overwrite_io(f2fs_inode_info *inode, loff_t pos, size_t len);
void f2fs_clear_page_cache_dirty_tag(struct page *page);
int f2fs_init_post_read_processing(void);
void f2fs_destroy_post_read_processing(void);
int f2fs_init_post_read_wq(struct f2fs_sb_info* sbi);
void f2fs_destroy_post_read_wq(struct f2fs_sb_info *sbi);

/* gc.c */
int f2fs_start_gc_thread(struct f2fs_sb_info* sbi);

//void f2fs_stop_gc_thread(struct f2fs_sb_info *sbi);
block_t f2fs_start_bidx_of_node(unsigned int node_ofs, struct inode *inode);
int f2fs_gc(struct f2fs_sb_info* sbi, bool sync, bool background, bool force, unsigned int segno);

void f2fs_build_gc_manager(struct f2fs_sb_info *sbi);
int f2fs_resize_fs(struct f2fs_sb_info *sbi, __u64 block_count);
int __init f2fs_create_garbage_collection_cache(void);
void f2fs_destroy_garbage_collection_cache(void);

/* recovery.c */
int f2fs_recover_fsync_data(struct f2fs_sb_info* sbi, bool check_only);
bool f2fs_space_for_roll_forward(struct f2fs_sb_info *sbi);
int __init f2fs_create_recovery_cache(void);
void f2fs_destroy_recovery_cache(void);

/*
 * debug.c
 */
#ifdef CONFIG_F2FS_STAT_FS
struct f2fs_stat_info {
	struct list_head stat_list;
	struct f2fs_sb_info *sbi;
	int all_area_segs, sit_area_segs, nat_area_segs, ssa_area_segs;
	int main_area_segs, main_area_sections, main_area_zones;
	unsigned long long hit_largest, hit_cached, hit_rbtree;
	unsigned long long hit_total, total_ext;
	int ext_tree, zombie_tree, ext_node;
	int ndirty_node, ndirty_dent, ndirty_meta, ndirty_imeta;
	int ndirty_data, ndirty_qdata;
	int inmem_pages;
	unsigned int ndirty_dirs, ndirty_files, nquota_files, ndirty_all;
	int nats, dirty_nats, sits, dirty_sits;
	int free_nids, avail_nids, alloc_nids;
	int total_count, utilization;
	int bg_gc, nr_wb_cp_data, nr_wb_data;
	int nr_rd_data, nr_rd_node, nr_rd_meta;
	int nr_dio_read, nr_dio_write;
	unsigned int io_skip_bggc, other_skip_bggc;
	int nr_flushing, nr_flushed, flush_list_empty;
	int nr_discarding, nr_discarded;
	int nr_discard_cmd;
	unsigned int undiscard_blks;
	int nr_issued_ckpt, nr_total_ckpt, nr_queued_ckpt;
	unsigned int cur_ckpt_time, peak_ckpt_time;
	int inline_xattr, inline_inode, inline_dir, append, update, orphans;
	int compr_inode;
	unsigned long long compr_blocks;
	int aw_cnt, max_aw_cnt, vw_cnt, max_vw_cnt;
	unsigned int valid_count, valid_node_count, valid_inode_count, discard_blks;
	unsigned int bimodal, avg_vblocks;
	int util_free, util_valid, util_invalid;
	int rsvd_segs, overp_segs;
	int dirty_count, node_pages, meta_pages;
	int prefree_count, call_count, cp_count, bg_cp_count;
	int tot_segs, node_segs, data_segs, free_segs, free_secs;
	int bg_node_segs, bg_data_segs;
	int tot_blks, data_blks, node_blks;
	int bg_data_blks, bg_node_blks;
	unsigned long long skipped_atomic_files[2];
	int curseg[NR_CURSEG_TYPE];
	int cursec[NR_CURSEG_TYPE];
	int curzone[NR_CURSEG_TYPE];
	unsigned int dirty_seg[NR_CURSEG_TYPE];
	unsigned int full_seg[NR_CURSEG_TYPE];
	unsigned int valid_blks[NR_CURSEG_TYPE];

	unsigned int meta_count[META_MAX];
	unsigned int segment_count[2];
	unsigned int block_count[2];
	unsigned int inplace_count;
	unsigned long long base_mem, cache_mem, page_mem;
};

static inline struct f2fs_stat_info *F2FS_STAT(struct f2fs_sb_info *sbi)
{
	return (struct f2fs_stat_info *)sbi->stat_info;
}

#define stat_inc_cp_count(si)		((si)->cp_count++)
#define stat_inc_bg_cp_count(si)	((si)->bg_cp_count++)
#define stat_inc_call_count(si)		((si)->call_count++)
#define stat_inc_bggc_count(si)		((si)->bg_gc++)
#define stat_io_skip_bggc_count(sbi)	((sbi)->io_skip_bggc++)
#define stat_other_skip_bggc_count(sbi)	((sbi)->other_skip_bggc++)
#define stat_inc_dirty_inode(sbi, type)	((sbi)->ndirty_inode[type]++)
#define stat_dec_dirty_inode(sbi, type)	((sbi)->ndirty_inode[type]--)
#define stat_inc_total_hit(sbi)		(atomic64_inc(&(sbi)->total_hit_ext))
#define stat_inc_rbtree_node_hit(sbi)	(atomic64_inc(&(sbi)->read_hit_rbtree))
#define stat_inc_largest_node_hit(sbi)	(atomic64_inc(&(sbi)->read_hit_largest))
#define stat_inc_cached_node_hit(sbi)	(atomic64_inc(&(sbi)->read_hit_cached))
#define stat_inc_inline_xattr(inode)					\
	do {								\
		if (f2fs_has_inline_xattr(inode))			\
			(atomic_inc(&F2FS_I_SB(inode)->inline_xattr));	\
	} while (0)
#define stat_dec_inline_xattr(inode)					\
	do {								\
		if (f2fs_has_inline_xattr(inode))			\
			(atomic_dec(&F2FS_I_SB(inode)->inline_xattr));	\
	} while (0)
#define stat_inc_inline_inode(inode)					\
	do {								\
		if (f2fs_has_inline_data(inode))			\
			(atomic_inc(&F2FS_I_SB(inode)->inline_inode));	\
	} while (0)
#define stat_dec_inline_inode(inode)					\
	do {								\
		if (f2fs_has_inline_data(inode))			\
			(atomic_dec(&F2FS_I_SB(inode)->inline_inode));	\
	} while (0)
#define stat_inc_inline_dir(inode)					\
	do {								\
		if (inode->f2fs_has_inline_dentry())			\
			(atomic_inc(&F2FS_I_SB(inode)->inline_dir));	\
	} while (0)
#define stat_dec_inline_dir(inode)					\
	do {								\
		if (inode->f2fs_has_inline_dentry())			\
			(atomic_dec(&F2FS_I_SB(inode)->inline_dir));	\
	} while (0)
#define stat_inc_compr_inode(inode)					\
	do {								\
		if (f2fs_compressed_file(inode))			\
			(atomic_inc(&F2FS_I_SB(inode)->compr_inode));	\
	} while (0)
#define stat_dec_compr_inode(inode)					\
	do {								\
		if (f2fs_compressed_file(inode))			\
			(atomic_dec(&F2FS_I_SB(inode)->compr_inode));	\
	} while (0)
#define stat_add_compr_blocks(inode, blocks)				\
		(atomic64_add(blocks, &F2FS_I_SB(inode)->compr_blocks))
#define stat_sub_compr_blocks(inode, blocks)				\
		(atomic64_sub(blocks, &F2FS_I_SB(inode)->compr_blocks))
#define stat_inc_meta_count(sbi, blkaddr)				\
	do {								\
		if (blkaddr < sbi->SIT_I()->sit_base_addr)		\
			atomic_inc(&(sbi)->meta_count[META_CP]);	\
		else if (blkaddr < NM_I(sbi)->nat_blkaddr)		\
			atomic_inc(&(sbi)->meta_count[META_SIT]);	\
		else if (blkaddr < sbi->SM_I()->ssa_blkaddr)		\
			atomic_inc(&(sbi)->meta_count[META_NAT]);	\
		else if (blkaddr < sbi->SM_I()->main_blkaddr)		\
			atomic_inc(&(sbi)->meta_count[META_SSA]);	\
	} while (0)
#define stat_inc_seg_type(sbi, curseg)					\
		((sbi)->segment_count[(curseg)->alloc_type]++)
#define stat_inc_block_count(sbi, curseg)				\
		((sbi)->block_count[(curseg)->alloc_type]++)
#define stat_inc_inplace_blocks(sbi)					\
		(atomic_inc(&(sbi)->inplace_count))
#define stat_update_max_atomic_write(inode)				\
	do {								\
		int cur = F2FS_I_SB(inode)->atomic_files;	\
		int max = atomic_read(&F2FS_I_SB(inode)->max_aw_cnt);	\
		if (cur > max)						\
			atomic_set(&F2FS_I_SB(inode)->max_aw_cnt, cur);	\
	} while (0)
#define stat_inc_volatile_write(inode)					\
		(atomic_inc(&F2FS_I_SB(inode)->vw_cnt))
#define stat_dec_volatile_write(inode)					\
		(atomic_dec(&F2FS_I_SB(inode)->vw_cnt))
#define stat_update_max_volatile_write(inode)				\
	do {								\
		int cur = atomic_read(&F2FS_I_SB(inode)->vw_cnt);	\
		int max = atomic_read(&F2FS_I_SB(inode)->max_vw_cnt);	\
		if (cur > max)						\
			atomic_set(&F2FS_I_SB(inode)->max_vw_cnt, cur);	\
	} while (0)
#define stat_inc_seg_count(sbi, type, gc_type)				\
	do {								\
		struct f2fs_stat_info *si = F2FS_STAT(sbi);		\
		si->tot_segs++;						\
		if ((type) == SUM_TYPE_DATA) {				\
			si->data_segs++;				\
			si->bg_data_segs += (gc_type == BG_GC) ? 1 : 0;	\
		} else {						\
			si->node_segs++;				\
			si->bg_node_segs += (gc_type == BG_GC) ? 1 : 0;	\
		}							\
	} while (0)

#define stat_inc_tot_blk_count(si, blks)				\
	((si)->tot_blks += (blks))

#define stat_inc_data_blk_count(sbi, blks, gc_type)			\
	do {								\
		struct f2fs_stat_info *si = F2FS_STAT(sbi);		\
		stat_inc_tot_blk_count(si, blks);			\
		si->data_blks += (blks);				\
		si->bg_data_blks += ((gc_type) == BG_GC) ? (blks) : 0;	\
	} while (0)

#define stat_inc_node_blk_count(sbi, blks, gc_type)			\
	do {								\
		struct f2fs_stat_info *si = F2FS_STAT(sbi);		\
		stat_inc_tot_blk_count(si, blks);			\
		si->node_blks += (blks);				\
		si->bg_node_blks += ((gc_type) == BG_GC) ? (blks) : 0;	\
	} while (0)

int f2fs_build_stats(struct f2fs_sb_info *sbi);
void f2fs_destroy_stats(struct f2fs_sb_info *sbi);
void __init f2fs_create_root_stats(void);
void f2fs_destroy_root_stats(void);
void f2fs_update_sit_info(struct f2fs_sb_info *sbi);
#else
#define stat_inc_cp_count(si)				do { } while (0)
#define stat_inc_bg_cp_count(si)			do { } while (0)
#define stat_inc_call_count(si)				do { } while (0)
#define stat_inc_bggc_count(si)				do { } while (0)
#define stat_io_skip_bggc_count(sbi)			do { } while (0)
#define stat_other_skip_bggc_count(sbi)			do { } while (0)
#define stat_inc_dirty_inode(sbi, type)			do { } while (0)
#define stat_dec_dirty_inode(sbi, type)			do { } while (0)
#define stat_inc_total_hit(sbi)				do { } while (0)
#define stat_inc_rbtree_node_hit(sbi)			do { } while (0)
#define stat_inc_largest_node_hit(sbi)			do { } while (0)
#define stat_inc_cached_node_hit(sbi)			do { } while (0)
#define stat_inc_inline_xattr(inode)			do { } while (0)
#define stat_dec_inline_xattr(inode)			do { } while (0)
#define stat_inc_inline_inode(inode)			do { } while (0)
#define stat_dec_inline_inode(inode)			do { } while (0)
#define stat_inc_inline_dir(inode)			do { } while (0)
#define stat_dec_inline_dir(inode)			do { } while (0)
#define stat_inc_compr_inode(inode)			do { } while (0)
#define stat_dec_compr_inode(inode)			do { } while (0)
#define stat_add_compr_blocks(inode, blocks)		do { } while (0)
#define stat_sub_compr_blocks(inode, blocks)		do { } while (0)
#define stat_update_max_atomic_write(inode)		do { } while (0)
#define stat_inc_volatile_write(inode)			do { } while (0)
#define stat_dec_volatile_write(inode)			do { } while (0)
#define stat_update_max_volatile_write(inode)		do { } while (0)
#define stat_inc_meta_count(sbi, blkaddr)		do { } while (0)
#define stat_inc_seg_type(sbi, curseg)			do { } while (0)
#define stat_inc_block_count(sbi, curseg)		do { } while (0)
#define stat_inc_inplace_blocks(sbi)			do { } while (0)
#define stat_inc_seg_count(sbi, type, gc_type)		do { } while (0)
#define stat_inc_tot_blk_count(si, blks)		do { } while (0)
#define stat_inc_data_blk_count(sbi, blks, gc_type)	do { } while (0)
#define stat_inc_node_blk_count(sbi, blks, gc_type)	do { } while (0)

static inline int f2fs_build_stats(struct f2fs_sb_info *sbi) { return 0; }
static inline void f2fs_destroy_stats(struct f2fs_sb_info *sbi) { }
static inline void __init f2fs_create_root_stats(void) { }
static inline void f2fs_destroy_root_stats(void) { }
static inline void f2fs_update_sit_info(struct f2fs_sb_info *sbi) {}
#endif

extern const struct file_operations f2fs_dir_operations;
extern const struct file_operations f2fs_file_operations;
extern const struct inode_operations f2fs_file_inode_operations;
//extern const struct address_space_operations f2fs_dblock_aops;
//extern const struct address_space_operations f2fs_node_aops;
//extern const struct address_space_operations f2fs_meta_aops;
extern const struct inode_operations f2fs_dir_inode_operations;
extern const struct inode_operations f2fs_symlink_inode_operations;
extern const struct inode_operations f2fs_encrypted_symlink_inode_operations;
extern const struct inode_operations f2fs_special_inode_operations;
extern struct kmem_cache *f2fs_inode_entry_slab;

/*
 * inline.c
 */
bool f2fs_may_inline_data(struct inode *inode);
bool f2fs_may_inline_dentry(struct inode *inode);
void f2fs_do_read_inline_data(struct page *page, struct page *ipage);
inline void f2fs_truncate_inline_inode(inode *inode, page *ipage, UINT64 from)UNSUPPORT_0;
int f2fs_read_inline_data(struct inode *inode, struct page *page);
int f2fs_convert_inline_page(struct dnode_of_data *dn, struct page *page);
//int f2fs_convert_inline_inode(struct inode *inode);
int f2fs_try_convert_inline_dir(struct inode *dir, struct dentry *dentry);
int f2fs_write_inline_data(struct inode *inode, struct page *page);
int f2fs_recover_inline_data(struct inode *inode, struct page *npage);
struct f2fs_dir_entry *f2fs_find_in_inline_dir(struct inode *dir,
					const struct f2fs_filename *fname,
					struct page **res_page);
//<YUAN> move to Cf2fsDirInode member
//int f2fs_make_empty_inline_dir(struct inode *inode, struct inode *parent, struct page *ipage);
//int f2fs_add_inline_entry(inode *dir, const f2fs_filename *fname, f2fs_inode_info *inode, nid_t ino, umode_t mode);
void f2fs_delete_inline_entry(f2fs_dir_entry *dentry, page *ppage, f2fs_inode_info *dir, f2fs_inode_info *iinode);
bool f2fs_empty_inline_dir(struct inode *dir);
int f2fs_read_inline_dir(struct file *file, struct dir_context *ctx,
			struct fscrypt_str *fstr);
int f2fs_inline_data_fiemap(struct inode *inode,
			struct fiemap_extent_info *fieinfo,
			__u64 start, __u64 len);

/* shrinker.c */
unsigned long f2fs_shrink_count(struct shrinker *shrink, struct shrink_control *sc);
unsigned long f2fs_shrink_scan(struct shrinker *shrink,	struct shrink_control *sc);
// <YUAN> 移到CF2fFileSystem下成员函数
//void f2fs_join_shrinker(struct f2fs_sb_info *sbi);
//void f2fs_leave_shrinker(struct f2fs_sb_info *sbi);

/* extent_cache.c */
struct rb_entry *f2fs_lookup_rb_tree(struct rb_root_cached *root,
				struct rb_entry *cached_re, unsigned int ofs);
struct rb_node **f2fs_lookup_rb_tree_ext(struct f2fs_sb_info *sbi,
				struct rb_root_cached *root,
				struct rb_node **parent,
				unsigned long long key, bool *left_most);
struct rb_node **f2fs_lookup_rb_tree_for_insert(struct f2fs_sb_info *sbi,
				struct rb_root_cached *root,
				struct rb_node **parent,
				unsigned int ofs, bool *leftmost);
struct rb_entry *f2fs_lookup_rb_tree_ret(struct rb_root_cached *root,
		struct rb_entry *cached_re, unsigned int ofs,
		struct rb_entry **prev_entry, struct rb_entry **next_entry,
		struct rb_node ***insert_p, struct rb_node **insert_parent,
		bool force, bool *leftmost);
bool f2fs_check_rb_tree_consistence(struct f2fs_sb_info *sbi,
				struct rb_root_cached *root, bool check_key);
unsigned int f2fs_shrink_extent_tree(struct f2fs_sb_info *sbi, int nr_shrink);
void f2fs_init_extent_tree(struct inode* inode, struct page* ipage);
void f2fs_drop_extent_tree(f2fs_inode_info *inode);
unsigned int f2fs_destroy_extent_node(struct inode *inode);
void f2fs_destroy_extent_tree(struct inode *inode);
bool f2fs_lookup_extent_cache(struct inode *inode, pgoff_t pgofs, struct extent_info *ei);
void f2fs_update_extent_cache(dnode_of_data *dn);

void f2fs_update_extent_cache_range(dnode_of_data *dn, pgoff_t fofs, block_t blkaddr, unsigned int len);
void f2fs_init_extent_cache_info(struct f2fs_sb_info *sbi);
int __init f2fs_create_extent_cache(void);
void f2fs_destroy_extent_cache(void);

/* sysfs.c */
int __init f2fs_init_sysfs(void);
void f2fs_exit_sysfs(void);
int f2fs_register_sysfs(struct f2fs_sb_info *sbi);
void f2fs_unregister_sysfs(struct f2fs_sb_info *sbi);

/* verity.c */
extern const struct fsverity_operations f2fs_verityops;

/*
 * crypto support
 */
static inline bool f2fs_encrypted_file(struct inode *inode)
{
	return IS_ENCRYPTED(inode) && S_ISREG(inode->i_mode);
}

static inline void f2fs_set_encrypted_inode(struct inode *inode)
{
#ifdef CONFIG_FS_ENCRYPTION
	file_set_encrypt(inode);
	f2fs_set_inode_flags(inode);
#endif
}

/*
 * Returns true if the reads of the inode's data need to undergo some
 * postprocessing step, like decryption or authenticity verification.
 */
static inline bool f2fs_post_read_required(inode *inode)
{
	return f2fs_encrypted_file(inode) || fsverity_active(inode) ||	f2fs_compressed_file(inode);
}

/*
 * compress.c
 */
#ifdef CONFIG_F2FS_FS_COMPRESSION
bool f2fs_is_compressed_page(struct page *page);
struct page *f2fs_compress_control_page(struct page *page);
int f2fs_prepare_compress_overwrite(struct inode *inode,
			struct page **pagep, pgoff_t index, void **fsdata);
bool f2fs_compress_write_end(struct inode *inode, void *fsdata,
					pgoff_t index, unsigned copied);
int f2fs_truncate_partial_cluster(struct inode *inode, UINT64 from, bool lock);
void f2fs_compress_write_end_io(struct bio *bio, struct page *page);
bool f2fs_is_compress_backend_ready(struct inode *inode);
int f2fs_init_compress_mempool(void);
void f2fs_destroy_compress_mempool(void);
void f2fs_end_read_compressed_page(struct page *page, bool failed);
bool f2fs_cluster_is_empty(struct compress_ctx *cc);
bool f2fs_cluster_can_merge_page(struct compress_ctx *cc, pgoff_t index);
void f2fs_compress_ctx_add_page(struct compress_ctx *cc, struct page *page);
int f2fs_write_multi_pages(struct compress_ctx *cc,
						int *submitted,
						struct writeback_control *wbc,
						enum iostat_type io_type);
int f2fs_is_compressed_cluster(struct inode *inode, pgoff_t index);
int f2fs_read_multi_pages(struct compress_ctx *cc, struct bio **bio_ret,
				unsigned nr_pages, sector_t *last_block_in_bio,
				bool is_readahead, bool for_write);
struct decompress_io_ctx *f2fs_alloc_dic(struct compress_ctx *cc);
void f2fs_decompress_end_io(struct decompress_io_ctx *dic, bool failed);
void f2fs_put_page_dic(struct page *page);
int f2fs_init_compress_ctx(struct compress_ctx *cc);
void f2fs_destroy_compress_ctx(struct compress_ctx *cc, bool reuse);
void f2fs_init_compress_info(struct f2fs_sb_info *sbi);
int f2fs_init_page_array_cache(struct f2fs_sb_info *sbi);
void f2fs_destroy_page_array_cache(struct f2fs_sb_info *sbi);
int __init f2fs_init_compress_cache(void);
void f2fs_destroy_compress_cache(void);
#define inc_compr_inode_stat(inode)					\
	do {								\
		struct f2fs_sb_info *sbi = F2FS_I_SB(inode);		\
		sbi->compr_new_inode++;					\
	} while (0)
#define add_compr_block_stat(inode, blocks)				\
	do {								\
		struct f2fs_sb_info *sbi = F2FS_I_SB(inode);		\
		int diff = F2FS_I(inode)->i_cluster_size - blocks;	\
		sbi->compr_written_block += blocks;			\
		sbi->compr_saved_block += diff;				\
	} while (0)
#else
static inline bool f2fs_is_compressed_page(struct page *page) { return false; }
static inline bool f2fs_is_compress_backend_ready(struct inode *inode)
{
	if (!f2fs_compressed_file(inode))	return true;
	/* not support compression */
	return false;
}
static inline struct page *f2fs_compress_control_page(struct page *page)
{
	JCASSERT(0);
	return NULL;
	//WARN_ON_ONCE(1);
//	return ERR_PTR(-EINVAL);
}
static inline int f2fs_init_compress_mempool(void) { return 0; }
static inline void f2fs_destroy_compress_mempool(void) { }
static inline void f2fs_end_read_compressed_page(struct page *page, bool failed)
{
	//WARN_ON_ONCE(1);
}
static inline void f2fs_put_page_dic(struct page *page)
{
	//WARN_ON_ONCE(1);
}
static inline int f2fs_init_page_array_cache(struct f2fs_sb_info *sbi) { return 0; }
static inline void f2fs_destroy_page_array_cache(struct f2fs_sb_info *sbi) { }
static inline int __init f2fs_init_compress_cache(void) { return 0; }
static inline void f2fs_destroy_compress_cache(void) { }
#define inc_compr_inode_stat(inode)		do { } while (0)
#endif



static inline void set_compress_context(struct inode *inode)
{
	f2fs_sb_info *sbi = F2FS_I_SB(inode);

	f2fs_inode_info* fi = F2FS_I(inode);

	fi->i_compress_algorithm = F2FS_OPTION(sbi).compress_algorithm;
	fi->i_log_cluster_size = F2FS_OPTION(sbi).compress_log_size;
	fi->i_compress_flag = F2FS_OPTION(sbi).compress_chksum ? 1 << COMPRESS_CHKSUM : 0;
	fi->i_cluster_size =	1 << fi->i_log_cluster_size;
	if (fi->i_compress_algorithm == COMPRESS_LZ4 &&	F2FS_OPTION(sbi).compress_level)
		fi->i_compress_flag |= 	F2FS_OPTION(sbi).compress_level <<	COMPRESS_LEVEL_OFFSET;
	fi->i_flags |= F2FS_COMPR_FL;
	fi->set_inode_flag(FI_COMPRESSED_FILE);
	stat_inc_compr_inode(inode);
	inc_compr_inode_stat(inode);
	fi->f2fs_mark_inode_dirty_sync( true);
}
#if 0 //TODO

static inline bool f2fs_disable_compressed_file(struct inode *inode)
{
	struct f2fs_inode_info *fi = F2FS_I(inode);

	if (!f2fs_compressed_file(inode))
		return true;
	if (S_ISREG(inode->i_mode) &&
		(get_dirty_pages(inode) || atomic_read(&fi->i_compr_blocks)))
		return false;

	fi->i_flags &= ~F2FS_COMPR_FL;
	stat_dec_compr_inode(inode);
	clear_inode_flag(inode, FI_COMPRESSED_FILE);
	inode->f2fs_mark_inode_dirty_sync( true);
	return true;
}
#endif //TODO

#define F2FS_FEATURE_FUNCS(name, flagname) \
static inline int f2fs_sb_has_##name(struct f2fs_sb_info *sbi) \
{ \
	return F2FS_HAS_FEATURE(sbi, F2FS_FEATURE_##flagname); \
}

F2FS_FEATURE_FUNCS(encrypt, ENCRYPT);
F2FS_FEATURE_FUNCS(blkzoned, BLKZONED);
F2FS_FEATURE_FUNCS(extra_attr, EXTRA_ATTR);
F2FS_FEATURE_FUNCS(project_quota, PRJQUOTA);
F2FS_FEATURE_FUNCS(inode_chksum, INODE_CHKSUM);
F2FS_FEATURE_FUNCS(flexible_inline_xattr, FLEXIBLE_INLINE_XATTR);
F2FS_FEATURE_FUNCS(quota_ino, QUOTA_INO);
F2FS_FEATURE_FUNCS(inode_crtime, INODE_CRTIME);
F2FS_FEATURE_FUNCS(lost_found, LOST_FOUND);
F2FS_FEATURE_FUNCS(verity, VERITY);
F2FS_FEATURE_FUNCS(sb_chksum, SB_CHKSUM);
F2FS_FEATURE_FUNCS(casefold, CASEFOLD);
F2FS_FEATURE_FUNCS(compression, COMPRESSION);


#ifdef CONFIG_BLK_DEV_ZONED
static inline bool f2fs_blkz_is_seq(struct f2fs_sb_info *sbi, int devi,
				    block_t blkaddr)
{
	unsigned int zno = blkaddr >> sbi->log_blocks_per_blkz;

	return test_bit(zno, FDEV(devi).blkz_seq);
}
#endif

static inline bool f2fs_hw_should_discard(struct f2fs_sb_info *sbi)
{
	return f2fs_sb_has_blkzoned(sbi);
}

static inline bool f2fs_bdev_support_discard(IVirtualDisk *bdev)
{
#if 0
	return blk_queue_discard(bdev_get_queue(bdev)) || bdev_is_zoned(bdev);
#else
	return false;
#endif
}

static inline bool f2fs_hw_support_discard(struct f2fs_sb_info *sbi)
{
	int i;

	if (!sbi->f2fs_is_multi_device())
		return f2fs_bdev_support_discard(sbi->s_bdev);

	for (i = 0; i < sbi->s_ndevs; i++)
	{
		if (f2fs_bdev_support_discard(FDEV(i).m_disk))
			return true;
	}
	return false;
}
static inline bool f2fs_realtime_discard_enable(struct f2fs_sb_info *sbi)
{
	return (test_opt(sbi, DISCARD) && f2fs_hw_support_discard(sbi)) ||
					f2fs_hw_should_discard(sbi);
}


static inline bool f2fs_hw_is_readonly(struct f2fs_sb_info *sbi)
{
	//int i;
	//if (!f2fs_is_multi_device(sbi))	return bdev_read_only(sbi->s_bdev);
	//for (i = 0; i < sbi->s_ndevs; i++)
	//	if (bdev_read_only(FDEV(i).bdev))	return true;
	return false;
}

static inline bool f2fs_lfs_mode(struct f2fs_sb_info *sbi)
{
	return F2FS_OPTION(sbi).fs_mode == FS_MODE_LFS;
}

static inline bool f2fs_may_compress(struct inode *inode)
{
	if (IS_SWAPFILE(inode) || f2fs_is_pinned_file(inode) ||
				f2fs_is_atomic_file(inode) ||
				f2fs_is_volatile_file(inode))
		return false;
	return S_ISREG(inode->i_mode) || S_ISDIR(inode->i_mode);
}

static inline void f2fs_i_compr_blocks_update(struct inode *inode,	UINT64 blocks, bool add)
{
	int diff = boost::numeric_cast<int>(F2FS_I(inode)->i_cluster_size - blocks);
	f2fs_inode_info *fi = F2FS_I(inode);

	/* don't update i_compr_blocks if saved blocks were released */
	if (!add && !atomic_read(&fi->i_compr_blocks))
		return;

	if (add) {
		atomic_add(diff, &fi->i_compr_blocks);
		stat_add_compr_blocks(inode, diff);
	} else {
		atomic_sub(diff, &fi->i_compr_blocks);
		stat_sub_compr_blocks(inode, diff);
	}
	fi->f2fs_mark_inode_dirty_sync( true);
}

static inline int block_unaligned_IO(struct inode *inode, kiocb *iocb, iov_iter *iter)
{
	unsigned int i_blkbits = READ_ONCE(inode->i_blkbits);
	unsigned int blocksize_mask = (1 << i_blkbits) - 1;
	loff_t offset = iocb->ki_pos;
	unsigned long align = (unsigned long)offset | iov_iter_alignment(iter);

	return align & blocksize_mask;
}

static inline int allow_outplace_dio(struct inode *inode, struct kiocb *iocb, struct iov_iter *iter)
{
	struct f2fs_sb_info *sbi = F2FS_I_SB(inode);
	int rw = iov_iter_rw(iter);

	return (f2fs_lfs_mode(sbi) && (rw == WRITE) && !block_unaligned_IO(inode, iocb, iter));
}

static inline bool f2fs_force_buffered_io(struct inode *inode, struct kiocb *iocb, struct iov_iter *iter)
{
	struct f2fs_sb_info *sbi = F2FS_I_SB(inode);
	int rw = iov_iter_rw(iter);

	if (f2fs_post_read_required(inode))
		return true;
	if (sbi->f2fs_is_multi_device())
		return true;
	/*
	 * for blkzoned device, fallback direct IO to buffered IO, so
	 * all IOs can be serialized by log-structured write.
	 */
	if (f2fs_sb_has_blkzoned(sbi))		return true;
	if (f2fs_lfs_mode(sbi) && (rw == WRITE)) 
	{
		if (block_unaligned_IO(inode, iocb, iter))
			return true;
		if (F2FS_IO_ALIGNED(sbi))
			return true;
	}
	if (sbi->is_sbi_flag_set(SBI_CP_DISABLED))	return true;

	return false;
}

static inline bool f2fs_need_verity(struct inode *node, pgoff_t idx)
{
	return fsverity_active(node) && idx < DIV_ROUND_UP<loff_t>(node->i_size, PAGE_SIZE);
}

#ifdef CONFIG_F2FS_FAULT_INJECTION
extern void f2fs_build_fault_attr(struct f2fs_sb_info *sbi, unsigned int rate,	unsigned int type);
#else
#define f2fs_build_fault_attr(sbi, rate, type)		do { } while (0)
#endif

static inline bool is_journalled_quota(struct f2fs_sb_info *sbi)
{
#ifdef CONFIG_QUOTA
	if (f2fs_sb_has_quota_ino(sbi))
		return true;
	if (F2FS_OPTION(sbi).s_qf_names[USRQUOTA] ||
		F2FS_OPTION(sbi).s_qf_names[GRPQUOTA] ||
		F2FS_OPTION(sbi).s_qf_names[PRJQUOTA])
		return true;
#endif
	return false;
}

#define EUCLEAN		200

#define EFSBADCRC	EBADMSG		/* Bad CRC detected */
#define EFSCORRUPTED	EUCLEAN		/* Filesystem is corrupted */

//inline bool f2fs_sb_has_blkzoned(f2fs_sb_info* sbi) { return false; }
//inline bool f2fs_sb_has_casefold(f2fs_sb_info* sbi) { return false; }
//inline bool f2fs_sb_has_inode_chksum(f2fs_sb_info* sbi) { return true; }


inline CF2fsFileSystem* f2fs_inode_info::GetFs(void) 
{
//	return reinterpret_cast<f2fs_sb_info*>(i_sb->s_fs_info)->m_fs; 
	return m_sbi->m_fs; 
}


// help functions


template <typename T>
inline T* new_and_zero_array(size_t size)
{
	T* p = new T[size];
	if (p == nullptr) THROW_ERROR(ERR_MEM, L"failed on creating array");
	memset(p, 0, sizeof(T) * size);
	return p;
}



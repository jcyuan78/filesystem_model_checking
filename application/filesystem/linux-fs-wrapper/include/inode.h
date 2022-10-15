///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
#pragma once

/*
 * Inode state bits.  Protected by inode->i_lock
 *
 * Four bits determine the dirty state of the inode: I_DIRTY_SYNC, I_DIRTY_DATASYNC, I_DIRTY_PAGES, and I_DIRTY_TIME.
 *
 * Four bits define the lifetime of an inode.  Initially, inodes are I_NEW, until that flag is cleared.  I_WILL_FREE, I_FREEING and I_CLEAR are set at various stages of removing an inode.
 *
 * Two bits are used for locking and completion notification, I_NEW and I_SYNC.
 *
 * I_DIRTY_SYNC		Inode is dirty, but doesn't have to be written on fdatasync() (unless I_DIRTY_DATASYNC is also set). Timestamp updates are the usual cause.
 * 
 * I_DIRTY_DATASYNC	Data-related inode changes pending.  We keep track of these changes separately from I_DIRTY_SYNC so that we don't have to write inode on fdatasync() when only	e.g. the timestamps have changed.
 * 
 * I_DIRTY_PAGES	Inode has dirty pages.  Inode itself may be clean.
 * 
 * I_DIRTY_TIME		The inode itself only has dirty timestamps, and the lazytime mount option is enabled.  We keep track of this separately from I_DIRTY_SYNC in order to implement	lazytime.  This gets cleared if I_DIRTY_INODE (I_DIRTY_SYNC and/or I_DIRTY_DATASYNC) gets set.  I.e. either I_DIRTY_TIME *or* I_DIRTY_INODE can be set in i_state, but not both.  I_DIRTY_PAGES may still be set.
 * 
 * 
 * I_NEW		Serves as both a mutex and completion notification.	New inodes set I_NEW.  If two processes both create	the same inode, one of them will release its inode and wait for I_NEW to be released before returning. Inodes in I_WILL_FREE, I_FREEING or I_CLEAR state can also cause waiting on I_NEW, without I_NEW actually being set.  find_inode() uses this to prevent returning nearly-dead inodes.
 * 
 * I_WILL_FREE		Must be set when calling write_inode_now() if i_count is zero.  I_FREEING must be set when I_WILL_FREE is cleared.
 * 
 * I_FREEING		Set when inode is about to be freed but still has dirty	pages or buffers attached or the inode itself is still dirty.
 * 
 * I_CLEAR		Added by clear_inode().  In this state the inode is clean and can be destroyed.  Inode keeps I_FREEING.
 *			Inodes that are I_WILL_FREE, I_FREEING or I_CLEAR are prohibited for many purposes.  iget() must wait for the inode to be completely released, then create it anew.  Other functions will just ignore such inodes, if appropriate.  I_NEW is used for waiting.
 *
 * I_SYNC		Writeback of inode is running. The bit is set during data writeback, and cleared with a wakeup on the bit address once it is done. The bit is also used to pin the inode in memory for flusher thread.
 *
 * I_REFERENCED		Marks the inode as recently references on the LRU list.
 *
 * I_DIO_WAKEUP		Never set.  Only used as a key for wait_on_bit().
 *
 * I_WB_SWITCH		Cgroup bdi_writeback switching in progress.  Used to synchronize competing switching instances and to tell wb stat updates to grab the i_pages lock.  See inode_switch_wbs_work_fn() for details.
 *
 * I_OVL_INUSE		Used by overlayfs to get exclusive ownership on upper and work dirs among overlayfs mounts.
 *
 * I_CREATING		New object's inode in the middle of setting up.
 *
 * I_DONTCACHE		Evict inode as soon as it is not used anymore.
 *
 * I_SYNC_QUEUED	Inode is queued in b_io or b_more_io writeback lists. Used to detect that mark_inode_dirty() should not move inode between dirty lists.
 *
 * Q: What is the difference between I_WILL_FREE and I_FREEING?
 */
#define I_DIRTY_SYNC		(1 << 0)
#define I_DIRTY_DATASYNC	(1 << 1)
#define I_DIRTY_PAGES		(1 << 2)
#define __I_NEW				3
#define I_NEW				(1 << __I_NEW)
#define I_WILL_FREE			(1 << 4)
#define I_FREEING			(1 << 5)
#define I_CLEAR				(1 << 6)
#define __I_SYNC			7
#define I_SYNC				(1 << __I_SYNC)
#define I_REFERENCED		(1 << 8)
#define __I_DIO_WAKEUP		9
#define I_DIO_WAKEUP		(1 << __I_DIO_WAKEUP)
#define I_LINKABLE			(1 << 10)
#define I_DIRTY_TIME		(1 << 11)
#define I_WB_SWITCH			(1 << 13)
#define I_OVL_INUSE			(1 << 14)
#define I_CREATING			(1 << 15)
#define I_DONTCACHE			(1 << 16)
#define I_SYNC_QUEUED		(1 << 17)

#define I_DIRTY_INODE		(I_DIRTY_SYNC | I_DIRTY_DATASYNC)
#define I_DIRTY				(I_DIRTY_INODE | I_DIRTY_PAGES)
#define I_DIRTY_ALL			(I_DIRTY | I_DIRTY_TIME)

void __mark_inode_dirty(struct inode*, int);

class CInodeManager;

/* Keep mostly read-only and often accessed (especially for the RCU path lookup and 'stat' data) fields at the beginning of the 'struct inode' */
struct inode
{
public:
	inode(void);
	virtual ~inode(void);
	// bad inode:在设计中，用i_op==&bad_inode_op判断是否bad。这里增加一个bool变量判断
public:
	bool is_bad_inode(void) { return m_is_bad; }
	void make_bad_inode(void);
protected:
	bool m_is_bad = false;

public:

	umode_t				i_mode;
	unsigned short		i_opflags;
	//kuid_t			i_uid;
	//kgid_t			i_gid;
	unsigned int		i_flags;

#ifdef CONFIG_FS_POSIX_ACL
	struct posix_acl* i_acl;
	struct posix_acl* i_default_acl;
#endif

	//<YUAN> 通过虚函数实现op
	//struct inode_operations	*i_op;
	virtual dentry* lookup(dentry*, unsigned int) UNSUPPORT_1(dentry*);
	virtual const char* get_link(dentry*, /*struct inode*,*/ delayed_call*)UNSUPPORT_1(const char*);
	virtual int permission(user_namespace*, /*struct inode*,*/ int)UNSUPPORT_1(int);
	//	virtual posix_acl* get_acl(/*struct inode*,*/ int, bool);
	virtual int readlink(dentry*, char __user*, int)UNSUPPORT_1(int);
	virtual int create(user_namespace*, /*struct inode*,*/ dentry*, umode_t, bool)UNSUPPORT_1(int);
	virtual int link(dentry*, /*struct inode*,*/ dentry*)UNSUPPORT_1(int);
	virtual int unlink(/*struct inode*, */ dentry*)UNSUPPORT_1(int);
	virtual int symlink(user_namespace*, /*struct inode*, */dentry*, const char*)UNSUPPORT_1(int);
	virtual int mkdir(user_namespace*, /*struct inode*, */dentry*, umode_t)UNSUPPORT_1(int);
	virtual int rmdir(/*struct inode*, */dentry*)UNSUPPORT_1(int);
	virtual int mknod(user_namespace*, /*struct inode*, */dentry*, umode_t, dev_t)UNSUPPORT_1(int);
	virtual int rename(user_namespace*, /*struct inode*, */dentry*, inode*, dentry*, unsigned int)UNSUPPORT_1(int);
	virtual int setattr(user_namespace*, dentry*, iattr*) = 0;
	virtual int getattr(user_namespace*, const path*, kstat*, u32, unsigned int) = 0;
	virtual ssize_t listxattr(dentry*, char*, size_t)UNSUPPORT_1(size_t);
	virtual int fiemap(/*struct inode*, */fiemap_extent_info*, u64 start, u64 len)UNSUPPORT_1(int);
	virtual int update_time(/*struct inode*, */timespec64*, int)UNSUPPORT_1(int);
	virtual int atomic_open(/*struct inode*, */dentry*, file*, unsigned open_flag, umode_t create_mode)UNSUPPORT_1(int);
	virtual int tmpfile(user_namespace*, /*struct inode*,*/ dentry*, umode_t)UNSUPPORT_1(int);
	//	virtual int set_acl(user_namespace*, /*struct inode*,*/ posix_acl*, int)UNSUPPORT_1(int);
	virtual int fileattr_set(user_namespace* mnt_userns, dentry* dentry, fileattr* fa)UNSUPPORT_1(int);
	virtual int fileattr_get(dentry* dentry, fileattr* fa)UNSUPPORT_1(int);

	// file operations
	struct module* owner;
	virtual loff_t llseek(file*, loff_t, int) { UNSUPPORT_1(loff_t); };
	virtual ssize_t read(file*, char __user*, size_t, loff_t*) { UNSUPPORT_1(ssize_t); };;
	virtual ssize_t write(file*, const char __user*, size_t, loff_t*) { UNSUPPORT_1(ssize_t); };;
	virtual ssize_t read_iter(kiocb*, iov_iter*) { UNSUPPORT_1(ssize_t); };;
	virtual ssize_t write_iter(kiocb*, iov_iter*) { UNSUPPORT_1(ssize_t); };
	virtual int iopoll(kiocb* kiocb, bool spin) { UNSUPPORT_1(int); }
	virtual int iterate(file*, dir_context*) { UNSUPPORT_1(int); }
	virtual int iterate_shared(file*, dir_context*) { UNSUPPORT_1(int); }
	//__poll_t (*poll) ( file *,  poll_table_struct *);
	virtual long unlocked_ioctl(file*, unsigned int, unsigned long) { UNSUPPORT_1(long); }
	virtual long compat_ioctl(file*, unsigned int, unsigned long) { UNSUPPORT_1(long); }
	virtual int mmap(file*, vm_area_struct*) { UNSUPPORT_1(int); }
	unsigned long mmap_supported_flags;
	virtual int open(inode*, file*) { UNSUPPORT_1(int); }
	virtual int flush(file*, fl_owner_t id) { UNSUPPORT_1(int); }
	virtual int release_file(file*) = 0;
	virtual int fsync(file*, loff_t start, loff_t end, int datasync) = 0;
	virtual int fasync(int, file*, int) { UNSUPPORT_1(int); }
	virtual int lock(file*, int, file_lock*) { UNSUPPORT_1(int); }
	virtual ssize_t sendpage(file*, page*, int, size_t, loff_t*, int) { UNSUPPORT_1(ssize_t); };
	virtual unsigned long get_unmapped_area(file*, unsigned long, unsigned long, unsigned long, unsigned long) 
	{ UNSUPPORT_1(unsigned long); }
	virtual int check_flags(int) { UNSUPPORT_1(int); }
	virtual int flock(file*, int, file_lock*) { UNSUPPORT_1(int); }
	virtual ssize_t splice_write(pipe_inode_info*, file*, loff_t*, size_t, unsigned int) { UNSUPPORT_1(ssize_t); };;
	virtual ssize_t splice_read(file*, loff_t*, pipe_inode_info*, size_t, unsigned int) { UNSUPPORT_1(ssize_t); };
	virtual int setlease(file*, long, file_lock**, void**) { UNSUPPORT_1(int); }
	virtual long fallocate(int mode, loff_t offset, loff_t len) { UNSUPPORT_1(long); }
	virtual void show_fdinfo(seq_file* m, file* f) { UNSUPPORT_0; }
#ifndef CONFIG_MMU
	virtual unsigned mmap_capabilities(file*) { UNSUPPORT_1(unsigned); }
#endif
	virtual ssize_t copy_file_range(file*, loff_t, file*, loff_t, size_t, unsigned int) { UNSUPPORT_1(ssize_t); };
	virtual loff_t remap_file_range(file* file_in, loff_t pos_in, file* file_out, loff_t pos_out,
		loff_t len, unsigned int remap_flags)
	{
		UNSUPPORT_1(loff_t);
	};
	virtual int fadvise(file*, loff_t, loff_t, int) { UNSUPPORT_1(int); }
	inline bool is_dir(void) const { return S_ISDIR(i_mode); }

	friend class CInodeManager;
	friend address_space* META_MAPPING(struct f2fs_sb_info* sbi);
	friend address_space* NODE_MAPPING(struct f2fs_sb_info* sbi);

	virtual void remove_alias(dentry* ddentry) = 0;

// ==== member functions ==============================================================================================
	// ---- fs.h
public:
	inline void invalidate_remote_inode(void)
	{
		if (S_ISREG(i_mode) || S_ISDIR(i_mode) || S_ISLNK(i_mode))
			invalidate_mapping_pages(i_mapping, 0, -1);
	}

	/* Flush file data before changing attributes.  Caller must hold any locks required to prevent further writes to
	   this file until we're done setting flags.  */
	inline int inode_drain_writes(void)
	{
		inode_dio_wait(this);
		return filemap_write_and_wait(i_mapping);
	}
	inline void mark_inode_dirty(void)	{__mark_inode_dirty(this, I_DIRTY); }
	inline void mark_inode_dirty_sync(void) {__mark_inode_dirty(this, I_DIRTY_SYNC); }
	// ---- inode.cpp
	void inode_nohighmem(void);

	// end of file operations

public:
	super_block* i_sb = NULL;

public:
	bool mapping_equal(address_space* mapping) { return i_mapping == mapping; }
	address_space* get_mapping(void) { return i_mapping; }
protected:
	address_space* i_mapping = NULL;
public:

#ifdef CONFIG_SECURITY
	void* i_security;
#endif

	/* Stat data, not accessed from path walking */
	unsigned long		i_ino;	//inode number
	/* Filesystems may only read i_nlink directly.  They shall use the following functions for modification:
	 *    (set|clear|inc|drop)_nlink
	 *    inode_(inc|dec)_link_count	 */
public:
	void clear_nlink(void);
	void set_nlink(unsigned int nlink);

protected:
	// in inode.cpp
	void inc_nlink(void);
	void drop_nlink(void);
	inline void inode_inc_link_count(void)
	{
		inc_nlink();
		mark_inode_dirty();
	}

	inline void inode_dec_link_count(void)
	{
		drop_nlink();
		mark_inode_dirty();
	}
public:
	union
	{
		const unsigned int i_nlink = 0;
		unsigned int __i_nlink;
	};
	dev_t			i_rdev;
	loff_t			i_size;		// 文件大小
	timespec64		i_atime;
	timespec64		i_mtime;
	timespec64		i_ctime;
	spinlock_t		i_lock;	/* i_blocks, i_bytes, maybe i_size */
	unsigned short          i_bytes;
	u8			i_blkbits;
	u8			i_write_hint;
	blkcnt_t		i_blocks;

#ifdef __NEED_I_SIZE_ORDERED
	seqcount_t		i_size_seqcount;
#endif

// ==== 状态管理 ====
protected:
	unsigned long		i_state;
	HANDLE				m_event_state;		//当i_state改变时通知
public:
	inline void SetState(unsigned long state_bmp) { i_state |= state_bmp; }
	inline void ClearState(unsigned long state_bmp) { i_state &= ~state_bmp; }
	void SetStateNotify(unsigned long state_bmp);
	void ClearStateNotify(unsigned long state_bmp);
	inline unsigned long TestState(unsigned long state_bmp) { return i_state & state_bmp; }
	void WaitForState(int state_id, DWORD timeout=1000);

	void __wait_on_freeing_inode(void);


	/* Misc */
	rw_semaphore		i_rwsem;

	LONGLONG	dirtied_when;			/* jiffies of first dirtying */
	LONGLONG	dirtied_time_when;

	CInodeManager* m_manager = nullptr;
	inline bool inode_unhashed(void) const { return m_manager == nullptr; }

	struct list_head	i_io_list;	/* backing dev IO list */
#ifdef CONFIG_CGROUP_WRITEBACK
	struct bdi_writeback* i_wb;		/* the associated cgroup wb */

	/* foreign inode detection, see wbc_detach_inode() */
	int			i_wb_frn_winner;
	u16			i_wb_frn_avg_time;
	u16			i_wb_frn_history;
#endif
	struct list_head	i_lru;		/* inode LRU list */
	struct list_head	i_sb_list;
	struct list_head	i_wb_list;	/* backing dev writeback list */
	union
	{
		struct hlist_head	i_dentry;
		//		struct rcu_head		i_rcu;
	};
	atomic64_t		i_version;
	atomic64_t		i_sequence; /* see futex */
	atomic_t		i_count;
	atomic_t		i_dio_count;
	atomic_t		i_writecount;
#if defined(CONFIG_IMA) || defined(CONFIG_FILE_LOCKING)
	atomic_t		i_readcount; /* struct files open RO */
#endif
	//<YUAN>通过虚函数实现op
	//union {
	//	const struct file_operations	*i_fop;	/* former ->i_op->default_file_ops */
	//	void (*free_inode)(struct inode *);
	//};
	struct file_lock_context* i_flctx;
	//address_space	i_data;
	struct list_head	i_devices;
	union
	{
		struct pipe_inode_info* i_pipe;
		struct cdev* i_cdev;
		char* i_link;
		unsigned		i_dir_seq;
	};

	__u32			i_generation;

#ifdef CONFIG_FSNOTIFY
	__u32			i_fsnotify_mask; /* all events this inode cares about */
	struct fsnotify_mark_connector __rcu* i_fsnotify_marks;
#endif

#ifdef CONFIG_FS_ENCRYPTION
	struct fscrypt_info* i_crypt_info;
#endif

#ifdef CONFIG_FS_VERITY
	struct fsverity_info* i_verity_info;
#endif

	void* i_private; /* fs or device private pointer */
};

//__randomize_layout;

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
#pragma once



/* Keep mostly read-only and often accessed (especially for the RCU path lookup and 'stat' data) fields at the
	beginning of the 'struct inode' */
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
	virtual int setattr(user_namespace*, dentry*, iattr*)UNSUPPORT_1(int);
	virtual int getattr(user_namespace*, const path*, kstat*, u32, unsigned int)UNSUPPORT_1(int);
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
	virtual int release_file(inode*, file*) { UNSUPPORT_1(int); }
	virtual int fsync(file*, loff_t start, loff_t end, int datasync) { UNSUPPORT_1(int); }
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

	friend class CInodeManager;
	friend address_space* META_MAPPING(struct f2fs_sb_info* sbi);
	friend address_space* NODE_MAPPING(struct f2fs_sb_info* sbi);

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
	// ---- inode.cpp
	void inode_nohighmem(void);
protected:

	// inline functions
	void inc_nlink(void);

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


	/* Misc */
	rw_semaphore		i_rwsem;

	unsigned long		dirtied_when;	/* jiffies of first dirtying */
	unsigned long		dirtied_time_when;

	struct hlist_node	i_hash;
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

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
#pragma once

class CF2fsFileSystem;
struct dnode_of_data;

enum inode_type {
	DIR_INODE,			/* for dirty dir inode */
	FILE_INODE,			/* for dirty regular/symlink inode */
	DIRTY_META,			/* for all dirtied inode metadata */
	ATOMIC_FILE,			/* for all atomic files */
	NR_INODE_TYPE,
};

class f2fs_inode_info : public inode
{
public:
	f2fs_inode_info(f2fs_sb_info * sbi, address_space * mapping = NULL);
	f2fs_inode_info(f2fs_sb_info * sbi, UINT32 ino);
	f2fs_inode_info(const f2fs_inode_info & src);
	virtual ~f2fs_inode_info(void);

public:

protected:
	CF2fsFileSystem* GetFs(void);

// virtual members
public:
	virtual int fsync(file*, loff_t start, loff_t end, int datasync);
	virtual int release_file(file* filp);
	virtual int setattr(user_namespace*, dentry*, iattr*);
	virtual int getattr(user_namespace*, const path*, kstat*, u32, unsigned int);


public:
//	struct inode vfs_inode;		/* serve a vfs inode */
	unsigned long i_flags;		/* keep an inode flags for ioctl */
	unsigned char i_advise;		/* use to give file attribute hints */
	// TODO for dir only, move to CF2fsDirInode
	unsigned char i_dir_level;	/* use for dentry level for large dir */ 
	unsigned int i_current_depth;	/* only for directory depth */
	/* for gc failure statistic */
	unsigned int i_gc_failures[MAX_GC_FAILURE];
	unsigned int i_pino;		/* parent inode number */
	umode_t i_acl_mode;		/* keep file acl mode temporarily */
	f2fs_sb_info* m_sbi;

	/* Use below internally in f2fs*/
	//unsigned long flags[BITS_TO_LONGS(FI_MAX)];	/* use to pass per-file flags */
	//unsigned long flags[BITS_TO_<long>(FI_MAX)];	/* use to pass per-file flags */
	UINT64 flags;
	semaphore  i_sem;	/* protect fi info */
	atomic_t dirty_pages;		/* # of dirty pages */
	f2fs_hash_t chash;		/* hash value of given file name */
	unsigned int clevel;		/* maximum level of given file name */
	struct task_struct* task;	/* lookup and create consistency */
//	struct task_struct* cp_task;	/* separate cp/wb IO stats*/
	nid_t i_xattr_nid;		/* node id that contains xattrs */
	loff_t	last_disk_size;		/* lastly written file size */
	CRITICAL_SECTION i_size_lock;		/* protect last_disk_size */

#ifdef CONFIG_QUOTA
	struct dquot* i_dquot[MAXQUOTAS];

	/* quota space reservation, managed internally by quota code */
	qsize_t i_reserved_quota;
#endif
	//struct list_head dirty_list;	/* dirty list for dirs and files */
	//struct list_head gdirty_list;	/* linked in global dirty list */
	struct list_head inmem_ilist;	/* list for inmem inodes */
	struct list_head inmem_pages;	/* inmemory pages managed by f2fs */
	struct task_struct* inmem_task;	/* store inmemory task */
	//用bool标记inode是否给加入相应的list
	bool m_in_list[NR_INODE_TYPE];
	HANDLE /*mutex*/ inmem_lock;	/* lock for inmemory pages */
	struct extent_tree* extent_tree;	/* cached extent_tree entry */

	/* avoid racing between foreground op and gc */
	SRWLOCK /*semaphore*/  i_gc_rwsem[2];
	semaphore  i_mmap_sem;
	semaphore  i_xattr_sem; /* avoid racing between reading and changing EAs */

	int i_extra_isize;		/* size of extra space located in i_addr */
#if 0 //磁盘使用配额设置，暂不支持
	kprojid_t i_projid;		/* id for project quota */
#endif
	int i_inline_xattr_size;	/* inline xattr size */
	time64_t i_crtime;	/* inode creation time */
	time64_t i_disk_time[4];/* inode disk times */
	/* for file compress */
	atomic_t i_compr_blocks;		/* # of compressed blocks */
	unsigned char i_compress_algorithm;	/* algorithm type */
	unsigned char i_log_cluster_size;	/* log of cluster size */
	unsigned char i_compress_level;		/* compress level (lz4hc,zstd) */
	unsigned short i_compress_flag;		/* compress flag */
	unsigned int i_cluster_size;		/* cluster size */


public:
	inline bool is_dir(void) const {	return S_ISDIR(i_mode);	}
//	inline void SetMapping(address_space* mapping) { JCASSERT(mapping && !i_mapping); i_mapping = mapping; }
	inline void get_inline_info(f2fs_inode* ri)
	{
		if (ri->i_inline & F2FS_INLINE_XATTR)		set_bit(FI_INLINE_XATTR,  &flags);
		if (ri->i_inline & F2FS_INLINE_DATA)		set_bit(FI_INLINE_DATA,   &flags);
		if (ri->i_inline & F2FS_INLINE_DENTRY)		set_bit(FI_INLINE_DENTRY, &flags);
		if (ri->i_inline & F2FS_DATA_EXIST)			set_bit(FI_DATA_EXIST,    &flags);
		if (ri->i_inline & F2FS_INLINE_DOTS)		set_bit(FI_INLINE_DOTS,   &flags);
		if (ri->i_inline & F2FS_EXTRA_ATTR)			set_bit(FI_EXTRA_ATTR,    &flags);
		if (ri->i_inline & F2FS_PIN_FILE)			set_bit(FI_PIN_FILE,      &flags);
	}

	inline int is_inode_flag_set(int flag) { return test_bit(flag, &flags);	}
	inline int f2fs_has_inline_dentry(void) {return is_inode_flag_set(FI_INLINE_DENTRY); }
	inline int f2fs_has_inline_dots(void) {	return is_inode_flag_set(FI_INLINE_DOTS); }
	inline void f2fs_i_depth_write(unsigned int depth)
	{
		i_current_depth = depth;
		f2fs_mark_inode_dirty_sync(true);
	}

	void f2fs_mark_inode_dirty_sync(bool sync);
	inline void set_file(int type) {	i_advise |= type;	f2fs_mark_inode_dirty_sync(true);	}
	inline void clear_file(int type)	{ i_advise &= ~type; f2fs_mark_inode_dirty_sync(true);	}

	inline void f2fs_i_pino_write(nid_t pino)
	{
		i_pino = pino;
		f2fs_mark_inode_dirty_sync(true);
	}

	inline void f2fs_i_links_write(bool inc)
	{
		if (inc)	inc_nlink();
		else		drop_nlink(this);
		f2fs_mark_inode_dirty_sync(true);
	}
	/*__mark_inode_dirty expects inodes to be hashed.  Since we don't want special inodes in the fileset inode space, 
	  we make them appear hashed, but do not put on any lists.  hlist_del() will work fine and require no locking. */
	inline void inode_fake_hash(void) 	{ hlist_add_fake(&i_hash); }
	inline loff_t GetFileSize(void) const { return i_size; }
	inline DWORD GetFileSizeHi(void) const { return HIDWORD(i_size); }
	inline DWORD GetFileSizeLo(void) const { return LODWORD(i_size); }
	DWORD GetFileAttribute(void) const;
	void SetFileAttribute(DWORD attr);

// ==== linux/fs.h ====
	inline void inode_lock(void) { down_write(&i_rwsem); }

	inline void inode_unlock(void) {up_write(&i_rwsem); }
// ==== f2fs.h ==== inline
//	static inline void __mark_inode_dirty_flag(inode* inode, int flag, bool set)	=> 两份都保留
	inline void __mark_inode_dirty_flag(int flag, bool set)
	{
		switch (flag)
		{
		case FI_INLINE_XATTR:
		case FI_INLINE_DATA:
		case FI_INLINE_DENTRY:
		case FI_NEW_INODE:
			if (set) return;
			//		fallthrough;
		case FI_DATA_EXIST:
		case FI_INLINE_DOTS:
		case FI_PIN_FILE:
			f2fs_mark_inode_dirty_sync(true);
		}
	}
 
	//static inline void set_inode_flag(inode* node, int flag) => 两份都保留
	inline void set_inode_flag(int flag)
	{
		set_bit(flag, &flags);
		__mark_inode_dirty_flag(flag, true);
	}
	//inline void f2fs_i_size_write(struct inode* inode, loff_t i_size)
	inline void f2fs_i_size_write(loff_t i_size)
	{
		bool clean = !is_inode_flag_set(FI_DIRTY_INODE);
		bool recover = is_inode_flag_set(FI_AUTO_RECOVER);

		if (i_size_read(this) == i_size)		return;

		i_size_write(this, i_size);
		f2fs_mark_inode_dirty_sync(true);
		if (clean || recover)	set_inode_flag(FI_AUTO_RECOVER);
	}

//	static inline bool f2fs_skip_inode_update(f2fs_inode_info* inode, int dsync)
	inline bool f2fs_skip_inode_update(int dsync);

// ==== inode.cpp ==
	void f2fs_update_inode(/*struct inode* inode, */page* node_page);
	void f2fs_update_inode_page(/*struct inode* inode*/);
	int f2fs_write_inode(writeback_control* wbc);

protected:
	void __set_inode_rdev(/*struct inode* inode, */ f2fs_inode* ri);


// ==== data.cpp ====
public:
	page* f2fs_get_new_data_page(page* ipage, pgoff_t index, bool new_i_size);
	page* f2fs_find_data_page(pgoff_t index);
	//-- tobe protected;
	page* f2fs_get_read_data_page(pgoff_t index, int op_flags, bool for_write);
	page* f2fs_get_lock_data_page(pgoff_t index, bool for_write);


// ==== namei.cpp ===
	void set_file_temperature(const std::wstring& name);
	int _internal_new_inode(f2fs_inode_info* dir, umode_t mode);


// ==== for dir, dir.cpp ====
	int f2fs_setup_filename(const qstr* iname, int lookup, f2fs_filename* fname);
	f2fs_dir_entry* find_in_level(unsigned int level, const f2fs_filename* fname, page** res_page);
	f2fs_dir_entry* find_in_block(page* dentry_page, const f2fs_filename* fname, int* max_slots);
	int __f2fs_setup_filename(const fscrypt_name* crypt_name, f2fs_filename* fname) const;
	int f2fs_init_casefolded_name(f2fs_filename* fname) const;
	page* f2fs_init_inode_metadata(inode* dir, const f2fs_filename* fname, page* dpage);
//	void f2fs_update_parent_metadata(f2fs_inode_info* inode, unsigned int current_depth);

// ==== inline.cpp ====
	int f2fs_convert_inline_inode(void);
	//-- tobe protected
	int f2fs_move_inline_dirents(page* ipage, void* inline_dentry);

// ==== gc.cpp ====
	//-- tobe protected
	int ra_data_block(pgoff_t index);
	int move_data_block(block_t bidx, int gc_type, unsigned int segno, int off);

// ==== file.cpp ====
	//-- tobe protected
	int truncate_partial_data_page(u64 from, bool cache_only);
	int f2fs_truncate_hole(pgoff_t pg_start, pgoff_t pg_end);
	int f2fs_do_sync_file(struct file* file, loff_t start, loff_t end, int datasync, bool atomic);
	void try_to_fix_pino(void);

	int get_parent_ino(nid_t* pino);


// ==== node.cpp ====
	// 创建一个当前inode对应的ondisk page，存放inode的ondisk data
	page* f2fs_new_inode_page(void);
	page* f2fs_new_node_page(dnode_of_data* dn, unsigned int ofs);

// ==== extent_cache.cpp ====
	void f2fs_update_extent_tree_range(pgoff_t fofs, block_t blkaddr, unsigned int len);

// ==== super.cpp ====
	int f2fs_inode_dirtied(bool sync);


// ==== 由于改变inode::i_mapping的访问规则，添加必要的访问函数
	int filemap_fdatawrite(void) { return i_mapping->filemap_fdatawrite(); }
	void flush_inline_data(void);

#ifdef _DEBUG
public:
	// 调试信息，
	std::wstring  m_description;

#endif


};

class lock_inode
{
public:
	lock_inode(inode& node) : m_node(&node) {}
	void lock(void) { down_write(&m_node->i_rwsem); }
	void unlock(void) { up_write(&m_node->i_rwsem); }
protected:
	inode* m_node;
};


class Cf2fsDirInode : public f2fs_inode_info
{
public:
	Cf2fsDirInode(const f2fs_inode_info& src) : f2fs_inode_info(src) {}
	Cf2fsDirInode(f2fs_sb_info * sbi);
	virtual ~Cf2fsDirInode(void) {}

public:
	//struct inode_operations	*i_op;
	virtual dentry* lookup(dentry*, unsigned int);
	virtual const char* get_link(dentry*, /*struct inode*,*/ delayed_call*)UNSUPPORT_1(const char*);
	virtual int permission(user_namespace*, /*struct inode*,*/ int)UNSUPPORT_1(int);
	//	virtual posix_acl* get_acl(/*struct inode*,*/ int, bool);
	virtual int readlink(dentry*, char __user*, int)UNSUPPORT_1(int);
	virtual int create(user_namespace*, dentry*, umode_t, bool);
	virtual int link(dentry*, /*struct inode*,*/ dentry*)UNSUPPORT_1(int);
	virtual int unlink(dentry*);
	virtual int symlink(user_namespace*, /*struct inode*, */dentry*, const char*)UNSUPPORT_1(int);
	virtual int mkdir(user_namespace*, /*struct inode*, */dentry*, umode_t);
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

public:
	//unsigned char i_dir_level;	/* use for dentry level for large dir */


// ==== namei.cpp ===
protected:
	int f2fs_prepare_lookup(dentry* dentry, f2fs_filename* fname);
	int __recover_dot_dentries(nid_t pino);

// ==== dir.cpp ====
public:
	f2fs_dir_entry* f2fs_find_entry(const qstr* child, page** res_page);
	int f2fs_do_add_link(const qstr* name, f2fs_inode_info* inode, nid_t ino, umode_t mode);
	f2fs_dir_entry* __f2fs_find_entry(const f2fs_filename* fname, page** res_page);
	// node以name为文件名，加入到当前node中
	int f2fs_add_dentry(const f2fs_filename* fname, f2fs_inode_info* node, nid_t ino, umode_t mode);
	// node以name为文件名，加入到当前node中
	int f2fs_add_regular_entry(const f2fs_filename* fname, f2fs_inode_info* node, nid_t ino, umode_t mode);
	int make_empty_dir(inode* parent, page* ppage);
	void f2fs_update_parent_metadata(f2fs_inode_info* inode, unsigned int current_depth);
protected:
	void f2fs_do_make_empty_dir(inode* parent, f2fs_dentry_ptr* d);

// ==== inline.cpp ====
	int f2fs_make_empty_inline_dir(inode* parent, page* ipage);
	int f2fs_add_inline_entry(const f2fs_filename* fname, f2fs_inode_info* inode, nid_t ino, umode_t mode);


protected:
	UINT64 dir_blocks(void)	{return ((unsigned long long) (i_size_read(this) + PAGE_SIZE - 1)) >> PAGE_SHIFT; }
	//从f2fs.h的f2fs_add_link(dentry* entry, f2fs_inode_info* inode)移植。
	// entry和inode为需要添加的子目录/文件的entry和inode，父目录指针通过entry的d_parent获取。
	// 这里通过this指针传递parent的inode以优化处理
	inline int f2fs_add_link(dentry* entry, f2fs_inode_info* inode)
	{
		if (fscrypt_is_nokey_name(entry))	return -ENOKEY;
#ifdef _DEBUG
//			f2fs_inode_info* fi = (d_inode(entry->d_parent));
		Cf2fsDirInode* di = dynamic_cast<Cf2fsDirInode*>(d_inode(entry->d_parent));
		if ( (di == NULL) || (di!=this) ) THROW_ERROR(ERR_APP, L"only dir inode support this feature");
#endif
		return f2fs_do_add_link( &entry->d_name, inode, inode->i_ino, inode->i_mode);
	}

#ifdef _DEBUG
public:
	void DebugListItems(void);
#else
	void DebugListItems(void) {};

#endif //_DEBUG
};

class Cf2fsFileNode : public f2fs_inode_info
{
public:
	Cf2fsFileNode(const f2fs_inode_info& src) : f2fs_inode_info(src) 	{}
	Cf2fsFileNode(f2fs_sb_info* sbi);
	virtual ~Cf2fsFileNode(void) {}

public:
	//struct inode_operations	*i_op;
	virtual dentry* lookup(dentry*, unsigned int)UNSUPPORT_1(dentry*);
	virtual const char* get_link(dentry*, /*struct inode*,*/ delayed_call*)UNSUPPORT_1(const char*);
	virtual int permission(user_namespace*, /*struct inode*,*/ int)UNSUPPORT_1(int);
	//	virtual posix_acl* get_acl(/*struct inode*,*/ int, bool);
	virtual int readlink(dentry*, char __user*, int)UNSUPPORT_1(int);
	virtual int create(user_namespace*, dentry*, umode_t, bool)UNSUPPORT_1(int);
	virtual int link(dentry*, /*struct inode*,*/ dentry*)UNSUPPORT_1(int);
	virtual int unlink(/*struct inode*, */ dentry*)UNSUPPORT_1(int);
	virtual int symlink(user_namespace*, /*struct inode*, */dentry*, const char*)UNSUPPORT_1(int);
	virtual int mkdir(user_namespace*, /*struct inode*, */dentry*, umode_t)UNSUPPORT_1(int);
	virtual int rmdir(/*struct inode*, */dentry*)UNSUPPORT_1(int);
	virtual int mknod(user_namespace*, /*struct inode*, */dentry*, umode_t, dev_t)UNSUPPORT_1(int);
	virtual int rename(user_namespace*, /*struct inode*, */dentry*, inode*, dentry*, unsigned int)UNSUPPORT_1(int);
//	virtual int setattr(user_namespace*, dentry*, iattr*)UNSUPPORT_1(int);
	virtual int getattr(user_namespace*, const path*, kstat*, u32, unsigned int)UNSUPPORT_1(int);
	virtual ssize_t listxattr(dentry*, char*, size_t)UNSUPPORT_1(size_t);
	virtual int fiemap(/*struct inode*, */fiemap_extent_info*, u64 start, u64 len)UNSUPPORT_1(int);
	virtual int update_time(/*struct inode*, */timespec64*, int)UNSUPPORT_1(int);
	virtual int atomic_open(/*struct inode*, */dentry*, file*, unsigned open_flag, umode_t create_mode)UNSUPPORT_1(int);
	virtual int tmpfile(user_namespace*, /*struct inode*,*/ dentry*, umode_t)UNSUPPORT_1(int);
	//	virtual int set_acl(user_namespace*, /*struct inode*,*/ posix_acl*, int)UNSUPPORT_1(int);
	virtual int fileattr_set(user_namespace* mnt_userns, dentry* dentry, fileattr* fa)UNSUPPORT_1(int);
	virtual int fileattr_get(dentry* dentry, fileattr* fa)UNSUPPORT_1(int);

	// file operations from fs.h/file_operations
	virtual ssize_t read(file*, char __user*, size_t, loff_t*) { UNSUPPORT_1(ssize_t); };;
	virtual ssize_t write(file*, const char __user*, size_t, loff_t*) { UNSUPPORT_1(ssize_t); };;
	virtual ssize_t read_iter(kiocb*, iov_iter*);
	virtual ssize_t write_iter(kiocb*, iov_iter*);

	virtual long fallocate(int mode, loff_t offset, loff_t len);
//	.llseek = f2fs_llseek,



//		.open = f2fs_file_open,
//		.mmap = f2fs_file_mmap,
//		.flush = f2fs_file_flush,
//		.fsync = f2fs_sync_file,
//		.fallocate = f2fs_fallocate,
//		.unlocked_ioctl = f2fs_ioctl,
//#ifdef CONFIG_COMPAT
//		.compat_ioctl = f2fs_compat_ioctl,
//#endif
//		.splice_read = generic_file_splice_read,
//		.splice_write = iter_file_splice_write,
//

//
//const struct address_space_operations f2fs_dblock_aops = {
//	.readpage	= f2fs_read_data_page,
//	.readahead	= f2fs_readahead,
//	.writepage	= f2fs_write_data_page,
//	.writepages	= f2fs_write_data_pages,
//	.write_begin	= f2fs_write_begin,
//	.write_end	= f2fs_write_end,
//	.set_page_dirty	= f2fs_set_data_page_dirty,
//	.invalidatepage	= f2fs_invalidate_page,
//	.releasepage	= f2fs_release_page,
//	.direct_IO	= f2fs_direct_IO,
//	.bmap		= f2fs_bmap,
//	.swap_activate  = f2fs_swap_activate,
//	.swap_deactivate = f2fs_swap_deactivate,
//#ifdef CONFIG_MIGRATION
//	.migratepage    = f2fs_migrate_page,
//#endif
//};


public:

protected:
	int expand_inode_data(loff_t offset, loff_t len, int mode);
	int punch_hole(loff_t offset, loff_t len);
	int fill_zero(pgoff_t index, loff_t start, loff_t len);
//	int f2fs_truncate_hole(pgoff_t pg_start, pgoff_t pg_end);
	int f2fs_insert_range(loff_t offset, loff_t len);
	int f2fs_zero_range(loff_t offset, loff_t len, int mode);

};

class Cf2fsSymbLink : public f2fs_inode_info
{
public:
	Cf2fsSymbLink(const f2fs_inode_info& src) : f2fs_inode_info(src) {}
	Cf2fsSymbLink(f2fs_sb_info* sbi);
	virtual ~Cf2fsSymbLink(void) {}

public:
	//struct inode_operations	*i_op;
	virtual dentry* lookup(dentry*, unsigned int)UNSUPPORT_1(dentry*);
	virtual const char* get_link(dentry*, /*struct inode*,*/ delayed_call*)UNSUPPORT_1(const char*);
	virtual int permission(user_namespace*, /*struct inode*,*/ int)UNSUPPORT_1(int);
	//	virtual posix_acl* get_acl(/*struct inode*,*/ int, bool);
	virtual int readlink(dentry*, char __user*, int)UNSUPPORT_1(int);
	virtual int create(user_namespace*, dentry*, umode_t, bool)UNSUPPORT_1(int);
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
};


f2fs_dir_entry* f2fs_find_target_dentry(const f2fs_dentry_ptr* d, const f2fs_filename* fname, int* max_slots);

void init_dir_entry_data(void);


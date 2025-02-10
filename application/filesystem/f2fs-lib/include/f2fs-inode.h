///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "config.h"
#include "../source/mapping.h"

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
	f2fs_inode_info(f2fs_sb_info * sbi, UINT32 ino, address_space * mapping = nullptr);
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
	// 只有dir支持lookup
	virtual dentry* lookup(dentry*, unsigned int)UNSUPPORT_1(dentry*);	
	virtual int create(user_namespace*, dentry*, umode_t, bool)UNSUPPORT_1(int);
	virtual int mkdir(user_namespace*, /*struct inode*, */dentry*, umode_t)UNSUPPORT_1(int);
	virtual int unlink(/*struct inode*, */ dentry*)UNSUPPORT_1(int);
	// 返回dir是否为空，非dir，返回true；
	virtual bool f2fs_empty_dir(void) const { return true; }

public:
//	struct inode vfs_inode;		/* serve a vfs inode */
	unsigned long	i_flags;		/* keep an inode flags for ioctl */
	unsigned char	i_advise;		/* use to give file attribute hints */
	// TODO for dir only, move to CF2fsDirInode
	unsigned char	i_dir_level;	/* use for dentry level for large dir */ 
	unsigned int	i_current_depth;	/* only for directory depth */
	/* for gc failure statistic */
	unsigned int	i_gc_failures[MAX_GC_FAILURE];
	unsigned int	i_pino;		/* parent inode number */
	umode_t			i_acl_mode;		/* keep file acl mode temporarily */
	f2fs_sb_info*	m_sbi;

	/* Use below internally in f2fs*/
	//unsigned long flags[BITS_TO_LONGS(FI_MAX)];	/* use to pass per-file flags */
	//unsigned long flags[BITS_TO_<long>(FI_MAX)];	/* use to pass per-file flags */
	UINT64			flags;
	semaphore		i_sem;	/* protect fi info */
	atomic_t		dirty_pages;		/* # of dirty pages */
	f2fs_hash_t		chash;		/* hash value of given file name */
	unsigned int	clevel;		/* maximum level of given file name */
//	struct task_struct* task;	/* lookup and create consistency */
	nid_t			i_xattr_nid;		/* node id that contains xattrs */
	loff_t			last_disk_size;		/* lastly written file size */
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
//	struct task_struct* inmem_task;	/* store inmemory task */
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

protected:
	CRITICAL_SECTION m_alias_lock;
	std::list<dentry*> m_alias;

public:
//	inline bool is_dir(void) const {	return S_ISDIR(i_mode);	}
	// dentry指向this inode，将inode与dentry向关联。如果相同的dentry已经存在（parent相同，且名称相同，不是hardlink）则返回dentry，否则将dentry关联到inode上。
	dentry* splice_alias(dentry* entry);
	virtual void remove_alias(dentry* ddentry);

	inline void get_inline_info(f2fs_inode* ri)
	{
		if (ri->i_inline & F2FS_INLINE_XATTR)		set_bit(FI_INLINE_XATTR,  flags);
		if (ri->i_inline & F2FS_INLINE_DATA)		set_bit(FI_INLINE_DATA,   flags);
		if (ri->i_inline & F2FS_INLINE_DENTRY)		set_bit(FI_INLINE_DENTRY, flags);
		if (ri->i_inline & F2FS_DATA_EXIST)			set_bit(FI_DATA_EXIST,    flags);
		if (ri->i_inline & F2FS_INLINE_DOTS)		set_bit(FI_INLINE_DOTS,   flags);
		if (ri->i_inline & F2FS_EXTRA_ATTR)			set_bit(FI_EXTRA_ATTR,    flags);
		if (ri->i_inline & F2FS_PIN_FILE)			set_bit(FI_PIN_FILE,      flags);
	}

	inline int is_inode_flag_set(int flag) const { return test_bit(flag, flags);	}
	inline int f2fs_has_inline_dentry(void) const {return is_inode_flag_set(FI_INLINE_DENTRY); }
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
	inline int get_inline_xattr_addrs(void)
	{
//		m_sbi->m_fs->m_config.feature
		return i_inline_xattr_size;
		//if (m_sbi->m_fs->m_config.feature & cpu_to_le32(F2FS_FEATURE_FLEXIBLE_INLINE_XATTR))
		//	return le16_to_cpu(inode->_u._s.i_inline_xattr_size);
		//else if (i_inline & F2FS_INLINE_XATTR || i_inline & F2FS_INLINE_DENTRY)
		//	return DEFAULT_INLINE_XATTR_ADDRS;
		//else			return 0;
	}

	inline void f2fs_i_links_write(bool inc)
	{
		if (inc)	inc_nlink();
		else		drop_nlink();
		f2fs_mark_inode_dirty_sync(true);
	}
	/*__mark_inode_dirty expects inodes to be hashed.  Since we don't want special inodes in the fileset inode space, 
	  we make them appear hashed, but do not put on any lists.  hlist_del() will work fine and require no locking. */
	inline void inode_fake_hash(void) { JCASSERT(0); /*hlist_add_fake(&i_hash);*/ }
	inline loff_t GetFileSize(void) const { return i_size; }
	inline DWORD GetFileSizeHi(void) const { return HIDWORD(i_size); }
	inline DWORD GetFileSizeLo(void) const { return LODWORD(i_size); }
	DWORD GetFileAttribute(void) const;
	void SetFileAttribute(fmode_t mode_add, fmode_t mode_sub);

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
		set_bit(flag, flags);
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
	inline int get_extra_isize(void) const 	{
		return i_extra_isize / sizeof(__le32);	
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
	int f2fs_map_blocks(struct f2fs_map_blocks* map, int create, int flag);

protected:
	int f2fs_preallocate_blocks(kiocb* iocb, iov_iter* from);		// 可以移动到CF2fsFileNode
public:
// ==== namei.cpp ===
	void set_file_temperature(const std::wstring& name);
	int _internal_new_inode(f2fs_inode_info* dir, umode_t mode);


// ==== for dir, dir.cpp ====
	int f2fs_setup_filename(const qstr* iname, int lookup, f2fs_filename* fname);
//	f2fs_dir_entry* find_in_level(unsigned int level, const f2fs_filename* fname, page** res_page);
//	f2fs_dir_entry* find_in_block(page* dentry_page, const f2fs_filename* fname, int* max_slots);
	int __f2fs_setup_filename(const fscrypt_name* crypt_name, f2fs_filename* fname) const;
	int f2fs_init_casefolded_name(f2fs_filename* fname) const;
	page* f2fs_init_inode_metadata(inode* dir, const f2fs_filename* fname, page* dpage);
//	void f2fs_update_parent_metadata(f2fs_inode_info* inode, unsigned int current_depth);

// ==== inline.cpp ====
	int f2fs_convert_inline_inode(void);
	//-- tobe protected
	//int f2fs_move_inline_dirents(page* ipage, void* inline_dentry);
public:
	inline void* inline_data_addr(page* ppage) const;
	//{
	//	f2fs_inode* ri = F2FS_INODE(ppage);
	//	int extra_size = get_extra_isize(this);
	//	return (void*)&(ri->_u.i_addr[extra_size + DEF_INLINE_RESERVED_SIZE]);
	//}

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
	int f2fs_truncate(void);
	int f2fs_truncate_blocks(u64 from, bool lock);
	int f2fs_do_truncate_blocks(u64 from, bool lock);


// ==== node.cpp ====
	// 创建一个当前inode对应的ondisk page，存放inode的ondisk data
	page* f2fs_new_inode_page(void);
	page* f2fs_new_node_page(dnode_of_data* dn, unsigned int ofs);

// ==== extent_cache.cpp ====
	void f2fs_update_extent_tree_range(pgoff_t fofs, block_t blkaddr, unsigned int len);

// ==== super.cpp ====
	int f2fs_inode_dirtied(bool sync);
	void f2fs_inode_synced(void);


// ==== 由于改变inode::i_mapping的访问规则，添加必要的访问函数
	int filemap_fdatawrite(void) { return i_mapping->filemap_fdatawrite(); }
	void flush_inline_data(void);

#ifdef INODE_DEBUG
public:
	// 调试信息，
	std::wstring  m_description;
#endif


#ifdef _DEBUG
public:
	void DumpInodeMapping(void);
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


/* for inline dir */
#define NR_INLINE_DENTRY(inode)	(MAX_INLINE_DATA(inode) * BITS_PER_BYTE / ((SIZE_OF_DIR_ENTRY + F2FS_SLOT_LEN) * 	BITS_PER_BYTE + 1))

#define INLINE_DENTRY_BITMAP_SIZE(inode) DIV_ROUND_UP<size_t>(NR_INLINE_DENTRY(inode), BITS_PER_BYTE)

#define INLINE_RESERVED_SIZE(inode)	(MAX_INLINE_DATA(inode) - \
				((SIZE_OF_DIR_ENTRY + F2FS_SLOT_LEN) * NR_INLINE_DENTRY(inode) + INLINE_DENTRY_BITMAP_SIZE(inode)))

class Cf2fsDirInode : public f2fs_inode_info
{
public:
	Cf2fsDirInode(f2fs_sb_info * sbi, UINT ino);
	virtual ~Cf2fsDirInode(void) {}

public:
	virtual dentry* lookup(dentry*, unsigned int);
	virtual const char* get_link(dentry*, /*struct inode*,*/ delayed_call*)UNSUPPORT_1(const char*);
	virtual int permission(user_namespace*, /*struct inode*,*/ int)UNSUPPORT_1(int);
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
	virtual int fileattr_set(user_namespace* mnt_userns, dentry* dentry, fileattr* fa)UNSUPPORT_1(int);
	virtual int fileattr_get(dentry* dentry, fileattr* fa)UNSUPPORT_1(int);

public:
	// 枚举所有子项目，用于Dokan的FindFiles
	int enum_childs(dentry * parent, std::list<dentry*> & result);
protected:
	int enum_from_dentry_ptr(dentry * parent, const f2fs_dentry_ptr* d, std::list<dentry*>& result);


// ==== namei.cpp ===
public:
	int f2fs_create_whiteout(f2fs_inode_info** whiteout);
protected:
	int f2fs_prepare_lookup(dentry* dentry, f2fs_filename* fname);
	int __recover_dot_dentries(nid_t pino);
	int __f2fs_tmpfile(dentry* dentry, umode_t mode, f2fs_inode_info** whiteout);

// ==== dir.cpp ====
public:
	int f2fs_do_add_link(const qstr* name, f2fs_inode_info* inode, nid_t ino, umode_t mode);
	f2fs_dir_entry* __f2fs_find_entry(const f2fs_filename* fname, page** res_page);
	// node以name为文件名，加入到当前node中
	int f2fs_add_dentry(const f2fs_filename* fname, f2fs_inode_info* node, nid_t ino, umode_t mode);
	// node以name为文件名，加入到当前node中
	int f2fs_add_regular_entry(const f2fs_filename* fname, f2fs_inode_info* node, nid_t ino, umode_t mode);
	int make_empty_dir(inode* parent, page* ppage);
	void f2fs_update_parent_metadata(f2fs_inode_info* inode, unsigned int current_depth);
	// 返回是否时空目录
	virtual bool f2fs_empty_dir(void) const;
	f2fs_dir_entry* f2fs_parent_dir(page** p);
	f2fs_dir_entry* f2fs_find_entry(const qstr* child, page** res_page);
	void f2fs_set_link(f2fs_dir_entry* de, page* ppage, inode* iinode);
	bool f2fs_has_enough_room(page* ipage, const f2fs_filename* fname);
	void f2fs_delete_entry(f2fs_dir_entry* dentry, page* ppage, f2fs_inode_info* iinode);

protected:
	void f2fs_do_make_empty_dir(inode* parent, f2fs_dentry_ptr* d);
	static unsigned int dir_buckets(unsigned int level, int dir_level);
	static unsigned int bucket_blocks(unsigned int level);
	f2fs_dir_entry* find_in_level(unsigned int level, const f2fs_filename* fname, page** res_page);
	static unsigned long dir_block_index(unsigned int level, int dir_level, unsigned int idx);
	f2fs_dir_entry* find_in_block(page* dentry_page, const f2fs_filename* fname, int* max_slots);

// ==== inline.cpp ====
public:
	int f2fs_move_inline_dirents(page* ipage, void* inline_dentry);
	int f2fs_try_convert_inline_dir(dentry* ddentry);

protected:
	int f2fs_add_inline_entry(const f2fs_filename* fname, f2fs_inode_info* inode, nid_t ino, umode_t mode);
	int f2fs_make_empty_inline_dir(inode* parent, page* ipage);
	f2fs_dir_entry* f2fs_find_in_inline_dir(const f2fs_filename* fname, page** res_page);
	bool f2fs_empty_inline_dir(void) const;
	void f2fs_delete_inline_entry(f2fs_dir_entry* dentry, page* ppage, f2fs_inode_info* iinode);


protected:
	UINT64 dir_blocks(void)	const {return ((unsigned long long) (i_size_read(this) + PAGE_SIZE - 1)) >> PAGE_SHIFT; }
public:
	//从f2fs.h的f2fs_add_link(dentry* entry, f2fs_inode_info* inode)移植。
	// entry和inode为需要添加的子目录/文件的entry和inode，父目录指针通过entry的d_parent获取。
	// 这里通过this指针传递parent的inode以优化处理
	inline int f2fs_add_link(dentry* entry, f2fs_inode_info* inode)
	{
		if (fscrypt_is_nokey_name(entry))	return -ENOKEY;
#if 0 // _DEBUG
//			f2fs_inode_info* fi = (d_inode(entry->d_parent));
		Cf2fsDirInode* di = dynamic_cast<Cf2fsDirInode*>(d_inode(entry->d_parent));
		if ( (di == NULL) || (di!=this) ) THROW_ERROR(ERR_APP, L"only dir inode support this feature");
#endif
		return f2fs_do_add_link( &entry->d_name, inode, inode->i_ino, inode->i_mode);
	}
protected:
	inline void make_dentry_ptr_block(f2fs_dentry_ptr* d, f2fs_dentry_block* t)
	{
		d->inode = this;
		d->max = NR_DENTRY_IN_BLOCK;
		d->nr_bitmap = SIZE_OF_DENTRY_BITMAP;
		d->bitmap = t->dentry_bitmap;
		d->dentry = t->dentry;
		d->filename = t->filename;
	}
	inline void make_dentry_ptr_inline(f2fs_dentry_ptr* d, void* t)
	{
		size_t entry_cnt = NR_INLINE_DENTRY(this);
		size_t bitmap_size = INLINE_DENTRY_BITMAP_SIZE(this);
		size_t reserved_size = INLINE_RESERVED_SIZE(this);

		d->inode = this;
		d->max = entry_cnt;
		d->nr_bitmap = bitmap_size;
		d->bitmap = t;
		d->dentry = reinterpret_cast<f2fs_dir_entry*>((BYTE*)t + bitmap_size + reserved_size);
		d->_f = reinterpret_cast<BYTE*>(t) + bitmap_size + reserved_size + SIZE_OF_DIR_ENTRY * entry_cnt;
	}
	friend void f2fs_delete_inline_entry(f2fs_dir_entry* dentry, page* ppage, f2fs_inode_info* dir, f2fs_inode_info* iinode);
	Cf2fsDataMapping m_data_mapping;


#ifdef INODE_DEBUG
public:
	void DebugListItems(void);
#else
	void DebugListItems(void) {};
#endif //_DEBUG
};

class Cf2fsFileNode : public f2fs_inode_info
{
public:
	//Cf2fsFileNode(const f2fs_inode_info& src) : f2fs_inode_info(src) 	{}
	Cf2fsFileNode(f2fs_sb_info* sbi, UINT ino);
	virtual ~Cf2fsFileNode(void) {}

public:
	//struct inode_operations	*i_op;
	virtual const char* get_link(dentry*, /*struct inode*,*/ delayed_call*)UNSUPPORT_1(const char*);
	virtual int permission(user_namespace*, /*struct inode*,*/ int)UNSUPPORT_1(int);
	//	virtual posix_acl* get_acl(/*struct inode*,*/ int, bool);
	virtual int readlink(dentry*, char __user*, int)UNSUPPORT_1(int);
	virtual int link(dentry*, /*struct inode*,*/ dentry*)UNSUPPORT_1(int);
	virtual int symlink(user_namespace*, /*struct inode*, */dentry*, const char*)UNSUPPORT_1(int);
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

protected:
	Cf2fsDataMapping m_data_mapping;
};

class Cf2fsSpecialInode : public f2fs_inode_info
{
public:
	//Cf2fsSpecialInode(const f2fs_inode_info& src) : f2fs_inode_info(src) {}
	Cf2fsSpecialInode(f2fs_sb_info* sbi, umode_t mode, dev_t dev) : f2fs_inode_info(sbi, 0, nullptr)
	{
		init_special_inode(this, mode, dev);
	}
	Cf2fsSpecialInode(f2fs_sb_info* sbi, UINT ino) : f2fs_inode_info(sbi, ino, nullptr)
	{
		JCASSERT(0);
//		init_special_inode(this, mode, dev);
	}
	virtual ~Cf2fsSpecialInode(void) {}

public:
	//struct inode_operations	*i_op; => from f2fs_special_inode_operation;
	virtual const char* get_link(dentry*, /*struct inode*,*/ delayed_call*)UNSUPPORT_1(const char*);
	virtual int permission(user_namespace*, /*struct inode*,*/ int)UNSUPPORT_1(int);
	//	virtual posix_acl* get_acl(/*struct inode*,*/ int, bool);
	virtual int readlink(dentry*, char __user*, int)UNSUPPORT_1(int);
	virtual int link(dentry*, /*struct inode*,*/ dentry*)UNSUPPORT_1(int);
	virtual int symlink(user_namespace*, /*struct inode*, */dentry*, const char*)UNSUPPORT_1(int);
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

};

class Cf2fsSymbLink : public f2fs_inode_info
{
public:
	//Cf2fsSymbLink(const f2fs_inode_info& src) : f2fs_inode_info(src) {}
	Cf2fsSymbLink(f2fs_sb_info* sbi, UINT ino);
	virtual ~Cf2fsSymbLink(void) {}

public:
	//struct inode_operations	*i_op;
	virtual const char* get_link(dentry*, /*struct inode*,*/ delayed_call*)UNSUPPORT_1(const char*);
	virtual int permission(user_namespace*, /*struct inode*,*/ int)UNSUPPORT_1(int);
	//	virtual posix_acl* get_acl(/*struct inode*,*/ int, bool);
	virtual int readlink(dentry*, char __user*, int)UNSUPPORT_1(int);
	virtual int link(dentry*, /*struct inode*,*/ dentry*)UNSUPPORT_1(int);
	virtual int symlink(user_namespace*, /*struct inode*, */dentry*, const char*)UNSUPPORT_1(int);
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

protected:
	Cf2fsDataMapping m_data_mapping;
};


f2fs_dir_entry* f2fs_find_target_dentry(const f2fs_dentry_ptr* d, const f2fs_filename* fname, int* max_slots);

void init_dir_entry_data(void);


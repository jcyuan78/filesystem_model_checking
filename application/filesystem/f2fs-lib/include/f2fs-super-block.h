///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
#pragma once

#include <linux-fs-wrapper.h>
#include "f2fs-inode.h"


/* COUNT_TYPE for monitoring
 * f2fs monitors the number of several block types such as on-writeback, dirty dentry blocks, dirty node blocks, 
   and dirty meta blocks. */
#define WB_DATA_TYPE(p)	(__is_cp_guaranteed(p) ? F2FS_WB_CP_DATA : F2FS_WB_DATA)
enum count_type
{
	F2FS_DIRTY_DENTS,
	F2FS_DIRTY_DATA,
	F2FS_DIRTY_QDATA,
	F2FS_DIRTY_NODES,
	F2FS_DIRTY_META,
	F2FS_INMEM_PAGES,
	F2FS_DIRTY_IMETA,
	F2FS_WB_CP_DATA,
	F2FS_WB_DATA,
	F2FS_RD_DATA,
	F2FS_RD_NODE,
	F2FS_RD_META,
	F2FS_DIO_WRITE,
	F2FS_DIO_READ,
	NR_COUNT_TYPE,
};

class CF2fsFileSystem;
struct f2fs_nm_info;
struct seg_entry;

struct MOUNT_OPTION
{
	size_t m_page_cache_num;		// page cache大小，以page数单位，必须是16的倍数
	size_t m_dentry_cache_num;		// dentry cache的数量
};

class CIoCompleteCtrl;



struct f2fs_sb_info : public super_block
{
public:
	f2fs_sb_info(CF2fsFileSystem * fs, file_system_type * type, const MOUNT_OPTION & opt);
	virtual ~f2fs_sb_info(void);

// 
public:
	

public:
	inline CF2fsFileSystem* GetFs(void) { return m_fs; }
	CF2fsFileSystem* m_fs;

	//static const struct super_operations f2fs_sops = {
	//	.alloc_inode = f2fs_alloc_inode,
	//	.free_inode = f2fs_free_inode,
	//	.drop_inode = f2fs_drop_inode,
	//	.write_inode = f2fs_write_inode,
	//	.show_options = f2fs_show_options,
	//#ifdef CONFIG_QUOTA
	//	.quota_read = f2fs_quota_read,
	//	.quota_write = f2fs_quota_write,
	//	.get_dquots = f2fs_get_dquots,
	//#endif
	//	.evict_inode = f2fs_evict_inode,
	//	.put_super = f2fs_put_super,
	//	.sync_fs = f2fs_sync_fs,
	//	.freeze_fs = f2fs_freeze,
	//	.unfreeze_fs = f2fs_unfreeze,
	//	.statfs = f2fs_statfs,
	//	.remount_fs = f2fs_remount,
	//};


public:
	virtual inode*  alloc_inode(super_block* sb) { return NULL; }
	virtual void	free_inode(inode*);

	virtual void	dirty_inode(inode *, int flags);
	virtual int		write_inode(inode* iinode, writeback_control* wbc)
	{
		f2fs_inode_info* fi = F2FS_I(iinode);
		return fi->f2fs_write_inode(wbc);
	}

	virtual int		drop_inode(inode*);
	virtual void	evict_inode(inode*);
	virtual void	put_super(void);
	virtual int		sync_fs(int wait);
	virtual int		freeze_fs(struct super_block*) { return 0; };
	virtual int		thaw_super(struct super_block*) { return 0; };
	virtual int		unfreeze_fs(struct super_block*) { return 0; };
	virtual int		statfs(struct dentry*, struct kstatfs*) { return 0; };
	virtual int		remount_fs(struct super_block*, int*, char*) { return 0; };

	virtual int		show_options(struct seq_file*, struct dentry*) { return 0; };
#ifdef CONFIQUOTA
	virtual ssize_t	quota_read(struct super_block*, int, char*, size_t, loff_t);
	virtual ssize_t	quota_write(struct super_block*, int, const char*, size_t, loff_t);
	virtual dquot** get_dquots(struct inode*);
#endif

public:

	struct proc_dir_entry* s_proc = NULL;			/* proc entry */
	f2fs_super_block* raw_super = NULL;				/* raw super block pointer */
	rw_semaphore  sb_lock;							/* lock for raw super block */
	int valid_super_block = 0;						/* valid super block no */
	unsigned long s_flag = 0;						/* flags for sbi */
	mutex writepages = 0;							/* mutex for writepages() */

#ifdef CONFIG_BLK_DEV_ZONED
	unsigned int blocks_per_blkz;					/* F2FS blocks per zone */
	unsigned int log_blocks_per_blkz;				/* log2 F2FS blocks per zone */
#endif

	/* for node-related operations */
	f2fs_nm_info* nm_info;							/* node manager */
	inode* node_inode = NULL;						/* cache node blocks */

	/* for segment-related operations */
	f2fs_sm_info* sm_info = NULL;					/* segment manager */

	/* for bio operations */
	f2fs_bio_info* write_io[NR_PAGE_TYPE];			/* for write bios */
	/* keep migration IO order for LFS mode */
	rw_semaphore  io_order_lock;
#if 0	//<TODO>
	mempool_t* write_io_dummy = NULL;				/* Dummy pages */
#endif
	/* for checkpoint */
	f2fs_checkpoint* ckpt = NULL;					/* raw checkpoint pointer */
	int cur_cp_pack = 0;							/* remain current cp pack */
	CRITICAL_SECTION cp_lock;						/* for flag in ckpt */
	inode* meta_inode = NULL;						/* cache meta blocks */
	rw_semaphore  cp_global_sem;					/* checkpoint procedure lock */
protected:
	rw_semaphore  cp_rwsem;							/* blocking FS operations */
public:
	rw_semaphore  node_write;						/* locking node writes */
	rw_semaphore  node_change;						/* locking node change */

	//<YUAN> 用CPU时钟替换
	LONGLONG last_time[MAX_TIME];
	LONGLONG interval_time[MAX_TIME];
	//	unsigned long last_time[MAX_TIME];	/* to store time in jiffies */
	//	long interval_time[MAX_TIME];		/* to store thresholds */
	ckpt_req_control cprc_info;						/* for checkpoint request control */

	inode_management im[MAX_INO_ENTRY];				/* manage inode cache */

	CRITICAL_SECTION fsync_node_lock;				/* for node entry lock */
	list_head fsync_node_list;						/* node list head */
	unsigned int fsync_seg_id =0;					/* sequence id */
	unsigned int fsync_node_num =0;					/* number of node entries */

	/* for orphan inode, use 0'th array */
	unsigned int max_orphans =0;					/* max orphan inodes */

	/* for inode management */
//	list_head inode_list[NR_INODE_TYPE];			/* dirty inode list */
	spinlock_t inode_lock[NR_INODE_TYPE];			/* for dirty inode list lock */
	mutex flush_lock = nullptr;						/* for flush exclusion */

	/* for extent tree cache */
	radix_tree_root extent_tree_root;				/* cache extent cache entries */
	mutex extent_tree_lock = nullptr;				/* locking extent radix tree */
	list_head extent_list;		/* lru list for shrinker */
	CRITICAL_SECTION extent_lock;			/* locking extent lru list */
	atomic_t total_ext_tree = 0;		/* extent tree count */
	list_head zombie_list;		/* extent zombie tree list */
	atomic_t total_zombie_tree = 0;		/* extent zombie tree count */
	atomic_t total_ext_node = 0;		/* extent info count */

	/* basic filesystem units */
	unsigned int log_sectors_per_block = 0;			/* log2 sectors per block */
	unsigned int log_blocksize = 0;					/* log2 block size */
	unsigned int blocksize = 0;						/* block size */
	unsigned int root_ino_num = 0;					/* root inode number*/
	unsigned int node_ino_num = 0;					/* node inode number*/
	unsigned int meta_ino_num = 0;					/* meta inode number*/
	unsigned int log_blocks_per_seg = 0;			/* log2 blocks per segment */
	unsigned int blocks_per_seg = 0;				/* blocks per segment */
	unsigned int segs_per_sec = 0;					/* segments per section */
	unsigned int secs_per_zone = 0;					/* sections per zone */
	unsigned int total_sections = 0;				/* total section count */
	unsigned int total_node_count = 0;				/* total node block count */
	unsigned int total_valid_node_count = 0;		/* valid node block count */
	int dir_level = 0;								/* directory level */
	int readdir_ra = 0;								/* readahead inode in readdir */
	UINT64 max_io_bytes = 0;						/* max io bytes to merge IOs */

	block_t user_block_count = 0;					/* # of user blocks */
	block_t total_valid_block_count = 0;			/* # of valid blocks */
	block_t discard_blks = 0;						/* discard command candidats */
	block_t last_valid_block_count = 0;				/* for recovery */
	block_t reserved_blocks = 0;					/* configurable reserved blocks */
	block_t current_reserved_blocks = 0;			/* current reserved blocks */

	/* Additional tracking for no checkpoint mode */
	block_t unusable_block_count = 0;		/* # of blocks saved by last cp */

	unsigned int nquota_files = 0;		/* # of quota sysfile */
	rw_semaphore  quota_sem;		/* blocking cp for flags */

public:
	/* # of allocated blocks */
	percpu_counter alloc_valid_block_count;
	/* writeback control */
	atomic_t wb_sync_req[META];	/* count # of WB_SYNC threads */

	/* valid inode count */
	percpu_counter total_valid_inode_count;
	f2fs_mount_info mount_opt;	/* mount options */
	inline bool test_opt_(UINT option) { return mount_opt.opt & option; }

	/* for cleaning operations */
	rw_semaphore gc_lock;			/* semaphore for GC, avoid race between GC and GC or CP */
	struct f2fs_gc_kthread* gc_thread = NULL;		/* GC thread */
	atgc_management am;		/* atgc management */
	unsigned int cur_victim_sec = 0;		/* current victim section num */
	unsigned int gc_mode = 0;			/* current GC state */
	unsigned int next_victim_seg[2] = { 0,0 };	/* next segment in victim section */

	/* for skip statistic */
	unsigned int atomic_files = 0;		/* # of opened atomic file */
	unsigned long long skipped_atomic_files[2] = { 0,0 };	/* FG_GC and BG_GC */
	unsigned long long skipped_gc_rwsem = 0;		/* FG_GC only */

	/* threshold for gc trials on pinned files */
	UINT64 gc_pin_file_threshold = 0;
	rw_semaphore  pin_sem;

	/* maximum # of trials to find a victim segment for SSR and GC */
	unsigned int max_victim_search = 0;
	/* migration granularity of garbage collection, unit: segment */
	unsigned int migration_granularity = 0;


	friend class CF2fsFileSystem;

	//== lists
protected:
	/* # of pages, see count_type */
	atomic_t nr_pages[NR_COUNT_TYPE];
	std::list<f2fs_inode_info*> m_inode_list[NR_INODE_TYPE];
public:
	void sb_list_add_tail(f2fs_inode_info* iinode, inode_type type)
	{
		m_inode_list[type].push_back(iinode);
		iinode->m_in_list[type] = true;
	}
	void sb_list_del_init(f2fs_inode_info* iinode, inode_type type)
	{
		m_inode_list[type].remove(iinode);
		iinode->m_in_list[type] = false;
	}
	f2fs_inode_info * get_list_first_entry(inode_type type)
	{
		return m_inode_list[type].front();
	}
	void list_move_tail(f2fs_inode_info* iinode, inode_type type)
	{
		m_inode_list[type].remove(iinode);
		m_inode_list[type].push_back(iinode);
	}
	bool list_empty(inode_type type) const { return m_inode_list[type].empty(); }

protected:
	CPageManager m_page_manager;
public:
	CPageManager* GetPageManager(void) { return &m_page_manager; }

public:

	/* for stat information. one is for the LFS mode, and the other is for the SSR mode. */
#ifdef CONFIG_F2FS_STAT_FS
	struct f2fs_stat_info* stat_info;	/* FS status information */
	atomic_t meta_count[META_MAX];		/* # of meta blocks */
	unsigned int segment_count[2];		/* # of allocated segments */
	unsigned int block_count[2];		/* # of allocated blocks */
	atomic_t inplace_count;		/* # of inplace update */
	atomic64_t total_hit_ext;		/* # of lookup extent cache */
	atomic64_t read_hit_rbtree;		/* # of hit rbtree extent node */
	atomic64_t read_hit_largest;		/* # of hit largest extent node */
	atomic64_t read_hit_cached;		/* # of hit cached extent node */
	atomic_t inline_xattr;			/* # of inline_xattr inodes */
	atomic_t inline_inode;			/* # of inline_data inodes */
	atomic_t inline_dir;			/* # of inline_dentry inodes */
	atomic_t compr_inode;			/* # of compressed inodes */
	atomic64_t compr_blocks;		/* # of compressed blocks */
	atomic_t vw_cnt;			/* # of volatile writes */
	atomic_t max_aw_cnt;			/* max # of atomic writes */
	atomic_t max_vw_cnt;			/* max # of volatile writes */
	unsigned int io_skip_bggc;		/* skip background gc for in-flight IO */
	unsigned int other_skip_bggc;		/* skip background gc for other reasons */
	unsigned int ndirty_inode[NR_INODE_TYPE];	/* # of dirty inodes */
#endif
	CRITICAL_SECTION stat_lock;			/* lock for stat operations */

	/* For app/fs IO statistics */
	CRITICAL_SECTION iostat_lock;
	unsigned long long rw_iostat[NR_IO_TYPE] = { 0 };
	unsigned long long prev_rw_iostat[NR_IO_TYPE] = { 0 };
	bool iostat_enable = 0;
	LONGLONG iostat_next_period = 0;
	unsigned int iostat_period_ms = 0;

	/* to attach REQ_META|REQ_FUA flags */
	unsigned int data_io_flag = 0;
	unsigned int node_io_flag = 0;

#if 0
	/* For sysfs suppport */
	struct kobject s_kobj;			/* /sys/fs/f2fs/<devname> */
	struct completion s_kobj_unregister;

	struct kobject s_stat_kobj;		/* /sys/fs/f2fs/<devname>/stat */
	struct completion s_stat_kobj_unregister;
#endif

	/* For shrinker support */
	list_head s_list;
	int s_ndevs = 0;				/* number of devices */
	f2fs_dev_info* devs = NULL;		/* for device list */
	unsigned int dirty_device = 0;		/* for checkpoint data flush */
	CRITICAL_SECTION dev_lock;			/* protect dirty_device */
	mutex umount_mutex = 0;
	unsigned int shrinker_run_no = 0;

	/* For write statistics */
	UINT64 sectors_written_start = 0;
	UINT64 kbytes_written = 0;

	/* Reference to checksum algorithm driver via cryptoapi */
	//<YUAN>简化动态checksum driver设计，改为固定函数，在libf2fs.cpp中实现。
//	struct crypto_shash *s_chksum_driver;

	/* Precomputed FS UUID checksum for seeding other checksums */
	__u32 s_chksum_seed = 0;

	workqueue_struct* post_read_wq = NULL;	/* post read workqueue */

	kmem_cache* inline_xattr_slab = NULL;	/* inline xattr entry */
	unsigned int inline_xattr_slab_size = 0;	/* default inline xattr slab size */

#ifdef CONFIG_F2FS_FS_COMPRESSION
	struct kmem_cache* page_array_slab;	/* page array entry */
	unsigned int page_array_slab_size;	/* default page array slab size */

	/* For runtime compression statistics */
	UINT64 compr_written_block;
	UINT64 compr_saved_block;
	u32 compr_new_inode;
#endif

protected:
	CDentryManager m_dentry_buf;
	std::list<dentry*> m_dentry_lru;

//// ==== io ==========================================================================================================
	CIoCompleteCtrl* m_io_control = nullptr;

// inline fucntions
public:
	/* 0, 1(node nid), 2(meta nid) are reserved node id */
	//#define F2FS_RESERVED_NODE_NUM		3

	inline UINT F2FS_ROOT_INO(void) const	{return root_ino_num;}
	inline UINT F2FS_NODE_INO(void) const	{return node_ino_num;}
	inline UINT F2FS_META_INO(void) const	{return meta_ino_num;}
	friend bool __is_cp_guaranteed(page* page);
	friend count_type __read_io_type(page* page);


// f2fs member functions
// ==== super.cpp ====
public:
	void init_sb_info(void);

	void SetDevice(IVirtualDisk* dev);
	// 用于mount
	int f2fs_fill_super(const boost::property_tree::wptree& option, int silent);
	// 用于unmount
	void kill_f2fs_super(void);
	int inc_valid_node_count(f2fs_inode_info* inode, bool is_inode);
	void dec_valid_node_count(f2fs_inode_info* iinode, bool is_inode);

protected:
	void destory_super(void);
	void destroy_device_list(void);
	int f2fs_scan_devices(void);
	int f2fs_disable_checkpoint(void);
	void destroy_percpu_info(void);
	int read_raw_super_block(f2fs_super_block*& raw_super, int* valid_super_block, int* recovery);
	int sanity_check_raw_super(CBufferHead* bh);
	bool sanity_check_area_boundary(CBufferHead* bh);

	void default_options(void);

	int parse_mount_options(const boost::property_tree::wptree& options, bool is_remount);


// ==== gc.cpp ====
public:
	void f2fs_stop_gc_thread(void);

// ==== checkpoint ====
public:
	int f2fs_start_ckpt_thread(void);
	//DWORD _issue_checkpoint_thread(void);
	int f2fs_write_checkpoint(struct cp_control* cpc);
	bool f2fs_is_valid_blkaddr(block_t blkaddr, int type);
	page* __get_meta_page(pgoff_t index, bool is_meta);
	int f2fs_get_valid_checkpoint(void);
	long f2fs_sync_meta_pages(enum page_type type, long nr_to_write, enum iostat_type io_type);
	void f2fs_add_orphan_inode(f2fs_inode_info* iinode);
	int f2fs_acquire_orphan_inode(void);
	void f2fs_release_orphan_inode(void);
	int __f2fs_write_meta_page(page* ppage, writeback_control* wbc, enum iostat_type io_type);


protected:
	int f2fs_recover_orphan_inodes(void);
	int recover_orphan_inode(nid_t ino);
	void write_orphan_inodes(block_t start_blk);
	int block_operations(void);
	void unblock_operations(void);

	page* validate_checkpoint(block_t cp_addr, unsigned long long* version);
	int get_checkpoint_version(block_t cp_addr, f2fs_checkpoint** cp_block, page** cp_page, unsigned long long* version);
	int do_checkpoint(cp_control* cpc);
	void commit_checkpoint(void* src, block_t blk_addr);
	void __add_ino_entry(nid_t ino, unsigned int devidx, int type);

		friend void f2fs_add_ino_entry(f2fs_sb_info* sbi, nid_t ino, int type);
		friend void f2fs_set_dirty_device(f2fs_sb_info* sbi, nid_t ino, unsigned int devidx, int type);
	page* f2fs_get_meta_page(pgoff_t index);
		friend page* get_current_sit_page(f2fs_sb_info* sbi, unsigned int segno);
		friend int f2fs_recover_orphan_inodes(f2fs_sb_info* sbi);
		friend int f2fs_nm_info::f2fs_get_node_info(nid_t nid, /*out*/ node_info* ni);
		friend int f2fs_nm_info::__get_nat_bitmaps(void);

public:
	//void __checkpoint_and_complete_reqs(void);
	//int __write_checkpoint_sync(void);

// ==== shrinker.cpp ====
	void f2fs_join_shrinker(void);
	void f2fs_leave_shrinker(void);


// ==== segment.cpp ====
protected:
	void f2fs_destroy_segment_manager(void);
	int read_compacted_summaries(void);
	int read_normal_summaries(int type);
	page* get_current_sit_page(unsigned int segno);
	inline pgoff_t current_sit_addr(unsigned int start);
	page* get_next_sit_page(unsigned int start);
	void add_sits_in_set(void);
	void remove_sits_in_journal(void);
	inline void check_seg_range(unsigned int segno);
	inline void get_sit_bitmap(void* dst_addr);

	inline void __set_free(unsigned int segno);
	inline void __set_inuse(unsigned int segno);
	inline void __set_test_and_free(unsigned int segno, bool inmem);
	inline void __set_test_and_inuse(unsigned int segno);

	friend  void get_new_segment(struct f2fs_sb_info* sbi, unsigned int* newseg, bool new_sec, int dir);
	friend  void __f2fs_restore_inmem_curseg(struct f2fs_sb_info* sbi, int type);
	friend  void __f2fs_save_inmem_curseg(f2fs_sb_info* sbi, int type);
	friend void change_curseg(f2fs_sb_info* sbi, int type, bool flush);
	friend void set_prefree_as_free_segments(f2fs_sb_info* sbi);

	inline int __f2fs_get_curseg(unsigned int segno);
	void __refresh_next_blkoff(curseg_info* seg);


public:
	void f2fs_balance_fs_bg(bool from_bg);
	inline free_segmap_info* FREE_I(void) {	return (sm_info->free_info); }

	int f2fs_build_segment_manager(void);
	int f2fs_create_flush_cmd_control(void);
	DWORD _issue_flush_thread(void);
	DWORD _issue_discard_thread(void);
//	void f2fs_stop_discard_thread(void);	// 直接调用Stop()成员函数

	int f2fs_ra_meta_pages(block_t start, int nrpages, int type, bool sync);
	int f2fs_npages_for_summary_flush(bool for_ra);
	//inline block_t written_block_count(void){	return (sm_info->sit_info->written_valid_blocks); }
	//inline int overprovision_segments(void)	{	return sm_info->ovp_segments;	}
	//inline unsigned int prefree_segments(void)	{		return sm_info->dirty_info->nr_dirty[PRE];	}
	block_t written_block_count(void);
	unsigned int free_segments(void) const;
	inline unsigned int free_sections(void) const;


	int overprovision_segments(void);
	unsigned int prefree_segments(void);
	void f2fs_balance_fs(bool need);
	void f2fs_do_write_meta_page(page* ppage, enum iostat_type io_type);
	void f2fs_flush_sit_entries(cp_control* cpc);

	int submit_flush_wait(nid_t ino);
	int __submit_flush_wait(block_device* bdev);
	int create_discard_cmd_control(void);
	int build_sit_info(void);
	int build_free_segmap(void);
//	int build_curseg(void);
	int restore_curseg_summaries(void);
	int build_sit_entries(void);
	void init_free_segmap(void);
//	int build_dirty_segmap(void);
	int init_victim_secmap(void);
	int sanity_check_curseg(void);
	void init_min_max_mtime(void);
	void allocate_segment_by_default(int type, bool force);
	int check_block_count(int segno, struct f2fs_sit_entry* raw_sit);

	inline UINT MAIN_SEGS(void)	{ return sm_info->main_segments;	}
	inline UINT MAIN_SECS(void) { return total_sections; }
	inline curseg_info* CURSEG_I(int type);
	inline block_t start_sum_block(void) { return __start_cp_addr() + le32_to_cpu(ckpt->cp_pack_start_sum); }
	inline block_t sum_blk_addr(int base, int type)
	{
		return __start_cp_addr() +	le32_to_cpu(ckpt->cp_pack_total_block_count) - (base + 1) + type;
	}
	inline unsigned short curseg_blkoff(int type);

	bool f2fs_is_checkpoint_ready(void);

	virtual void	dentry_list_lru_del(dentry* dd) { m_dentry_lru.remove(dd); }
	virtual void	dentry_list_lru_add(dentry* dd)	{	m_dentry_lru.push_back(dd);	}
	inline seg_entry* get_seg_entry(unsigned int segno);
	bool has_not_enough_free_secs(int freed, int needed);
	inline bool has_curseg_enough_space(void);

	void f2fs_do_replace_block(struct f2fs_summary* sum, block_t old_blkaddr, block_t new_blkaddr,
		bool recover_curseg, bool recover_newaddr, bool from_gc);

	void f2fs_allocate_data_block(page* page, block_t old_blkaddr, block_t* new_blkaddr, f2fs_summary* sum, int type, f2fs_io_info* fio);
	unsigned int f2fs_usable_blks_in_seg(unsigned int segno);

	bool __has_curseg_space(curseg_info* curseg);
	void new_curseg(int type, bool new_sec);

// ==== data.cpp ====
public:
	void f2fs_submit_page_write(f2fs_io_info* fio);
	int f2fs_submit_page_bio(f2fs_io_info* fio);
	IVirtualDisk* f2fs_target_device(block_t blk_addr, bio* bio);

	friend static int add_ipu_page(f2fs_io_info* fio, bio** bio, page* page);
	friend int f2fs_merge_page_bio(f2fs_io_info* fio);
	friend static int f2fs_read_single_page(f2fs_inode_info* inode, page* ppage, unsigned nr_pages,
		struct f2fs_map_blocks* map, bio** bio_ret, sector_t* last_block_in_bio, bool is_readahead);

	/* submit_bio_wait - submit a bio, and wait until it completes
	 * @bio: The &struct bio which describes the I/O
	 * Simple wrapper around submit_bio(). Returns 0 on success, or the error from bio_endio() on failure.
	 * WARNING: Unlike to how submit_bio() is usually used, this function does not result in bio reference to be consumed. The caller must drop the reference on his own. */
	int submit_bio_wait(bio* bbio);
	inline void f2fs_submit_bio(bio* bio, enum page_type type) {	__submit_bio(bio, type); }
	inline void __submit_bio(bio* bio, enum page_type type);
//	void submit_bio(bio* bb);
	int f2fs_submit_page_read(f2fs_inode_info* inode, page* page, block_t blkaddr, int op_flags, bool for_write);
	int __blkdev_issue_discard(block_device*, sector_t lba, sector_t len, gfp_t gfp_mask, int flag, bio**);

protected:
	bool io_is_mergeable(bio* bio, f2fs_bio_info* io, f2fs_io_info* fio, block_t last_blkaddr, block_t cur_blkaddr);
	bool page_is_mergeable(bio* bio, block_t last_blkaddr, block_t cur_blkaddr);
	void __submit_merged_bio(f2fs_bio_info* io);
//	friend int f2fs_mpage_readpages(f2fs_inode_info* inode, readahead_control* rac, struct page* ppage);
	void __f2fs_submit_merged_write(enum page_type type, enum temp_type temp);

	void submit_sync_io(bio* bb);
	//void submit_async_io(bio* bb);
	bio* f2fs_grab_read_bio(f2fs_inode_info* inode, block_t blkaddr, unsigned nr_pages, unsigned op_flag, pgoff_t first_idx, bool for_write);

	friend int discard_cmd_control::__submit_discard_cmd(discard_policy* dpolicy, discard_cmd* dc, unsigned int* issued);
	friend void __submit_merged_write_cond(f2fs_sb_info* sbi, struct inode* inode, struct page* page, nid_t ino, enum page_type type, bool force);


//protected:
//	f2fs_fsck* fsck;
//public:
//	f2fs_fsck* F2FS_FSCK() { return fsck; }

protected:

// ==== node.cpp ====
public:
	int f2fs_build_node_manager(void);
//	int f2fs_build_free_nids(bool sync, bool mount);
	//<YUAN> nid：inode number
	page* f2fs_get_node_page(pgoff_t nid) { return __get_node_page(nid, NULL, 0); }
	bool f2fs_in_warm_node_list(page*);
	int read_node_page(page* page, int op_flags);
	void set_node_addr(node_info* ni, block_t new_blkaddr, bool fsync_done);
	page* f2fs_get_node_page_ra(page* parent, int start);


protected:
//	int init_node_manager(void);
	int init_free_nid_cache(void);
	void load_free_nid_bitmap(void);
	page* __get_node_page(pgoff_t nid, page* parent, int start);
	void f2fs_destroy_node_manager(void);


// ==== inode.cpp ====
protected:
	int do_create_read_inode(f2fs_inode_info*& out_inode, unsigned long ino);

public:
	f2fs_inode_info* f2fs_iget(nid_t ino);
	f2fs_inode_info* f2fs_iget_retry(unsigned long ino);

//	f2fs_inode_info* f2fs_iget(unsigned long ino);
//	int f2fs_submit_page_bio(f2fs_io_info* fio);

// ==== namei.cpp ====

protected:
	CInodeManager m_inodes;
public:
	// 为创建新的inode调用。
	template <class NODE_TYPE> NODE_TYPE* NewInode(void)
	{
		NODE_TYPE* node = new NODE_TYPE(this, 0);
		inode* base_node = static_cast<inode*>(node);
		m_inodes.new_inode(base_node);
		return node;
	}

	f2fs_inode_info* f2fs_new_inode(Cf2fsDirInode* dir, umode_t mode, inode_type type);
	int f2fs_rename(Cf2fsDirInode* old_dir, dentry* old_dentry, Cf2fsDirInode* new_dir, dentry* new_dentry, unsigned int flags);
	inode* ilookup(nid_t ino) { return m_inodes.ilookup(ino); }
	inode* find_inode_nowait(unsigned long hashval, int (*match)(inode*, unsigned long, void*), void* data)
	{
		return m_inodes.find_inode_nowait(hashval, match, data);
	}

	// 构建inode对象为读取做准备，因此带有ino
	template <class NODE_TYPE> NODE_TYPE* GetInodeLocked(bool thp_support, unsigned long ino)
	{
		NODE_TYPE* node = new NODE_TYPE(this, ino);
		inode* base_node = static_cast<inode*>(node);
		base_node->i_sb = this;
		m_inodes.internal_iget_locked(base_node, thp_support, ino);
		m_inodes.insert_inode_locked(base_node);
		m_inodes.init_inode_mapping(base_node, NULL, thp_support);
		return node;
	}

// ==== inline functions ====
public:
	inline f2fs_super_block* F2FS_RAW_SUPER(void)	{ return raw_super; }
	inline f2fs_checkpoint* F2FS_CKPT(void)			{ return ckpt; }
	inline f2fs_sm_info* SM_I(void)			{ return sm_info; 	}
	inline unsigned int reserved_segments(void)	const	{ return sm_info->reserved_segments; }
	inline block_t valid_user_blocks(void)			{ return total_valid_block_count;	}


	inline void f2fs_lock_op(void)					{ down_read(&cp_rwsem);	}
	inline void f2fs_unlock_op(void)				{ up_read(&cp_rwsem);	}
	inline int f2fs_trylock_op(void) { return down_read_trylock(&cp_rwsem); }
	inline void f2fs_lock_all(void)					{ down_write(&cp_rwsem);	}
	inline void f2fs_unlock_all(void)				{ up_write(&cp_rwsem);	}
	class auto_lock_op {
	public:
		auto_lock_op(f2fs_sb_info& sb) : m_sb(sb) {};
		void lock(void) { m_sb.f2fs_lock_op(); }
		void unlock(void) { m_sb.f2fs_unlock_op(); }
	protected:
		f2fs_sb_info& m_sb;
	};

	inline int utilization()				
	{
		//	return div_u64((u64)valid_user_blocks(sbi) * 100, sbi->user_block_count);
		return boost::numeric_cast<int>((UINT64)total_valid_block_count *100 / user_block_count);
	}
	inline int reserved_sections(void) const
	{
		UINT segno = reserved_segments();
		return (((segno) == -1) ? -1 : (segno) / segs_per_sec);
	}
	/* Test if the mounted volume is a multi-device volume.
		*   - For a single regular disk volume, sbi->s_ndevs is 0.
		*   - For a single zoned disk volume, sbi->s_ndevs is 1.
		*   - For a multi-device volume, sbi->s_ndevs is always 2 or more. */
	inline bool f2fs_is_multi_device(void )	{	return s_ndevs > 1;	}
	inline bool __is_large_section(void)	{	return segs_per_sec > 1;	}

	inline bool is_set_ckpt_flags(unsigned int f)	{		return __is_set_ckpt_flags(F2FS_CKPT(), f);	}
	inline struct sit_info* SIT_I(void)			{		return (sm_info->sit_info);	}
	inline unsigned int valid_node_count(void)	{		return total_valid_node_count;	}
	inline s64 valid_inode_count(void) {		return total_valid_inode_count.count;	}

	inline bool __exist_node_summaries(void)
	{
		return (is_set_ckpt_flags(CP_UMOUNT_FLAG) || is_set_ckpt_flags(CP_FASTBOOT_FLAG));
	}
	inline block_t __cp_payload(void)		{		return le32_to_cpu(raw_super->cp_payload);	}
	inline block_t __start_cp_addr(void)
	{
		block_t start_addr = le32_to_cpu(raw_super->cp_blkaddr);
		if (cur_cp_pack == 2)		start_addr += blocks_per_seg;
		return start_addr;
	}
	inline unsigned long __bitmap_size(int flag)
	{
//		struct f2fs_checkpoint* ckpt = F2FS_CKPT();
		/* return NAT or SIT bitmap */
		if (flag == NAT_BITMAP)			return le32_to_cpu(ckpt->nat_ver_bitmap_bytesize);
		else if (flag == SIT_BITMAP)	return le32_to_cpu(ckpt->sit_ver_bitmap_bytesize);
		return 0;
	}

	inline BYTE * __bitmap_ptr(int flag)
	{
//		f2fs_checkpoint* ckpt = F2FS_CKPT();
//		unsigned char* tmp_ptr = &ckpt->sit_nat_version_bitmap;
		BYTE* tmp_ptr = ckpt->sit_nat_version_bitmap;
		int offset;

		if (is_set_ckpt_flags(CP_LARGE_NAT_BITMAP_FLAG))
		{
			offset = (flag == SIT_BITMAP) ?	le32_to_cpu(ckpt->nat_ver_bitmap_bytesize) : 0;
			/* if large_nat_bitmap feature is enabled, leave checksum protection for all nat/sit bitmaps.	 */
			return tmp_ptr + offset + sizeof(__le32);
		}

		if (__cp_payload() > 0)
		{
			if (flag == NAT_BITMAP)			return ckpt->sit_nat_version_bitmap;
			else							return (unsigned char*)ckpt + F2FS_BLKSIZE;
		}
		else
		{
			offset = (flag == NAT_BITMAP) ?	le32_to_cpu(ckpt->sit_ver_bitmap_bytesize) : 0;
			return tmp_ptr + offset;
		}
	}
	void dec_valid_block_count(struct inode* inode, block_t count);
	bio* __bio_alloc(f2fs_io_info* fio, int npages);

// ==== f2fs.h ====

	inline bool is_sbi_flag_set(unsigned int type)	{	return test_bit(type, s_flag);		}
	inline void set_sbi_flag(unsigned int type) 	{	set_bit(type, s_flag);	}

	inline int __get_cp_reason(void)
	{
		int reason = CP_SYNC;
		if (test_opt_(F2FS_MOUNT_FASTBOOT))				reason = CP_FASTBOOT;
		if (is_sbi_flag_set(SBI_IS_CLOSE))	reason = CP_UMOUNT;
		return reason;
	}

	inline bool f2fs_cp_error(void) {	return is_set_ckpt_flags(CP_ERROR_FLAG);	}

	inline void f2fs_update_time(int type)
	{
		//	unsigned long now = jiffies;
		LONGLONG now = jcvos::GetTimeStamp();
		last_time[type] = now;

		/* DISCARD_TIME and GC_TIME are based on REQ_TIME */
		if (type == REQ_TIME)
		{
			last_time[DISCARD_TIME] = now;
			last_time[GC_TIME] = now;
		}
	}

	inline bool f2fs_readonly(void) const { return sb_rdonly(this); }

	inline s64 get_pages(int count_type)	const {	return atomic_read(&const_cast<f2fs_sb_info*>(this)->nr_pages[count_type]);	}
	inline void inc_page_count(int count_type);
	inline void dec_page_count(int count_type);

	inline int get_blocktype_secs(int block_type) const
	{
		unsigned int pages_per_sec = segs_per_sec * blocks_per_seg;
		unsigned int segs = boost::numeric_cast<UINT>((get_pages(block_type) + pages_per_sec - 1) >> log_blocks_per_seg);
		return segs / segs_per_sec;
	}
	inline void f2fs_update_iostat(enum iostat_type type, unsigned long long io_bytes);
	void f2fs_record_iostat(void);

#ifdef _DEBUG
	enum seg_type {
		SEG_TYPE_DATA, SEG_TYPE_CUR_DATA, SEG_TYPE_NODE, SEG_TYPE_CUR_NODE,	SEG_TYPE_MAX,
	};
	void DumpSegInfo(FILE *out = nullptr);
	f2fs_summary_block* get_sum_block(UINT segno, seg_type & ret_type);
#endif

};


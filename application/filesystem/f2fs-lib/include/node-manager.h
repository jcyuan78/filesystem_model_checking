///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
#pragma once


struct free_nid;
struct nat_entry;
struct node_info;

/* For free nid management */
enum nid_state
{
	FREE_NID,		/* newly added to free nid list */
	PREALLOC_NID,		/* it is preallocated */
	MAX_NID_STATE,
};

enum nat_state
{
	TOTAL_NAT,
	DIRTY_NAT,
	RECLAIMABLE_NAT,
	MAX_NAT_STATE,
};

struct f2fs_nm_info
{
public:
	f2fs_nm_info(f2fs_sb_info * sbi);
	~f2fs_nm_info(void);

	//inline functions
public:
	free_nid* __lookup_free_nid_list(nid_t n)	{	return radix_tree_lookup<free_nid>(&free_nid_root, n);	}

public:
	nat_entry* __lookup_nat_cache(nid_t n);
	int __insert_free_nid(free_nid* i);
	void __remove_free_nid(free_nid* i, enum nid_state state);
	void remove_free_nid(nid_t nid);
	int f2fs_get_node_info(nid_t nid, /*out*/ node_info* ni);
	void __move_free_nid(free_nid* i, enum nid_state org_state, enum nid_state dst_state);
	void update_free_nid_bitmap(nid_t nid, bool set, bool build);
	int f2fs_build_free_nids(bool sync, bool mount);
	int __get_nat_bitmaps(void);
	int __f2fs_build_free_nids(bool sync, bool mount);
	int scan_nat_page(page* nat_page, nid_t start_nid);
	bool add_free_nid(nid_t nid, bool build, bool update);


public:	// protected
	void cache_nat_entry(nid_t nid, struct f2fs_nat_entry* ne);
	nat_entry* __init_nat_entry(nat_entry* ne, f2fs_nat_entry* raw_ne, bool no_fail);
	bool f2fs_alloc_nid(OUT nid_t* nid);
	void f2fs_alloc_nid_done(nid_t nid);

	void __update_nat_bits(nid_t start_nid, struct page* page);



public:

	block_t nat_blkaddr;		/* base disk address of NAT */
	nid_t max_nid;			/* maximum possible node ids */
	nid_t available_nids;		/* # of available node ids */
	nid_t next_scan_nid;		/* the next nid to be scanned */
	unsigned int ram_thresh;	/* control the memory footprint */
	unsigned int ra_nid_pages;	/* # of nid pages to be readaheaded */
	unsigned int dirty_nats_ratio;	/* control dirty nats ratio threshold */

	/* NAT cache management */
	radix_tree_root /* <nat_entry> */		nat_root;		/* root of the nat entry cache */
	radix_tree_root /* <nat_entry_set> */	nat_set_root;	/* root of the nat set cache */
	SRWLOCK /*rw_semaphore*/  nat_tree_lock;	/* protect nat entry tree */
	struct list_head nat_entries;	/* cached nat entry list (clean) */
	CRITICAL_SECTION nat_list_lock;	/* protect clean nat entry list */
	unsigned int nat_cnt[MAX_NAT_STATE]; /* the # of cached nat entries */
	unsigned int nat_blocks;	/* # of nat blocks */

	/* free node ids management */
	radix_tree_root free_nid_root;/* root of the free_nid cache */
	// 存放free nid的列表，主要记录free的nid号。
	list_head free_nid_list;		/* list for free nids excluding preallocated nids */
	unsigned int nid_cnt[MAX_NID_STATE];	/* the number of free node id */
	CRITICAL_SECTION nid_list_lock;	/* protect nid lists ops */
	mutex build_lock;	/* lock for build free nids */
	unsigned char** free_nid_bitmap;
	unsigned char* nat_block_bitmap;
	unsigned short* free_nid_count;	/* free nid count of NAT block */

	/* for checkpoint */
	char* nat_bitmap;		/* NAT bitmap pointer */

	unsigned int nat_bits_blocks;	/* # of nat bits blocks */
	unsigned char* nat_bits;	/* NAT bits blocks */
	unsigned char* full_nat_bits;	/* full NAT pages */
	unsigned char* empty_nat_bits;	/* empty NAT pages */
#ifdef CONFIG_F2FS_CHECK_FS
	char* nat_bitmap_mir;		/* NAT bitmap mirror */
#endif
	int bitmap_size;		/* bitmap size */

	f2fs_sb_info* m_sbi;
};

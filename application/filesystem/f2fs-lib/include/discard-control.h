///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
#pragma once

/* for the bitmap indicate blocks to be discarded */
struct discard_entry
{
	struct list_head list;	/* list head */
	block_t start_blkaddr;	/* start blockaddr of current segment */
	unsigned char discard_map[SIT_VBLOCK_MAP_SIZE];	/* segment discard bitmap */
};

/* default discard granularity of inner discard thread, unit: block count */
#define DEFAULT_DISCARD_GRANULARITY		16

/* max discard pend list number */
#define MAX_PLIST_NUM		512
#define plist_idx(blk_num)	((blk_num) >= MAX_PLIST_NUM ? (MAX_PLIST_NUM - 1) : ((blk_num) - 1))

enum
{
	D_PREP,			/* initial */
	D_PARTIAL,		/* partially submitted */
	D_SUBMIT,		/* all submitted */
	D_DONE,			/* finished */
};

struct discard_info
{
	block_t lstart;			/* logical start address */
	block_t len;			/* length */
	block_t start;			/* actual start address in dev */
};

struct discard_cmd
{
	struct rb_node rb_node;		/* rb node located in rb-tree */
	union
	{
		struct
		{
			block_t lstart;	/* logical start address */
			block_t len;	/* length */
			block_t start;	/* actual start address in dev */
		};
		struct discard_info di;	/* discard info */

	};
	struct list_head list;		/* command list */
	completion wait;		/* compleation */
	block_device* bdev;	/* bdev */
	unsigned short ref;		/* reference count */
	unsigned char state;		/* state */
	unsigned char queued;		/* queued discard */
	int error;			/* bio error */
	CRITICAL_SECTION lock;		/* for state/bio_ref updating */
	unsigned short bio_ref;		/* bio reference count */
};

enum
{
	DPOLICY_BG,
	DPOLICY_FORCE,
	DPOLICY_FSTRIM,
	DPOLICY_UMOUNT,
	MAX_DPOLICY,
};

struct discard_policy
{
	int type;			/* type of discard */
	unsigned int min_interval;	/* used for candidates exist */
	unsigned int mid_interval;	/* used for device busy */
	unsigned int max_interval;	/* used for candidates not exist */
	unsigned int max_requests;	/* # of discards issued per round */
	unsigned int io_aware_gran;	/* minimum granularity discard not be aware of I/O */
	bool io_aware;			/* issue discard in idle time */
	bool sync;			/* submit discard with REQ_SYNC flag */
	bool ordered;			/* issue discard by lba order */
	bool timeout;			/* discard timeout for put_super */
	unsigned int granularity;	/* discard granularity */
};

struct f2fs_sb_info;

class discard_cmd_control : public CControlThread
{
public:
	discard_cmd_control(f2fs_sb_info* sbi);
public:
	virtual DWORD Run(void) { return issue_discard_thread(); }
	atomic_t atomic_read_queued(void) { return atomic_read(&queued_discard); }
	void set_discard_granularity(unsigned int g) { discard_granularity = g; }
	atomic_t atomic_read_cmd_count(void) { return atomic_read(&discard_cmd_cnt); }
public:
	friend struct f2fs_sm_info;
	friend struct f2fs_sb_info;

protected:
	void destroy_discard_cmd_control(void);
//	void f2fs_stop_discard_thread(void);

// ==== segment.cpp ====
public:
	void f2fs_wait_discard_bio(block_t blkaddr);
protected:
	void __punch_discard_cmd(discard_cmd* dc, block_t blkaddr);
	bool __drop_discard_cmd(void);
	bool f2fs_issue_discard_timeout(void);

protected:
	DWORD issue_discard_thread(void);
	void __init_discard_policy(discard_policy* dpolicy, int discard_type, unsigned int granularity);
	int __issue_discard_cmd(discard_policy* dpolicy);
	unsigned int __issue_discard_cmd_orderly(discard_policy* dpolicy);
	int __submit_discard_cmd(discard_policy* dpolicy, struct discard_cmd* dc, unsigned int* issued);
public:
	void __update_discard_tree_range(block_device* bdev, block_t lstart, block_t start, block_t len);
protected:
	void __relocate_discard_cmd(discard_cmd* dc);
	void __remove_discard_cmd(discard_cmd* dc);
	void __detach_discard_cmd(discard_cmd* dc);
	void __insert_discard_tree(block_device* bdev, block_t lstart,
		block_t start, block_t len, rb_node** insert_p, rb_node* insert_parent);
	discard_cmd* __attach_discard_cmd(block_device* bdev, block_t lstart,
		block_t start, block_t len,
		struct rb_node* parent, struct rb_node** p,
		bool leftmost);
	discard_cmd* __create_discard_cmd(block_device* bdev, block_t lstart, block_t start, block_t len);
	unsigned int __wait_all_discard_cmd(discard_policy* dpolicy);
	unsigned int __wait_discard_cmd_range(discard_policy* dpolicy, block_t start, block_t end);
	unsigned int __wait_one_discard_bio(discard_cmd* dc);

public:
	//	struct task_struct *f2fs_issue_discard;	/* discard thread */
		//HANDLE /*task_struct*/	f2fs_issue_discard;
		//DWORD thread_id;
	struct list_head entry_list;		/* 4KB discard entry list */
	struct list_head pend_list[MAX_PLIST_NUM];/* store pending entries */
	struct list_head wait_list;		/* store on-flushing entries */
	struct list_head fstrim_list;		/* in-flight discard from fstrim */
#ifdef _DEBUG
	long n_pend_list[MAX_PLIST_NUM];		// 统计列表的大小
#endif
	unsigned int discard_wake;		/* to wake up discard thread */
	HANDLE /*mutex*/ cmd_lock;
	unsigned int nr_discards;		/* # of discards in the list */
	unsigned int max_discards;		/* max. discards to be issued */
	unsigned int discard_granularity;	/* discard granularity */
	unsigned int undiscard_blks;		/* # of undiscard blocks */
	unsigned int next_pos;			/* next discard position */
	atomic_t issued_discard;		/* # of issued discard */
	atomic_t queued_discard;		/* # of queued discard */
	atomic_t discard_cmd_cnt;		/* # of cached cmd count */
	rb_root_cached root;		/* root of discard rb-tree */
	bool rbtree_check;			/* config for consistence check */

protected:
	f2fs_sb_info* m_sbi;
};


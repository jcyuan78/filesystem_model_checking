///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "control-thread.h"

struct f2fs_sb_info;

/* For SIT manager
 *
 * By default, there are 6 active log areas across the whole main area. When considering hot and cold data separation to reduce cleaning overhead, we split 3 for data logs and 3 for node logs as hot, warm, and cold types, respectively.
 * In the current design, you should not change the numbers intentionally. Instead, as a mount option such as active_logs=x, you can use 2, 4, and 6 logs individually according to the underlying devices. (default: 6)
 * Just in case, on-disk layout covers maximum 16 logs that consist of 8 for data and 8 for node logs. */


#define	NR_CURSEG_DATA_TYPE	(3)
#define NR_CURSEG_NODE_TYPE	(3)
#define NR_CURSEG_INMEM_TYPE	(2)
#define NR_CURSEG_PERSIST_TYPE	(NR_CURSEG_DATA_TYPE + NR_CURSEG_NODE_TYPE)
#define NR_CURSEG_TYPE		(NR_CURSEG_INMEM_TYPE + NR_CURSEG_PERSIST_TYPE)


struct flush_cmd
{
	completion wait;
	llist_node llnode;
	nid_t ino;
	int ret;
};

class flush_cmd_control : public CControlThread
{
public:
	flush_cmd_control(f2fs_sb_info * sb_info);
	~flush_cmd_control(void);

public:
	DWORD issue_flush_thread(void);
	//bool StartThread(void);
	//void StopThread(void);
	//static DWORD WINAPI _issue_flush_thread(LPVOID data);
	//bool is_running(void) { return (f2fs_issue_flush_thread && m_run); }
	virtual DWORD Run(void) { return issue_flush_thread(); }
	int f2fs_issue_flush(nid_t ino);
//	void f2fs_destroy_flush_cmd_control(bool free);
	atomic_t atomic_read_queued(void) { return atomic_read(&queued_flush); }

protected:
//	struct task_struct* f2fs_issue_flush;	/* flush thread */
	//HANDLE f2fs_issue_flush_thread;
	//DWORD  thread_id;

//	wait_queue_head_t flush_wait_queue;	/* waiting queue for wake-up */
	//HANDLE m_que_event;		// 用于出发queue的变化，代替wait queue
	atomic_t issued_flush;			/* # of issued flushes */
	atomic_t queued_flush;			/* # of queued flushes */
	struct llist_head issue_list;		/* list for command issue */
	struct llist_node* dispatch_list;	/* list for command dispatch */

	//member functions
	f2fs_sb_info* m_sb_info;
	//long m_run;
};

//<YUAN> segment info?
struct f2fs_sm_info
{
	f2fs_sm_info(f2fs_super_block * raw_super, f2fs_sb_info * sbi);
	~f2fs_sm_info(void);
	struct sit_info* sit_info = nullptr;		/* whole segment information */
	struct free_segmap_info* free_info = nullptr;	/* free segment information */
	struct dirty_seglist_info* dirty_info = nullptr;	/* dirty segment information */
	struct curseg_info* curseg_array = nullptr;	/* active segment information */

	SRWLOCK /*semaphore*/  curseg_lock;	/* for preventing curseg change */

	block_t seg0_blkaddr;		/* block address of 0'th segment */
	block_t main_blkaddr;		/* start block address of main area */
	block_t ssa_blkaddr;		/* start block address of SSA area */

	unsigned int segment_count;	/* total # of segments */
	unsigned int main_segments;	/* # of segments in main area */
	unsigned int reserved_segments;	/* # of reserved segments */
	unsigned int ovp_segments;	/* # of overprovision segments */

	/* a threshold to reclaim prefree segments */
	unsigned int rec_prefree_segments;

	/* for batched trimming */
	unsigned int trim_sections = 0;		/* # of sections to trim */

	list_head sit_entry_set;	/* sit entry set list */

	unsigned int ipu_policy;	/* in-place-update policy */
	unsigned int min_ipu_util;	/* in-place-update threshold */
	unsigned int min_fsync_blocks;	/* threshold for fsync */
	unsigned int min_seq_blocks;	/* threshold for sequential blocks */
	unsigned int min_hot_blocks;	/* threshold for hot block allocation */
	unsigned int min_ssr_sections;	/* threshold to trigger SSR allocation */
	/* for flush command control */
	flush_cmd_control* fcc_info= nullptr;
	/* for discard command control */
	discard_cmd_control* dcc_info = nullptr;

public:
	//inline unsigned int reserved_segments(void) { return reserved_segments; }

protected:
	inline int reserved_sections(unsigned int segs_per_sec)
	{
		UINT segno = reserved_segments;
		return (((segno) == -1) ? -1 : (segno) / segs_per_sec);
	}

public:
// 从静态函数转化为成员函数
// ==== 来自 segment.cpp ====
	int build_dirty_segmap(f2fs_sb_info* sbi);
	int build_curseg(f2fs_sb_info * sbi);
//	void f2fs_destroy_segment_manager();

protected:
	int init_victim_secmap(unsigned int bitmap_size);

	void destroy_dirty_segmap(void);
	void discard_dirty_segmap(enum dirty_type dirty_type);
	void destroy_victim_secmap(void);
	void destroy_curseg(void);
	void destroy_free_segmap(void);

	void destroy_sit_info(void);
	void f2fs_destroy_flush_cmd_control(bool free);

};
#pragma once

//#include <dokanfs-lib.h>
//#include <nand_driver.h>
#include "error.h"
#include "yaffs_define.h"
#include "yaffs_obj.h"
#include "tags_handler.h"
#include "yaffs_packedtags2.h"
#include "allocator.h"

#include "block_manager.h"



#define YAFFSFS_N_HANDLES	100
#define YAFFSFS_N_DSC		20

#define YAFFS_MAX_RW_SIZE	0x70000000
#define YAFFSFS_MAX_SYMLINK_DEREFERENCES 5
#define YAFFS_MAX_FILE_SIZE_32		0x7fffffff
#define YAFFS_MAX_FILE_SIZE \
	( (sizeof(loff_t) < 8) ? YAFFS_MAX_FILE_SIZE_32 : (0x800000000LL - 1) )
/* YAFFSFS_RW_SIZE must be a power of 2 */
#define YAFFSFS_RW_SHIFT (13)
#define YAFFSFS_RW_SIZE  (1<<YAFFSFS_RW_SHIFT)


/* Block data in RAM */
#define	YAFFS_NUMBER_OF_BLOCK_STATES (YAFFS_BLOCK_STATE_DEAD + 1)

#define YAFFS_TRACE_VERIFY		0x00010000
#define YAFFS_TRACE_VERIFY_NAND		0x00020000
#define YAFFS_TRACE_VERIFY_FULL		0x00040000
#define YAFFS_TRACE_VERIFY_ALL		0x000f0000



#define EXTRA_HEADER_INFO_FLAG	0x80000000
#define EXTRA_SHRINK_FLAG	0x40000000
#define EXTRA_SHADOWS_FLAG	0x20000000
#define EXTRA_SPARE_FLAGS	0x10000000

#define ALL_EXTRA_FLAGS		0xf0000000

/* Also, the top 4 bits of the object Id are set to the object type. */
#define EXTRA_OBJECT_TYPE_SHIFT (28)
#define EXTRA_OBJECT_TYPE_MASK  ((0x0f) << EXTRA_OBJECT_TYPE_SHIFT)





/* Fake object Id for summary data */
#define YAFFS_OBJECTID_SUMMARY		0x10

/* Binary data version stamps */
#define YAFFS_SUMMARY_VERSION		1



/*
 * Checkpoints are really no benefit on very small partitions.
 *
 * To save space on small partitions don't bother with checkpoints unless
 * the partition is at least this big.
 */
#define YAFFS_CHECKPOINT_MIN_BLOCKS 60
#define YAFFS_SMALL_HOLE_THRESHOLD 4


/* Give us a  Y=0x59, Give us an A=0x41, Give us an FF=0xff, Give us an S=0x53
* And what have we got... */
//#define YAFFS_MAGIC			0x5941ff53



struct yaffsfs_Handle {
	short int fdId;
	short int useCount;
};

struct yaffsfs_FileDes {
	u8 isDir : 1; 		/* This s a directory */
	u8 reading : 1;
	u8 writing : 1;
	u8 append : 1;
	u8 shareRead : 1;
	u8 shareWrite : 1;
	s32 inodeId : 12;		/* Index to corresponding yaffsfs_Inode */
	s32 handleCount : 10;	/* Number of handles for this fd */
};



union yaffs_block_info_union 
{
	struct yaffs_block_info bi;
	u32	as_u32[2];
};

struct yaffs_dev
{
	struct yaffs_param param;
	/* Context storage. Holds extra OS specific data for this device */
	void *os_context;
	//struct list_head dev_list;
	int ll_init;
	/* Runtime parameters. Set up by YAFFS. */
	u32 data_bytes_per_chunk;

	/* Non-wide tnode stuff */
	u16 chunk_grp_bits;	/* Number of bits that need to be resolved if the tnodes are not wide enough.				 */
	u16 chunk_grp_size;	/* == 2^^chunk_grp_bits */
	struct yaffs_tnode *tn_swap_buffer;

	/* Stuff to support wide tnodes */
	u32 tnode_width;
	u32 tnode_mask;
	size_t tnode_size;

	/* Stuff for figuring out file offset to chunk conversions */
	u32 chunk_shift;	/* Shift value */
	u32 chunk_div;		/* Divisor after shifting: 1 for 2^n sizes */
	u32 chunk_mask;		/* Mask to use for power-of-2 case */

	int is_mounted;
	int read_only;
	//bool is_checkpointed;
	int swap_endian;	/* Stored endian needs endian swap. */

	/* Stuff to support block offsetting to support start block zero */
	//u32 internal_start_block;
	//u32 internal_end_block;
	// 简化block offset，需要的话，在nand driver中实现。
	//int block_offset;
	//int chunk_offset;

	/* Runtime checkpointing stuff */
	int checkpt_page_seq;	/* running sequence number of checkpt pages */
	int checkpt_byte_count;
	int checkpt_byte_offs;
//	u8 *checkpt_buffer;
//	int checkpt_open_write;
	//u32 blocks_in_checkpt;
	int checkpt_cur_chunk;
	int checkpt_cur_block;
	int checkpt_next_block;
//	int *checkpt_block_list;
	//u32 checkpt_max_blocks;
	//u32 checkpt_sum;
	//u32 checkpt_xor;

	// 记录目前所需要的checkpoint block个数，如果这个数清零，会要求强制重新计算。
	// 当tnode的数量或者obj的数量有变化时，会把这个变量清零，强制重新计算
//	int checkpoint_blocks_required;	/* Number of blocks needed to store current checkpoint set */

	/* Block Info */
	//struct yaffs_block_info *block_info;
	//u8 *chunk_bits;			/* bitmap of chunks in use */
	//u8 block_info_alt : 1;	/* allocated using alternative alloc */
	//u8 chunk_bits_alt : 1;	/* allocated using alternative alloc */
	int chunk_bit_stride;	/* Number of bytes of chunk_bits per block. Must be consistent with chunks_per_block.				 */

	//int n_erased_blocks;
	// 下一个可分配的block
	//int alloc_block;		/* Current block being allocated off */
	//u32 alloc_page;
	//int alloc_block_finder;	/* Used to search for next allocation block */

	/* Object and Tnode memory management */
	yaffs_allocator *allocator;
	int n_obj;
	int n_tnodes;

	int n_hardlinks;

	//<MIGRATE> obj bucket在CYafFs中实现
	u32 bucket_finder;
//	int n_free_chunks;

	/* Garbage collection control */
	u32 *gc_cleanup_list;	/* objects to delete at the end of a GC. */
	u32 n_clean_ups;

	unsigned has_pending_prioritised_gc;	/* We think this device might
						have pending prioritised gcs */
	unsigned gc_disable;
	unsigned gc_block_finder;
	unsigned gc_dirtiest;
	unsigned gc_pages_in_use;
	unsigned gc_not_done;
	unsigned gc_block;
	unsigned gc_chunk;
	unsigned gc_skip;
	struct yaffs_summary_tags *gc_sum_tags;

	//<migrate> 移到CYaffs中		/* Special directories */
	/* Stuff for background deletion and unlinked files. */

	int buffered_block;			/* Which block is buffered here? */
	int doing_buffered_block_rewrite;

	struct yaffs_cache *cache;
	int cache_last_use;

	struct yaffs_obj *unlinked_deletion;	/* Current file being background deleted. */
	// 通过获取del_dir和unlink_dir的计数
	int n_bg_deletions;	/* Count of background deletions. */

	/* Temporary buffer management */
	struct yaffs_buffer temp_buffer[YAFFS_N_TEMP_BUFFERS];
	int max_temp;
	int temp_in_use;
	int unmanaged_buffer_allocs;
	int unmanaged_buffer_deallocs;

	/* yaffs2 runtime stuff */
	//unsigned seq_number;		/* Sequence number of currently allocating block */
	//unsigned oldest_dirty_seq;
	//unsigned oldest_dirty_block;

	/* Block refreshing */
	int refresh_skip;	/* A skip down counter. Refresh happens when this gets to zero. */

	/* Dirty directory handling */	// 移植到CYafFs::m_dirty_dirs;

	/* Summary */
	UINT32 chunks_per_summary;					// 每个summary所包含的chunk数
	struct yaffs_summary_tags *sum_tags;	// 一个chunk的summary

	/* Statistics */
	u32 n_page_writes;
	u32 n_page_reads;
	//u32 n_erasures;
	u32 n_bad_queries;
	u32 n_bad_markings;
	u32 n_erase_failures;
	u32 n_gc_copies;
	u32 all_gcs;
	u32 passive_gc_count;
	u32 oldest_dirty_gc_count;
	u32 n_gc_blocks;
	u32 bg_gcs;
	u32 n_retried_writes;
	//u32 n_retired_blocks;
	u32 n_ecc_fixed;
	u32 n_ecc_unfixed;
	u32 n_tags_ecc_fixed;
	u32 n_tags_ecc_unfixed;
	//u32 n_deletions;
	//u32 n_unmarked_deletions;
	u32 refresh_count;
	u32 cache_hits;
	u32 tags_used;
	u32 summary_used;

};

/*
 * The summary is built up in an array of summary tags.
 * This gets written to the last one or two (maybe more) chunks in a block.
 * A summary header is written as the first part of each chunk of summary data.
 * The summary header must match or the summary is rejected.	*/

 /* Summary tags don't need the sequence number because that is redundant. */
struct yaffs_summary_tags {
	unsigned obj_id;
	unsigned chunk_id;
	unsigned n_bytes;
};

/* Summary header */
struct yaffs_summary_header {
	unsigned version;	/* Must match current version */
	unsigned block;		/* Must be this block */
	unsigned seq;		/* Must be this sequence number */
	unsigned sum;		/* Just add up all the bytes in the tags */
};


#include "checkpoint.h"

class CYaffsFile;

class CYafFs : public CDokanFsBase
{
public:
	CYafFs();
	virtual ~CYafFs();

public:
	friend class CYaffsCheckPoint;

public:
	virtual void GetRoot(IFileInfo * & root);
	virtual void Lock(void) {};
	virtual void Unlock(void) {};

public:
	virtual ULONG GetFileSystemOption(void) const { return 0; }
	virtual bool ConnectToDevice(IVirtualDisk * dev);
	virtual void Disconnect(void);
	virtual bool Mount(void);
	virtual void Unmount(void);

	virtual bool DokanGetDiskSpace(ULONGLONG &free_bytes, ULONGLONG &total_bytes, ULONGLONG &total_free_bytes);

	virtual bool GetVolumnInfo(std::wstring & vol_name,
		DWORD & sn, DWORD & max_fn_len, DWORD & fs_flag,
		std::wstring & fs_name) {
		return false;
	}

	virtual bool DokanDeleteFile(const std::wstring & fn, IFileInfo * file, bool isdir);
	//virtual void FindFiles(void);
	virtual void FindStreams(void) {}
	virtual bool DokanMoveFile(const std::wstring & src_fn, const std::wstring & dst_fn, bool replace, IFileInfo * file) { return false; }

	virtual bool MakeFileSystem(UINT32 volume_size, const std::wstring & volume_name);
	virtual bool FileSystemCheck(bool repair) { return false; }

protected:
	void InitHandles(void);
	//bool IsPathDivider(YCHAR ch);
	bool Match(YCHAR a, YCHAR b) { return (a == b); }
	yaffs_obj * DoFindDirectory(yaffs_obj * startDir, const YCHAR * path, YCHAR ** name,
		int symDepth, int *notDir, bool &loop);

public:
	void SetError(int err) {}
	bool LowLevelInit(void);
	//bool InitBlocks(void);
	void DeInitBlocks(void);

protected:
	bool Initialize(void);
	void DeInitialize(void);
	//<MIGRATE> yaffs_guts.c : static int yaffs_create_initial_dir(struct yaffs_dev *dev)
	bool CreateInitialDir(void);
	//<MIGRATE> yaffs_guts.c : 	static struct yaffs_obj *yaffs_create_fake_dir(..)
	// name is for debug only
	CYaffsDir * CreateFakeDir(int number, u32 mode, const wchar_t * name);
	void DeleteFakeDir(void);


	void InitRawObjs(void);

	//-- Summary
	// summary，写入以部分数据以后，填入一个summary，便于启动时的快速恢复。
protected:

	//<MIGRATE>int yaffs_summary_fetch(struct yaffs_dev *dev,struct yaffs_ext_tags *tags,int chunk_in_block)
	bool SummaryFetch(yaffs_ext_tags * tags, UINT32 chunk_in_block);

	//<MIGRATE> yaffs_summary.c :	int yaffs_summary_init(struct yaffs_dev *dev)
	bool SummaryInit(void);
	//<MIGRATE> yaffs_summary.c :		void yaffs_summary_deinit(struct yaffs_dev *dev)
	void SummaryDeInit(void);
	//<MIGRATE> int yaffs_summary_add(struct yaffs_dev *dev,
	bool SummaryAdd(yaffs_ext_tags * tags, int chunk_in_nand);
	//<MIGRATE> yaffs_summary_read(dev, m_dev->sum_tags, blk);
	bool SummaryRead(yaffs_summary_tags * tags, int block);
	//<MIGRATE> yaffs_summary_sum(dev))
	UINT32 SummarySum(void);
	//<MIGRATE>	yaffs_pack_tags2_tags_only
	void PackTags2TagsOnly(yaffs_packed_tags2_tags_only * tags_only, yaffs_ext_tags * tags);
	//yaffs_dump_packed_tags2_tags_only
	void DumpPackedTags2TagsOnly(const struct yaffs_packed_tags2_tags_only *ptt);
	//yaffs_dump_tags2
	void DumpTags2(const struct yaffs_ext_tags *t);
	//yaffs_check_tags_extra_packable
	bool CheckTagsExtraPackable(const struct yaffs_ext_tags *t);
	//yaffs_pack_tags2
	void UnpackTags2TagsOnly(struct yaffs_ext_tags *t, struct yaffs_packed_tags2_tags_only *ptt_ptr);

	//void yaffs_unpack_tags2_tags_only(struct yaffs_dev *dev,
	//yaffs_unpack_tags2

//<MIGRATE> yaffs_summary_write
	// 将summary写入指定的block
	bool SummaryWrite(int block);
	//<MIGRATE> yaffs_summary_clear
	void SummaryClear(void);

	//--
	//<MIGRATE> yaffs_guts.c: yaffs_get_n_free_chunks()
	UINT32 GetNFreeChunks(void);



	//-- cache manager
public:
	bool FlushCacheForFile(CYaffsFile * obj, bool discard);

	//<MIGRATE> yaffs_guts.c :	static void yaffs_strip_deleted_objs(struct yaffs_dev *dev)
	void StripDeletedObjs(void);
	//<MIGRATE>	static void yaffs_fix_hanging_objs(struct yaffs_dev *dev)
	void FixHangingObjs(void);

	//<MIGRATE> yaffs_gust.c : yaffs_flush_whole_cache()
	void FlushWholeCache(bool discard);
	//<MIGRATE> yaffsfs.c : yaffsfs_IsDevBusy()
	bool IsDevBusy(void);
	//<MIGRATE> yaffsfs.c : static void yaffsfs_BreakDeviceHandles(struct yaffs_dev *dev)
	void BreakDeviceHandles(void);
	void VerifyObjects(void) {};
	void VerifyBlocks(void) {};
	void VerifyFreeChunks(void) {};
	void VerifyBlk(yaffs_block_info * bi, int block) {};
	void VerifyCollectedBlk(yaffs_block_info * bi, int block) {};

// 这里应该不需要handle管理
	CYaffsObject * HandleToObject(int handle) { return NULL; }
	yaffsfs_FileDes * HandleToFileDes(int handle) { return NULL; }
	yaffsfs_Handle * HandleToPointer(int handle) { return NULL; }
	void PutInode(int inode) {}

	

protected:
//<MIGRATE> yaffs2_scan_backwords()
	bool ScanBackwords(void);

//<MIGRATE> yaffs2_scan_chunk()
	//<TODO> migrate it
	bool ScanChunk(yaffs_block_info * bi, UINT32 blk, UINT32 chunk_in_block,
		int * found_chunks, BYTE * chunk_data, bool summary_available);
	//{
	//	return false;
	//}

public:
	size_t GetMaxFileSize(void) const 
	{
		if (sizeof(loff_t) < 8)		return YAFFS_MAX_FILE_SIZE_32;
		else			return ((loff_t)YAFFS_MAX_CHUNK_ID) * m_dev->data_bytes_per_chunk;
	}
	inline void ChunkToPage(int chunk, int &blk, int &page)
	{
		blk = chunk / m_dev->param.chunks_per_block;
		page = chunk % m_dev->param.chunks_per_block;
	}

//<MIGRATE> yaffs_guts.c : u8 *yaffs_get_temp_buffer(struct yaffs_dev * dev)
	BYTE * GetTempBuffer(void);
	void ReleaseTempBuffer(BYTE * buffer);
	// temp buffers
	bool InitTempBuffers(void);
	void DeinitTempBuffers(void);


//<MIGRATE> yaffs_guts.c :	int yaffs_rd_chunk_tags_nand()
	bool ReadChunkTagsNand(int nand_chunk, BYTE * buffer, yaffs_ext_tags * tags);
//<MIGRATE> yaffs_tagscompat.c : 	static int yaffs_rd_chunk_nand()
	bool ReadChunkNand(int nand_chunk, BYTE * data, yaffs_spare * spare, INandDriver::ECC_RESULT & ecc_result, int correct_errors);
//<MIGRATE> 	int yaffs_wr_chunk_tags_nand()
	bool WriteChunkTagsNand(int nand_chunk, const BYTE * buffer, yaffs_ext_tags *tags);
//<MIGRATE>    yaffs_wr_nand(dev, nand_chunk, data, &spare);
	bool WriteChunkNand(int nand_chunk, const BYTE * buffer, yaffs_spare * spare);
//<MIGRATE>		yaffs_write_new_chunk()
	int WriteNewChunk(const BYTE * buffer, yaffs_ext_tags * tag, bool use_reserve);

protected:
//<MIGRATE> yaffs_verify_chunk_written
	bool VerifyChunkWritten(int chunk, const BYTE * data, yaffs_ext_tags * tag) { return true; }
//<MIGRATE> yaffs_guts.c: void yaffs_handle_chunk_error()
	void HandleChunkError(yaffs_block_info * bi);
//<MIGRATE> yaffs_handle_chunk_wr_error()
	void HandleChunkWriteError(int chunk, bool erase_ok) {}
//<MIGRATE> yaffs_handle_chunk_wr_ok()
	void HandleChunkWriteOk(int chunk, const BYTE * data, yaffs_ext_tags * tag) {}

////<MIGRATE> yaffs_alloc_chunk()
	int AllocChunk(bool use_reserver, yaffs_block_info * & bi);
////<MIGRATE> yaffs_find_alloc_block()
//	int FindAllocBlock(void);

//-- Tnode的分配管理
protected:
//<MIGRATE> yaffs_guts.c : static void yaffs_deinit_tnodes_and_objs(struct yaffs_dev *dev)
	bool InitTnodesAndObjs(void);
	void DeInitTnodesAndObjs(void);
//<MIGRATE> yaffs_guts.c : void yaffs_init_raw_tnodes_and_objs()
	// 以下两个函数直接在Init/DeInitTnodesAndObjs中展开。
	//void InitRawTnodeAndObjs(void);
	//void DeInitRawTnodesAndObjs(void);
	void InitRawTnodes(void);
public:
//<MIGRATE> yaffs_guts.c : yaffs_tnode *yaffs_get_tnode()
	yaffs_tnode* GetTnode(void);
//<MIGRATE> yaffs_free_tnode()
	void FreeTnode(yaffs_tnode * & tn);

//<MIGRATE> yaffs_oh_to_size()
	loff_t ObjhdrToSize(struct yaffs_obj_hdr *oh);



//-- Check Point 和 Scan
public:
//<MIGRATE> yaffs_yaffs2.c/yaffs2_checkpt_invalidate()
	//void CheckptInvalidate(void);
protected:
//<MIGRATE> yaffs_yaffs2.c : yaffs_checkpoint_save()
	bool CheckpointSave(void);
//<MIGRATE> yaffs_checkptrw.c/yaffs_checkpt_erase()
	//bool CheckptErase(void);
//<MIGRATE> yaffs_yaffs2.c : int yaffs2_checkpt_restore(struct yaffs_dev *dev)
	bool CheckptRestore(void);
//<MIGRATE> yaffs_yaffs2.c: yaffs_rd_checkpt_data()
	bool ReadCheckptData(void);
	//<MIGRATE> 	yaffs2_checkpt_open
	//bool CheckptOpen(bool writing);
	//<MIGRATE> 	yaffs2_checkpt_space_ok
	//bool CheckptSpaceOk(void);
	//<MIGRATE>		yaffs2_checkpt_init_chunk_hdr(dev);
	void CheckptInitChunkHeader(void);
	//<MIGRATE> 	yaffs2_rd_checkpt_validity_marker
	//bool ReadCheckptValidityMarker(int head);

	//<MIGRATE> 	yaffs2_checkpt_rd
	// 从checkpt_buffer中读取指定数量(size)的字节到cp，如果buffer中的数据耗尽，则从nand中读取。
	//	从nand中读取的方法：搜索所有的block的page 0，找到存放check point的block。从page 0开始按顺序读取。
	//size_t CheckptRead(void * cp, size_t size);

	//<MIGRATE> 				yaffs2_checkpt_find_block(dev);
	// 扫描所有block，读取第0 page，检查tag是否为checkpt。如果是，则复制到checkpt
	//void CheckptFindBlock(void);

	//<MIGRATE> 		yaffs2_checkpt_check_chunk_hdr(dev)
	// 检查checkpt_buffer中头部的相关参数，并且offset移动到头部后
	//bool CheckptCheckChunkHeader(void);

	//<MIGRATE> 	yaffs2_rd_checkpt_dev
	bool ReadCheckptDev(CYaffsCheckPoint &);

	//<MIGRATE>	yaffs_checkpt_dev_to_dev(dev, &cp);
	//	从yaffs_checkpt_dev中读取，并且保存到m_dev中
	//void CheckptDevToDev(yaffs_checkpt_dev * cp);
	//<MIGRATE> yaffs2_checkpt_obj_to_obj()
	bool CheckptObjToObj(CYaffsObject * obj, yaffs_checkpt_obj * cp);
	//<MIGRATE> yaffs_rd_checkpt_tnodes()
	bool ReadCheckptTnodes(CYaffsCheckPoint &, CYaffsObject * obj);
	//<MIGRATE> 	yaffs2_rd_checkpt_objs
	bool ReadCheckptObjects(CYaffsCheckPoint &);
	//<MIGRATE> 	yaffs2_rd_checkpt_sum
	//bool ReadCheckptSum(CYaffsCheckPoint &);

	//<MIGRATE> 	yaffs_checkpt_close
	//bool CheckptClose(void);

	//<MIGRATE> yaffs2_checkpt_flush_buffer
	//bool CheckptFlushBuffer(void);

	//<MIGRATE> yaffs_calc_checkpt_blocks_required(dev);
	int CalcCheckptBlocksRequired(void);

	//<MIGRATE> yaffs2_checkpt_required
	bool CheckptRequired(void);
	//<MIGRATE>		yaffs2_wr_checkpt_data(dev);
	bool WriteCheckptData(void);

	////	ok = yaffs2_wr_checkpt_validity_marker(dev, 1);
	//bool WriteCheckptValidityMarker(int header);
	//	ok = yaffs2_wr_checkpt_dev(dev);
	bool WriteCheckptDev(CYaffsCheckPoint &);
	//	ok = yaffs2_wr_checkpt_objs(dev);
	bool WriteCheckptObjs(CYaffsCheckPoint & checkpt);
	//	ok = yaffs2_wr_checkpt_sum(dev);
	//bool WriteCheckptSum(CYaffsCheckPoint & checkpt);

//	yaffs2_checkpt_find_erased_block(dev);
	//void CheckptFindErasedBlock(void);


public:
//int yaffs2_checkpt_wr(struct yaffs_dev *dev, const void *data, int n_bytes)
	//size_t CheckptWrite(const void * data, size_t n_bytes);

protected:
//	static void yaffs2_dev_to_checkpt_dev()
	//void DevToCheckptDev(struct yaffs_checkpt_dev *cp);





//-- Object管理
protected:
	//<MIGRATE> yaffs_find_or_create_by_number
	bool FindOrCreateByNumber(CYaffsObject * & out_obj, UINT32 obj_id, yaffs_obj_type type);
	//<MIGRATE> yaffs_find_by_number(dev, number);
	bool FindByNumber(CYaffsObject * & out_obj, UINT32 obj_id);
public:
	// 计算object的hash，并把它放入obj_bucket中
	void HashObject(CYaffsObject * obj);
	// <MYGRATE> yaffs_guts.c : yaffs_unhash_obj()
	void UnhashObject(CYaffsObject *obj);
	// static int yaffs_new_obj_id(struct yaffs_dev *dev)
	int NewObjectId(void);
	//static int yaffs_find_nice_bucket(struct yaffs_dev *dev)
	int FindNiceBucket(void);




//--
protected:

public:
	//inline void ClearCheckpointBlocksRequired(void) { m_dev->checkpoint_blocks_required = 0; };


//<migtate from> yaffs_checkptrw.c/apply_chunk_offset(dev, nand_chunk);
	//int ApplyChunkOffset(int chunk) {return chunk - m_dev->chunk_offset; }
	//int ApplyBlockOffset(int block) {return block - m_dev->block_offset;	}

//<MIGRATE> yaffs_getblockinfo.h : yaffs_get_block_info()
	yaffs_block_info * GetBlockInfo(int block_num)
	{
		return m_block_manager->GetBlockInfo(block_num);
	}

////<MIGRATE> yaffs_check_chunk_bit(struct yaffs_dev *dev, int blk, int chunk)
	//bool CheckChunkBit(int blk, int chunk)
	//{

	//}

////<MIGRATE> void yaffs_verify_chunk_bit_id(struct yaffs_dev *dev, int blk, int chunk)
//	void VerifyChunkBitId(int blk, int chunk);

	/*
 * yaffs2_update_oldest_dirty_seq()
 * Update the oldest dirty sequence number whenever we dirty a block.
 * Only do this if the oldest_dirty_seq is actually being tracked.
 */
//<MIGRATE>	void yaffs2_update_oldest_dirty_seq()
	//void UpdateOldestDirtySeq(UINT32 block_no, yaffs_block_info *bi);

//<MIGRATE>	yaffs_guts.c : void yaffs_block_became_dirty(struct yaffs_dev *dev, int block_no)
	void BlockBecameDirty(int block_no);
//<MIGRATE> yaffs2_clear_oldest_dirty_seq
	//void ClearOldestDirtySeq(const yaffs_block_info*);

public:
	void AddToDirty(CYaffsObject * obj);

//<MIGRATE>	int yaffs_erase_block(struct yaffs_dev *dev, int block_no)
	bool EraseBlock(int block_no);
//<MIGRATE> yaffs_nand.c : int yaffs_query_init_block_state()
	//bool QueryInitBlockState(int block_no, enum yaffs_block_state & state, UINT32 & seq_num)
	//{
	//	return m_tagger->QueryBlock(block_no, state, seq_num);
	//}

//////-- functions for bitmap
//<MIGRATE> static inline u8 *yaffs_block_bits(struct yaffs_dev *dev, int blk)
	//BYTE*  BlockBits(int block);
//<MIGRATE> void yaffs_clear_chunk_bit(struct yaffs_dev *dev, int blk, int chunk)
	//void ClearChunkBit(int blk, int chunk);

//<MIGRATE> yaffs_bitmap.c : yaffs_clear_chunk_bits()
	//void ClearChunkBits(int blk)
	//{
	//	BYTE *blk_bits = BlockBits(blk);
	//	memset(blk_bits, 0, m_dev->chunk_bit_stride);
	//}
//<MIGRATE> yaffs_set_chunk_bit()
	//void SetChunkBit(int blk, int chunk)
	//{
	//	BYTE *blk_bits = BlockBits(blk);
	//	VerifyChunkBitId(blk, chunk);
	//	blk_bits[chunk / 8] |= (1 << (chunk & 7));
	//}



//<MIGRATE> yaffs_skip_verification
	bool SkipVerification(void) {
	return !(m_trace_mask &	 (YAFFS_TRACE_VERIFY | YAFFS_TRACE_VERIFY_FULL));
	}

protected:
//<MIGRATE> yaffs_check_chunk_erased
	bool CheckChunkErased(int chunk);
//<MIGRATE> yaffs_check_ff()
	bool CheckFF(BYTE * data, size_t size);
public:
//<MIGRATE>		yaffs_retire_block(dev, block_no);
	void RetireBlock(int block_no);

//<MIGRATE> yaffs_guts.c : yaffs_invalidate_whole_cache()
	// 删除obj使用的chache
	void InvalidateWholeCache(CYaffsObject *obj);

	void ChunkDel(int chunk_id, bool mark_flash, int lyn);

//-- garbage collection functions
public:
//<MIGRATE> yaffs_check_gc()
	bool CheckGc(bool background);
protected:
//<MIGRATE> yaffs2_find_refresh_block
	UINT32 FindRefreshBlock(void);
//<MIGRATE> yaffs_find_gc_block();
	UINT32 FindGcBlock(bool agressive, bool background);
//<MIGRATE> yaffs_gc_block();
	bool GcBlock(int gc_block, bool aggressive);
//<MIGRATE> yaffs_get_erased_chunks()
	//int GetErasedChunks(void);
//<MIGRATE> yaffs_block_ok_for_gc()
	bool BlockOkForGc(const yaffs_block_info * bi);
//<MIGRATE> yaffs2_find_oldest_dirty_seq()
	//void FindOldestDirtySeq(void);
//<MIGRATE> yaffs_summary_gc();
	void SummaryGc(int block);
//<MIGRATE> yaffs_still_some_chunks()
	//bool StillSomeChunks(int block);
//<MIGRATE> yaffs_gc_process_chunk()
	bool GcProcessChunk(yaffs_block_info * bi, int chunk, BYTE * buf);
//<MIGRATE> yaffs_calc_oldest_dirty_seq();
	//void CaleOldestDirtySeq(void);

public:

//<MIGRATE> yaffs_check_alloc_available(dev, YAFFS_SMALL_HOLE_THRESHOLD + 1)
	bool CheckAllocAvailable(int n_chunks);

//<MIGRATE> yaffs_grab_chunk_cache()
	yaffs_cache * GrabChunkCache(void) { return NULL; };
//<MIGRATE> yaffs_use_cache()
	void UseCache(yaffs_cache * cache, bool is_write);

//<MIGRATE> yaffs_obj_cache_dirty()
	bool ObjectCacheDirty(CYaffsObject * obj);
//<MIGRATE> yaffs_find_chunk_cache()
	yaffs_cache * FindChunkCache(CYaffsObject * obj, int chunk);
//<MIGRATE> yaffs_invalidate_chunk_cache()
	void InvalidateChunkCache(CYaffsObject * obj, int chunk);

protected:
////<MIGRATE> yaffs_skip_rest_of_block()
//// 跳过当前的active block中剩余的page
//	void SkipRestOfBlock(void);

public:
//<MIGRATE> yaffs_guts.c : yaffs_addr_to_chunk()
	// 从文件的偏移量(offset)转换为chunk地址(chunk)和chunk内偏移量(start)
	void AddrToChunk(loff_t offset, int & chunk, size_t & start);

public:
	//bool CreateNandDriver(bool format);
	bool CreateTagHandler(void);
	// properties
	inline UINT32 GetBytePerChunk(void) const { return m_dev->data_bytes_per_chunk; }
	inline UINT32 GetChunkPerBlock(void) const { return m_dev->param.chunks_per_block; }
	inline bool IsDisableSoftDel(void) const { return m_dev->param.disable_soft_del; }
	//inline bool IsYaffs2(void) const { return m_dev->param.is_yaffs2; }
	inline bool IsCacheBypassAligned(void) const { return m_dev->param.cache_bypass_aligned; }
	//inline bool IsInbandTags(void) const { return m_dev->param.inband_tags; }
	inline size_t GetCacheNum(void) const { return m_dev->param.n_caches; }
//	inline UINT32 GetStartBlock(void) const { return m_dev->internal_start_block; }
//	inline UINT32 GetEndBlock(void) const { return m_dev->internal_end_block; }
	inline CBlockManager * GetBlockManager(void) { return m_block_manager; }
	inline bool ValidChunkId(UINT32 chunk_id) const {
		return (chunk_id >= m_block_manager->GetFirstBlock() * m_dev->param.chunks_per_block) &&
			(chunk_id < (m_block_manager->GetLastBlock() + 1) * m_dev->param.chunks_per_block);
	}
	inline int IsDeferedDirUpdate(void) const { return m_dev->param.defered_dir_update; }

	inline UINT32 GetTnodeWidth(void) const { return m_dev->tnode_width; }
	inline size_t GetTnodeSize(void) const { return m_dev->tnode_size; }
	inline UINT32 GetTnodeMask(void) const { return m_dev->tnode_mask; }
	inline UINT16 GetChunkGrpBits(void) const { return m_dev->chunk_grp_bits; }
	inline size_t GetChunkGrpSize(void) const { return m_dev->chunk_grp_size; }

public:
	CYaffsObjAllocator & GetYaffsObjAllocator() { return m_obj_allocator; }
// Debug functions
protected:
	//void ShowBlockStates(void);


protected:
	bool m_handlesInitialized;
	DirSearchContext m_dsc[YAFFSFS_N_DSC];
	Inode m_inode[YAFFSFS_N_HANDLES];
	FileDes m_fd[YAFFSFS_N_HANDLES];
	Handle m_handle[YAFFSFS_N_HANDLES];

//	struct list_head m_deviceList = { &(m_deviceList), &(m_deviceList) };

	INandDriver * m_driver;
	ITagsHandler * m_tagger;
	yaffs_dev * m_dev;

	UINT32 m_trace_mask;
	CTnodeAllocator m_tnode_allocator;
	CYaffsObjAllocator m_obj_allocator;

	CBlockManager * m_block_manager;
	CYaffsCheckPoint * m_checkpt;

public:
	// only open for YaffsObject
	CYaffsDir * m_root_dir;
	CYaffsDir * m_lost_n_found;
	CYaffsDir * m_unlinked_dir;	// Directory where unlinked and deleted files live.
	CYaffsDir * m_del_dir;		// Directory where deleted objects are sent to disappear. */

	std::list<CYaffsObject*> m_obj_bucket[YAFFS_NOBJECT_BUCKETS];
	std::list<CYaffsObject*> m_dirty_dirs;


	// for DEBUG only
public:
	static size_t m_obj_num;
};


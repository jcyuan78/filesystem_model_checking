#pragma once

#include "yaffs_define.h"
#include "tags_handler.h"
#include "checkpoint.h"

#define INVALID_BLOCK (-1)
#define VALID_BLOCKID(blk) (blk != (UINT)-1)
#define INVALID_BLOCKID(blk) (blk == (UINT)-1)


/* Sequence numbers are used in YAFFS2 to determine block allocation order.
 * The range is limited slightly to help distinguish bad numbers from good.
 * This also allows us to perhaps in the future use special numbers for
 * special purposes.
 * EFFFFF00 allows the allocation of 8 blocks/second (~1Mbytes) for 15 years,
 * and is a larger number than the lifetime of a 2GB device.
 */
#define YAFFS_LOWEST_SEQUENCE_NUMBER	0x00001000
#define YAFFS_HIGHEST_SEQUENCE_NUMBER	0xefffff00
 /* Special sequence number for bad block that failed to be marked bad */
#define YAFFS_SEQUENCE_BAD_BLOCK	0xffff0000

/* Yaffs2 scanning */
struct yaffs_block_index
{
	int seq;
	UINT32 block;
};



class CBlockManager
{
public:
	CBlockManager(INandDriver * driver);
	~CBlockManager(void);

public:
	bool InitBlockManager(INandDriver * driver, ITagsHandler* tagger, 
		UINT32 start_blk, UINT32 end_blk, UINT32 chunk_per_blk, UINT32 reserved_blk);
	void Deinitialize(void);
	const yaffs_block_info * GetBlockInfo(UINT32 blk) const;
	yaffs_block_info * GetBlockInfo(UINT32 blk);

	//<MIGRATE> yaffs_alloc_chunk()
	UINT32 AllocChunk(yaffs_block_info * & block_ptr);

	bool CleanBlock(UINT32 blk);
	bool RetireBlock(UINT32 blk);

	//<MIGRATE> yaffs_check_chunk_bit(struct yaffs_dev *dev, int blk, int chunk)
	bool CheckChunkBit(UINT32 blk, UINT32 chunk) const;
	//<MIGRATE> void yaffs_clear_chunk_bit(struct yaffs_dev *dev, int blk, int chunk)
	void ClearChunkBit(UINT32 blk, UINT32 chunk);

	UINT32 GetErasedBlocks(void) const { return m_erased_blocks; }
	UINT32 GetErasedChunks(void) const;
	UINT32 GetSeqNum(void) const { return m_seq_number; }
	UINT32 GetBlockNumber(void) const { return m_blk_num; }
	UINT32 GetCheckptMaxBlocks(void) const {
		return (m_internal_end_block - m_internal_start_block) / 16 + 2;
	}
	UINT32 GetFirstBlock(void) const { return m_internal_start_block; }
	UINT32 GetLastBlock(void) const { return m_internal_end_block; }
	UINT32 GetChunksPerBlock(void) const { return m_chunks_per_block; }
	UINT32 GetAllocBlocks(void) const { return m_alloc_block; }
	UINT32 GetFreeChunks(void) const { return m_free_chunks; }
	inline UINT32 GetOldestDirtyBlock(void) const { return m_oldest_dirty_block; }
	UINT32 GetBlockState(UINT32 block) const;
	inline UINT32 GetBitStride(void) const { return m_chunk_bit_stride;}
	inline UINT32 GetAvailBlock(void) const {
		if (m_erased_blocks > m_reserved_blocks) return m_erased_blocks - m_reserved_blocks;
		else return 0;
	}

	UINT32 CheckAllocAvailable(UINT32 chunks, UINT32 checkpt_blks) const;

	UINT32 FindOldestBlock(void);
	UINT32 FindOldestDirtyBlock(UINT32 & seq);

	//<MIGRATE> yaffs_skip_rest_of_block()
	// 跳过当前的active block中剩余的page
	void SkipRestOfBlock(void);

	UINT32 ScanBackwords(yaffs_block_index *, UINT32 & checkpt_blks, ITagsHandler * tagger);

	UINT32 FindBlockBySeq(UINT32 start, UINT32 seq);
	UINT32 FindEmptyBlock(UINT32 start);
	void BlockUsed(UINT32 blk_num, UINT32 chunks=0);
	void BlockReleased(UINT32 blk_num, UINT32 chunks = 0);

	void ReleaseBlock(UINT32 blk);


	bool ReadFromCheckpt(CYaffsCheckPoint & checkpt, int & n_bg_deletions);
	bool WriteToCheckpt(CYaffsCheckPoint & checkpt, int n_bg_deletions);

	//<MIGRATE> yaffs_set_chunk_bit()
	void SetChunkBit(UINT32 blk, UINT32 chunk)
	{
		BYTE *blk_bits = BlockBits(blk);
		VerifyChunkBitId(blk, chunk);
		blk_bits[chunk / 8] |= (1 << (chunk & 7));
	}

	UINT32 GetNFreeChunks(UINT32 dirty_caches, UINT32 checkpts);
	// 返回ture，表示需要删除整个block，否则不需要删除block
	bool ChunkDel(UINT32 block, UINT32 page);

	void ClearOldestDirtySeq(UINT32 block);
	void ClaimPage(UINT32 block, UINT32 page);
	bool StillSomeChunks(UINT32 blk);

	bool ScanChunkInBlock(UINT32 blk, UINT32 last_chunk, bool summary, CYafFs * fs);
	bool ScanChunkUnused(yaffs_block_info * bi, UINT32 blk, UINT32 chunk_in_block, int * found_chunks);

protected:
	//<MIGRATE> yaffs_find_alloc_block()
	UINT32 FindAllocBlock(void);

	//yaffs_block_info * _GetBlockInfo(UINT32 blk);
	//////-- functions for bitmap
	//<MIGRATE> static inline u8 *yaffs_block_bits(struct yaffs_dev *dev, int blk)
	BYTE*  BlockBits(UINT32 block);

	const BYTE*  BlockBits(UINT32 block) const ;
	//<MIGRATE> yaffs_bitmap.c : yaffs_clear_chunk_bits()
	void ClearChunkBits(UINT32 blk)
	{
		BYTE *blk_bits = BlockBits(blk);
		memset(blk_bits, 0, m_chunk_bit_stride);
	}

	//<MIGRATE> void yaffs_verify_chunk_bit_id(struct yaffs_dev *dev, int blk, int chunk)
	void VerifyChunkBitId(UINT32 blk, UINT32 chunk) const;

	void UpdateOldestDirtySeq(UINT32 block_no, yaffs_block_info *bi);
	//void BlockBecameDirty(int block_no);

	//void QueryInitBlockState(void);


protected:
	yaffs_block_info * m_block_info;
	UINT32 m_internal_start_block;
	UINT32 m_internal_end_block;
	UINT32 m_blk_num;
	UINT32 m_chunk_bit_stride;
	UINT32 m_chunks_per_block;
	UINT32 m_reserved_blocks;

	UINT32 m_free_chunks;
	UINT32 m_erased_blocks;
	UINT32 m_retired_blocks;
	UINT32 m_seq_number;		// 

	UINT32 m_alloc_block;		// 下一个可分配的block；
	UINT32 m_alloc_page;
	UINT32 m_alloc_block_finder;	// Used to search for next allocation block 

	UINT32 m_oldest_dirty_block;
	UINT32 m_oldest_dirty_seq;

	INandDriver * m_driver;
	BYTE * m_chunk_bits;
};
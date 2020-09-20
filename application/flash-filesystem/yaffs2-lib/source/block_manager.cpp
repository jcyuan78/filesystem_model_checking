#include "stdafx.h"
#include "../include/block_manager.h"

LOCAL_LOGGER_ENABLE(L"blockmanager", LOGGER_LEVEL_WARNING);

#include "../include/yaf_fs.h"


CBlockManager::CBlockManager(INandDriver * driver)
	:m_block_info(NULL), m_chunk_bits(NULL), m_driver(NULL)
{
	m_driver = driver;
	JCASSERT(m_driver);
	m_driver->AddRef();
}

CBlockManager::~CBlockManager(void)
{
	delete [] m_block_info;
	delete[] m_chunk_bits;
	RELEASE(m_driver);
}

bool CBlockManager::InitBlockManager(INandDriver * driver, ITagsHandler * tagger, 
	UINT32 start_blk, UINT32 end_blk, UINT32 chunk_per_blk, UINT32 reserved_blk)
{
	//int n_blocks = m_dev->internal_end_block - m_dev->internal_start_block + 1;
	m_internal_start_block = start_blk;
	m_internal_end_block = end_blk;
	m_erased_blocks = 0;
	m_oldest_dirty_block = 0;
	m_oldest_dirty_seq = 0;
	m_alloc_block = -1;	/* force it to get a new one */
	m_alloc_page = -1;
	m_retired_blocks = 0;
	m_free_chunks = 0;
	m_reserved_blocks = reserved_blk;

	m_blk_num = end_blk - start_blk + 1;
	m_chunks_per_block = chunk_per_blk;
//	m_dev->block_info = NULL;
//	m_dev->chunk_bits = NULL;
	

	/* If the first allocation strategy fails, thry the alternate one */
	m_block_info = new yaffs_block_info[m_blk_num];
	if (!m_block_info) THROW_ERROR(ERR_APP, L"failed on creating block info");

	/* Set up dynamic blockinfo stuff. Round up bytes. */
	m_chunk_bit_stride = (m_chunks_per_block + 7) / 8;
	m_chunk_bits = new BYTE[m_chunk_bit_stride * m_blk_num];
	if (!m_chunk_bits) THROW_ERROR(ERR_APP, L"failed on allocate chunk bits");

	memset(m_block_info, 0, m_blk_num * sizeof(struct yaffs_block_info));
	memset(m_chunk_bits, 0, m_chunk_bit_stride * m_blk_num);
	return true;

}

void CBlockManager::Deinitialize(void)
{
	delete[]m_block_info;
	m_block_info = NULL;
	delete[] m_chunk_bits;
	m_chunk_bits = NULL;
}

const yaffs_block_info * CBlockManager::GetBlockInfo(UINT32 blk) const
{
	if (blk < m_internal_start_block || blk > m_internal_end_block)
		THROW_ERROR(ERR_APP, L"invalid block id=%d, expected:(%d~%d)", blk, m_internal_start_block, m_internal_end_block);
	return &m_block_info[blk - m_internal_start_block];
}

UINT32 CBlockManager::AllocChunk(yaffs_block_info * & block_ptr)
{
	int ret_val;
	struct yaffs_block_info *bi;

	if (INVALID_BLOCKID(m_alloc_block))
	{	/* Get next block to allocate off */
		m_alloc_block = FindAllocBlock();
		m_alloc_page = 0;
	}

//	if (m_erased_blocks < (int)m_param.n_reserved_blocks && m_alloc_page == 0)
//		LOG_NOTICE(L"Allocating reserve");

	/* Next page please.... */
	if (VALID_BLOCKID(m_alloc_block))
	{
		bi = GetBlockInfo(m_alloc_block);
		ret_val = (m_alloc_block * m_chunks_per_block) + m_alloc_page;
		bi->pages_in_use++;
		SetChunkBit(m_alloc_block, m_alloc_page);
		m_alloc_page++;		//下一个分配的page
		m_free_chunks--;

		/* If the block is full set the state to full */
		if (m_alloc_page >= m_chunks_per_block)
		{
			bi->block_state = YAFFS_BLOCK_STATE_FULL;
			m_alloc_block = -1;
		}
		block_ptr = bi;
		return ret_val;
	}
	LOG_ERROR(L"[err] !!!!!!!!! Allocator out !!!!!!!!!!!!!!!!!");
	return -1;
}

bool CBlockManager::CleanBlock(UINT32 blk)
{
	/* Clean it up... */
	yaffs_block_info *bi = GetBlockInfo(blk);

	bi->block_state = YAFFS_BLOCK_STATE_EMPTY;
	bi->seq_number = 0;
	m_erased_blocks++;
	bi->pages_in_use = 0;
	bi->has_shrink_hdr = 0;
	bi->skip_erased_check = 1;	/* Clean, so no need to check */
	bi->gc_prioritise = 0;
	bi->has_summary = 0;

	ClearChunkBit(blk, 0);
	return true;
}

bool CBlockManager::RetireBlock(UINT32 blk)
{
	yaffs_block_info *bi = GetBlockInfo(blk);
	bi->block_state = YAFFS_BLOCK_STATE_DEAD;
	bi->gc_prioritise = 0;
	bi->needs_retiring = 0;

	m_retired_blocks++;

	//return false;
	return true;
}

UINT32 CBlockManager::GetNFreeChunks(UINT32 dirty_caches, UINT32 checkpts)
{
	/* This is what we report to the outside world */
	int n_free;
	UINT32 blocks_for_checkpt;
	//u32 i;

	n_free = m_free_chunks;
	//	n_free += m_dev->n_deleted_files;

		/* Now count and subtract the number of dirty chunks in the cache. */

	n_free -= dirty_caches;
	n_free -= ((m_reserved_blocks + 1) * m_chunks_per_block);
	/* Now figure checkpoint space and report that... */
	blocks_for_checkpt = checkpts;
	n_free -= (blocks_for_checkpt * m_chunks_per_block);

	if (n_free < 0) n_free = 0;

	return n_free;
}

UINT32 CBlockManager::FindAllocBlock(void)
{
	if (m_erased_blocks < 1)
	{	/* Hoosterman we've got a problem. Can't get space to gc		 */
		LOG_ERROR(L"yaffs tragedy: no more erased blocks");
		return -1;
	}

	/* Find an empty block. */
	for (UINT32 ii = m_internal_start_block; ii <= m_internal_end_block; ii++)
	{
		m_alloc_block_finder++;

		if (m_alloc_block_finder < m_internal_start_block
			|| m_alloc_block_finder > m_internal_end_block)
		{
			m_alloc_block_finder = m_internal_start_block;
		}

		struct yaffs_block_info *bi;
		bi = GetBlockInfo(m_alloc_block_finder);

		if (bi->block_state == YAFFS_BLOCK_STATE_EMPTY)
		{
			bi->block_state = YAFFS_BLOCK_STATE_ALLOCATING;
			m_seq_number++;
			bi->seq_number = m_seq_number;
			m_erased_blocks--;

			LOG_NOTICE(L"Allocated block %d, seq  %d, %d left",
				m_alloc_block_finder, m_seq_number, m_erased_blocks);
			return m_alloc_block_finder;
		}
	}
	LOG_NOTICE(L"yaffs tragedy: no more erased blocks, but there should have been %d",
		m_erased_blocks);
	return -1;
}

yaffs_block_info * CBlockManager::GetBlockInfo(UINT32 blk)
{
	if (blk < m_internal_start_block || blk > m_internal_end_block)
		THROW_ERROR(ERR_APP, L"invalid block id=%d, expected:(%d~%d)", blk, m_internal_start_block, m_internal_end_block);
	return &m_block_info[blk - m_internal_start_block];
}

BYTE * CBlockManager::BlockBits(UINT32 blk)
{
	if (blk < m_internal_start_block || blk > m_internal_end_block)
		THROW_ERROR(ERR_APP, L"BlockBits block %d is not valid, expected:(%d~%d)", blk,
			m_internal_start_block, m_internal_end_block);
	return m_chunk_bits + (m_chunk_bit_stride * (blk - m_internal_start_block));
}

const BYTE * CBlockManager::BlockBits(UINT32 blk) const
{
	if (blk < m_internal_start_block || blk > m_internal_end_block)
		THROW_ERROR(ERR_APP, L"BlockBits block %d is not valid, expected:(%d~%d)", blk,
			m_internal_start_block, m_internal_end_block);
	return m_chunk_bits + (m_chunk_bit_stride * (blk - m_internal_start_block));
}

void CBlockManager::ClearChunkBit(UINT32 blk, UINT32 chunk)
{
	BYTE *blk_bits = BlockBits(blk);
	blk_bits[chunk / 8] &= ~(1 << (chunk & 7));
}

UINT32 CBlockManager::GetErasedChunks(void) const
{
	UINT32 n;
	n = m_erased_blocks * m_chunks_per_block;
	if (m_alloc_block > 0) n += (m_chunks_per_block - m_alloc_page);
	return n;
}

UINT32 CBlockManager::GetBlockState(UINT32 block) const
{
	const yaffs_block_info * bi = GetBlockInfo(block);
	return bi->block_state;
}

UINT32 CBlockManager::CheckAllocAvailable(UINT32 chunks, UINT32 checkpt_blks) const
{
	int reserved_chunks = (m_reserved_blocks + checkpt_blks) *m_chunks_per_block;
	return (m_free_chunks > (reserved_chunks + chunks));
}

UINT32 CBlockManager::FindOldestBlock(void)
{
	UINT32 oldest = 0;
	UINT32 oldest_seq = 0;

//	m_dev->refresh_skip = m_dev->param.refresh_period;
//	m_dev->refresh_count++;
	yaffs_block_info * bi = m_block_info;
	for (UINT32 bb = m_internal_start_block; bb <= m_internal_end_block; bb++)
	{
		if (bi->block_state == YAFFS_BLOCK_STATE_FULL)
		{
			if (oldest < 1 || bi->seq_number < oldest_seq)
			{
				oldest = bb;
				oldest_seq = bi->seq_number;
			}
		}
		bi++;
	}
	return oldest;
}

UINT32 CBlockManager::FindOldestDirtyBlock(UINT32 & seq)
{
	if (m_oldest_dirty_block == 0)
	{
		// 否则查找
		UINT32 seq;
		UINT32 block_no = 0;
		//struct yaffs_block_info *b;

		seq = m_seq_number + 1;
		yaffs_block_info *bi = m_block_info;
		for (UINT32 ii = m_internal_start_block; ii <= m_internal_end_block; ii++)
		{
			if (bi->block_state == YAFFS_BLOCK_STATE_FULL &&
				(UINT32)bi->pages_in_use < m_chunks_per_block &&
				bi->seq_number < seq)
			{
				seq = bi->seq_number;
				block_no = ii;
			}
			bi++;
		}

		if (block_no)
		{
			m_oldest_dirty_seq = seq;
			m_oldest_dirty_block = block_no;
		}
	}
	seq = m_oldest_dirty_seq;
	return m_oldest_dirty_block;
}

void CBlockManager::SkipRestOfBlock(void)
{
	//struct yaffs_block_info *bi;
	if (m_alloc_block != (UINT)-1)
	{
		yaffs_block_info * bi = GetBlockInfo(m_alloc_block);
		if (bi->block_state == YAFFS_BLOCK_STATE_ALLOCATING)
		{
			bi->block_state = YAFFS_BLOCK_STATE_FULL;
			m_alloc_block = -1;
		}
	}

}

UINT32 CBlockManager::ScanBackwords(yaffs_block_index * block_index, UINT32 & checkpt_blks, ITagsHandler * tagger)
{
	JCASSERT(block_index && tagger);
	LOG_STACK_TRACE_EX(L"yaffs2_scan_backwards starts  intstartblk %d intendblk %d...",
		m_internal_start_block, m_internal_end_block);

	UINT32 n_to_scan = 0;
	//int c;
	//struct yaffs_block_info *bi;
//	int n_blocks = m_internal_end_block - m_internal_start_block + 1;
	//int found_chunks;
	//int alloc_failed = 0;
	//int alt_block_index = 0;

	m_seq_number = YAFFS_LOWEST_SEQUENCE_NUMBER;

//	yaffs_block_index *block_index = new yaffs_block_index[m_blk_num];
//	if (!block_index)
//	{
//		LOG_ERROR(L"[err] could not allocate block index!");
//		return false;
//	}

	checkpt_blks = 0;

//	BYTE * chunk_data = GetTempBuffer();

	/* Scan all the blocks to determine their state */
	yaffs_block_info * bi = m_block_info;
	for (UINT32 blk = m_internal_start_block; blk <= m_internal_end_block; blk++)
	{
		ClearChunkBits(blk);
		bi->pages_in_use = 0;
//		QueryInitBlockState(blk, state, seq_number);
		UINT32 seq_number;
		enum yaffs_block_state state;
		tagger->QueryBlock(blk, state, seq_number);

		bi->block_state = state;
		bi->seq_number = seq_number;

		if (bi->seq_number == YAFFS_SEQUENCE_CHECKPOINT_DATA)	bi->block_state = YAFFS_BLOCK_STATE_CHECKPOINT;
		if (bi->seq_number == YAFFS_SEQUENCE_BAD_BLOCK)			bi->block_state = YAFFS_BLOCK_STATE_DEAD;

		LOG_DEBUG(L"Block scanning block %d state %d seq %d", blk, bi->block_state, seq_number);

		if (bi->block_state == YAFFS_BLOCK_STATE_CHECKPOINT) 		checkpt_blks++;
		else if (bi->block_state == YAFFS_BLOCK_STATE_DEAD)
		{ 
			LOG_NOTICE(L"block %d is bad", blk); 
		}
		else if (bi->block_state == YAFFS_BLOCK_STATE_EMPTY)
		{
			LOG_NOTICE(L"block %d empty", blk);
			m_erased_blocks++;
			m_free_chunks += m_chunks_per_block;
		}
		else if (bi->block_state == YAFFS_BLOCK_STATE_NEEDS_SCAN)
		{	/* Determine the highest sequence number */
			if (seq_number >= YAFFS_LOWEST_SEQUENCE_NUMBER &&
				seq_number < YAFFS_HIGHEST_SEQUENCE_NUMBER)
			{
				block_index[n_to_scan].seq = seq_number;
				block_index[n_to_scan].block = blk;
				n_to_scan++;
				if (seq_number >= m_seq_number)	m_seq_number = seq_number;
			}
			else
			{	/* TODO: Nasty sequence number! */
				LOG_NOTICE(L"Block scanning block %d has bad sequence number %d", blk, seq_number);
			}
		}
		bi++;
	}

	LOG_NOTICE(L"%d blocks to be sorted...", n_to_scan);
	return n_to_scan;
}

bool CBlockManager::CheckChunkBit(UINT32 blk, UINT32 chunk) const
{
	const BYTE * blk_bits = BlockBits(blk);
	VerifyChunkBitId(blk, chunk);
	return (blk_bits[chunk / 8] & (1 << (chunk & 7))) ? 1 : 0;
}

void CBlockManager::VerifyChunkBitId(UINT32 blk, UINT32 chunk) const
{
	if (blk < m_internal_start_block || blk >m_internal_end_block)
		THROW_ERROR(ERR_APP, L"invalid block id=%d, expected:(%d~%d)", blk, m_internal_start_block, m_internal_end_block);
	if (INVALID_BLOCKID(chunk) || chunk >= m_chunks_per_block)
		THROW_ERROR(ERR_APP, L"invalid chunk id=%d, expected:(%d~%d)", blk, 0, m_chunks_per_block);
}

bool CBlockManager::ChunkDel(UINT32 block, UINT32 page)
{
	//UINT32 block;
	//UINT32 page;

	//	struct yaffs_block_info *bi;

	//if (chunk_id <= 0) return;
	// 这个参数好像没有被使用
//	m_dev->n_deletions++;
	//block = chunk_id / m_chunks_per_block;
	//page = chunk_id % m_chunks_per_block;

	if (!CheckChunkBit(block, page))
	{
		LOG_ERROR(L"[err] Deleteing invalid chunk (%d:%d)", block, page);
		JCASSERT(0);
	}
	yaffs_block_info * bi = GetBlockInfo(block);

	UpdateOldestDirtySeq(block, bi);

	LOG_NOTICE(L"delete of chunk (%d:%d)", block, page);

	// 这个参数好像没有被使用
	//m_dev->n_unmarked_deletions++;

	/* Pull out of the management area. If the whole block became dirty, this will kick off an erasure. */
	if (bi->block_state == YAFFS_BLOCK_STATE_ALLOCATING ||
		bi->block_state == YAFFS_BLOCK_STATE_FULL ||
		bi->block_state == YAFFS_BLOCK_STATE_NEEDS_SCAN ||
		bi->block_state == YAFFS_BLOCK_STATE_COLLECTING)
	{
		m_free_chunks++;
		ClearChunkBit(block, page);
		bi->pages_in_use--;

		if (bi->pages_in_use == 0 && !bi->has_shrink_hdr &&
			bi->block_state != YAFFS_BLOCK_STATE_ALLOCATING &&
			bi->block_state != YAFFS_BLOCK_STATE_NEEDS_SCAN)
		{
//			BlockBecameDirty(block);
			return true;
		}
	}
	return false;
}

void CBlockManager::ClearOldestDirtySeq(UINT32 block)
{
	yaffs_block_info * bi = GetBlockInfo(block);
	if (!bi || bi->seq_number == m_oldest_dirty_seq)
	{
		m_oldest_dirty_seq = 0;
		m_oldest_dirty_block = 0;
	}

}

void CBlockManager::ClaimPage(UINT32 block, UINT32 page)
{
	if (CheckChunkBit(block, page))
	{
		yaffs_block_info * bi = GetBlockInfo(block);
		ClearChunkBit(block, page);
		bi->pages_in_use--;
		m_free_chunks++;
	}

}

void CBlockManager::UpdateOldestDirtySeq(UINT32 block_no, yaffs_block_info * bi)
{
	if (m_oldest_dirty_seq)
	{
		if (m_oldest_dirty_seq > bi->seq_number)
		{
			m_oldest_dirty_seq = bi->seq_number;
			m_oldest_dirty_block = block_no;
		}
	}

}

bool CBlockManager::StillSomeChunks(UINT32 blk)
{
	BYTE *blk_bits = BlockBits(blk);
	for (UINT32 ii = 0; ii < m_chunk_bit_stride; ii++)
	{
		if (*blk_bits)	return true;
		blk_bits++;
	}
	return false;
}


//bool CBlockManager::ScanChunkInBlock(UINT32 blk, UINT32 last_chunk, bool summary, CYafFs * fs)
//{
//	yaffs_block_info * bi = GetBlockInfo(blk);
//	if (bi->block_state != YAFFS_BLOCK_STATE_NEEDS_SCAN &&
//		bi->block_state != YAFFS_BLOCK_STATE_ALLOCATING) return true;
//
//	/* For each chunk in each block that needs scanning.... */
//	// 从最后一个chunk开始scan整个block
//	bool alloc_failed = false;
//	UINT32 chunk = last_chunk; 
//	BYTE * chunk_data = fs->GetTempBuffer();
//	while (1)
//	{	/* Scan backwards...		 * Read the tags and decide what to do	 */
//		bool br = ScanChunk(bi, blk, chunk, &found_chunks, chunk_data, summary);
//		if (!br)
//		{
//			alloc_failed = true;
//			break;
//		}
//		if (chunk == 0) break;
//		chunk--;
//	}
//
//	if (bi->block_state == YAFFS_BLOCK_STATE_NEEDS_SCAN)
//	{	/* If we got this far while scanning, then the block is fully allocated. */
//		bi->block_state = YAFFS_BLOCK_STATE_FULL;
//	}
//
//	/* Now let's see if it was dirty */
//	if (bi->pages_in_use == 0 && !bi->has_shrink_hdr &&	bi->block_state == YAFFS_BLOCK_STATE_FULL)
//	{
//		fs->BlockBecameDirty(blk);
//	}
//	fs->ReleaseTempBuffer(chunk_data);
//
//	return false;
//}

bool CBlockManager::ScanChunkUnused(
	yaffs_block_info * bi, UINT32 blk, UINT32 chunk_in_block, int * found_chunks)
{
	/* Let's have a good look at this chunk... */
	/* An unassigned chunk in the block. If there are used chunks after this one,
	then it is a chunk that was skipped due to failing the erased check.
	Just skip it so that it can be deleted. But, more typically, We get here when
	this is an unallocated chunk and his means that either the block is empty
	or this is the one being allocated from */

	if (*found_chunks)
	{	/* This is a chunk that was skipped due to failing the erased check */
	}
	else if (chunk_in_block == 0)
	{	/* We're looking at the first chunk in the block so the block is unused */
		bi->block_state = YAFFS_BLOCK_STATE_EMPTY;
		m_erased_blocks++;
	}
	else
	{
		if (bi->block_state == YAFFS_BLOCK_STATE_NEEDS_SCAN ||
			bi->block_state == YAFFS_BLOCK_STATE_ALLOCATING)
		{
			if (m_seq_number == bi->seq_number)
			{	/* Allocating from this block*/
				LOG_NOTICE(L" Allocating from %d %d", blk, chunk_in_block);
				bi->block_state = YAFFS_BLOCK_STATE_ALLOCATING;
				m_alloc_block = blk;
				m_alloc_page = chunk_in_block;
				m_alloc_block_finder = blk;
			}
			else
			{	/* This is a partially written block that is not the current allocation block.	 */
				LOG_WARNING(L"Partially written block %d detected. gc will fix this.", blk);
			}
		}
	}
	m_free_chunks++;
	return true;
}

//void CBlockManager::BlockBecameDirty(int block_no)
//{
//	yaffs_block_info *bi = GetBlockInfo(block_no);
//	int erased_ok = 0;
//	//u32 i;
//
//	/* If the block is still healthy erase it and mark as clean.
//	 * If the block has had a data failure, then retire it. */
//	LOG_NOTICE(L"[GC] yaffs_block_became_dirty block %d state %d %s",
//		block_no, bi->block_state, (bi->needs_retiring) ? L"needs retiring" : L"");
//
//	ClearOldestDirtySeq(bi);
//	bi->block_state = YAFFS_BLOCK_STATE_DIRTY;
//	/* If this is the block being garbage collected then stop gc'ing */
//	if (block_no == (int)m_dev->gc_block)	m_dev->gc_block = 0;
//	/* If this block is currently the best candidate for gc then drop as a candidate */
//	if (block_no == (int)m_dev->gc_dirtiest)
//	{
//		m_dev->gc_dirtiest = 0;
//		m_dev->gc_pages_in_use = 0;
//	}
//
//	if (!bi->needs_retiring)
//	{
//		CheckptInvalidate();
//		erased_ok = EraseBlock(block_no);
//		if (!erased_ok)
//		{
//			m_dev->n_erase_failures++;
//			LOG_NOTICE(L"[fail] Erasure failed %d", block_no);
//		}
//	}
//
//	/* Verify erasure if needed */
//	if (erased_ok && !SkipVerification())
//	{
//		for (i = 0; i < m_dev->param.chunks_per_block; i++)
//		{
//			if (!CheckChunkErased(block_no * m_dev->param.chunks_per_block + i))
//			{
//				LOG_ERROR(L">>Block %d erasure supposedly OK, but chunk %d not erased", block_no, i);
//			}
//		}
//	}
//
//	if (!erased_ok)
//	{	/* We lost a block of free space */
//		m_dev->n_free_chunks -= m_dev->param.chunks_per_block;
//		RetireBlock(block_no);
//		LOG_ERROR(L"**>> Block %d retired", block_no);
//		return;
//	}
//
//	/* Clean it up... */
//	m_block_manager->CleanBlock(block_no);
//	//bi->block_state = YAFFS_BLOCK_STATE_EMPTY;
//	//bi->seq_number = 0;
//	//m_dev->n_erased_blocks++;
//	//bi->pages_in_use = 0;
//	//bi->has_shrink_hdr = 0;
//	//bi->skip_erased_check = 1;	/* Clean, so no need to check */
//	//bi->gc_prioritise = 0;
//	//bi->has_summary = 0;
//
//	//m_block_manager->ClearChunkBit(block_no, 0);
//	LOG_NOTICE(L"[GC] Erased block %d", block_no);
//
//}

UINT32 CBlockManager::FindBlockBySeq(UINT32 start, UINT32 seq)
{
	//for (UINT32 ii = start; ii <= m_internal_end_block; ii++)
	//{
	//	//			int chunk = ii * m_chunks_per_block;
	//	enum yaffs_block_state state;
	//	u32 seq;
	//	//m_tagger->ReadChunkTags(chunk, NULL, &tags);
	//	//LOG_DEBUG_(0, L"find next checkpt block: search: block %d oid %d seq %d eccr %d",
	//	//	ii, tags.obj_id, tags.seq_number, tags.ecc_result);
	//	//if (tags.seq_number != YAFFS_SEQUENCE_CHECKPOINT_DATA)		continue;
	//	m_tagger->QueryBlock(ii, state, seq);
	//	//排除bad block
	//	if (state == YAFFS_BLOCK_STATE_DEAD || seq != YAFFS_SEQUENCE_CHECKPOINT_DATA)		continue;

	//	/* Right kind of block */
	//	m_next_block = tags.obj_id;
	//	m_cur_block = ii;
	//	m_block_list[m_blocks_in_checkpt] = ii;
	//	m_blocks_in_checkpt++;
	//	LOG_DEBUG_(1, L"found checkpt block %d", ii);
	//	return;
	//}
	return -1;
}

UINT32 CBlockManager::FindEmptyBlock(UINT32 start)
{
	for (UINT32 ii = start; ii <= m_internal_end_block; ii++)
	{
		yaffs_block_info *bi = GetBlockInfo(ii);
		if (bi->block_state == YAFFS_BLOCK_STATE_EMPTY)
		{
			return ii;
		}
	}
	return -1;
}

void CBlockManager::BlockUsed(UINT32 blk_num, UINT32 chunks)
{
	m_free_chunks -= blk_num * m_chunks_per_block;
	m_erased_blocks -= blk_num;
	m_free_chunks -= chunks;
}

void CBlockManager::BlockReleased(UINT32 blk_num, UINT32 chunks)
{
	m_free_chunks += blk_num * m_chunks_per_block;
	m_erased_blocks += blk_num;
	m_free_chunks += chunks;
}

void CBlockManager::ReleaseBlock(UINT32 blk)
{
	struct yaffs_block_info *bi = GetBlockInfo(blk);

	if (bi->block_state != YAFFS_BLOCK_STATE_EMPTY && bi->block_state != YAFFS_BLOCK_STATE_DEAD)
	{
		LOG_NOTICE(L"erasing checkpt block %d", blk);
		//m_dev->n_erasures++;
		bool result = m_driver->Erase(blk);
		//			result = m_dev->drv.drv_erase_fn(dev, offset_i);
		if (result)
		{
			bi->block_state = YAFFS_BLOCK_STATE_EMPTY;
			m_erased_blocks++;
			m_free_chunks += m_chunks_per_block;
		}
		else
		{
			m_driver->MarkBad(blk);
			bi->block_state = YAFFS_BLOCK_STATE_DEAD;
		}
	}
}

bool CBlockManager::ReadFromCheckpt(CYaffsCheckPoint & checkpt, int & n_bg_deletions)
{
	bool ok;

	struct yaffs_checkpt_dev cp;
	ok = checkpt.TypedRead(&cp);
	if (!ok)	return false;
	if (cp.struct_type != sizeof(cp)) 	return 0;

	m_erased_blocks = cp.n_erased_blocks;
	m_alloc_block = cp.alloc_block;
	m_alloc_page = cp.alloc_page;
	m_free_chunks = cp.n_free_chunks;
	n_bg_deletions = cp.n_bg_deletions;
	m_seq_number = cp.seq_number;



	size_t n_bytes = m_blk_num * sizeof(struct yaffs_block_info);

	ok = checkpt.Read(m_block_info, n_bytes) == n_bytes;
	if (!ok)	return false;
	n_bytes = m_blk_num * m_chunk_bit_stride;

	ok = checkpt.Read(m_chunk_bits, n_bytes) == n_bytes;
	return ok;
}

bool CBlockManager::WriteToCheckpt(CYaffsCheckPoint & checkpt, int n_bg_deletions)
{
	/* Write block info. */
	//if (!m_dev->swap_endian) {
	struct yaffs_checkpt_dev cp;

	cp.struct_type = sizeof(cp);

	cp.n_erased_blocks = m_erased_blocks;
	cp.alloc_block = m_alloc_block;
	cp.alloc_page = m_alloc_page;
	cp.n_free_chunks = m_free_chunks;

	cp.n_deleted_files = 0;
	cp.n_unlinked_files = 0;
	cp.n_bg_deletions = n_bg_deletions;
	cp.seq_number = m_seq_number;

	bool ok = checkpt.TypedWrite(&cp);
	if (!ok) return false;


	UINT32 n_bytes = m_blk_num * sizeof(struct yaffs_block_info);
	ok = (checkpt.Write(m_block_info, n_bytes) ==  n_bytes);
	if (!ok) return false;

	/* Write chunk bits. Chunk bits are in bytes so no endian conversion is needed. */
	n_bytes = m_blk_num * m_chunk_bit_stride;
	ok = checkpt.Write(m_chunk_bits, n_bytes) == n_bytes;
	return ok;
}

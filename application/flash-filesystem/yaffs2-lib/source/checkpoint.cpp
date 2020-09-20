#include "stdafx.h"

#include "../include/checkpoint.h"
#include "../include/yaf_fs.h"
#include "../include/yaffs_file.h"
#include "../include/block_manager.h"

LOCAL_LOGGER_ENABLE(L"checkpoint", LOGGER_LEVEL_NOTICE);


CYaffsCheckPoint::CYaffsCheckPoint(ITagsHandler * tagger, CBlockManager * block_manager,
	UINT32 bytes_per_chunk, UINT32 data_bytes_per_chunk, UINT32 chunk_per_block)
	:m_buffer(NULL), m_block_list(NULL), m_block_manager(NULL)
{
	m_buffer = new BYTE[bytes_per_chunk];
	m_tagger = tagger; JCASSERT(m_tagger);
	m_tagger->AddRef();
	JCASSERT(block_manager);
	m_block_manager = block_manager;

	m_bytes_per_chunk = bytes_per_chunk;
	m_data_bytes_per_chunk = data_bytes_per_chunk;
//	m_chunks_per_block = m_block_manager->GetChunksPerBlock();
	m_chunks_per_block = chunk_per_block;

	m_blocks_in_checkpt = 0;
	m_is_checkpointed = false;
}

CYaffsCheckPoint::~CYaffsCheckPoint(void)
{
	delete[] m_buffer;
	delete[] m_block_list;
	RELEASE(m_tagger);
}

bool CYaffsCheckPoint::Open(bool writing)
{
	JCASSERT(m_block_manager);
	m_max_blocks = m_block_manager->GetCheckptMaxBlocks();
	m_blocks_in_checkpt = 0;
	m_open_write = writing;
	m_is_checkpointed = false;
	if (writing && (m_block_manager->GetAvailBlock() <=0)) return false;

	if (!m_buffer)	m_buffer = new BYTE[m_bytes_per_chunk];
	if (!m_buffer)		THROW_ERROR(ERR_APP, L"failed on allocate checkpoint buffer");

	m_page_seq = 0;
	m_byte_count = 0;
	m_sum = 0;
	m_xor = 0;
	m_cur_block = INVALID_BLOCK;
	m_cur_chunk = INVALID_BLOCK;
	m_next_block = m_block_manager->GetFirstBlock();

	if (writing)
	{
		memset(m_buffer, 0, m_data_bytes_per_chunk);
		InitChunkHeader();
		return Erase();
	}

	/* Opening for a read */
	/* Set to a value that will kick off a read */
	m_byte_offs = m_data_bytes_per_chunk;
	/* A checkpoint block list of 1 checkpoint block per 16 block is
	 * (hopefully) going to be way more than we need */

	if (!m_block_list)		m_block_list = new UINT32[m_max_blocks];
	if (!m_block_list)		THROW_ERROR(ERR_APP, L"failed on allocate checkpoint block list");
	for (UINT32 ii = 0; ii < m_max_blocks; ii++)		m_block_list[ii] = -1;

	return true;
}

bool CYaffsCheckPoint::Close(void)
{
	if (m_open_write)
	{
		if (m_byte_offs != sizeof(sizeof(struct yaffs_checkpt_chunk_hdr)))	FlushBuffer();
	}
	else if (m_block_list)
	{
		for (UINT32 ii = 0; ii < m_blocks_in_checkpt && m_block_list[ii] >= 0; ii++)
		{
			int blk = m_block_list[ii];
			//if ((int)m_dev->internal_start_block <= blk && blk <= (int)m_dev->internal_end_block)
			//	bi = GetBlockInfo(blk);
			struct yaffs_block_info *bi = m_block_manager->GetBlockInfo(blk);
			if (bi && bi->block_state == YAFFS_BLOCK_STATE_EMPTY)
				bi->block_state = YAFFS_BLOCK_STATE_CHECKPOINT;
		}
	}

	m_block_manager->BlockUsed(m_blocks_in_checkpt);
	//m_dev->n_free_chunks -= m_dev->blocks_in_checkpt * m_dev->param.chunks_per_block;
	//m_dev->n_erased_blocks -= m_dev->blocks_in_checkpt;

	LOG_NOTICE(L"checkpoint byte count %d", m_byte_count);

	if (m_buffer)
	{
		m_is_checkpointed = true;
		return true;
	}
	else
	{
		m_is_checkpointed = false;
		return false;
	}
}

bool CYaffsCheckPoint::ReadValidityMarker(int head)
{
	struct yaffs_checkpt_validity cp;
	bool ok = TypedRead(&cp);
//	ok = Read(&cp, sizeof(cp)) == sizeof(cp);
	if (!ok) return false;
	ok = (cp.struct_type == sizeof(cp)) &&
			(cp.magic == YAFFS_MAGIC) &&
			(cp.version == YAFFS_CHECKPOINT_VERSION) &&
			(cp.head == ((head) ? 1 : 0));
	return ok;
}

bool CYaffsCheckPoint::WriteValidityMarker(int header)
{
	struct yaffs_checkpt_validity cp;

	memset(&cp, 0, sizeof(cp));

	cp.struct_type = sizeof(cp);
	cp.magic = YAFFS_MAGIC;
	cp.version = YAFFS_CHECKPOINT_VERSION;
	cp.head = (header) ? 1 : 0;

	//yaffs2_do_endian_validity_marker(dev, &cp);

	return TypedWrite(&cp);
//	, sizeof(cp)) == sizeof(cp));
}

bool CYaffsCheckPoint::ReadSum(void)
{
	UINT32 checkpt_sum1;
	bool ok;
	UINT32 checkpt_sum0 = (m_sum << 8) | (m_xor & 0xff);
	ok = TypedRead(&checkpt_sum1);
	if (!ok)	return false;
	if (checkpt_sum0 != checkpt_sum1)
	{
		LOG_ERROR(L"[err] Wrong checksum %d, expected %d", checkpt_sum1, checkpt_sum0);
		return false;
	}
	return true;
}

bool CYaffsCheckPoint::WriteSum(void)
{
	bool ok;
	UINT32 checkpt_sum = (m_sum << 8) | (m_xor & 0xff);
	ok = TypedWrite(&checkpt_sum);
	return ok;
}

UINT32 CYaffsCheckPoint::CalcCheckptBlockRequired(UINT32 obj_num, UINT32 tnode_size)
{
	UINT32 n_bytes = 0;
	UINT32 block_num = m_block_manager->GetBlockNumber();
	n_bytes += sizeof(struct yaffs_checkpt_validity);
	n_bytes += sizeof(struct yaffs_checkpt_dev);
	n_bytes += block_num * sizeof(struct yaffs_block_info);
	n_bytes += block_num * m_block_manager->GetBitStride();
	n_bytes += (sizeof(struct yaffs_checkpt_obj) + sizeof(u32)) * obj_num;
	n_bytes += tnode_size;
	n_bytes += sizeof(struct yaffs_checkpt_validity);
	n_bytes += sizeof(u32);	/* checksum */

		/* Round up and add 2 blocks to allow for some bad blocks, so add 3 */
	UINT32 n_blocks = (n_bytes / (m_data_bytes_per_chunk * m_chunks_per_block)) + 3;
	if (n_blocks > m_blocks_in_checkpt) return n_blocks - m_blocks_in_checkpt;
	else return 0;
}

bool CYaffsCheckPoint::Erase(void)
{
	//JCASSERT(driver);
	UINT32 first = m_block_manager->GetFirstBlock();
	UINT32 last = m_block_manager->GetLastBlock();
	LOG_NOTICE(L"checking blocks %d to %d", first, last);
	for (UINT32 ii = first; ii <= last; ii++)
	{
//		struct yaffs_block_info *bi = GetBlockInfo(ii);
		//int offset_i = ii;
		//bool result;
		UINT32 block_state = m_block_manager->GetBlockState(ii);
		if (block_state == YAFFS_BLOCK_STATE_CHECKPOINT)	m_block_manager->ReleaseBlock(ii);
		//{
		//	LOG_NOTICE(L"erasing checkpt block %d", ii);
		//	m_dev->n_erasures++;
		//	result = driver->Erase(offset_i);
		//	//			result = m_dev->drv.drv_erase_fn(dev, offset_i);
		//	if (result)
		//	{
		//		bi->block_state = YAFFS_BLOCK_STATE_EMPTY;
		//		m_block_manager->BlockReleased(1, 0);
		//		//m_dev->n_erased_blocks++;
		//		//m_dev->n_free_chunks += m_dev->param.chunks_per_block;
		//	}
		//	else
		//	{
		//		m_driver->MarkBad(offset_i);
		//		bi->block_state = YAFFS_BLOCK_STATE_DEAD;
		//	}
		//}
	}

	m_blocks_in_checkpt = 0;
	return true;
}

void CYaffsCheckPoint::Invalidate(void)
{
	if (m_is_checkpointed || m_blocks_in_checkpt > 0)
	{
		m_is_checkpointed = false;
		Erase();
	}
}


//bool CYaffsCheckPoint::ReadDev(UINT32 blk_num, yaffs_block_info * blk_info, 
//	UINT32 * chunk_bits, UINT32 chunk_bit_stride)
//{
//	JCASSERT(blk_info && chunk_bits);
//	struct yaffs_checkpt_dev cp;
//	bool ok = TypedRead(&cp);
////	ok = (Read(&cp, sizeof(cp)) == sizeof(cp));
//	if (!ok)	return false;
//
//	if (cp.struct_type != sizeof(cp)) 	return 0;
//	CheckptDevToDev(&cp);
//	size_t n_bytes = blk_num * sizeof(struct yaffs_block_info);
//
//	ok = Read(blk_info, n_bytes) == n_bytes;
//	if (!ok)	return false;
//	n_bytes = blk_num * chunk_bit_stride;
//
//	ok = Read(chunk_bits, n_bytes) == n_bytes;
//	return ok;
//}

//bool CYaffsCheckPoint::ReadTnodes(CYaffsObject * obj)
//{
//	UINT32 base_chunk;
//	bool ok = true;
//
//	CYaffsFile * ff = dynamic_cast<CYaffsFile*>(obj);
//	JCASSERT(ff);	//需要load tnode的一定是file
//	struct yaffs_tnode *tn;
//	int nread = 0;
//	
//	ok = TypedRead(&base_chunk);
////	ok = (Read(&base_chunk, sizeof(base_chunk)) == sizeof(base_chunk));
//	//	yaffs_do_endian_u32(dev, &base_chunk);
//
//	while (ok && (~base_chunk))
//	{
//		nread++;
//		/* Read level 0 tnode */
//		tn = GetTnode();
//		if (tn)
//		{
//			ok = (CheckptRead(tn, m_dev->tnode_size) == m_dev->tnode_size);
//		}
//		else	ok = false;
//
//		if (tn && ok)
//		{
//			if (ff->AddFindTnode0(base_chunk, tn)) ok = true;
//			else ok = false;
//		}
//		if (ok)
//		{
//			ok = (CheckptRead(&base_chunk, sizeof(base_chunk)) == sizeof(base_chunk));
//		}
//	}
//	LOG_NOTICE(L"Checkpoint read tnodes %d records, last %d. ok %d", nread, base_chunk, ok);
//	return ok;
//}

size_t CYaffsCheckPoint::Read(void * data, size_t n_bytes)
{
	size_t ii = 0;
	struct yaffs_ext_tags tags;
	int chunk;
	int offset_chunk;
	BYTE *data_bytes = (BYTE *)data;

	if (!m_buffer) 		THROW_ERROR(ERR_APP, L"checkpoint buffer is invailable.")
	if (m_open_write)		return -1;

	while (ii < n_bytes)
	{
		if (m_byte_offs < 0 || m_byte_offs >= (int)m_data_bytes_per_chunk)
		{
			if ( INVALID_BLOCKID(m_cur_block) )
			{
				FindBlock();
				m_cur_chunk = 0;
			}
			/* Bail out if we can't find a checpoint block */
			if (INVALID_BLOCKID(m_cur_block))			break;
			chunk = m_cur_block * m_chunks_per_block + m_cur_chunk;

			offset_chunk = chunk;
			// 作为统计数据，暂时不使用
//			m_dev->n_page_reads++;

			/* Read in the next chunk */
			m_tagger->ReadChunkTags(offset_chunk, m_buffer, &tags);

			/* Bail out if the chunk is corrupted. */
			if (tags.chunk_id != (u32)(m_page_seq + 1) ||
				tags.ecc_result > INandDriver::ECC_RESULT_FIXED ||
				tags.seq_number != YAFFS_SEQUENCE_CHECKPOINT_DATA)
				break;

			/* Bail out if it is not a checkpoint chunk. */
			bool br = CheckChunkHeader(m_buffer);
			m_byte_offs = sizeof(yaffs_checkpt_chunk_hdr);
			if (!br) break;

			m_page_seq++;
			m_cur_chunk++;

			if (m_cur_chunk >= m_chunks_per_block) m_cur_block = INVALID_BLOCK;
		}

		*data_bytes = m_buffer[m_byte_offs];
		m_sum += *data_bytes;
		m_xor ^= *data_bytes;
		m_byte_offs++;
		ii++;
		data_bytes++;
		m_byte_count++;
	}

	return ii; /* Number of bytes read */
}

size_t CYaffsCheckPoint::Write(const void * data, size_t n_bytes)
{
	//LOG_STACK_TRACE();
	bool ok = 1;
	u8 *data_bytes = (u8 *)data;

	if (!m_buffer) return 0;

	if (!m_open_write)
		return -1;

	size_t ii = 0;
	while (ii < n_bytes && ok)
	{
		m_buffer[m_byte_offs] = *data_bytes;
		m_sum += *data_bytes;
		m_xor ^= *data_bytes;

		m_byte_offs++;
		ii++;
		data_bytes++;
		m_byte_count++;

		if (m_byte_offs < 0 || m_byte_offs >=m_bytes_per_chunk) ok = FlushBuffer();
	}
	return ii;
}

//bool CYaffsCheckPoint::SpaceOk(void)
//{
//	return false;
//}

void CYaffsCheckPoint::InitChunkHeader(void)
{
	struct yaffs_checkpt_chunk_hdr hdr;
	JCASSERT(m_buffer);

	hdr.version = YAFFS_CHECKPOINT_VERSION;
	hdr.seq = m_page_seq;
	hdr.sum = m_sum;
	hdr.xor_ = m_xor;

	memcpy(m_buffer, &hdr, sizeof(hdr));
	m_byte_offs = sizeof(hdr);
}

bool CYaffsCheckPoint::FlushBuffer(void)
{
	JCASSERT(m_block_manager);
	//LOG_STACK_TRACE();
	int chunk;
	int offset_chunk;
	struct yaffs_ext_tags tags;

	if (INVALID_BLOCKID( m_cur_block))
	{
		FindErasedBlock();
		m_cur_chunk = 0;
		LOG_DEBUG(L"found checkpt block %d", m_cur_block);
	}

	if (INVALID_BLOCKID( m_cur_block) )		return false;

	tags.obj_id = m_next_block;	/* Hint to next place to look */
	tags.chunk_id = m_page_seq + 1;
	tags.seq_number = YAFFS_SEQUENCE_CHECKPOINT_DATA;
	tags.n_bytes = m_data_bytes_per_chunk;
	if (m_cur_chunk == 0)
	{	/* First chunk we write for the block? Set block state to checkpoint */
		yaffs_block_info *bi = m_block_manager->GetBlockInfo(m_cur_block);
		bi->block_state = YAFFS_BLOCK_STATE_CHECKPOINT;
		m_blocks_in_checkpt++;
	}

	chunk = m_cur_block * m_chunks_per_block + m_cur_chunk;

	LOG_NOTICE(L"checkpoint wite buffer nand %d(%d:%d) objid %d chId %d",
		chunk, m_cur_block, m_cur_chunk,
		tags.obj_id, tags.chunk_id);

	offset_chunk = chunk;
	// 作为统计数据，暂时不使用
	//m_dev->n_page_writes++;

	m_tagger->WriteChunkTags(offset_chunk, m_buffer, &tags);
	m_page_seq++;
	m_cur_chunk++;
	if (m_cur_chunk >= m_chunks_per_block)
	{
		m_cur_chunk = 0;
		m_cur_block = INVALID_BLOCK;
	}
	memset(m_buffer, 0, m_data_bytes_per_chunk);
	InitChunkHeader();
	return true;
}

void CYaffsCheckPoint::FindErasedBlock(void)
{
//	int blocks_avail = m_dev->n_erased_blocks - m_dev->param.n_reserved_blocks;
//	UINT32 blocks_avail = m_block_manager->GetAvailBlockNum();
//
////	LOG_NOTICE(L"allocating checkpt block: erased %d reserved %d avail %d next %d ",
////		m_dev->n_erased_blocks, m_dev->param.n_reserved_blocks,
////		blocks_avail, m_dev->checkpt_next_block);
//
//	if (m_next_block >= 0 &&
////		m_next_block <= (int)m_dev->internal_end_block &&
//		blocks_avail > 0)
//	{
		m_cur_block = m_block_manager->FindEmptyBlock(m_next_block);
		if ( VALID_BLOCKID( m_cur_block))
		{
			m_next_block = m_cur_block + 1;
		}
		else m_next_block = -1;

	//	for (UINT32 ii = m_dev->checkpt_next_block; ii <= m_dev->internal_end_block; ii++)
	//	{
	//		yaffs_block_info *bi = m_block_manager->GetBlockInfo(ii);
	//		if (bi->block_state == YAFFS_BLOCK_STATE_EMPTY)
	//		{
	//			m_dev->checkpt_next_block = ii + 1;
	//			m_dev->checkpt_cur_block = ii;
	//			LOG_NOTICE(L"allocating checkpt block %d", ii);
	//			return;
	//		}
	//	}
	//}
	//LOG_TRACE(L"out of checkpt blocks");

	//m_dev->checkpt_next_block = -1;
	//m_dev->checkpt_cur_block = -1;

}

void CYaffsCheckPoint::FindBlock(void)
{
	struct yaffs_ext_tags tags;

	LOG_NOTICE(L"find next checkpt block: start:  blocks %d next %d",
		m_blocks_in_checkpt, m_next_block);

	UINT32 last_block = m_block_manager->GetLastBlock();

	if (m_blocks_in_checkpt < m_max_blocks)
	{
//		m_block_manager->FindBlockBySeq(m_next_block, YAFFS_SEQUENCE_CHECKPOINT_DATA);
		for (UINT32 ii = m_next_block; ii <= last_block; ii++)
		{
			UINT32 chunk = ii * m_chunks_per_block;
			enum yaffs_block_state state;
			u32 seq;
			m_tagger->ReadChunkTags(chunk, NULL, &tags);
			//LOG_DEBUG_(0, L"find next checkpt block: search: block %d oid %d seq %d eccr %d",
			//	ii, tags.obj_id, tags.seq_number, tags.ecc_result);
			//if (tags.seq_number != YAFFS_SEQUENCE_CHECKPOINT_DATA)		continue;
			m_tagger->QueryBlock(ii, state, seq);
			//排除bad block
			if (state == YAFFS_BLOCK_STATE_DEAD || seq != YAFFS_SEQUENCE_CHECKPOINT_DATA)		continue;

			/* Right kind of block */
			m_next_block = tags.obj_id;
			m_cur_block = ii;
			m_block_list[m_blocks_in_checkpt] = ii;
			m_blocks_in_checkpt++;
			LOG_DEBUG_(1, L"found checkpt block %d", ii);
			return;
		}
	}
	LOG_NOTICE(L"found no more checkpt blocks");
	m_next_block = INVALID_BLOCK;
	m_cur_block = INVALID_BLOCK;

}

bool CYaffsCheckPoint::CheckChunkHeader(BYTE * buffer)
{
	struct yaffs_checkpt_chunk_hdr hdr;
	JCASSERT(buffer);
	memcpy(&hdr, buffer, sizeof(hdr));
//	m_byte_offs = sizeof(hdr);

	return hdr.version == YAFFS_CHECKPOINT_VERSION &&
		hdr.seq == m_page_seq &&
		hdr.sum == m_sum &&
		hdr.xor_ == m_xor;
}

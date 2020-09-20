#include "stdafx.h"
#include "..\include\tags_handler.h"
#include "..\include\yaf_fs.h"


CTagsHandler::CTagsHandler()
{
}


CTagsHandler::~CTagsHandler()
{
}

//<MIGRATE> yaffs_tagscompat.c : int yaffs_tags_compat_wr();
int CTagsHandler::WriteChunkTags(int nand_chunk, const BYTE * data, const yaffs_ext_tags * ext_tags)
{
	JCASSERT(m_fs);
	struct yaffs_spare spare;
	struct yaffs_tags tags;

//	yaffs_spare_init(&spare);
	memset(&spare, 0xff, sizeof(yaffs_spare));

	//if (ext_tags->is_deleted)		spare.page_status = 0;
	//else 
	//{
		tags.obj_id = ext_tags->obj_id;
		tags.chunk_id = ext_tags->chunk_id;

		tags.n_bytes_lsb = ext_tags->n_bytes & (1024 - 1);

		if (m_fs->GetBytePerChunk() >= 1024)	tags.n_bytes_msb = (ext_tags->n_bytes >> 10) & 3;
		else			tags.n_bytes_msb = 3;

		tags.serial_number = ext_tags->serial_number;

		//<TODO> Ignor ECC here
		//if (!dev->param.use_nand_ecc && data) 
		//{
		//	yaffs_ecc_calc(data, spare.ecc1);
		//	yaffs_ecc_calc(&data[256], spare.ecc2);
		//}

//		yaffs_load_tags_to_spare(dev, &spare, &tags);
		LoadTagsToSpare(&spare, &tags);
	//}
	return m_fs->WriteChunkNand(nand_chunk, data, &spare);
		//yaffs_wr_nand(dev, nand_chunk, data, &spare);
}

//<MIGRATE> yaffs_tagscompat.c : static int yaffs_tags_compat_rd()
bool CTagsHandler::ReadChunkTags(int nand_chunk, BYTE * data, yaffs_ext_tags * ext_tags)
{
	JCASSERT(m_fs);
	struct yaffs_spare spare;
	struct yaffs_tags tags;
	INandDriver::ECC_RESULT ecc_result = INandDriver::ECC_RESULT_UNKNOWN;
	static struct yaffs_spare spare_ff;
	static int init;
	int deleted;

	if (!init) 
	{
		memset(&spare_ff, 0xff, sizeof(spare_ff));
		init = 1;
	}

	if (!m_fs->ReadChunkNand(nand_chunk, data, &spare, ecc_result, 1)) return false;
	/* ext_tags may be NULL */
	if (!ext_tags)		return true;
	// 计算bit 1的数量
	deleted = (yaffs_hweight8(spare.page_status) < 7) ? 1 : 0;

	//ext_tags->is_deleted = deleted;
	ext_tags->ecc_result = ecc_result;
	ext_tags->block_bad = 0;	/* We're reading it */
	/* therefore it is not a bad block */
	ext_tags->chunk_used = memcmp(&spare_ff, &spare, sizeof(spare_ff)) ? 1 : 0;

	if (ext_tags->chunk_used) 
	{
//		yaffs_get_tags_from_spare(dev, &spare, &tags);
		GetTagsFromSpare(&spare, &tags);

		ext_tags->obj_id = tags.obj_id;
		ext_tags->chunk_id = tags.chunk_id;
		ext_tags->n_bytes = tags.n_bytes_lsb;

//		if (dev->data_bytes_per_chunk >= 1024)
		if (m_fs->GetBytePerChunk() >= 1024)
			ext_tags->n_bytes |= (((unsigned)tags.n_bytes_msb) << 10);
		ext_tags->serial_number = tags.serial_number;
	}
	return true;
}

//<MIGRATE> yaffs_tagcompat.c : int yaffs_tags_compat_mark_bad()
bool CTagsHandler::MarkBad(int block_no)
{
	JCASSERT(m_fs);
	struct yaffs_spare spare;
	memset(&spare, 0xff, sizeof(struct yaffs_spare));
	spare.block_status = 'Y';

	//yaffs_wr_nand(dev, flash_block * dev->param.chunks_per_block, NULL,
	//	&spare);
	m_fs->WriteChunkNand(block_no * m_fs->GetChunkPerBlock(), NULL, &spare);
	//yaffs_wr_nand(dev, flash_block * dev->param.chunks_per_block + 1,
	//	NULL, &spare);
	m_fs->WriteChunkNand(block_no * m_fs->GetChunkPerBlock()+1, NULL, &spare);
	return true;
}

//<MIGRATE> yaffs_tagscompat.c : int yaffs_tags_comapt_query_block()
// 通过NAND获取block的状态
bool CTagsHandler::QueryBlock(int block_no, yaffs_block_state &state, UINT32 & seq_number)
{
	JCASSERT(m_fs);
	struct yaffs_spare spare0, spare1;
	static struct yaffs_spare spare_ff;
	static int init;
	INandDriver::ECC_RESULT dummy;

	if (!init) 
	{
		memset(&spare_ff, 0xff, sizeof(spare_ff));
		init = 1;
	}

	seq_number = 0;

	/* Look for bad block markers in the first two chunks */
	//yaffs_rd_chunk_nand(dev, block_no * dev->param.chunks_per_block,
	//	NULL, &spare0, &dummy, 0);
	m_fs->ReadChunkNand(block_no * m_fs->GetChunkPerBlock(), NULL, &spare0, dummy, 0);
	//yaffs_rd_chunk_nand(dev, block_no * dev->param.chunks_per_block + 1,
	//	NULL, &spare1, &dummy, 0);
	m_fs->ReadChunkNand(block_no * m_fs->GetChunkPerBlock() + 1, NULL, &spare1, dummy, 0);

	if (yaffs_hweight8(spare0.block_status & spare1.block_status) < 7)
		state = YAFFS_BLOCK_STATE_DEAD;
	else if (memcmp(&spare_ff, &spare0, sizeof(spare_ff)) == 0)
		state = YAFFS_BLOCK_STATE_EMPTY;
	else	state = YAFFS_BLOCK_STATE_NEEDS_SCAN;
	return true;
}

void CTagsHandler::GetTagsFromSpare(yaffs_spare * spare_ptr, yaffs_tags * tags_ptr)
{
	union yaffs_tags_union *tu = (union yaffs_tags_union *)tags_ptr;
	union yaffs_tags_union tags_stored;
	//int result;

	tags_stored.as_bytes[0] = spare_ptr->tb0;
	tags_stored.as_bytes[1] = spare_ptr->tb1;
	tags_stored.as_bytes[2] = spare_ptr->tb2;
	tags_stored.as_bytes[3] = spare_ptr->tb3;
	tags_stored.as_bytes[4] = spare_ptr->tb4;
	tags_stored.as_bytes[5] = spare_ptr->tb5;
	tags_stored.as_bytes[6] = spare_ptr->tb6;
	tags_stored.as_bytes[7] = spare_ptr->tb7;

// 忽略 endian
//	yaffs_do_endian_u32(dev, &tags_stored.as_u32[0]);
//	yaffs_do_endian_u32(dev, &tags_stored.as_u32[1]);

	*tu = tags_stored;
// 忽略 spare的ECC
	//result = yaffs_check_tags_ecc(tags_ptr);
	//if (result > 0)		dev->n_tags_ecc_fixed++;
	//else if (result < 0)	dev->n_tags_ecc_unfixed++;

}

void CTagsHandler::LoadTagsToSpare(yaffs_spare * spare_ptr, yaffs_tags * tags_ptr)
{
	union yaffs_tags_union *tu_ptr = (union yaffs_tags_union *)tags_ptr;
	union yaffs_tags_union tags_stored = *tu_ptr;

//<TODO> ignore spare ECC
//	yaffs_calc_tags_ecc(&tags_stored.as_tags);

//ignore endian
//	yaffs_do_endian_u32(dev, &tags_stored.as_u32[0]);
//	yaffs_do_endian_u32(dev, &tags_stored.as_u32[1]);

	spare_ptr->tb0 = tags_stored.as_bytes[0];
	spare_ptr->tb1 = tags_stored.as_bytes[1];
	spare_ptr->tb2 = tags_stored.as_bytes[2];
	spare_ptr->tb3 = tags_stored.as_bytes[3];
	spare_ptr->tb4 = tags_stored.as_bytes[4];
	spare_ptr->tb5 = tags_stored.as_bytes[5];
	spare_ptr->tb6 = tags_stored.as_bytes[6];
	spare_ptr->tb7 = tags_stored.as_bytes[7];

}

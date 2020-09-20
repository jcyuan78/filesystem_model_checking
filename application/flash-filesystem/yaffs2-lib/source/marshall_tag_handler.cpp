#include "stdafx.h"
#include "..\include\tags_handler.h"
#include "..\include\yaf_fs.h"

LOCAL_LOGGER_ENABLE(L"tagger", LOGGER_LEVEL_WARNING);





static void DumpTags2(const struct yaffs_ext_tags *t)
{
	LOG_DEBUG(L"ext.tags eccres %d blkbad %d chused %d obj %d chunk%d byte %d del %d ser %d seq %d",
		t->ecc_result, t->block_bad, t->chunk_used, t->obj_id,
		t->chunk_id, t->n_bytes, 0,  
		//t->is_deleted, 
		t->serial_number,
		t->seq_number);
}

static void DumpPackedTag2(const struct yaffs_packed_tags2 *ptt)
{
	LOG_DEBUG(L"packed tags obj %d chunk %d byte %d seq %d",
		ptt->t.obj_id, ptt->t.chunk_id, ptt->t.n_bytes, ptt->t.seq_number);
}

static bool CheckTagsExtraPackable(const struct yaffs_ext_tags *t)
{
	if (t->chunk_id != 0 || !t->extra_available)	return false;

	/* Check if the file size is too long to store */
	if (t->extra_obj_type == YAFFS_OBJECT_TYPE_FILE &&
		(t->extra_file_size >> 31) != 0)	return false;
	return true;
}


void PackTags2(yaffs_packed_tags2 *ptt, const yaffs_ext_tags *t)
{
	ptt->t.chunk_id = t->chunk_id;
	ptt->t.seq_number = t->seq_number;
	ptt->t.n_bytes = t->n_bytes;
	ptt->t.obj_id = t->obj_id;

	/* Only store extra tags for object headers.
	 * If it is a file then only store  if the file size is short\
	 * enough to fit. */
	if (CheckTagsExtraPackable(t))
	{
		/* Store the extra header info instead */
		/* We save the parent object in the chunk_id */
		ptt->t.chunk_id = EXTRA_HEADER_INFO_FLAG | t->extra_parent_id;
		if (t->extra_is_shrink)		ptt->t.chunk_id |= EXTRA_SHRINK_FLAG;
		if (t->extra_shadows)		ptt->t.chunk_id |= EXTRA_SHADOWS_FLAG;

		ptt->t.obj_id &= ~EXTRA_OBJECT_TYPE_MASK;
		ptt->t.obj_id |= (t->extra_obj_type << EXTRA_OBJECT_TYPE_SHIFT);

		if (t->extra_obj_type == YAFFS_OBJECT_TYPE_HARDLINK)
			ptt->t.n_bytes = t->extra_equiv_id;
		else if (t->extra_obj_type == YAFFS_OBJECT_TYPE_FILE)
			ptt->t.n_bytes = (unsigned)t->extra_file_size;
		else		ptt->t.n_bytes = 0;
	}

	DumpPackedTag2(ptt);
	DumpTags2(t);
	//yaffs_do_endian_packed_tags2(dev, ptt);

	//yaffs_ecc_calc_other((unsigned char *)&pt->t,
	//	    sizeof(struct yaffs_packed_tags2_tags_only),
	//			    &pt->ecc);

}


void UnpackTag2(yaffs_ext_tags *t, yaffs_packed_tags2 *ptt)
{
	INandDriver::ECC_RESULT ecc_result = INandDriver::ECC_RESULT_NO_ERROR;

	if (ptt->t.seq_number != 0xffffffff)
	{
		/* Chunk is in use and we need to do ECC */

		//struct yaffs_ecc_other ecc;
		int result = 0;
		//yaffs_ecc_calc_other((unsigned char *)&pt->t,
		//	sizeof(struct yaffs_packed_tags2_tags_only),
		//	&ecc);
		//result =
		//	yaffs_ecc_correct_other((unsigned char *)&pt->t,
		//		sizeof(struct yaffs_packed_tags2_tags_only),
		//		&pt->ecc, &ecc);
		switch (result)
		{
		case 0: ecc_result = INandDriver::ECC_RESULT_NO_ERROR; break;
		case 1:	ecc_result = INandDriver::ECC_RESULT_FIXED; 	break;
		case -1: ecc_result = INandDriver::ECC_RESULT_UNFIXED; 	break;
		default: ecc_result = INandDriver::ECC_RESULT_UNKNOWN;
		}
	}
	//struct yaffs_packed_tags2_tags_only ptt_copy = *ptt_ptr;

	memset(t, 0, sizeof(struct yaffs_ext_tags));

	if (ptt->t.seq_number == 0xffffffff)	return;

	//yaffs_do_endian_packed_tags2(dev, &ptt_copy);

	t->block_bad = 0;
	t->chunk_used = 1;
	t->obj_id = ptt->t.obj_id;
	t->chunk_id = ptt->t.chunk_id;
	t->n_bytes = ptt->t.n_bytes;
	//t->is_deleted = 0;
	t->serial_number = 0;
	t->seq_number = ptt->t.seq_number;

	/* Do extra header info stuff */
	if (ptt->t.chunk_id & EXTRA_HEADER_INFO_FLAG) {
		t->chunk_id = 0;
		t->n_bytes = 0;

		t->extra_available = 1;
		t->extra_parent_id = ptt->t.chunk_id & (~(ALL_EXTRA_FLAGS));
		t->extra_is_shrink = ptt->t.chunk_id & EXTRA_SHRINK_FLAG ? 1 : 0;
		t->extra_shadows = ptt->t.chunk_id & EXTRA_SHADOWS_FLAG ? 1 : 0;
		t->extra_obj_type = (yaffs_obj_type)(ptt->t.obj_id >> EXTRA_OBJECT_TYPE_SHIFT);
		t->obj_id &= ~EXTRA_OBJECT_TYPE_MASK;

		if (t->extra_obj_type == YAFFS_OBJECT_TYPE_HARDLINK)
			t->extra_equiv_id = ptt->t.n_bytes;
		else
			t->extra_file_size = ptt->t.n_bytes;
	}
	t->ecc_result = ecc_result;

	DumpPackedTag2(ptt);
	DumpTags2(t);
}



CMarshallTags::CMarshallTags(void)
	:m_fs(NULL), m_nand(NULL)
{

}

CMarshallTags::~CMarshallTags(void)
{
	RELEASE(m_fs);
	RELEASE(m_nand);
}

int CMarshallTags::WriteChunkTags(int nand_chunk, const BYTE * data, const yaffs_ext_tags * tags)
{
	JCASSERT(m_fs && m_nand && data && tags);
	LOG_STACK_TRACE_EX(L"chunk=%d data=%p tags=%p",	nand_chunk, data, tags);

	struct yaffs_packed_tags2 pt;
	int retval;

	//int packed_tags_size = sizeof(pt);
	//	//dev->param.no_tags_ecc ? sizeof(pt.t) : sizeof(pt);
	//void *packed_tags_ptr = (void *)&pt;
	//	//dev->param.no_tags_ecc ? (void *)&pt.t : (void *)&pt;


	/* For yaffs2 writing there must be both data and tags.
	 * If we're using inband tags, then the tags are stuffed into
	 * the end of the data buffer.  */
	//if (!data || !tags)
	//	BUG();

	//if ( m_fs->IsInbandTags() ) {
	//	struct yaffs_packed_tags2_tags_only *pt2tp;
	//	pt2tp =
	//		(struct yaffs_packed_tags2_tags_only *)(data +
	//			dev->
	//			data_bytes_per_chunk);
	//	yaffs_pack_tags2_tags_only(dev, pt2tp, tags);
	//}
	//else {
	// out band tags only, not support tags, but remain space for ecc.
	PackTags2(&pt, tags);
	//}

	//retval = dev->drv.drv_write_chunk_fn(dev, nand_chunk,
	//	data, dev->param.total_bytes_per_chunk,
	//	(dev->param.inband_tags) ? NULL : packed_tags_ptr,
	//	(dev->param.inband_tags) ? 0 : packed_tags_size);
	retval = m_nand->WriteChunk(nand_chunk, data, m_page_size, (BYTE*)&pt, sizeof(pt));
	return retval;
}

bool CMarshallTags::ReadChunkTags(int nand_chunk, BYTE * data, yaffs_ext_tags * tags)
{
	JCASSERT(m_fs && m_nand);
	LOG_STACK_TRACE_EX(L"chunk=%d data=%p tags=%p", nand_chunk, data, tags);
	bool retval = 0;
	//int local_data = 0;
	BYTE spare_buffer[100];
	INandDriver::ECC_RESULT ecc_result = INandDriver::ECC_RESULT_UNKNOWN;

	struct yaffs_packed_tags2 pt;

	//int packed_tags_size =
	//	dev->param.no_tags_ecc ? sizeof(pt.t) : sizeof(pt);
	//void *packed_tags_ptr =
	//	dev->param.no_tags_ecc ? (void *)&pt.t : (void *)&pt;



	//if (dev->param.inband_tags) {
	//	if (!data) {
	//		local_data = 1;
	//		data = yaffs_get_temp_buffer(dev);
	//	}
	//}

	//if (dev->param.inband_tags || (data && !tags))
	//	retval = dev->drv.drv_read_chunk_fn(dev, nand_chunk,
	//		data, dev->param.total_bytes_per_chunk,
	//		NULL, 0,
	//		&ecc_result);
	//else if (tags)
	retval = m_nand->ReadChunk(nand_chunk, data, m_page_size,
			spare_buffer, sizeof(pt), ecc_result);
	//else
	//	BUG();


	if (retval == false)	return false;
	if (tags) 
	{
		memcpy(&pt, spare_buffer, sizeof(pt));
		UnpackTag2(tags, &pt);
	}

	//if (local_data)		yaffs_release_temp_buffer(dev, data);

	if (tags && ecc_result == INandDriver::ECC_RESULT_UNFIXED) 
	{
		tags->ecc_result = INandDriver::ECC_RESULT_UNFIXED;
		//<TODO> implemet
		//dev->n_ecc_unfixed++;
	}

	if (tags && ecc_result == INandDriver::ECC_RESULT_FIXED) 
	{
		if (tags->ecc_result <= INandDriver::ECC_RESULT_NO_ERROR)
			tags->ecc_result = INandDriver::ECC_RESULT_FIXED;
		//<TODO> implemet
		//dev->n_ecc_fixed++;
	}

	if (ecc_result < INandDriver::ECC_RESULT_UNFIXED)	return true;
	else		return false;
}

bool CMarshallTags::QueryBlock(int block_no, yaffs_block_state & state, UINT32 & seq_number)
{
	LOG_STACK_TRACE_EX(L"block= %d", block_no)
	JCASSERT(m_nand);
	bool retval = m_nand->CheckBad(block_no);

	if (retval == false) 
	{
		LOG_DEBUG(L"block is bad");
		state = YAFFS_BLOCK_STATE_DEAD;
		seq_number = 0;
	}
	else 
	{
		struct yaffs_ext_tags t;
		ReadChunkTags(block_no * m_chunk_per_block, NULL, &t);
		if (t.chunk_used) 
		{
			seq_number = t.seq_number;
			state = YAFFS_BLOCK_STATE_NEEDS_SCAN;
		}
		else 
		{
			seq_number = 0;
			state = YAFFS_BLOCK_STATE_EMPTY;
		}
	}

	LOG_DEBUG(L"block query returns  seq %d state %d", seq_number, state);

	if (retval == 0)	return true;
	else	return false;
}

bool CMarshallTags::MarkBad(int block_no)
{
	JCASSERT(m_fs && m_nand);
	m_nand->MarkBad(block_no);
	return true;
}

void CMarshallTags::SetFileSystem(CYafFs * fs, INandDriver * nand)
{
	JCASSERT(fs && nand);
	m_fs = fs;
	m_fs->AddRef();
	m_nand = nand;
	m_nand->AddRef();
	m_page_size = m_fs->GetBytePerChunk();
	m_chunk_per_block = m_fs->GetChunkPerBlock();
}

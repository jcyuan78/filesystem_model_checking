#include "stdafx.h"
#include "../include/yaffs_file.h"
#include "../include/yaffs_dir.h"
#include "../include/yaf_fs.h"

LOCAL_LOGGER_ENABLE(L"yaffs_file", LOGGER_LEVEL_WARNING);

//-- construct
CYaffsFile::CYaffsFile(void) : m_top(NULL)
{
}

CYaffsFile::~CYaffsFile(void)
{
	JCASSERT(m_fs);
	if (m_top) m_fs->FreeTnode(m_top);
}

//<MIGRATE> 同时包含 yaffs_oh_size_load()
void CYaffsFile::LoadObjectHeader(yaffs_obj_hdr * oh)
{
	loff_t file_size = 0;
	if (oh->parent_obj_id != YAFFS_OBJECTID_DELETED &&
		oh->parent_obj_id != YAFFS_OBJECTID_UNLINKED)
		file_size = m_stored_size;
	/* Marshalling functions to get loff_t file sizes into and out of object headers. */
	oh->file_size_low = LODWORD(file_size);
	oh->file_size_high = HIDWORD(file_size);
}

int CYaffsFile::FindChunkInFile(int inode_chunk, yaffs_ext_tags * tags)
{
	/*Get the Tnode, then get the level 0 offset chunk offset */
	struct yaffs_tnode *tn;
	//int the_chunk = -1;
	struct yaffs_ext_tags local_tags;
	//int ret_val = -1;
	JCASSERT(m_fs);
	//	struct yaffs_dev *dev = in->my_dev;

		/* Passed a NULL, so use our own tags space */
	if (!tags) tags = &local_tags;
	//	tn = yaffs_find_tnode_0(dev, &in->variant.file_variant, inode_chunk);
	tn = FindTnode0(inode_chunk);
	if (!tn) return -1;
	//	the_chunk = yaffs_get_group_base(dev, tn, inode_chunk);
	int the_chunk = GetGroupBase(tn, inode_chunk);
	//	ret_val = yaffs_find_chunk_in_group(dev, the_chunk, tags, in->obj_id, inode_chunk);
		// 通常，一个chunk_grp包含1个chunk，那么the_chunk就是物理chunk。
		// 当disable wide tnode时，tnode的宽度固定为16 bit
		//   此时，总的chunk数大于65536个时，一个group包含chunk_num/65536个(对齐到2的幂)chunk。
		//   the_chunk表示group的第一个chunk。要顺次查看group中的所有chunk，以确定哪个是所需要的。
	return FindChunkInGroup(the_chunk, tags, m_obj.obj_id, inode_chunk);
	//return ret_val;
}

int CYaffsFile::GetGroupBase(yaffs_tnode * tn, UINT32 pos)
{
	JCASSERT(m_fs);
	UINT32 tnode_width = m_fs->GetTnodeWidth();
	u32 *map = (u32 *)tn;
	u32 bit_in_map;
	u32 bit_in_word;
	u32 word_in_map;
	u32 val;

	pos &= YAFFS_TNODES_LEVEL0_MASK;

	// tnode的宽度根据总的chunk数确定，不小于16 bit
	bit_in_map = pos * tnode_width;
	word_in_map = bit_in_map / 32;
	bit_in_word = bit_in_map & (32 - 1);

	val = map[word_in_map] >> bit_in_word;

	if (tnode_width > (32 - bit_in_word))
	{ //当获得的val不够tnode的宽度是，从下一个word补充
		bit_in_word = (32 - bit_in_word);
		word_in_map++;
		val |= (map[word_in_map] << bit_in_word);
	}

	val &= m_fs->GetTnodeMask();
	val <<= m_fs->GetChunkGrpBits();

	return val;
}

int CYaffsFile::FindChunkInGroup(int the_chunk, yaffs_ext_tags * tags, int obj_id, int inode_chunk)
{
	JCASSERT(m_fs);
	CBlockManager * block_manager = m_fs->GetBlockManager();
	JCASSERT(block_manager);
	size_t chunk_grp_size = m_fs->GetChunkGrpSize();
	//int j;

	for (size_t jj = 0; the_chunk && jj < chunk_grp_size; jj++)
	{
		int blk, page;
		m_fs->ChunkToPage(the_chunk, blk, page);
		if (block_manager->CheckChunkBit(blk, page))
		{
			if (chunk_grp_size == 1) return the_chunk;
			else
			{
				m_fs->ReadChunkTagsNand(the_chunk, NULL, tags);
				if (TagsMatch(tags, obj_id, inode_chunk))
				{	/* found it; */
					return the_chunk;
				}
			}
		}
		the_chunk++;
	}
	return -1;
}

void CYaffsFile::PruneChunks(loff_t new_size)
{
	JCASSERT(m_fs);
	//	struct yaffs_dev *dev = in->my_dev;
	size_t page_size = m_fs->GetBytePerChunk();
	size_t page_num = m_fs->GetChunkPerBlock();
	loff_t old_size = m_file_size;
	int chunk_id;
	size_t dummy;
	int last_del;
	int start_del;

	if (old_size > 0)	m_fs->AddrToChunk(old_size - 1, last_del, dummy);
	else				last_del = 0;
	m_fs->AddrToChunk(new_size + page_size - 1, start_del, dummy);
	last_del++;
	start_del++;

	/* Delete backwards so that we don't end up with holes if
	 * power is lost part-way through the operation.	 */
	for (int ii = last_del; ii >= start_del; ii--)
	{
		/* NB this could be optimised somewhat,
		 * eg. could retrieve the tags and write them without
		 * using yaffs_chunk_del	 */
		chunk_id = FindDelFileChunk(ii, NULL);
		if (chunk_id < 1)	continue;
		//if ((u32)chunk_id < (m_fs->GetStartBlock() * page_num) ||
		//	(u32)chunk_id >= ((m_fs->GetEndBlock() + 1) * page_num))
		if (!m_fs->ValidChunkId(chunk_id))
		{
			LOG_NOTICE(L"Found daft chunk_id %d for %d", chunk_id, ii);
		}
		else
		{
			m_data_chunks--;
			m_fs->ChunkDel(chunk_id, true, __LINE__);
		}
	}

}

int CYaffsFile::FindDelFileChunk(int inode_chunk, yaffs_ext_tags * tags)
{
	/* Get the Tnode, then get the level 0 offset chunk offset */
	//struct yaffs_tnode *tn;
	//int the_chunk = -1;
	struct yaffs_ext_tags local_tags;
	//	struct yaffs_dev *dev = in->my_dev;
	JCASSERT(m_fs);
	int ret_val = -1;

	if (!tags)
	{	/* Passed a NULL, so use our own tags space */
		tags = &local_tags;
	}

	//	tn = yaffs_find_tnode_0(dev, &in->variant.file_variant, inode_chunk);
	yaffs_tnode * tn = FindTnode0(inode_chunk);
	if (!tn) return ret_val;
	//the_chunk = yaffs_get_group_base(dev, tn, inode_chunk);
	int the_chunk = GetGroupBase(tn, inode_chunk);
	//	ret_val = yaffs_find_chunk_in_group(dev, the_chunk, tags, in->obj_id,
	//		inode_chunk);
	ret_val = FindChunkInGroup(the_chunk, tags, m_obj.obj_id, inode_chunk);
	/* Delete the entry in the file structure (if found) */
	if (ret_val != -1)
		//		yaffs_load_tnode_0(dev, tn, inode_chunk, 0);
		LoadTnode0(tn, inode_chunk, 0);
	return ret_val;
}

//-- tnode
void CYaffsFile::LoadTnode0(yaffs_tnode * tn, int pos, int val)
{
	u32 *map = (u32 *)tn;
	u32 bit_in_map;
	u32 bit_in_word;
	u32 word_in_map;
	u32 mask;
	JCASSERT(m_fs);
	size_t tnode_width = m_fs->GetTnodeWidth();
	UINT32 tnode_mask = m_fs->GetTnodeMask();
	pos &= YAFFS_TNODES_LEVEL0_MASK;
	val >>= m_fs->GetChunkGrpBits();
	//	dev->chunk_grp_bits;

	bit_in_map = pos * tnode_width;
	word_in_map = bit_in_map / 32;
	bit_in_word = bit_in_map & (32 - 1);

	mask = tnode_mask << bit_in_word;

	map[word_in_map] &= ~mask;
	map[word_in_map] |= (mask & (val << bit_in_word));

	if (tnode_width > (32 - bit_in_word))
	{
		bit_in_word = (32 - bit_in_word);
		word_in_map++;
		mask = tnode_mask >> bit_in_word;
		map[word_in_map] &= ~mask;
		map[word_in_map] |= (mask & (val >> bit_in_word));
	}
}

bool CYaffsFile::PruneTree(void)
{
	int done = 0;
	struct yaffs_tnode *tn;
	if (m_top_level < 1)	return true;
	m_top = PruneWorker(m_top, m_top_level, 0);

	/* Now we have a tree with all the non-zero branches NULL but
	 * the height is the same as it was.
	 * Let's see if we can trim internal tnodes to shorten the tree.
	 * We can do this if only the 0th element in the tnode is in use
	 * (ie all the non-zero are NULL)
	 */

	while (m_top_level && !done)
	{
		tn = m_top;
		int has_data = 0;
		for (int i = 1; i < YAFFS_NTNODES_INTERNAL; i++)
		{
			if (tn->internal[i]) has_data++;
		}

		if (!has_data)
		{
			m_top = tn->internal[0];
			m_top_level--;
			m_fs->FreeTnode(tn);
		}
		else 	done = 1;
	}
	return true;

}

yaffs_tnode * CYaffsFile::PruneWorker(yaffs_tnode * tn, UINT32 level, bool del0)
{
	JCASSERT(m_fs);
	//int i;
	int has_data;

	if (!tn)	return tn;

	has_data = 0;

	if (level > 0)
	{
		for (int ii = 0; ii < YAFFS_NTNODES_INTERNAL; ii++)
		{
			if (tn->internal[ii])
			{
				tn->internal[ii] = PruneWorker(tn->internal[ii], level - 1, (ii == 0) ? del0 : 1);
			}
			if (tn->internal[ii])	has_data++;
		}
	}
	else
	{
		int tnode_size_u32 = m_fs->GetTnodeSize() / sizeof(UINT32);
		u32 *map = (u32 *)tn;
		for (int ii = 0; !has_data && ii < tnode_size_u32; ii++)
		{
			if (map[ii])	has_data++;
		}
	}

	if (has_data == 0 && del0)
	{	/* Free and return NULL */
//		yaffs_free_tnode(dev, tn);
		m_fs->FreeTnode(tn);
		tn = NULL;
	}
	return tn;
}

yaffs_tnode * CYaffsFile::FindTnode0(int chunk_id)
{
	// 在tnode中找到chunk_id对应的tnode
	struct yaffs_tnode *tn = m_top;
	u32 i;
	int required_depth;
	int level = m_top_level;

	/* Check sane level and chunk Id */
	if (level < 0 || level > YAFFS_TNODES_MAX_LEVEL)	return NULL;
	if (chunk_id > YAFFS_MAX_CHUNK_ID)		return NULL;

	/* First check we're tall enough (ie enough top_level) */
	i = chunk_id >> YAFFS_TNODES_LEVEL0_BITS;		// 每个tnode包含4个chunk
	required_depth = 0;
	while (i)
	{
		i >>= YAFFS_TNODES_INTERNAL_BITS;
		required_depth++;
	}
	/* Not tall enough, so we can't find it */
	if (required_depth > m_top_level)	return NULL;
	/* Traverse down to level 0 */
	while (level > 0 && tn)
	{
		//tn = tn->internal[(chunk_id >> (YAFFS_TNODES_LEVEL0_BITS +
		//	(level - 1) *	YAFFS_TNODES_INTERNAL_BITS)) &
		//	YAFFS_TNODES_INTERNAL_MASK];
		tn = tn->internal[IndexInLevel(chunk_id, level)];
		level--;
	}
	return tn;
}

yaffs_tnode * CYaffsFile::AddFindTnode0(int chunk_id, yaffs_tnode * passed_tn)
{
	JCASSERT(m_fs);
	int required_depth;
	int i;
	//int l;
	//struct yaffs_tnode *tn;
	//u32 x;

	/* Check sane level and page Id */
	if (m_top_level < 0 || m_top_level > YAFFS_TNODES_MAX_LEVEL)	return NULL;
	if (chunk_id > YAFFS_MAX_CHUNK_ID)	return NULL;

	/* First check we're tall enough (ie enough top_level) */
	UINT32 x = chunk_id >> YAFFS_TNODES_LEVEL0_BITS;
	required_depth = 0;
	while (x)
	{
		x >>= YAFFS_TNODES_INTERNAL_BITS;
		required_depth++;
	}

	if (required_depth > m_top_level)
	{
		/* Not tall enough, gotta make the tree taller */
		for (i = m_top_level; i < required_depth; i++)
		{
			//tn = yaffs_get_tnode(dev);
			yaffs_tnode * tn = m_fs->GetTnode();
			if (tn)
			{	// 往上生长tnode的层次
				tn->internal[0] = m_top;
				m_top = tn;
				m_top_level++;
			}
			else
			{
				LOG_ERROR(L"yaffs: no more tnodes");
				return NULL;
			}
		}
	}
	/* Traverse down to level 0, adding anything we need */
	int ll = m_top_level;
	yaffs_tnode * tn = m_top;

	if (ll > 0)
	{
		while (ll > 0 && tn)
		{
			//UINT32 xx = (chunk_id >> (YAFFS_TNODES_LEVEL0_BITS + (ll - 1) * YAFFS_TNODES_INTERNAL_BITS)) &
			//	YAFFS_TNODES_INTERNAL_MASK;
			UINT32 xx = IndexInLevel(chunk_id, ll);
			if ((ll > 1) && !tn->internal[xx])
			{	/* Add missing non-level-zero tnode */
//				tn->internal[xx] = yaffs_get_tnode(dev);
				tn->internal[xx] = m_fs->GetTnode();
				if (!tn->internal[xx])	return NULL;
			}
			else if (ll == 1)
			{	/* Looking from level 1 at level 0 */
				if (passed_tn)
				{	/* If we already have one, release it */
					//if (tn->internal[xx])
					//	yaffs_free_tnode(dev,
					//		tn->internal[xx]);
					if (tn->internal[xx]) m_fs->FreeTnode(tn->internal[xx]);
					tn->internal[xx] = passed_tn;
				}
				else if (!tn->internal[xx])
				{	/* Don't have one, none passed in */
//					tn->internal[xx] = yaffs_get_tnode(dev);
					tn->internal[xx] = m_fs->GetTnode();
					if (!tn->internal[xx])		return NULL;
				}
			}
			tn = tn->internal[xx];
			ll--;
		}
	}
	else
	{	/* top is level 0 */
		if (passed_tn)
		{
			//			memcpy(tn, passed_tn,	(dev->tnode_width * YAFFS_NTNODES_LEVEL0) / 8);
			memcpy(tn, passed_tn, (m_fs->GetTnodeWidth() * YAFFS_NTNODES_LEVEL0) / 8);
			//			yaffs_free_tnode(dev, passed_tn);
			m_fs->FreeTnode(passed_tn);
		}
	}
	//<TODO> 需要注意tn的周期管理和回收
	return tn;
}

//-- check point
void CYaffsFile::LoadObjectFromCheckpt(yaffs_checkpt_obj * cp)
{
	__super::LoadObjectFromCheckpt(cp);
	m_file_size = cp->size_or_equiv_obj;
	m_stored_size = cp->size_or_equiv_obj;
	m_data_chunks = cp->n_data_chunks;
}


bool CYaffsFile::CheckptTnodeWorker(CYaffsCheckPoint & checkpt, struct yaffs_tnode *tn, u32 level,int chunk_offset)
{
	//int i;
	//	struct yaffs_dev *dev = in->my_dev;
	bool ok = true;

	if (!tn) return true;

	if (level > 0) 
	{
		for (int ii = 0; ii < YAFFS_NTNODES_INTERNAL && ok; ii++) 
		{
			if (!tn->internal[ii])	continue;
			ok = CheckptTnodeWorker(checkpt, tn->internal[ii], level - 1,	(chunk_offset << YAFFS_TNODES_INTERNAL_BITS) + ii);
		}
		return ok;
	}

	/* Level 0 tnode */
	UINT32 base_offset = chunk_offset << YAFFS_TNODES_LEVEL0_BITS;
//	yaffs_do_endian_u32(dev, &base_offset);

//	ok = (yaffs2_checkpt_wr(dev, &base_offset, sizeof(base_offset)) ==
//		sizeof(base_offset));
	ok = checkpt.TypedWrite(&base_offset);
//	CheckptWrite(&base_offset, sizeof(base_offset)) == sizeof(base_offset);
	if (!ok) ERROR_HANDLE(return false, L"[err] failed on writing checkpt");
	/* NB Can't do an in-place endian swizzle since that would damage current tnode data.
	 * If a tnode endian conversion is required we do a copy. */
//		tn = yaffs2_do_endian_tnode_copy(dev, tn);
	//ok = (yaffs2_checkpt_wr(dev, tn, m_dev->tnode_size) ==
	//		(int)m_dev->tnode_size);
	ok = checkpt.Write(tn, m_fs->GetTnodeSize()) == m_fs->GetTnodeSize();
	return ok;
}

bool CYaffsFile::WriteCheckptTnodes(CYaffsCheckPoint & checkpt)
{
	u32 end_marker = ~0;
	bool ok = true;

//	if (obj->variant_type != YAFFS_OBJECT_TYPE_FILE)	return ok;
	JCASSERT(m_type == YAFFS_OBJECT_TYPE_FILE && m_fs);

	ok = CheckptTnodeWorker(checkpt, m_top, m_top_level, 0);
	if (ok) ok = checkpt.TypedWrite(&end_marker);
//	CheckptWrite(&end_marker, sizeof(end_marker)) == sizeof(end_marker);
//		ok = (yaffs2_checkpt_wr(obj->my_dev, &end_marker,
//			sizeof(end_marker)) == sizeof(end_marker));

	return ok;
}



//<MIGRATE> yaffs_guts.c: yaffs_resize_file()
bool CYaffsFile::SetAllocationSize(LONGLONG new_size)
{
	//<TODO> 优化：当扩大文件时，会通过数据写来扩大文件。这样浪费NAND，需要省略写入动作。
	JCASSERT(m_fs);
	//struct yaffs_dev *dev = in->my_dev;
	loff_t old_size = m_file_size;

	//	yaffs_flush_file_cache(in, 1);
	FlushFile();
	//	yaffs_invalidate_whole_cache(in);
	m_fs->InvalidateWholeCache(this);

	//	yaffs_check_gc(dev, 0);
	m_fs->CheckGc(false);

	//if (in->variant_type != YAFFS_OBJECT_TYPE_FILE)
	//	return YAFFS_FAIL;

	if (new_size == old_size) return true;

	if (new_size > old_size)
	{
		//		yaffs2_handle_hole(in, new_size);
		HandleHole((loff_t)new_size);
		m_file_size = (loff_t)new_size;
	}
	else
	{	/* new_size < old_size */
//		yaffs_resize_file_down(in, new_size);
		ResizeFileDown((loff_t)new_size);
	}

	/* Write a new object header to reflect the resize.
	 * show we've shrunk the file, if need be
	 * Do this only if the file is not in the deleted directories
	 * and is not shadowed.
	 */

	if (m_parent && !m_obj.is_shadowed)
	{
		CYaffsDir * pp = dynamic_cast<CYaffsDir*>(m_parent);
		if (!pp->IsDirOf(CYaffsDir::UNLINK_DIR) && !pp->IsDirOf(CYaffsDir::DEL_DIR))
		{
			UpdateObjectHeader(NULL, false, false, false, NULL);
		}
	}
	return true;
}

bool CYaffsFile::SetEndOfFile(LONGLONG new_size)
{
	return SetAllocationSize(new_size);
}

void CYaffsFile::CloseFile(void)
{
	JCASSERT(m_fs);
	m_fs->FlushCacheForFile(this, true);
}

bool CYaffsFile::FlushFile(void)
{
	JCASSERT(m_fs);
	return m_fs->FlushCacheForFile(this, false);
}

bool CYaffsFile::GetFileInformation(LPBY_HANDLE_FILE_INFORMATION fileinfo) const
{
	memset(fileinfo, 0, sizeof(BY_HANDLE_FILE_INFORMATION));
	fileinfo->nFileSizeLow = LODWORD(m_file_size);
	fileinfo->nFileSizeHigh = 0;
//	fileinfo->dwFileAttributes;
	fileinfo->ftCreationTime = m_obj.win_ctime;
	fileinfo->ftLastAccessTime = m_obj.win_atime;
	fileinfo->ftLastWriteTime = m_obj.win_mtime;
	fileinfo->nFileIndexLow = m_obj.hdr_chunk;
	fileinfo->nFileIndexHigh = 0;
	return true;
}


bool CYaffsFile::HandleHole(loff_t new_size)
{
	JCASSERT(m_fs);
	/* if new_size > old_file_size.
	 * We're going to be writing a hole.
	 * If the hole is small then write zeros otherwise write a start
	 * of hole marker.
	 */
	loff_t old_file_size;
	loff_t increase;
	bool small_hole;
	bool result = true;
	//struct yaffs_dev *dev = NULL;
	u8 *local_buffer = NULL;
	bool small_increase_ok = false;

	//if (!obj)	return false;
	//if (obj->variant_type != YAFFS_OBJECT_TYPE_FILE)
	//	return false;

	//dev = obj->my_dev;
	size_t page_size = m_fs->GetBytePerChunk();
	/* Bail out if not yaffs2 mode */
	//if (!m_fs->IsYaffs2())	return true;
	old_file_size = m_file_size;
	if (new_size <= old_file_size)	return true;
	increase = new_size - old_file_size;
	if (increase < YAFFS_SMALL_HOLE_THRESHOLD * page_size &&
		//		yaffs_check_alloc_available(dev, YAFFS_SMALL_HOLE_THRESHOLD + 1)
		m_fs->CheckAllocAvailable(YAFFS_SMALL_HOLE_THRESHOLD + 1))
	{
		small_hole = true;
		local_buffer = m_fs->GetTempBuffer();
	}
	else	small_hole = false;
	//if (small_hole)	

	if (local_buffer)
	{	/* fill hole with zero bytes */
		loff_t pos = old_file_size;
		int this_write;
		int written;
		memset(local_buffer, 0, page_size);
		small_increase_ok = true;

		while (increase > 0 && small_increase_ok)
		{
			this_write = increase;
			if (this_write > (int)page_size)	this_write = page_size;
			//			written = yaffs_do_file_wr(obj, local_buffer, pos, this_write, 0);
			written = DoFileWrite(local_buffer, pos, this_write, false);
			if (written == this_write)
			{
				pos += this_write;
				increase -= this_write;
			}
			else 	small_increase_ok = false;
		}

		//		yaffs_release_temp_buffer(dev, local_buffer);
		m_fs->ReleaseTempBuffer(local_buffer);

		/* If out of space then reverse any chunks we've added */
		if (!small_increase_ok)	ResizeFileDown(old_file_size);
		//yaffs_resize_file_down(obj, old_file_size);
	}

	if (!small_increase_ok && m_parent)
	{
		CYaffsDir * pp = dynamic_cast<CYaffsDir*>(m_parent);
		if (!pp->IsDirOf(CYaffsDir::UNLINK_DIR) && !pp->IsDirOf(CYaffsDir::DEL_DIR))
		{	///* Write a hole start header with the old file size */
			UpdateObjectHeader(NULL, false, true, false, NULL);
		}
	}
	return result;
}

void CYaffsFile::ResizeFileDown(loff_t new_size)
{
	int new_full;
	size_t new_partial;
	JCASSERT(m_fs);
	size_t page_size = m_fs->GetBytePerChunk();
	//	yaffs_addr_to_chunk(dev, new_size, &new_full, &new_partial);
	m_fs->AddrToChunk(new_size, new_full, new_partial);

	// 回收多余的chunk
	PruneChunks(new_size);
	if (new_partial != 0)
	{
		int last_chunk = 1 + new_full;
		BYTE * local_buffer = m_fs->GetTempBuffer();
		/* Rewrite the last chunk with its new size and zero pad */
		ReadDataObject(last_chunk, local_buffer);
		memset(local_buffer + new_partial, 0, page_size - new_partial);
		WriteDataObject(last_chunk, local_buffer, new_partial, true);
		m_fs->ReleaseTempBuffer(local_buffer);
	}

	m_file_size = new_size;
	m_stored_size = new_size;

	//yaffs_prune_tree(dev, &obj->variant.file_variant);
	PruneTree();
}


//-- read write
bool CYaffsFile::DokanReadFile(LPVOID vbuf, DWORD nbyte, DWORD & read, LONGLONG offset)
{
	JCASSERT(m_fs);
	loff_t pos = 0;
	loff_t endPos = 0;
	int nRead = 0;
	int nToRead = 0;
	int totalRead = 0;
	loff_t maxRead;
	BYTE *buf = (BYTE *)vbuf;
	m_fs->Lock();

	if (0) {}
	else if (nbyte > YAFFS_MAX_RW_SIZE)
	{
		m_fs->SetError(-EINVAL);
		totalRead = -1;
	}
	else 
	{	// only support Pread in Dokan
		loff_t startPos = (loff_t)offset;
		pos = startPos;

		if (m_file_size > pos)	maxRead = m_file_size - pos;
		else			maxRead = 0;
		if (nbyte > maxRead)	nbyte = maxRead;

		endPos = pos + nbyte;
		if (pos < 0 || pos > YAFFS_MAX_FILE_SIZE ||
			nbyte > YAFFS_MAX_RW_SIZE ||
			endPos < 0 || endPos > YAFFS_MAX_FILE_SIZE) 
		{
			totalRead = -1;
			nbyte = 0;
		}

		while (nbyte > 0) 
		{	// 按照YAFFSFS_RW_SIZE对齐读取来读取，第一次读取从pos到下一个YAFFSFS_RW_SIZE的位置。
			//  优化：是否可以简化这个步骤，直接按chunk读取？
			nToRead = YAFFSFS_RW_SIZE -	(pos & (YAFFSFS_RW_SIZE - 1));
			if (nToRead > (int)nbyte)	nToRead = nbyte;

			/* Tricky bit...
			 * Need to reverify object in case the device was unmounted in another thread.  */
			nRead = FileRead(buf, pos, nToRead);

			if (nRead > 0) 
			{
				totalRead += nRead;
				pos += nRead;
				buf += nRead;
			}

			if (nRead == nToRead)		nbyte -= nRead;
			else						nbyte = 0;	/* no more to read */

			if (nbyte > 0) 
			{
				m_fs->Unlock();
				m_fs->Lock();
			}
		}
	}

	m_fs->Unlock();
	read = totalRead;
	return true;
}

bool CYaffsFile::DokanWriteFile(const void * vbuf, DWORD nbyte, DWORD & written, LONGLONG offset)
{
	//struct yaffsfs_FileDes *fd = NULL;
	//struct yaffs_obj *obj = NULL;
	loff_t pos = 0;
	loff_t startPos = 0;
	loff_t endPos;
	int nWritten = 0;
	//int written = 0;
	bool write_trhrough = false;
	int nToWrite = 0;
	const u8 *buf = (const u8 *)vbuf;

	//if (yaffsfs_CheckMemRegion(vbuf, nbyte, 0) < 0) {
	//	m_fs->SetError(-EFAULT);
	//	return -1;
	//}

	m_fs->Lock();
	//fd = yaffsfs_HandleToFileDes(handle);
	//obj = yaffsfs_HandleToObject(handle);

	if (0) {}
	//if (!fd || !obj) 
	//{	/* bad handle */
	//	m_fs->SetError(-EBADF);
	//	written = -1;
	//}
	//else if (!fd->writing) {
	//	m_fs->SetError(-EINVAL);
	//	written = -1;
	//}
	//else if (obj->my_dev->read_only) {
	//	m_fs->SetError(-EROFS);
	//	written = -1;
	//}
	else
	{
		//if (fd->append)		startPos = yaffs_get_obj_length(obj);
		//else if (isPwrite)	startPos = offset;
		//else		startPos = fd->v.position;
		startPos = (loff_t)offset;

		//yaffsfs_GetHandle(handle);
		pos = startPos;
		endPos = pos + nbyte;

		if (pos < 0 || pos > YAFFS_MAX_FILE_SIZE ||
			nbyte > YAFFS_MAX_RW_SIZE ||
			endPos < 0 || endPos > YAFFS_MAX_FILE_SIZE) 
		{
			written = -1;
			nbyte = 0;
		}

		while (nbyte > 0)
		{
			nToWrite = YAFFSFS_RW_SIZE - (pos & (YAFFSFS_RW_SIZE - 1));
			if (nToWrite > (int)nbyte) 	nToWrite = nbyte;

			/* Tricky bit...
			 * Need to reverify object in case the device was
			 * remounted or unmounted in another thread.
			 */
			//obj = yaffsfs_HandleToObject(handle);
			//if (!obj || obj->my_dev->read_only)		nWritten = 0;
			//else	nWritten =	yaffs_wr_file(obj, buf, pos, nToWrite,write_trhrough);
			// 展开yaffs_wr_file(). HandleHole中会写入数据，和DoFileWrite重复，可以优化。
			HandleHole(pos);
			nWritten = DoFileWrite(buf, pos, nToWrite, write_trhrough);

			if (nWritten > 0) 
			{
				written += nWritten;
				pos += nWritten;
				buf += nWritten;
			}

			if (nWritten == nToWrite)				nbyte -= nToWrite;
			else				nbyte = 0;

			if (nWritten < 1 && written < 1) 
			{
				m_fs->SetError(-ENOSPC);
				written = -1;
			}

			if (nbyte > 0) 
			{
				m_fs->Unlock();
				m_fs->Lock();
			}
		}

		//yaffsfs_PutHandle(handle);

		//if (!isPwrite) {
		//	if (written > 0)
		//		fd->v.position = startPos + written;
		//	else
		//		m_fs->SetError(-EINVAL);
		//}
	}

	m_fs->Unlock();

	return (written != -1);
}

size_t CYaffsFile::DoFileWrite(const BYTE * buffer, loff_t offset, size_t n_bytes, bool write_through)
{

	int chunk;
	size_t start;
	size_t n = n_bytes;
	size_t n_done = 0;
	int n_writeback;
	loff_t start_write = offset;
	int chunk_written = 0;
	u32 n_bytes_read;
	loff_t chunk_start;
	JCASSERT(m_fs);
	//struct yaffs_dev *dev;
	//dev = in->my_dev;

	size_t page_size = m_fs->GetBytePerChunk();
	bool cache_not_aligned = !m_fs->IsCacheBypassAligned();
	//bool inband_tag = m_fs->IsInbandTags();
	bool support_cache = m_fs->GetCacheNum() > 0;
	while (n > 0 && chunk_written >= 0)
	{
		//		yaffs_addr_to_chunk(dev, offset, &chunk, &start);
		m_fs->AddrToChunk(offset, chunk, start);
		if (((loff_t)chunk) * page_size + start != offset || start >= page_size)
		{
			LOG_ERROR(L"[err] AddrToChunk of offset %lld gives chunk %d start %d", offset, chunk, start);
			JCASSERT(0);
		}
		chunk++;	/* File pos to chunk in file offset */

		/* OK now check for the curveball where the start and end are in
		 * the same chunk. */
		size_t n_copy;		// 一次写入的大小
		if ((start + n) < page_size)
		{	// 整个写入内容在一个page之内 (？如果start==0，n=page_size，是否会有问题）
			n_copy = n;
			/* Now calculate how many bytes to write back....
			 * If we're overwriting and not writing to then end of
			 * file then we need to write back as much as was there
			 * before. */

			chunk_start = (((loff_t)(chunk - 1)) * page_size);
			if (chunk_start > m_file_size)  n_bytes_read = 0;	/* Past end of file */
			else n_bytes_read = m_file_size - chunk_start;	// 读取并且更新
			if (n_bytes_read > page_size)	n_bytes_read = page_size;
			n_writeback = (n_bytes_read > (start + n)) ? n_bytes_read : (start + n);
			JCASSERT(n_writeback >= 0 && n_writeback <= (int)page_size)
		}
		else
		{	// 跨page
			n_copy = page_size - start;
			n_writeback = page_size;
		}

		if (n_copy != page_size || cache_not_aligned/* || inband_tag*/)
		{	/* An incomplete start or end chunk (or maybe both start and end chunk), or we're using inband tags,
			 * or we're forcing writes through the cache, so we want to use the cache buffers.	 */
			if (support_cache)
			{	// support cache
				struct yaffs_cache *cache;
				/* If we can't find the data in the cache, then load the cache */
//				cache = yaffs_find_chunk_cache(in, chunk);
				cache = m_fs->FindChunkCache(static_cast<CYaffsObject*>(this), chunk);

				if (!cache && m_fs->CheckAllocAvailable(1))
					//					yaffs_check_alloc_available(dev, 1)) 
				{
					//cache = yaffs_grab_chunk_cache(dev);
					cache = m_fs->GrabChunkCache();
					cache->object = static_cast<CYaffsObject*>(this);
					cache->chunk_id = chunk;
					cache->dirty = false;
					cache->locked = false;
					//					yaffs_rd_data_obj(in, chunk, cache->data);
					ReadDataObject(chunk, cache->data);
				}
				else if (cache && !cache->dirty && !m_fs->CheckAllocAvailable(1))
					//					!yaffs_check_alloc_available(dev,1)) 
				{	/* Drop the cache if it was a read cache
					 * item and no space check has been made
					 * for it.	 */
					cache = NULL;
				}

				if (cache)
				{
					//					yaffs_use_cache(dev, cache, 1);
					m_fs->UseCache(cache, true);
					cache->locked = true;
					memcpy(&cache->data[start], buffer, n_copy);
					cache->locked = false;
					cache->n_bytes = n_writeback;

					if (write_through)
					{
						chunk_written =
							//yaffs_wr_data_obj(cache->object,	cache->chunk_id,
							//	cache->data,cache->n_bytes, 1);
							WriteDataObject(cache->chunk_id, cache->data, cache->n_bytes, 1);
						cache->dirty = false;
					}
				}
				else
				{
					chunk_written = -1;	/* fail write */
				}
			}
			else
			{	// no cache support?
				/* An incomplete start or end chunk (or maybe
				 * both start and end chunk). Read into the
				 * local buffer then copy over and write back.  */

				BYTE *local_buffer = m_fs->GetTempBuffer();
				//yaffs_get_temp_buffer(dev);
//				yaffs_rd_data_obj(in, chunk, local_buffer);
				ReadDataObject(chunk, local_buffer);
				memcpy(&local_buffer[start], buffer, n_copy);
				chunk_written = WriteDataObject(chunk, local_buffer, n_writeback, 0);
				//yaffs_wr_data_obj(in, chunk,
				//	local_buffer,
				//	n_writeback, 0);
			//yaffs_release_temp_buffer(dev, local_buffer);
				m_fs->ReleaseTempBuffer(local_buffer);
			}
		}
		else
		{	/* A full chunk. Write directly from the buffer. */
			chunk_written = WriteDataObject(chunk, buffer, page_size, 0);
			//yaffs_wr_data_obj(in, chunk, buffer,
			//	page_size, 0);

		/* Since we've overwritten the cached data,
		 * we better invalidate it. */
		 //yaffs_invalidate_chunk_cache(in, chunk);
			m_fs->InvalidateChunkCache(static_cast<CYaffsObject*>(this), chunk);
		}

		if (chunk_written >= 0) {
			n -= n_copy;
			offset += n_copy;
			buffer += n_copy;
			n_done += n_copy;
		}
	}

	/* Update file object */

	if ((start_write + n_done) > m_file_size)
		m_file_size = (start_write + n_done);

	m_obj.dirty = 1;
	return n_done;
}

bool CYaffsFile::SetObjectByHeaderTag(CYafFs * fs, UINT32 chunk, yaffs_obj_hdr * oh, yaffs_ext_tags & tags)
{
	//JCASSERT(oh);

	loff_t file_size;
	bool is_shrink;
	UINT32 parent_obj_id;
	if (oh)
	{
		file_size = m_fs->ObjhdrToSize(oh);
		is_shrink = oh->is_shrink;
		parent_obj_id = oh->parent_obj_id;
	}
	else
	{
		file_size = tags.extra_file_size;
		is_shrink = tags.extra_is_shrink;
		parent_obj_id = tags.extra_parent_id;
	}

	/* If it is deleted (unlinked at start also means deleted)
	 * we treat the file size as being zeroed at this point.  */
	if (parent_obj_id == YAFFS_OBJECTID_DELETED ||
		parent_obj_id == YAFFS_OBJECTID_UNLINKED)
	{
		file_size = 0;
		is_shrink = true;
	}
	else
	{
		CYaffsObject::SetObjectByHeaderTag(fs, chunk, oh, tags);
		m_obj.dirty = 0;

		/* Note re hardlinks.
		 Since we might scan a hardlink before its equivalent object is scanned we put
		 them all in a list. After scanning is complete, we should have all the objects,
		 so we run through this list and fix up all the chains. */

		 //	case YAFFS_OBJECT_TYPE_FILE:
		 //	file_var = &obj_in->variant.file_variant;
		if (m_stored_size < file_size)
		{
			/* This covers the case where the file size is greater than the data held.
			 This will happen if the file is resized to be larger than its current data extents.*/
			m_file_size = file_size;
			m_stored_size = file_size;
		}
		//if (m_shrink_size > file_size) m_shrink_size = file_size;
	}
	if (m_shrink_size > file_size) m_shrink_size = file_size;
	return true;
}

bool CYaffsFile::ReadDataObject(int inode_chunk, BYTE * buffer)
{
	JCASSERT(m_fs);
	int nand_chunk = FindChunkInFile(inode_chunk, NULL);
	if (nand_chunk >= 0) return m_fs->ReadChunkTagsNand(nand_chunk, buffer, NULL);
	else
	{	// 在文件中未找到所要求的chunk，属于正常情况，填充0返回。
		LOG_DEBUG(L"chunk %d not found, zero instead", nand_chunk);
		/* get sane (zero) data if you read a hole */
		memset(buffer, 0, m_fs->GetBytePerChunk());
		return false;
	}
}

size_t CYaffsFile::FileRead(BYTE * buffer, loff_t offset, loff_t n_bytes)
{
	int n_copy;
	int n = n_bytes;
	int n_done = 0;
	struct yaffs_cache *cache;
	JCASSERT(m_fs);
	//struct yaffs_dev *dev;
	//dev = in->my_dev;

	size_t page_size = m_fs->GetBytePerChunk();
	while (n > 0) 
	{
//		yaffs_addr_to_chunk(dev, offset, &chunk, &start);
		int chunk;
		size_t start;
		m_fs->AddrToChunk(offset, chunk, start);
		chunk++;

		/* OK now check for the curveball where the start and end are in
		 * the same chunk.		 */
		if ((start + n) < page_size)		n_copy = n;
		else			n_copy = page_size - start;

//		cache = yaffs_find_chunk_cache(in, chunk);
		cache = m_fs->FindChunkCache(static_cast<CYaffsObject*>(this), chunk);

		/* If the chunk is already in the cache or it is less than
		 * a whole chunk or we're using inband tags then use the cache
		 * (if there is caching) else bypass the cache.	 */
		if (cache || n_copy != (int)page_size /*|| m_fs->IsInbandTags()*/)
			//dev->param.inband_tags) 
		{
			//if (dev->param.n_caches > 0) 
			if (m_fs->GetCacheNum() > 0)
			{	/* If we can't find the data in the cache, then load it up. */
				if (!cache) 
				{
					//cache =	yaffs_grab_chunk_cache(in->my_dev);
					cache = m_fs->GrabChunkCache();
					cache->object = static_cast<CYaffsObject*>(this);
					cache->chunk_id = chunk;
					cache->dirty = 0;
					cache->locked = 0;
//					yaffs_rd_data_obj(in, chunk,						cache->data);
					ReadDataObject(chunk, cache->data);
					cache->n_bytes = 0;
				}

//				yaffs_use_cache(dev, cache, 0);
				m_fs->UseCache(cache, false);
				cache->locked = 1;
				memcpy(buffer, &cache->data[start], n_copy);
				cache->locked = 0;
			}
			else 
			{	/* Read into the local buffer then copy.. */
				//u8 *local_buffer =					yaffs_get_temp_buffer(dev);
				BYTE * local_buffer = m_fs->GetTempBuffer();
				//yaffs_rd_data_obj(in, chunk, local_buffer);
				ReadDataObject(chunk, local_buffer);
				memcpy(buffer, &local_buffer[start], n_copy);
//				yaffs_release_temp_buffer(dev, local_buffer);
				m_fs->ReleaseTempBuffer(local_buffer);
			}
		}
		else 
		{	/* A full chunk. Read directly into the buffer. */
//			yaffs_rd_data_obj(in, chunk, buffer);
			ReadDataObject(chunk, buffer);
		}
		n -= n_copy;
		offset += n_copy;
		buffer += n_copy;
		n_done += n_copy;
	}
	return n_done;
}

size_t CYaffsFile::WriteDataObject(int inode_chunk, const BYTE * buffer, size_t n_bytes, bool use_reserve)
{
	LOG_STACK_TRACE();
	/* Find old chunk Need to do this to get serial number
	 * Write new one and patch into tree.
	 * Invalidate old tags.  */
	int prev_chunk_id;
	struct yaffs_ext_tags prev_tags;
	int new_chunk_id;
	struct yaffs_ext_tags new_tags;
	JCASSERT(m_fs);
	//struct yaffs_dev *dev = in->my_dev;
	loff_t endpos;

	//	yaffs_check_gc(dev, 0);
	m_fs->CheckGc(false);

	/* Get the previous chunk at this location in the file if it exists.
	 * If it does not exist then put a zero into the tree. This creates
	 * the tnode now, rather than later when it is harder to clean up. */
	 //	prev_chunk_id = yaffs_find_chunk_in_file(in, inode_chunk, &prev_tags);
	prev_chunk_id = FindChunkInFile(inode_chunk, &prev_tags);
	// 逻辑上分为：如果prev_chunk_id不存在(<1)则添加一个新chuang到tnode,
	//	如果添加失败，返回0
	if (prev_chunk_id < 1)
	{		//!yaffs_put_chunk_in_file(in, inode_chunk, 0, 0))
		if (!PutChunkInFile(inode_chunk, 0, 0))
			ERROR_HANDLE(return 0, L"[err] failed on putting new chunk");
	}

	/* Set up new tags */
	memset(&new_tags, 0, sizeof(new_tags));

	new_tags.chunk_id = inode_chunk;
	new_tags.obj_id = m_obj.obj_id;
	new_tags.serial_number =
		(prev_chunk_id > 0) ? prev_tags.serial_number + 1 : 1;
	new_tags.n_bytes = n_bytes;
	size_t page_size = m_fs->GetBytePerChunk();
	if (n_bytes < 1 || n_bytes >page_size)
	{
		LOG_ERROR(L"Writing %d bytes to chunk!!!!!!!!!", n_bytes);
		JCASSERT(0);
	}

	/* If this is a data chunk and the write goes past the end of the stored
	 * size then update the stored_size. */
	if (inode_chunk > 0)
	{
		endpos = (inode_chunk - 1) * page_size + n_bytes;
		if (m_stored_size < endpos) 	m_stored_size = endpos;
	}

	new_chunk_id = m_fs->WriteNewChunk(buffer, &new_tags, use_reserve);
	//		yaffs_write_new_chunk(dev, buffer, &new_tags, use_reserve);

	if (new_chunk_id > 0)
	{
		//		yaffs_put_chunk_in_file(in, inode_chunk, new_chunk_id, 0);
		PutChunkInFile(inode_chunk, new_chunk_id, 0);
		if (prev_chunk_id > 0) m_fs->ChunkDel(prev_chunk_id, true, __LINE__);
		//			yaffs_chunk_del(dev, prev_chunk_id, 1, __LINE__);
		//		yaffs_verify_file_sane(in);
		VerifyFileSane();
	}
	return new_chunk_id;
}

//<MIGRATE> yaffs_guts.c : yaffs_del_obj() : case YAFFS_OBJECT_TYPE_FILE -> yaffs_del_file
bool CYaffsFile::DelObj(void)
{
	//<MIGRATE> yaffs_del_file
	bool ret_val = true;
	int deleted;	/* Need to cache value on stack if in is freed */
	JCASSERT(m_fs);
	SetAllocationSize(0);
	if (m_data_chunks > 0)
	{
		/* Use soft deletion if there is data in the file.
		 * That won't be the case if it has been resized to zero.	 */
		if (!m_obj.unlinked) ret_val = UnlinkFileIfNeeded();
		// yaffs_unlink_file_if_needed(in);
		deleted = m_obj.deleted;
		if (ret_val == true && m_obj.unlinked && !m_obj.deleted)
		{
			m_obj.deleted = 1;
			deleted = 1;
			SoftDelFile();
		}
		return deleted ? true : false;
	}
	else 
	{	/* The file has no data chunks so we toss it immediately */
		m_fs->FreeTnode(m_top);
		__super::DelObj();
		return true;
	}
	return false;
}

bool CYaffsFile::UnlinkFileIfNeeded(void)
{
	//int ret_val;
	//bool del_now = 0;
	//struct yaffs_dev *dev = in->my_dev;
	JCASSERT(m_fs);

	//if (!in->my_inode) del_now = 1;

	//if (del_now) 
	//{
	//	ret_val =
	//		yaffs_change_obj_name(in, in->my_dev->del_dir,
	//			_Y("deleted"), 0, 0);
	//	yaffs_trace(YAFFS_TRACE_TRACING,
	//		"yaffs: immediate deletion of file %d",
	//		in->obj_id);
	//	in->deleted = 1;
	//	in->my_dev->n_deleted_files++;
	//	if (dev->param.disable_soft_del || dev->param.is_yaffs2)
	//		yaffs_resize_file(in, 0);
	//	yaffs_soft_del_file(in);
	//}
	//else {
	bool br = this->ChangeObjName(m_fs->m_unlinked_dir, L"unlinked", false, false);
			//yaffs_change_obj_name(in, in->my_dev->unlinked_dir,
			//	_Y("unlinked"), 0, 0);
	//}
	return br;
}

void CYaffsFile::SoftDelFile(void)
{
	// Yaffs2不支持soft delete
	return;
	//if (!m_obj.deleted || m_obj.soft_del) return;

	//if (m_data_chunks <= 0) 
	//{
	//	/* Empty file with no duplicate object headers, just delete it immediately */
	//	yaffs_free_tnode(m_obj.my_dev, m_obj.variant.file_variant.top);
	//	m_obj.variant.file_variant.top = NULL;
	//	yaffs_trace(YAFFS_TRACE_TRACING,
	//		"yaffs: Deleting empty file %d",
	//		m_obj.obj_id);
	//	yaffs_generic_obj_del(obj);
	//}
	//else 
	//{
	//	yaffs_soft_del_worker(obj,
	//		m_obj.variant.file_variant.top,
	//		m_obj.variant.
	//		file_variant.top_level, 0);
	//	m_obj.soft_del = 1;
	//}
}

bool CYaffsFile::PutChunkInFile(int inode_chunk, int nand_chunk, int in_scan)
{
	/* NB in_scan is zero unless scanning. 
	 * For forward scanning, in_scan is > 0; for backward scanning in_scan is < 0
	 * nand_chunk = 0 is a dummy insert to make sure the tnodes are there. */

	//struct yaffs_tnode *tn;
	JCASSERT(m_fs);
	//struct yaffs_dev *dev = in->my_dev;
	int existing_chunk;

	// 通过虚函数实现，忽略sanity check
	JCASSERT(m_type == YAFFS_OBJECT_TYPE_FILE);
	//if (m_type != YAFFS_OBJECT_TYPE_FILE) 
	//{
	//	/* Just ignore an attempt at putting a chunk into a non-file during scanning.
	//	 * If it is not during Scanning then something went wrong!  */
	//	if (!in_scan) {
	//		yaffs_trace(YAFFS_TRACE_ERROR,
	//			"yaffs tragedy:attempt to put data chunk into a non-file"
	//		);
	//		BUG();
	//	}

	//	yaffs_chunk_del(dev, nand_chunk, 1, __LINE__);
	//	return true;
	//}

	//tn = yaffs_add_find_tnode_0(dev,
	//	&in->variant.file_variant,
	//	inode_chunk, NULL);
	yaffs_tnode * tn = AddFindTnode0(inode_chunk, NULL);
	if (!tn) ERROR_HANDLE(return false, L"[err] failed on adding tnode")

		/* Dummy insert, bail now */
	if (!nand_chunk)	return true;

	//existing_chunk = yaffs_get_group_base(dev, tn, inode_chunk);
	existing_chunk = GetGroupBase(tn, inode_chunk);

	if (in_scan != 0) 
	{	/* If we're scanning then we need to test for duplicates 
		 * NB This does not need to be efficient since it should only happen when the 
		     power fails during a write, then only one chunk should ever be affected.
		 * Correction for YAFFS2: This could happen quite a lot and we need to think 
		     about efficiency! TODO
		 * Update: For backward scanning we don't need to re-read tags so this is quite cheap. */
		struct yaffs_ext_tags existing_tags;
		struct yaffs_ext_tags new_tags;
		unsigned existing_serial, new_serial;

		if (existing_chunk > 0) 
		{	/* NB Right now existing chunk will not be real chunk_id if the chunk group 
			    size > 1 thus we have to do a FindChunkInFile to get the real chunk id.
			 * We have a duplicate now we need to decide which one to use:
			 * Backwards scanning YAFFS2: The old one is what we use, dump the new one.
			 * YAFFS1: Get both sets of tags and compare serial numbers.	 */

			if (in_scan > 0) 
			{	/* Only do this for forward scanning */
				//yaffs_rd_chunk_tags_nand(dev,
				//	nand_chunk,
				//	NULL, &new_tags);
				m_fs->ReadChunkTagsNand(nand_chunk, NULL, &new_tags);
				/* Do a proper find */
				//existing_chunk =	yaffs_find_chunk_in_file(in, inode_chunk,&existing_tags);
				existing_chunk = FindChunkInFile(inode_chunk, &existing_tags);
			}

			if (existing_chunk <= 0) 
			{	/*Hoosterman - how did this happen? */
				LOG_ERROR(L"[err] yaffs tragedy: existing chunk < 0 in scan");
				JCASSERT(0);
			}

			/* NB The deleted flags should be false, otherwise
			 * the chunks will not be loaded during a scan		 */
			if (in_scan > 0) 
			{
				new_serial = new_tags.serial_number;
				existing_serial = existing_tags.serial_number;
			}

			if ((in_scan > 0) && (existing_chunk <= 0 ||
				((existing_serial + 1) & 3) == new_serial)) 
			{	/* Forward scanning.
				 * Use new Delete the old one and drop through to update the tnode	 */
				//yaffs_chunk_del(dev, existing_chunk, 1,
				//	__LINE__);
				m_fs->ChunkDel(existing_chunk, true, __LINE__);
			}
			else 
			{	/* Backward scanning or we want to use the existing one
				 * Delete the new one and return early so that the tnode isn't changed	 */
				//yaffs_chunk_del(dev, nand_chunk, 1, __LINE__);
				m_fs->ChunkDel(nand_chunk, true, __LINE__);
				return true;
			}
		}
	}

	if (existing_chunk == 0)	m_data_chunks++;
//	yaffs_load_tnode_0(dev, tn, inode_chunk, nand_chunk);
	LoadTnode0(tn, inode_chunk, nand_chunk);
	return true;
}


// <MIGRATE> yaffs_guts.c: yaffs_gc_process_chunk()
int CYaffsFile::RefreshChunk(int old_chunk, yaffs_ext_tags & tags, BYTE * buffer)
{
	JCASSERT(m_fs);
	if (!m_fs->SkipVerification())
	{
		int matching_chunk;

		if (tags.chunk_id == 0)		matching_chunk = m_obj.hdr_chunk;
		else						matching_chunk = FindChunkInFile(tags.chunk_id, NULL);
		if (old_chunk != matching_chunk)
		{
			LOG_ERROR(L"[GC] page in gc mismatch: %d %d %d %d",
				old_chunk, matching_chunk, tags.obj_id, tags.chunk_id);
			//JCASSERT(0);
		}	
	}
	//bool mark_flash = true;
	int new_chunk = 0;

	//JCASSERT(!m_obj.deleted /*|| !m_obj.soft_del*/);
	/* It's either a data chunk in a live file or an ObjectHeader, so we're interested in it.
	 * NB Need to keep the ObjectHeaders of deleted files until the whole file has been deleted off */
	tags.serial_number++;
	if (tags.chunk_id == 0)
	{
		/* It is an object Id, We need to nuke the shrinkheader flags since its work
		   is done. Also need to clean up shadowing.
		 * NB We don't want to do all the work of translating object header endianism 
		   back and forth so we leave the oh endian in its stored order.  */
		struct yaffs_obj_hdr *oh;
		oh = (struct yaffs_obj_hdr *) buffer;

		oh->is_shrink = 0;
		tags.extra_is_shrink = 0;
		oh->shadows_obj = 0;
		oh->inband_shadowed_obj_id = 0;
		tags.extra_shadows = 0;

		/* Update file size */
		JCASSERT(m_type == YAFFS_OBJECT_TYPE_FILE);
		ObjHdrSizeLoad(oh, m_stored_size);
		tags.extra_file_size = m_stored_size;
		VerifyObjHdr(oh, &tags, true);
		new_chunk = m_fs->WriteNewChunk((BYTE*)oh, &tags, true);
		JCASSERT(new_chunk >= 0);
		if (new_chunk >= 0)
		{
			m_obj.hdr_chunk = new_chunk;
			m_obj.serial = tags.serial_number;
		}
	}
	else
	{
		//			new_chunk = yaffs_write_new_chunk(dev, buffer, &tags, 1);
		new_chunk = m_fs->WriteNewChunk(buffer, &tags, true);
		JCASSERT(new_chunk >= 0);
		if (new_chunk >= 0)
		{
			PutChunkInFile(tags.chunk_id, new_chunk, 0);
		}
	}
	return new_chunk;
}


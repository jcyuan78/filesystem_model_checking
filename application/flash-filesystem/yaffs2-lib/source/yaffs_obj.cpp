#include "stdafx.h"
#include "../include/yaffs_obj.h"
#include "../include/yaf_fs.h"
#include "../include/yaffs_file.h"

#include "../include/yaffs_dir.h"
#include "../include/allocator.h"

LOCAL_LOGGER_ENABLE(L"yaffs_file", LOGGER_LEVEL_WARNING);

size_t CYafFs::m_obj_num = 0;

CYaffsObject::CYaffsObject(void) : m_data_valid(false)
{
	memset(&m_obj, 0, sizeof(m_obj));
	CYafFs::m_obj_num++;
}

CYaffsObject::~CYaffsObject(void)
{
	if (m_parent)
	{
		CYaffsDir * pp = dynamic_cast<CYaffsDir*>(m_parent); JCASSERT(pp);
		pp->RemoveObject(this);
		m_parent = NULL;
	}
	CYafFs::m_obj_num--;
}

bool CYaffsObject::GetFileInformation(LPBY_HANDLE_FILE_INFORMATION fileinfo) const
{
	memset(fileinfo, 0, sizeof(BY_HANDLE_FILE_INFORMATION));
	return false;
}

//<MIGRATE> merged yaffs_guts.c : yaffs_generic_obj_del();
bool CYaffsObject::DelObj(void)
{
	JCASSERT(m_fs);
	m_fs->InvalidateWholeCache(static_cast<CYaffsObject*>(this));
	// 不支持 deleted文件夹，直接删除文件。
	CYaffsDir * pp = dynamic_cast<CYaffsDir*>(m_parent); JCASSERT(pp);
	// 为何要把删除的文件改名并且添加到delete文件夹？
	//	加入到del_dir是一个dummy操作，其目的是通过修改文件名，update chunk，从而使delete动作被记录在NAND上。
	//	加入到del_dir后，接下来马上从del_dir中删除。
	if (pp && !pp->IsDirOf(CYaffsDir::DEL_DIR))
	{		/* Move to unlinked directory so we have a deletion record */
		ChangeObjName(m_fs->m_del_dir, L"deleted", false, false);
		pp = dynamic_cast<CYaffsDir*>(m_parent);
	}
	pp->RemoveObject(this);
	m_parent = NULL;
	m_fs->ChunkDel(m_obj.hdr_chunk, true, __LINE__);
//<MYCODE> 问题：删除一个文件后，特别是文件长度为0的情况下，没有任何NAND的写入动作。
//	如果此时unmount，删除的动作会被忽略。因此需要强制checkpoint
//	m_fs->CheckptInvalidate();
	m_obj.hdr_chunk = 0;

	// 问题2，遗漏unhash
	m_fs->UnhashObject(this);
	return true;
}

#if 0

void CYaffsObject::AllocEmptyObject(CYaffsObject *& obj, CYafFs * fs)
{
	// 省略alloctor功能，直接以new分配空间
	obj = jcvos::CDynamicInstance<CYaffsObject>::Create();
	JCASSERT(obj);
	//struct yaffs_obj *obj = yaffs_alloc_raw_obj(dev);
	//if (!obj) return obj;

//	dev->n_obj++;

	/* Now sweeten it up... */

//	memset(obj, 0, sizeof(struct yaffs_obj));
	obj->m_obj.being_created = 1;

//	obj->my_dev = dev;
	obj->m_fs = fs;

	obj->m_obj.hdr_chunk = 0;
	obj->m_type = YAFFS_OBJECT_TYPE_UNKNOWN;
//	INIT_LIST_HEAD(&(obj->hard_links));
//	INIT_LIST_HEAD(&(obj->hash_link));
//	INIT_LIST_HEAD(&obj->siblings);

//<TODO> 把siblings	添加到root的子节点中。

	/* Now make the directory sane */
	//if (dev->root_dir) {
	//	obj->m_obj.parent = dev->root_dir;
	//	list_add(&(obj->m_obj.siblings),
	//		&dev->root_dir->variant.dir_variant.children);
	//}

	/* Add it to the lost and found directory.
	 * NB Can't put root or lost-n-found in lost-n-found so
	 * check if lost-n-found exists first
	 */
	//if (dev->lost_n_found)
	//	yaffs_add_obj_to_dir(dev->lost_n_found, obj);

	obj->m_obj.being_created = 0;

//	fs->m_dev->checkpoint_blocks_required = 0;	/* force recalculation */
	fs->ClearCheckpointBlocksRequired();
	//return obj;
}

#endif

void CYaffsObject::RemoveObjFromDir(void)
{
	CYaffsDir * pp = dynamic_cast<CYaffsDir*>(m_parent);
	if (!pp) return;
	pp->RemoveObject(this);
}

//<MIGRATE> yaffs_guts.c : yaffs_new_obj()
// 创建一个新的object
void CYaffsObject::NewObject(CYaffsObject *& file, CYafFs * fs, int number, yaffs_obj_type type)
{
	JCASSERT(fs && file == NULL);
	//<TODO> handle for number < 0
	if (number < 0)		number = fs->NewObjectId();

//	the_obj = yaffs_alloc_empty_obj(dev);
//	AllocEmptyObject(the_obj, fs);
//	JCASSERT(the_obj);
	switch (type) {
	case YAFFS_OBJECT_TYPE_FILE: {
//		CYaffsFile * ff = jcvos::CDynamicInstance<CYaffsFile>::Create();
		CYaffsFile * ff = CAllocateInstance<CYaffsFile, CYaffsObjAllocator>::Create(fs->GetYaffsObjAllocator());
		ff->m_file_size = 0;
		ff->m_stored_size = 0;
		ff->m_shrink_size = fs->GetMaxFileSize();
		//			yaffs_max_file_size(dev);
		ff->m_top_level = 0;
		ff->m_top = fs->GetTnode();
		file = static_cast<CYaffsObject*>(ff);
		break;	}
	case YAFFS_OBJECT_TYPE_DIRECTORY: {
//		CYaffsDir * dd = jcvos::CDynamicInstance<CYaffsDir>::Create();
		CYaffsDir * dd = CAllocateInstance<CYaffsDir, CYaffsObjAllocator>::Create(fs->GetYaffsObjAllocator());
		file = static_cast<CYaffsObject*>(dd);
		break; }
	case YAFFS_OBJECT_TYPE_SYMLINK:
	case YAFFS_OBJECT_TYPE_HARDLINK:
	case YAFFS_OBJECT_TYPE_SPECIAL:
		/* No action required */
//		file = jcvos::CDynamicInstance<CYaffsObject>::Create();
		file = CAllocateInstance<CYaffsObject, CYaffsObjAllocator>::Create(fs->GetYaffsObjAllocator());
		break;
	case YAFFS_OBJECT_TYPE_UNKNOWN:
		/* todo this should not happen */
		JCASSERT(0);
		break;
	}
	file->m_parent = NULL;
	file->m_fs = fs;
	file->m_obj.fake = 0;
	file->m_obj.rename_allowed = 1;
	file->m_obj.unlink_allowed = 1;
	file->m_obj.obj_id = number;
	fs->HashObject(file);
	file->m_type = type;
	file->LoadCurrentTime(true, true);
	LOG_DEBUG(L"created a new object, id=%d,name=%s", file->m_obj.obj_id, file->m_obj.short_name);
}

bool CYaffsObject::ChangeObjName(CYaffsObject * new_dir, const YCHAR * new_name, bool force, bool shadows)
{	// 把this object移动到new_dir下，并改名为new name
	if (new_dir == NULL)	new_dir = m_parent;	/* use the old directory */
	if (!new_dir->IsDirectory()) 
	{
		LOG_ERROR(L"tragedy: yaffs_change_obj_name: new_dir is not a directory");
		JCASSERT(0);
		return false;
	}
	CYaffsDir * _new_dir = dynamic_cast<CYaffsDir*>(new_dir);	JCASSERT(_new_dir);
	bool unlink_op = _new_dir->IsDirOf(CYaffsDir::UNLINK_DIR);
	bool del_op = _new_dir->IsDirOf(CYaffsDir::DEL_DIR);

//	CYaffsObject * existing_target = NULL;
	jcvos::auto_interface<CYaffsObject> existing_target;
	_new_dir->FindByName(existing_target, new_name);
//	existing_target = yaffs_find_by_name(new_dir, new_name);

	/* If the object is a file going into the unlinked directory,
	 *   then it is OK to just stuff it in since duplicate names are OK.
	 *   else only proceed if the new name does not exist and we're putting
	 *   it into a directory.	 */
	if (!(unlink_op || del_op || force || shadows || !existing_target))
	//	!new_dir->IsDirectory())		// new_dir is not Dir这个条件在前面已经判断过，此处忽略
	{
		// unlink和del允许重名
		LOG_ERROR(L"file name duplicated : unlink=%d, del=%d, force=%d, shadows=%d, existing_target=0x%08X",
			unlink_op, del_op, force, shadows, existing_target);
		return false;
	}

	SetObjName(new_name);
	m_obj.dirty = 1;
	_new_dir->AddObjToDir(this);
	if (unlink_op)	m_obj.unlinked = 1;

	/* If it is a deletion then we mark it as a shrink for gc  */
	//<TODO> to be mirated
	//JCASSERT(0)
//	if (yaffs_update_oh(obj, new_name, 0, del_op, shadows, NULL) >= 0)
	if (UpdateObjectHeader(new_name, 0, del_op, shadows, NULL) >=0 ) 	return true;
	return false;
}







void CYaffsObject::UpdateParent(void)
{
	JCASSERT(m_fs);
	//struct yaffs_dev *dev;
	//if (!obj)		return;
	//dev = obj->my_dev;
	m_obj.dirty = 1;
//	yaffs_load_current_time(obj, 0, 1);
	LoadCurrentTime(false, true);
	if (m_fs->IsDeferedDirUpdate() ) 
	{
		//struct list_head *link = &m_obj.variant.dir_variant.dirty;

		//if (list_empty(link)) {
		//	list_add(link, &dev->dirty_dirs);
		//	yaffs_trace(YAFFS_TRACE_BACKGROUND,
		//		"Added object %d to dirty directories",
		//		m_obj.obj_id);
		//}
		m_fs->AddToDirty(this);
		LOG_NOTICE(L"Added object %d to dirty directories", m_obj.obj_id);
	}
	else UpdateObjectHeader(NULL, false, false, false, NULL);
}

bool CYaffsObject::SetObjectByHeaderTag(CYafFs * fs, UINT32 chunk, yaffs_obj_hdr * oh, yaffs_ext_tags & tags)
{
	//	obj_in->valid = 1;
	m_fs = fs;
	m_obj.hdr_chunk = chunk;

	if (oh)
	{
		JCASSERT(GetType() == oh->type);
		m_obj.yst_mode = oh->yst_mode;
		LoadAttribs(oh);
	//	if (oh->shadows_obj > 0) yaffs_handle_shadowed_obj(dev, oh->shadows_obj, 1);
		SetObjName(oh->name);
	}
	else
	{
		JCASSERT(GetType() == tags.extra_obj_type)
		//equiv_id = tags.extra_equiv_id;
		m_obj.lazy_loaded = true;
	}
	m_obj.dirty = 0;
	m_data_valid = true;
	return true;
}

CYaffsObject * CYaffsObject::AllocRawObject(CYafFs * fs)
{
	return NULL;
}

//void CYaffsObject::HashObj(void)
//{
//	JCASSERT(m_fs);
//	int bucket = yaffs_hash_fn(m_obj.obj_id);
////	struct yaffs_dev *dev = m_obj.my_dev;
//	m_fs->AddToObjBucket(this, bucket);
////	list_add(&in->hash_link, &dev->obj_bucket[bucket].list);
////	dev->obj_bucket[bucket].count++;
//
//}

void CYaffsObject::LoadCurrentTime(bool do_a, bool do_c)
{
	SYSTEMTIME now;
	GetSystemTime(&now);
	SystemTimeToFileTime(&now, &m_obj.win_atime);
	m_obj.win_ctime = m_obj.win_mtime = m_obj.win_atime;
	//yfsd_win_file_time_now(m_obj.win_atime);
	//m_obj.win_ctime[0] = m_obj.win_mtime[0] = m_obj.win_atime[0];
	//m_obj.win_ctime[1] = m_obj.win_mtime[1] = m_obj.win_atime[1];
}

void CYaffsObject::LoadAttribs(yaffs_obj_hdr * oh)
{
	JCASSERT(oh);
	m_obj.win_atime = oh->win_atime;
	m_obj.win_ctime = oh->win_ctime;
	m_obj.win_mtime = oh->win_mtime;
	//m_obj.win_atime[0] = oh->win_atime[0];
	//m_obj.win_ctime[0] = oh->win_ctime[0];
	//m_obj.win_mtime[0] = oh->win_mtime[0];
	//m_obj.win_atime[1] = oh->win_atime[1];
	//m_obj.win_ctime[1] = oh->win_ctime[1];
	//m_obj.win_mtime[1] = oh->win_mtime[1];
}

void CYaffsObject::LoadAttribsObjectHeader(yaffs_obj_hdr * oh)
{
	JCASSERT(oh);
	oh->win_atime = m_obj.win_atime;
	oh->win_ctime = m_obj.win_ctime;
	oh->win_mtime = m_obj.win_mtime;
}

UINT16 yaffs_calc_name_sum(const YCHAR * name)
{
	UINT16 sum = 0;
	UINT16 i = 1;

	if (!name)	return 0;

	while ((*name) && i < (YAFFS_MAX_NAME_LENGTH / 2))
	{
		/* 0x1f mask is case insensitive */
		sum += ((*name) & 0x1f) * i;
		i++;
		name++;
	}
	return sum;
}

void CYaffsObject::SetObjName(const YCHAR * name)
{
	memset(m_obj.short_name, 0, sizeof(m_obj.short_name));

	if (name && !name[0]) 
	{
		FixFullName(m_obj.short_name, YAFFS_SHORT_NAME_LENGTH);
		name = m_obj.short_name;
	}
	else if (name && wcsnlen(name, YAFFS_SHORT_NAME_LENGTH + 1) <= YAFFS_SHORT_NAME_LENGTH) 
	{
		wcscpy_s(m_obj.short_name, name);
	}
	m_obj.sum = yaffs_calc_name_sum(name);
}

void CYaffsObject::LoadNameFromOh(YCHAR * name, const YCHAR * oh_name, int buffer_size)
{
	wcscpy_s(name, buffer_size, oh_name);
}

void CYaffsObject::FixFullName(YCHAR * name, int buffer_size)
{
	/* Create an object name if we could not find one. */
	if (wcsnlen(name, YAFFS_MAX_NAME_LENGTH) == 0) 
	{
		YCHAR local_name[20];
		YCHAR num_string[20];
		YCHAR *x = &num_string[19];
		unsigned v = m_obj.obj_id;
		num_string[19] = 0;
		while (v > 0) 
		{
			x--;
			*x = '0' + (v % 10);
			v /= 10;
		}
		/* make up a name */
		wcscpy_s(local_name, YAFFS_LOSTNFOUND_PREFIX);
		wcscat_s(local_name, x);
		wcscpy_s(name, buffer_size-1, local_name);
	}
}

int CYaffsObject::UpdateObjectHeader(const YCHAR * name, 
		bool force, bool is_shrink, bool shadows, yaffs_xattr_mod * xmod)
{
	LOG_STACK_TRACE();
	struct yaffs_block_info *bi;
	JCASSERT(m_fs);
	size_t page_size = m_fs->GetBytePerChunk();

	int prev_chunk_id;
	int ret_val = 0;
	bool result = false;
	int new_chunk_id;
	struct yaffs_ext_tags new_tags;
	struct yaffs_ext_tags old_tags;
	const YCHAR *alias = NULL;
//	u8 *buffer = NULL;
	YCHAR old_name[YAFFS_MAX_NAME_LENGTH + 1];
//	struct yaffs_obj_hdr *oh = NULL;
	loff_t file_size = 0;

	wcscpy_s(old_name, L"silly old name");
	if (m_obj.fake && /*in != dev->root_dir &&*/ !force && !xmod)
	{	// 不处理除了root以外的其他fake dir
		CYaffsDir * dd = dynamic_cast<CYaffsDir *> (this);
		if (!dd || !dd->IsDirOf(CYaffsDir::ROOT_DIR))	return ret_val;
	}

//	yaffs_check_gc(dev, 0);
	m_fs->CheckGc(false);
//	yaffs_check_obj_details_loaded(in);
	CheckObjDetailsLoaded();

//	buffer = yaffs_get_temp_buffer(m_obj.my_dev);
	BYTE * buffer = m_fs->GetTempBuffer();
//	oh = (struct yaffs_obj_hdr *)buffer;
	yaffs_obj_hdr * oh = reinterpret_cast<yaffs_obj_hdr*>(buffer);
	prev_chunk_id = m_obj.hdr_chunk;
	if (prev_chunk_id > 0) 
	{	/* Access the old obj header just to read the name. */
//		result = yaffs_rd_chunk_tags_nand(dev, prev_chunk_id,
//			buffer, &old_tags);
		result = m_fs->ReadChunkTagsNand(prev_chunk_id, buffer, &old_tags);
		if (result == true) 
		{
//			yaffs_verify_oh(in, oh, &old_tags, 0);
			VerifyObjectHeader(oh, &old_tags, 0);
			memcpy_s(old_name, sizeof(old_name), oh->name, sizeof(oh->name));
			/* NB We only wipe the object header area because the rest of
			* the buffer might contain xattribs. */
			memset(oh, 0xff, sizeof(*oh));
		}
	}
	else 		memset(buffer, 0xff, page_size);

	oh->type = m_type;
	oh->yst_mode = m_obj.yst_mode;
	oh->shadows_obj = oh->inband_shadowed_obj_id = shadows;

//	yaffs_load_attribs_oh(oh, in);
	LoadAttribsObjectHeader(oh);

	if (m_parent)	oh->parent_obj_id = m_parent->m_obj.obj_id;
	else			oh->parent_obj_id = 0;

	if (name && *name) 
	{
		memset(oh->name, 0, sizeof(oh->name));
//		yaffs_load_oh_from_name(dev, oh->name, name);
		// yaffs_load_oh_from_name主要处理unicode和utf之间的转换，目前忽略UFT。此处可简化为复制字符串
		wcscpy_s(oh->name, name);
	}
	else if (prev_chunk_id > 0) memcpy_s(oh->name, sizeof(oh->name), old_name, sizeof(oh->name));
	else memset(oh->name, 0, sizeof(oh->name));
	oh->is_shrink = is_shrink;

	
	//switch (m_obj.variant_type) {
	//case YAFFS_OBJECT_TYPE_UNKNOWN:
	//	/* Should not happen */
	//	break;
	//case YAFFS_OBJECT_TYPE_FILE:
	//	if (oh->parent_obj_id != YAFFS_OBJECTID_DELETED &&
	//		oh->parent_obj_id != YAFFS_OBJECTID_UNLINKED)
	//		file_size = m_obj.variant.file_variant.stored_size;
	//	yaffs_oh_size_load(dev, oh, file_size, 0);
	//	break;
	//case YAFFS_OBJECT_TYPE_HARDLINK:
	//	oh->equiv_id = m_obj.variant.hardlink_variant.equiv_id;
	//	break;
	//case YAFFS_OBJECT_TYPE_SPECIAL:
	//	/* Do nothing */
	//	break;
	//case YAFFS_OBJECT_TYPE_DIRECTORY:
	//	/* Do nothing */
	//	break;
	//case YAFFS_OBJECT_TYPE_SYMLINK:
	//	alias = m_obj.variant.symlink_variant.alias;
	//	if (!alias)
	//		alias = _Y("no alias");
	//	strncpy(oh->alias, alias, YAFFS_MAX_ALIAS_LENGTH);
	//	oh->alias[YAFFS_MAX_ALIAS_LENGTH] = 0;
	//	break;
	//}

	//以上分支通过虚函数实现
	LoadObjectHeader(oh);

	/* process any xattrib modifications */
		//		yaffs_apply_xattrib_mod(in, (char *)buffer, xmod);
	if (xmod)	ApplyXattribMod(buffer, xmod);

	/* Tags */
	memset(&new_tags, 0, sizeof(new_tags));
	m_obj.serial++;
	new_tags.chunk_id = 0;
	new_tags.obj_id = m_obj.obj_id;
	new_tags.serial_number = m_obj.serial;

	/* Add extra info for file header */
	new_tags.extra_available = 1;
	new_tags.extra_parent_id = oh->parent_obj_id;
	new_tags.extra_file_size = file_size;
	new_tags.extra_is_shrink = oh->is_shrink;
	new_tags.extra_equiv_id = oh->equiv_id;
	new_tags.extra_shadows = (oh->shadows_obj > 0) ? 1 : 0;
	new_tags.extra_obj_type = m_type;

	VerifyObjectHeader(oh, &new_tags, 1);

	/* Create new chunk in NAND */
	new_chunk_id = m_fs->WriteNewChunk(buffer, &new_tags, (prev_chunk_id > 0));

	if (buffer) m_fs->ReleaseTempBuffer(buffer);

	if (new_chunk_id < 0)
	{
		LOG_ERROR(L"[err] failed on writing obj header");
		return new_chunk_id;
	}
	m_obj.hdr_chunk = new_chunk_id;

	if (prev_chunk_id > 0)
		//		yaffs_chunk_del(dev, prev_chunk_id, 1, __LINE__);
		m_fs->ChunkDel(prev_chunk_id, true, __LINE__);

//	if (!yaffs_obj_cache_dirty(in))
	// 在cache中查找，是否有和此object相关的cache
	if (!m_fs->ObjectCacheDirty(this))	m_obj.dirty = 0;

	/* If this was a shrink, then mark the block that the chunk lives on */
	if (is_shrink) 
	{
		int blk, page;
		m_fs->ChunkToPage(new_chunk_id, blk, page);
		bi = m_fs->GetBlockInfo(blk);
		bi->has_shrink_hdr = 1;
	}
	return new_chunk_id;
}

void CYaffsObject::ObjToCheckptObj(struct yaffs_checkpt_obj *cp)
{
	cp->obj_id = m_obj.obj_id;
	cp->parent_id = (m_parent) ? m_parent->m_obj.obj_id : 0;
	cp->hdr_chunk = m_obj.hdr_chunk;

	CheckptObjBitAssign(cp, CHECKPOINT_VARIANT_BITS, m_type);
	CheckptObjBitAssign(cp, CHECKPOINT_DELETED_BITS, m_obj.deleted);
	//CheckptObjBitAssign(cp, CHECKPOINT_SOFT_DEL_BITS, m_obj.soft_del);
	CheckptObjBitAssign(cp, CHECKPOINT_UNLINKED_BITS, m_obj.unlinked);
	CheckptObjBitAssign(cp, CHECKPOINT_FAKE_BITS, m_obj.fake);
	CheckptObjBitAssign(cp, CHECKPOINT_RENAME_ALLOWED_BITS, m_obj.rename_allowed);
	CheckptObjBitAssign(cp, CHECKPOINT_UNLINK_ALLOWED_BITS, m_obj.unlink_allowed);
	CheckptObjBitAssign(cp, CHECKPOINT_SERIAL_BITS, m_obj.serial);


	//if (m_type == YAFFS_OBJECT_TYPE_FILE)
	//	cp->size_or_equiv_obj = m_obj.variant.file_variant.file_size;
	//else if (m_obj.variant_type == YAFFS_OBJECT_TYPE_HARDLINK)
	//	cp->size_or_equiv_obj = m_obj.variant.hardlink_variant.equiv_id;
}

int CYaffsObject::RefreshChunk(int old_chunk, yaffs_ext_tags & tags, BYTE * buffer)
{
	JCASSERT(m_fs);
	JCASSERT(tags.chunk_id == 0);

	if (!m_fs->SkipVerification())
	{
		//int matching_chunk;
		//if (tags.chunk_id == 0)		matching_chunk = m_obj.hdr_chunk;
		if (old_chunk != m_obj.hdr_chunk)
		{
			LOG_ERROR(L"[GC] page in gc mismatch: %d %d %d %d",
				old_chunk, m_obj.hdr_chunk, tags.obj_id, tags.chunk_id);
			JCASSERT(0);
		}
	}
	int new_chunk = 0;
	/* It's either a data chunk in a live file or an ObjectHeader, so we're interested in it.
	 * NB Need to keep the ObjectHeaders of deleted files until the whole file has been deleted off */
	tags.serial_number++;
	if (tags.chunk_id == 0)
	{
		/* It is an object Id, We need to nuke the shrinkheader flags since its
		 * work is done. Also need to clean up shadowing.
		 * NB We don't want to do all the work of translating
		 object header endianism back and forth so we leave
		 * the oh endian in its stored order.  */

		struct yaffs_obj_hdr *oh;
		oh = (struct yaffs_obj_hdr *) buffer;

		oh->is_shrink = 0;
		tags.extra_is_shrink = 0;
		oh->shadows_obj = 0;
		oh->inband_shadowed_obj_id = 0;
		tags.extra_shadows = 0;
//			yaffs_verify_oh(object, oh, &tags, 1);
		VerifyObjHdr(oh, &tags, true);
		//			new_chunk = yaffs_write_new_chunk(dev, (u8 *)oh, &tags, 1);
		new_chunk = m_fs->WriteNewChunk((BYTE*)oh, &tags, true);
		//<TODO> error handle new_chunk < 0
		JCASSERT(new_chunk>=0)

		if (new_chunk >= 0)
		{
			m_obj.hdr_chunk = new_chunk;
			m_obj.serial = tags.serial_number;
		}
	}
	return new_chunk;
}


void CYaffsObject::LoadObjectFromCheckpt(yaffs_checkpt_obj * cp)
{
	m_obj.obj_id = cp->obj_id;
	m_obj.hdr_chunk = cp->hdr_chunk;

	m_type = CheckptObjBitGet<yaffs_obj_type>(cp, CHECKPOINT_VARIANT_BITS);
	m_obj.deleted = CheckptObjBitGet<BYTE>(cp, CHECKPOINT_DELETED_BITS);
	//m_obj.soft_del = CheckptObjBitGet<BYTE>(cp, CHECKPOINT_SOFT_DEL_BITS);
	m_obj.unlinked = CheckptObjBitGet<BYTE>(cp, CHECKPOINT_UNLINKED_BITS);
	m_obj.fake = CheckptObjBitGet<BYTE>(cp, CHECKPOINT_FAKE_BITS);
	m_obj.rename_allowed = CheckptObjBitGet<BYTE>(cp, CHECKPOINT_RENAME_ALLOWED_BITS);
	m_obj.unlink_allowed = CheckptObjBitGet<BYTE>(cp, CHECKPOINT_UNLINK_ALLOWED_BITS);
	m_obj.serial = CheckptObjBitGet<BYTE>(cp, CHECKPOINT_SERIAL_BITS);

	if (m_obj.hdr_chunk > 0)		m_obj.lazy_loaded = 1;
	m_data_valid = true;

}

//int CYaffsObject::ApplyXattribMod(BYTE * buffer, yaffs_xattr_mod * xmod)
//{
////	int retval = 0;
////	int x_offs = sizeof(struct yaffs_obj_hdr);
//////	struct yaffs_dev *dev = obj->my_dev;
////	JCASSERT(m_fs);
////	size_t page_size = m_fs->GetBytePerChunk();
////	int x_size = page_size - sizeof(struct yaffs_obj_hdr);
////	BYTE *x_buffer = buffer + x_offs;
////
////	if (xmod->set)	retval =
////		nval_set(dev, x_buffer, x_size, xmod->name, xmod->data,
////			xmod->size, xmod->flags);
////	else
////		retval = nval_del(dev, x_buffer, x_size, xmod->name);
////
////	m_obj.has_xattr = nval_hasvalues(dev, x_buffer, x_size);
////	m_obj.xattr_known = 1;
////	xmod->result = retval;
////
////	return retval;
//	return 0;
//}
//



// yaffs_guts.c : int yaffs_get_obj_name(struct yaffs_obj *obj, YCHAR *name, int buffer_size)
size_t CYaffsObject::GetObjName(YCHAR * name, int buffer_size)
{
	memset(name, 0, buffer_size * sizeof(YCHAR));
//	yaffs_check_obj_details_loaded(obj);
	CheckObjDetailsLoaded();
	if (m_obj.obj_id == YAFFS_OBJECTID_LOSTNFOUND) 
	{
//		wcsncpy(name, YAFFS_LOSTNFOUND_NAME, buffer_size - 1);
		wcscpy_s(name, buffer_size, YAFFS_LOSTNFOUND_NAME);
	}
	else if (m_obj.short_name[0]) 
	{
		//wcscpy(name, m_obj.short_name);
		wcscpy_s(name, buffer_size, m_obj.short_name);
	}
	else if (m_obj.hdr_chunk > 0) 
	{
		bool result;
//		u8 *buffer = yaffs_get_temp_buffer(m_obj.my_dev);
		BYTE * buffer = m_fs->GetTempBuffer();
		struct yaffs_obj_hdr *oh = (struct yaffs_obj_hdr *)buffer;
		memset(buffer, 0, m_fs->GetBytePerChunk());

		if (m_obj.hdr_chunk > 0) 
		{
//			result = yaffs_rd_chunk_tags_nand(m_obj.my_dev,	m_obj.hdr_chunk, buffer, NULL);
			result = m_fs->ReadChunkTagsNand(m_obj.hdr_chunk, buffer, NULL);
			if (result)
			{
				LoadNameFromOh(name, oh->name, buffer_size);
//				yaffs_load_name_from_oh(m_obj.my_dev, name, oh->name, buffer_size);
			}
		}
//		yaffs_release_temp_buffer(m_obj.my_dev, buffer);
		m_fs->ReleaseTempBuffer(buffer);
	}

//	yaffs_fix_null_name(obj, name, buffer_size);
	FixFullName(name, buffer_size);

	return wcsnlen(name, YAFFS_MAX_NAME_LENGTH);
}



// yaffs_guts.c : static void yaffs_check_obj_details_loaded(struct yaffs_obj *in)
void CYaffsObject::CheckObjDetailsLoaded(void)
{
	JCASSERT(m_fs);
	BYTE *buf;
	struct yaffs_obj_hdr *oh;
	struct yaffs_ext_tags tags;
	int result;

	// lazy_loaded=0, 表示数据已被读入，=1表示需要读取数据
	if (!m_obj.lazy_loaded || m_obj.hdr_chunk < 1)
	{
		m_data_valid = true;
		return;
	}
	buf = m_fs->GetTempBuffer();
	result = m_fs->ReadChunkTagsNand(m_obj.hdr_chunk, buf, &tags);
	if (result == false)	return;

	oh = (struct yaffs_obj_hdr *)buf;

	m_obj.lazy_loaded = 0;
	m_obj.yst_mode = oh->yst_mode;
	LoadAttribs(oh);
	SetObjName(oh->name);

	if (m_type == YAFFS_OBJECT_TYPE_SYMLINK)
	{
		//<TODO> implement symlink
		//m_obj.variant.symlink_variant.alias =
		//	yaffs_clone_str(oh->alias);
	}
	m_fs->ReleaseTempBuffer(buf);
	m_data_valid = true;
}


YCHAR *yaffs_clone_str(const YCHAR *str)
{
	YCHAR *new_str = NULL;
	size_t len;

	if (!str)	str = L"";

	len = wcsnlen(str, YAFFS_MAX_ALIAS_LENGTH);
	new_str = new wchar_t[len + 1];
	if (new_str)
	{
		wcscpy_s(new_str, len, str);
		new_str[len] = 0;
	}
	return new_str;
}

bool CYaffsObject::IsDirectory(void) const
{
	return (m_type == YAFFS_OBJECT_TYPE_DIRECTORY);
}


/*
 * Checkpoints are really no benefit on very small partitions.
 *
 * To save space on small partitions don't bother with checkpoints unless
 * the partition is at least this big.
 */
#define YAFFS_CHECKPOINT_MIN_BLOCKS 60
#define YAFFS_SMALL_HOLE_THRESHOLD 4













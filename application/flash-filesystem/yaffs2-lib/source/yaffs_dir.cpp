#include "stdafx.h"
#include "../include/yaffs_dir.h"

LOCAL_LOGGER_ENABLE(L"yaffs_dir", LOGGER_LEVEL_WARNING);


CYaffsDir::CYaffsDir(void)
	: m_dir_type(NORMAL_DIR)
{
	memset(m_children, 0, sizeof(CYaffsObject*)*MAX_CHILDREN);
	m_child_num = 0;
}


CYaffsDir::~CYaffsDir(void)
{
	for (size_t it = 0; it < MAX_CHILDREN; ++it)
	{
		CYaffsObject * obj = m_children[it];
		if (obj)
		{
			obj->m_parent = NULL;
			RELEASE(obj);
//			m_children[it] = NULL;
		}
	}
	memset(m_children, 0, sizeof(CYaffsObject*)*MAX_CHILDREN);
	m_child_num = 0;
}

//<MIGRATE> yaffs_guts.c : void yaffs_add_obj_to_dir()
// 把obj添加到当前目录
void CYaffsDir::AddObjToDir(CYaffsObject * obj)
{
	JCASSERT(m_type == YAFFS_OBJECT_TYPE_DIRECTORY && obj);
	VerifyDir();
	if (m_child_num >= MAX_CHILDREN) THROW_ERROR(ERR_USER, L"child object memory out");

	obj->RemoveObjFromDir();
	JCASSERT(obj->m_parent == NULL);
	/* Now add it */
	std::wstring name = obj->GetFileName();
	int sum = yaffs_calc_name_sum(name.c_str());
	int hash = sum % CHILD_HASH;
	int ii = hash+1;
	while (1)
	{
		if (ii >= MAX_CHILDREN) ii = 0;
		if (m_children[ii] == NULL) break;
		if (ii == hash)break;
		ii++;
	}
	if (ii == hash) THROW_ERROR(ERR_USER, L"failed on finding slot hash=%d", hash);
	m_children[ii] = obj;
	m_child_num++;
//	m_children.push_back(obj);
	obj->AddRef();
	obj->m_parent = static_cast<CYaffsObject*>(this);
	// 通过在CYaffDir中增加标记m_dir_type来识别
	if (m_dir_type == UNLINK_DIR || m_dir_type == DEL_DIR)
	{
		obj->m_obj.unlinked = 1;
		// 通过在CYafFs中获取计数实现
	//	m_fs->m_dev->n_unlinked_files++;
		obj->m_obj.rename_allowed = 0;
	}
	VerifyDir();
	obj->VerifyObjectInDir();
}

// yaffs_guts.c : yaffs_find_by_name()
bool CYaffsDir::OpenChild(IFileInfo *& file, const wchar_t * name) const
{
	JCASSERT(file == NULL);
	CYaffsObject * obj = NULL;
	bool br = FindByName(obj, name);
	file = static_cast<IFileInfo*>(obj);
	return br;
}

bool CYaffsDir::CreateChild(IFileInfo *& file, const wchar_t * fn, bool dir)
{
	CYaffsObject * obj = NULL;
	yaffs_obj_type type;
	if (dir) type = YAFFS_OBJECT_TYPE_DIRECTORY;
	else type = YAFFS_OBJECT_TYPE_FILE;
	bool br = false;
	br = CreateObject(obj, type, fn, 0, 0, 0, NULL, NULL, 0);
	file = static_cast<IFileInfo*>(obj);
	return br;
}


bool CYaffsDir::CreateObject(CYaffsObject *& out_obj, yaffs_obj_type type, const YCHAR * name, UINT32 mode, UINT32 uid, UINT32 gid, CYaffsObject * equiv_obj, const YCHAR * alias_str, UINT32 rdev)
{
	jcvos::auto_interface<CYaffsObject> obj;
	YCHAR *str = NULL;
	JCASSERT(m_fs && out_obj == NULL);
	/* Check if the entry exists. If it does then fail the call since we don't want a dup. */
	if (FindByName(obj, name))	return false;
	NewObject(obj, m_fs, -1, type);
	if (!obj) return false;

	obj->m_obj.hdr_chunk = 0;
	obj->m_obj.valid = 1;
	obj->m_obj.yst_mode = mode;
	obj->AttribusInit(gid, uid, rdev);
	obj->SetObjName(name);
	obj->m_obj.dirty = 1;
	obj->m_data_valid = true;

	AddObjToDir(obj);
	switch (type)
	{
	case YAFFS_OBJECT_TYPE_SYMLINK:
		//obj->m_obj.variant.symlink_variant.alias = str;
		break;
	case YAFFS_OBJECT_TYPE_HARDLINK:
		//obj->m_obj.variant.hardlink_variant.equiv_obj = equiv_obj;
		//obj->m_obj.variant.hardlink_variant.equiv_id = equiv_obj->obj_id;
		//list_add(&obj->m_obj.hard_links, &equiv_obj->hard_links);
		break;
	case YAFFS_OBJECT_TYPE_FILE:
	case YAFFS_OBJECT_TYPE_DIRECTORY:
	case YAFFS_OBJECT_TYPE_SPECIAL:
	case YAFFS_OBJECT_TYPE_UNKNOWN:
		/* do nothing */
		break;
	}

	if (obj->UpdateObjectHeader(name, 0, 0, 0, NULL) < 0)
	{	/* Could not create the object header, fail */
		LOG_ERROR(L"[err] failed on update object header, name=%s", name);
		obj->DelObj();
		return false;
	}
	if (obj) UpdateParent();	// this is parent
	obj.detach(out_obj);
	return true;
}


void CYaffsDir::CreateFakeDir(CYaffsDir *& dir, CYafFs * fs, int number, UINT32 mode, const wchar_t * name)
{
	jcvos::auto_interface<CYaffsObject> file;
	NewObject(file, fs, number, YAFFS_OBJECT_TYPE_DIRECTORY);
	JCASSERT(file);
	file->m_obj.fake = 1;	/* it is fake so it might not use NAND */
	file->m_obj.rename_allowed = 0;
	file->m_obj.unlink_allowed = 0;
	file->m_obj.deleted = 0;
	file->m_obj.unlinked = 0;
	file->m_obj.yst_mode = mode;
	file->SetObjName(name);
	file->m_fs = fs;
	file->m_obj.hdr_chunk = 0;	/* Not a valid chunk. */
	file->m_data_valid = true;
	file.detach<CYaffsDir>(dir);	JCASSERT(dir);
	switch (number)
	{
	case YAFFS_OBJECTID_UNLINKED: dir->m_dir_type = UNLINK_DIR; break;
	case YAFFS_OBJECTID_DELETED: dir->m_dir_type = DEL_DIR; break;
	case YAFFS_OBJECTID_ROOT: dir->m_dir_type = ROOT_DIR; break;
	case YAFFS_OBJECTID_LOSTNFOUND: dir->m_dir_type = LOSTNFOUND_DIR; break;
	}

}

bool CYaffsDir::GetFileInformation(LPBY_HANDLE_FILE_INFORMATION fileinfo) const
{
	memset(fileinfo, 0, sizeof(BY_HANDLE_FILE_INFORMATION));
	fileinfo->ftCreationTime = m_obj.win_ctime;
	fileinfo->ftLastAccessTime = m_obj.win_atime;
	fileinfo->ftLastWriteTime = m_obj.win_mtime;
	fileinfo->nFileIndexLow = m_obj.hdr_chunk;
	fileinfo->dwFileAttributes = FILE_ATTRIBUTE_DIRECTORY;

	return true;
}

void CYaffsDir::DeleteDirContents(void)
{
	for (size_t it = 0; it < MAX_CHILDREN; ++it)
	{
		CYaffsObject * obj = m_children[it];
		if (obj == NULL) continue;
		if (obj->IsDirectory())
		{
			CYaffsDir * dd = dynamic_cast<CYaffsDir*>(obj);
			JCASSERT(dd);
			dd->DeleteDirContents();
		}
		LOG_NOTICE(L"Deleting lost_found object %d", obj->GetObjectId());
		obj->UnlinkObj();
	}
}


void CYaffsDir::RemoveObject(CYaffsObject * obj)
{
	obj->VerifyObjectInDir();
	VerifyDir();
	for (size_t it = 0; it <MAX_CHILDREN; ++it)
	{
		if (m_children[it] == obj)
		{
			m_children[it] =NULL;
			obj->m_parent = NULL;
			obj->Release();
			m_child_num--;
			break;
		}
	}
	VerifyDir();
}

bool CYaffsDir::SetObjectByHeaderTag(CYafFs * fs, UINT32 chunk, yaffs_obj_hdr * oh, yaffs_ext_tags & tags)
{
//	CYaffsObject::SetObjectByHeaderTag(fs, chunk, oh, tags);

	if (tags.obj_id == YAFFS_OBJECTID_ROOT || tags.obj_id == YAFFS_OBJECTID_LOSTNFOUND)
	{	/* We only load some info, don't fiddle with directory structure */
//		obj_in->valid = 1;
		m_fs = fs;
		if (oh)
		{
			m_obj.yst_mode = oh->yst_mode;
//			yaffs_load_attribs(in, oh);
			LoadAttribs(oh);
			m_obj.lazy_loaded = 0;
		}
		else
		{
			m_obj.lazy_loaded = 1;
		}
		m_obj.hdr_chunk = chunk;
	}
	else CYaffsObject::SetObjectByHeaderTag(fs, chunk, oh, tags);
	return true;
}

bool CYaffsDir::DelObj(void)
{
	int ret_val = -1;
	if (!m_dirty.empty())
	{
		LOG_NOTICE(L"Remove object %d from dirty directories", m_obj.obj_id);
		JCASSERT(0);
		m_dirty.clear();
	}
	//<migrate> below from yaffs_del_dir()
	if (m_child_num !=0) return false;
	__super::DelObj();
	return true;
}

void CYaffsDir::DeleteChildren(void)
{
	for (size_t it = 0; it != MAX_CHILDREN; ++it)
	{
		if (m_children[it])
		{
			m_children[it]->DelObj();
			m_children[it] = NULL;
		}
	}
	m_child_num = 0;
}


bool CYaffsDir::FindByName(CYaffsObject *& obj, const YCHAR * name) const
{
	JCASSERT(obj == NULL && m_type == YAFFS_OBJECT_TYPE_DIRECTORY);
	YCHAR buffer[YAFFS_MAX_NAME_LENGTH + 1];
	if (!name)	return false;
	int sum = yaffs_calc_name_sum(name);
	int hash = sum % CHILD_HASH;
	int ii = hash + 1;
	while (1)
	{
		if (ii >= MAX_CHILDREN) ii = 0;
		CYaffsObject * ll = m_children[ii];
		if (ll)
		{
			ll->CheckObjDetailsLoaded();
			if (ll->m_obj.sum == sum || ll->m_obj.hdr_chunk <= 0)
			{
				ll->GetObjName(buffer, YAFFS_MAX_NAME_LENGTH + 1);
				if (!wcsncmp(name, buffer, YAFFS_MAX_NAME_LENGTH))
				{
					obj = ll;
					obj->AddRef();
					return true;
				}
			}
		}
		if (ii == hash) break;
		ii++;
	}
	//for (auto it = m_children.begin(); it != m_children.end(); ++it)
	//{
	//	CYaffsObject * ll = (*it);
	//	JCASSERT(ll && ll->m_parent == static_cast<const CYaffsObject*>(this));
	//	ll->CheckObjDetailsLoaded();
	//	/* Special case for lost-n-found */
	//	// 此处可以优化：我们预先设定lost and found object的名称，那两者可以同意处理。
	//	if (ll->m_obj.sum == sum || ll->m_obj.hdr_chunk <= 0)
	//	{	/* LostnFound chunk called Objxxx Do a real check	 */
	//		ll->GetObjName(buffer, YAFFS_MAX_NAME_LENGTH + 1);
	//		if (!wcsncmp(name, buffer, YAFFS_MAX_NAME_LENGTH))
	//		{
	//			obj = ll;
	//			obj->AddRef();
	//			return true;
	//		}
	//	}
	//}
	return false;
}



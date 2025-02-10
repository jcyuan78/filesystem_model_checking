///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//#include "pch.h"
#include "../include/reference_fs.h"
#include "../include/fs_simulator.h"
#include <boost/cast.hpp>

LOCAL_LOGGER_ENABLE(L"ref_fs", LOGGER_LEVEL_DEBUGINFO);


///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//== Help functions ==



///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//== Reference file system ==


void CReferenceFs::Initialize(const std::string & root_path)
{
	memset(m_files, 0, sizeof(m_files));
	// add root
	m_files[0].InitFile(-1, nullptr, root_path, true, 0);
	m_files[0].next = INVALID_BLK;
	m_used_list = 0;

	for (UINT32 ii = 1; ii < MAX_FILE_NUM; ++ii)
	{
		// checksum作为链表指针？
		m_files[ii].next = ii + 1;
		m_files[ii].fid = INVALID_BLK;
	}
	m_files[MAX_FILE_NUM - 1].next = INVALID_BLK;
	m_free_list = 1;
	m_free_num = MAX_FILE_NUM;
	m_dir_num = 1;		// 根目录
	m_free_num--;
	m_reset_count = 0;
}

void CReferenceFs::CopyFrom(const CReferenceFs & src)
{
	memcpy_s(this, sizeof(CReferenceFs), &src, sizeof(CReferenceFs));
}

UINT CReferenceFs::get_file(void)
{
	//wprintf_s(L"pre get file: used_list=%d, next_used=%d, free_list=%d, next_free=%d\n",
	//	m_used_list, m_files[m_used_list].next, m_free_list, m_files[m_free_list].next);
	UINT obj_id = m_free_list;
	CRefFile* obj = m_files + obj_id;
	m_free_list = obj->next;
	m_free_num--;
	// 加入used list
	obj->next = m_used_list;
	m_used_list = obj_id;

	//wprintf_s(L"post get file: used_list=%d, next_used=%d, free_list=%d, next_free=%d\n",
	//	m_used_list, m_files[m_used_list].next, m_free_list, m_files[m_free_list].next);
	return obj_id;
}

void CReferenceFs::put_file(UINT index)
{
	UINT next = m_files[m_used_list].next;
	//wprintf_s(L"pre put file: index=%d, used_list=%d, next_used=%d, free_list=%d, next_free=%d\n",
	//	index, m_used_list, m_files[m_used_list].next, m_free_list, m_files[m_free_list].next);
	//debug_out_used_files();

	CRefFile& obj = m_files[index];
	if (m_used_list == index)
	{
		m_used_list = obj.next;
	}
	else {
	// 找到前一个
		UINT pre = m_used_list;
		while ((is_valid(pre)) && (m_files[pre].next != index)) {
			pre = m_files[pre].next;
		}
		if (is_invalid(pre)) THROW_ERROR(ERR_APP, L"the ref file before %d is invalid", index);
		m_files[pre].next = obj.next;
	}
	obj.fid = INVALID_BLK;

//	m_used_list = obj.next;
	// 加入free list
	obj.next = m_free_list;
	m_free_list = index;
	m_free_num++;

	//wprintf_s(L"post put file, index=%d, next=%d, used_list=%d, used_nr=%d, free_list=%d, free_nr=%d\n", index, obj.next,
	//	m_used_list, (MAX_FILE_NUM - m_free_num), m_free_list, m_free_num);
	//debug_out_used_files();

}

void CReferenceFs::debug_out_used_files(void)
{
	wprintf_s(L"out used list:\n");
//	UINT used = MAX_FILE_NUM - m_free_num;
	for (UINT jj = 0; jj < MAX_FILE_NUM; ++jj)
	{
		if (is_valid(m_files[jj].fid)) {
			wprintf_s(L"index:%d, next:%d, fid=%d, fn:%S\n", jj, m_files[jj].next, m_files[jj].fid, m_files[jj].m_fn);
		}
	}
}

CReferenceFs::CRefFile * CReferenceFs::AddPath(const std::string & path, bool dir, _NID fid )
{
	if (m_free_num <= 0) return nullptr;
	// find a free object;
	UINT obj_id = get_file();
//	debug_out_used_files();

	// 查找父节点
	size_t pos = path.find_last_of("\\");
	std::string parent_fn;
	if (pos >0 ) parent_fn =std::string(path.c_str(), pos);
	else parent_fn = "\\";

	UINT parent_id = FindFileIndex(parent_fn);
	if (parent_id > MAX_FILE_NUM )
	{
		LOG_ERROR(L"[err] the parent (%s) is not exist", parent_fn.c_str());
		THROW_ERROR(ERR_APP, L"[err] the parent (%s) is not exist", parent_fn.c_str());
		return nullptr;
	}
	CRefFile * parent = &m_files[parent_id];
	if (!parent->isdir())
	{
		LOG_ERROR(L"[err] the parent (%s) is not a dir!", parent_fn.c_str());
		THROW_ERROR(ERR_APP, L"[err] the parent (%s) is not a dir!", parent_fn.c_str() );
		return nullptr;
	}
	// 初始化file obj
	m_files[obj_id].InitFile(parent_id, parent, path, dir, fid);
	//wprintf_s(L"add file, index=%d, fid=%d, fn=%S, used_list=%d, used_nr=%d, free_list=%d, free_nr=%d\n", obj_id, fid, path.c_str(),
	//	m_used_list, (MAX_FILE_NUM - m_free_num), m_free_list, m_free_num);
	//debug_out_used_files();

	// 添加到父节点
	parent->m_children[parent->size] = obj_id;
//	m_files[obj_id].pos = parent->size;
	parent->size++;
	if (dir) m_dir_num++;
	else m_file_num++;

//	LOG_DEBUG(L"this = 0x%08p", this);
//	m_ref.insert(std::make_pair(path, obj_id));

	// 更新encode
	parent->UpdateEncode(m_files);
	return m_files + obj_id;
}

void CReferenceFs::GetFileInfo(const CRefFile & file, DWORD & checksum, FSIZE & len) const
{
	checksum = 0;
	len = file.size;
}

void CReferenceFs::GetFilePath(const CRefFile & file, std::string & path) const
{
	path = file.m_fn;
}

void CReferenceFs::UpdateFile(const std::string & path, DWORD checksum, size_t len)
{
	CRefFile * pp = FindFile(path);
	if (pp == nullptr || pp->isdir() )
	{
		THROW_ERROR(ERR_APP, L"[err] file not exist or dir");
	}
	UpdateFile(*pp, checksum, len);
}

void CReferenceFs::UpdateFile(CRefFile & file, DWORD checksum, size_t len)
{
//	file.checksum = checksum;
	file.size = boost::numeric_cast<int>(len);
	file.m_write_count++;
	// 更新encode
	file.UpdateEncode(m_files);
}



void CReferenceFs::OpenFile(CRefFile& file)
{
	if (file.m_is_open) THROW_ERROR(ERR_APP, L"[err] file is already open, fid=%d", file.fid);
	if (file.isdir())	THROW_ERROR(ERR_APP, L"[err] cannot open dir", file.fid);
	file.m_is_open = true;
	file.UpdateEncode(m_files);

	m_opened++;
}

void CReferenceFs::CloseFile(CRefFile& file)
{
	if (!file.m_is_open) THROW_ERROR(ERR_APP, L"[err] file is not open");
	file.m_is_open = false;
	file.UpdateEncode(m_files);

	m_opened--;
}

void CReferenceFs::Demount(void)
{	// 关闭所有文件
	for (int ii=0; ii<MAX_FILE_NUM; ++ii)
	{
		if (is_valid(m_files[ii].fid))
		{
			m_files[ii].m_is_open = false;
		}
	}
	m_reset_count++;
	m_opened = 0;
}

UINT CReferenceFs::FindFileIndex(const std::string& path)
{
	UINT ii = m_used_list;
	while (is_valid(ii))
	{
		CRefFile& file = m_files[ii];
		if (path == file.m_fn) { return ii; }
		ii = file.next;
	}
	return ii;
}

CReferenceFs::CRefFile * CReferenceFs::FindFile(const std::string & path)
{
	UINT ii = FindFileIndex(path);
	if (is_invalid(ii)) return nullptr;
	return m_files + ii;
}

bool CReferenceFs::IsExist(const std::string& path)
{
	UINT ii = FindFileIndex(path);
	return is_valid(ii);
}

void CReferenceFs::RemoveFile(const std::string & path)
{
//	LOG_DEBUG(L"remove dir/file %s", path.c_str());
	UINT obj_id = FindFileIndex(path);
	if (obj_id >= MAX_FILE_NUM) THROW_ERROR(ERR_APP, L"[err] file (%s) cannot find for remove", path.c_str());
	CRefFile * obj = m_files + obj_id;
	bool isdir = obj->isdir();

	// 从父节点中删除文件
	CRefFile * parent = &m_files[obj->m_parent];
	parent->RemoveChild(obj_id);
	if (isdir) m_dir_num--;
	else m_file_num--;

	// 更新encode
	parent->UpdateEncode(m_files);
//	memset(obj, 0, sizeof(CRefFile));
	_NID fid = obj->fid;
//	obj->fid = INVALID_BLK;

	// 移除used list
	put_file(obj_id);
	//wprintf_s(L"remove file, index=%d, fid=%d, fn=%S, used_list=%d, used_nr=%d, free_list=%d, free_nr=%d\n", 
	//	obj_id, fid, path.c_str(),
	//	m_used_list, (MAX_FILE_NUM - m_free_num), m_free_list, m_free_num);
//	debug_out_used_files();

}



size_t CReferenceFs::Encode(char* code, size_t buf_len) const
{
	const char* encode = m_files[0].m_encode;
	int len = m_files[0].m_encode_size;
//	memset(code, 0, sizeof(code));
	char* ptr = code;
	if (m_reset_count < 10) {
		*ptr = m_reset_count + '0';
	}
	else *ptr = '9';
	ptr++;

	memcpy_s(ptr, buf_len-1, encode, len);
	ptr += len;
	*ptr = 0;
	return ptr -code;
}



///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#define ENCODE_TYPE 2

void CReferenceFs::CRefFile::InitFile(UINT parent_id, CRefFile * parent, const std::string& path, bool isdir, _NID _fid)
{
	m_parent = parent_id;
	this->fid = _fid;
	strcpy_s(m_fn, path.c_str());
	size = 0;
	m_is_open = false;
	m_is_dir = isdir;
	if (isdir)
	{
#if ENCODE_TYPE==1
		strcpy_s(m_encode, "0011");
		m_encode_size = 4;
#else
		strcpy_s(m_encode, "[]");
		m_encode_size = 2;
#endif
	}
	else
	{
#if ENCODE_TYPE==1
		strcpy_s(m_encode, "01");
		m_encode_size = 2;
#else
		strcpy_s(m_encode, "0");
		m_encode_size = 1;
#endif
	}
	m_write_count = 0;
	if (parent_id == (UINT)(-1)) m_depth = 0;
	else {
		JCASSERT(parent);
		m_depth = parent->m_depth + 1;
	}
	memset(m_children, 0, sizeof(m_children));
}

void CReferenceFs::CRefFile::UpdateEncode(CRefFile* files)
{
	//检查是否是目录
//	if (!isdir() ) THROW_ERROR(ERR_APP, L"[err] file cannot run encode");
	if (!isdir())
	{	// 构建文件的Encode
		char* p = m_encode;
		m_encode_size = 0;
		if (m_is_open)
		{
			*p = '*';
			p++;
			m_encode_size++;
		}
		if (m_write_count <= 2) *p = '0' + m_write_count;
		else					*p = '2';
		p++;
		m_encode_size++;
	}
	else
	{	// 构建目录的Encode

		//对子节点排序
		qsort_s(m_children, size, sizeof(_NID), CRefFile::CompareEncode, files);
		char* ptr = m_encode;
#if ENCODE_TYPE==1
	* ptr = '0';
#else
		* ptr = '[';
#endif
		ptr++;
		m_encode_size = 1;
		for (UINT ii = 0; ii < size; ++ii)
		{
			CRefFile& child = files[m_children[ii]];
			int len = child.m_encode_size;
			if (m_encode_size + len >= MAX_ENCODE_SIZE)
				THROW_ERROR(ERR_APP, L"encode is too long, cur=%d, child(%d)=%d", m_encode_size, ii, len);
			memcpy_s(ptr, (MAX_ENCODE_SIZE - m_encode_size), child.m_encode, len);
			m_encode_size += len;
			ptr += len;
		}
		// 最后添加 .
#if ENCODE_TYPE==1
	memcpy_s(ptr, MAX_ENCODE_SIZE - m_encode_size, "011", 4);
	m_encode_size += 3;
#else
		memcpy_s(ptr, MAX_ENCODE_SIZE - m_encode_size, "]", 2);
		m_encode_size += 1;
#endif
	}
	// 更新父节点的 Encode
	if (m_parent > MAX_FILE_NUM) return;
	CRefFile& parent = files[m_parent];
	parent.UpdateEncode(files);
}


int CReferenceFs::CRefFile::CompareEncode(void* _files, const void* e1, const void* e2)
{
	CRefFile* files = reinterpret_cast<CRefFile*>(_files);
	_NID fid1 = *(_NID*)e1, fid2 = *(_NID*)e2;

	CRefFile& f1 = files[fid1];
	CRefFile& f2 = files[fid2];
	const char* code1 = f1.m_encode;
	const char* code2 = f2.m_encode;
	return strcmp(code1, code2);
}

void CReferenceFs::CRefFile::RemoveChild(_NID fid)
{
	//找到child
	UINT ii = 0;
	for (; ii < size; ++ii)
	{
		if (m_children[ii] == fid)	{	break;	}
	}
	if (ii >= size) THROW_ERROR(ERR_APP, L"[err] cannot find child fid=%d", fid);
	size--;
	m_children[ii] = m_children[size];
	m_children[size] = 0;
}
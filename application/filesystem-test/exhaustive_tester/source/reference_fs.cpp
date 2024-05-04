///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//#include "pch.h"
#include "../include/reference_fs.h"
#include <boost/cast.hpp>

LOCAL_LOGGER_ENABLE(L"ref_fs", LOGGER_LEVEL_DEBUGINFO);


///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//== Help functions ==

OP_CODE StringToOpId(const std::wstring& str)
{
	OP_CODE id;
	if (false) {}
	else if (str == L"CreateFile")	id = OP_CODE::OP_FILE_CREATE;
	else if (str == L"CreateDir")	id = OP_CODE::OP_DIR_CREATE;
	//else if (str == L"Append")		id = OP_CODE::APPEND_FILE;
	else if (str == L"OverWrite")	id = OP_CODE::OP_FILE_WRITE;
	else if (str == L"DeleteFile")	id = OP_CODE::OP_FILE_DELETE;
	else if (str == L"DeleteDir")	id = OP_CODE::OP_DIR_DELETE;
	else if (str == L"Move")		id = OP_CODE::OP_MOVE;
	else if (str == L"Mount")		id = OP_CODE::OP_DEMOUNT_MOUNT;
	else if (str == L"PowerCycle")	id = OP_CODE::OP_POWER_OFF_RECOVER;
	else							id = OP_CODE::OP_NOP;
	return id;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//== Reference file system ==


void CReferenceFs::Initialize(const std::wstring & root_path)
{
	memset(m_files, 0, sizeof(m_files));
	// add root
	m_files[0].InitFile(-1, nullptr, root_path, true);
	m_ref.insert(std::make_pair(m_files[0].m_fn, 0));

	for (UINT32 ii = 1; ii < MAX_FILE_NUM; ++ii)
	{
		// checksum作为链表指针？
		m_files[ii].checksum = ii + 1;
	}
	m_files[MAX_FILE_NUM - 1].checksum = 0;
	m_free_list = 1;
	m_free_num = MAX_FILE_NUM;
}

void CReferenceFs::CopyFrom(const CReferenceFs & src)
{
	m_ref = src.m_ref;
	memcpy_s(m_files, sizeof(m_files), src.m_files, sizeof(m_files));
	m_free_list = src.m_free_list;
	m_free_num = src.m_free_num;
}

bool CReferenceFs::IsExist(const std::wstring & path)
{
	auto it= m_ref.find(path);
	return (it != m_ref.end());
}

bool CReferenceFs::AddPath(const std::wstring & path, bool dir)
{
	if (m_free_num <= 0) return false;
	// find a free object;
	UINT obj_id = m_free_list;
	CRefFile * obj = m_files + m_free_list;
	m_free_list = obj->checksum;

	// 查找父节点
	size_t pos = path.find_last_of(L"\\");
	std::wstring parent_fn;
	if (pos >0 ) parent_fn =std::wstring(path.c_str(), pos);
	else parent_fn = L"\\";

	UINT parent_id = FindFileIndex(parent_fn);
//	CRefFile * parent = FindFile(parent_fn);
	if (parent_id > MAX_FILE_NUM )
	{
		LOG_ERROR(L"[err] the parent (%s) is not exist", parent_fn.c_str());
		return false;

	}
	CRefFile * parent = &m_files[parent_id];
//	JCASSERT(parent);
	if (!parent->isdir())
	{
		LOG_ERROR(L"[err] the parent (%s) is not a dir!", parent_fn.c_str());
		return false;
	}
	// 初始化file obj
	obj->InitFile(parent_id, parent, path, dir);

	// 添加到父节点
	parent->m_children[parent->size] = obj_id;
	parent->size++;

	LOG_DEBUG(L"this = 0x%08p", this);
	m_ref.insert(std::make_pair(path, obj_id));

	// 更新encode
	//while (parent != nullptr)
	while (1)
	{
		parent->UpdateEncode(m_files);
		parent_id = parent->m_parent;
		if (parent_id > MAX_FILE_NUM) break;
		parent = m_files + parent_id;
	}
	return true;
}

void CReferenceFs::GetFileInfo(const CRefFile & file, DWORD & checksum, FSIZE & len) const
{
	checksum = file.checksum;
	len = file.size;
}

void CReferenceFs::GetFilePath(const CRefFile & file, std::wstring & path) const
{
	path = file.m_fn;
}

void CReferenceFs::UpdateFile(const std::wstring & path, DWORD checksum, size_t len)
{
	CRefFile * pp = FindFile(path);
	if (pp == nullptr || pp->isdir() )
	{
		THROW_ERROR(ERR_APP, L"[err] file not exist or dir");
	}
//	JCASSERT(pp);
	pp->checksum = checksum;
	pp->size = boost::numeric_cast<int>(len);
	pp->m_write_count++;
}

void CReferenceFs::UpdateFile(CRefFile & file, DWORD checksum, size_t len)
{
	file.checksum = checksum;
	file.size = boost::numeric_cast<int>(len);
}

const CReferenceFs::CRefFile & CReferenceFs::GetFile(CReferenceFs::CONST_ITERATOR & it) const
{
	return *(m_files+it->second);
}

//bool CReferenceFs::IsDir(const CRefFile & file) const
//{
//	return file.isdir();
//}

CReferenceFs::CRefFile * CReferenceFs::FindFile(const std::wstring & path)
{
	auto it = m_ref.find(path);
	if (it == m_ref.end()) return NULL;
	return m_files + it->second;
}

void CReferenceFs::RemoveFile(const std::wstring & path)
{
//	LOG_DEBUG(L"this = 0x%08p", this);
	LOG_DEBUG(L"remove dir/file %s", path.c_str());
	UINT obj_id = FindFileIndex(path);
	if (obj_id >= MAX_FILE_NUM) THROW_ERROR(ERR_APP, L"[err] file (%s) cannot find for remove", path.c_str());
	CRefFile * obj = m_files + obj_id;

	// 从父节点中删除文件
	CRefFile * parent = &m_files[obj->m_parent];
	parent->RemoveChild(obj_id);

	// 更新encode
	while (1)
	{
		parent->UpdateEncode(m_files);
		UINT parent_id = parent->m_parent;
		if (parent_id > MAX_FILE_NUM) break;
		parent = m_files + parent_id;
	}

	memset(obj, 0, sizeof(CRefFile));
	// 加入free list
	obj->checksum = m_free_list;
	m_free_list = obj_id;
	m_free_num++;
	size_t erased = m_ref.erase(path);
}

UINT CReferenceFs::FindFileIndex(const std::wstring & path)
{
	auto it = m_ref.find(path);
	if (it == m_ref.end()) return -1;
	return it->second;
}


void CReferenceFs::Encode(DWORD* code, size_t buf_len) const
{
	const char* encode = m_files[0].m_encode;
	int len = m_files[0].m_encode_size;
	memset(code, 0, sizeof(DWORD) * buf_len);

	DWORD* ptr = code;
	*ptr = LOWORD(len);
	DWORD mask = 0x00010000;
	for (int ii = 0; ii < len; ++ii)
	{
		if (encode[ii] == '1') (*ptr) |= mask;
		
		if (mask == 0x80000000)
		{
			mask = 1;
			ptr++;
		}
		mask <<= 1;
	}
}

void CReferenceFs::GetEncodeString(std::string& str) const
{
	str = m_files[0].m_encode;
}


///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
void CReferenceFs::CRefFile::InitFile(UINT parent_id, CRefFile * parent, const std::wstring& path, bool isdir)
{
	m_parent = parent_id;
	wcscpy_s(m_fn, path.c_str());
	if (isdir)
	{
		checksum = -1;	// 目录标志
		size = 0;		// 每个dir一定有一个"."文件，用于区分文件的叶节点。
		strcpy_s(m_encode, "0011");
		m_encode_size = 4;
	}
	else
	{
		checksum = 0;
		size = 0;
		strcpy_s(m_encode, "01");
		m_encode_size = 2;
	}
	m_write_count = 0;
	if (parent_id == (UINT)(-1)) m_depth = 0;
	else {
		JCASSERT(parent);
		m_depth = parent->m_depth + 1;
	}
	memset(m_children, 0, sizeof(m_children));
}

void CReferenceFs::CRefFile::UpdateEncode(CRefFile * files)
{
	//检查是否是目录
	if (!isdir() ) THROW_ERROR(ERR_APP, L"[err] file cannot run encode");
	//对子节点排序
	qsort_s(m_children, size, sizeof(UINT), CRefFile::CompareEncode, files);
	char* ptr = m_encode;
	*ptr = '0'; ptr++;
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
	memcpy_s(ptr, MAX_ENCODE_SIZE - m_encode_size, "011", 4);
	m_encode_size+=3;
}


int CReferenceFs::CRefFile::CompareEncode(void* _files, const void* e1, const void* e2)
{
	CRefFile* files = reinterpret_cast<CRefFile*>(_files);
	UINT fid1 = *(UINT*)e1, fid2 = *(UINT*)e2;

	CRefFile& f1 = files[fid1];
	CRefFile& f2 = files[fid2];
	const char* code1 = f1.m_encode;
	const char* code2 = f2.m_encode;
	return strcmp(code1, code2);
}

void CReferenceFs::CRefFile::RemoveChild(UINT fid)
{
	//找到child
	UINT ii = 0;
	for (; ii < size; ++ii)
	{
		if (m_children[ii] == fid)
		{
			break;
			//if (ii == (size - 1)) 	{	break;	}
		}
	}
	if (ii >= size) THROW_ERROR(ERR_APP, L"[err] cannot find child fid=%d", fid);
	size--;
	m_children[ii] = m_children[size];
	m_children[size] = 0;
}
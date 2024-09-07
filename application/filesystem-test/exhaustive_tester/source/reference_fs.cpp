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
	else if (str == L"OpenFile")	id = OP_CODE::OP_FILE_OPEN;
	else if (str == L"CloseFile")	id = OP_CODE::OP_FILE_CLOSE;
	//else if (str == L"Append")		id = OP_CODE::APPEND_FILE;
	else if (str == L"OverWrite")	id = OP_CODE::OP_FILE_WRITE;
	else if (str == L"Write")		id = OP_CODE::OP_FILE_WRITE;
	else if (str == L"DeleteFile")	id = OP_CODE::OP_FILE_DELETE;
	else if (str == L"DeleteDir")	id = OP_CODE::OP_DIR_DELETE;
	else if (str == L"Move")		id = OP_CODE::OP_MOVE;
	else if (str == L"Mount")		id = OP_CODE::OP_DEMOUNT_MOUNT;
	else if (str == L"PowerCycle")	id = OP_CODE::OP_POWER_OFF_RECOVER;
	else if (str == L"Verify")		id = OP_CODE::OP_FILE_VERIFY;
	else if (str == L"Power")		id = OP_CODE::OP_POWER_OFF_RECOVER;
	else							id = OP_CODE::OP_NOP;
	return id;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//== Reference file system ==


void CReferenceFs::Initialize(const std::string & root_path)
{
	memset(m_files, 0, sizeof(m_files));
	// add root
	m_files[0].InitFile(-1, nullptr, root_path, true, 0);
	m_ref.insert(std::make_pair(m_files[0].m_fn, 0));

	for (UINT32 ii = 1; ii < MAX_FILE_NUM; ++ii)
	{
		// checksum作为链表指针？
		m_files[ii].checksum = ii + 1;
		m_files[ii].fid = INVALID_BLK;
	}
	m_files[MAX_FILE_NUM - 1].checksum = 0;
	m_free_list = 1;
	m_free_num = MAX_FILE_NUM;
	m_dir_num = 1;		// 根目录
}

void CReferenceFs::CopyFrom(const CReferenceFs & src)
{
	m_ref = src.m_ref;
	memcpy_s(m_files, sizeof(m_files), src.m_files, sizeof(m_files));
	m_free_list = src.m_free_list;
	m_free_num = src.m_free_num;
	m_file_num = src.m_file_num;
	m_dir_num = src.m_dir_num;
	m_opened = src.m_opened;
	m_reset_count = src.m_reset_count;
}

bool CReferenceFs::IsExist(const std::string & path)
{
	auto it= m_ref.find(path);
	return (it != m_ref.end());
}

CReferenceFs::CRefFile * CReferenceFs::AddPath(const std::string & path, bool dir, UINT fid )
{
	if (m_free_num <= 0) return nullptr;
	// find a free object;
	UINT obj_id = m_free_list;
	CRefFile * obj = m_files + m_free_list;
	m_free_list = obj->checksum;

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
	obj->InitFile(parent_id, parent, path, dir, fid);

	// 添加到父节点
	parent->m_children[parent->size] = obj_id;
	parent->size++;
	if (dir) m_dir_num++;
	else m_file_num++;

//	LOG_DEBUG(L"this = 0x%08p", this);
	m_ref.insert(std::make_pair(path, obj_id));

	// 更新encode
	parent->UpdateEncode(m_files);
	//while (1)
	//{
	//	parent->UpdateEncode(m_files);
	//	parent_id = parent->m_parent;
	//	if (parent_id > MAX_FILE_NUM) break;
	//	parent = m_files + parent_id;
	//}
	return obj;
}

void CReferenceFs::GetFileInfo(const CRefFile & file, DWORD & checksum, FSIZE & len) const
{
	checksum = file.checksum;
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
	file.checksum = checksum;
	file.size = boost::numeric_cast<int>(len);
	file.m_write_count++;
	// 更新encode
	file.UpdateEncode(m_files);
	//if (file.m_write_count <= 2) file.m_encode[0] = '0' + file.m_write_count;
	//else file.m_encode[0] = '2';
	//file.m_encode[1] = 0;
	//file.m_encode_size = 1;

	//UINT parent_id = file.m_parent;
	//if (parent_id <MAX_FILE_NUM) m_files[parent_id].UpdateEncode(m_files);

	//while (1)
	//{
	//	parent->UpdateEncode(m_files);
	//	parent_id = parent->m_parent;
	//	if (parent_id > MAX_FILE_NUM) break;
	//	parent = m_files + parent_id;
	//}
}

const CReferenceFs::CRefFile & CReferenceFs::GetFile(CReferenceFs::CONST_ITERATOR & it) const
{
	return *(m_files+it->second);
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
		if (m_files[ii].fid != INVALID_BLK)
		{
			m_files[ii].m_is_open = false;
		}
	}
	m_reset_count++;
	m_opened = 0;
}

CReferenceFs::CRefFile * CReferenceFs::FindFile(const std::string & path)
{
	auto it = m_ref.find(path);
	if (it == m_ref.end()) return NULL;
	return m_files + it->second;
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
	//while (1)
	//{
	//	parent->UpdateEncode(m_files);
	//	UINT parent_id = parent->m_parent;
	//	if (parent_id > MAX_FILE_NUM) break;
	//	parent = m_files + parent_id;
	//}

	memset(obj, 0, sizeof(CRefFile));
	obj->fid = INVALID_BLK;
	// 加入free list
	obj->checksum = m_free_list;
	m_free_list = obj_id;
	m_free_num++;
	size_t erased = m_ref.erase(path);
}

UINT CReferenceFs::FindFileIndex(const std::string & path)
{
	auto it = m_ref.find(path);
	if (it == m_ref.end()) return -1;
	return it->second;
}


//void CReferenceFs::GetEncodeString(std::string& str) const
//{
//	str = m_files[0].m_encode;
//}

size_t CReferenceFs::Encode(char* code, size_t buf_len) const
{
	const char* encode = m_files[0].m_encode;
	int len = m_files[0].m_encode_size;
//	memset(code, 0, sizeof(code));
	char* ptr = code;
	if (m_reset_count > 0) {
		*ptr = 'R';
		ptr++;
	}
	memcpy_s(ptr, buf_len-1, encode, len);
	ptr += len;
	*ptr = 0;
	return ptr -code;
}



///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#define ENCODE_TYPE 2

void CReferenceFs::CRefFile::InitFile(UINT parent_id, CRefFile * parent, const std::string& path, bool isdir, UINT fid)
{
	m_parent = parent_id;
	this->fid = fid;
	strcpy_s(m_fn, path.c_str());
	if (isdir)
	{
		checksum = -1;	// 目录标志
		size = 0;		// 每个dir一定有一个"."文件，用于区分文件的叶节点。
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
		checksum = 0;
		size = 0;
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
		qsort_s(m_children, size, sizeof(UINT), CRefFile::CompareEncode, files);
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
		if (m_children[ii] == fid)	{	break;	}
	}
	if (ii >= size) THROW_ERROR(ERR_APP, L"[err] cannot find child fid=%d", fid);
	size--;
	m_children[ii] = m_children[size];
	m_children[size] = 0;
}
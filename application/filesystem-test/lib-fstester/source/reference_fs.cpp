#include "pch.h"
#include "../include/reference_fs.h"
#include <boost/cast.hpp>
LOCAL_LOGGER_ENABLE(L"ref_fs", LOGGER_LEVEL_DEBUGINFO);

void CReferenceFs::Initialize(const std::wstring & root_path)
{
	memset(m_files, 0, sizeof(m_files));
	// add root
	m_files[0].m_size = -1;
	m_files[0].m_checksum = 0;
	wcscpy_s(m_files[0].m_fn, root_path.c_str());
	m_ref.insert(std::make_pair(m_files[0].m_fn, 0));
	//	m_ref.insert(std::make_pair(L"\\", -1));

	for (UINT32 ii = 1; ii < MAX_FILE_NUM; ++ii)
	{
		m_files[ii].m_checksum = ii + 1;
	}
	m_files[MAX_FILE_NUM - 1].m_checksum = 0;
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
	m_free_list = obj->m_checksum;
	wcscpy_s(obj->m_fn, path.c_str());

	LOG_DEBUG(L"this = 0x%08p", this);
	if (dir)
	{
		//m_ref.insert(std::make_pair(path, -1));
		obj->m_size = -1;
		LOG_DEBUG(L"add dir %s, val=-1", path.c_str());
	}
	else
	{
		obj->m_size = 0;
		LOG_DEBUG(L"add file %s, val=", path.c_str());
	}
	obj->m_checksum = 0;
	m_ref.insert(std::make_pair(path, obj_id));
	// 更新父节点中子节点数量
	size_t pos = path.find_last_of(L"\\");
	std::wstring parent;
	if (pos >0 ) parent =std::wstring(path.c_str(), pos);
	else parent = L"\\";

	CRefFile * pp = FindFile(parent);	JCASSERT(pp);
	pp->m_checksum++;
	return true;
}

void CReferenceFs::GetFileInfo(const CRefFile & file, DWORD & checksum, size_t & len) const
{
	//UINT64 val = file.second;
	//len = (size_t)(val & 0xFFFFFFFF);
	//checksum = (DWORD)(val >> 32);
	checksum = file.m_checksum;
	len = file.m_size;
}

void CReferenceFs::GetFilePath(const CRefFile & file, std::wstring & path) const
{
	//path = file.first;
	path = file.m_fn;
}

void CReferenceFs::UpdateFile(const std::wstring & path, DWORD checksum, size_t len)
{
	CRefFile * pp = FindFile(path);	JCASSERT(pp);
	pp->m_checksum = checksum;
	pp->m_size = boost::numeric_cast<int>(len);
}

void CReferenceFs::UpdateFile(CRefFile & file, DWORD checksum, size_t len)
{
	file.m_checksum = checksum;
	file.m_size = boost::numeric_cast<int>(len);
}

const CReferenceFs::CRefFile & CReferenceFs::GetFile(CReferenceFs::CONST_ITERATOR & it) const
{
	return *(m_files+it->second);
}

bool CReferenceFs::IsDir(const CRefFile & file) const
{
	return (file.m_size < 0);
	//UINT64 val = file.second;
	//if ((INT64)val < 0) return true;
	//else return false;
//	return file.second == (UINT64)-1;
}

CReferenceFs::CRefFile * CReferenceFs::FindFile(const std::wstring & path)
{
	auto it = m_ref.find(path);
	if (it == m_ref.end()) return NULL;
	return m_files + it->second;
//	CReferenceFs::CRefFile * file = (*it);
	//return &(CRefFile)(*it);
}

void CReferenceFs::RemoveFile(const std::wstring & path)
{
	LOG_DEBUG(L"this = 0x%08p", this);
	LOG_DEBUG(L"remove dir/file %s", path.c_str());
	UINT obj_id = FindFileIndex(path);
	if (obj_id != (UINT)(-1))
	{
		CRefFile * obj = m_files + obj_id;
		memset(obj, 0, sizeof(CRefFile));
		obj->m_checksum = m_free_list;
		m_free_list = obj_id;
		m_free_num++;
		size_t erased = m_ref.erase(path);
		if (erased > 0)
		{
			// 更新父节点中子节点数量
			size_t pos = path.find_last_of(L"\\");
			std::wstring parent;
			if (pos > 0) parent = std::wstring(path.c_str(), pos);
			else parent = L"\\";
			CRefFile * pp = FindFile(parent);	JCASSERT(pp);
			pp->m_checksum--;
		}

	}
}

UINT CReferenceFs::FindFileIndex(const std::wstring & path)
{
	auto it = m_ref.find(path);
	if (it == m_ref.end()) return -1;
	return it->second;
}

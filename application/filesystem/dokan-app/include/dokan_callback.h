#pragma once

#include "../../dokanfs-lib/include/ifilesystem.h"
#include <dokan.h>

int StartDokan(IFileSystem* fs, const std::wstring & mount);
int StartDokanAsync(IFileSystem* fs, const std::wstring& mount);

class CDokanFindFileCallback : public EnumFileListener
{
public:
	CDokanFindFileCallback(PFillFindData fill_data, PDOKAN_FILE_INFO info);
	~CDokanFindFileCallback(void);
public:
	//virtual bool EnumFileCallback(const wchar_t * fn, size_t fn_len, 
	//	UINT32 ino, UINT32 entry, // entry 在父目录中的位置
	//	BY_HANDLE_FILE_INFORMATION * finfo);
	virtual bool EnumFileCallback(const std::wstring & fn, 
		UINT32 ino, UINT32 entry, // entry 在父目录中的位置
		BY_HANDLE_FILE_INFORMATION * finfo);
protected:
	PFillFindData	m_fill_data;
	IFileSystem *	m_fs;
	PDOKAN_FILE_INFO m_file_info;
};
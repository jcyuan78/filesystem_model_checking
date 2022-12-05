///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// ==== health info file ====
#include "pch.h"
#include "../include/f2fs-filesystem.h"

LOCAL_LOGGER_ENABLE(L"f2fs.health", LOGGER_LEVEL_DEBUGINFO);


CF2fsSpecialFile::~CF2fsSpecialFile(void)
{
}

bool CF2fsSpecialFile::DokanReadFile(LPVOID buf, DWORD len, DWORD& read, LONGLONG offset)
{
	read = m_fs->GetSpecialData(buf, len, m_fid);
	//	DWORD cp_size = min(len, (DWORD)m_da ta_size);
	//memcpy_s(buf, len, m_data_ptr, cp_size);
	//read = cp_size;
	return true;
}

bool CF2fsSpecialFile::GetFileInformation(LPBY_HANDLE_FILE_INFORMATION fileinfo) const
{
	if (fileinfo)
	{
		fileinfo->dwFileAttributes = FILE_ATTRIBUTE_HIDDEN | FILE_ATTRIBUTE_READONLY | FILE_ATTRIBUTE_SYSTEM;
		fileinfo->dwVolumeSerialNumber = 0;
		fileinfo->nFileIndexHigh = 0xFFFFFFFF;
		fileinfo->nFileIndexLow = m_fid;
		fileinfo->nFileSizeHigh = 0;
		fileinfo->nFileSizeLow = m_data_size;
		fileinfo->nNumberOfLinks = 1;
	}
	return true;
}

std::wstring CF2fsSpecialFile::GetFileName(void) const
{
	return m_fn;
}


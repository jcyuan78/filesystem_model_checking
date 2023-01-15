///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
#include "pch.h"
#include "../include/actual_fs.h"

LOCAL_LOGGER_ENABLE(L"actual_fs", LOGGER_LEVEL_DEBUGINFO);

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// == File Info ==

bool CActualFileInfo::DokanReadFile(LPVOID buf, DWORD len, DWORD& read, LONGLONG offset)
{
	LARGE_INTEGER pos, new_pos;
	pos.QuadPart = offset;
	BOOL br = SetFilePointerEx(m_file, pos, &new_pos, FILE_BEGIN);
	if (!br)
	{
		LOG_WIN32_ERROR(L"failed on setting file pointer, pos=%lld", offset);
		return false;
	}

	br = ReadFile(m_file, buf, len, &read, nullptr);
	if (!br)
	{
		LOG_WIN32_ERROR(L"failed on reading file, len=%d", len);
		return false;
	}
	return true;
}

bool CActualFileInfo::DokanWriteFile(const void* buf, DWORD len, DWORD& written, LONGLONG offset)
{
	LARGE_INTEGER pos, new_pos;
	pos.QuadPart = offset;
	BOOL br = SetFilePointerEx(m_file, pos, &new_pos, FILE_BEGIN);
	if (!br)
	{
		LOG_WIN32_ERROR(L"failed on setting file pointer, pos=%lld", offset);
		return false;
	}

	br = WriteFile(m_file, buf, len, &written, nullptr);
	if (!br)
	{
		LOG_WIN32_ERROR(L"failed on writing file, len=%d", len);
		return false;
	}
	return true;
}

bool CActualFileInfo::GetFileInformation(LPBY_HANDLE_FILE_INFORMATION fileinfo) const
{
	BOOL br =GetFileInformationByHandle(m_file, fileinfo);
	return br!=FALSE;
}

bool CActualFileInfo::SetEndOfFile(LONGLONG size)
{
	LARGE_INTEGER pos, new_pos;
	pos.QuadPart = size;
	BOOL br = SetFilePointerEx(m_file, pos, &new_pos, FILE_BEGIN);
	if (!br)
	{
		LOG_WIN32_ERROR(L"failed on setting file pointer, pos=%lld", size);
		return false;
	}
	br = ::SetEndOfFile(m_file);
	if (!br)
	{
		LOG_WIN32_ERROR(L"failed on setting end of file");
		return false;
	}
	return true;
}

bool CActualFileInfo::FlushFile(void)
{
	BOOL br = ::FlushFileBuffers(m_file);
	if (!br)
	{
		LOG_WIN32_ERROR(L"failed on flush file");
		return false;
	}
	return true;
}


///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// == File System ==

bool CActualFileSystem::DokanGetDiskSpace(ULONGLONG& free_bytes, ULONGLONG& total_bytes, ULONGLONG& total_free_bytes)
{
	ULARGE_INTEGER free, total, total_free;
	BOOL br= GetDiskFreeSpaceEx(m_root.c_str(), &free, &total, &total_free);
	if (!br) 
	{
		LOG_WIN32_ERROR(L"failed on getting disk space, fs=%s", m_root.c_str());
		return false;
	}
	free_bytes = free.QuadPart;
	total_bytes = total.QuadPart;
	total_free_bytes = total_free.QuadPart;
	return true;
}

bool CActualFileSystem::GetVolumnInfo(std::wstring& vol_name, DWORD& sn, DWORD& max_fn_len, DWORD& fs_flag, std::wstring& fs_name)
{
	vol_name.resize(MAX_PATH);
	fs_name.resize(MAX_PATH);
	BOOL br = GetVolumeInformation(m_root.c_str(),
		const_cast<wchar_t*>(vol_name.data()), MAX_PATH, &sn, &max_fn_len, &fs_flag,
		const_cast<wchar_t*>(fs_name.data()), MAX_PATH);
	if (!br)
	{
		LOG_WIN32_ERROR(L"failed on getting volume info, fs=%s", m_root.c_str());
		return false;
	}
	return true;
}

NTSTATUS CActualFileSystem::DokanCreateFile(IFileInfo*& file, const std::wstring& fn, ACCESS_MASK access_mask, DWORD attr, FsCreateDisposition disp, ULONG share, ULONG opt, bool isdir)
{
	if (isdir) THROW_ERROR(ERR_APP, L"unsopport, using MakeDir()");
	DWORD dispo;
	switch (disp)
	{
	case FS_CREATE_NEW: dispo = CREATE_NEW; break;
	case FS_CREATE_ALWAYS: dispo = CREATE_ALWAYS; break;
	case FS_OPEN_EXISTING: dispo = OPEN_EXISTING;break;
	case FS_OPEN_ALWAYS: dispo = OPEN_ALWAYS;break;
	case FS_TRUNCATE_EXISTING: dispo = TRUNCATE_EXISTING;break;
	//case FS_: dispo = ;break;
	default: THROW_ERROR(ERR_APP, L"unknown dispo=%d", disp);
	}
	HANDLE ff = CreateFile(fn.c_str(), access_mask, share, nullptr, dispo, attr, nullptr);
	if (ff == INVALID_HANDLE_VALUE)
	{
		LOG_WIN32_ERROR(L"failed on creating file= %", fn.c_str());
		return STATUS_UNSUCCESSFUL;
	}

	CActualFileInfo* finfo = jcvos::CDynamicInstance<CActualFileInfo>::Create();
	finfo->m_file = ff;
	finfo->m_fn = fn;
	file = static_cast<CActualFileInfo*>(finfo);

	return STATUS_SUCCESS;
}


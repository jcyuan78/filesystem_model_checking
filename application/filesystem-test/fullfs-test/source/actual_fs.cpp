///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
#include "pch.h"
#include "../include/actual_fs.h"

LOCAL_LOGGER_ENABLE(L"actual_fs", LOGGER_LEVEL_DEBUGINFO);

#define BLOCK_SIZE (4096)

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// == File Info ==

CActualFileInfo::CActualFileInfo(void)
{
	InitializeCriticalSection(&m_file_lock);
}

CActualFileInfo::~CActualFileInfo(void)
{
	DeleteCriticalSection(&m_file_lock);
}

void CActualFileInfo::CloseFile(void)
{
	CloseHandle(m_file);
	m_file = nullptr;
}

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
	size_t end = offset + len;
	if (end > m_size)	{	SetEndOfFile(end);	}
	size_t start_blk = offset / BLOCK_SIZE;
	size_t end_blk = ROUND_UP_DIV(end, BLOCK_SIZE);
	for (size_t bb = start_blk; bb < end_blk; ++bb)
	{
		InterlockedIncrement((LONG*)(&m_blk_access[bb]));
	}

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
//	written = len;

	return true;
}

bool CActualFileInfo::GetFileInformation(LPBY_HANDLE_FILE_INFORMATION fileinfo) const
{
	BOOL br =GetFileInformationByHandle(m_file, fileinfo);
	return br!=FALSE;
}

bool CActualFileInfo::SetEndOfFile(LONGLONG size)
{
	size_t new_blk_nr = ROUND_UP_DIV(size, BLOCK_SIZE);
	EnterCriticalSection(&m_file_lock);
	if (new_blk_nr > m_blk_nr)
	{
		m_blk_access.resize(new_blk_nr);
		m_blk_nr = new_blk_nr;
	}
	m_size = size;
	LeaveCriticalSection(&m_file_lock);

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

CActualFileSystem::CActualFileSystem(void)
{
	InitializeCriticalSection(&m_fs_lock);
	m_last_fid = 1;
}

CActualFileSystem::~CActualFileSystem(void)
{
	for (auto it = m_files.begin(); it != m_files.end(); ++it)
	{
		it->second->Release();
		it->second = nullptr;
	}
	DeleteCriticalSection(&m_fs_lock);

}

void CActualFileSystem::Unmount(void)
{
	// 统计信息：
	size_t logica_saturation = 0;	// 逻辑饱和度：有效的（写过的）block数量
	size_t physical_saturation = 0;	// 物理饱和度：总写入量（host write）
	size_t no_mapping_blk = 0;		// 从未被写过的block数量
	size_t cold_blk_nr = 0;			// 仅写如果1次的block数量
	size_t hot_blk_nr = 0;			// 多次改写block数量
	// 输出结果
	FILE* outf = nullptr;
	_wfopen_s(&outf, L"trace_summary.csv", L"w+");
	fprintf_s(outf, "fid,blk_id,write_count,fn\n");
	EnterCriticalSection(&m_fs_lock);

	for (auto it = m_files.begin(); it != m_files.end(); ++it)
	{
		CActualFileInfo* file = it->second;
		size_t blk_id = 0;
		for (auto ff = file->m_blk_access.begin(); ff != file->m_blk_access.end(); ++ff, ++blk_id)
		{
			fprintf_s(outf, "%d,%lld,%d,%S\n", file->m_fid, blk_id, *ff, file->m_fn.c_str());
		}
	}

	LeaveCriticalSection(&m_fs_lock);
	fclose(outf);
}

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
	HANDLE fhandle = CreateFile(fn.c_str(), access_mask, share, nullptr, dispo, attr, nullptr);
	if (fhandle == INVALID_HANDLE_VALUE)
	{
		LOG_WIN32_ERROR(L"failed on creating file= %", fn.c_str());
		return STATUS_UNSUCCESSFUL;
	}

	//CActualFileInfo* finfo = jcvos::CDynamicInstance<CActualFileInfo>::Create();
	//finfo->m_file = ff;
	//finfo->m_fn = fn;
	//file = static_cast<CActualFileInfo*>(finfo);

	EnterCriticalSection(&m_fs_lock);
	auto it = m_files.find(fn);
	if (it != m_files.end())
	{
		it->second->m_file = fhandle;
		file = static_cast<IFileInfo*>(it->second);
	}
	else
	{
		CActualFileInfo* ff = jcvos::CDynamicInstance<CActualFileInfo>::Create();
		if (ff == nullptr) THROW_ERROR(ERR_MEM, L"failed on creating file");
		ff->m_fn = fn;
		ff->m_fid = m_last_fid++;
		ff->m_dir = isdir;
		//if (ff->m_file == nullptr) ff->m_file = fhandle;
		ff->m_file = fhandle;
		m_files.insert(std::make_pair(fn, ff));
		file = static_cast<IFileInfo*>(ff);
	}
	LeaveCriticalSection(&m_fs_lock);
	file->AddRef();

	return STATUS_SUCCESS;
}

bool CActualFileSystem::MakeDir(const std::wstring& dir)
{
	BOOL br = CreateDirectory(dir.c_str(), nullptr);
	if (!br) return false;

	EnterCriticalSection(&m_fs_lock);
	auto it = m_files.find(dir);
	if (it != m_files.end())
	{	// 目录以存在
		LeaveCriticalSection(&m_fs_lock);
		return false;
	}
	else
	{
		CActualFileInfo* ff = jcvos::CDynamicInstance<CActualFileInfo>::Create();
		if (ff == nullptr) THROW_ERROR(ERR_MEM, L"failed on creating file");
		ff->m_fn = dir;
		ff->m_fid = m_last_fid++;
		ff->m_dir = true;
		m_files.insert(std::make_pair(dir, ff));
	}
	LeaveCriticalSection(&m_fs_lock);

	return true;
}


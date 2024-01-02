///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
#include "pch.h"
#include "../include/statistic_fs.h"

#define BLOCK_SIZE (4096)

CStatisticFileSystem::CStatisticFileSystem(void)
{
	InitializeCriticalSection(&m_fs_lock);
	m_last_fid = 1;
}

CStatisticFileSystem::~CStatisticFileSystem(void)
{
	for (auto it = m_files.begin(); it != m_files.end(); ++it)
	{
		it->second->Release();
		it->second = nullptr;
	}
	DeleteCriticalSection(&m_fs_lock);
}

bool CStatisticFileSystem::Mount(IVirtualDisk* dev)
{
	return true;
}

void CStatisticFileSystem::Unmount(void)
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
		CStatisticFileInfo* file = it->second;
		size_t blk_id = 0;
		for (auto ff = file->m_blk_access.begin(); ff != file->m_blk_access.end(); ++ff, ++blk_id)
		{
			fprintf_s(outf, "%d,%lld,%d,%S\n", file->m_fid, blk_id, *ff, file->m_fn.c_str());
		}
	}

	LeaveCriticalSection(&m_fs_lock);
	fclose(outf);
}

NTSTATUS CStatisticFileSystem::DokanCreateFile(IFileInfo*& file, const std::wstring& fn, ACCESS_MASK access_mask, DWORD attr, FsCreateDisposition disp, ULONG share, ULONG opt, bool isdir)
{
	EnterCriticalSection(&m_fs_lock);
	auto it = m_files.find(fn);
	if (it != m_files.end())
	{
		file = static_cast<IFileInfo*>(it->second);
	}
	else
	{
		CStatisticFileInfo* ff = jcvos::CDynamicInstance<CStatisticFileInfo>::Create();
		if (ff == nullptr) THROW_ERROR(ERR_MEM, L"failed on creating file");
		ff->m_fn = fn;
		ff->m_fid = m_last_fid++;
		ff->m_dir = isdir;
		m_files.insert(std::make_pair(fn, ff));
		file = static_cast<IFileInfo*>(ff);
	}
	LeaveCriticalSection(&m_fs_lock);
	file->AddRef();

	return STATUS_SUCCESS;
}

bool CStatisticFileSystem::MakeDir(const std::wstring& dir)
{
	EnterCriticalSection(&m_fs_lock);
	auto it = m_files.find(dir);
	if (it != m_files.end())
	{	// 目录以存在
		LeaveCriticalSection(&m_fs_lock);
		return false;
	}
	else
	{
		CStatisticFileInfo* ff = jcvos::CDynamicInstance<CStatisticFileInfo>::Create();
		if (ff == nullptr) THROW_ERROR(ERR_MEM, L"failed on creating file");
		ff->m_fn = dir;
		ff->m_fid = m_last_fid++;
		ff->m_dir = true;
		m_files.insert(std::make_pair(dir, ff));
	}
	LeaveCriticalSection(&m_fs_lock);
	return true;
}

NTSTATUS CStatisticFileSystem::DokanDeleteFile(const std::wstring& fn, IFileInfo* file, bool isdir)
{
	EnterCriticalSection(&m_fs_lock);
	auto it = m_files.find(fn);
	if (it == m_files.end())
	{	// 文件、目录不存在
		LeaveCriticalSection(&m_fs_lock);
		return STATUS_UNSUCCESSFUL;
	}
	RELEASE(it->second);
	m_files.erase(it);
	LeaveCriticalSection(&m_fs_lock);
	return STATUS_SUCCESS;
}


///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

CStatisticFileInfo::CStatisticFileInfo(void)
{
	InitializeCriticalSection(&m_file_lock);
}

CStatisticFileInfo::~CStatisticFileInfo(void)
{
	DeleteCriticalSection(&m_file_lock);
}

void CStatisticFileInfo::CloseFile(void)
{
}

bool CStatisticFileInfo::DokanReadFile(LPVOID buf, DWORD len, DWORD& read, LONGLONG offset)
{
	read = len;
	return true;
}

bool CStatisticFileInfo::DokanWriteFile(const void* buf, DWORD len, DWORD& written, LONGLONG offset)
{
	size_t end = offset + len;
	if (end > m_size) SetEndOfFile(end);
	size_t start_blk = offset / BLOCK_SIZE;
	size_t end_blk = ROUND_UP_DIV(end, BLOCK_SIZE);
	for (size_t bb = start_blk; bb < end_blk; ++bb)
	{
		m_blk_access[bb]++;
	}
	written = len;
	return true;
}

bool CStatisticFileInfo::SetEndOfFile(LONGLONG new_size)
{
	size_t new_blk_nr = ROUND_UP_DIV(new_size, BLOCK_SIZE);
	LockFile(0, -1);
	if (new_blk_nr > m_blk_nr)
	{
		m_blk_access.resize(new_blk_nr);
		m_blk_nr = new_blk_nr;
	}
	m_size = new_size;
	UnlockFile(0,-1);
	return true;
}

bool CStatisticFileInfo::FlushFile(void)
{
	return true;
}

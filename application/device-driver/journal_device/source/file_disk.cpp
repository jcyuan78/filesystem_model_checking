///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
#include "stdafx.h"
#include "..\include\file_disk.h"

LOCAL_LOGGER_ENABLE(L"disk.file", LOGGER_LEVEL_NOTICE);

CFileDiskBase::CFileDiskBase(void)
{
}

CFileDiskBase::~CFileDiskBase(void)
{
	if (m_log_file)		CloseHandle(m_log_file);
	if (m_data_file)	CloseHandle(m_data_file);
}

bool CFileDiskBase::CreateFileImage(size_t secs, size_t journal_size, size_t log_buf, bool read_only)
{
	LOG_STACK_TRACE();
	std::wstring img_fn = m_dev_path + L".img";

	DWORD flag = FILE_FLAG_RANDOM_ACCESS | FILE_FLAG_NO_BUFFERING;
	if (m_async_io) flag |= FILE_FLAG_OVERLAPPED;

	m_data_file = CreateFileW(img_fn.c_str(), GENERIC_ALL, FILE_SHARE_READ, NULL, OPEN_ALWAYS, flag, NULL);
	if (m_data_file == INVALID_HANDLE_VALUE) THROW_WIN32_ERROR(L"failed on open %s", m_dev_path.c_str());

	m_capacity = secs * m_sector_size;
	LARGE_INTEGER size;
	GetFileSizeEx(m_data_file, &size);
	if (size.QuadPart < m_capacity)
	{
		size.QuadPart = m_capacity;
		SetFilePointerEx(m_data_file, size, nullptr, FILE_BEGIN);
		SetEndOfFile(m_data_file);
	}

#if 0
	// open journal file
	if (m_journal_enable)
	{
		std::wstring log_fn = fn + L".journal";
		m_log_file = CreateFileW(log_fn.c_str(), GENERIC_ALL, FILE_SHARE_READ, NULL, OPEN_ALWAYS, 0, NULL);
		if (m_log_file == INVALID_HANDLE_VALUE) THROW_WIN32_ERROR(L"failed on open %s", fn.c_str());

		//_wfopen_s(&m_log_file, log_fn.c_str(), L"r+");
		//if (!m_log_file)
		//{
		//	_wfopen_s(&m_log_file, log_fn.c_str(), L"w+");
		//	if (!m_log_file) THROW_WIN32_ERROR(L"failed on open log file %s", log_fn.c_str());
		//}

		//m_data_file = CreateFileW(data_fn.c_str(),
		//	GENERIC_READ | GENERIC_WRITE,
		//	FILE_SHARE_READ, NULL,
		//	OPEN_ALWAYS, 0, NULL);
		//if (m_data_file == INVALID_HANDLE_VALUE) THROW_WIN32_ERROR(L"failed on open %s", data_fn.c_str());
		//		if (load_journal) LoadJournal(m_log_file, m_data_file, 0xFFFFFFFF);
	}
#endif
	return true;
}

bool CFileDiskBase::CreateDriveImage(size_t secs, const std::wstring journal_file, size_t journal_size, size_t log_buf)
{
	m_capacity = secs * m_sector_size;

	if (m_async_io)
	{
		m_data_file = CreateFile(m_dev_path.c_str(), GENERIC_ALL, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, FILE_FLAG_NO_BUFFERING | FILE_FLAG_OVERLAPPED, nullptr);
	}
	else
	{
		m_data_file = CreateFile(m_dev_path.c_str(), GENERIC_ALL, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, FILE_FLAG_NO_BUFFERING, nullptr);

	}
	// <TODO> 检查磁盘大小
	if (m_data_file == INVALID_HANDLE_VALUE) THROW_WIN32_ERROR(L"failed on open drive %s", m_dev_path.c_str());
	return true;
}


bool CFileDiskBase::InitializeDevice(const boost::property_tree::wptree& config)
{
	m_sector_size = config.get<UINT>(L"sector_size", 512);
	std::wstring str_type = config.get<std::wstring>(L"type", L"file");
	size_t secs = config.get<size_t>(L"sectors");
//	m_async_io = config.get<bool>(L"async_io", false);

	if (str_type == L"file")
	{
		m_type = FILE_DRIVE;
		m_dev_path = config.get<std::wstring>(L"image_file");
		m_journal_enable = config.get<bool>(L"enable_journal");
		//	CJournalDevice* _dev = jcvos::CDynamicInstance<CJournalDevice>::Create();
		size_t log_cap = 0, log_buf = 0;
		if (m_journal_enable)
		{
			log_cap = config.get<size_t>(L"log_capacity");
			log_buf = log_cap * 16;
		}
		CreateFileImage(secs, log_cap, log_buf);

	}
	else if (str_type == L"logical_drive")
	{
		m_type = LOGICAL_DRIVE;
		m_sector_size = 512;
		const std::wstring& str_path = config.get<std::wstring>(L"path");
		m_dev_path = L"\\\\?\\" + str_path;
		CreateDriveImage(secs, L"", 0, 0);
	}
	else if (str_type == L"physical_drive")
	{
		LOG_ERROR(L"unsupport drive type: %s", str_type.c_str());
		return false;
	}
	else
	{
		LOG_ERROR(L"unknown drive type: %s", str_type.c_str());
		return false;
	}

	return true;
}


///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// == 同步版本 ==

CFileDiskSync::CFileDiskSync(void)
{
	m_journal_id = 0;
	m_async_io = false;
}

CFileDiskSync::~CFileDiskSync(void)
{
	//	RELEASE(m_file);
}


//bool CFileDiskSync::LoadJournal(FILE* journal, HANDLE data, size_t steps)
//{
//	LOG_STACK_TRACE();
//	JCASSERT(journal && data && (data != INVALID_HANDLE_VALUE));
//	// replay jornal
//	while (m_journal_id < steps)
//	{
//		if (feof(journal)) break;
//		size_t id, lba, secs, file_offset;
//		int ir = fscanf_s(journal, "%zd,WRT,%zX,%zX,%zX\n", &id, &lba, &secs, &file_offset);
//		if (ir <= 0) break;
//		size_t offset = lba * m_sector_size;
//		DWORD read = 0;
//		ReadFile(data, m_buf + offset, (DWORD)(secs * m_sector_size), &read, NULL);
//		LOG_DEBUG(L"load journal %d, read=%d", id, read);
//		m_journal_id++;
//	}
//	return false;
//}

//bool CFileDiskSync::LoadJournal(const std::wstring& fn, size_t steps)
//{
//	LOG_STACK_TRACE();
//	std::wstring log_fn = fn + L".log";
//	std::wstring data_fn = fn + L".bin";
//
//	_wfopen_s(&m_log_file, log_fn.c_str(), L"r+");
//	if (!m_log_file)
//	{
//		_wfopen_s(&m_log_file, log_fn.c_str(), L"w+");
//		if (!m_log_file) THROW_WIN32_ERROR(L"failed on open log file %s", log_fn.c_str());
//	}
//
//	m_data_file = CreateFileW(data_fn.c_str(),
//		GENERIC_READ | GENERIC_WRITE,
//		FILE_SHARE_READ, NULL,
//		OPEN_ALWAYS, 0, NULL);
//	if (m_data_file == INVALID_HANDLE_VALUE) THROW_WIN32_ERROR(L"failed on open %s", data_fn.c_str());
//	LoadJournal(m_log_file, m_data_file, steps);
//	return true;
//}

//bool CFileDiskSync::SaveSnapshot(const std::wstring& fn)
//{
//	HANDLE file = CreateFileW(fn.c_str(),
//		GENERIC_READ | GENERIC_WRITE,
//		FILE_SHARE_READ, NULL,
//		OPEN_ALWAYS, FILE_FLAG_RANDOM_ACCESS, NULL);
//	if (file == INVALID_HANDLE_VALUE) THROW_WIN32_ERROR(L"failed on open snapshot %s", fn.c_str());
//	DWORD written = 0;
//	WriteFile(file, m_buf, (DWORD)m_capacity, &written, NULL);
//	LOG_DEBUG(L"write snapshot = %d", written);
//	CloseHandle(file);
//	return true;
//}



//bool CFileDiskSync::BackLog(size_t num)
//{	// 重新播放log
//	if (num > m_journal_id) THROW_ERROR(ERR_APP, L"back number (%d) is larger than log count (%d)", num, m_journal_id);
//	memcpy_s(m_buf, m_capacity, m_snapshot, m_capacity);
//	size_t playback_num = m_journal_id - num;
//	for (size_t ii = 0; ii < playback_num; ++ii)
//	{	// playback
//		//JCASSERT(m_sector_size);
//		//LOG_DEBUG(L"write lba=%08X, secs=%d", lba, secs);
//		//size_t offset = lba * m_sector_size;
//		//size_t len = secs * m_sector_size;
//		//JCASSERT((offset + len) <= m_capacity);
//		//memcpy_s(m_buf + offset, len, buf, len);
//		InternalWrite(m_logs[ii].data_ptr, m_logs[ii].lba, m_logs[ii].secs);
//	}
//	return true;
//}

//void CFileDiskSync::ResetLog(void)
//{	// save snapshot
//	memcpy_s(m_snapshot, m_capacity, m_buf, m_capacity);
//	// reset journal
//	m_journal_id = 0;
//	m_log_data_ptr = m_log_buf;
//}

void CFileDiskSync::InternalWrite(void* buf, size_t lba, size_t secs)
{
	JCASSERT(m_sector_size);
	//	LOG_DEBUG(L"write lba=%08X, secs=%d", lba, secs);
	size_t offset = lba * m_sector_size;
	size_t len = secs * m_sector_size;
	if ((offset + len) > m_capacity) THROW_ERROR(ERR_APP, L"write over capacity cap=%d, lba=%d, secs=%zd", BYTE_TO_SECTOR(m_capacity), lba, secs);
	LARGE_INTEGER _offset;
	_offset.QuadPart = offset;
	SetFilePointerEx(m_data_file, _offset, nullptr, FILE_BEGIN);
	WriteFile(m_data_file, buf, (DWORD)len, nullptr, nullptr);
	LOG_TRACK(L"disk", L"write  lba=%08X, secs=%d, offset=0x%llX", lba, secs, offset);
}

//bool CFileDiskSync::MakeSnapshot(IVirtualDisk*& dev, size_t steps)
//{
//	CFileDiskSync* _dev = jcvos::CDynamicInstance<CFileDiskSync>::Create();
//	_dev->CreateFileImage(m_dev_path, m_capacity / SECTOR_SIZE, false, true);
//	fseek(m_log_file, 0, SEEK_SET);
//	SetFilePointer(m_data_file, 0, NULL, FILE_BEGIN);
//	_dev->LoadJournal(m_log_file, m_data_file, steps);
//	SetFilePointer(m_data_file, 0, NULL, FILE_END);
//	fseek(m_log_file, 0, SEEK_END);
//	dev = static_cast<IVirtualDisk*>(_dev);
//	return true;
//}

//size_t CFileDiskSync::GetSteps(void) const
//{
//	return m_journal_id;
//}


bool CFileDiskSync::ReadSectors(void* buf, size_t lba, size_t secs)
{
	JCASSERT(m_sector_size);
	// 以sector为单位:
	size_t offset = lba * m_sector_size;
	size_t len = secs * m_sector_size;
	JCASSERT((offset + len) <= m_capacity);
	LARGE_INTEGER _offset;
	_offset.QuadPart = offset;
	SetFilePointerEx(m_data_file, _offset, nullptr, FILE_BEGIN);
	ReadFile(m_data_file, buf, (DWORD)len, nullptr, nullptr);
	
	LOG_TRACK(L"disk", L"read  lba=%08X, secs=%d, offset=0x%llX", lba, secs, offset);
	m_host_read += secs;
	return true;
}


bool CFileDiskSync::WriteSectors(void* buf, size_t lba, size_t secs)
{
	size_t len = secs * m_sector_size;
	InternalWrite(buf, lba, secs);

	if (m_journal_enable)
	{
		static const int buf_len = 64;
		char log_buf[buf_len];
		// write journal
		//if (m_journal_id >= m_log_capacity) THROW_ERROR(ERR_APP, L"log is full");
		//if ((m_log_data_ptr - m_log_buf) + len > SECTOR_TO_BYTE(m_log_buf_secs)) THROW_ERROR(ERR_APP, L"log buffer is full");

		//m_logs[m_journal_id].lba = boost::numeric_cast<UINT>(lba);
		//m_logs[m_journal_id].secs = boost::numeric_cast<UINT>(secs);
		//m_logs[m_journal_id].data_ptr = m_log_data_ptr;
		//memcpy_s(m_log_data_ptr, len, buf, len);
		//m_log_data_ptr += len;

		// log_id, cmd, lba, secs, offset_in_file
//		fprintf_s(m_log_file, "%04zd,WRT,%08zX,%04zX,%08zX\n", m_journal_id, lba, secs, m_data_offset);
		int str_len = sprintf_s(log_buf, "%04zd,WRT,%08zX,%04zX\n", m_journal_id, lba, secs);
		DWORD written = 0;
		WriteFile(m_log_file, log_buf, str_len, &written, nullptr);
		//<TODO>处理当le超过32位时的情况。
		m_journal_id++;
	}

	m_host_write += secs;
	return true;
}

bool CFileDiskSync::AsyncWriteSectors(void* buf, size_t secs, OVERLAPPED* overlap)
{
	size_t len = secs * m_sector_size;
	DWORD written = 0;
	LARGE_INTEGER _offset;
	_offset.LowPart = overlap->Offset;
	_offset.HighPart = overlap->OffsetHigh;
	SetFilePointerEx(m_data_file, _offset, nullptr, FILE_BEGIN);
	WriteFile(m_data_file, buf, (DWORD)len, &written, nullptr);
	SetEvent(overlap->hEvent);
	return true;
}

bool CFileDiskSync::AsyncReadSectors(void* buf, size_t secs, OVERLAPPED* overlap)
{
	size_t len = secs * m_sector_size;
	DWORD written = 0;
	LARGE_INTEGER _offset;
	_offset.LowPart = overlap->Offset;
	_offset.HighPart = overlap->OffsetHigh;
	SetFilePointerEx(m_data_file, _offset, nullptr, FILE_BEGIN);
	ReadFile(m_data_file, buf, (DWORD)len, &written, nullptr);
	SetEvent(overlap->hEvent);
	return true;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// == 异步版本 ==

CFileDiskAsync::CFileDiskAsync(void)
{
	m_journal_id = 0;
	m_async_io = true;
}

CFileDiskAsync::~CFileDiskAsync(void)
{
}

//bool CFileDiskAsync::BackLog(size_t num)
//{	// 重新播放log
//	if (num > m_journal_id) THROW_ERROR(ERR_APP, L"back number (%d) is larger than log count (%d)", num, m_journal_id);
//	memcpy_s(m_buf, m_capacity, m_snapshot, m_capacity);
//	size_t playback_num = m_journal_id - num;
//	for (size_t ii = 0; ii < playback_num; ++ii)
//	{	// playback
//		//JCASSERT(m_sector_size);
//		//LOG_DEBUG(L"write lba=%08X, secs=%d", lba, secs);
//		//size_t offset = lba * m_sector_size;
//		//size_t len = secs * m_sector_size;
//		//JCASSERT((offset + len) <= m_capacity);
//		//memcpy_s(m_buf + offset, len, buf, len);
//		InternalWrite(m_logs[ii].data_ptr, m_logs[ii].lba, m_logs[ii].secs);
//	}
//	return true;
//}

//void CFileDiskAsync::ResetLog(void)
//{	// save snapshot
//	memcpy_s(m_snapshot, m_capacity, m_buf, m_capacity);
//	// reset journal
//	m_journal_id = 0;
//	m_log_data_ptr = m_log_buf;
//}

void CFileDiskAsync::InternalWrite(void* buf, size_t lba, size_t secs)
{
	JCASSERT(m_sector_size);
	//	LOG_DEBUG(L"write lba=%08X, secs=%d", lba, secs);
	size_t offset = lba * m_sector_size;
	size_t len = secs * m_sector_size;
	if ((offset + len) > m_capacity) THROW_ERROR(ERR_APP, L"write over capacity cap=%d, lba=%d, secs=%zd", BYTE_TO_SECTOR(m_capacity), lba, secs);
	// 在异步条件下执行同步write
	OVERLAPPED ol;
	ol.Pointer = (PVOID)offset;
	ol.hEvent = CreateEvent(nullptr, TRUE, FALSE, nullptr);
	WriteFile(m_data_file, buf, (DWORD)len, nullptr, &ol);
	WaitForSingleObject(ol.hEvent, INFINITE);
	CloseHandle(ol.hEvent);

	LOG_TRACK(L"disk", L"write  lba=%08X, secs=%d, offset=0x%llX", lba, secs, offset);
}

//bool CFileDiskAsync::MakeSnapshot(IVirtualDisk*& dev, size_t steps)
//{
//	CFileDiskAsync* _dev = jcvos::CDynamicInstance<CFileDiskAsync>::Create();
//	_dev->CreateFileImage(m_dev_path, m_capacity / SECTOR_SIZE, false, true);
//	fseek(m_log_file, 0, SEEK_SET);
//	SetFilePointer(m_data_file, 0, NULL, FILE_BEGIN);
//	_dev->LoadJournal(m_log_file, m_data_file, steps);
//	SetFilePointer(m_data_file, 0, NULL, FILE_END);
//	fseek(m_log_file, 0, SEEK_END);
//	dev = static_cast<IVirtualDisk*>(_dev);
//	return true;
//}

bool CFileDiskAsync::ReadSectors(void* buf, size_t lba, size_t secs)
{
	JCASSERT(m_sector_size);
	// 以sector为单位:
	size_t offset = lba * m_sector_size;
	size_t len = secs * m_sector_size;
	JCASSERT((offset + len) <= m_capacity);
	// 在异步条件下执行同步write
	OVERLAPPED ol;
	ol.Pointer = (PVOID)offset;
	ol.hEvent = CreateEvent(nullptr, TRUE, FALSE, nullptr);
	ReadFile(m_data_file, buf, (DWORD)len, nullptr, &ol);
	WaitForSingleObject(ol.hEvent, INFINITE);
	CloseHandle(ol.hEvent);

	LOG_TRACK(L"disk", L"read  lba=%08X, secs=%d, offset=0x%llX", lba, secs, offset);
	m_host_read += secs;
	return true;
}


bool CFileDiskAsync::WriteSectors(void* buf, size_t lba, size_t secs)
{
	size_t len = secs * m_sector_size;
	InternalWrite(buf, lba, secs);

	if (m_journal_enable)
	{
		static const int buf_len = 64;
		char log_buf[buf_len];
		// write journal
		//if (m_journal_id >= m_log_capacity) THROW_ERROR(ERR_APP, L"log is full");
		//if ((m_log_data_ptr - m_log_buf) + len > SECTOR_TO_BYTE(m_log_buf_secs)) THROW_ERROR(ERR_APP, L"log buffer is full");

		//m_logs[m_journal_id].lba = boost::numeric_cast<UINT>(lba);
		//m_logs[m_journal_id].secs = boost::numeric_cast<UINT>(secs);
		//m_logs[m_journal_id].data_ptr = m_log_data_ptr;
		//memcpy_s(m_log_data_ptr, len, buf, len);
		//m_log_data_ptr += len;

		// log_id, cmd, lba, secs, offset_in_file
//		fprintf_s(m_log_file, "%04zd,WRT,%08zX,%04zX,%08zX\n", m_journal_id, lba, secs, m_data_offset);
		int str_len = sprintf_s(log_buf, "%04zd,WRT,%08zX,%04zX\n", m_journal_id, lba, secs);
		DWORD written = 0;
		WriteFile(m_log_file, log_buf, str_len, &written, nullptr);
		//<TODO>处理当le超过32位时的情况。
		m_journal_id++;
	}

	m_host_write += secs;
	return true;
}

bool CFileDiskAsync::AsyncWriteSectors(void* buf, size_t secs, OVERLAPPED* overlap)
{
	size_t len = secs * m_sector_size;
	DWORD written = 0;
	BOOL br = WriteFile(m_data_file, buf, (DWORD)len, &written, overlap);
	DWORD err = GetLastError();
	LOG_TRACK(L"disk", L"async read, block=%X, size=%d, res=%d, err=%d", overlap->Offset, secs, br, err);
	if (br && err == 0) 
	{
		SetEvent(overlap->hEvent);
		return true;
	}
	if (err == ERROR_IO_PENDING) return true;
	return false;

}

bool CFileDiskAsync::AsyncReadSectors(void* buf, size_t secs, OVERLAPPED* overlap)
{
	size_t len = secs * m_sector_size;
	DWORD written = 0;
	BOOL br = ReadFile(m_data_file, buf, (DWORD)len, &written, overlap);
	DWORD err = GetLastError();
	LOG_TRACK(L"disk", L"async read, block=%X, size=%d, res=%d, err=%d", overlap->Offset, secs, br, err);
	if (br && err == 0) 
	{
		SetEvent(overlap->hEvent);
		return true;
	}
	if (err == ERROR_IO_PENDING) return true;
	return false;
}

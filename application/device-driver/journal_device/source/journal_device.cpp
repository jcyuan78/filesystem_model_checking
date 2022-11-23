#include "stdafx.h"
#include "../include/journal_device.h"
#include "..\include\journal_device.h"

LOCAL_LOGGER_ENABLE(L"fat.image", LOGGER_LEVEL_DEBUGINFO);


CJournalDevice::CJournalDevice(void)
	: m_buf(NULL), m_sector_size(512), m_log_file(NULL), m_data_file(NULL)
{
	m_journal_id = 0;
	m_data_offset = 0;
	m_logs = NULL;
	m_log_buf = NULL;
}

CJournalDevice::~CJournalDevice(void)
{
//	RELEASE(m_file);
	if (m_log_file)		fclose(m_log_file);
	if (m_data_file)	CloseHandle(m_data_file);
	delete[] m_buf;
	delete[] m_snapshot;

	delete[] m_logs;
	delete[] m_log_buf;
}

bool CJournalDevice::CreateFileImage(const std::wstring & fn, size_t secs, size_t journal_size, size_t log_buf, bool read_only)
{
	LOG_STACK_TRACE();
	m_dev_name = fn;
	std::wstring img_fn = fn+L".img";
	HANDLE file = CreateFileW(img_fn.c_str(), GENERIC_READ, FILE_SHARE_READ, NULL, 
		OPEN_ALWAYS, FILE_FLAG_RANDOM_ACCESS, NULL);
	if (file == INVALID_HANDLE_VALUE) THROW_WIN32_ERROR(L"failed on open %s", fn.c_str());

	m_capacity = secs * m_sector_size;
	m_buf = new BYTE[m_capacity];
	m_snapshot = new BYTE[m_capacity];
	DWORD read = 0;
	ReadFile(file, m_buf, (DWORD)m_capacity, &read, NULL);
	memcpy_s(m_snapshot, m_capacity, m_buf, m_capacity);
	LOG_DEBUG(L"read snapshot = %d", read);
	CloseHandle(file);

	// open journal file
	if (!read_only)
	{
		std::wstring log_fn = fn + L".journal";
		std::wstring data_fn = fn + L".bin";

		_wfopen_s(&m_log_file, log_fn.c_str(), L"r+");
		if (!m_log_file)
		{
			_wfopen_s(&m_log_file, log_fn.c_str(), L"w+");
			if (!m_log_file) THROW_WIN32_ERROR(L"failed on open log file %s", log_fn.c_str());
		}

		m_data_file = CreateFileW(data_fn.c_str(),
			GENERIC_READ | GENERIC_WRITE,
			FILE_SHARE_READ, NULL,
			OPEN_ALWAYS, 0, NULL);
		if (m_data_file == INVALID_HANDLE_VALUE) THROW_WIN32_ERROR(L"failed on open %s", data_fn.c_str());
//		if (load_journal) LoadJournal(m_log_file, m_data_file, 0xFFFFFFFF);
	}
	if (journal_size > 0)
	{
		m_log_capacity = journal_size;
		m_logs = new LOG_STRUCTURE[m_log_capacity];
		memset(m_logs, 0, m_log_capacity * sizeof(LOG_STRUCTURE));
		m_log_buf_secs = log_buf;
		m_log_buf = new BYTE[SECTOR_TO_BYTE(log_buf)];
		m_log_data_ptr = m_log_buf;
	}
	return true;
}

bool CJournalDevice::LoadJournal(FILE * journal, HANDLE data, size_t steps)
{
	LOG_STACK_TRACE();
	JCASSERT(journal && data && (data != INVALID_HANDLE_VALUE));
	// replay jornal
	while (m_journal_id < steps)
	{
		if (feof(journal)) break;
		size_t id, lba, secs, file_offset;
		int ir = fscanf_s(journal, "%zd,WRT,%zX,%zX,%zX\n", &id, &lba, &secs, &file_offset);
		if (ir <= 0) break;
		size_t offset = lba * m_sector_size;
		DWORD read = 0;
		ReadFile(data, m_buf + offset, (DWORD)(secs * m_sector_size), &read, NULL);
		LOG_DEBUG(L"load journal %d, read=%d", id, read);
		m_journal_id++;
	}
	return false;
}

bool CJournalDevice::LoadJournal(const std::wstring & fn, size_t steps)
{
	LOG_STACK_TRACE();
	std::wstring log_fn = fn + L".log";
	std::wstring data_fn = fn + L".bin";

	_wfopen_s(&m_log_file, log_fn.c_str(), L"r+");
	if (!m_log_file)
	{
		_wfopen_s(&m_log_file, log_fn.c_str(), L"w+");
		if (!m_log_file) THROW_WIN32_ERROR(L"failed on open log file %s", log_fn.c_str());
	}

	m_data_file = CreateFileW(data_fn.c_str(), 
		GENERIC_READ | GENERIC_WRITE, 
		FILE_SHARE_READ, NULL, 
		OPEN_ALWAYS, 0, NULL);
	if (m_data_file == INVALID_HANDLE_VALUE) THROW_WIN32_ERROR(L"failed on open %s", data_fn.c_str());
	LoadJournal(m_log_file, m_data_file, steps);
	return true;
}

bool CJournalDevice::SaveSnapshot(const std::wstring & fn)
{
	HANDLE file = CreateFileW(fn.c_str(),
		GENERIC_READ | GENERIC_WRITE,
		FILE_SHARE_READ, NULL,
		OPEN_ALWAYS, FILE_FLAG_RANDOM_ACCESS, NULL);
	if (file == INVALID_HANDLE_VALUE) THROW_WIN32_ERROR(L"failed on open snapshot %s", fn.c_str());
	DWORD written = 0;
	WriteFile(file, m_buf, (DWORD)m_capacity, &written, NULL);
	LOG_DEBUG(L"write snapshot = %d", written);
	CloseHandle(file);
	return true;
}

bool CJournalDevice::InitializeDevice(const boost::property_tree::wptree& config)
{
	m_sector_size = config.get<UINT>(L"sector_size", 512);
	size_t secs = config.get<size_t>(L"sectors");
	const std::wstring& fn = config.get<std::wstring>(L"image_file");
	m_journal_enable = config.get<bool>(L"enable_journal");
//	CJournalDevice* _dev = jcvos::CDynamicInstance<CJournalDevice>::Create();
	size_t log_cap = 0, log_buf=0;
	if (m_journal_enable)
	{
		log_cap = config.get<size_t>(L"log_capacity");
		log_buf = log_cap * 16;
	}
	CreateFileImage(fn, secs, log_cap, log_buf);
	return true;
}

bool CJournalDevice::BackLog(size_t num)
{	// 重新播放log
	if (num > m_journal_id) THROW_ERROR(ERR_APP, L"back number (%d) is larger than log count (%d)", num, m_journal_id);
	memcpy_s(m_buf, m_capacity, m_snapshot, m_capacity);
	size_t playback_num = m_journal_id - num;
	for (size_t ii = 0; ii < playback_num; ++ii)
	{	// playback
		//JCASSERT(m_sector_size);
		//LOG_DEBUG(L"write lba=%08X, secs=%d", lba, secs);
		//size_t offset = lba * m_sector_size;
		//size_t len = secs * m_sector_size;
		//JCASSERT((offset + len) <= m_capacity);
		//memcpy_s(m_buf + offset, len, buf, len);
		InternalWrite(m_logs[ii].data_ptr, m_logs[ii].lba, m_logs[ii].secs);
	}
	return true;
}

void CJournalDevice::ResetLog(void)
{	// save snapshot
	memcpy_s(m_snapshot, m_capacity, m_buf, m_capacity);
	// reset journal
	m_journal_id = 0;
	m_log_data_ptr = m_log_buf;
}

void CJournalDevice::InternalWrite(void* buf, size_t lba, size_t secs)
{
	JCASSERT(m_sector_size);
//	LOG_DEBUG(L"write lba=%08X, secs=%d", lba, secs);
	size_t offset = lba * m_sector_size;
	size_t len = secs * m_sector_size;
	if ((offset + len) > m_capacity) THROW_ERROR(ERR_APP,
		L"write over capacity cap=%d, lba=%d, secs=%zd", BYTE_TO_SECTOR(m_capacity), lba, secs);
	memcpy_s(m_buf + offset, len, buf, len);
	LOG_DEBUG(L"write  lba=%08X, secs=%d, offset=0x%llX, data=%X", lba, secs, offset, ((DWORD*)(m_buf+offset))[0]);
}

bool CJournalDevice::MakeSnapshot(IVirtualDisk * &dev, size_t steps)
{
	CJournalDevice * _dev = jcvos::CDynamicInstance<CJournalDevice>::Create();
	_dev->CreateFileImage(m_dev_name, m_capacity/SECTOR_SIZE, false, true);
	fseek(m_log_file, 0, SEEK_SET);
	SetFilePointer(m_data_file, 0, NULL, FILE_BEGIN);
	_dev->LoadJournal(m_log_file, m_data_file, steps);
	SetFilePointer(m_data_file, 0, NULL, FILE_END);
	fseek(m_log_file, 0, SEEK_END);
	dev = static_cast<IVirtualDisk*>(_dev);
	return true;
}

size_t CJournalDevice::GetSteps(void) const
{
	return m_journal_id;
}

size_t CJournalDevice::GetCapacity(void)
{
	return BYTE_TO_SECTOR(m_capacity);
}

bool CJournalDevice::ReadSectors(void * buf, size_t lba, size_t secs)
{
	JCASSERT(m_sector_size);
	// 以sector为单位:
	size_t offset = lba * m_sector_size;
	size_t len = secs * m_sector_size;
	JCASSERT((offset + len) <= m_capacity);
	memcpy_s(buf, len, m_buf + offset, len);
	LOG_DEBUG(L"read  lba=%08X, secs=%d, offset=0x%llX, data=%X", lba, secs, offset, ((DWORD*)buf)[0]);
	m_host_read += secs;
	return true;
}

bool CJournalDevice::WriteSectors(void * buf, size_t lba, size_t secs)
{
	size_t len = secs * m_sector_size;
	InternalWrite(buf, lba, secs);
	
	if (m_journal_enable)
	{
		// write journal
		if (m_journal_id >= m_log_capacity) THROW_ERROR(ERR_APP, L"log is full");
		if ((m_log_data_ptr - m_log_buf) + len > SECTOR_TO_BYTE(m_log_buf_secs)) THROW_ERROR(ERR_APP, L"log buffer is full");

		m_logs[m_journal_id].lba = boost::numeric_cast<UINT>(lba);
		m_logs[m_journal_id].secs = boost::numeric_cast<UINT>(secs);
		m_logs[m_journal_id].data_ptr = m_log_data_ptr;
		memcpy_s(m_log_data_ptr, len, buf, len);
		m_log_data_ptr += len;

		// log_id, cmd, lba, secs, offset_in_file
		fprintf_s(m_log_file, "%04zd,WRT,%08zX,%04zX,%08zX\n", m_journal_id, lba, secs, m_data_offset);
		DWORD written = 0;
		//<TODO>处理当le超过32位时的情况。
		WriteFile(m_data_file, buf, boost::numeric_cast<DWORD>(len), &written, NULL);
		m_data_offset += len;
		m_journal_id++;
	}

	m_host_write += secs;
	return true;
}

#if 0
bool CreateVirtualDevice(IVirtualDevice *& dev, const std::wstring & fn, size_t secs)
{
	JCASSERT(dev == NULL);

	CImageDevice * _dev = jcvos::CDynamicInstance<CImageDevice>::Create();
	if (!_dev) return false;
	_dev->CreateFileImage(fn, secs);
	dev = static_cast<IVirtualDevice *>(_dev);

	return true;
}
#endif
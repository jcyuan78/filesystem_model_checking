#include "pch.h"
#include "..\include\yaffs_driver.h"

LOCAL_LOGGER_ENABLE(L"nand_driver", LOGGER_LEVEL_DEBUGINFO);

#define CHECK_LOG

CNandDriverFile::CNandDriverFile()
	:m_block_buf(NULL), m_file(NULL), m_log_file(NULL)
	, m_logs(NULL)
{
	InitializeCriticalSection(&m_file_lock);
	m_log_file = NULL;

	// 初始化内部log
	m_logs = new CNandLog[MAX_LOG_NUM];
	m_log_header = 0;
	m_log_size = 0;
	m_log_count = 0;
}


CNandDriverFile::~CNandDriverFile()
{
	if (m_file) CloseHandle(m_file);
	delete[] m_block_buf;
	DeleteCriticalSection(&m_file_lock);
	if (m_log_file) fclose(m_log_file);
//	if (m_log_file && m_log_file != INVALID_HANDLE_VALUE) CloseHandle(m_log_file);

	// 回收内部log
	delete[] m_logs;
}

bool CNandDriverFile::CreateDevice(const boost::property_tree::wptree & prop, bool create)
{
	JCASSERT(m_block_buf == NULL);
	m_page_size = prop.get<UINT32>(L"page_size", 0);
	if (m_page_size == 0)
	{
		m_data_size = prop.get<UINT32>(L"data_size");
		m_spare_size = prop.get<UINT32>(L"spare_size");
		m_page_size = m_data_size + m_spare_size;
	}
	else
	{
		m_data_size = m_spare_size = 0;
	}
	// 每个page后面补充2字节，第一字节表示是否写入，第二字节表示是否bad
	m_page_size += 2;
	m_page_per_block = prop.get<UINT32>(L"page_num");
	m_block_size = m_page_size * m_page_per_block;
	m_block_num = prop.get<size_t>(L"block_num");
	m_file_size = m_block_num * m_block_size;
	const std::wstring & filename = prop.get<std::wstring>(L"data_file");
	DWORD dispo;
	if (create) dispo = CREATE_ALWAYS;
	else dispo = OPEN_ALWAYS;

	m_file = CreateFile(filename.c_str(), GENERIC_READ | GENERIC_WRITE,
		0, NULL, dispo, FILE_ATTRIBUTE_NORMAL, NULL);
	if (!m_file || m_file == INVALID_HANDLE_VALUE)
	{
		LOG_WIN32_ERROR(L"[err] failed on creating nand file %s", filename.c_str());
		return false;
	}

	m_block_buf = new BYTE[m_block_size];
	if (!m_block_buf)
	{
		LOG_ERROR(L"[err] failed on creating block buffer");
		return false;
	}
	BY_HANDLE_FILE_INFORMATION info;
	GetFileInformationByHandle(m_file, &info);
	if (info.nFileSizeHigh == 0 && info.nFileSizeLow == 0)
	{	// Format data
		LOG_NOTICE(L"initialize nand data file")
		memset(m_block_buf, 0xFF, m_block_size);
		for (size_t bb = 0; bb < m_block_num; ++bb)
		{
			DWORD written = 0;
			BOOL br = WriteFile(m_file, m_block_buf, (DWORD)m_block_size, &written, NULL);
			JCASSERT(br && written == m_block_size);
		}
	}
	else
	{	// checking nand data

	}
	//Initialize();
	// open log file
	m_log_fn = prop.get<std::wstring>(L"log_file", L"");
	if (!m_log_fn.empty())
	{
		//_wfopen_s(&m_log_file, m_log_fn.c_str(), L"w+");
		m_log_file = _wfsopen(m_log_fn.c_str(), L"w+", _SH_DENYNO);
		if (!m_log_file) THROW_ERROR(ERR_USER, L"failed on opening log file %s", m_log_fn.c_str());
	}
	m_empty_blocks = 0;
	m_host_write = 0;
	m_host_read = 0;
	m_media_read = 0;
	m_media_write = 0;
	m_total_erase_count = 0;
	return true;
}

bool CNandDriverFile::GetHealthInfo(HEALTH_INFO & info)
{
	info.empty_block = m_empty_blocks;
	info.host_read = m_host_read;
	info.host_write = m_host_write;
	info.media_read = m_media_read;
	info.media_write = m_media_write;
	return true;
}


void CNandDriverFile::GetFlashId(NAND_DEVICE_INFO & info)
{
	info.data_size = m_data_size;
	info.spare_size = m_spare_size;
	info.page_size = m_page_size;
	info.page_num = m_page_per_block;
	info.block_num = m_block_num;
}

bool CNandDriverFile::WriteChunk(FAddress nand_page, const BYTE *data, size_t data_len)
{
	JCASSERT(data_len < m_page_size);
	WORD blk = HIWORD(nand_page);
	WORD page = LOWORD(nand_page);
	int nand_chunk = blk * m_page_per_block + page;
	return WriteChunk(nand_chunk, data, m_data_size, data + m_data_size, m_spare_size);
}

bool CNandDriverFile::WriteChunk(int nand_chunk, const BYTE * data, size_t data_len, 
	const BYTE * oob, size_t oob_len)
{
	JCASSERT(m_file && (data_len + oob_len) <= (int)m_page_size && data &&oob&& m_block_buf);
	UINT32 blk = nand_chunk / m_page_per_block;
	UINT32 page = nand_chunk % m_page_per_block;
	if (page == 0) m_empty_blocks--;

	if (m_log_file)
	{
		fwprintf_s(m_log_file, L"WRITE page=(%03X:%03X), size=%zd, log=%zd, oop=%08X, %08X, %08X, %08X\n",
			blk, page, data_len, m_log_header, ((DWORD*)(oob))[0], ((DWORD*)(oob))[1], ((DWORD*)(oob))[2], 
			((DWORD*)(oob))[3]);
#ifdef CHECK_LOG
		fflush(m_log_file);
#endif
	}

	// add internal log, for power test
	PushLog(CNandLog::LOG_WRITE_CHUNK, nand_chunk);

	size_t offset = nand_chunk * m_page_size;
	DWORD written = 0;
	EnterCriticalSection(&m_file_lock);
	memcpy_s(m_block_buf, m_data_size, data, data_len);
	memcpy_s(m_block_buf + m_data_size, m_spare_size, oob, oob_len);
	memset(m_block_buf + m_data_size + oob_len, 0, m_page_size-m_data_size - oob_len);
	SetFilePointer(m_file, offset, NULL, FILE_BEGIN);
	BOOL br = WriteFile(m_file, m_block_buf, m_page_size, &written, NULL);
	LeaveCriticalSection(&m_file_lock);
	if (!br) THROW_WIN32_ERROR(L"failed on writing file");

	m_host_write += 1;
	m_media_write += 1;

	return true;

	//<TODO> 优化存储效率，over program检查

	//if (blk >= m_block_num)
	//{
	//	JCASSERT(0);
	//	THROW_ERROR(ERR_APP, L"illeagle block no:%d, chunk=%d", blk, nand_chunk);
	//}
	//if (blk != m_cached_blk)	SwitchBlock(blk);

	//// program data to cache
	//size_t offset = page * m_page_size;
	//memcpy_s(m_block_buf + offset, m_block_size - offset, data, data_len);
	//memcpy_s(m_block_buf + offset + data_len, m_block_size - offset - data_len, oob, oob_len);
	//m_dirty = true;
	//if (page == (m_page_per_block - 1)) SwitchBlock(-1);	// save block
	//return true;
}

bool CNandDriverFile::ReadChunk(FAddress nand_page, BYTE *data, size_t data_len, ECC_RESULT &ecc_result)
{
	JCASSERT(data_len < m_page_size);
	WORD blk = HIWORD(nand_page);
	WORD page = LOWORD(nand_page);
	int nand_chunk = blk * m_page_per_block + page;
	return  ReadChunk(nand_chunk, data, m_data_size, data + m_data_size, m_spare_size, ecc_result);
}


// data, oob, 允许为NULL。此时不传data
bool CNandDriverFile::ReadChunk(int nand_chunk, BYTE * data, size_t data_len, 
	BYTE * oob, size_t oob_len, ECC_RESULT & ecc_result)
{
	JCASSERT(m_file && (data_len +oob_len)<=(int)m_page_size && m_block_buf);
	size_t offset = nand_chunk * m_page_size;
	DWORD read = 0;
	EnterCriticalSection(&m_file_lock);
	SetFilePointer(m_file, offset, NULL, FILE_BEGIN);
	BOOL br = ReadFile(m_file, m_block_buf, m_page_size, &read, NULL);
	if (m_block_buf[m_page_size - 1] == 'x') ecc_result = ECC_RESULT_UNFIXED;
	if (data) memcpy_s(data, data_len, m_block_buf, data_len);
	if (oob) memcpy_s(oob, oob_len, m_block_buf + m_data_size, oob_len);
	LeaveCriticalSection(&m_file_lock);
	if (!br) THROW_WIN32_ERROR(L"failed on reading file");
	m_host_read += 1;
	m_media_read += 1;
	return true;
}

bool CNandDriverFile::Erase(int block_no)
{
	JCASSERT(block_no < (int)m_block_num && m_block_buf && m_file);
	m_empty_blocks++;
	if (m_log_file)
	{
		fwprintf_s(m_log_file, L"ERASE page=(%03X:), empty=%d\n", block_no, m_empty_blocks);
#ifdef CHECK_LOG
		fflush(m_log_file);
#endif
	}

	size_t offset = block_no * m_page_per_block * m_page_size;
	DWORD written = 0;
	EnterCriticalSection(&m_file_lock);
	memset(m_block_buf, 0xFF, m_block_size);
	SetFilePointer(m_file, offset, NULL, FILE_BEGIN);
	BOOL br=WriteFile(m_file, m_block_buf, (DWORD)m_block_size, &written, NULL);
	LeaveCriticalSection(&m_file_lock);
	if (!br) THROW_WIN32_ERROR(L"failed on writing file");
	m_total_erase_count++;
	return true;
}

bool CNandDriverFile::MarkBad(int block_no)
{
	size_t offset = block_no * m_page_per_block * m_page_size;
	DWORD transfer = 0;
	// 在block的第0page的最后一个byte 标记X
	EnterCriticalSection(&m_file_lock);
	SetFilePointer(m_file, offset, NULL, FILE_BEGIN);
	BOOL br = ReadFile(m_file, m_block_buf, m_page_size, &transfer, NULL);
	m_block_buf[m_page_size - 1] = 'X';
	br &= WriteFile(m_file, m_block_buf, m_page_size, &transfer, NULL);
	LeaveCriticalSection(&m_file_lock);
	if (!br) THROW_WIN32_ERROR(L"failed on writing file");
	return true;
}

bool CNandDriverFile::CheckBad(int block_no)
{
	size_t offset = block_no * m_page_per_block * m_page_size;
	DWORD transfer = 0;
	bool bad = true;		// false: bad block
	// 在block的第0page的最后一个byte 标记X
	EnterCriticalSection(&m_file_lock);
	SetFilePointer(m_file, offset, NULL, FILE_BEGIN);
	BOOL br = ReadFile(m_file, m_block_buf, m_page_size, &transfer, NULL);
	if (m_block_buf[m_page_size - 1] == 'X') bad = false;
	LeaveCriticalSection(&m_file_lock);
	if (!br) THROW_WIN32_ERROR(L"failed on writing file");
	return bad;
}

void CNandDriverFile::PushLog(UINT32 op, UINT32 chunk)
{
	m_logs[m_log_header].op = op;
	m_logs[m_log_header].nand_chunk = chunk;
	m_log_header++;
	if ( m_log_header >= MAX_LOG_NUM) m_log_header = 0;
//	m_log_size++;
	m_log_count++;
}

void CNandDriverFile::PopLog(UINT32 & op, UINT32 & chunk)
{
	if (m_log_header == 0) m_log_header = MAX_LOG_NUM;
	m_log_header--;
	op = m_logs[m_log_header].op;
	chunk = m_logs[m_log_header].nand_chunk;
	m_log_count--;
}

bool CNandDriverFile::BackLog(size_t num)
{
	if (num > m_log_count) 
		THROW_ERROR(ERR_APP, L"log count=%d, no enough log to back (%d)", m_log_count, num);
	for (size_t ii = 0; ii < num; ++ii)
	{
		UINT32 op, chunk;
		PopLog(op, chunk);

		UINT32 blk = chunk / m_page_per_block;
		UINT32 page = chunk % m_page_per_block;

		if (m_log_file)
		{
			fwprintf_s(m_log_file, L"DAMAGE page=(%03X:%03X), size=%d, log=%zd\n", blk, page, m_page_size, m_log_header);
		}
		size_t offset = chunk * m_page_size;
		DWORD written = 0;
		EnterCriticalSection(&m_file_lock);
		memset(m_block_buf, 0, m_page_size);
		m_block_buf[m_page_size - 1] = 'x';		// x 表示page有ECC, 'X'表示bad block
		SetFilePointer(m_file, offset, NULL, FILE_BEGIN);
		BOOL br = WriteFile(m_file, m_block_buf, m_page_size, &written, NULL);
		LeaveCriticalSection(&m_file_lock);
	}
	fflush(m_log_file);
	return true;
}

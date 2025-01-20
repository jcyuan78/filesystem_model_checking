///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#include "stdafx.h"
#include "../include/crash_disk.h"

LOCAL_LOGGER_ENABLE(L"fat.image", LOGGER_LEVEL_DEBUGINFO);

#define SECTOR_PER_BLOCK 8

CCrashDisk::CCrashDisk(void)
{
	m_sector_size = 512;
	m_cache_size = 0;
	m_disk_size = 0;		// sector 单位
	m_lba_mapping = nullptr;

	m_data_file = INVALID_HANDLE_VALUE;
}

CCrashDisk::~CCrashDisk(void)
{
	for (auto it = m_cache.begin(); it != m_cache.end(); ++it)
	{
		LBA_ENTRY* entry = *it;
		delete entry;
	}
	CloseHandle(m_data_file);
}

void CCrashDisk::Reset(void)
{
	for (auto it = m_cache.begin(); it != m_cache.end(); ++it)
	{
		LBA_ENTRY* entry = *it;
		delete entry;
	}
	m_cache.clear();
	CloseHandle(m_data_file);
	m_data_file = INVALID_HANDLE_VALUE;

	delete[] m_lba_mapping;
	m_lba_mapping = nullptr;

	m_sector_size = 512;
	m_cache_size = 0;
	m_disk_size = 0;		// sector 单位
}

void CCrashDisk::ReadSingleBlock(BYTE* buf, UINT blk_no)
{
	if (!is_valid(m_lba_mapping[blk_no]))
	{
		memset(buf, 0, SECTOR_SIZE);
	}
	else
	{
		LBA_ENTRY* entry = m_cache[m_lba_mapping[blk_no]];
		LONG file_pos = entry->offset * m_block_size;
		SetFilePointer(m_data_file, file_pos, nullptr, FILE_BEGIN);
		DWORD read = 0;
		ReadFile(m_data_file, buf, m_block_size, &read, nullptr);
	}

}

bool CCrashDisk::ReadSectors(void* _buf, size_t lba, size_t secs)
{
	JCASSERT(lba % 8 == 0 && secs % 8 == 0);
	BYTE *buf = reinterpret_cast<BYTE*>(_buf);
	UINT start_blk = (UINT)(lba / SECTOR_PER_BLOCK);
//	UINT end_blk = (UINT)(round_up((lba + secs), (size_t)(SECTOR_PER_BLOCK)));
	UINT end_lba = (UINT)(lba + secs);
	UINT end_blk = (UINT)(ROUND_UP_DIV(end_lba, SECTOR_PER_BLOCK));
	LOG_DEBUG(L"[disk] read: start_lba=%lld, secs=%lld, start_blk=%d, end_blk=%d", lba, secs, start_blk, end_blk);
	DWORD written = 0, read=0;

	LONG file_offset = -1;
	for (; start_blk < end_blk; ++start_blk)
	{
		ReadSingleBlock(buf, start_blk);
		buf += m_block_size;
	}
	return true;
}

bool CCrashDisk::WriteSectors(void* _buf, size_t lba, size_t secs)
{
	JCASSERT(lba % 8 == 0 && secs % 8 == 0);
	BYTE *buf = reinterpret_cast<BYTE*>(_buf);
	UINT start_blk = (UINT)(lba / SECTOR_PER_BLOCK);
//	UINT end_blk = (UINT)(round_up((lba + secs), (size_t)(SECTOR_PER_BLOCK)));
	UINT end_lba = (UINT)(lba + secs);
	UINT end_blk = (UINT)(ROUND_UP_DIV(end_lba, SECTOR_PER_BLOCK));
//	LOG_DEBUG_(1, L"save entry from block %d to block %d", start_blk, end_blk);
	LOG_DEBUG(L"[disk] write: start_lba=%lld, secs=%lld, start_blk=%d, end_blk=%d", lba, secs, start_blk, end_blk);
	DWORD written = 0, read=0;

#if 0
	// 4KB 补头补尾
	BYTE* block_buf =nullptr;
	// 第一个block
	UINT start_lba = start_blk * SECTOR_PER_BLOCK;
	if (start_lba < lba)
	{	// 读取, 补头
		block_buf = new BYTE[m_block_size];
		//if (is_valid(m_lba_mapping[start_blk])) {
		//	LBA_ENTRY* entry = m_cache[m_lba_mapping[lba]];
		//	LONG file_pos = entry->offset * m_block_size;
		//	SetFilePointer(m_data_file, file_pos, nullptr, FILE_BEGIN);
		//	ReadFile(m_data_file, block_buf, m_block_size, &read, nullptr);
		//}
		//else memset(block_buf, 0, m_block_size);
		ReadSingleBlock(block_buf, start_lba);

		size_t head_size = (start_lba + SECTOR_PER_BLOCK - lba) * m_sector_size;
		memcpy_s(block_buf + (lba - start_lba) * m_sector_size, head_size, buf, head_size);
		SetFilePointer(m_data_file, 0, nullptr, FILE_END);
		WriteFile(m_data_file, block_buf, m_block_size, &written, nullptr);
		start_blk++;
		buf += head_size;

		LBA_ENTRY* entry = new LBA_ENTRY;
		entry->lba = start_blk;
		m_cache.push_back(entry);
		entry->offset = (UINT)(m_cache.size() - 1);
		cache_enque(entry->lba, entry->offset);
	}

	UINT _end_blk = end_blk;
	UINT end_lba = end_blk * SECTOR_PER_BLOCK;
	if (end_lba > lba + secs)
	{	// 补尾
		_end_blk--;
		if (block_buf == nullptr) block_buf = new BYTE[m_block_size];
	}
#endif

	// write data to image file
	// 确定write point
	SetFilePointer(m_data_file, 0, nullptr, FILE_END);
	WriteFile(m_data_file, buf, (DWORD)(secs * SECTOR_SIZE), &written, nullptr);

	// 记录cache
	for (; start_blk < end_blk; ++start_blk)
	{
		LBA_ENTRY* entry = new LBA_ENTRY;
		memset(entry, 0xFF, sizeof(LBA_ENTRY));
		entry->lba = start_blk;
		m_cache.push_back(entry);
		UINT cur_index = (UINT)(m_cache.size() - 1);
		entry->offset = cur_index;
		cache_enque(entry->lba, cur_index);
	}
	LOG_DEBUG_(1,L"cache size = %lld", m_cache.size());
	return true;
}

bool CCrashDisk::AsyncWriteSectors(void* buf, size_t secs, OVERLAPPED* overlap)
{
	JCASSERT(overlap);
	LONG offset = overlap->Offset;
	bool br = WriteSectors(buf, offset/m_sector_size, secs);

	SetEvent(overlap->hEvent);
	return br;
}

bool CCrashDisk::AsyncReadSectors(void* buf, size_t secs, OVERLAPPED* overlap)
{
	JCASSERT(overlap);
	LONG offset = overlap->Offset;
	bool br = ReadSectors(buf, offset / m_sector_size, secs);
	SetEvent(overlap->hEvent);
	return br;
}

void CCrashDisk::cache_enque(UINT lba, UINT index)
{
	LBA_ENTRY* new_entry = m_cache[index];
	if (!is_valid(m_lba_mapping[lba]))
	{
		new_entry->cache_next = index;
		new_entry->cache_prev = index;
	}
	else
	{
		LBA_ENTRY* cur_entry = m_cache[m_lba_mapping[lba]];
		LBA_ENTRY* next_entry = m_cache[cur_entry->cache_next];

		new_entry->cache_prev = m_lba_mapping[lba];
		new_entry->cache_next = cur_entry->cache_next;
		cur_entry->cache_next = index;
		next_entry->cache_prev = index;
	}
	m_lba_mapping[lba] = index;
}

void CCrashDisk::cache_deque(UINT index)
{
	LBA_ENTRY* cur_entry = m_cache[index];
	UINT lba = cur_entry->lba;
	if (cur_entry->cache_next == cur_entry->cache_prev)
	{
		m_lba_mapping[lba] = (UINT)-1;
	}
	else
	{
		m_lba_mapping[lba] = cur_entry->cache_prev;
		LBA_ENTRY* prev_entry = m_cache[cur_entry->cache_prev];
		LBA_ENTRY* next_entry = m_cache[cur_entry->cache_next];
		prev_entry->cache_next = cur_entry->cache_next;
		next_entry->cache_prev = cur_entry->cache_prev;

	}
}


bool CCrashDisk::InitializeDevice(const boost::property_tree::wptree& config)
{
	m_data_fn = config.get<std::wstring>(L"file_name");
	m_disk_size = config.get<UINT>(L"sectors");
	m_sector_size = config.get<UINT>(L"sector_size", 512);
	m_block_size = SECTOR_PER_BLOCK * m_sector_size;

	m_lba_mapping = new UINT[m_disk_size];
	memset(m_lba_mapping, 0xFF, sizeof(UINT) * m_disk_size);

	m_data_file = CreateFile(m_data_fn.c_str(), GENERIC_ALL, FILE_SHARE_READ, nullptr, CREATE_ALWAYS, 
		FILE_FLAG_RANDOM_ACCESS, nullptr);
	if (m_data_file == INVALID_HANDLE_VALUE) THROW_ERROR(ERR_USER, L"failed on creating file %s", m_data_fn.c_str());
	m_file_pos = 0;
	return true;
}

bool CCrashDisk::LoadFromFile(const std::wstring& fn)
{
	std::string str_fn;
	jcvos::UnicodeToUtf8(str_fn, fn);

	boost::property_tree::wptree archive;
	boost::property_tree::json_parser::read_json(str_fn, archive);
	const boost::property_tree::wptree& config = archive.get_child(L"config");

	m_data_fn = config.get<std::wstring>(L"data_file");
	m_disk_size = config.get<UINT>(L"sectors");
	m_sector_size = config.get<UINT>(L"sector_size", 512);
	m_block_size = SECTOR_PER_BLOCK * m_sector_size;

	m_lba_mapping = new UINT[m_disk_size];
	memset(m_lba_mapping, 0xFF, sizeof(UINT) * m_disk_size);

	m_data_file = CreateFile(m_data_fn.c_str(), GENERIC_ALL, FILE_SHARE_READ, nullptr, OPEN_ALWAYS, 
		FILE_FLAG_RANDOM_ACCESS, nullptr);
	if (m_data_file == INVALID_HANDLE_VALUE) THROW_ERROR(ERR_USER, L"failed on creating file %s", m_data_fn.c_str());

	const boost::property_tree::wptree& logs = archive.get_child(L"logs");
	for (auto it = logs.begin(); it != logs.end(); ++it)
	{
		const boost::property_tree::wptree& log = it->second;
		LBA_ENTRY* entry = new LBA_ENTRY;
		entry->lba = log.get<UINT>(L"lba");
		entry->offset = log.get<UINT>(L"offset");
	
		m_cache.push_back(entry);
		UINT index = (UINT)(m_cache.size() - 1);
		JCASSERT(index == entry->offset);

		cache_enque(entry->lba, index);
	}

	return true;
}

bool CCrashDisk::SaveToFile(const std::wstring& fn)
{
	LOG_DEBUG(L"cache size = %lld", m_cache.size());
	FlushFileBuffers(m_data_file);
	boost::property_tree::wptree archive;
	// 配置部分
	boost::property_tree::wptree config;
	config.add(L"sectors", m_disk_size);
	config.add(L"sector_size", m_sector_size);
	config.add(L"data_file", m_data_fn);
	// log部分
	boost::property_tree::wptree logs;
	for (auto it = m_cache.begin(); it != m_cache.end(); ++it)
	{
		LBA_ENTRY* entry = *it;
		boost::property_tree::wptree log;
		log.add(L"lba", entry->lba);
		log.add(L"offset", entry->offset);
		logs.add_child(L"entry", log);
	}
	// mapping部分

	archive.add_child(L"config", config);
	archive.add_child(L"logs", logs);
	std::string str_fn;
	jcvos::UnicodeToUtf8(str_fn, fn);
	boost::property_tree::json_parser::write_json(str_fn, archive);
	return true;
}




bool CCrashDisk::BackLog(size_t num)
{
	if (num > m_cache.size()) return false;
	for (size_t ii = 0; ii < num; ++ii)
	{
		UINT index = (UINT)(m_cache.size() - 1);
		cache_deque(index);
		LBA_ENTRY* cur_entry = m_cache[index];
		m_cache.pop_back();
		delete cur_entry;
//		m_cache.
	}
	return true;
}

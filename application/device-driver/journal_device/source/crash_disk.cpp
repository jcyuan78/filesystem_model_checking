///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#include "stdafx.h"
#include "../include/crash_disk.h"

LOCAL_LOGGER_ENABLE(L"disk.crash", LOGGER_LEVEL_DEBUGINFO);

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
	CloseHandle(m_data_file);
	for (auto it = m_cmd_cache.begin(); it != m_cmd_cache.end(); ++it)
	{
		CMD_ENTRY& entry = *it;
		delete[] entry.blocks;
	}
	delete[] m_lba_mapping;
}

void CCrashDisk::Reset(void)
{
	for (auto it = m_cmd_cache.begin(); it != m_cmd_cache.end(); ++it)
	{
		CMD_ENTRY& entry = *it;
		delete[] entry.blocks;
	}
	m_cmd_cache.clear();
	UINT blk_nr = m_disk_size / SECTOR_PER_BLOCK;
	memset(m_lba_mapping, 0, sizeof(LP_LBA_ENTRY) * blk_nr);

	SetFilePointer(m_data_file, 0, 0, FILE_BEGIN);
	SetEndOfFile(m_data_file);
	m_cache_size = 0;
//	m_disk_size = 0;		// sector 单位
}

void CCrashDisk::ReadSingleBlock(BYTE* buf, UINT blk_no)
{
	//if (!is_valid(m_lba_mapping[blk_no]))
	if (m_lba_mapping[blk_no] == nullptr)
	{
		LOG_WARNING(L"[warning] read unmapped block, lba=%d, blk_no=%d", blk_no * SECTOR_PER_BLOCK, blk_no);
		memset(buf, 0, SECTOR_SIZE);
	}
	else
	{
//		LBA_ENTRY* entry = m_cache[m_lba_mapping[blk_no]];
		LBA_ENTRY* entry = m_lba_mapping[blk_no];
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
	//UINT start_blk = (UINT)(lba / SECTOR_PER_BLOCK);
	//UINT end_lba = (UINT)(lba + secs);
	//UINT end_blk = (UINT)(ROUND_UP_DIV(end_lba, SECTOR_PER_BLOCK));
	UINT start_blk, end_blk;
	lba_to_block(start_blk, end_blk, (UINT)lba, (UINT)secs);
	LOG_DEBUG_(1,L"[disk] read: start_lba=%lld, secs=%lld, start_blk=%d, end_blk=%d", lba, secs, start_blk, end_blk);
	DWORD written = 0, read=0;

	LONG file_offset = -1;
	for (; start_blk < end_blk; ++start_blk)
	{
		ReadSingleBlock(buf, start_blk);
		buf += m_block_size;
	}
	return true;
}

void CCrashDisk::add_command(UINT start_lba, UINT secs, UINT start_blk, UINT end_blk, UINT & start_offset)
{
	m_cmd_cache.emplace_back();
	CMD_ENTRY& cmd_entry = m_cmd_cache.back();
	cmd_entry.start_lba = start_lba;
	cmd_entry.secs = secs;
	UINT blk_nr = end_blk - start_blk;
	cmd_entry.blocks = new LBA_ENTRY[blk_nr];
	memset(cmd_entry.blocks, 0, sizeof(LBA_ENTRY) * (blk_nr));

	// 记录cache
	for (int index = 0; start_blk < end_blk; ++start_blk, ++index)
	{
		//		LBA_ENTRY* entry = new LBA_ENTRY;
		//		memset(entry, 0xFF, sizeof(LBA_ENTRY));
		//		entry->lba = start_blk;
		//		m_cache.push_back(entry);
		//		UINT cur_index = (UINT)(m_cache.size() - 1);
		// UINT cur_index = start_offset;
		cmd_entry.blocks[index].offset = start_offset;	//指向data文件中的位置
		//		entry->offset = cur_index;
		//		cache_enque(entry->lba, cur_index);
		cache_enque(&cmd_entry.blocks[index], start_blk);
//		m_cache_size++;
		start_offset++;
	}

}

bool CCrashDisk::WriteSectors(void* _buf, size_t lba, size_t secs)
{
	JCASSERT(lba % 8 == 0 && secs % 8 == 0);
	BYTE *buf = reinterpret_cast<BYTE*>(_buf);
//	UINT start_blk = (UINT)(lba / SECTOR_PER_BLOCK);
////	UINT end_blk = (UINT)(round_up((lba + secs), (size_t)(SECTOR_PER_BLOCK)));
//	UINT end_lba = (UINT)(lba + secs);
//	UINT end_blk = (UINT)(ROUND_UP_DIV(end_lba, SECTOR_PER_BLOCK));
	UINT start_blk, end_blk;
	lba_to_block(start_blk, end_blk, (UINT)lba, (UINT)secs);
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

	add_command((UINT)lba, (UINT)secs, start_blk, end_blk, m_cache_size);

	//m_cmd_cache.emplace_back();
	//CMD_ENTRY& cmd_entry = m_cmd_cache.back();
	//cmd_entry.start_lba = lba;
	//cmd_entry.secs = secs;
	//UINT blk_nr = end_blk - start_blk;
	//cmd_entry.blocks = new LBA_ENTRY[blk_nr];
	//memset(&cmd_entry.blocks, 0, sizeof(LBA_ENTRY) * (blk_nr));

	//// 记录cache
	//for (int index=0; start_blk < end_blk; ++start_blk, ++index)
	//{
	//	UINT cur_index = m_cache_size - 1;
	//	cmd_entry.blocks[index].offset = cur_index;	//指向data文件中的位置
	//	cache_enque(&cmd_entry.blocks[index], start_blk);
	//	m_cache_size++;
	//}
	LOG_DEBUG_(1,L"cache size = %lld", m_cache_size);
	return true;
}

UINT CCrashDisk::lba_to_block(UINT& start_blk, UINT& end_blk, UINT lba, UINT secs)
{
	start_blk = (lba / SECTOR_PER_BLOCK);
	UINT end_lba = (lba + secs);
	end_blk = (UINT)(ROUND_UP_DIV(end_lba, SECTOR_PER_BLOCK));
	return end_blk - start_blk;
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

// 将lba_entry连接到每个lba cache的队列上
//void CCrashDisk::cache_enque(UINT lba, UINT index)
void CCrashDisk::cache_enque(LBA_ENTRY* lba_entry, UINT blk)
{
	LBA_ENTRY* new_entry = lba_entry;
	if (m_lba_mapping[blk] == nullptr)
	{
		new_entry->cache_next = new_entry;
		new_entry->cache_prev = new_entry;
	}
	else
	{
		LBA_ENTRY* cur_entry = m_lba_mapping[blk]; //m_cache[m_lba_mapping[lba]];
		LBA_ENTRY* next_entry = cur_entry->cache_next;

		new_entry->cache_prev = cur_entry;
		new_entry->cache_next = next_entry;
		cur_entry->cache_next = new_entry;
		next_entry->cache_prev = new_entry;
	}
	m_lba_mapping[blk] = new_entry;
}



void CCrashDisk::cache_deque(UINT blk)
{
	LBA_ENTRY* cur_entry = m_lba_mapping[blk]; //m_cache[index];
//	UINT lba = cur_entry->lba;
	if (cur_entry->cache_next == cur_entry->cache_prev)
	{
		m_lba_mapping[blk] = nullptr;
	}
	else
	{
		m_lba_mapping[blk] = cur_entry->cache_prev;
		LBA_ENTRY* prev_entry = cur_entry->cache_prev;
		LBA_ENTRY* next_entry = cur_entry->cache_next;
		prev_entry->cache_next = cur_entry->cache_next;
		next_entry->cache_prev = cur_entry->cache_prev;
	}
}

void CCrashDisk::build_lba_mapping(void)
{
	UINT blk_nr = m_disk_size / SECTOR_PER_BLOCK;
	m_lba_mapping = new LP_LBA_ENTRY[blk_nr];
	memset(m_lba_mapping, 0, sizeof(LP_LBA_ENTRY) * blk_nr);
}

int CCrashDisk::IoCtrl(int mode, UINT cmd, void* arg)
{
	if (cmd == (UINT)IOCTRL_MARK) {
		wchar_t* str_cmd = (wchar_t*)(arg);
		m_cmd_cache.emplace_back();
		CMD_ENTRY& entry = m_cmd_cache.back();
		entry.op = str_cmd;
		entry.start_lba = (UINT)-1;
		entry.secs = 0;
		entry.blocks = nullptr;
	}
	return 0;
}

bool CCrashDisk::InitializeDevice(const boost::property_tree::wptree& config)
{
	m_data_fn = config.get<std::wstring>(L"file_name");
	m_disk_size = config.get<UINT>(L"sectors");
	m_sector_size = config.get<UINT>(L"sector_size", 512);
	m_block_size = SECTOR_PER_BLOCK * m_sector_size;

	build_lba_mapping();

	m_data_file = CreateFile(m_data_fn.c_str(), GENERIC_ALL, FILE_SHARE_READ, nullptr, CREATE_ALWAYS, 
		FILE_FLAG_RANDOM_ACCESS, nullptr);
	if (m_data_file == INVALID_HANDLE_VALUE) THROW_ERROR(ERR_USER, L"failed on creating file %s", m_data_fn.c_str());
	m_file_pos = 0;
	m_cache_size = 0;
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

	//m_lba_mapping = new UINT[m_disk_size];
	//memset(m_lba_mapping, 0xFF, sizeof(UINT) * m_disk_size);
	build_lba_mapping();
	m_cache_size = 0;

	m_data_file = CreateFile(m_data_fn.c_str(), GENERIC_ALL, FILE_SHARE_READ, nullptr, OPEN_ALWAYS, 
		FILE_FLAG_RANDOM_ACCESS, nullptr);
	if (m_data_file == INVALID_HANDLE_VALUE) THROW_ERROR(ERR_USER, L"failed on creating file %s", m_data_fn.c_str());

	const boost::property_tree::wptree& logs = archive.get_child(L"logs");
	for (auto it = logs.begin(); it != logs.end(); ++it)
	{
		const boost::property_tree::wptree& log = it->second;
		const std::wstring& str_cmd = log.get<std::wstring>(L"cmd");
		if (str_cmd == L"write") {
			UINT lba = log.get<UINT>(L"lba");
			UINT secs = log.get<UINT>(L"secs");
			UINT offset = log.get<UINT>(L"offset");
			JCASSERT(m_cache_size == offset);
			UINT start_blk, end_blk;
			lba_to_block(start_blk, end_blk, lba, secs);
			add_command(lba, secs, start_blk, end_blk, m_cache_size);
		}
		else
		{
			m_cmd_cache.emplace_back();
			CMD_ENTRY& entry = m_cmd_cache.back();
			entry.op = str_cmd;
			entry.start_lba = (UINT)-1;
			entry.secs = 0;
			entry.blocks = nullptr;
		}
	}

	return true;
}

bool CCrashDisk::SaveToFile(const std::wstring& fn)
{
//	LOG_DEBUG(L"cache size = %lld", m_cache.size());
	FlushFileBuffers(m_data_file);
	boost::property_tree::wptree archive;
	// 配置部分
	boost::property_tree::wptree config;
	config.add(L"sectors", m_disk_size);
	config.add(L"sector_size", m_sector_size);
	config.add(L"data_file", m_data_fn);
	// log部分
	boost::property_tree::wptree logs;
	for (auto it = m_cmd_cache.begin(); it!= m_cmd_cache.end(); ++it)
	{
		CMD_ENTRY& entry = *it;
		boost::property_tree::wptree log;
		if (entry.op.empty())
		{
			log.add(L"cmd", L"write");
			log.add(L"lba", entry.start_lba);
			log.add(L"secs", entry.secs);
			log.add(L"offset", entry.blocks[0].offset);
		}
		else {
			log.add(L"cmd", entry.op);
		}
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

size_t CCrashDisk::GetIoLogs(IO_ENTRY* entries, size_t io_nr)
{
//	for (size_t ii = 0; ii < io_nr && ii < m_cache.size(); ++ii)
	size_t ii = 0;
	for (auto it = m_cmd_cache.rbegin(); it!= m_cmd_cache.rend(); ++it)
	{
		if (ii >= io_nr) break;
		if (is_valid(it->start_lba))
		{
			UINT start_blk, end_blk, blk_nr;
			blk_nr = lba_to_block(start_blk, end_blk, it->start_lba, it->secs);
			UINT jj = 0;
			for (; (jj < blk_nr) && is_valid(it->blocks[jj].offset); ++jj) {}
			if (jj == 0) continue;
			//		entries[ii].cmd = IO_ENTRY::WRITE_SECTOR;
//			if (it->op.empty()) 
//			else entries[ii].op = it->op;
			entries[ii].op = L"write";
			entries[ii].lba = it->start_lba;
			entries[ii].secs = it->secs;
			entries[ii].block_index = it->blocks[0].offset;
			entries[ii].blk_nr = jj;		// 只计算有效的block number，被rollback的不算
		}
		else
		{
			entries[ii].op = it->op;
			entries[ii].lba = 0;
			entries[ii].secs = 0;
			entries[ii].block_index = 0;
			entries[ii].blk_nr = 0;
		}
		++ii;
	}
	return ii;
}

bool CCrashDisk::BackLog(size_t num)
{
	for (auto it = m_cmd_cache.rbegin(); it != m_cmd_cache.rend(); ++it)
	{
		if (!is_valid(it->start_lba)) continue;
		UINT start_blk, end_blk, blk_nr;
		blk_nr = lba_to_block(start_blk, end_blk, it->start_lba, it->secs);
		while (num >0)
		{
			if (end_blk <= start_blk) break;
			end_blk--;
			cache_deque(end_blk);
			it->blocks[--blk_nr].offset = (UINT)-1;
			num--;
		}
		if (num == 0) break;
	}
//	if (num > m_cache.size()) return false;
//	for (size_t ii = 0; ii < num; ++ii)
//	{
//		UINT index = (UINT)(m_cache.size() - 1);
//		cache_deque(index);
//		LBA_ENTRY* cur_entry = m_cache[index];
//		m_cache.pop_back();
//		delete cur_entry;
////		m_cache.
//	}
	return true;
}

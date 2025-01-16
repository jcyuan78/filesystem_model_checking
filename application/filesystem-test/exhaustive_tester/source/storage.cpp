///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
#include "pch.h"

#include "../include/storage.h"
#include "../include/f2fs_simulator.h"

LOCAL_LOGGER_ENABLE(L"simulator.storage", LOGGER_LEVEL_DEBUGINFO);

CStorage::CStorage(CF2fsSimulator* fs)
{
	m_pages = & fs->m_pages;
	memset(m_data, -1, sizeof(BLOCK_DATA) * TOTAL_BLOCK_NR);
}

void CStorage::Initialize(void)
{
	memset(m_data, -1, sizeof(StorageEntry) * TOTAL_BLOCK_NR);
	memset(m_cache, -1, sizeof(StorageEntry) * SSD_CACHE_SIZE);
	m_cache_head = 0;
	m_cache_tail = 0;
	m_cache_size = 0;

}

void CStorage::CopyFrom(const CStorage* src)
{
	size_t mem_size = sizeof(StorageEntry) * TOTAL_BLOCK_NR;
	memcpy_s(m_data, mem_size, src->m_data, mem_size);
	size_t cache_size = sizeof(StorageEntry) * SSD_CACHE_SIZE;
	memcpy_s(m_cache, cache_size, src->m_cache, cache_size);
	m_cache_head = src->m_cache_head;
	m_cache_tail = src->m_cache_tail;
	m_cache_size = src-> m_cache_size;
}

void CStorage::Reset(void)
{
	// 清空缓存
	for (UINT ii = 0; ii < TOTAL_BLOCK_NR; ++ii)
	{
		m_data[ii].cache_next = INVALID_BLK;
		m_data[ii].cache_prev = INVALID_BLK;
	}
	memset(m_cache, -1, sizeof(StorageEntry) * SSD_CACHE_SIZE);
	m_cache_head = 0;
	m_cache_tail = 0;
	m_cache_size = 0;
}

void CStorage::cache_deque(LBLK_T cache_index)
{
	StorageEntry* cache = m_cache + cache_index;
	LBLK_T lba = cache->lba;
	StorageEntry* s = m_data + lba;
	if (is_invalid(cache->cache_next)) 
	{
		s->cache_next = INVALID_BLK;
		s->cache_prev = INVALID_BLK;
	}
	else
	{
		StorageEntry* next = m_cache + cache->cache_next;
		if (next->lba != lba) THROW_ERROR(ERR_APP, L"cache data not match, lba=%d, lba in cache=%d", lba, next->lba);
		next->cache_prev = INVALID_BLK;
		s->cache_next = cache->cache_next;
	}
	memcpy_s(&(s->data), sizeof(BLOCK_DATA), &(cache->data), sizeof(BLOCK_DATA));
	LOG_DEBUG_(1, L"flush cache (index=%d) to storage lba=%d", cache_index, lba);
}

void CStorage::cache_enque(LBLK_T lba, LBLK_T cache_index)
{
	StorageEntry* s = m_data + lba;
	LBLK_T prev = s->cache_prev;
	if (is_valid(prev)) {
		m_cache[prev].cache_next = cache_index;
		m_cache[cache_index].cache_prev = prev;
	}
	else
	{
		s->cache_next = cache_index;
		m_cache[cache_index].cache_prev = INVALID_BLK;

	}
	s->cache_prev = cache_index;
	m_cache[cache_index].cache_next = INVALID_BLK;
	m_cache[cache_index].lba = lba;
	LOG_DEBUG_(1, L"write to cache (index=%d), lba=%d", cache_index, lba);
}


void CStorage::BlockWrite(LBLK_T lba, CPageInfo* page)
{
	JCASSERT(page);
	BLOCK_DATA* block = m_pages->get_data(page);	JCASSERT(block);
	// 检查cache是否满，
	if (((m_cache_tail + 1) % SSD_CACHE_SIZE) == m_cache_head)
	{	//	满了：head写入磁盘
		cache_deque(m_cache_head);
		m_cache_head++;
		m_cache_size--;
		if (m_cache_head >= SSD_CACHE_SIZE) m_cache_head = 0;
	}

	// 当前命令写入cache
	memcpy_s(&m_cache[m_cache_tail].data, sizeof(BLOCK_DATA), block, sizeof(BLOCK_DATA));
	cache_enque(lba, m_cache_tail);
	m_cache_tail++;
	m_cache_size++;
	if (m_cache_tail >= SSD_CACHE_SIZE) m_cache_tail = 0;
//	memcpy_s(m_data + lba, sizeof(BLOCK_DATA), block, sizeof(BLOCK_DATA));
}

void CStorage::BlockRead(LBLK_T lba, CPageInfo *page)
{
	BLOCK_DATA* dst = m_pages->get_data(page);
	// 检查数据是否在cache中
	BLOCK_DATA* src = nullptr;

	if (is_valid(m_data[lba].cache_prev)) {
		// 在cache中
		StorageEntry& cache = m_cache[ m_data[lba].cache_prev ];
		if (cache.lba != lba) {
			THROW_ERROR(ERR_APP, L"LBA (%d) in cache does not match request (%d).", cache.lba, lba);
		}
		src = &cache.data;
	}
	else {
		src = & m_data[lba].data;
	}
	memcpy_s(dst, sizeof(BLOCK_DATA), src, sizeof(BLOCK_DATA));
}

void CStorage::Sync(void)
{
	while (1)
	{
		if (m_cache_head == m_cache_tail) break;
		if (m_cache_tail == 0) m_cache_tail = SSD_CACHE_SIZE - 1;
		else m_cache_tail--;
		m_cache_size--;
		// 只需复制最新的cache到ssd
		if (is_invalid(m_cache[m_cache_tail].lba)) continue;
		// 复制cache到storage
		LBLK_T lba = m_cache[m_cache_tail].lba;
		BLOCK_DATA* dst = &m_data[lba].data;
		BLOCK_DATA* src = &m_cache[m_cache_tail].data;
		memcpy_s(dst, sizeof(BLOCK_DATA), src, sizeof(BLOCK_DATA));
		LOG_DEBUG_(1, L"sync: cache[%d] to lba=%d", m_cache_tail, lba);
		m_data[lba].cache_next = INVALID_BLK;
		m_data[lba].cache_prev = INVALID_BLK;
		// 清除cache chain
		LBLK_T cache = m_cache_tail;
		while (1)
		{
			LBLK_T prev = m_cache[cache].cache_prev;
			m_cache[cache].cache_next = INVALID_BLK;
			m_cache[cache].cache_prev = INVALID_BLK;
			m_cache[cache].lba = INVALID_BLK;
			cache = prev;
			if (is_invalid(cache)) break;
		}
	}
}

LBLK_T CStorage::GetCacheNum(void)
{
	return m_cache_size;
}

void CStorage::Rollback(LBLK_T nr)
{
	if (nr > m_cache_size) THROW_ERROR(ERR_APP, L"rollback (%d) > cache size (%d)", nr, m_cache_size);
	LBLK_T ii = 0;
	while (true)
	{
		if (m_cache_tail == m_cache_head) break;	// cache空
		if (m_cache_tail == 0) m_cache_tail = SSD_CACHE_SIZE - 1;
		else m_cache_tail--;
		m_cache_size--;

		LBLK_T lba = m_cache[m_cache_tail].lba;
		// sanity check
		if (lba > TOTAL_BLOCK_NR) THROW_ERROR(ERR_APP, L"invalid lba (%d) in cache[%d]", lba, m_cache_tail);
		if (is_valid(m_cache[m_cache_tail].cache_next)) {
			THROW_ERROR(ERR_APP, L"not the end of cache chain, next=%d", m_cache[m_cache_tail].cache_next);
		}
		LBLK_T prev = m_cache[m_cache_tail].cache_prev;
		if (is_valid(prev))
		{
			m_cache[prev].cache_next = INVALID_BLK;
			m_data[lba].cache_prev = prev;
		}
		else
		{
			m_data[lba].cache_next = INVALID_BLK;
			m_data[lba].cache_prev = INVALID_BLK;
		}
		// 清除m_cache_tail
		m_cache[m_cache_tail].cache_next = INVALID_BLK;
		m_cache[m_cache_tail].cache_prev = INVALID_BLK;
		m_cache[m_cache_tail].lba = INVALID_BLK;

		LOG_DEBUG_(1,L"deque: index=%d, lba=%d, new_index=%d", m_cache_tail, lba, prev);

		++ii;
		if ((lba==0 || lba>= SIT_START_BLK) && (ii >= nr)) break;
	}
}




/// <summary>
/// ///////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// file system simulator error handling
/// </summary>
/// <param name="code"></param>
/// <returns></returns>

const wchar_t* CFsException::ErrCodeToString(ERROR_CODE code)
{
	switch (code)
	{
	case ERR_OK:				return L"OK";
	case ERR_NO_OPERATION:		return L"No operation";	// 测试过程中，正常情况下，当前操作无法被执行。
	case ERR_NO_SPACE:			return L"No enough space";		// GC时发现空间不够，无法回收更多segment
//	case ERR_NO_SPACE:			return L"No ehough node";
//	case ERR_NO_SPACE:		return L"Dentry reaches max level";		// 文件夹的dentry已经满了: 
	case ERR_MAX_OPEN_FILE:		return L"Reached max open file";		// 打开的文件超过数量
//	case ERR_NO_SPACE:		return L"No resource";	// 由于资源不够放弃操作


	case ERR_GENERAL:			return L"General Error";
	case OK_ALREADY_EXIST:		return L"File already exist";			// 文件或者目录已经存在，但是结果返回true: 
	case ERR_CREATE_EXIST:		return L"File already exist";			// 对于已经存在的文件，重复创建。: 
	case ERR_CREATE:			return L"Create file failed";			// 文件或者目录不存在，但是创建失败:
	case ERR_OPEN_FILE:			return L"Open file failed";				// 试图打开一个已经存在的文件是出错:
	case ERR_GET_INFOMATION:	return L"Get file information failed";	// 获取File Informaiton时出错:
	case ERR_DELETE_FILE:		return L"Delete file failed";			// 删除文件时出错: 
	case ERR_DELETE_DIR:		return L"Delete directory failed";		// 删除目录时出错:
	case ERR_NON_EMPTY:			return L"Directory is not empty";		// 删除文件夹时，非空
	case ERR_READ_FILE:			return L"Failed on reading file";		// 读文件时出错: 
	case ERR_WRONG_FILE_SIZE:	return L"Wrong file size";				// :
	case ERR_WRONG_FILE_DATA:	return L"Wrong file data";
	case ERR_SYNC:				return L"Error happended in sync fs";	

		//			case ERR_UNKNOWN, :
	case ERR_PENDING:			return L"Test is running";				// 测试还在进行中: 
	case ERR_PARENT_NOT_EXIST:	return L"Parent directory not exist";	// 打开文件或者创建文件时，父目录不存在
	case ERR_WRONG_PATH:		return L"Wrong path format";			// 文件名格式不对，要求从\\开始
	case ERR_VERIFY_FILE:		return L"File compare fail";			// 文件比较时，长度不对

	case ERR_WRONG_BLOCK_TYPE:	return L"Wrong block type";				// block的类型不符
	case ERR_WRONG_FILE_TYPE:	return L"Wrong file/dir type";
	case ERR_WRONG_FILE_NUM:	return L"Wrong file number";			// 文件或者目录数量不匹配

	case ERR_SIT_MISMATCH:		return L"SIT mismatch";
	case ERR_PHY_ADDR_MISMATCH:	return L"Physical address mismatch";
	case ERR_INVALID_INDEX:		return L"Invalid index number";
	case ERR_INVALID_CHECKPOINT:return L"Invalid Checkpoint";
	case ERR_JOURNAL_OVERFLOW:	return L"Journal Overflow";

	case ERR_DEAD_NID:			return L"Dead NID";
	case ERR_LOST_NID:			return L"Lost NID";
	case ERR_DOUBLED_NID:		return L"Doubled NID"; 
	case ERR_INVALID_NID:		return L"Invalid NID";						// 不合法的nid, 或者不存在的nid/fid
	case ERR_DEAD_BLK:			return L"Dead Block";
	case ERR_LOST_BLK:			return L"Lost Block";
	case ERR_DOUBLED_BLK:		return L"Doubled Block"; 
	case ERR_INVALID_BLK:		return L"Invalid Block";						// 不合法的physical block id

	default:					return L"Unknown Error";
	}

}

#define EXCEPTION_BUF (512)
CFsException::CFsException(ERROR_CODE code, const wchar_t* func, int line, const wchar_t* msg, ...)
	: jcvos::CJCException(L"", jcvos::CJCException::ERR_APP, (UINT)code)
{
	wchar_t str[EXCEPTION_BUF + 2];
	int ptr = swprintf_s(str, L"[Exception] <%s:%d> err=%d (%s)", func, line, code, ErrCodeToString(code));
	va_list argptr;
	va_start(argptr, msg);
	vswprintf_s(str + ptr, EXCEPTION_BUF - ptr, msg, argptr);
	m_err_msg = str;
}

ERROR_CODE CFsException::get_error_code(void) const
{
	return ERROR_CODE(m_err_id & jcvos::CJCException::ERR_CODE_MASK);
}



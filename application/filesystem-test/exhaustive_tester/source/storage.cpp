///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
#include "pch.h"

#include "../include/storage.h"
#include "../include/f2fs_simulator.h"

LOCAL_LOGGER_ENABLE(L"simulator.storage", LOGGER_LEVEL_DEBUGINFO+1);

CStorage::CStorage(CF2fsSimulator* fs)
{
	m_pages = & fs->m_pages;
//	m_block_buf = &fs->m_block_buf;
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
	// ��ջ���
	for (UINT ii = 0; ii < TOTAL_BLOCK_NR; ++ii)
	{
		m_data[ii].cache_next = INVALID_FID;
		m_data[ii].cache_prev = INVALID_FID;
	}
	memset(m_cache, -1, sizeof(StorageEntry) * SSD_CACHE_SIZE);
	m_cache_head = 0;
	m_cache_tail = 0;
	m_cache_size = 0;
}

void CStorage::cache_deque(UINT cache_index)
{
	StorageEntry* cache = m_cache + cache_index;
	UINT lba = cache->lba;
	StorageEntry* s = m_data + lba;
	if (cache->cache_next == INVALID_FID) 
	{
		s->cache_next = INVALID_FID;
		s->cache_prev = INVALID_FID;
	}
	else
	{
		StorageEntry* next = m_cache + cache->cache_next;
		if (next->lba != lba) THROW_ERROR(ERR_APP, L"cache data not match, lba=%d, lba in cache=%d", lba, next->lba);
		next->cache_prev = INVALID_FID;
		s->cache_next = cache->cache_next;
	}
	memcpy_s(&(s->data), sizeof(BLOCK_DATA), &(cache->data), sizeof(BLOCK_DATA));
	LOG_DEBUG_(1, L"flush cache (index=%d) to storage lba=%d", cache_index, lba);
}

void CStorage::cache_enque(UINT lba, UINT cache_index)
{
	StorageEntry* s = m_data + lba;
	UINT prev = s->cache_prev;
	if (prev != INVALID_FID) {
		m_cache[prev].cache_next = cache_index;
		m_cache[cache_index].cache_prev = prev;
	}
	else
	{
		s->cache_next = cache_index;
		m_cache[cache_index].cache_prev = INVALID_FID;

	}
	s->cache_prev = cache_index;
	m_cache[cache_index].cache_next = INVALID_FID;
	m_cache[cache_index].lba = lba;
	LOG_DEBUG_(1, L"write to cache (index=%d), lba=%d", cache_index, lba);
}


void CStorage::BlockWrite(UINT lba, CPageInfo* page)
{
	JCASSERT(page);
	BLOCK_DATA* block = m_pages->get_data(page);	JCASSERT(block);
	// ���cache�Ƿ�����
	if (((m_cache_tail + 1) % SSD_CACHE_SIZE) == m_cache_head)
	{	//	���ˣ�headд�����
		cache_deque(m_cache_head);
		m_cache_head++;
		m_cache_size--;
		if (m_cache_head >= SSD_CACHE_SIZE) m_cache_head = 0;
	}

	// ��ǰ����д��cache
	memcpy_s(&m_cache[m_cache_tail].data, sizeof(BLOCK_DATA), block, sizeof(BLOCK_DATA));
	cache_enque(lba, m_cache_tail);
	m_cache_tail++;
	m_cache_size++;
	if (m_cache_tail >= SSD_CACHE_SIZE) m_cache_tail = 0;
//	memcpy_s(m_data + lba, sizeof(BLOCK_DATA), block, sizeof(BLOCK_DATA));
}

void CStorage::BlockRead(UINT lba, CPageInfo *page)
{
	BLOCK_DATA* dst = m_pages->get_data(page);
	// ��������Ƿ���cache��
	BLOCK_DATA* src = nullptr;

	if (m_data[lba].cache_prev != INVALID_FID) {
		// ��cache��
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
		// ֻ�踴�����µ�cache��ssd
		if (m_cache[m_cache_tail].lba == INVALID_BLK) continue;
		// ����cache��storage
		UINT lba = m_cache[m_cache_tail].lba;
		BLOCK_DATA* dst = &m_data[lba].data;
		BLOCK_DATA* src = &m_cache[m_cache_tail].data;
		memcpy_s(dst, sizeof(BLOCK_DATA), src, sizeof(BLOCK_DATA));
		LOG_DEBUG(L"sync: cache[%d] to lba=%d", m_cache_tail, lba);
		m_data[lba].cache_next = INVALID_FID;
		m_data[lba].cache_prev = INVALID_FID;
		// ���cache chain
		UINT cache = m_cache_tail;
		while (1)
		{
			UINT prev = m_cache[cache].cache_prev;
			m_cache[cache].cache_next = INVALID_FID;
			m_cache[cache].cache_prev = INVALID_FID;
			m_cache[cache].lba = INVALID_BLK;
			cache = prev;
			if (cache == INVALID_FID) break;
		}
	}
	
}

UINT CStorage::GetCacheNum(void)
{
	//if (m_cache_tail >= m_cache_head) return (m_cache_tail - m_cache_head);
	//return SSD_CACHE_SIZE - (m_cache_head - m_cache_tail);
	return m_cache_size;
}

void CStorage::Rollback(UINT nr)
{
	if (nr > m_cache_size) THROW_ERROR(ERR_APP, L"rollback (%d) > cache size (%d)", nr, m_cache_size);
	for (UINT ii = 0; ii < nr; ++ii)
	{
		if (m_cache_tail == m_cache_head) break;	// cache��
		if (m_cache_tail == 0) m_cache_tail = SSD_CACHE_SIZE - 1;
		else m_cache_tail--;
		m_cache_size--;

		UINT lba = m_cache[m_cache_tail].lba;
		// sanity check
		if (lba > TOTAL_BLOCK_NR) THROW_ERROR(ERR_APP, L"invalid lba (%d) in cache[%d]", lba, m_cache_tail);
		if (m_cache[m_cache_tail].cache_next != INVALID_FID) {
			THROW_ERROR(ERR_APP, L"not the end of cache chain, next=%d", m_cache[m_cache_tail].cache_next);
		}
		WORD prev = m_cache[m_cache_tail].cache_prev;
		if (prev != INVALID_FID)
		{
			m_cache[prev].cache_next = INVALID_FID;
			m_data[lba].cache_prev = prev;
		}
		else
		{
			m_data[lba].cache_next = INVALID_FID;
			m_data[lba].cache_prev = INVALID_FID;
		}
		// ���m_cache_tail
		m_cache[m_cache_tail].cache_next = INVALID_FID;
		m_cache[m_cache_tail].cache_prev = INVALID_FID;
		m_cache[m_cache_tail].lba = INVALID_BLK;

		LOG_DEBUG(L"deque: index=%d, lba=%d, new_index=%d", m_cache_tail, lba, prev);
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
	case ERR_GENERAL:			return L"General Error";
	case OK_ALREADY_EXIST:		return L"File already exist";			// �ļ�����Ŀ¼�Ѿ����ڣ����ǽ������true: 
	case ERR_CREATE_EXIST:		return L"File already exist";			// �����Ѿ����ڵ��ļ����ظ�������: 
	case ERR_CREATE:			return L"Create file failed";			// �ļ�����Ŀ¼�����ڣ����Ǵ���ʧ��:
	case ERR_OPEN_FILE:			return L"Open file failed";				// ��ͼ��һ���Ѿ����ڵ��ļ��ǳ���:
	case ERR_GET_INFOMATION:	return L"Get file information failed";	// ��ȡFile Informaitonʱ����:
	case ERR_DELETE_FILE:		return L"Delete file failed";			// ɾ���ļ�ʱ����: 
	case ERR_DELETE_DIR:		return L"Delete directory failed";		// ɾ��Ŀ¼ʱ����:
	case ERR_NON_EMPTY:			return L"Directory is not empty";		// ɾ���ļ���ʱ���ǿ�
	case ERR_READ_FILE:			return L"Failed on reading file";		// ���ļ�ʱ����: 
	case ERR_WRONG_FILE_SIZE:	return L"Wrong file size";				// :
	case ERR_WRONG_FILE_DATA:	return L"Wrong file data";
	case ERR_NODE_FULL:			return L"Node full";

		//			case ERR_UNKNOWN, :
	case ERR_PENDING:			return L"Test is running";				// ���Ի��ڽ�����: 
	case ERR_DENTRY_FULL:		return L"Dentry reaches max level";		// �ļ��е�dentry�Ѿ�����: 
	case ERR_MAX_OPEN_FILE:		return L"Reached max open file";		// �򿪵��ļ���������
	case ERR_PARENT_NOT_EXIST:	return L"Parent directory not exist";	// ���ļ����ߴ����ļ�ʱ����Ŀ¼������
	case ERR_WRONG_PATH:		return L"Wrong path format";			// �ļ�����ʽ���ԣ�Ҫ���\\��ʼ
	case ERR_VERIFY_FILE:		return L"File compare fail";			// �ļ��Ƚ�ʱ�����Ȳ���

	case ERR_WRONG_BLOCK_TYPE:	return L"Wrong block type";				// block�����Ͳ���
	case ERR_WRONG_FILE_TYPE:	return L"Wrong file/dir type";
	case ERR_WRONG_FILE_NUM:	return L"Wrong file number";			// �ļ�����Ŀ¼������ƥ��

	case ERR_SIT_MISMATCH:		return L"SIT mismatch";
	case ERR_PHY_ADDR_MISMATCH:	return L"Physical address mismatch";
	case ERR_INVALID_INDEX:		return L"Invalid index number";
	case ERR_INVALID_CHECKPOINT:return L"Invalid Checkpoint";
	case ERR_JOURNAL_OVERFLOW:	return L"Journal Overflow";

	case ERR_DEAD_NID:			return L"Dead NID";
	case ERR_LOST_NID:			return L"Lost NID";
	case ERR_DOUBLED_NID:		return L"Doubled NID"; 
	case ERR_INVALID_NID:		return L"Invalid NID";						// ���Ϸ���nid, ���߲����ڵ�nid/fid
	case ERR_DEAD_BLK:			return L"Dead Block";
	case ERR_LOST_BLK:			return L"Lost Block";
	case ERR_DOUBLED_BLK:		return L"Doubled Block"; 
	case ERR_INVALID_BLK:		return L"Invalid Block";						// ���Ϸ���physical block id

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



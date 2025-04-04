///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
#include "pch.h"

#include "../include/storage.h"
#include "../include/f2fs_simulator.h"

LOCAL_LOGGER_ENABLE(L"simulator.storage", LOGGER_LEVEL_DEBUGINFO);

LOG_CLASS_SIZE(StorageDataBase);


void StorageDataBase::Release(void) {
	long ref = InterlockedDecrement(&m_ref);
	if (ref == 0) {
		// 为避免递给深度太大，采用循环
		StorageDataBase* pre = pre_ver;
		while (pre)
		{
			long rr = InterlockedDecrement(&pre->m_ref);
			if (rr != 0) break;
			StorageDataBase* cur = pre;
			pre = cur->pre_ver;
			delete cur;
		}
		//LOG_DEBUG(L"delete StorageDataBase, ptr=%p", this);
		//if (pre_ver)
		//{
		//	LOG_DEBUG_(1, L"[storage] release storage data, lba=%d, ptr=%p, ref=%d", pre_ver->lba, pre_ver, pre_ver->m_ref);
		//	pre_ver->Release();
		//}
		delete this;
	}
}

CStorage::CStorage(CF2fsSimulator* fs)
{
	m_pages = & fs->m_pages;
}

CStorage::~CStorage()
{
	for (LBLK_T ii = 0; ii < TOTAL_BLOCK_NR; ++ii)
	{
		if (m_data[ii]) {
			LOG_DEBUG_(1, L"[storage] release storage data, storage=%p, lba=%d, ptr=%p, ref=%d", 
				this, m_data[ii]->lba, m_data[ii], m_data[ii]->m_ref);
			m_data[ii]->Release();
		}
	}
}

void CStorage::Initialize(void)
{
	memset(m_data, 0, sizeof(m_data));
	m_cache_tail = nullptr;
	m_cache_nr = 0;

	m_begin_cache = nullptr;
	m_begin_cache_nr = 0;
	m_parent = nullptr;

}

void CStorage::CopyFrom(const CStorage* src)
{
	//先清除原有记录
#if 1
	for (LBLK_T ii = 0; ii < TOTAL_BLOCK_NR; ++ii)
	{
		if (m_data[ii]) {
			LOG_DEBUG_(1, L"[storage] release storage data, storage=%p lba=%d, ptr=%p, ref=%d", 
				this, m_data[ii]->lba, m_data[ii], m_data[ii]->m_ref);
			m_data[ii]->Release();
		}
	}
#endif
	//复制新的数据
	memcpy_s(m_data, sizeof(m_data), src->m_data, sizeof(m_data));
	for (LBLK_T ii = 0; ii < TOTAL_BLOCK_NR; ++ii)
	{
		if (m_data[ii]) {
			m_data[ii]->AddRef();
			LOG_DEBUG_(1, L"[storage] add ref storage data, state=%p, lba=%d, ptr=%p, ref=%d", 
				this, m_data[ii]->lba, m_data[ii], m_data[ii]->m_ref);
		}
	}
	m_cache_tail = src->m_cache_tail;
	m_cache_nr = src->m_cache_nr;

	m_begin_cache = m_cache_tail;
	m_begin_cache_nr = m_cache_nr;
	m_parent = src;
	LOG_DEBUG(L"[dup storage], cur=%p, parent=%p", this, m_parent);
}

void CStorage::Reset(void)
{
	// 清空缓存
	//for (UINT ii = 0; ii < TOTAL_BLOCK_NR; ++ii)
	//{
	//	m_data[ii].cache_next = INVALID_BLK;
	//	m_data[ii].cache_prev = INVALID_BLK;
	//}
	//memset(m_cache, -1, sizeof(StorageEntry) * SSD_CACHE_SIZE);
	//m_cache_head = 0;
	//m_cache_tail = 0;
	//m_cache_size = 0;
}

void CStorage::DumpStorage(FILE* out)
{
	fprintf_s(out, "[Storage Trace]\n");
	StorageDataBase* data = m_cache_tail;
	LBLK_T first = -1, end = -1, count = 0;

	for (UINT ii = 0; ii < m_cache_nr; ++ii)
	{
		JCASSERT(data);
		LBLK_T lba = data->lba;
		if (is_invalid(lba))
		{	// show mark

		}
		else {
			if (lba + 1 == end) {	//merge
				end--; count++;
			}
			else {					// output
				if (is_valid(end)) fprintf_s(out, "\t Write: start_lba=%d, count=%d\n", end, count);
				// start new
				end = lba;
				count = 1;
			}
		}
		data = data->pre_write;
	}
	if (is_valid(end)) fprintf_s(out, "\t Write: start_lba=%d, count=%d\n", end, count);
}

void CStorage::cache_deque(LBLK_T cache_index)
{
	JCASSERT(0);
}

void CStorage::cache_enque(LBLK_T lba, LBLK_T cache_index)
{
	JCASSERT(0);
}


void CStorage::BlockWrite(LBLK_T lba, CPageInfo* page)
{
	JCASSERT(page);
	JCASSERT(lba < TOTAL_BLOCK_NR);
	StorageDataBase* data = new StorageDataBase;
	data->lba = lba;
	BLOCK_DATA* block = m_pages->get_data(page);	JCASSERT(block);
	memcpy_s(&data->data, sizeof(BLOCK_DATA), block, sizeof(BLOCK_DATA));


//	InterlockedExchangePointer((PVOID*)(&data->pre_ver), m_data[lba]);
//	InterlockedExchangePointer((PVOID*)(&m_data[lba]), data);
//	InterlockedExchangePointer((PVOID*)(&data->pre_write), m_cache_tail);
//	InterlockedExchangePointer((PVOID*)(&m_cache_tail), data);

	data->pre_ver = m_data[lba];
	m_data[lba] = data;
	data->pre_write = m_cache_tail;
	m_cache_tail = data;
	m_cache_nr++;
	LOG_DEBUG_(1, L"[storage] new storage data, lba=%d, ptr=%p, ref=%d", data->lba, data, data->m_ref);
	LOG_DEBUG_(0, L"write block, storage=%p, lba=%d, type=%d, ptr=%p, ref=%d, pre_ver=%p, pre_write=%p", 
		this, lba, block->m_type, data, data->m_ref, data->pre_ver, data->pre_write);
}

void CStorage::BlockRead(LBLK_T lba, CPageInfo *page)
{
	BLOCK_DATA* dst = m_pages->get_data(page);
	// 检查数据是否在cache中
	if (m_data[lba] == nullptr)
	{
		THROW_FS_ERROR(ERR_READ_NO_MAPPING_DATA, L"lba=%d is no mapping");
	}
	if (m_data[lba]->lba != lba)
	{
		LOG_DEBUG_(1,L"storage=%p, lba=%d, data=%p", this, lba, m_data[lba]);
		THROW_FS_ERROR(ERR_PHYSICAL_ADD_NOMATCH, L"request lba=%d, in data lba=%d", lba, m_data[lba]->lba);
	}
	BLOCK_DATA* src = &m_data[lba]->data;
	LOG_DEBUG_(1,L"read block, lba=%d, type=%d", lba, src->m_type);

	memcpy_s(dst, sizeof(BLOCK_DATA), src, sizeof(BLOCK_DATA));
}

void CStorage::Sync(void)
{
	//while (1)
	//{
	//	if (m_cache_head == m_cache_tail) break;
	//	if (m_cache_tail == 0) m_cache_tail = SSD_CACHE_SIZE - 1;
	//	else m_cache_tail--;
	//	m_cache_size--;
	//	// 只需复制最新的cache到ssd
	//	if (is_invalid(m_cache[m_cache_tail].lba)) continue;
	//	// 复制cache到storage
	//	LBLK_T lba = m_cache[m_cache_tail].lba;
	//	BLOCK_DATA* dst = &m_data[lba].data;
	//	BLOCK_DATA* src = &m_cache[m_cache_tail].data;
	//	memcpy_s(dst, sizeof(BLOCK_DATA), src, sizeof(BLOCK_DATA));
	//	LOG_DEBUG_(1, L"sync: cache[%d] to lba=%d", m_cache_tail, lba);
	//	m_data[lba].cache_next = INVALID_BLK;
	//	m_data[lba].cache_prev = INVALID_BLK;
	//	// 清除cache chain
	//	LBLK_T cache = m_cache_tail;
	//	while (1)
	//	{
	//		LBLK_T prev = m_cache[cache].cache_prev;
	//		m_cache[cache].cache_next = INVALID_BLK;
	//		m_cache[cache].cache_prev = INVALID_BLK;
	//		m_cache[cache].lba = INVALID_BLK;
	//		cache = prev;
	//		if (is_invalid(cache)) break;
	//	}
	//}
}

LBLK_T CStorage::GetCacheNum(void)
{
	JCASSERT(m_cache_nr >= m_begin_cache_nr);
	return m_cache_nr - m_begin_cache_nr;
}

void CStorage::Rollback(LBLK_T nr)
{
	if (nr > m_cache_nr) THROW_ERROR(ERR_APP, L"rollback (%d) > cache size (%d)", nr, m_cache_nr);
	LBLK_T ii = 0;

	for (int ii = 0; ii < nr; ++ii)
	{
		StorageDataBase* data = m_cache_tail;
//		InterlockedExchangePointer((PVOID*)(&m_cache_tail), m_cache_tail->pre_write);
		m_cache_tail = data->pre_write;	
		m_cache_nr--;
		if (m_parent) {
//			JCASSERT(m_cache_nr >= m_parent->m_begin_cache_nr);
		}
		if (m_cache_nr < m_begin_cache_nr) m_begin_cache_nr = m_cache_nr;
		LBLK_T lba = data->lba;
		if (is_invalid(lba)) {
			// mark, 简单放弃
		}
		else
		{
			if (lba >= TOTAL_BLOCK_NR) {
				THROW_FS_ERROR(ERR_PHYSICAL_ADD_NOMATCH, L"lba (%d) in cache is invalid", lba);
			}
//			InterlockedExchangePointer((PVOID*)(&m_data[lba]), data->pre_ver);
			m_data[lba] = data->pre_ver;
			if (m_data[lba]) m_data[lba]->AddRef();
			LOG_DEBUG_(1, L"[storage] release storage data, storage=%p, lba=%d, ptr=%p, ref=%d, pre_ptr=%p, pre_ref=%d", 
				this, lba, data, data->m_ref, m_data[lba], m_data[lba]?m_data[lba]->m_ref:0);
			data->Release();
		}
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
	case ERR_MAX_OPEN_FILE:		return L"Reached max open file";		// 打开的文件超过数量

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

	case ERR_PENDING:			return L"Test is running";				// 测试还在进行中: 
	case ERR_PARENT_NOT_EXIST:	return L"Parent directory not exist";	// 打开文件或者创建文件时，父目录不存在
	case ERR_WRONG_PATH:		return L"Wrong path format";			// 文件名格式不对，要求从\\开始
	case ERR_VERIFY_FILE:		return L"File compare fail";			// 文件比较时，长度不对
	case ERR_CKPT_FAIL:			return L"Ckeckpoint failed";			// Ckeckpoint 错误。两个同时有效且版本相等，或者两个都无效

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



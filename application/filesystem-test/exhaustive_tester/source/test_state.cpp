///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
#include "pch.h"
#include "../include/extester.h"
#include <Psapi.h>
#include <boost/property_tree/json_parser.hpp>

LOCAL_LOGGER_ENABLE(L"extester.state", LOGGER_LEVEL_DEBUGINFO);

LOG_CLASS_SIZE(CReferenceFs);
LOG_CLASS_SIZE(CFsState);


///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//== State ==

CFsState::~CFsState(void)
{
	if (m_real_fs) m_real_fs->release();
//	delete m_real_fs;
}

void CFsState::OutputState(FILE* log_file)
{
	// output ref fs
	JCASSERT(log_file);

	// ���Encode
	ENCODE encode;
	size_t len = m_ref_fs.Encode(encode.code, MAX_ENCODE_SIZE);
	char* str_encode = (char*)(encode.code);

	fprintf_s(log_file, "[REF FS]: encode={%s}\n", str_encode);
	auto endit = m_ref_fs.End();
	auto it = m_ref_fs.Begin();
	for (; it != endit; it++)
	{
		const CReferenceFs::CRefFile& ref_file = m_ref_fs.GetFile(it);
		std::string path;
		m_ref_fs.GetFilePath(ref_file, path);
		bool dir = m_ref_fs.IsDir(ref_file);
		DWORD ref_checksum;
		FSIZE ref_len;
		m_ref_fs.GetFileInfo(ref_file, ref_checksum, ref_len);
		fprintf_s(log_file, "<%s> %s : ", dir ? "dir " : "file", path.c_str());
		if (dir)	fprintf_s(log_file, "children=%d\n", ref_checksum);
		else		fprintf_s(log_file, "size=%d, checksum=0x%08X\n", ref_len, ref_checksum);
	}
	// output op
	char str[256];
	Op2String(str, m_op);
	fprintf_s(log_file, "%s\n", str);
	fprintf_s(log_file, "[END of REF FS]\n");
}

void CFsState::DuplicateFrom(CFsState* src_state)
{
	m_ref_fs.CopyFrom(src_state->m_ref_fs);
	if (m_real_fs == nullptr) 	src_state->m_real_fs->Clone(m_real_fs);
	else m_real_fs->CopyFrom(src_state->m_real_fs);
	m_depth = src_state->m_depth + 1;
	m_parent = src_state;
	src_state->m_ref++;
}

void CFsState::DuplicateWithoutFs(CFsState* src_state)
{
	m_ref_fs.CopyFrom(src_state->m_ref_fs);
	JCASSERT(m_real_fs == nullptr);
	m_real_fs = src_state->m_real_fs;
	m_real_fs->add_ref();
	m_depth = src_state->m_depth + 1;
	m_parent = src_state;
	src_state->m_ref++;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//== State Manager ==
CStateManager::CStateManager(void)
{
	m_duplicate_real_fs = true;
	InitializeCriticalSection(&m_lock);
	// for debug
	//memset(states, 0, sizeof(states));
}

CStateManager::~CStateManager(void)
{	// ���free
	while (m_free_list)
	{
		CFsState* next = m_free_list->m_parent;
		delete m_free_list;
		m_free_list = next;
		m_free_nr--;
	}
	LOG_DEBUG(L"free = %zd", m_free_nr);
	DeleteCriticalSection(&m_lock);
}

void CStateManager::Initialize(size_t size, bool duplicate_real_fs)
{
	m_duplicate_real_fs = duplicate_real_fs;
}

CFsState* CStateManager::get(void)
{
	CFsState* new_state = nullptr;
#ifdef STATE_MANAGER_THREAD_SAFE
	EnterCriticalSection(&m_lock);
#endif 
	if (m_free_nr > 0)
	{
		new_state = m_free_list;
		m_free_list = new_state->m_parent;
		new_state->m_parent = nullptr;
		m_free_nr--;
	}
#ifdef STATE_MANAGER_THREAD_SAFE
	LeaveCriticalSection(&m_lock);
#endif
	if (new_state == nullptr)	{
		new_state = new CFsState;
		//states[m_buffer_size] = new_state;
		//m_buffer_size++;
	}
	new_state->m_ref = 1;
	return new_state;
}

void CStateManager::put(CFsState*& state)
{
	while (state)
	{
//		UINT ref = InterlockedDecrement(&state->m_ref);
		UINT ref = --(state->m_ref);
		if (ref != 0) break;
		if (!m_duplicate_real_fs) {
			state->m_real_fs->release();
			state->m_real_fs = nullptr;
		}
		CFsState* pp = state->m_parent;
		// ����free list
#ifdef STATE_MANAGER_THREAD_SAFE
		EnterCriticalSection(&m_lock);
#endif
		state->m_parent = m_free_list;
		m_free_list = state;
		m_free_nr++;
#ifdef STATE_MANAGER_THREAD_SAFE
		LeaveCriticalSection(&m_lock);
#endif
		// continue put parent
		state = pp;
	}
	state = nullptr;
}

CFsState* CStateManager::duplicate(CFsState* state)
{
	CFsState* new_state = get();
	//	LOG_DEBUG_(1, L"duplicate state: <%p>", new_state);
	if (m_duplicate_real_fs)	new_state->DuplicateFrom(state);
	else						new_state->DuplicateWithoutFs(state);
	return new_state;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//== State heap and hash code ==

bool CStateHeap::Check(const CFsState* state)
{
	const CReferenceFs& fs = state->m_ref_fs;
	ENCODE encode;
	size_t len = fs.Encode(encode.code, MAX_ENCODE_SIZE);
	auto it = m_fs_state.find(encode);
	bool exist = (it != m_fs_state.end());
	LOG_DEBUG(L"fs encode=%S, exist=%d", encode.code, exist);
	return exist;
}


void CStateHeap::Insert(const CFsState* state)
{
	const CReferenceFs& fs = state->m_ref_fs;
	ENCODE encode;
	size_t len = fs.Encode(encode.code, MAX_ENCODE_SIZE);
	m_fs_state.insert(encode);
}

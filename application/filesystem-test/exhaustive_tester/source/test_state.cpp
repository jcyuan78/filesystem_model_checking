///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
#include "pch.h"
#include "../include/extester.h"
#include <Psapi.h>
#include <boost/property_tree/json_parser.hpp>

LOCAL_LOGGER_ENABLE(L"extester.state", LOGGER_LEVEL_DEBUGINFO);


///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//== State ==

CFsState::~CFsState(void)
{
	delete m_real_fs;
}

void CFsState::OutputState(FILE* log_file)
{
	// output ref fs
	JCASSERT(log_file);

	// 检查Encode
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
	LOG_STACK_PERFORM(L"state_duplication");
	m_ref_fs.CopyFrom(src_state->m_ref_fs);
	if (m_real_fs == nullptr) 	src_state->m_real_fs->Clone(m_real_fs);
	else m_real_fs->CopyFrom(src_state->m_real_fs);
	m_depth = src_state->m_depth + 1;
	m_parent = src_state;
	m_ref = 1;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//== State Manager ==
CStateManager::CStateManager(void)
{
}

CStateManager::~CStateManager(void)
{	// 清空free
	while (m_free_list)
	{
		CFsState* next = m_free_list->m_parent;
		delete m_free_list;
		m_free_list = next;
		m_free_nr--;
	}
	LOG_DEBUG(L"free = %zd", m_free_nr);
}

void CStateManager::Initialize(size_t size)
{
}

CFsState* CStateManager::get(void)
{
	CFsState* new_state = nullptr;
	if (m_free_nr > 0)
	{
		new_state = m_free_list;
		m_free_list = new_state->m_parent;
		new_state->m_parent = nullptr;
		m_free_nr--;
	}
	else
	{
		new_state = new CFsState;
	}
	return new_state;
}

void CStateManager::put(CFsState*& state)
{
	while (state)
	{
		UINT ref = InterlockedDecrement(&state->m_ref);
		if (ref != 0) break;
		CFsState* pp = state->m_parent;
		// 放入free list
		state->m_parent = m_free_list;
		m_free_list = state;
		m_free_nr++;

		state = pp;
	}
	state = nullptr;
}

CFsState* CStateManager::duplicate(CFsState* state)
{
	CFsState* new_state = get();
	//	LOG_DEBUG_(1, L"duplicate state: <%p>", new_state);
	new_state->DuplicateFrom(state);
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

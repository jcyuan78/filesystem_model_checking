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
	delete m_err_msg;
}

void CFsState::OutputState(FILE* fp, DWORD option) const
{
	char str_encode[MAX_ENCODE_SIZE];

	FSIZE logic_blk = 0;
	fprintf_s(fp, "state=%p, parent=%p, depth=%d\n", this, m_parent, m_depth);
	IFsSimulator* fs = m_real_fs;
	CReferenceFs& ref_fs = const_cast<CReferenceFs &>(m_ref_fs);

	if (option & CExTester::TRACE_REAL_FS) {
		FS_INFO fs_info;
		UINT real_file_nr = 0, real_dir_nr = 0;
		try {
			fs->GetFsInfo(fs_info);
			fs->GetFileDirNum(0, real_file_nr, real_dir_nr);		// TODO: os需要保存file number以供检查
		}
		catch (jcvos::CJCException& err)
		{
			CFsException* _err = dynamic_cast<CFsException*>(&err);
			ERROR_CODE ir = ERR_UNKNOWN;
			if (_err) ir = _err->get_error_code();
			printf_s("Test failed with code=%d, (%S) exception: %s\n", ir, CFsException::ErrCodeToString(ir), err.what());
		}

		fprintf_s(fp, "\treal_fs: dir=%d, files=%d, logic=%d, physic=%d, seg:free/total=%d / %d, blk:free/total=%d / %d, \n",
			real_dir_nr, real_file_nr, fs_info.used_blk, fs_info.used_seg * BLOCK_PER_SEG,
			fs_info.free_seg, fs_info.main_seg_nr, fs_info.free_blk, fs_info.main_blk_nr);
	}

	if (option & CExTester::TRACE_REF_FS) {
		UINT ref_dir_nr = ref_fs.m_dir_num, ref_file_nr = ref_fs.m_file_num;
		//			if (option & TRACE_ENCODE)	ref_fs.Encode(str_encode, MAX_ENCODE_SIZE);
		//			else str_encode[0] = 0;
		int opcode_len = HIBYTE(HIWORD(option));
		ref_fs.Encode(str_encode, MAX_ENCODE_SIZE, opcode_len);
		fprintf_s(fp, "\tref_fs: dir=%d, file=%d, encode=%s\n ", ref_dir_nr, ref_file_nr, str_encode);
	}

	if (option & CExTester::TRACE_FILES) {
		auto endit = ref_fs.End();
		for (auto it = ref_fs.Begin(); it != endit; ++it)
		{
			const CReferenceFs::CRefFile& ref_file = ref_fs.GetFile(it);
//			if (option & CExTester::TRACE_ENCODE)		ref_file.GetEncodeString(str_encode, MAX_ENCODE_SIZE);

			std::string path;
			ref_fs.GetFilePath(ref_file, path);
			bool dir = ref_fs.IsDir(ref_file);
			FSIZE ref_len = 0;
			if (!dir)
			{
				DWORD ref_checksum;
				ref_fs.GetFileInfo(ref_file, ref_checksum, ref_len);
			}
			static const size_t str_size = (INDEX_TABLE_SIZE * 10);
			FSIZE file_size = ref_len, file_blk_nr = 0, file_index_nr = 0;		// 文件占用的block数量，包括inode和index node
			logic_blk += (file_blk_nr + file_index_nr);
			fprintf_s(fp, "\t\t<check %s> (fid=%03d) %s,\t\t blk_nr=%d, size=%d\n",
				dir ? "dir " : "file", ref_file.get_fid(), path.c_str(), (file_blk_nr + file_index_nr), file_size);
		}
	}

	if (option & CExTester::TRACE_DUMP_FS) {
		IFsSimulator* fs = m_real_fs;
		m_real_fs->DumpFs(fp);
	}

	char str[256];
	Op2String(str, m_op);
	fprintf_s(fp, "\t%s, result=%d (%S) %s\n", str, this->m_result, CFsException::ErrCodeToString(m_result),
		m_err_msg ? m_err_msg->c_str() : "");

	// output ref fs
//	JCASSERT(log_file);

//	// 检查Encode
//	ENCODE encode;
//	size_t len = m_ref_fs.Encode(encode.code, MAX_ENCODE_SIZE);
//	char* str_encode = (char*)(encode.code);
//
//	fprintf_s(log_file, "[REF FS]: encode={%s}\n", str_encode);
//	auto endit = m_ref_fs.End();
//	auto it = m_ref_fs.Begin();
//	for (; it != endit; ++it)
//	{
//		const CReferenceFs::CRefFile& ref_file = m_ref_fs.GetFile(it);
//		std::string path;
//		m_ref_fs.GetFilePath(ref_file, path);
//		bool dir = m_ref_fs.IsDir(ref_file);
//		DWORD ref_checksum;
//		FSIZE ref_len;
//		m_ref_fs.GetFileInfo(ref_file, ref_checksum, ref_len);
//		fprintf_s(log_file, "<%s> %s : ", dir ? "dir " : "file", path.c_str());
//		if (dir)	fprintf_s(log_file, "children=%d\n", ref_checksum);
//		else		fprintf_s(log_file, "size=%d, checksum=0x%08X\n", ref_len, ref_checksum);
//	}
//	// output op
//	char str[256];
//	Op2String(str, m_op);
//	fprintf_s(log_file, "%s\n", str);
//	fprintf_s(log_file, "[END of REF FS]\n");
}

void CFsState::DuplicateFrom(CFsState* src_state)
{
	m_ref_fs.CopyFrom(src_state->m_ref_fs);
	if (m_real_fs == nullptr) 	src_state->m_real_fs->Clone(m_real_fs);
	else m_real_fs->CopyFrom(src_state->m_real_fs);
	m_depth = src_state->m_depth + 1;
	m_parent = src_state;
//	m_io_nr = src_state->m_io_nr;
	m_result = ERR_OK;
	if (m_err_msg) { m_err_msg->clear(); }
	InterlockedIncrement(&src_state->m_ref);
}

void CFsState::DuplicateWithoutFs(CFsState* src_state)
{
	m_ref_fs.CopyFrom(src_state->m_ref_fs);
	JCASSERT(m_real_fs == nullptr);
	m_real_fs = src_state->m_real_fs;
	InterlockedIncrement(&src_state->m_ref);
	m_real_fs->add_ref();
	m_depth = src_state->m_depth + 1;
	m_parent = src_state;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//== State Manager ==
CStateManager::CStateManager(void)
{
	m_duplicate_real_fs = true;
	InitializeCriticalSection(&m_lock);
}

CStateManager::~CStateManager(void)
{	// 清空free
	LOG_STACK_TRACE();
	while (m_free_list)
	{
		CFsState* next = m_free_list->m_parent;
		delete m_free_list;
		m_free_list = next;
		m_free_nr--;
		m_allocated--;
	}
	LOG_DEBUG(L"allocated=%d, free=%zd", m_allocated, m_free_nr);
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
		m_allocated++;
	}
	// reset state
	new_state->m_stable = false;
	new_state->m_ref = 1;
	return new_state;
}

void CStateManager::put(CFsState*& state)
{
	while (state)
	{
		UINT ref = InterlockedDecrement(&state->m_ref);
		if (ref != 0) break;
		if (!m_duplicate_real_fs && state->m_real_fs!=nullptr) {
			state->m_real_fs->release();
			state->m_real_fs = nullptr;
		}
		CFsState* pp = state->m_parent;
		// 放入free list
#if 1
#ifdef STATE_MANAGER_THREAD_SAFE
		EnterCriticalSection(&m_lock);
#endif
		// reset state
		state->m_parent = m_free_list;
		m_free_list = state;
		m_free_nr++;
#ifdef STATE_MANAGER_THREAD_SAFE
		LeaveCriticalSection(&m_lock);
#endif
#else
		// 不放入free list
		delete state;

#endif
		// continue put parent
		state = pp;
	}
	state = nullptr;
}

CFsState* CStateManager::duplicate(CFsState* state)
{
	LOG_STACK_TRACE();
	CFsState* new_state = get();
	//	LOG_DEBUG_(1, L"duplicate state: <%p>", new_state);
	if (m_duplicate_real_fs)	new_state->DuplicateFrom(state);
	else						new_state->DuplicateWithoutFs(state);
	return new_state;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//== State heap and hash code ==

#if defined USE_BOOST_HASH

CStateHeap::CStateHeap() {};
CStateHeap::~CStateHeap() {};

/*
bool CStateHeap::Check(const CFsState* state)
{
	const CReferenceFs& fs = state->m_ref_fs;
	ENCODE encode;
	size_t len = fs.Encode(encode.code, MAX_ENCODE_SIZE);
	auto it = m_fs_state.find(encode);
	bool exist = false;	// 存在相同节点，且深度小于当前深度
	if (it != m_fs_state.end())
	{
		LOG_DEBUG(L"current depth=%d", it->second);
		if (it->second <= state->m_depth) exist = true;
		else exist = false;

	}
	//bool exist = (it != m_fs_state.end());
	//LOG_DEBUG(L"fs encode=%S, exist=%d", encode.code, exist);
	return exist;
}

void CStateHeap::Insert(const CFsState* state)
{
	const CReferenceFs& fs = state->m_ref_fs;
	ENCODE encode;
	size_t len = fs.Encode(encode.code, MAX_ENCODE_SIZE);
	auto res = m_fs_state.insert(std::make_pair(encode, state->m_depth));
	auto it = m_fs_state.find(encode);
	LOG_DEBUG(L"inser result=%d, depth=%d", res.second, it->second);
//	(encode);
}
*/

struct OutState {
	const char encode[MAX_ENCODE_SIZE];
	UINT index;
};

static OutState g_out_states[] =
{
	{"[(2#)[[0]0]1]0M", 0},
	{"[(2#)[[0]0]0]0W", 0},
	{"[(2#)[[0]0]1]0", 0},
	{"[(2#)[[0]0]0]0", 0},
	{"[(1#)[[0]0]0]0", 0},
	{"[(1#)[0]0]0", 0},
	{"[(0#)[0]0]0", 0},
//	{"[[0]0]0", 0},
};

void DebugShowConfilictState(const char * encode, size_t encode_len, const CFsState* state, int code_len)
{
	char fn[MAX_PATH];
	char trace[256];
	DWORD state_option = CExTester::TRACE_REF_FS | CExTester::TRACE_FILES;
	state_option |= (code_len << 24);	// high byte for opcode length
	for (size_t ii=0; ii< sizeof(g_out_states) / sizeof(g_out_states[0]); ii++)
	{
		size_t len = strlen(g_out_states[ii].encode);
		if (memcmp(g_out_states[ii].encode, encode, len) == 0)
		{
			if (g_out_states[ii].index < 10) {
				// output state
				sprintf_s(fn, "%s-%d.txt", g_out_states[ii].encode, g_out_states[ii].index);
				FILE* fp = nullptr;
				fopen_s(&fp, fn, "w");
				if (!fp) THROW_WIN32_ERROR(L"failed on creating log file: %S", fn);
				state->OutputState(fp, state_option);
				// output trace
				
				fprintf_s(fp, "TRACE:\n");
				const CFsState* cur = state;
				while (cur) {
//					const TRACE_ENTRY& op = cur->m_op;
					Op2String(trace, cur->m_op);
					fprintf_s(fp, "\t%s, result=%d (%S) %s\n", trace, cur->m_result, CFsException::ErrCodeToString(cur->m_result),
						cur->m_err_msg ? cur->m_err_msg->c_str() : "");
					cur = cur->m_parent;
				}
				fclose(fp);
				g_out_states[ii].index++;
			}
			return;
		}
	}
}

bool CStateHeap::CheckAndInsert(const CFsState* state)
{
	const CReferenceFs& fs = state->m_ref_fs;
	ENCODE encode;
	size_t len = fs.Encode(encode.code, MAX_ENCODE_SIZE, m_opcode_len);
	auto it = m_fs_state.find(encode);
	bool exist = false;	// 存在相同节点，且深度小于当前深度
	if (it != m_fs_state.end())
	{	// state 存在，输出旧的state和新的state
//		DebugShowConfilictState(encode.code, len, state,m_opcode_len);

		if (it->second <= state->m_depth) exist = true;
		else {// 替换
			it->second = state->m_depth;
			exist = false;
		}
	}
	else
	{	// 直接插入
//		DebugShowConfilictState(encode.code, len, state, m_opcode_len);

		auto res = m_fs_state.insert(std::make_pair(encode, state->m_depth));
		JCASSERT(res.second);
		exist = false;
	}
	return exist;
}

#elif defined USE_HASH_ARRAY

#define MAX_HEAP_SIZE	(0x40000000)

CStateHeap::CStateHeap()
{
	m_states = new WORD[MAX_HEAP_SIZE];
	memset(m_states, 0xFF, sizeof(WORD) * MAX_HEAP_SIZE);
}

CStateHeap::~CStateHeap()
{
	delete[] m_states;
}


//bool CStateHeap::Check(const CFsState* state)
//{
//	JCASSERT(0);
//	return false;
//}
//void CStateHeap::Insert(const CFsState* state)
//{
//	JCASSERT(0);
//}

bool CStateHeap::CheckAndInsert(const CFsState* state)
{
	static const uint32_t seed = 42;
	const CReferenceFs& fs = state->m_ref_fs;
	ENCODE encode;
	size_t len = fs.Encode(encode.code, MAX_ENCODE_SIZE);
	
	UINT64 hash[2];
	MurmurHash3_x64_128(encode.code, len, seed, hash);

	UINT hh = hash[0] & (MAX_HEAP_SIZE -1);

//	auto it = m_fs_state.find(encode);
	bool exist = false;	// 存在相同节点，且深度小于当前深度
	WORD dd = m_states[hh];
	if (dd != 0xFFFF)
	{	// 存在
		if (dd <= state->m_depth) exist = true;
		else {// 替换
			m_states[hh] = state->m_depth;
			exist = false;
		}
	}
	else
	{	// 直接插入
		m_states[hh] = state->m_depth;
		m_hashed++;
//		auto res = m_fs_state.insert(std::make_pair(encode, state->m_depth));
//		JCASSERT(res.second);
		exist = false;
	}
	return exist;


}


#endif
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
#include "pch.h"
#include "../include/extester.h"
#include <Psapi.h>

LOCAL_LOGGER_ENABLE(L"extester", LOGGER_LEVEL_DEBUGINFO);

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
	int len = m_ref_fs.Encode(encode.code);
	char* str_encode = (char*)(encode.code);

	fprintf_s(log_file, "[REF FS]: encode={%s}\n", str_encode);
	auto endit = m_ref_fs.End();
	auto it = m_ref_fs.Begin();
	for (; it != endit; it++)
	{
		const CReferenceFs::CRefFile& ref_file = m_ref_fs.GetFile(it);
		std::wstring path;
		m_ref_fs.GetFilePath(ref_file, path);
		bool dir = m_ref_fs.IsDir(ref_file);
		DWORD ref_checksum;
		FSIZE ref_len;
		m_ref_fs.GetFileInfo(ref_file, ref_checksum, ref_len);
		fprintf_s(log_file, "<%s> %S : ", dir ? "dir " : "file", path.c_str());
		if (dir)	fprintf_s(log_file, "children=%d\n", ref_checksum);
		else		fprintf_s(log_file, "size=%d, checksum=0x%08X\n", ref_len, ref_checksum);

	}
//	if (m_cur_op == 0) return;	// 当前状态无效
	// output op
	TRACE_ENTRY& op = m_op;
	const char* op_name = NULL;
	char str_param[128] = "";
	switch (op.op_code)
	{
	case OP_CODE::OP_NOP:			op_name = "none       ";	break;
	case OP_CODE::OP_FILE_CREATE:	op_name = "create-file";	break;
	case OP_CODE::OP_DIR_CREATE:	op_name = "create-dir ";	break;
	case OP_CODE::OP_FILE_DELETE:	op_name = "delete-file";	break;
	case OP_CODE::OP_DIR_DELETE:	op_name = "delete-dir ";	break;
	case OP_CODE::OP_MOVE:			op_name = "move       ";	break;
		//case OP_CODE::APPEND_FILE:	op_name = L"append     ";	break;
	case OP_CODE::OP_FILE_WRITE:	op_name = "overwrite  ";
		sprintf_s(str_param, "offset=%d, secs=%d", op.offset, op.length);
		break;
	case OP_CODE::OP_DEMOUNT_MOUNT:	op_name = "demnt-mount";	break;
	default:						op_name = "unknown    ";	break;
	}
	fprintf_s(log_file, "op:(%d) [%s], path=%S, param: %s\n", op.op_sn, op_name, op.file_path.c_str(), str_param);
	fprintf_s(log_file, "[END of REF FS]\n");
}

void CFsState::DuplicateFrom(CFsState* src_state)
{
	m_ref_fs.CopyFrom(src_state->m_ref_fs);
	src_state->m_real_fs->Clone(m_real_fs);
	m_depth = src_state->m_depth + 1;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//== State Manager ==
CStateManager::CStateManager(void)
{
}

CStateManager::~CStateManager(void)
{
}

void CStateManager::Initialize(size_t size)
{
}

CFsState* CStateManager::get(void)
{
	return new CFsState();
}

void CStateManager::put(CFsState* &state)
{
	delete state;
	state = nullptr;
}

CFsState* CStateManager::duplicate(CFsState* state)
{
	CFsState* new_state = new CFsState;
	new_state->DuplicateFrom(state);
	return new_state;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//== State heap and hash code ==

bool CStateHeap::Check(const CFsState* state)
{
	const CReferenceFs& fs = state->m_ref_fs;
	ENCODE encode;
	int len = fs.Encode(encode.code);
	auto it = m_fs_state.find(encode);
	return it != m_fs_state.end();
}


void CStateHeap::Insert(const CFsState* state)
{
	const CReferenceFs& fs = state->m_ref_fs;
	ENCODE encode;
	int len = fs.Encode(encode.code);
	m_fs_state.insert(encode);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//== Help functions ==
void GenerateFn(wchar_t* fn, size_t len)
{
	size_t ii;
	for (ii = 0; ii < len; ++ii)
	{
		fn[ii] = rand() % 26 + 'A';
	}
	fn[ii] = 0;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//== Exahustive Tester ==
CExTester::CExTester(void)
{
}

CExTester::~CExTester(void)
{
//	RELEASE(m_fs_factory);
}



int CExTester::PrepareTest(const boost::property_tree::wptree& config, IFsSimulator * fs, const std::wstring & log_path)
{
//	m_root = config.get<std::wstring>(L"root", L"");
	JCASSERT(fs);
	m_fs_factory = fs;
//	m_fs_factory->AddRef();

	m_timeout = config.get<DWORD>(L"timeout", INFINITE);
	m_update_ms = config.get<DWORD>(L"message_interval", 30);		// 以秒为单位的更新时间
	m_format = config.get<bool>(L"format_before_test", false);
	m_mount = config.get<bool>(L"mount_before_test", false);
//	m_volume_size = config.get<size_t>(L"volume_size", 0);

	//m_root = config.get<std::wstring>(L"root_dir");
	m_test_depth = config.get<int>(L"depth", 5);
	m_max_child_num = config.get<size_t>(L"max_child", 5);
	m_max_dir_depth = config.get<size_t>(L"max_dir_depth", 5);
	m_max_file_op = config.get<size_t>(L"max_file_op", 5);

	// 文件系统支持的最大文件大小。
	FSIZE max_file_secs = m_fs_factory->MaxFileSize();
	m_max_file_size = config.get<FSIZE>(L"max_file_size", 1048576);
	if (m_max_file_size > max_file_secs)
	{
		LOG_WARNING(L"[warning] max file size %zd (secs) > file system supported %zd (secs)", m_max_file_size, max_file_secs);
		m_max_file_size = max_file_secs;
	}

	m_clear_temp = config.get<int>(L"clear_temp", 1);
	// 读取测试项目
	const boost::property_tree::wptree& pt_ops = config.get_child(L"operaters");
	for (auto it = pt_ops.begin(); it != pt_ops.end(); it++)
	{
		if (it->first != L"operater") continue;
		const boost::property_tree::wptree& pt_op = it->second;
		const std::wstring& str_op = pt_op.get<std::wstring>(L"<xmlattr>.name");
		OP_CODE op_id = StringToOpId(str_op);
		const std::wstring& str_cond = pt_op.get<std::wstring>(L"<xmlattr>.condition");
		if (str_cond == L"File") m_file_op_set.push_back(op_id);
		else if (str_cond == L"Dir") m_dir_op_set.push_back(op_id);
		else if (str_cond == L"FS") m_fs_op_set.push_back(op_id);
	}

	// 设置log文件夹
	m_log_path = log_path;
	std::wstring trace_log_fn = m_log_path + L"\\log.txt";
	m_log_file = _wfsopen(trace_log_fn.c_str(), L"w+", _SH_DENYNO);
	if (!m_log_file) THROW_ERROR(ERR_USER, L"failed on opening log file %s", log_path.c_str());

	m_op_sn = 0;
	return 0;
}

int CExTester::PreTest(void)
{
	// 准备测试
	CFsState* init_state = m_states.get();
	IFsSimulator* fs = nullptr;
	m_fs_factory->Clone(fs);

	init_state->Initialize(L"\\", fs);
	m_open_list.push_front(init_state);
	return 0;
}

int CExTester::RunTest(void)
{
	while (!m_open_list.empty())
	{
		CFsState* cur_state = m_open_list.front();
		ERROR_CODE err = Verify(cur_state);
		if (err != ERR_OK) THROW_ERROR(ERR_APP, L"verify failed, code=%d", err);
		// (1) 将当前状态加入hash
		m_closed.Insert(cur_state);
		// 如果verify出现错误，cur_state会在FinishTest()中被回收
		m_open_list.pop_front();

		// check depth
		LONG depth = cur_state->m_depth;
		LONG max_depth = m_max_depth;
		if (depth > max_depth) {
			InterlockedExchange(&m_max_depth, depth);
		}
		// (2) 扩展当前状态，
		auto it = m_open_list.begin();
		EnumerateOp(cur_state, it);
		// (3) 移除当前状态      
		m_states.put(cur_state);
	}

	return 0;
}

int CExTester::StartTest(void)
{
	// 主线程用于监视，子线程用于测试
	m_ts_start = boost::posix_time::microsec_clock::local_time();
	// 启动测试
	m_test_thread = CreateThread(NULL, 0, _RunTest, (PVOID)this, 0, &m_test_thread_id);

	// 监视测试过程
	while (1)
	{
		DWORD ir = WaitForSingleObject(m_test_thread, m_update_ms);
		boost::posix_time::ptime ts_cur = boost::posix_time::microsec_clock::local_time();
		INT64 ts = (ts_cur - m_ts_start).total_seconds();
		PrintProgress(ts);

		if (ir == 0)
		{	// test completed
			wprintf_s(L"test completed\n");
			break;
		}
	}
	return 0;
}

DWORD WINAPI CExTester::_RunTest(PVOID p)
{
	CExTester* tester = (CExTester*)p;
	int ir = 0;
	try
	{
		tester->PreTest();
		ir = tester->RunTest();
		wprintf_s(L"test completed with code=%d\n", ir);
	}
	catch (jcvos::CJCException & err)
	{
		ir = ERR_GENERAL;
		if (tester->m_log_file) {
			fprintf_s(tester->m_log_file, "[err] test failed with exception (code=%d): %S", ir, err.WhatT());
		}
	}
	catch (const std::exception& err)
	{
		ir = ERR_GENERAL;
		wprintf_s(L"test failed with error code=%d\n", ir);
	}
	tester->FinishTest();
	return ir;
}

bool CExTester::PrintProgress(INT64 ts)
{
	bool health_valid = false;
	HANDLE handle = GetCurrentProcess();
	PROCESS_MEMORY_COUNTERS_EX pmc = { 0 };
	GetProcessMemoryInfo(handle, (PROCESS_MEMORY_COUNTERS*)&pmc, sizeof(pmc));
	float mem_cost = (float)(pmc.WorkingSetSize / (1024.0 * 1024.0));	//MB
	if (mem_cost > m_max_memory_cost) m_max_memory_cost = mem_cost;

	// 输出到concole
	wprintf_s(L"ts=%llds, op=%d, depth=%d, closed=%zd, open=%zd, mem=%.1fMB, max_mem=%.1fMB\n",
		ts, m_op_sn, m_max_depth, m_closed.size(), m_open_list.size(), mem_cost, m_max_memory_cost);

	return true;
}


void CExTester::FinishTest(void)
{
	auto it = m_open_list.begin();
	for (; it != m_open_list.end(); ++it)
	{
		CFsState* state = *it;
		m_states.put(state);
	}
	m_open_list.clear();
}

void CExTester::ShowTestFailure(FILE* log)
{
}

void CExTester::GetTestSummary(boost::property_tree::wptree& pt_sum)
{
	boost::property_tree::wptree sum;
	sum.add(L"op_num", m_op_sn);

	boost::posix_time::ptime ts_cur = boost::posix_time::microsec_clock::local_time();
	INT64 ts = (ts_cur - m_ts_start).total_seconds();
	sum.add(L"duration", ts);			//s
	sum.add(L"max_memory", m_max_memory_cost);		//MB
	size_t state = m_closed.size();
	sum.add(L"state_closed", state);
	sum.add(L"max_depth", m_max_depth);
	pt_sum.add_child(L"exhaustive_summary", sum);
}

bool CExTester::EnumerateOp(CFsState* cur_state, std::list<CFsState*>::iterator& insert)
{
	CReferenceFs& ref_fs = cur_state->m_ref_fs;
	auto endit = ref_fs.End();
	auto it = ref_fs.Begin();
	for (; it != endit; it++)
	{
		const CReferenceFs::CRefFile& file = ref_fs.GetFile(it);
		bool isdir = ref_fs.IsDir(file);

		std::wstring path;
		ref_fs.GetFilePath(file, path);

		DWORD checksum;
		FSIZE file_len;
		ref_fs.GetFileInfo(file, checksum, file_len);

		TRACE_ENTRY op;
		if (isdir)
		{	// 目录
			UINT child_num = file.child_num();
			if (child_num >= m_max_child_num) continue;
			if (ref_fs.GetFileNumber() >= MAX_FILE_NUM) continue;

			for (auto op_it = m_dir_op_set.begin(); op_it != m_dir_op_set.end(); ++op_it)
			{
				wchar_t fn[3];	//文件名，随机产生2字符
				GenerateFn(fn, 2);

				switch (*op_it)
				{
				case OP_CODE::OP_FILE_CREATE: {
					op.op_code = OP_FILE_CREATE;
					op.file_path = (path.size() > 1) ? (path + L"\\" + fn) : (path + fn);
					DoFsOperator(cur_state, op, insert);
					break; }

				case OP_CODE::OP_DIR_CREATE:
					// 深度限制
					if (file.depth() < m_max_dir_depth)
					{
						op.op_code = OP_DIR_CREATE;
						op.file_path = (path.size() > 1) ? (path + L"\\" + fn) : (path + fn);
						DoFsOperator(cur_state, op, insert);
					}
					break;


				case OP_CODE::OP_MOVE:
					break;
				}
				// 测试后处理
			}
		}
		else
		{	// 文件
			for (auto op_it = m_file_op_set.begin(); op_it != m_file_op_set.end(); ++op_it)
			{
				FSIZE len = rand() * m_max_file_size / RAND_MAX;
				FSIZE offset = rand() * file_len / RAND_MAX;
				switch (*op_it)
				{
				case OP_CODE::OP_FILE_WRITE:
					if (file.write_count() < m_max_file_op)
					{
						op.op_code = OP_FILE_WRITE;
						op.file_path = path;
						op.offset = offset;
						op.length = len;
						DoFsOperator(cur_state, op, insert);
					}
					break;

				case OP_CODE::OP_FILE_DELETE:
					op.op_code = OP_FILE_DELETE;
					op.file_path = path;
					DoFsOperator(cur_state, op, insert);
					break;

				case OP_CODE::OP_MOVE:
					break;
				}
			}
		}
	}
	return true;
}

ERROR_CODE CExTester::DoFsOperator(CFsState* cur_state, TRACE_ENTRY& op, std::list<CFsState*>::iterator& insert)
{
	JCASSERT(cur_state);
	// 标准测试流程：
	// (1) 复制测试状态，
	CFsState* new_state = m_states.duplicate(cur_state);
	new_state->m_op = op;
	
	// (2) 在新的测试状态上执行测试
	ERROR_CODE ir = ERR_OK;
	switch (op.op_code)
	{
	case OP_FILE_CREATE:
		ir = TestCreateFile(new_state, op.file_path);
		break;
	case OP_DIR_CREATE:
		ir = TestCreateDir(new_state, op.file_path);
		break;

	case OP_FILE_WRITE:
		ir = TestWriteFile(new_state, op.file_path, op.offset, op.length);
		break;

	case OP_FILE_DELETE:
		ir = TestDeleteFile(new_state, op.file_path);
		break;

//	case OP_FILE_OVERWRITE:
	case OP_DIR_DELETE:
	case OP_MOVE:
		break;
	}
	// (3) 检查测试结果
	if (ir != ERR_OK)
	{
		m_states.put(new_state);
		THROW_ERROR(ERR_APP, L"run test error, code=%d op=%d, file=%s, ", ir, op.op_code, op.file_path.c_str());
		return ir;
	}
//	new_state->OutputState(m_log_file);
	if (m_closed.Check(new_state))
	{
//		fprintf_s(m_log_file, "state existed => remove\n");
		m_states.put(new_state);
		return ERR_OK;
	}
	if (new_state)
	{
		m_open_list.insert(insert, new_state);
	}
	return ir;
}

ERROR_CODE CExTester::Verify(CFsState* cur_state)
{
	ERROR_CODE err = ERR_OK;

	LOG_STACK_TRACE();
	TEST_LOG("[BEGIN VERIFY]\n");

	CReferenceFs& ref = cur_state->m_ref_fs;
	IFsSimulator* fs = cur_state->m_real_fs;

	std::string str_encode;
	ref.GetEncodeString(str_encode);
//	TEST_LOG(L"\t encode=%S\n", str_encode.c_str());

	size_t checked_file = 0, checked_total = 0;
	auto endit = ref.End();
	for (auto it = ref.Begin(); it != endit; ++it)
	{
		const CReferenceFs::CRefFile& ref_file = ref.GetFile(it);
		std::wstring path;
		ref.GetFilePath(ref_file, path);
		if (path == L"\\") continue;	//不对根目录做比较

		checked_total++;
		bool dir = ref.IsDir(ref_file);
		ref_file.GetEncodeString(str_encode);
//		TEST_LOG(L"\t<check %s> %s\\, (%S)", dir ? L"dir" : L"file", path.c_str(), str_encode.c_str());
		DWORD access = 0;
		DWORD flag = 0;
		if (dir) { flag |= FILE_FLAG_BACKUP_SEMANTICS; }
		else { access |= GENERIC_READ; }

		FID fid = fs->FileOpen(path);

		if (fid == INVALID_BLK)
		{
			err = ERR_OPEN_FILE;
			THROW_WIN32_ERROR(L"failed on open file=%s", path.c_str());
		}

		if (!dir)
		{
			checked_file++;
			DWORD ref_checksum;
			FSIZE ref_len, file_secs = fs->GetFileSize(fid);
			ref.GetFileInfo(ref_file, ref_checksum, ref_len);

//			TEST_LOG(L" ref size=%d, file size=%d, checksum=0x%08X", ref_len, file_secs, ref_checksum);
			if (ref_len != file_secs)
			{
				err = ERR_WRONG_FILE_SIZE;
				TEST_ERROR("file length does not match ref=%d, file=%d", ref_len, file_secs);
				THROW_ERROR(ERR_APP, L"verify file failed, length does not match");
			}

			std::vector<CPageInfo*> blks;
			fs->FileRead(blks, fid, 0, file_secs);
			FSIZE ss = 0;
			for (auto it = blks.begin(); it != blks.end(); ++it, ss++)
			{
				CPageInfo* page = *it;
				if (page->inode != fid || page->offset != ss) {
					THROW_ERROR(ERR_APP, L"read file does not match, fid=%d, lblk=%d, page_fid=%d, page_offset=%d",
						fid, ss, page->inode, page->offset);
				}
			}

			//if (file_secs == 0)
			//{	// 有可能是over write的roll back造成的，忽略比较
			//	TEST_LOG(L" ignor comparing");
			//}
			//else
			//{
			//	TEST_LOG(L" compare");
			//	if (ref_len != file_secs)
			//	{
			//		err = ERR_WRONG_FILE_SIZE;
			//		TEST_ERROR(L"file length does not match ref=%zd, file=%zd", ref_len, file_secs);
			//		m_fs->FileClose(fid);
			//		THROW_ERROR(ERR_USER, L"file length does not match ref=%d, file=%d", ref_len, file_secs);
			//		break;
			//	}
			//	DWORD checksum = CalFileChecksum(fid);
			//	if (checksum != ref_checksum)
			//	{
			//		err = ERR_WRONG_FILE_DATA;
			//		TEST_ERROR(L"checksum does not match, ref=0x%08X, file=0x%08X", ref_checksum, checksum);
			//		m_fs->FileClose(fid);
			//		THROW_ERROR(ERR_USER, L"checksum does not match, ref=0x%08X, file=0x%08X", ref_checksum, checksum);
			//		break;
			//	}
			//}
		}
		fs->FileClose(fid);
//		TEST_LOG(L", closed");
//		TEST_CLOSE_LOG;
	}
	TEST_LOG("[END VERIFY], total checked=%zd, file checked= %zd\n", checked_total, checked_file);
	return err;
}

ERROR_CODE CExTester::TestCreateFile(CFsState* cur_state, const std::wstring& path)
{
	JCASSERT(cur_state);
	IFsSimulator* real_fs = cur_state->m_real_fs;
	JCASSERT(real_fs);
	CReferenceFs& ref = cur_state->m_ref_fs;

	ERROR_CODE err = ERR_OK;
	// 检查自己点数量是否超标
	// create full path name
	TEST_LOG("[OPERATE ](%d) CREATE FILE, path=%S,", m_op_sn++, path.c_str());

	std::wstring file_path = path;
	bool create_result = false;
	FID fid = real_fs->FileCreate(file_path);
	if (fid != INVALID_BLK)
	{
		create_result = true;
		real_fs->FileClose(fid);
		TEST_LOG(", closed");
	}

	// check if doubled name, update ref fs
	if (ref.IsExist(path))
	{	// 文件已经存在，要求返回false
		TEST_LOG(" existing file");
//		err = OK_ALREADY_EXIST;
		if (create_result)
		{
			err = ERR_CREATE_EXIST;
			TEST_ERROR("create a file which is existed path=%S.", path.c_str());
		}
	}
	else
	{	// create file in fs
		TEST_LOG(" new file");
		ref.AddPath(path, false);
		if (!create_result)
		{
			err = ERR_CREATE;
			TEST_ERROR("failed on creating file fn=%S", path.c_str());
		}
	}
	TEST_CLOSE_LOG;

	return err;
}

ERROR_CODE CExTester::TestDeleteFile(CFsState* cur_state, const std::wstring& path)
{
	JCASSERT(cur_state);
	IFsSimulator* real_fs = cur_state->m_real_fs;
	JCASSERT(real_fs);
	CReferenceFs& ref = cur_state->m_ref_fs;

	ERROR_CODE err = ERR_OK;
	TEST_LOG("[OPERATE ](%d) DELETE FILE, path=%S,", m_op_sn++, path.c_str());

	real_fs->FileDelete(path);
	ref.RemoveFile(path);

	TEST_CLOSE_LOG;
	return err;
}

ERROR_CODE CExTester::TestCreateDir(CFsState* cur_state, const std::wstring& path)
{
	JCASSERT(cur_state);
	IFsSimulator* real_fs = cur_state->m_real_fs;
	JCASSERT(real_fs);
	CReferenceFs& ref = cur_state->m_ref_fs;

	ERROR_CODE err = ERR_OK;
	// 检查自己点数量是否超标
	// create full path name
	TEST_LOG("[OPERATE ](%d) CREATE DIR_, path=%S,", m_op_sn++, path.c_str());

	std::wstring file_path = path;
	bool create_result = false;
	FID fid = real_fs->DirCreate(file_path);
	if (fid != INVALID_BLK)
	{
		create_result = true;
		real_fs->FileClose(fid);
	}

	// check if doubled name, update ref fs
	if (ref.IsExist(path))
	{	// 文件已经存在，要求返回false
		TEST_LOG(" existing dir");
		//err = OK_ALREADY_EXIST;
		if (create_result)
		{
			err = ERR_CREATE_EXIST;
			TEST_ERROR("create a file which is existed path=%S.", path.c_str());
		}
	}
	else
	{	// create file in fs
		TEST_LOG(" new dir");
		ref.AddPath(path, true);
		if (!create_result)
		{
			err = ERR_CREATE;
			TEST_ERROR("failed on creating file fn=%S", path.c_str());
		}
	}
	TEST_CLOSE_LOG;
	return err;
}

ERROR_CODE CExTester::TestWriteFile(CFsState* cur_state, const std::wstring& path, FSIZE offset, FSIZE len)
{
	JCASSERT(cur_state);
	IFsSimulator* real_fs = cur_state->m_real_fs;
	JCASSERT(real_fs);
	CReferenceFs& ref = cur_state->m_ref_fs;

	ERROR_CODE err = ERR_OK;
	//	len &= ~3;		// DWORD对齐
	TEST_LOG("[OPERATE ](%d) WriteFile, path=%S, offset=%d, size=%d", m_op_sn++, path.c_str(), offset, len);

	//	HANDLE file = CreateFile(path.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, 0, 0);
	FID fid = real_fs->FileOpen(path);
	if (fid == INVALID_BLK)
	{
		err = ERR_OPEN_FILE;
		THROW_WIN32_ERROR(L"failed on opening file, fn=%s", path.c_str());
	}

	//	size_t total, used, free, max_files, file_num;
	FsSpaceInfo space_info;
	real_fs->GetSpaceInfo(space_info);

	FSIZE cur_secs;
	DWORD cur_checksum;
	CReferenceFs::CRefFile* ref_file = ref.FindFile(path);
	if (!ref_file) THROW_ERROR(ERR_USER, L"cannof find ref file: %s", path.c_str());
	ref.GetFileInfo(*ref_file, cur_checksum, cur_secs);

	// get current file length
	FSIZE file_secs = real_fs->GetFileSize(fid);
	//	if (cur_len != info.nFileSizeLow && info.nFileSizeLow != 0)
	if (cur_secs != file_secs)	{
		THROW_ERROR(ERR_USER, L"file length does not match ref=%d, file=%d", cur_secs, file_secs);
	}

	// 
	FSIZE new_secs = offset + len;
	if (new_secs > file_secs)
	{	// 当要写入的文件大小超过空余容量时，缩小写入量。
		FSIZE increment = ROUND_UP_DIV(new_secs - file_secs, BLOCK_SIZE);
		if (increment > space_info.free_blks)
		{
			increment = space_info.free_blks;
			new_secs = file_secs + increment;
			len = new_secs - offset;
		}
	}

	real_fs->FileWrite(fid, offset, len);
	if (file_secs > new_secs) new_secs = file_secs;

	//DWORD checksum = CalFileChecksum(fid);
	DWORD checksum = fid;
//	ref_file->m_pre_size = (int)cur_secs;
	ref.UpdateFile(path, checksum, new_secs);
	TEST_LOG("\t current size=%d, new size=%d", file_secs, new_secs);
	//CloseHandle(file);
	real_fs->FileClose(fid);
	TEST_LOG(", closed");

	TEST_CLOSE_LOG;
	return err;
}
﻿///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
#include "pch.h"
#include "../include/full_tester.h"
#include <boost/cast.hpp>

#include "../../../filesystem/dokanfs-lib/dokanfs-lib.h"


LOCAL_LOGGER_ENABLE(L"fulltester", LOGGER_LEVEL_DEBUGINFO);



void FillData(char* buf, size_t size)
{
	for (size_t ii = 0; ii < size; ++ii)			buf[ii] = ii % 26 + 'A';
}

int CTesterBase::CompareData(const char* src, const char* tar, size_t size)
{
	for (size_t ii = 0; ii < size; ++ii)
	{
		if (src[ii] != tar[ii]) return boost::numeric_cast<int>(ii);
	}
	return -1;
}

void GenerateFn(wchar_t* fn, size_t len)
{
	size_t ii;
	for (ii = 0; ii < len; ++ii)
	{
		fn[ii] = rand() % 26 + 'A';
	}
	fn[ii] = 0;
}

DWORD AppendChecksum(DWORD cur_checksum, const BYTE* buf, size_t size)
{
	const DWORD* bb = reinterpret_cast<const DWORD*>(buf);
	size_t ss = size / 4;
	for (size_t ii = 0; ii < ss; ++ii) cur_checksum += bb[ii];
	return cur_checksum;
}

#define BUF_SIZE (64*1024)

DWORD CalFileChecksum(HANDLE file)
{
	DWORD checksum = 0;
//	BYTE* buf = new BYTE[BUF_SIZE];
	jcvos::auto_array<BYTE> _buf(BUF_SIZE);
	BYTE* buf = _buf;
	SetFilePointer(file, 0, 0, FILE_BEGIN);
	
	while (1)
	{
		DWORD read = 0;
		BOOL br = ReadFile(file, buf, BUF_SIZE, &read, nullptr);
		if (!br) THROW_WIN32_ERROR(L"failed on reading file");
		checksum = AppendChecksum(checksum, buf, read);
		if (read < BUF_SIZE) break;
	}
	return checksum;
}

OP_CODE StringToOpId(const std::wstring& str)
{
	OP_CODE id;
	if (false) {}
	else if (str == L"CreateFile")	id = OP_CODE::OP_FILE_CREATE;
	else if (str == L"CreateDir")	id = OP_CODE::OP_DIR_CREATE;
	//else if (str == L"Append")		id = OP_CODE::APPEND_FILE;
	else if (str == L"OverWrite")	id = OP_CODE::OP_FILE_WRITE;
	else if (str == L"DeleteFile")	id = OP_CODE::OP_FILE_DELETE;
	else if (str == L"DeleteDir")	id = OP_CODE::OP_DIR_DELETE;
	else if (str == L"Move")		id = OP_CODE::OP_MOVE;
	else if (str == L"Mount")		id = OP_CODE::OP_DEMOUNT_MOUNT;
	else if (str == L"PowerCycle")	id = OP_CODE::OP_POWER_OFF_RECOVER;
	else							id = OP_CODE::OP_NOP;
	return id;

}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// ==== CFullTesterDokan ====






int CFullTester::FsOperate(CReferenceFs& ref, TRACE_ENTRY* op)
{
	int err = 0;
	bool isdir = true;
//	size_t len = 0;
	JCASSERT(op);
	switch (op->op_code)
	{
	case OP_CODE::OP_FILE_CREATE: isdir = false;
	case OP_CODE::OP_DIR_CREATE: 	// 共用代码
		// 检查自己点数量是否超标
		err = TestCreate(ref, op->file_path, isdir);
		if (err == OK_ALREADY_EXIST)
		{
			op->op_code = OP_CODE::OP_NO_EFFECT;
			err = 0;
		}
		break;

	case OP_CODE::OP_FILE_DELETE:	break;
	case OP_CODE::OP_DIR_DELETE:	break;
	case OP_CODE::OP_MOVE:			break;
	case OP_CODE::OP_FILE_WRITE:
//		swscanf_s(op->param1.c_str(), L"%zd", &len);
		err = TestWrite(ref, op->file_path, op->offset, op->length);
		break;
//	case OP_CODE::OP_APPEND_FILE:
//		swscanf_s(op->param1.c_str(), L"%zd", &len);
		//err = TestWrite(ref, false, op->file_path, op->param3_val);
//		break;
	case OP_CODE::OP_DEMOUNT_MOUNT:
		//TestMount(fs, ref);
		break;
	case OP_CODE::OP_POWER_OFF_RECOVER:
		//TestPower(fs, ref);
		break;
	}
	return err;
}

int CFullTester::TestCreate(CReferenceFs& ref, /*const std::wstring& src_path, const std::wstring& fn,*/const std::wstring & path, bool isdir)
{
	int err = 0;
	// 检查自己点数量是否超标
	// create full path name
	TEST_LOG(L"[OPERATE ](%d) CREATE %s, path=%s,", m_op_sn++, isdir ? L"DIR" : L"FILE", path.c_str()/*, fn.c_str()*/);

	//std::wstring path;
	//if (src_path.size() > 1)	path = src_path + L"\\" + fn;	//non-root
	//else path = src_path + fn;		// root

	std::wstring file_path = path;
	bool create_result = false;
	if (isdir)
	{
		BOOL br = CreateDirectory(file_path.c_str(), nullptr);
		if (br) create_result = true;
	}
	else
	{
		HANDLE file = CreateFile(file_path.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, CREATE_NEW, 0, 0);
		if (file && (file != INVALID_HANDLE_VALUE) )
		{
			create_result = true;
			CloseHandle(file);
			TEST_LOG(L", closed");
		}
	}
	// check if doubled name, update ref fs
	if (ref.IsExist(path))
	{	// 文件已经存在，要求返回false
		TEST_LOG(L" existing file/dir");
		err = OK_ALREADY_EXIST;
		if (create_result)
		{
			err = ERR_CREATE_EXIST;
			TEST_ERROR(L"create a file which is existed path=%s.", path.c_str());
//			THROW_ERROR(ERR_USER, L"create a file which is existed path=%s.", path.c_str());
		}
	}
	else
	{	// create file in fs
		TEST_LOG(L" new file/dir");
		ref.AddPath(path, isdir);
		if (!create_result)
		{
			err = ERR_CREATE;
			TEST_ERROR(L"failed on creating file fn=%s", path.c_str());
//			THROW_ERROR(ERR_USER, L"failed on creating file fn=%s", path.c_str());
		}
	}
	TEST_CLOSE_LOG;

	return err;
}

int CFullTester::TestWrite(CReferenceFs& ref, const std::wstring& path, size_t offset, size_t len)
{
	int err = 0;
	len &= ~3;		// DWORD对齐
	TEST_LOG(L"[OPERATE ](%d) WriteFile, path=%s, offset=%zd, size=%zd", m_op_sn++, path.c_str(), offset, len);

	HANDLE file = CreateFile(path.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, 0, 0);
	if (!file || file == INVALID_HANDLE_VALUE)
	{
		err = ERR_OPEN_FILE;
		THROW_WIN32_ERROR(L"failed on opening file, fn=%s", path.c_str());
	}

	size_t cur_len;
	DWORD cur_checksum;
	CReferenceFs::CRefFile* ref_file = ref.FindFile(path);
	if (!ref_file) THROW_ERROR(ERR_USER, L"cannof find ref file: %s", path.c_str());
	ref.GetFileInfo(*ref_file, cur_checksum, cur_len);

	BY_HANDLE_FILE_INFORMATION info;
	BOOL br = GetFileInformationByHandle(file, &info);
	if (!br)
	{
		err = ERR_GET_INFOMATION;
		THROW_WIN32_ERROR(L"failed on get file information, file=%s", path.c_str());
	}
	// get current file length
	if (cur_len != info.nFileSizeLow && info.nFileSizeLow != 0)
	{
		JCASSERT(0);
		THROW_ERROR(ERR_USER, L"file length does not match ref=%d, file=%d", cur_len, info.nFileSizeLow);
	}
	jcvos::auto_array<char> buf(len);
	FillData(buf, len);
	DWORD written = 0;
	size_t new_size = offset + len;

	size_t file_old_len = info.nFileSizeLow;
	if (offset < file_old_len)
	{
		LONG offset_lo = LODWORD(offset);
		LONG offset_hi = HIDWORD(offset);
		SetFilePointer(file, offset_lo, &offset_hi, FILE_BEGIN);
		size_t ss = offset + len;
		new_size = max(file_old_len, ss);
	}
	else
	{
		SetFilePointer(file, 0, 0, FILE_END);
		new_size = file_old_len + len;
	}

//	SetEndOfFile(file);
	WriteFile(file, buf, boost::numeric_cast<DWORD>(len), & written, nullptr);
//	DWORD checksum = AppendChecksum(cur_checksum, buf, len);
	DWORD checksum = CalFileChecksum(file);
	ref.UpdateFile(path, checksum, new_size);
	TEST_LOG(L"\t current size=%d, new size=%zd", info.nFileSizeLow, new_size);
	CloseHandle(file);
	TEST_LOG(L", closed");

	TEST_CLOSE_LOG;
	return err;
}

int CFullTester::Rollback(CReferenceFs& ref, const TRACE_ENTRY* op)
{
	int err = 0;
	// 对op进行你操作
	std::wstring path;
	switch (op->op_code)
	{
	case OP_CODE::OP_NO_EFFECT:
		TEST_LOG(L"[ROLLBACK](%d) (op=%d) NO OPERATION", m_op_sn++, op->op_sn);
		break;
	case OP_CODE::OP_FILE_CREATE: {
		//if (op->file_path == L"\\") path = op->file_path + op->target_path;
		//else path = op->file_path + L"\\" + op->target_path;
		path = op->file_path;
		TEST_LOG(L"[ROLLBACK](%d) (op=%d) DELETE FILE, path=%s", m_op_sn++, op->op_sn, path.c_str());
		BOOL br = DeleteFile(path.c_str());

		if (!br)
		{
			err = ERR_DELETE_FILE;
			TEST_ERROR(L"failed on deleting file %s", path.c_str());
			THROW_WIN32_ERROR(L"failed on deleting file=%s", path.c_str());
		}
		//		ref.RemoveFile(path);	// ref有backup，退回backup状态，不需要rollback
		TEST_CLOSE_LOG;
		break; }

	case OP_CODE::OP_DIR_CREATE: {	// rollback for: 在 “op->path”创建dir“op->param" => 删除dir:"op->path\\op->param"
		//if (op->file_path == L"\\") path = op->file_path + op->target_path;
		//else path = op->file_path + L"\\" + op->target_path;
		path = op->file_path;
		TEST_LOG(L"[ROLLBACK](%d) (op=%d) DELETE DIR, path=%s", m_op_sn++, op->op_sn, path.c_str());
		BOOL br = RemoveDirectory(path.c_str());
		if (!br)
		{
			err = ERR_DELETE_DIR;
			TEST_ERROR(L"failed on deleting dir %s", path.c_str());
			THROW_WIN32_ERROR(L"failed on deleting dir=%s", path.c_str());
		}
		//		ref.RemoveFile(path);	// ref有backup，退回backup状态，不需要rollback
		TEST_CLOSE_LOG;
		break; }

	case OP_CODE::OP_FILE_DELETE:	break;
	case OP_CODE::OP_DIR_DELETE:	break;
	case OP_CODE::OP_MOVE:			break;
	case OP_CODE::OP_FILE_WRITE: {
		if (!m_support_trunk) break;
		// overwrite没有逆操作，只能删除文件，同时删除ref中的文件 X
		TEST_LOG(L"[ROLLBACK](%d) (op=%d) TRUNK FILE, path=%s", m_op_sn++, op->op_sn, op->file_path.c_str());
		size_t cur_len;
		DWORD cur_checksum;
		CReferenceFs::CRefFile* ref_file = ref.FindFile(op->file_path);
		if (!ref_file) THROW_ERROR(ERR_USER, L"cannof find ref file: %s", op->file_path.c_str());
		ref.GetFileInfo(*ref_file, cur_checksum, cur_len);

		HANDLE file = CreateFile(op->file_path.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, TRUNCATE_EXISTING, 0, 0);
		if (!file || file == INVALID_HANDLE_VALUE)
		{
			err = ERR_OPEN_FILE;
			THROW_WIN32_ERROR(L"failed on open file=%s", op->file_path.c_str());
		}
		CloseHandle(file);
		TEST_LOG(L", closed");
		ref.UpdateFile(*ref_file, 0, 0);
		TEST_CLOSE_LOG;
		break; }
/*
	case OP_CODE::OP_APPEND_FILE: {
		if (!m_support_trunk) break;
		size_t cur_len;
		DWORD cur_checksum;
		CReferenceFs::CRefFile* ref_file = ref.FindFile(op->file_path);
		TEST_LOG(L"[ROLLBACK](%d) TRUNK, path=%s", m_op_sn++, op->file_path.c_str());
		if (!ref_file) THROW_ERROR(ERR_USER, L"cannof find ref file: %s", op->file_path.c_str());
		ref.GetFileInfo(*ref_file, cur_checksum, cur_len);

		HANDLE file = CreateFile(op->file_path.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, TRUNCATE_EXISTING, 0, 0);
		if (!file || file == INVALID_HANDLE_VALUE)
		{
			err = ERR_OPEN_FILE;
			THROW_WIN32_ERROR(L"failed on open file=%s", op->file_path.c_str());
		}
		BY_HANDLE_FILE_INFORMATION info;
		BOOL br = GetFileInformationByHandle(file, &info);
		if (!br)
		{
			err = ERR_GET_INFOMATION;
			THROW_WIN32_ERROR(L"failed on getting file info, file=%s", op->file_path.c_str());
		}
		TEST_LOG(L"\t cur_size=%d, org_size=%zd", info.nFileSizeLow, cur_len);

		SetFilePointer(file, boost::numeric_cast<LONG>(cur_len), nullptr, FILE_BEGIN);
		br = SetEndOfFile(file);
		TEST_LOG(L"\t cur_size=%d, org_size=%zd", info.nFileSizeLow, cur_len);

		CloseHandle(file);
		TEST_LOG(L", closed");

		TEST_CLOSE_LOG;
		break; }
*/
	}
	return err;
}

int CFullTester::Verify(const CReferenceFs& ref)
{
	int err = 0;
	LOG_STACK_TRACE();
	TEST_LOG(L"[BEGIN VERIFY]\n");
	auto endit = ref.End();
	for (auto it = ref.Begin(); it != endit; ++it)
	{
		const CReferenceFs::CRefFile& ref_file = ref.GetFile(it);
		std::wstring path;
		ref.GetFilePath(ref_file, path);
		if (path == m_root) continue;	//不对根目录做比较
		bool dir = ref.IsDir(ref_file);
		TEST_LOG(L"\t<check %s> %s\\", dir ? L"dir" : L"file", path.c_str());
//		std::wstring ff = path;
		DWORD access = 0;
		DWORD flag = 0;
		if (dir)	{	flag |= FILE_FLAG_BACKUP_SEMANTICS;	}
		else	{	access |= GENERIC_READ;		}

		HANDLE file = CreateFile(path.c_str(), access, FILE_SHARE_READ , nullptr, OPEN_EXISTING, flag, 0);
		if (!file || file == INVALID_HANDLE_VALUE)
		{
			err = ERR_OPEN_FILE;
			THROW_WIN32_ERROR(L"failed on open file=%s", path.c_str());
		}

		if (!dir)
		{
			DWORD ref_checksum;
			size_t ref_len;
			ref.GetFileInfo(ref_file, ref_checksum, ref_len);
			BY_HANDLE_FILE_INFORMATION info;
			BOOL br = GetFileInformationByHandle(file, &info);
			if (!br)
			{
				err = ERR_GET_INFOMATION;
				CloseHandle(file);
//				TEST_ERROR(L"failed on getting file info, file=%s", path.c_str());
				THROW_WIN32_ERROR(L"failed on getting file info, file=%s", path.c_str());
				break;
			}

			TEST_LOG(L" ref size=%zd, file size=%d, checksum=0x%08X", ref_len, info.nFileSizeLow, ref_checksum);
			if (info.nFileSizeLow == 0)
			{	// 有可能是over write的roll back造成的，忽略比较
				TEST_LOG(L" ignor comparing");
			}
			else
			{
				TEST_LOG(L" compare");
				if (ref_len != info.nFileSizeLow)
				{
					err = ERR_WRONG_FILE_SIZE;
					TEST_ERROR(L"file length does not match ref=%zd, file=%d", ref_len, info.nFileSizeLow);
					CloseHandle(file);
					THROW_ERROR(ERR_USER, L"file length does not match ref=%d, file=%d", ref_len, info.nFileSizeLow);
					break;
				}
				DWORD checksum = CalFileChecksum(file);
				if (checksum != ref_checksum)
				{
					err = ERR_WRONG_FILE_DATA;
					TEST_ERROR(L"checksum does not match, ref=0x%08X, file=0x%08X", ref_checksum, checksum);
					CloseHandle(file);
					THROW_ERROR(ERR_USER, L"checksum does not match, ref=0x%08X, file=0x%08X", ref_checksum, checksum);
					break;
				}
			}
		}
		CloseHandle(file);
		TEST_LOG(L", closed");

		TEST_CLOSE_LOG;
	}
	TEST_LOG(L"[END VERIFY]\n")
	return err;
}


void CFullTester::Config(const boost::property_tree::wptree& pt, const std::wstring& root)
{
	CTesterBase::Config(pt, root);
	//m_root = pt.get<std::wstring>(L"root_dir");
	m_test_depth = pt.get<int>(L"depth", 5);
	m_max_child_num = pt.get<size_t>(L"max_child", 5);
	m_max_file_size = pt.get<size_t>(L"max_file_size", 1048576);
	//const std::wstring & log_fn  = pt.get<std::wstring>(L"log_file", L"");
	//if (!log_fn.empty()) SetLogFile(log_fn);
	m_clear_temp = pt.get<int>(L"clear_temp", 1);
	// 读取测试项目
	const boost::property_tree::wptree& pt_ops = pt.get_child(L"operaters");
	for (auto it = pt_ops.begin(); it != pt_ops.end(); it++)
	{
		const boost::property_tree::wptree& pt_op = it->second;
		const std::wstring& str_op = pt_op.get<std::wstring>(L"name");
		OP_CODE op_id = StringToOpId(str_op);
		const std::wstring& str_cond = pt_op.get<std::wstring>(L"condition");
		if (str_cond == L"File") m_file_op_set.push_back(op_id);
		else if (str_cond == L"Dir") m_dir_op_set.push_back(op_id);
		else if (str_cond == L"FS") m_fs_op_set.push_back(op_id);
	}
}

int CFullTester::PrepareTest(void)
{
	int err = 0;
	err = MakeFs();
	if (err) THROW_ERROR(ERR_APP, L"failed on make file system");

	if (m_need_mount)
	{
		err = MountFs();
		if (err) THROW_ERROR(ERR_APP, L"failed on mount fs during preparing");
	}
	m_test_state[0].Initialize(m_root);
	EnumerateOp(m_test_state[0]);
	m_op_sn = 0;
	return err;
}

int CFullTester::FinishTest(void)
{
	int err = 0;
	// 清除所有以创建文件和目录
	if (m_clear_temp)
	{
		for (int depth = m_cur_depth; depth >= 0; depth--)
		{
			CTestState& state = m_test_state[depth];
			if (state.m_cur_op == 0) continue;
			TRACE_ENTRY& op = state.m_ops[state.m_cur_op - 1];
			const std::wstring& path = op.file_path;

			//if (op.file_path == L"\\") path = op.file_path + op.target_path;
			//else path = op.file_path + L"\\" + op.target_path;
			BOOL br;
			if (op.op_code == OP_CODE::OP_FILE_CREATE)
			{
				br = DeleteFile(path.c_str());
				LOG_DEBUG(L"delete file %s, res=%d", path.c_str(), br);
			}
			else if (op.op_code == OP_CODE::OP_DIR_CREATE)
			{	// rollback for: 在 “op->path”创建dir“op->param" => 删除dir:"op->path\\op->param"
				br = RemoveDirectory(path.c_str());
				LOG_DEBUG(L"delete dir %s, res=%d", path.c_str(), br);
			}
		}
	}

	if (m_need_mount)
	{
		err = UnmountFs();
		if (err) THROW_ERROR(ERR_APP, L"failed on unmount fs after test");
	}

	TEST_LOG(L"\n\n=== result ===\n");
	TEST_LOG(L"  Test successed!\n");
	return err;
}

void CFullTester::ShowTestFailure(FILE * log)
{
	if (log == nullptr) return;
	TEST_LOG(L"\n=== statck ===\n");
	for (int dd = 0; dd <= m_test_depth; ++dd)
	{
		fwprintf_s(log, L"stacks[%d]:\n", dd);
		m_test_state[dd].OutputState(log);
	}
}


int CFullTester::RunTest(void)
{
	UINT32 show_msg = 0;
	boost::posix_time::ptime ts_update = boost::posix_time::microsec_clock::local_time();;
	int err=0;

	// initlaize state
	CTestState* cur_state = m_test_state;
	m_cur_depth = 0;
	while (1)
	{
		// pickup the first op in the 
		if (cur_state->m_cur_op >= cur_state->m_ops.size())
		{	// rollback, 需要逆向操作一下
			cur_state->m_cur_op = 0;	// 使当前状态无效

			if (m_cur_depth <= 0) break;
			m_cur_depth--;
			cur_state = m_test_state + m_cur_depth;
			JCASSERT(cur_state->m_cur_op > 0);
			TRACE_ENTRY& op = cur_state->m_ops[cur_state->m_cur_op - 1];
			Rollback(cur_state->m_ref_fs, &op);
			continue;
		}
		TRACE_ENTRY& op = cur_state->m_ops[cur_state->m_cur_op];
		cur_state->m_cur_op++;

		// test
		CTestState* next_state = cur_state + 1;
		next_state->Initialize(&cur_state->m_ref_fs);
		// generate new state
		op.op_sn = m_op_sn;
		err = FsOperate(next_state->m_ref_fs, &op);
		if (err)
		{
			THROW_ERROR(ERR_USER, L"failed on call operations, err=%d", err);
		}
		// 操作成功，保存操作，搜索下一步。
		// check max m_cur_depth
		if (m_cur_depth >= m_test_depth)
		{	// 达到最大深度，检查结果
			err = Verify(next_state->m_ref_fs);
			if (err)
			{
				TEST_ERROR(L"failed on verify fs, err=%d", err);
				LOG_ERROR(L"[err] failed on verify fs, err=%d", err);
				break;
			}
			Rollback(cur_state->m_ref_fs, &op);
			continue;
		}
		else
		{	// enumlate ops for new state
			m_cur_depth++;
			EnumerateOp(*next_state);
			cur_state = next_state;
		}
		SetEvent(m_monitor_event);
	}

	TEST_LOG(L"\n\n=== result ===\n");
	TEST_LOG(L"  Test successed!\n");
	boost::posix_time::ptime ts_cur = boost::posix_time::microsec_clock::local_time();
	INT64 ts = (ts_cur - m_ts_start).total_seconds();
	PrintProgress(ts);
	wprintf_s(L"Test completed\n");
	return true;
}



bool CFullTester::EnumerateOp(CTestState& state)
{
	CReferenceFs& ref_fs = state.m_ref_fs;
	auto endit = ref_fs.End();
	auto it = ref_fs.Begin();
	for (; it != endit; it++)
	{
		const CReferenceFs::CRefFile& file = ref_fs.GetFile(it);
		bool isdir = ref_fs.IsDir(file);

		std::wstring path;
		ref_fs.GetFilePath(file, path);

		DWORD checksum;
		size_t file_len;
		ref_fs.GetFileInfo(file, checksum, file_len);

		if (isdir)
		{	// 目录
			//size_t child_num = ~file_len;
			UINT child_num = checksum;
			if (child_num >= m_max_child_num) continue;

			for (auto op_it = m_dir_op_set.begin(); op_it != m_dir_op_set.end(); ++op_it)
			{
				wchar_t fn[3];	//文件名，随机产生2字符
				GenerateFn(fn, 2);

				switch (*op_it)
				{
				case OP_CODE::OP_FILE_CREATE:
//					state.AddOperation(OP_CODE::OP_FILE_CREATE, path, fn, 0);
					state.AddCreateOperation(path, fn, false);
					break;

				case OP_CODE::OP_DIR_CREATE:
//					state.AddOperation(OP_CODE::OP_DIR_CREATE, path, fn, 0);
					state.AddCreateOperation(path, fn, true);
					break;

				case OP_CODE::OP_MOVE:
					break;
				}
			}
		}
		else
		{	// 文件
			for (auto op_it = m_file_op_set.begin(); op_it != m_file_op_set.end(); ++op_it)
			{
				size_t len = rand() * m_max_file_size / RAND_MAX;
				size_t offset = rand() * file_len / RAND_MAX;
				switch (*op_it)
				{
				//case OP_CODE::APPEND_FILE:
				//	state.AddOperation(OP_CODE::APPEND_FILE, path, L"", len);
				//	break;
				case OP_CODE::OP_FILE_WRITE:
//					state.AddOperation(OP_CODE::OP_FILE_WRITE, path, L"", offset, len);
					state.AddWriteOperation(path, offset, len);
					break;
				case OP_CODE::OP_MOVE:
					break;
				}
			}
		}
	}
	//for (auto op_it = m_fs_op_set.begin(); op_it != m_fs_op_set.end(); ++op_it)
	//{
	//	state.AddOperation(*op_it, L"", L"", 0);
	//}
	return true;
}


///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// ==== Test State ====
//void CTestState::AddOperation(OP_CODE op_id, const std::wstring& src_path, const std::wstring& param1, UINT64 param3, UINT64 param4)
//{
//	m_ops.emplace_back();
//	TRACE_ENTRY& op = m_ops.back();
//	op.op_code = op_id;
//	op.file_path = src_path;
//	op.param1_str = param1;
//	op.param3_val = param3;
//	op.param4_val = param4;
//}

void CTestState::AddCreateOperation(const std::wstring& src_path, const std::wstring tar_path, bool is_dir)
{
	m_ops.emplace_back();
	TRACE_ENTRY& op = m_ops.back();
	if (is_dir) op.op_code = OP_CODE::OP_DIR_CREATE;
	else op.op_code = OP_CODE::OP_FILE_CREATE;

	if (src_path.size() > 1) op.file_path = src_path + L"\\" + tar_path;
	else op.file_path = src_path + tar_path;
	op.is_dir = is_dir;
	op.is_async = false;
}

void CTestState::AddWriteOperation(const std::wstring& src_path, UINT64 offset, UINT64 length)
{
	m_ops.emplace_back();
	TRACE_ENTRY& op = m_ops.back();
	op.op_code = OP_CODE::OP_FILE_WRITE;
	op.file_path = src_path;
	op.offset = offset;
	op.length = length;
}

void CTestState::OutputState(FILE* log_file)
{
	// output ref fs
	JCASSERT(log_file);
	if (m_cur_op == 0) return;	// 当前状态无效

	fwprintf_s(log_file, L"ref fs=\n");
	auto endit = m_ref_fs.End();
	auto it = m_ref_fs.Begin();
	for (; it != endit; ++it)
	{
		const CReferenceFs::CRefFile& ref_file = m_ref_fs.GetFile(it);
		std::wstring path;
		m_ref_fs.GetFilePath(ref_file, path);
		bool dir = m_ref_fs.IsDir(ref_file);
		DWORD ref_checksum;
		size_t ref_len;
		m_ref_fs.GetFileInfo(ref_file, ref_checksum, ref_len);
		fwprintf_s(log_file, L"<%s> %s : ", dir ? L"dir " : L"file", path.c_str());
		if (dir)	fwprintf_s(log_file, L"children=%d\n", ref_checksum);
		else		fwprintf_s(log_file, L"size=%zd, checksum=0x%08X\n", ref_len, ref_checksum);

	}
	// output op
//	JCASSERT(m_cur_op > 0);
	TRACE_ENTRY& op = m_ops[m_cur_op - 1];
	const wchar_t* op_name = NULL;
	switch (op.op_code)
	{
	case OP_CODE::OP_NOP:			op_name = L"none       ";	break;
	case OP_CODE::OP_FILE_CREATE:	op_name = L"create-file";	break;
	case OP_CODE::OP_DIR_CREATE:	op_name = L"create-dir ";	break;
	case OP_CODE::OP_FILE_DELETE:	op_name = L"delete-file";	break;
	case OP_CODE::OP_DIR_DELETE:	op_name = L"delete-dir ";	break;
	case OP_CODE::OP_MOVE:			op_name = L"move       ";	break;
	//case OP_CODE::APPEND_FILE:	op_name = L"append     ";	break;
	case OP_CODE::OP_FILE_WRITE:	op_name = L"overwrite  ";	break;
	case OP_CODE::OP_DEMOUNT_MOUNT:	op_name = L"demnt-mount";	break;
	default:						op_name = L"unknown    ";	break;
	}
	fwprintf_s(log_file, L"[%s] path=%s\n", op_name, op.file_path.c_str() /*, op.param1_str.c_str(), op.param3_val, op.param4_val*/);
}

void CTestState::Initialize(const CReferenceFs* src)
{
	JCASSERT(src);
	m_ref_fs.CopyFrom(*src);
	m_ops.clear();
	m_cur_op = 0;
}



void CTestState::Initialize(const std::wstring & root_path)
{
	m_ref_fs.Initialize(root_path);
	m_ops.clear();
	m_cur_op = 0;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// // ==== CTesterBase ==== // //

CTesterBase::CTesterBase(IFileSystem* fs, IVirtualDisk* disk)
{
	m_fs = fs;
	if (m_fs) m_fs->AddRef();
	m_disk = disk;
	if (m_disk) m_disk->AddRef();
}

CTesterBase::~CTesterBase(void)
{
	RELEASE(m_disk);
	RELEASE(m_fs);
}

void CTesterBase::Config(const boost::property_tree::wptree& pt, const std::wstring& root)
{
	m_root = root;
	const std::wstring & log_fn  = pt.get<std::wstring>(L"log_file", L"");
	if (!log_fn.empty()) SetLogFile(log_fn);
	m_timeout = pt.get<DWORD>(L"timeout", INFINITE);
	m_message_interval = pt.get<DWORD>(L"message_interval", 30);		// 以秒为单位的更新时间
	m_format = pt.get<bool>(L"format_before_test", false);
	m_mount = pt.get<bool>(L"mount_before_test", false);
	m_volume_size = pt.get<size_t>(L"volume_size", 0);

}

int CTesterBase::StartTest(void)
{
	m_ts_start = boost::posix_time::microsec_clock::local_time();

	int err = 0;
	try
	{
		if (m_format)
		{
			bool br = m_fs->MakeFileSystem(m_disk, m_volume_size, L"FSTESTER");
			if (!br) THROW_ERROR(ERR_APP, L"failed on format fs, size=%lld", m_volume_size);
		}
		if (m_mount)
		{
			bool br = m_fs->Mount(m_disk);
			if (!br) THROW_ERROR(ERR_APP, L"failed on mount fs");
		}
		err = PrepareTest();
		if (err) { LOG_ERROR(L"[err] failed on preparing test, err=%d", err); }
		// 打开监控文件
		std::wstring health_fn = m_root + L"\\$HEALTH";
		m_fsinfo_file = CreateFile(health_fn.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr);
		//m_fs->DokanCreateFile(m_health_file, L"\\$HEALTH", GENERIC_READ, 0, IFileSystem::FS_OPEN_EXISTING, FILE_SHARE_READ, 0, false);
		if (m_fsinfo_file == INVALID_HANDLE_VALUE || m_fsinfo_file == nullptr)
		{
			LOG_WARNING(L"file system does not support health info");
			m_fsinfo_file = nullptr;
		}

		// 启动监控线程
		m_running = 1;
		m_monitor_event = CreateEvent(NULL, FALSE, FALSE, NULL);
		DWORD thread_id = 0;
		m_monitor_thread = CreateThread(NULL, 0, _Monitor, (PVOID)this, 0, &thread_id);

		err = RunTest();
		if (err) { LOG_ERROR(L"[err] failed on testing, err=%d", err); }

	}
	catch (jcvos::CJCException& err)
	{
		// show stack
		TEST_LOG(L"\n\n=== result ===\n");
		TEST_LOG(L"  Test failed\n [err] test failed with error: %s\n", err.WhatT());
		ShowTestFailure(m_log_file);
		wprintf_s(L" Test failed! \n");
	}

	InterlockedExchange(&m_running, 0);
	SetEvent(m_monitor_event);
	WaitForSingleObject(m_monitor_thread, INFINITE);
	CloseHandle(m_monitor_thread);
	m_monitor_thread = NULL;
	CloseHandle(m_monitor_event);
	m_monitor_event = NULL;

	err = FinishTest();
	if (m_log_file) { fclose(m_log_file); }

	boost::posix_time::ptime ts_cur = boost::posix_time::microsec_clock::local_time();
	INT64 ts = (ts_cur - m_ts_start).total_seconds();
	PrintProgress(ts);

	if (m_mount)
	{
		m_fs->Unmount();
	}

	wprintf_s(L"Test completed\n");


	return err;
}







//
//TRACE_ENTRY::TRACE_ENTRY(const TRACE_ENTRY& entry)
//{
//	ts = entry.ts;
//	op_code = entry.op_code;
//	thread_id = entry.thread_id
//}
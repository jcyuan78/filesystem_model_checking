// dokanfs-tester.cpp : 此文件包含 "main" 函数。程序执行将在此处开始并结束。
//
#include "pch.h"
#include "dokanfs-tester.h"
#include <shlwapi.h>

#pragma comment (lib, "shlwapi.lib")
#include <Shlobj.h>

#include <vld.h>

#include <iostream>
#include <Psapi.h>
#include <boost/property_tree/json_parser.hpp>
namespace prop_tree = boost::property_tree;
#include <boost/date_time/posix_time/posix_time.hpp>


LOCAL_LOGGER_ENABLE(L"fstester.app", LOGGER_LEVEL_DEBUGINFO);

const TCHAR CFsTesterApp::LOG_CONFIG_FN[] = L"fstester.cfg";
typedef jcvos::CJCApp<CFsTesterApp>	CApplication;
CApplication _app;

#define _class_name_	CApplication
BEGIN_ARGU_DEF_TABLE()

ARGU_DEF(L"format", 'f', m_volume_name, L"format device using volume name.")
ARGU_DEF(L"mount", 'm', m_mount, L"mount point.")
ARGU_DEF(L"unmount", 'u', m_unmount, L"unmount device.", false)
ARGU_DEF(L"config", 'c', m_config_file, L"configuration file name")
ARGU_DEF(L"depth", 'd', m_test_depth, L"depth for test", (size_t)(MAX_DEPTH))
ARGU_DEF(L"log_file", 'l', m_log_fn, L"specify test log file name")
END_ARGU_DEF_TABLE()

int _tmain(int argc, _TCHAR* argv[])
{
	return jcvos::local_main(argc, argv);
}

BOOL WINAPI HandlerRoutine(DWORD dwCtrlType);

CFsTesterApp::CFsTesterApp(void)
	: m_dev(NULL), m_fs(NULL), m_capacity(0)
{
}

CFsTesterApp::~CFsTesterApp(void)
{
}

int CFsTesterApp::Initialize(void)
{
	//	EnableSrcFileParam('i');
	EnableDstFileParam('o');
	return 0;
}

void CFsTesterApp::CleanUp(void)
{
	RELEASE(m_fs);
	RELEASE(m_dev);
}

typedef bool(*PLUGIN_GET_FACTORY)(IFsFactory * &);

int CFsTesterApp::Run(void)
{
	LOG_STACK_TRACE();
	m_op_id = 0;
	// 解析config file的路径，将其设置为缺省路径
	wchar_t path[MAX_PATH];
	wchar_t filename[MAX_PATH];
	wchar_t cur_dir[MAX_PATH];
	GetCurrentDirectory(MAX_PATH - 1, cur_dir);
	GetFullPathName(m_config_file.c_str(), MAX_PATH, path, NULL);

	wcscpy_s(filename, path);
	PathRemoveFileSpec(path);
	SetCurrentDirectory(path);
	PathStripPath(filename);

	// load configuration
	std::string str_fn;
	//jcvos::UnicodeToUtf8(str_fn, m_config_file);
	jcvos::UnicodeToUtf8(str_fn, filename);
	prop_tree::wptree pt;
	prop_tree::json_parser::read_json(str_fn, pt);

	m_str_lib = pt.get<std::wstring>(L"config.library");
	m_str_fs = pt.get<std::wstring>(L"config.file_system", L"");
	auto & device_pt = pt.get_child_optional(L"config.device");

	if (m_str_lib.empty())	THROW_ERROR(ERR_PARAMETER, L"missing DLL.");
	LOG_DEBUG(L"loading dll: %s...", m_str_lib.c_str());
	HMODULE plugin = LoadLibrary(m_str_lib.c_str());
	if (plugin == NULL) THROW_WIN32_ERROR(L" failure on loading driver %s ", m_str_lib.c_str());

	LOG_DEBUG(L"getting entry...");
	PLUGIN_GET_FACTORY get_factory = (PLUGIN_GET_FACTORY)(GetProcAddress(plugin, "GetFactory"));
	if (get_factory == NULL)	THROW_WIN32_ERROR(L"file %s is not a file system plugin.", m_str_lib.c_str());

	jcvos::auto_interface<IFsFactory> factory;
	bool br = (get_factory)(factory);
	if (!br || !factory.valid()) THROW_ERROR(ERR_USER, L"failed on getting plugin register in %s", m_str_lib.c_str());

	br = factory->CreateFileSystem(m_fs, m_str_fs);
	if (!br || !m_fs) THROW_ERROR(ERR_APP, L"failed on creating file system");

	if (device_pt)
	{
		br = factory->CreateVirtualDisk(m_dev, (*device_pt), true);
	}

	// test for format
//	br = SporTest();
//	br = GeneralTest();
	br = FullTest();
	return 0;
}

void FillData(char * buf, size_t size)
{
	for (size_t ii = 0; ii < size; ++ii)			buf[ii] = ii % 26 + 'A';
}

int CompareData(const char * src, const char * tar, size_t size)
{
	for (size_t ii = 0; ii < size; ++ii)
	{
		if (src[ii] != tar[ii]) return ii;
//			THROW_ERROR(ERR_APP, L"failed on reading file, offset=%d", ii);
	}
	return -1;
}

bool CFsTesterApp::GeneralTest(void)
{
	bool br;
	br = m_fs->ConnectToDevice(m_dev);
	if (!br) THROW_ERROR(ERR_APP, L"failed on connect device");
	br = m_fs->MakeFileSystem(m_capacity, L"test");
	if (!br) THROW_ERROR(ERR_APP, L"failed on make file system");
	br = m_fs->Mount();
	if (!br) THROW_ERROR(ERR_APP, L"failed on mount");

	// test for file operations
	jcvos::auto_interface<IFileInfo> file;
	br = m_fs->DokanCreateFile(file, L"\\test.txt", GENERIC_READ | GENERIC_WRITE, 0, CREATE_ALWAYS, 0, 0, false);
	if (!br || !file) THROW_ERROR(ERR_APP, L"failed on creating file %s", L"\\test.txt");
	// write test
	file->SetEndOfFile(1024);
	jcvos::auto_array<char> buf(1024);
	FillData(buf, 1024);
	DWORD written = 0;
	file->DokanWriteFile(buf, 1024, written, 0);
	file->Cleanup();
	file->CloseFile();

	file.release();

	m_fs->Unmount();
	br = m_fs->Mount();
	if (!br) THROW_ERROR(ERR_APP, L"failed on mount");

	br = m_fs->DokanCreateFile(file, L"\\test.txt", GENERIC_READ, 0, OPEN_EXISTING, 0, 0, false);
	if (!br || !file) THROW_ERROR(ERR_APP, L"failed on opening file %s", L"\\test.txt");
	jcvos::auto_array<char> buf2(1024);
	DWORD read = 0;
	file->DokanReadFile(buf2, 1024, read, 0);
	int ir = CompareData(buf, buf2, 1024);
	if (ir >= 0)
	{
		THROW_ERROR(ERR_APP, L"failed on reading file, offset=%d", ir);
	}
	file->CloseFile();
	m_fs->Unmount();
	m_fs->Disconnect();

	return true;
}

#if 0
bool CFsTesterApp::SporTest(void)
{
	CJournalDevice *dev = dynamic_cast<CJournalDevice*>(m_dev);
	// create a file
	// test for format
	bool br;
	br = m_fs->ConnectToDevice(m_dev);
	if (!br) THROW_ERROR(ERR_APP, L"failed on connect device");
//	br = m_fs->MakeFileSystem(m_capacity, L"test");
//	if (!br) THROW_ERROR(ERR_APP, L"failed on make file system");
	br = m_fs->Mount();
	if (!br) THROW_ERROR(ERR_APP, L"failed on mount");
//	dev->SaveSnapshot(L"testdata\\formated.img");

	jcvos::auto_interface<IFileInfo> file;
	br = m_fs->DokanCreateFile(file, L"\\test.txt", GENERIC_READ | GENERIC_WRITE, 0, CREATE_ALWAYS, 0, 0, false);
	if (!br || !file) THROW_ERROR(ERR_APP, L"failed on creating file %s", L"\\test.txt");
	// write test
	file->SetEndOfFile(1024);
	jcvos::auto_array<char> buf(1024);
	for (size_t ii = 0; ii < 1024; ++ii)
	{
		buf[ii] = ii % 26 + 'A';
	}
	DWORD written = 0;
	file->DokanWriteFile(buf, 1024, written, 0);
	file->Cleanup();
	file->CloseFile();

	file.release();
	br = m_fs->FileSystemCheck(false);

	m_fs->Unmount();
	m_fs->Disconnect();

	//dev->SaveSnapshot(L"testdata\\snap.bin");

	//m_fs->ConnectToDevice(m_dev);
	//m_fs->Mount();

	//return true;

	// for each
	size_t steps = dev->GetSteps();
	for (size_t ss = 1; ss<=steps; ++ss)
	{
		LOG_NOTICE(L"start testing for step %d", ss);
		// merge data
		jcvos::auto_interface<IVirtualDisk> snapshot;
		dev->MakeSnapshot(snapshot, ss);
		// check fs
		br = m_fs->ConnectToDevice(snapshot);
		br = m_fs->Mount();
		br = m_fs->FileSystemCheck(false);
		if (br) { LOG_NOTICE(L"step %d without error", ss); }
		else 
		{
			wchar_t fn[MAX_PATH];
			swprintf_s(fn, L"testdata\\spor-snapshot-%d.img", ss);
//			snapshot->SaveSnapshot(fn);
			LOG_NOTICE(L"step %d found error, save snapshot to %s", ss, fn); 
		}
		m_fs->Unmount();
		m_fs->Disconnect();

	}
	return false;
}
#endif

#define TEST_LOG_SINGLE(...)  {\
	swprintf_s(m_log_buf, __VA_ARGS__);	\
	LOG_DEBUG(m_log_buf);	\
	if (m_log_file) {fwprintf_s(m_log_file, L"%s\n", m_log_buf); \
	fflush(m_log_file);} }

#define TEST_LOG(...)  {\
	swprintf_s(m_log_buf, __VA_ARGS__);	\
	LOG_DEBUG(m_log_buf);	\
	if (m_log_file) {fwprintf_s(m_log_file, m_log_buf); \
	/*fflush(m_log_file);*/ }}
	
#define TEST_ERROR(...) {	\
	swprintf_s(m_log_buf, __VA_ARGS__); \
	if (m_log_file) {fwprintf_s(m_log_file, L"[err] %s\n", m_log_buf); fflush(m_log_file);}\
	THROW_ERROR(ERR_USER, m_log_buf);	}

#define TEST_CLOSE_LOG {\
	if (m_log_file) {fwprintf_s(m_log_file, L"\n"); \
	fflush(m_log_file); }}

#define SHOW_PROGRESS  500
bool CFsTesterApp::FullTest(void)
{
	m_log_file=NULL;
	if (!m_log_fn.empty())
	{
		//_wfopen_s(&m_log_file, m_log_fn.c_str(), L"w+");
		m_log_file = _wfsopen(m_log_fn.c_str(), L"w+", _SH_DENYNO);
		if (!m_log_file) THROW_ERROR(ERR_USER, L"failed on opening log file %s", m_log_fn.c_str());
	}
	int depth = -1;
	UINT32 show_msg = 0;
	INandDriver * nand = dynamic_cast<INandDriver*>(m_dev);
	m_total_block = 0;
	if (nand)
	{
		INandDriver::NAND_DEVICE_INFO info;
		nand->GetFlashId(info);
		m_total_block = info.block_num;
	}

	boost::posix_time::ptime ts_start = boost::posix_time::microsec_clock::local_time();
	boost::posix_time::ptime ts_update = boost::posix_time::microsec_clock::local_time();;
	try
	{	// initlaize state
		CTestState * cur_state = m_test_state;
		bool br;
		br = m_fs->ConnectToDevice(m_dev);
		if (!br) THROW_ERROR(ERR_APP, L"failed on connect device");
		br = m_fs->MakeFileSystem(m_capacity, L"test");
		if (!br) THROW_ERROR(ERR_APP, L"failed on make file system");

		br = m_fs->Mount();
		if (!br) THROW_ERROR(ERR_APP, L"failed on mount");

		cur_state->Initialize(NULL);
		cur_state->EnumerateOp();
		depth = 0;
		while (1)
		{
			// pickup the first op in the 
			if (cur_state->m_cur_op >= cur_state->m_ops.size())
			{	// rollback, 需要逆向操作一下
				if (depth <= 0) break;
				depth--;
				cur_state = m_test_state + depth;
				JCASSERT(cur_state->m_cur_op > 0);
				FS_OP & op = cur_state->m_ops[cur_state->m_cur_op - 1];
				Rollback(m_fs, cur_state->m_ref_fs, &op);
				continue;
			}
			FS_OP & op = cur_state->m_ops[cur_state->m_cur_op];
			cur_state->m_cur_op++;

			// test
			CTestState * next_state = cur_state + 1;
			next_state->Initialize(&cur_state->m_ref_fs);
			// generate new state
			bool br = FsOperate(m_fs, next_state->m_ref_fs, &op);
			if (br)
			{	// 操作成功，保存操作，搜索下一步。
				// check max depth
				if (depth >= m_test_depth)
				{	// 达到最大深度，检查结果
					bool br = Verify(next_state->m_ref_fs, m_fs);
					if (!br) break;
					Rollback(m_fs, cur_state->m_ref_fs, &op);
					continue;
				}
				else
				{	// enumlate ops for new state
					depth++;
					next_state->EnumerateOp();
					cur_state = next_state;
				}
			}	

			boost::posix_time::ptime ts_cur = boost::posix_time::microsec_clock::local_time();
			if ((ts_cur - ts_update).total_seconds() > 30)
			{	// update lot
				INT64 ts = (ts_cur - ts_start).total_seconds();
				// get memory info
				bool br = PrintProgress(ts);
				if (!br) THROW_ERROR(ERR_USER, L"failed on getting space or health");
				ts_update = ts_cur;
			}
		}

		TEST_LOG(L"\n\n=== result ===\n");
		TEST_LOG(L"  Test successed!\n");
		boost::posix_time::ptime ts_cur = boost::posix_time::microsec_clock::local_time();
		INT64 ts = (ts_cur - ts_start).total_seconds();
		PrintProgress(ts);
		wprintf_s(L"Test completed\n");
		m_fs->Unmount();
		m_fs->Disconnect();
	}
	catch (jcvos::CJCException & err)
	{
		// show stack
		TEST_LOG(L"\n\n=== result ===\n");
		TEST_LOG(L"  Test failed\n [err] test failed with error: %s\n", err.WhatT());
		TEST_LOG(L"\n=== statck ===\n");
		for (int dd = 0; dd <= depth; ++dd)
		{
			fwprintf_s(m_log_file, L"stacks[%d]:\n", dd);
			m_test_state[dd].OutputState(m_log_file);
		}
		wprintf_s(L" Test failed! \n");
	}
	fclose(m_log_file);
	return true;
}

void CTestState::Initialize(const CReferenceFs * src)
{
	if (src)	m_ref_fs.CopyFrom(*src);
	else m_ref_fs.Initialize();
	m_ops.clear();
	m_cur_op = 0;
}

bool CFsTesterApp::FsOperate(IFileSystem * fs, CReferenceFs & ref, const FS_OP * op)
{
	bool isdir = true;
	bool res = true;
	size_t len = 0;
	JCASSERT(fs && op);
	switch (op->op_id)
	{
	case CREATE_FILE:	isdir = false;
	case CREATE_DIR:	{	// 共用代码
		// 检查自己点数量是否超标
		// create full path name
		TEST_LOG(L"[OP](%d) CREATE %s, path=%s, fn=%s,", m_op_id++, isdir ? L"DIR" : L"FILE", op->path.c_str(), op->param1.c_str());
		std::wstring path;
		if (op->path.size() > 1)	path = op->path + L"\\" + op->param1;	//non-root
		else path = op->path +  op->param1;		// root
		jcvos::auto_interface<IFileInfo> file;
		std::wstring file_path = path;
		bool br = fs->DokanCreateFile(file, file_path, GENERIC_READ | GENERIC_WRITE, 0, CREATE_NEW, 0, 0, isdir);
		// check if doubled name, update ref fs
		if (ref.IsExist(path))
		{	// 文件已经存在，要求返回false
			TEST_LOG(L" existing file/dir");
			if (br == true || file != NULL) THROW_ERROR(ERR_USER, L"create a file which is existed path=%s.", path.c_str());
			res = false;
		}
		else
		{	// create file in fs
			TEST_LOG(L" new file/dir");
			ref.AddPath(path, isdir);
			if (br == false || file == NULL) THROW_ERROR(ERR_USER, L"failed on creating file fn=%s", path.c_str());
			file->CloseFile();
			res = true;
		}
		TEST_CLOSE_LOG;
		break; }

	case DELETE_FILE:	break;
	case DELETE_DIR:	break;
	case MOVE:			break;
	case OVER_WRITE:	
		swscanf_s(op->param1.c_str(), L"%zd", &len);
		res = TestWrite(fs, ref, true, op->path, len);
		break;
	case APPEND_FILE:
		swscanf_s(op->param1.c_str(), L"%zd", &len);
		res = TestWrite(fs, ref, false, op->path, len);
		break;
	case DEMOUNT_MOUNT:
		TestMount(fs, ref);
		break;
	case POWER_OFF_RECOVERY:
		TestPower(fs, ref);
		break;
	}

	return res;
}

bool CFsTesterApp::TestMount(IFileSystem * fs, CReferenceFs & ref)
{
	TEST_LOG(L"[OP](%d) MOUNT", m_op_id++);
	//PrintProgress(0);
	IVirtualDisk::HEALTH_INFO info;
	m_dev->GetHealthInfo(info);
	LOG_DEBUG(L"empty block = %d", info.empty_block);
	fs->Unmount();
	bool br = fs->Mount();
	TEST_LOG(L" res=%s", br ? L"pass" : L"failed");
	if (!br) THROW_ERROR(ERR_USER, L"mount test returns false");
	TEST_CLOSE_LOG
	return br;
}

bool CFsTesterApp::TestWrite(IFileSystem * fs, CReferenceFs & ref, bool overwrite, const std::wstring & path, size_t len)
{
	len &= ~3;		// DWORD对齐
	TEST_LOG(L"[OP](%d) %s, path=%s, size=%zd", m_op_id++,
		overwrite ? L"OVER WRITE" : L"APPEND", path.c_str(), len);

	jcvos::auto_interface<IFileInfo> file;
	bool br = fs->DokanCreateFile(file, path, GENERIC_READ | GENERIC_WRITE, 0, OPEN_EXISTING, 0, 0, false);
	if (!br || !file)
	{
		JCASSERT(0);
		THROW_ERROR(ERR_USER, L"failed on opening file, fn=%s", path.c_str());
	}

	size_t cur_len;
	DWORD cur_checksum;
	CReferenceFs::CRefFile * ref_file = ref.FindFile(path);
	if (!ref_file) THROW_ERROR(ERR_USER, L"cannof find ref file: %s", path.c_str());
	ref.GetFileInfo(*ref_file, cur_checksum, cur_len);

	BY_HANDLE_FILE_INFORMATION info;
	file->GetFileInformation(&info);
	// get current file length
	if (cur_len != info.nFileSizeLow && info.nFileSizeLow != 0)
	{
		JCASSERT(0);
		THROW_ERROR(ERR_USER, L"file length does not match ref=%d, file=%d", cur_len, info.nFileSizeLow);
	}
	size_t offset = overwrite ? 0 : info.nFileSizeLow;
	if (offset == 0) cur_checksum = 0;
	jcvos::auto_array<char> buf(len);
	FillData(buf, len);
	DWORD written = 0;
	size_t new_size = offset + len;
	file->SetEndOfFile(new_size);
	file->DokanWriteFile(buf, boost::numeric_cast<DWORD>(len), written, offset);
	DWORD checksum = AppendChecksum(cur_checksum, buf, len);
	ref.UpdateFile(path, checksum, new_size);
	file->CloseFile();
	TEST_LOG(L"\t current size=%d, new size=%zd", info.nFileSizeLow, new_size);
	TEST_CLOSE_LOG;
	return true;
}

bool CFsTesterApp::TestPower(IFileSystem * fs, CReferenceFs & ref)
{
	IVirtualDisk::HEALTH_INFO info;
	m_dev->GetHealthInfo(info);
	LOG_DEBUG(L"empty block = %d", info.empty_block);
	fs->Unmount();

	INandDriver * nand = dynamic_cast<INandDriver*>(m_dev);
	if (!nand) THROW_ERROR(ERR_APP, L"the drive does not support NAND");
#if 1
	size_t log_num = nand->GetLogNumber();
	if (log_num == 0)
	{
		TEST_LOG(L"[OP](%d) POWER, backlog 0,", m_op_id++);
	}
	else
	{
		int roll_back = rand() % log_num;
		nand->BackLog(roll_back);
		TEST_LOG(L"[OP](%d) POWER, backlog %d / %zd,", m_op_id++, roll_back, log_num);
		LOG_NOTICE(L"roll back log %d", roll_back);
	}
#endif
	bool br = fs->Mount();
	nand->ResetLog();
	TEST_LOG(L" res=%s", br ? L"pass" : L"failed");
	if (!br) THROW_ERROR(ERR_USER, L"mount test returns false");
	TEST_CLOSE_LOG
	return true;
}

bool CFsTesterApp::Rollback(IFileSystem * fs, CReferenceFs & ref, const FS_OP * op)
{
	// 对op进行你操作
	std::wstring path;
	switch (op->op_id)
	{
	case CREATE_FILE: {
		//std::wstring path = op->path + L"\\" + op->param1;
		if (op->path == L"\\") path = op->path + op->param1;
		else path = op->path + L"\\" + op->param1;
		TEST_LOG(L"[ROLLBACK](%d) DELETE FILE, path=%s", m_op_id++, path.c_str());
		bool br = fs->DokanDeleteFile(path, NULL, false);
		if (!br) TEST_ERROR(L"failed on deleting file %s", path.c_str());
//		ref.RemoveFile(path);	// ref有backup，退回backup状态，不需要rollback
		TEST_CLOSE_LOG;
		break; }

	case CREATE_DIR: {	// rollback for: 在 “op->path”创建dir“op->param" => 删除dir:"op->path\\op->param"
		if (op->path == L"\\") path = op->path + op->param1;
		else path = op->path + L"\\" + op->param1;
		TEST_LOG(L"[ROLLBACK](%d) DELETE DIR, path=%s", m_op_id++, path.c_str());
		bool br = fs->DokanDeleteFile(path, NULL, true);
		if (!br) TEST_ERROR(L"failed on deleting dir %s", path.c_str());
//		ref.RemoveFile(path);	// ref有backup，退回backup状态，不需要rollback
		TEST_CLOSE_LOG;
		break; }

	case DELETE_FILE:	break;
	case DELETE_DIR:	break;
	case MOVE:			break;
	case OVER_WRITE: {
		// overwrite没有逆操作，只能删除文件，同时删除ref中的文件 X
		//	由于
		//TEST_LOG(L"[ROLLBACK](%d) DELETE FILE, path=%s", m_op_id ++, op->path.c_str());
		//bool br = fs->DokanDeleteFile(op->path, NULL, false);
		//if (!br) TEST_ERROR(L"failed on deleting file %s", op->path.c_str());
		//ref.RemoveFile(op->path);
		TEST_LOG(L"[ROLLBACK](%d) TRUNK FILE, path=%s", m_op_id ++, op->path.c_str());
		size_t cur_len;
		DWORD cur_checksum;
		CReferenceFs::CRefFile * ref_file = ref.FindFile(op->path);
		if (!ref_file) THROW_ERROR(ERR_USER, L"cannof find ref file: %s", op->path.c_str());
		ref.GetFileInfo(*ref_file, cur_checksum, cur_len);

		jcvos::auto_interface<IFileInfo> file;
		bool br = fs->DokanCreateFile(file, op->path, GENERIC_READ | GENERIC_WRITE, 0, OPEN_EXISTING, 0, 0, false);
		if (!br || !file) THROW_ERROR(ERR_USER, L"failed on opening file, fn=%s", op->path.c_str());
		BY_HANDLE_FILE_INFORMATION info;
		file->GetFileInformation(&info);
		TEST_LOG(L"\t cur_size=%d, org_size=%zd", info.nFileSizeLow, cur_len);

		file->SetEndOfFile(0);
		file->CloseFile();
		ref.UpdateFile(*ref_file, 0, 0);
		TEST_CLOSE_LOG;
		break; }

	case APPEND_FILE: {
		size_t cur_len;
		DWORD cur_checksum;
		CReferenceFs::CRefFile * ref_file = ref.FindFile(op->path);
		TEST_LOG(L"[ROLLBACK](%d) TRUNK, path=%s", m_op_id++, op->path.c_str());
		if (!ref_file) THROW_ERROR(ERR_USER, L"cannof find ref file: %s", op->path.c_str());
		ref.GetFileInfo(*ref_file, cur_checksum, cur_len);

		jcvos::auto_interface<IFileInfo> file;
		bool br = fs->DokanCreateFile(file, op->path, GENERIC_READ | GENERIC_WRITE, 0, OPEN_EXISTING, 0, 0, false);
		if (!br || !file) THROW_ERROR(ERR_USER, L"failed on opening file, fn=%s", op->path.c_str());
		BY_HANDLE_FILE_INFORMATION info;
		file->GetFileInformation(&info);
		TEST_LOG(L"\t cur_size=%d, org_size=%zd", info.nFileSizeLow, cur_len);

		file->SetEndOfFile(cur_len);
		file->CloseFile();
		TEST_CLOSE_LOG;
		break; }
	}
	return true;
}

DWORD CFsTesterApp::AppendChecksum(DWORD cur_checksum, const char * buf, size_t size)
{
	const DWORD * bb = reinterpret_cast<const DWORD*>(buf);
	size_t ss = size / 4;
	for (size_t ii = 0; ii < ss; ++ii) cur_checksum += bb[ii];
	return cur_checksum;
}

bool CFsTesterApp::Verify(const CReferenceFs & ref, IFileSystem * fs)
{
	LOG_STACK_TRACE();
	TEST_LOG(L"[BEGIN VERIFY]\n")
	auto endit = ref.End();
	for (auto it = ref.Begin(); it != endit; ++it)
	{
		const CReferenceFs::CRefFile & ref_file = ref.GetFile(it);
		std::wstring path;
		ref.GetFilePath(ref_file, path);
		bool dir = ref.IsDir(ref_file);
		TEST_LOG(L"\t<check %s> %s\\", dir ? L"dir" : L"file", path.c_str());
		jcvos::auto_interface<IFileInfo> file;
		bool br = fs->DokanCreateFile(file, path, GENERIC_READ, 0, OPEN_EXISTING, 0, 0, dir);
		if (!br || !file) THROW_ERROR(ERR_USER, L"failed on opening file, fn=%s", path.c_str());

		if (!dir)	
		{
			DWORD ref_checksum;
			size_t ref_len;
			ref.GetFileInfo(ref_file, ref_checksum, ref_len);
			BY_HANDLE_FILE_INFORMATION info;
			file->GetFileInformation(&info);
			TEST_LOG(L" ref size=%zd, file size=%d, checksum=0x%08X", ref_len, info.nFileSizeLow, ref_checksum);
			if (info.nFileSizeLow == 0)
			{	// 有可能是over write的roll back造成的，忽略比较
				TEST_LOG(L" ignor comparing");
			}
			else
			{
				TEST_LOG(L" compare");
				if (ref_len != info.nFileSizeLow) THROW_ERROR(ERR_USER, L"file length does not match ref=%d, file=%d", ref_len, info.nFileSizeLow);
				jcvos::auto_array<char> buf(ref_len);
				DWORD read = 0;
				file->DokanReadFile(buf, boost::numeric_cast<DWORD>(ref_len), read, 0);

				DWORD checksum = 0;
				checksum = AppendChecksum(0, buf, ref_len);
				if (checksum != ref_checksum) THROW_ERROR(ERR_USER, L"checksum does not match, ref=0x%08X, file=0x%08X", ref_checksum, checksum);
			}
		}
		file->CloseFile();
		TEST_CLOSE_LOG;
	}
	TEST_LOG(L"[END VERIFY]\n")
	return true;
}

bool CFsTesterApp::PrintProgress(INT64 ts)
{
	HANDLE handle = GetCurrentProcess();
	PROCESS_MEMORY_COUNTERS_EX pmc = { 0 };
	GetProcessMemoryInfo(handle, (PROCESS_MEMORY_COUNTERS*)&pmc, sizeof(pmc));

	IVirtualDisk::HEALTH_INFO hinfo;
	bool br1 = m_dev->GetHealthInfo(hinfo);
	if (!br1) LOG_ERROR(L"failed on getting disk health info");
	ULONGLONG free_bytes, total_bytes, total_free_bytes;
	bool br2 = m_fs->DokanGetDiskSpace(free_bytes, total_bytes, total_free_bytes);
	if (!br2) LOG_ERROR(L"failed on getting fs space");
	float usage = (float)(total_bytes - free_bytes) / total_bytes * 100;
	wprintf_s(L"ts=%ds, op=%d, fs_usage=%.1f%%, disk_usage=%d, write=%d, mem=%.1fMB \n",
		(int)ts, m_op_id, usage, m_total_block - hinfo.empty_block, hinfo.media_write,
		(float)pmc.WorkingSetSize / 1024.0);
	return br1 && br2;
}

bool CTestState::EnumerateOp(void)
{
	auto endit = m_ref_fs.End();
	auto it = m_ref_fs.Begin();
	for (; it != endit; it++)
	{
		const CReferenceFs::CRefFile & file = m_ref_fs.GetFile(it);
		bool isdir = m_ref_fs.IsDir(file);
		//size_t pos = path.find(':');
		std::wstring path;
		m_ref_fs.GetFilePath(file, path);

		DWORD checksum;
		size_t len;
		m_ref_fs.GetFileInfo(file, checksum, len);

		if (isdir)
		{	// 目录
			size_t child_num = ~len;
			if (child_num >= MAX_CHILD_NUM) continue;

			wchar_t fn[3];	//文件名，随机产生2字符
			// 添加目录
			GenerateFn(fn, 2);
			FS_OP op2(CREATE_DIR, path, fn);
			m_ops.push_back(op2);
			// 添加文件
			GenerateFn(fn, 2);
			FS_OP op1(CREATE_FILE, path, fn);
			m_ops.push_back(op1);
			// 移动目录
		}
		else
		{	// 文件
			wchar_t str_len[20];
			if (len == 0)
			{	// 空文件
				size_t len = rand() * MAX_FILE_SIZE / RAND_MAX;
				swprintf_s(str_len, L"%zd", len);
				FS_OP op1(APPEND_FILE, path, str_len);
				m_ops.push_back(op1);
			}
			else
			{
				size_t len = rand() * MAX_FILE_SIZE / RAND_MAX;
				swprintf_s(str_len, L"%zd", len);
				FS_OP op1(APPEND_FILE, path, str_len);
				m_ops.push_back(op1);

				len = rand() * MAX_FILE_SIZE / RAND_MAX;
				swprintf_s(str_len, L"%zd", len);
				FS_OP op2(OVER_WRITE, path, str_len);
				m_ops.push_back(op2);
			}
		}
	}
	//FS_OP mount_op(DEMOUNT_MOUNT, L"");
	//m_ops.push_back(mount_op);

	FS_OP spor_op(POWER_OFF_RECOVERY, L"");
	m_ops.push_back(spor_op);
	return true;
}

void CTestState::OutputState(FILE * log_file)
{
	// output ref fs
	JCASSERT(log_file);
	fwprintf_s(log_file, L"ref fs=\n");
	auto endit = m_ref_fs.End();
	auto it = m_ref_fs.Begin();
	for (; it != endit; it++)
	{
		const CReferenceFs::CRefFile & ref_file = m_ref_fs.GetFile(it);
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
	JCASSERT(m_cur_op > 0);
	FS_OP &op = m_ops[m_cur_op-1];
	const wchar_t * op_name = NULL;
	switch (op.op_id)
	{
	case OP_NONE:		op_name = L"none       ";	break;
	case CREATE_FILE:   op_name = L"create-file";	break;
	case CREATE_DIR:	op_name = L"create-dir ";	break;
	case DELETE_FILE:	op_name = L"delete-file";	break;
	case DELETE_DIR:	op_name = L"delete-dir ";	break;
	case MOVE:			op_name = L"move       ";	break;
	case APPEND_FILE:	op_name = L"append     ";	break;
	case OVER_WRITE:	op_name = L"overwrite  ";	break;
	case DEMOUNT_MOUNT:	op_name = L"demnt-mount";	break;
	default:			op_name = L"unknown    ";	break;
	}
	fwprintf_s(log_file, L"[%s] path=%s, %s, %s\n", op_name, op.path.c_str(), op.param1.c_str(), op.param2.c_str());
}

void CTestState::GenerateFn(wchar_t * fn, size_t len)
{
	size_t ii;
	for (ii = 0; ii < len; ++ii)
	{
		fn[ii] = rand() % 26 + 'A';
	}
	fn[ii] = 0;
}

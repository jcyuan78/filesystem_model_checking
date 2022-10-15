///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// dokanfs-tester.cpp : 此文件包含 "main" 函数。程序执行将在此处开始并结束。
//
#include "pch.h"
#include "dokanfs-tester.h"
#include <shlwapi.h>

#pragma comment (lib, "shlwapi.lib")
#include <Shlobj.h>
#include <list>

#ifdef _DEBUG
#include <vld.h>
#endif

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
	m_test_spor = false;
	m_support_trunk = true;
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
	jcvos::UnicodeToUtf8(str_fn, filename);
	prop_tree::wptree pt;
	prop_tree::json_parser::read_json(str_fn, pt);

	const auto & device_pt = pt.get_child_optional(L"config.device");
	int err = CDokanFsBase::LoadFilesystemByConfig(m_fs, m_dev, L"", pt);
	if (err)
	{
		LOG_ERROR(L"[err] failed on creating file system from configuration, err=%d", err);
		return err;
	}
	m_capacity = m_dev->GetCapacity();

	bool br = false;
	const prop_tree::wptree& test_pt = pt.get_child(L"config.test");
	const std::wstring& test_mode = test_pt.get<std::wstring>(L"mode", L"general");

// load config
	m_test_spor = test_pt.get < bool>(L"spor", false);
	m_support_trunk = test_pt.get<bool>(L"support_trunk", true);
	// test for format
	if (test_mode == L"general")		br = GeneralTest();
	else if (test_mode == L"full")		br = FullTest();

//	br = SporTest();
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
		if (src[ii] != tar[ii]) return boost::numeric_cast<int>(ii);
//			THROW_ERROR(ERR_APP, L"failed on reading file, offset=%d", ii);
	}
	return -1;
}



class CDokanTestFind : public EnumFileListener
{
public:
	CDokanTestFind(void) {};
	~CDokanTestFind(void) {};
public:
	virtual bool EnumFileCallback(const std::wstring& fn,
		UINT32 ino, UINT32 entry, // entry 在父目录中的位置
		BY_HANDLE_FILE_INFORMATION* finfo)
	{
		wprintf_s(L"found item, fn:%s, index=%d, size=%d\n", fn.c_str(), ino, finfo->nFileSizeLow);
		return true;
	}
};

class CDokanSearchCallback : public EnumFileListener
{
public:
	CDokanSearchCallback(const std::wstring & pp, std::list<std::wstring> & cc) : parent(pp), child(cc) {};
	~CDokanSearchCallback(void) {};
public:
	virtual bool EnumFileCallback(const std::wstring& fn,
		UINT32 ino, UINT32 entry, // entry 在父目录中的位置
		BY_HANDLE_FILE_INFORMATION* finfo)
	{
		std::wstring path;
		if (parent == L"\\")  path = L"\\" + fn;
		else path = parent + L"\\" + fn;
		wprintf_s(L"item, path=%s,\tindex=%d,\tsize=%d\n", path.c_str(), ino, finfo->nFileSizeLow);
		if (fn != L"." && fn != L"..")	child.push_back(path);
		return true;
	}

	std::list<std::wstring>& child;
	std::wstring parent;
};

// 用于深度搜索栈的结构
class CStackEntry
{
public:
	std::wstring cur_path;
	std::wstring cur_name;
	std::list<std::wstring> children;
	std::list<std::wstring>::iterator cur_child;
};

void expand_node(IFileSystem* fs, CStackEntry & entry)
{
	// 扩展当前节点。如果当前节点的路径指向目录，则枚举所有的子目录放入child列表，初始化cur_child指针。
	const std::wstring& cur_path = entry.cur_path;
	jcvos::auto_interface<IFileInfo> dir;
	NTSTATUS err = fs->DokanCreateFile(dir, cur_path, GENERIC_READ, 0, IFileSystem::FS_OPEN_EXISTING, 0, 0, false);
	if (dir == nullptr) THROW_ERROR(ERR_APP, L"failed on open %s", cur_path.c_str());
	if (dir->IsDirectory() )
	{
		CDokanSearchCallback callback(entry.cur_path, entry.children);
		dir->EnumerateFiles(&callback);
		entry.cur_child = entry.children.begin();
	}
	dir->Cleanup();
	dir->CloseFile();
}



bool CFsTesterApp::ListAllItems(const std::wstring& root)
{
	wprintf_s(L"[begin] list all items of %s\n", root.c_str());
	std::list<CStackEntry> stack;

	// init stack
	stack.emplace_back();
	CStackEntry& entry = stack.back();
	entry.cur_path = root;
	entry.cur_name = L"";
	expand_node(m_fs, entry);

	// depth first search
	auto cur_depth = stack.begin();
	// 循环不变状态：cur_depth指向一个节点，如果该节点的 children列表为空，则不需要搜索。
	//		否则该节点的cur_child指向下一个要搜索的路径。
	while (1)
	{
		CStackEntry& entry = *cur_depth;

		if (entry.children.empty() || entry.cur_child == entry.children.end())
		{	// roll back
			if (cur_depth == stack.begin()) break;		// 搜索完成
			cur_depth--;
			stack.pop_back();
		}
		else
		{
			const std::wstring& path = *(entry.cur_child);
			entry.cur_child++;
			stack.emplace_back();
			CStackEntry& new_entry = stack.back();
			new_entry.cur_path = path;
			cur_depth++;
			expand_node(m_fs, *cur_depth);
		}
	}
	wprintf_s(L"[end] list items\n\n");
	return false;
}

bool CFsTesterApp::MakeDir(const std::wstring& dir_name)
{
	jcvos::auto_interface<IFileInfo> dir;
	NTSTATUS err = m_fs->DokanCreateFile(dir, dir_name, GENERIC_ALL, 0, IFileSystem::FS_CREATE_ALWAYS, 0, 0, true);
	if (err != STATUS_SUCCESS || !dir) THROW_ERROR(ERR_APP, L"failed on creating dir %s", dir_name.c_str());
	//	dir->CloseFile();	
	return true;
}

int CFsTesterApp::NewFile(const std::wstring& file_name)
{
	jcvos::auto_interface<IFileInfo> file;

	int err = m_fs->DokanCreateFile(file, file_name, GENERIC_ALL, 0, IFileSystem::FS_CREATE_ALWAYS, 0, 0, false);
	if (/*!br ||*/ !file)
	{
		LOG_ERROR(L"[err] failed on opening file %s", file_name.c_str());
		return err;
	}
	file->CloseFile();
	return err;
}

int CFsTesterApp::DtDeleteFile(const std::wstring& file_name)
{
	jcvos::auto_interface<IFileInfo> file;
	int err = m_fs->DokanCreateFile(file, file_name, GENERIC_ALL, 0, IFileSystem::FS_OPEN_EXISTING, 0, 0, false);
	if (!file)
	{
		LOG_ERROR(L"[err] failed on opening file %s", file_name.c_str());
		return err;
	}
	err = m_fs->DokanDeleteFile(file_name, file, false);
	file->CloseFile();
	wprintf_s(L"delete file %s, err=0x%X\n", file_name.c_str(), err);
	return err;
}

int CFsTesterApp::DtMoveFile(const std::wstring& src_dir, const std::wstring & src_fn, const std::wstring& dst, bool replace)
{
	jcvos::auto_interface<IFileInfo> f1;
	// open src dir
	jcvos::auto_interface<IFileInfo> d1;
	NTSTATUS err = 0;

	//err = m_fs->DokanCreateFile(f1, src_fn, GENERIC_ALL, 0, IFileSystem::FS_OPEN_EXISTING, 0, 0, true);
	//wprintf_s(L"open source file %s, err=0x%X\n", src_fn.c_str(), err);
	//if (f1 == nullptr)
	//{
	//	wprintf_s(L"source file %s does not exist\n", src_fn.c_str());
	//	return err;
	//}

	err = m_fs->DokanCreateFile(d1, src_dir, GENERIC_ALL, 0, IFileSystem::FS_OPEN_EXISTING, 0, 0, true);
	wprintf_s(L"open source dir %s, err=0x%X\n", src_dir.c_str(), err);
	if (d1 == nullptr)
	{
		wprintf_s(L"source file %s does not exist\n", src_dir.c_str());
		return err;
	}	
	err = m_fs->DokanMoveFile(src_fn, dst, replace, nullptr);
	wprintf_s(L"move file from (%s) %s to %s, err=0x%X\n", replace?L"replace":L"non-replace", src_fn.c_str(), dst.c_str(), err);
	d1->CloseFile();
	//f1->CloseFile();
	return err;
}
int CFsTesterApp::FileInfo(const std::wstring& fn)
{
	jcvos::auto_interface<IFileInfo> f1;
	NTSTATUS err = m_fs->DokanCreateFile(f1, fn, GENERIC_ALL, 0, IFileSystem::FS_OPEN_EXISTING, 0, 0, true);
	if (f1 == nullptr)
	{
		wprintf_s(L"file %s does not exists, err=0x%X\n", fn.c_str(), err);
		return err;
	}
	BY_HANDLE_FILE_INFORMATION fileinfo;
	f1->GetFileInformation(&fileinfo);
	wprintf_s(L"file info: %s, attr=0x%X, size=%d, link=%d, index=%d\n", fn.c_str(), fileinfo.dwFileAttributes, fileinfo.nFileSizeLow, fileinfo.nNumberOfLinks, fileinfo.nFileIndexLow);
	return 0;
}


bool CFsTesterApp::GeneralTest(void)
{
	bool br;
	br = m_fs->MakeFileSystem(m_dev, boost::numeric_cast<UINT32>(m_capacity), L"test");
	if (!br) THROW_ERROR(ERR_APP, L"failed on make file system");
 	br = m_fs->Mount(m_dev);
	if (!br) THROW_ERROR(ERR_APP, L"failed on mount");

	NTSTATUS err;

	err = MakeDir(L"\\55a576b3");
	err = FileInfo(L"\\55a576b3");
	err = MakeDir(L"\\55a576b3\\foo");
	err = MakeDir(L"\\55a576b3\\baz");
	err = FileInfo(L"\\55a576b3");
	err = DtMoveFile(L"\\55a576b3", L"\\55a576b3\\foo", L"\\55a576b3\\bar", false);
	err = FileInfo(L"\\55a576b3");
	err = DtMoveFile(L"\\55a576b3", L"\\55a576b3\\bar", L"\\55a576b3\\baz", false);
	err = FileInfo(L"\\55a576b3");
	err = DtDeleteFile(L"\\55a576b3\\baz");
	err = FileInfo(L"\\55a576b3");
	err = DtMoveFile(L"\\55a576b3", L"\\55a576b3\\bar", L"\\55a576b3\\baz", true);
	err = FileInfo(L"\\55a576b3");
	//err = DtDeleteFile(L"\\55a576b3\\baz");
	//err = FileInfo(L"\\55a576b3");
	//err = DtDeleteFile(L"\\55a576b3");
	

//	MakeDir(L"\\folder\\subfolder");
//	MakeDir(L"\\ll2");

	//NewFile(L"\\ll2\\baz");
	//NewFile(L"\\folder\\bar");
	//DtMoveFile(L"\\ll2\\baz", L"\\folder\\baz");
	//ListAllItems(L"\\folder");

	//DtDeleteFile(L"\\folder\\bar");
	//DtDeleteFile(L"\\folder\\baz");
	//DtDeleteFile(L"\\folder");


//#define TEST_FN	(L"\\test.txt")
#define TEST_FN	(L"\\folder\\subfolder\\test.txt")

	jcvos::auto_interface<IFileInfo> file;
	jcvos::auto_array<char> buf(1024);
#if 0
	// test for file operations
	err = m_fs->DokanCreateFile(file, TEST_FN, GENERIC_READ | GENERIC_WRITE, 0, IFileSystem::FS_CREATE_ALWAYS, 0, 0, false);
	if (/*err!=STATUS_SUCCESS ||*/ !file) THROW_ERROR(ERR_APP, L"failed on creating file %s", TEST_FN);
	// write test
	file->SetEndOfFile(1024);
	FillData(buf, 1024);
	DWORD written = 0;
	file->DokanWriteFile(buf, 1024, written, 0);
//	file->Cleanup();
	file->CloseFile();
	file.release();

	err = m_fs->DokanCreateFile(file, TEST_FN, GENERIC_READ, 0, IFileSystem::FS_OPEN_EXISTING, 0, 0, false);
	if (/*!br ||*/ !file) THROW_ERROR(ERR_APP, L"failed on opening file %s", TEST_FN);
	file->CloseFile();
	file.release();

	ListAllItems(L"\\");

	m_fs->DokanMoveFile(L"\\folder\\subfolder\\test.txt", L"\\l2\\ttt.txt", false, nullptr);
	ListAllItems(L"\\");

	err = m_fs->DokanCreateFile(file, L"\\l2\\ttt.txt", GENERIC_READ, 0, IFileSystem::FS_OPEN_EXISTING, 0, 0, false);
	if (/*!br ||*/ !file) THROW_ERROR(ERR_APP, L"failed on opening file %s", TEST_FN);
	m_fs->DokanDeleteFile(L"\\l2\\ttt.txt", file, false);
	file->CloseFile();
	file.release();
#endif

//	m_fs->DokanDeleteFile(L"\\folder\\subfolder\\test.txt", nullptr, false);
	ListAllItems(L"\\");

	//CDokanTestFind callback;
	//err = m_fs->DokanCreateFile(file, L"\\", GENERIC_READ, 0, IFileSystem::FS_OPEN_EXISTING, 0, 0, true);
	//if (/*!br ||*/ !file) THROW_ERROR(ERR_APP, L"failed on open root dir");
	//file->EnumerateFiles(&callback);
	//file->Cleanup();
	//file->CloseFile();
	//file.release();

	m_fs->Unmount();
	br = m_fs->Mount(m_dev);
	if (!br) THROW_ERROR(ERR_APP, L"failed on mount");

	ListAllItems(L"\\");

	err = m_fs->DokanCreateFile(file, TEST_FN, GENERIC_READ, 0, IFileSystem::FS_OPEN_EXISTING, 0, 0, false);
	if (/*!br ||*/ !file) THROW_ERROR(ERR_APP, L"failed on opening file %s", L"\\folder\\test.txt");
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
//	m_fs->Disconnect();
	wprintf_s(L"test completed, no error detected");
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
		m_total_block = boost::numeric_cast<UINT32>( info.block_num);
	}

	boost::posix_time::ptime ts_start = boost::posix_time::microsec_clock::local_time();
	boost::posix_time::ptime ts_update = boost::posix_time::microsec_clock::local_time();;
	try
	{	// initlaize state
		CTestState * cur_state = m_test_state;
		bool br;
//		br = m_fs->ConnectToDevice(m_dev);
//		if (!br) THROW_ERROR(ERR_APP, L"failed on connect device");
		br = m_fs->MakeFileSystem(m_dev, boost::numeric_cast<UINT32>(m_capacity), L"test");
		if (!br) THROW_ERROR(ERR_APP, L"failed on make file system");

		br = m_fs->Mount(m_dev);
		if (!br) THROW_ERROR(ERR_APP, L"failed on mount");

		cur_state->Initialize(NULL);
		cur_state->EnumerateOp(m_test_spor);
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
					next_state->EnumerateOp(m_test_spor);
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
		//m_fs->Disconnect();
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
		bool br = fs->DokanCreateFile(file, file_path, GENERIC_READ | GENERIC_WRITE, 0, IFileSystem::FS_CREATE_NEW, 0, 0, isdir);
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
			if (br == false || !file) THROW_ERROR(ERR_USER, L"failed on creating file fn=%s", path.c_str());
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
	bool br = fs->Mount(m_dev);
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
	bool br = fs->DokanCreateFile(file, path, GENERIC_READ | GENERIC_WRITE, 0, IFileSystem::FS_OPEN_EXISTING, 0, 0, false);
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
	//<TODO> ！！此处设置 SetEndOfFile的用法有误。SetEndOfFile()将打开文件的当前位置设置为文件结束。
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

	//INandDriver * nand = dynamic_cast<INandDriver*>(m_dev);
	//if (!nand) THROW_ERROR(ERR_APP, L"the drive does not support NAND");
#if 1
	size_t log_num = m_dev->GetLogNumber();
	if (log_num == 0)
	{
		TEST_LOG(L"[OP](%d) POWER, backlog 0,", m_op_id++);
	}
	else
	{
		int roll_back = rand() % log_num;
		m_dev->BackLog(roll_back);
		TEST_LOG(L"[OP](%d) POWER, backlog %d / %zd,", m_op_id++, roll_back, log_num);
		LOG_NOTICE(L"roll back log %d", roll_back);
	}
#endif
	bool br = fs->Mount(m_dev);
	m_dev->ResetLog();
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
		if (!m_support_trunk) break;
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
		bool br = fs->DokanCreateFile(file, op->path, GENERIC_READ | GENERIC_WRITE, 0, IFileSystem::FS_OPEN_EXISTING, 0, 0, false);
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
		if (!m_support_trunk) break;
		size_t cur_len;
		DWORD cur_checksum;
		CReferenceFs::CRefFile * ref_file = ref.FindFile(op->path);
		TEST_LOG(L"[ROLLBACK](%d) TRUNK, path=%s", m_op_id++, op->path.c_str());
		if (!ref_file) THROW_ERROR(ERR_USER, L"cannof find ref file: %s", op->path.c_str());
		ref.GetFileInfo(*ref_file, cur_checksum, cur_len);

		jcvos::auto_interface<IFileInfo> file;
		bool br = fs->DokanCreateFile(file, op->path, GENERIC_READ | GENERIC_WRITE, 0, IFileSystem::FS_OPEN_EXISTING, 0, 0, false);
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
		bool br = fs->DokanCreateFile(file, path, GENERIC_READ, 0, IFileSystem::FS_OPEN_EXISTING, 0, 0, dir);
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

bool CTestState::EnumerateOp(bool test_spor)
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
	if (test_spor)
	{
		FS_OP spor_op(POWER_OFF_RECOVERY, L"");
		m_ops.push_back(spor_op);
	}
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

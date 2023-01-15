///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// dokanfs-tester.cpp : 此文件包含 "main" 函数。程序执行将在此处开始并结束。
//
#include "pch.h"
#include <lib-fstester.h>
#include "../include/general_tester.h"
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
#include <boost/property_tree/xml_parser.hpp>
namespace prop_tree = boost::property_tree;
#include <boost/date_time/posix_time/posix_time.hpp>

//#include "../include/full_tester.h"


LOCAL_LOGGER_ENABLE(L"fstester.app", LOGGER_LEVEL_DEBUGINFO);

const TCHAR CFsTesterApp::LOG_CONFIG_FN[] = L"fstester.cfg";
typedef jcvos::CJCApp<CFsTesterApp>	CApplication;
CApplication _app;

#define _class_name_	CApplication
BEGIN_ARGU_DEF_TABLE()

ARGU_DEF(L"format", 'f', m_volume_name, L"format device using volume name.")
//ARGU_DEF(L"mount", 'm', m_mount, L"mount point.")
//ARGU_DEF(L"unmount", 'u', m_unmount, L"unmount device.", false)
ARGU_DEF(L"config", 'c', m_config_file, L"configuration file name")
ARGU_DEF(L"depth", 'd', m_test_depth, L"depth for test", (size_t)(MAX_DEPTH))
ARGU_DEF(L"log_file", 'l', m_log_fn, L"specify test log file name")
ARGU_DEF(L"save_config", 's', m_save_config, L"save config to specified file")

//ARGU_DEF(L"target", 't', m_root, L"target folder to test, like D:, D:\\test")

END_ARGU_DEF_TABLE()

int _tmain(int argc, _TCHAR* argv[])
{
	return jcvos::local_main(argc, argv);
}

BOOL WINAPI HandlerRoutine(DWORD dwCtrlType);

CFsTesterApp::CFsTesterApp(void)
	: m_dev(NULL), m_fs(NULL)
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
	if (str_fn.rfind(".xml") != std::string::npos) { prop_tree::xml_parser::read_xml(str_fn, pt); }
	else if (str_fn.rfind(".json") != std::string::npos) { prop_tree::json_parser::read_json(str_fn, pt); }
	else THROW_ERROR(ERR_APP, L"Unknown format: %s", filename);
	//	prop_tree::json_parser::read_json(str_fn, pt);

	if (!m_save_config.empty())
	{
		GetFullPathName(m_save_config.c_str(), MAX_PATH, path, NULL);

		wcscpy_s(filename, path);
		PathRemoveFileSpec(path);
		SetCurrentDirectory(path);
		PathStripPath(filename);
		wprintf_s(L"save config to %s", filename);
		jcvos::UnicodeToUtf8(str_fn, filename);
		if (str_fn.rfind(".xml") != std::string::npos) 
		{
			prop_tree::xml_parser::write_xml(str_fn, pt); 
		}
		else if (str_fn.rfind(".json") != std::string::npos) 
		{
			prop_tree::json_parser::write_json(str_fn, pt); 
		}
		else THROW_ERROR(ERR_APP, L"Unknown format: %s", filename);
		return 0;
	}

	const auto & device_pt = pt.get_child_optional(L"config.device");
	int err = CDokanFsBase::LoadFilesystemByConfig(m_fs, m_dev, L"", pt);
	if (err)
	{
		LOG_ERROR(L"[err] failed on creating file system from configuration, err=%d", err);
		return err;
	}
//	m_capacity = m_dev->GetCapacity();

	bool br = false;
	const prop_tree::wptree& test_pt = pt.get_child(L"config.test");
	const std::wstring& test_mode = test_pt.get<std::wstring>(L"mode", L"general");

// load config
	m_test_spor = test_pt.get < bool>(L"spor", false);
	m_support_trunk = test_pt.get<bool>(L"support_trunk", true);
	// test for format
	CTesterBase* tester = nullptr;
	if (0) {}
	else if (test_mode == L"general")			{ tester = new CGeneralTester	(m_fs, m_dev); }
	else if (test_mode == L"full")				{ tester = new CFullTester		(m_fs, m_dev); }
	//else if (test_mode == L"full_multi_thread") { tester = new CMultiThreadTest	(m_fs, m_dev); }
	else if (test_mode == L"trace_test")		{ tester = new CTraceTester		(m_fs, m_dev); }
	else if (test_mode == L"performance") { tester = new CPerformanceTester(m_fs, m_dev); }
	if (tester == nullptr) THROW_ERROR(ERR_MEM, L"failed on creating tester: %s", test_mode.c_str());
	tester->Config(test_pt, L"");
	err = tester->StartTest();
	delete tester;

	return err;
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



bool CGeneralTester::ListAllItems(const std::wstring& root)
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

int CGeneralTester::DtMoveFile(const std::wstring& src_dir, const std::wstring & src_fn, const std::wstring& dst, bool replace)
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

int CGeneralTester::CopyFileToDokan(const std::wstring& src_fn, const std::wstring& dst_fn, size_t cache_size)
{
	HANDLE src_file = CreateFile(src_fn.c_str(), GENERIC_READ, FILE_SHARE_READ|FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr);
	if (src_file == INVALID_HANDLE_VALUE) THROW_WIN32_ERROR(L"failed on open source file %s", src_fn.c_str());
	size_t src_size = GetFileSize(src_file, nullptr);

	jcvos::auto_interface<IFileInfo> file;
	m_fs->DokanCreateFile(file, dst_fn, GENERIC_ALL, 0, IFileSystem::FS_CREATE_ALWAYS, FILE_SHARE_READ|FILE_SHARE_WRITE, 0, false);
	if (file == nullptr) THROW_ERROR(ERR_APP, L"failed on create destinate file: %s", dst_fn.c_str());
	file->SetEndOfFile(src_size);

	jcvos::auto_array<BYTE> buf(cache_size);
	size_t offset = 0;
		DokanHealthInfo health_info;
	while (src_size > 0)
	{
		DWORD to_read = (DWORD)(min(cache_size, src_size));
		DWORD read = 0, written=0;
		BOOL br = ReadFile(src_file, buf, to_read, &read, nullptr);
		if (!br || read != to_read) THROW_ERROR(ERR_APP, L"failed on reading source file at %zd", offset);
		br = file->DokanWriteFile(buf, to_read, written, offset);
		if (!br || written != to_read) THROW_ERROR(ERR_APP, L"failed on writing destinate at %zd", offset);
		offset += to_read;
		src_size -= to_read;

		DWORD rr;
		m_health_file->DokanReadFile(&health_info, sizeof(DokanHealthInfo), rr, 0);
		LOG_DEBUG(L"page_nr=%lld, free=%lld, inactive=%lld, active=%lld, used=%lld",
			health_info.m_page_cache_size, health_info.m_page_cache_free, health_info.m_page_cache_inactive, health_info.m_page_cache_active, (health_info.m_page_cache_size - health_info.m_page_cache_free - health_info.m_page_cache_inactive - health_info.m_page_cache_active));
	}
	file->CloseFile();
	LOG_DEBUG(L"page_nr=%lld, free=%lld, inactive=%lld, active=%lld, used=%lld",
		health_info.m_page_cache_size, health_info.m_page_cache_free, health_info.m_page_cache_inactive, health_info.m_page_cache_active, (health_info.m_page_cache_size - health_info.m_page_cache_free - health_info.m_page_cache_inactive - health_info.m_page_cache_active));

	CloseHandle(src_file);
	return 0;
}

int CGeneralTester::CopyCompareTest(const std::wstring& src_fn, const std::wstring& dst_fn, size_t cache_size)
{
	HANDLE src_file = CreateFile(src_fn.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr);
	if (src_file == INVALID_HANDLE_VALUE) THROW_WIN32_ERROR(L"failed on open source file %s", src_fn.c_str());
	size_t src_size = GetFileSize(src_file, nullptr);

	jcvos::auto_interface<IFileInfo> file;
	m_fs->DokanCreateFile(file, dst_fn, GENERIC_ALL, 0, IFileSystem::FS_CREATE_ALWAYS, FILE_SHARE_READ | FILE_SHARE_WRITE, 0, false);
	if (file == nullptr) THROW_ERROR(ERR_APP, L"failed on create destinate file: %s", dst_fn.c_str());
	file->SetEndOfFile(src_size);

	jcvos::auto_array<BYTE> buf(cache_size);
	size_t offset = 0;
	DokanHealthInfo health_info;
	while (src_size > 0)
	{
		DWORD to_read = (DWORD)(min(cache_size, src_size));
		DWORD read = 0, written = 0;
		BOOL br = ReadFile(src_file, buf, to_read, &read, nullptr);
		if (!br || read != to_read) THROW_ERROR(ERR_APP, L"failed on reading source file at %zd", offset);
		br = file->DokanWriteFile(buf, to_read, written, offset);
		if (!br || written != to_read) THROW_ERROR(ERR_APP, L"failed on writing destinate at %zd", offset);
		offset += to_read;
		src_size -= to_read;

		DWORD rr;
		m_health_file->DokanReadFile(&health_info, sizeof(DokanHealthInfo), rr, 0);
		LOG_DEBUG(L"page_nr=%lld, free=%lld, inactive=%lld, active=%lld, used=%lld",
			health_info.m_page_cache_size, health_info.m_page_cache_free, health_info.m_page_cache_inactive, health_info.m_page_cache_active, (health_info.m_page_cache_size - health_info.m_page_cache_free - health_info.m_page_cache_inactive - health_info.m_page_cache_active));
	}

	jcvos::auto_array<char> buf1(cache_size);
	jcvos::auto_array<char> buf2(cache_size);
	offset = 0;
	SetFilePointer(src_file, 0, nullptr, FILE_BEGIN);
	while (src_size > 0)
	{
		DWORD to_read = (DWORD)(min(cache_size, src_size));
		DWORD read = 0, written = 0;
		BOOL br = ReadFile(src_file, buf1, to_read, &read, nullptr);
		if (!br || read != to_read) THROW_ERROR(ERR_APP, L"failed on reading source file at %zd", offset);

		br = file->DokanReadFile(buf2, to_read, read, offset);
		if (!br || read != to_read) THROW_ERROR(ERR_APP, L"failed on writing destinate at %zd", offset);

		int ir = CTesterBase::CompareData(buf1, buf2, cache_size);
		if (ir >= 0) THROW_ERROR(ERR_APP, L"failed on compare data at 0x%zX + 0x%X", offset, ir);

		offset += to_read;
		src_size -= to_read;
	}



	file->CloseFile();
	LOG_DEBUG(L"page_nr=%lld, free=%lld, inactive=%lld, active=%lld, used=%lld",
		health_info.m_page_cache_size, health_info.m_page_cache_free, health_info.m_page_cache_inactive, health_info.m_page_cache_active, (health_info.m_page_cache_size - health_info.m_page_cache_free - health_info.m_page_cache_inactive - health_info.m_page_cache_active));

	CloseHandle(src_file);
	return 0;
}


int CGeneralTester::CompareFile(const std::wstring& src_fn, const std::wstring& dst_fn, size_t cache_size)
{
	HANDLE src_file = CreateFile(src_fn.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr);
	if (src_file == INVALID_HANDLE_VALUE) THROW_WIN32_ERROR(L"failed on open source file %s", src_fn.c_str());
	size_t src_size = GetFileSize(src_file, nullptr);

	jcvos::auto_interface<IFileInfo> file;
	m_fs->DokanCreateFile(file, dst_fn, GENERIC_READ, 0, IFileSystem::FS_OPEN_EXISTING, FILE_SHARE_READ | FILE_SHARE_WRITE, 0, false);
	if (file == nullptr) THROW_ERROR(ERR_APP, L"failed on create destinate file: %s", dst_fn.c_str());

	jcvos::auto_array<char> buf1(cache_size);
	jcvos::auto_array<char> buf2(cache_size);
	size_t offset = 0;
	while (src_size > 0)
	{
		DWORD to_read = (DWORD)(min(cache_size, src_size));
		DWORD read = 0, written = 0;
		BOOL br = ReadFile(src_file, buf1, to_read, &read, nullptr);
		if (!br || read != to_read) THROW_ERROR(ERR_APP, L"failed on reading source file at %zd", offset);

		br = file->DokanReadFile(buf2, to_read, read, offset);
		if (!br || read != to_read) THROW_ERROR(ERR_APP, L"failed on writing destinate at %zd", offset);

		int ir = CompareData(buf1, buf2, cache_size);
		if (ir >= 0) THROW_ERROR(ERR_APP, L"failed on compare data at 0x%zX + 0x%X", offset, ir);

		offset += to_read;
		src_size -= to_read;
	}
	file->CloseFile();
	CloseHandle(src_file);
	return 0;
}

int CGeneralTester::FileInfo(const std::wstring& fn)
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

//bool CFsTesterApp::GeneralTest(void)
int CGeneralTester::RunTest(void)
{
	m_capacity = m_disk->GetCapacity();

	bool br;
	//br = m_fs->MakeFileSystem(m_disk, m_capacity, L"test");
	//if (!br) THROW_ERROR(ERR_APP, L"failed on make file system");
 //	br = m_fs->Mount(m_disk);
	//if (!br) THROW_ERROR(ERR_APP, L"failed on mount");

	//NTSTATUS err;
	DokanHealthInfo health_info;
//	jcvos::auto_interface<IFileInfo> health_file;
	m_fs->DokanCreateFile(m_health_file, L"\\$HEALTH", GENERIC_READ, 0, IFileSystem::FS_OPEN_EXISTING, FILE_SHARE_READ, 0, false);
	DWORD read = 0;
	m_health_file->DokanReadFile(&health_info, sizeof(DokanHealthInfo), read, 0);

#if 1	// for Copy compare test
	CopyFileToDokan(L".\\Bluetooth.pdf", L"\\dc.pdf", 1048576);
	CompareFile(L".\\Bluetooth.pdf", L"\\dc.pdf", 1048576);

	//CopyCompareTest(L".\\Bluetooth.pdf", L"\\dc.pdf", 1048576);

#endif
	m_health_file->DokanReadFile(&health_info, sizeof(DokanHealthInfo), read, 0);
	m_health_file->CloseFile();
	RELEASE(m_health_file);

#if 0
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
#endif

//	MakeDir(L"\\folder\\subfolder");
//	MakeDir(L"\\ll2");

	//NewFile(L"\\ll2\\baz");
	//NewFile(L"\\folder\\bar");
	//DtMoveFile(L"\\ll2\\baz", L"\\folder\\baz");
	//ListAllItems(L"\\folder");

	//DtDeleteFile(L"\\folder\\bar");
	//DtDeleteFile(L"\\folder\\baz");
	//DtDeleteFile(L"\\folder");


#define TEST_FN	(L"\\test.txt")
//#define TEST_FN	(L"\\folder\\subfolder\\test.txt")

#define file_size 5000
	jcvos::auto_interface<IFileInfo> file;
	jcvos::auto_array<char> buf(file_size);
#if 0	// for read write test without unmount/mount
	// test for file operations
	err = m_fs->DokanCreateFile(file, TEST_FN, GENERIC_READ | GENERIC_WRITE, 0, IFileSystem::FS_CREATE_ALWAYS, 0, 0, false);
	if (!file) THROW_ERROR(ERR_APP, L"failed on creating file %s", TEST_FN);
	// write test
	file->SetEndOfFile(file_size);
	FillData(buf, file_size);
	DWORD written = 0;
	file->DokanWriteFile(buf, file_size, written, 0);
//	file->Cleanup();
	file->CloseFile();
	file.release();

	err = m_fs->DokanCreateFile(file, TEST_FN, GENERIC_READ, 0, IFileSystem::FS_OPEN_EXISTING, 0, 0, false);
	if (!file) THROW_ERROR(ERR_APP, L"failed on opening file %s", TEST_FN);

	jcvos::auto_array<char> buf2(file_size);
	DWORD read = 0;
	file->DokanReadFile(buf2, file_size, read, 0);
	int ir = CompareData(buf, buf2, file_size);
	if (ir >= 0)
	{
		THROW_ERROR(ERR_APP, L"failed on reading file, offset=%d", ir);
	}

	file->CloseFile();
	file.release();

#endif

#if 0
	ListAllItems(L"\\");

	m_fs->DokanMoveFile(L"\\folder\\subfolder\\test.txt", L"\\l2\\ttt.txt", false, nullptr);
	ListAllItems(L"\\");

	err = m_fs->DokanCreateFile(file, L"\\l2\\ttt.txt", GENERIC_READ, 0, IFileSystem::FS_OPEN_EXISTING, 0, 0, false);
	if (/*!br ||*/ !file) THROW_ERROR(ERR_APP, L"failed on opening file %s", TEST_FN);
	m_fs->DokanDeleteFile(L"\\l2\\ttt.txt", file, false);
	file->CloseFile();
	file.release();

//	m_fs->DokanDeleteFile(L"\\folder\\subfolder\\test.txt", nullptr, false);
	ListAllItems(L"\\");

	//CDokanTestFind callback;
	//err = m_fs->DokanCreateFile(file, L"\\", GENERIC_READ, 0, IFileSystem::FS_OPEN_EXISTING, 0, 0, true);
	//if (/*!br ||*/ !file) THROW_ERROR(ERR_APP, L"failed on open root dir");
	//file->EnumerateFiles(&callback);
	//file->Cleanup();
	//file->CloseFile();
	//file.release();
#endif

#if 1
	m_fs->Unmount();
	br = m_fs->Mount(m_disk);
	if (!br) THROW_ERROR(ERR_APP, L"failed on mount");

	ListAllItems(L"\\");
	CompareFile(L".\\Bluetooth.pdf", L"\\dc.pdf", 1048576);

/*
	err = m_fs->DokanCreateFile(file, TEST_FN, GENERIC_READ, 0, IFileSystem::FS_OPEN_EXISTING, 0, 0, false);
	if (!file) THROW_ERROR(ERR_APP, L"failed on opening file %s", L"\\folder\\test.txt");
	jcvos::auto_array<char> buf2(1024);
	DWORD read = 0;
	file->DokanReadFile(buf2, 1024, read, 0);
	int ir = CompareData(buf, buf2, 1024);
	if (ir >= 0)
	{
		THROW_ERROR(ERR_APP, L"failed on reading file, offset=%d", ir);
	}
	file->CloseFile();
*/

#endif

//	m_fs->Unmount();
//	m_fs->Disconnect();
	wprintf_s(L"test completed, no error detected");
	return 0;
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

//#define SHOW_PROGRESS  500
//bool CFsTesterApp::FullTest(const boost::property_tree::wptree& test_config)
//{
//	m_fs->MakeFileSystem(m_dev, m_capacity, L"dokan_test");
//	CFullTesterDokan tester(m_fs, m_dev);
//
//	// loading config
//	std::string str_config_fn;
//	jcvos::UnicodeToUtf8(str_config_fn, m_config_file);
//	tester.Config(test_config);
//	//if (!m_root.empty()) tester.SetTestRoot(m_root);
//	// runtest
//	int err = tester.StartTest();
//	return err;
//}

DWORD CFsTesterApp::AppendChecksum(DWORD cur_checksum, const char * buf, size_t size)
{
	const DWORD * bb = reinterpret_cast<const DWORD*>(buf);
	size_t ss = size / 4;
	for (size_t ii = 0; ii < ss; ++ii) cur_checksum += bb[ii];
	return cur_checksum;
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



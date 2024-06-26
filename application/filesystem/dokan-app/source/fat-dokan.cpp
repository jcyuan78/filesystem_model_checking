///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// fat-dokan.cpp: 定义控制台应用程序的入口点。
//

#include "stdafx.h"
#include "fat-dokan-app.h"
#include "../include/dokan_callback.h"
#include <dokanfs-lib.h>
#include <boost/property_tree/xml_parser.hpp>
#include <shlwapi.h>
#pragma comment (lib, "shlwapi.lib")

//#ifdef _DEBUG
//#include <vld.h>
//#endif

LOCAL_LOGGER_ENABLE(_T("fat.app"), LOGGER_LEVEL_DEBUGINFO);

const TCHAR CFatDokanApp::LOG_CONFIG_FN[] = L"dokanfs.cfg";
typedef jcvos::CJCApp<CFatDokanApp>	CApplication;
CApplication _app;

#define _class_name_	CApplication
BEGIN_ARGU_DEF_TABLE()
	ARGU_DEF(L"config",		'c', m_config_fn,	L"config file name.")	
	ARGU_DEF(L"device",		'd', m_device,		L"device which contain filesystem.")
	ARGU_DEF(L"lib",		'l', m_str_lib,		L"library name.")
	ARGU_DEF(L"filesystem",	's', m_str_fs,		L"file system name.")
	ARGU_DEF(L"format",		'f', m_volume_name,	L"format device using volume name.")
	ARGU_DEF(L"mount",		'm', m_mount,		L"mount point.")
	ARGU_DEF(L"unmount",	'u', m_unmount,		L"unmount device.", false)
END_ARGU_DEF_TABLE()

int _tmain(int argc, _TCHAR* argv[])
{
	return jcvos::local_main(argc, argv);
}

BOOL WINAPI HandlerRoutine(DWORD dwCtrlType);

CFatDokanApp::CFatDokanApp(void)
	: m_dev(NULL), m_fs(NULL), m_capacity(0)
{
}

CFatDokanApp::~CFatDokanApp(void)
{
}

int CFatDokanApp::Initialize(void)
{
	//	EnableSrcFileParam('i');
	EnableDstFileParam('o');
	return 0;
}

void CFatDokanApp::CleanUp(void)
{
	RELEASE(m_dev);
	RELEASE(m_fs);

#ifdef _DEBUG
	getc(stdin);
#endif
}

DWORD CFatDokanApp::StartDokanThread(LPVOID lp)
{
	LOG_STACK_TRACE();
	CFatDokanApp * app = reinterpret_cast<CFatDokanApp*>(lp);
	LOG_DEBUG(L"app=0x%p", app);
	int ir = StartDokan(app->m_fs, app->m_mount);
	return (DWORD)ir;
}

void CFatDokanApp::Demount(void)
{
	DokanRemoveMountPoint(m_mount.c_str());
}

typedef bool(*PLUGIN_GET_FACTORY)(IFsFactory * &);


int CFatDokanApp::Run(void)
{
	LOG_STACK_TRACE();
	DWORD ir = 0;
	if (m_unmount)
	{
		DokanRemoveMountPoint(m_mount.c_str());
		return 0;
	}

	// 解析config file的路径，将其设置为缺省路径
	wchar_t path[MAX_PATH];
	wchar_t filename[MAX_PATH];
	wchar_t cur_dir[MAX_PATH];
	GetCurrentDirectory(MAX_PATH - 1, cur_dir);
	GetFullPathName(m_config_fn.c_str(), MAX_PATH, path, NULL);

	wcscpy_s(filename, path);
	PathRemoveFileSpec(path);
	SetCurrentDirectory(path);
	PathStripPath(filename);

	// load configuration
	std::string str_fn;
	jcvos::UnicodeToUtf8(str_fn, filename);
	boost::property_tree::wptree pt;
	//boost::property_tree::json_parser::read_json(str_fn, pt);
	if (str_fn.rfind(".xml") != std::string::npos) { boost::property_tree::xml_parser::read_xml(str_fn, pt); }
	else if (str_fn.rfind(".json") != std::string::npos) { boost::property_tree::json_parser::read_json(str_fn, pt); }
	else THROW_ERROR(ERR_APP, L"Unknown config format: %s", filename);

	int err = CDokanFsBase::LoadFilesystemByConfig(m_fs, m_dev, L"", pt);
	if (err)
	{
		LOG_ERROR(L"[err] failed on creating file system or device, err=%d", err);
		return err;
	}

	if (!m_mount.empty())
	{	// Mount
		bool br = m_fs->Mount(m_dev);
#ifdef SYNC_DOKAN
		m_thread_dokan = CreateThread(NULL, 0, StartDokanThread, reinterpret_cast<LPVOID>(this), 0, NULL);

		while (1)
		{
			wchar_t ch = getc(stdin);
			LOG_DEBUG(L"input = %X", ch);
			if (ch == 'X') break;
		}

		LOG_DEBUG(L"demount ..");
		DokanRemoveMountPoint(m_mount.c_str());

		ir = WaitForSingleObject(m_thread_dokan, 20000);		// 20 second
		LOG_DEBUG(L"wait thread exit = %d", ir);
#else
		// 由于Dokan 2.0 支持非block的，不在需要显式的多线程支持。同时利用dokanctrl.exe工具来unmount
		StartDokanAsync(m_fs, m_mount);
#endif
		m_fs->Unmount();
		RELEASE(m_fs);

	}
	else if (!m_volume_name.empty())
	{
		wprintf_s(L"format volume: %s\n", m_volume_name.c_str());
		m_capacity = m_dev->GetCapacity();
		m_fs->MakeFileSystem(m_dev, m_capacity, m_volume_name);
	}
	return err;
}

BOOL WINAPI HandlerRoutine(DWORD dwCtrlType)
{
	switch (dwCtrlType)
	{
	case CTRL_C_EVENT:
	case CTRL_BREAK_EVENT:
	case CTRL_CLOSE_EVENT:
	case CTRL_LOGOFF_EVENT:
	case CTRL_SHUTDOWN_EVENT: {
		LOG_DEBUG(_T("Ctrl + C has been pressed."));
		SetConsoleCtrlHandler(HandlerRoutine, FALSE);
		CFatDokanApp * app = dynamic_cast<CFatDokanApp*>(CFatDokanApp::GetApp());
		JCASSERT(app);
		app->Demount();
		} break;
	default:
		LOG_DEBUG(_T("console event 0x%X"), dwCtrlType);
	}
	return TRUE;
}
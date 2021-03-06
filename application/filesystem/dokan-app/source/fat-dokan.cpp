// fat-dokan.cpp: 定义控制台应用程序的入口点。
//

#include "stdafx.h"
#include "fat-dokan-app.h"
#include "../include/dokan_callback.h"

//#pragma comment (lib, "mini-fat.lib")
//#pragma comment (lib, "fat-dokan.lib")

#include "image_device.h"


//#ifdef _DEBUG
//#include <vld.h>
//#endif

LOCAL_LOGGER_ENABLE(_T("fat.app"), LOGGER_LEVEL_DEBUGINFO);

const TCHAR CFatDokanApp::LOG_CONFIG_FN[] = L"dokanfs.cfg";
typedef jcvos::CJCApp<CFatDokanApp>	CApplication;
CApplication _app;

#define _class_name_	CApplication
BEGIN_ARGU_DEF_TABLE()
	ARGU_DEF(L"device",		'd', m_device,		L"device which contain filesystem.")
	ARGU_DEF(L"lib",		'l', m_str_lib,		L"library name.")
	ARGU_DEF(L"filesyste",	's', m_str_fs,		L"file system name.")
	ARGU_DEF(L"capacity",	'c', m_capacity,	L"capacity in sectors.", size_t(0))
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
	LOG_DEBUG(L"app=0x%08X", (DWORD)(app));
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

//	import DLL
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

//	br = CreateVirtualDevice(m_dev, m_device, m_capacity);
/*
	factory->CreateVirtualDevice(m_dev, m_device, m_capacity);
	if (!br || !m_dev) THROW_ERROR(ERR_APP, L"failed on open device %s with cap=%d", m_device.c_str(), m_capacity);
	if (m_capacity == 0) m_capacity = m_dev->GetCapacity();
*/

	br = factory->CreateFileSystem(m_fs, m_str_fs);
	if (!br || !m_fs) THROW_ERROR(ERR_APP, L"failed on creating file system");;

	br = factory->CreateVirtualDisk(m_dev, m_device, m_capacity);
	if (br && m_dev) m_fs->ConnectToDevice(m_dev);

	if (!m_volume_name.empty())
	{
		wprintf_s(L"format FAT32\n");
		m_fs->MakeFileSystem(m_capacity, m_volume_name);
		m_fs->Disconnect();
		return 0;
	}

	if (!m_mount.empty())
	{
		// open file
		m_fs->Mount();
		LOG_DEBUG(L"this=0x%08X", (DWORD)(this))
		m_thread_dokan = CreateThread(NULL, 0, StartDokanThread, reinterpret_cast<LPVOID>(this), 0, NULL);

		while (1)
		{
			wchar_t ch = getc(stdin);
			LOG_DEBUG(L"input = %X", ch);
			if (ch == 'X') break;
		}

		LOG_DEBUG(L"demount ..")
		DokanRemoveMountPoint(m_mount.c_str());

		ir = WaitForSingleObject(m_thread_dokan, 20000);		// 20 second
		LOG_DEBUG(L"wait thread exit = %d", ir);
		m_fs->Unmount();
		m_fs->Disconnect();
	}
	return ir;
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
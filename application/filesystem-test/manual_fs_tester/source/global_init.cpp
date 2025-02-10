#include "stdafx.h"
//#include "DiskLet.h"
#include "global_init.h"

LOCAL_LOGGER_ENABLE(L"init", LOGGER_LEVEL_NOTICE);

static bool g_init = false;

StaticInit global(L"f2fs.cfg");

StaticInit::StaticInit(const std::wstring & config) 
{
	LOG_STACK_TRACE();
	// for test
	wchar_t dllpath[MAX_PATH] = { 0 };
#ifdef _DEBUG
	GetCurrentDirectory(MAX_PATH, dllpath);
	wprintf_s(L"current path=%s\n", dllpath);
	memset(dllpath, 0, sizeof(wchar_t) * MAX_PATH);
#endif
	// 获取DLL路径
	MEMORY_BASIC_INFORMATION mbi;
	static int dummy;
	VirtualQuery(&dummy, &mbi, sizeof(mbi));
	HMODULE this_dll = reinterpret_cast<HMODULE>(mbi.AllocationBase);
	::GetModuleFileName(this_dll, dllpath, MAX_PATH);
	// Load log config
	std::wstring fullpath, fn;
	jcvos::ParseFileName(dllpath, m_module_path, fn);
#ifdef _DEBUG
	wprintf_s(L"module path=%s\n", m_module_path.c_str());
#endif
	LOGGER_CONFIG(config.c_str(), L".\\");
	LOG_DEBUG(_T("log config..."));

	//EnabledDebugPrivilege();
	//bool br = CreateStorageManager(m_manager);
	//if (!br || m_manager == nullptr) throw gcnew System::ApplicationException(L"failed on creating Storage Manager object");
	//m_manager->Initialize(true);
	g_init = true;
}

StaticInit::~StaticInit(void)
{
	g_init = false;
	LOG_STACK_TRACE();
	// 由于PowerShell的垃圾回收器异步工作，有可能在CleanUpSmiDevice之后执行
	// IPageInfo或者IBinaryBufer的拆构工作，造成异常。
	// 暂时禁止CleanUpSmiDevice()，此处需要仔细检查，以调整调用顺序。
	//smidev::CleanUpSmiDevice();
	//RELEASE(m_manager);
	//RELEASE(m_cur_dev);
	//RELEASE(m_cur_disk);
}

//void StaticInit::SelectDevice(IStorageDevice * dev)
//{
//	RELEASE(m_cur_dev);
//	m_cur_dev = dev;
//	if (m_cur_dev) m_cur_dev->AddRef();
//}
//
//void StaticInit::GetDevice(IStorageDevice *& dev)
//{
//	dev = m_cur_dev;
//	if (dev) dev->AddRef();
//}
//
////Clone::StorageDevice ^ StaticInit::GetDevice(void)
////{
////	if (m_cur_dev) return gcnew Clone::StorageDevice(m_cur_dev);
////	else return nullptr;
////}
//
//void StaticInit::SelectDisk(IDiskInfo * disk)
//{
//	RELEASE(m_cur_disk);
//	m_cur_disk = disk;
//	if (m_cur_disk) m_cur_disk->AddRef();
//}
//
//void StaticInit::GetDisk(IDiskInfo *& disk)
//{
//	disk = m_cur_disk;
//	if (disk) disk->AddRef();
//}
//

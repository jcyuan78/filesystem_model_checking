// dllmain.cpp : 定义 DLL 应用程序的入口点。
#include "pch.h"

#include <dokanfs-lib.h>
#include <include\journal_device.h>
#include <include/file_disk.h>
#include <boost/property_tree/ptree.hpp>

BOOL APIENTRY DllMain(HMODULE hModule, DWORD  ul_reason_for_call, LPVOID lpReserved)
{
    switch (ul_reason_for_call)
    {
    case DLL_PROCESS_ATTACH:
    case DLL_THREAD_ATTACH:
    case DLL_THREAD_DETACH:
    case DLL_PROCESS_DETACH:
        break;
    }
    return TRUE;
};


class CDeviceFactory : public IFsFactory
{
public:
    virtual bool CreateFileSystem(IFileSystem*& fs, const std::wstring& fs_name) { return false; }
    //virtual bool CreateVirtualDisk(IVirtualDisk * & dev, const std::wstring & fn, size_t secs);
    virtual bool CreateVirtualDisk(IVirtualDisk*& dev, const boost::property_tree::wptree& prop, bool create)
    {
        const std::wstring& dev_name = prop.get<std::wstring>(L"name", L"");
        if (dev_name == L"journal")
        {
            CJournalDevice* _dev = jcvos::CDynamicInstance<CJournalDevice>::Create();
            if (_dev == NULL) THROW_ERROR(ERR_MEM, L"failed on creating CJournalDevice");
            bool br = _dev->InitializeDevice(prop);
            if (!br)
            {
                _dev->Release();
                THROW_ERROR(ERR_APP, L"failed on config Journal Device");
            }
            dev = static_cast<IVirtualDisk*>(_dev);
        }
        else if (dev_name == L"file_disk")
        {
            CFileDisk* _dev = jcvos::CDynamicInstance<CFileDisk>::Create();
            if (_dev == nullptr) THROW_ERROR(ERR_MEM, L"failed on creating CFileDisk");
            bool br = _dev->InitializeDevice(prop);
            if (!br)
            {
                _dev->Release();
                THROW_ERROR(ERR_APP, L"failed on config File Disk");
            }
            dev = static_cast<IVirtualDisk*>(_dev);
        }
        else { return false; }
        return true;
    }

};

jcvos::CStaticInstance<CDeviceFactory> g_factory;

extern "C" __declspec(dllexport) bool GetFactory(IFsFactory * &fac)
{
    JCASSERT(fac == NULL);
    fac = static_cast<IFsFactory*>(&g_factory);
    return true;
}
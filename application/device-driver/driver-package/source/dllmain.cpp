///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// dllmain.cpp : 定义 DLL 应用程序的入口点。
#include "pch.h"
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#include <dokanfs-lib.h>
#include <include\journal_device.h>
#include <include/crash_disk.h>
#include <include/file_disk.h>
#include <boost/property_tree/ptree.hpp>
#include "image_device.h"

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
    virtual bool CreateVirtualDisk(IVirtualDisk*& dev, const std::wstring & name, 
        const boost::property_tree::wptree& prop, bool create)
    {
        std::wstring dev_name;
        if (name.empty()) dev_name = prop.get<std::wstring>(L"name", L"");
        else dev_name = name;

        if (dev_name == L"journal")
        {
            CJournalDevice* _dev = jcvos::CDynamicInstance<CJournalDevice>::Create();
            if (_dev == NULL) THROW_ERROR(ERR_MEM, L"failed on creating CJournalDevice");
            dev = static_cast<IVirtualDisk*>(_dev);
        }
        else if (dev_name == L"file_disk")
        {
            bool async_io = prop.get<bool>(L"async_io", false);
            if (async_io) {     dev = jcvos::CDynamicInstance<CFileDiskAsync>::Create(); }
            else            {   dev = jcvos::CDynamicInstance<CFileDiskSync>::Create();      }
            if (dev == nullptr) THROW_ERROR(ERR_MEM, L"failed on creating CFileDisk");
        }
        else if (dev_name == L"image_device")
        {
            CImageDevice* _dev = jcvos::CDynamicInstance<CImageDevice>::Create();
            if (_dev == nullptr) THROW_ERROR(ERR_MEM, L"failed on creating CFileDisk");
            JCASSERT(0);    //TODO
            dev = static_cast<IVirtualDisk*>(_dev);
            //_dev->CreateFileImage(fn, secs);
            //dev = static_cast<IVirtualDisk*>(_dev);

        }
        else if (dev_name == L"crash_disk")
        {
            CCrashDisk* _dev = jcvos::CDynamicInstance<CCrashDisk>::Create();
            if (_dev == nullptr) THROW_ERROR(ERR_MEM, L"failed on creating CCrashDisk");
            dev = static_cast<IVirtualDisk*>(_dev);
        }
        else { return false; }

        if (!prop.empty()) {
            bool br = dev->InitializeDevice(prop);
            if (!br)
            {
                RELEASE(dev);
                THROW_ERROR(ERR_APP, L"failed on config Journal Device");
            }
        }
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
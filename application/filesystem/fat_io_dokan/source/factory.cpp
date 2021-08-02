#include "stdafx.h"
extern "C" {
#include "../../fat_io_lib/fat_io_lib.h"
}
#include "fat_io_dokan.h"
#include "../../../device-driver/journal_device/include/journal_device.h"
LOCAL_LOGGER_ENABLE(L"fat.factory", LOGGER_LEVEL_DEBUGINFO);


///////////////////////////////////////////////////////////////////////////////
// -- Factory

jcvos::CStaticInstance<CFatIoFactory> g_factory;

extern "C" __declspec(dllexport) bool GetFactory(IFsFactory * & fac)
{
	JCASSERT(fac == NULL);
	fac = static_cast<IFsFactory*>(&g_factory);
	return true;
}

bool CFatIoFactory::CreateFileSystem(IFileSystem *& fs, const std::wstring & fs_name)
{
	JCASSERT(fs == NULL);
	fs = static_cast<IFileSystem*>(&g_fs);
	return true;
}

//bool CFatIoFactory::CreateVirtualDisk(IVirtualDisk *& dev, const std::wstring & fn, size_t secs)
//{
//	JCASSERT(dev == NULL);
//	CJournalDevice * _dev = jcvos::CDynamicInstance<CJournalDevice>::Create();
//	//std::wstring ifn = fn + L".img";
//	_dev->CreateFileImage(fn, secs, false);
//	//std::wstring jfn = fn + L"-journal";
////	_dev->LoadJournal(jfn, 0xFFFFFFFF);
////	_dev->LoadJournal(jfn, 0);
//	dev = static_cast<IVirtualDisk *>(_dev);
//	return true;
//}

bool CFatIoFactory::CreateVirtualDisk(IVirtualDisk *& dev, const boost::property_tree::wptree & prop, bool create)
{
	JCASSERT(dev == NULL);
	const std::wstring& dev_name = prop.get<std::wstring>(L"name", L"");
	if (dev_name == L"journal")
	{
		auto& dev_pt = prop.get_child(L"dev");
		size_t secs = dev_pt.get<size_t>(L"sectors");
		const std::wstring& fn = dev_pt.get<std::wstring>(L"image_file");
		bool journal = dev_pt.get<bool>(L"enable_journal");
		CJournalDevice * _dev = jcvos::CDynamicInstance<CJournalDevice>::Create();
		size_t log_cap = dev_pt.get<size_t>(L"log_capacity");
		size_t log_buf = log_cap * 16;
		_dev->CreateFileImage(fn, secs, log_cap, log_buf);
		dev = static_cast<IVirtualDisk *>(_dev);
		return true;
	}
	return false;
}


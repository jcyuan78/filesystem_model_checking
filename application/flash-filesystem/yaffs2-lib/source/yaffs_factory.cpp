#include "stdafx.h"
#include <boost/property_tree/ptree.hpp>
#include "yaffs_factory.h"
#include <nand_driver.h>
#include "..\include\yaf_fs.h"
//#include "..\include\yaffs_driver.h"

jcvos::CStaticInstance<CYaffsFactory> g_factory;

extern "C" __declspec(dllexport) bool GetFactory(IFsFactory * & fac)
{
	JCASSERT(fac == NULL);
	fac = static_cast<IFsFactory*>(&g_factory);
	return true;
}

bool CYaffsFactory::CreateFileSystem(IFileSystem *& fs, const std::wstring & fs_name)
{
	JCASSERT(fs == NULL);
	if (fs_name == L"yaffs2")
	{
		CYafFs *yaffs = jcvos::CDynamicInstance<CYafFs>::Create();
		fs = static_cast<IFileSystem*>(yaffs);
		return true;
	}
	return false;
}

bool CYaffsFactory::CreateVirtualDisk(IVirtualDisk *& dev, const std::wstring & fn, size_t secs)
{
	JCASSERT(dev == NULL);
	return false;
}

bool CYaffsFactory::CreateVirtualDisk(IVirtualDisk *& dev, const boost::property_tree::wptree & prop, bool create)
{
	JCASSERT(dev == NULL);
	const std::wstring & dev_name = prop.get<std::wstring>(L"name", L"");
	if (dev_name == L"nand")
	{
		auto nand_pt = prop.get_child_optional(L"nand");
		if (!nand_pt) return false;
		jcvos::auto_interface<CNandDriverFile> _dev(jcvos::CDynamicInstance<CNandDriverFile>::Create());
		if (!_dev) return false;
		//const boost::property_tree::wptree & nand_pt = 
		bool br = _dev->CreateDevice((*nand_pt), create);
		if (!br) return false;
		_dev.detach<IVirtualDisk>(dev);
		return true;
	}
	return false;

}

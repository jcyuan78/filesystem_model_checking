#pragma once

#include <dokanfs-lib.h>


class CYaffsFactory : public IFsFactory
{
public:
	virtual bool CreateFileSystem(IFileSystem * & fs, const std::wstring & fs_name);
	virtual bool CreateVirtualDisk(IVirtualDisk * & dev, const std::wstring & fn, size_t secs);
	virtual bool CreateVirtualDisk(IVirtualDisk * & dev, const boost::property_tree::wptree & prop, bool create);
};


#pragma once
#include <ifilesystem.h>


class CCrashMonkeyFactory : public IFsFactory
{
public:
	virtual bool CreateFileSystem(IFileSystem*& fs, const std::wstring& fs_name);
	virtual bool CreateVirtualDisk(IVirtualDisk*& dev, const boost::property_tree::wptree& prop, bool create);

};
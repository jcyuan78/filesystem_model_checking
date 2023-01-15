///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once
#include <lib-fstester.h>

class CGeneralTester : public CTesterBase
{
public:
	CGeneralTester(IFileSystem* fs, IVirtualDisk* disk) :CTesterBase(fs, disk) {}
public:
	virtual void Config(const boost::property_tree::wptree& pt, const std::wstring& root)
	{
		CTesterBase::Config(pt, root);
	}
	virtual int PrepareTest(void) { return 0; };
	virtual int RunTest(void);
	virtual int FinishTest(void) { return 0; };
	virtual void ShowTestFailure(FILE* log) {}

protected:
	int  CopyFileToDokan(const std::wstring& src_fn, const std::wstring& dst_fn, size_t cache_size);
	int  DtMoveFile(const std::wstring& src_dir, const std::wstring& src_fn, const std::wstring& dst, bool replace);
	int  CompareFile(const std::wstring& src_fn, const std::wstring& dst_fn, size_t cache_size);
	int  CopyFileFromDokan(const std::wstring& src_fn, const std::wstring& dst_fn, size_t cache_size);
	int  CopyCompareTest(const std::wstring& src_fn, const std::wstring& dst_fn, size_t cache_size);
	int  FileInfo(const std::wstring& src_dir);
	bool ListAllItems(const std::wstring& root);

protected:
	size_t m_capacity;		// in sectors
	IFileInfo* m_health_file = nullptr;

};
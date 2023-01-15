///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "tester_base.h"
#include <vector>




class CPerformanceTester : public CTesterBase
{
public:
	enum TEST_CASE {
		SEQU_READ = 1, SEQU_WRITE=2, RAND_READ=4, RAND_WRITE=8,
	};
	class TEST_CASE_INFO
	{
	public:
		TEST_CASE case_id;
		size_t block_size;
		size_t queue_depth;
		size_t test_cycle;
	};

public:
	CPerformanceTester(IFileSystem *fs, IVirtualDisk * disk) : CTesterBase(fs, disk) {}
	virtual ~CPerformanceTester(void) {}

protected:
	virtual void Config(const boost::property_tree::wptree& pt, const std::wstring& root);

	virtual int PrepareTest(void);
	virtual int RunTest(void);
	virtual int FinishTest(void) { return 0; }
	virtual void ShowTestFailure(FILE* log) {}

protected:
	void RandomReadTest(TEST_CASE_INFO & info);
	void RandomReadSingle(TEST_CASE_INFO& info);
	void RandomWriteTest(TEST_CASE_INFO& info);
	void RandomWriteSingle(TEST_CASE_INFO& info);
	void SequentialReadTest(TEST_CASE_INFO & info){}
	void SequentialWriteTest(TEST_CASE_INFO & info){}

protected:
	//UINT m_case;
	size_t m_file_size;
	//size_t m_block_size;
	//size_t m_queue_depth;

	std::vector<TEST_CASE_INFO> m_test_cases;

	std::wstring m_file_name;
};
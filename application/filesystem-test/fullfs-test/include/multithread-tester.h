///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
#pragma once
//#include "full_tester.h"
#include <lib-fstester.h>
#include <list>

class CMultiThreadTest;

class OperateRequest
{
public:
	CMultiThreadTest* m_tester;
	CTestState* m_state;
	TRACE_ENTRY* m_op;
};



class CMultiThreadTest :public CFullTester
{
public:
	CMultiThreadTest(IFileSystem* fs, IVirtualDisk* disk) : CFullTester(fs, disk) {}
public: 
	virtual int PrepareTest(void);
	virtual int RunTest(void);
	virtual int FinishTest(void);


protected:

	DWORD Worker(void);

	static DWORD WINAPI _Worker(PVOID p)
	{
		CMultiThreadTest* tester = reinterpret_cast<CMultiThreadTest*>(p);
		return tester->Worker();
	}
protected:
	DWORD m_thread_nr;
	HANDLE* m_threads;
	DWORD* m_thread_ids;

	//std::list<OperateRequest*> *m_requests;

	OperateRequest * m_requests;
	PTP_WORK* m_works;
	TP_CALLBACK_ENVIRON m_callback_evn;
	PTP_POOL m_pool;
	static void CALLBACK AsyncFsOperate(PTP_CALLBACK_INSTANCE instance, PVOID context, PTP_WORK work);

};
	

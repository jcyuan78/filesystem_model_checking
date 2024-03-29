﻿///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// performance_simulation.cpp : 此文件包含 "main" 函数。程序执行将在此处开始并结束。
//

#include "pch.h"
#include <jcapp.h>
#include "segment_manager.h"
#include "ssd_simulator.h"
#include "trace_tester.h"
#include "f2fs_simulator.h"
#include <boost/property_tree/xml_parser.hpp>

#ifdef _DEBUG
#include <vld.h>
#endif

void UnitTest(void);



class CSimulatorApp
	: public jcvos::CJCAppSupport<jcvos::AppArguSupport>
{
protected:
	typedef jcvos::CJCAppSupport<jcvos::AppArguSupport> BaseAppClass;

public:
	static const TCHAR LOG_CONFIG_FN[];
	CSimulatorApp(void);
	virtual ~CSimulatorApp(void);

public:
	virtual int Initialize(void);
	virtual int Run(void);
	virtual void CleanUp(void);
	virtual LPCTSTR AppDescription(void) const {
		return L"File System Tester, by Jingcheng Yuan\n";
	};

protected:
	void MakeTestId(void);
	void GenerateLogFileName(void);
	void SsdTest(const boost::property_tree::wptree & config);
	void FsTest(const boost::property_tree::wptree& config, CLfsInterface * lfs);

	// app 参数
public:
	std::wstring m_config_file;
	std::wstring m_test_id;
	int m_multihead_cnt=0;

protected:
	std::wstring m_fn_log, m_fn_lblka, m_fn_lblkb, m_fn_seg;
};


LOCAL_LOGGER_ENABLE(L"simulation.app", LOGGER_LEVEL_DEBUGINFO);

const TCHAR CSimulatorApp::LOG_CONFIG_FN[] = L"simulate.cfg";
typedef jcvos::CJCApp<CSimulatorApp>	CApplication;
CApplication _app;

#define _class_name_	CApplication

BEGIN_ARGU_DEF_TABLE()
ARGU_DEF(L"config",		'c', m_config_file, L"configuration file name")
ARGU_DEF(L"test_id",	't', m_test_id, L"test id for generating log and result")
ARGU_DEF(L"multihead",	'm', m_multihead_cnt, L"number of head count for mulithead log")
//ARGU_DEF(L"target", 't', m_root, L"target folder to test, like D:, D:\\test")
END_ARGU_DEF_TABLE()

int _tmain(int argc, wchar_t* argv[])
{
	return jcvos::local_main(argc, argv);
}

CSimulatorApp::CSimulatorApp(void)
{
}

CSimulatorApp::~CSimulatorApp(void)
{
}

int CSimulatorApp::Initialize(void)
{
	//	EnableSrcFileParam('i');
	EnableDstFileParam('o');
	return 0;
}

void CSimulatorApp::CleanUp(void)
{
}
void HeapTest(void);


int CSimulatorApp::Run(void)
{
	UnitTest();
	return 0;

	LOG_STACK_TRACE();

	if (m_test_id.empty()) MakeTestId();
	GenerateLogFileName();
	// load config file
	std::string config_fn;
	jcvos::UnicodeToUtf8(config_fn, m_config_file);
	boost::property_tree::wptree prop;
	boost::property_tree::xml_parser::read_xml(config_fn, prop);

	const boost::property_tree::wptree& test_config = prop.get_child(L"config.test");

	boost::property_tree::wptree& fs_config = prop.get_child(L"config.filesystem");

	const std::wstring& test_type = test_config.get<std::wstring>(L"type");
	if (test_type == L"ssd_test")
	{
		const boost::property_tree::wptree& device_config = prop.get_child(L"config.device");
		SsdTest(device_config);
	}
	else if (test_type == L"lfs_test")
	{
		jcvos::auto_ptr<CLfsInterface> lfs(new CSingleLogSimulator);
		lfs->SetLogFolder(m_test_id);
		lfs->Initialzie(fs_config);
		FsTest(test_config, lfs);
	}
	else if (test_type == L"f2fs_test")
	{
		if (m_multihead_cnt != 0)
		{
			fs_config.put(L"multi_header_num", m_multihead_cnt);
		}
		jcvos::auto_ptr<CLfsInterface> lfs(new CF2fsSimulator);
		lfs->SetLogFolder(m_test_id);
		lfs->Initialzie(fs_config);
		FsTest(test_config, lfs);
	}
	// 保存测试的配置结果
	std::string log_config_fn;
	jcvos::UnicodeToUtf8(log_config_fn, m_test_id + L"\\config.xml");
	boost::property_tree::xml_parser::write_xml(log_config_fn, prop);
	return 0;
}

void CSimulatorApp::FsTest(const boost::property_tree::wptree& config, CLfsInterface * lfs)
{
	CTraceTester* test = new CTraceTester(lfs);
	test->SetLogFolder(m_test_id);
	test->Config(config, L"\\");
	test->StartTest();
	delete test;
}

void CSimulatorApp::SsdTest(const boost::property_tree::wptree& device_config)
{
	CSsdSimulatorInterface* seg_manager = nullptr;

	std::wstring sm_type = device_config.get<std::wstring>(L"segment_manager");
	if (sm_type == L"singlehead") { seg_manager = new CSingleHeadSM(); }
	else if (sm_type == L"multihead") { seg_manager = new CMultiHeadSM; }

	seg_manager->Initialize(device_config);
	size_t blk_nr = seg_manager->GetLBlockNr();

	//	size_t blk_nr = 25600;
	srand(100);
	size_t phy_blk_nr = (size_t)(25600 * 1.3) / BLOCK_PER_SEG;
	//CSingleHeadSM* seg_manager = new CSingleHeadSM((size_t)(blk_nr*1.3) / BLOCK_PER_SEG, blk_nr * 8);
	// log file
	seg_manager->SetLogFile(m_fn_log);

	// fill data
	wprintf_s(L"fill sequential data\n");
	size_t start_of_cold = blk_nr / 2;
	for (size_t bb = 0; bb < blk_nr / 2; )
	{
		seg_manager->WriteSector(bb * 8, 256 * 8, BT_COLD_DATA);
		seg_manager->WriteSector((bb + start_of_cold) * 8, 256 * 8, BT_COLD_DATA);
		bb += 256;
	}
	seg_manager->CheckingColdDataBySeg(m_test_id + L"\\seg_a.csv");
	//	swprintf_s(str_fn, MAX_PATH, L"%s-lblk_a.csv", m_test_id.c_str());
	seg_manager->DumpL2PMap(m_fn_lblka);
	// random write
	wprintf_s(L"random write\n");
	// hot blk: < 1/2 max blk
	for (size_t ii = 0; ii < blk_nr * 100; ++ii)
	{
		DWORD blk = (rand() << 7) + rand();
		blk %= (start_of_cold);
		DWORD blks = rand() % 10;
		if (blk + blks > start_of_cold)
		{
			blks = (DWORD)start_of_cold - blk;
		}
		//		seg_manager->WriteSector(blk * 8, blks * 8, BT_HOT__DATA);
		seg_manager->WriteSector(blk * 8, 8, BT_HOT__DATA);
	}

	//	swprintf_s(str_fn, MAX_PATH, L"%s-lblk_b.csv", m_test_id.c_str());
	seg_manager->DumpL2PMap(m_fn_lblkb);

	//	swprintf_s(str_fn, MAX_PATH, L"%s-seg_a.csv", m_test_id.c_str());
	seg_manager->CheckingColdDataBySeg(m_test_id + L"\\seg_b.csv");
	delete seg_manager;

}

void CSimulatorApp::MakeTestId(void)
{
	// 随机生成 test id
	time_t now;
	time(&now);
	tm ptm;
	localtime_s(&ptm, &now);
	m_test_id.resize(50);
	wchar_t* str_tid = const_cast<wchar_t*>(m_test_id.data());
	int len = swprintf_s(str_tid, 50, L"T%02d%02d%02d%02d%02d", ptm.tm_mon, ptm.tm_mday, ptm.tm_hour, ptm.tm_min, ptm.tm_sec);
	m_test_id.resize(len);
}

void CSimulatorApp::GenerateLogFileName(void)
{
	CreateDirectory(m_test_id.c_str(), nullptr);
	jcvos::auto_array<wchar_t> str_fn(MAX_PATH+1);
	swprintf_s(str_fn, MAX_PATH, L"%s\\log.csv", m_test_id.c_str());
	m_fn_log = str_fn;
	swprintf_s(str_fn, MAX_PATH, L"%s\\lblk_a.csv", m_test_id.c_str());
	m_fn_lblka = str_fn;
	swprintf_s(str_fn, MAX_PATH, L"%s\\lblk_b.csv", m_test_id.c_str());
	m_fn_lblkb = str_fn;
	swprintf_s(str_fn, MAX_PATH, L"%s\\seg_a.csv", m_test_id.c_str());
	m_fn_seg = str_fn;
}


///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// == unit test

//void ShowHeapLarge(GcPool<16> * pool)
//{
//	for (int ii = 0; ii < pool->large_len; ++ii)
//	{
//		SEG_INFO& seg = pool->segs[pool->large_heap[ii]];
//		wprintf_s(L"%02d ", seg.valid_blk_nr);
//	}
//	wprintf_s(L"\n");
//}
//
//void ShowHeapSmall(GcPool<16>* pool)
//{
//	for (int ii = 0; ii < pool->small_len; ++ii)
//	{
//		SEG_INFO& seg = pool->segs[pool->small_heap[ii]];
//		wprintf_s(L"%02d ", seg.valid_blk_nr);
//	}
//	wprintf_s(L"\n");
//}

/*
void HeapTest(void)
{
	// 随机生成一组seg
	srand(300);
	int size = 20;
	SEG_INFO* segs = new SEG_INFO[size];

	wprintf_s(L"source: \t");
	for (int ii = 0; ii < size; ++ii)
	{
		segs[ii].valid_blk_nr = rand() % 100;
		wprintf_s(L"%02d ", segs[ii].valid_blk_nr);
	}
	wprintf_s(L"\n");

	GcPool<16>* pool = new GcPool<16>(segs);

	for (SEG_T ii = 0; ii < size; ++ii)
	{
		pool->Push(ii);
		wprintf_s(L"large(%d):\t", ii);
		pool->ShowHeap(1);
	}
	wprintf_s(L"\n");

	pool->LargeToSmall();
	wprintf_s(L"small heap:\t"); pool->ShowHeap(0);
	wprintf_s(L"\n");

	wprintf_s(L"smallest segs:\t");
	for (int ii = 0; ii < 15; ++ii)
	{
		SEG_T ss = pool->Pop();
		if (ss == INVALID_BLK) break;
		wprintf_s(L"%02d ", segs[ss].valid_blk_nr);
		//wprintf_s(L"\n small:\t");
		//pool->ShowHeap(pool->small_heap, pool->small_len);
	}
	wprintf_s(L"\n");


	delete pool;
	delete[] segs;
}
*/

#define SEG_NUM 4096
#define POOL_SIZE 64

//#define VERIFY_SORTING

struct TESTSEG {
	DWORD valid_blk_nr;
};

struct SORT_PERFORMANCE
{
	INT64 cycles;
	double duration;
};


template<typename POOL>
void CheckSortingResult(TESTSEG* src, POOL& pool)
{
	// 检查排序结果
	TESTSEG* seg0 = pool.Pop();
	DWORD pre_val = seg0->valid_blk_nr, max_val = 0;
	if (pre_val > 512) THROW_ERROR(ERR_APP, L"wrong valid blk value, pool[0] (%d)", pre_val);
	seg0->valid_blk_nr = 0xFFFF;

	// 检查pool内大小
	wprintf_s(L"target: %d, ", pre_val);
	size_t ii = 1;
	for (ii = 1; ii < POOL_SIZE; ++ii)
	{
		TESTSEG* seg1 = pool.Pop();
		if (seg1 == nullptr) continue;
		max_val = seg1->valid_blk_nr;
		wprintf_s(L"%d, ", max_val);
		if (max_val > 512) THROW_ERROR(ERR_APP, L"wrong valid blk value, pool[%lld] (%d)", ii, max_val);
		if (max_val < pre_val)
			THROW_ERROR(ERR_APP, L"wrong order: pool[%lld] (%d) > pool[%lld] (%d)", ii - 1, pre_val, ii, max_val);
		seg1->valid_blk_nr = 0xFFFF;
		pre_val = max_val;
	}
	wprintf_s(L"\n");

	// 检查pool外大小
//	size_t data_size = pool.get_seg_nr();
//	for (; ii < data_size; ++ii)
	for (size_t ii=0; ii<SEG_NUM; ++ii)
	{
//		TESTSEG* seg1 = pool.Pop();
		TESTSEG& seg1 = src[ii];
		DWORD cur_val = seg1.valid_blk_nr;
		if (cur_val == 0xFFFF) continue;

		if (cur_val > 512) THROW_ERROR(ERR_APP, L"wrong valid blk value, src[%lld] (%d)", ii, cur_val);
		if (cur_val < max_val) THROW_ERROR(ERR_APP, L"wrong order out side, src[%ll] (%d), < max (%d)", ii, cur_val, max_val);
//		seg1->valid_blk_nr = (0xFFFF);
	}
	// 检查标记
	//for (ii = 0; ii < SEG_NUM; ++ii)
	//{
	//	if (src[ii].valid_blk_nr != 0xFFFF && src[ii].valid_blk_nr!=0) 
	//		THROW_ERROR(ERR_APP, L"unmarked value, src[%lld] (%d)", ii, src[ii].valid_blk_nr);
	//}
}

void SortingTestQ(GcPoolQuick<POOL_SIZE, TESTSEG> & pool, TESTSEG* src, SORT_PERFORMANCE &performance)
{
	{
		LOG_STACK_TRACE();
		pool.init();

		for (size_t ii = 0; ii < SEG_NUM; ++ii)
		{
			if (src[ii].valid_blk_nr > 0) pool.Push(src + ii);
		}

		pool.Sort();
		//for (size_t ii = 0; ii < POOL_SIZE; ++ii)
		//{
		//	pool.pop();
		//}
		performance.duration = RUNNING_TIME;
		performance.cycles = pool.sort_count;
	}
	CheckSortingResult(src, pool);
}

void SortingTestH(TESTSEG * src, SORT_PERFORMANCE& performance)
{
	LOG_STACK_TRACE();

	//GcPool<POOL_SIZE, TESTSEG> pool(segs);
	GcPoolHeap<POOL_SIZE, TESTSEG> pool(nullptr);
	for (SEG_T ss = 0; ss < SEG_NUM; ss++)	{	pool.Push(src+ss);	}
//	pool.ShowHeap(1);
	pool.Sort();
#ifdef VERIFY_SORTING
	CheckSortingResult(src, pool);
#else
	for (size_t ii = 0; ii < POOL_SIZE; ++ii)	{	pool.Pop();	}
	performance.duration = RUNNING_TIME;
	performance.cycles = 0;
#endif

}

void UnitTest(void)
{
	SORT_PERFORMANCE total_per;
	total_per.cycles = 0;
	total_per.duration = 0;
	GcPoolQuick<POOL_SIZE, TESTSEG> pool(SEG_NUM);
	TESTSEG segs[SEG_NUM];


	int test_cycle = 10000;
	for (int tt = 0; tt < test_cycle; ++tt)
	{
		srand(100+tt);
		wprintf_s(L"test cycle: %d \n", tt);
		// create source
//		wprintf_s(L"source: ");

		for (size_t ii = 0; ii < SEG_NUM; ++ii)
		{
			DWORD vv = rand() & 511;
			segs[ii].valid_blk_nr = vv;
			//		wprintf_s(L"%d, ", vv);
		}
//		wprintf_s(L"\n");

		SORT_PERFORMANCE per;
//		SortingTestQ(pool, segs, per);
		SortingTestH(segs, per);
		total_per.cycles += per.cycles;
		total_per.duration += per.duration;
	}
	wprintf_s(L"test cycles=%d, avg duration=%f(us), avg sort cnt=%lld\n",
		test_cycle, total_per.duration / test_cycle, total_per.cycles / test_cycle);
}
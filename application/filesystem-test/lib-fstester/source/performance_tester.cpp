///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
#include "pch.h"
#include "../include/performance_tester.h"

LOCAL_LOGGER_ENABLE(L"fstester.performance", LOGGER_LEVEL_DEBUGINFO);



void CPerformanceTester::Config(const boost::property_tree::wptree& pt, const std::wstring& root)
{
	CTesterBase::Config(pt, root);
	m_file_size = pt.get<size_t>(L"file_size", 10 * 1024 * 1024);
	const boost::property_tree::wptree& test_pt = pt.get_child(L"test_cases");
	for (auto ii = test_pt.begin(); ii != test_pt.end(); ++ii)
	{
		const boost::property_tree::wptree& case_pt = ii->second;
		m_test_cases.emplace_back();
		TEST_CASE_INFO& info = m_test_cases.back();

		const std::wstring& test_name = case_pt.get<std::wstring>(L"name", L"");
		if (0) {}
		else if (test_name == L"RandomRead")		info.case_id = RAND_READ;
		else if (test_name == L"RandomWrite")		info.case_id = RAND_WRITE;
		else if (test_name == L"SequentialRead")	info.case_id = SEQU_READ;
		else if (test_name == L"SequentialWrite")	info.case_id = SEQU_WRITE;

		info.test_cycle = case_pt.get<size_t>(L"test_cycle", 1);
		info.block_size		= case_pt.get<size_t>(L"block_size", 4096);
		info.queue_depth	= case_pt.get<size_t>(L"queue_depth", 1);
	}
	m_file_name = root + L"\\" + L"performance_test.dat";
}

int CPerformanceTester::PrepareTest(void)
{
	// 准备文件
	jcvos::auto_interface<IFileInfo> file;
	m_fs->DokanCreateFile(file, m_file_name, GENERIC_ALL, 0, IFileSystem::FS_CREATE_ALWAYS, FILE_SHARE_READ | FILE_SHARE_WRITE, 0, false);
	if (file == nullptr) THROW_ERROR(ERR_APP, L"failed on creating file: %s", m_file_name.c_str());
	FillFile(file, 10, 1, 0, m_file_size);
	file->CloseFile();
	FileVerify(m_file_name, 10, 1, m_file_size);
	return 0;
}

int CPerformanceTester::RunTest(void)
{
	for (auto it = m_test_cases.begin(); it != m_test_cases.end(); ++it)
	{
		switch (it->case_id)
		{
		case SEQU_READ:	 SequentialReadTest(*it); break;
		case SEQU_WRITE: SequentialWriteTest(*it); break;
		case RAND_READ:  RandomReadTest(*it); break;
		case RAND_WRITE: RandomWriteTest(*it); break;
		}
	}
	return 0;
}

void CPerformanceTester::RandomReadTest(TEST_CASE_INFO & info)
{
	size_t queue_depth = info.queue_depth;
	if (queue_depth == 1)
	{
		RandomReadSingle(info);
		return;
	}
	size_t block_size = info.block_size;
	size_t blk_num = m_file_size / block_size;
	INT64 test_cycle = blk_num * info.test_cycle;

	wprintf_s(L"Random Read: block=%lld(KB), qd=%lld, cycle=%lld\n", block_size / 1024, queue_depth, test_cycle);

	HANDLE file = CreateFile(m_file_name.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, FILE_FLAG_OVERLAPPED | FILE_FLAG_RANDOM_ACCESS | FILE_FLAG_NO_BUFFERING, nullptr);
	if (file == INVALID_HANDLE_VALUE) THROW_WIN32_ERROR(L"failed on opening file: %s", m_file_name.c_str());

	jcvos::auto_array<OVERLAPPED> _ops(queue_depth, 0);
	jcvos::auto_array<HANDLE> _events(queue_depth);
	jcvos::auto_array<BYTE> _bufs(queue_depth * block_size);
	jcvos::auto_array<UINT64> _start_ts(queue_depth, 0);

	for (size_t ii = 0; ii < queue_depth; ++ii)
	{	// 初始化
		_events[ii] = CreateEvent(nullptr, FALSE, TRUE, nullptr);
		if (_events[ii] == nullptr) THROW_WIN32_ERROR(L"failed on creating event, index=%d", ii);
	}

//	INT64 test_cycle = blk_num;
	size_t running = queue_depth;
	double cycle = jcvos::GetTsCycle();

	UINT64 begin = jcvos::GetTimeStamp();
	UINT64 acc_time = 0;		// 累计IO时间
	size_t acc_count = 0;		// 累计IO及时次数，
	while (test_cycle > 0 || running > 0)
	{
		DWORD ir = WaitForMultipleObjects((DWORD)queue_depth, _events, FALSE, INFINITE);
		if (ir > (WAIT_OBJECT_0 + queue_depth)) THROW_WIN32_ERROR(L"failed on waiting io event, ir=%d", ir);
		UINT64 end_ts = jcvos::GetTimeStamp();
		running--;
		DWORD index = ir - WAIT_OBJECT_0;
		if (_start_ts[index] > 0 && end_ts > _start_ts[index])
		{
			end_ts -= _start_ts[index];
			acc_time += end_ts;
			acc_count++;
		}
		LOG_DEBUG(L"read complete, index=%d, time=%.f(us), cycle=%lld, running=%zd", index, end_ts * cycle, test_cycle, running);
		//开始读取
		if (test_cycle <= 0) continue;
		size_t blk_nr = blk_num* rand() / (RAND_MAX);

		DWORD read = 0;
		OVERLAPPED& op = _ops[index];
		op.hEvent = _events[index];
		op.Pointer = (PVOID)(blk_nr * block_size);
		_start_ts[index] = jcvos::GetTimeStamp();
		LOG_DEBUG(L"start read, index=%d", index);
		ReadFile(file, _bufs + queue_depth * index, (DWORD)block_size, &read, &op);
		test_cycle--;
		running++;
	}

	UINT64 duration = jcvos::GetTimeStamp() - begin;

	for (size_t ii = 0; ii < queue_depth; ++ii)
	{
		CloseHandle(_events[ii]);
	}

	CloseHandle(file);
	wprintf_s(L"read time=%.f(us), avg bw=%.f(MB/s)\n", duration * cycle, blk_num * block_size / (duration * cycle));
	wprintf_s(L"read io time=%.f(us), avg bw=%.f(MB/s)\n\n", acc_time * cycle, acc_count * block_size / (acc_time * cycle));
}

void CPerformanceTester::RandomReadSingle(TEST_CASE_INFO& info)
{
	size_t block_size = info.block_size;
	size_t blk_num = m_file_size / block_size;
	INT64 test_cycle = blk_num * info.test_cycle;

	wprintf_s(L"Random Read: block=%lld(KB), qd=%d, cycle=%lld\n", block_size / 1024, 1, test_cycle);

	jcvos::auto_interface<IFileInfo> file;
	m_fs->DokanCreateFile(file, m_file_name, GENERIC_READ | GENERIC_WRITE, 0, IFileSystem::FS_OPEN_EXISTING, 0, 0, false);

	if (file == nullptr) THROW_ERROR(ERR_APP, L"failed on opening file: %s", m_file_name.c_str());

	//	INT64 test_cycle = blk_num;
	double cycle = jcvos::GetTsCycle();

	UINT64 begin = jcvos::GetTimeStamp();
	UINT64 acc_time = 0;		// 累计IO时间
	size_t acc_count = 0;		// 累计IO及时次数，
	jcvos::auto_array<BYTE> buf(block_size);
	BYTE* _buf = buf;
	while (test_cycle > 0)
	{
		size_t blk_nr = blk_num * rand() / (RAND_MAX);

		DWORD read = 0;
		size_t offset = (blk_nr * block_size);
		file->DokanReadFile(_buf, (DWORD)block_size, read, offset);
		test_cycle--;
	}

	UINT64 duration = jcvos::GetTimeStamp() - begin;

	//CloseHandle(file);
	file->CloseFile();
	wprintf_s(L"read time=%.f(us), avg bw=%.f(MB/s)\n", duration * cycle, blk_num * block_size / (duration * cycle));
	//wprintf_s(L"read io time=%.f(us), avg bw=%.f(MB/s)\n\n", acc_time * cycle, acc_count * block_size / (acc_time * cycle));

}

void CPerformanceTester::RandomWriteSingle(TEST_CASE_INFO& info)
{
	size_t block_size = info.block_size;
	JCASSERT(m_file_size % block_size == 0);
	size_t blk_num = m_file_size / block_size;		//必须要求block对齐
	// test_cycle = info.test_cycle;
	
	jcvos::auto_array<DWORD> blk_list(blk_num);
	jcvos::auto_array<BYTE> blk_check(blk_num, 0);

	wprintf_s(L"Random Write: block=%lld(KB), qd=%d, cycle=%lld\n", block_size / 1024, 1, info.test_cycle);

	jcvos::auto_interface<IFileInfo> file;
	m_fs->DokanCreateFile(file, m_file_name, GENERIC_READ | GENERIC_WRITE, 0, IFileSystem::FS_OPEN_EXISTING, 0, 0, false);

	if (file == nullptr) THROW_ERROR(ERR_APP, L"failed on opening file: %s", m_file_name.c_str());
	LOG_DEBUG(L"block num=%lld", blk_num);
	//	INT64 test_cycle = blk_num;
	double cycle = jcvos::GetTsCycle();

	UINT64 begin = jcvos::GetTimeStamp();
	UINT64 acc_time = 0;		// 累计IO时间
	size_t acc_count = 0;		// 累计IO及时次数，
	jcvos::auto_array<BYTE> buf(block_size);
	BYTE* _buf = buf;
	for (size_t cycle=0; cycle < info.test_cycle; ++cycle)
//	while (test_cycle > 0)
	{
		for (DWORD bb = 0; bb < blk_num; ++bb) blk_list[bb] = bb;
		DWORD remain_blks = blk_num;
		for (DWORD bb = 0; bb < blk_num; ++bb)
		{
//			DWORD blk_index = remain_blks * rand() / (RAND_MAX);
			DWORD blk_index = rand() % remain_blks;
			DWORD blk_nr = blk_list[blk_index];
			blk_list[blk_index] = blk_list[remain_blks - 1];
			remain_blks--;

//			LOG_DEBUG(L"Write block=%d, index=%d, update blk=%d", blk_nr, blk_index, blk_list[blk_index]);
			blk_check[blk_nr] ++;

			DWORD read = 0;
			size_t offset = (blk_nr * block_size);
			FillBuffer(_buf, 1, 2, offset, block_size);
			file->DokanWriteFile(_buf, (DWORD)block_size, read, offset);
//			test_cycle--;
		}
	}

	UINT64 duration = jcvos::GetTimeStamp() - begin;
//	file->FlushFile();

	file->CloseFile();
	wprintf_s(L"write time=%.f(us), avg bw=%.f(MB/s)\n", duration * cycle, blk_num * block_size / (duration * cycle));
//	file.release();
	for (DWORD bb = 0; bb < blk_num; ++bb)
	{
		if (blk_check[bb] != 1) LOG_ERROR(L"[err], missing check block=%d, count=%d, offset=%lld", bb, blk_check[bb], bb * block_size/16);
	}
	wprintf_s(L"verify:\n");
	FileVerify(m_file_name, 1, 2, m_file_size);
}

void CPerformanceTester::RandomWriteTest(TEST_CASE_INFO & info)
{
	size_t queue_depth = info.queue_depth;
	if (queue_depth == 1)
	{
		RandomWriteSingle(info);
		return;
	}
	size_t block_size = info.block_size;
	size_t blk_num = m_file_size / block_size;
	INT64 test_cycle = blk_num * info.test_cycle;

	wprintf_s(L"Random Write: block=%lld(KB), qd=%lld, cycle=%lld\n", block_size / 1024, queue_depth, test_cycle);


	HANDLE file = CreateFile(m_file_name.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, FILE_FLAG_OVERLAPPED | FILE_FLAG_RANDOM_ACCESS | FILE_FLAG_NO_BUFFERING, nullptr);
	if (file == INVALID_HANDLE_VALUE) THROW_WIN32_ERROR(L"failed on opening file: %s", m_file_name.c_str());

	jcvos::auto_array<OVERLAPPED> _ops(queue_depth, 0);
	jcvos::auto_array<HANDLE> _events(queue_depth);
	jcvos::auto_array<BYTE> _bufs(queue_depth * block_size);
	for (size_t ii = 0; ii < queue_depth * block_size; ++ii)
	{
		_bufs[ii] = (BYTE)rand();
	}
	jcvos::auto_array<UINT64> _start_ts(queue_depth, 0);
//	size_t blk_num = m_file_size / block_size;

	for (size_t ii = 0; ii < queue_depth; ++ii)
	{	// 初始化
		_events[ii] = CreateEvent(nullptr, FALSE, TRUE, nullptr);
		if (_events[ii] == nullptr) THROW_WIN32_ERROR(L"failed on creating event, index=%d", ii);
	}

	//INT64 test_cycle = blk_num;
	size_t running = queue_depth;
	double cycle = jcvos::GetTsCycle();

	UINT64 begin = jcvos::GetTimeStamp();
	UINT64 acc_time = 0;		// 累计IO时间
	size_t acc_count = 0;		// 累计IO及时次数，
	while (test_cycle > 0 || running > 0)
	{
		DWORD ir = WaitForMultipleObjects((DWORD)queue_depth, _events, FALSE, INFINITE);
		if (ir > (WAIT_OBJECT_0 + queue_depth)) THROW_WIN32_ERROR(L"failed on waiting io event, ir=%d", ir);
		UINT64 end_ts = jcvos::GetTimeStamp();
		running--;
		DWORD index = ir - WAIT_OBJECT_0;
		if (_start_ts[index] > 0 && end_ts > _start_ts[index])
		{
			end_ts -= _start_ts[index];
			acc_time += end_ts;
			acc_count++;
		}
		LOG_DEBUG(L"write complete, index=%d, time=%.f(us), cycle=%lld, running=%zd", index, end_ts * cycle, test_cycle, running);
		//开始读取
		if (test_cycle <= 0) continue;
		size_t blk_nr = blk_num * rand() / (RAND_MAX);

		DWORD read = 0;
		OVERLAPPED& op = _ops[index];
		op.hEvent = _events[index];
		op.Pointer = (PVOID)(blk_nr * block_size);
		_start_ts[index] = jcvos::GetTimeStamp();
		LOG_DEBUG(L"start write, index=%d", index);
		WriteFile(file, _bufs + queue_depth * index, (DWORD)block_size, &read, &op);
		test_cycle--;
		running++;
	}

	UINT64 duration = jcvos::GetTimeStamp() - begin;

	for (size_t ii = 0; ii < queue_depth; ++ii)
	{
		CloseHandle(_events[ii]);
	}

	CloseHandle(file);
	wprintf_s(L"write time=%.f(us), avg bw=%.f(MB/s)\n", duration * cycle, blk_num * block_size / (duration * cycle));
	wprintf_s(L"write io time=%.f(us), avg bw=%.f(MB/s)\n\n", acc_time * cycle, acc_count * block_size / (acc_time * cycle));
}

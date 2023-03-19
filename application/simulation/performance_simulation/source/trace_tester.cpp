///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
#include "pch.h"

#include <iostream>
#include <string>
#include <stdio.h>
#include <stdlib.h>
#include <Psapi.h>


#include "trace_tester.h"

LOCAL_LOGGER_ENABLE(L"test.trace", LOGGER_LEVEL_DEBUGINFO);



#define MAX_LINE_BUF	(1024)
#define PATH_PREFIX		(44)


class FIELD_INFO
{
public:
	std::wstring	field_name;
	UINT			field_id;		// 用于标识段的类型（opcode, path等）
	UINT			field_sn;		// 段在csv中出现的位置
};

enum FIELD_INDEX
{
	FIELD_TIMESTAMP=0, FIELD_OPERATION=1, FIELD_PATH=2, FIELD_RESULT=3,FIELD_TID=4, FIELD_PARAM1=5,
	FIELD_PARAM2=6, FIELD_PARAM3=7,
};

CTraceTester::CTraceTester(CLfsInterface* lfs)
//	: CTesterBase(fs, disk)
{
	m_lfs = lfs;
}

CTraceTester::~CTraceTester(void)
{
	LOG_STACK_TRACE();
}

void CTraceTester::Config(const boost::property_tree::wptree& pt, const std::wstring& root)
{
	LOG_STACK_TRACE();

	m_root = root;
	const std::wstring& log_fn = pt.get<std::wstring>(L"log_file", L"");
	m_timeout = pt.get<DWORD>(L"timeout", INFINITE);
	m_message_interval = pt.get<DWORD>(L"message_interval", 30);		// 以秒为单位的更新时间
	m_repeat = pt.get<int>(L"repeat", 1);
	// 添加root
	m_file_access.emplace_back();	// add file 0 (dummy)
	m_path_map.insert(std::make_pair(L"", 0));
	m_file_access.emplace_back();
	FILE_ACCESS_INFO& info = m_file_access.at(0);
	info.fid = 1;
	info.file_name = L"";
	info.parent_fid = 0;
	info.is_dir = true;

	m_file_buf = new char[m_file_buf_size];

	const boost::property_tree::wptree& trace_list = pt.get_child(L"traces");
	for (auto it = trace_list.begin(); it != trace_list.end(); ++it)
	{
		// for each thread
		LOG_DEBUG(L"got node=%s", it->first.c_str());
		if (it->first != L"thread") continue;
		const boost::property_tree::wptree& thread_prop = it->second;
		m_traces.emplace_back();
		TRACE_INFO& trace_info = m_traces.back();
		trace_info.m_tid = thread_prop.get<UINT>(L"id");
		trace_info.m_repeat = thread_prop.get<int>(L"repeat");
		
		const boost::property_tree::wptree& trace_files = thread_prop.get_child(L"trace");
		for (auto tt = trace_files.begin(); tt != trace_files.end(); ++tt)
		{
			LOG_DEBUG(L"got sub node=%s", tt->first.c_str());
			if (tt->first != L"file") continue;
			const std::wstring & fn = tt->second.get_value<std::wstring>();
			trace_info.m_trace_fn = fn;
			LoadTrace(fn, trace_info);
		}

		//trace_info.m_trace_fn = fn;

		//const std::wstring& fn = it->second.get<std::wstring>(L"file");
		//UINT tid = it->second.get<UINT>(L"thread");
		//LoadTrace(fn, tid - 1);
	}
	delete[] m_file_buf;
}

void CTraceTester::FillFile(FID fid, DWORD revision, size_t secs)
{
	JCASSERT(fid < m_file_access.size());
	FILE_ACCESS_INFO& info = m_file_access.at(fid);
	//LOG_DEBUG_(0, L"Fill file: fid=%d, fn=%s", fid, info.file_name);

	m_lfs->FileOpen(fid);
	m_lfs->FileWrite(fid, 0, secs);
	m_lfs->FileClose(fid);
}

void CTraceTester::PrepareFiles(void)
{
	// 准备文件
	for (auto it = m_file_access.begin(); it != m_file_access.end(); ++it)
	{
		FILE_ACCESS_INFO& info = *it;
		if (info.file_name == L"" || info.fid == 1) continue;
		std::wstring path = /*m_root +*/ info.file_name;
//		LOG_DEBUG(L"processing fid=%d, dir=%d, path=%s", info.fid, info.is_dir, path.c_str());
//		LOG_TRACK(L"trace", L",PREPARE,fid=%d,isdir=%d,fn=%s", info.fid, info.is_dir, path.c_str());
		if (info.is_dir || info.child_nr > 0)
		{
			//			BOOL br = CreateDirectory(path.c_str(), nullptr);
			//bool br = m_fs->MakeDir(path);
			//if (!br) THROW_WIN32_ERROR(L"failed on creating dir: %s", path.c_str());
		}
		else
		{
			FID fid = info.fid;
			LOG_TRACK(L"trace", L",PREPARE_WRITE,fid=%d,fn=%s,len=%lld", info.fid, info.file_name.c_str(), info.max_length);
			FillFile(fid, 0, ROUND_UP_DIV(info.max_length, 512));
			SetEvent(m_monitor_event);
		}
	}
	// verify
	for (auto it = m_file_access.begin(); it != m_file_access.end(); ++it)
	{
		FILE_ACCESS_INFO& info = *it;
		if (info.file_name == L"" || info.fid == 0) continue;
		std::wstring path = /*m_root +*/ info.file_name;
		if (info.is_dir || info.child_nr > 0) continue;
		FID fid = info.fid;
		LOG_TRACK(L"verify", L",VERIFY,fid=%d,fn=%s,len=%lld", info.fid, info.file_name.c_str(), info.max_length);
		m_lfs->FileRead(fid, 0, ROUND_UP_DIV(info.max_length, 512));
		SetEvent(m_monitor_event);
	}
}

int CTraceTester::PrepareTest(void)
{
	DWORD ii = 0;
	for (auto it = m_traces.begin(); it != m_traces.end(); it++, ii++)
	{
		it->m_tester = this;
		it->m_tid = ii;
		//it->m_max_buf_size = round_up(it->m_max_buf_size, sizeof(FILE_FILL_DATA));
		//it->m_max_buf_size += sizeof(FILE_FILL_DATA);		// 两头对其
		//it->m_buf = new BYTE[it->m_max_buf_size];
	}
	PrepareFiles();
	return 0;
}

int CTraceTester::RunTest(void)
{
	HANDLE* threads = new HANDLE[m_traces.size()];
	memset(threads, 0, sizeof(HANDLE) * m_traces.size());
	
	for (size_t ii =0; ii < m_traces.size(); ++ii)
	{
		// create thread for test
		TRACE_INFO& trace = m_traces.at(ii);
//		DWORD thread_id;
//		threads[ii] = CreateThread(nullptr, 0, _TestThread, &trace, 0, &thread_id);
//		trace.m_thread = threads[ii];
		trace.m_thread = nullptr;
		TestThread(trace);
	}
	WaitForMultipleObjects(m_traces.size(), threads, TRUE, INFINITE);
	delete[]threads;
	return 0;
}

int CTraceTester::FinishTest(void)
{
	// 统计文件 access
#if 0
	for (auto it = m_file_access.begin(); it != m_file_access.end(); ++it)
	{
		FILE_ACCESS_INFO& info = *it;
		if (info.max_length == 0) continue;
		wprintf_s(L"length=%lld, write=%.1f%%, read=%.1f%%, file=%s\n", info.max_length,
			(double)info.total_write / info.max_length * 100, (double)info.total_read / info.max_length * 100, info.file_name.c_str());
	}
#endif
	// 统计读写性能
	UINT64 min_read_time = UINT64_MAX, min_write_time = UINT64_MAX;
	UINT64 max_read_time = 0, max_write_time = 0, total_read_time = 0, total_write_time = 0;
	UINT64 total_read_byte=0, total_write_byte=0, total_read_count=0, total_write_count=0;
	double max_read_bw=0, max_write_bw=0;
	double ts_cycle = jcvos::GetTsCycle();
	for (auto trace = m_traces.begin(); trace != m_traces.end(); ++trace)
	{
		std::vector<TRACE_ENTRY>& ops = trace->m_trace;
		for (auto op = ops.begin(); op != ops.end(); ++op)
		{
			if (op->op_code == OP_CODE::OP_FILE_READ)
			{
				if (op->duration > max_read_time) max_read_time = op->duration;
				if (op->duration < min_read_time) min_read_time = op->duration;
				total_read_time += op->duration;
				total_read_byte += op->length;
				total_read_count++;
				// 时间单位：us, bw: B/us = MB/s
				double read_bw = (double)(op->length) / (double)(op->duration * ts_cycle);
				if (read_bw > max_read_bw) max_read_bw = read_bw;
			}
			else if (op->op_code == OP_CODE::OP_FILE_WRITE)
			{
				if (op->duration > max_write_time) max_write_time = op->duration;
				if (op->duration < min_write_time) min_write_time = op->duration;
				total_write_time += op->duration;
				total_write_byte += op->length;
				total_write_count++;
				// 时间单位：us, bw: B/us = MB/s
				double read_bw = (double)(op->length) / (double)(op->duration * ts_cycle);
				if (read_bw > max_write_bw) max_write_bw = read_bw;
			}
		}
	}
	wprintf_s(L"read  command=%lld, total read =%.1f(MB), total  read time t1=%.1f(us), t2=%lld(us) \n", total_read_count, total_read_byte / (1024.0 * 1024.0), total_read_time*ts_cycle, m_total_read_time);
	wprintf_s(L"write command=%lld, total write=%.1f(MB), total write time t1=%.1f(us), t2=%lld(us)\n", total_write_count, total_write_byte / (1024.0 * 1024.0), total_write_time*ts_cycle, m_total_write_time);
	wprintf_s(L"Read  Performance: delay = %.f~%.f(us), avg bw=%.f(MB/s), max bw=%.f(MB/s)\n", min_read_time * ts_cycle, max_read_time * ts_cycle, (double)total_read_byte / (total_read_time * ts_cycle), max_read_bw);
	wprintf_s(L"Write Performance: delay = %.f~%.f(us), avg bw=%.f(MB/s), max bw=%.f(MB/s)\n", min_write_time * ts_cycle, max_write_time * ts_cycle, (double)total_write_byte / (total_write_time * ts_cycle), max_write_bw);


	for (auto it = m_traces.begin(); it != m_traces.end(); it++)
	{
		//delete[](it->m_buf);
	}
	return 0;
}

void CTraceTester::CalculateFileAccess(TRACE_ENTRY& op)
{
	FILE_ACCESS_INFO& finfo = m_file_access.at(op.fid);
	size_t start = op.offset;
	size_t end = op.offset + op.length;
	if (end > finfo.max_length) finfo.max_length = end;
	if (op.op_code == OP_CODE::OP_FILE_READ) finfo.total_read += op.length;
	else if (op.op_code == OP_CODE::OP_FILE_WRITE) finfo.total_write += op.length;
	else THROW_ERROR(ERR_APP, L"not read/write op, op=%d", op.op_code);
}

FID CTraceTester::FindOrNewFile(const std::wstring& path, bool is_dir)
{
	// try to find file id
	auto found = m_path_map.find(path);
	if (found != m_path_map.end())
	{
		size_t index = found->second;
		LOG_DEBUG_(1, L"found item=%s, id=%d", path.c_str(), index);
		return index;
	}
	// 没有找到 => 新建file info
	//	(1) 找到parent的file info
	size_t parent_index = (size_t)(-1);
	size_t ss = path.rfind('\\');
	if (ss == std::wstring::npos) 
	{	// 没找到\，root
		parent_index = FID_ROOT;
	}
	else
	{
		std::wstring parent_path(path.c_str(), ss);
		if (parent_path.empty()) parent_index = FID_ROOT;	// root
		else parent_index = FindOrNewFile(parent_path, true);
	}
	JCASSERT(parent_index != (size_t)(-1));

	//DWORD fid = m_max_fid++;
	FID fid = m_lfs->FileCreate(path);
	LOG_TRACK(L"trace", L",CREATE,fid=%d,fn=%s", fid, path.c_str());

//	LOG_DEBUG_(0, L"create item=%s, id=%d, parent_id=%d", path.c_str(), fid, parent_id);
	m_file_access.emplace_back();
	size_t index = m_file_access.size() - 1;
	FILE_ACCESS_INFO& ff = m_file_access.at(index);
	JCASSERT(ff.fid == 0);

	FILE_ACCESS_INFO& parent = m_file_access.at(parent_index);
	parent.child_nr++;
	parent.is_dir = true;
	
	ff.fid = fid;
	ff.parent_fid = parent_index;
	ff.file_name = path;
	ff.is_dir = is_dir;

	m_path_map.insert(std::make_pair(path, index));

	m_lfs->FileClose(fid);
	return index;
}

#define TRACE_BUFFER (1024*128)

#if 0
void CTraceTester::LoadTrace(const std::wstring& fn, UINT tid)
{
	// 读取trace.
	FILE* trace_file = nullptr;
	_wfopen_s(&trace_file, fn.c_str(), L"r");
	if (trace_file == nullptr) THROW_ERROR(ERR_APP, L"failed on opening file %s", fn.c_str());
	jcvos::auto_array<wchar_t> _buf(MAX_LINE_BUF);

	std::vector<std::wstring> heads;
	wchar_t* line = nullptr;
	while (1)
	{
		line = fgetws(_buf, MAX_LINE_BUF - 1, trace_file);
		if (!line) THROW_ERROR(ERR_APP, L"failed on reading csv file header");
		if (line[0] != '#') break;
	}

	// 读取头部
	wchar_t* context = nullptr;
	wchar_t* field_name = wcstok_s(line, L",", &context);
	for (; field_name; field_name = wcstok_s(nullptr, L",", &context))
	{
		std::wstring str(field_name + 1, wcslen(field_name) - 2);
		heads.push_back(str);
	}

	if (tid >= m_traces.size())
	{
		m_traces.emplace_back();
	}
	TRACE_INFO& trace_info = m_traces.at(tid);
	trace_info.m_trace_fn = fn;

	// 读取文件内容
	while (1)
	{
		line = fgetws(_buf, MAX_LINE_BUF, trace_file);
		if (line == nullptr) break;

		if (trace_info.m_trace_nr >= trace_info.m_trace.size())
		{
			size_t new_size = trace_info.m_trace.size() + TRACE_BUFFER;
			trace_info.m_trace.resize(new_size);
		}
		//trace_info.m_trace.emplace_back();
		//TRACE_ENTRY& op = trace_info.m_trace.back();
		TRACE_ENTRY& op = trace_info.m_trace.at(trace_info.m_trace_nr++);

		UINT field_sn = 0;
		wchar_t* field_val = wcstok_s(line, L",\n", &context);
		OP_CODE mode = OP_CODE::OP_NOP;
		for (; field_val; field_val = wcstok_s(nullptr, L",\n", &context), field_sn++)
		{
			//去除引号
			field_val[wcslen(field_val) - 1] = 0;
			field_val++;

			switch (field_sn)
			{
			case FIELD_TIMESTAMP:	// time stamp
				swscanf_s(field_val, L"%lld", &(op.ts));
				break;
			case FIELD_OPERATION:	// operation
				op.op_code = StringToOpCode(field_val); break;
			case FIELD_PATH: {	// path: 出去路径前缀，加入文件map
				if (field_val[0] == 0 ) continue;
				std::wstring path = field_val + PATH_PREFIX; 
				op.file_path = path;
				op.fid = NewFileInfo(op);
				break;
			}
						   //case 3:	// Result
						   //case 4: // Detail
			case FIELD_TID:	// TID
				swscanf_s(field_val, L"%d", &(op.thread_id));
				break;
			case FIELD_PARAM1:	// Param1
				if (op.op_code == OP_CODE::OP_FILE_READ || op.op_code == OP_CODE::OP_FILE_WRITE)
				{	// offset by byte
					swscanf_s(field_val, L"%zd", &(op.offset));
				}
				else if (op.op_code == OP_CODE::OP_FILE_CREATE)
				{	// mode: create, open, delete
					if (wcscmp(field_val, L"Open") == 0) mode = OP_CODE::OP_FILE_OPEN;
					else if (wcscmp(field_val, L"Create") == 0) mode = OP_CODE::OP_FILE_CREATE;
					else if (wcscmp(field_val, L"Overwrite") == 0) mode = OP_CODE::OP_FILE_OVERWRITE;
					else if (wcscmp(field_val, L"Delete") == 0) mode = OP_CODE::OP_FILE_DELETE;
					//					else if (wcscmp(field_val, L"") == 0) op.mode = OP_CODE::OP_;
										//else op.mode = OP_CODE::OP_NOP;
				}
				else if (op.op_code == OP_CODE::OP_THREAD_CREATE || op.op_code == OP_CODE::OP_THREAD_EXIT)
				{	// thread id of new
					swscanf_s(field_val , L"%d", &(op.new_thread_id));
				}
				break;

			case FIELD_PARAM2: // Param2
				if (op.op_code == OP_CODE::OP_FILE_READ || op.op_code == OP_CODE::OP_FILE_WRITE)
				{	// length in byte
					swscanf_s(field_val, L"%zd", &(op.length));
					if (trace_info.m_max_buf_size < op.length) trace_info.m_max_buf_size = op.length;
				}
				else if (op.op_code == OP_CODE::OP_FILE_CREATE)
				{	// dir or file
					//if (wcscmp(field_val, L"TRUE") == 0) op.is_dir = true;
					if (field_val[0] == 'T') op.is_dir = true;	// TRUE;
					else op.is_dir = false;
				}
				else if (op.op_code == OP_CODE::OP_THREAD_CREATE || op.op_code == OP_CODE::OP_THREAD_EXIT)
				{	// user time

				}
				break;

			case FIELD_PARAM3:	// Param3
				if (op.op_code == OP_CODE::OP_FILE_READ || op.op_code == OP_CODE::OP_FILE_WRITE)
				{	// priority
				}
				else if (op.op_code == OP_CODE::OP_FILE_CREATE)
				{	// sync or acync
					//if (wcscmp(field_val, L"TRUE") == 0) op.is_async = false;
					if (field_val[0] == 'T') op.is_async = false;
					else op.is_async = true;
				}
				else if (op.op_code == OP_CODE::OP_THREAD_CREATE || op.op_code == OP_CODE::OP_THREAD_EXIT)
				{	// kernel time

				}
				break;
			}
		}
		if (op.op_code == OP_CODE::OP_FILE_CREATE)
		{
			//DWORD fid = NewFileInfo(op);
			//op.fid = fid;

			if (mode == OP_CODE::OP_FILE_OPEN) op.op_code = OP_CODE::OP_FILE_OPEN;
			else if (mode == OP_CODE::OP_FILE_DELETE) op.op_code = OP_CODE::OP_FILE_DELETE;
			else if (mode == OP_CODE::OP_FILE_OVERWRITE) op.op_code = OP_CODE::OP_FILE_OVERWRITE;
			//else if (mode = OP_CODE::OP_FILE_) op.op_code = OP_CODE::OP_FILE;
			//else if (mode = OP_CODE::OP_FILE_) op.op_code = OP_CODE::OP_FILE;
			//else if (mode = OP_CODE::OP_FILE_) op.op_code = OP_CODE::OP_FILE;
		}
		if (op.op_code == OP_CODE::OP_FILE_CREATE)
		{
		}
		else if (op.op_code == OP_CODE::OP_FILE_READ || op.op_code == OP_CODE::OP_FILE_WRITE)
		{
			CalculateFileAccess(op);
		}
	}
	fclose(trace_file);
}
#else

int CTraceTester::CalculatePrefix(const char* path)
{
	int len = strlen(path);
	if (len <= 3) return 0;
	int ii = 3;	//跳过C:/
	for (; ii < len && path[ii] != '\\'; ii++);
	LOG_DEBUG(L"path=%S, len=%d, prefix=%d", path, len, ii);
	return ii;
}

//void CTraceTester::LoadTrace(const std::wstring& fn, UINT tid)
void CTraceTester::LoadTrace(const std::wstring& fn, TRACE_INFO& trace_info)
{
	// 读取trace.
	FILE* trace_file = nullptr;
	_wfopen_s(&trace_file, fn.c_str(), L"r");
	if (trace_file == nullptr) THROW_ERROR(ERR_APP, L"failed on opening file %s", fn.c_str());
	size_t data_len = fread(m_file_buf, 1, m_file_buf_size, trace_file);
	fclose(trace_file);
	char* line_context = nullptr;
	char* line = strtok_s(m_file_buf, "\r\n", &line_context);
	char* buf_end = m_file_buf + data_len;

	jcvos::auto_array<wchar_t> _buf(MAX_LINE_BUF);

	std::vector<std::string> heads;
	while (1)
	{
		line = strtok_s(nullptr, "\r\n", &line_context);
		if (line[0] == 0) continue;
		if (line[0] == '#') continue;
		break;
	}

	// 读取头部
	char* context = nullptr;
	char* field_name = strtok_s(line, ",", &context);
	for (; field_name; field_name = strtok_s(nullptr, ",", &context))
	{
		field_name[strlen(field_name) - 1] = 0;
		field_name++;
		heads.push_back(field_name);
	}

	//if (tid >= m_traces.size())
	//{
	//	m_traces.emplace_back();
	//}
	//TRACE_INFO& trace_info = m_traces.at(tid);
	//trace_info.m_trace_fn = fn;
	int path_prefix = 0;	//去除path的前缀，临时目录部分
	// 读取文件内容
	while (1)
	{
		line = strtok_s(nullptr, "\r\n", &line_context);
		if (line >= buf_end) break;
		if (line == nullptr) break;
		std::string str_line = line;	// for debug

		if (trace_info.m_trace_nr >= trace_info.m_trace.size())
		{
			size_t new_size = trace_info.m_trace.size() + TRACE_BUFFER;
			trace_info.m_trace.resize(new_size);
		}
		TRACE_ENTRY& op = trace_info.m_trace.at(trace_info.m_trace_nr++);

		UINT field_sn = 0;
		char* field_val = strtok_s(line, ",", &context);
		OP_CODE mode = OP_CODE::OP_NOP;
		for (; field_val; field_val = strtok_s(nullptr, ",", &context), field_sn++)
		{
			//去除引号
			field_val[strlen(field_val) - 1] = 0;
			field_val++;

			switch (field_sn)
			{
			case FIELD_TIMESTAMP:	// time stamp
				op.ts = _atoi64(field_val);
				break;
			case FIELD_OPERATION:	// operation
				op.op_code = StringToOpCode(field_val); break;
			case FIELD_PATH: {	// path: 出去路径前缀，加入文件map
				if (field_val[0] == 0) continue;
				if (path_prefix == 0) path_prefix = CalculatePrefix(field_val);
				std::string path = field_val + path_prefix;
				jcvos::Utf8ToUnicode(op.file_path, path);
				op.fid = NewFileInfo(op);
				break;
			}
						   //case 3:	// Result
						   //case 4: // Detail
			case FIELD_TID:	// TID
				op.thread_id = atoi(field_val);
				break;
			case FIELD_PARAM1:	// Param1
				if (op.op_code == OP_CODE::OP_FILE_READ || op.op_code == OP_CODE::OP_FILE_WRITE)
				{	// offset by byte
					op.offset = _atoi64(field_val);
				}
				else if (op.op_code == OP_CODE::OP_FILE_CREATE)
				{	// mode: create, open, delete
					if (strcmp(field_val, "Open") == 0) mode = OP_CODE::OP_FILE_OPEN;
					else if (strcmp(field_val, "Create") == 0) mode = OP_CODE::OP_FILE_CREATE;
					else if (strcmp(field_val, "Overwrite") == 0) mode = OP_CODE::OP_FILE_OVERWRITE;
					else if (strcmp(field_val, "Delete") == 0) mode = OP_CODE::OP_FILE_DELETE;
				}
				else if (op.op_code == OP_CODE::OP_THREAD_CREATE || op.op_code == OP_CODE::OP_THREAD_EXIT)
				{	// thread id of new
					op.new_thread_id = atoi(field_val);
				}
				break;

			case FIELD_PARAM2: // Param2
				if (op.op_code == OP_CODE::OP_FILE_READ || op.op_code == OP_CODE::OP_FILE_WRITE)
				{	// length in byte
					op.length = _atoi64(field_val);
					//if (trace_info.m_max_buf_size < op.length) trace_info.m_max_buf_size = op.length;
				}
				else if (op.op_code == OP_CODE::OP_FILE_CREATE)
				{	// dir or file
					if (field_val[0] == 'T') op.is_dir = true;	// TRUE;
					else op.is_dir = false;
				}
				else if (op.op_code == OP_CODE::OP_THREAD_CREATE || op.op_code == OP_CODE::OP_THREAD_EXIT)
				{	// user time

				}
				break;

			case FIELD_PARAM3:	// Param3
				if (op.op_code == OP_CODE::OP_FILE_READ || op.op_code == OP_CODE::OP_FILE_WRITE)
				{	// priority
				}
				else if (op.op_code == OP_CODE::OP_FILE_CREATE)
				{	// sync or acync
					if (field_val[0] == 'T') op.is_async = false;
					else op.is_async = true;
				}
				else if (op.op_code == OP_CODE::OP_THREAD_CREATE || op.op_code == OP_CODE::OP_THREAD_EXIT)
				{	// kernel time

				}
				break;
			}
		}
		if (op.op_code == OP_CODE::OP_FILE_CREATE)
		{
			if (mode == OP_CODE::OP_FILE_OPEN) op.op_code = OP_CODE::OP_FILE_OPEN;
			else if (mode == OP_CODE::OP_FILE_DELETE) op.op_code = OP_CODE::OP_FILE_DELETE;
			else if (mode == OP_CODE::OP_FILE_OVERWRITE) op.op_code = OP_CODE::OP_FILE_OVERWRITE;
		}
		if (op.op_code == OP_CODE::OP_FILE_CREATE)
		{
		}
		else if (op.op_code == OP_CODE::OP_FILE_READ || op.op_code == OP_CODE::OP_FILE_WRITE)
		{
			CalculateFileAccess(op);
		}
	}
}
#endif

DWORD CTraceTester::TestThread(TRACE_INFO& trace_info)
{
	DWORD tid = trace_info.m_tid;
	std::vector<TRACE_ENTRY>& trace = trace_info.m_trace;
	for (int pp = 0; pp < trace_info.m_repeat; ++pp)
	{
		wprintf_s(L"start testing cycle=%d\n", pp);
		for (size_t ii = 0; ii < trace_info.m_trace_nr; ++ii)
		{
			InterlockedIncrement(&m_op_sn);
			//TRACE_ENTRY& op = *it;
			TRACE_ENTRY& op = trace.at(ii);
			FILE_ACCESS_INFO& info = m_file_access.at(op.fid);

			switch (op.op_code)
			{
			case OP_CODE::OP_FILE_CREATE: // continue
			case OP_CODE::OP_FILE_OPEN:
			case OP_CODE::OP_FILE_OVERWRITE: {
				DWORD flag = 0;
				if (op.file_path == L"") continue;
				if (info.is_dir) continue;
				//if (info.file_handle[tid])
				//{
				//	info.open_ref[tid]++; 
				//	continue;
				//}
				std::wstring path = m_root + op.file_path;
				m_lfs->FileOpen(info.fid);
				if (op.op_code == OP_CODE::OP_FILE_OVERWRITE)
				{
					LOG_TRACK(L"trace", L",OVERWRITE,file=%s", path.c_str());
					m_lfs->FileTruncate(info.fid);
				}
				else
				{
					LOG_TRACK(L"trace", L",OPEN,file=%s", path.c_str());
				}
				//info.file_handle[tid] = file;
				info.open_ref[tid]++;
				break;
			}
			case OP_CODE::OP_FILE_CLOSE: {
				if (op.file_path == L"") continue;
				if (info.is_dir) continue;
				//JCASSERT(info.open_ref[tid] > 0 && info.file_handle[tid])
				info.open_ref[tid]--;
				if (info.open_ref[tid] == 0)
				{
					LOG_TRACK(L"trace", L",CLOSE,file=%s", op.file_path.c_str());
					m_lfs->FileClose(info.fid);
				}

				break;
			}
			case OP_CODE::OP_FILE_DELETE: {
				std::wstring path = m_root + op.file_path;
				LOG_TRACK(L"trace", L",DELETE,file=%s", path.c_str());

				if (info.is_dir) { LOG_WARNING(L"delete directry: %s", path.c_str()); }
				//JCASSERT(info.file_handle[tid] == nullptr);
				m_lfs->FileOpen(info.fid, true);
				//info.file_handle[tid] = file;
				info.open_ref[tid]++;
				break;
			}
			case OP_CODE::OP_FILE_WRITE: {
				AcquireSRWLockExclusive(&info.file_lock);
				info.revision++;
				UINT64 duration = WriteTest(info, op.offset, op.length);
				op.duration = duration;
				ReleaseSRWLockExclusive(&info.file_lock);
				LOG_TRACK(L"trace", L",WRITE,file=%s,offset=%zd,length=%zd,time=%.1f,(us),bw=%.1f,(MB/s)", op.file_path.c_str(), op.offset, op.length, duration * jcvos::GetTsCycle(), op.length / (duration * jcvos::GetTsCycle()));
				break;
			}
			case OP_CODE::OP_FILE_READ: {
				//if (info.file_handle[tid] == nullptr) continue;
				AcquireSRWLockShared(&info.file_lock);
				op.duration = ReadTest(info, op.offset, op.length);
				ReleaseSRWLockShared(&info.file_lock);
				LOG_TRACK(L"trace", L",READ,file=%s,offset=%zd,length=%zd,time=%.1f,(us),bw=%.1f,(MB/s)", op.file_path.c_str(), op.offset, op.length, op.duration * jcvos::GetTsCycle(), op.length / (op.duration * jcvos::GetTsCycle()));
				break;
			}
			case OP_CODE::OP_FILE_FLUSH: {
				break;
			}
			case OP_CODE::OP_THREAD_CREATE: {break; }
			case OP_CODE::OP_THREAD_EXIT: {break; }
										//		case OP_CODE::OP_FILE: {break; }
			default: LOG_WARNING(L"Unknown operation op_code=%d", op.op_code);
			}
			SetEvent(m_monitor_event);
		}
	}
	return 0;
}

FID CTraceTester::NewFileInfo(TRACE_ENTRY& op)
{
	LOG_DEBUG_(1, L"add file=%s, dir=%d, ts=%lld", op.file_path.c_str(), op.is_dir, op.ts);
	FID fid = FindOrNewFile(op.file_path, op.is_dir);
	return fid;
}

UINT64 CTraceTester::WriteTest(FILE_ACCESS_INFO& info, size_t start, size_t len)
{
	// size to sector
	size_t start_lba, secs;
	SizeToSector(start_lba, secs, start, len);
	info.total_write += secs;

	DWORD written = 0;
	UINT64 begin = jcvos::GetTimeStamp();
	time_t now = time(NULL);
	m_lfs->FileWrite(info.fid, start_lba, secs);
	UINT64 duration = jcvos::GetTimeStamp() - begin;
	time_t now2 = time(NULL);
	LONGLONG dt = (LONGLONG)(difftime(now2, now)*1000*1000);	// to us
	InterlockedAdd64(&m_total_write_time, dt);

	return duration;
}

UINT64 CTraceTester::ReadTest(const FILE_ACCESS_INFO& info, size_t start, size_t len)
{
	size_t start_lba, secs;
	SizeToSector(start_lba, secs, start, len);

	DWORD read = 0;
	UINT64 begin = jcvos::GetTimeStamp();
	time_t now = time(NULL);

	m_lfs->FileRead(info.fid, start_lba, secs);
	UINT64 duration = jcvos::GetTimeStamp() - begin;
	time_t now2 = time(NULL);
	LONGLONG dt = (LONGLONG)(difftime(now2, now)*1000*1000);	// to us
	InterlockedAdd64(&m_total_read_time, dt);
	return duration;
}

void CTraceTester::ReserveFile(const FILE_ACCESS_INFO& info)
{
	//LONG size_lo = (LONG)(info.max_length & 0xFFFFFFFF);
	//LONG size_hi = (LONG)(info.max_length >> 32);
	//DWORD p = SetFilePointer(file, size_lo, &size_hi, FILE_BEGIN);
	//BOOL br = SetEndOfFile(file);
	//file->SetEndOfFile(info.max_length);
	m_lfs->SetFileSize(info.fid, ROUND_UP_POWER(info.max_length, SECTOR_SIZE_BIT) );
}

void CTraceTester::SizeToSector(size_t& lba, size_t& secs, size_t offset, size_t len)
{
	lba = (offset >> SECTOR_SIZE_BIT);
	size_t end_byte = offset + len;
	size_t end_lba = ROUND_UP_POWER(end_byte, SECTOR_SIZE_BIT);
	secs = end_lba - lba;
}

OP_CODE CTraceTester::StringToOpCode(const std::wstring& str)
{
	if (0) {}
	else if (str == L"Thread Create")	return OP_CODE::OP_THREAD_CREATE;
	else if (str == L"Thread Exit")		return OP_CODE::OP_THREAD_EXIT;
	else if (str == L"CreateFile")		return OP_CODE::OP_FILE_CREATE;
	else if (str == L"CloseFile")		return OP_CODE::OP_FILE_CLOSE;
	else if (str == L"ReadFile")		return OP_CODE::OP_FILE_READ;
	else if (str == L"WriteFile")		return OP_CODE::OP_FILE_WRITE;
	else if (str == L"FlushBuffersFile") return OP_CODE::OP_FILE_FLUSH;
	else THROW_ERROR(ERR_APP, L"Unkonw operation: %s", str.c_str());
	return OP_CODE::OP_NOP;
}

OP_CODE CTraceTester::StringToOpCode(const char* str)
{
	if (0) {}
	else if (strcmp(str,"Thread Create")	==0)	return OP_CODE::OP_THREAD_CREATE;
	else if (strcmp(str,"Thread Exit")		==0)	return OP_CODE::OP_THREAD_EXIT;
	else if (strcmp(str,"CreateFile")		==0)	return OP_CODE::OP_FILE_CREATE;
	else if (strcmp(str,"CloseFile")		==0)	return OP_CODE::OP_FILE_CLOSE;
	else if (strcmp(str,"ReadFile")		==0)	return OP_CODE::OP_FILE_READ;
	else if (strcmp(str,"WriteFile")		==0)	return OP_CODE::OP_FILE_WRITE;
	else if (strcmp(str,"FlushBuffersFile")==0)	return OP_CODE::OP_FILE_FLUSH;
	else THROW_ERROR(ERR_APP, L"Unkonw operation: %S", str);
	return OP_CODE::OP_NOP;
}

void CTraceTester::DumpFileMap(int index)
{
	wchar_t fn[32];
	swprintf_s(fn, L"\\file_map_path_%03d.csv", index);
	std::wstring path = m_log_folder + fn;

	FILE* log = nullptr;
	_wfopen_s(&log, path.c_str(), L"w+");
	if (log == nullptr) THROW_ERROR(ERR_APP, L"failed on opening file %s", path.c_str());
	fprintf_s(log, "fn,fid,host_write,start_offset,lblk_nr,phy_blk,seg_id,blk_id\n");

	for (auto it = m_file_access.begin(); it != m_file_access.end(); ++it)
	{
		FILE_ACCESS_INFO& info = *it;
		if (info.file_name == L"" || info.fid == 0) continue;
		if (info.is_dir || info.child_nr > 0) continue;

		fprintf_s(log, "%S,%d,%lld,,,,,\n", info.file_name.c_str(), info.fid, info.total_write);
//		m_lfs->DumpFileMap(log, info.fid);
	}
	fclose(log);
	// dump file by FID
	swprintf_s(fn, L"\\file_map_fid_%03d.csv", index);
	m_lfs->DumpAllFileMap(m_log_folder + fn);
}

DWORD CTraceTester::Monitor(void)
{
	wprintf_s(L"start monitoring, message=%d, timeout=%d\n", m_message_interval, m_timeout);
	boost::posix_time::ptime ts_update = boost::posix_time::microsec_clock::local_time();;

	while (InterlockedAdd(&m_running, 0))
	{
		DWORD ir = WaitForSingleObject(m_monitor_event, m_timeout);
		boost::posix_time::ptime ts_cur = boost::posix_time::microsec_clock::local_time();
		INT64 ts = (ts_cur - m_ts_start).total_seconds();
		if ((ts_cur - ts_update).total_seconds() > m_message_interval)
		{	// update lot
			// get memory info
			bool br = PrintProgress(ts);
			if (!br) THROW_ERROR(ERR_USER, L"failed on getting space or health");
			ts_update = ts_cur;
		}
		if (ir == WAIT_TIMEOUT)
		{
			wprintf_s(L"ts=%llds, test failed: timeout.\n", ts);
			break;
		}
	}
	//if (m_fsinfo_file) CloseHandle(m_fsinfo_file);
	wprintf_s(L"finished testing\n");
	return 0;
}

bool CTraceTester::PrintProgress(INT64 ts)
{
	bool health_valid = false;
	FsHealthInfo health;
	m_lfs->GetHealthInfo(health);

	HANDLE handle = GetCurrentProcess();
	PROCESS_MEMORY_COUNTERS_EX pmc = { 0 };
	GetProcessMemoryInfo(handle, (PROCESS_MEMORY_COUNTERS*)&pmc, sizeof(pmc));

	//ULONGLONG free_bytes = 0, total_bytes = 0, total_free_bytes = 0;
	//float usage = (float)(total_bytes - free_bytes) / total_bytes * 100;

	// 输出到concole
	wprintf_s(L"ts=%llds, op=%d, total_blocks=%d, host_write=%lld, media_write=%lld, WAF=%.2f, free_blk=%d, logical_blk=%d, logical_sat=%d, mem=%.1fMB \n",
		ts, m_op_sn, health.m_blk_nr, health.m_total_host_write, health.m_total_media_write,
		(float)(health.m_total_media_write) / (float)(health.m_total_host_write),
		health.m_free_blk, health.m_logical_blk_nr, health.m_logical_saturation,
		(float)pmc.WorkingSetSize / (1024.0 * 1024.0));
	// 输出到log
//	fprintf_s(m_log_file, "Event,LBlock,BlockNr,HostWrite,MediaWrite,WAF,RunningWAF,FreeSeg,FreeBlock\n");
	fprintf_s(m_log_file, "%d,%d,%lld,%lld,%.2f,%.2f,%d,%d,%d\n",
		m_op_sn, health.m_blk_nr, health.m_total_host_write, health.m_total_media_write,
		(float)(health.m_total_media_write) / (float)(health.m_total_host_write), 0.0,
		health.m_free_blk, health.m_logical_blk_nr, health.m_logical_saturation);
	return true;
}

void CTraceTester::SetLogFolder(const std::wstring& fn)
{
	m_log_folder = fn;
	std::wstring log_fn = m_log_folder + L"\\log.csv";
	_wfopen_s(&m_log_file, log_fn.c_str(), L"w+");
	if (m_log_file == nullptr) THROW_ERROR(ERR_APP, L"failed on create log file %s", fn.c_str());
	fprintf_s(m_log_file, "OpNr,TotalBlock,HostWrite,MediaWrite,WAF,RunningWAF,FreeBlock,LogicalBlock,LogicalSaturation\n");

}

int CTraceTester::StartTest(void)
{
	m_ts_start = boost::posix_time::microsec_clock::local_time();
	wchar_t fn[32];

	int err = 0;
	try
	{
		// 启动监控线程
		m_running = 1;
		m_monitor_event = CreateEvent(NULL, FALSE, FALSE, NULL);
		DWORD thread_id = 0;
		m_monitor_thread = CreateThread(NULL, 0, _Monitor, (PVOID)this, 0, &thread_id);

		err = PrepareTest();
		DumpFileMap(1);

		swprintf_s(fn, L"\\segment_blocks_%03d.csv", 1);
		m_lfs->DumpSegmentBlocks(m_log_folder + fn);

		swprintf_s(fn, L"\\segments_%03d.csv", 1);
		m_lfs->DumpSegments(m_log_folder + fn, true);


		if (err) { LOG_ERROR(L"[err] failed on preparing test, err=%d", err); }

		err = RunTest();
		if (err) { LOG_ERROR(L"[err] failed on testing, err=%d", err); }

	}
	catch (jcvos::CJCException& /*err*/)
	{
		// show stack
		//TEST_LOG(L"\n\n=== result ===\n");
		//TEST_LOG(L"  Test failed\n [err] test failed with error: %s\n", err.WhatT());
		//ShowTestFailure(m_log_file);
		wprintf_s(L" Test failed! \n");
	}

	InterlockedExchange(&m_running, 0);
	SetEvent(m_monitor_event);
	WaitForSingleObject(m_monitor_thread, INFINITE);
	CloseHandle(m_monitor_thread);
	m_monitor_thread = NULL;
	CloseHandle(m_monitor_event);
	m_monitor_event = NULL;

	err = FinishTest();
	if (m_log_file) { fclose(m_log_file); }

	boost::posix_time::ptime ts_cur = boost::posix_time::microsec_clock::local_time();
	INT64 ts = (ts_cur - m_ts_start).total_seconds();
	//PrintProgress(ts);

	//m_lfs->DumpAllFileMap(m_log_folder + L"\\file_map.csv");
	DumpFileMap(2);
	swprintf_s(fn, L"\\segment_blocks_%03d.csv", 2);
	m_lfs->DumpSegmentBlocks(m_log_folder + fn);
	swprintf_s(fn, L"\\segments_%03d.csv", 2);
	m_lfs->DumpSegments(m_log_folder + fn, true);

	wprintf_s(L"Test completed\n");
	if (m_log_file) fclose(m_log_file);
	return err;
}



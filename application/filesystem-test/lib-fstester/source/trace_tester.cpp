///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
#include "pch.h"

#include <iostream>
#include <string>
#include <stdio.h>
#include <stdlib.h>

#include "../include/trace_tester.h"

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

CTraceTester::CTraceTester(IFileSystem * fs, IVirtualDisk * disk) 
	: CTesterBase(fs, disk)
{
}

CTraceTester::~CTraceTester(void)
{
}

void CTraceTester::Config(const boost::property_tree::wptree& pt, const std::wstring& root)
{
	LOG_STACK_TRACE();

	__super::Config(pt, root);
	// 添加root
	m_file_access.emplace_back();	// add file 0 (dummy)
	m_path_map.insert(std::make_pair(L"\\", m_max_fid));
	m_file_access.emplace_back();
	FILE_ACCESS_INFO& info = m_file_access.at(m_max_fid);
	info.fid = 1;
	info.file_name = L"\\";
	info.parent_fid = 0;
	info.is_dir = true;
	m_max_fid++;

	m_file_buf = new char[m_file_buf_size];

	const boost::property_tree::wptree& trace_list = pt.get_child(L"traces");
	for (auto it = trace_list.begin(); it != trace_list.end(); ++it)
	{
		const std::wstring& fn = it->second.get<std::wstring>(L"file");
		UINT tid = it->second.get<UINT>(L"thread");
		LoadTrace(fn, tid - 1);
	}
	delete[] m_file_buf;
}

void CTraceTester::PrepareFiles(void)
{
	// 准备文件
	for (auto it = m_file_access.begin(); it != m_file_access.end(); ++it)
	{
		FILE_ACCESS_INFO& info = *it;
		if (info.file_name == L"" || info.fid == 1) continue;
		std::wstring path = m_root + info.file_name;
//		LOG_DEBUG(L"processing fid=%d, dir=%d, path=%s", info.fid, info.is_dir, path.c_str());
		LOG_TRACK(L"trace", L",PREPARE, fn=%s, fid=%d, isdir=%d", path.c_str(), info.fid, info.is_dir);
		if (info.is_dir || info.child_nr > 0)
		{
			//			BOOL br = CreateDirectory(path.c_str(), nullptr);
			bool br = m_fs->MakeDir(path);
			if (!br) THROW_WIN32_ERROR(L"failed on creating dir: %s", path.c_str());
			SetEvent(m_monitor_event);
		}
		else
		{
			jcvos::auto_interface<IFileInfo> file;
			m_fs->DokanCreateFile(file, path, GENERIC_ALL, 0, IFileSystem::FS_CREATE_ALWAYS, 0, 0, false);
			//HANDLE file = CreateFile(path.c_str(), GENERIC_ALL, 0, nullptr, CREATE_ALWAYS, 0, nullptr);
			//if (file == INVALID_HANDLE_VALUE) THROW_WIN32_ERROR(L"failed on creating file: %s", path.c_str());
			if (file == nullptr) THROW_ERROR(ERR_APP, L"fialed on creating file: %s", path.c_str());
			LOG_TRACK(L"trace", L",PREPARE_WRITE,fn=%s,len=%lld", info.file_name.c_str(), info.max_length);
			FillFile(file, info.fid, info.revision, 0, info.max_length);
			//CloseHandle(file);
			file->CloseFile();
		}
	}
}

int CTraceTester::PrepareTest(void)
{

	DWORD ii = 0;
	for (auto it = m_traces.begin(); it != m_traces.end(); it++, ii++)
	{
		it->m_tester = this;
		it->m_tid = ii;
		it->m_max_buf_size = round_up(it->m_max_buf_size, sizeof(FILE_FILL_DATA));
		it->m_max_buf_size += sizeof(FILE_FILL_DATA);		// 两头对其
		it->m_buf = new BYTE[it->m_max_buf_size];
	}
	return 0;
}

int CTraceTester::RunTest(void)
{
	PrepareFiles();
	HANDLE* threads = new HANDLE[m_traces.size()];
	memset(threads, 0, sizeof(HANDLE) * m_traces.size());
	
	for (size_t ii =0; ii < m_traces.size(); ++ii)
	{
		// create thread for test
		TRACE_INFO& trace = m_traces.at(ii);
		DWORD thread_id;
		threads[ii] = CreateThread(nullptr, 0, _TestThread, &trace, 0, &thread_id);
		trace.m_thread = threads[ii];
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
		delete[](it->m_buf);
	//	delete[]it->m_files;
	//	delete[]it->m_open_ref;
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

DWORD CTraceTester::FindOrNewFile(const std::wstring& path, bool is_dir)
{
	// try to find file id
	auto found = m_path_map.find(path);
	if (found != m_path_map.end())
	{
		DWORD fid = found->second;
		//return m_file_access.at(fid);
		LOG_DEBUG_(1, L"found item=%s, id=%d", path.c_str(), fid);
		return fid;
	}
	// 没有找到 => 新建file info
	//	(1) 找到parent的file info
	DWORD parent_id = 0;
	size_t ss = path.rfind('\\');
	if (ss == std::wstring::npos) 
	{	// 没找到\，root
		parent_id = 1;
	}
	else
	{
		std::wstring parent_path(path.c_str(), ss);
		if (parent_path.empty()) parent_id = 1;	// root
		else parent_id = FindOrNewFile(parent_path, true);
	}
	JCASSERT(parent_id != 0);

	DWORD fid = m_max_fid++;
	LOG_DEBUG_(1, L"create item=%s, id=%d, parent_id=%d", path.c_str(), fid, parent_id);
	m_path_map.insert(std::make_pair(path, fid));
	m_file_access.emplace_back();
	FILE_ACCESS_INFO& ff = m_file_access.at(fid);
	JCASSERT(ff.fid == 0);

	FILE_ACCESS_INFO& parent = m_file_access.at(parent_id);
	parent.child_nr++;
	parent.is_dir = true;
	
	ff.fid = fid;
	ff.parent_fid = parent_id;
	ff.file_name = path;
	ff.is_dir = is_dir;
	return fid;
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

//const size_t buf_size = 128 * 1024 * 1024;
//char* file_buf = new char[buf_size];

void CTraceTester::LoadTrace(const std::wstring& fn, UINT tid)
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
//	wchar_t* line = nullptr;
	while (1)
	{
		line = strtok_s(nullptr, "\r\n", &line_context);
//		line = fgetws(_buf, MAX_LINE_BUF - 1, trace_file);
//		if (!line) THROW_ERROR(ERR_APP, L"failed on reading csv file header");
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
//		std::wstring str(field_name + 1, wcslen(field_name) - 2);
		heads.push_back(field_name);
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
//		line = fgetws(_buf, MAX_LINE_BUF, trace_file);
		line = strtok_s(nullptr, "\r\n", &line_context);
		if (line >= buf_end) break;
		if (line == nullptr) break;
		std::string str_line = line;	// for debug

		if (trace_info.m_trace_nr >= trace_info.m_trace.size())
		{
			size_t new_size = trace_info.m_trace.size() + TRACE_BUFFER;
			trace_info.m_trace.resize(new_size);
		}
		//trace_info.m_trace.emplace_back();
		//TRACE_ENTRY& op = trace_info.m_trace.back();
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
//				sscanf_s(field_val, "%lld", &(op.ts));
				op.ts = _atoi64(field_val);
				break;
			case FIELD_OPERATION:	// operation
				op.op_code = StringToOpCode(field_val); break;
			case FIELD_PATH: {	// path: 出去路径前缀，加入文件map
				if (field_val[0] == 0) continue;
				std::string path = field_val + PATH_PREFIX;
				jcvos::Utf8ToUnicode(op.file_path, path);
//				op.file_path = path;
				op.fid = NewFileInfo(op);
				break;
			}
						   //case 3:	// Result
						   //case 4: // Detail
			case FIELD_TID:	// TID
//				sscanf_s(field_val, "%d", &(op.thread_id));
				op.thread_id = atoi(field_val);
				break;
			case FIELD_PARAM1:	// Param1
				if (op.op_code == OP_CODE::OP_FILE_READ || op.op_code == OP_CODE::OP_FILE_WRITE)
				{	// offset by byte
//					sscanf_s(field_val, "%zd", &(op.offset));
					op.offset = _atoi64(field_val);
				}
				else if (op.op_code == OP_CODE::OP_FILE_CREATE)
				{	// mode: create, open, delete
					if (strcmp(field_val, "Open") == 0) mode = OP_CODE::OP_FILE_OPEN;
					else if (strcmp(field_val, "Create") == 0) mode = OP_CODE::OP_FILE_CREATE;
					else if (strcmp(field_val, "Overwrite") == 0) mode = OP_CODE::OP_FILE_OVERWRITE;
					else if (strcmp(field_val, "Delete") == 0) mode = OP_CODE::OP_FILE_DELETE;
					//					else if (strcmp(field_val, L"") == 0) op.mode = OP_CODE::OP_;
										//else op.mode = OP_CODE::OP_NOP;
				}
				else if (op.op_code == OP_CODE::OP_THREAD_CREATE || op.op_code == OP_CODE::OP_THREAD_EXIT)
				{	// thread id of new
//					sscanf_s(field_val, L"%d", &(op.new_thread_id));
					op.new_thread_id = atoi(field_val);
				}
				break;

			case FIELD_PARAM2: // Param2
				if (op.op_code == OP_CODE::OP_FILE_READ || op.op_code == OP_CODE::OP_FILE_WRITE)
				{	// length in byte
//					sscanf_s(field_val, L"%zd", &(op.length));
					op.length = _atoi64(field_val);
					if (trace_info.m_max_buf_size < op.length) trace_info.m_max_buf_size = op.length;
				}
				else if (op.op_code == OP_CODE::OP_FILE_CREATE)
				{	// dir or file
					//if (strcmp(field_val, L"TRUE") == 0) op.is_dir = true;
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
					//if (strcmp(field_val, L"TRUE") == 0) op.is_async = false;
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
	//fclose(trace_file);
}
#endif

DWORD CTraceTester::TestThread(TRACE_INFO& trace_info)
{
	DWORD tid = trace_info.m_tid;
	std::vector<TRACE_ENTRY>& trace = trace_info.m_trace;
//	HANDLE* files = trace_info.m_files;
//	for (auto it = trace.begin(); it != trace.end(); ++it)
	for (size_t ii=0; ii<trace_info.m_trace_nr; ++ii)
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
			if (info.file_handle[tid])
			{
				info.open_ref[tid]++; 
				continue;
			}
			std::wstring path = m_root + op.file_path;
			//DWORD dipos;
			IFileSystem::FsCreateDisposition dipos;
			if (op.op_code == OP_CODE::OP_FILE_OVERWRITE)	
			{
				dipos = IFileSystem::FS_TRUNCATE_EXISTING; 
				LOG_TRACK(L"trace", L",OVERWRITE,file=%s", path.c_str());
			}
			else 
			{
				dipos = IFileSystem::FS_OPEN_ALWAYS; 
				LOG_TRACK(L"trace", L",OPEN,file=%s", path.c_str());
			}
			DWORD access, share;
			access = GENERIC_READ | GENERIC_WRITE;
			share = FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE;

			IFileInfo* file = nullptr;
			m_fs->DokanCreateFile(file, path, access, 0, dipos, share, 0, false);
//			HANDLE file = CreateFile(path.c_str(), access, share, nullptr, dipos, 0, nullptr);
//			if (file == INVALID_HANDLE_VALUE) THROW_WIN32_ERROR(L"failed on opening file: %s", path.c_str());
			if (file == nullptr) THROW_ERROR(ERR_APP, L"failed on opening file: %s", path.c_str());
			info.file_handle[tid] = file;
			info.open_ref[tid]++;
			break;
		}
		case OP_CODE::OP_FILE_CLOSE: {
			if (op.file_path == L"") continue;
			if (info.is_dir) continue;
			JCASSERT(info.open_ref[tid] > 0 && info.file_handle[tid])
			info.open_ref[tid]--;
			if (info.open_ref[tid] == 0)
			{
				LOG_TRACK(L"trace", L",CLOSE,file=%s", op.file_path.c_str());
				//CloseHandle(info.file_handle[tid]);
				//info.file_handle[tid] = nullptr;
				info.file_handle[tid]->CloseFile();
				RELEASE(info.file_handle[tid]);
			}

			break;
		}
		case OP_CODE::OP_FILE_DELETE: {
			std::wstring path = m_root + op.file_path;
			LOG_TRACK(L"trace", L",DELETE,file=%s", path.c_str());

			if (info.is_dir) { LOG_WARNING(L"delete directry: %s", path.c_str()); }
			JCASSERT(info.file_handle[tid] == nullptr);
			//			if (info.file_handle) continue;		// 文件已经打开
			IFileInfo* file = nullptr;
			m_fs->DokanCreateFile(file, path, GENERIC_ALL, 0, IFileSystem::FS_OPEN_EXISTING, FILE_SHARE_DELETE, FILE_FLAG_DELETE_ON_CLOSE, false);
			if (file == nullptr) THROW_ERROR(ERR_APP, L"failed on open for deleting: %s", path.c_str());
			//HANDLE file = CreateFile(path.c_str(), GENERIC_ALL, FILE_SHARE_DELETE, nullptr, OPEN_EXISTING, FILE_FLAG_DELETE_ON_CLOSE, nullptr);
			//if (file == INVALID_HANDLE_VALUE) THROW_WIN32_ERROR(L"failed on opening file: %s", path.c_str());
			info.file_handle[tid] = file;
			info.open_ref[tid]++;
			break;
		}
		case OP_CODE::OP_FILE_WRITE: {
			//			JCASSERT(info.file_handle);
			if (info.file_handle[tid] == nullptr) continue;
			AcquireSRWLockExclusive(&info.file_lock);
			info.revision++;
			UINT64 duration = WriteTest(info, info.file_handle[tid], trace_info.m_buf, op.offset, op.length);
			op.duration = duration;
			ReleaseSRWLockExclusive(&info.file_lock);
			LOG_TRACK(L"trace", L",WRITE,file=%s,offset=%zd,length=%zd,time=%.1f,(us),bw=%.1f,(MB/s)", op.file_path.c_str(), op.offset, op.length, duration * jcvos::GetTsCycle(), op.length / (duration * jcvos::GetTsCycle()) );
			break;
		}
		case OP_CODE::OP_FILE_READ: {
			if (info.file_handle[tid] == nullptr) continue;
			AcquireSRWLockShared(&info.file_lock);
			op.duration = ReadTest(info, info.file_handle[tid], trace_info.m_buf, op.offset, op.length);
			ReleaseSRWLockShared(&info.file_lock);
			LOG_TRACK(L"trace", L",READ,file=%s,offset=%zd,length=%zd,time=%.1f,(us),bw=%.1f,(MB/s)", op.file_path.c_str(), op.offset, op.length, op.duration * jcvos::GetTsCycle(), op.length / (op.duration * jcvos::GetTsCycle()) );
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

	return 0;
}

DWORD CTraceTester::NewFileInfo(TRACE_ENTRY& op)
{
	LOG_DEBUG_(1, L"add file=%s, dir=%d, ts=%lld", op.file_path.c_str(), op.is_dir, op.ts);
	DWORD fid = FindOrNewFile(op.file_path, op.is_dir);
	return fid;
}

UINT64 CTraceTester::WriteTest(const FILE_ACCESS_INFO& info, IFileInfo* file, BYTE * _buf, size_t start, size_t len)
{
	JCASSERT(file);

	size_t aligned_start = round_down(start, sizeof(FILE_FILL_DATA));
	size_t aligned_end = round_up(start + len, sizeof(FILE_FILL_DATA));
	size_t aligned_len = aligned_end - aligned_start;

	FILE_FILL_DATA* buf = (FILE_FILL_DATA*)(_buf);
	size_t buffer_nr = aligned_len / sizeof(FILE_FILL_DATA);
	size_t offset = start - aligned_start;
	// fill buf buffer
	size_t index = aligned_start / sizeof(FILE_FILL_DATA);
	for (size_t ii = 0; ii < buffer_nr; ii++)
	{
		buf[ii].fid = info.fid;
		buf[ii].rev = info.revision;
		buf[ii].offset = ii + index;
	}
	//LONG start_lo = (LONG)(start & 0xFFFFFFFF);
	//LONG start_hi = (LONG)(start >> 32);
	//SetFilePointer(file, start_lo, &start_hi, FILE_BEGIN);

	DWORD written = 0;
	UINT64 begin = jcvos::GetTimeStamp();
	time_t now = time(NULL);
//	BOOL br = WriteFile(file, buf + offset, (DWORD)len, &written, nullptr);
	bool br = file->DokanWriteFile(buf + offset, (DWORD)len, written, start);
	if (!br || written != len) THROW_WIN32_ERROR(L"failed on writing file=%s, offset=%zd, len=%zd", info.file_name.c_str(), start, len);
	UINT64 duration = jcvos::GetTimeStamp() - begin;
	time_t now2 = time(NULL);
	LONGLONG dt = (LONGLONG)(difftime(now2, now)*1000*1000);	// to us
	InterlockedAdd64(&m_total_write_time, dt);

	return duration;
}

UINT64 CTraceTester::ReadTest(const FILE_ACCESS_INFO& info, IFileInfo* file, BYTE * _buf, size_t start, size_t len)
{
	JCASSERT(file);
	size_t aligned_start = round_down(start, sizeof(FILE_FILL_DATA));
	size_t aligned_end = round_up(start + len, sizeof(FILE_FILL_DATA));
	size_t aligned_len = aligned_end - aligned_start;
	size_t offset = start - aligned_start;

	//LONG start_lo = (LONG)(start & 0xFFFFFFFF);
	//LONG start_hi = (LONG)(start >> 32);
	//SetFilePointer(file, start_lo, &start_hi, FILE_BEGIN);

	DWORD read = 0;
	UINT64 begin = jcvos::GetTimeStamp();
	time_t now = time(NULL);
//	BOOL br = ReadFile(file, _buf + offset, (DWORD)len, &read, nullptr);
	bool br = file->DokanReadFile(_buf + offset, (DWORD)len, read, start);
	if (!br || read != len) THROW_WIN32_ERROR(L"failed on reading file=%s, offset=%zd, len=%zd", info.file_name.c_str(), start, len);
	UINT64 duration = jcvos::GetTimeStamp() - begin;
	time_t now2 = time(NULL);
	LONGLONG dt = (LONGLONG)(difftime(now2, now)*1000*1000);	// to us
	InterlockedAdd64(&m_total_read_time, dt);
	return duration;
}

void CTraceTester::ReserveFile(IFileInfo * file, const FILE_ACCESS_INFO& info)
{
	//LONG size_lo = (LONG)(info.max_length & 0xFFFFFFFF);
	//LONG size_hi = (LONG)(info.max_length >> 32);
	//DWORD p = SetFilePointer(file, size_lo, &size_hi, FILE_BEGIN);
	//BOOL br = SetEndOfFile(file);
	file->SetEndOfFile(info.max_length);
}

OP_CODE CTraceTester::StringToOpCode(const std::wstring& str)
{
	//switch (str[0])
	//{
	//case 'T':return OP_CODE::OP_THREAD_CREATE;
	//	//	case '':return OP_CODE::OP_THREAD_EXIT;
	//	//	case '':return OP_CODE::OP_FILE_CREATE;
	//case 'C':return OP_CODE::OP_FILE_CLOSE;
	//case 'R':return OP_CODE::OP_FILE_READ;
	//case 'W':return OP_CODE::OP_FILE_WRITE;
	//case 'F':return OP_CODE::OP_FILE_FLUSH;
	//};

	if (0) {}
	else if (str == L"Thread Create")	return OP_CODE::OP_THREAD_CREATE;
	else if (str == L"Thread Exit")		return OP_CODE::OP_THREAD_EXIT;
	else if (str == L"CreateFile")		return OP_CODE::OP_FILE_CREATE;
	else if (str == L"CloseFile")		return OP_CODE::OP_FILE_CLOSE;
	else if (str == L"ReadFile")		return OP_CODE::OP_FILE_READ;
	else if (str == L"WriteFile")		return OP_CODE::OP_FILE_WRITE;
	else if (str == L"FlushBuffersFile") return OP_CODE::OP_FILE_FLUSH;
	//else if (str == L"") return OP_CODE::OP_;
	//else if (str == L"") return OP_CODE::OP_;
	//else if (str == L"") return OP_CODE::OP_;
	//else if (str == L"") return OP_CODE::OP_;
	//else if (str == L"") return OP_CODE::OP_;
	//else if (str == L"") return OP_CODE::OP_;
	//else if (str == L"") return OP_CODE::OP_;
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
	//else if (str == L"") return OP_CODE::OP_;
	//else if (str == L"") return OP_CODE::OP_;
	//else if (str == L"") return OP_CODE::OP_;
	//else if (str == L"") return OP_CODE::OP_;
	//else if (str == L"") return OP_CODE::OP_;
	//else if (str == L"") return OP_CODE::OP_;
	//else if (str == L"") return OP_CODE::OP_;
	else THROW_ERROR(ERR_APP, L"Unkonw operation: %S", str);
	return OP_CODE::OP_NOP;
}

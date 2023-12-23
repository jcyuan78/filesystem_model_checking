///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
#include "pch.h"

#include <iostream>
#include <string>
#include <stdio.h>
#include <stdlib.h>


#include "trace_tester.h"

LOCAL_LOGGER_ENABLE(L"test.trace_info", LOGGER_LEVEL_DEBUGINFO);

static const size_t g_file_buf_size = 128 * 1024 * 1024;
char g_file_buf [g_file_buf_size];


///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// == TRACE_INFO ==
void TRACE_INFO::LoadTrace(const boost::property_tree::wptree& trace_prop /*, CTraceTester* tester, int trace_id */ )
{
	JCASSERT(m_tester);

	m_tid = trace_prop.get<UINT>(L"id");
	m_repeat = trace_prop.get<int>(L"repeat");
	int trace_id = 0;

	const boost::property_tree::wptree& trace_files = trace_prop.get_child(L"trace");
	for (auto tt = trace_files.begin(); tt != trace_files.end(); ++tt, trace_id ++)
	{
		LOG_DEBUG(L"got sub node=%s", tt->first.c_str());
		if (tt->first != L"file") continue;
		m_trace_fn = tt->second.get_value<std::wstring>();
		//LoadTrace(fn, trace_info, trace_id++);

		// 读取trace.
		FILE* trace_file = nullptr;
		_wfopen_s(&trace_file, m_trace_fn.c_str(), L"r");
		if (trace_file == nullptr) THROW_ERROR(ERR_APP, L"failed on opening file %s", m_trace_fn.c_str());
		size_t data_len = fread(g_file_buf, 1, g_file_buf_size, trace_file);
		fclose(trace_file);
		char* line_context = nullptr;
		char* line = strtok_s(g_file_buf, "\r\n", &line_context);
		char* buf_end = g_file_buf + data_len;

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

		int path_prefix = 0;	//去除path的前缀，临时目录部分
		// 加入trace开始标志
		m_trace.emplace_back();
		TRACE_ENTRY& op = m_trace.at(m_trace.size() - 1);
		m_trace_nr++;
		op.op_code = OP_MARK_TRACE_BEGIN;
		op.trace_id = trace_id;

		// 读取文件内容
		while (1)
		{
			line = strtok_s(nullptr, "\r\n", &line_context);
			if (line >= buf_end) break;
			if (line == nullptr) break;
			std::string str_line = line;	// for debug

			m_trace.emplace_back();
			TRACE_ENTRY& op = m_trace.at(m_trace.size() - 1);
			m_trace_nr++;

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
					op.op_code = CTraceTester::StringToOpCode(field_val); break;
				case FIELD_PATH: {	// path: 出去路径前缀，加入文件map
					if (field_val[0] == 0) continue;
					if (path_prefix == 0) path_prefix = CTraceTester::CalculatePrefix(field_val);
					std::string path = field_val + path_prefix;
//					jcvos::Utf8ToUnicode(op.file_path, path);
					std::wstring wpath;
					jcvos::Utf8ToUnicode(wpath, path);
					op.file_index = m_tester->NewFileInfo(wpath, op.is_dir);
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
				//JCASSERT(m_tester);
				m_tester->CalculateFileAccess(op);
			}
		}
	}
}
TRACE_ENTRY* TRACE_INFO::NextOp(void)
{
	if (m_next_cycle >= m_repeat) return nullptr;
	if (m_next_op == m_trace.end())
	{
		if (m_next_cycle >= m_repeat) return nullptr;
		m_next_cycle++;
		m_next_op = m_trace.begin();
	}
	TRACE_ENTRY* op = &(*m_next_op);
	m_next_op++;
	return op;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// == Cold Files ==
void TRACE_INFO_COLD_FILES::LoadTrace(const boost::property_tree::wptree& trace_prop/*, CTraceTester* tester, int traceid*/)
{
	JCASSERT(m_tester);
	m_tid = trace_prop.get<UINT>(L"id");
	const boost::property_tree::wptree& files = trace_prop.get_child(L"files");
	for (auto it = files.begin(); it != files.end(); it++)
	{
		if (it->first != L"file") continue;
		const std::wstring& fn = it->second.get<std::wstring>(L"name");
		size_t size = it->second.get<size_t>(L"secs") * SECTOR_SIZE;
		m_tester->AddFIleInfo(fn, size);
	}
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// == Hot Files ==
TRACE_ENTRY* TRACE_INFO_HOT_FILES::NextOp(void)
{
	int i1 = rand(), i2 = rand();
	int ii = ((i1 << 15) + i2) % m_secs;
	memset(&m_op, 0, sizeof(TRACE_ENTRY));

	m_op.file_index = m_file_index;
	m_op.op_code = OP_CODE::OP_FILE_WRITE;
	m_op.offset = ii*SECTOR_SIZE;
	m_op.length = SECTOR_SIZE;

	return &m_op;
}

void TRACE_INFO_HOT_FILES::LoadTrace(const boost::property_tree::wptree& trace_prop/*, CTraceTester* tester, int traceid*/)
{
	JCASSERT(m_tester);
	m_tid = trace_prop.get<UINT>(L"id");
	const boost::property_tree::wptree& files = trace_prop.get_child(L"files");
	for (auto it = files.begin(); it != files.end(); it++)
	{
		// 只处理一个file
		if (it->first != L"file") continue;
		const std::wstring& fn = it->second.get<std::wstring>(L"name");
		m_secs = it->second.get<UINT>(L"secs");
		size_t length = m_secs *SECTOR_SIZE;
		m_file_index = m_tester->AddFIleInfo(fn, length);
//		m_files.push_back(file);
//		m_file = & tester->GetFile(index);
		break;
	}
//	m_secs = m_file->max_length / 512;
}

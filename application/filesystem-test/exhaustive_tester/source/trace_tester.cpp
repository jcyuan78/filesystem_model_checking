///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
#include "pch.h"
#include "../include/extester.h"
#include <boost/property_tree/json_parser.hpp>

OP_CODE StringToOpId(const std::wstring& str)
{
	OP_CODE id;
	if (false) {}
	else if (str == L"CreateFile")	id = OP_CODE::OP_FILE_CREATE;
	else if (str == L"CreateDir")	id = OP_CODE::OP_DIR_CREATE;
	else if (str == L"OpenFile")	id = OP_CODE::OP_FILE_OPEN;
	else if (str == L"CloseFile")	id = OP_CODE::OP_FILE_CLOSE;
	//else if (str == L"Append")		id = OP_CODE::APPEND_FILE;
	else if (str == L"OverWrite")	id = OP_CODE::OP_FILE_WRITE;
	else if (str == L"Write")		id = OP_CODE::OP_FILE_WRITE;
	else if (str == L"DeleteFile")	id = OP_CODE::OP_FILE_DELETE;
	else if (str == L"DeleteDir")	id = OP_CODE::OP_DIR_DELETE;
	else if (str == L"Move")		id = OP_CODE::OP_MOVE;
	else if (str == L"Mount")		id = OP_CODE::OP_DEMOUNT_MOUNT;
//	else if (str == L"PowerCycle")	id = OP_CODE::OP_POWER_OFF_RECOVER;
	else if (str == L"Verify")		id = OP_CODE::OP_FILE_VERIFY;
	else if (str == L"PowerOutage")	id = OP_CODE::OP_POWER_OFF_RECOVER;
	else							id = OP_CODE::OP_NOP;
	return id;
}

const char* OpName(OP_CODE op_code)
{
	switch (op_code)
	{
	case OP_CODE::OP_NOP:			return "none       ";	break;
	case OP_CODE::OP_FILE_CREATE:	return "CreateFile";	break;
	case OP_CODE::OP_DIR_CREATE:	return "CreateDir";		break;
	case OP_CODE::OP_FILE_DELETE:	return "DeleteFile";	break;
	case OP_CODE::OP_DIR_DELETE:	return "DeleteDir";		break;
	case OP_CODE::OP_MOVE:			return "Move";			break;
	case OP_CODE::OP_FILE_WRITE:	return "OverWrite";		break;
	case OP_CODE::OP_FILE_OPEN:		return "OpenFile";		break;
	case OP_CODE::OP_FILE_CLOSE:	return "CloseFile";		break;
	case OP_CODE::OP_DEMOUNT_MOUNT:	return "Mount";			break;
	case OP_CODE::OP_POWER_OFF_RECOVER:	return "PowerOutage";		break;
	default:						return "unknown    ";	break;
	}
}

template <size_t N>
void Op2String(char(&str)[N], TRACE_ENTRY& op)
{
	const char* op_name = NULL;
	char str_param[128] = "";
	//switch (op.op_code)
	//{
	//case OP_CODE::OP_NOP:				op_name = "none       ";	break;
	//case OP_CODE::OP_FILE_CREATE:		op_name = "create-file";	break;
	//case OP_CODE::OP_DIR_CREATE:		op_name = "create-dir ";	break;
	//case OP_CODE::OP_FILE_DELETE:		op_name = "delete-file";	break;
	//case OP_CODE::OP_DIR_DELETE:		op_name = "delete-dir ";	break;
	//case OP_CODE::OP_MOVE:				op_name = "move       ";	break;
	//case OP_CODE::OP_FILE_WRITE:		op_name = "overwrite  ";
	//	sprintf_s(str_param, "offset=%d, secs=%d", op.offset, op.length);
	//	break;
	//case OP_CODE::OP_FILE_OPEN:			op_name = "open-file  ";	break;
	//case OP_CODE::OP_FILE_CLOSE:		op_name = "close-file ";	break;
	//case OP_CODE::OP_DEMOUNT_MOUNT:		op_name = "demnt-mount";	break;
	//case OP_CODE::OP_POWER_OFF_RECOVER:	op_name = "power-off-on";	break;
	//default:							op_name = "unknown    ";	break;
	//}
	if (op.op_code == OP_CODE::OP_FILE_WRITE) {
		sprintf_s(str_param, "offset=%d, secs=%d", op.offset, op.length);
	}
	else if (op.op_code == OP_CODE::OP_POWER_OFF_RECOVER) {
		sprintf_s(str_param, "rollback=%d", op.rollback);
	}
	sprintf_s(str, "op:(%d) [%s], path=%s, param: %s", op.op_sn, OpName(op.op_code), op.file_path.c_str(), str_param);
}

int CExTraceTester::PrepareTest(const boost::property_tree::wptree& config, IFsSimulator* fs, const std::wstring& log_path)
{
	CExTester::PrepareTest(config, fs, log_path);
	const std::wstring & fn  = config.get<std::wstring>(L"trace", L"");
	std::string str_fn;
	jcvos::UnicodeToUtf8(str_fn, fn);

	boost::property_tree::read_json(str_fn, m_trace);

	return ERR_OK;
}


ERROR_CODE CExTraceTester::RunTest(void)
{
	printf_s("Running TRACE test\n");
	InitializeCriticalSection(&m_trace_crit);

	boost::property_tree::ptree & trace=m_trace;
	boost::property_tree::ptree& ops = trace.get_child("op");

	IFsSimulator* fs = nullptr;
	m_fs_factory->Clone(fs);
	CFsState * cur_state = m_states.get();
	cur_state->Initialize("\\", fs);
//	m_cur_state->m_ref = 1;

//	CFsState& state = *m_cur_state;

	ERROR_CODE ir = ERR_OK;
	int step = 0;
	for (auto it = ops.begin(); it != ops.end(); ++it, ++step)
	{
		boost::property_tree::ptree& prop_op = it->second;
		TRACE_ENTRY op;
		std::wstring str_op;
		jcvos::Utf8ToUnicode(str_op, prop_op.get<std::string>("op_name"));
		op.op_code = StringToOpId(str_op);
		op.file_path = prop_op.get<std::string>("path", "");
		if (op.op_code == OP_FILE_OVERWRITE || op.op_code == OP_FILE_WRITE)
		{
			op.offset = prop_op.get<FSIZE>("offset");
			op.length = prop_op.get<FSIZE>("length");
		}
		else if (op.op_code == OP_POWER_OFF_RECOVER)
		{
			op.offset = prop_op.get<FSIZE>("offset");
		}

		printf_s("[setp]=%06d, ", step);
		CFsState* next_state = m_states.duplicate(cur_state);
		next_state->m_op = op;
		bool is_power_off = false;
		switch (op.op_code)
		{
		case OP_CODE::OP_NOP:			break;
		case OP_CODE::OP_FILE_CREATE:
			printf_s("[CreateFile] %s\n", op.file_path.c_str());
			ir = TestCreateFile(next_state, op.file_path);
			break;
		case OP_CODE::OP_DIR_CREATE:
			printf_s("[CreateDir] %s\n", op.file_path.c_str());
			ir = TestCreateDir(next_state, op.file_path);
			break;
		case OP_CODE::OP_FILE_DELETE:
			printf_s("[DeleteFile] %s\n", op.file_path.c_str());
			ir = TestDeleteFile(next_state, op.file_path);
			break;
		case OP_CODE::OP_DIR_DELETE:
			printf_s("[DeleteDir] %s\n", op.file_path.c_str());
			ir = TestDeleteDir(next_state, op.file_path);
			break;
		case OP_CODE::OP_MOVE:			break;
		case OP_CODE::OP_FILE_WRITE: {
			CReferenceFs::CRefFile* ref_file = next_state->m_ref_fs.FindFile(op.file_path);
			NID fid = ref_file->get_fid();
			printf_s("[WriteFile] %s, fid=%d, offset=%d, len=%d\n", op.file_path.c_str(), fid, op.offset, op.length);
			ir = TestWriteFileV2(next_state, fid, op.offset, op.length, op.file_path);
			break; }
		case OP_CODE::OP_FILE_OPEN:			
			printf_s("[OpenFile] %s\n", op.file_path.c_str());
			ir =TestOpenFile(next_state, op.file_path);
			break;

		case OP_CODE::OP_FILE_CLOSE: {
			CReferenceFs::CRefFile* ref_file = next_state->m_ref_fs.FindFile(op.file_path);
			NID fid = ref_file->get_fid();
			printf_s("[CloseFile] %s, fid=%d\n", op.file_path.c_str(), fid);
			ir = TestCloseFile(next_state, fid, op.file_path);
			break; }

		case OP_CODE::OP_DEMOUNT_MOUNT:
			printf_s("[Mount] \n");
			ir =TestMount(next_state);
			break;
		case OP_CODE::OP_POWER_OFF_RECOVER:	
			printf_s("[Power] \n");
			is_power_off = true;
			ir = TestPowerOutage(next_state, op.rollback);
			break;

		case OP_CODE::OP_FILE_VERIFY: 
			ir = Verify(next_state);
			 break;
		default:						break;
		}
		printf_s("[Verify] \n");
		if (is_power_off)	ir = VerifyForPower(next_state);
		else				ir = Verify(next_state);
		FS_INFO fs_info;
		fs->GetFsInfo(fs_info);
		printf_s("fs: logic=%d, phisic=%d, total_blk=%d, free_blk=%d, total_seg=%d, free_seg=%d\n",
			fs_info.used_blks, fs_info.physical_blks, fs_info.total_blks, fs_info.free_blks, fs_info.total_seg, fs_info.free_seg);
		printf_s("fs: free pages= %d / %d, free data = %d / %d\n",
			fs_info.free_page_nr, fs_info.total_page_nr, fs_info.free_data_nr, fs_info.total_data_nr);
		printf_s("\n");

		OutputTrace_Thread(next_state, ir, "debug");
		m_states.put(cur_state);
		cur_state = next_state;
	}
	m_states.put(cur_state);
	DeleteCriticalSection(&m_trace_crit);

	return ir;
}

void CExTester::TraceTestVerify(IFsSimulator* fs, const std::string& fn)
{
	NID fid = INVALID_BLK;
	ERROR_CODE err = fs->FileOpen(fid, fn);
	if (err == ERR_MAX_OPEN_FILE) return;

	FSIZE file_size = fs->GetFileSize(fid);
	size_t blk_nr = ROUND_UP_DIV(file_size, BLOCK_SIZE);
	FILE_DATA* data = new FILE_DATA[blk_nr + 10];
	memset(data, 0, sizeof(FILE_DATA) * (blk_nr + 10));
	blk_nr = fs->FileRead(data, fid, 0, file_size);
	for (size_t ii = 0; ii < blk_nr; ++ii)
	{
		if (data[ii].fid != fid || data[ii].offset != ii) {
			THROW_ERROR(ERR_APP, L"read file does not match, fid=%d, lblk=%d, page_fid=%d, page_offset=%d",
				fid, ii, data[ii].fid, data[ii].offset);
		}
	}
	delete[] data;
}

bool CExTester::OutputTrace(CFsState* state)
{
	std::list<CFsState*> stack;
	while (state)
	{
		TEST_LOG("state=%p, parent=%p, depth=%d\n", state, state->m_parent, state->m_depth);
		IFsSimulator* fs = state->m_real_fs;
		FS_INFO fs_info;
		fs->GetFsInfo(fs_info);
		TEST_LOG("\tfs: logic=%d, phisic=%d, total_blk=%d, free_blk=%d, total_seg=%d, free_seg=%d\n",
			 fs_info.used_blks, fs_info.physical_blks, fs_info.total_blks, fs_info.free_blks, fs_info.total_seg, fs_info.free_seg);
		TEST_LOG("\tfs: free pages= %d / %d, free data = %d / %d\n",
			fs_info.free_page_nr, fs_info.total_page_nr, fs_info.free_data_nr, fs_info.total_data_nr);

		CReferenceFs& ref = state->m_ref_fs;
		auto endit = ref.End();
		for (auto it = ref.Begin(); it != endit; ++it)
		{
			const CReferenceFs::CRefFile& ref_file = ref.GetFile(it);
			std::string path;
			ref.GetFilePath(ref_file, path);
			bool dir = ref.IsDir(ref_file);
			char str_encode[MAX_ENCODE_SIZE];
			ref_file.GetEncodeString(str_encode, MAX_ENCODE_SIZE);
			FSIZE ref_len = 0;
			if (!dir)
			{
				DWORD ref_checksum;
				ref.GetFileInfo(ref_file, ref_checksum, ref_len);
			}
			static const size_t str_size = (INDEX_TABLE_SIZE * 10);
			char str_index[str_size];
#if 0
			NID index[INDEX_TABLE_SIZE];		// 磁盘数据，
			size_t nr = fs->DumpFileIndex(index, INDEX_TABLE_SIZE, ref_file.get_fid());
			size_t ptr = 0;
			for (size_t ii = 0; ii < INDEX_TABLE_SIZE; ++ii)
			{
				if (index[ii] == INVALID_BLK) break;
				ptr += sprintf_s(str_index+ptr, str_size-ptr, "%03X ", index[ii]);
			}
#else
			str_index[0] = 0;
#endif

			TEST_LOG("\t\t<check %s> (fid=%02d) %s [%s], (%s), size=%d\n",
				dir ? "dir " : "file", ref_file.get_fid(), path.c_str(), str_index, str_encode, ref_len);

			std::vector<GC_TRACE> gc;
			fs->GetGcTrace(gc);
			if (!gc.empty())
			{
				TEST_LOG("gc:\n");
				for (auto ii = gc.begin(); ii != gc.end(); ++ii)
				{
					TEST_LOG("(%d,%d): %d=>%d\n", ii->fid, ii->offset, ii->org_phy, ii->new_phy);
				}
			}
		}
		//	TRACE_ENTRY& op = state->m_op;
		char str[256];
		Op2String(str, state->m_op);
		TEST_LOG("\t%s\n", str);
		stack.push_front(state);
		state = state->m_parent;
	}

	// 将trace 转化为json文件，便于后续调试
	boost::property_tree::ptree prop_trace;
	boost::property_tree::ptree prop_op_array;

	for (auto ii = stack.begin(); ii != stack.end(); ++ii)
	{
		TRACE_ENTRY& op = (*ii)->m_op;
		// op to propeyty
		boost::property_tree::ptree prop_op;
		prop_op.add<std::string>("op_name", OpName(op.op_code));
		prop_op.add<std::string>("path", op.file_path);
		prop_op.add("offset", op.offset);
		prop_op.add("length", op.length);
		prop_op_array.push_back(std::make_pair("", prop_op));
	}
	prop_trace.add_child("op", prop_op_array);

	DWORD tid = GetCurrentThreadId();
	char str_fn[MAX_PATH];
	sprintf_s(str_fn, "%S\\trace_%d.json", m_log_path.c_str(), tid);
//	std::string str_fn;
//	jcvos::UnicodeToUtf8(str_fn, (m_log_path + L"\\trace.json"));
	boost::property_tree::write_json(str_fn, prop_trace);
	TEST_CLOSE_LOG;
	return false;
}




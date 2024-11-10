///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
#include "pch.h"
#include "../include/extester.h"
#include <Psapi.h>
#include <boost/property_tree/json_parser.hpp>

LOCAL_LOGGER_ENABLE(L"extester.obselete", LOGGER_LEVEL_DEBUGINFO);


ERROR_CODE CExTester::TestWriteFile(CFsState* cur_state, const std::string& path, FSIZE offset, FSIZE len)
{
	JCASSERT(cur_state);
	IFsSimulator* real_fs = cur_state->m_real_fs;
	JCASSERT(real_fs);
	CReferenceFs& ref = cur_state->m_ref_fs;

	ERROR_CODE err = ERR_OK;
	//	len &= ~3;		// DWORD对齐
	TEST_LOG("[OPERATE ](%d) WriteFile, path=%s, offset=%d, size=%d\n", cur_state->m_op.op_sn, path.c_str(), offset, len);

	//	HANDLE file = CreateFile(path.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, 0, 0);
	
	NID fid = INVALID_BLK;
	err = real_fs->FileOpen(fid, path);
	if (err!= ERR_OK || fid == INVALID_BLK)
	{
		err = ERR_OPEN_FILE;
		THROW_WIN32_ERROR(L"failed on opening file, fn=%s", path.c_str());
	}

	//	size_t total, used, free, max_files, file_num;
	FS_INFO space_info;
	real_fs->GetFsInfo(space_info);

	FSIZE cur_ref_size;
	DWORD cur_checksum;
	CReferenceFs::CRefFile* ref_file = ref.FindFile(path);
	if (!ref_file) THROW_ERROR(ERR_USER, L"cannof find ref file: %s", path.c_str());
	ref.GetFileInfo(*ref_file, cur_checksum, cur_ref_size);

	// get current file length
	FSIZE cur_file_size = real_fs->GetFileSize(fid);
	//	if (cur_len != info.nFileSizeLow && info.nFileSizeLow != 0)
	if (cur_ref_size != cur_file_size) {
		THROW_ERROR(ERR_USER, L"file length does not match ref=%d, file=%d", cur_ref_size, cur_file_size);
	}

	// 
	FSIZE new_file_size = offset + len;
	if (new_file_size > cur_file_size)
	{	// 当要写入的文件大小超过空余容量时，缩小写入量。
		FSIZE incre_blks = ROUND_UP_DIV(new_file_size - cur_file_size, BLOCK_SIZE);
		if (incre_blks > space_info.free_blks)
		{
			incre_blks = space_info.free_blks;
			new_file_size = cur_file_size + incre_blks * BLOCK_SIZE;
			if (new_file_size < offset)
			{
				len = 0; new_file_size = offset;
			}
			else len = new_file_size - offset;
		}
	}
	real_fs->FileWrite(fid, offset, len);
	if (cur_file_size > new_file_size) new_file_size = cur_file_size;

	//DWORD checksum = CalFileChecksum(fid);
	DWORD checksum = fid;
	//	ref_file->m_pre_size = (int)cur_ref_size;
	ref.UpdateFile(path, checksum, new_file_size);
	//	TEST_LOG("\t current size=%d, new size=%d", cur_file_size, new_file_size);
		//CloseHandle(file);
	real_fs->FileClose(fid);
	//	TEST_LOG(", closed");

	//	TEST_CLOSE_LOG;
	return err;
}


#include "pch.h"
#include "../include/BaseTestCase.h"

using fs_testing::tests::BaseTestCase;
//using std::string;

//using fs_testing::user_tools::api::CmFsOps;
using fs_testing::user_tools::api::RecordCmFsOps;
using fs_testing::user_tools::api::PassthroughCmFsOps;

LOCAL_LOGGER_ENABLE(L"crashmonke.testcase", LOGGER_LEVEL_DEBUGINFO);


int BaseTestCase::init_values(std::wstring mount_dir, size_t filesys_size, IFileSystem * fs)
{
	LOG_NOTICE(L"init test case, mount_dir = %s", mount_dir.c_str());
	if (fs == NULL) THROW_ERROR(ERR_APP, L"file system cannot be null");
	m_fs = fs;
	m_fs->AddRef();

//	mnt_dir_ = mount_dir;
	filesys_size_ = filesys_size;
	return 0;
}

int BaseTestCase::Run(FILE* change_fd, const int checkpoint)
{
#if 1 //_TO_BE_IMPLEMENTED_
//	DefaultFsFns default_fns;
	RecordCmFsOps cm(m_fs);
	PassthroughCmFsOps pcm(m_fs);
	if (checkpoint == 0) { cm_ = &cm; }
	else { cm_ = &pcm; }
#endif

	int res_1 = run(checkpoint);
	if (res_1 < 0) { return res_1; }

	if (checkpoint == 0)
	{
#if 1 // _TO_BE_IMPLEMENTED_
		int res_2 = cm.Serialize(change_fd);
		if (res_2 < 0) { return res_2; }
#endif
	}
	cm_ = NULL;
	return res_1;
}


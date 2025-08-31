#include "pch.h"
#include "../include/BaseTestCase.h"
#include <boost/cast.hpp>

using fs_testing::tests::BaseTestCase;
//using std::string;

//using fs_testing::user_tools::api::CmFsOps;
using fs_testing::user_tools::api::RecordCmFsOps;
using fs_testing::user_tools::api::PassthroughCmFsOps;
using fs_testing::utils::communication::SocketMessage;
using fs_testing::utils::communication::SocketError;
using fs_testing::utils::communication::ClientSocket;

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
	LOG_STACK_TRACE();
	m_client = new ClientSocket(fs_testing::utils::communication::kSocketNameOutbound);
	m_client->Init();

//	DefaultFsFns default_fns;
	RecordCmFsOps cm(m_fs);
	PassthroughCmFsOps pcm(m_fs);
	if (checkpoint == 0) { cm_ = &cm; }
	else { cm_ = &pcm; }

	int res_1 = run(checkpoint);
	LOG_DEBUG(L"test run returned, code = %d", res_1);
	m_client->Disconnect();

	if (res_1 < 0) { return res_1; }

	if (checkpoint == 0)
	{
		JCASSERT(change_fd);
		int res_2 = cm.Serialize(change_fd);
		if (res_2 < 0) { return res_2; }
	}
	cm_ = NULL;

	delete m_client;
	m_client = NULL;
	return res_1;
}

int fs_testing::tests::BaseTestCase::Checkpoint(const wchar_t* cmt)
{
	LOG_STACK_TRACE();
	JCASSERT(m_client);
	
	LOG_NOTICE(L"send message = checkpoint");
	//	if (conn.SendCommand(send_command) != SocketError::kNone)	return -2;
	SocketMessage msg;
	msg.type = SocketMessage::kCheckpoint;
	if (cmt) msg.string_value = cmt;
	msg.size = boost::numeric_cast<unsigned int>(msg.string_value.size() * sizeof(wchar_t));
	if (m_client->SendPipeMessage(msg) != SocketError::kNone) return -2;

	SocketMessage ret;
	LOG_NOTICE(L"waiting for reply message...");
	if (m_client->WaitForMessage(&ret) != SocketError::kNone)	return -3;
	LOG_NOTICE(L"got reply");
	if (cm_) cm_->CmCheckpoint(NULL);
//	if (conn.SendCommand(SocketMessage::kDisconnect) != SocketError::kNone) return -3;
	return !(ret.type == SocketMessage::kCheckpointDone);
}



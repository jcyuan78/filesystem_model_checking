#include "pch.h"
#include "actions.h"
#include <crashmonkey_comm.h>

#include "ClientCommandSender.h"

//namespace fs_testing {
//namespace user_tools {
//namespace api {

using fs_testing::utils::communication::ClientCommandSender;
using fs_testing::utils::communication::kSocketNameOutbound;
using fs_testing::utils::communication::SocketMessage;

int fs_testing::user_tools::api::Checkpoint(const wchar_t * cmt)
{
	ClientCommandSender c(fs_testing::utils::communication::kSocketNameOutbound, SocketMessage::kCheckpoint,
		SocketMessage::kCheckpointDone, cmt);
	return c.Run();
}

//} // fs_testing
//} // user_tools
//} // api

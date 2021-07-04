﻿#include "../../pch.h"
#include <string>

#include "ClientCommandSender.h"
//#include "ClientSocket.h"
#include "SocketUtils.h"
#include "../../ThreadMessageQue.h"

//namespace fs_testing {
//namespace utils {
//namespace communication {
using fs_testing::utils::communication::ClientCommandSender;
using fs_testing::utils::communication::ClientSocket;

using std::string;

ClientCommandSender::ClientCommandSender(const std::wstring &socket_addr, SocketMessage::CmCommand send,
	SocketMessage::CmCommand recv) 
	: socket_address(socket_addr), send_command(send),return_command(recv), conn(ClientSocket(socket_address))
{
}

int ClientCommandSender::Run()
{
	if (conn.Init() < 0)
	{
		return -1;
	}

	if (conn.SendCommand(send_command) != SocketError::kNone)
	{
		return -2;
	}

	SocketMessage ret;
	if (conn.WaitForMessage(&ret) != SocketError::kNone)
	{
		return -3;
	}
	return !(ret.type == return_command);
}

//}  // namesapce communication
//}  // namesapce utils
//}  // namesapce fs_testing

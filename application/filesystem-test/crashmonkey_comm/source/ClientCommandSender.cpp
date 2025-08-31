#include "pch.h"
/////== scaler 120 column == //////////////////////////////////////////////////////////////////////////////////////////

#include <string>
#include <boost/cast.hpp>

#include "../include/ClientCommandSender.h"


using fs_testing::utils::communication::ClientCommandSender;
using fs_testing::utils::communication::ClientSocket;
using fs_testing::utils::communication::SocketError;

LOCAL_LOGGER_ENABLE(L"crashmonkey.socket", LOGGER_LEVEL_DEBUGINFO);

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// == ClientSocket

ClientSocket::ClientSocket(std::wstring address)
    : m_pipe_name(address)
{
    m_client = NULL;
};

ClientSocket::~ClientSocket()
{
    //    close(socket_fd);
    CloseClient();
}

int ClientSocket::Init()
{
    LOG_STACK_TRACE()
    LOG_DEBUG(L"waiting for connection: %s", m_pipe_name.c_str());
    BOOL br = WaitNamedPipe(m_pipe_name.c_str(), NMPWAIT_WAIT_FOREVER);
    if (!br)
    {
        LOG_WIN32_ERROR(L"[err] failed on waiting name pipe");
        return SocketError::kSyscall;
    }
    LOG_DEBUG(L"connected to server");

    m_client = CreateFile(m_pipe_name.c_str(), GENERIC_READ | GENERIC_WRITE,
        0, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (m_client == INVALID_HANDLE_VALUE)
    {
        m_client = NULL;
        LOG_WIN32_ERROR(L"[err] failed on connecting name pipe");
        return -1;
    }
    LOG_DEBUG(L"got clinet handle=%lld", m_client);
    return 0;
}

SocketError ClientSocket::SendCommand(SocketMessage::CmCommand c)
{
    SocketMessage m;
    m.type = c;
    m.size = 0;
    return SendPipeMessage(m);
}

SocketError ClientSocket::SendPipeMessage(SocketMessage& m)
{
    if (m_client == NULL)    {        return SocketError::kNotConnected;    }
    if (BaseSocket::WriteMessageToSocket(m_client, m) < 0)    {        return SocketError::kSyscall;    }
    return SocketError::kNone;
}

SocketError ClientSocket::WaitForMessage(SocketMessage* m)
{
    if (BaseSocket::ReadMessageFromSocket(m_client, m) < 0) return SocketError::kSyscall;
    return SocketError::kNone;
}

void ClientSocket::CloseClient()
{
    if (m_client) CloseHandle(m_client);
    m_client = NULL;
}

SocketError fs_testing::utils::communication::ClientSocket::Disconnect(void)
{
    LOG_STACK_TRACE();
    SocketError err = SendCommand(SocketMessage::kDisconnect);
    if (err != SocketError::kNone)
    {
        LOG_ERROR(L"failed sending disconnect command");
        return err;
    }
    return SocketError::kNone;
    CloseClient();
}


///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// == ClientCommandSender


ClientCommandSender::ClientCommandSender(const std::wstring &socket_addr, SocketMessage::CmCommand send,
	SocketMessage::CmCommand recv, const wchar_t * msg) 
	: socket_address(socket_addr), send_command(send),return_command(recv), conn(ClientSocket(socket_address))
{
    if (msg) m_msg = msg;
}

int ClientCommandSender::Run()
{
	if (conn.Init() < 0)	return -1;
    LOG_NOTICE(L"send message = %d", send_command);
    //	if (conn.SendCommand(send_command) != SocketError::kNone)	return -2;
    SocketMessage msg;
    msg.type = send_command;
    msg.string_value = m_msg;
    msg.size = boost::numeric_cast<unsigned int>(m_msg.size() * sizeof(wchar_t));
    if (conn.SendPipeMessage(msg) != SocketError::kNone) return -2;

	SocketMessage ret;
    LOG_NOTICE(L"waiting for reply message...");
	if (conn.WaitForMessage(&ret) != SocketError::kNone)	return -3;
    LOG_NOTICE(L"got reply");
    if (conn.SendCommand(SocketMessage::kDisconnect) != SocketError::kNone) return -3;
	return !(ret.type == return_command);
}

using fs_testing::utils::communication::ClientCommandSender;
using fs_testing::utils::communication::kSocketNameOutbound;
using fs_testing::utils::communication::SocketMessage;

int fs_testing::user_tools::api::_Checkpoint(const wchar_t* cmt)
{
    ClientCommandSender c(fs_testing::utils::communication::kSocketNameOutbound, SocketMessage::kCheckpoint,
        SocketMessage::kCheckpointDone, cmt);
    return c.Run();
}

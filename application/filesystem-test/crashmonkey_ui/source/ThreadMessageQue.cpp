#include "pch.h"
/////== scaler 120 column == //////////////////////////////////////////////////////////////////////////////////////////

#include "ThreadMessageQue.h"
#include <sys/stat.h>
#include <string>

LOCAL_LOGGER_ENABLE(L"fstester.socket", LOGGER_LEVEL_DEBUGINFO);

using fs_testing::utils::communication::ServerSocket;
using fs_testing::utils::communication::SocketError;
using fs_testing::utils::communication::BaseSocket;

/////== scaler 120 column == //////////////////////////////////////////////////////////////////////////////////////////
namespace 
{
    const unsigned int kNonBlockPollTimeout = 25;
}

ServerSocket::ServerSocket(std::wstring address) 
    : m_pipe_name(address), m_server(NULL)
{
};

ServerSocket::~ServerSocket() {
    // If we try to close an invalid file descriptor then oh well, nothing bad should happen (famous last words...).
    CloseServer();
}

int ServerSocket::Init(unsigned int queue_depth) 
{
    LOG_STACK_TRACE()
    m_server = CreateNamedPipe(m_pipe_name.c_str(), PIPE_ACCESS_DUPLEX,
        PIPE_TYPE_BYTE | PIPE_WAIT, PIPE_UNLIMITED_INSTANCES, 256, 256, kNonBlockPollTimeout, NULL);
    if (m_server == INVALID_HANDLE_VALUE || m_server == 0)
    {
        m_server = NULL;
        THROW_WIN32_ERROR(L"failed on creating name pipe");
    }
    LOG_DEBUG(L"pipe server created = %lld", m_server);
    return 0;
}

SocketError ServerSocket::SendCommand(SocketMessage::CmCommand c) 
{
    SocketMessage m;
    m.type = c;
    m.size = 0;
    return SendMessage(m);
}

SocketError ServerSocket::SendMessage(SocketMessage& m) 
{
    if (m_server == NULL) 
    {
        LOG_ERROR(L"named pipe server does not connected");
        return SocketError::kNotConnected;
    }

    if (BaseSocket::WriteMessageToSocket(m_server, m) < 0) 
    {
        LOG_ERROR(L"[err] failed on writing message");
        return SocketError::kSyscall;
    }
    return SocketError::kNone;
}

SocketError ServerSocket::WaitForMessage(SocketMessage* m) 
{
    LOG_STACK_TRACE();
    // We're already connected to something.
#if 0
    if (m_client > 0) 
    {
        LOG_ERROR(L"[err] client already connected");
        return SocketError::kAlreadyConnected;
    }
    // Block until we can call accept without blocking.
    //struct pollfd pfd;
    //pfd.fd = server_socket;
    //pfd.events = POLLIN;

    //int res = poll(&pfd, 1, -1);

    //if (res == -1) {
    //    return SocketError::kSyscall;
    //}
    //else if (res == 0) {
    //    return SocketError::kTimeout;
    //}
    //else if (!(pfd.revents & POLLIN)) {
    //    return SocketError::kOther;
    //}

//    DWORD ir = WaitForSingleObject(m_server, INFINITE);
    LOG_DEBUG(L"waiting for pipe");
    BOOL br = WaitNamedPipe(m_pipe_name.c_str(), NMPWAIT_WAIT_FOREVER);
    if (!br)
    {
        LOG_WIN32_ERROR(L"[err] failed on waiting name pipe");
        return SocketError::kSyscall;
    }

    // For now, don't care about getting the client address.
    //client_socket = accept(server_socket, NULL, NULL);
    //if (client_socket < 0) {
    //    return SocketError::kSyscall;
    //}

    m_client = CreateFile(m_pipe_name.c_str(), GENERIC_READ | GENERIC_WRITE, 
        0, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (m_client == INVALID_HANDLE_VALUE)
    {
        m_client = NULL;
        LOG_WIN32_ERROR(L"[err] failed on connecting name pipe");
        return SocketError::kSyscall;
    }

    if (BaseSocket::ReadMessageFromSocket(m_client, m)) 
    {
        LOG_ERROR(L"[err] failed on reading message");
        return SocketError::kSyscall;
    }
    return SocketError::kNone;
#endif
    if (BaseSocket::ReadMessageFromSocket(m_server, m))
    {
        LOG_WIN32_ERROR(L"[err] failed on connecting name pipe");
        return SocketError::kSyscall;
    }
    LOG_DEBUG(L"finished sending message");
    return SocketError::kNone;
}

SocketError ServerSocket::TryForMessage(SocketMessage* m) 
{
    LOG_STACK_TRACE();
#if 0
    // We're already connected to something.
    //if (m_client > 0) 
    //{
    //    LOG_ERROR(L"named pipe client alread connected")
    //    return SocketError::kAlreadyConnected;
    //}
    // Block until we can call accept without blocking.
    //struct pollfd pfd;
    //pfd.fd = server_socket;
    //pfd.events = POLLIN;

    //int res = poll(&pfd, 1, kNonBlockPollTimeout);

    //if (res == -1) {
    //    return SocketError::kSyscall;
    //}
    //else if (res == 0) {
    //    return SocketError::kTimeout;
    //}
    //else if (!(pfd.revents & POLLIN)) {
    //    return SocketError::kOther;
    //}
    LOG_DEBUG(L"waiting for connection");
    BOOL br = WaitNamedPipe(m_pipe_name.c_str(), kNonBlockPollTimeout);
    if (!br)
    {
        LOG_WIN32_ERROR(L"[err] failed on waiting name pipe");
        return SocketError::kSyscall;
    }

    // For now, don't care about getting the client address.
    //client_socket = accept(server_socket, NULL, NULL);
    //if (client_socket < 0) {
    //    return SocketError::kSyscall;
    //}

    HANDLE client = CreateFile(m_pipe_name.c_str(), GENERIC_READ | GENERIC_WRITE,
        0, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (client == INVALID_HANDLE_VALUE)
    {
//        m_client = NULL;
        LOG_WIN32_ERROR(L"[err] failed on connecting name pipe");
        return SocketError::kSyscall;
    }

    if (BaseSocket::ReadMessageFromSocket(client, m)) 
    {
        LOG_WIN32_ERROR(L"[err] failed on connecting name pipe");
        return SocketError::kSyscall;
    }
    CloseHandle(client);
    return SocketError::kNone;
#else
    if (BaseSocket::ReadMessageFromSocket(m_server, m))
    {
        LOG_WIN32_ERROR(L"[err] failed on connecting name pipe");
        return SocketError::kSyscall;
    }
    LOG_DEBUG(L"finished sending message");
    return SocketError::kNone;
#endif
}

SocketError fs_testing::utils::communication::ServerSocket::WaitingForClient(void)
{
    LOG_DEBUG(L"waiting connection");
    BOOL br = ConnectNamedPipe(m_server, NULL);
    LOG_DEBUG(L"got connection = %d", br);
    if (!br)
    {
        DWORD ir = GetLastError();
        LOG_DEBUG(L"waiting code = %d", ir);
        if (ir == ERROR_PIPE_LISTENING) return SocketError::kNotConnected;
        else if (ir == ERROR_PIPE_CONNECTED) return SocketError::kNone;
        else
        {
            LOG_WIN32_ERROR_ID(ir, L"[err] failed on waiting connection");
            return SocketError::kSyscall;
        }
    }
    return kNone;
}

void ServerSocket::CloseClient() 
{
}

void ServerSocket::CloseServer() 
{
    CloseClient();
    if (m_server)
    {
        DisconnectNamedPipe(m_server);
        CloseHandle(m_server);
    }
    m_server = NULL;
}

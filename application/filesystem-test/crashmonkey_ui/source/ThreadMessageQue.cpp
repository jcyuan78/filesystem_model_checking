#include "pch.h"
#include "ThreadMessageQue.h"

//#include <poll.h>
//#include <sys/socket.h>
#include <sys/stat.h>
//#include <sys/un.h>
//#include <unistd.h>

#include <string>

LOCAL_LOGGER_ENABLE(L"fstester.socket", LOGGER_LEVEL_DEBUGINFO);


//#include "ServerSocket.h"
using fs_testing::utils::communication::ServerSocket;
using fs_testing::utils::communication::SocketError;
using fs_testing::utils::communication::BaseSocket;
using fs_testing::utils::communication::ClientSocket;


//using std::memset;
//using std::string;
//using std::strncpy;

int BaseSocket::ReadMessageFromSocket(HANDLE socket, SocketMessage* m) {
    // What sort of message are we dealing with and how big is it?
    // TODO(ashmrtn): Improve error checking/recoverability.
    int res = ReadIntFromSocket(socket, (int*)&m->type);
    if (res < 0) {
        return res;
    }
    res = ReadIntFromSocket(socket, (int*)&m->size);
    if (res < 0) {
        return res;
    }

    switch (m->type) {
        // These messges should contain no extra data.
    case SocketMessage::kHarnessError:
    case SocketMessage::kInvalidCommand:
    case SocketMessage::kPrepare:
    case SocketMessage::kPrepareDone:
    case SocketMessage::kBeginLog:
    case SocketMessage::kBeginLogDone:
    case SocketMessage::kEndLog:
    case SocketMessage::kEndLogDone:
    case SocketMessage::kRunTests:
    case SocketMessage::kRunTestsDone:
    case SocketMessage::kCheckpoint:
    case SocketMessage::kCheckpointDone:
    case SocketMessage::kCheckpointFailed:
        // Somebody sent us extra data anyway. Gobble it up and throw it away.
        if (m->size != 0) {
            res = GobbleData(socket, m->size);
        }
        break;
    default:
        res = -1;
    }
    return res;
};

int BaseSocket::WriteMessageToSocket(HANDLE socket, SocketMessage& m) {
    // What sort of message are we dealing with and how big is it?
    // TODO(ashmrtn): Improve error checking/recoverability.
    int res = WriteIntToSocket(socket, (int)m.type);
    if (res < 0) {
        return res;
    }

    switch (m.type) {
        // These messges should contain no extra data.
    case SocketMessage::kHarnessError:
    case SocketMessage::kInvalidCommand:
    case SocketMessage::kPrepare:
    case SocketMessage::kPrepareDone:
    case SocketMessage::kBeginLog:
    case SocketMessage::kBeginLogDone:
    case SocketMessage::kEndLog:
    case SocketMessage::kEndLogDone:
    case SocketMessage::kRunTests:
    case SocketMessage::kRunTestsDone:
    case SocketMessage::kCheckpoint:
    case SocketMessage::kCheckpointDone:
    case SocketMessage::kCheckpointFailed:
        // By default, always send the proper size of the message and no other,
        // extra data.
        res = WriteIntToSocket(socket, 0);
        if (res < 0) {
            return res;
        }
        break;
    default:
        res = -1;
    }
    return res;
};

int BaseSocket::GobbleData(HANDLE socket, unsigned int len) 
{
    int bytes_read = 0;
//    char tmp[len];
    jcvos::auto_array<BYTE> tmp(len);
    do 
    {
        DWORD read = 0;
        BOOL br = ReadFile(socket, tmp + bytes_read, len - bytes_read, &read, NULL);
        //int res = recv(socket, tmp + bytes_read, sizeof(tmp) - bytes_read, 0);
        //if (res < 0) {
        //    return -1;
        //}
        if (!br || read == 0) { LOG_WIN32_ERROR(L"[err] error in reading pipe"); return -1; }
        bytes_read += read;
    } while (bytes_read < tmp.size());
    return 0;
}

int BaseSocket::ReadIntFromSocket(HANDLE socket, int* data) 
{
    int bytes_read = 0;
    int32_t d;
    BYTE* buf = (BYTE*)&d;
    do
    {
        DWORD read = 0;
        BOOL br =ReadFile(socket, buf + bytes_read, sizeof(d) - bytes_read, &read, NULL);
        //int res = recv(socket, (char*)&d + bytes_read, sizeof(d) - bytes_read, 0);
        //if (res < 0) {
        //    return -1;
        //}
        if (!br || read == 0) { LOG_WIN32_ERROR(L"[err] error in reading pipe"); return -1; }
        bytes_read += read;
    } while (bytes_read < sizeof(d));

    // Correct the byte order of the command.
    *data = d;
    return 0;
}

int BaseSocket::WriteIntToSocket(HANDLE socket, int data) 
{
//    int32_t d = htonl(data);
    int bytes_written = 0;
    BYTE* buf = (BYTE*)&data;
    do {
        DWORD written = 0;
        BOOL br = WriteFile(socket, buf + bytes_written, sizeof(data) - bytes_written, &written, NULL);
        //int res = send(socket, (char*)&d + bytes_written,
        //    sizeof(d) - bytes_written, 0);
        //if (res < 0) {
        //    return -1;
        //}
        if (!br || written == 0) { LOG_WIN32_ERROR(L"[err] error in writting pipe"); return -1; }
        bytes_written += written;
    } while (bytes_written < sizeof(data));
    return 0;
}

int BaseSocket::ReadStringFromSocket(HANDLE socket, unsigned int len, std::string& data) 
{
    // Assume all messages are sent in network endian. Furthermore, strings have
    // been rounded up to the nearest multiple of sizeof(uint32_t) bytes.

    // Not a multiple of sizeof(uint32_t) bytes.
    if (len & (sizeof(uint32_t) - 1)) 
    {
        return -1;
    }

    size_t bytes_read = 0;
    //char read_string[len];
    jcvos::auto_array<BYTE> read_string(len,0);
    do 
    {
        BYTE* buf = read_string + bytes_read;
        DWORD read = 0;
        BOOL br = ReadFile(socket, buf, len - bytes_read, &read, NULL);
        //int res = recv(socket, read_string + bytes_read, len - bytes_read, 0);
        if (!br)
        {
            LOG_WIN32_ERROR(L"[err] error in reading pipe");
            return -1;
        }
        bytes_read += read;
    } while (bytes_read < len);

    // Correct the byte order of the string by abusing type casts... yay!
    //uint32_t* tmp = (uint32_t*)read_string.get_ptr();
    //for (int i = 0; i < len / sizeof(uint32_t); ++i) 
    //{
    //    *(tmp + i) = ntohl(*(tmp + i));
    //}
    data = (char*)(read_string.get_ptr());
    return 0;
}

// Assume all messages are sent in network endian. Furthermore, strings are
// rounded up to the nearest multiple of sizeof(uint32_t) bytes.
int BaseSocket::WriteStringToSocket(HANDLE socket, const std::string& data) 
{
    // Some prep work so we can send everything one after the other.
    const int len =
        (data.size() + (sizeof(uint32_t) - 1)) & ~(sizeof(uint32_t) - 1);

    //char send_data[len];
    jcvos::auto_array<char> send_data(len, 0);
    //memset(send_data, 0, len);
    //strcpy(send_data, data.c_str());
    strcpy_s(send_data, len, data.c_str());
    uint32_t* tmp = (uint32_t*)send_data.get_ptr();
    //for (int i = 0; i < len / sizeof(uint32_t); ++i) 
    //{
    //    *(tmp + i) = htonl(*(tmp + i));
    //}

    // Send string size.
    int res = WriteIntToSocket(socket, len);
    if (res < 0) {   return res;  }

    // Send string itself.
    int bytes_written = 0;
    do 
    {
        DWORD written = 0;
        BOOL br = WriteFile(socket, send_data.get_ptr() + bytes_written, len - bytes_written, &written, NULL);
        //int res = send(socket, send_data + bytes_written, len - bytes_written, 0);
        //if (res < 0) {
        //    return -1;
        //}
        if (!br || written == 0)
        {
            LOG_WIN32_ERROR(L"[err] error in writing pipe");
        }
        bytes_written += written;
    } while (bytes_written < len);

    return 0;
}



/////== scaler 120 column == //////////////////////////////////////////////////////////////////////////////////////////
namespace 
{
    const unsigned int kNonBlockPollTimeout = 25;
}

ServerSocket::ServerSocket(std::wstring address) 
    : m_pipe_name(address), m_server(NULL), m_client(NULL)
{
};

ServerSocket::~ServerSocket() {
    // If we try to close an invalid file descriptor then oh well, nothing bad
    // should happen (famous last words...).
    CloseServer();
    //close(client_socket);
    //close(server_socket);
    //unlink(m_pipe_name.c_str());
}

int ServerSocket::Init(unsigned int queue_depth) 
{
    m_server = CreateNamedPipe(m_pipe_name.c_str(), PIPE_ACCESS_DUPLEX,
        PIPE_TYPE_BYTE | PIPE_NOWAIT, PIPE_UNLIMITED_INSTANCES, 256, 256, kNonBlockPollTimeout,
        NULL);
    if (m_server == INVALID_HANDLE_VALUE || m_server == 0)
    {
        m_server = NULL;
        THROW_WIN32_ERROR(L"failed on creating name pipe");
    }
    //server_socket =
    //    socket(AF_LOCAL, SOCK_STREAM | SOCK_CLOEXEC | SOCK_NONBLOCK, 0);
    //if (server_socket < 0) {
    //    return -1;
    //}
    //struct sockaddr_un comm;
    //comm.sun_family = AF_LOCAL;
    //strcpy(comm.sun_path, m_pipe_name.c_str());
    //if (bind(server_socket, (const struct sockaddr*)&comm, sizeof(comm)) < 0) {
    //    return -1;
    //}

    //if (listen(server_socket, queue_depth) < 0) {
    //    return -1;
    //}
    BOOL br = ConnectNamedPipe(m_server, NULL);
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

    if (BaseSocket::WriteMessageToSocket(m_client, m) < 0) 
    {
        LOG_ERROR(L"[err] failed on writing message");
        return SocketError::kSyscall;
    }
    return SocketError::kNone;
}

SocketError ServerSocket::WaitForMessage(SocketMessage* m) 
{
    // We're already connected to something.
    if (m_client >= 0) 
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
}

SocketError ServerSocket::TryForMessage(SocketMessage* m) 
{
    // We're already connected to something.
    if (m_client >= 0) 
    {
        LOG_ERROR(L"named pipe client alread connected")
        return SocketError::kAlreadyConnected;
    }
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
        LOG_WIN32_ERROR(L"[err] failed on connecting name pipe");
        return SocketError::kSyscall;
    }
    return SocketError::kNone;
}

void ServerSocket::CloseClient() 
{
    if (m_client) CloseHandle(m_client);
    m_client = NULL;
    //close(client_socket);
    //client_socket = -1;
}

void ServerSocket::CloseServer() 
{
    //close(client_socket);
    //client_socket = -1;
    CloseClient();
    if (m_server)
    {
        DisconnectNamedPipe(m_server);
        CloseHandle(m_server);
    }
    m_server = NULL;
    //close(server_socket);
    //server_socket = -1;
}


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
    m_client = CreateFile(m_pipe_name.c_str(), GENERIC_READ | GENERIC_WRITE,
        0, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (m_client == INVALID_HANDLE_VALUE)
    {
        m_client = NULL;
        LOG_WIN32_ERROR(L"[err] failed on connecting name pipe");
        return -1;
    }

    //socket_fd = socket(PF_LOCAL, SOCK_STREAM, 0);
    //if (socket_fd < 0)
    //{
    //    return -1;
    //}
    //struct sockaddr_un socket_info;
    //socket_info.sun_family = AF_LOCAL;
    //strcpy(socket_info.sun_path, socket_address.c_str());

    //if (connect(socket_fd, (struct sockaddr*)&socket_info,
    //    sizeof(socket_info)) < 0)
    //{
    //    return -1;
    //}
    return 0;
}

SocketError ClientSocket::SendCommand(SocketMessage::CmCommand c)
{
    SocketMessage m;
    m.type = c;
    m.size = 0;
    return SendMessage(m);
}

SocketError ClientSocket::SendMessage(SocketMessage& m)
{
    if (m_client == NULL)
    {
        return SocketError::kNotConnected;
    }

    if (BaseSocket::WriteMessageToSocket(m_client, m) < 0)
    {
        return SocketError::kSyscall;
    }
    return SocketError::kNone;
}

SocketError ClientSocket::WaitForMessage(SocketMessage* m)
{
    if (BaseSocket::ReadMessageFromSocket(m_client, m) < 0)
    {
        return SocketError::kSyscall;
    }

    return SocketError::kNone;
}

void ClientSocket::CloseClient()
{
    //close(socket_fd);
    //socket_fd = -1;
    if (m_client) CloseHandle(m_client);
    m_client = NULL;
}

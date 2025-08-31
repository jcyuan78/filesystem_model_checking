#include "pch.h"
/////== scaler 120 column == //////////////////////////////////////////////////////////////////////////////////////////


#include "../include/socket-base.h"

LOCAL_LOGGER_ENABLE(L"crashmonkey.socket", LOGGER_LEVEL_DEBUGINFO);
using fs_testing::utils::communication::BaseSocket;
using fs_testing::utils::communication::SocketError;


int BaseSocket::ReadMessageFromSocket(HANDLE socket, SocketMessage* m)
{
//    LOG_STACK_TRACE();
    // What sort of message are we dealing with and how big is it?
    // TODO(ashmrtn): Improve error checking/recoverability.
    int res = ReadIntFromSocket(socket, (int*)&m->type);
    if (res < 0)    {        return res;    }
    res = ReadIntFromSocket(socket, (int*)&m->size);
    if (res < 0)    {        return res;    }
    LOG_DEBUG(L"read message=%d, size=%d", m->type,m->size);

    switch (m->type)
    {      // These messges should contain no extra data.
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
    case SocketMessage::kCheckpointDone:
    case SocketMessage::kCheckpointFailed:
    case SocketMessage::kDisconnect:
        // Somebody sent us extra data anyway. Gobble it up and throw it away.
        if (m->size != 0)        {            res = GobbleData(socket, m->size);        }
        break;
    case SocketMessage::kCheckpoint:        // 需要接受附加信息
        if (m->size > 0)        ReadStringFromSocket(socket, m->size, m->string_value);
        break;
    default:        res = -1;
    }
    return res;
};

int BaseSocket::WriteMessageToSocket(HANDLE socket, SocketMessage& m)
{
    // What sort of message are we dealing with and how big is it?
    // TODO(ashmrtn): Improve error checking/recoverability.
    int res = WriteIntToSocket(socket, (int)m.type);
    if (res < 0)    return res;

    switch (m.type)
    {        // These messges should contain no extra data.
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
    case SocketMessage::kCheckpointDone:
    case SocketMessage::kCheckpointFailed:
    case SocketMessage::kDisconnect:
        // By default, always send the proper size of the message and no other, extra data.
        res = WriteIntToSocket(socket, 0);
        if (res < 0)        {            return res;        }
        break;
    case SocketMessage::kCheckpoint:    // 发送附加信息
        if (m.size > 0)     WriteStringToSocket(socket, m.string_value);
        break;
    default:        res = -1;
    }
    LOG_DEBUG(L"sent messagey=%d, size=%d", m.type, m.size);
    return res;
};

int BaseSocket::GobbleData(HANDLE socket, unsigned int len)
{
    LOG_STACK_TRACE();
    int bytes_read = 0;
    jcvos::auto_array<BYTE> tmp(len);
    do
    {
        DWORD read = 0;
        BOOL br = ReadFile(socket, tmp + bytes_read, len - bytes_read, &read, NULL);
        if (!br || read == 0) { LOG_WIN32_ERROR(L"[err] error in reading pipe"); return -1; }
        bytes_read += read;
    } while (bytes_read < tmp.size());
    return 0;
}

int BaseSocket::ReadIntFromSocket(HANDLE socket, int* data)
{
//    LOG_STACK_TRACE(L"socket=%lld", socket);
    int bytes_read = 0;
    int32_t d;
    BYTE* buf = (BYTE*)&d;
    do
    {
        DWORD read = 0;
        BOOL br = ReadFile(socket, buf + bytes_read, sizeof(d) - bytes_read, &read, NULL);
        if (!br || read == 0) { LOG_WIN32_ERROR(L"[err] error in reading pipe"); return -1; }
        bytes_read += read;
    } while (bytes_read < sizeof(d));

    // Correct the byte order of the command.
    *data = d;
    return 0;
}

int BaseSocket::WriteIntToSocket(HANDLE socket, int data)
{
    int bytes_written = 0;
    BYTE* buf = (BYTE*)&data;
    do
    {
        DWORD written = 0;
        BOOL br = WriteFile(socket, buf + bytes_written, sizeof(data) - bytes_written, &written, NULL);
        if (!br || written == 0) { LOG_WIN32_ERROR(L"[err] error in writting pipe"); return -1; }
        bytes_written += written;
    } while (bytes_written < sizeof(data));
    return 0;
}

int BaseSocket::ReadStringFromSocket(HANDLE socket, unsigned int len, std::wstring& data)
{
    // Assume all messages are sent in network endian. Furthermore, strings have
    // been rounded up to the nearest multiple of sizeof(uint32_t) bytes.

    // Not a multiple of sizeof(uint32_t) bytes.
    if (len & (sizeof(uint32_t) - 1))    { return -1;  }

    UINT32 bytes_read = 0;
    jcvos::auto_array<BYTE> read_string(len+2, 0);
    do
    {
        BYTE* buf = read_string + bytes_read;
        DWORD read = 0;
        BOOL br = ReadFile(socket, buf, len - bytes_read, &read, NULL);
        if (!br)    THROW_WIN32_ERROR(L"[err] error in reading pipe");
        bytes_read += read;
    } while (bytes_read < len);

    data = (wchar_t*)(read_string.get_ptr());
    return 0;
}

// Assume all messages are sent in network endian. Furthermore, strings are rounded up to the nearest multiple of sizeof(uint32_t) bytes.
int BaseSocket::WriteStringToSocket(HANDLE socket, const std::wstring& data)
{
    // Some prep work so we can send everything one after the other.
    const int len = (data.size() * sizeof(wchar_t) + (sizeof(uint32_t) - 1)) & ~(sizeof(uint32_t) - 1);
    jcvos::auto_array<wchar_t> send_data(len+1, 0);
    wcscpy_s(send_data, len+1, data.c_str());
    uint32_t* tmp = (uint32_t*)send_data.get_ptr();

    // Send wstring size.
    int res = WriteIntToSocket(socket, len);
    if (res < 0) { return res; }

    // Send wstring itself.
    int bytes_written = 0;
    do
    {
        DWORD written = 0;
        BOOL br = WriteFile(socket, send_data.get_ptr() + bytes_written, len - bytes_written, &written, NULL);
         if (!br || written == 0)        {            THROW_WIN32_ERROR(L"[err] error in writing pipe");        }
        bytes_written += written;
    } while (bytes_written < len);
    return 0;
}

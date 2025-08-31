#ifndef FS_TESTING_UTILS_COMMUNICATION_CLIENT_COMMAND_SENDER_H
#define FS_TESTING_UTILS_COMMUNICATION_CLIENT_COMMAND_SENDER_H

/////== scaler 120 column == //////////////////////////////////////////////////////////////////////////////////////////

#include <string>

//#include <crashmonkey_comm.h>
#include "SocketUtils.h"
#include "socket-base.h"

namespace fs_testing {
	namespace utils {
		namespace communication {
			// Simple class that acts as a client for a socket by sending and sometimes receiving messages.
			//<warning!> *** This is not a thread-safe class. ***
			class ClientSocket
			{
			public:
				ClientSocket(std::wstring address);
				~ClientSocket();
				int Init();
				SocketError SendCommand(SocketMessage::CmCommand c);
				SocketError SendPipeMessage(SocketMessage& m);
				SocketError WaitForMessage(SocketMessage* m);
				void CloseClient();
				SocketError Disconnect(void);
			private:
				HANDLE m_client;
				const std::wstring m_pipe_name;
			};

			class ClientCommandSender
			{
			public:
				ClientCommandSender(const std::wstring & socket_addr, SocketMessage::CmCommand send,
					SocketMessage::CmCommand recv, const wchar_t* msg = NULL);
				int Run();

			private:
				const std::wstring socket_address;
				const SocketMessage::CmCommand send_command;
				const SocketMessage::CmCommand return_command;
				ClientSocket conn;
				std::wstring m_msg;
			};

		}  // namesapce communication
	}  // namesapce utils
}  // namesapce fs_testing

namespace fs_testing {
	namespace user_tools {
		namespace api {
			//<YUAN>参数中增加一个附加信息cmt，用于调试时核对checkpoint的位置
			int _Checkpoint(const wchar_t* cmt = NULL);

		} // fs_testing
	} // user_tools
} // api


#endif  // FS_TESTING_UTILS_COMMUNICATION_CLIENT_COMMAND_SENDER_H

#pragma once
/////== scaler 120 column == //////////////////////////////////////////////////////////////////////////////////////////


#include "utils/communication/SocketUtils.h"

//<YUAN>将Socket改为命名管道实现，<TODO>可以优化为
//	原设计中，client为发送端，server为接收端
namespace fs_testing {
	namespace utils {
		namespace communication {
			class BaseSocket {
			public:
				static int ReadMessageFromSocket(HANDLE socket, SocketMessage* data);
				static int WriteMessageToSocket(HANDLE socket, SocketMessage& data);

			private:
				static int GobbleData(HANDLE socket, unsigned int len);
				static int ReadIntFromSocket(HANDLE socket, int* data);
				static int WriteIntToSocket(HANDLE socket, int data);
				static int ReadStringFromSocket(HANDLE socket, unsigned int len, std::string& data);
				static int WriteStringToSocket(HANDLE socket, const std::string& data);
			};

			// Simple class that acts as a server for a socket by receiving and replying to
			// messages.
			// *** This is not a thread-safe class. ***
			class ServerSocket {
			public:
				ServerSocket(std::wstring address);
				~ServerSocket();
				int Init(unsigned int queue_depth);
				// Shorthand for SendMessage with the proper options.
				SocketError SendCommand(SocketMessage::CmCommand c);
				SocketError SendMessage(SocketMessage& m);
				SocketError WaitForMessage(SocketMessage* m);
				SocketError TryForMessage(SocketMessage* m);
				void CloseClient();
				void CloseServer();
			private:
				HANDLE m_server;
				HANDLE m_client;
//				int server_socket = -1;
//				int client_socket = -1;
				const std::wstring m_pipe_name;
			};

			// Simple class that acts as a client for a socket by sending and sometimes
			// receiving messages.
			// *** This is not a thread-safe class. ***
			class ClientSocket
			{
			public:
				ClientSocket(std::wstring address);
				~ClientSocket();
				int Init();
				SocketError SendCommand(SocketMessage::CmCommand c);
				SocketError SendMessage(SocketMessage& m);
				SocketError WaitForMessage(SocketMessage* m);
				void CloseClient();
			private:
				HANDLE m_client;
				const std::wstring m_pipe_name;
			};


		}  // namespace communication
	}  // namespace utils
}  // namespace fs_testing
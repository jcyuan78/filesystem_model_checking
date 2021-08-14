#pragma once
/////== scaler 120 column == //////////////////////////////////////////////////////////////////////////////////////////

#include <crashmonkey_comm.h>

//<YUAN>将Socket改为命名管道实现，<TODO>可以优化为
//	原设计中，client为发送端，server为接收端
namespace fs_testing {
	namespace utils {
		namespace communication {
			// Simple class that acts as a server for a socket by receiving and replying to messages.
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
				SocketError WaitingForClient(void);
				void DisconnectClient(void) { DisconnectNamedPipe(m_server); }
				void CloseClient();
				void CloseServer();
				HANDLE GetServer(void) const { return m_server; }
			private:
				HANDLE m_server;
				const std::wstring m_pipe_name;
			};

		}  // namespace communication
	}  // namespace utils
}  // namespace fs_testing
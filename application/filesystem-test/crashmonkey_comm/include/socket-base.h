#pragma once

#include "SocketUtils.h"

//<YUAN>将Socket改为命名管道实现，<TODO>可以优化为
//	原设计中，client为发送端，server为接收端
namespace fs_testing {
	namespace utils {
		namespace communication {
			class BaseSocket
			{
			public:
				static int ReadMessageFromSocket(HANDLE socket, SocketMessage* data);
				static int WriteMessageToSocket(HANDLE socket, SocketMessage& data);

			private:
				static int GobbleData(HANDLE socket, unsigned int len);
				static int ReadIntFromSocket(HANDLE socket, int* data);
				static int WriteIntToSocket(HANDLE socket, int data);
				static int ReadStringFromSocket(HANDLE socket, unsigned int len, std::wstring& data);
				static int WriteStringToSocket(HANDLE socket, const std::wstring& data);
			};
		}
	}
}
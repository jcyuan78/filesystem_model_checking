#ifndef USER_TOOLS_API_ACTIONS_H
#define USER_TOOLS_API_ACTIONS_H

namespace fs_testing {
namespace user_tools {
namespace api {
	//<YUAN>参数中增加一个附加信息cmt，用于调试时核对checkpoint的位置
int Checkpoint(const wchar_t* cmt = NULL);

} // fs_testing
} // user_tools
} // api

#endif // USER_TOOLS_API_ACTIONS_H

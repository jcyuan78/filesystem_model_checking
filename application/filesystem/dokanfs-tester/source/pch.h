// 入门提示: 
//   1. 使用解决方案资源管理器窗口添加/管理文件
//   2. 使用团队资源管理器窗口连接到源代码管理
//   3. 使用输出窗口查看生成输出和其他消息
//   4. 使用错误列表窗口查看错误
//   5. 转到“项目”>“添加新项”以创建新的代码文件，或转到“项目”>“添加现有项”以将现有代码文件添加到项目
//   6. 将来，若要再次打开此项目，请转到“文件”>“打开”>“项目”并选择 .sln 文件

#ifndef PCH_H
#define PCH_H

// TODO: 添加要在此处预编译的标头
#define WIN32_NO_STATUS
//#include "targetver.h"

#include <stdio.h>
#include <tchar.h>

// TODO: 在此处引用程序需要的其他头文件
// TODO: reference additional headers your program requires here
//#ifdef _DEBUG
#define LOG_OUT_CLASS_SIZE
#define LOGGER_LEVEL LOGGER_LEVEL_DEBUGINFO
//#else
//#define LOGGER_LEVEL	LOGGER_LEVEL_NOTICE
//#endif


#include <stdext.h>

#undef WIN32_NO_STATUS

#endif //PCH_H

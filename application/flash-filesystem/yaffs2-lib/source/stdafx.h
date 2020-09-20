// stdafx.h: 标准系统包含文件的包含文件，
// 或是经常使用但不常更改的
// 项目特定的包含文件
//

#pragma once

#include "targetver.h"

#define WIN32_LEAN_AND_MEAN             // 从 Windows 头文件中排除极少使用的内容
#define WIN32_NO_STATUS
// Windows 头文件
//#include <sdkddkver.h>

#include <windows.h>

#include <stdio.h>
#include <tchar.h>



// 在此处引用程序需要的其他标头
//#ifdef _DEBUG
#define LOG_OUT_CLASS_SIZE
#define LOGGER_LEVEL LOGGER_LEVEL_DEBUGINFO
//#else
//#define LOGGER_LEVEL	LOGGER_LEVEL_NOTICE
//#endif
#include <stdext.h>

#undef WIN32_NO_STATUS

// stdafx.h : 标准系统包含文件的包含文件，
// 或是经常使用但不常更改的
// 特定于项目的包含文件

#pragma once

#define WIN32_LEAN_AND_MEAN             // 从 Windows 头文件中排除极少使用的内容
#define WIN32_NO_STATUS


#include <stdio.h>
#include <tchar.h>

//#ifdef _DEBUG
#define LOG_OUT_CLASS_SIZE
#define LOGGER_LEVEL LOGGER_LEVEL_DEBUGINFO
//#else
//#define LOGGER_LEVEL	LOGGER_LEVEL_NOTICE
//#endif


#include <stdext.h>
#include <jcapp.h>
#include <vector>
#include <set>
#include <ctime>
#include <iostream>

#include <WbemIdl.h>
//#include <vector>
//#include <winioctl.h>



#pragma once
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

//#define BITS_PER_BYTE	(8)

//#define _LINUX_PACKED	__attribute__((packed))	
#define _LINUX_PACKED
#define ANDROID_WINDOWS_HOST

#define F2FS_MAJOR_VERSION	(0x100)
#define F2FS_MINOR_VERSION	(0x101)
#define PATH_MAX			(MAX_PATH)
//typedef unsigned char UINT8;
//typedef unsigned short UINT16;
//typedef unsigned int UINT32;
//
//typedef short INT16;
//typedef int INT32;

//typedef UINT64 loff_t;
typedef UINT32 u32;


template <typename T, size_t n> inline size_t ARRAY_SIZE(const T(&t)[n]) { return n;}

#define READ 0
#define WRITE 1

// 一次读写的最大block数量
#define MAX_IO_BLOCKS	64






// ==== config ====
#define CONFIG_UNICODE
//#define CONFIG_F2FS_FS_XATTR
//#define CONFIG_F2FS_FS_COMPRESSION
//#define CONFIG_F2FS_CHECK_FS

//<YUAN> 有效：则在前台运行write check point，否则后台运行check point
//#define CONFIG_SYNC_CHECKPT

// ==== for debug ====
//#define DEBUG_INODE

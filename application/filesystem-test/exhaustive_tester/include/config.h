///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
#pragma once

// 文件系统规模
//#define INDEX_TABLE_SIZE			128

#define BLOCK_PER_SEG			(32)			// 一个segment有多少块
#define BITMAP_SIZE				(2)				// 512 blocks / 32 bit
#define SEG_NUM					(128)
#define MAIN_SEG_OFFSET			(10)
#define MAIN_SEG_NR				(SEG_NUM - MAIN_SEG_OFFSET)

// 每个SIT block拥有的segment数量
#define SIT_ENTRY_PER_BLK		(12)		// 每个SIT block存放 12 个segment entry
#define SIT_BLK_NR				(10)		// 总共10个SIT block(120个main segment)
#define SIT_START_BLK			(1)
#define SSA_START_BLK			(NAT_START_BLK + NAT_BLK_NR)
#define SUMMARY_PER_BLK			(BLOCK_PER_SEG)		// 
#define SSA_BLK_NUM				(MAIN_SEG_NR)		//
#define GC_THRESHOLD_LO			(5)
#define GC_THRESHOLD_HI			(15)

// NAT
#define NAT_START_BLK			(SIT_START_BLK + SIT_BLK_NR)
#define NAT_ENTRY_PER_BLK		(32)			// 每个NAT block的entry数量
#define NAT_BLK_NR				(8)				// NAT block的数量
#define NODE_NR					(256)			// inode和index node的数量

// SSD 空间
#define TOTAL_BLOCK_NR			(SEG_NUM * BLOCK_PER_SEG)

// 文件系统内存配置// OS 缓存配置
#define MAX_PAGE_NUM			(2048)
//#define DATA_BUFFER_SIZE		(1024)	// data buffer大小，block/page数
#define BLOCK_BUF_SIZE			(2048)	// 暂时使用最大值，使用过程总不需要交换，实际可以缩小
#define MAX_OPEN_FILE			(8)		// 最多同时打开的文件数量

// nid, index node配置
#define MAX_INDEX_LEVEL			(3)				// index node 层次
#define INDEX_SIZE				(32)			// inode中，包含index block的数量
#define INDEX_TABLE_SIZE		(32)			// index block中，包含index/data block的数量
#define MAX_FILE_BLKS			(INDEX_SIZE * INDEX_TABLE_SIZE)		// 一个文件的最大长度，block单位.

// Dentry 配置
#define DENTRY_PER_BLOCK		(4)				// 每个block的dentry数量
#define FN_SLOT_LEN				(2)				// 每个slot的文件名大小
#define MAX_FILENAME_LEN		(DENTRY_PER_BLOCK * FN_SLOT_LEN)		// 文件名的最大长度
#define MAX_DENTRY_LEVEL		(4)


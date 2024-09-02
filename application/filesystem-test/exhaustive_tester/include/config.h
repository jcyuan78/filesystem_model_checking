///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
#pragma once

// �ļ�ϵͳ��ģ
//#define INDEX_TABLE_SIZE			128

#define BLOCK_PER_SEG			(32)			// һ��segment�ж��ٿ�
#define BITMAP_SIZE				(2)				// 512 blocks / 32 bit
#define SEG_NUM					(128)
#define MAIN_SEG_OFFSET			(10)
#define MAIN_SEG_NR				(SEG_NUM - MAIN_SEG_OFFSET)

// ÿ��SIT blockӵ�е�segment����
#define SIT_ENTRY_PER_BLK		(12)		// ÿ��SIT block��� 12 ��segment entry
#define SIT_BLK_NR				(10)		// �ܹ�10��SIT block(120��main segment)
#define SIT_START_BLK			(1)
#define SSA_START_BLK			(NAT_START_BLK + NAT_BLK_NR)
#define SUMMARY_PER_BLK			(BLOCK_PER_SEG)		// 
#define SSA_BLK_NUM				(MAIN_SEG_NR)		//
#define GC_THRESHOLD_LO			(5)
#define GC_THRESHOLD_HI			(15)

// NAT
#define NAT_START_BLK			(SIT_START_BLK + SIT_BLK_NR)
#define NAT_ENTRY_PER_BLK		(32)			// ÿ��NAT block��entry����
#define NAT_BLK_NR				(8)				// NAT block������
#define NODE_NR					(256)			// inode��index node������

// SSD �ռ�
#define TOTAL_BLOCK_NR			(SEG_NUM * BLOCK_PER_SEG)

// �ļ�ϵͳ�ڴ�����// OS ��������
#define MAX_PAGE_NUM			(2048)
//#define DATA_BUFFER_SIZE		(1024)	// data buffer��С��block/page��
#define BLOCK_BUF_SIZE			(2048)	// ��ʱʹ�����ֵ��ʹ�ù����ܲ���Ҫ������ʵ�ʿ�����С
#define MAX_OPEN_FILE			(8)		// ���ͬʱ�򿪵��ļ�����

// nid, index node����
#define MAX_INDEX_LEVEL			(3)				// index node ���
#define INDEX_SIZE				(32)			// inode�У�����index block������
#define INDEX_TABLE_SIZE		(32)			// index block�У�����index/data block������
#define MAX_FILE_BLKS			(INDEX_SIZE * INDEX_TABLE_SIZE)		// һ���ļ�����󳤶ȣ�block��λ.

// Dentry ����
#define DENTRY_PER_BLOCK		(4)				// ÿ��block��dentry����
#define FN_SLOT_LEN				(2)				// ÿ��slot���ļ�����С
#define MAX_FILENAME_LEN		(DENTRY_PER_BLOCK * FN_SLOT_LEN)		// �ļ�������󳤶�
#define MAX_DENTRY_LEVEL		(4)


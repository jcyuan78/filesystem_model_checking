#pragma once

#include "yaffs_define.h"
#include "tags_handler.h"
#include "yaffs_obj.h"

class CBlockManager;

/* Pseudo object ids for checkpointing */
#define YAFFS_OBJECTID_CHECKPOINT_DATA	0x20
#define YAFFS_SEQUENCE_CHECKPOINT_DATA	0x21
#define YAFFS_CHECKPOINT_VERSION	8

/* Give us a  Y=0x59, Give us an A=0x41, Give us an FF=0xff, Give us an S=0x53 And what have we got... */
#define YAFFS_MAGIC			0x5941ff53



struct yaffs_checkpt_chunk_hdr
{
	int version;
	int seq;
	UINT32 sum;
	UINT32 xor_;
};

struct yaffs_checkpt_validity
{
	int struct_type;
	UINT32 magic;
	UINT32 version;
	UINT32 head;
};


// The CheckpointDevice structure holds the device information that changes 
//	at runtime and must be preserved over unmount/mount cycles.
struct yaffs_checkpt_dev
{
	int struct_type;
	int n_erased_blocks;
	int alloc_block;		/* Current block being allocated off */
	u32 alloc_page;
	int n_free_chunks;
	int n_deleted_files;	/* Count of files awaiting deletion; */
	int n_unlinked_files;	/* Count of unlinked files. */
	int n_bg_deletions;		/* Count of background deletions. */
	unsigned seq_number;	/* Sequence number of currently allocating block */
};



class CYaffsCheckPoint
{
public:
	CYaffsCheckPoint(ITagsHandler * tagger, CBlockManager * block_manager,
		UINT32 bytes_per_chunk, UINT32 data_bytes_per_chunk, UINT chunk_per_block);
	~CYaffsCheckPoint(void);

public:
	//<MIGRATE> 	yaffs2_checkpt_open
	bool Open(bool writing);
	//<MIGRATE> 	yaffs_checkpt_close
	bool Close(void);


	//<MIGRATE> 	yaffs2_rd_checkpt_validity_marker
	bool ReadValidityMarker(int head);

	//	ok = yaffs2_wr_checkpt_validity_marker(dev, 1);
	bool WriteValidityMarker(int header);

	//<MIGRATE> 	yaffs2_rd_checkpt_sum
	bool ReadSum(void);
	//<MIGRATE> 	yaffs2_rd_checkpt_sum
	bool WriteSum(void);

	UINT32 CalcCheckptBlockRequired(UINT32 obj_num, UINT32 tnode_size);

	bool Erase(void);


	void Invalidate(void);

	//<MIGRATE> yaffs_yaffs2.c : yaffs_checkpoint_save()
	bool CheckpointSave(void);
	//<MIGRATE> yaffs_checkptrw.c/yaffs_checkpt_erase()
//	bool CheckptErase(void);
	//<MIGRATE> yaffs_yaffs2.c : int yaffs2_checkpt_restore(struct yaffs_dev *dev)
	bool CheckptRestore(void);
	//<MIGRATE> yaffs_yaffs2.c: yaffs_rd_checkpt_data()
	bool ReadCheckptData(void);
	//<MIGRATE> 	yaffs2_checkpt_space_ok
	//bool SpaceOk(void);

	inline bool IsCheckpointed(void) const { return m_is_checkpointed; }

protected:
	//<MIGRATE>		yaffs2_checkpt_init_chunk_hdr(dev);
	void InitChunkHeader(void);
	//<MIGRATE> yaffs2_checkpt_flush_buffer
	bool FlushBuffer(void);
	//	yaffs2_checkpt_find_erased_block(dev);
	void FindErasedBlock(void);

	//<MIGRATE> 				yaffs2_checkpt_find_block(dev);
	// 扫描所有block，读取第0 page，检查tag是否为checkpt。如果是，则复制到checkpt
	void FindBlock(void);

	//<MIGRATE> 		yaffs2_checkpt_check_chunk_hdr(dev)
	// 检查checkpt_buffer中头部的相关参数，并且offset移动到头部后
	bool CheckChunkHeader(BYTE * buffer);


	//<MIGRATE>	yaffs_checkpt_dev_to_dev(dev, &cp);
	//	从yaffs_checkpt_dev中读取，并且保存到m_dev中
	void CheckptDevToDev(yaffs_checkpt_dev * cp);

	//<MIGRATE> yaffs2_checkpt_obj_to_obj()
	bool CheckptObjToObj(CYaffsObject * obj, yaffs_checkpt_obj * cp);





	//<MIGRATE> yaffs_calc_checkpt_blocks_required(dev);
	int CalcCheckptBlocksRequired(void);

	//<MIGRATE> yaffs2_checkpt_required
	bool CheckptRequired(void);
	//<MIGRATE>		yaffs2_wr_checkpt_data(dev);
	bool WriteCheckptData(void);

	//	ok = yaffs2_wr_checkpt_validity_marker(dev, 1);
	bool WriteCheckptValidityMarker(int header);
	//	ok = yaffs2_wr_checkpt_dev(dev);
	bool WriteCheckptDev(void);
	//	ok = yaffs2_wr_checkpt_objs(dev);
	bool WriteCheckptObjs(void);
	//	ok = yaffs2_wr_checkpt_sum(dev);
	bool WriteCheckptSum(void);



public:

protected:
	//	static void yaffs2_dev_to_checkpt_dev()
	void DevToCheckptDev(struct yaffs_checkpt_dev *cp);


public:
	//<MIGRATE> 	yaffs2_checkpt_rd
	// 从checkpt_buffer中读取指定数量(size)的字节到cp，如果buffer中的数据耗尽，则从nand中读取。
	//	从nand中读取的方法：搜索所有的block的page 0，找到存放check point的block。从page 0开始按顺序读取。
	size_t Read(void * cp, size_t size);

	template <class T>
	bool TypedRead(T * cp)
	{
		size_t read = Read(cp, sizeof(T));
		if (read != sizeof(T))
		{
			LOG_ERROR(L"[err] read size (%d) is not expected (%d)", read, sizeof(T));
			return false;
		}
		return true;
	}

	//int yaffs2_checkpt_wr(struct yaffs_dev *dev, const void *data, int n_bytes)
	size_t Write(const void * data, size_t n_bytes);
	template <class T>
	bool TypedWrite(T * cp)
	{
		size_t written = Write(cp, sizeof(T));
		if (written != sizeof(T))
		{
			LOG_ERROR(L"[err] write size (%d) is not expected (%d)", written, sizeof(T));
			return false;
		}
		return true;
	}

protected:
	/* Runtime checkpointing stuff */
	int checkpt_page_seq;	/* running sequence number of checkpt pages */
	int checkpt_next_block;

protected:
	bool m_open_write;
	bool m_is_checkpointed;
	UINT32 m_bytes_per_chunk;
	UINT32 m_data_bytes_per_chunk;
	UINT32 m_chunks_per_block;
	UINT32 m_page_seq;

	UINT32 m_byte_offs;
	BYTE *m_buffer;

	UINT32 m_max_blocks;			// 分配给Checkpoint的最大block数量
	UINT32 m_blocks_in_checkpt;		// 正在使用的checkpoint的block数量
	UINT32 * m_block_list;
	UINT32 m_next_block;

	UINT32 m_cur_chunk;
	UINT32 m_cur_block;

	UINT32 m_sum;
	UINT32 m_xor;

	ITagsHandler * m_tagger;
	int m_byte_count;

	CBlockManager * m_block_manager;
};
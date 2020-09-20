#pragma once

#include "yaffs_obj.h"
#include "checkpoint.h"

class CYaffsFile : public CYaffsObject
{
public:
	friend class CYaffsObject;
	CYaffsFile(void);
	~CYaffsFile(void);

	//-- Interface of IFileInfo
public:
	virtual bool DokanReadFile(LPVOID buf, DWORD len, DWORD & read, LONGLONG offset);
//<MIGRATE> yaffsfs.c  yaffsfs_do_write()

	virtual bool DokanWriteFile(const void * buf, DWORD len, DWORD & written, LONGLONG offset);
	//<MIGRATE> yaffs_guts.c : yaffs_resize_file()
		//void ResizeFile(size_t new_size) {};
	virtual bool SetAllocationSize(LONGLONG size);
	virtual bool SetEndOfFile(LONGLONG size);
	virtual void Cleanup(void) {};
	virtual void CloseFile(void);
	virtual bool FlushFile(void);
	virtual bool GetFileInformation(LPBY_HANDLE_FILE_INFORMATION fileinfo) const;


protected:
	virtual bool DelObj(void);
	virtual bool IsEmpty(void) { JCASSERT(0);  return false; };
	//virtual void FreeTnode(void);

	//<MIGRATE> yaffs_guts.c : int yaffs_unlink_file_if_needed()
	bool UnlinkFileIfNeeded(void);
	//<MIGRATE> yaffs_guts.c : int yaffs_soft_del_file()
	void SoftDelFile(void);

	//<MIGRATE> yaffs_yaffs2.c : int yaffs2_handle_hole(struct yaffs_obj *obj, loff_t new_size)
	bool HandleHole(loff_t new_size);
	//<MIGRATE> yaffs_resize_file_down()
	void ResizeFileDown(loff_t new_size);
	//<MIGRATE> yaffs_guts.c : yaffs_do_file_wr()
	size_t DoFileWrite(const BYTE * buf, loff_t offset, size_t len, bool write_through);
	virtual bool SetObjectByHeaderTag(CYafFs * fs, UINT32 chunk, yaffs_obj_hdr * oh, yaffs_ext_tags & tags);


public:
	//<MIGRATE> yaffs_guts.c: yaffs_wr_data_obj()
		// use_reserver: 当需要申请新的block时， 是否使用保留的block
	size_t WriteDataObject(int chunk, const BYTE* buf, size_t len, bool use_reserve);
	//<MIGRATE> yaffs_put_chunk_in_file
	bool PutChunkInFile(int chunk, int nand_chunk, int in_scan);
	loff_t GetShrinkSize(void) const { return m_shrink_size; };
	//void SetShrinkSize(loff_t size) { m_shrink_size = size; }
	loff_t GetStoredSize(void) const { return m_stored_size; }
	void SetStoredSize(loff_t size) 
	{
		m_stored_size = size;
		m_file_size = size;
	}

protected:
	//<MIGRATE> yaffs_rd_data_obj()
	bool ReadDataObject(int chunk, BYTE *buf);
	//<MIGRATE> yaffs_file_rd(obj, buf, pos, nToRead);
	size_t FileRead(BYTE * buf, loff_t pos, loff_t size);
	//<MIGRATE> yaffs_verify_file_sane();
	bool VerifyFileSane(void) { return false; }


protected:
	//<MIGRATE> yaffs_prune_chunks()
		// 回收多余的chunk
	void PruneChunks(loff_t new_size);
	//<MIGRATE> yaffs_find_del_file_chunk
	int FindDelFileChunk(int chunk, yaffs_ext_tags * tags);
	//<MIGRATE> yaffs_prune_tree()
	bool PruneTree(void);
	//<MIGRATE> yaffs_prune_worker
	yaffs_tnode * PruneWorker(yaffs_tnode * tn, UINT32 level, bool del0);
	//<MIGRATE> yaffs_find_tnode_0
	yaffs_tnode * FindTnode0(int chunk);

	//这两个函数是一对，分别从tnode的制定位置获取值和设置值
	//<MIGRATE> yaffs_guts.c: yaffs_get_group_base()
	int GetGroupBase(yaffs_tnode * tn, UINT32 chunk);
	//<MIGRATE> yaffs_load_tnode_0()
	void LoadTnode0(yaffs_tnode * tn, int pos, int val);
public:
	//<MIGRATE> yaffs_add_find_tnode_0
	yaffs_tnode* AddFindTnode0(int base_chunk, yaffs_tnode* tn);
	//<MIGRATE> yaffs_guts.c: yaffs_find_chunk_in_group()
	int FindChunkInGroup(int chunk, yaffs_ext_tags * tags, int id, int inode_chunk);
	//<MIGRATE> yaffs_tags_match()
	inline bool TagsMatch(yaffs_ext_tags* tags, int obj_id, int chunk_obj)
	{
		return (tags->chunk_id == (u32)chunk_obj
			&& tags->obj_id == (u32)obj_id);
			//&& !tags->is_deleted);
	}

	virtual void LoadObjectHeader(yaffs_obj_hdr * oh);

	//<MIGRATE> yaffs_find_chunk_in_file()
		// 从文件的相对chunk查找physical chunk（mapping), 返回 <0 表示未找到
	int FindChunkInFile(int inode, yaffs_ext_tags * tags);
	virtual void LoadObjectFromCheckpt(yaffs_checkpt_obj * cp);
	//	static void yaffs2_obj_checkpt_obj()
	virtual void ObjToCheckptObj(struct yaffs_checkpt_obj *cp)
	{
		__super::ObjToCheckptObj(cp);
		cp->size_or_equiv_obj = m_file_size;
		cp->n_data_chunks = m_data_chunks;
	}
	//			static int yaffs2_checkpt_tnode_worker()
	bool CheckptTnodeWorker(CYaffsCheckPoint & checkpt, struct yaffs_tnode *tn, u32 level, int chunk_offset);
	//			static int yaffs2_wr_checkpt_tnodes(struct yaffs_obj *obj)
	bool WriteCheckptTnodes(CYaffsCheckPoint & checkpt);
//
	void UpdateChunk(int chunk, const yaffs_ext_tags & tags);
	// 这个函数是CYaffs::GcProcessChunk的一部分。将文件中的chunk搬到新chunk位置。用于GC。返回new chunk
	virtual int RefreshChunk(int old_chunk, yaffs_ext_tags & tags, BYTE * buffer);
	//int ReduceDataChunk(void) { return (--m_obj.n_data_chunks); }
//<MIGRATE> yaffs_oh_size_load()
	void ObjHdrSizeLoad(yaffs_obj_hdr * oh, loff_t fsize)
	{
		oh->file_size_low = LODWORD(fsize);
		oh->file_size_high = HIDWORD(fsize);
	}

protected:
	inline static int IndexInLevel(int chunk, int level) {
		return ((chunk >> (YAFFS_TNODES_LEVEL0_BITS + (level - 1) * YAFFS_TNODES_INTERNAL_BITS)) &
			YAFFS_TNODES_INTERNAL_MASK);
	}

protected:
	UINT32 m_data_chunks;	// 占用chunk的数量
	loff_t m_file_size;
	loff_t m_stored_size;
	loff_t m_shrink_size;
	int m_top_level;
	struct yaffs_tnode *m_top;
};

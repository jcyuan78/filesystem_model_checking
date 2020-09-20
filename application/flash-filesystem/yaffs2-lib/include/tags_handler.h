#pragma once
//#include "yaffs_driver.h"

#include <dokanfs-lib.h>
#include "yaffs_define.h"

class CYafFs;

class ITagsHandler : public IJCInterface
{
public:
	// (*write_chunk_tags_fn)
	virtual int WriteChunkTags(int nand_chunk, const BYTE *data, const yaffs_ext_tags *tags) = 0;
	// (*read_chunk_tags_fn) 
	virtual bool ReadChunkTags(int nand_chunk, BYTE* data, yaffs_ext_tags *tags) = 0;
	// (*query_block_fn)
	virtual bool QueryBlock(int block_no, yaffs_block_state &state, UINT32 & seq_number) = 0;
	// (*mark_bad_fn)
	virtual bool MarkBad(int block_no) = 0;
	virtual void SetFileSystem(CYafFs *fs, INandDriver * nand) = 0;
};


class CTagsHandler : public ITagsHandler
{
public:
	CTagsHandler();
	virtual ~CTagsHandler();
public:
	// (*write_chunk_tags_fn)
	virtual int WriteChunkTags(int nand_chunk, const BYTE *data, const yaffs_ext_tags *tags);
	// (*read_chunk_tags_fn) 
	virtual bool ReadChunkTags(int nand_chunk, BYTE *data, yaffs_ext_tags *tags);
	// (*query_block_fn)
	virtual bool QueryBlock(int block_no, yaffs_block_state &state, UINT32 & seq_number);
	// (*mark_bad_fn)
	virtual bool MarkBad(int block_no);

	virtual void SetFileSystem(CYafFs *fs, INandDriver*) {	m_fs = fs;	}

protected:
//<MIGRATE> yaffs_tagscompat.c : static void yaffs_get_tags_from_spare()
	void GetTagsFromSpare(yaffs_spare * spare_ptr, yaffs_tags * tags_ptr);
	void LoadTagsToSpare(yaffs_spare * spare_ptr, yaffs_tags * tags_ptr);
	CYafFs *m_fs;
};

class CMarshallTags : public ITagsHandler
{
public:
	CMarshallTags(void);
	~CMarshallTags(void);

public:
	// (*write_chunk_tags_fn)
	virtual int WriteChunkTags(int nand_chunk, const BYTE *data, const yaffs_ext_tags *tags);
	// (*read_chunk_tags_fn) 
	virtual bool ReadChunkTags(int nand_chunk, BYTE *data, yaffs_ext_tags *tags);
	// (*query_block_fn)
	virtual bool QueryBlock(int block_no, yaffs_block_state &state, UINT32 & seq_number);
	// (*mark_bad_fn)
	virtual bool MarkBad(int block_no);

	virtual void SetFileSystem(CYafFs *fs, INandDriver * nand);

//protected:
//	void PackTags2(yaffs_packed_tags2 *ptt, const yaffs_ext_tags *t);
//	void UnpackTag2(yaffs_ext_tags *t, yaffs_packed_tags2 *pt);


protected:
	CYafFs *m_fs;
	INandDriver * m_nand;
	UINT32 m_page_size, m_chunk_per_block;
};


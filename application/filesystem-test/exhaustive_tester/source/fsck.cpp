///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
#include "pch.h"
#include "../include/f2fs_simulator.h"
#include <boost/unordered_set.hpp>

LOCAL_LOGGER_ENABLE(L"simulator.f2fs", LOGGER_LEVEL_DEBUGINFO);

struct F2FS_FSCK {
	UINT sit_valid_seg;
	UINT sit_valid_blk;
	UINT nat_valid_node;

	DWORD main_area_map[SEG_NUM * BLOCK_PER_SEG / 32];
	DWORD sit_area_map[SEG_NUM * BLOCK_PER_SEG / 32];
	DWORD nat_area_map[NODE_NR / 32];
	DWORD node_map[NODE_NR / 32];
	char path[MAX_PATH_SIZE];			// 用于跟踪目前检查的文件名
	char* path_ptr;						// 指向path的末尾

	bool fixed;
	bool need_to_fix;
};

int CF2fsSimulator::fsck_init(F2FS_FSCK* fsck)
{
	memset(fsck, 0, sizeof(F2FS_FSCK));
	return 0;
}

ERROR_CODE CF2fsSimulator::fsck(bool fix)
{
	// prepare:
	Mount();
	if (m_log_fsck)	{
		LOG_DEBUG(L"[fsck dump after mount]")
		DumpLog(m_log_out, "");
	}

	F2FS_FSCK fsck;
	fsck_init(&fsck);
	fsck.need_to_fix = fix;
	// check current seg
	// check metadata
	fsck_chk_metadata(&fsck);
	// check checkpoint
	// check orphan node
	// check inode from root
	UINT blk_cnt = 0;
	fsck.path[0] = '\\';
	fsck.path_ptr = fsck.path+1;
	fsck_chk_node_blk(&fsck, ROOT_FID, F2FS_FILE_DIR, BLOCK_DATA::BLOCK_INODE, blk_cnt);
	// verify
	fsck_verify(&fsck);
	// fix
	if (fsck.fixed) {
		// clear journal in checkpoint
//		m_segments.SyncSIT();
//		m_nat.Sync();
//		m_segments.SyncSSA();
//		m_segments.reset_dirty_map();
//		m_storage.Sync();
	}
	m_segments.Reset();
	m_nat.Reset();
	m_pages.Reset();

	return ERR_OK;
}

inline DWORD set_bitmap(DWORD* bmp, UINT offset)
{
	DWORD index = (offset >> 5);
	JCASSERT(index < SEG_NUM * BLOCK_PER_SEG / 32);
	DWORD mask = (1 << (offset & 0x1F));
	DWORD org = bmp[index];
	bmp[index] |= mask;
	return org;
}

inline DWORD test_bitmap(const DWORD* bmp, UINT offset)
{
	DWORD index = (offset >> 5);
	JCASSERT(index < SEG_NUM * BLOCK_PER_SEG / 32);
	DWORD mask = (1 << (offset & 0x1F));
	return bmp[index] & mask;
}

DWORD CF2fsSimulator::f2fs_test_main_bitmap(F2FS_FSCK* fsck, PHY_BLK blk)
{
	return test_bitmap(fsck->main_area_map, blk);
}

DWORD CF2fsSimulator::f2fs_set_main_bitmap(F2FS_FSCK* fsck, PHY_BLK blk)
{
	return set_bitmap(fsck->main_area_map, blk);
}

DWORD CF2fsSimulator::f2fs_set_nat_bitmap(F2FS_FSCK* fsck, _NID nid)
{
	return set_bitmap(fsck->nat_area_map, nid);
}

DWORD CF2fsSimulator::f2fs_test_node_bitmap(F2FS_FSCK* fsck, _NID nid)
{
	return test_bitmap(fsck->node_map, nid);
}

DWORD CF2fsSimulator::f2fs_set_node_bitmap(F2FS_FSCK* fsck, _NID nid)
{
	return set_bitmap(fsck->node_map, nid);
}

int CF2fsSimulator::fsck_chk_metadata(F2FS_FSCK* fsck)
{
	// check SIT
	SEG_T total_seg_nr = m_segments.get_seg_nr();
	SEG_T valid_seg_nr = 0;
	fsck->sit_valid_blk = 0;
	PHY_BLK phy_blk = 0;
	for (SEG_T ii = 0; ii < total_seg_nr; ++ii)
	{
		const SegmentInfo & seg = m_segments.get_segment(ii);
		BLK_T seg_valid_blk = 0;
//		DWORD mask = 1;
		phy_blk = ii * BLOCK_PER_SEG;
		for (BLK_T bb = 0; bb < BLOCK_PER_SEG; ++bb, ++phy_blk/*, mask <<= 1*/)
		{
			if (CF2fsSegmentManager::test_bitmap(&(seg.valid_bmp), bb) )
			{
				// 原来BLOCK_PER_SEG=32的时候，一个DWORD表示一个segment。当BLOCK_PER_SEG=16时，比例不对。这个方法会overflow
//				fsck->sit_area_map[ii] |= mask;
				set_bitmap(fsck->sit_area_map, phy_blk);
				fsck->sit_valid_blk++;
				seg_valid_blk++;
			}
		}
		if (seg_valid_blk != seg.valid_blk_nr) {
			THROW_FS_ERROR(ERR_SIT_MISMATCH, L"seg [%d] valid block count in sit=%d mismatch valid block count in bitmap %d", 
				ii, seg.valid_blk_nr, seg_valid_blk);
		}
		// 当前segment计算如valid segment；
		if (m_segments.m_cur_segs[seg.seg_temp].seg_no == ii || seg.valid_blk_nr != 0) valid_seg_nr++;
//		if (seg.valid_blk_nr != 0)		valid_seg_nr++;
	}
	if (valid_seg_nr + m_segments.get_free_nr() != total_seg_nr) {
		THROW_FS_ERROR(ERR_SIT_MISMATCH, L"segment number mismatch, total=%d, valid=%d, free=%d", 
			total_seg_nr, valid_seg_nr, m_segments.get_free_nr());
	}
	fsck->sit_valid_seg = valid_seg_nr;

	// check NAT
	UINT valid_node_nr = 0;
	for (_NID nn = 0; nn < NODE_NR; ++nn)
	{
		PHY_BLK blk = m_nat.get_phy_blk(nn);
		if ( is_valid(blk) ) {
			valid_node_nr++;
			f2fs_set_nat_bitmap(fsck, nn);
		}
	}
	fsck->nat_valid_node = valid_node_nr;

	return 0;
}

int CF2fsSimulator::fsck_verify(F2FS_FSCK* fsck)
{
	// 检查nat中有没有未处理的node
	UINT valid_node_nr = 0;
	for (_NID nn = 0; nn < NODE_NR; ++nn)
	{
		PHY_BLK blk = m_nat.get_phy_blk(nn);
		if (is_valid(blk) ) {
			valid_node_nr++;
			if (f2fs_test_node_bitmap(fsck, nn) == 0)
			{	// 有未处理的node，
				if (fsck->need_to_fix) {	// <TODO> 要作为orphan inode处理
					LOG_ERROR(L"nid [%d] allocated but not used, blk=%d.", nn, blk);
					m_nat.put_node(nn);
//					m_segments.InvalidBlock(blk);
					fsck->fixed = true;
				}
				else THROW_FS_ERROR(ERR_DEAD_NID, L"nid [%d] allocated but not used, blk=%d.", nn, blk);
			}
		}
	}

	// 检查block中有没有未处理的
	SEG_T total_seg_nr = m_segments.get_seg_nr();
	for (SEG_T ii = 0; ii < total_seg_nr; ++ii)
	{
		if (m_segments.get_valid_blk_nr(ii) == 0) continue;
		DWORD mask = 1;
		for (BLK_T bb = 0; bb < BLOCK_PER_SEG; ++bb, mask <<= 1)
		{
			PHY_BLK blk = CF2fsSegmentManager::PhyBlock(ii, bb);
			if (m_segments.is_blk_valid(ii, bb) && f2fs_test_main_bitmap(fsck, blk) == 0) {
				if (fsck->need_to_fix) {
					m_segments.InvalidBlock(ii, bb);
					fsck->fixed = true;
				}
				else {
					THROW_FS_ERROR(ERR_DEAD_BLK, L"phy block [%d, %d] phy=%d, allocated but not used.",	ii, bb, blk);
				}
			}
		}
	}
	return 0;
}


int CF2fsSimulator::fsck_chk_data_blk(F2FS_FSCK* fsck, _NID nid, WORD offset, PHY_BLK blk, F2FS_FILE_TYPE file_type)
{
	if (blk >= MAIN_BLK_NR) {
		THROW_FS_ERROR(ERR_INVALID_BLK, L"data [%d, %d], phy block addr=%d is invalid", nid, offset, blk);
	}

	// check block assignment
	if (f2fs_test_main_bitmap(fsck, blk) != 0) {
		THROW_FS_ERROR(ERR_DOUBLED_BLK, L"block %d is double assigned", blk);
	}
	f2fs_set_main_bitmap(fsck, blk);

	// check SIT,  SSA: 在此模型中，没有SIT bitmap。SIT bitmap通过SSA实现。
	_NID ssa_nid;
	WORD ssa_offset;
	m_segments.GetBlockInfo(ssa_nid, ssa_offset, blk);
	if ((ssa_nid != nid) || (ssa_offset != offset)) {
		if (fsck->need_to_fix) {
			LOG_ERROR(L"data [%d, %d], phy=%d in ssa does not match, ssa.nid=%d, ssa.offset=%d",
				nid, offset, blk, ssa_nid, ssa_offset);
			m_segments.SetBlockInfo(nid, offset, blk);
			fsck->fixed = true;
		}
		else {
			THROW_FS_ERROR(ERR_LOST_BLK, L"data [%d, %d], phy=%d in ssa does not match, ssa.nid=%d, ssa.offset=%d",
				nid, offset, blk, ssa_nid, ssa_offset);
		}
	}

	if (file_type != F2FS_FILE_DIR) return 0;
	// check dentry read block
	CPageInfo* page = m_pages.allocate(true);
	m_storage.BlockRead(CF2fsSegmentManager::phyblk_to_lba(blk), page);
	BLOCK_DATA* block = m_pages.get_data(page);
	if (block->m_type != BLOCK_DATA::BLOCK_DENTRY) {
		m_pages.free(page);
		THROW_FS_ERROR(ERR_WRONG_BLOCK_TYPE, L"data [%d, %d], phy=%d block type (%d) unmatch, expected: BLOCK_DENTRY",
			nid, offset, blk, block->m_type);
	}
	DENTRY_BLOCK& entries = block->dentry;
	for (int ii = 0; ii < DENTRY_PER_BLOCK; ++ii) {
		if (is_invalid(entries.dentries[ii].ino)) continue;
		UINT blk_cnt = 0;
		memcpy_s(fsck->path_ptr, MAX_PATH_SIZE - (fsck->path_ptr-fsck->path), entries.filenames[ii], entries.dentries[ii].name_len);
		fsck->path_ptr += entries.dentries[ii].name_len;
//		fsck->path_ptr[0] = '\\';
		fsck->path_ptr[0] = 0;
//		fsck->path_ptr++;
		fsck_chk_node_blk(fsck, entries.dentries[ii].ino, F2FS_FILE_UNKNOWN, BLOCK_DATA::BLOCK_INODE, blk_cnt);
	}
	m_pages.free(page);
	return 0;
}


int CF2fsSimulator::fsck_chk_node_blk(F2FS_FSCK* fsck, _NID nid, F2FS_FILE_TYPE file_type, BLOCK_DATA::BLOCK_TYPE block_type, UINT &blk_cnt)
{
	if (nid >= NODE_NR) THROW_FS_ERROR(ERR_INVALID_NID, L"nid [%d] is invalid", nid);

	if (f2fs_test_node_bitmap(fsck, nid) != 0) {
		THROW_FS_ERROR(ERR_DOUBLED_NID, L"nid [%d] is double assigned", nid);
	}
	f2fs_set_node_bitmap(fsck, nid);

	PHY_BLK blk = m_nat.get_phy_blk(nid);
	if (blk >= MAIN_BLK_NR) {
		THROW_FS_ERROR(ERR_LOST_NID, L"nid [%d], not allocated or invalid phy block: %d", nid, blk);
	}
	// check block assignment
	if (f2fs_test_main_bitmap(fsck, blk) != 0) {
		THROW_FS_ERROR(ERR_DOUBLED_BLK, L"block %d is double assigned", blk);
	}
	// 检查node与SIT的bitmap是否匹配，
	SEG_T seg_no;
	BLK_T blk_no;
	CF2fsSegmentManager::BlockToSeg(seg_no, blk_no, blk);

	const SegmentInfo& seg = m_segments.get_segment(seg_no);
	if (test_bitmap(&(seg.valid_bmp), blk_no) == 0) {
		THROW_FS_ERROR(ERR_SIT_MISMATCH, L"nid [%d] phy_blk=%d, is not valid in SIT[%d, %d]", nid, blk, seg_no, blk_no);
	}

	_NID ssa_nid;
	WORD ssa_offset;
	m_segments.GetBlockInfo(ssa_nid, ssa_offset, blk);
	if ((ssa_nid != nid) || (is_valid(ssa_offset)) ) {
		if (fsck->need_to_fix) {
			LOG_ERROR(L"nid [%d], block=%d in ssa does not match, ssa.nid=%d, ssa.offset=%d",
				nid, blk, ssa_nid, ssa_offset);
			m_segments.SetBlockInfo(nid, INVALID_BLK, blk);
			fsck->fixed = true;
		}
		else {
			THROW_FS_ERROR(ERR_LOST_BLK, L"nid [%d], block=%d in ssa does not match, ssa.nid=%d, ssa.offset=%d",
				nid, blk, ssa_nid, ssa_offset);
		}
	}
	// fsck.valid_blk_cnt ++
	// fsck.valid_node_cnt ++
	CPageInfo* page = m_pages.allocate(true);
	m_storage.BlockRead(CF2fsSegmentManager::phyblk_to_lba(blk), page);

	BLOCK_DATA* blk_data = m_pages.get_data(page);
	if (blk_data->m_type != block_type) {
		m_pages.free(page);
		THROW_FS_ERROR(ERR_WRONG_BLOCK_TYPE, L"nid [%d] block type (%d) unmatch, expected: %d", 
			nid, blk_data->m_type, block_type);
	}

	NODE_INFO& node_data = blk_data->node;
	if (node_data.m_nid != nid) {
		m_pages.free(page);
		THROW_FS_ERROR(ERR_INVALID_NID, L"nid [%d] does not match, nid in node= %d", nid, node_data.m_nid);
	}

	if (block_type == BLOCK_DATA::BLOCK_INODE) {
		fsck_chk_inode_blk(fsck, nid, blk, file_type, node_data);
	}
	else if (block_type == BLOCK_DATA::BLOCK_INDEX) {
		fsck_chk_index_blk(fsck, nid, blk, file_type, node_data, blk_cnt);
	}
	else {
		m_pages.free(page);
		THROW_FS_ERROR(ERR_WRONG_BLOCK_TYPE, L"node %d, type=%d is neither inode nor index node", nid, block_type);
	}
	m_pages.free(page);

	return 0;
}

int CF2fsSimulator::fsck_chk_inode_blk(F2FS_FSCK* fsck, _NID ino, PHY_BLK blk, F2FS_FILE_TYPE file_type, NODE_INFO & node_data)
{
	// 检查main bitmap，确保无重复 （sanity check中已经检查）。
	f2fs_set_main_bitmap(fsck, blk);
	char* path_ptr = fsck->path_ptr;
	if (node_data.inode.file_type == F2FS_FILE_DIR) {
		fsck->path_ptr[0] = '\\';
		fsck->path_ptr++;
		fsck->path_ptr[0] = 0;
	}
	LOG_DEBUG(L"[fsck] check inode block: nid=%03d, type=%s, fn=%S",
		ino, node_data.inode.file_type == F2FS_FILE_DIR ? L"DIR " : L"FILE", fsck->path);
	LOG_DEBUG(L"\t file_size = %d, block_nr = %d", 
		node_data.inode.file_size, node_data.inode.blk_num);

	file_type = node_data.inode.file_type;
	if (node_data.m_ino != node_data.m_nid) {
		THROW_FS_ERROR(ERR_INVALID_NID, L"nid [%d] inode id (%d) does not match", ino, node_data.m_ino);
	}
	// 检查每个index
	UINT blk_cnt = 0;
	for (int ii = 0; ii < INDEX_TABLE_SIZE; ++ii)
	{
		_NID  index_nid = node_data.inode.index[ii];
		if ( is_valid(index_nid) )
		{
			fsck_chk_node_blk(fsck, index_nid, file_type, BLOCK_DATA::BLOCK_INDEX, blk_cnt);
		}
	}
	if (node_data.inode.blk_num != blk_cnt) {
		THROW_FS_ERROR(ERR_INVALID_NID, L"ino [%d] valid data blk in node (%d) does not match data blk (%d) ", 
			ino, node_data.inode.blk_num, blk_cnt);
	}
	fsck->path_ptr = path_ptr;
	return 0;
}

int CF2fsSimulator::fsck_chk_index_blk(F2FS_FSCK* fsck, _NID nid, PHY_BLK blk, F2FS_FILE_TYPE file_type, NODE_INFO& node_data, UINT &blk_cnt)
{
	// 检查main bitmap，确保无重复 （sanity check中已经检查）。
	f2fs_set_main_bitmap(fsck, blk);

	// 检查买个data block
	UINT valid_index =0;
	for (int ii = 0; ii < INDEX_SIZE; ++ii)
	{
		PHY_BLK data_blk = node_data.index.index[ii];
		char* path_ptr = fsck->path_ptr;
		if ( is_valid(data_blk) ) {
			valid_index++;
			fsck_chk_data_blk(fsck, nid, ii, data_blk, file_type);
		}
		fsck->path_ptr = path_ptr;
	}
	blk_cnt += valid_index;
	if (node_data.index.valid_data != valid_index) {
		THROW_FS_ERROR(ERR_INVALID_NID, L"nid [%d] valid data in node (%d) does not match data number (%d) ",
			nid, node_data.index.valid_data, valid_index);
	}
	return 0;
}


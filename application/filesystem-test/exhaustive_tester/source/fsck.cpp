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

	bool fixed;
	bool need_to_fix;
};

int CF2fsSimulator::fsck_init(F2FS_FSCK* fsck)
{
//	memset(&fsck->main_area_map, 0, sizeof(fsck->main_area_map));
//	memset(&fsck->sit_area_map, 0, sizeof(fsck->sit_area_map));
	memset(fsck, 0, sizeof(F2FS_FSCK));
	return 0;
}

ERROR_CODE CF2fsSimulator::fsck(bool fix)
{
	// prepare:
//	m_nat.Load();
//	m_segments.Load();
	Mount();

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
	fsck_chk_node_blk(&fsck, ROOT_FID, F2FS_FILE_DIR, BLOCK_DATA::BLOCK_INODE, blk_cnt);
	// verify
	fsck_verify(&fsck);
	// fix
	if (fsck.fixed) {
		// clear journal in checkpoint
		m_segments.SyncSIT();
		m_nat.Sync();
		m_segments.SyncSSA();
		m_segments.reset_dirty_map();
		m_storage.Sync();
	}
	m_segments.Reset();
	m_nat.Reset();
	m_pages.Reset();

	return ERR_OK;
}

inline DWORD set_bitmap(DWORD* bmp, UINT offset)
{
	DWORD index = (offset >> 5);
	DWORD mask = (1 << (offset & 0x1F));
	DWORD org = bmp[index];
	bmp[index] |= mask;
	return org;
}

inline DWORD test_bitmap(const DWORD* bmp, UINT offset)
{
	DWORD index = (offset >> 5);
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

DWORD CF2fsSimulator::f2fs_set_nat_bitmap(F2FS_FSCK* fsck, NID nid)
{
	return set_bitmap(fsck->nat_area_map, nid);
}

DWORD CF2fsSimulator::f2fs_test_node_bitmap(F2FS_FSCK* fsck, NID nid)
{
	return test_bitmap(fsck->node_map, nid);
}

DWORD CF2fsSimulator::f2fs_set_node_bitmap(F2FS_FSCK* fsck, NID nid)
{
	return set_bitmap(fsck->node_map, nid);
}

int CF2fsSimulator::fsck_chk_metadata(F2FS_FSCK* fsck)
{
	// check SIT
	SEG_T total_seg_nr = m_segments.get_seg_nr();
	SEG_T valid_seg_nr = 0;
	fsck->sit_valid_blk = 0;
	for (SEG_T ii = 0; ii < total_seg_nr; ++ii)
	{
		const SegmentInfo & seg = m_segments.get_segment(ii);
		BLK_T seg_valid_blk = 0;
		DWORD mask = 1;
		for (BLK_T bb = 0; bb < BLOCK_PER_SEG; ++bb, mask <<= 1)
		{
			if (CF2fsSegmentManager::test_bitmap(seg.valid_bmp, bb) )
			{
				fsck->sit_area_map[ii] |= mask;
				fsck->sit_valid_blk++;
				seg_valid_blk++;
			}
		}
		if (seg_valid_blk != seg.valid_blk_nr) {
			THROW_FS_ERROR(ERR_SIT_MISMATCH, L"seg [%d] valid block count in sit=%d mismatch valid block count in bitmap %d", 
				ii, seg.valid_blk_nr, seg_valid_blk);
		}
		if (seg.valid_blk_nr != 0)		valid_seg_nr++;
	}
	if (valid_seg_nr + m_segments.get_free_nr() != total_seg_nr) {
		THROW_FS_ERROR(ERR_SIT_MISMATCH, L"segment number mismatch, total=%d, valid=%d, free=%d", 
			total_seg_nr, valid_seg_nr, m_segments.get_free_nr());
	}
	fsck->sit_valid_seg = valid_seg_nr;

	// check NAT
	UINT valid_node_nr = 0;
	for (NID nn = 0; nn < NODE_NR; ++nn)
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
	// ���nat����û��δ������node
	UINT valid_node_nr = 0;
	for (NID nn = 0; nn < NODE_NR; ++nn)
	{
		PHY_BLK blk = m_nat.get_phy_blk(nn);
		if (is_valid(blk) ) {
			valid_node_nr++;
			if (f2fs_test_node_bitmap(fsck, nn) == 0)
			{	// ��δ������node��
				if (fsck->need_to_fix) {	// <TODO> Ҫ��Ϊorphan inode����
					LOG_ERROR(L"nid [%d] allocated but not used, blk=%d.", nn, blk);
					m_nat.put_node(nn);
					m_segments.InvalidBlock(blk);
					fsck->fixed = true;
				}
				else THROW_FS_ERROR(ERR_DEAD_NID, L"nid [%d] allocated but not used, blk=%d.", nn, blk);
			}
		}
	}

	// ���block����û��δ������
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


int CF2fsSimulator::fsck_chk_data_blk(F2FS_FSCK* fsck, NID nid, WORD offset, PHY_BLK blk, F2FS_FILE_TYPE file_type)
{
	if (blk >= MAIN_BLK_NR) {
		THROW_FS_ERROR(ERR_INVALID_BLK, L"data [%d, %d], phy block addr=%d is invalid", nid, offset, blk);
	}

	// check block assignment
	if (f2fs_test_main_bitmap(fsck, blk) != 0) {
		THROW_FS_ERROR(ERR_DOUBLED_BLK, L"block %d is double assigned", blk);
	}
	f2fs_set_main_bitmap(fsck, blk);

	// check SIT,  SSA: �ڴ�ģ���У�û��SIT bitmap��SIT bitmapͨ��SSAʵ�֡�
	NID ssa_nid;
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
		if (entries.dentries[ii].ino == INVALID_BLK) continue;
		UINT blk_cnt = 0;
		fsck_chk_node_blk(fsck, entries.dentries[ii].ino, F2FS_FILE_UNKNOWN, BLOCK_DATA::BLOCK_INODE, blk_cnt);
	}
	m_pages.free(page);
	return 0;
}

int CF2fsSimulator::fsck_chk_node_blk(F2FS_FSCK* fsck, NID nid, F2FS_FILE_TYPE file_type, BLOCK_DATA::BLOCK_TYPE block_type, UINT &blk_cnt)
{
	//f2fs_sanity_check_nid();

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
	// ���node��SIT��bitmap�Ƿ�ƥ�䣬
	SEG_T seg_no;
	BLK_T blk_no;
	CF2fsSegmentManager::BlockToSeg(seg_no, blk_no, blk);

	const SegmentInfo& seg = m_segments.get_segment(seg_no);
	if (test_bitmap(seg.valid_bmp, blk_no) == 0) {
		THROW_FS_ERROR(ERR_SIT_MISMATCH, L"nid [%d] phy_blk=%d, is not valid in SIT[%d, %d]", nid, blk, seg_no, blk_no);
	}

	NID ssa_nid;
	WORD ssa_offset;
	m_segments.GetBlockInfo(ssa_nid, ssa_offset, blk);
	if ((ssa_nid != nid) || (is_valid(ssa_offset)) ) {
		if (fsck->need_to_fix) {
			LOG_ERROR(L"nid [%d], block=%d in ssa does not match, ssa.nid=%d, ssa.offset=%d",
				nid, blk, ssa_nid, ssa_offset);
			m_segments.SetBlockInfo(nid, INVALID_FID, blk);
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

int CF2fsSimulator::fsck_chk_inode_blk(F2FS_FSCK* fsck, NID ino, PHY_BLK blk, F2FS_FILE_TYPE file_type, NODE_INFO & node_data)
{
	// ���main bitmap��ȷ�����ظ� ��sanity check���Ѿ���飩��
	f2fs_set_main_bitmap(fsck, blk);

	file_type = node_data.inode.file_type;
	if (node_data.m_ino != node_data.m_nid) {
		THROW_FS_ERROR(ERR_INVALID_NID, L"nid [%d] inode id (%d) does not match", ino, node_data.m_ino);
	}
	// ���ÿ��index
	UINT blk_cnt = 0;
	for (int ii = 0; ii < INDEX_TABLE_SIZE; ++ii)
	{
		NID  index_nid = node_data.inode.index[ii];
		if ( is_valid(index_nid) )
		{
			fsck_chk_node_blk(fsck, index_nid, file_type, BLOCK_DATA::BLOCK_INDEX, blk_cnt);
		}
	}
	if (node_data.inode.blk_num != blk_cnt) {
		THROW_FS_ERROR(ERR_INVALID_NID, L"ino [%d] valid data blk in node (%d) does not match data blk (%d) ", 
			ino, node_data.inode.blk_num, blk_cnt);
	}

	return 0;
}

int CF2fsSimulator::fsck_chk_index_blk(F2FS_FSCK* fsck, NID nid, PHY_BLK blk, F2FS_FILE_TYPE file_type, NODE_INFO& node_data, UINT &blk_cnt)
{
	// ���main bitmap��ȷ�����ظ� ��sanity check���Ѿ���飩��
	f2fs_set_main_bitmap(fsck, blk);

	// ������data block
	UINT valid_index =0;
	for (int ii = 0; ii < INDEX_SIZE; ++ii)
	{
		PHY_BLK data_blk = node_data.index.index[ii];
		if ( is_valid(data_blk) ) {
			valid_index++;
			fsck_chk_data_blk(fsck, nid, ii, data_blk, file_type);
		}
	}
	blk_cnt += valid_index;
	if (node_data.index.valid_data != valid_index) {
		THROW_FS_ERROR(ERR_INVALID_NID, L"nid [%d] valid data in node (%d) does not match data number (%d) ",
			nid, node_data.index.valid_data, valid_index);
	}
	return 0;
}

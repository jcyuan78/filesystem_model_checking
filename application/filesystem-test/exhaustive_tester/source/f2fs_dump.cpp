///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
#include "pch.h"
#include "../include/f2fs_simulator.h"

LOCAL_LOGGER_ENABLE(L"simulator.f2fs.nat", LOGGER_LEVEL_DEBUGINFO);


void CF2fsSimulator::DumpLog(FILE* out, const char* log_name)
{
	if (log_name == nullptr || log_name[0] == 0 || strcmp(log_name, "all") == 0)
	{
		DumpCheckpoint(out, m_checkpoint);
		m_nat.DumpNat(out);
//		DumpNatPage(out);
		m_segments.DumpSegments(out);
	}
	else if (memcmp("file:", log_name, 5) == 0)
	{
		int fid;
		sscanf_s(log_name + 5, "%d", &fid);
		DumpFile((_NID)(fid));
	}
	else if (strcmp(log_name, "checkpoint") == 0)
	{
		DumpCheckpoint(out, m_checkpoint);
	}
	else if (strcmp(log_name, "sit") ==0)
	{
		m_segments.DumpSegments(out);
	}
	else if (strcmp(log_name, "nat")==0)
	{
		m_nat.DumpNat(out);
	}
	else if (strcmp(log_name, "storage") == 0)
	{
		m_storage.DumpStorage(out);
	}
}

void CF2fsSimulator::DumpCheckpoint(FILE* out, const CKPT_BLOCK& checkpoint)
{
	fprintf_s(out, "[Checkpoint]\n");
	//fprintf_s(out, "\t Current Segment: \n");
	for (int ii = 0; ii < BT_TEMP_NR; ++ii)
	{
		if (!is_valid(checkpoint.header.cur_segs[ii].seg_no)) continue;
		if (0) {}
		else if (ii==BT_COLD_DATA) fprintf_s(out, "\t Current Segment: COLD_DATA ");
		else if (ii==BT_COLD_NODE) fprintf_s(out, "\t Current Segment: CODE_NODE ");
		else if (ii==BT_WARM_DATA) fprintf_s(out, "\t Current Segment: WARM_DATA ");
		else if (ii==BT_WARM_NODE) fprintf_s(out, "\t Current Segment: WARM_NODE ");
		else if (ii==BT_HOT__DATA) fprintf_s(out, "\t Current Segment: HOT__DATA ");
		else if (ii==BT_HOT__NODE) fprintf_s(out, "\t Current Segment: HOT__NODE ");
		fprintf_s(out, "seg no=%d, block offset=%d\n", checkpoint.header.cur_segs[ii].seg_no, checkpoint.header.cur_segs[ii].blk_offset);
	}
	fprintf_s(out, "\t open version=%d, closed version=%d\n", checkpoint.header.ver_open, checkpoint.header.ver_close);
	fprintf_s(out, "\t SIT version bitmap: %08X\n", checkpoint.header.sit_ver_bitmap);
	fprintf_s(out, "\t NAT version bitmap: %08X\n", checkpoint.header.nat_ver_bitmap);
	fprintf_s(out, "\t SIT journal nr=%d, NAT journal nr=%d\n", checkpoint.header.sit_journal_nr, checkpoint.header.nat_journal_nr);
}

void CF2fsSimulator::DumpNatPage(FILE* out)
{
	fprintf_s(out, "[Read NAT pages]\n");
	UINT nn = 0;
	for (_NID nid = 0; nid < NODE_NR; )
	{
		CPageInfo page;
		LBLK_T blk = m_nat.get_nat_block(nn, m_checkpoint);
		fprintf_s(out, "\t Read nat page, blk=%d\n", blk);
		m_storage.BlockRead(blk, &page);
		BLOCK_DATA* data = m_pages.get_data(&page);
		for (int ii = 0; nid < NODE_NR && ii < NAT_ENTRY_PER_BLK; ++ii, ++nid) {
			if (!is_valid(data->nat.nat[ii])) continue;
			PHY_BLK phy = data->nat.nat[ii];
			SEG_T segno;
			BLK_T segblk;
			CF2fsSegmentManager::BlockToSeg(segno, segblk, phy);
			fprintf_s(out, "\t\t nid=%d, phy_blk=%d(%d,%d)\n", nid, phy, segno, segblk);
		}
		nn++;
	}
	// ��checkpoint��journal�лָ�
	fprintf_s(out, "\t read from journal, count=%d\n", m_checkpoint.header.nat_journal_nr);
	for (UINT ii = 0; ii < m_checkpoint.header.nat_journal_nr; ++ii)
	{
		_NID nid = m_checkpoint.nat_journals[ii].nid;
		UINT phy_blk = m_checkpoint.nat_journals[ii].phy_blk;
		SEG_T segno;
		BLK_T segblk;
		CF2fsSegmentManager::BlockToSeg(segno, segblk, phy_blk);
		fprintf_s(out, "\t\t nid=%d, phy_blk=%d(%d,%d)\n", nid, phy_blk,segno, segblk);
	}
//	build_free();
//	memset(dirty, 0, sizeof(DWORD) * NAT_BLK_NR);
//	return true;
}

void CF2fsSimulator::DumpFile(_NID fid)
{
	LOG_DEBUG(L"[dump file] fid=%d", fid);
	PHY_BLK blk = m_nat.get_phy_blk(fid);
	if (is_invalid(blk)) {
		LOG_DEBUG(L"\tinvalid fid %d", fid);
		return;
	}
	CPageInfo inode_page;
	m_storage.BlockRead(CF2fsSegmentManager::phyblk_to_lba(blk), &inode_page);
	BLOCK_DATA* blk_data = m_pages.get_data(&inode_page);
	INODE& inode = blk_data->node.inode;
	LOG_DEBUG(L"\tinode(%d), phy_blk=%d, file_len=%d, file_blk_nr=%d", fid, blk, inode.file_size, inode.blk_num);
	for (int ii = 0; ii < INDEX_TABLE_SIZE; ++ii)
	{
		_NID index_nid = inode.index[ii];
		if (is_valid(index_nid))
		{
			PHY_BLK index_blk = m_nat.get_phy_blk(index_nid);
			if (!is_valid(index_blk)) {
				LOG_DEBUG(L"\tindex[%d], nid=%d, is invalid", ii, index_nid);
			}
			LOG_DEBUG(L"\tindex[%d], nid=%d, phy_blk=%d", ii, index_nid, index_blk);
			CPageInfo index_page;
			m_storage.BlockRead(CF2fsSegmentManager::phyblk_to_lba(index_blk), &index_page);
			BLOCK_DATA* index_data = m_pages.get_data(&index_page);
			INDEX_NODE& index = index_data->node.index;
			wchar_t str[48];
			wchar_t* ptr = str;
			for (int jj = 0; jj < INDEX_SIZE; ++jj)
			{
				ptr+= swprintf_s(ptr, 48 - (ptr - str), L"%04X ", index.index[jj]);
				if (jj % 8 == 7) {
					LOG_DEBUG(L"\t data blks:%s", str);
					ptr = str;
				}
			}
		}
	}
	LOG_DEBUG(L"[end dump file]");
}

void CNodeAddressTable::DumpNat(FILE* out)
{
	fprintf_s(out, "[NAT]\n");
	for (_NID nn = 0; nn < NODE_NR; nn++) {
		if (!is_valid(nat[nn])) continue;
		PHY_BLK phy = nat[nn];
		SEG_T segno;
		BLK_T segblk;
		CF2fsSegmentManager::BlockToSeg(segno, segblk, phy);

		fprintf_s(out, "\t nid=%d, phy_blk=%d(%d,%d), dirty=%d\n", nn, phy, segno, segblk, is_dirty(nn) != 0);
	}
}

void CF2fsSegmentManager::DumpSegments(FILE * out)
{
	LOG_DEBUG(L"[cur segment]");
	for (int ii = 0; ii < BT_TEMP_NR; ++ii)
	{
		CURSEG_INFO& cur = m_cur_segs[ii];
		LOG_DEBUG(L"\t type=%d, seg_no=%d, blk_offset=%d", ii, cur.seg_no, cur.blk_offset);
	}

	LOG_DEBUG(L"[Segments]");
	wchar_t str[256];
	for (SEG_T segno = 0; segno < MAIN_SEG_NR; ++segno)
	{
		SegmentInfo& seg = m_segments[segno];
		if (is_valid(seg.free_next)) continue;
		LOG_DEBUG(L"  seg_no=%d, valid_blk=%d, temp=%d", segno, seg.valid_blk_nr, seg.seg_temp);
		int ptr = 0;
		DWORD valid_mask = 1;
		for (BLK_T blk = 0; blk < BLOCK_PER_SEG; ++blk)
		{
			DWORD valid = is_blk_valid(segno, blk);
			if (is_invalid(seg.nids[blk])) {
				ptr+=swprintf_s(str + ptr, 256 - ptr, L"(XXX, XXX):%d,", valid != 0);
			}
			else if (is_invalid(seg.offset[blk])) {
				ptr+=swprintf_s(str + ptr, 256 - ptr, L"(%03d, XXX):%d,", seg.nids[blk], valid != 0);
			}
			else {
				ptr += swprintf_s(str + ptr, 256 - ptr, L"(%03d, %03d):%d,", seg.nids[blk], seg.offset[blk], valid != 0);
			}
			if ((blk % 4) == 3) {
				LOG_DEBUG(str);
				ptr = 0;
			}
		}

	}
}

void CF2fsSimulator::LogOption(FILE* out, DWORD flag)
{
	m_log_out = out;
	if (flag)
	{
		m_log_fsck = true;
	}
}
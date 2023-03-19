///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
#include "pch.h"
#include "lfs_simulator.h"

LOCAL_LOGGER_ENABLE(L"simulation.lfs", LOGGER_LEVEL_DEBUGINFO);

/// ///////////////////////////////////////////////////////////////////////////////////////////////////////////////////

enum IO_EVENT
{
	UNDEFINED=0, WRITE_DATA=1, WRITE_NODE=2, TRIM_DATA=3, TRIM_NODE=4,
};

void TrackIo(int event, PHY_BLK phy, FID fid, LBLK_T lblk)
{
	static PHY_BLK start_phy=0, pre_phy = 0;
	static FID cur_fid = 0;
	static LBLK_T start_lblk=0, pre_lblk = 0;
	static int cur_event = 0;
	static const wchar_t* EventToString[] = {
		L"Undefined", L"WRITE_DATA", L"WRITE_NODE", L"TRIM__DATA", L"TRIM__NODE",
	};

	if (cur_event == event, cur_fid == fid && phy == pre_phy + 1 && lblk == pre_lblk + 1)
	{	// merge
		pre_phy = phy;
		pre_lblk = lblk;
	}
	else
	{	// update
		if (cur_fid != 0)
		{
			LOG_TRACK(L"io", L",%s,fid=%d,lblk=%d~%d,pblk=0x%X~0x%X", EventToString[cur_event],
				cur_fid, start_lblk, pre_lblk, start_phy, pre_phy);
		}
		cur_fid = fid;
		cur_event = event;
		start_phy = phy; pre_phy = phy;
		start_lblk = lblk; pre_lblk = lblk;
	}
}

/// ///////////////////////////////////////////////////////////////////////////////////////////////////////////////////

CSingleLogSimulator::CSingleLogSimulator(void)
{	// ��ʼ��m_level_to_offset
	m_level_to_offset[0] = LEVEL1_OFFSET;
	m_level_to_offset[1] = m_level_to_offset[0] + (LEVEL2_OFFSET - LEVEL1_OFFSET) * INDEX_SIZE;
	m_level_to_offset[2] = m_level_to_offset[1] + (MAX_TABLE_SIZE - LEVEL2_OFFSET) * INDEX_SIZE * INDEX_SIZE;
}

CSingleLogSimulator::~CSingleLogSimulator(void)
{
	LOG_STACK_TRACE();
}


bool CSingleLogSimulator::Initialzie(const boost::property_tree::wptree& config)
{
	memset(&m_health_info, 0, sizeof(FsHealthInfo));

	size_t fs_size = config.get<size_t>(L"volume_size");
	m_health_info.m_logical_blk_nr = (LBLK_T)(ROUND_UP_DIV(fs_size, SECTOR_PER_BLOCK));
	float op = config.get<float>(L"over_provision");
	size_t phy_blk = (size_t)(m_health_info.m_logical_blk_nr * op);
	SEG_T seg_nr = (SEG_T)ROUND_UP_DIV(phy_blk, BLOCK_PER_SEG);
	m_segments.SetHealth(&m_health_info);
	m_segments.InitSegmentManager(seg_nr);

	m_health_info.m_seg_nr = seg_nr;
	m_health_info.m_blk_nr = seg_nr * BLOCK_PER_SEG;
	m_health_info.m_free_blk = m_health_info.m_blk_nr;

	return true;
}


FID CSingleLogSimulator::FileCreate(const std::wstring &fn)
{
	// allocate inode
	inode_info* inode = m_inodes.allocate_inode(inode_info::NODE_INODE, nullptr);
	inode->m_fn = fn;
	// ��ʼ��inode
	inode->m_dirty = true;
	// ����inode
	WriteInode(*inode);
	return inode->m_fid;
}

void CSingleLogSimulator::FileWrite(FID fid, size_t offset, size_t secs)
{
	// ������ʼblock�ͽ���block��end_block�����Ҫд�����һ����blk_nr=end_block - start_block
	DWORD start_blk, end_blk;
	LbaToBlock(start_blk, end_blk, offset, secs);

	inode_info* inode = m_inodes.get_node(fid);
	JCASSERT(inode);
	if (end_blk > MaxFileSize()) THROW_ERROR(ERR_APP, L"file size is too large, blks=%d, max_size=%d", end_blk, MaxFileSize());

	if (end_blk > inode->inode_blk.m_blks) inode->inode_blk.m_blks = end_blk;
	index_path ipath;
	InitIndexPath(ipath, inode);
	inode_info* direct_node = nullptr;
	for (; start_blk < end_blk; start_blk++)
	{
		// ����PHY_BLK��λ��
		int index;
		if (ipath.level < 0)
		{
			OffsetToIndex(ipath, start_blk, true);
			// ��ȡindex��λ�ã���Ҫ����index
			direct_node = ipath.node[ipath.level];
		}
		index = ipath.offset[ipath.level];

		// �ɵ�block��Ϣ
		PHY_BLK old_blk = direct_node->index_blk.m_index[index];// phy_index;
		// ȷ�������¶�
		BLK_TEMP temp = get_temperature(*inode, start_blk);
		// ����segment��
		InterlockedIncrement64(&m_health_info.m_total_host_write);
		PHY_BLK new_blk = m_segments.WriteBlockToSeg(fid, start_blk, BT_HOT__DATA);
		TrackIo(WRITE_DATA, new_blk, fid, start_blk);
//		LFS_BLOCK_INFO & blk = m_segments.get_block(new_blk);

		direct_node->index_blk.m_index[index] = new_blk;
		direct_node->m_dirty = true;
		if (old_blk == INVALID_BLK)
		{
			direct_node->index_blk.m_valid_blk++;
			// ����߼���û�б�д���������߼����Ͷ�
			InterlockedIncrement(&m_health_info.m_logical_saturation);
			if (m_health_info.m_logical_saturation >= (m_health_info.m_logical_blk_nr * 0.9))
			{
				THROW_ERROR(ERR_APP, L"logical saturation overflow, logical_saturation=%d, logical_block=%d", m_health_info.m_logical_saturation, m_health_info.m_logical_blk_nr);
			}
//			blk.host_write = 1;
		}
		else
		{	// ��Чԭ����block
//			LFS_BLOCK_INFO
			m_segments.InvalidBlock(old_blk);
		}

		// ��ipath�ƶ�����һ��offset 
		NextOffset(ipath);
		inode->inode_blk.m_host_write++;
	}
	// ����ipath
	LBLK_T bb = 0;
	for (size_t ii=0; ii<MAX_TABLE_SIZE; ++ii)
	{
		if (inode->inode_blk.m_direct[ii])
		{
			WriteInode(*inode->inode_blk.m_direct[ii]);
		}
		bb += INDEX_SIZE;
		if (bb >= inode->inode_blk.m_blks) break;
	}
	inode->m_dirty = true;
	WriteInode(*inode);
	// check GC

}

void CSingleLogSimulator::FileRead(FID fid, size_t offset, size_t secs)
{
	// sanity check
	// ������ʼblock�ͽ���block��end_block�����Ҫд�����һ����blk_nr=end_block - start_block
	DWORD start_blk, end_blk;
	LbaToBlock(start_blk, end_blk, offset, secs);

	inode_info * inode = m_inodes.get_node(fid);
	JCASSERT(inode);
	if (end_blk > inode->inode_blk.m_blks)
	{
		LOG_WARNING(L"Oversize on reading file, fid=%d,blks=%d,file_blks=%d", fid, end_blk, inode->inode_blk.m_blks);
	}

	index_path ipath;
	InitIndexPath(ipath, inode);
	inode_info* direct_node = nullptr;
	for (; start_blk < end_blk; start_blk++)
	{
		// ����PHY_BLK��λ��
		if (ipath.level < 0)
		{
			OffsetToIndex(ipath, start_blk, false);
			direct_node = ipath.node[ipath.level];
		}
		// ��ȡindex��λ�ã���Ҫ����index
		int index = ipath.offset[ipath.level];
//		inode_info* direct_node = GetDirectNodeForRead(index, ipath);
		if (direct_node && direct_node->index_blk.m_index[index] != INVALID_BLK)
		{
			PHY_BLK phy = direct_node->index_blk.m_index[index];
			LFS_BLOCK_INFO & blk = m_segments.get_block(phy);
			if (blk.nid != fid || blk.offset != start_blk)
			{
				THROW_ERROR(ERR_APP, L"L2P not match, fid=%d, lblk=%d, phy_blk=%X, fid_p2f=%d, lblk_p2f=%d", fid, start_blk, phy, blk.nid, blk.offset);
			}
		}
		// ��ipath�ƶ�����һ��offset 
		NextOffset(ipath);
	}
}


void CSingleLogSimulator::FileTruncate(FID fid)
{
	// �ļ�������block����Ч��Ȼ�󱣴�inode
	inode_info * inode = m_inodes.get_node(fid);
	JCASSERT(inode);
	index_path ipath;
	InitIndexPath(ipath, inode);
	inode_info* direct_node = nullptr;
	for (LBLK_T bb = 0; bb < inode->inode_blk.m_blks; ++bb)
	{
//		PHY_BLK phy_blk = inode.inode_blk.m_index[bb];
		if (ipath.level < 0)
		{
			OffsetToIndex(ipath, bb, false);
			direct_node = ipath.node[ipath.level];
			//inode_info* direct_blk = GetDirectNodeForRead(index, ipath);
		}
		int index = ipath.offset[ipath.level];
		if (direct_node && direct_node->index_blk.m_index[index] != INVALID_BLK)
		{
			PHY_BLK* phy_blk = direct_node->index_blk.m_index + index;
			if (*phy_blk != INVALID_BLK)
			{
	//			LOG_TRACK(L"io", L",TRIM,pblk=%X,fid=%d,lblk=%d", phy_blk, fid, bb);
				TrackIo(TRIM_DATA, *phy_blk, fid, bb);
				InterlockedDecrement(&m_health_info.m_logical_saturation);
			}
			m_segments.InvalidBlock(*phy_blk);
			*phy_blk = INVALID_BLK;
			direct_node->m_dirty = true;
			direct_node->index_blk.m_valid_blk--;
		}
		NextOffset(ipath);
	}

	// ����ipath
	LBLK_T bb = 0;
	for (size_t ii = 0; ii < MAX_TABLE_SIZE; ++ii)
	{
		if (inode->inode_blk.m_direct[ii])
		{
			WriteInode(*inode->inode_blk.m_direct[ii]);
		}
		bb += INDEX_SIZE;
		if (bb >= inode->inode_blk.m_blks) break;
	}
	inode->m_dirty = true;
	WriteInode(*inode);
}

void CSingleLogSimulator::FileDelete(FID fid)
{
	// ɾ���ļ�������inode
	inode_info * inode = m_inodes.get_node(fid);
	JCASSERT(inode);
	if (inode->inode_blk.m_ref_count > 0) THROW_ERROR(ERR_APP, L"file is still referenced, fid=%d", fid);
	FileTruncate(fid);
	PHY_BLK inode_blk = inode->m_phy_blk;
	if (inode_blk != INVALID_BLK)
	{
		TrackIo(TRIM_NODE, inode->m_phy_blk, fid, INVALID_BLK);
	}
	m_segments.InvalidBlock(inode_blk);
	inode->m_phy_blk = INVALID_BLK;
	m_inodes.free_inode(fid);
}

void CSingleLogSimulator::FileFlush(FID fid)
{
}

void CSingleLogSimulator::FileOpen(FID fid, bool delete_on_close)
{
	inode_info * inode = m_inodes.get_node(fid);
	JCASSERT(inode);
	if (inode->m_phy_blk == INVALID_BLK) THROW_ERROR(ERR_APP, L"open an invalid file, fid=%d", fid);
	inode->inode_blk.m_delete_on_close = delete_on_close;
	InterlockedIncrement(&inode->inode_blk.m_ref_count);
}

void CSingleLogSimulator::FileClose(FID fid)
{
	inode_info * inode = m_inodes.get_node(fid);
	JCASSERT(inode);
	InterlockedDecrement(&inode->inode_blk.m_ref_count);
	if (inode->inode_blk.m_delete_on_close) FileDelete(fid);
}


DWORD CSingleLogSimulator::MaxFileSize(void) const
{
	// Ŀǰ��֧��һ�� index
	//return LEVEL1_OFFSET + (LEVEL2_OFFSET - LEVEL1_OFFSET) * INDEX_SIZE + (MAX_TABLE_SIZE - LEVEL2_OFFSET) * INDEX_SIZE * INDEX_SIZE;
	return MAX_TABLE_SIZE * INDEX_SIZE;
}

void CSingleLogSimulator::InitIndexPath(index_path& path, inode_info* inode)
{
	path.level = -1;
	memset(path.offset, 0, sizeof(path.offset));
	//memset(path.index_node, 0xFF, sizeof(path.index_node));
	memset(path.node, 0, sizeof(path.node));
	path.node[0] = inode;
	//path.index_node[0] = inode->m_fid;
}

void CSingleLogSimulator::OffsetToIndex(index_path& ipath, LBLK_T offset, bool alloc)
{
	// ������Ҫ����level
	//int level = 0;
//	if (offset < m_level_to_offset[0])
//	{
//		path.level = 0;
//		path.offset[0] = offset;
////		path.index_node[0] = 
//	}
//	else if (offset < m_level_to_offset[1])
//	{
//		offset -= m_level_to_offset[0];
//		path.level = 1;
//		path.offset[1] = (offset & (INDEX_SIZE - 1));
//		offset >>= INDEX_SIZE_BIT;
//
//		path.offset[0] = offset + LEVEL2_OFFSET;
//	}
//	else if (offset < m_level_to_offset[2])
//	{
//		offset -= m_level_to_offset[1];
//		path.level = 2;
//
//		path.offset[2] = (offset &(INDEX_SIZE-1));
//		offset >>= INDEX_SIZE_BIT;
//
//		path.offset[1] = (offset & (INDEX_SIZE - 1));
//		offset >>= INDEX_SIZE_BIT;
//
//		path.offset[0] = offset + LEVEL2_OFFSET;
//	}
//	else THROW_ERROR(ERR_APP, L"offset is too large, offset=%d, max_offset=%d", offset, MaxFileSize());

	// ��inode�а��Ѿ��е�index block����node[]��
	ipath.level = 1;
	ipath.offset[0] = offset / INDEX_SIZE;
	
	inode_info* node = ipath.node[0];
	ipath.node[1] = node->inode_blk.m_direct[ipath.offset[0]];
	if (ipath.node[1] == nullptr)
	{
		if (alloc)
		{
			ipath.node[1] = m_inodes.allocate_inode(inode_info::NODE_INDEX, node);
			ipath.node[1]->m_parent_offset = ipath.offset[0];
			node->inode_blk.m_direct[ipath.offset[0]] = ipath.node[1];
			node->m_dirty = true;
		}
	}
	//ipath.index_node[1] = ipath.node[1]->m_fid;
	ipath.offset[1] = offset % INDEX_SIZE;
	// ����ÿһ���ƫ����
}




void CSingleLogSimulator::NextOffset(index_path& ipath)
{
//	bool invalid = false;
//	ipath.offset[path.level]++;
//	if (ipath.level == 0 && path.offset[0] >= LEVEL1_OFFSET) invalid = true;
//	else
//	{
//		while (ipath.level >0 && path.offset[ipath.level] >= INDEX_SIZE)
////		if (path.offset[ipath.level] >= INDEX_SIZE)
//		{
//			// ����level���ڲ�ģ����Ҹ�����һ���table
//			inode_info& node = m_inodes.get_node(path.index_node[ipath.level]);
//			node.m_dirty = true;
//			PHY_BLK phy_blk = WriteInode(node);
//
//			path.level--;
//			node.m_parent->index_blk.m_index[ipath.offset[path.level]] = phy_blk;
//			ipath.offset[path.level]++;
//			//inode_info& parent = m_inodes.get_node(ipath.index_node[path.level - 1]);
//			//parent.index_blk.m_index[ipath.offset[path.level - 1]] = phy_blk;
//			invalid = true;
//		}
//	}
//	if (invalid) ipath.level = -1;
	ipath.offset[1]++;
	if (ipath.offset[1] >= INDEX_SIZE)
	{
		LOG_DEBUG_(1, L"reset ipath, level=%d, offset=%d", ipath.level, ipath.offset[ipath.level]);
		ipath.level = -1;
		ipath.node[1] = nullptr;
	}
}

// ��inodeд��segment��
PHY_BLK CSingleLogSimulator::WriteInode(inode_info& inode)
{
	// ����inode
	if (!inode.m_dirty) return inode.m_phy_blk;
	if (inode.m_type == inode_info::NODE_INDEX && inode.index_blk.m_valid_blk == 0)
	{	// ɾ��index block
		inode_info* parent = inode.m_parent;
		parent->inode_blk.m_direct[inode.m_parent_offset] = nullptr;
		m_segments.InvalidBlock(inode.m_phy_blk);
		m_inodes.free_inode(inode.m_phy_blk);
		return INVALID_BLK;
	}
	else if (inode.m_type == inode_info::NODE_INODE)
	{
		PHY_BLK old_blk = inode.m_phy_blk;
		inode.m_phy_blk = m_segments.WriteBlockToSeg(inode.m_fid, -1, BT_HOT__DATA);
		TrackIo(WRITE_NODE, inode.m_phy_blk, inode.m_fid, INVALID_BLK);
		m_segments.InvalidBlock(old_blk);
		inode.m_dirty = false;
		return inode.m_phy_blk;
	}
	else return INVALID_BLK;
}

void CSingleLogSimulator::DumpSegmentBlocks(const std::wstring& fn)
{
	m_segments.DumpSegmentBlocks(fn);
}

void CSingleLogSimulator::DumpSegments(const std::wstring& fn, bool sanity_check)
{
	FILE* log = nullptr;
	_wfopen_s(&log, fn.c_str(), L"w+");
	if (log == nullptr) THROW_ERROR(ERR_APP, L"failed on opening file %s", fn.c_str());
	fprintf_s(log, "seg,blk_nr,invalid,hot_node,hot_data,warm_node,warm_data,codl_node,cold_data\n");
	for (SEG_T ss = 0; ss < m_segments.get_seg_nr(); ++ss)
	{
		UINT blk_count[BT_TEMP_NR + 1];
		memset(blk_count, 0, sizeof(UINT) * (BT_TEMP_NR + 1));
		SEG_INFO<LFS_BLOCK_INFO>& seg = m_segments.get_segment(ss);
		for (BLK_T bb = 0; bb < BLOCK_PER_SEG; ++bb)
		{
			LFS_BLOCK_INFO& blk = seg.blk_map[bb];
			if (blk.nid == INVALID_BLK) blk_count[BT_TEMP_NR]++;
			else
			{
				PHY_BLK src_phy = PhyBlock(ss, bb);
				if (blk.offset == INVALID_BLK)
				{
					blk_count[BT_HOT__NODE]++;
					// sanity check
					if (sanity_check)
					{
						inode_info * inode = m_inodes.get_node(blk.nid);
						JCASSERT(inode);
						if (inode->m_phy_blk != src_phy)
						{
							THROW_ERROR(ERR_APP, L"node P2L does not match, phy_blk=%X, fid=%d, phy_in_inod=%X",
								src_phy, blk.nid, inode->m_phy_blk);
						}
					}
				}
				else
				{
					blk_count[BT_HOT__DATA] ++;
					// sanity check
					if (sanity_check)
					{
						inode_info * inode = m_inodes.get_node(blk.nid);
						JCASSERT(inode);
						index_path ipath;
						InitIndexPath(ipath, inode);
						OffsetToIndex(ipath, blk.offset, false);
						PHY_BLK phy_blk = ipath.node[ipath.level]->index_blk.m_index[ipath.offset[ipath.level]];
						if (phy_blk != PhyBlock(ss, bb))
						{
							THROW_ERROR(ERR_APP, L"data P2L does not match, phy_blk=%X, fid=%d, offset=%d, phy_in_inode=%X", src_phy, blk.nid, blk.offset, phy_blk);
						}

					}
				}
			}
		}
		fprintf_s(log, "%d,512,%d,%d,%d,%d,%d,%d,%d\n", ss, blk_count[BT_TEMP_NR],
			blk_count[BT_HOT__NODE], blk_count[BT_HOT__DATA],
			blk_count[BT_WARM_NODE], blk_count[BT_WARM_DATA],
			blk_count[BT_COLD_NODE], blk_count[BT_COLD_DATA]);
	}
	fclose(log);
}

void CSingleLogSimulator::DumpAllFileMap(const std::wstring& fn)
{
	FILE* log = nullptr;
	_wfopen_s(&log, fn.c_str(), L"w+");
	if (log == nullptr) THROW_ERROR(ERR_APP, L"failed on opening file %s", fn.c_str());
	fprintf_s(log, "fn,fid,start_offset,lblk_nr,phy_blk,seg_id,blk_id\n");

	for (FID ii = 0; ii < m_inodes.get_node_nr(); ++ii)
	{
		inode_info* inode = m_inodes.get_node(ii);
		if (!inode || inode->m_type != inode_info::NODE_INODE) continue;
		DumpFileMap(log, ii);
	}
	fclose(log);
}

#define OUT_FILE_MAP(phy)	{\
	if (start_phy != INVALID_BLK) {\
		SEG_T seg; BLK_T blk; BlockToSeg(seg,blk,start_phy);	\
		fprintf_s(out, "%S,%d,%d,%d,%X,%d,%d\n", inode->m_fn.c_str(), fid, start_offset, (bb - start_offset), start_phy,seg,blk);}\
	start_phy = phy; pre_phy = phy; }

void CSingleLogSimulator::DumpFileMap_merge(FILE* out, FID fid)
{
	inode_info * inode = m_inodes.get_node(fid);
	index_path ipath;
	InitIndexPath(ipath, inode);
	inode_info* direct_node = nullptr;

	PHY_BLK start_phy=INVALID_BLK, pre_phy=0;
	LBLK_T start_offset = INVALID_BLK, pre_offset = INVALID_BLK;
	LBLK_T bb = 0;
	for (bb = 0; bb < inode->inode_blk.m_blks; ++bb)
	{
		if (ipath.level < 0)
		{
			OffsetToIndex(ipath, bb, false);
			direct_node = ipath.node[ipath.level];
		}
		int index = ipath.offset[ipath.level];
		if (direct_node == nullptr)
		{	// output
			OUT_FILE_MAP(INVALID_BLK);

			bb += INDEX_SIZE;
			ipath.level = -1;
			continue;
		}
		PHY_BLK phy_blk = direct_node->index_blk.m_index[index];

		if (phy_blk != INVALID_BLK)
		{
			// merge
			if (phy_blk == pre_phy + 1)
			{
				pre_phy = phy_blk;
				pre_offset = bb;
			}
			else
			{	// out
				OUT_FILE_MAP(phy_blk);
				start_offset = bb;
			}
			// sanity check
			LFS_BLOCK_INFO& blk = m_segments.get_block(phy_blk);
			if (blk.nid != fid || blk.offset != bb)
			{
				THROW_ERROR(ERR_APP, L"L2P not match, fid=%d, lblk=%d, phy_blk=%X, fid_p2f=%d, lblk_p2f=%d", 
					fid, bb, phy_blk, blk.nid, blk.offset);
			}
		}
		else	{	OUT_FILE_MAP(INVALID_BLK);	}
		NextOffset(ipath);
	}
	OUT_FILE_MAP(INVALID_BLK);
}


void CSingleLogSimulator::DumpFileMap_no_merge(FILE* out, FID fid)
{
	inode_info* inode = m_inodes.get_node(fid);
	index_path ipath;
	InitIndexPath(ipath, inode);
	inode_info* direct_node = nullptr;

	PHY_BLK start_phy = INVALID_BLK, pre_phy = 0;
	LBLK_T start_offset = INVALID_BLK, pre_offset = INVALID_BLK;
	LBLK_T bb = 0;
	for (bb = 0; bb < inode->inode_blk.m_blks; ++bb)
	{
		if (ipath.level < 0)
		{
			OffsetToIndex(ipath, bb, false);
			direct_node = ipath.node[ipath.level];
		}
		int index = ipath.offset[ipath.level];
		if (direct_node)
		{	// output
			PHY_BLK phy_blk = direct_node->index_blk.m_index[index];

			if (phy_blk != INVALID_BLK)
			{
				SEG_T seg; BLK_T seg_blk; 
				BlockToSeg(seg, seg_blk, phy_blk);
				fprintf_s(out, "%S,%d,%d,1,%X,%d,%d\n", inode->m_fn.c_str(), fid, bb, phy_blk, seg, seg_blk);

				// sanity check
				LFS_BLOCK_INFO& blk = m_segments.get_block(phy_blk);
				if (blk.nid != fid || blk.offset != bb)
				{
					THROW_ERROR(ERR_APP, L"L2P not match, fid=%d, lblk=%d, phy_blk=%X, fid_p2f=%d, lblk_p2f=%d",
						fid, bb, phy_blk, blk.nid, blk.offset);
				}
			}
		}
		NextOffset(ipath);
	}
	OUT_FILE_MAP(INVALID_BLK);
}


///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// == inode manager ==

CInodeManager::CInodeManager(void)
{
//	m_inodes.emplace_back();	// ����FID=0, ����root
	m_node_nr = 0;
	m_free_nr = 0;
	m_used_nr = 0;
	// ����fid=0 ��Ϊroot
	allocate_inode(inode_info::NODE_INODE, nullptr);
}

CInodeManager::~CInodeManager(void)
{
	LOG_STACK_TRACE();
	for (size_t ii = 0; ii < m_node_nr; ii += 1024)
	{	// ÿ��1024��λ�ã�Ϊһ�����䵥λ
		inode_info* node = m_nodes.at(ii);
		delete[] node;
	}
}



inode_info * CInodeManager::allocate_inode(inode_info::NODE_TYPE type, inode_info* parent)
{
	FID nid = 0;
	if (m_free_list.empty())
	{
		inode_info* nodes = new inode_info[1024];
		for (size_t ii = 0; ii < 1024; ++ii)
		{
			m_nodes.push_back(nodes + ii);
			m_free_list.push_back((FID)(m_node_nr + ii));
		}
		m_node_nr += 1024;
		m_free_nr += 1024;
	}

	nid = m_free_list.front();
	m_free_list.pop_front();
	m_free_nr--;
	m_used_nr++;

	inode_info* node = m_nodes.at(nid);/*get_node(nid);*/
	if (type == inode_info::NODE_INDEX) node->init_index(nid, parent);
	else if (type == inode_info::NODE_INODE) node->init_inode(nid);
	return node;
}


void CInodeManager::free_inode(FID nid)
{
	inode_info* inode = get_node(nid);
	JCASSERT(inode);
	inode->m_phy_blk = INVALID_BLK;
	inode->m_type = inode_info::NODE_FREE;
	m_free_list.push_back(nid);
	m_free_nr++;
	m_used_nr--;
}

inode_info * CInodeManager::get_node(FID nid)
{
	inode_info * inode = m_nodes.at(nid);
//	if (inode->m_phy_blk == INVALID_BLK) THROW_ERROR(ERR_APP, L"open an invalid inode, fid=%d", nid);
	if (inode->m_phy_blk == INVALID_BLK) return nullptr;
	return inode;
}

void inode_info::init_index(FID fid, inode_info* parent)
{
//	memset(this, 0, sizeof(inode_info));
	m_type = NODE_INDEX;
	m_phy_blk = INVALID_BLK;
	m_fid = fid;
	m_parent = parent;
	m_parent_offset = 0;

	index_blk.m_valid_blk = 0;
	memset(index_blk.m_index, 0xFF, sizeof(PHY_BLK) * INDEX_SIZE);

}

void inode_info::init_inode(FID fid)
{
//	memset(this, 0, sizeof(inode_info));
	m_type = NODE_INODE;
	m_phy_blk = INVALID_BLK;
	m_fid = fid;
	m_parent = nullptr;
	m_parent_offset = 0;
	
	//inode_blk.m_size = 0;
	inode_blk.m_blks = 0;
	memset(inode_blk.m_direct, 0, sizeof(inode_info*) * MAX_TABLE_SIZE);
	inode_blk.m_ref_count = 1;
	inode_blk.m_delete_on_close = false;
	inode_blk.m_host_write = 0;
}


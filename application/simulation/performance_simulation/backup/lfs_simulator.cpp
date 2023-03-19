///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
#include "pch.h"
#include "lfs_simulator.h"

LOCAL_LOGGER_ENABLE(L"simulation.lfs", LOGGER_LEVEL_DEBUGINFO);



CSingleLogSimulator::CSingleLogSimulator(void)
{
	// 初始化m_level_to_offset
	m_level_to_offset[0] = LEVEL1_OFFSET;
	m_level_to_offset[1] = m_level_to_offset[0] + (LEVEL2_OFFSET - LEVEL1_OFFSET) * INDEX_SIZE;
	m_level_to_offset[2] = m_level_to_offset[1] + (MAX_TABLE_SIZE - LEVEL2_OFFSET) * INDEX_SIZE * INDEX_SIZE;

}

bool CSingleLogSimulator::Initialzie(const boost::property_tree::wptree& config)
{
	size_t fs_size = config.get<size_t>(L"volume_size");
	m_logic_blks = (LBLK_T)(ROUND_UP_DIV(fs_size, SECTOR_PER_BLOCK));
	float op = config.get<float>(L"over_provision");
	size_t phy_blk = (size_t)(m_logic_blks * op);
	SEG_T seg_nr = (SEG_T)ROUND_UP_DIV(phy_blk, BLOCK_PER_SEG);
	m_segments.InitSegmentManager(seg_nr);
	m_physical_blks = seg_nr * BLOCK_PER_SEG;
	return true;
}


FID CSingleLogSimulator::FileCreate(void)
{
	// allocate inode
	FID fid = m_inodes.allocate_inode();
	inode_info& inode = m_inodes.get_node(fid);
	// 初始化inode
	memset(&inode, 0xFF, sizeof(inode_info));
	inode.m_fid = fid;
	inode.inode_blk.m_size = 0;
	inode.inode_blk.m_blks = 0;
	inode.m_dirty = true;
	inode.inode_blk.m_ref_count = 1;
	inode.inode_blk.m_delete_on_close = false;
	inode.m_type = 0;
	// 更新inode
//	inode.m_phy_blk = m_segments.WriteBlockToSeg(fid, -1, BT_HOT__DATA);
	WriteInode(inode);
	return fid;
}

void CSingleLogSimulator::FileWrite(FID fid, size_t offset, size_t secs)
{
	// 计算起始block和结束block，end_block是最后要写入的下一个。blk_nr=end_block - start_block
	DWORD start_blk, end_blk;
	LbaToBlock(start_blk, end_blk, offset, secs);

	inode_info& inode = m_inodes.get_node(fid);
	if (end_blk > MaxFileSize()) THROW_ERROR(ERR_APP, L"file size is too large, blks=%d, max_size=%d", end_blk, MaxFileSize());

	if (end_blk > inode.inode_blk.m_blks) inode.inode_blk.m_blks = end_blk;
	index_path ipath;
	for (; start_blk < end_blk; start_blk++)
	{
		// 查找PHY_BLK定位到
		PHY_BLK* index_tab = inode.inode_blk.m_index;
		PHY_BLK* index_pos = nullptr;
		if (ipath.level < 0)
		{
			OffsetToIndex(ipath, start_blk);
			inode_info* node = &inode;
			for (int ll = 0; ll < ipath.level; ++ll)
			{
				if (ll == (ipath.level - 1))
				{	// 到达叶子层
					index_pos = index_tab + ipath.offset[ll];
					break;
				}
				//否则：读取下一层的table
	//			index_blk_info* index_info = nullptr;
				FID nid = index_tab[ipath.offset[ll]];
				if (nid == INVALID_BLK)
				{	// 创建新的index block
					JCASSERT(ll >= 1);
					nid = m_inodes.allocate_index_block();
					inode_info& index_info = m_inodes.get_node(nid);
					index_info.m_fid = nid;
					index_info.m_type = 1;
					// 设置parent
					index_info.m_parent = node;
					node = &index_info;
				}
				inode_info& index_info = m_inodes.get_node(nid);
				index_info.m_dirty = true;
				index_tab = index_info.index_blk.m_index;
				// Write Index Node
			}
		}

		// 旧的block信息
//		PHY_BLK old_blk = inode.m_index[start_blk];
		PHY_BLK old_blk = *index_pos;
		// 确定数据温度
		BLK_TEMP temp = get_temperature(inode, start_blk);
		// 分配segment，
		PHY_BLK new_blk = m_segments.WriteBlockToSeg(fid, start_blk, BT_HOT__DATA);
		LOG_TRACK(L"io",L",WRITE_DATA,pblk=%X,temp=%d,fid=%d, lblk=%d", new_blk, BT_HOT__DATA, fid, start_blk);

		// 刷新记录
//		inode.m_index[start_blk] = new_blk;
		*index_pos = new_blk;
		// 无效原来的block
		LOG_TRACK(L"io",L",TRIM,pblk=%X,fid=%d,lblk=%d", old_blk, fid, start_blk);
		m_segments.InvalidBlock(old_blk);

		// 将ipath移动到下一个offset 
		NextOffset(ipath);
	}
	// 更新inode
	//PHY_BLK new_inode_blk = m_segments.WriteBlockToSeg(fid, -1, BT_HOT__DATA);
	//LOG_TRACK(L"io",L",WRITE_NODE,pblk=%X,temp=%d, fid=%d", new_inode_blk, BT_HOT__DATA, fid);
	//LOG_TRACK(L"io",L",TRIM,pblk=%X,fid=%d,inode", inode.m_phy_blk, fid);
	//m_segments.InvalidBlock(inode.m_phy_blk);
	//inode.m_phy_blk = new_inode_blk;
	inode.m_dirty = true;
	WriteInode(inode);
	// check GC

}

void CSingleLogSimulator::FileTruncate(FID fid)
{
	// 文件的所有block都无效，然后保存inode
	inode_info& inode = m_inodes.get_node(fid);
	index_path ipath;
	for (LBLK_T bb = 0; bb < inode.inode_blk.m_blks; ++bb)
	{
//		PHY_BLK phy_blk = inode.inode_blk.m_index[bb];
		if (ipath.level < 0) OffsetToIndex(ipath, bb);
		PHY_BLK* phy_blk = GetPhyBlock(ipath);
		if (phy_blk)
		{
			LOG_TRACK(L"io", L",TRIM,pblk=%X,fid=%d,lblk=%d", phy_blk, fid, bb);
			m_segments.InvalidBlock(*phy_blk);
			*phy_blk = INVALID_BLK;
			//inode.inode_blk.m_index[bb] = INVALID_BLK;
		}
	}
	//PHY_BLK new_inode_blk = m_segments.WriteBlockToSeg(fid, -1, BT_HOT__DATA);
	//LOG_TRACK(L"io",L",WRITE_NODE,pblk=%X,temp=%d,fid=%d", new_inode_blk, BT_HOT__DATA, fid);
	//LOG_TRACK(L"io", L",TRIM,pblk=%X,fid=%d,inode", inode.m_phy_blk, fid);
	//m_segments.InvalidBlock(inode.m_phy_blk);
	//inode.m_phy_blk = new_inode_blk;
	inode.m_dirty = true;
	WriteInode(inode);
}

void CSingleLogSimulator::FileDelete(FID fid)
{
	// 删除文件，回收inode
	inode_info& inode = m_inodes.get_node(fid);
	if (inode.inode_blk.m_ref_count > 0) THROW_ERROR(ERR_APP, L"file is still referenced, fid=%d", fid);
	FileTruncate(fid);
	PHY_BLK inode_blk = inode.m_phy_blk;
	LOG_TRACK(L"io", L",TRIM,pblk=%X,fid=%d,inode", inode.m_phy_blk, fid);
	m_segments.InvalidBlock(inode_blk);
	inode.m_phy_blk = INVALID_BLK;
	m_inodes.free_inode(fid);
}

void CSingleLogSimulator::FileFlush(FID fid)
{
}

void CSingleLogSimulator::FileOpen(FID fid, bool delete_on_close)
{
	inode_info& inode = m_inodes.get_node(fid);
	if (inode.m_phy_blk == INVALID_BLK) THROW_ERROR(ERR_APP, L"open an invalid file, fid=%d", fid);
	inode.inode_blk.m_delete_on_close = delete_on_close;
	InterlockedIncrement(&inode.inode_blk.m_ref_count);
}

void CSingleLogSimulator::FileClose(FID fid)
{
	inode_info& inode = m_inodes.get_node(fid);
	InterlockedDecrement(&inode.inode_blk.m_ref_count);
	if (inode.inode_blk.m_delete_on_close) FileDelete(fid);
}


DWORD CSingleLogSimulator::MaxFileSize(void) const
{

	// 目前仅支持一层 index
	return LEVEL1_OFFSET + (LEVEL2_OFFSET - LEVEL1_OFFSET) * INDEX_SIZE + (MAX_TABLE_SIZE - LEVEL2_OFFSET) * INDEX_SIZE * INDEX_SIZE;
}

PHY_BLK* CSingleLogSimulator::GetPhyBlock(index_path& path)
{

}


void CSingleLogSimulator::OffsetToIndex(index_path& path, LBLK_T offset)
{
	// 计算需要多少level
	//int level = 0;
	if (offset < m_level_to_offset[0])
	{
		path.level = 0;
		path.offset[0] = offset;
//		path.index_node[0] = 
	}
	else if (offset < m_level_to_offset[1])
	{
		offset -= m_level_to_offset[0];
		path.level = 1;
		path.offset[1] = (offset & (INDEX_SIZE - 1));
		offset >>= INDEX_SIZE_BIT;

		path.offset[0] = offset + LEVEL2_OFFSET;
	}
	else if (offset < m_level_to_offset[2])
	{
		offset -= m_level_to_offset[1];
		path.level = 2;

		path.offset[2] = (offset &(INDEX_SIZE-1));
		offset >>= INDEX_SIZE_BIT;

		path.offset[1] = (offset & (INDEX_SIZE - 1));
		offset >>= INDEX_SIZE_BIT;

		path.offset[0] = offset + LEVEL2_OFFSET;
	}
	else THROW_ERROR(ERR_APP, L"offset is too large, offset=%d, max_offset=%d", offset, MaxFileSize());
	// 计算每一层的偏移量
}

void CSingleLogSimulator::NextOffset(index_path& path)
{
	bool invalid = false;
	path.offset[path.level]++;
	if (path.level == 0 && path.offset[0] >= LEVEL1_OFFSET) invalid = true;
	else
	{
		while (path.level >0 && path.offset[path.level] >= INDEX_SIZE)
//		if (path.offset[path.level] >= INDEX_SIZE)
		{
			// 保存level所在层的，并且更新上一层的table
			inode_info& node = m_inodes.get_node(path.index_node[path.level]);
			node.m_dirty = true;
			PHY_BLK phy_blk = WriteInode(node);

			path.level--;
			node.m_parent->index_blk.m_index[path.offset[path.level]] = phy_blk;
			path.offset[path.level]++;
			//inode_info& parent = m_inodes.get_node(path.index_node[path.level - 1]);
			//parent.index_blk.m_index[path.offset[path.level - 1]] = phy_blk;
			invalid = true;
		}
	}
	if (invalid) path.level = -1;
}


// 将inode写入segment中
PHY_BLK CSingleLogSimulator::WriteInode(inode_info& inode)
{
	// 更新inode
	if (!inode.m_dirty) return;
	PHY_BLK old_blk = inode.m_phy_blk;
	inode.m_phy_blk = m_segments.WriteBlockToSeg(inode.m_fid, -1, BT_HOT__DATA);
	LOG_TRACK(L"io", L",WRITE_NODE,pblk=%X,temp=%d, fid=%d", inode.m_phy_blk, BT_HOT__DATA, inode.m_fid);
	LOG_TRACK(L"io", L",TRIM,pblk=%X,fid=%d,inode", old_blk, inode.m_fid);
	m_segments.InvalidBlock(old_blk);
	inode.m_dirty = false;
	return inode.m_phy_blk;
}



///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// == inode manager ==

CInodeManager::CInodeManager(void)
{
	m_inodes.emplace_back();	// 保留FID=0, 代表root
}

FID CInodeManager::allocate_inode(void)
{
	FID nid = 0;
	if (m_free_list.empty())
	{
		m_inodes.emplace_back();
		nid = (FID) (m_inodes.size() - 1);
	}
	else
	{
		nid = m_free_list.front();
		m_free_list.pop_front();
	}

	return nid;
}

void CInodeManager::free_inode(FID nid)
{
	m_free_list.push_back(nid);
}

inode_info& CInodeManager::get_node(FID nid)
{
	inode_info & inode = m_inodes.at(nid);
	if (inode.m_phy_blk == INVALID_BLK) THROW_ERROR(ERR_APP, L"open an invalid inode, fid=%d", nid);
	return inode;
}

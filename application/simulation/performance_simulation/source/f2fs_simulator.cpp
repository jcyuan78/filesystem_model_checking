///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
#include "pch.h"
#include "f2fs_simulator.h"

LOCAL_LOGGER_ENABLE(L"simulator.f2fs", LOGGER_LEVEL_DEBUGINFO);

#define MULTI_HEAD	4


CF2fsSimulator::~CF2fsSimulator(void)
{
	// 删除所有file
	for (FID ii = 0; ii < m_inodes.get_node_nr(); ++ii)
	{
		CNodeInfoBase* inode = m_inodes.get_node(ii);
		if (!inode || inode->m_type != inode_info::NODE_INODE) continue;
		CInodeInfo* _inode = dynamic_cast<CInodeInfo*>(inode);
		JCASSERT(_inode);
		_inode->m_ref_count = 0;
		FileDelete(ii);
	}
}

bool CF2fsSimulator::Initialzie(const boost::property_tree::wptree& config)
{
	m_segments.m_gc_trace = m_gc_trace;

	memset(&m_health_info, 0, sizeof(FsHealthInfo));

	size_t fs_size = config.get<size_t>(L"volume_size");
	m_health_info.m_logical_blk_nr = (LBLK_T)(ROUND_UP_DIV(fs_size, SECTOR_PER_BLOCK));
	float op = config.get<float>(L"over_provision");
	size_t phy_blk = (size_t)(m_health_info.m_logical_blk_nr * op);
	SEG_T seg_nr = (SEG_T)ROUND_UP_DIV(phy_blk, BLOCK_PER_SEG);

	m_gc_th_low = config.get<SEG_T>(L"gc_seg_lo");
	m_gc_th_hi = config.get<SEG_T>(L"gc_seg_hi");

	m_segments.SetHealth(&m_health_info);
	m_segments.InitSegmentManager(seg_nr, m_gc_th_low, m_gc_th_hi);
	m_segments.m_inodes = &m_inodes;

	m_health_info.m_seg_nr = seg_nr;
	m_health_info.m_blk_nr = seg_nr * BLOCK_PER_SEG;
	m_health_info.m_free_blk = m_health_info.m_blk_nr;

	return true;
}

FID CF2fsSimulator::FileCreate(const std::wstring& fn)
{
	// allocate inode
	CNodeInfoBase* inode = m_inodes.allocate_inode(CNodeInfoBase::NODE_INODE, nullptr);
	CInodeInfo* _inode = dynamic_cast<CInodeInfo*>(inode);
	_inode->m_fn = fn;
	// 更新inode
	UpdateInode(_inode);
	LOG_TRACK(L"fs", L",CreateFile,fn=%d,fid=%d", fn.c_str(), inode->m_fid);
	InterlockedIncrement(&_inode->m_ref_count);

	LOG_TRACK(L"inode", L",CREATE,fid=%d,phy=%X", inode->m_fid, inode->data_page->phy_blk);
	return inode->m_fid;
}

void CF2fsSimulator::FileWrite(FID fid, size_t offset, size_t secs)
{
	// 计算起始block和结束block，end_block是最后要写入的下一个。blk_nr=end_block - start_block
	DWORD start_blk, end_blk;
	LbaToBlock(start_blk, end_blk, offset, secs);
	LOG_TRACK(L"fs", L",WriteFile,fid=%d,offset=%d,secs=%d", fid, start_blk, (end_blk - start_blk));

	CInodeInfo* inode = dynamic_cast<CInodeInfo*>(m_inodes.get_node(fid));
	JCASSERT(inode);

	if (end_blk > MaxFileSize()) THROW_ERROR(ERR_APP, L"file size is too large, blks=%d, max_size=%d", end_blk, MaxFileSize());

	if (end_blk > inode->m_blks) inode->m_blks = end_blk;
	CIndexPath ipath(inode);

	CNodeInfoBase* direct_node = nullptr;
	fprintf_s(m_log_write_trace, "%lld,%d,%d,%d", m_write_count++, fid, start_blk, end_blk - start_blk);
	for (; start_blk < end_blk; start_blk++)
	{
		// 查找PHY_BLK定位到
		int index;
		if (ipath.level < 0)
		{
			OffsetToIndex(ipath, start_blk, true);
			// 获取index的位置，需要更新index
			direct_node = ipath.node[ipath.level];
		}
		index = ipath.offset[ipath.level];

		// 确定数据温度
//		BLK_TEMP temp = 
		InterlockedIncrement64(&m_health_info.m_total_host_write);
		CPageInfo* dpage = direct_node->data[index];
		if (dpage)
		{	// 数据存在
			dpage->host_write++;
		}
		else
		{	// 数据不存在
			dpage = new CPageInfo;
			dpage->type = CPageInfo::PAGE_DATA;
			dpage->inode = inode;
			dpage->offset = start_blk;
			direct_node->valid_data++;
			// 这个逻辑块没有被写过，增加逻辑饱和度
			InterlockedIncrement(&m_health_info.m_logical_saturation);
			if (m_health_info.m_logical_saturation >= (m_health_info.m_logical_blk_nr * 0.9))
			{
				THROW_ERROR(ERR_APP, L"logical saturation overflow, logical_saturation=%d, logical_block=%d", m_health_info.m_logical_saturation, m_health_info.m_logical_blk_nr);
			}
		}
		direct_node->data[index] = dpage;
		direct_node->data_page->dirty = true;
		dpage->dirty = true;
		//dpage->temp = get_temperature(inode, start_blk);
		dpage->host_write++;
		inode->m_host_write++;

		//dpage->temp = BT_HOT__DATA;
		m_segments.WriteBlockToSeg(dpage, GetBlockTemp(dpage) );
		// 将ipath移动到下一个offset 
		m_segments.CheckGarbageCollection(this);
		NextOffset(ipath);
	}
//	FileFlush(fid);
	//m_segments.CheckGarbageCollection();

	// 更新ipath
	UpdateInode(inode);
}

void CF2fsSimulator::FileRead(std::vector<CPageInfoBase*>& blks, FID fid, size_t offset, size_t secs)
{
	// sanity check
	// 计算起始block和结束block，end_block是最后要写入的下一个。blk_nr=end_block - start_block
	DWORD start_blk, end_blk;
	LbaToBlock(start_blk, end_blk, offset, secs);

	CInodeInfo* inode = dynamic_cast<CInodeInfo*>(m_inodes.get_node(fid));
	JCASSERT(inode);
	if (end_blk > inode->m_blks)
	{
		//	LOG_WARNING(L"Oversize on reading file, fid=%d,blks=%d,file_blks=%d", fid, end_blk, inode->inode_blk.m_blks);
	}

	CIndexPath ipath(inode);
	CNodeInfoBase* direct_node = nullptr;
	for (; start_blk < end_blk; start_blk++)
	{
		// 查找PHY_BLK定位到
		if (ipath.level < 0)
		{
			OffsetToIndex(ipath, start_blk, false);
			direct_node = ipath.node[ipath.level];
		}
		// 获取index的位置，需要更新index
		int index = ipath.offset[ipath.level];
		if (direct_node && direct_node->data[index])
		{
			CPageInfo* dpage = direct_node->data[index];

			if (dpage->inode == nullptr || dpage->inode->m_fid != fid || dpage->offset != start_blk)
			{
				THROW_ERROR(ERR_APP, L"L2P not match, fid=%d, lblk=%d, phy_blk=%X, fid_p2f=%d, lblk_p2f=%d", 
					fid, start_blk, dpage->phy_blk, dpage->inode?dpage->inode->m_fid:INVALID_BLK, dpage->offset);
			}
			blks.push_back(dpage);
		}
		else blks.push_back(nullptr);
		// 将ipath移动到下一个offset 
		NextOffset(ipath);
	}
}

void CF2fsSimulator::FileTruncate(FID fid)
{
	// 文件的所有block都无效，然后保存inode
	CInodeInfo* inode = dynamic_cast<CInodeInfo*>(m_inodes.get_node(fid));
	JCASSERT(inode);
	CIndexPath ipath(inode);

	CNodeInfoBase* direct_node = nullptr;
	for (LBLK_T bb = 0; bb < inode->m_blks; /*++bb*/)
	{
		if (ipath.level < 0)
		{
			OffsetToIndex(ipath, bb, false);
			direct_node = ipath.node[ipath.level];
		}
		if (!direct_node)
		{
			bb += INDEX_SIZE;
			ipath.level = -1;
			continue;
		}
		int index = ipath.offset[ipath.level];
		CPageInfo* dpage = direct_node->data[index];
		if (dpage)
		{
			InterlockedDecrement(&m_health_info.m_logical_saturation);
			InvalidBlock("TRANCATE", dpage->phy_blk);
			direct_node->data[index] = nullptr;
			direct_node->data_page->dirty = true;
			direct_node->valid_data--;
			delete dpage;
		}
		NextOffset(ipath);
		bb++;
	}
	// 更新ipath
	UpdateInode(inode);
}

void CF2fsSimulator::FileDelete(FID fid)
{
	// 删除文件，回收inode
	LOG_TRACK(L"fs", L",DeleteFile,fid=%d", fid);

	CInodeInfo* inode = dynamic_cast<CInodeInfo*>(m_inodes.get_node(fid));
	JCASSERT(inode);
	if (inode->m_ref_count > 0) THROW_ERROR(ERR_APP, L"file is still referenced, fid=%d", fid);
	FileTruncate(fid);

	InvalidBlock("DELETE_NODE", inode->data_page->phy_blk);
	// page 在free_inode中删除
	m_inodes.free_inode(fid);
}

void CF2fsSimulator::FileFlush(FID fid)
{
	return;
	// 文件的所有block都无效，然后保存inode
	CInodeInfo* inode = dynamic_cast<CInodeInfo*>(m_inodes.get_node(fid));
	JCASSERT(inode);
	CIndexPath ipath(inode);
//	InitIndexPath(ipath, inode);
	CNodeInfoBase* direct_node = nullptr;
	for (LBLK_T bb = 0; bb < inode->m_blks;/* ++bb*/)
	{
		if (ipath.level < 0)
		{
			OffsetToIndex(ipath, bb, false);
			direct_node = ipath.node[ipath.level];
		}
		int index = ipath.offset[ipath.level];
		if (!direct_node)
		{
			bb += INDEX_SIZE;
			ipath.level = -1;
			continue;
		}
		CPageInfo* page = direct_node->data[index];
		if (page)
		{
			if (page->dirty)
			{
				m_segments.WriteBlockToSeg(page, GetBlockTemp(page) );
				direct_node->data_page->dirty = true;
			}
		}
		NextOffset(ipath);
		bb++;
	}
	// 更新ipath
	UpdateInode(inode);
}

void CF2fsSimulator::DumpSegments(const std::wstring& fn, bool sanity_check)
{
	FILE* log = nullptr;
	_wfopen_s(&log, fn.c_str(), L"w+");
	if (log == nullptr) THROW_ERROR(ERR_APP, L"failed on opening file %s", fn.c_str());
	fprintf_s(log, "seg,blk_nr,invalid,hot_node,hot_data,warm_node,warm_data,codl_node,cold_data\n");
	for (SEG_T ss = 0; ss < m_segments.get_seg_nr(); ++ss)
	{
		UINT blk_count[BT_TEMP_NR + 1];
		memset(blk_count, 0, sizeof(UINT) * (BT_TEMP_NR + 1));

		SEG_INFO<CPageInfo*>& seg = m_segments.get_segment(ss);

		for (BLK_T bb = 0; bb < BLOCK_PER_SEG; ++bb)
		{
			CPageInfo * page = seg.blk_map[bb];
			if (page == nullptr)
			{
				blk_count[BT_TEMP_NR]++;
				continue;
			}
			PHY_BLK src_phy = PhyBlock(ss, bb);

			//if (page->offset == INVALID_BLK)
			if (page->data != nullptr)
			{	// inode / index node
				JCASSERT(page->type == CPageInfo::PAGE_NODE);
				blk_count[BT_HOT__NODE]++;
				// sanity check
				if (sanity_check)
				{
					if (page->phy_blk != src_phy)
					{
						THROW_ERROR(ERR_APP, L"node P2L does not match, phy_blk=%X, phy_in_page=%X", src_phy, page->phy_blk);
					}
				}
			}
			else
			{	// data block
				JCASSERT(page->type == CPageInfo::PAGE_DATA);
				blk_count[BT_HOT__DATA] ++;
				// sanity check
				if (sanity_check)
				{
					CInodeInfo* inode = page->inode;
					JCASSERT(inode);
					CIndexPath ipath(inode);
					OffsetToIndex(ipath, page->offset, false);
					CNodeInfoBase * direct_node = ipath.node[ipath.level];
					int index = ipath.offset[ipath.level];

					if (direct_node == nullptr || direct_node->data[index] == nullptr)
					{
						THROW_ERROR(ERR_APP, L"data P2L does not match, phy=%X, fid=%d, offset=%d, page=null", PhyBlock(ss,bb), inode->m_fid, page->offset);
					}
					CPageInfo* ipage = direct_node->data[index];
					if (ipage->phy_blk != PhyBlock(ss, bb))
					{
						THROW_ERROR(ERR_APP, L"data P2L does not match, phy_blk=%X, fid=%d, offset=%d, phy_in_page=%X", PhyBlock(ss,bb), inode->m_fid, page->offset, page->phy_blk);
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

void CF2fsSimulator::DumpSegmentBlocks(const std::wstring& fn)
{
	m_segments.DumpSegmentBlocks(fn);
}


// 将inode写入segment中
PHY_BLK CF2fsSimulator::SyncInode(CPageInfo* ipage)
{
	// 更新inode
	CNodeInfoBase* inode = ipage->data;

	bool free_seg = false;
	if (!ipage->dirty) return ipage->phy_blk;
	if (inode->m_type == CNodeInfoBase::NODE_INDEX && inode->valid_data == 0)
	{	// 删除index block
		CDirectInfo* index_blk = dynamic_cast<CDirectInfo*>(inode);
		CNodeInfoBase* parent = inode->parent;	JCASSERT(parent);
		parent->data[ipage->offset] = nullptr;
		parent->data_page->dirty = true;

//		INVALID_PHY_BLOCK("WRITE_NODE", inode->m_phy_blk);
		InvalidBlock("WRITE_NODE", inode->data_page->phy_blk);
		delete inode->data_page;
		inode->data_page = nullptr;
		m_inodes.free_inode(ipage->phy_blk);
		return INVALID_BLK;
	}
	else if (inode->m_type == CNodeInfoBase::NODE_INODE)
	{
		//ipage->temp = BT_HOT__NODE;
		m_segments.WriteBlockToSeg(ipage, GetBlockTemp(ipage) );
		// 更新父节点指针
		if (inode->parent)
		{
			inode->parent->data_page->dirty = true;
		}
//		TrackIo(WRITE_NODE, inode->m_phy_blk, inode->m_fid, INVALID_BLK);
		m_segments.CheckGarbageCollection(this);
		ipage->dirty = false;
		return ipage->phy_blk;
	}
	else return INVALID_BLK;
}

void CF2fsSimulator::UpdateInode(CInodeInfo* inode)
{
	// 更新ipath
	LBLK_T bb = 0;
	for (size_t ii = 0; (ii < MAX_TABLE_SIZE) && (bb<inode->m_blks); ++ii, bb+=INDEX_SIZE)
	{
		CPageInfo* ipage = inode->data[ii];
		if (!ipage) continue;

		CNodeInfoBase* index_blk = ipage->data;
		if (index_blk == nullptr) THROW_ERROR(ERR_APP, L"index block in page is null, fid=%d, index=%d", inode->m_fid, ii);
		if (index_blk->data_page != ipage) THROW_ERROR(ERR_APP, L"data unmatch, fid=%d, index=%d, page=%p, page_in_blk=%p",
			inode->m_fid, ii, ipage, index_blk->data_page);
		//ipage->temp = BT_HOT__NODE;
		CDirectInfo* direct_blk = dynamic_cast<CDirectInfo*>(index_blk);
		JCASSERT(index_blk);
		if (direct_blk->valid_data == 0)
		{
			InvalidBlock("", ipage->phy_blk);
			m_inodes.free_inode(index_blk->m_fid);
			inode->data[ii] = nullptr;
			inode->data_page->dirty = true;
			continue;
		}
		if (ipage->dirty == false) continue;
		ipage->host_write++;
		m_segments.WriteBlockToSeg(ipage, GetBlockTemp(ipage) );
		inode->data_page->dirty = true;
	}

	CPageInfo* ipage = inode->data_page;
	PHY_BLK old_phy = ipage->phy_blk;
	if (ipage->dirty)
	{
		//ipage->temp = BT_HOT__NODE;
		ipage->host_write++;
		m_segments.WriteBlockToSeg(ipage, GetBlockTemp(ipage) );
	}
	LOG_TRACK(L"inode", L",UPDATE,fid=%d,new_phy=%X,old_phy=%X", inode->m_fid, ipage->phy_blk, old_phy);
}

BLK_TEMP CF2fsSimulator::GetBlockTemp(CPageInfo* page)
{
#if (MULTI_HEAD==1)
	return BT_HOT__DATA;
#elif (MULTI_HEAD==2)
	if (page->type == CPageInfo::PAGE_NODE) return BT_HOT__NODE;
	else if (page->type == CPageInfo::PAGE_DATA) return BT_HOT__DATA;
	else JCASSERT(0);
#elif (MULTI_HEAD==4)
	if (page->type == CPageInfo::PAGE_NODE)
	{
		JCASSERT(page->data);
		if (page->data->m_type == CNodeInfoBase::NODE_INODE) return BT_HOT__NODE;
		else if (page->data->m_type == CNodeInfoBase::NODE_INDEX) return BT_COLD_NODE;
		else JCASSERT(0);
	}
	else if (page->type == CPageInfo::PAGE_DATA)
	{
		if (page->host_write >= 3) return BT_HOT__DATA;
		else return BT_COLD_DATA;
	}
	else JCASSERT(0);

#else
	JCASSERT(0);
#endif
	return BT_HOT__DATA;

}

#define FLUSH_FILE_MAP(phy)	{\
	if (start_phy != INVALID_BLK) {\
		SEG_T seg; BLK_T blk; BlockToSeg(seg,blk,start_phy);	\
		fprintf_s(out, "%S,%d,%d,%d,%X,%d,%d,%d,%d\n", inode->m_fn.c_str(), fid, start_offset, (bb - start_offset), start_phy,seg,blk,host_write,media_write);}\
		host_write=0, media_write=0; start_phy = phy; pre_phy = phy; }

void CF2fsSimulator::DumpFileMap_merge(FILE* out, FID fid)
{
	CInodeInfo* inode = dynamic_cast<CInodeInfo*>(m_inodes.get_node(fid));
	JCASSERT(inode);
	CIndexPath ipath(inode);
	CNodeInfoBase* direct_node = nullptr;

	PHY_BLK start_phy = INVALID_BLK, pre_phy = 0;
	LBLK_T start_offset = INVALID_BLK, pre_offset = INVALID_BLK;
	LBLK_T bb = 0;
	UINT host_write = 0, media_write = 0;

	for (bb = 0; bb < inode->m_blks; /*++bb*/)
	{
		if (ipath.level < 0)
		{
			OffsetToIndex(ipath, bb, false);
			direct_node = ipath.node[ipath.level];
		}
		int index = ipath.offset[ipath.level];
		if (!direct_node)
		{	// 空的index block
			FLUSH_FILE_MAP(INVALID_BLK);
			bb += INDEX_SIZE;
			ipath.level = -1;
			continue;
		}
		
		if (direct_node->data[index])
		{
			CPageInfo* page = direct_node->data[index];
			PHY_BLK phy_blk = page->phy_blk;
			if (phy_blk == pre_phy + 1)
			{
				host_write += page->host_write;
				media_write += page->media_write;
				pre_phy = phy_blk;
				pre_offset = bb;
			}
			else
			{	// out
				FLUSH_FILE_MAP(phy_blk);
				start_offset = bb;
			}
			// sanity check
			if (page->inode->m_fid != fid || page->offset != bb)
			{
				THROW_ERROR(ERR_APP, L"L2P not match, fid=%d, lblk=%d, phy_blk=%X, fid_p2f=%d, lblk_p2f=%d",
					fid, bb, phy_blk, page->inode->m_fid, page->offset);
			}
		}
		else { FLUSH_FILE_MAP(INVALID_BLK); }
		bb++;
		NextOffset(ipath);
	}
	FLUSH_FILE_MAP(INVALID_BLK);
}

void CF2fsSimulator::DumpFileMap_no_merge(FILE* out, FID fid)
{
	CInodeInfo* inode = dynamic_cast<CInodeInfo*>(m_inodes.get_node(fid));
	JCASSERT(inode);
	CIndexPath ipath(inode);
	CNodeInfoBase* direct_node = nullptr;

	PHY_BLK start_phy = INVALID_BLK, pre_phy = 0;
	LBLK_T start_offset = INVALID_BLK, pre_offset = INVALID_BLK;
	LBLK_T bb = 0;
	for (bb = 0; bb < inode->m_blks; /*++bb*/)
	{
		if (ipath.level < 0)
		{
			OffsetToIndex(ipath, bb, false);
			direct_node = ipath.node[ipath.level];
		}
		int index = ipath.offset[ipath.level];
		if (!direct_node)
		{
			bb += INDEX_SIZE;
			ipath.level = -1;
			continue;
		}

		if (direct_node->data[index])
		{	// output
			CPageInfo* page = direct_node->data[index];
			PHY_BLK phy_blk = page->phy_blk;

			if (phy_blk != INVALID_BLK)
			{
				SEG_T seg; BLK_T seg_blk;
				BlockToSeg(seg, seg_blk, phy_blk);
				CPageInfo * page = m_segments.get_block(phy_blk);
				fprintf_s(out, "%S,%d,%d,1,%X,%d,%d,%d,%d\n",
					inode->m_fn.c_str(), fid, bb, phy_blk, seg, seg_blk, page->host_write, page->media_write);

				// sanity check
				if (page->inode->m_fid != fid || page->offset != bb)
				{
					THROW_ERROR(ERR_APP, L"L2P not match, fid=%d, lblk=%d, phy_blk=%X, fid_p2f=%d, lblk_p2f=%d",
						fid, bb, phy_blk, page->inode->m_fid, page->offset);
				}
			}
		}
		NextOffset(ipath);
		bb++;
	}
	//	FLUSH_FILE_MAP(INVALID_BLK);
}

//bool CF2fsSimulator::InvalidBlock(const char* reason, CPageInfo* page)
bool CF2fsSimulator::InvalidBlock(const char* reason, PHY_BLK phy_blk)
{
//	if (!page) return false;
//	PHY_BLK phy_blk = page->phy_blk;
	if (phy_blk == INVALID_BLK) return false;

	bool free_seg = m_segments.InvalidBlock(phy_blk);
	SEG_T _seg; 
	BLK_T _blk;		
	BlockToSeg(_seg,_blk,phy_blk);
	fprintf(m_log_invalid_trace, "%lld,%s,%d,%d,%d\n",
		m_write_count++/*index*/, reason ? reason : "",
		phy_blk/*blk_invalid*/, _seg, _blk);

	return free_seg;
}

void CF2fsSimulator::OffsetToIndex(CIndexPath& ipath, LBLK_T offset, bool alloc)
{
	// 从inode中把已经有的index block填入node[]中
	ipath.level = 1;
	ipath.offset[0] = offset / INDEX_SIZE;

	CNodeInfoBase* node = ipath.node[0];

	ipath.page[1] = node->data[ipath.offset[0]];

	if (ipath.page[1] == nullptr)
	{
		if (alloc)
		{
			ipath.node[1] = m_inodes.allocate_inode(CNodeInfoBase::NODE_INDEX, node);
			CPageInfo * dpage = ipath.node[1]->data_page;
			dpage->offset =  ipath.offset[0];
			dpage->dirty = true;

			ipath.page[1] = dpage;
			node->data[ipath.offset[0]] = dpage;
		}
	}
	else
	{
		ipath.node[1] = ipath.page[1]->data;
	}
	//ipath.index_node[1] = ipath.node[1]->m_fid;
	ipath.offset[1] = offset % INDEX_SIZE;
	// 计算每一层的偏移量
}

void CF2fsSimulator::NextOffset(CIndexPath& ipath)
{
	ipath.offset[1]++;
	if (ipath.offset[1] >= INDEX_SIZE)
	{
//		LOG_DEBUG_(1, L"reset ipath, level=%d, offset=%d", ipath.level, ipath.offset[ipath.level]);
		ipath.level = -1;
		ipath.node[1] = nullptr;
	}
}

void CF2fsSimulator::FileOpen(FID fid, bool delete_on_close)
{
	LOG_TRACK(L"fs", L",OpenFile,fid=%d,delete=%d", fid, delete_on_close);
	CInodeInfo* inode = dynamic_cast<CInodeInfo*>(m_inodes.get_node(fid));
	JCASSERT(inode);
	if (inode->data_page->phy_blk == INVALID_BLK) THROW_ERROR(ERR_APP, L"open an invalid file, fid=%d", fid);
	inode->m_delete_on_close = delete_on_close;
	InterlockedIncrement(&inode->m_ref_count);
}

void CF2fsSimulator::FileClose(FID fid)
{
	CInodeInfo* inode = dynamic_cast<CInodeInfo*>(m_inodes.get_node(fid));
	JCASSERT(inode);
	InterlockedDecrement(&inode->m_ref_count);
	LOG_TRACK(L"fs", L",CloseFile,fid=%d,delete=%d", fid, inode->m_delete_on_close);
	if (inode->m_delete_on_close) FileDelete(fid);
}

void CF2fsSimulator::DumpAllFileMap(const std::wstring& fn)
{
	FILE* log = nullptr;
	_wfopen_s(&log, fn.c_str(), L"w+");
	if (log == nullptr) THROW_ERROR(ERR_APP, L"failed on opening file %s", fn.c_str());
	fprintf_s(log, "fn,fid,start_offset,lblk_nr,phy_blk,seg_id,blk_id,host_write,media_write\n");

	for (FID ii = 0; ii < m_inodes.get_node_nr(); ++ii)
	{
		CNodeInfoBase* inode = m_inodes.get_node(ii);
		if (!inode || inode->m_type != inode_info::NODE_INODE) continue;
		DumpFileMap(log, ii);
	}
	fclose(log);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//template <> void TypedInvalidBlock<CPageInfo*>(CPageInfo*& blk)
//{
//	blk = nullptr;
//}


PHY_BLK CF2fsSegmentManager::WriteBlockToSeg(const CF2fsSegmentManager::_BLK_TYPE & lblk, BLK_TEMP temp)
{
	JCASSERT(temp < BT_TEMP_NR);
	CPageInfo* page = const_cast<CPageInfo*>(lblk);

	SEG_T& cur_seg_id = m_cur_segs[temp];
	if (cur_seg_id == INVALID_BLK) cur_seg_id = AllocSegment(temp);
	//	seg_id = m_cur_seg;
	SEG_INFO<CPageInfo*>& seg = m_segments[cur_seg_id];

	BLK_T blk_id = seg.cur_blk;
	page->media_write++;
	seg.blk_map[blk_id] = page;


	seg.valid_blk_nr++;
	seg.cur_blk++;

	InterlockedIncrement64(&m_health->m_total_media_write);
	InterlockedDecrement(&m_health->m_free_blk);
	InterlockedIncrement(&m_health->m_physical_saturation);

	if (page->type == CPageInfo::PAGE_NODE) m_health->m_media_write_node++;
	else m_health->m_media_write_data++;

	SEG_T tar_seg = cur_seg_id;
	BLK_T tar_blk = blk_id;
	PHY_BLK old_blk = page->phy_blk;
	PHY_BLK phy_blk = PhyBlock(tar_seg, tar_blk);
	page->phy_blk = phy_blk;
	if (old_blk != INVALID_BLK) InvalidBlock(old_blk);

	page->dirty = false;

	if (seg.cur_blk >= BLOCK_PER_SEG)
	{	// 当前segment已经写满
		cur_seg_id = INVALID_BLK;
	}

	return  phy_blk;
}

void CF2fsSegmentManager::GarbageCollection(CF2fsSimulator* fs)
{
	LOG_STACK_TRACE();
	GcPool<64, CPageInfo*> pool(m_segments);
	for (SEG_T ss = 0; ss < m_seg_nr; ss++)
	{
		SEG_INFO<CPageInfo*>& seg = m_segments[ss];
		if (seg.cur_blk < BLOCK_PER_SEG) continue;  // 跳过未写满的segment
		pool.Push(ss);
	}

	pool.LargeToSmall();
	SEG_T free_before_gc = m_free_nr;
	SEG_T claimed_seg = 0;

	LONG64 media_write_before = m_health->m_total_media_write;
	LONG64 host_write_before = m_health->m_total_host_write;
	UINT media_write_count = 0;
	while (m_free_nr < m_gc_hi)
	{
		SEG_T min_seg = pool.Pop();
		if (min_seg == INVALID_BLK)
		{
			// GC pool中的block都以取完，如果有block被回收，则暂时停止GC。否则报错
			if (m_free_nr >= m_gc_lo) break;
			THROW_ERROR(ERR_APP, L"cannot find segment which has invalid block");
		}
		SEG_INFO<CPageInfo*>& src_seg = m_segments[min_seg];
		for (BLK_T bb = 0; bb < BLOCK_PER_SEG; ++bb)
		{
			CPageInfo* blk = src_seg.blk_map[bb];
			if (blk == nullptr/* || blk->inode == nullptr*/) continue;
			JCASSERT(blk->inode);
			//PHY_BLK old_phy = PhyBlock(min_seg, bb);
			WriteBlockToSeg(blk, fs->GetBlockTemp(blk) );
			media_write_count++;
		}
		claimed_seg++;
	}
	LOG_TRACK(L"gc", L"SUM,free_before=%d,free_after=%d,released_seg=%d,src_nr=%d,host_write=%lld,%lld,media_write=%lld,%lld,delta=%d,media_write=%d",
		free_before_gc, m_free_nr, m_free_nr - free_before_gc, claimed_seg,
		host_write_before, m_health->m_total_host_write, media_write_before, m_health->m_total_media_write,
		(UINT)(m_health->m_total_media_write - media_write_before), media_write_count);
	if (m_gc_trace)
	{
		//fprintf_s(m_gc_trace, "free_before,free_after,reclaimed,src_sge,media_write\n");
		fprintf_s(m_gc_trace, "%d,%d,%d,%d,%d,%d\n",
			free_before_gc, m_free_nr, m_free_nr - free_before_gc, claimed_seg,
			(UINT)(m_health->m_total_media_write - media_write_before), media_write_count);
	}


}

#define FLUSH_SEGMENT_BLOCK_OUT(_blk)	\
	if (pre_fid != INVALID_BLK) {	\
		fprintf_s(log, "%d,%d,%d,1,DATA,%d,%d\n", ss, start_blk, (_blk-start_blk), pre_fid, start_lblk);	\
		pre_fid = INVALID_BLK; start_blk = _blk; start_lblk = 0; pre_lblk = 0; }

void CF2fsSegmentManager::DumpSegmentBlocks(const std::wstring& fn)
{
	// for merge
	FID pre_fid = INVALID_BLK;
	LBLK_T start_lblk = 0, pre_lblk = 0;
	BLK_T start_blk = 0;


	FILE* log = nullptr;
	_wfopen_s(&log, fn.c_str(), L"w+");
	if (log == nullptr) THROW_ERROR(ERR_APP, L"failed on opening file %s", fn.c_str());
	fprintf_s(log, "seg,blk,blk_num,valid,type,fid,lblk\n");
	for (SEG_T ss = 0; ss < m_seg_nr; ++ss)
	{
		SEG_INFO<CPageInfo*>& seg = m_segments[ss];
		for (BLK_T bb = 0; bb < BLOCK_PER_SEG; ++bb)
		{
			CPageInfo * blk = seg.blk_map[bb];
			if (blk == nullptr || blk->inode == nullptr)
			{	// invalid block：不输出
				FLUSH_SEGMENT_BLOCK_OUT(bb);
				continue;
			}
			if (blk->offset == INVALID_BLK)
			{	// node
				FLUSH_SEGMENT_BLOCK_OUT(bb);
				fprintf_s(log, "%d,%d,1,NODE,%d,xxxx\n", ss, bb, blk->inode->m_fid);
				continue;
			}
			// data block
			if (pre_fid == blk->inode->m_fid && (pre_lblk + 1) == blk->offset)
			{	// merge
				pre_lblk++;
			}
			else
			{	// reflush
				FLUSH_SEGMENT_BLOCK_OUT(bb);
				pre_fid = blk->inode->m_fid;
				start_blk = bb;
				start_lblk = blk->offset;
				pre_lblk = blk->offset;
			}
		}
		FLUSH_SEGMENT_BLOCK_OUT(BLOCK_PER_SEG);
	}
	fclose(log);
}

bool CF2fsSegmentManager::InitSegmentManager(SEG_T segment_nr, SEG_T gc_lo, SEG_T gc_hi, int init)
{
	InitSegmentManagerBase(segment_nr, gc_lo, gc_hi, 0);
	if (m_gc_trace)
	{
		// free_before: GG前的free segment，free_after: GC后的free segment, reclaimed_seg: GC释放的segment数量
		// src_seg: GC中使用的segment数量（和released相比较可以体现GC效率)
		// 通过GC前后health.media_write计算得到的media write block数量
		// GC过程中累加的media write block数量（这两者只是计算方法不同，理论上数值应该相同）
		fprintf_s(m_gc_trace, "free_before,free_after,released_seg,src_sge,media_write,media_write_gc\n");
	}
	return true;
}

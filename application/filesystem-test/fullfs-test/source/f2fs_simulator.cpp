///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
#include "pch.h"
#include "f2fs_simulator.h"

LOCAL_LOGGER_ENABLE(L"simulator.f2fs", LOGGER_LEVEL_DEBUGINFO);

//#define MULTI_HEAD	1
static const char* BLK_TEMP_NAME[] = { "COLD_DATA", "COLD_NODE", "WARM_DATA", "WARM_NODE", "HOT__DATA", "HOT__NODE", "EMPTY"};


CF2fsSimulator::~CF2fsSimulator(void)
{
	// ɾ������file
	for (FID ii = 0; ii < m_inodes.get_node_nr(); ++ii)
	{
		CNodeInfoBase* inode = m_inodes.get_node(ii);
		if (!inode || inode->m_type != inode_info::NODE_INODE) continue;
		CInodeInfo* _inode = dynamic_cast<CInodeInfo*>(inode);
		JCASSERT(_inode);
		_inode->m_ref_count = 0;
		// Node::�˴�ɾ���ļ��Ƿ���writeͳ�Ƶ����ӣ�
		FileDelete(ii);
	}
	if (m_inode_trace) fclose(m_inode_trace);
}

bool CF2fsSimulator::Initialzie(const boost::property_tree::wptree& config)
{
	m_segments.m_gc_trace = m_gc_trace;

	memset(&m_health_info, 0, sizeof(FsHealthInfo));
	m_multihead_cnt = config.get<int>(L"multi_header_num");
	wprintf_s(L"F2FS simulator, multihead=%d\n", m_multihead_cnt);
	size_t fs_size = config.get<size_t>(L"volume_size");
	m_health_info.m_logical_blk_nr = (LBLK_T)(ROUND_UP_DIV(fs_size, SECTOR_PER_BLOCK));
	float op = config.get<float>(L"over_provision");
	size_t phy_blk = (size_t)(m_health_info.m_logical_blk_nr * op);
	SEG_T seg_nr = (SEG_T)ROUND_UP_DIV(phy_blk, BLOCK_PER_SEG);

	m_gc_th_low = config.get<SEG_T>(L"gc_seg_lo");
	m_gc_th_hi = config.get<SEG_T>(L"gc_seg_hi");

	m_segments.SetHealth(&m_health_info);
	m_segments.InitSegmentManager(this, seg_nr, m_gc_th_low, m_gc_th_hi);
	m_segments.m_inodes = &m_inodes;

	m_health_info.m_seg_nr = seg_nr;
	m_health_info.m_blk_nr = seg_nr * BLOCK_PER_SEG;
	m_health_info.m_free_blk = m_health_info.m_blk_nr;

	memset(m_truncated_host_write, 0, sizeof(UINT64) * BT_TEMP_NR);
	memset(m_truncated_media_write, 0, sizeof(UINT64) * BT_TEMP_NR);

	return true;
}

FID CF2fsSimulator::FileCreate(const std::wstring& fn)
{
	// allocate inode
	CNodeInfoBase* inode = m_inodes.allocate_inode(CNodeInfoBase::NODE_INODE, nullptr);
	CInodeInfo* _inode = dynamic_cast<CInodeInfo*>(inode);
	_inode->m_fn = fn;
	fprintf_s(m_log_write_trace, "%lld,CREATE,%d,0,0\n", m_write_count++, _inode->m_fid);

	// ����inode
	UpdateInode(_inode,"CREATE");
	LOG_TRACK(L"fs", L",CreateFile,fn=%d,fid=%d", fn.c_str(), inode->m_fid);
	InterlockedIncrement(&_inode->m_ref_count);

	LOG_TRACK(L"inode", L",CREATE,fid=%d,phy=%X", inode->m_fid, inode->data_page->phy_blk);
	return inode->m_fid;
}

void CF2fsSimulator::FileWrite(FID fid, size_t offset, size_t secs)
{
	// ������ʼblock�ͽ���block��end_block�����Ҫд�����һ����blk_nr=end_block - start_block
	DWORD start_blk, end_blk;
	LbaToBlock(start_blk, end_blk, offset, secs);
	LOG_TRACK(L"fs", L",WriteFile,fid=%d,offset=%d,secs=%d", fid, start_blk, (end_blk - start_blk));

	CInodeInfo* inode = dynamic_cast<CInodeInfo*>(m_inodes.get_node(fid));
	JCASSERT(inode);

	if (end_blk > MaxFileSize()) THROW_ERROR(ERR_APP, L"file size is too large, blks=%d, max_size=%d", end_blk, MaxFileSize());

	if (end_blk > inode->m_blks) inode->m_blks = end_blk;
	CIndexPath ipath(inode);

	CNodeInfoBase* direct_node = nullptr;
	fprintf_s(m_log_write_trace, "%lld,WRITE,%d,%d,%d\n", m_write_count++, fid, start_blk, end_blk - start_blk);
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

		// ȷ�������¶�
		CPageInfo* dpage = direct_node->data[index];
		if (dpage)
		{	// ���ݴ���
//			dpage->host_write++; => �ظ�����
		}
		else
		{	// ���ݲ�����
			dpage = new CPageInfo;
			dpage->type = CPageInfo::PAGE_DATA;
			dpage->inode = inode;
			dpage->offset = start_blk;
			direct_node->valid_data++;
			// ����߼���û�б�д���������߼����Ͷ�
			InterlockedIncrement(&m_health_info.m_logical_saturation);
			if (m_health_info.m_logical_saturation >= (m_health_info.m_logical_blk_nr * 0.9))
			{
				THROW_ERROR(ERR_APP, L"logical saturation overflow, logical_saturation=%d, logical_block=%d", m_health_info.m_logical_saturation, m_health_info.m_logical_blk_nr);
			}
		}
		direct_node->data[index] = dpage;
		direct_node->data_page->dirty = true;
		dpage->dirty = true;

		InterlockedIncrement64(&m_health_info.m_total_host_write);
		dpage->host_write++;

		inode->m_host_write++;
		inode->m_media_write++;

		m_segments.WriteBlockToSeg(dpage);
		// ��ipath�ƶ�����һ��offset 
		m_segments.CheckGarbageCollection(this);
		NextOffset(ipath);
	}
	// ����ipath
	UpdateInode(inode,"WRITE");
}

void CF2fsSimulator::FileRead(std::vector<CPageInfoBase*>& blks, FID fid, size_t offset, size_t secs)
{
	// sanity check
	// ������ʼblock�ͽ���block��end_block�����Ҫд�����һ����blk_nr=end_block - start_block
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
		// ����PHY_BLK��λ��
		if (ipath.level < 0)
		{
			OffsetToIndex(ipath, start_blk, false);
			direct_node = ipath.node[ipath.level];
		}
		// ��ȡindex��λ�ã���Ҫ����index
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
		// ��ipath�ƶ�����һ��offset 
		NextOffset(ipath);
	}
}

void CF2fsSimulator::FileTruncate(FID fid)
{
	// �ļ�������block����Ч��Ȼ�󱣴�inode
	fprintf_s(m_log_write_trace, "%lld,TRUNCATE,%d,0,0\n", m_write_count++, fid);
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
			// ��¼truncated��WAF
			BLK_TEMP temp = dpage->ttemp;
			JCASSERT(temp < BT_TEMP_NR);
			m_truncated_host_write[temp] += dpage->host_write;
			m_truncated_media_write[temp] += dpage->media_write;

			delete dpage;
		}
		NextOffset(ipath);
		bb++;
	}
	// ����ipath
	UpdateInode(inode, "TRUNCATE");
}

void CF2fsSimulator::FileDelete(FID fid)
{
	fprintf_s(m_log_write_trace, "%lld,DELETE,%d,0,0\n", m_write_count++, fid);
	// ɾ���ļ�������inode
	LOG_TRACK(L"fs", L",DeleteFile,fid=%d", fid);

	CInodeInfo* inode = dynamic_cast<CInodeInfo*>(m_inodes.get_node(fid));
	JCASSERT(inode);
	if (inode->m_ref_count > 0) THROW_ERROR(ERR_APP, L"file is still referenced, fid=%d", fid);
	FileTruncate(fid);

	InvalidBlock("DELETE_NODE", inode->data_page->phy_blk);
	// page ��free_inode��ɾ��
			//ͳ�Ʊ����յ�inode��WAF
	CPageInfo * ipage = inode->data_page;
	BLK_TEMP temp = ipage->ttemp;
	JCASSERT(temp < BT_TEMP_NR);
	m_truncated_host_write[temp] += ipage->host_write;
	m_truncated_media_write[temp] += ipage->media_write;
	m_inodes.free_inode(fid);
}

void CF2fsSimulator::FileFlush(FID fid)
{
	fprintf_s(m_log_write_trace, "%lld,FLUSH,%d,0,0\n", m_write_count++, fid);
	return;
	// �ļ�������block����Ч��Ȼ�󱣴�inode
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
				m_segments.WriteBlockToSeg(page);
				direct_node->data_page->dirty = true;
			}
		}
		NextOffset(ipath);
		bb++;
	}
	// ����ipath
	UpdateInode(inode,"FLUSH");
}

void CF2fsSimulator::DumpSegments(const std::wstring& fn, bool sanity_check)
{
	FILE* log = nullptr;
	_wfopen_s(&log, fn.c_str(), L"w+");
	if (log == nullptr) THROW_ERROR(ERR_APP, L"failed on opening file %s", fn.c_str());
	fprintf_s(log, "seg,seg_temp,erase_cnt,blk_nr,invalid,hot_node,hot_data,warm_node,warm_data,cold_node,cold_data\n");

	UINT blk_count[BT_TEMP_NR + 1];
	for (SEG_T ss = 0; ss < m_segments.get_seg_nr(); ++ss)
	{
		memset(blk_count, 0, sizeof(UINT) * (BT_TEMP_NR + 1));
		SEG_INFO<CPageInfo*>& seg = m_segments.get_segment(ss);
//		BLK_TEMP seg_temp;	// ���ڼ���segment temperature
		for (BLK_T bb = 0; bb < BLOCK_PER_SEG; ++bb)
		{
			CPageInfo * page = seg.blk_map[bb];
			if (page == nullptr)
			{
				blk_count[BT_TEMP_NR]++;
				continue;
			}
			BLK_TEMP temp = page->ttemp;
			JCASSERT(temp < BT_TEMP_NR);
			blk_count[temp] ++;

/*			PHY_BLK src_phy = PhyBlock(ss, bb);
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
*/
		}
		// ����segment������
		BLK_TEMP seg_temp = seg.seg_temp;
		if (seg_temp >= BT_TEMP_NR) seg_temp = BT_TEMP_NR;
		fprintf_s(log, "%d,%s,%d,512,%d,%d,%d,%d,%d,%d,%d\n", ss, BLK_TEMP_NAME[seg_temp],seg.erase_count,
			blk_count[BT_TEMP_NR],
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



void CF2fsSimulator::UpdateInode(CInodeInfo* inode, const char* caller)
{
	// ��������Ƿ�һ����host����ģ�
	// ����ipath
	LBLK_T bb = 0;
	for (size_t ii = 0; (ii < MAX_TABLE_SIZE) && (bb<inode->m_blks); ++ii, bb+=INDEX_SIZE)
	{
		CPageInfo* ipage = inode->data[ii];
		if (!ipage) continue;

		CNodeInfoBase* index_blk = ipage->data;
		if (index_blk == nullptr) THROW_ERROR(ERR_APP, L"index block in page is null, fid=%d, index=%d", inode->m_fid, ii);
		if (index_blk->data_page != ipage) THROW_ERROR(ERR_APP, L"data unmatch, fid=%d, index=%d, page=%p, page_in_blk=%p",
			inode->m_fid, ii, ipage, index_blk->data_page);
		CDirectInfo* direct_blk = dynamic_cast<CDirectInfo*>(index_blk);
		JCASSERT(index_blk);
		if (direct_blk->valid_data == 0)
		{
			InvalidBlock("", ipage->phy_blk);
			//ͳ�Ʊ����յ�inode��WAF
			JCASSERT(ipage == index_blk->data_page);
			BLK_TEMP temp = ipage->ttemp;
			JCASSERT(temp < BT_TEMP_NR);
			m_truncated_host_write[temp] += ipage->host_write;
			m_truncated_media_write[temp] += ipage->media_write;

			m_inodes.free_inode(index_blk->m_fid);
			inode->data[ii] = nullptr;
			inode->data_page->dirty = true;
			continue;
		}
		if (ipage->dirty == false) continue;
		ipage->host_write++;
		m_segments.WriteBlockToSeg(ipage);
		//<TRACE>��¼inode�ĸ��������
		// opid, fid, inode����index id, ԭ�����ݸ���������
		fprintf_s(m_inode_trace, "%lld,%d,%lld,%s\n", m_write_count, inode->m_fid, ii, caller);
		inode->data_page->dirty = true;
	}

	CPageInfo* ipage = inode->data_page;
	PHY_BLK old_phy = ipage->phy_blk;
	if (ipage->dirty)
	{
		ipage->host_write++;
		m_segments.WriteBlockToSeg(ipage);
		//<TRACE>��¼inode�ĸ��������
		// opid, fid, inode����index id, ԭ�����ݸ���������
		fprintf_s(m_inode_trace, "%lld,%d,0,UPDATE\n", m_write_count, inode->m_fid);
	}
	LOG_TRACK(L"inode", L",UPDATE,fid=%d,new_phy=%X,old_phy=%X", inode->m_fid, ipage->phy_blk, old_phy);
}

BLK_TEMP CF2fsSimulator::GetBlockTemp(CPageInfo* page)
{
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
	return BT_HOT__DATA;
}

BLK_TEMP CF2fsSimulator::GetAlgorithmBlockTemp(CPageInfo* page, BLK_TEMP temp)
{
	if (m_multihead_cnt == 1)	return BT_HOT__DATA;
	else if (m_multihead_cnt == 2)
	{
		if (temp == BT_COLD_NODE || temp == BT_WARM_NODE || temp == BT_HOT__NODE) return BT_HOT__NODE;
		else return BT_HOT__DATA;
	}
	else if (m_multihead_cnt == 4) return temp;
	else JCASSERT(0);
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
		{	// �յ�index block
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

bool CF2fsSimulator::InvalidBlock(const char* reason, PHY_BLK phy_blk)
{
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
	// ��inode�а��Ѿ��е�index block����node[]��
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
	// ����ÿһ���ƫ����
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


void CF2fsSimulator::DumpBlockWAF(const std::wstring& fn)
{
	FILE* log = nullptr;
	_wfopen_s(&log, fn.c_str(), L"w+");
	if (log == nullptr) THROW_ERROR(ERR_APP, L"failed on opening file %s", fn.c_str());

	UINT64 host_write[BT_TEMP_NR + 1];
	UINT64 media_write[BT_TEMP_NR + 1];
	UINT64 blk_count[BT_TEMP_NR + 1];

	memset(host_write, 0,	sizeof(UINT64) * (BT_TEMP_NR + 1));
	memset(media_write,0,	sizeof(UINT64) * (BT_TEMP_NR + 1));
	memset(blk_count,0,		sizeof(UINT64) * (BT_TEMP_NR + 1));


	for (SEG_T ss = 0; ss < m_segments.get_seg_nr(); ++ss)
	{
		SEG_INFO<CPageInfo*>& seg = m_segments.get_segment(ss);
		for (BLK_T bb = 0; bb < BLOCK_PER_SEG; ++bb)
		{
			CPageInfo* page = seg.blk_map[bb];
			if (page == nullptr) {continue;}

			BLK_TEMP temp = page->ttemp;
			JCASSERT(temp < BT_TEMP_NR);
			host_write[temp] += page->host_write;
			media_write[temp] += page->media_write;
			blk_count[temp] ++;
		}
	}
	fprintf_s(log, "block_temp,host_write,media_write,WAF,blk_num,avg_host_write\n");
	for (int ii = 0; ii < BT_TEMP_NR; ++ii)
	{
		fprintf_s(log, "%s,%lld,%lld,%.2f,%lld,%.2f\n", BLK_TEMP_NAME[ii], host_write[ii], media_write[ii], 
			(float)(media_write[ii]) / (float)(host_write[ii]), 
			blk_count[ii], (float)(host_write[ii])/blk_count[ii]);
	}

	// �ֱ����data��node������
	UINT64 data_cnt = blk_count[BT_COLD_DATA] + blk_count[BT_WARM_DATA] + blk_count[BT_HOT__DATA];
	UINT64 data_host_wr = host_write[BT_COLD_DATA] + host_write[BT_WARM_DATA] + host_write[BT_HOT__DATA];
	UINT64 data_media_wr = media_write[BT_COLD_DATA] + media_write[BT_WARM_DATA] + media_write[BT_HOT__DATA];

	fprintf_s(log, "%s,%lld,%lld,%.2f,%lld,%.2f\n", "DATA", data_host_wr, data_media_wr,
		(float)(data_media_wr) / (float)(data_host_wr),
		data_cnt, (float)(data_host_wr) / data_cnt);

	UINT64 node_cnt = blk_count[BT_COLD_NODE] + blk_count[BT_WARM_NODE] + blk_count[BT_HOT__NODE];
	UINT64 node_host_wr = host_write[BT_COLD_NODE] + host_write[BT_WARM_NODE] + host_write[BT_HOT__NODE];
	UINT64 node_media_wr = media_write[BT_COLD_NODE] + media_write[BT_WARM_NODE] + media_write[BT_HOT__NODE];

	fprintf_s(log, "%s,%lld,%lld,%.2f,%lld,%.2f\n", "NODE", node_host_wr, node_media_wr,
		(float)(node_media_wr) / (float)(node_host_wr),
		node_cnt, (float)(node_host_wr) / node_cnt);

	fprintf_s(log, "%s,%lld,%lld,%.2f,\n", "TOTAL", m_health_info.m_total_host_write, m_health_info.m_total_media_write,
		(float)(m_health_info.m_total_media_write)/(float)(m_health_info.m_total_host_write));
//	fprintf_s(log, "%s,data:%lld,node:%lld,\n", "MEDIA", m_health_info.m_media_write_data, m_health_info.m_media_write_node);

	for (int ii = 0; ii < BT_TEMP_NR; ++ii)
	{
		fprintf_s(log, "TRUNC_%s,%lld,%lld,%.2f,%d,%.2f\n", BLK_TEMP_NAME[ii], 
			m_truncated_host_write[ii], m_truncated_media_write[ii], 
			(float)(m_truncated_media_write[ii]) / (float)(m_truncated_host_write[ii]), -1, -1.0);
	}
	fclose(log);
}

void CF2fsSimulator::GetConfig(boost::property_tree::wptree& config, const std::wstring& config_name)
{
	if (config_name == L"multi_header_num")
	{
//		config.put(L"multi_header_num", (int)m_multihead_cnt);
		config.put(L"multi_header_num", m_multihead_cnt);
	}
}

void CF2fsSimulator::SetLogFolder(const std::wstring& fn)
{
	__super::SetLogFolder(fn);
	// ���� inode host write trace
	_wfopen_s(&m_inode_trace, (fn + L"\\trace_inode.csv").c_str(), L"w+");
	if (m_inode_trace == nullptr) THROW_ERROR(ERR_APP, L"failed on creating log file: trace_inode.csv");
	fprintf_s(m_inode_trace, "op_id,fid,index,reason\n");

}

//PHY_BLK CF2fsSegmentManager::WriteBlockToSeg(const CF2fsSegmentManager::_BLK_TYPE& lblk, BLK_TEMP temp)
PHY_BLK CF2fsSegmentManager::WriteBlockToSeg(CPageInfo * page, bool by_gc)
{
	// ����page���¶ȣ���ʵ���¶ȣ�����ͳ�ƣ�
	page->ttemp = m_fs->GetBlockTemp(page);
	BLK_TEMP temp = m_fs->GetAlgorithmBlockTemp(page, page->ttemp);

	// �����¶ȣ��㷨�¶ȣ���page����segment��

	SEG_T& cur_seg_id = m_cur_segs[temp];
	if (cur_seg_id == INVALID_BLK) cur_seg_id = AllocSegment(temp);
	//	seg_id = m_cur_seg;
	SEG_INFO<CPageInfo*>& seg = m_segments[cur_seg_id];

	BLK_T blk_id = seg.cur_blk;
	page->media_write++;
	seg.blk_map[blk_id] = page;

	seg.valid_blk_nr++;
	seg.cur_blk++;

	InterlockedIncrement64(&m_health_info->m_total_media_write);
	InterlockedDecrement(&m_health_info->m_free_blk);
	InterlockedIncrement(&m_health_info->m_physical_saturation);

	if (page->type == CPageInfo::PAGE_NODE) m_health_info->m_media_write_node++;
	else m_health_info->m_media_write_data++;

	SEG_T tar_seg = cur_seg_id;
	BLK_T tar_blk = blk_id;
	PHY_BLK old_blk = page->phy_blk;
	PHY_BLK phy_blk = PhyBlock(tar_seg, tar_blk);
	page->phy_blk = phy_blk;
	if (old_blk != INVALID_BLK) InvalidBlock(old_blk);

	// ����WriteBlockToSeg()����GC�б����ã�Ŀǰ��GC�㷨���segment����GC��GC�󲢲������dirty��־��
	if (!by_gc) page->dirty = false;

	if (seg.cur_blk >= BLOCK_PER_SEG)
	{	// ��ǰsegment�Ѿ�д��
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
		if (seg.cur_blk < BLOCK_PER_SEG) continue;  // ����δд����segment
		pool.Push(ss);
	}

	pool.LargeToSmall();
	SEG_T free_before_gc = m_free_nr;
	SEG_T claimed_seg = 0;

	LONG64 media_write_before = m_health_info->m_total_media_write;
	LONG64 host_write_before = m_health_info->m_total_host_write;
	UINT media_write_count = 0;
	while (m_free_nr < m_gc_hi)
	{
		SEG_T min_seg = pool.Pop();
		if (min_seg == INVALID_BLK)
		{
			// GC pool�е�block����ȡ�꣬�����block�����գ�����ʱֹͣGC�����򱨴�
			if (m_free_nr >= m_gc_lo) break;
			THROW_ERROR(ERR_APP, L"cannot find segment which has invalid block");
		}
		SEG_INFO<CPageInfo*>& src_seg = m_segments[min_seg];
		for (BLK_T bb = 0; bb < BLOCK_PER_SEG; ++bb)
		{
			CPageInfo* blk = src_seg.blk_map[bb];
			if (blk == nullptr) continue;
			JCASSERT(blk->inode);
			// ע�⣺GC��Ӧ�øı�block���¶ȣ�������һ�����
			BLK_TEMP before_temp = blk->ttemp;
			WriteBlockToSeg(blk,true);
			BLK_TEMP after_temp = blk->ttemp;
			JCASSERT(before_temp == after_temp);

			media_write_count++;
		}
		claimed_seg++;
	}
	LOG_TRACK(L"gc", L"SUM,free_before=%d,free_after=%d,released_seg=%d,src_nr=%d,host_write=%lld,%lld,media_write=%lld,%lld,delta=%d,media_write=%d",
		free_before_gc, m_free_nr, m_free_nr - free_before_gc, claimed_seg,
		host_write_before, m_health_info->m_total_host_write, media_write_before, m_health_info->m_total_media_write,
		(UINT)(m_health_info->m_total_media_write - media_write_before), media_write_count);
	if (m_gc_trace)
	{
		//fprintf_s(m_gc_trace, "free_before,free_after,reclaimed,src_sge,media_write\n");
		fprintf_s(m_gc_trace, "%d,%d,%d,%d,%d,%d\n",
			free_before_gc, m_free_nr, m_free_nr - free_before_gc, claimed_seg,
			(UINT)(m_health_info->m_total_media_write - media_write_before), media_write_count);
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
			{	// invalid block�������
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

bool CF2fsSegmentManager::InitSegmentManager(CF2fsSimulator* fs, SEG_T segment_nr, SEG_T gc_lo, SEG_T gc_hi, int init)
{
	InitSegmentManagerBase(fs, segment_nr, gc_lo, gc_hi, 0);
	if (m_gc_trace)
	{
		// free_before: GGǰ��free segment��free_after: GC���free segment, reclaimed_seg: GC�ͷŵ�segment����
		// src_seg: GC��ʹ�õ�segment��������released��ȽϿ�������GCЧ��)
		// ͨ��GCǰ��health.media_write����õ���media write block����
		// GC�������ۼӵ�media write block������������ֻ�Ǽ��㷽����ͬ����������ֵӦ����ͬ��
		fprintf_s(m_gc_trace, "free_before,free_after,released_seg,src_sge,media_write,media_write_gc\n");
	}
	return true;
}

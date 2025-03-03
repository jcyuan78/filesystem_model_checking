///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
#include "pch.h"
#include "../include/f2fs_simulator.h"
#include <boost/unordered_set.hpp>
#include <list>

LOCAL_LOGGER_ENABLE(L"simulator.f2fs", LOGGER_LEVEL_DEBUGINFO +1);

LOG_CLASS_SIZE(BLOCK_DATA);

LOG_CLASS_SIZE(INODE);
LOG_CLASS_SIZE(INDEX_NODE);
LOG_CLASS_SIZE(NODE_INFO);
LOG_CLASS_SIZE(DENTRY);
LOG_CLASS_SIZE(DENTRY_BLOCK);
LOG_CLASS_SIZE(NAT_BLOCK);
LOG_CLASS_SIZE(SIT_BLOCK);
LOG_CLASS_SIZE(SUMMARY);
LOG_CLASS_SIZE(SUMMARY_BLOCK);
LOG_CLASS_SIZE(CKPT_HEAD);
LOG_CLASS_SIZE(CKPT_NAT_JOURNAL);
LOG_CLASS_SIZE(CKPT_SIT_JOURNAL);

LOG_CLASS_SIZE(CKPT_BLOCK);
LOG_CLASS_SIZE(CStorage);
LOG_CLASS_SIZE(CF2fsSimulator);
//LOG_CLASS_SIZE();

//#define MULTI_HEAD	1
const char* BLK_TEMP_NAME[] = { "COLD_DATA", "COLD_NODE", "WARM_DATA", "WARM_NODE", "HOT__DATA", "HOT__NODE", "EMPTY" };

// type 1: 输出gc log, block count相关，2: 输出gc 性能分析，最大值VB，最小值VB，等
//#define GC_TRACE_TYPE 1
#define GC_TRACE_TYPE 2

CF2fsSimulator::CF2fsSimulator(void)
	:m_segments(this), m_storage(this), m_pages(this), m_nat(this)
{
	JCASSERT(END_META_BLK < (MAIN_SEG_OFFSET * BLOCK_PER_SEG))
	m_ref = 1;
}

CF2fsSimulator::~CF2fsSimulator(void)
{
#ifdef ENABLE_FS_TRACE
	if (m_inode_trace) fclose(m_inode_trace);
#endif
}

void CF2fsSimulator::Clone(IFsSimulator*& dst)
{
	JCASSERT(dst == nullptr)
	CF2fsSimulator* fs = new CF2fsSimulator;
	fs->InternalCopyFrom(this);
	dst = static_cast<IFsSimulator*>(fs);
//	LOG_DEBUG(L"overprovision segments = %d\n", m_op_segs);

}

void CF2fsSimulator::CopyFrom(const IFsSimulator* src)
{
	const CF2fsSimulator* _src = dynamic_cast<const CF2fsSimulator*>(src);
	if (_src == nullptr) THROW_FS_ERROR(ERR_GENERAL, L"cannot copy from non CF2fsSimulator, src=%p", src);
	InternalCopyFrom(_src);
//	LOG_DEBUG(L"overprovision segments = %d\n", m_op_segs);
}

void CF2fsSimulator::InternalCopyFrom(const CF2fsSimulator* _src)
{
	m_log_path = _src->m_log_path;
	memcpy_s(&m_health_info, sizeof(FsHealthInfo), &_src->m_health_info, sizeof(FsHealthInfo));
	memcpy_s(&m_pages, sizeof(CPageAllocator), &_src->m_pages, sizeof(CPageAllocator));
	m_multihead_cnt = _src->m_multihead_cnt;
	m_node_blks = _src->m_node_blks;
	m_segments.CopyFrom(_src->m_segments);
	m_storage.CopyFrom(&_src->m_storage);
	m_nat.CopyFrom(&_src->m_nat);
	memcpy_s(m_open_files, sizeof(m_open_files), _src->m_open_files, sizeof(m_open_files));
	m_free_ptr = _src->m_free_ptr;
	m_open_nr = _src->m_open_nr;
	memcpy_s(&m_checkpoint, sizeof(m_checkpoint), &_src->m_checkpoint, sizeof(m_checkpoint));
	m_op_segs = _src->m_op_segs;
	m_cur_ckpt = _src->m_cur_ckpt;
//	memcpy_s(&m_fs_info, sizeof(FS_INFO), &_src->m_fs_info, sizeof(FS_INFO));
}

bool CF2fsSimulator::SanityCheckConfig(void)
{
	LOG_DEBUG(L"== Main Segment ==");
	LOG_DEBUG(L"\t BLK per SEG=%d, SEG nr=%d, BLK nr=%d", BLOCK_PER_SEG, MAIN_SEG_NR, MAIN_BLK_NR);
	LOG_DEBUG(L"\t Start SEG no=%d, End SEG no=%d", MAIN_SEG_OFFSET, MAIN_SEG_OFFSET + MAIN_SEG_NR);
	LOG_DEBUG(L"== Checkpoint ==");
	LOG_DEBUG(L"\t Start CKPT=%d, CKPT nr=%d, SIT Journal nr=%d, NIT Journal nr=%d", CKPT_START_BLK, CKPT_BLK_NR, SIT_JOURNAL_BLK, 1);
	LOG_DEBUG(L"\t SIT Journal nr=%d, SIT Journal blk=%d, SIT Journal per blk=%d", JOURNAL_NR, SIT_JOURNAL_BLK, JOURNAL_NR/ SIT_JOURNAL_BLK);
	LOG_DEBUG(L"\t NAT Journal nr=%d, NAT Journal blk=%d, NAT JOurnal per blk=%d", JOURNAL_NR, JOURNAL_NR / JOURNAL_NR, JOURNAL_NR);
//	LOG_DEBUG(L"\t ");
//	LOG_DEBUG(L"\t ");	

	LOG_DEBUG(L"== SIT ==");
	LOG_DEBUG(L"\t Start SIT=%d, SIT nr=%d, SIT blk size=%d", SIT_START_BLK, SIT_BLK_NR, sizeof(SEG_INFO) * SIT_ENTRY_PER_BLK);
	LOG_DEBUG(L"\t SIT entry per Blk=%d, SIT blk nr=%d, Total entry nr =%d,", SIT_ENTRY_PER_BLK, SIT_BLK_NR, SIT_ENTRY_PER_BLK * SIT_BLK_NR);
	LOG_DEBUG(L"\t check: MAIN SEG (%d) <= SIT entry nr (%d),", MAIN_SEG_NR, SIT_ENTRY_PER_BLK * SIT_BLK_NR);
	if (MAIN_SEG_NR > SIT_ENTRY_PER_BLK * SIT_BLK_NR) THROW_ERROR(ERR_APP, L"\t check: MAIN SEG (%d) <= SIT entry nr (%d),", MAIN_SEG_NR, SIT_ENTRY_PER_BLK * SIT_BLK_NR);
	LOG_DEBUG(L"== NAT ==");
	LOG_DEBUG(L"\t NODE nr=%d, NAT entry per Blk=%d, NAT blk size=%d", NODE_NR, NAT_ENTRY_PER_BLK, NAT_ENTRY_PER_BLK * sizeof(PHY_BLK));
	LOG_DEBUG(L"\t Start NAT=%d, NAT nr=%d", NAT_START_BLK, NAT_BLK_NR);
	LOG_DEBUG(L"\t check: NODE nr(%d) <= NAT entry nr(%d)", NODE_NR, NAT_BLK_NR * NAT_ENTRY_PER_BLK);
	if (NODE_NR > NAT_BLK_NR * NAT_ENTRY_PER_BLK)  THROW_ERROR(ERR_APP, L"check: NODE nr(%d) <= NAT entry nr(%d)", NODE_NR, NAT_BLK_NR * NAT_ENTRY_PER_BLK);
	LOG_DEBUG(L"== SSA ==");
	LOG_DEBUG(L"\t SSA start blk=%d, SSA blk nr=%d", SSA_START_BLK, SSA_BLK_NUM);

	LOG_DEBUG(L"\t Max file blks = %d", INDEX_SIZE * INDEX_TABLE_SIZE);

//	LOG_DEBUG(L"\t ");
//	LOG_DEBUG(L"\t ");
//	LOG_DEBUG(L"\t ");
//	LOG_DEBUG(L"\t ");
//	LOG_DEBUG(L"\t ");
//	LOG_DEBUG(L"\t ");
//	LOG_DEBUG(L"\t ");
//	LOG_DEBUG(L"\t ");
	LOG_DEBUG(L"\t check: META BLK nr(%d) <= BLK before MAIN_SEG (%d) ", END_META_BLK, MAIN_SEG_OFFSET * BLOCK_PER_SEG);
	if (END_META_BLK > MAIN_SEG_OFFSET * BLOCK_PER_SEG) THROW_ERROR(ERR_APP, L"check: META BLK nr(%s) <= BLK before MAIN_SEG (%d) ", END_META_BLK, MAIN_SEG_OFFSET * BLOCK_PER_SEG);
	return true;
}	



bool CF2fsSimulator::Initialzie(const boost::property_tree::wptree& config, const std::wstring & log_path)
{
	SanityCheckConfig();
	memset(&m_health_info, 0, sizeof(FsHealthInfo));
	float op = config.get<float>(L"over_provision");
	m_multihead_cnt = config.get<int>(L"multi_header_num");

	// 这里配置上有个问题，原先方法时配置文件指定logical block的数量，然后根据这个数量，计算所需要的physical block数量。
	// 但是这里出现了计算错误，physical block数量应该要大于logical block数量。
	// 将block的计算方法修改为正常，但是这个修改可能会引起WAF测试的计算错误。因此保留原来的算法
	size_t device_size = config.get<size_t>(L"volume_size");	// device_size 以字节为单位
	BLK_T phy_blk = (BLK_T)(ROUND_UP_DIV(device_size, BLOCK_SIZE));		
	SEG_T seg_nr = (SEG_T)ROUND_UP_DIV(phy_blk, BLOCK_PER_SEG);

	SEG_T gc_th_lo = config.get<SEG_T>(L"gc_seg_lo");
	SEG_T gc_th_hi = config.get<SEG_T>(L"gc_seg_hi");

	m_op_segs = config.get<int>(L"op_segments", OP_SEGMENT);
	LOG_DEBUG(L"overprovision segments = %d\n", m_op_segs);
	// 初始化
	m_storage.Initialize();

	m_segments.InitSegmentManager(seg_nr, gc_th_lo, gc_th_hi);
	m_pages.Init(m_health_info.m_blk_nr);
	m_nat.Init(NID_IN_USE);
	InitOpenList();

	// 文件系统大小由编码固定，
	m_health_info.m_seg_nr = MAIN_SEG_NR;
	m_health_info.m_blk_nr = m_health_info.m_seg_nr * BLOCK_PER_SEG;
	m_health_info.m_logical_blk_nr = m_health_info.m_blk_nr - ((gc_th_lo+10) * BLOCK_PER_SEG);
	m_health_info.m_free_blk = m_health_info.m_logical_blk_nr;

	// 初始化root
	CPageInfo* root_page = m_pages.allocate(true);
	BLOCK_DATA* root_node = m_pages.get_data(root_page);
	if (!InitInode(root_node, root_page, F2FS_FILE_DIR))	{
		THROW_FS_ERROR(ERR_GENERAL, L"node full during make fs");
	}
	root_node->node.m_ino = ROOT_FID;
	root_node->node.m_nid = ROOT_FID;
	root_page->nid = ROOT_FID;
	// root 作为永远打开的文件
	root_node->node.inode.ref_count = 1;			// root inode 始终cache
	m_nat.node_cache[ROOT_FID] = m_pages.page_id(root_page);
	PHY_BLK root_phy = UpdateInode(root_page, "CREATE");	// 写入磁盘
	if (is_invalid(root_phy)) {
		THROW_FS_ERROR(ERR_GENERAL, L"no enough segment during make fs");
	}
	m_nat.set_phy_blk(ROOT_FID, root_phy);

	// 初始化磁盘上的ckeckpoint,确保两个都无效。在接下来的write checkpoint()中，将有效的checkpoint写入。
	CPageInfo page;
	BLOCK_DATA* data = m_pages.get_data(&page);
	memset(data, 0, sizeof(BLOCK_DATA));
	data->m_type = BLOCK_DATA::BLOCK_CKPT_HEADER;
	m_storage.BlockWrite(CKPT_START_BLK, &page);
	m_storage.BlockWrite(CKPT_START_BLK + CKPT_BLK_NR, &page);
	
	memset(&m_checkpoint, 0, sizeof(m_checkpoint));
	m_checkpoint.header.ver_open = 1;
	m_checkpoint.header.ver_close = 1;
	m_cur_ckpt = 1;

	// 初始化结束
	m_segments.SyncSSA();
	m_segments.SyncSIT();
	m_segments.reset_dirty_map();
	m_nat.Sync();
	f2fs_write_checkpoint();
	m_storage.Sync();

//	memset(&m_fs_info, 0, sizeof(m_fs_info));

	// set log path
	m_log_path = log_path;
	// invalid block trace log：记录按时间顺序写入和无效化的phy block
	return true;
}

#ifdef ENABLE_FS_TRACE
void CF2fsSimulator::fs_trace(const char* op, _NID fid, DWORD start_blk, DWORD blk_nr)
{
	// write count, operation, start blk, blk nr, used blks, free blks
	fprintf_s(m_log_write_trace, "%lld,%s,%d,%d,%d,%d,%d,%d,%d\n", m_write_count++, op, fid, start_blk, blk_nr, m_health_info.m_logical_saturation, m_node_blks, m_free_blks, m_health_info.m_free_seg);
	fflush(m_log_write_trace);
}
#endif

void CF2fsSimulator::InitOpenList(void)
{
	// build open file free list
	for (UINT ii = 0; ii < MAX_OPEN_FILE; ++ii)
	{
		m_open_files[ii].ino = INVALID_BLK;
		m_open_files[ii].ipage = ii + 1;		// free link ptr
	}
	m_open_files[MAX_OPEN_FILE - 1].ipage = INVALID_BLK;
	m_free_ptr = 0;
	m_open_nr = 0;
}


NODE_INFO& CF2fsSimulator::ReadNode(_NID nid, CPageInfo * & page)
{	// 读取node，返回在page中。读取的page会保存在cache中，不需要调用者释放。
	// 如果page已经被缓存，则从缓存中读取。否则从磁盘读取。
	if (nid >= NODE_NR) THROW_FS_ERROR(ERR_GENERAL, L"Invalid node id: %d", nid);
	JCASSERT(page == nullptr);

	// 检查node是否在cache中
	BLOCK_DATA * block =nullptr;
	if (m_nat.node_cache[nid] != INVALID_BLK)
	{
		page = m_pages.page(m_nat.node_cache[nid]);
		block = m_pages.get_data(page);
	}
	else
	{	// 从磁盘中读取
		PHY_BLK blk = m_nat.get_phy_blk(nid);
		if (is_invalid(blk)) {
			THROW_FS_ERROR(ERR_INVALID_NID, L"invalid nid=%d in reading node", nid);
		}
		page = m_pages.allocate(true);
		m_storage.BlockRead(CF2fsSegmentManager::phyblk_to_lba(blk), page);
		page->phy_blk = blk;
		page->nid = nid;
		page->offset = INVALID_BLK;
		// 缓存读取的block
		m_nat.node_cache[nid] = m_pages.page_id(page);
		block = m_pages.get_data(page);
		if (block->m_type != BLOCK_DATA::BLOCK_INDEX && block->m_type != BLOCK_DATA::BLOCK_INODE) {
			THROW_ERROR(ERR_APP, L"block phy-%d is not a node block, nid=%d", blk, nid);
		}		
		if (block->m_type == BLOCK_DATA::BLOCK_INODE) block->node.inode.ref_count = 0;
		
	}
	block->node.page_id = m_pages.page_id(page);//page->page_id;
	// 对于node，由于使用nat机制，page中不需要记录父node和offset。
	return block->node;
}

inline int DepthFromBlkNr(UINT blk_nr)
{
	int max_depth = 0;
	while (blk_nr != 0) max_depth++, blk_nr >>= 1;		// 通过block数量计算最大层次
	return max_depth;
}

_NID CF2fsSimulator::FindFile(NODE_INFO& parent, const char* fn)
{
	UINT blk_nr = get_file_blks(parent.inode);
	if (blk_nr == 0) return INVALID_BLK;
	// 计算文件名hash
	WORD hash = FileNameHash(fn);
	WORD fn_len = (WORD)(strlen(fn));
	_NID fid = INVALID_BLK;

	// 抽象的 dir file 结构：以2^level的数量，在每层中存放dentry block。每个block属于一个hash slot。
	// 逐层查找文件 （f2fs :: __f2fs_find_entry() )
	int max_depth = DepthFromBlkNr(blk_nr);
	int blk_num=1, blk_index=0;		// 当前层次下，block数量，第一个block，和最后一个block
	for (int level = 0; level < max_depth; level++)
	{	// find_in_level
		WORD hh = hash % blk_num;
		CPageInfo* page = nullptr;
		FileReadInternal(&page, parent, blk_index + hh, blk_index + hh + 1);
		if (page == nullptr)
		{	// page 空洞，找下一个level
			blk_index += blk_num;
			blk_num *= 2;
			continue;
		}
		BLOCK_DATA * block = m_pages.get_data(page);
		if (block != nullptr && block->m_type == BLOCK_DATA::BLOCK_DENTRY)
		{
			DENTRY_BLOCK& entries = block->dentry;

			// 沿着bitmap查找
//			int index = 0;
			for (int index = 0; index < DENTRY_PER_BLOCK; ++index)
			{
				if (is_invalid(entries.dentries[index].ino)|| entries.dentries[index].hash != hash) continue;
				char* ff = entries.filenames[index];
				if (entries.dentries[index].name_len == fn_len && memcmp(ff, fn, fn_len)==0)
				{
					fid = entries.dentries[index].ino;
					break;
				}
			}
			if (is_valid(fid))
			{
				m_pages.free(page);
				break;
			}
		}
		blk_index += blk_num;
		blk_num *= 2;
		m_pages.free(page);
	}
	// 回收block
	return fid;
}

bool CF2fsSimulator::CloseInode(CPageInfo* &ipage)
{
	bool dirty = false;

	BLOCK_DATA* data = m_pages.get_data(ipage);
	if (data->m_type != BLOCK_DATA::BLOCK_INODE) THROW_ERROR(ERR_USER, L"data in the page is not an inode");
	_NID ino = data->node.m_nid;
	if (ipage->nid != ino)
	{
		THROW_ERROR(ERR_USER, L"nid does not match in data (%d) and page (%d)", ino, ipage->nid);
	}
	INODE & inode = data->node.inode;

	if (inode.ref_count != 0)
	{
		ipage = nullptr;
		return false;
	}
	if (inode.nlink == 0)
	{
		FileRemove(ipage);
		dirty = true;
	}
	else
	{	// fsync()
		LBLK_T start_blk, last_blk;
		OffsetToBlock(start_blk, last_blk, 0, inode.file_size);
		LBLK_T bb = 0;

		for (size_t ii = 0; (ii < INDEX_TABLE_SIZE) && (bb < last_blk); ++ii, bb += INDEX_SIZE)
		{
			_NID nid = inode.index[ii];
			if (is_invalid(nid)) continue;		// index 空洞，跳过
			PAGE_INDEX page_id = m_nat.node_cache[nid];
			if (is_valid(page_id))
			{
				CPageInfo* page = m_pages.page(page_id);
				if (page->dirty)
				{	// <TODO> page的nid和offset应该在page申请时设置
					if (page->nid != nid) THROW_ERROR(ERR_USER, L"nid does not match in data (%d) and page (%d)", nid, page->nid);
					PHY_BLK phy = m_segments.WriteBlockToSeg(page, true);
					if (is_invalid(phy))		{
						m_pages.free(page);
						THROW_FS_ERROR(ERR_GENERAL, L"no enough segment during close file, fid=%d", ipage->nid);
					}
					m_nat.set_phy_blk(nid, phy);
					dirty = true;
				}
				// decache index page
				m_nat.node_cache[nid] = INVALID_BLK;
				m_pages.free(page);
			}
		}
		// release ipage
		if (ipage->dirty)
		{
			PHY_BLK phy = m_segments.WriteBlockToSeg(ipage, true);
			if (is_invalid(phy)) {
				m_pages.free(ipage);
				THROW_FS_ERROR(ERR_GENERAL, L"no enough segment during close file, fid=%d", ipage->nid);
			}
			m_nat.set_phy_blk(ino, phy);
			dirty = true;
		}
		// decache index page
		m_nat.node_cache[ino] = INVALID_BLK;
		m_pages.free(ipage);	
	}
	ipage = nullptr;
	return dirty;
}

void CF2fsSimulator::UpdateNat(_NID nid, PHY_BLK phy_blk)
{
	// 当一个node block被搬移的时候，更新L2P的链接。 <TODO> F2FS 检查
	// 首先检查这个nid是否在cache中
	//	如果在cache中，且cache dirty：放弃GC，将cache 写入storage
	//	如果在cache中，且cache undirty：discache
	m_nat.set_phy_blk(nid, phy_blk);
}

void CF2fsSimulator::UpdateIndex(_NID nid, UINT offset, PHY_BLK phy_blk)
{
	// 检查index是否被catch
	CPageInfo* page = nullptr;
	NODE_INFO* node = nullptr;
	node = &ReadNode(nid, page);
	// 更新index
	if (is_invalid(node->index.index[offset]))
	{
		THROW_ERROR(ERR_USER, L"data block in index is invalid, index nid=%d, offset=%d", nid, offset);
	}
	node->index.index[offset] = phy_blk;
	// 回写nid，这个函数旨在GC中被调用，不需要再次触发GC。
	page->dirty = true;
}

CPageInfo* CF2fsSimulator::AllocateDentryBlock()
{
	CPageInfo* page = m_pages.allocate(true);
	BLOCK_DATA * block = m_pages.get_data(page);
	InitDentry(block);
	return page;
}

ERROR_CODE CF2fsSimulator::add_link(NODE_INFO* parent, const char* fn, _NID fid)
{
	// 计算文件名hash
	WORD hash = FileNameHash(fn);
	WORD fn_len = (WORD)(strlen(fn));
	int slot_num = ROUND_UP_DIV(fn_len, FN_SLOT_LEN);

	UINT blk_nr = get_file_blks(parent->inode);	
	int max_depth = DepthFromBlkNr(blk_nr);		// 表示level number，层次数量
	if (blk_nr == 0) max_depth = 0;
	int blk_num=1, blk_index=0;		// 当前层次下，block数量，第一个block，和最后一个block
	int level = 0;
	int index_start = 0;

	int blk_offset = 0;
	CPageInfo* dentry_page = nullptr;

	// 读取dir文件，
	for (; level < max_depth; level++)
	{	// 按层次查找空位，
		WORD hh = hash % blk_num;
		blk_offset = blk_index + hh;
		CPageInfo* page = nullptr;
		FileReadInternal(&page, *parent, blk_offset, blk_offset+1);
		if (page != nullptr)
		{
			BLOCK_DATA& block = *m_pages.get_data(page);
			DENTRY_BLOCK& entries = block.dentry;

			int index = 0;
			DWORD mask = 1;
			index_start = 0;
			while (1)
			{
				// 定位第一个空闲的slot，bitmap的高位为0作为guide
				while (entries.bitmap & mask) { mask <<= 1; index_start++; }	
				if (index_start >= DENTRY_PER_BLOCK) break;	// 没有空位 goto next 
				index = index_start;
				// 定位下一个非空闲的slot
				while (((entries.bitmap & mask) == 0) && index < DENTRY_PER_BLOCK) { mask <<= 1; index++; }
				if ((index - index_start) >= slot_num)
				{	// 找到空位
					dentry_page = page;
					break;
				}
				index_start = index;
			}
			if (index_start < DENTRY_PER_BLOCK)		break;	//找到空位
			m_pages.free(page);
		}
		else
		{	// 这个slot是空的，创建slot 申请一个空的block
			blk_offset = blk_index + hh;
			dentry_page = AllocateDentryBlock();
			index_start = 0;
			// 只有在增加文件层次的时候才需要调整文件大小。
			break;
		}
		blk_index += blk_num, blk_num *= 2;
	}

	if (index_start >= DENTRY_PER_BLOCK || dentry_page == nullptr)
	{	// 没有找到空位， 添加新的层次
		max_depth++;
		if (max_depth > MAX_DENTRY_LEVEL) {
//			THROW_FS_ERROR(ERR_NO_SPACE, L"parent fid=%d, fn len=%d", parent->m_nid, fn_len)
			return ERR_NO_SPACE;
		}
		blk_num = (1 << (max_depth - 1));
		blk_index = blk_num - 1;
		WORD hh = hash % blk_num;

		// 申请一个空的block
		blk_offset = blk_index + hh;
		dentry_page = AllocateDentryBlock(/*dpage_id*/);
		index_start = 0;
		// 更新文件大小。文件大小必须填满一层，确保从文件大小计算层次时没错。文件支持空洞，增加文件大小不影响实际使用。
		parent->inode.file_size = ((1<<max_depth)-1) * BLOCK_SIZE;
		CPageInfo* parent_page = m_pages.page(parent->page_id); JCASSERT(parent_page);
		parent_page->dirty = true;
	}

	// 向dentry写入文件
	if (index_start >= DENTRY_PER_BLOCK || dentry_page == nullptr) {
		THROW_ERROR(ERR_APP, L"failed on alloacting dentry block");
	}
	BLOCK_DATA* dentry_block = m_pages.get_data(dentry_page);
	DENTRY_BLOCK& entries = dentry_block->dentry;
	entries.dentries[index_start].ino = fid;
	entries.dentries[index_start].hash = hash;
	entries.dentries[index_start].name_len = fn_len;

	char* entry_fn = entries.filenames[index_start];
	memcpy_s(entry_fn, (DENTRY_PER_BLOCK-index_start) * FN_SLOT_LEN, fn, FN_SLOT_LEN * slot_num);
	DWORD mask = (1 << index_start);
	for (int ii = 0; ii < slot_num; ++ii) {
		entries.bitmap |= mask;
		mask <<= 1;
	}

	UINT written = FileWriteInternal(*parent, blk_offset, blk_offset+1, &dentry_page);
	m_pages.free(dentry_page);
	if (written == 0) return ERR_NO_SPACE;
	else return ERR_OK;
}

void CF2fsSimulator::unlink(_NID fid, CPageInfo * parent_page)
{
	BLOCK_DATA* data = m_pages.get_data(parent_page);
	NODE_INFO& parent = data->node;

	UINT blk_nr = get_file_blks(parent.inode);
	bool found = false;

	for (UINT ii = 0; ii < blk_nr && !found; ++ii)
	{
		CPageInfo* page = nullptr;
		FileReadInternal(&page, parent, ii, ii+1);

		if (page != nullptr)
		{
			BLOCK_DATA& block = *m_pages.get_data(page);
			DENTRY_BLOCK& entries = block.dentry;
			for (int jj = 0; jj < DENTRY_PER_BLOCK; ++jj)
			{
				if (entries.dentries[jj].ino == fid)
				{	// 删除
					int slot_num = ROUND_UP_DIV(entries.dentries[jj].name_len, FN_SLOT_LEN);
					DWORD mask = (1 << jj);
					for (int ll = 0; ll < slot_num; ++ll, mask <<= 1)
					{
						entries.bitmap &= (~mask);
					}
					memset(entries.filenames[jj], 0, FN_SLOT_LEN * slot_num);
					memset(entries.dentries + jj, 0xFF, sizeof(DENTRY));
					found = true;
					page->dirty = true;
//					FileWriteInternal(parent, ii, ii + 1, &page);
					break;
				}
			}
			if (entries.bitmap == 0) FileTruncateInternal(parent_page, ii, ii + 1);
			else if (page->dirty) {
				UINT written = FileWriteInternal(parent, ii, ii + 1, &page, true);
				if (written == 0) THROW_FS_ERROR(ERR_DELETE_DIR, L"no resource when delete link");
			}
			m_pages.free(page);
		}
	}
	if (!found) THROW_ERROR(ERR_APP, L"cannot find fid:%d in parent", fid);
	ERROR_CODE ir = sync_fs();
	if (ir != ERR_OK) {
		LOG_ERROR(L"[err] failed on sync_fs() code=%d, when unlink fid=%d", ir, fid);
	}
}

UINT CF2fsSimulator::GetChildNumber(NODE_INFO* inode)
{
	UINT blk_nr = get_file_blks(inode->inode);
//	bool found = false;
	UINT child = 0;

	for (UINT ii = 0; ii < blk_nr; ++ii)
	{
		CPageInfo* page = nullptr;
		FileReadInternal(&page, *inode, ii, ii + 1);

		if (page != nullptr)
		{
			BLOCK_DATA& block = *m_pages.get_data(page);
			DENTRY_BLOCK& entries = block.dentry;
			for (int jj = 0; jj < DENTRY_PER_BLOCK; ++jj)
			{
				if (is_valid(entries.dentries[jj].ino)) child++;
			}
			m_pages.free(page);
		}
	}
//	if (!found) THROW_ERROR(ERR_APP, L"cannot find fid:%d in parent", fid);

	return child;
}


ERROR_CODE CF2fsSimulator::InternalCreatreFile(CPageInfo*& file_page, _NID & fid, const std::string& fn, bool is_dir)
{
	// 根据路径找到父节点
	fid = INVALID_BLK;
	char full_path[MAX_PATH_SIZE+1];
	strcpy_s(full_path, fn.c_str());

	char * ptr = strrchr(full_path, '\\');
	if (ptr == nullptr || *ptr == 0) {
		return ERR_WRONG_PATH;
	}
	*ptr = 0;
	CPageInfo* page = nullptr; // m_pages.allocate_index(true);
	_NID parent_fid = FileOpenInternal(full_path, page);	// 返回需要添加的父目录的nid，parent_page是祖父目录的page
	CloseInode(page);
	if (is_invalid(parent_fid))
	{
		LOG_ERROR(L"[err] parent path %S is not exist", full_path);
		return ERR_PARENT_NOT_EXIST;
	}

	CPageInfo* parent_page = nullptr;
	NODE_INFO & parent = ReadNode(parent_fid, parent_page);
	// 检查 fn 不在 parent目录中
	char* name = ptr + 1;
	if (is_valid(FindFile(parent, name)))
	{
		LOG_ERROR(L"[err] file %S is already exist", fn.c_str());
		return ERR_CREATE_EXIST;
	}

	// 创建inode，allocate nid
	file_page = m_pages.allocate(true);
	BLOCK_DATA* inode_block = m_pages.get_data(file_page);
	F2FS_FILE_TYPE file_type = is_dir ? F2FS_FILE_DIR : F2FS_FILE_REG;
	if (!InitInode(inode_block, file_page, file_type)) {
		CloseInode(parent_page);
		m_pages.free(file_page);
		file_page = nullptr;
		return ERR_NO_SPACE;
	}
	fid = inode_block->node.m_nid;

		
	// 添加到父节点dentry中
	ERROR_CODE ir = add_link(&parent, name, fid);
	if (ir != ERR_OK) {
		LOG_ERROR(L"[err] failed on add child to parent");
		CloseInode(parent_page);
		m_nat.put_node(fid);
		m_pages.free(file_page);
		file_page = nullptr;
		fid = INVALID_BLK;
		return ir;
	}
	// cache page;
	inode_block->node.inode.nlink++;
	// 更新inode
	PHY_BLK phy_blk = UpdateInode(file_page, "CREATE");
	if (is_invalid(phy_blk)) {
		LOG_ERROR(L"[err] no enough segment during creating %S, fid=%d", fn.c_str(), fid);
		CloseInode(parent_page);
		m_pages.free(file_page);
		m_nat.put_node(fid);
		file_page = nullptr;
		fid = INVALID_BLK;
		return ERR_NO_SPACE;
	}
	m_nat.set_phy_blk(fid, phy_blk);	
	m_nat.node_cache[fid] = m_pages.page_id(file_page);
	InterlockedIncrement(&(inode_block->node.inode.ref_count));
	CloseInode(parent_page);

	ir = sync_fs();
	if (ir != ERR_OK) {
		LOG_ERROR(L"[err] failed on sync_fs() code=%d, when creating %S", ir, fn.c_str());
	}

	return ir;
}

ERROR_CODE CF2fsSimulator::FileCreate(_NID & fid, const std::string& fn)
{
	if (m_open_nr >= MAX_OPEN_FILE) {
		fid = INVALID_BLK;
		return ERR_MAX_OPEN_FILE;
	}

	CPageInfo* page = nullptr; // m_pages.allocate_index(true);
	ERROR_CODE ir = InternalCreatreFile(page, fid, fn, false);
	if (ir != ERR_OK) {
		fid = INVALID_BLK;
		return ir;
	}
		
	if (is_invalid(fid)) 	{
		return ERR_CREATE;
	}
	// 放入open list中
//	m_health_info.m_file_num++;

//	NODE_INFO *inode = &ReadNode(fid, page);		// now, the ipage points to the opened file.
	// 放入open list
	AddFileToOpenList(fid, page);
//	inode->inode.ref_count++;
	return ERR_OK;
}

ERROR_CODE CF2fsSimulator::DirCreate(_NID & fid, const std::string& fn)
{
	CPageInfo* page = nullptr; // m_pages.allocate_index(true);
	ERROR_CODE ir = InternalCreatreFile(page, fid, fn, true);
	if (ir != ERR_OK) {
		fid = INVALID_BLK;
		return ir;
	}
	if (is_invalid(fid)) {
		return ERR_CREATE;
	}
	// close dir file : 对于多线程，需要检查ref_count;
	(m_pages.get_data(page))->node.inode.ref_count--;
	CloseInode(page);
//	m_health_info.m_dir_num++;
	return ir;
}

OPENED_FILE* CF2fsSimulator::FindOpenFile(_NID fid)
{
	for (int ii = 0; ii < MAX_OPEN_FILE; ++ii)
	{
		if (m_open_files[ii].ino == fid) return (m_open_files + ii);
	}
	return nullptr;
}

OPENED_FILE* CF2fsSimulator::AddFileToOpenList(_NID fid, CPageInfo* page)
{
	// 放入open list
	UINT index = m_free_ptr;
	m_free_ptr = m_open_files[index].ipage;		// next
	m_open_files[index].ino = fid;
	m_open_files[index].ipage = m_pages.page_id(page);	// page->page_id;
	m_open_nr++;
	return m_open_files + index;
}

ERROR_CODE CF2fsSimulator::FileOpen(_NID &fid, const std::string& fn, bool delete_on_close)
{
	fid = INVALID_BLK;
	char full_path[MAX_PATH_SIZE + 1];
	strcpy_s(full_path, fn.c_str());

	CPageInfo* page = nullptr; // m_pages.allocate_index(true);
	fid = FileOpenInternal(full_path, page);
	CloseInode(page);
	if (is_invalid(fid))	{	return ERR_OPEN_FILE; }
	page = nullptr;

	// 查找文件是否已经打开
	NODE_INFO* inode = nullptr;
	OPENED_FILE* file = FindOpenFile(fid);
	if (file)
	{	// 文件已经打开
		page = m_pages.page(file->ipage);
		inode = &m_pages.get_data(page)->node;
	}
	else
	{
		if (m_open_nr >= MAX_OPEN_FILE) {
//			THROW_FS_ERROR(ERR_MAX_OPEN_FILE, L"oped file reached max (%d)", m_open_nr);
			fid = INVALID_BLK;
			return ERR_MAX_OPEN_FILE;
		}
		inode = &ReadNode(fid, page);		// now, the ipage points to the opened file.
		// 放入open list
		AddFileToOpenList(fid, page);
	}
	inode->inode.ref_count++;
	return ERR_OK;
}

void CF2fsSimulator::ReadNodeNoCache(NODE_INFO& node, _NID nid)
{
	if (is_valid(m_nat.node_cache[nid]))
	{
		CPageInfo * page = m_pages.page(m_nat.node_cache[nid]);
		BLOCK_DATA * blk_data = m_pages.get_data(page);
		memcpy_s(&node, sizeof(NODE_INFO), &blk_data->node, sizeof(NODE_INFO));
	}
	else
	{
		PHY_BLK blk = m_nat.get_phy_blk(nid);
		if (blk >= MAIN_BLK_NR) {
			THROW_FS_ERROR(ERR_LOST_NID, L"nid [%d], not allocated or invalid phy block: %d", nid, blk);
		}
		CPageInfo page;
		m_storage.BlockRead(CF2fsSegmentManager::phyblk_to_lba(blk), &page);
		BLOCK_DATA* blk_data = m_pages.get_data(&page);
		if (blk_data->m_type != BLOCK_DATA::BLOCK_INODE && blk_data->m_type != BLOCK_DATA::BLOCK_INDEX) {
			THROW_FS_ERROR(ERR_WRONG_BLOCK_TYPE, L"nid [%d] block type (%d) is not INODE", nid, blk_data->m_type);
		}
		if (blk_data->node.m_nid != nid) {
			THROW_FS_ERROR(ERR_INVALID_NID, L"nid [%d] does not match, nid in node= %d", nid, blk_data->node.m_nid);
		}
		memcpy_s(&node, sizeof(NODE_INFO), &blk_data->node, sizeof(NODE_INFO));
	}
}

void CF2fsSimulator::ReadBlockNoCache(BLOCK_DATA& data, PHY_BLK blk)
{
	CPageInfo page;
	m_storage.BlockRead(CF2fsSegmentManager::phyblk_to_lba(blk), &page);
	BLOCK_DATA* src = m_pages.get_data(&page);
	memcpy_s(&data, sizeof(BLOCK_DATA), src, sizeof(BLOCK_DATA));
}

void CF2fsSimulator::GetFileDirNum(_NID fid, UINT& file_nr, UINT& dir_nr)
{
	file_nr = 0; dir_nr = 0;
	// 打开inode fid
	NODE_INFO inode;
	ReadNodeNoCache(inode, fid);

	// 如果是file，则返回
	if (inode.inode.file_type != F2FS_FILE_DIR) {
		file_nr = 1;
		dir_nr = 0;
		return;
	}
	// 如果是dir，检查所有dentry
	dir_nr++;
	for (int ii = 0; ii < INDEX_TABLE_SIZE; ++ii)
	{
		_NID index_nid = inode.inode.index[ii];
		if (is_invalid(index_nid)) continue;

		NODE_INFO index;
		ReadNodeNoCache(index, index_nid);
		for (int jj = 0; jj < INDEX_SIZE; ++jj)
		{
			PHY_BLK data_blk = index.index.index[jj];
			if (is_invalid(data_blk)) continue;
			BLOCK_DATA dentry_blk;
			ReadBlockNoCache(dentry_blk, data_blk);
			if (dentry_blk.m_type != BLOCK_DATA::BLOCK_DENTRY) {
				THROW_FS_ERROR(ERR_WRONG_BLOCK_TYPE, L"data [%d, %d], phy=%d block type (%d) unmatch, expected: BLOCK_DENTRY", index_nid, jj, data_blk, dentry_blk.m_type);
			}

			DENTRY_BLOCK& entries = dentry_blk.dentry;
			for (int ii = 0; ii < DENTRY_PER_BLOCK; ++ii) {
				if ( is_invalid(entries.dentries[ii].ino) ) continue;
				// 对于获得的ino，进行递归调用
				UINT sub_file_nr = 0, sub_dir_nr = 0;
				GetFileDirNum(entries.dentries[ii].ino, sub_file_nr, sub_dir_nr);
				file_nr += sub_file_nr;
				dir_nr += sub_dir_nr;
			}
		}
	}
	// 如果dentry项目是目录，递归检查，
}

bool CF2fsSimulator::Mount(void)
{
	load_checkpoint();
	m_segments.Load(m_checkpoint);
	m_nat.Load(m_checkpoint);
	// load root
	CPageInfo* page = nullptr;	//m_pages.allocate_index(true);
	NODE_INFO & root_node = ReadNode(ROOT_FID, page);
	root_node.inode.ref_count = 1;
	m_nat.node_cache[root_node.m_nid] = m_pages.page_id(page);	// page->page_id;

	return true;
}

bool CF2fsSimulator::Unmount(void)
{
	// close all files
	for (int ii = 0; ii < MAX_OPEN_FILE; ++ii)
	{
		if (is_valid(m_open_files[ii].ino)) {
			ForceClose(m_open_files + ii);
		}
	}
	ERROR_CODE ir = sync_fs();
	if (ir != ERR_OK) {
		LOG_ERROR(L"[err] failed on sync_fs() code=%d, when unmount", ir);
	}
	m_storage.Sync();
	return true;
}

bool CF2fsSimulator::Reset(UINT rollback)
{
	m_storage.Rollback(rollback);
	m_storage.Sync();

	// reset open list
	InitOpenList();
	m_segments.Reset();
	m_nat.Reset();
	m_pages.Reset();
	m_storage.Reset();
	// reset mopen_files
	
	return true;
}

ERROR_CODE CF2fsSimulator::f2fs_sync_node_page(void)
{
//	UINT org_dirty = m_nat.get_dirty_node_nr();		// 最初的dirty page数量
//	LOG_DEBUG_(1, L"sync node page: dirty node nr=%d", org_dirty);
	// 第一轮： sync direct node
	// 第二轮： sync inode
	for (int phase = 0; phase < 2; ++phase)
	{
		for (int ii = 0; ii < NODE_NR; ++ii)
		{
			PAGE_INDEX page_id = m_nat.node_cache[ii];
			if (is_invalid(page_id)) continue;

			CPageInfo* page = m_pages.page(page_id);
			if (!page->dirty) continue;
			BLOCK_DATA* blk = m_pages.get_data(page);
			if ( (phase == 0) && (blk->m_type != BLOCK_DATA::BLOCK_INDEX)) continue;

			LOG_DEBUG_(1, L"flush node=%d, phase=%d, type=%d", ii, phase, blk->m_type);
			//			{	// <DONE> page的nid和offset应该在page申请时设置
			PHY_BLK phy = m_segments.WriteBlockToSeg(page, true);
			if (is_invalid(phy)) {
				LOG_ERROR(L"[err] segment is not enough when sync_node()");
				return ERR_NO_SPACE;
			}
			m_nat.set_phy_blk(ii, phy);
			page->dirty = false;
			//		dirty++;
		}
	}
	return ERR_OK;
}

ERROR_CODE CF2fsSimulator::sync_fs(void)
{
	LOG_STACK_TRACE();
	int retry = 10;
	UINT dirty_nr;
	while (retry > 0) {
		dirty_nr = m_nat.get_dirty_node_nr();		// 最初的dirty page数量
		LOG_DEBUG_(1,L"dirty node nr=%d", dirty_nr);
		if (dirty_nr == 0) break;
		// 保存index, 可能是因为gc打开的index page
		ERROR_CODE ir = f2fs_sync_node_page();
		if (ir != ERR_OK) THROW_FS_ERROR(ERR_SYNC, L"[err] segment is not enough when sync_node()");
		retry--;
	}
	if (retry <= 0) {
		THROW_FS_ERROR(ERR_SYNC, L"loop flushing, dirty page nr=%d", dirty_nr);
	}
	// 考虑到写入page时，触发GC。GC会使得已经flush的node再次变脏。
	//	对策：将脏node放入链表中，循环flush链表，知道链表清空。

	m_segments.SyncSSA();
	f2fs_write_checkpoint();
	return ERR_OK;
}

void CF2fsSimulator::f2fs_write_checkpoint()
{
	LOG_STACK_TRACE();
	// flush_nat : 把nat的变化写入journal
	m_nat.f2fs_flush_nat_entries(m_checkpoint);
	// flush_sit
	m_segments.f2fs_flush_sit_entries(m_checkpoint);
	// do_checkpoint
	// 1. 复制每个curseg
	size_t curseg_size = sizeof(CURSEG_INFO) * BT_TEMP_NR;

	memcpy_s(m_checkpoint.header.cur_segs, curseg_size, m_segments.m_cur_segs, curseg_size);
	// 2. curseg的summary (summary block不复制，依靠fsck重建）
	// 3. write to storage
	// 反转checkpoing id
	m_cur_ckpt ^= 1;
	save_checkpoint(m_cur_ckpt);
}

int CF2fsSimulator::load_checkpoint()
{
	// check checkpoint version
	LBLK_T base_blk = CKPT_START_BLK;
	int cur_ckpt = -1;
//	CKPT_HEAD ckpt_header[2]ckpt_header_1;
	UINT ckpt_ver[2];
	CPageInfo page[2];
	for (int ii = 0; ii < 2; ++ii)
	{
		ckpt_ver[ii] = 0;
		m_storage.BlockRead(base_blk + ii*CKPT_BLK_NR, page+ii);
		BLOCK_DATA* data = m_pages.get_data(page+ii);
		if (data->m_type != BLOCK_DATA::BLOCK_CKPT_HEADER) {
			LOG_ERROR(L"Checkpoint %d, type mismatch, type=%d, request=%d", ii, data->m_type, BLOCK_DATA::BLOCK_CKPT_HEADER);
			continue;
		}
		if (data->ckpt_header.ver_open != data->ckpt_header.ver_close) {
			LOG_ERROR(L"Checkpoint %d, version mismatch, open=%d, close=%d", ii, data->ckpt_header.ver_open, data->ckpt_header.ver_close);
			continue;
		}
		ckpt_ver[ii] = data->ckpt_header.ver_close;
	}
	if (ckpt_ver[0] && ckpt_ver[1])
	{
		if (ckpt_ver[0] == ckpt_ver[1]) {
			THROW_FS_ERROR(ERR_CKPT_FAIL, L"either checkpoint is validate and have the same version, ver=%d", ckpt_ver[0]);
		}
		else if (ckpt_ver[1] > ckpt_ver[0]) { cur_ckpt = 1; }
		else { cur_ckpt = 0; }
	}
	else if (ckpt_ver[0])	{ cur_ckpt = 0; }
	else if (ckpt_ver[1]) { cur_ckpt = 1; }
	else {
		THROW_FS_ERROR(ERR_CKPT_FAIL, L"neigher checkpoing is validate");
		return -1;
	}
	LOG_DEBUG(L"get valid checkpoint, id=%d, version=%d", cur_ckpt, ckpt_ver[cur_ckpt]);
	// read checkpoint 0
	base_blk = CKPT_START_BLK + cur_ckpt * CKPT_BLK_NR;
	BLOCK_DATA* data = m_pages.get_data(page+cur_ckpt);

	static const size_t SIT_JOURNAL_PER_BLK = JOURNAL_NR / SIT_JOURNAL_BLK;
	static const size_t SIT_JOURNAL_SIZE = sizeof(SIT_JOURNAL_ENTRY) * SIT_JOURNAL_PER_BLK;
//	CPageInfo* page = m_pages.allocate(true);
	// cur segment
//	m_storage.BlockRead(CKPT_START_BLK, &page);	
	memcpy_s(&m_checkpoint.header, sizeof(CKPT_HEAD), &(data->ckpt_header), sizeof(CKPT_HEAD));
	// nit journal
	// 此处仅利用page[]中的任意一个作为读缓存。方便起见，用page[cur_ckpt]，不用重复获取data。
	m_storage.BlockRead(base_blk + 1, &page[cur_ckpt]);
	memcpy_s(m_checkpoint.nat_journals, sizeof(CKPT_NAT_JOURNAL), &(data->ckpt_nat_nournal), sizeof(CKPT_NAT_JOURNAL));

	for (int ii = 0; ii < SIT_JOURNAL_BLK; ++ii)
	{		// sit journal 1
		m_storage.BlockRead(base_blk + 2+ii, &page[cur_ckpt]);
		memcpy_s(m_checkpoint.sit_journals + ii * SIT_JOURNAL_PER_BLK, SIT_JOURNAL_SIZE,
			&(data->ckpt_sit_nournal), SIT_JOURNAL_SIZE);
	}
	return cur_ckpt;
}

void CF2fsSimulator::save_checkpoint(int ckpt_id)
{
	JCASSERT(ckpt_id ==0 || ckpt_id==1);
	LBLK_T base_blk = CKPT_START_BLK + ckpt_id * CKPT_BLK_NR;

	static const size_t SIT_JOURNAL_PER_BLK = JOURNAL_NR / SIT_JOURNAL_BLK;
	static const size_t SIT_JOURNAL_SIZE = sizeof(SIT_JOURNAL_ENTRY) * SIT_JOURNAL_PER_BLK;
	LOG_DEBUG(L"SIT_JOURNAL_PER_BLK=%d, SIT_JOURNAL_SIZE=%d", SIT_JOURNAL_PER_BLK, SIT_JOURNAL_SIZE)
	CPageInfo page;
	BLOCK_DATA* data = m_pages.get_data(&page);
	m_checkpoint.header.ver_open ++;
	memcpy_s(&(data->ckpt_header), sizeof(CKPT_HEAD), &m_checkpoint.header, sizeof(CKPT_HEAD));
	data->m_type = BLOCK_DATA::BLOCK_CKPT_HEADER;
	m_storage.BlockWrite(base_blk, &page);

	memcpy_s(&(data->ckpt_nat_nournal), sizeof(CKPT_NAT_JOURNAL), m_checkpoint.nat_journals, sizeof(CKPT_NAT_JOURNAL));
	data->m_type = BLOCK_DATA::BLOCK_CKPT_NAT_JOURNAL;
	m_storage.BlockWrite(base_blk+1, &page);

	for (int ii = 0; ii < SIT_JOURNAL_BLK; ++ii)
	{
		memcpy_s(&(data->ckpt_sit_nournal), SIT_JOURNAL_SIZE,
			m_checkpoint.sit_journals + ii * SIT_JOURNAL_PER_BLK, SIT_JOURNAL_SIZE);
		data->m_type = BLOCK_DATA::BLOCK_CKPT_SIT_JOURNAL;
		m_storage.BlockWrite(base_blk + 2 + ii, &page);
	}

	m_checkpoint.header.ver_close++;
	memcpy_s(&(data->ckpt_header), sizeof(CKPT_HEAD), &m_checkpoint.header, sizeof(CKPT_HEAD));
	data->m_type = BLOCK_DATA::BLOCK_CKPT_HEADER;
	m_storage.BlockWrite(base_blk, &page);
}


_NID CF2fsSimulator::FileOpenInternal(char* fn, CPageInfo* &parent_inode)
{
	JCASSERT(parent_inode == nullptr);
	NODE_INFO& root = ReadNode(ROOT_FID, parent_inode);		// 打开root文件
	NODE_INFO* parent_node = &root;
	_NID fid = ROOT_FID;

	char* next = nullptr;
	char* dir = strtok_s(fn, "\\", &next);			// dir应该首先指向第一个“\”
	if (dir == nullptr || *dir == 0) return ROOT_FID;

	while (1)
	{	
		// cur: 父节点，dir：目标节点
		fid = FindFile(*parent_node, dir);
		if (is_invalid(fid))
		{
			LOG_ERROR(L"[err] cannot find %S in %S", dir, fn);
			return INVALID_BLK;
		}
		dir = strtok_s(nullptr, "\\", &next);
		if (dir == nullptr || *dir == 0) break;
		// 关闭已经打开的node
		CloseInode(parent_inode);							// 关闭上次打开的文件
		parent_node = & ReadNode(fid, parent_inode);		// 再次打开文件
	}
	return fid;
}

void CF2fsSimulator::ForceClose(OPENED_FILE* file)
{
	CPageInfo* ipage = m_pages.page(file->ipage);
	BLOCK_DATA* data = m_pages.get_data(ipage);
	INODE& inode = data->node.inode;
	inode.ref_count = 0;
	CloseInode(ipage);
}

void CF2fsSimulator::FileClose(_NID fid)
{
	// find opne file
	OPENED_FILE* file = FindOpenFile(fid);
	if (file == nullptr) THROW_FS_ERROR(ERR_OPEN_FILE, L"file fid=%d is not opened", fid);
	CPageInfo* ipage = m_pages.page(file->ipage);
	INODE& inode = m_pages.get_data(ipage)->node.inode;

	if (inode.ref_count <= 0) THROW_FS_ERROR(ERR_OPEN_FILE, L"file's refrence == 0, fid=%d", fid);
	inode.ref_count--;
	if (inode.ref_count == 0)
	{	// 释放 open list
		file->ino = INVALID_BLK;
		file->ipage = m_free_ptr;
		m_free_ptr = (UINT)(file - m_open_files);
		m_open_nr--;
		bool dirty = CloseInode(ipage);
		if (dirty) {
			ERROR_CODE ir = sync_fs();
			if (ir != ERR_OK) {
				LOG_ERROR(L"[err] failed on sync_fs() code=%d, during close file (fid=%d)", ir, fid);
				//THROW_FS_ERROR(ir, L"no resource for sync_fs() during close file, fid=%d", fid);
			}
		}
	}
}

FSIZE CF2fsSimulator::FileWrite(_NID fid, FSIZE offset, FSIZE len)
{
	// find opne file
	OPENED_FILE* file = FindOpenFile(fid);
	if (file == nullptr) THROW_ERROR(ERR_USER, L"file fid=%d is not opened", fid);
	CPageInfo* ipage = m_pages.page(file->ipage);
	NODE_INFO & inode = m_pages.get_data(ipage)->node;

	// 计算起始block和结束block，end_block是最后要写入的下一个。blk_nr=end_block - start_block
	LBLK_T start_blk, end_blk;
	OffsetToBlock(start_blk, end_blk, offset, len);

	if (end_blk > MaxFileSize()) {
		//THROW_ERROR(ERR_APP, L"file size is too large, blks=%d, max_size=%d", end_blk, MaxFileSize())
		LOG_ERROR(L"file size is too large, blks=%d, max_size=%d", end_blk, MaxFileSize());
		return 0;
	}
	DWORD blk_num = end_blk - start_blk;
	PPAGE pages[MAX_FILE_BLKS];
	for (UINT ii=0, bb = start_blk; bb < end_blk; ++ii, ++bb)
	{
		pages[ii] = m_pages.allocate(true);
		BLOCK_DATA* data = m_pages.get_data(pages[ii]);
		data->m_type = BLOCK_DATA::BLOCK_FILE_DATA;
		data->file.fid = fid;
		data->file.offset = bb;
	}
	UINT written = FileWriteInternal(inode, start_blk, end_blk, pages);
	for (UINT ii = 0; ii < blk_num; ++ii) m_pages.free(pages[ii]);
	if (written == 0) return 0;		// 没有足够的空间
	// 设置新的文件大小
	UINT end_pos = offset + len;
	if (start_blk + written < end_blk) end_pos = (start_blk + written) * BLOCK_SIZE;
	// 文件有增长，检查实际长度
	if (end_pos > inode.inode.file_size) {	inode.inode.file_size = end_pos;	}
	ipage->dirty = true;
	return (end_pos - offset);
}

UINT CF2fsSimulator::FileWriteInternal(NODE_INFO& inode, FSIZE start_blk, FSIZE end_blk, CPageInfo* pages[], bool force)
{
	// 估算空间
//	printf_s("write: over provision segment=%d\n", m_op_segs);
	FSIZE data_blk_nr = end_blk - start_blk;
	FSIZE node_blk_nr = ROUND_UP_DIV(data_blk_nr, INDEX_SIZE);
	PHY_BLK available_blk;
	if (force)	available_blk = m_segments.get_free_blk_nr() - (RESERVED_SEG) * BLOCK_PER_SEG;
	else		available_blk = m_segments.get_free_blk_nr() - (RESERVED_SEG + m_op_segs) * BLOCK_PER_SEG;
	if (data_blk_nr + node_blk_nr > available_blk) return 0;

	CIndexPath ipath(&inode);
	CPageInfo* ipage = m_pages.page(inode.page_id);
	NODE_INFO* di_node = nullptr;
	CPageInfo* di_page = nullptr;
	//fs_trace("WRITE", fid, start_blk, end_blk - start_blk);
	// 变量说明
	//								page offset,	page 指针,	node offset, node 指针,	block,
	// 文件的inode:					na,			na,			fid,		nid,	
	// 当前处理的direct offset node:	??,			di_page,	ipath中,		di_node,
	// 当前数据块					_pp,		dpage,		na,			na,	
	int page_index = 0;
	// 临时page, 当要写入的page为null是，表示用户数据，不care。
	for (; start_blk < end_blk; start_blk++, page_index++)
	{
		// 查找PHY_BLK定位到
		if (ipath.level < 0)
		{
			if (!OffsetToIndex(ipath, start_blk, true))			{
				LOG_ERROR(L"[err] no enough node during write data");
				return page_index;
			}
			// 获取index的位置，需要更新index
			di_node = ipath.node[ipath.level];
			di_page = ipath.page[ipath.level];
		}
		// data block在index中的位置
		int offset = ipath.offset[ipath.level];

		// 确定数据温度
		PHY_BLK phy_blk = di_node->index.index[offset];
		CPageInfo* dpage = pages[page_index];
		JCASSERT(dpage);

		// <TODO> page的nid和offset应该在page申请时设置。作为data page，没有缓存，在写入前设置page.
		dpage->phy_blk = phy_blk;
		dpage->nid = di_node->m_nid;
		dpage->offset = offset;
		PHY_BLK new_phy_blk = m_segments.WriteBlockToSeg(dpage,force);
		if (is_invalid(new_phy_blk)) 
		{
			LOG_ERROR(L"[err] no enough segment during write data");
			return page_index;
		}
		di_node->index.index[offset] = new_phy_blk;

		if (is_invalid(phy_blk))
		{	// 这个逻辑块没有被写过，增加逻辑饱和度，增加inode的block number。对于写过的block，在WriteBlockToSeg()中invalidate 旧block
			InterlockedIncrement16((SHORT*)(& m_health_info.m_logical_saturation));
			if (m_health_info.m_logical_saturation >= (m_health_info.m_logical_blk_nr))
			{
//				LOG_WARNING(L"[warning] logical saturation overflow, logical_saturation=%d, logical_block=%d", m_health_info.m_logical_saturation, m_health_info.m_logical_blk_nr);
			}
			inode.inode.blk_num++;
			di_node->index.valid_data++;
			ipage->dirty = true;
		}
		// 注意，此处并没有实现 NAT，存在wandering tree问题。
		di_page->dirty = true;
		InterlockedIncrement64(&m_health_info.m_total_host_write);
//		dpage->host_write++;
		// 将ipath移动到下一个offset 
//		m_segments.CheckGarbageCollection(this);
		NextOffset(ipath);
	}
	// node block 会延迟到调用fsync()时更新
	return page_index;
} 

void CF2fsSimulator::FileReadInternal(CPageInfo * pages[], NODE_INFO& inode, FSIZE start_blk, FSIZE end_blk)
{
	// sanity check
	_NID fid = inode.m_nid;
	CIndexPath ipath(&inode);
	NODE_INFO* di_node = nullptr;
	CPageInfo* di_page = nullptr;

	int page_index = 0;
	for (; start_blk < end_blk; start_blk++, page_index ++)
	{
		// 查找PHY_BLK定位到
		if (ipath.level < 0)
		{
			OffsetToIndex(ipath, start_blk, false);
			di_node = ipath.node[ipath.level];
			di_page = ipath.page[ipath.level];
		}
		if (di_node == nullptr || di_page == nullptr)
		{	// index 空洞，跳过
			continue;
		}
		// 此处暂不支持空洞
		if (!di_node) THROW_ERROR(ERR_APP, L"invalid index block, fid=%d, lblk=%d", inode.m_nid, start_blk);
		// 获取index的位置，需要更新index
		int index = ipath.offset[ipath.level];
		PHY_BLK blk = di_node->index.index[index];
//		LOG_DEBUG(L"read fid:%d, lblk:%d, phy blk:%d", inode.m_ino, start_blk, blk);
		if (pages[page_index] != nullptr) THROW_ERROR(ERR_APP, L"read internal() will allocate page");
		if (is_valid(blk))
		{	// 存在block，读取。否则这个page返回nullptr
			CPageInfo* page = m_pages.allocate(true);
			m_storage.BlockRead(CF2fsSegmentManager::phyblk_to_lba(blk), page);
			page->phy_blk = blk;
			page->nid = di_node->m_nid;
			page->offset = index;
			pages[page_index] = page;
		}
		// 将ipath移动到下一个offset 
		NextOffset(ipath);
	}
}

size_t CF2fsSimulator::FileRead(FILE_DATA blks[], _NID fid, FSIZE offset, FSIZE secs)
{
	// find opne file
	OPENED_FILE* file = FindOpenFile(fid);
	if (file == nullptr) THROW_ERROR(ERR_USER, L"file fid=%d is not opened", fid);
	CPageInfo* ipage = m_pages.page(file->ipage);
	NODE_INFO& inode = m_pages.get_data(ipage)->node;
	// sanity check
	FSIZE end_secs = offset + secs;
	if (end_secs > inode.inode.file_size)
	{
		LOG_WARNING(L"Oversize on truncating file, fid=%d, secs=%d, file_secs=%d", fid, end_secs, inode.inode.file_size);
		end_secs = inode.inode.file_size;
		secs = end_secs - offset;
	}

	// 计算起始block和结束block，end_block是最后要写入的下一个。blk_nr=end_block - start_block
	LBLK_T start_blk, end_blk;
	OffsetToBlock(start_blk, end_blk, offset, secs);
	DWORD page_nr = end_blk - start_blk;

	CPageInfo* data_pages[MAX_FILE_BLKS];
	memset(data_pages, 0, sizeof(CPageInfo*)*page_nr);
	FileReadInternal(data_pages, inode, start_blk, end_blk);

	for (DWORD ii = 0; ii < page_nr; ++ii)
	{
		if (data_pages[ii] != nullptr)
		{
			BLOCK_DATA* data = m_pages.get_data(data_pages[ii]);
			memcpy_s(blks + ii, sizeof(FILE_DATA), &data->file, sizeof(FILE_DATA));
			m_pages.free(data_pages[ii]);
		}
		else
		{
			memset(blks + ii, 0xFF, sizeof(FILE_DATA));
		}
	}
	return page_nr;
}

void CF2fsSimulator::FileTruncateInternal(CPageInfo* ipage, LBLK_T start_blk, LBLK_T end_blk)
{
	// find opne file
	NODE_INFO& inode = m_pages.get_data(ipage)->node;

	// 文件的所有block都无效，然后保存inode
	CIndexPath ipath(&inode);
	NODE_INFO* di_node = nullptr;
	CPageInfo* di_page = nullptr;

	for (; start_blk < end_blk; start_blk++)
	{
		// 查找PHY_BLK定位到
		if (ipath.level < 0)
		{
			OffsetToIndex(ipath, start_blk, false);
			di_node = ipath.node[ipath.level];
			di_page = ipath.page[ipath.level];
		}
		// index 空洞，跳过
		if (di_node == nullptr || di_page == nullptr) continue;
		// 获取index的位置，需要更新index
		int index = ipath.offset[ipath.level];
		if (di_node)
		{
			PHY_BLK phy_blk = di_node->index.index[index];
			if (is_valid(phy_blk) )
			{
				InterlockedDecrement16((SHORT*)(&m_health_info.m_logical_saturation));
				InvalidBlock("TRANCATE", phy_blk);
				di_node->index.index[index] = INVALID_BLK;
				di_page->dirty = true;
				di_node->index.valid_data--;
				inode.inode.blk_num--;
				ipage->dirty = true;
			}
			if (di_node->index.valid_data == 0) {
				_NID _nid = di_node->m_nid;
				m_nat.node_cache[_nid] = INVALID_BLK;
				m_pages.free(di_page);
				m_nat.put_node(_nid);
				inode.inode.index[ipath.offset[0]] = INVALID_BLK;
			}
		}
		// 将ipath移动到下一个offset 
		NextOffset(ipath);
	}
	// node block延迟到fsync()时更新
//	UpdateInode(ipage, "TRUNCATE");
}


void CF2fsSimulator::FileTruncate(_NID fid, FSIZE offset, FSIZE len)
{
	// find opne file
	OPENED_FILE* file = FindOpenFile(fid);
	if (file == nullptr) THROW_ERROR(ERR_USER, L"file fid=%d is not opened", fid);
	CPageInfo* ipage = m_pages.page(file->ipage);
	NODE_INFO& inode = m_pages.get_data(ipage)->node;

	// 文件的所有block都无效，然后保存inode
	FSIZE end_pos = offset + len;
	if (end_pos > inode.inode.file_size)
	{
		LOG_WARNING(L"Oversize on truncating file, fid=%d, secs=%d, file_secs=%d", fid, end_pos, inode.inode.file_size);
		end_pos = inode.inode.file_size;
		len = end_pos - offset;
	}

	LBLK_T start_blk, end_blk;
	OffsetToBlock(start_blk, end_blk, offset, len);
	fs_trace("TRUCATE", fid, start_blk, end_blk - start_blk);
	FileTruncateInternal(ipage, start_blk, end_blk);

	if (end_pos == inode.inode.file_size)
	{
		inode.inode.file_size = offset;
		ipage->dirty = true;
	}
}

void CF2fsSimulator::FileDelete(const std::string& fn)
{
	// Delete 分两步操作：（1）unlink，将文件的inode从dentry中删除，inode的link减1。（2）remove inode，将inode删除
	char _fn[MAX_PATH_SIZE + 1];
	strcpy_s(_fn, fn.c_str());

	// unlink
	CPageInfo* parent_page = nullptr;
	_NID fid = FileOpenInternal(_fn, parent_page);
	if (is_invalid(fid))
	{
		CloseInode(parent_page);
		THROW_FS_ERROR(ERR_DELETE_FILE, L"[err] file %S is not existing", fn.c_str());
	}
	// find opne file
	CPageInfo* ipage = nullptr;
	NODE_INFO* inode = nullptr;
	inode = &ReadNode(fid, ipage);

	unlink(fid, parent_page);
	inode->inode.nlink--;
	CloseInode(ipage);

	CloseInode(parent_page);

	// <TODO> 对于目录，删除其所有子目录
}

ERROR_CODE CF2fsSimulator::DirDelete(const std::string& fn)
{
	char _fn[MAX_PATH_SIZE + 1];
	strcpy_s(_fn, fn.c_str());
	CPageInfo* parent_page = nullptr;
	_NID fid = FileOpenInternal(_fn, parent_page);
	if (is_invalid(fid))
	{
		CloseInode(parent_page);
		THROW_FS_ERROR(ERR_DELETE_DIR, L"[err] dir %S is not existing", fn.c_str());
	}

	CPageInfo* ipage = nullptr;
	NODE_INFO* inode = nullptr;
	inode = &ReadNode(fid, ipage);

	UINT child_num = GetChildNumber(inode);
	if (child_num > 0)
	{
		CloseInode(ipage);
		CloseInode(parent_page);
		return ERR_NON_EMPTY;
	}

	unlink(fid, parent_page);
	inode->inode.nlink--;
	CloseInode(ipage);
	CloseInode(parent_page);
	return ERR_OK;
}

void CF2fsSimulator::FileRemove(CPageInfo * &inode_page)
{
	BLOCK_DATA* data = m_pages.get_data(inode_page);
	NODE_INFO* inode = &data->node;
	// 删除文件，回收inode
	if (inode->inode.ref_count > 0) {
		LOG_ERROR(L"[err] file reference code is not zero, fid=%d, count=%d", inode->m_nid, inode->inode.ref_count);
	}
	LBLK_T end_blk = ROUND_UP_DIV(inode->inode.file_size, BLOCK_SIZE);
	FileTruncateInternal(inode_page, 0, end_blk);
	// 删除map由FileDelete负责
	_NID _nid = inode->m_nid;
	m_nat.node_cache[_nid] = INVALID_BLK;
	m_pages.free(inode_page);
	m_nat.put_node(_nid);

//	InvalidBlock("DELETE_NODE", inode_page->phy_blk);
	inode_page = nullptr;

	m_node_blks--;
	// page 在free_inode中删除, 统计被回收的inode的WAF
//	m_health_info.m_file_num--;
}

void CF2fsSimulator::FileFlush(_NID fid)
{
	fs_trace("FLUSH", fid, 0, 0);
	return;
}

PHY_BLK CF2fsSimulator::UpdateInode(CPageInfo * ipage, const char* caller)
{
	// 变量说明
	//								page offset,	page 指针,	node offset, node 指针,	block,
	// 文件的inode:					na,			i_page,		fid,		nid,	
	// 当前处理的direct offset node:	di_page_id,	di_page,	di_node_id,	di_node,
	// 当前数据块					_pp,		dpage,		na,			na,	

	// 这个更新是否一定是host发起的？// 更新ipath
	BLOCK_DATA* idata = m_pages.get_data(ipage);
	NODE_INFO& inode = idata->node;
	LBLK_T start_blk, last_blk;
	OffsetToBlock(start_blk, last_blk, 0, inode.inode.file_size);
	LBLK_T bb = 0;
	for (size_t ii = 0; (ii < INDEX_TABLE_SIZE) && (bb < last_blk); ++ii, bb += INDEX_SIZE)
	{
		_NID nid = inode.inode.index[ii];
		if (is_invalid(nid)) continue;	// index 空洞

		PAGE_INDEX di_page_id = m_nat.node_cache[nid];
		if (is_invalid(di_page_id)) continue;		// offset node的数据不存在，或者没有被更新

		CPageInfo* di_page = m_pages.page(di_page_id);
		if (!di_page) THROW_ERROR(ERR_USER, L"failed on getting page, id=%d", di_page_id);

		BLOCK_DATA* data = m_pages.get_data(di_page);
		if (data == nullptr) THROW_ERROR(ERR_USER, L"index block in page is null, fid=%d, index=%d", inode.m_nid, ii);

		NODE_INFO& di_node = data->node;
		if (di_node.page_id != di_page_id) {
			THROW_ERROR(ERR_USER, L"data unmatch, fid=%d, index=%d, page=%p, page_in_blk=%p",
				inode.m_nid, ii, di_page, di_node.page_id);
		}

		if (di_node.index.valid_data == 0)
		{
			InvalidBlock("", di_page->phy_blk);
			m_node_blks--;
//			m_nat.put_node(di_node.m_nid);
			_NID _nid = di_node.m_nid;
			// decache index page
			m_nat.node_cache[nid] = INVALID_BLK;
			m_pages.free(di_page);
			m_nat.put_node(_nid);

			inode.inode.index[ii] = INVALID_BLK;
			ipage->dirty = true;
			continue;
		}
		if (di_page->dirty)
		{
//			di_page->host_write++;
			if (is_invalid(di_page->phy_blk)) m_node_blks++;
			// page的nid和offset应该在page申请时设置
			PHY_BLK phy_blk = m_segments.WriteBlockToSeg(di_page, false);
			if (is_invalid(phy_blk)) {
				LOG_ERROR(L"[err] no enough segment during update index node");
				return INVALID_BLK;
			}
			//LOG_DEBUG(L"write index, fid=%d, nid=%d, phy=%d,", inode.m_ino, di_node.m_nid, phy_blk);
			m_nat.set_phy_blk(nid, phy_blk);
//			m_segments.CheckGarbageCollection(this);
		}

#ifdef ENABLE_FS_TRACE
		//<TRACE>记录inode的更新情况。
		// opid, fid, inode或者index id, 原因，数据更新数量。
		fprintf_s(m_inode_trace, "%lld,%d,%lld,%s\n", m_write_count, nid.m_nid, ii, caller);
#endif
		ipage->dirty = true;
	}

	PHY_BLK old_phy = ipage->phy_blk;
	PHY_BLK new_phy = old_phy;
	if (ipage->dirty)
	{
//		ipage->host_write++;
		if (is_invalid(ipage->phy_blk)) m_node_blks++;
		// <DONE> page的nid和offset应该在page申请时设置
		new_phy = m_segments.WriteBlockToSeg(ipage, false);
		if (is_invalid(new_phy)) {
			LOG_ERROR(L"[err] no enough segment during update inode");
			return INVALID_BLK;
		}
//		LOG_DEBUG(L"write offset block, fid:%d, nid:%d, phy:%d", inode.m_ino, inode.m_nid, new_phy);
		m_nat.set_phy_blk(inode.m_nid, new_phy);
//		m_segments.CheckGarbageCollection(this);

#ifdef ENABLE_FS_TRACE
		//<TRACE>记录inode的更新情况。
		// opid, fid, inode或者index id, 原因，数据更新数量。
		fprintf_s(m_inode_trace, "%lld,%d,0,UPDATE\n", m_write_count, nid.m_nid);
#endif
	}
	return new_phy;
}

FSIZE CF2fsSimulator::GetFileSize(_NID fid)
{
	OPENED_FILE* file = FindOpenFile(fid);
	if (file == nullptr) {
		THROW_FS_ERROR(ERR_OPEN_FILE, L"file fid=%d is not opened", fid);
	}
	CPageInfo* ipage = m_pages.page(file->ipage);
	INODE& inode = m_pages.get_data(ipage)->node.inode;
	UINT file_size = inode.file_size;
	return (file_size);
}

void CF2fsSimulator::GetFileInfo(_NID fid, FSIZE& size, FSIZE& node_blk, FSIZE& data_blk)
{
	CPageInfo* page = nullptr;
	NODE_INFO& inode = ReadNode(fid, page);
	size = inode.inode.file_size;
	data_blk = inode.inode.blk_num;
	node_blk = 1;	// inode;
	for (int ii = 0; ii < INDEX_TABLE_SIZE; ++ii)
	{
		if (is_valid(inode.inode.index[ii])) node_blk++;
	}
}

void CF2fsSimulator::GetFsInfo(FS_INFO& space_info)
{
	space_info.total_seg = m_segments.get_seg_nr();
	space_info.free_seg = m_segments.get_free_nr();
	space_info.total_blks = m_health_info.m_blk_nr;
	space_info.used_blks = m_health_info.m_logical_saturation;
	space_info.free_blks = m_health_info.m_free_blk;
	// 可能出现负数
//	if (space_info.free_blks > m_health_info.m_logical_blk_nr) space_info.free_blks = 0;
	space_info.free_blks = m_segments.get_free_blk_nr();
	space_info.physical_blks = m_health_info.m_physical_saturation;
	space_info.total_page_nr = m_pages.total_page_nr();
	space_info.free_page_nr  = m_pages.free_page_nr();

	m_health_info.m_free_node_nr = m_nat.free_nr;
}

void CF2fsSimulator::GetHealthInfo(FsHealthInfo& info) const
{
	memcpy_s(&info, sizeof(FsHealthInfo), &m_health_info, sizeof(FsHealthInfo));
}

BLK_TEMP CF2fsSimulator::GetBlockTemp(CPageInfo* page)
{
	BLOCK_DATA* data = m_pages.get_data(page);
	if (data && (data->m_type == BLOCK_DATA::BLOCK_INODE || data->m_type == BLOCK_DATA::BLOCK_INDEX) )
	{
		//if (data->m_type == BLOCK_DATA::BLOCK_INODE) return BT_HOT__NODE;
		//else if (data->m_type == BLOCK_DATA::BLOCK_INDEX) return BT_COLD_NODE;
		//else JCASSERT(0);
		return BT_HOT__NODE;
	}
	else
	{
//		if (page->host_write >= 3) return BT_HOT__DATA;
//		else return BT_COLD_DATA;
		return BT_HOT__DATA;
	}
	return BT_HOT__DATA;
}

BLK_TEMP CF2fsSimulator::GetAlgorithmBlockTemp(CPageInfo* page, BLK_TEMP temp)
{
	//if (m_multihead_cnt == 1)	return BT_HOT__DATA;
	//else if (m_multihead_cnt == 2)
	//{
	//	if (temp == BT_COLD_NODE || temp == BT_WARM_NODE || temp == BT_HOT__NODE) return BT_HOT__NODE;
	//	else return BT_HOT__DATA;
	//}
	//else if (m_multihead_cnt == 4) return temp;
	//else JCASSERT(0);
	//return BT_HOT__DATA;
	return temp;
}



bool CF2fsSimulator::InvalidBlock(const char* reason, PHY_BLK phy_blk)
{
	if (is_invalid(phy_blk)) return false;
	bool free_seg = m_segments.InvalidBlock(phy_blk);
#ifdef ENABLE_FS_TRACE
	SEG_T _seg;
	BLK_T _blk;
	CF2fsSegmentManager::BlockToSeg(_seg, _blk, phy_blk);
	fprintf(m_log_invalid_trace, "%lld,%s,%d,%d,%d\n", m_write_count, reason ? reason : "", phy_blk, _seg, _blk);
#endif
	return free_seg;
}

bool CF2fsSimulator::OffsetToIndex(CIndexPath& ipath, LBLK_T offset, bool alloc)
{
	// 从inode中把已经有的index block填入node[]中
	ipath.level = 1;
	ipath.offset[0] = offset / INDEX_SIZE;

	NODE_INFO* inode = ipath.node[0];
	_NID nid = inode->inode.index[ipath.offset[0]];
	CPageInfo* index_page = nullptr;
	NODE_INFO * index_node = nullptr;
	if (is_invalid(nid))
	{	// 对应的 offset node还未创建
		if (alloc)
		{	// 创建新的 node
			index_page = m_pages.allocate(true);
			BLOCK_DATA * block = m_pages.get_data(index_page);
			if (!InitIndexNode(block, inode->m_ino, index_page)) {
//				THROW_FS_ERROR(ERR_NO_SPACE, L"node full during allocate index block");
				LOG_ERROR(L"node full during allocate index block");
				return false;
			}
			index_node = &block->node;
			nid = block->node.m_nid;
			inode->inode.index[ipath.offset[0]] = nid;
			CPageInfo* ipage = m_pages.page(inode->page_id);
			ipage->dirty = true;
			//缓存page
			m_nat.node_cache[nid] = m_pages.page_id(index_page);//index_page->page_id;
			// 给index node预留block
			PHY_BLK phy = m_segments.WriteBlockToSeg(index_page, false);
			if (is_invalid(phy)) {
				LOG_ERROR(L"[err] no enough space for adding index block");
				return false;
			}
			m_nat.set_phy_blk(nid, phy);
		}
	}
	else
	{	// node 已经创建，检查是否已经缓存
		index_node = &ReadNode(nid, index_page);
	}
	ipath.page[1] = index_page;
	ipath.node[1] = index_node;
	ipath.offset[1] = offset % INDEX_SIZE;
	// 计算每一层的偏移量
	return true;
}

void CF2fsSimulator::NextOffset(CIndexPath& ipath)
{
	ipath.offset[1]++;
	if (ipath.offset[1] >= INDEX_SIZE)
	{
		ipath.level = -1;
		ipath.node[1] = nullptr;
	}
}

void CF2fsSimulator::GetConfig(boost::property_tree::wptree& config, const std::wstring& config_name)
{
	if (config_name == L"multi_header_num")
	{
		config.put(L"multi_header_num", m_multihead_cnt);
	}
}

//void CF2fsSimulator::DumpAllFileMap(const std::wstring& fn)
//{
//}
//
//
//void CF2fsSimulator::DumpBlockWAF(const std::wstring& fn)
//{
//	FILE* log = nullptr;
//	_wfopen_s(&log, fn.c_str(), L"w+");
//	if (log == nullptr) THROW_ERROR(ERR_APP, L"failed on opening file %s", fn.c_str());
//
//	UINT64 host_write[BT_TEMP_NR + 1];
//	UINT64 media_write[BT_TEMP_NR + 1];
//	UINT64 blk_count[BT_TEMP_NR + 1];
//
//	memset(host_write, 0, sizeof(UINT64) * (BT_TEMP_NR + 1));
//	memset(media_write, 0, sizeof(UINT64) * (BT_TEMP_NR + 1));
//	memset(blk_count, 0, sizeof(UINT64) * (BT_TEMP_NR + 1));
//
//
//	for (SEG_T ss = 0; ss < m_segments.get_seg_nr(); ++ss)
//	{
//		const SegmentInfo& seg = m_segments.get_segment(ss);
//		for (BLK_T bb = 0; bb < BLOCK_PER_SEG; ++bb)
//		{
//			//PAGE_INDEX _pp = seg.blk_map[bb];
//			//CPageInfo* page = m_pages.page(_pp);
//			//if (page == nullptr) { continue; }
//
//			//BLK_TEMP temp = page->ttemp;
//			//JCASSERT(temp < BT_TEMP_NR);
//			//host_write[temp] += page->host_write;
//			//media_write[temp] += page->media_write;
//			//blk_count[temp]++;
//		}
//	}
//	fprintf_s(log, "block_temp,host_write,media_write,WAF,blk_num,avg_host_write\n");
//	for (int ii = 0; ii < BT_TEMP_NR; ++ii)
//	{
//		fprintf_s(log, "%s,%lld,%lld,%.2f,%lld,%.2f\n", BLK_TEMP_NAME[ii], host_write[ii], media_write[ii],
//			(float)(media_write[ii]) / (float)(host_write[ii]),
//			blk_count[ii], (float)(host_write[ii]) / blk_count[ii]);
//	}
//
//	// 分别输出data和node的数据
//	UINT64 data_cnt = blk_count[BT_COLD_DATA] + blk_count[BT_WARM_DATA] + blk_count[BT_HOT__DATA];
//	UINT64 data_host_wr = host_write[BT_COLD_DATA] + host_write[BT_WARM_DATA] + host_write[BT_HOT__DATA];
//	UINT64 data_media_wr = media_write[BT_COLD_DATA] + media_write[BT_WARM_DATA] + media_write[BT_HOT__DATA];
//
//	fprintf_s(log, "%s,%lld,%lld,%.2f,%lld,%.2f\n", "DATA", data_host_wr, data_media_wr,
//		(float)(data_media_wr) / (float)(data_host_wr),
//		data_cnt, (float)(data_host_wr) / data_cnt);
//
//	UINT64 node_cnt = blk_count[BT_COLD_NODE] + blk_count[BT_WARM_NODE] + blk_count[BT_HOT__NODE];
//	UINT64 node_host_wr = host_write[BT_COLD_NODE] + host_write[BT_WARM_NODE] + host_write[BT_HOT__NODE];
//	UINT64 node_media_wr = media_write[BT_COLD_NODE] + media_write[BT_WARM_NODE] + media_write[BT_HOT__NODE];
//
//	fprintf_s(log, "%s,%lld,%lld,%.2f,%lld,%.2f\n", "NODE", node_host_wr, node_media_wr,
//		(float)(node_media_wr) / (float)(node_host_wr),
//		node_cnt, (float)(node_host_wr) / node_cnt);
//
//	fprintf_s(log, "%s,%lld,%lld,%.2f,\n", "TOTAL", m_health_info.m_total_host_write, m_health_info.m_total_media_write,
//		(float)(m_health_info.m_total_media_write) / (float)(m_health_info.m_total_host_write));
//
//	for (int ii = 0; ii < BT_TEMP_NR; ++ii)
//	{
//		fprintf_s(log, "TRUNC_%s,%d,%.2f\n", BLK_TEMP_NAME[ii], -1, -1.0);
//	}
//	fclose(log);
//}
//
//size_t CF2fsSimulator::DumpFileIndex(_NID index[], size_t buf_size, _NID fid)
//{
//	CPageInfo* ipage = nullptr; // m_pages.allocate_index(true);
//	NODE_INFO &inode = ReadNode(fid, ipage);
//	memcpy_s(index, sizeof(_NID) * buf_size, inode.inode.index, sizeof(_NID) * INDEX_TABLE_SIZE);
//	return inode.index.valid_data;
//}
//
//void CF2fsSimulator::DumpSegments(const std::wstring& fn, bool sanity_check)
//{
//}
//
//void CF2fsSimulator::DumpSegmentBlocks(const std::wstring& fn)
//{
////	m_segments.DumpSegmentBlocks(fn);
//}
//
#define FLUSH_FILE_MAP(phy)	{\
	if (is_invalid(start_phy)) {\
		SEG_T seg; BLK_T blk; BlockToSeg(seg,blk,start_phy);	\
		fprintf_s(out, "%S,%d,%d,%d,%X,%d,%d,%d,%d\n", inode.inode.m_fn.c_str(), fid, start_offset, (bb - start_offset), start_phy,seg,blk,host_write,media_write);}\
		host_write=0, media_write=0; start_phy = phy; pre_phy = phy; }

//void CF2fsSimulator::DumpFileMap_merge(FILE* out, _NID fid)
//{
//}

//void CF2fsSimulator::DumpFileMap_no_merge(FILE* out, _NID fid)
//{
//}




bool CF2fsSimulator::InitInode(BLOCK_DATA* block, CPageInfo* page, F2FS_FILE_TYPE type)
{
	memset(block, 0xFF, sizeof(BLOCK_DATA));
	block->m_type = BLOCK_DATA::BLOCK_INODE;

	NODE_INFO* node = &(block->node);
	node->m_nid = m_nat.get_node();
	if (is_invalid(node->m_nid)) {
		LOG_ERROR(L"node is full");
		return false;
	}

	//node->valid_data = 0;
	node->page_id = m_pages.page_id(page);// page->page_id;
	node->m_ino = node->m_nid;

	node->inode.blk_num = 0;
	node->inode.file_size = 0;
	node->inode.ref_count = 0;
	node->inode.nlink = 0;
	node->inode.file_type = type;

	page->nid = node->m_nid;
	page->offset = INVALID_BLK;
	page->dirty = true;
	return true;
}

bool CF2fsSimulator::InitIndexNode(BLOCK_DATA* block, _NID parent, CPageInfo* page)
{
	memset(block, 0xFF, sizeof(BLOCK_DATA));
	block->m_type = BLOCK_DATA::BLOCK_INDEX;

	NODE_INFO* node = &(block->node);
	node->m_nid = m_nat.get_node();
	if (is_invalid(node->m_nid)) {
		LOG_ERROR(L"node is full");
		//			THROW_ERROR(ERR_APP, L"node is full");
		return false;
	}

	node->m_ino = parent;
	node->index.valid_data = 0;
	node->page_id = m_pages.page_id(page); // page->page_id;

	page->nid = node->m_nid;
	page->offset = INVALID_BLK;
	page->dirty = true;
	return true;
}
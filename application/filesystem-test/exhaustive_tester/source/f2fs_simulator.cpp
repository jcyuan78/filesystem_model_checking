///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
#include "pch.h"
#include "../include/f2fs_simulator.h"
#include <boost/unordered_set.hpp>

LOCAL_LOGGER_ENABLE(L"simulator.f2fs", LOGGER_LEVEL_DEBUGINFO);

LOG_CLASS_SIZE(INODE);
LOG_CLASS_SIZE(INDEX_NODE);
LOG_CLASS_SIZE(NODE_INFO);
LOG_CLASS_SIZE(DENTRY);
LOG_CLASS_SIZE(DENTRY_BLOCK);
LOG_CLASS_SIZE(NAT_BLOCK);
LOG_CLASS_SIZE(SIT_BLOCK);
LOG_CLASS_SIZE(BLOCK_DATA);
LOG_CLASS_SIZE(SUMMARY);
LOG_CLASS_SIZE(SUMMARY_BLOCK);
//LOG_CLASS_SIZE();
//LOG_CLASS_SIZE();

//#define MULTI_HEAD	1
const char* BLK_TEMP_NAME[] = { "COLD_DATA", "COLD_NODE", "WARM_DATA", "WARM_NODE", "HOT__DATA", "HOT__NODE", "EMPTY" };

// type 1: 输出gc log, block count相关，2: 输出gc 性能分析，最大值VB，最小值VB，等
//#define GC_TRACE_TYPE 1
#define GC_TRACE_TYPE 2

CF2fsSimulator::CF2fsSimulator(void)
	:m_segments(this), m_storage(this), m_pages(this), m_nat(this)
{
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
}

void CF2fsSimulator::CopyFrom(const IFsSimulator* src)
{
	const CF2fsSimulator* _src = dynamic_cast<const CF2fsSimulator*>(src);
	if (_src == nullptr) THROW_ERROR(ERR_APP, L"cannot copy from non CF2fsSimulator, src=%p", src);
	InternalCopyFrom(_src);
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
}

bool CF2fsSimulator::Initialzie(const boost::property_tree::wptree& config, const std::wstring & log_path)
{
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

	// 初始化
//	SEG_T gc_th_lo = GC_THRESHOLD_LO;
//	SEG_T gc_th_hi = GC_THRESHOLD_HI;
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
	CPageInfo* page = m_pages.allocate(true);
	BLOCK_DATA* root_node = m_pages.get_data(page);
	InitInode(root_node, page);
	root_node->node.m_ino = ROOT_FID;
	root_node->node.m_nid = ROOT_FID;
	page->nid = ROOT_FID;
	// root 作为永远打开的文件
	root_node->node.inode.ref_count = 1;			// root inode 始终cache
	m_nat.node_catch[ROOT_FID] = page->page_id;
	PHY_BLK root_phy = UpdateInode(page, "CREATE");	// 写入磁盘
	m_nat.set_phy_blk(ROOT_FID, root_phy);

	m_health_info.m_dir_num = 1;
	m_health_info.m_file_num = 0;
	// 初始化结束
	
	//fs_trace("INIT", 0, 0, 0);
	// set log path
	m_log_path = log_path;
	// invalid block trace log：记录按时间顺序写入和无效化的phy block
#ifdef ENABLE_FS_TRACE
	_wfopen_s(&m_log_invalid_trace, (m_log_path + L"\\trace_invalid_blk.csv").c_str(), L"w+");
	if (m_log_invalid_trace == nullptr) THROW_ERROR(ERR_APP, L"failed on creating log file %s", (m_log_path + L"\\trace_invalid_blk.csv").c_str());
	fprintf_s(m_log_invalid_trace, "index,reason,invalid,invalid_seg,invalid_blk\n");

	// file system trace log:
	// share for read
	m_log_write_trace = _wfsopen((m_log_path + L"\\trace_fs.csv").c_str(), L"w+", _SH_DENYNO);
	if (m_log_write_trace == nullptr) THROW_ERROR(ERR_APP, L"failed on creating log file %s", (m_log_path + L"\\trace_write_blk.csv").c_str());
	fprintf_s(m_log_write_trace, "op_id,op,fid,start_blk,blk_nr,used_blks,node_blks,free_blks,free_seg\n");

	// write trace log:
	_wfopen_s(&m_gc_trace, (m_log_path + L"\\trace_gc.csv").c_str(), L"w+");
	if (m_gc_trace == nullptr) THROW_ERROR(ERR_APP, L"failed on creating log file %s", (m_log_path + L"\\trace_gc.csv").c_str());

	// 增加 nid host write trace
	_wfopen_s(&m_inode_trace, (m_log_path + L"\\trace_inode.csv").c_str(), L"w+");
	if (m_inode_trace == nullptr) THROW_ERROR(ERR_APP, L"failed on creating log file: trace_inode.csv");
	fprintf_s(m_inode_trace, "op_id,fid,index,reason\n");
	m_segments.m_gc_trace = m_gc_trace;
#endif
	return true;
}

#ifdef ENABLE_FS_TRACE
void CF2fsSimulator::fs_trace(const char* op, NID fid, DWORD start_blk, DWORD blk_nr)
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


NODE_INFO& CF2fsSimulator::ReadNode(NID nid, CPageInfo * & page)
{	// 读取node，返回在page中。读取的page会保存在cache中，不需要调用者释放。
	// 如果page已经被缓存，则从缓存中读取。否则从磁盘读取。
	if (nid >= NODE_NR) THROW_ERROR(ERR_USER, L"Invalid node id: %d", nid);
	JCASSERT(page == nullptr);

	// 检查node是否在cache中
	BLOCK_DATA * block =nullptr;
	if (m_nat.node_catch[nid] != INVALID_BLK)
	{
		page = m_pages.page(m_nat.node_catch[nid]);
		block = m_pages.get_data(page);
	}
	else
	{	// 从磁盘中读取
		PHY_BLK blk = m_nat.get_phy_blk(nid);
		if (blk == INVALID_BLK) THROW_ERROR(ERR_APP, L"invalid nid=%d in reading node", nid);
		page = m_pages.allocate(true);
		m_storage.BlockRead(CF2fsSegmentManager::phyblk_to_lba(blk), page);
		page->phy_blk = blk;
		page->nid = nid;
		page->offset = INVALID_BLK;
		// 缓存读取的block
		m_nat.node_catch[nid] = page->page_id;
		block = m_pages.get_data(page);
		if (block->m_type != BLOCK_DATA::BLOCK_INDEX && block->m_type != BLOCK_DATA::BLOCK_INODE) {
			THROW_ERROR(ERR_APP, L"block phy-%d is not a node block, nid=%d", blk, nid);
		}		
		if (block->m_type == BLOCK_DATA::BLOCK_INODE) block->node.inode.ref_count = 0;
		
	}
	block->node.page_id = page->page_id;
	// 对于node，由于使用nat机制，page中不需要记录父node和offset。
	return block->node;
}

inline int DepthFromBlkNr(UINT blk_nr)
{
	int max_depth = 0;
	while (blk_nr != 0) max_depth++, blk_nr >>= 1;		// 通过block数量计算最大层次
	return max_depth;
}

NID CF2fsSimulator::FindFile(NODE_INFO& parent, const char* fn)
{
	if (parent.inode.blk_num == 0) return INVALID_BLK;
	// 计算文件名hash
	WORD hash = FileNameHash(fn);
	WORD fn_len = (WORD)(strlen(fn));
	NID fid = INVALID_BLK;

	// 抽象的 dir file 结构：以2^level的数量，在每层中存放dentry block。每个block属于一个hash slot。
	// 逐层查找文件 （f2fs :: __f2fs_find_entry() )
	int max_depth = DepthFromBlkNr(parent.inode.blk_num);
	int blk_num=1, blk_index=0/*, blk_end=blk_index+blk_num*/;		// 当前层次下，block数量，第一个block，和最后一个block
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
			int index = 0;
			for (int index = 0; index < DENTRY_PER_BLOCK; ++index)
			{
				if (entries.dentries[index].ino == INVALID_BLK || entries.dentries[index].hash != hash) continue;
				char* ff = entries.filenames[index];
				if (entries.dentries[index].name_len == fn_len && memcmp(ff, fn, fn_len)==0)
				{
					fid = entries.dentries[index].ino;
					break;
				}
			}
			if (fid != INVALID_BLK)
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

void CF2fsSimulator::CloseInode(CPageInfo* &ipage)
{
	BLOCK_DATA* data = m_pages.get_data(ipage);
	if (data->m_type != BLOCK_DATA::BLOCK_INODE) THROW_ERROR(ERR_USER, L"data in the page is not an inode");
	NID ino = data->node.m_nid;
	if (ipage->nid != ino)
	{
		THROW_ERROR(ERR_USER, L"nid does not match in data (%d) and page (%d)", ino, ipage->nid);
	}
	INODE & inode = data->node.inode;

	if (inode.ref_count != 0)
	{
		ipage = nullptr;
		return;
	}
	if (inode.nlink == 0)
	{
		FileRemove(ipage);
	}
	else
	{
		DWORD start_blk, last_blk;
		OffsetToBlock(start_blk, last_blk, 0, inode.file_size);
		LBLK_T bb = 0;

		for (size_t ii = 0; (ii < INDEX_TABLE_SIZE) && (bb < last_blk); ++ii, bb += INDEX_SIZE)
		{
			NID nid = inode.index[ii];
			if (nid == INVALID_BLK) continue;		// index 空洞，跳过
			PAGE_INDEX page_id = m_nat.node_catch[nid];
			if (page_id != INVALID_BLK)
			{
				CPageInfo* page = m_pages.page(page_id);
				if (page->dirty)
				{
					// <TODO> page的nid和offset应该在page申请时设置
					if (page->nid != nid) THROW_ERROR(ERR_USER, L"nid does not match in data (%d) and page (%d)", nid, page->nid);
					PHY_BLK phy = m_segments.WriteBlockToSeg(page);
					m_nat.set_phy_blk(nid, phy);
				}
				// decache index page
				m_nat.node_catch[nid] = INVALID_BLK;
				m_pages.free(page);
			}
		}
		// release ipage
		if (ipage->dirty)
		{
			PHY_BLK phy = m_segments.WriteBlockToSeg(ipage);
			m_nat.set_phy_blk(ino, phy);
			// decache index page
			m_nat.node_catch[ino] = INVALID_BLK;
			m_pages.free(ipage);
		}
	}
	ipage = nullptr;
}

void CF2fsSimulator::UpdateNat(NID nid, PHY_BLK phy_blk)
{
	// 当一个node block被搬移的时候，更新L2P的链接。 <TODO> F2FS 检查
	// 首先检查这个nid是否在cache中
	//	如果在cache中，且cache dirty：放弃GC，将cache 写入storage
	//	如果在cache中，且cache undirty：discache
	m_nat.set_phy_blk(nid, phy_blk);
}

void CF2fsSimulator::UpdateIndex(NID nid, UINT offset, PHY_BLK phy_blk)
{
	// 检查index是否被catch
	CPageInfo* page = nullptr;
	NODE_INFO* node = nullptr;
	node = &ReadNode(nid, page);
	// 更新index
	if (node->index.index[offset] == INVALID_BLK)
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

void CF2fsSimulator::AddChild(NODE_INFO* parent, const char* fn, NID fid)
{
	// 计算文件名hash
	WORD hash = FileNameHash(fn);
	WORD fn_len = (WORD)(strlen(fn));
	int slot_num = ROUND_UP_DIV(fn_len, FN_SLOT_LEN);

	int max_depth = DepthFromBlkNr(parent->inode.blk_num);
	int blk_num=1, blk_index=0;		// 当前层次下，block数量，第一个block，和最后一个block
	int level = 0;
	int index_start = 0;

	int blk_offset = 0;
	CPageInfo* dentry_page = nullptr;

	// 读取dir文件，
	if (parent->inode.blk_num > 0)
	{

		for (; level < max_depth; level++)
		{	// 按层次查找空位，
			WORD hh = hash % blk_num;
			blk_offset = blk_index + hh;
			CPageInfo* page = nullptr;
			FileReadInternal(&page, *parent, blk_offset, 1);
			if (page != nullptr)
			{
				BLOCK_DATA& block = *m_pages.get_data(page);
				DENTRY_BLOCK& entries = block.dentry;

				int index = 0;
				DWORD mask = 1;
				while (1)
				{
					while (entries.bitmap & mask) { mask <<= 1; index_start++; }
					if (index_start >= DENTRY_PER_BLOCK) break;	// 没有空位 goto next 
					index = index_start;
					while (((entries.bitmap & mask) == 0) && index < DENTRY_PER_BLOCK) { mask <<= 1; index++; }
					if ((index - index_start) >= slot_num)
					{	// 找到空位
						dentry_page = page;
						break;
					}
				}
				if (index_start < DENTRY_PER_BLOCK)		break;	//找到空位
				m_pages.free(page);
			}
			else
			{	// 这个slot是空的，创建slot 申请一个空的block
				blk_offset = blk_index + hh;
				dentry_page = AllocateDentryBlock();
				index_start = 0;
				// 如果block在文件大小外，更新文件大小
				UINT file_size = (blk_offset + 1) * BLOCK_SIZE;
				if (file_size > parent->inode.file_size) parent->inode.file_size = file_size;
				break;
			}
			blk_index += blk_num, blk_num *= 2;
		}
	}

	if (index_start >= DENTRY_PER_BLOCK || dentry_page == nullptr)
	{	// 没有找到空位， 添加新的层次
		max_depth++;
		if (max_depth >= MAX_DENTRY_LEVEL) {
			THROW_ERROR(ERR_APP, L"dentry is full, parent fid=%d, fn len=%zd", parent->m_nid, fn_len);
		}
		blk_num = (1 << (max_depth - 1));
		blk_index = blk_num - 1;
		WORD hh = hash % blk_num;

		// 申请一个空的block
		blk_offset = blk_index + hh;
		dentry_page = AllocateDentryBlock(/*dpage_id*/);
		index_start = 0;
		// 更新文件大小
		parent->inode.file_size = (blk_offset + 1) * BLOCK_SIZE;
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

	// 向文件填充 blk_num block
	FileWriteInternal(*parent, blk_offset, 1, &dentry_page);
	m_pages.free(dentry_page);
}

void CF2fsSimulator::Unlink(NID fid, CPageInfo * parent_page)
{
	BLOCK_DATA* data = m_pages.get_data(parent_page);
	NODE_INFO& parent = data->node;

	for (UINT ii = 0; ii < parent.inode.blk_num; ++ii)
	{
		CPageInfo* page = nullptr;
		FileReadInternal(&page, parent, ii, 1);

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
				}
			}
			m_pages.free(page);
		}
	}
}


NID CF2fsSimulator::InternalCreatreFile(const std::string& fn, bool is_dir)
{
	// 根据路径找到父节点
	char full_path[MAX_PATH_SIZE+1];
	strcpy_s(full_path, fn.c_str());

	char * ptr = strrchr(full_path, '\\');
	if (ptr == nullptr || *ptr == 0) return INVALID_BLK;
	*ptr = 0;
	CPageInfo* page = nullptr; // m_pages.allocate(true);
	NID parent_fid = FileOpenInternal(full_path, page);	// 返回需要添加的父目录的nid，parent_page是祖父目录的page
	CloseInode(page);
	if (parent_fid == INVALID_BLK)
	{
		LOG_ERROR(L"[err] parent path %S is not exist", full_path);
		return INVALID_BLK;
	}

	CPageInfo* parent_page = nullptr;
	NODE_INFO & parent = ReadNode(parent_fid, parent_page);
	// 检查 fn 不在 parent目录中
	char* name = ptr + 1;
	if (FindFile(parent, name) != INVALID_BLK)
	{
		LOG_ERROR(L"[err] file %s is already exist", fn.c_str());
		return INVALID_BLK;
	}

	// 创建inode，allocate nid
	page = m_pages.allocate(true);
	BLOCK_DATA* inode_block = m_pages.get_data(page);
	InitInode(inode_block, page);
	NID nid = inode_block->node.m_nid;
	m_nat.node_catch[nid] = page->page_id;		// cache page;
	// 添加到父节点dentry中
	AddChild(&parent, name, nid);
	inode_block->node.inode.nlink++;
//	fs_trace("CREATE", page->data_index, 0, 0);
	// 更新inode
	PHY_BLK phy_blk = UpdateInode(page, "CREATE");
	m_nat.set_phy_blk(nid, phy_blk);
	InterlockedIncrement(&(inode_block->node.inode.ref_count));
	CloseInode(parent_page);
	return nid;
}

NID CF2fsSimulator::FileCreate(const std::string& fn)
{
	if (m_open_nr >= MAX_OPEN_FILE) THROW_ERROR(ERR_USER, L"oped file reached max (%d)", MAX_OPEN_FILE);

	NID fid = InternalCreatreFile(fn, false);
	if (fid == INVALID_BLK) return INVALID_BLK;
	// 放入open list中
	m_health_info.m_file_num++;

	CPageInfo* page = nullptr; // m_pages.allocate(true);
	NODE_INFO *inode = &ReadNode(fid, page);		// now, the ipage points to the opened file.
	// 放入open list
	AddFileToOpenList(fid, page);
	return fid;
}

NID CF2fsSimulator::DirCreate(const std::string& fn)
{
	NID fid = InternalCreatreFile(fn, true);
	if (fid != INVALID_BLK) m_health_info.m_dir_num++;
	return fid;
}

OPENED_FILE* CF2fsSimulator::FindOpenFile(NID fid)
{
	for (int ii = 0; ii < MAX_OPEN_FILE; ++ii)
	{
		if (m_open_files[ii].ino == fid) return (m_open_files + ii);
	}
	return nullptr;
}

OPENED_FILE* CF2fsSimulator::AddFileToOpenList(NID fid, CPageInfo* page)
{
	// 放入open list
	UINT index = m_free_ptr;
	m_free_ptr = m_open_files[index].ipage;		// next
	m_open_files[index].ino = fid;
	m_open_files[index].ipage = page->page_id;
	m_open_nr++;
	return m_open_files + index;
}

NID CF2fsSimulator::FileOpen(const std::string& fn, bool delete_on_close)
{
	char full_path[MAX_PATH_SIZE + 1];
	strcpy_s(full_path, fn.c_str());

	CPageInfo* page = nullptr; // m_pages.allocate(true);
	NID nid = FileOpenInternal(full_path, page);
	CloseInode(page);
	if (nid == INVALID_BLK)	{	return INVALID_BLK; }
	page = nullptr;

	// 查找文件是否已经打开
	NODE_INFO* inode = nullptr;
	OPENED_FILE* file = FindOpenFile(nid);
	if (file)
	{	// 文件已经打开
		page = m_pages.page(file->ipage);
		inode = &m_pages.get_data(page)->node;
	}
	else
	{
		if (m_open_nr >= MAX_OPEN_FILE) THROW_ERROR(ERR_USER, L"oped file reached max (%d)", MAX_OPEN_FILE);
		inode = &ReadNode(nid, page);		// now, the ipage points to the opened file.
		// 放入open list
		AddFileToOpenList(nid, page);
	}
	inode->inode.ref_count++;
	return nid;
}

bool CF2fsSimulator::Mount(void)
{
	m_segments.Load();
	m_nat.Load();
	// load root
	CPageInfo* page = nullptr;	//m_pages.allocate(true);
	NODE_INFO & root_node = ReadNode(ROOT_FID, page);
	root_node.inode.ref_count = 1;
	m_nat.node_catch[root_node.m_nid] = page->page_id;

	return true;
}

bool CF2fsSimulator::Unmount(void)
{
	// close all files
	for (int ii = 0; ii < MAX_OPEN_FILE; ++ii)
	{
		if (m_open_files[ii].ino != INVALID_BLK) {
			FileCloseInternal(m_open_files + ii);
		}
	}
	// 保存index, 可能是因为gc打开的index page
	for (int ii = 0; ii < NODE_NR; ++ii)
	{
		PAGE_INDEX page_id = m_nat.node_catch[ii];
		if (page_id != INVALID_BLK)
		{
			CPageInfo* page = m_pages.page(page_id);
			if (page->dirty)
			{
				// <DONE> page的nid和offset应该在page申请时设置
				PHY_BLK phy = m_segments.WriteBlockToSeg(page);
				m_nat.set_phy_blk(ii, phy);
			}
			m_pages.free(page);
			m_nat.node_catch[ii] = INVALID_BLK;
		}
	}

	m_nat.Sync();
	m_segments.Sync();
	return true;
}

bool CF2fsSimulator::Reset(void)
{
	// reset open list
	InitOpenList();
	m_segments.Reset();
	m_nat.Reset();
	m_pages.Reset();
	
	return true;
}

NID CF2fsSimulator::FileOpenInternal(char* fn, CPageInfo* &parent_inode)
{
	JCASSERT(parent_inode == nullptr);
	NODE_INFO& root = ReadNode(ROOT_FID, parent_inode);		// 打开root文件
	NODE_INFO* parent_node = &root;
	NID fid = ROOT_FID;

	char* next = nullptr;
	char* dir = strtok_s(fn, "\\", &next);			// dir应该首先指向第一个“\”
	if (dir == nullptr || *dir == 0) return ROOT_FID;

	while (1)
	{	
		// cur: 父节点，dir：目标节点
		fid = FindFile(*parent_node, dir);
		if (fid == INVALID_BLK)
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

void CF2fsSimulator::FileCloseInternal(OPENED_FILE* file)
{
	CPageInfo* ipage = m_pages.page(file->ipage);
	BLOCK_DATA* data = m_pages.get_data(ipage);
	INODE& inode = data->node.inode;

	if (inode.ref_count <= 0) THROW_ERROR(ERR_USER, L"file's refrence == 0, fid=%d", data->node.m_ino);
	inode.ref_count--;
	if (inode.ref_count == 0)
	{
		// 释放 open list
		file->ino = INVALID_BLK;
		file->ipage = m_free_ptr;
		m_free_ptr = (UINT)(file - m_open_files);
		m_open_nr--;
		CloseInode(ipage);
	}
}

void CF2fsSimulator::FileClose(NID fid)
{
	// find opne file
	OPENED_FILE* file = FindOpenFile(fid);
	if (file == nullptr) THROW_ERROR(ERR_USER, L"file fid=%d is not opened", fid);
	CPageInfo* ipage = m_pages.page(file->ipage);
	INODE& inode = m_pages.get_data(ipage)->node.inode;

	FileCloseInternal(file);
}

void CF2fsSimulator::FileWrite(NID fid, FSIZE offset, FSIZE len)
{
	// find opne file
	OPENED_FILE* file = FindOpenFile(fid);
	if (file == nullptr) THROW_ERROR(ERR_USER, L"file fid=%d is not opened", fid);
	CPageInfo* ipage = m_pages.page(file->ipage);
	NODE_INFO & inode = m_pages.get_data(ipage)->node;

	// set file size
	UINT end_pos = offset + len;
	if (end_pos > inode.inode.file_size) inode.inode.file_size = end_pos;

	// 计算起始block和结束block，end_block是最后要写入的下一个。blk_nr=end_block - start_block
	DWORD start_blk, end_blk;
	OffsetToBlock(start_blk, end_blk, offset, len);
//	LOG_TRACK(L"fs", L",WriteFile,fid=%d,offset=%d,len=%d", fid, start_blk, (end_blk - start_blk));

	if (end_blk > MaxFileSize()) 
		THROW_ERROR(ERR_APP, L"file size is too large, blks=%d, max_size=%d", end_blk, MaxFileSize());
	DWORD blk_num = end_blk - start_blk;
	PPAGE pages[MAX_FILE_BLKS];
	for (UINT ii=0, bb = start_blk; bb < end_blk; ++ii, ++bb)
	{
		pages[ii] = m_pages.allocate(true);
		BLOCK_DATA* data = m_pages.get_data(pages[ii]);
		data->file.fid = fid;
		data->file.offset = bb;
	}
	FileWriteInternal(inode, start_blk, end_blk, pages);
	for (UINT ii = 0; ii < blk_num; ++ii) m_pages.free(pages[ii]);
}

void CF2fsSimulator::FileWriteInternal(NODE_INFO& inode, FSIZE start_blk, FSIZE end_blk, CPageInfo* pages[])
{
	CIndexPath ipath(&inode);
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
			OffsetToIndex(ipath, start_blk, true);
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
		PHY_BLK new_phy_blk = m_segments.WriteBlockToSeg(dpage);
		di_node->index.index[offset] = new_phy_blk;
//		LOG_DEBUG(L"write data page: fid=%d, index_nid=%d, lblk=%d, phy=%d,", inode.m_ino, di_node->m_nid, start_blk, new_phy_blk);
//		LOG_DEBUG(L"write fid:%d, lblk:%d, phy blk:%d", inode.m_ino, start_blk, new_phy_blk);

		if (phy_blk == INVALID_BLK)
		{	// 这个逻辑块没有被写过，增加逻辑饱和度，增加inode的block number。对于写过的block，在WriteBlockToSeg()中invalidate 旧block
			InterlockedIncrement(&m_health_info.m_logical_saturation);
			if (m_health_info.m_logical_saturation >= (m_health_info.m_logical_blk_nr))
			{
				LOG_WARNING(L"[warning] logical saturation overflow, logical_saturation=%d, logical_block=%d", m_health_info.m_logical_saturation, m_health_info.m_logical_blk_nr);
			}
			inode.inode.blk_num++;
			di_node->valid_data++;
		}
		// 注意，此处并没有实现 NAT，存在wandering tree问题。
		di_page->dirty = true;
		InterlockedIncrement64(&m_health_info.m_total_host_write);
		dpage->host_write++;
		// 将ipath移动到下一个offset 
		m_segments.CheckGarbageCollection(this);
		NextOffset(ipath);
	}
	// 更新ipath
	CPageInfo* ipage = m_pages.page(inode.page_id);
	UpdateInode(ipage, "WRITE");
} 

void CF2fsSimulator::FileReadInternal(CPageInfo * pages[], NODE_INFO& inode, FSIZE start_blk, FSIZE end_blk)
{
	// sanity check
	NID fid = inode.m_nid;
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
		CPageInfo* page = m_pages.allocate(true);
		m_storage.BlockRead(CF2fsSegmentManager::phyblk_to_lba(blk),page);
		page->phy_blk = blk;
		page->nid = di_node->m_nid;
		page->offset = index;
		if (pages[page_index] != nullptr) THROW_ERROR(ERR_APP, L"read internal() will allocate page");
		pages[page_index] = page;
		// 将ipath移动到下一个offset 
		NextOffset(ipath);
	}
}

size_t CF2fsSimulator::FileRead(FILE_DATA blks[], NID fid, FSIZE offset, FSIZE secs)
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
	DWORD start_blk, end_blk;
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
			if (phy_blk != INVALID_BLK)
			{
				InterlockedDecrement(&m_health_info.m_logical_saturation);
				InvalidBlock("TRANCATE", phy_blk);
				di_node->index.index[index] = INVALID_BLK;
				di_page->dirty = true;
				di_node->valid_data--;
				inode.inode.blk_num--;
			}
		}
		// 将ipath移动到下一个offset 
		NextOffset(ipath);
	}
	UpdateInode(ipage, "TRUNCATE");
}


void CF2fsSimulator::FileTruncate(NID fid, FSIZE offset, FSIZE len)
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

	DWORD start_blk, end_blk;
	OffsetToBlock(start_blk, end_blk, offset, len);
	fs_trace("TRUCATE", fid, start_blk, end_blk - start_blk);
	FileTruncateInternal(ipage, start_blk, end_blk);

	if (end_pos == inode.inode.file_size)
	{
		inode.inode.file_size = offset;
	}
}

void CF2fsSimulator::FileDelete(const std::string& fn)
{
	// Delete 分两步操作：（1）unlink，将文件的inode从dentry中删除，inode的link减1。（2）remove inode，将inode删除
	char _fn[MAX_PATH_SIZE + 1];
	strcpy_s(_fn, fn.c_str());

	// unlink
	CPageInfo* parent_page = nullptr; // m_pages.allocate(true);
	NID fid = FileOpenInternal(_fn, parent_page);
	if (fid == INVALID_BLK)
	{	//m_pages.free(parent_page);
		CloseInode(parent_page);
		THROW_ERROR(ERR_APP, L"[err] file %S is not existing", fn.c_str());
	}
	// 检查文件是否已经打开
	// find opne file
	CPageInfo* ipage = nullptr;
	NODE_INFO* inode = nullptr;
	inode = &ReadNode(fid, ipage);

	Unlink(fid, parent_page);
	inode->inode.nlink--;
	CloseInode(ipage);

	CloseInode(parent_page);

	// <TODO> 对于目录，删除其所有子目录
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
	m_nat.node_catch[inode->m_nid] = INVALID_BLK;
	m_nat.put_node(inode->m_nid);
	InvalidBlock("DELETE_NODE", inode_page->phy_blk);
	m_pages.free(inode_page);
	inode_page = nullptr;

	m_node_blks--;
	// page 在free_inode中删除, 统计被回收的inode的WAF
	m_health_info.m_file_num--;
}

void CF2fsSimulator::FileFlush(NID fid)
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
	DWORD start_blk, last_blk;
	OffsetToBlock(start_blk, last_blk, 0, inode.inode.file_size);
	LBLK_T bb = 0;
	for (size_t ii = 0; (ii < INDEX_TABLE_SIZE) && (bb < last_blk); ++ii, bb += INDEX_SIZE)
	{
		NID nid = inode.inode.index[ii];
		if (nid == INVALID_BLK) continue;	// index 空洞

		PAGE_INDEX di_page_id = m_nat.node_catch[nid];
		if (di_page_id == INVALID_BLK) continue;		// offset node的数据不存在，或者没有被更新

		CPageInfo* di_page = m_pages.page(di_page_id);
		if (!di_page) THROW_ERROR(ERR_USER, L"failed on getting page, id=%d", di_page_id);

		BLOCK_DATA* data = m_pages.get_data(di_page);
		if (data == nullptr) THROW_ERROR(ERR_USER, L"index block in page is null, fid=%d, index=%d", inode.m_nid, ii);

		NODE_INFO& di_node = data->node;
		if (di_node.page_id != di_page_id) {
			THROW_ERROR(ERR_USER, L"data unmatch, fid=%d, index=%d, page=%p, page_in_blk=%p",
				inode.m_nid, ii, di_page, di_node.page_id);
		}

		if (di_node.valid_data == 0)
		{
			InvalidBlock("", di_page->phy_blk);
			m_node_blks--;
			m_nat.put_node(di_node.m_nid);
			// decache index page
			m_nat.node_catch[nid] = INVALID_BLK;
			m_pages.free(di_page_id);

			inode.inode.index[ii] = INVALID_BLK;
			ipage->dirty = true;
			continue;
		}
		if (di_page->dirty)
		{
			di_page->host_write++;
			if (di_page->phy_blk == INVALID_BLK) m_node_blks++;
			// page的nid和offset应该在page申请时设置
			PHY_BLK phy_blk = m_segments.WriteBlockToSeg(di_page);
			//LOG_DEBUG(L"write index, fid=%d, nid=%d, phy=%d,", inode.m_ino, di_node.m_nid, phy_blk);
			m_nat.set_phy_blk(nid, phy_blk);
			m_segments.CheckGarbageCollection(this);
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
		ipage->host_write++;
		if (ipage->phy_blk == INVALID_BLK) m_node_blks++;
		// <DONE> page的nid和offset应该在page申请时设置
		new_phy = m_segments.WriteBlockToSeg(ipage);
//		LOG_DEBUG(L"write offset block, fid:%d, nid:%d, phy:%d", inode.m_ino, inode.m_nid, new_phy);
		m_nat.set_phy_blk(inode.m_nid, new_phy);
		m_segments.CheckGarbageCollection(this);

#ifdef ENABLE_FS_TRACE
		//<TRACE>记录inode的更新情况。
		// opid, fid, inode或者index id, 原因，数据更新数量。
		fprintf_s(m_inode_trace, "%lld,%d,0,UPDATE\n", m_write_count, nid.m_nid);
#endif
	}
	return new_phy;
}

FSIZE CF2fsSimulator::GetFileSize(NID fid)
{
	OPENED_FILE* file = FindOpenFile(fid);
	if (file == nullptr) THROW_ERROR(ERR_USER, L"file fid=%d is not opened", fid);
	CPageInfo* ipage = m_pages.page(file->ipage);
	INODE& inode = m_pages.get_data(ipage)->node.inode;
	UINT file_size = inode.file_size;
	return (file_size);
}

void CF2fsSimulator::GetFsInfo(FS_INFO& space_info)
{
	space_info.total_seg = m_segments.get_seg_nr();
	space_info.free_seg = m_segments.get_free_nr();
	space_info.total_blks = m_health_info.m_blk_nr;
	space_info.used_blks = m_health_info.m_logical_saturation;
	space_info.free_blks = m_health_info.m_free_blk;
	// 可能出现负数
	if (space_info.free_blks > m_health_info.m_logical_blk_nr) space_info.free_blks = 0;
	space_info.physical_blks = m_health_info.m_physical_saturation;
	space_info.max_file_nr = NODE_NR/2;

	space_info.dir_nr = m_health_info.m_dir_num;
	space_info.file_nr = m_health_info.m_file_num;

	space_info.total_host_write = m_health_info.m_total_host_write;
	space_info.total_media_write = m_health_info.m_total_media_write;

	space_info.total_page_nr = m_pages.total_page_nr();
	space_info.total_data_nr = m_pages.total_data_nr();
	space_info.free_page_nr  = m_pages.free_page_nr();
	space_info.free_data_nr  = m_pages.free_data_nr();
	space_info.max_opened_file = MAX_OPEN_FILE;
}

BLK_TEMP CF2fsSimulator::GetBlockTemp(CPageInfo* page)
{
	BLOCK_DATA* data = m_pages.get_data(page);
	if (data && (data->m_type == BLOCK_DATA::BLOCK_INODE || data->m_type == BLOCK_DATA::BLOCK_INDEX) )
	{
		if (data->m_type == BLOCK_DATA::BLOCK_INODE) return BT_HOT__NODE;
		else if (data->m_type == BLOCK_DATA::BLOCK_INDEX) return BT_COLD_NODE;
		else JCASSERT(0);
	}
	else
	{
		if (page->host_write >= 3) return BT_HOT__DATA;
		else return BT_COLD_DATA;
	}
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



bool CF2fsSimulator::InvalidBlock(const char* reason, PHY_BLK phy_blk)
{
	if (phy_blk == INVALID_BLK) return false;
	bool free_seg = m_segments.InvalidBlock(phy_blk);
#ifdef ENABLE_FS_TRACE
	SEG_T _seg;
	BLK_T _blk;
	CF2fsSegmentManager::BlockToSeg(_seg, _blk, phy_blk);
	fprintf(m_log_invalid_trace, "%lld,%s,%d,%d,%d\n", m_write_count, reason ? reason : "", phy_blk, _seg, _blk);
#endif
	return free_seg;
}

void CF2fsSimulator::OffsetToIndex(CIndexPath& ipath, LBLK_T offset, bool alloc)
{
	// 从inode中把已经有的index block填入node[]中
	ipath.level = 1;
	ipath.offset[0] = offset / INDEX_SIZE;

	NODE_INFO* inode = ipath.node[0];
	NID nid = inode->inode.index[ipath.offset[0]];
	CPageInfo* index_page = nullptr;
	NODE_INFO * index_node = nullptr;
	if (nid == INVALID_BLK)
	{	// 对应的 offset node还未创建
		if (alloc)
		{	// 创建新的 node
			index_page = m_pages.allocate(true);
			BLOCK_DATA * block = m_pages.get_data(index_page);
			InitIndexNode(block, inode->m_ino, index_page);
			index_node = &block->node;
			nid = block->node.m_nid;
			inode->inode.index[ipath.offset[0]] = nid;
			//缓存page
			m_nat.node_catch[nid] = index_page->page_id;
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

void CF2fsSimulator::DumpAllFileMap(const std::wstring& fn)
{
}


void CF2fsSimulator::DumpBlockWAF(const std::wstring& fn)
{
	FILE* log = nullptr;
	_wfopen_s(&log, fn.c_str(), L"w+");
	if (log == nullptr) THROW_ERROR(ERR_APP, L"failed on opening file %s", fn.c_str());

	UINT64 host_write[BT_TEMP_NR + 1];
	UINT64 media_write[BT_TEMP_NR + 1];
	UINT64 blk_count[BT_TEMP_NR + 1];

	memset(host_write, 0, sizeof(UINT64) * (BT_TEMP_NR + 1));
	memset(media_write, 0, sizeof(UINT64) * (BT_TEMP_NR + 1));
	memset(blk_count, 0, sizeof(UINT64) * (BT_TEMP_NR + 1));


	for (SEG_T ss = 0; ss < m_segments.get_seg_nr(); ++ss)
	{
		const SegmentInfo& seg = m_segments.get_segment(ss);
		for (BLK_T bb = 0; bb < BLOCK_PER_SEG; ++bb)
		{
			//PAGE_INDEX _pp = seg.blk_map[bb];
			//CPageInfo* page = m_pages.page(_pp);
			//if (page == nullptr) { continue; }

			//BLK_TEMP temp = page->ttemp;
			//JCASSERT(temp < BT_TEMP_NR);
			//host_write[temp] += page->host_write;
			//media_write[temp] += page->media_write;
			//blk_count[temp]++;
		}
	}
	fprintf_s(log, "block_temp,host_write,media_write,WAF,blk_num,avg_host_write\n");
	for (int ii = 0; ii < BT_TEMP_NR; ++ii)
	{
		fprintf_s(log, "%s,%lld,%lld,%.2f,%lld,%.2f\n", BLK_TEMP_NAME[ii], host_write[ii], media_write[ii],
			(float)(media_write[ii]) / (float)(host_write[ii]),
			blk_count[ii], (float)(host_write[ii]) / blk_count[ii]);
	}

	// 分别输出data和node的数据
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
		(float)(m_health_info.m_total_media_write) / (float)(m_health_info.m_total_host_write));

	for (int ii = 0; ii < BT_TEMP_NR; ++ii)
	{
		fprintf_s(log, "TRUNC_%s,%d,%.2f\n", BLK_TEMP_NAME[ii], -1, -1.0);
	}
	fclose(log);
}

size_t CF2fsSimulator::DumpFileIndex(NID index[], size_t buf_size, NID fid)
{
	CPageInfo* ipage = nullptr; // m_pages.allocate(true);
	NODE_INFO &inode = ReadNode(fid, ipage);
	memcpy_s(index, sizeof(NID) * buf_size, inode.inode.index, sizeof(NID) * INDEX_TABLE_SIZE);
	return inode.valid_data;
}

void CF2fsSimulator::DumpSegments(const std::wstring& fn, bool sanity_check)
{
}

void CF2fsSimulator::DumpSegmentBlocks(const std::wstring& fn)
{
	m_segments.DumpSegmentBlocks(fn);
}

#define FLUSH_FILE_MAP(phy)	{\
	if (start_phy != INVALID_BLK) {\
		SEG_T seg; BLK_T blk; BlockToSeg(seg,blk,start_phy);	\
		fprintf_s(out, "%S,%d,%d,%d,%X,%d,%d,%d,%d\n", inode.inode.m_fn.c_str(), fid, start_offset, (bb - start_offset), start_phy,seg,blk,host_write,media_write);}\
		host_write=0, media_write=0; start_phy = phy; pre_phy = phy; }

void CF2fsSimulator::DumpFileMap_merge(FILE* out, NID fid)
{
}

void CF2fsSimulator::DumpFileMap_no_merge(FILE* out, NID fid)
{
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
CNodeAddressTable::CNodeAddressTable(CF2fsSimulator* fs)
	: m_pages(&fs->m_pages), m_storage(&fs->m_storage)
{
}
NID CNodeAddressTable::Init(PHY_BLK root)
{
	memset(nat, 0xFF, sizeof(PHY_BLK) * NODE_NR);
	memset(node_catch, 0xFF, sizeof(PAGE_INDEX) * NODE_NR);
	build_free();
	return 0;
}

void CNodeAddressTable::CopyFrom(const CNodeAddressTable* src)
{
	memcpy_s(nat, sizeof(PHY_BLK) * NODE_NR, src->nat, sizeof(PHY_BLK) * NODE_NR);
	memcpy_s(node_catch, sizeof(PAGE_INDEX) * NODE_NR, src->node_catch, sizeof(PAGE_INDEX) * NODE_NR);
	next_scan = src->next_scan;
	free_nr = src->free_nr;
}

bool CNodeAddressTable::Load(void)
{
	UINT lba = NAT_START_BLK;
	for (NID nid = 0; nid < NODE_NR; )
	{
		CPageInfo* page = m_pages->allocate(true);
		m_storage->BlockRead(lba, page);
		BLOCK_DATA* data = m_pages->get_data(page);
		memcpy_s(nat + nid, sizeof(PHY_BLK) * NAT_ENTRY_PER_BLK, &data->nat, sizeof(PHY_BLK) * NAT_ENTRY_PER_BLK);
		nid += NAT_ENTRY_PER_BLK;
		lba++;
	}
	build_free();
	return true;
}

void CNodeAddressTable::Sync(void)
{
	UINT lba = NAT_START_BLK;
	for (NID nid = 0; nid < NODE_NR; )
	{
		CPageInfo* page = m_pages->allocate(true);
		BLOCK_DATA* data = m_pages->get_data(page);
		memcpy_s(&data->nat, sizeof(PHY_BLK) * NAT_ENTRY_PER_BLK, nat + nid, sizeof(PHY_BLK) * NAT_ENTRY_PER_BLK);
		m_storage->BlockWrite(lba, page);
		nid += NAT_ENTRY_PER_BLK;
		lba++;
	}
}

void CNodeAddressTable::Reset(void)
{
	memset(nat, 0xFF, sizeof(PHY_BLK) * NODE_NR);
	memset(node_catch, 0xFF, sizeof(PAGE_INDEX) * NODE_NR);
	next_scan = 0;
	free_nr = 0;
}

void CNodeAddressTable::build_free(void)
{
	for (next_scan = 0; next_scan < NODE_NR; next_scan++)
	{
		if (nat[next_scan] == INVALID_BLK) break;
	}
	if (next_scan >= NODE_NR) THROW_ERROR(ERR_APP, L"no valid node");
	for (int ii = 0; ii < NODE_NR; ++ii)
	{
		if (nat[ii] == INVALID_BLK) free_nr++;
	}
}

NID CNodeAddressTable::get_node(void)
{
	NID cur = next_scan;
	if (free_nr == 0) THROW_ERROR(ERR_APP, L"remained node=0");
	while (1)
	{
		if (nat[next_scan] == INVALID_BLK)
		{
			free_nr--;
//			LOG_DEBUG(L"allocate node: nid=%d, remain=%d", next_scan, free_nr);
			nat[next_scan] = NID_IN_USE;	// 避免分配以后，node没有写入磁盘之前，被再次分配，用NID_IN_USE标志已使用。
			// TODO 需要确认f2fs真实系统中如何实现。
			return next_scan++;
		}
		next_scan++;
		if (next_scan >= NODE_NR) 
			next_scan = 0;
		if (next_scan == cur)
		{
			THROW_ERROR(ERR_APP, L"node run out");
			LOG_ERROR(L"no valid node");
			return INVALID_BLK;
		}
	}
}

void CNodeAddressTable::put_node(NID node)
{
	free_nr++;
//	LOG_DEBUG(L"free node: nid=%d, remain=%d", node, free_nr);
	if (node >= NODE_NR) THROW_ERROR(ERR_USER, L"wrong node id:%d", node);
	nat[node] = INVALID_BLK;
}

PHY_BLK CNodeAddressTable::get_phy_blk(NID nid)
{
	if (nid >= NODE_NR) THROW_ERROR(ERR_APP, L"invalid node id %d", nid);
	return nat[nid];
}

void CNodeAddressTable::set_phy_blk(NID nid, PHY_BLK phy_blk)
{
	if (nid >= NODE_NR) THROW_ERROR(ERR_APP, L"invalid node id %d", nid);
	nat[nid] = phy_blk;
}

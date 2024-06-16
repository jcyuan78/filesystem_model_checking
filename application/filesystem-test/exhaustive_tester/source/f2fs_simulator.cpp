///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
#include "pch.h"
#include "../include/f2fs_simulator.h"
#include <boost/unordered_set.hpp>

LOCAL_LOGGER_ENABLE(L"simulator.f2fs", LOGGER_LEVEL_DEBUGINFO);

//#define MULTI_HEAD	1
static const char* BLK_TEMP_NAME[] = { "COLD_DATA", "COLD_NODE", "WARM_DATA", "WARM_NODE", "HOT__DATA", "HOT__NODE", "EMPTY" };

// type 1: 输出gc log, block count相关，2: 输出gc 性能分析，最大值VB，最小值VB，等
//#define GC_TRACE_TYPE 1
#define GC_TRACE_TYPE 2


CF2fsSimulator::~CF2fsSimulator(void)
{
	// 删除所有file
	for (FID fid = 1; fid < MAX_NODE_NUM; ++fid)
	{
		BLOCK_DATA& block = get_block(fid);
		if (block.m_type == BLOCK_DATA::BLOCK_INODE) FileRemove(&block.node);
	}
#ifdef ENABLE_FS_TRACE
	if (m_inode_trace) fclose(m_inode_trace);
#endif
}

void CF2fsSimulator::Clone(IFsSimulator*& dst)
{
	JCASSERT(dst == nullptr)
	CF2fsSimulator* fs = new CF2fsSimulator;
	fs->m_log_path = m_log_path;
	memcpy_s(&fs->m_health_info, sizeof(FsHealthInfo), &m_health_info, sizeof(FsHealthInfo));
	memcpy_s(&fs->m_pages, sizeof(CPageAllocator), &m_pages, sizeof(CPageAllocator));
	memcpy_s(&fs->m_block_buf, sizeof(BLOCK_BUF_TYPE), &m_block_buf, sizeof(BLOCK_BUF_TYPE));
	fs->m_multihead_cnt = m_multihead_cnt;
	fs->m_node_blks = m_node_blks;
	fs->m_segments.CopyFrom(m_segments, fs);
	dst = static_cast<IFsSimulator*>(fs);
}

void CF2fsSimulator::CopyFrom(const IFsSimulator* src)
{
	const CF2fsSimulator* _src = dynamic_cast<const CF2fsSimulator*>(src);
	if (_src == nullptr) THROW_ERROR(ERR_APP, L"cannot copy from non CF2fsSimulator, src=%p", src);
	m_log_path = _src->m_log_path;

	memcpy_s(&m_health_info, sizeof(FsHealthInfo), &_src->m_health_info, sizeof(FsHealthInfo));
	memcpy_s(&m_pages, sizeof(CPageAllocator), &_src->m_pages, sizeof(CPageAllocator));
	memcpy_s(&m_block_buf, sizeof(BLOCK_BUF_TYPE), &_src->m_block_buf, sizeof(BLOCK_BUF_TYPE));
	m_multihead_cnt = _src->m_multihead_cnt;
	m_node_blks = _src->m_node_blks;
	m_segments.CopyFrom(_src->m_segments, this);
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
	m_segments.InitSegmentManager(this, seg_nr, gc_th_lo, gc_th_hi);

	// 文件系统大小由编码固定，
	m_health_info.m_seg_nr = m_segments.m_seg_nr;
	m_health_info.m_blk_nr = m_health_info.m_seg_nr * BLOCK_PER_SEG;
//	m_health_info.m_logical_blk_nr = (UINT)(m_health_info.m_blk_nr * op);
	m_health_info.m_logical_blk_nr = m_health_info.m_blk_nr - ((gc_th_lo+10) * BLOCK_PER_SEG);

//	m_free_blks = m_health_info.m_logical_blk_nr;
	m_health_info.m_free_blk = m_health_info.m_logical_blk_nr;

	m_pages.Init(m_health_info.m_blk_nr);
	BLOCK_DATA * root_node = m_block_buf.Init();

	// 初始化root
	CPageAllocator::INDEX page_id = m_pages.get_page();
	CPageInfo* page = m_pages.page(page_id);
	InitInode(root_node, ROOT_FID, INVALID_BLK, page_id, page);
//	m_file_num++;
	m_health_info.m_dir_num = 1;
	m_health_info.m_file_num = 0;
	UpdateInode(root_node->node, "CREATE");
	// 初始化结束

	fs_trace("INIT", 0, 0, 0);

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

	// 增加 inode host write trace
	_wfopen_s(&m_inode_trace, (m_log_path + L"\\trace_inode.csv").c_str(), L"w+");
	if (m_inode_trace == nullptr) THROW_ERROR(ERR_APP, L"failed on creating log file: trace_inode.csv");
	fprintf_s(m_inode_trace, "op_id,fid,index,reason\n");
	m_segments.m_gc_trace = m_gc_trace;
#endif
	return true;
}

#ifdef ENABLE_FS_TRACE
void CF2fsSimulator::fs_trace(const char* op, FID fid, DWORD start_blk, DWORD blk_nr)
{
	// write count, operation, start blk, blk nr, used blks, free blks
	fprintf_s(m_log_write_trace, "%lld,%s,%d,%d,%d,%d,%d,%d,%d\n", m_write_count++, op, fid, start_blk, blk_nr, m_health_info.m_logical_saturation, m_node_blks, m_free_blks, m_health_info.m_free_seg);
	fflush(m_log_write_trace);
}
#endif

inline int DepthFromBlkNr(UINT blk_nr)
{
	int max_depth = 0;
	while (blk_nr != 0) max_depth++, blk_nr >>= 1;		// 通过block数量计算最大层次
	return max_depth;
}

FID CF2fsSimulator::FindFile(NODE_INFO& parent, const char* fn)
{
	// 抽象的 dir file 结构：以2^level的数量，在每层中存放dentry block。每个block属于一个hash slot。
	//std::vector<CPageInfo*> dentry_pages;
	CPageAllocator::INDEX* page_ids = new CPageAllocator::INDEX[parent.inode.blk_num];
	FileReadInternal(page_ids, parent, 0, parent.inode.blk_num);
	// 计算文件名hash
	WORD hash = FileNameHash(fn);
	FID fid = INVALID_BLK;

	// 逐层查找文件 （f2fs :: __f2fs_find_entry() )
	int max_depth = DepthFromBlkNr(parent.inode.blk_num);

	int blk_num=1, blk_index=0/*, blk_end=blk_index+blk_num*/;		// 当前层次下，block数量，第一个block，和最后一个block
	for (int level = 0; level < max_depth; level++)
	{	// find_in_level
		WORD hh = hash % blk_num;
		CPageAllocator::INDEX page_id = page_ids[blk_index + hh];
		if (page_id != INVALID_BLK) 
		{
			CPageInfo* page = m_pages.page(page_id);
			BLOCK_DATA& block = get_block(page->data_index);
			DENTRY_BLOCK& entries = block.dentry;

			// 沿着bitmap查找
			int index = 0;
			for (int index = 0; index < NR_DENTRY_IN_BLOCK; ++index)
			{
				if (entries.dentries[index].ino == INVALID_BLK || entries.dentries[index].hash != hash) continue;
				char* ff = entries.filenames[index];
				if (strcmp(ff, fn) == 0)
				{
					fid = entries.dentries[index].ino;
					break;
				}
			}
			if (fid != INVALID_BLK) break;
		}
		blk_index += blk_num;
		blk_num *= 2;
	}
	delete[] page_ids;
	return fid;
}

BLOCK_DATA* CF2fsSimulator::AllocateDentryBlock(CPageAllocator::INDEX& page_id)
{
	auto blk_id = m_block_buf.get_block();
	BLOCK_DATA& block = get_block(blk_id);
	InitDentry(&block);

	page_id = m_pages.get_page();
	CPageInfo* page = m_pages.page(page_id);
	page->data_index = blk_id;

	return &block;
}

void CF2fsSimulator::AddChild(NODE_INFO* parent, const char* fn, FID fid)
{
//	std::vector<CPageInfo*> dentry_pages;
	CPageAllocator::INDEX* page_ids = new CPageAllocator::INDEX[parent->inode.blk_num];
	FileReadInternal(page_ids, *parent, 0, parent->inode.blk_num);
	// 计算文件名hash
	WORD hash = FileNameHash(fn);
//	FID fid = INVALID_BLK;
	WORD fn_len = (WORD)(strlen(fn));
	int slot_num = ROUND_UP_DIV(fn_len, FN_SLOT_LEN);

	int max_depth = DepthFromBlkNr(parent->inode.blk_num);
	int blk_num=1, blk_index=0/*, blk_end=blk_index+blk_num*/;		// 当前层次下，block数量，第一个block，和最后一个block
	int level = 0;
	int index_start = 0;

	BLOCK_DATA* dentry_block = nullptr;
	CPageAllocator::INDEX dpage_id = INVALID_BLK;
	int blk_offset = 0;

	for (; level < max_depth; level++)
	{	// 按层次查找空位，
		WORD hh = hash % blk_num;
		blk_offset = blk_index + hh;
		dpage_id = page_ids[blk_offset];
		if (dpage_id != INVALID_BLK)
		{
			CPageInfo* page = m_pages.page(dpage_id);
			BLOCK_DATA& block = get_block(page->data_index);
			DENTRY_BLOCK& entries = block.dentry;

			int index = 0;
			DWORD mask = 1;
			while (1)
			{
				while (entries.bitmap & mask) { mask <<= 1; index_start++; }
				if (index_start >= NR_DENTRY_IN_BLOCK) break;	// 没有空位 goto next 
				index = index_start;
				while (((entries.bitmap & mask) == 0) && index < NR_DENTRY_IN_BLOCK) { mask <<= 1; index++; }
				if ((index - index_start) >= slot_num)
				{	// 找到空位
					dentry_block = &block;
					break;
				}
			}
			if (index_start < NR_DENTRY_IN_BLOCK)		break;	//找到空位
		}
		else
		{	// 这个slot时空的，创建slot 申请一个空的block
			//auto blk_id = m_block_buf.get_block();
			//BLOCK_DATA& block = get_block(blk_id);
			//InitDentry(&block);

			//dpage_id = m_pages.get_page();
			//CPageInfo* page = m_pages.page(dpage_id);
			//page->data_index = blk_id;

			blk_offset = blk_index + hh;
			dentry_block = AllocateDentryBlock(dpage_id);
			index_start = 0;
		}
		blk_index += blk_num, blk_num *= 2;
	}
	delete[] page_ids;

	if (index_start >= NR_DENTRY_IN_BLOCK || dentry_block == nullptr)
	{	// 没有找到空位， 添加新的层次
		max_depth++;
		if (max_depth >= MAX_DENTRY_LEVEL) {
			THROW_ERROR(ERR_APP, L"dentry is full, parent fid=%d, fn len=%zd", parent->m_nid, fn_len);
		}
		blk_num = (1 << (max_depth - 1));
		blk_index = blk_num - 1;
		WORD hh = hash % blk_num;

		// 申请一个空的block
		//auto blk_id = m_block_buf.get_block();
		//BLOCK_DATA& block = get_block(blk_id);
		//InitDentry(&block);
		//dpage_id = m_pages.get_page();
		//CPageInfo* page = m_pages.page(dpage_id);
		//page->data_index = blk_id;

		blk_offset = blk_index + hh;
		dentry_block = AllocateDentryBlock(dpage_id);
		index_start = 0;
	}

	// 向dentry写入文件
	if (index_start >= NR_DENTRY_IN_BLOCK || dentry_block == nullptr) {
		THROW_ERROR(ERR_APP, L"failed on alloacting dentry block");
	}
	DENTRY_BLOCK& entries = dentry_block->dentry;
	entries.dentries[index_start].ino = fid;
	entries.dentries[index_start].hash = hash;
	entries.dentries[index_start].name_len = fn_len;
	strcpy_s(entries.filenames[index_start], FN_SLOT_LEN*slot_num, fn);
	DWORD mask = (1 << index_start);
	for (int ii = 0; ii < slot_num; ++ii) {
		entries.bitmap |= mask;
		mask <<= 1;
	}

	// 向文件填充 blk_num block
	FileWriteInternal(*parent, blk_offset, 1, &dpage_id);
}

void CF2fsSimulator::RemoveChild(NODE_INFO* parent, WORD hash, FID fid)
{
	CPageAllocator::INDEX* page_ids = new CPageAllocator::INDEX[parent->inode.blk_num];
	FileReadInternal(page_ids, *parent, 0, parent->inode.blk_num);
	for (int ii = 0; ii < parent->inode.blk_num; ++ii)
	{
		CPageAllocator::INDEX dpage_id = page_ids[ii];
		if (dpage_id != INVALID_BLK)
		{
			CPageInfo* page = m_pages.page(dpage_id);
			BLOCK_DATA& block = get_block(page->data_index);
			DENTRY_BLOCK& entries = block.dentry;
			for (int jj = 0; jj < NR_DENTRY_IN_BLOCK; ++jj)
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
		}
	}
	delete[] page_ids;
}


FID CF2fsSimulator::InternalCreatreFile(const std::wstring& fn, bool is_dir)
{
	// 根据路径找到父节点
	char full_path[MAX_PATH_SIZE+1];
	jcvos::UnicodeToUtf8(full_path, MAX_PATH_SIZE, fn.c_str(), fn.size());
	char * ptr = strrchr(full_path, '\\');
	if (ptr == nullptr || *ptr == 0) return INVALID_BLK;
	*ptr = 0;
	NODE_INFO * parent = FileOpenInternal(full_path);
	if (parent == nullptr)
	{
		LOG_ERROR(L"[err] parent path %S is not exist", full_path);
		return INVALID_BLK;
	}

	// 检查 fn 不在 parent目录中
	char* name = ptr + 1;
	if (FindFile(*parent, name) != INVALID_BLK)
	{
		LOG_ERROR(L"[err] file %s is already exist", fn.c_str());
		return INVALID_BLK;
	}

	// 创建inode，allocate inode
	CPageAllocator::INDEX _pp = m_pages.get_page();
	CPageInfo* page = m_pages.page(_pp);
	auto nid = m_block_buf.get_block();
	BLOCK_DATA& inode_block = get_block(nid);
	InitInode(&inode_block, nid, parent->m_nid, _pp, page);

	// 添加到父节点dentry中
	AddChild(parent, name, nid);
//	m_file_num++;
//	m_health_info.m_file_num++;
	fs_trace("CREATE", nid, 0, 0);
	// 更新inode
	UpdateInode(inode_block.node, "CREATE");
	InterlockedIncrement(&(inode_block.node.inode.ref_count));
	return nid;
}

FID CF2fsSimulator::FileCreate(const std::wstring& fn)
{
	FID fid = InternalCreatreFile(fn, false);
	m_health_info.m_file_num++;
	return fid;
}

FID CF2fsSimulator::DirCreate(const std::wstring& fn)
{
	FID fid = InternalCreatreFile(fn, true);
	m_health_info.m_dir_num++;
	return fid;
}

FID CF2fsSimulator::FileOpen(const std::wstring& fn, bool delete_on_close)
{
	char full_path[MAX_PATH_SIZE + 1];
	jcvos::UnicodeToUtf8(full_path, MAX_PATH_SIZE, fn.c_str(), fn.size());

	NODE_INFO* node = FileOpenInternal(full_path);
	if (node == nullptr) return INVALID_BLK;
	InterlockedIncrement(&(node->inode.ref_count));
	return node->m_nid;
}

NODE_INFO * CF2fsSimulator::FileOpenInternal(const char* fn)
{
	NODE_INFO& root = get_inode(ROOT_FID);
	NODE_INFO* cur = &root;

	char* _fn = const_cast<char*>(fn);
	char* next = nullptr;
	char* dir = strtok_s(_fn, "\\", &next);			// dir应该首先指向第一个“\”

	while (1)
	{
		if (dir == nullptr || *dir == 0) break;
		FID fid = FindFile(*cur, dir);
		if (fid == INVALID_BLK)
		{
			LOG_ERROR(L"[err] cannot find %S in %S", dir, fn);
			return nullptr;
		}
		NODE_INFO& node = get_inode(fid);
		cur = &node;
		dir = strtok_s(nullptr, "\\", &next);
	}
	return cur;
}

void CF2fsSimulator::FileClose(FID fid)
{
	NODE_INFO& inode= get_inode(fid);
	InterlockedDecrement(&inode.inode.ref_count);
	if (inode.inode.delete_on_close) FileRemove(&inode);
}

void CF2fsSimulator::FileWrite(FID fid, FSIZE offset, FSIZE len)
{
//	THROW_ERROR(ERR_APP, L"simulation");		// test

	NODE_INFO& inode = get_inode(fid);

	UINT end_pos = offset + len;
	if (end_pos > inode.inode.file_size) inode.inode.file_size = end_pos;

	// 计算起始block和结束block，end_block是最后要写入的下一个。blk_nr=end_block - start_block
	DWORD start_blk, end_blk;
	OffsetToBlock(start_blk, end_blk, offset, len);
//	LOG_TRACK(L"fs", L",WriteFile,fid=%d,offset=%d,len=%d", fid, start_blk, (end_blk - start_blk));

	if (end_blk > MaxFileSize()) 
		THROW_ERROR(ERR_APP, L"file size is too large, blks=%d, max_size=%d", end_blk, MaxFileSize());

	FileWriteInternal(inode, start_blk, end_blk, nullptr);
}

void CF2fsSimulator::FileWriteInternal(NODE_INFO& inode, FSIZE start_blk, FSIZE end_blk, CPageAllocator::INDEX* pages)
{
	CIndexPath ipath(&inode);

	//CNodeInfoBase* di_node = nullptr;
	NODE_INFO* di_node = nullptr;
	CPageInfo* di_page = nullptr;

	//fs_trace("WRITE", fid, start_blk, end_blk - start_blk);

	// 变量说明
	//								page index,	page 指针,	node index, node 指针,	block,
	// 文件的inode:					na,			na,			fid,		inode,	
	// 当前处理的direct index node:	??,			di_page,	ipath中,		di_node,
	// 当前数据块					_pp,		dpage,		na,			na,	
	int page_index = 0;

	for (; start_blk < end_blk; start_blk++, page_index++)
	{
		// 查找PHY_BLK定位到
		int index;
		if (ipath.level < 0)
		{
			OffsetToIndex(ipath, start_blk, true);
			// 获取index的位置，需要更新index
			di_node = ipath.node[ipath.level];
			di_page = ipath.page[ipath.level];
		}
		index = ipath.offset[ipath.level];

		// 确定数据温度
		CPageAllocator::INDEX _pp = di_node->index.index_table[index];
		CPageInfo* dpage = m_pages.page(_pp);
		if (!dpage)
		{	// 数据不存在
			if (pages && pages[page_index] != INVALID_BLK) _pp = pages[page_index];
			else _pp = m_pages.get_page();
			dpage = m_pages.page(_pp);
			dpage->type = CPageInfo::PAGE_DATA;
			dpage->inode = inode.m_nid;
			dpage->offset = start_blk;
			di_node->valid_data++;
			di_node->index.index_table[index] = _pp;

			// 这个逻辑块没有被写过，增加逻辑饱和度
			InterlockedIncrement(&m_health_info.m_logical_saturation);
			if (m_health_info.m_logical_saturation >= (m_health_info.m_logical_blk_nr))
			{
				//THROW_ERROR(ERR_APP, L"logical saturation overflow, logical_saturation=%d, logical_block=%d", m_health_info.m_logical_saturation, m_health_info.m_logical_blk_nr);
				LOG_WARNING(L"[warning] logical saturation overflow, logical_saturation=%d, logical_block=%d", m_health_info.m_logical_saturation, m_health_info.m_logical_blk_nr);
			}
			inode.inode.blk_num++;
		}
		// 注意，此处并没有实现 NAT，存在wandering tree问题。

		di_page->dirty = true;
		dpage->dirty = true;

		InterlockedIncrement64(&m_health_info.m_total_host_write);
		dpage->host_write++;

		m_segments.WriteBlockToSeg(_pp);
		// 将ipath移动到下一个offset 
		m_segments.CheckGarbageCollection(this);
		NextOffset(ipath);
	}
	// 更新ipath
	UpdateInode(inode, "WRITE");
} 

void CF2fsSimulator::FileReadInternal(CPageAllocator::INDEX * pages, NODE_INFO &inode, FSIZE start_blk, FSIZE end_blk)
{
	// sanity check
	FID fid = inode.m_nid;
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
		// 此处暂不支持空洞
		if (!di_node) THROW_ERROR(ERR_APP, L"invalid index block, fid=%d, lblk=%d", inode.m_nid, start_blk);
		// 获取index的位置，需要更新index
		int index = ipath.offset[ipath.level];
		CPageAllocator::INDEX _pp = di_node->index.index_table[index];
		pages[page_index] = _pp;

		// 将ipath移动到下一个offset 
		NextOffset(ipath);
	}
}

void CF2fsSimulator::FileRead(std::vector<CPageInfo*>& blks, FID fid, FSIZE offset, FSIZE secs)
{
	// sanity check
	NODE_INFO& inode = get_inode(fid);
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

	CPageAllocator::INDEX* pages = new CPageAllocator::INDEX[page_nr];
	FileReadInternal(pages, inode, start_blk, end_blk);

	for (DWORD ii = 0; ii < page_nr; ++ii)
	{
		CPageAllocator::INDEX _pp = pages[ii];
		if (_pp == INVALID_BLK)		// 允许出现空洞
		{
			blks.push_back(nullptr);
		}
		else
		{
			CPageInfo* dpage = m_pages.page(_pp);
			if (!dpage) THROW_ERROR(ERR_APP, L"invalid data block, fid=%d, lblk=%d", inode.m_nid, start_blk);
			if (dpage->inode == INVALID_BLK || dpage->offset != start_blk+ii)
			{
				THROW_ERROR(ERR_APP, L"L2P not match, fid=%d, lblk=%d, phy_blk=%X, fid_p2f=%d, lblk_p2f=%d",
					fid, start_blk, dpage->phy_blk, dpage->inode, dpage->offset);
			}
			blks.push_back(dpage);
		}
	}
	delete[] pages;

#if 0
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
		// 此处暂不支持空洞
		if (!di_node) THROW_ERROR(ERR_APP, L"invalid index block, fid=%d, lblk=%d", fid, start_blk);
		// 获取index的位置，需要更新index
		int index = ipath.offset[ipath.level];
		CPageAllocator::INDEX _pp = di_node->index.index_table[index];
		CPageInfo* dpage = m_pages.page(_pp);

		if (!dpage) THROW_ERROR(ERR_APP, L"invalid data block, fid=%d, lblk=%d", fid, start_blk);
		if (dpage->inode == INVALID_BLK || dpage->offset != start_blk)
		{
			THROW_ERROR(ERR_APP, L"L2P not match, fid=%d, lblk=%d, phy_blk=%X, fid_p2f=%d, lblk_p2f=%d",
				fid, start_blk, dpage->phy_blk, dpage->inode, dpage->offset);
		}
		blks.push_back(dpage);
		// 将ipath移动到下一个offset 
		NextOffset(ipath);
	}
#endif
}



void CF2fsSimulator::FileTruncate(FID fid, FSIZE offset, FSIZE secs)
{
	// 文件的所有block都无效，然后保存inode
	NODE_INFO& inode = get_inode(fid);

	FSIZE end_secs = offset + secs;
	if (end_secs > inode.inode.file_size)
	{
		LOG_WARNING(L"Oversize on truncating file, fid=%d, secs=%d, file_secs=%d", fid, end_secs, inode.inode.file_size);
		end_secs = inode.inode.file_size;
		secs = end_secs - offset;
	}


	DWORD start_blk, end_blk;
	OffsetToBlock(start_blk, end_blk, offset, secs);
	fs_trace("TRUCATE", fid, start_blk, end_blk - start_blk);

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
		// 获取index的位置，需要更新index
		int index = ipath.offset[ipath.level];
		if (di_node)
		{
			CPageAllocator::INDEX _pp = di_node->index.index_table[index];
			if (_pp != INVALID_BLK)
			{
				CPageInfo* dpage = m_pages.page(_pp);
				if (dpage->inode == INVALID_BLK || dpage->offset != start_blk)
				{
					THROW_ERROR(ERR_APP, L"L2P not match, fid=%d, lblk=%d, phy_blk=%X, fid_p2f=%d, lblk_p2f=%d",
						fid, start_blk, dpage->phy_blk, dpage->inode, dpage->offset);
				}
				//if (dpage)
				//{
					InterlockedDecrement(&m_health_info.m_logical_saturation);
					InvalidBlock("TRANCATE", dpage->phy_blk);
					di_node->index.index_table[index] = INVALID_BLK;
					di_page->dirty = true;
					di_node->valid_data--;
					if (dpage->data_index != INVALID_BLK)
					{
						m_block_buf.put_block(dpage->data_index);
					}
					m_pages.put_page(_pp);
					inode.inode.blk_num--;
//				}
			}
		}
		// 将ipath移动到下一个offset 
		NextOffset(ipath);
	}
	UpdateInode(inode, "TRUNCATE");
	if (end_secs == inode.inode.file_size)
	{
		inode.inode.file_size = offset;
	}
}

void CF2fsSimulator::FileDelete(const std::wstring& fn)
{
	char _fn[MAX_PATH_SIZE + 1];
	jcvos::UnicodeToUtf8(_fn, MAX_PATH_SIZE, fn.c_str(), fn.size());
	NODE_INFO* inode = FileOpenInternal(_fn);
	FID fid = inode->m_nid;
	if (inode == nullptr) THROW_ERROR(ERR_APP, L"[err] file %s is not existing", fn.c_str());

	// TODO: 对于目录，删除其所有子目录
	FileRemove(inode);
	//TODO：注意此处的顺序，先删除inode，还是先删除dentry
	NODE_INFO& parent = get_inode(inode->m_parent_node);
	RemoveChild(&parent,0, fid);
}

void CF2fsSimulator::FileRemove(NODE_INFO * inode)
{
//	fs_trace("DELETE", fid, 0, 0);
	// 删除文件，回收inode
//	NODE_INFO& inode = get_inode(fid);

//	if (inode->inode.ref_count > 0) THROW_ERROR(ERR_APP, L"file is still referenced, fid=%d", inode->m_nid);
	if (inode->inode.ref_count > 0) {
		LOG_ERROR(L"[err] file reference code is not zero, fid=%d, count=%d", inode->m_nid, inode->inode.ref_count);
	}
	FileTruncate(inode->m_nid, 0, inode->inode.file_size);

	// 删除map由FileDelete负责
	CPageInfo* ipage = m_pages.page(inode->page_id);

	InvalidBlock("DELETE_NODE", ipage->phy_blk);
	m_node_blks--;

	// page 在free_inode中删除, 统计被回收的inode的WAF
	BLK_TEMP temp = ipage->ttemp;
	JCASSERT(temp < BT_TEMP_NR);
	m_block_buf.put_block(inode->m_nid);
	m_pages.put_page(inode->page_id);
//	m_file_num--;
	m_health_info.m_file_num--;
}

void CF2fsSimulator::FileFlush(FID fid)
{
	fs_trace("FLUSH", fid, 0, 0);
	return;
#if 0
	// 文件的所有block都无效，然后保存inode
	CInodeInfo* inode = (CInodeInfo*)(m_inodes.get_node(fid));
	JCASSERT(inode);
	CIndexPath ipath(inode);
	//	InitIndexPath(ipath, inode);
	CNodeInfoBase* direct_node = nullptr;
	for (LBLK_T bb = 0; bb < inode.inode.m_blks;/* ++bb*/)
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
				m_segments.CheckGarbageCollection(this);
				direct_node->data_page->dirty = true;
			}
		}
		NextOffset(ipath);
		bb++;
	}
	// 更新ipath
	UpdateInode(inode, "FLUSH");
#endif
}

void CF2fsSimulator::UpdateInode(NODE_INFO& inode, const char* caller)
{
	// 变量说明
	//								page index,	page 指针,	node index, node 指针,	block,
	// 文件的inode:					na,			i_page,		fid,		inode,	
	// 当前处理的direct index node:	di_page_id,	di_page,	di_node_id,	di_node,
	// 当前数据块					_pp,		dpage,		na,			na,	

	// 这个更新是否一定是host发起的？
	// 更新ipath
	CPageInfo* i_page = m_pages.page(inode.page_id);
	DWORD start_blk, last_blk;
	OffsetToBlock(start_blk, last_blk, 0, inode.inode.file_size);
	LBLK_T bb = 0;
	for (size_t ii = 0; (ii < MAX_TABLE_SIZE) && (bb < last_blk); ++ii, bb += INDEX_SIZE)
	{
		CPageAllocator::INDEX di_page_id = inode.inode.index_table[ii];
		CPageInfo* di_page = m_pages.page(di_page_id);
		if (!di_page) continue;

		NID di_node_id = di_page->data_index;
		if (di_node_id == INVALID_BLK)
			THROW_ERROR(ERR_APP, L"index block in page is null, fid=%d, index=%d", inode.m_nid, ii);

		NODE_INFO& di_node = get_inode(di_page->data_index);
		if (di_node.page_id != di_page_id)
			THROW_ERROR(ERR_APP, L"data unmatch, fid=%d, index=%d, page=%p, page_in_blk=%p",
				inode.m_nid, ii, di_page, di_node.page_id);

		if (di_node.valid_data == 0)
		{
			InvalidBlock("", di_page->phy_blk);
			m_node_blks--;

			//统计被回收的inode的WAF，invalid block不用计算温度
			//JCASSERT(di_page_id == di_node.page_id);
			//BLK_TEMP temp = di_page->ttemp;
			//JCASSERT(temp < BT_TEMP_NR);
			//m_truncated_host_write[temp] += di_page->host_write;
			//m_truncated_media_write[temp] += di_page->media_write;

			m_block_buf.put_block(di_node_id);
			m_pages.put_page(di_page_id);

			inode.inode.index_table[ii] = INVALID_BLK;
			i_page->dirty = true;
			continue;
		}
		if (di_page->dirty == false) continue;
		di_page->host_write++;
		if (di_page->phy_blk == INVALID_BLK) m_node_blks++;
		m_segments.WriteBlockToSeg(di_page_id);
		m_segments.CheckGarbageCollection(this);

#ifdef ENABLE_FS_TRACE
		//<TRACE>记录inode的更新情况。
		// opid, fid, inode或者index id, 原因，数据更新数量。
		fprintf_s(m_inode_trace, "%lld,%d,%lld,%s\n", m_write_count, inode.m_nid, ii, caller);
#endif
		i_page->dirty = true;
	}

	PHY_BLK old_phy = i_page->phy_blk;
	if (i_page->dirty)
	{
		i_page->host_write++;
		if (i_page->phy_blk == INVALID_BLK) m_node_blks++;
		m_segments.WriteBlockToSeg(inode.page_id);
		m_segments.CheckGarbageCollection(this);

#ifdef ENABLE_FS_TRACE
		//<TRACE>记录inode的更新情况。
		// opid, fid, inode或者index id, 原因，数据更新数量。
		fprintf_s(m_inode_trace, "%lld,%d,0,UPDATE\n", m_write_count, inode.m_nid);
#endif
	}
	LOG_TRACK(L"inode", L",UPDATE,fid=%d,new_phy=%X,old_phy=%X", inode.m_nid, i_page->phy_blk, old_phy);
}



FSIZE CF2fsSimulator::GetFileSize(FID fid)
{
//	CInodeInfo* inode = (CInodeInfo*)(m_inodes.get_node(fid));
	NODE_INFO& inode = get_inode(fid);
	return (inode.inode.file_size);
}

void CF2fsSimulator::GetFsInfo(FS_INFO& space_info)
{
	space_info.total_seg = m_segments.get_seg_nr();
	space_info.free_seg = m_segments.get_free_nr();
	space_info.total_blks = m_health_info.m_blk_nr;
	space_info.used_blks = m_health_info.m_logical_saturation;
//	space_info.free_blks = m_health_info.m_blk_nr - m_health_info.m_physical_saturation;
	space_info.free_blks = m_health_info.m_free_blk;
	// 可能出现负数
	if (space_info.free_blks > m_health_info.m_logical_blk_nr) space_info.free_blks = 0;
	space_info.physical_blks = m_health_info.m_physical_saturation;
	space_info.max_file_nr = MAX_NODE_NUM;

//	space_info.created_files = m_file_num;
	space_info.dir_nr = m_health_info.m_dir_num;
	space_info.file_nr = m_health_info.m_file_num;

	space_info.total_host_write = m_health_info.m_total_host_write;
	space_info.total_media_write = m_health_info.m_total_media_write;
}

BLK_TEMP CF2fsSimulator::GetBlockTemp(CPageInfo* page)
{
	if (page->type == CPageInfo::PAGE_NODE)
	{
		BLOCK_DATA& block = get_block(page->data_index);
//		NODE_INFO& node = get_inode(page->data_index);
		if (block.m_type == BLOCK_DATA::BLOCK_INODE) return BT_HOT__NODE;
		else if (block.m_type == BLOCK_DATA::BLOCK_INDEX) return BT_COLD_NODE;
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
		const SEG_INFO& seg = m_segments.get_segment(ss);
		//		BLK_TEMP seg_temp;	// 用于计算segment temperature
		for (BLK_T bb = 0; bb < BLOCK_PER_SEG; ++bb)
		{
			CPageInfo* page = m_pages.page(seg.blk_map[bb]);
			if (page == nullptr)
			{
				blk_count[BT_TEMP_NR]++;
				continue;
			}
			BLK_TEMP temp = page->ttemp;
			JCASSERT(temp < BT_TEMP_NR);
			blk_count[temp]++;
		}
		// 计算segment的类型
		BLK_TEMP seg_temp = seg.seg_temp;
		if (seg_temp >= BT_TEMP_NR) seg_temp = BT_TEMP_NR;
		fprintf_s(log, "%d,%s,%d,512,%d,%d,%d,%d,%d,%d,%d\n", ss, BLK_TEMP_NAME[seg_temp], seg.erase_count,
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



#define FLUSH_FILE_MAP(phy)	{\
	if (start_phy != INVALID_BLK) {\
		SEG_T seg; BLK_T blk; BlockToSeg(seg,blk,start_phy);	\
		fprintf_s(out, "%S,%d,%d,%d,%X,%d,%d,%d,%d\n", inode.inode.m_fn.c_str(), fid, start_offset, (bb - start_offset), start_phy,seg,blk,host_write,media_write);}\
		host_write=0, media_write=0; start_phy = phy; pre_phy = phy; }

void CF2fsSimulator::DumpFileMap_merge(FILE* out, FID fid)
{
	//	CInodeInfo* inode = dynamic_cast<CInodeInfo*>(m_inodes.get_node(fid));
	//CInodeInfo* inode = (CInodeInfo*)(m_inodes.get_node(fid));
	//JCASSERT(inode);

#if 0		// TODO: 需要修复FLUSH_FILE_MAP
	NODE_INFO& inode = get_inode(fid);

	CIndexPath ipath(&inode);
//	CNodeInfoBase* direct_node = nullptr;
	NODE_INFO* di_node = nullptr;
	CPageInfo* di_page = nullptr;

	PHY_BLK start_phy = INVALID_BLK, pre_phy = 0;
	LBLK_T start_offset = INVALID_BLK, pre_offset = INVALID_BLK;
	LBLK_T bb = 0;
	UINT host_write = 0, media_write = 0;

	LBLK_T start_blk, end_blk;
	OffsetToBlock(start_blk, end_blk, 0, inode.inode.file_size);

	for (bb = 0; bb < end_blk; /*++bb*/)
	{
		if (ipath.level < 0)
		{
			OffsetToIndex(ipath, bb, false);
			di_node = ipath.node[ipath.level];
			di_page = ipath.page[ipath.level];

		}
		int index = ipath.offset[ipath.level];
		if (!di_node)
		{	// 空的index block
			FLUSH_FILE_MAP(INVALID_BLK);
			bb += INDEX_SIZE;
			ipath.level = -1;
			continue;
		}

		CPageAllocator::INDEX _pp = di_node->index.index_table[index];

		if (_pp != INVALID_BLK)
		{
			CPageInfo* dpage = m_pages.page(_pp);
//			CPageInfo* page = di_node->data[index];
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
#endif
}

void CF2fsSimulator::DumpFileMap_no_merge(FILE* out, FID fid)
{
	//	CInodeInfo* inode = dynamic_cast<CInodeInfo*>(m_inodes.get_node(fid));
//	CInodeInfo* inode = (CInodeInfo*)(m_inodes.get_node(fid));
//	JCASSERT(inode);
	NODE_INFO& inode = get_inode(fid);

	CIndexPath ipath(&inode);
//	CNodeInfoBase* direct_node = nullptr;
	NODE_INFO* di_node = nullptr;
	CPageInfo* di_page = nullptr;

	PHY_BLK start_phy = INVALID_BLK, pre_phy = 0;
	LBLK_T start_offset = INVALID_BLK, pre_offset = INVALID_BLK;
	LBLK_T bb = 0;
	LBLK_T start_blk, end_blk;
	OffsetToBlock(start_blk, end_blk, 0, inode.inode.file_size);


	for (bb = 0; bb < end_blk; /*++bb*/)
	{
		if (ipath.level < 0)
		{
			OffsetToIndex(ipath, bb, false);
			di_node = ipath.node[ipath.level];
		}
		int index = ipath.offset[ipath.level];
		if (!di_node)
		{
			bb += INDEX_SIZE;
			ipath.level = -1;
			continue;
		}

		CPageAllocator::INDEX _pp = di_node->index.index_table[index];
		if (_pp != INVALID_BLK)
		{	// output
			//CPageInfo* page = direct_node->data[index];
			CPageInfo* dpage = m_pages.page(_pp);
			PHY_BLK phy_blk = dpage->phy_blk;

			if (phy_blk != INVALID_BLK)
			{
				SEG_T seg; BLK_T seg_blk;
				BlockToSeg(seg, seg_blk, phy_blk);
				//CPageInfo* page = m_segments.get_block(phy_blk);

				fprintf_s(out, "%S,%d,%d,1,%X,%d,%d,%d,%d\n",
					L"(FN)", fid, bb, phy_blk, seg, seg_blk, dpage->host_write, dpage->media_write);

				// sanity check
				if (dpage->inode != fid || dpage->offset != bb)
				{
					THROW_ERROR(ERR_APP, L"L2P not match, fid=%d, lblk=%d, phy_blk=%X, fid_p2f=%d, lblk_p2f=%d",
						fid, bb, phy_blk, dpage->inode, dpage->offset);
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
	BlockToSeg(_seg, _blk, phy_blk);
#ifdef ENABLE_FS_TRACE
	fprintf(m_log_invalid_trace, "%lld,%s,%d,%d,%d\n", m_write_count, reason ? reason : "", phy_blk, _seg, _blk);
#endif
	return free_seg;
}

void CF2fsSimulator::OffsetToIndex(CIndexPath& ipath, LBLK_T offset, bool alloc)
{
	// 从inode中把已经有的index block填入node[]中
	ipath.level = 1;
	ipath.offset[0] = offset / INDEX_SIZE;

	NODE_INFO* node = ipath.node[0];
	CPageAllocator::INDEX _pid = node->inode.index_table[ipath.offset[0]];

	if (_pid == INVALID_BLK)
	{	// direct index node为空，申请新的 node
		if (alloc)
		{
			CPageAllocator::INDEX _pp = m_pages.get_page();
			CPageInfo* dpage = m_pages.page(_pp);

			auto nid = m_block_buf.get_block();
//			NODE_INFO& direct_index_node = get_inode(nid);
			BLOCK_DATA& block = get_block(nid);
			InitIndexNode(&block, nid, node->m_nid, _pp, dpage);
			NODE_INFO& direct_index_node = block.node;
			ipath.node[1] = &direct_index_node;
			dpage->offset = ipath.offset[0];
			dpage->dirty = true;

			ipath.page[1] = dpage;
			node->inode.index_table[ipath.offset[0]] = _pp;
		}
	}
	else
	{
		CPageInfo* dpage = m_pages.page(_pid);
		ipath.page[1] = dpage;
		ipath.node[1] = &(get_inode(dpage->data_index));
	}
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

void CF2fsSimulator::DumpAllFileMap(const std::wstring& fn)
{
	FILE* log = nullptr;
	_wfopen_s(&log, fn.c_str(), L"w+");
	if (log == nullptr) THROW_ERROR(ERR_APP, L"failed on opening file %s", fn.c_str());
	fprintf_s(log, "fn,fid,start_offset,lblk_nr,phy_blk,seg_id,blk_id,host_write,media_write\n");

	//for (FID ii = 0; ii < m_inodes.get_node_nr(); ++ii)
	//{
	//	CNodeInfoBase* inode = m_inodes.get_node(ii);
	//	if (!inode || inode.inode.m_type != inode_info::NODE_INODE) continue;
	//	DumpFileMap(log, ii);
	//}
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

	memset(host_write, 0, sizeof(UINT64) * (BT_TEMP_NR + 1));
	memset(media_write, 0, sizeof(UINT64) * (BT_TEMP_NR + 1));
	memset(blk_count, 0, sizeof(UINT64) * (BT_TEMP_NR + 1));


	for (SEG_T ss = 0; ss < m_segments.get_seg_nr(); ++ss)
	{
		const SEG_INFO& seg = m_segments.get_segment(ss);
		for (BLK_T bb = 0; bb < BLOCK_PER_SEG; ++bb)
		{
			CPageAllocator::INDEX _pp = seg.blk_map[bb];
			CPageInfo* page = m_pages.page(_pp);
			if (page == nullptr) { continue; }

			BLK_TEMP temp = page->ttemp;
			JCASSERT(temp < BT_TEMP_NR);
			host_write[temp] += page->host_write;
			media_write[temp] += page->media_write;
			blk_count[temp]++;
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
	//	fprintf_s(log, "%s,data:%lld,node:%lld,\n", "MEDIA", m_health_info.m_media_write_data, m_health_info.m_media_write_node);

	for (int ii = 0; ii < BT_TEMP_NR; ++ii)
	{
		//fprintf_s(log, "TRUNC_%s,%lld,%lld,%.2f,%d,%.2f\n", BLK_TEMP_NAME[ii],
		//	m_truncated_host_write[ii], m_truncated_media_write[ii],
		//	(float)(m_truncated_media_write[ii]) / (float)(m_truncated_host_write[ii]), -1, -1.0);
		fprintf_s(log, "TRUNC_%s,%d,%.2f\n", BLK_TEMP_NAME[ii], -1, -1.0);
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





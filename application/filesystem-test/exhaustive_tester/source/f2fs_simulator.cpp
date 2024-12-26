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
LOG_CLASS_SIZE(CKPT_BLOCK);
LOG_CLASS_SIZE(CF2fsSimulator);
//LOG_CLASS_SIZE();
//LOG_CLASS_SIZE();

//#define MULTI_HEAD	1
const char* BLK_TEMP_NAME[] = { "COLD_DATA", "COLD_NODE", "WARM_DATA", "WARM_NODE", "HOT__DATA", "HOT__NODE", "EMPTY" };

// type 1: ���gc log, block count��أ�2: ���gc ���ܷ��������ֵVB����СֵVB����
//#define GC_TRACE_TYPE 1
#define GC_TRACE_TYPE 2

CF2fsSimulator::CF2fsSimulator(void)
	:m_segments(this), m_storage(this), m_pages(this), m_nat(this)
{
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
}

void CF2fsSimulator::CopyFrom(const IFsSimulator* src)
{
	const CF2fsSimulator* _src = dynamic_cast<const CF2fsSimulator*>(src);
	if (_src == nullptr) THROW_FS_ERROR(ERR_GENERAL, L"cannot copy from non CF2fsSimulator, src=%p", src);
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
	memcpy_s(&m_checkpoint, sizeof(m_checkpoint), &_src->m_checkpoint, sizeof(m_checkpoint));
}

bool CF2fsSimulator::Initialzie(const boost::property_tree::wptree& config, const std::wstring & log_path)
{
	memset(&m_health_info, 0, sizeof(FsHealthInfo));
	float op = config.get<float>(L"over_provision");
	m_multihead_cnt = config.get<int>(L"multi_header_num");

	// �����������и����⣬ԭ�ȷ���ʱ�����ļ�ָ��logical block��������Ȼ����������������������Ҫ��physical block������
	// ������������˼������physical block����Ӧ��Ҫ����logical block������
	// ��block�ļ��㷽���޸�Ϊ��������������޸Ŀ��ܻ�����WAF���Եļ��������˱���ԭ�����㷨
	size_t device_size = config.get<size_t>(L"volume_size");	// device_size ���ֽ�Ϊ��λ
	BLK_T phy_blk = (BLK_T)(ROUND_UP_DIV(device_size, BLOCK_SIZE));		
	SEG_T seg_nr = (SEG_T)ROUND_UP_DIV(phy_blk, BLOCK_PER_SEG);

	SEG_T gc_th_lo = config.get<SEG_T>(L"gc_seg_lo");
	SEG_T gc_th_hi = config.get<SEG_T>(L"gc_seg_hi");

	// ��ʼ��
	m_storage.Initialize();

	m_segments.InitSegmentManager(seg_nr, gc_th_lo, gc_th_hi);
	m_pages.Init(m_health_info.m_blk_nr);
	m_nat.Init(NID_IN_USE);
	InitOpenList();

	// �ļ�ϵͳ��С�ɱ���̶���
	m_health_info.m_seg_nr = MAIN_SEG_NR;
	m_health_info.m_blk_nr = m_health_info.m_seg_nr * BLOCK_PER_SEG;
	m_health_info.m_logical_blk_nr = m_health_info.m_blk_nr - ((gc_th_lo+10) * BLOCK_PER_SEG);
	m_health_info.m_free_blk = m_health_info.m_logical_blk_nr;

	// ��ʼ��root
	CPageInfo* page = m_pages.allocate(true);
	BLOCK_DATA* root_node = m_pages.get_data(page);
	if (!InitInode(root_node, page, F2FS_FILE_DIR))	{
		THROW_FS_ERROR(ERR_NODE_FULL, L"node full during initializing");
	}
	root_node->node.m_ino = ROOT_FID;
	root_node->node.m_nid = ROOT_FID;
	page->nid = ROOT_FID;
	// root ��Ϊ��Զ�򿪵��ļ�
	root_node->node.inode.ref_count = 1;			// root inode ʼ��cache
	m_nat.node_catch[ROOT_FID] = m_pages.page_id(page);
	PHY_BLK root_phy = UpdateInode(page, "CREATE");	// д�����
	m_nat.set_phy_blk(ROOT_FID, root_phy);

	m_health_info.m_dir_num = 1;
	m_health_info.m_file_num = 0;
	memset(&m_checkpoint, 0, sizeof(m_checkpoint));

	// ��ʼ������
	m_segments.SyncSSA();
	m_segments.SyncSIT();
	m_segments.reset_dirty_map();
	m_nat.Sync();
	f2fs_write_checkpoint();
	m_storage.Sync();

	// set log path
	m_log_path = log_path;
	// invalid block trace log����¼��ʱ��˳��д�����Ч����phy block
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
{	// ��ȡnode��������page�С���ȡ��page�ᱣ����cache�У�����Ҫ�������ͷš�
	// ���page�Ѿ������棬��ӻ����ж�ȡ������Ӵ��̶�ȡ��
	if (nid >= NODE_NR) THROW_FS_ERROR(ERR_GENERAL, L"Invalid node id: %d", nid);
	JCASSERT(page == nullptr);

	// ���node�Ƿ���cache��
	BLOCK_DATA * block =nullptr;
	if (m_nat.node_catch[nid] != INVALID_BLK)
	{
		page = m_pages.page(m_nat.node_catch[nid]);
		block = m_pages.get_data(page);
	}
	else
	{	// �Ӵ����ж�ȡ
		PHY_BLK blk = m_nat.get_phy_blk(nid);
		if (is_invalid(blk)) {
			THROW_FS_ERROR(ERR_INVALID_NID, L"invalid nid=%d in reading node", nid);
		}
		page = m_pages.allocate(true);
		m_storage.BlockRead(CF2fsSegmentManager::phyblk_to_lba(blk), page);
		page->phy_blk = blk;
		page->nid = nid;
		page->offset = INVALID_BLK;
		// �����ȡ��block
		m_nat.node_catch[nid] = m_pages.page_id(page);
		block = m_pages.get_data(page);
		if (block->m_type != BLOCK_DATA::BLOCK_INDEX && block->m_type != BLOCK_DATA::BLOCK_INODE) {
			THROW_ERROR(ERR_APP, L"block phy-%d is not a node block, nid=%d", blk, nid);
		}		
		if (block->m_type == BLOCK_DATA::BLOCK_INODE) block->node.inode.ref_count = 0;
		
	}
	block->node.page_id = m_pages.page_id(page);//page->page_id;
	// ����node������ʹ��nat���ƣ�page�в���Ҫ��¼��node��offset��
	return block->node;
}

inline int DepthFromBlkNr(UINT blk_nr)
{
	int max_depth = 0;
	while (blk_nr != 0) max_depth++, blk_nr >>= 1;		// ͨ��block�������������
	return max_depth;
}

NID CF2fsSimulator::FindFile(NODE_INFO& parent, const char* fn)
{
	UINT blk_nr = get_file_blks(parent.inode);
	if (blk_nr == 0) return INVALID_BLK;
	// �����ļ���hash
	WORD hash = FileNameHash(fn);
	WORD fn_len = (WORD)(strlen(fn));
	NID fid = INVALID_BLK;

	// ����� dir file �ṹ����2^level����������ÿ���д��dentry block��ÿ��block����һ��hash slot��
	// �������ļ� ��f2fs :: __f2fs_find_entry() )
	int max_depth = DepthFromBlkNr(blk_nr);
	int blk_num=1, blk_index=0;		// ��ǰ����£�block��������һ��block�������һ��block
	for (int level = 0; level < max_depth; level++)
	{	// find_in_level
		WORD hh = hash % blk_num;
		CPageInfo* page = nullptr;
		FileReadInternal(&page, parent, blk_index + hh, blk_index + hh + 1);
		if (page == nullptr)
		{	// page �ն�������һ��level
			blk_index += blk_num;
			blk_num *= 2;
			continue;
		}
		BLOCK_DATA * block = m_pages.get_data(page);
		if (block != nullptr && block->m_type == BLOCK_DATA::BLOCK_DENTRY)
		{
			DENTRY_BLOCK& entries = block->dentry;

			// ����bitmap����
//			int index = 0;
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
	// ����block
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
	{	// fsync()
		DWORD start_blk, last_blk;
		OffsetToBlock(start_blk, last_blk, 0, inode.file_size);
		LBLK_T bb = 0;

		for (size_t ii = 0; (ii < INDEX_TABLE_SIZE) && (bb < last_blk); ++ii, bb += INDEX_SIZE)
		{
			NID nid = inode.index[ii];
			if (nid == INVALID_BLK) continue;		// index �ն�������
			PAGE_INDEX page_id = m_nat.node_catch[nid];
			if (page_id != INVALID_BLK)
			{
				CPageInfo* page = m_pages.page(page_id);
				if (page->dirty)
				{
					// <TODO> page��nid��offsetӦ����page����ʱ����
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
	// ��һ��node block�����Ƶ�ʱ�򣬸���L2P�����ӡ� <TODO> F2FS ���
	// ���ȼ�����nid�Ƿ���cache��
	//	�����cache�У���cache dirty������GC����cache д��storage
	//	�����cache�У���cache undirty��discache
	m_nat.set_phy_blk(nid, phy_blk);
}

void CF2fsSimulator::UpdateIndex(NID nid, UINT offset, PHY_BLK phy_blk)
{
	// ���index�Ƿ�catch
	CPageInfo* page = nullptr;
	NODE_INFO* node = nullptr;
	node = &ReadNode(nid, page);
	// ����index
	if (node->index.index[offset] == INVALID_BLK)
	{
		THROW_ERROR(ERR_USER, L"data block in index is invalid, index nid=%d, offset=%d", nid, offset);
	}
	node->index.index[offset] = phy_blk;
	// ��дnid���������ּ��GC�б����ã�����Ҫ�ٴδ���GC��
	page->dirty = true;
}

CPageInfo* CF2fsSimulator::AllocateDentryBlock()
{
	CPageInfo* page = m_pages.allocate(true);
	BLOCK_DATA * block = m_pages.get_data(page);
	InitDentry(block);
	return page;
}

ERROR_CODE CF2fsSimulator::add_link(NODE_INFO* parent, const char* fn, NID fid)
{
	// �����ļ���hash
	WORD hash = FileNameHash(fn);
	WORD fn_len = (WORD)(strlen(fn));
	int slot_num = ROUND_UP_DIV(fn_len, FN_SLOT_LEN);

	UINT blk_nr = get_file_blks(parent->inode);	
	int max_depth = DepthFromBlkNr(blk_nr);		// ��ʾlevel number���������
	if (blk_nr == 0) max_depth = 0;
	int blk_num=1, blk_index=0;		// ��ǰ����£�block��������һ��block�������һ��block
	int level = 0;
	int index_start = 0;

	int blk_offset = 0;
	CPageInfo* dentry_page = nullptr;

	// ��ȡdir�ļ���
	for (; level < max_depth; level++)
	{	// ����β��ҿ�λ��
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
				// ��λ��һ�����е�slot��bitmap�ĸ�λΪ0��Ϊguide
				while (entries.bitmap & mask) { mask <<= 1; index_start++; }	
				if (index_start >= DENTRY_PER_BLOCK) break;	// û�п�λ goto next 
				index = index_start;
				// ��λ��һ���ǿ��е�slot
				while (((entries.bitmap & mask) == 0) && index < DENTRY_PER_BLOCK) { mask <<= 1; index++; }
				if ((index - index_start) >= slot_num)
				{	// �ҵ���λ
					dentry_page = page;
					break;
				}
				index_start = index;
			}
			if (index_start < DENTRY_PER_BLOCK)		break;	//�ҵ���λ
			m_pages.free(page);
		}
		else
		{	// ���slot�ǿյģ�����slot ����һ���յ�block
			blk_offset = blk_index + hh;
			dentry_page = AllocateDentryBlock();
			index_start = 0;
			// ֻ���������ļ���ε�ʱ�����Ҫ�����ļ���С��
			break;
		}
		blk_index += blk_num, blk_num *= 2;
	}

	if (index_start >= DENTRY_PER_BLOCK || dentry_page == nullptr)
	{	// û���ҵ���λ�� ����µĲ��
		max_depth++;
		if (max_depth > MAX_DENTRY_LEVEL) {
//			THROW_FS_ERROR(ERR_DENTRY_FULL, L"parent fid=%d, fn len=%d", parent->m_nid, fn_len)
			return ERR_DENTRY_FULL;
		}
		blk_num = (1 << (max_depth - 1));
		blk_index = blk_num - 1;
		WORD hh = hash % blk_num;

		// ����һ���յ�block
		blk_offset = blk_index + hh;
		dentry_page = AllocateDentryBlock(/*dpage_id*/);
		index_start = 0;
		// �����ļ���С���ļ���С��������һ�㣬ȷ�����ļ���С������ʱû���ļ�֧�ֿն��������ļ���С��Ӱ��ʵ��ʹ�á�
		parent->inode.file_size = ((1<<max_depth)-1) * BLOCK_SIZE;
		CPageInfo* parent_page = m_pages.page(parent->page_id); JCASSERT(parent_page);
		parent_page->dirty = true;
	}

	// ��dentryд���ļ�
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
	if (written == 0) return ERR_NODE_FULL;
	else return ERR_OK;
}

void CF2fsSimulator::unlink(NID fid, CPageInfo * parent_page)
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
				{	// ɾ��
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
				UINT written = FileWriteInternal(parent, ii, ii + 1, &page);
				if (written == 0) THROW_FS_ERROR(ERR_DELETE_DIR, L"no resource when delete link");
			}
			m_pages.free(page);
		}
	}
	if (!found) THROW_ERROR(ERR_APP, L"cannot find fid:%d in parent", fid);
	sync_fs();
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
				if (entries.dentries[jj].ino != INVALID_BLK) child++;
			}
			m_pages.free(page);
		}
	}
//	if (!found) THROW_ERROR(ERR_APP, L"cannot find fid:%d in parent", fid);

	return child;
}


ERROR_CODE CF2fsSimulator::InternalCreatreFile(NID & fid, const std::string& fn, bool is_dir)
{
	// ����·���ҵ����ڵ�
	fid = INVALID_BLK;
	char full_path[MAX_PATH_SIZE+1];
	strcpy_s(full_path, fn.c_str());

	char * ptr = strrchr(full_path, '\\');
	if (ptr == nullptr || *ptr == 0) {
		return ERR_WRONG_PATH;
	}
	*ptr = 0;
	CPageInfo* page = nullptr; // m_pages.allocate_index(true);
	NID parent_fid = FileOpenInternal(full_path, page);	// ������Ҫ��ӵĸ�Ŀ¼��nid��parent_page���游Ŀ¼��page
	CloseInode(page);
	if (parent_fid == INVALID_BLK)
	{
		LOG_ERROR(L"[err] parent path %S is not exist", full_path);
		return ERR_PARENT_NOT_EXIST;
	}

	CPageInfo* parent_page = nullptr;
	NODE_INFO & parent = ReadNode(parent_fid, parent_page);
	// ��� fn ���� parentĿ¼��
	char* name = ptr + 1;
	if (FindFile(parent, name) != INVALID_BLK)
	{
		LOG_ERROR(L"[err] file %S is already exist", fn.c_str());
		return ERR_CREATE_EXIST;
	}

	// ����inode��allocate nid
	page = m_pages.allocate(true);
	BLOCK_DATA* inode_block = m_pages.get_data(page);
	F2FS_FILE_TYPE file_type = is_dir ? F2FS_FILE_DIR : F2FS_FILE_REG;
	if (!InitInode(inode_block, page, file_type)) {
		CloseInode(parent_page);
		m_pages.free(page);
		return ERR_NODE_FULL;
	}
		
	fid = inode_block->node.m_nid;
	// ��ӵ����ڵ�dentry��
	ERROR_CODE ir = add_link(&parent, name, fid);
	if (ir != ERR_OK) {
		LOG_ERROR(L"[err] failed on add child to parent");
		CloseInode(parent_page);
		m_nat.put_node(fid);
		m_pages.free(page);
		fid = INVALID_BLK;
		return ir;
	}
	// cache page;
	m_nat.node_catch[fid] = m_pages.page_id(page);
	inode_block->node.inode.nlink++;
//	fs_trace("CREATE", page->data_index, 0, 0);
	// ����inode
	PHY_BLK phy_blk = UpdateInode(page, "CREATE");
	m_nat.set_phy_blk(fid, phy_blk);
	InterlockedIncrement(&(inode_block->node.inode.ref_count));
	CloseInode(parent_page);

	sync_fs();

	return ERR_OK;
}

ERROR_CODE CF2fsSimulator::FileCreate(NID & fid, const std::string& fn)
{
	if (m_open_nr >= MAX_OPEN_FILE) {
		fid = INVALID_BLK;
		return ERR_MAX_OPEN_FILE;
	}

	ERROR_CODE ir = InternalCreatreFile(fid, fn, false);
	if (ir != ERR_OK) {
		fid = INVALID_BLK;
		return ir;
	}
		
	if (fid == INVALID_BLK) 	{
		return ERR_CREATE;
	}
	// ����open list��
	m_health_info.m_file_num++;

	CPageInfo* page = nullptr; // m_pages.allocate_index(true);
	NODE_INFO *inode = &ReadNode(fid, page);		// now, the ipage points to the opened file.
	// ����open list
	AddFileToOpenList(fid, page);
	inode->inode.ref_count++;
	return ERR_OK;
}

ERROR_CODE CF2fsSimulator::DirCreate(NID & fid, const std::string& fn)
{
	ERROR_CODE ir = InternalCreatreFile(fid, fn, true);
	if (ir != ERR_OK) {
		fid = INVALID_BLK;
		return ir;
	}
	if (fid == INVALID_BLK) {
		return ERR_CREATE;
	}
	m_health_info.m_dir_num++;
	return ir;
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
	// ����open list
	UINT index = m_free_ptr;
	m_free_ptr = m_open_files[index].ipage;		// next
	m_open_files[index].ino = fid;
	m_open_files[index].ipage = m_pages.page_id(page);	// page->page_id;
	m_open_nr++;
	return m_open_files + index;
}

ERROR_CODE CF2fsSimulator::FileOpen(NID &fid, const std::string& fn, bool delete_on_close)
{
	fid = INVALID_BLK;
	char full_path[MAX_PATH_SIZE + 1];
	strcpy_s(full_path, fn.c_str());

	CPageInfo* page = nullptr; // m_pages.allocate_index(true);
	fid = FileOpenInternal(full_path, page);
	CloseInode(page);
	if (fid == INVALID_BLK)	{	return ERR_OPEN_FILE; }
	page = nullptr;

	// �����ļ��Ƿ��Ѿ���
	NODE_INFO* inode = nullptr;
	OPENED_FILE* file = FindOpenFile(fid);
	if (file)
	{	// �ļ��Ѿ���
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
		// ����open list
		AddFileToOpenList(fid, page);
	}
	inode->inode.ref_count++;
	return ERR_OK;
}

void CF2fsSimulator::ReadNodeNoCache(NODE_INFO& node, NID nid)
{
	if (is_valid(m_nat.node_catch[nid]))
	{
		CPageInfo * page = m_pages.page(m_nat.node_catch[nid]);
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

void CF2fsSimulator::GetFileDirNum(NID fid, UINT& file_nr, UINT& dir_nr)
{
	file_nr = 0; dir_nr = 0;
	// ��inode fid
	NODE_INFO inode;
	ReadNodeNoCache(inode, fid);

	// �����file���򷵻�
	if (inode.inode.file_type != F2FS_FILE_DIR) {
		file_nr = 1;
		dir_nr = 0;
		return;
	}
	// �����dir���������dentry
	dir_nr++;
	for (int ii = 0; ii < INDEX_TABLE_SIZE; ++ii)
	{
		NID index_nid = inode.inode.index[ii];
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
				// ���ڻ�õ�ino�����еݹ����
				UINT sub_file_nr = 0, sub_dir_nr = 0;
				GetFileDirNum(entries.dentries[ii].ino, sub_file_nr, sub_dir_nr);
				file_nr += sub_file_nr;
				dir_nr += sub_dir_nr;
			}
		}
	}
	// ���dentry��Ŀ��Ŀ¼���ݹ��飬
}

bool CF2fsSimulator::Mount(void)
{
	get_checkpoint();
	m_segments.Load(m_checkpoint);
	m_nat.Load(m_checkpoint);
	// load root
	CPageInfo* page = nullptr;	//m_pages.allocate_index(true);
	NODE_INFO & root_node = ReadNode(ROOT_FID, page);
	root_node.inode.ref_count = 1;
	m_nat.node_catch[root_node.m_nid] = m_pages.page_id(page);	// page->page_id;

	return true;
}

bool CF2fsSimulator::Unmount(void)
{
	// close all files
	for (int ii = 0; ii < MAX_OPEN_FILE; ++ii)
	{
		if (m_open_files[ii].ino != INVALID_BLK) {
			ForceClose(m_open_files + ii);
		}
	}
	sync_fs();
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

void CF2fsSimulator::sync_fs(void)
{
	// ����index, ��������Ϊgc�򿪵�index page
	for (int ii = 0; ii < NODE_NR; ++ii)
	{
		PAGE_INDEX page_id = m_nat.node_catch[ii];
		if (page_id != INVALID_BLK)
		{
			CPageInfo* page = m_pages.page(page_id);
			if (page->dirty)
			{
				// <DONE> page��nid��offsetӦ����page����ʱ����
				PHY_BLK phy = m_segments.WriteBlockToSeg(page);
//				LOG_TRACK(L"write_node", L"save node[%d] to block:%d", ii, phy);
				m_nat.set_phy_blk(ii, phy);
				page->dirty = false;
			}
		}
	}
	m_segments.SyncSSA();
	f2fs_write_checkpoint();
}

void CF2fsSimulator::f2fs_write_checkpoint()
{
	// flush_nat : ��nat�ı仯д��journal
	m_nat.f2fs_flush_nat_entries(m_checkpoint);
	// flush_sit
	m_segments.f2fs_flush_sit_entries(m_checkpoint);
	// do_checkpoint
	// 1. ����ÿ��curseg
	size_t curseg_size = sizeof(CURSEG_INFO) * BT_TEMP_NR;

	memcpy_s(m_checkpoint.cur_segs, curseg_size, m_segments.m_cur_segs, curseg_size);
	// 2. curseg��summary (summary block�����ƣ�����fsck�ؽ���
	
	// 3. write to storage
	CPageInfo* page = m_pages.allocate(true);
	BLOCK_DATA* data = m_pages.get_data(page);
	memcpy_s(&data->ckpt, sizeof(CKPT_BLOCK), &m_checkpoint, sizeof(CKPT_BLOCK));
	data->m_type = BLOCK_DATA::BLOCK_CKPT;

	m_storage.BlockWrite(CKPT_START_BLK, page);
	m_pages.free(page);
}

bool CF2fsSimulator::get_checkpoint()
{
	CPageInfo* page = m_pages.allocate(true);
	CKPT_BLOCK* ckpt = &m_pages.get_data(page)->ckpt;
	m_storage.BlockRead(CKPT_START_BLK, page);
	memcpy_s(&m_checkpoint, sizeof(CKPT_BLOCK), ckpt, sizeof(CKPT_BLOCK));
	m_pages.free(page);
	return true;
}



NID CF2fsSimulator::FileOpenInternal(char* fn, CPageInfo* &parent_inode)
{
	JCASSERT(parent_inode == nullptr);
	NODE_INFO& root = ReadNode(ROOT_FID, parent_inode);		// ��root�ļ�
	NODE_INFO* parent_node = &root;
	NID fid = ROOT_FID;

	char* next = nullptr;
	char* dir = strtok_s(fn, "\\", &next);			// dirӦ������ָ���һ����\��
	if (dir == nullptr || *dir == 0) return ROOT_FID;

	while (1)
	{	
		// cur: ���ڵ㣬dir��Ŀ��ڵ�
		fid = FindFile(*parent_node, dir);
		if (fid == INVALID_BLK)
		{
			LOG_ERROR(L"[err] cannot find %S in %S", dir, fn);
			return INVALID_BLK;
		}
		dir = strtok_s(nullptr, "\\", &next);
		if (dir == nullptr || *dir == 0) break;
		// �ر��Ѿ��򿪵�node
		CloseInode(parent_inode);							// �ر��ϴδ򿪵��ļ�
		parent_node = & ReadNode(fid, parent_inode);		// �ٴδ��ļ�
	}
	return fid;
}

void CF2fsSimulator::ForceClose(OPENED_FILE* file)
{
	CPageInfo* ipage = m_pages.page(file->ipage);
	BLOCK_DATA* data = m_pages.get_data(ipage);
	INODE& inode = data->node.inode;
	inode.ref_count = 0;

	//if (inode.ref_count <= 0) THROW_ERROR(ERR_USER, L"file's refrence == 0, fid=%d", data->node.m_ino);
	//inode.ref_count--;
	//if (inode.ref_count == 0)
	//{
	//	// �ͷ� open list
	//	file->ino = INVALID_BLK;
	//	file->ipage = m_free_ptr;
	//	m_free_ptr = (UINT)(file - m_open_files);
	//	m_open_nr--;
	//	CloseInode(ipage);
	//}
	CloseInode(ipage);
}

void CF2fsSimulator::FileClose(NID fid)
{
	// find opne file
	OPENED_FILE* file = FindOpenFile(fid);
	if (file == nullptr) THROW_FS_ERROR(ERR_OPEN_FILE, L"file fid=%d is not opened", fid);
	CPageInfo* ipage = m_pages.page(file->ipage);
	INODE& inode = m_pages.get_data(ipage)->node.inode;

	if (inode.ref_count <= 0) THROW_FS_ERROR(ERR_OPEN_FILE, L"file's refrence == 0, fid=%d", fid);
	inode.ref_count--;
	if (inode.ref_count == 0)
	{
//		ForceClose(file);
		
		// �ͷ� open list
		file->ino = INVALID_BLK;
		file->ipage = m_free_ptr;
		m_free_ptr = (UINT)(file - m_open_files);
		m_open_nr--;
		CloseInode(ipage);
	}


}

FSIZE CF2fsSimulator::FileWrite(NID fid, FSIZE offset, FSIZE len)
{
	// find opne file
	OPENED_FILE* file = FindOpenFile(fid);
	if (file == nullptr) THROW_ERROR(ERR_USER, L"file fid=%d is not opened", fid);
	CPageInfo* ipage = m_pages.page(file->ipage);
	NODE_INFO & inode = m_pages.get_data(ipage)->node;

	// ������ʼblock�ͽ���block��end_block�����Ҫд�����һ����blk_nr=end_block - start_block
	DWORD start_blk, end_blk;
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
	// �����µ��ļ���С
	UINT end_pos = offset + len;
	if (start_blk + written < end_blk) end_pos = (start_blk + written) * BLOCK_SIZE;
	// �ļ������������ʵ�ʳ���
	if (end_pos > inode.inode.file_size) {	inode.inode.file_size = end_pos;	}
	ipage->dirty = true;
	return (end_pos - offset);
}

UINT CF2fsSimulator::FileWriteInternal(NODE_INFO& inode, FSIZE start_blk, FSIZE end_blk, CPageInfo* pages[])
{
	CIndexPath ipath(&inode);
	CPageInfo* ipage = m_pages.page(inode.page_id);
	NODE_INFO* di_node = nullptr;
	CPageInfo* di_page = nullptr;
	//fs_trace("WRITE", fid, start_blk, end_blk - start_blk);
	// ����˵��
	//								page offset,	page ָ��,	node offset, node ָ��,	block,
	// �ļ���inode:					na,			na,			fid,		nid,	
	// ��ǰ�����direct offset node:	??,			di_page,	ipath��,		di_node,
	// ��ǰ���ݿ�					_pp,		dpage,		na,			na,	
	int page_index = 0;
	// ��ʱpage, ��Ҫд���pageΪnull�ǣ���ʾ�û����ݣ���care��
	for (; start_blk < end_blk; start_blk++, page_index++)
	{
		// ����PHY_BLK��λ��
		if (ipath.level < 0)
		{
			if (!OffsetToIndex(ipath, start_blk, true))			{
				return page_index;
			}
			// ��ȡindex��λ�ã���Ҫ����index
			di_node = ipath.node[ipath.level];
			di_page = ipath.page[ipath.level];
		}
		// data block��index�е�λ��
		int offset = ipath.offset[ipath.level];

		// ȷ�������¶�
		PHY_BLK phy_blk = di_node->index.index[offset];
		CPageInfo* dpage = pages[page_index];
		JCASSERT(dpage);

		// <TODO> page��nid��offsetӦ����page����ʱ���á���Ϊdata page��û�л��棬��д��ǰ����page.
		dpage->phy_blk = phy_blk;
		dpage->nid = di_node->m_nid;
		dpage->offset = offset;
		PHY_BLK new_phy_blk = m_segments.WriteBlockToSeg(dpage);
		di_node->index.index[offset] = new_phy_blk;

		if (phy_blk == INVALID_BLK)
		{	// ����߼���û�б�д���������߼����Ͷȣ�����inode��block number������д����block����WriteBlockToSeg()��invalidate ��block
			InterlockedIncrement(&m_health_info.m_logical_saturation);
			if (m_health_info.m_logical_saturation >= (m_health_info.m_logical_blk_nr))
			{
				LOG_WARNING(L"[warning] logical saturation overflow, logical_saturation=%d, logical_block=%d", m_health_info.m_logical_saturation, m_health_info.m_logical_blk_nr);
			}
			inode.inode.blk_num++;
			di_node->index.valid_data++;
			ipage->dirty = true;
		}
		// ע�⣬�˴���û��ʵ�� NAT������wandering tree���⡣
		di_page->dirty = true;
		InterlockedIncrement64(&m_health_info.m_total_host_write);
		dpage->host_write++;
		// ��ipath�ƶ�����һ��offset 
		m_segments.CheckGarbageCollection(this);
		NextOffset(ipath);
	}
	// node block ���ӳٵ�����fsync()ʱ����
	return page_index;
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
		// ����PHY_BLK��λ��
		if (ipath.level < 0)
		{
			OffsetToIndex(ipath, start_blk, false);
			di_node = ipath.node[ipath.level];
			di_page = ipath.page[ipath.level];
		}
		if (di_node == nullptr || di_page == nullptr)
		{	// index �ն�������
			continue;
		}
		// �˴��ݲ�֧�ֿն�
		if (!di_node) THROW_ERROR(ERR_APP, L"invalid index block, fid=%d, lblk=%d", inode.m_nid, start_blk);
		// ��ȡindex��λ�ã���Ҫ����index
		int index = ipath.offset[ipath.level];
		PHY_BLK blk = di_node->index.index[index];
//		LOG_DEBUG(L"read fid:%d, lblk:%d, phy blk:%d", inode.m_ino, start_blk, blk);
		if (pages[page_index] != nullptr) THROW_ERROR(ERR_APP, L"read internal() will allocate page");
		if (blk != INVALID_BLK)
		{	// ����block����ȡ���������page����nullptr
			CPageInfo* page = m_pages.allocate(true);
			m_storage.BlockRead(CF2fsSegmentManager::phyblk_to_lba(blk), page);
			page->phy_blk = blk;
			page->nid = di_node->m_nid;
			page->offset = index;
			pages[page_index] = page;
		}
		// ��ipath�ƶ�����һ��offset 
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

	// ������ʼblock�ͽ���block��end_block�����Ҫд�����һ����blk_nr=end_block - start_block
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

	// �ļ�������block����Ч��Ȼ�󱣴�inode
	CIndexPath ipath(&inode);
	NODE_INFO* di_node = nullptr;
	CPageInfo* di_page = nullptr;

	for (; start_blk < end_blk; start_blk++)
	{
		// ����PHY_BLK��λ��
		if (ipath.level < 0)
		{
			OffsetToIndex(ipath, start_blk, false);
			di_node = ipath.node[ipath.level];
			di_page = ipath.page[ipath.level];
		}
		// index �ն�������
		if (di_node == nullptr || di_page == nullptr) continue;
		// ��ȡindex��λ�ã���Ҫ����index
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
				di_node->index.valid_data--;
				inode.inode.blk_num--;
				ipage->dirty = true;
			}
		}
		// ��ipath�ƶ�����һ��offset 
		NextOffset(ipath);
	}
	// node block�ӳٵ�fsync()ʱ����
//	UpdateInode(ipage, "TRUNCATE");
}


void CF2fsSimulator::FileTruncate(NID fid, FSIZE offset, FSIZE len)
{
	// find opne file
	OPENED_FILE* file = FindOpenFile(fid);
	if (file == nullptr) THROW_ERROR(ERR_USER, L"file fid=%d is not opened", fid);
	CPageInfo* ipage = m_pages.page(file->ipage);
	NODE_INFO& inode = m_pages.get_data(ipage)->node;

	// �ļ�������block����Ч��Ȼ�󱣴�inode
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
		ipage->dirty = true;
	}
}

void CF2fsSimulator::FileDelete(const std::string& fn)
{
	// Delete ��������������1��unlink�����ļ���inode��dentry��ɾ����inode��link��1����2��remove inode����inodeɾ��
	char _fn[MAX_PATH_SIZE + 1];
	strcpy_s(_fn, fn.c_str());

	// unlink
	CPageInfo* parent_page = nullptr;
	NID fid = FileOpenInternal(_fn, parent_page);
	if (fid == INVALID_BLK)
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

	// <TODO> ����Ŀ¼��ɾ����������Ŀ¼
}

ERROR_CODE CF2fsSimulator::DirDelete(const std::string& fn)
{
	char _fn[MAX_PATH_SIZE + 1];
	strcpy_s(_fn, fn.c_str());
	CPageInfo* parent_page = nullptr;
	NID fid = FileOpenInternal(_fn, parent_page);
	if (fid == INVALID_BLK)
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
	// ɾ���ļ�������inode
	if (inode->inode.ref_count > 0) {
		LOG_ERROR(L"[err] file reference code is not zero, fid=%d, count=%d", inode->m_nid, inode->inode.ref_count);
	}
	LBLK_T end_blk = ROUND_UP_DIV(inode->inode.file_size, BLOCK_SIZE);
	FileTruncateInternal(inode_page, 0, end_blk);
	// ɾ��map��FileDelete����
	m_nat.node_catch[inode->m_nid] = INVALID_BLK;
	m_nat.put_node(inode->m_nid);
	InvalidBlock("DELETE_NODE", inode_page->phy_blk);
	m_pages.free(inode_page);
	inode_page = nullptr;

	m_node_blks--;
	// page ��free_inode��ɾ��, ͳ�Ʊ����յ�inode��WAF
	m_health_info.m_file_num--;
}

void CF2fsSimulator::FileFlush(NID fid)
{
	fs_trace("FLUSH", fid, 0, 0);
	return;

}

PHY_BLK CF2fsSimulator::UpdateInode(CPageInfo * ipage, const char* caller)
{
	// ����˵��
	//								page offset,	page ָ��,	node offset, node ָ��,	block,
	// �ļ���inode:					na,			i_page,		fid,		nid,	
	// ��ǰ�����direct offset node:	di_page_id,	di_page,	di_node_id,	di_node,
	// ��ǰ���ݿ�					_pp,		dpage,		na,			na,	

	// ��������Ƿ�һ����host����ģ�// ����ipath
	BLOCK_DATA* idata = m_pages.get_data(ipage);
	NODE_INFO& inode = idata->node;
	DWORD start_blk, last_blk;
	OffsetToBlock(start_blk, last_blk, 0, inode.inode.file_size);
	LBLK_T bb = 0;
	for (size_t ii = 0; (ii < INDEX_TABLE_SIZE) && (bb < last_blk); ++ii, bb += INDEX_SIZE)
	{
		NID nid = inode.inode.index[ii];
		if (nid == INVALID_BLK) continue;	// index �ն�

		PAGE_INDEX di_page_id = m_nat.node_catch[nid];
		if (di_page_id == INVALID_BLK) continue;		// offset node�����ݲ����ڣ�����û�б�����

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
			m_nat.put_node(di_node.m_nid);
			// decache index page
			m_nat.node_catch[nid] = INVALID_BLK;
			m_pages.free(di_page);

			inode.inode.index[ii] = INVALID_BLK;
			ipage->dirty = true;
			continue;
		}
		if (di_page->dirty)
		{
			di_page->host_write++;
			if (di_page->phy_blk == INVALID_BLK) m_node_blks++;
			// page��nid��offsetӦ����page����ʱ����
			PHY_BLK phy_blk = m_segments.WriteBlockToSeg(di_page);
			//LOG_DEBUG(L"write index, fid=%d, nid=%d, phy=%d,", inode.m_ino, di_node.m_nid, phy_blk);
			m_nat.set_phy_blk(nid, phy_blk);
			m_segments.CheckGarbageCollection(this);
		}

#ifdef ENABLE_FS_TRACE
		//<TRACE>��¼inode�ĸ��������
		// opid, fid, inode����index id, ԭ�����ݸ���������
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
		// <DONE> page��nid��offsetӦ����page����ʱ����
		new_phy = m_segments.WriteBlockToSeg(ipage);
//		LOG_DEBUG(L"write offset block, fid:%d, nid:%d, phy:%d", inode.m_ino, inode.m_nid, new_phy);
		m_nat.set_phy_blk(inode.m_nid, new_phy);
		m_segments.CheckGarbageCollection(this);

#ifdef ENABLE_FS_TRACE
		//<TRACE>��¼inode�ĸ��������
		// opid, fid, inode����index id, ԭ�����ݸ���������
		fprintf_s(m_inode_trace, "%lld,%d,0,UPDATE\n", m_write_count, nid.m_nid);
#endif
	}
	return new_phy;
}

FSIZE CF2fsSimulator::GetFileSize(NID fid)
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

void CF2fsSimulator::GetFileInfo(NID fid, FSIZE& size, FSIZE& node_blk, FSIZE& data_blk)
{
	CPageInfo* page = nullptr;
	NODE_INFO& inode = ReadNode(fid, page);
	size = inode.inode.file_size;
	data_blk = inode.inode.blk_num;
	node_blk = 1;	// inode;
	for (int ii = 0; ii < INDEX_TABLE_SIZE; ++ii)
	{
		if (inode.inode.index[ii] != INVALID_BLK) node_blk++;
	}
}

void CF2fsSimulator::GetFsInfo(FS_INFO& space_info)
{
	space_info.total_seg = m_segments.get_seg_nr();
	space_info.free_seg = m_segments.get_free_nr();
	space_info.total_blks = m_health_info.m_blk_nr;
	space_info.used_blks = m_health_info.m_logical_saturation;
	space_info.free_blks = m_health_info.m_free_blk;
	// ���ܳ��ָ���
	if (space_info.free_blks > m_health_info.m_logical_blk_nr) space_info.free_blks = 0;
	space_info.physical_blks = m_health_info.m_physical_saturation;
	space_info.max_file_nr = NODE_NR/2;

	//space_info.dir_nr = m_health_info.m_dir_num;
	//space_info.file_nr = m_health_info.m_file_num;

	space_info.total_host_write = m_health_info.m_total_host_write;
	space_info.total_media_write = m_health_info.m_total_media_write;

	space_info.total_page_nr = m_pages.total_page_nr();
	space_info.total_data_nr = 0;// m_pages.total_data_nr();
	space_info.free_page_nr  = m_pages.free_page_nr();
	space_info.free_data_nr = 0;// m_pages.free_data_nr();
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

bool CF2fsSimulator::OffsetToIndex(CIndexPath& ipath, LBLK_T offset, bool alloc)
{
	// ��inode�а��Ѿ��е�index block����node[]��
	ipath.level = 1;
	ipath.offset[0] = offset / INDEX_SIZE;

	NODE_INFO* inode = ipath.node[0];
	NID nid = inode->inode.index[ipath.offset[0]];
	CPageInfo* index_page = nullptr;
	NODE_INFO * index_node = nullptr;
	if (nid == INVALID_BLK)
	{	// ��Ӧ�� offset node��δ����
		if (alloc)
		{	// �����µ� node
			index_page = m_pages.allocate(true);
			BLOCK_DATA * block = m_pages.get_data(index_page);
			if (!InitIndexNode(block, inode->m_ino, index_page)) {
//				THROW_FS_ERROR(ERR_NODE_FULL, L"node full during allocate index block");
				LOG_ERROR(L"node full during allocate index block");
				return false;
			}
			index_node = &block->node;
			nid = block->node.m_nid;
			inode->inode.index[ipath.offset[0]] = nid;
			CPageInfo* ipage = m_pages.page(inode->page_id);
			ipage->dirty = true;
			//����page
			m_nat.node_catch[nid] = m_pages.page_id(index_page);//index_page->page_id;
		}
	}
	else
	{	// node �Ѿ�����������Ƿ��Ѿ�����
		index_node = &ReadNode(nid, index_page);
	}
	ipath.page[1] = index_page;
	ipath.node[1] = index_node;
	ipath.offset[1] = offset % INDEX_SIZE;
	// ����ÿһ���ƫ����
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
		(float)(m_health_info.m_total_media_write) / (float)(m_health_info.m_total_host_write));

	for (int ii = 0; ii < BT_TEMP_NR; ++ii)
	{
		fprintf_s(log, "TRUNC_%s,%d,%.2f\n", BLK_TEMP_NAME[ii], -1, -1.0);
	}
	fclose(log);
}

size_t CF2fsSimulator::DumpFileIndex(NID index[], size_t buf_size, NID fid)
{
	CPageInfo* ipage = nullptr; // m_pages.allocate_index(true);
	NODE_INFO &inode = ReadNode(fid, ipage);
	memcpy_s(index, sizeof(NID) * buf_size, inode.inode.index, sizeof(NID) * INDEX_TABLE_SIZE);
	return inode.index.valid_data;
}

void CF2fsSimulator::DumpSegments(const std::wstring& fn, bool sanity_check)
{
}

void CF2fsSimulator::DumpSegmentBlocks(const std::wstring& fn)
{
//	m_segments.DumpSegmentBlocks(fn);
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


bool CF2fsSimulator::InitInode(BLOCK_DATA* block, CPageInfo* page, F2FS_FILE_TYPE type)
{
	memset(block, 0xFF, sizeof(BLOCK_DATA));
	block->m_type = BLOCK_DATA::BLOCK_INODE;

	NODE_INFO* node = &(block->node);
	node->m_nid = m_nat.get_node();
	if (node->m_nid == INVALID_BLK) {
		//			THROW_ERROR(ERR_APP, L"node is full");
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

bool CF2fsSimulator::InitIndexNode(BLOCK_DATA* block, NID parent, CPageInfo* page)
{
	memset(block, 0xFF, sizeof(BLOCK_DATA));
	block->m_type = BLOCK_DATA::BLOCK_INDEX;

	NODE_INFO* node = &(block->node);
	node->m_nid = m_nat.get_node();
	if (node->m_nid == INVALID_BLK) {
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
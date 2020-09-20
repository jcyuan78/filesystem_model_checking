#include "stdafx.h"
#include "..\include\yaffs_file.h"
#include "..\include\yaffs_dir.h"
#include "..\include\yaf_fs.h"
#include <boost\cast.hpp>

LOCAL_LOGGER_ENABLE(L"yaffs_fs", LOGGER_LEVEL_NOTICE);

// -- help functions

// Function to return the number of shifts to get a 1 in bit 0
static inline UINT32 calc_shifts(UINT32 x)
{
	u32 shifts;
	shifts = 0;
	if (!x)		return 0;
	while (!(x & 1)) 
	{
		x >>= 1;
		shifts++;
	}
	return shifts;
}

/* Function to return the number of shifts for a power of 2 greater than or
 * equal to the given number
 * Note we don't try to cater for all possible numbers and this does not have to
 * be hellishly efficient. */

static inline UINT32 calc_shifts_ceiling(UINT32 x)
{
	int extra_bits;
	int shifts;
	shifts = extra_bits = 0;
	while (x > 1) 
	{
		if (x & 1)			extra_bits++;
		x >>= 1;
		shifts++;
	}
	if (extra_bits)		shifts++;
	return shifts;
}

// -- CYafFs

CYafFs::CYafFs() 
	: m_handlesInitialized(false), m_driver(NULL), m_tagger(NULL)
	, m_block_manager(NULL)
{
	m_lost_n_found = NULL;
	m_unlinked_dir = NULL;
	m_del_dir = NULL;
	m_root_dir = NULL;

	m_dev = new yaffs_dev;
	memset(m_dev, 0, sizeof(yaffs_dev));
	m_trace_mask = 0;

	m_dev->gc_cleanup_list = NULL;
	m_dev->cache = NULL;
}


CYafFs::~CYafFs()
{
	delete[] m_dev->gc_cleanup_list;
	if (m_dev->param.n_caches > 0 && m_dev->cache)
	{
		for (int ii = 0; ii < m_dev->param.n_caches; ii++)
		{
			delete[](m_dev->cache[ii].data);
			//m_dev->cache[ii].data = NULL;
		}
		delete[](m_dev->cache);
		m_dev->cache = NULL;
	}

	RELEASE(m_driver);
	RELEASE(m_tagger);
	delete m_dev;
	delete m_block_manager;
	delete m_checkpt;


}

void CYafFs::GetRoot(IFileInfo *& root)
{
	root = static_cast<IFileInfo*>(m_root_dir);
	if (root) root->AddRef();
}

bool CYafFs::ConnectToDevice(IVirtualDisk * dev)
{
	m_driver = dynamic_cast<INandDriver*>(dev);
	if (m_driver == NULL)
	{
		LOG_ERROR(L"[err] the device is not a INandDeirver!");
		return false;
	}

	//<TODO> 临时处理：创建固定的driver, nand driver作为IVirtualDisk参数传入
	m_driver->AddRef();
	INandDriver::NAND_DEVICE_INFO dev_info;
	m_driver->GetFlashId(dev_info);
	m_dev->param.total_bytes_per_chunk = boost::numeric_cast<UINT32>(dev_info.data_size);
	m_dev->param.chunks_per_block = boost::numeric_cast<UINT32>(dev_info.page_num);
	m_dev->param.spare_bytes_per_chunk = boost::numeric_cast<UINT32>(dev_info.spare_size);

	m_dev->param.start_block = 1;
	m_dev->param.end_block =  boost::numeric_cast<UINT32>(dev_info.block_num - 1);

	m_dev->param.n_reserved_blocks = 8;
	m_dev->param.use_nand_ecc = true;
	if (!LowLevelInit())	return false;
	CreateTagHandler();
	InitTempBuffers();

	m_block_manager = new CBlockManager(m_driver);
	m_checkpt = new CYaffsCheckPoint(m_tagger, m_block_manager,
		m_dev->param.total_bytes_per_chunk, m_dev->data_bytes_per_chunk,
		m_dev->param.chunks_per_block);

	return true;
}

void CYafFs::Disconnect(void)
{
	DeinitTempBuffers();
	RELEASE(m_tagger);
	RELEASE(m_driver);
	delete m_block_manager;
	m_block_manager = NULL;
	delete m_checkpt;
	m_checkpt = NULL;
}

//<migrate> yaffs_guts.c:yaffs_guts_initialize()
//<migrate> yaffs.c: yaffs_mount_common()
bool CYafFs::Mount(void)
{
	bool read_only = false;
	bool br = false;
	bool retVal = false;
	bool result = false;

#ifdef _DEBUG
	LOG_DEBUG(L"before mount: object number=%d", m_obj_num);
	size_t pre_free = m_tnode_allocator.m_free_num;
#endif
	Lock();
	InitHandles();
	if (!m_dev->is_mounted) 
	{
		m_dev->read_only = read_only ? 1 : 0;
		result = Initialize();
		if (!result)	SetError(-ENOMEM);
		retVal = result;
	}
	else	SetError(-EBUSY);
	Unlock();
	LOG_DEBUG(L"after mount: object number=%d", m_obj_num);
	LOG_DEBUG(L"free tnode from %d => %d", pre_free, m_tnode_allocator.m_free_num);
	LOG_DEBUG(L"erased block = %d", m_block_manager->GetErasedBlocks());
	return retVal;
}

//<MIGRATE> yaffsfs.c : yaffs_unmount2_common
void CYafFs::Unmount(void)
{
#ifdef _DEBUG
	LOG_DEBUG(L"before unmount: object number=%d", m_obj_num);
	size_t pre_free = m_tnode_allocator.m_free_num;
#endif

	Lock();
	bool force = true;
	if (m_dev->is_mounted) 
	{
		int inUse;
		FlushWholeCache(false);
		CheckpointSave();
		inUse = IsDevBusy();
		if (!inUse || force) 
		{
			if (inUse)	BreakDeviceHandles();
			DeInitialize();
		}
		else SetError(-EBUSY);
	}
	else	SetError(-EINVAL);
	Unlock();
	LOG_DEBUG(L"free tnode from %d => %d", pre_free, m_tnode_allocator.m_free_num)
	LOG_DEBUG(L"after unmount: object number=%d", m_obj_num);
	LOG_DEBUG(L"erased block = %d", m_block_manager->GetErasedBlocks());
}

bool CYafFs::DokanGetDiskSpace(ULONGLONG & free_bytes, ULONGLONG & total_bytes, ULONGLONG & total_free_bytes)
{
	// get total space (BYTE)
	// <MIGRATE> yaffsfs.c yaffs_totalspace_common()
	free_bytes = 0;
	total_bytes = 0;
	total_free_bytes = 0;
	JCASSERT(m_dev);
	if (m_dev->is_mounted)
	{
		size_t retVal = 0;
		retVal = (m_dev->param.end_block - m_dev->param.start_block + 1) -
			m_dev->param.n_reserved_blocks;
		retVal *= m_dev->param.chunks_per_block;
		retVal *= m_dev->data_bytes_per_chunk;
		total_bytes = retVal;
		// get free space (BYTE)
		//	<MIGRATE> yaffsfs.c yaffs_freespace_common()

		retVal = GetNFreeChunks();
		retVal *= m_dev->data_bytes_per_chunk;
		free_bytes = retVal;
		total_free_bytes = free_bytes;
	}
	return true;
}

//<migrate> yaffs_guts.c : yaffs_del_file
bool CYafFs::DokanDeleteFile(const std::wstring & fn, IFileInfo * file, bool isdir)
{
	// 如果是dir，检查是否为空
#ifdef _DEBUG
// for debug
	size_t pre_free = m_tnode_allocator.m_free_num;
#endif
	CYaffsObject * obj = NULL;
	jcvos::auto_interface<IFileInfo> ff;
	if (!file)
	{	// find file by fn
		bool br = DokanCreateFile(ff, fn, GENERIC_READ | GENERIC_WRITE, 0, OPEN_EXISTING, 0, 0, isdir);
		// file not found
		if (!br || !ff)
		{
			LOG_ERROR(L"file or dir %s not found", fn.c_str());
			return false;
		}
		obj = ff.d_cast<CYaffsObject*>();	JCASSERT(obj);
		ff->CloseFile();
	}
	else
	{
		obj = dynamic_cast<CYaffsObject*>(file);		JCASSERT(obj);
	}
	if (isdir && !obj->IsEmpty()) return false;
	// DelObj会把object从其父节点中移除，如果没有其他节点引用此节点，则自动删除此节点。
	obj->DelObj();
	// for debug
#ifdef _DEBUG
	if (!isdir)
	{
		LOG_DEBUG(L"free tnode from %d => %d", pre_free, m_tnode_allocator.m_free_num)
	}
#endif
	return true;
}

//<migrate> from yaffs_guts.c: yaffs_guts_format_dev
bool CYafFs::MakeFileSystem(UINT32 volume_size, const std::wstring & volume_name)
{
	yaffs_block_state state;
	UINT32 dummy;
	// LowLevelInit()在ConnectToDevice()中处理
	if (m_dev->is_mounted)	return false;
	UINT32 first = m_block_manager->GetFirstBlock();
	UINT32 last = m_block_manager->GetLastBlock();
	for (UINT32 ii = first; ii <= last; ii++) 
	{
//		QueryInitBlockState(ii, state, dummy);
		m_tagger->QueryBlock(ii, state, dummy);
		if (state != YAFFS_BLOCK_STATE_DEAD)	EraseBlock(ii);
	}
	return true;
}

void CYafFs::InitHandles(void)
{
	int i;

	if (m_handlesInitialized) 	return;
	m_handlesInitialized = true;

	memset(m_inode, 0, sizeof(m_inode));
	memset(m_fd, 0, sizeof(m_fd));
	memset(m_handle, 0, sizeof(m_handle));
	memset(m_dsc, 0, sizeof(m_dsc));

	for (i = 0; i < YAFFSFS_N_HANDLES; i++)		m_fd[i].inodeId = -1;
	for (i = 0; i < YAFFSFS_N_HANDLES; i++)		m_handle[i].fdId = -1;
}

//bool CYafFs::IsPathDivider(YCHAR ch)
//{
//	return ch == YAFFS_PATH_DIVIDERS_CHAR;
//}


#ifdef UNDER_MIGRATING
yaffs_obj * CYafFs::DoFindDirectory(yaffs_obj * startDir, const YCHAR * path, YCHAR ** name, int symDepth, int * notDir, bool &loop)
{
	struct yaffs_obj *dir;
	const YCHAR *restOfPath;
	YCHAR str[YAFFS_MAX_NAME_LENGTH + 1];
	int i;

	if (symDepth > YAFFSFS_MAX_SYMLINK_DEREFERENCES) 
	{
		loop = true;
		return NULL;
	}

	if (startDir) 
	{
		dir = startDir;
		restOfPath = (YCHAR *)path;
	}
	else
	{
		dir = m_root_dir;
		restOfPath = path + 1;
	}
//	yaffsfs_FindRoot(path, &restOfPath);

	while (dir) 
	{
		/*
		 * parse off /.
		 * curve ball: also throw away surplus '/'
		 * eg. "/ram/x////ff" gets treated the same as "/ram/x/ff"
		 */
		while (IsPathDivider(*restOfPath))	restOfPath++;	/* get rid of '/' */
		*name = restOfPath;
		i = 0;

		while (*restOfPath && ! IsPathDivider(*restOfPath)) 
		{
			if (i < YAFFS_MAX_NAME_LENGTH) 
			{
				str[i] = *restOfPath;
				str[i + 1] = '\0';
				i++;
			}
			restOfPath++;
		}

		if (!*restOfPath)	return dir;			/* got to the end of the string */
		else 
		{
			if (wcscmp(str, L".") == 0)
			{ 
			//	yaffs_strcmp(str, _Y(".")) == 0) {
				/* Do nothing */
			}
			else if (wcscmp(str, L"..") == 0) 
			{
				dir = dir->parent;
			}
			else 
			{
				dir = yaffs_find_by_name(dir, str);

				dir = yaffsfs_FollowLink(dir, symDepth, loop);

				if (dir && dir->variant_type != YAFFS_OBJECT_TYPE_DIRECTORY) 
				{
					if (notDir)
						*notDir = 1;
					dir = NULL;
				}

			}
		}
	}
	/* directory did not exist. */
	return NULL;
}
#endif



// yaffs_guts.c : int yaffs_guts_initialise(struct yaffs_dev *dev)
bool CYafFs::Initialize(void)
{
	LOG_STACK_TRACE();
	bool init_failed = false;
	u32 x;
	u32 bits;

	// LowLevelInit()在ConnectToDevice()中处理
	//if (!LowLevelInit())	return false;

	if (m_dev->is_mounted) 
	{
		LOG_ERROR(L"[err] device already mounted");
		return false;
	}

	m_dev->is_mounted = 1;
	/* OK now calculate a few things for the device */
	// Calculate all the chunk size manipulation numbers:
	x = m_dev->data_bytes_per_chunk;
	/* We always use m_dev->chunk_shift and m_dev->chunk_div */
	m_dev->chunk_shift = calc_shifts(x);
	x >>= m_dev->chunk_shift;
	m_dev->chunk_div = x;
	/* We only use chunk mask if chunk_div is 1 */
	m_dev->chunk_mask = (1 << m_dev->chunk_shift) - 1;

	UINT32 start_blk = m_dev->param.start_block;
	UINT32 end_blk = m_dev->param.end_block;

	if (m_dev->param.start_block == 0)
	{
		start_blk = m_dev->param.start_block + 1;
		end_blk = m_dev->param.end_block + 1;
	}

	// Calculate chunk_grp_bits.
	// We need to find the next power of 2 > than internal_end_block
	x = m_dev->param.chunks_per_block * (start_blk + 1);
	bits = calc_shifts_ceiling(x);

	/* Set up tnode width if wide tnodes are enabled. */
	if (!m_dev->param.wide_tnodes_disabled)
	{	/* bits must be even so that we end up with 32-bit words */
		if (bits & 1)		bits++;
		if (bits < 16)		m_dev->tnode_width = 16;
		else				m_dev->tnode_width = bits;
	}
	else 	m_dev->tnode_width = 16;
	m_dev->tnode_mask = (1 << m_dev->tnode_width) - 1;

	/* Level0 Tnodes are 16 bits or wider (if wide tnodes are enabled),
	 * so if the bitwidth of the chunk range we're using is greater than 16 we need
	 * to figure out chunk shift and chunk_grp_size */
	if (bits <= m_dev->tnode_width)			m_dev->chunk_grp_bits = 0;
	else			m_dev->chunk_grp_bits = bits - m_dev->tnode_width;

	m_dev->tnode_size = (m_dev->tnode_width * YAFFS_NTNODES_LEVEL0) / 8;
	if (m_dev->tnode_size < sizeof(struct yaffs_tnode))
		m_dev->tnode_size = sizeof(struct yaffs_tnode);

	m_dev->chunk_grp_size = 1 << m_dev->chunk_grp_bits;
	if (m_dev->param.chunks_per_block < m_dev->chunk_grp_size) 
	{	/* We have a problem because the soft delete won't work if
		 * the chunk group size > chunks per block.
		 * This can be remedied by using larger "virtual blocks". */
		LOG_ERROR(L"[err] chunk group too large ")
		return false;
	}

	/* Finished verifying the device, continue with initialisation */
	/* More device initialisation */
	m_dev->all_gcs = 0;
	m_dev->passive_gc_count = 0;
	m_dev->oldest_dirty_gc_count = 0;
	m_dev->bg_gcs = 0;
	m_dev->gc_block_finder = 0;
	m_dev->buffered_block = -1;
	m_dev->doing_buffered_block_rewrite = 0;
	//m_dev->n_deleted_files = 0;
	m_dev->n_bg_deletions = 0;
	//m_dev->n_unlinked_files = 0;
	m_dev->n_ecc_fixed = 0;
	m_dev->n_ecc_unfixed = 0;
	m_dev->n_tags_ecc_fixed = 0;
	m_dev->n_tags_ecc_unfixed = 0;
	m_dev->n_erase_failures = 0;
	//m_dev->n_erased_blocks = 0;
	m_dev->gc_disable = 0;
	m_dev->has_pending_prioritised_gc = 1; /* Assume the worst for now,
						  * will get fixed on first GC */
//	INIT_LIST_HEAD(&m_dev->dirty_dirs);
	//m_dev->oldest_dirty_seq = 0;
	//m_dev->oldest_dirty_block = 0;

//<TODO>忽略endian
//	yaffs_endian_config(m_dev);

	/* Initialise temporary buffers and caches. */
	init_failed = true;
	do
	{
		if (!InitTempBuffers())
		{
			LOG_ERROR(L"[err] failed on initializing temp buffer");
			break;
		}
			//init_failed = true;
		JCASSERT(m_dev->cache == NULL);
		JCASSERT(m_dev->gc_cleanup_list == NULL);
//		m_dev->gc_cleanup_list = NULL;

		if (/*!init_failed &&*/ m_dev->param.n_caches > 0)
		{
			u32 i;
			BYTE *buf;
			u32 cache_bytes = m_dev->param.n_caches * sizeof(struct yaffs_cache);
			if (m_dev->param.n_caches > YAFFS_MAX_SHORT_OP_CACHES)
				m_dev->param.n_caches = YAFFS_MAX_SHORT_OP_CACHES;

			m_dev->cache = new yaffs_cache[m_dev->param.n_caches];
			if (m_dev->cache == NULL)
			{
				LOG_ERROR(L"[err] failed on allocating cache");
				break;
			}
			buf = (BYTE *)m_dev->cache;
			/*if (m_dev->cache)		*/memset(m_dev->cache, 0, cache_bytes);

			for (i = 0; i < m_dev->param.n_caches && buf; i++)
			{
				m_dev->cache[i].object = NULL;
				m_dev->cache[i].last_use = 0;
				m_dev->cache[i].dirty = 0;
				m_dev->cache[i].data = buf = new BYTE[m_dev->param.total_bytes_per_chunk];
			}
			if (!buf)
			{
				LOG_ERROR(L"[err] failed on allocating cache data");
				break;
			}
//				init_failed = true;
			m_dev->cache_last_use = 0;
		}

		m_dev->cache_hits = 0;
		//if (!init_failed)
		//{
		m_dev->gc_cleanup_list = new UINT32[m_dev->param.chunks_per_block];
		if (!m_dev->gc_cleanup_list)
		{
			LOG_ERROR(L"[err] failed on allocating gc clean list");
			//init_failed = true;
			break;
		}
		//}

		m_dev->param.use_header_file_size = true;


		//if (!init_failed)
		//{
		JCASSERT(m_block_manager);
		bool br = m_block_manager->InitBlockManager(m_driver, m_tagger, start_blk, end_blk,
			m_dev->param.chunks_per_block, m_dev->param.n_reserved_blocks);
		if (!br)
		{
			LOG_ERROR(L"[err] failed on initialize block manager");
			DeInitBlocks();
			//init_failed = true;
			break;
		}
		//}
		InitTnodesAndObjs();
		if (/*!init_failed &&*/ !CreateInitialDir())
		{
			LOG_ERROR(L"[err] failed on creating init directory");
			//init_failed = true;
			break;
		}
		if (/*!init_failed && */!m_dev->param.disable_summary)
		{
			bool br = SummaryInit();
			if (!br) 
			{
				LOG_ERROR(L"[err] failed on initialize summary");
				//init_failed = true; 
				break;
			}
		}


		LOG_NOTICE(L"[yaffs] restore checkpoint");
		//if (init_failed) break;
		/* Now scan the flash. */
		if (CheckptRestore())
		{
			m_root_dir->CheckObjDetailsLoaded();
			init_failed = false;
			LOG_NOTICE(L"[yaffs] succeeded restoring from checkpoint");
			break;
		}

		LOG_NOTICE(L"[yaffs] need to scan blocks");
		/* Clean up the mess caused by an aborted checkpoint load then scan backwards.	 */
		DeInitBlocks();
		DeInitTnodesAndObjs();
		m_dev->n_bg_deletions = 0;
		JCASSERT(m_block_manager);
		//m_block_manager = new CBlockManager;
		br = m_block_manager->InitBlockManager(m_driver, m_tagger, start_blk, end_blk,
			m_dev->param.chunks_per_block, m_dev->param.n_reserved_blocks);
		if (!br)
		{
			LOG_ERROR(L"[err] failed on initializing block manager");
			DeInitBlocks();
			//init_failed = true;
			break;
		}
		InitTnodesAndObjs();
		if (/*!init_failed &&*/ !CreateInitialDir())
		{
			LOG_ERROR(L"[err] failed on creating initial directors")
			//init_failed = true;
			break;
		}
		if (/*!init_failed && */!ScanBackwords())
		{
			LOG_ERROR(L"[err] failed on scan backwords");
			//init_failed = true;
			break;
		}
		StripDeletedObjs();
		FixHangingObjs();
		if (m_dev->param.empty_lost_n_found)		m_lost_n_found->DeleteDirContents();

		init_failed = false;
	} while (0);

	if (init_failed) 
	{	/* Clean up the mess */
		LOG_NOTICE(L"[yaffs] Initialize() aborted");
		DeInitialize();
		return false;
	}
	/* Zero out stats */
	m_dev->n_page_reads = 0;
	m_dev->n_page_writes = 0;
	m_dev->n_gc_copies = 0;
	m_dev->n_retried_writes = 0;

//<TODO> tobe implemented verify
//	yaffs_verify_free_chunks(m_dev);
//	yaffs_verify_blocks(m_dev);

	/* Clean up any aborted checkpoint data */
	m_checkpt->Invalidate();
	return true;
}

//<MIGRATE> yaffs_guts.c : yaffs_deinitialize()
void CYafFs::DeInitialize(void)
{
	if (!m_dev->is_mounted) return;

	//delete m_checkpt;
	//m_checkpt = NULL;

	u32 i;
	DeInitBlocks();
	DeInitTnodesAndObjs();
	SummaryDeInit();

	if (m_dev->param.n_caches > 0 && m_dev->cache) 
	{
		for (i = 0; i < m_dev->param.n_caches; i++) 
		{
			delete [] (m_dev->cache[i].data);
			m_dev->cache[i].data = NULL;
		}
		delete[](m_dev->cache);
		m_dev->cache = NULL;
	}
	delete[](m_dev->gc_cleanup_list);
	m_dev->gc_cleanup_list = NULL;
	m_dev->is_mounted = 0;
	// 对等原则，DInitialize NAND driver在Disconnect中执行
}


//<migrate> yaffs_guts.c : int yaffs_guts_ll_init(struct yaffs_dev *dev)
bool CYafFs::LowLevelInit(void)
{
	LOG_STACK_TRACE();
	if (m_dev->ll_init)		return true;
//	m_dev->n_free_chunks = 0;
	m_dev->gc_block = 0;

	UINT32 internal_start_blk = m_dev->param.start_block;
	UINT32 internal_end_blk = m_dev->param.end_block;
	if (m_dev->param.start_block == 0) 
	{
		internal_start_blk = m_dev->param.start_block + 1;
		internal_end_blk = m_dev->param.end_block + 1;
	}

	/* Check geometry parameters. */
	if ((m_dev->param.total_bytes_per_chunk < 1024) 
		||	m_dev->param.chunks_per_block < 2 
		||	m_dev->param.n_reserved_blocks < 2 
		||	internal_start_blk <= 0 
		||	internal_end_blk <= 0 
		||	internal_end_blk <=(internal_start_blk + m_dev->param.n_reserved_blocks + 2)
		) 
	{	/* otherwise it is too small */
		LOG_ERROR( L"NAND geometry problems: chunk size %d, type is yaffs%s, inband_tags %d ",
			m_dev->param.total_bytes_per_chunk,	L"2", 0);
		return false;
	}

	/* Sort out space for inband tags, if required */
	// we do not support inband tags
	m_dev->data_bytes_per_chunk = m_dev->param.total_bytes_per_chunk;
	//<yuan> initialize NAND driver (file) here, need to change to dynamic initialize
	//以下内容移至ConnectToDevice
	m_dev->ll_init = true;
	return true;
}

//bool CYafFs::InitBlocks(void)
//{
//	//int n_blocks = m_dev->internal_end_block - m_dev->internal_start_block + 1;
//
//	//m_dev->block_info = NULL;
//	//m_dev->chunk_bits = NULL;
//	//m_dev->alloc_block = -1;	/* force it to get a new one */
//
//	///* If the first allocation strategy fails, thry the alternate one */
//	//m_dev->block_info = new yaffs_block_info[n_blocks];
//	//if (!m_dev->block_info) 
//	//{
//	//	LOG_ERROR(L"[err] failed on creating block infors");
//	//	return false;
//	//}
//	//else
//	//{
//	//	m_dev->block_info_alt = 0;
//	//}
//
//	///* Set up dynamic blockinfo stuff. Round up bytes. */
//	//m_dev->chunk_bit_stride = (m_dev->param.chunks_per_block + 7) / 8;
//	//m_dev->chunk_bits = new BYTE[m_dev->chunk_bit_stride * n_blocks];
//
//	//if (!m_dev->chunk_bits) 
//	//{
//	//	LOG_ERROR(L"[err] failed on allocate chunk bits");
//	//	DeInitBlocks();
//	//	return false;
//	//}
//	//else 
//	//{
//	//	m_dev->chunk_bits_alt = 0;
//	//}
//	//memset(m_dev->block_info, 0, n_blocks * sizeof(struct yaffs_block_info));
//	//memset(m_dev->chunk_bits, 0, m_dev->chunk_bit_stride * n_blocks);
//	//return true;
//}

bool CYafFs::CreateInitialDir(void)
{
	/* Initialise the unlinked, deleted, root and lost+found directories */
	JCASSERT(m_lost_n_found == NULL && m_root_dir == NULL 
		&& m_unlinked_dir == NULL && m_del_dir == NULL);

	m_unlinked_dir = CreateFakeDir( YAFFS_OBJECTID_UNLINKED, S_IFDIR, YAFFS_UNLINKEDIR_NAME);
	m_del_dir = CreateFakeDir( YAFFS_OBJECTID_DELETED, S_IFDIR, YAFFS_DELETEDIR_NAME);
	m_root_dir = CreateFakeDir( YAFFS_OBJECTID_ROOT, YAFFS_ROOT_MODE | S_IFDIR, L"");
	m_lost_n_found = CreateFakeDir( YAFFS_OBJECTID_LOSTNFOUND, YAFFS_LOSTNFOUND_MODE | S_IFDIR, YAFFS_LOSTNFOUND_NAME);

	if (m_lost_n_found && m_root_dir && m_unlinked_dir && m_del_dir) 
	{	/* If lost-n-found is hidden then yank it out of the directory tree. */
		if (m_dev->param.hide_lost_n_found)
		{
			//JCASSERT(m_lost_n_found->m_parent == NULL);
			//list_del_init(&m_dev->lost_n_found->siblings);
		}
		else
		{
			m_root_dir->AddObjToDir(m_lost_n_found);
		}
		return true;
	}
	return false;
}

bool CYafFs::SummaryFetch(yaffs_ext_tags * tags, UINT32 chunk_in_block)
{
	struct yaffs_packed_tags2_tags_only tags_only;
	struct yaffs_summary_tags *sum_tags;
	if (chunk_in_block >= 0 && chunk_in_block < m_dev->chunks_per_summary) 
	{
		sum_tags = &m_dev->sum_tags[chunk_in_block];
		tags_only.chunk_id = sum_tags->chunk_id;
		tags_only.n_bytes = sum_tags->n_bytes;
		tags_only.obj_id = sum_tags->obj_id;
//		yaffs_unpack_tags2_tags_only(dev, tags, &tags_only);
		UnpackTags2TagsOnly(tags, &tags_only);
		return true;
	}
	return false;
}

bool CYafFs::SummaryInit(void)
{
	int sum_bytes;
	int chunks_used; /* Number of chunks used by summary */
	int sum_tags_bytes;

	sum_bytes = m_dev->param.chunks_per_block *	sizeof(struct yaffs_summary_tags);

	chunks_used = (sum_bytes + m_dev->data_bytes_per_chunk - 1) /
		(m_dev->data_bytes_per_chunk - sizeof(struct yaffs_summary_header));

	m_dev->chunks_per_summary = m_dev->param.chunks_per_block - chunks_used;
	sum_tags_bytes = sizeof(struct yaffs_summary_tags) * m_dev->chunks_per_summary;
//	m_dev->sum_tags = kmalloc(sum_tags_bytes, GFP_NOFS);
	m_dev->sum_tags = new yaffs_summary_tags[m_dev->chunks_per_summary];
//	m_dev->gc_sum_tags = kmalloc(sum_tags_bytes, GFP_NOFS);
	m_dev->gc_sum_tags = new yaffs_summary_tags[m_dev->chunks_per_summary];

	if (!m_dev->sum_tags || !m_dev->gc_sum_tags) 
	{
//		yaffs_summary_deinit(dev);
		SummaryDeInit();
		return false;
	}

//	yaffs_summary_clear(dev);
	memset(m_dev->sum_tags, 0, m_dev->chunks_per_summary * sizeof(struct yaffs_summary_tags));

	return true;
}

void CYafFs::SummaryDeInit(void)
{
	delete [] m_dev->sum_tags;
	m_dev->sum_tags = NULL;
	delete [] m_dev->gc_sum_tags;
	m_dev->gc_sum_tags = NULL;
	m_dev->chunks_per_summary = 0;
}

bool CYafFs::SummaryAdd(yaffs_ext_tags * tags, int chunk_in_nand)
{
	// 将当前chunk的tags添加到summary，如果超过指定的chunk数，则保存summary到nand，并且丢弃余下的empty page
	//<TODO> 优化：丢弃余下的empty page过于浪费，需要优化。
	struct yaffs_packed_tags2_tags_only tags_only;
	struct yaffs_summary_tags *sum_tags;
	int block_in_nand = chunk_in_nand / m_dev->param.chunks_per_block;
	int chunk_in_block = chunk_in_nand % m_dev->param.chunks_per_block;

	if (!m_dev->sum_tags)	return true;

	if (chunk_in_block >= 0 && chunk_in_block < m_dev->chunks_per_summary) 
	{
//		yaffs_pack_tags2_tags_only(dev, &tags_only, tags);
		PackTags2TagsOnly(&tags_only, tags);
		sum_tags = &m_dev->sum_tags[chunk_in_block];

		sum_tags->chunk_id = tags_only.chunk_id;
		sum_tags->n_bytes = tags_only.n_bytes;
		sum_tags->obj_id = tags_only.obj_id;

		if (chunk_in_block == m_dev->chunks_per_summary - 1) 
		{	// Time to write out the summary
//			yaffs_summary_write(dev, block_in_nand);
			SummaryWrite(block_in_nand);
//			yaffs_summary_clear(dev);
			SummaryClear();
//			yaffs_skip_rest_of_block(dev);
			m_block_manager->SkipRestOfBlock();
		}
	}
	return true;
}

//void CYafFs::CheckptInvalidate(void)
//{
//	if (m_dev->is_checkpointed || m_dev->blocks_in_checkpt > 0) 
//	{
//		m_dev->is_checkpointed = 0;
////		yaffs2_checkpt_invalidate_stream(dev);
//		CheckptErase();
//	}
//	//<TODO> to be implemented
//	//if (m_dev->param.sb_dirty_fn)
//	//	m_dev->param.sb_dirty_fn(dev);
//
//}

//bool CYafFs::CheckptErase(void)
//{
//	//u32 i;
////	if (!m_dev->drv.drv_erase_fn)	return 0;
//	JCASSERT(m_driver);
//	LOG_NOTICE(L"checking blocks %d to %d",	m_dev->internal_start_block, m_dev->internal_end_block);
//	for (UINT32 ii = m_dev->internal_start_block; ii <= m_dev->internal_end_block; ii++) 
//	{
//		struct yaffs_block_info *bi = GetBlockInfo(ii);
//		//int offset_i = ApplyBlockOffset(ii);
//		int offset_i = ii;
//		bool result;
//
//		if (bi->block_state == YAFFS_BLOCK_STATE_CHECKPOINT) 
//		{
//			LOG_NOTICE(L"erasing checkpt block %d", ii);
//			m_dev->n_erasures++;
//			result = m_driver->Erase(offset_i);
////			result = m_dev->drv.drv_erase_fn(dev, offset_i);
//			if (result) 
//			{
//				bi->block_state = YAFFS_BLOCK_STATE_EMPTY;
//				m_dev->n_erased_blocks++;
//				m_dev->n_free_chunks += m_dev->param.chunks_per_block;
//			}
//			else 
//			{
//				m_driver->MarkBad(offset_i);
////				m_dev->drv.drv_mark_bad_fn(dev, offset_i);
//				bi->block_state = YAFFS_BLOCK_STATE_DEAD;
//			}
//		}
//	}
//
//	m_dev->blocks_in_checkpt = 0;
//
//	return true;
//}

bool CYafFs::InitTnodesAndObjs(void)
{
	//int i;

	m_dev->n_obj = 0;
	m_dev->n_tnodes = 0;
//	展开yaffs_init_raw_tnodes_and_objs(m_dev);
	JCASSERT(m_dev->allocator == NULL);
	m_dev->allocator = new yaffs_allocator;
	InitRawTnodes();
	InitRawObjs();
	return true;
}

void CYafFs::InitRawTnodes(void)
{
	JCASSERT(m_dev->allocator);
	m_dev->allocator->alloc_tnode_list = NULL;
	m_dev->allocator->free_tnodes = NULL;
	m_dev->allocator->n_free_tnodes = 0;
	m_dev->allocator->n_tnodes_created = 0;

	m_tnode_allocator.Initialize(100, 0);
}


void CYafFs::InitRawObjs(void)
{
	m_obj_allocator.Initlaize(300);
}


void CYafFs::DeInitTnodesAndObjs(void)
{
//	展开yaffs_deinit_raw_tnodes_and_objs(dev);
	DeleteFakeDir();
	m_dev->n_obj = 0;
	m_dev->n_tnodes = 0;
	// delete all objects in bucket
	for (int ii = 0; ii < YAFFS_NOBJECT_BUCKETS; ii++)
	{
		std::list<CYaffsObject*> &list = m_obj_bucket[ii];
		for (auto it = list.begin(); it != list.end(); ++it)
		{
			CYaffsObject * &obj = (*it);
			RELEASE(obj);
		}
		list.clear();
	}

	delete m_dev->allocator;
	m_dev->allocator = NULL;
	m_tnode_allocator.Deinitialize();
	m_obj_allocator.Deinitialize();
}

void CYafFs::DeInitBlocks(void)
{
	//delete [] m_dev->block_info;
	////if (m_dev->block_info_alt && m_dev->block_info)
	////	vfree(m_dev->block_info);
	////else
	////	kfree(m_dev->block_info);

	//m_dev->block_info_alt = 0;
	//m_dev->block_info = NULL;

	////if (m_dev->chunk_bits_alt && m_dev->chunk_bits)
	////	vfree(m_dev->chunk_bits);
	////else
	////	kfree(m_dev->chunk_bits);
	//delete[] m_dev->chunk_bits;
	//m_dev->chunk_bits_alt = 0;
	//m_dev->chunk_bits = NULL;
	//delete m_block_manager;
	//m_block_manager = NULL;
	m_block_manager->Deinitialize();
}

bool CYafFs::FlushCacheForFile(CYaffsFile * file, bool discard)
{
	if (m_dev->param.n_caches < 1) return true;
	for (size_t ii = 0; ii < m_dev->param.n_caches; ii++) 
	{
		yaffs_cache * cache = &m_dev->cache[ii];
		if (cache->object == static_cast<CYaffsObject*>(file))
		{
//			yaffs_flush_single_cache(cache, discard);
			// 展开
			if (!cache || cache->locked)	continue;

			/* Write it out and free it up  if need be.*/
			if (cache->dirty) 
			{
				file->WriteDataObject(cache->chunk_id, cache->data, cache->n_bytes, true);
				cache->dirty = 0;
			}
			if (discard)	RELEASE(cache->object);
		}
	}
	return true;
}


void CYafFs::StripDeletedObjs(void)
{
	//  Sort out state of unlinked and deleted objects after scanning.
	//struct list_head *i;
	//struct list_head *n;
	//struct yaffs_obj *l;

	if (m_dev->read_only)	return;

	/* Soft delete all the unlinked files */
	CYaffsDir * dir = dynamic_cast<CYaffsDir*>(m_unlinked_dir);
	JCASSERT(dir);
	dir->DeleteChildren();

	dir = dynamic_cast<CYaffsDir*>(m_del_dir);
	JCASSERT(dir);
}

void CYafFs::FixHangingObjs(void)
{
	// 检查每个object是否被引用，root是否为其祖先节点。如果不是，则加入lost andfound
	int i;
	int depth_limit;
	bool hanging;

	if (m_dev->read_only)	return;

	/* Iterate through the objects in each hash entry, looking at each object. Make sure it is rooted. */
	for (i = 0; i < YAFFS_NOBJECT_BUCKETS; i++) 
	{
		std::list<CYaffsObject*> &list = m_obj_bucket[i];
		for (auto it=list.begin(); it!=list.end(); ++it)
		{
			CYaffsObject * obj = (*it);	
			JCASSERT(obj);
			CYaffsObject* parent = NULL;
			obj->GetParent(parent);
			/* These directories are not hanging */
			if (obj == static_cast<CYaffsObject*>(m_del_dir)
				|| obj==static_cast<CYaffsObject*>(m_unlinked_dir)
				|| obj==static_cast<CYaffsObject*>(m_root_dir)) hanging = false;
			else if (!parent || !parent->IsDirectory())			hanging = true;
			else 
			{	/* Need to follow the parent chain to see if it is hanging.	 */
				hanging = false;
				depth_limit = 100;

				CYaffsObject * pp = NULL;
				parent->GetParent(pp);
				while (parent != m_root_dir &&	pp && pp->IsDirectory() && depth_limit > 0) 
				{
					parent->Release();
					parent = pp;
					parent->GetParent(pp);
					depth_limit--;
				}
				if (pp) pp->Release();
				if (parent != m_root_dir) hanging = true;
			}
			if (hanging)
			{
				LOG_NOTICE(L"Hanging object %d moved to lost and found", obj->GetObjectId());
				m_lost_n_found->AddObjToDir(obj);
			}
			if (parent) parent->Release();
		}
	}

}
//<MIGRATE> yaffs_gust.c : yaffs_flush_whole_cache()
void CYafFs::FlushWholeCache(bool discard)
{
	CYaffsObject *obj;
	int n_caches = m_dev->param.n_caches;
	int i;

	/* Find a dirty object in the cache and flush it...
	 * until there are no further dirty objects.
	 */
	do 
	{
		obj = NULL;
		for (i = 0; i < n_caches && !obj; i++) 
		{
			if (m_dev->cache[i].object && m_dev->cache[i].dirty)
				obj = m_dev->cache[i].object;
		}
		if (obj)	obj->FlushFile();
//			yaffs_flush_file_cache(obj, discard);
	} while (obj);


}

bool CYafFs::CheckpointSave(void)
{
	LOG_NOTICE(L"[CHECKPT] save entry: is_checkpointed %d", m_checkpt->IsCheckpointed());
//	yaffs_verify_objects(dev);
	VerifyObjects();
//	yaffs_verify_blocks(dev);
	VerifyBlocks();
//	yaffs_verify_free_chunks(dev);
	VerifyFreeChunks();

	if (!m_checkpt->IsCheckpointed()) 
	{
//		yaffs2_checkpt_invalidate(dev);
		m_checkpt->Invalidate();
//		yaffs2_wr_checkpt_data(dev);
		WriteCheckptData();
	}

	LOG_NOTICE(L"[CHECKPT] save exit: is_checkpointed %d", m_checkpt->IsCheckpointed());
	return m_checkpt->IsCheckpointed();
}

//<MIGRATE> yaffsfs.c : yaffsfs_IsDevBusy()
bool CYafFs::IsDevBusy(void)
{
	int i;
	CYaffsObject *obj;

	for (i = 0; i < YAFFSFS_N_HANDLES; i++)
	{
//		obj = yaffsfs_HandleToObject(i);
		obj = HandleToObject(i);
		if (obj && obj->IsValid() )		return true;
	}
	return false;
}

//<MIGRATE> yaffsfs.c : static void yaffsfs_BreakDeviceHandles(struct yaffs_dev *dev)
void CYafFs::BreakDeviceHandles(void)
{
	struct yaffsfs_FileDes *fd;
	struct yaffsfs_Handle *h;
	CYaffsObject *obj;
	int i;
	for (i = 0; i < YAFFSFS_N_HANDLES; i++) 
	{
		h = HandleToPointer(i);
		fd = HandleToFileDes(i);
		obj = HandleToObject(i);
		if (h && h->useCount > 0) 
		{
			h->useCount = 0;
			h->fdId = 0;
		}
		if (fd && fd->handleCount > 0 && obj && obj->IsValid() ) 
		{
			fd->handleCount = 0;
			PutInode(fd->inodeId);
			fd->inodeId = -1;
		}
	}
}

//<migrate> yaffs_guts.c : static int yaffs_init_tmp_buffers(struct yaffs_dev *dev)
bool CYafFs::InitTempBuffers(void)
{
	int i;
	BYTE *buf = (BYTE *)1;

	memset(m_dev->temp_buffer, 0, sizeof(m_dev->temp_buffer));

	for (i = 0; buf && i < YAFFS_N_TEMP_BUFFERS; i++)
	{
		m_dev->temp_buffer[i].in_use = 0;
		buf = new BYTE[m_dev->param.total_bytes_per_chunk];
		m_dev->temp_buffer[i].buffer = buf;
	}
	return buf != NULL;
}

void CYafFs::DeinitTempBuffers(void)
{
	for (size_t ii = 0; ii < YAFFS_N_TEMP_BUFFERS; ii++)
	{
		delete[](m_dev->temp_buffer[ii].buffer);
		m_dev->temp_buffer[ii].buffer = NULL;
	}
}


// yaffs_guts.c : u8 *yaffs_get_temp_buffer(struct yaffs_dev * dev)
BYTE * CYafFs::GetTempBuffer(void)
{
	int i;

	m_dev->temp_in_use++;
	if (m_dev->temp_in_use > m_dev->max_temp)
		m_dev->max_temp = m_dev->temp_in_use;

	for (i = 0; i < YAFFS_N_TEMP_BUFFERS; i++) 
	{
		if (m_dev->temp_buffer[i].in_use == 0) 
		{
			m_dev->temp_buffer[i].in_use = 1;
			return m_dev->temp_buffer[i].buffer;
		}
	}

//<TODO> handle of memory out;
	//yaffs_trace(YAFFS_TRACE_BUFFERS, "Out of temp buffers");
	LOG_ERROR(L"[err] out of temp buffers");
	/*
	 * If we got here then we have to allocate an unmanaged one
	 * This is not good.
	 */

	//m_dev->unmanaged_buffer_allocs++;
	//return kmalloc(m_dev->data_bytes_per_chunk, GFP_NOFS);
	return NULL;
}

void CYafFs::ReleaseTempBuffer(BYTE * buffer)
{
	int i;

	m_dev->temp_in_use--;

	for (i = 0; i < YAFFS_N_TEMP_BUFFERS; i++) 
	{
		if (m_dev->temp_buffer[i].buffer == buffer) 
		{
			m_dev->temp_buffer[i].in_use = 0;
			return;
		}
	}
	LOG_ERROR(L"[err] memory is not in temp buffers");

//<TODO> handle of memory out 
/*
	if (buffer) 
	{
		// assume it is an unmanaged one.
		yaffs_trace(YAFFS_TRACE_BUFFERS,
			"Releasing unmanaged temp buffer");
		kfree(buffer);
		m_dev->unmanaged_buffer_deallocs++;
	}
*/
}

bool CYafFs::ReadChunkTagsNand(int nand_chunk, BYTE * buffer, yaffs_ext_tags * tags)
{
	bool result;
	struct yaffs_ext_tags local_tags;
	//int flash_chunk = ApplyChunkOffset(nand_chunk);
	int flash_chunk = nand_chunk;

	m_dev->n_page_reads++;

	/* If there are no tags provided use local tags. */
	if (!tags)	tags = &local_tags;

	result = m_tagger->ReadChunkTags(flash_chunk, buffer, tags);
	if (tags && tags->ecc_result > INandDriver::ECC_RESULT_NO_ERROR) 
	{
		yaffs_block_info *bi = m_block_manager->GetBlockInfo(nand_chunk / m_dev->param.chunks_per_block);
//		bi = GetBlockInfo(nand_chunk / m_dev->param.chunks_per_block);
		HandleChunkError(bi);
//		yaffs_handle_chunk_error(dev, bi);
	}
	return result;
}

CYaffsDir * CYafFs::CreateFakeDir(int number, u32 mode, const wchar_t * name)
{
	CYaffsDir* dir = NULL;
	CYaffsDir::CreateFakeDir(dir, this, number, mode, name);
	JCASSERT(dir);
	return dir;
}

void CYafFs::DeleteFakeDir(void)
{
	RELEASE(m_lost_n_found);
	RELEASE(m_unlinked_dir);
	RELEASE(m_del_dir);
	RELEASE(m_root_dir);
}

inline void cond_resched(void)
{
}

static int yaffs2_ybicmp(const void *a, const void *b)
{
	int aseq = ((struct yaffs_block_index *)a)->seq;
	int bseq = ((struct yaffs_block_index *)b)->seq;
	int ablock = ((struct yaffs_block_index *)a)->block;
	int bblock = ((struct yaffs_block_index *)b)->block;

	if (aseq == bseq)	return ablock - bblock;
	return aseq - bseq;
}



bool CYafFs::ScanBackwords(void)
{
	UINT32 n_to_scan = 0;
	int found_chunks;
	int alt_block_index = 0;

	UINT32 n_blocks = m_block_manager->GetBlockNumber();
	jcvos::auto_array<yaffs_block_index> block_index(new yaffs_block_index[n_blocks]);
	//yaffs_block_index *block_index = new yaffs_block_index[n_blocks];
	if (!block_index) 
	{
		LOG_ERROR(L"[err] could not allocate block index!");
		return false;
	}

	UINT32 checkpt_blks = 0;
	n_to_scan = m_block_manager->ScanBackwords(block_index, checkpt_blks, m_tagger);
	BYTE * chunk_data = GetTempBuffer();
	cond_resched();
	/* Sort the blocks by sequence number */
	qsort(block_index, n_to_scan, sizeof(struct yaffs_block_index), yaffs2_ybicmp);
	cond_resched();
	LOG_NOTICE(L"...done");

	/* Now scan the blocks looking at the data. */
	int start_iter=0;
	int end_iter=n_to_scan - 1;
	LOG_DEBUG(L"%d blocks to scan", n_to_scan);

	/* For each block.... backwards */
	bool alloc_failed = false;
	for (int block_iter = end_iter;	!alloc_failed && block_iter >= start_iter; block_iter--) 
	{	/* Cooperative multitasking! This loop can run for so  long that watchdog timers expire. */
		cond_resched();
		/* get the block to scan in the correct order */
		int blk = block_index[block_iter].block;

		yaffs_block_info * bi = m_block_manager->GetBlockInfo(blk);
		bool summary_available = SummaryRead(m_dev->sum_tags, blk);


		/* For each chunk in each block that needs scanning.... */
		found_chunks = 0;
		int chunk;
		if (summary_available)	chunk = m_dev->chunks_per_summary - 1;
		else					chunk = m_dev->param.chunks_per_block - 1;
		//bool br = m_block_manager->ScanChunkInBlock(blk, chunk, summary_available, this);
		//if (!br) alloc_failed = true;

		// 从最后一个chunk开始scan整个block
		for (/* chunk is already initialised */; 
			!alloc_failed && chunk >= 0 &&
			(bi->block_state == YAFFS_BLOCK_STATE_NEEDS_SCAN ||	bi->block_state == YAFFS_BLOCK_STATE_ALLOCATING);
			chunk--) 
		{	/* Scan backwards...		 * Read the tags and decide what to do	 */
			if (!ScanChunk(bi, blk, chunk, &found_chunks, chunk_data, summary_available))
				alloc_failed = true;
		}

		if (bi->block_state == YAFFS_BLOCK_STATE_NEEDS_SCAN) 
		{	/* If we got this far while scanning, then the block is fully allocated. */
			bi->block_state = YAFFS_BLOCK_STATE_FULL;
		}

		/* Now let's see if it was dirty */
		if (bi->pages_in_use == 0 && !bi->has_shrink_hdr &&	bi->block_state == YAFFS_BLOCK_STATE_FULL) 
		{
			BlockBecameDirty(blk);
		}
	}

	m_block_manager->SkipRestOfBlock();
//	delete[] block_index;

	// Ok, we've done all the scanning. Fix up the hard link chains. 
	// We have scanned all the objects, now it's time to add these hardlinks.
	//ReleaseTempBuffer(chunk_data);
	if (alloc_failed)		return false;
	return true;
}

bool CYafFs::ScanChunk(yaffs_block_info * bi, UINT32 blk, UINT32 chunk_in_block, int * found_chunks, 
	BYTE * chunk_data, bool summary_available)
{
#if 1
	int is_unlinked;
	struct yaffs_ext_tags tags;
	bool result;
	bool alloc_failed = false;
	struct yaffs_file_var *file_var;
	struct yaffs_hardlink_var *hl_var;
	struct yaffs_symlink_var *sl_var;


	UINT32 chunk = blk * m_dev->param.chunks_per_block + chunk_in_block;
	if (summary_available) 
	{
		result = SummaryFetch(&tags, chunk_in_block);
		tags.seq_number = bi->seq_number;
	}

	if (!summary_available || tags.obj_id == 0) 
	{
		result = ReadChunkTagsNand(chunk, NULL, &tags);
		m_dev->tags_used++;
	}
	else 
	{
		m_dev->summary_used++;
	}
	LOG_DEBUG(L"Scan chunk (%03X:%03X), id=%d", blk, chunk_in_block, tags.chunk_id);

	if (result == false)	LOG_ERROR(L"[warning] Could not get tags for chunk (%03X:%03X)", blk, chunk_in_block);

	/* Let's have a good look at this chunk... */
	if (!tags.chunk_used) 
	{	/* An unassigned chunk in the block. If there are used chunks after this one, 
		then it is a chunk that was skipped due to failing the erased check. 
		Just skip it so that it can be deleted. But, more typically, We get here when 
		this is an unallocated chunk and his means that either the block is empty 
		or this is the one being allocated from */
		m_block_manager->ScanChunkUnused(bi, blk, chunk_in_block, found_chunks);
	}
	else if (tags.ecc_result == INandDriver::ECC_RESULT_UNFIXED) 
	{
		LOG_NOTICE(L" Unfixed ECC in chunk(%d:%d), chunk ignored",	blk, chunk_in_block);
		m_block_manager->BlockReleased(0, 1);
		//m_dev->n_free_chunks++;
	}
	else if (tags.obj_id > YAFFS_MAX_OBJECT_ID ||
		tags.chunk_id > YAFFS_MAX_CHUNK_ID ||
		tags.obj_id == YAFFS_OBJECTID_SUMMARY ||
		(tags.chunk_id > 0 &&	tags.n_bytes > m_dev->data_bytes_per_chunk) ||
		tags.seq_number != bi->seq_number) 
	{
		LOG_NOTICE(L"[warning] Chunk (%d:%d) with bad tags:obj = %d, chunk_id = %d, n_bytes = %d, ignored",
			blk, chunk_in_block, tags.obj_id, tags.chunk_id, tags.n_bytes);
		m_block_manager->BlockReleased(0, 1);
		//m_dev->n_free_chunks++;
	}
	else if (tags.chunk_id > 0) 
	{	/* chunk_id > 0 so it is a data chunk... */
		loff_t endpos;
		loff_t chunk_base = (tags.chunk_id - 1) * m_dev->data_bytes_per_chunk;
		*found_chunks = 1;
		//yaffs_set_chunk_bit(dev, blk, chunk_in_block);
		m_block_manager->SetChunkBit(blk, chunk_in_block);
		bi->pages_in_use++;
		//in = yaffs_find_or_create_by_number(dev,
		//	tags.obj_id,
		//	YAFFS_OBJECT_TYPE_FILE);
		jcvos::auto_interface<CYaffsObject> obj_in;
		FindOrCreateByNumber(obj_in, tags.obj_id, YAFFS_OBJECT_TYPE_FILE);
//		JCASSERT(ff);
		if (!obj_in)
		{
			LOG_ERROR(L"[err] failed on allocating object");
			return false;
		}
		//alloc_failed = true;	/* Out of memory */

		bool invalidate = true;	// suppose the chunk is invalidate; 
		if (obj_in->GetType() == YAFFS_OBJECT_TYPE_FILE)
		{
			CYaffsFile * ff = obj_in.d_cast<CYaffsFile*>();
			JCASSERT(ff);
			if (chunk_base < ff->GetShrinkSize())
			{	/* This has not been invalidated by a resize */
				//if (!yaffs_put_chunk_in_file(in, tags.chunk_id,
				//	chunk, -1))
				if (!ff->PutChunkInFile(tags.chunk_id, chunk, -1))	alloc_failed = false;
				/* File size is calculated by looking at the data chunks if we have not
				 * seen an object header yet. Stop this practice once we find an object header. */
				endpos = chunk_base + tags.n_bytes;
				if (!obj_in->IsValid() && ff->GetStoredSize() < endpos) ff->SetStoredSize(endpos);
				//{
				//	in->variant.file_variant.stored_size = endpos;
				//	in->variant.file_variant.file_size = endpos;
				//}
				invalidate = false;
			}
		}
		//This chunk has been invalidated by a resize, or a past file deletion so delete the chunk		
		if (invalidate)			ChunkDel(chunk, 1, __LINE__);
		//}
	}
	else 
	{	/* chunk_id == 0, so it is an ObjectHeader. Thus, we read in the object header and make
		 * the object */
		LOG_DEBUG(L"Scan chunk (%03X:%03X) for obj chunk", blk, chunk_in_block);

		do
		{
			*found_chunks = 1;
			//		yaffs_set_chunk_bit(dev, blk, chunk_in_block);
			m_block_manager->SetChunkBit(blk, chunk_in_block);
			bi->pages_in_use++;

			struct yaffs_obj_hdr *oh = NULL;
			jcvos::auto_interface<CYaffsObject> obj_in;
			if (tags.extra_available)
			{
				FindOrCreateByNumber(obj_in, tags.obj_id, tags.extra_obj_type);
//				if (obj_in && obj_in->IsValid() ) break;
				if (!obj_in)	alloc_failed = true;
			}

			yaffs_obj_type local_type = tags.extra_obj_type;

			if (!obj_in ||
				(!obj_in->IsValid() && m_dev->param.disable_lazy_load) ||
				tags.extra_shadows ||
				(!obj_in->IsValid() && (tags.obj_id == YAFFS_OBJECTID_ROOT ||
					tags.obj_id == YAFFS_OBJECTID_LOSTNFOUND)))
			{
				/* If we don't have valid info then we need to read the chunk
				 * TODO In future we can probably defer reading the chunk and living with
				 * invalid data until needed. */

				 //result = yaffs_rd_chunk_tags_nand(dev, chunk, chunk_data, NULL);
				result = ReadChunkTagsNand(chunk, chunk_data, NULL);
				oh = (struct yaffs_obj_hdr *)chunk_data;
				if (oh) local_type = (yaffs_obj_type) oh->type;
				if (!obj_in)
				{
					FindOrCreateByNumber(obj_in, tags.obj_id, (yaffs_obj_type)oh->type);
					if (!obj_in)	alloc_failed = true;
				}
			}

			if (!obj_in)
			{		/* TODO Hoosterman we have a problem! */
				LOG_ERROR(L"[err] yaffs tragedy: Could not make object for object  %d at chunk %d during scan",
					tags.obj_id, chunk);
				return false;
			}
			bool is_shrink;

			if (obj_in->IsValid())
			{	/* We have already filled this one. We have a duplicate that will be
				 * discarded, but we first have to suck out resize info if it is a file.  */
				// 相关的object已经被读取，这个chunk是一个过时的chunk，需要删除。
				if ((obj_in->GetType() == YAFFS_OBJECT_TYPE_FILE) &&
					(local_type == YAFFS_OBJECT_TYPE_FILE) )
				{
					is_shrink = (oh) ? oh->is_shrink : tags.extra_is_shrink;
					if (is_shrink) bi->has_shrink_hdr = 1;
					obj_in->SetObjectByHeaderTag(this, chunk, oh, tags);
				}
				/* Use existing - destroy this one. */
				ChunkDel(chunk, true, __LINE__);
				break;
			}

			//if (!obj_in->IsValid() &&
			if (obj_in->GetType() != local_type)
			{
				LOG_ERROR(L"[err] yaffs tragedy: Bad type, %d != %d, for object %d at chunk %d during scan",
					local_type,
					obj_in->GetType(), tags.obj_id, chunk);
//				in = yaffs_retype_obj(in, oh ? oh->type : tags.extra_obj_type);
				UINT32 id = obj_in->GetObjectId();
				obj_in.release();
				CYaffsObject::NewObject(obj_in, this, id, local_type);
			}

			obj_in->SetObjectByHeaderTag(this, chunk, oh, tags);
//			if (obj_in->IsValid()) break;

			//else if (!obj_in->IsValid())
			//{
			
			/* we need to load this info */
//			obj_in->valid = 1;
//			obj_in->hdr_chunk = chunk;
			jcvos::auto_interface<CYaffsObject> parent;
			loff_t file_size;
			UINT32 yst_mode;
			int equive_id;
			bool layz_loaded = false;

			if (oh)
			{
				FindOrCreateByNumber(parent, oh->parent_obj_id, YAFFS_OBJECT_TYPE_DIRECTORY);
				is_shrink = oh->is_shrink;
				//equiv_id = oh->equiv_id;
				//if (oh->shadows_obj > 0) yaffs_handle_shadowed_obj(dev, oh->shadows_obj, 1);
			}
			else
			{
				FindOrCreateByNumber(parent, tags.extra_parent_id, YAFFS_OBJECT_TYPE_DIRECTORY);
				//file_size = tags.extra_file_size;
				is_shrink = tags.extra_is_shrink;
				//equiv_id = tags.extra_equiv_id;
			}
			
			if (!parent)				alloc_failed = 1;
			/* directory stuff...		 hook up to parent			 */
			//if (parent && parent->variant_type == YAFFS_OBJECT_TYPE_UNKNOWN)
			//{		/* Set up as a directory */
			//	parent->variant_type = YAFFS_OBJECT_TYPE_DIRECTORY;
			//	INIT_LIST_HEAD(&parent->variant.dir_variant.children);
			//}
			//else if (!parent || parent->variant_type != YAFFS_OBJECT_TYPE_DIRECTORY)
			//{	/* Hoosterman, another problem.... Trying to use a non-directory as a directory	 */
			//	yaffs_trace(YAFFS_TRACE_ERROR,
			//		"yaffs tragedy: attempting to use non-directory as a directory in scan. Put in lost+found."
			//	);
			//	parent = m_dev->lost_n_found;
			//}
			
			CYaffsDir * pp = parent.d_cast<CYaffsDir*>();
			pp->AddObjToDir(obj_in);
			JCASSERT(pp);
			//is_unlinked = (parent == m_dev->del_dir) || (parent == m_dev->unlinked_dir);
			if (is_shrink)		bi->has_shrink_hdr = 1;		/* Mark the block */
					

			/* Note re hardlinks.
			 Since we might scan a hardlink before its equivalent object is scanned we put
			 them all in a list. After scanning is complete, we should have all the objects, 
			 so we run through this list and fix up all the chains. */

			//case YAFFS_OBJECT_TYPE_HARDLINK:
			//	hl_var = &obj_in->variant.hardlink_variant;
			//	if (!is_unlinked) {
			//		hl_var->equiv_id = equiv_id;
			//		list_add(&obj_in->hard_links, hard_list);
			//	}
			//	break;
			//case YAFFS_OBJECT_TYPE_SYMLINK:
			//	sl_var = &obj_in->variant.symlink_variant;
			//	if (oh) {
			//		sl_var->alias =
			//			yaffs_clone_str(oh->alias);
			//		if (!sl_var->alias)
			//			alloc_failed = 1;
			//	}
			//	break;
		} while (0);
	}
	return alloc_failed ? false : true;
#else
	return false;
#endif 
}

bool CYafFs::CheckptRestore(void)
{
	bool retval = true;
	
	LOG_NOTICE(L"restore entry : is_checkpointed % d", m_checkpt->IsCheckpointed());
	//<TODO> to be migrated
//	retval = yaffs2_rd_checkpt_data(dev);
	retval = ReadCheckptData();

	if (m_checkpt->IsCheckpointed()) 
	{
		//yaffs_verify_objects(dev);
		VerifyObjects();
		//yaffs_verify_blocks(dev);
		VerifyBlocks();
		//yaffs_verify_free_chunks(dev);
		VerifyFreeChunks();
	}

	LOG_NOTICE(L"restore exit: is_checkpointed %d", m_checkpt->IsCheckpointed());
	return retval;
}

bool CYafFs::ReadCheckptData(void)
{
	JCASSERT(m_checkpt);
	bool ok = true;
	if (m_dev->param.skip_checkpt_rd) ERROR_HANDLE(return false, L"[err] skipping checkpoint read");

	ok = m_checkpt->Open(false);
	if (!ok) ERROR_HANDLE(return false, L"[err] failed on open checkpt");

	LOG_NOTICE(L"read checkpoint validity");
	ok = m_checkpt->ReadValidityMarker(1);
	if (!ok) ERROR_HANDLE(return false, L"[err] failed on checkpt validity maker 1");

	LOG_NOTICE(L"read checkpoint device");
	ok = ReadCheckptDev(*m_checkpt);
	if (!ok) ERROR_HANDLE(return false, L"[err] failed on reading checkpt device");

	LOG_NOTICE(L"read checkpoint objects");
	ok = ReadCheckptObjects(*m_checkpt);
	if (!ok) ERROR_HANDLE(return false, L"[err] failed on reading checkpt objects");

	LOG_NOTICE(L"read checkpoint validity");
//	ok = ReadCheckptValidityMarker(0);
	ok = m_checkpt->ReadValidityMarker(0);
	if (!ok) ERROR_HANDLE(return false, L"[err] failed on reading checkpt marker 0");

	ok = m_checkpt->ReadSum();
	if (!ok) ERROR_HANDLE(return false, L"[err] failed on reading checkpt sum");
	LOG_NOTICE(L"read checkpoint checksum %d", ok);

	if (!m_checkpt->Close()) ok = false;

	//if (ok)	m_dev->is_checkpointed = 1;
	//else	m_dev->is_checkpointed = 0;
	return ok;
}

//bool CYafFs::CheckptOpen(bool writing)
//{
//	u32 i;
//
//	m_dev->checkpt_open_write = writing;
//
//	/* Got the functions we need? */
//	//if (!m_dev->tagger.write_chunk_tags_fn ||
//	//	!m_dev->tagger.read_chunk_tags_fn ||
//	//	!m_dev->drv.drv_erase_fn ||
//	//	!m_dev->drv.drv_mark_bad_fn)
//	//	return 0;
//	JCASSERT(m_driver && m_tagger);
//
////	if (writing && !yaffs2_checkpt_space_ok(dev))		return 0;
//	if (writing && !CheckptSpaceOk()) return false;
//
//	if (!m_dev->checkpt_buffer)
//		//		m_dev->checkpt_buffer =	kmalloc(m_dev->param.total_bytes_per_chunk, GFP_NOFS);
//		m_dev->checkpt_buffer = new BYTE[m_dev->param.total_bytes_per_chunk];
//	if (!m_dev->checkpt_buffer)	return false;
//
//	m_dev->checkpt_page_seq = 0;
//	m_dev->checkpt_byte_count = 0;
//	m_dev->checkpt_sum = 0;
//	m_dev->checkpt_xor = 0;
//	m_dev->checkpt_cur_block = -1;
//	m_dev->checkpt_cur_chunk = -1;
//	m_dev->checkpt_next_block = m_dev->internal_start_block;
//
//	if (writing) 
//	{
//		memset(m_dev->checkpt_buffer, 0, m_dev->data_bytes_per_chunk);
////		yaffs2_checkpt_init_chunk_hdr(dev);
//		CheckptInitChunkHeader();
////		return yaffs_checkpt_erase(dev);
//		return CheckptErase();
//	}
//
//	/* Opening for a read */
//	/* Set to a value that will kick off a read */
//	m_dev->checkpt_byte_offs = m_dev->data_bytes_per_chunk;
//	/* A checkpoint block list of 1 checkpoint block per 16 block is
//	 * (hopefully) going to be way more than we need */
//	m_dev->blocks_in_checkpt = 0;
//	m_dev->checkpt_max_blocks = (m_dev->internal_end_block - m_dev->internal_start_block) / 16 + 2;
//	if (!m_dev->checkpt_block_list)
//		//		m_dev->checkpt_block_list =	kmalloc(sizeof(int) * m_dev->checkpt_max_blocks, GFP_NOFS);
//		m_dev->checkpt_block_list = new int[m_dev->checkpt_max_blocks];
//
//	if (!m_dev->checkpt_block_list)		return false;
//
//	for (i = 0; i < m_dev->checkpt_max_blocks; i++)		m_dev->checkpt_block_list[i] = -1;
//
//	return 1;
//}

//bool CYafFs::CheckptSpaceOk(void)
//{
//	int blocks_avail = m_block_manager->GetErasedBlocks() - m_dev->param.n_reserved_blocks;
//	LOG_NOTICE(L"checkpt blocks_avail = %d", blocks_avail);
//	return (blocks_avail > 0);
//}

//void CYafFs::CheckptInitChunkHeader(void)
//{
//	struct yaffs_checkpt_chunk_hdr hdr;
//
//	hdr.version = YAFFS_CHECKPOINT_VERSION;
//	hdr.seq = m_dev->checkpt_page_seq;
//	hdr.sum = m_dev->checkpt_sum;
//	hdr.xor_ = m_dev->checkpt_xor;
//
//	m_dev->checkpt_byte_offs = sizeof(hdr);
//
////	yaffs2_do_endian_hdr(dev, &hdr);
//	memcpy(m_dev->checkpt_buffer, &hdr, sizeof(hdr));
//}

//bool CYafFs::ReadCheckptValidityMarker(int head)
//{
//	struct yaffs_checkpt_validity cp;
//	bool ok;
//
////	ok = (yaffs2_checkpt_rd(dev, &cp, sizeof(cp)) == sizeof(cp));
//	ok = CheckptRead(&cp, sizeof(cp)) == sizeof(cp);
////	yaffs2_do_endian_validity_marker(dev, &cp);
//
//	if (ok)
//	{
//		ok = (cp.struct_type == sizeof(cp)) &&
//			(cp.magic == YAFFS_MAGIC) &&
//			(cp.version == YAFFS_CHECKPOINT_VERSION) &&
//			(cp.head == ((head) ? 1 : 0));
//	}
//	return ok;
//}

//size_t CYafFs::CheckptRead(void * data, size_t n_bytes)
//{
//	size_t i = 0;
//	struct yaffs_ext_tags tags;
//	int chunk;
//	int offset_chunk;
//	BYTE *data_bytes = (BYTE *)data;
//
//	if (!m_dev->checkpt_buffer) 		return 0;
//	if (m_dev->checkpt_open_write)		return -1;
//
//	while (i < n_bytes) 
//	{
//		if (m_dev->checkpt_byte_offs < 0 ||
//			m_dev->checkpt_byte_offs >= (int)m_dev->data_bytes_per_chunk) 
//		{
//			if (m_dev->checkpt_cur_block < 0) 
//			{
////				yaffs2_checkpt_find_block(dev);
//				CheckptFindBlock();
//				m_dev->checkpt_cur_chunk = 0;
//			}
//			/* Bail out if we can't find a checpoint block */
//			if (m_dev->checkpt_cur_block < 0)			break;
//			chunk = m_dev->checkpt_cur_block * m_dev->param.chunks_per_block +
//				m_dev->checkpt_cur_chunk;
//
////			offset_chunk = apply_chunk_offset(dev, chunk);
//			offset_chunk = chunk;
//			m_dev->n_page_reads++;
//
//			/* Read in the next chunk */
//			//m_dev->tagger.read_chunk_tags_fn(dev, offset_chunk,
//			//	m_dev->checkpt_buffer,	&tags);
//			m_tagger->ReadChunkTags(offset_chunk, m_dev->checkpt_buffer, &tags);
//
//			/* Bail out if the chunk is corrupted. */
//			if (tags.chunk_id != (u32)(m_dev->checkpt_page_seq + 1) ||
//				tags.ecc_result > INandDriver::ECC_RESULT_FIXED ||
//				tags.seq_number != YAFFS_SEQUENCE_CHECKPOINT_DATA)
//				break;
//
//			/* Bail out if it is not a checkpoint chunk. */
////			if (!yaffs2_checkpt_check_chunk_hdr(dev))		break;
//			if (!CheckptCheckChunkHeader()) break;
//
//			m_dev->checkpt_page_seq++;
//			m_dev->checkpt_cur_chunk++;
//
//			if (m_dev->checkpt_cur_chunk >=	(int)m_dev->param.chunks_per_block)
//				m_dev->checkpt_cur_block = -1;
//		}
//
//		*data_bytes = m_dev->checkpt_buffer[m_dev->checkpt_byte_offs];
//		m_dev->checkpt_sum += *data_bytes;
//		m_dev->checkpt_xor ^= *data_bytes;
//		m_dev->checkpt_byte_offs++;
//		i++;
//		data_bytes++;
//		m_dev->checkpt_byte_count++;
//	}
//
//	return i; /* Number of bytes read */
//}

//void CYafFs::CheckptFindBlock(void)
//{
//	//u32 i;
//	struct yaffs_ext_tags tags;
//
//	LOG_NOTICE(L"find next checkpt block: start:  blocks %d next %d",
//		m_dev->blocks_in_checkpt, m_dev->checkpt_next_block);
//
//	if (m_dev->blocks_in_checkpt < m_dev->checkpt_max_blocks)
//	{
//		for (UINT32 ii = m_dev->checkpt_next_block; ii <= m_dev->internal_end_block; ii++)
//		{
//			int chunk = ii * m_dev->param.chunks_per_block;
//			enum yaffs_block_state state;
//			u32 seq;
//			m_tagger->ReadChunkTags(chunk, NULL, &tags);
//			LOG_DEBUG_(0, L"find next checkpt block: search: block %d oid %d seq %d eccr %d",
//				ii, tags.obj_id, tags.seq_number, tags.ecc_result);
//			if (tags.seq_number != YAFFS_SEQUENCE_CHECKPOINT_DATA)		continue;
//			m_tagger->QueryBlock(ii, state, seq);
//			//排除bad block
//			if (state == YAFFS_BLOCK_STATE_DEAD)		continue;
//
//			/* Right kind of block */
//			m_dev->checkpt_next_block = tags.obj_id;
//			m_dev->checkpt_cur_block = ii;
//			m_dev->checkpt_block_list[m_dev->blocks_in_checkpt] = ii;
//			m_dev->blocks_in_checkpt++;
//			LOG_DEBUG_(1, L"found checkpt block %d", ii);
//			return;
//		}
//	}
//	LOG_NOTICE(L"found no more checkpt blocks");
//	m_dev->checkpt_next_block = -1;
//	m_dev->checkpt_cur_block = -1;
//}

//bool CYafFs::CheckptCheckChunkHeader(void)
//{
//	struct yaffs_checkpt_chunk_hdr hdr;
//
//	memcpy(&hdr, m_dev->checkpt_buffer, sizeof(hdr));
//	m_dev->checkpt_byte_offs = sizeof(hdr);
//
//	return hdr.version == YAFFS_CHECKPOINT_VERSION &&
//		hdr.seq == m_dev->checkpt_page_seq &&
//		hdr.sum == m_dev->checkpt_sum &&
//		hdr.xor_ == m_dev->checkpt_xor;
//}

bool CYafFs::ReadCheckptDev(CYaffsCheckPoint & checkpt)
{
	//struct yaffs_checkpt_dev cp;
	//u32 n_blocks =	(m_dev->internal_end_block - m_dev->internal_start_block + 1);
	bool ok;

	//ok = m_checkpt->TypedRead(&cp);
	//if (!ok)	return false;
	//if (cp.struct_type != sizeof(cp)) 	return 0;

	//CheckptDevToDev(&cp);

	ok = m_block_manager->ReadFromCheckpt(checkpt, m_dev->n_bg_deletions);
	return ok;
}

//void CYafFs::CheckptDevToDev(yaffs_checkpt_dev * cp)
//{
//	m_dev->n_erased_blocks = cp->n_erased_blocks;
//	m_dev->alloc_block = cp->alloc_block;
//	m_dev->alloc_page = cp->alloc_page;
//	m_dev->n_free_chunks = cp->n_free_chunks;
//	m_dev->n_bg_deletions = cp->n_bg_deletions;
//	m_dev->seq_number = cp->seq_number;
//}

bool CYafFs::CheckptObjToObj(CYaffsObject * obj, yaffs_checkpt_obj * cp)
{
	jcvos::auto_interface<CYaffsObject> parent;
	yaffs_obj_type cp_variant_type = CYaffsObject::CheckptObjBitGet<yaffs_obj_type>(cp, CHECKPOINT_VARIANT_BITS);

	if (obj->GetType() != cp_variant_type) 
	{
		LOG_ERROR(L"[err] Checkpoint read object %d type %d chunk %d does not match existing object type %d",
			cp->obj_id, cp_variant_type, cp->hdr_chunk,
			obj->GetType());
		return false;
	}

	if (cp->parent_id)	FindOrCreateByNumber(parent, cp->parent_id, YAFFS_OBJECT_TYPE_DIRECTORY);
	else				parent = NULL;

	if (parent) 
	{
		if (parent->GetType() != YAFFS_OBJECT_TYPE_DIRECTORY) 
		{
			LOG_ERROR(L"[err] Checkpoint read object %d parent %d type %d chunk %d Parent type, %d, not directory",
				cp->obj_id, cp->parent_id,
				cp_variant_type, cp->hdr_chunk,
				parent->GetType() );
			return false;
		}
		CYaffsDir * pp = parent.d_cast<CYaffsDir*>();
		JCASSERT(pp);
		pp->AddObjToDir(obj);
	}

	//<MIGRAGE> to CYaffsObject::LoadObjectFromCheckpt()
	obj->LoadObjectFromCheckpt(cp);
	return true;
}

bool CYafFs::ReadCheckptTnodes(CYaffsCheckPoint & checkpt, CYaffsObject * obj)
{
	u32 base_chunk;
	bool ok = true;
	CYaffsFile * ff = dynamic_cast<CYaffsFile*>(obj);
	JCASSERT(ff);	//需要load tnode的一定是file
	struct yaffs_tnode *tn;
	int nread = 0;

	ok = m_checkpt->TypedRead(&base_chunk);

	while (ok && (~base_chunk)) 
	{
		nread++;
		/* Read level 0 tnode */
		tn = GetTnode();
		if (tn) ok = (m_checkpt->Read(tn, m_dev->tnode_size) == m_dev->tnode_size);
		else	ok = false;

		if (tn && ok)
		{
			if (ff->AddFindTnode0(base_chunk, tn)) ok = true;
			else ok = false;
		}
		if (ok) ok = m_checkpt->TypedRead(&base_chunk);
	}
	LOG_NOTICE(L"Checkpoint read tnodes %d records, last %d. ok %d", nread, base_chunk, ok);
	return ok;
}

bool CYafFs::ReadCheckptObjects(CYaffsCheckPoint & checkpt)
{
	struct yaffs_checkpt_obj cp;
	bool ok = true, done = false;
	yaffs_obj_type cp_variant_type;

	while (ok && !done) 
	{
		m_checkpt->TypedRead(&cp);
		if (!ok || cp.struct_type != sizeof(cp)) 
		{
			LOG_ERROR(L"[err] struct size %d instead of %d ok %d", cp.struct_type, (int)sizeof(cp), ok);
			ok = false;
		}
		cp_variant_type = CYaffsObject::CheckptObjBitGet<yaffs_obj_type>(&cp, CHECKPOINT_VARIANT_BITS);
		LOG_NOTICE(L"read checkpt obj id=%X,parent=%X,type=%d,chunk=%d",
			cp.obj_id, cp.parent_id, cp_variant_type,		cp.hdr_chunk);

		// 是否会因为data loss, 无法读取到guard object而死循环
		if (ok && cp.obj_id == (u32)(~0)) done = true;
		else if (ok) 
		{
			jcvos::auto_interface<CYaffsObject> obj;
			FindOrCreateByNumber(obj, cp.obj_id, cp_variant_type);
			if (obj) 
			{
				ok = CheckptObjToObj(obj, &cp);
				if (!ok)	break;
				if (obj->GetType() == YAFFS_OBJECT_TYPE_FILE) 	ok = ReadCheckptTnodes(checkpt, obj);
			}
			else ok = false;
		}
	}
	return ok;
}

//bool CYafFs::ReadCheckptSum(CYaffsCheckPoint & checkpt)
//{
//	UINT32 checkpt_sum1;
//	bool ok;
//	UINT32 checkpt_sum0 = (m_dev->checkpt_sum << 8) | (m_dev->checkpt_xor & 0xff);
//	ok = m_checkpt->TypedRead(&checkpt_sum1);
////	ok = CheckptRead(&checkpt_sum1, sizeof(checkpt_sum1)) == sizeof(checkpt_sum1);
//	if (!ok)	return false;
//	if (checkpt_sum0 != checkpt_sum1)
//	{
//		LOG_ERROR(L"[err] Wrong checksum %d, expected %d", checkpt_sum1, checkpt_sum0);
//		return false;
//	}
//	return true;
//}

//bool CYafFs::CheckptClose(void)
//{
//	//u32 i;
//
//	if (m_dev->checkpt_open_write) 
//	{
//		if (m_dev->checkpt_byte_offs != sizeof(sizeof(struct yaffs_checkpt_chunk_hdr)))
//			//			yaffs2_checkpt_flush_buffer(dev);
//			CheckptFlushBuffer();
//	}
//	else if (m_dev->checkpt_block_list) 
//	{
//		for (UINT32 ii = 0; ii < m_dev->blocks_in_checkpt && m_dev->checkpt_block_list[ii] >= 0; ii++) 
//		{
//			int blk = m_dev->checkpt_block_list[ii];
//			struct yaffs_block_info *bi = NULL;
//			if ((int)m_dev->internal_start_block <= blk && blk <= (int)m_dev->internal_end_block)
//				//				bi = yaffs_get_block_info(dev, blk);
//				bi = GetBlockInfo(blk);
//			if (bi && bi->block_state == YAFFS_BLOCK_STATE_EMPTY)
//				bi->block_state = YAFFS_BLOCK_STATE_CHECKPOINT;
//		}
//	}
//
//	m_dev->n_free_chunks -= m_dev->blocks_in_checkpt * m_dev->param.chunks_per_block;
//	m_dev->n_erased_blocks -= m_dev->blocks_in_checkpt;
//
//	LOG_NOTICE(L"checkpoint byte count %d", m_dev->checkpt_byte_count);
//
//	if (m_dev->checkpt_buffer)	return true;
//	else		return false;
//}

int CYafFs::CalcCheckptBlocksRequired(void)
{
	//int retval;
	//int n_bytes = 0;
	//int n_blocks;
	//int dev_blocks;

	//if (!m_dev->param.is_yaffs2)	return 0;

	//if (!m_dev->checkpoint_blocks_required && yaffs2_checkpt_required(dev)) 
	if (/*!m_dev->checkpoint_blocks_required &&*/ CheckptRequired())
	{	/* Not a valid value so recalculate */
		//int dev_blocks = m_dev->param.end_block - m_dev->param.start_block + 1;
		//n_bytes += sizeof(struct yaffs_checkpt_validity);
		//n_bytes += sizeof(struct yaffs_checkpt_dev);
		//n_bytes += dev_blocks * sizeof(struct yaffs_block_info);
		//n_bytes += dev_blocks * m_dev->chunk_bit_stride;
		//n_bytes += (sizeof(struct yaffs_checkpt_obj) + sizeof(u32)) * m_dev->n_obj;
		//n_bytes += (m_dev->tnode_size + sizeof(u32)) * m_dev->n_tnodes;
		//n_bytes += sizeof(struct yaffs_checkpt_validity);
		//n_bytes += sizeof(u32);	/* checksum */

		///* Round up and add 2 blocks to allow for some bad blocks, so add 3 */
		//n_blocks = (n_bytes / (m_dev->data_bytes_per_chunk * m_dev->param.chunks_per_block)) + 3;
		UINT32 tnode_size = (m_dev->tnode_size + sizeof(u32)) * m_dev->n_tnodes;
//		m_dev->checkpoint_blocks_required = n_blocks;
		return m_checkpt->CalcCheckptBlockRequired(m_dev->n_obj, tnode_size);
	}
	else return 0;
	//retval = m_dev->checkpoint_blocks_required - m_dev->blocks_in_checkpt;
	//if (retval < 0) retval = 0;
	//return retval;


}

bool CYafFs::CheckptRequired(void)
{
//	int nblocks;
//	nblocks = m_dev->internal_end_block - m_dev->internal_start_block + 1;
	UINT32 blocks = m_block_manager->GetBlockNumber();
	return !m_dev->param.skip_checkpt_wr && !m_dev->read_only && (blocks >= YAFFS_CHECKPOINT_MIN_BLOCKS);
}

bool CYafFs::WriteCheckptData(void)
{
	bool ok = true;
	//m_dev->is_checkpointed = false;

	if (!CheckptRequired())
	{
		LOG_NOTICE(L"skipping checkpoint write");
		return false;
	}


	//if (ok) ok = yaffs2_checkpt_open(dev, 1);
	//ok = CheckptOpen(true);
//	CYaffsCheckPoint checkpt(m_dev->param.total_bytes_per_chunk, m_tagger);
//	UINT32 max_block = (m_dev->internal_end_block - m_dev->internal_start_block) / 16 + 2;
	ok = m_checkpt->Open(true);
	if (!ok)	ERROR_HANDLE(return false, L"[err] failed on opening checkpt");

	LOG_DEBUG(L"write checkpoint validity");
//	ok = WriteCheckptValidityMarker(1);
	ok = m_checkpt->WriteValidityMarker(1);
	if (!ok)	ERROR_HANDLE(return false, L"[err] failed on writing checkpt validity 1");
	
	LOG_DEBUG(L"write checkpoint device");
//	ok = yaffs2_wr_checkpt_dev(dev);
	ok = WriteCheckptDev(*m_checkpt);
	if (!ok)	ERROR_HANDLE(return false, L"[err] failed on writing checkpt dev");
	LOG_DEBUG(L"write checkpoint objects");
//	ok = yaffs2_wr_checkpt_objs(dev);
	ok = WriteCheckptObjs(*m_checkpt);
	if (!ok)	ERROR_HANDLE(return false, L"[err] failed on writing checkpt obj");

	//if (ok) {
	LOG_DEBUG(L"write checkpoint validity");
//	ok = yaffs2_wr_checkpt_validity_marker(dev, 0);
	//ok= WriteCheckptValidityMarker(0);
	ok = m_checkpt->WriteValidityMarker(0);
	if (!ok)	ERROR_HANDLE(return false, L"[err] failed on writing checkpt validity 0");
//	ok = yaffs2_wr_checkpt_sum(dev);
	ok = m_checkpt->WriteSum();
	if (!ok)	ERROR_HANDLE(return false, L"[err] failed on writing checkpt sum");
//	ok = f (!yaffs_checkpt_close(dev))
	ok = m_checkpt->Close();
	if (!ok)	ERROR_HANDLE(return false, L"[err] failed on closing checkpt");
	//if (ok)
	//	m_dev->is_checkpointed = true;
	//else
	//	m_dev->is_checkpointed = 0;

	//m_dev->is_checkpointed = ok;
	return m_checkpt->IsCheckpointed();
}


//bool CYafFs::WriteCheckptValidityMarker(int head)
//{
//	struct yaffs_checkpt_validity cp;
//
//	memset(&cp, 0, sizeof(cp));
//
//	cp.struct_type = sizeof(cp);
//	cp.magic = YAFFS_MAGIC;
//	cp.version = YAFFS_CHECKPOINT_VERSION;
//	cp.head = (head) ? 1 : 0;
//
//	//yaffs2_do_endian_validity_marker(dev, &cp);
//
//	return (CheckptWrite(&cp, sizeof(cp)) == sizeof(cp));
//}
//
bool CYafFs::WriteCheckptDev(CYaffsCheckPoint & checkpt)
{
	//struct yaffs_checkpt_dev cp;
	//u32 n_bytes;
	//u32 n_blocks = m_dev->internal_end_block - m_dev->internal_start_block + 1;
	bool ok;

	/* Write device runtime values */
	//DevToCheckptDev(&cp);
	//ok = (CheckptWrite(&cp, sizeof(cp)) == sizeof(cp));
	//if (!ok) return false;



	/* Write block info. */
	//if (!m_dev->swap_endian) {
		//n_bytes = n_blocks * sizeof(struct yaffs_block_info);
		//ok = (CheckptWrite(m_dev->block_info, n_bytes) ==
		//	(int)n_bytes);
	//}
	//else {
		/*
		 * Need to swap the endianisms. We can't do this in place since that would damage live data,
		 * so write one block info at a time using a copy. */
		//for (i = 0; i < n_blocks && ok; i++) {
		//	bu.bi = m_dev->block_info[i];
		//	bu.as_u32[0] = swap_u32(bu.as_u32[0]);
		//	bu.as_u32[1] = swap_u32(bu.as_u32[1]);
		//	ok = (CheckptWrite(&bu, sizeof(bu)) == sizeof(bu));
		//}
		// dunot support edian
	//	JCASSERT(0);
	//}

	//if (!ok) return 0;

	///*
	// * Write chunk bits. Chunk bits are in bytes so
	// * no endian conversion is needed.
	// */
	//n_bytes = n_blocks * m_dev->chunk_bit_stride;
	//ok = (CheckptWrite(m_dev->chunk_bits, n_bytes) == n_bytes);

	ok = m_block_manager->WriteToCheckpt(checkpt, m_dev->n_bg_deletions);
	return ok;
}

bool CYafFs::WriteCheckptObjs(CYaffsCheckPoint & checkpt)
{
	struct yaffs_checkpt_obj cp;
	int i;
	bool ok = 1;
	//struct list_head *lh;
	u32 cp_variant_type;

	/* Iterate through the objects in each hash entry,
	 * dumping them to the checkpointing stream.	 */

	for (i = 0; ok && i < YAFFS_NOBJECT_BUCKETS; i++) 
	{
		std::list<CYaffsObject*> & list = m_obj_bucket[i];
		for (auto it=list.begin(); it!=list.end(); ++it)
		{
			CYaffsObject * obj = *it;
			//if (!obj->defered_free) 
			if (!obj->IsDeferedFree())
			{
				obj->ObjToCheckptObj(&cp);
				cp.struct_type = sizeof(cp);
				cp_variant_type = CYaffsObject::CheckptObjBitGet<yaffs_obj_type>(&cp, CHECKPOINT_VARIANT_BITS);
				LOG_DEBUG(L"write checkpt obj id=%X,name=%s,parent=%d,type=%d,chunk=%d,obj addr=%p",
					cp.obj_id, obj->GetFileName().c_str(), cp.parent_id, cp_variant_type, cp.hdr_chunk, obj);

				//yaffs2_do_endian_checkpt_obj(dev, &cp);
				ok = m_checkpt->TypedWrite(&cp);
//				(CheckptWrite(&cp, sizeof(cp)) == sizeof(cp));

				if (ok && obj->GetType() == YAFFS_OBJECT_TYPE_FILE)
				{
					CYaffsFile * ff = dynamic_cast<CYaffsFile*>(obj);
					JCASSERT(ff);
					ok = ff->WriteCheckptTnodes(*m_checkpt);
				}
			}
		}
	}

	/* Dump end of list */
	memset(&cp, 0xff, sizeof(struct yaffs_checkpt_obj));
	cp.struct_type = sizeof(cp);
	//yaffs2_do_endian_checkpt_obj(dev, &cp);

	if (ok) ok = m_checkpt->TypedWrite(&cp);
//	(CheckptWrite(&cp, sizeof(cp)) == sizeof(cp));

	return ok;
}


//bool CYafFs::WriteCheckptSum(CYaffsCheckPoint & checkpt)
//{
//	bool ok;
//	//	yaffs2_get_checkpt_sum(); 直接展开
//	UINT32 checkpt_sum = (m_dev->checkpt_sum << 8) | (m_dev->checkpt_xor & 0xff);
//	//yaffs_do_endian_u32(dev, &checkpt_sum);
//	ok = m_checkpt->TypedWrite(&checkpt_sum);
////	, sizeof(checkpt_sum)) == sizeof(checkpt_sum));
//	return ok;
//}

//size_t CYafFs::CheckptWrite(const void * data, size_t n_bytes)
//{
//	//LOG_STACK_TRACE();
//	bool ok = 1;
//	u8 *data_bytes = (u8 *)data;
//
//	if (!m_dev->checkpt_buffer)
//		return 0;
//
//	if (!m_dev->checkpt_open_write)
//		return -1;
//
//	size_t ii = 0;
//	while (ii < n_bytes && ok) 
//	{
//		m_dev->checkpt_buffer[m_dev->checkpt_byte_offs] = *data_bytes;
//		m_dev->checkpt_sum += *data_bytes;
//		m_dev->checkpt_xor ^= *data_bytes;
//
//		m_dev->checkpt_byte_offs++;
//		ii++;
//		data_bytes++;
//		m_dev->checkpt_byte_count++;
//
//		if (m_dev->checkpt_byte_offs < 0 || m_dev->checkpt_byte_offs >= (int)m_dev->data_bytes_per_chunk)
//			ok = CheckptFlushBuffer();
//	}
//	return ii;
//}

//bool CYafFs::CheckptFlushBuffer(void)
//{
//	//LOG_STACK_TRACE();
//	int chunk;
//	int offset_chunk;
//	struct yaffs_ext_tags tags;
//
//	if (m_dev->checkpt_cur_block < 0) 
//	{
//		CheckptFindErasedBlock();
//		m_dev->checkpt_cur_chunk = 0;
//		LOG_DEBUG(L"found checkpt block %d", m_dev->checkpt_cur_block);
//	}
//
//	if (m_dev->checkpt_cur_block < 0)		return false;
//
//	//tags.is_deleted = 0;
//	tags.obj_id = m_dev->checkpt_next_block;	/* Hint to next place to look */
//	tags.chunk_id = m_dev->checkpt_page_seq + 1;
//	tags.seq_number = YAFFS_SEQUENCE_CHECKPOINT_DATA;
//	tags.n_bytes = m_dev->data_bytes_per_chunk;
//	if (m_dev->checkpt_cur_chunk == 0) 
//	{	/* First chunk we write for the block? Set block state to checkpoint */
//		yaffs_block_info *bi = GetBlockInfo(m_dev->checkpt_cur_block);
//			//yaffs_get_block_info(dev, m_dev->checkpt_cur_block);
//		bi->block_state = YAFFS_BLOCK_STATE_CHECKPOINT;
//		m_dev->blocks_in_checkpt++;
//	}
//
//	chunk =	m_dev->checkpt_cur_block * m_dev->param.chunks_per_block + m_dev->checkpt_cur_chunk;
//
//	LOG_NOTICE(L"checkpoint wite buffer nand %d(%d:%d) objid %d chId %d",
//		chunk, m_dev->checkpt_cur_block, m_dev->checkpt_cur_chunk,
//		tags.obj_id, tags.chunk_id);
//
////	offset_chunk = apply_chunk_offset(dev, chunk);
//	offset_chunk = chunk;
//
//	m_dev->n_page_writes++;
//
//	//m_dev->tagger.write_chunk_tags_fn(dev, offset_chunk,
//	//	m_dev->checkpt_buffer, &tags);
//	m_tagger->WriteChunkTags(offset_chunk, m_dev->checkpt_buffer, &tags);
//	m_dev->checkpt_page_seq++;
//	m_dev->checkpt_cur_chunk++;
//	if (m_dev->checkpt_cur_chunk >= (int)m_dev->param.chunks_per_block) 
//	{
//		m_dev->checkpt_cur_chunk = 0;
//		m_dev->checkpt_cur_block = -1;
//	}
//	memset(m_dev->checkpt_buffer, 0, m_dev->data_bytes_per_chunk);
//
//	//yaffs2_checkpt_init_chunk_hdr(dev);
//	CheckptInitChunkHeader();
//	return true;
//}

//void CYafFs::CheckptFindErasedBlock(void)
//{
////	u32 i;
//	int blocks_avail = m_block_manager->GetErasedBlocks() - m_dev->param.n_reserved_blocks;
//
//	LOG_NOTICE(L"allocating checkpt block: erased %d reserved %d avail %d next %d ",
//		m_block_manager->GetErasedBlocks(), m_dev->param.n_reserved_blocks,
//		blocks_avail, m_dev->checkpt_next_block);
//
//	if (m_dev->checkpt_next_block >= 0 &&
//		m_dev->checkpt_next_block <= (int)m_dev->internal_end_block &&
//		blocks_avail > 0) 
//	{
//		for (UINT32 ii = m_dev->checkpt_next_block; ii <= m_dev->internal_end_block;
//			ii++) 
//		{
//			yaffs_block_info *bi = GetBlockInfo(ii);
//			//bi = yaffs_get_block_info(dev, ii);
//			if (bi->block_state == YAFFS_BLOCK_STATE_EMPTY) 
//			{
//				m_dev->checkpt_next_block = ii + 1;
//				m_dev->checkpt_cur_block = ii;
//				LOG_NOTICE(L"allocating checkpt block %d", ii);
//				return;
//			}
//		}
//	}
//	LOG_TRACE(L"out of checkpt blocks");
//
//	m_dev->checkpt_next_block = -1;
//	m_dev->checkpt_cur_block = -1;
//}


//void CYafFs::DevToCheckptDev(struct yaffs_checkpt_dev *cp)
//{
//	cp->struct_type = sizeof(*cp);
//
//	cp->n_erased_blocks = m_dev->n_erased_blocks;
//	cp->alloc_block = m_dev->alloc_block;
//	cp->alloc_page = m_dev->alloc_page;
//	cp->n_free_chunks = m_dev->n_free_chunks;
//
//	cp->n_deleted_files = 0;
//	cp->n_unlinked_files = 0;
//	cp->n_bg_deletions = m_dev->n_bg_deletions;
//	cp->seq_number = m_dev->seq_number;
//
//}


bool CYafFs::FindOrCreateByNumber(CYaffsObject *& out_obj, UINT32 obj_id, yaffs_obj_type type)
{
	//bool br;
	JCASSERT(out_obj == NULL);
	if (obj_id > 0)	FindByNumber(out_obj, obj_id);
	if (out_obj)
	{
		LOG_DEBUG(L"find object, id=0x%X,obj=%p", obj_id, out_obj);
	}
	else
	{	// NewObject中调用HashObj会把object放入obj_bucket中。
		CYaffsObject::NewObject(out_obj, this, obj_id, type);
		LOG_DEBUG(L"create object, id=0x%X,obj=%p", obj_id, out_obj);
	}
	return true;
}

bool CYafFs::FindByNumber(CYaffsObject *& out_obj, UINT32 obj_id)
{
	JCASSERT(out_obj == NULL);
	int bucket = yaffs_hash_fn(obj_id);

	std::list<CYaffsObject*> & list = m_obj_bucket[bucket];
	for (auto it =list.begin(); it!=list.end(); ++it)
	{	/* Look if it is in the list */
		CYaffsObject * obj = *it;
		if (obj->GetObjectId() == obj_id) 
		{	/* Don't show if it is defered free */
			if (obj->IsDeferedFree()) return false;
			out_obj = obj;
			out_obj->AddRef();
			return true;
		}
	}
	return false;
}

void CYafFs::HashObject(CYaffsObject * obj)
{
	JCASSERT(obj);
	LOG_STACK_TRACE_EX(L"add obj, id=%d,obj=%p", obj->GetObjectId(), obj);
	int bucket = yaffs_hash_fn(obj->GetObjectId());
	JCASSERT(bucket < YAFFS_NOBJECT_BUCKETS);
	m_obj_bucket[bucket].push_back(obj);
	obj->AddRef();
	m_dev->n_obj++;
}

void CYafFs::UnhashObject(CYaffsObject * obj)
{
	int bucket = yaffs_hash_fn(obj->GetObjectId());
	/* If it is still linked into the bucket list, free from the list */
	auto it = std::find(m_obj_bucket[bucket].begin(), m_obj_bucket[bucket].end(), obj);
	if (it != m_obj_bucket[bucket].end())
	{
		m_obj_bucket[bucket].erase(it);
		obj->Release();
		m_dev->n_obj--;
		//m_dev->checkpoint_blocks_required = 0;
	}
}


int CYafFs::NewObjectId(void)
{
//	int bucket = yaffs_find_nice_bucket(dev);
	int bucket = FindNiceBucket();
	int found = 0;
	//struct list_head *i;
	u32 n = (u32)bucket;

	/* Now find an object value that has not already been taken
	 * by scanning the list, incrementing each time by number of buckets.	 */
	while (!found) 
	{
		found = 1;
		n += YAFFS_NOBJECT_BUCKETS;
//		list_for_each(i, &dev->obj_bucket[bucket].list) {
		for (auto it=m_obj_bucket[bucket].begin(); it!=m_obj_bucket[bucket].end(); ++it)
		{		/* Check if this value is already taken. */
			CYaffsObject *obj = (*it);
			JCASSERT(obj);
//			if (i && list_entry(i, struct yaffs_obj,
//				hash_link)->obj_id == n)
			if (obj->GetObjectId() == n)		found = 0;
		}
	}
	return n;
}

int CYafFs::FindNiceBucket(void)
{
	int i;
	int l = 999;
	size_t lowest = 999999;

	/* Search for the shortest list or one that isn't too long. */

	for (i = 0; i < 10 && lowest > 4; i++) 
	{
		m_dev->bucket_finder++;
		m_dev->bucket_finder %= YAFFS_NOBJECT_BUCKETS;
//		if (m_dev->obj_bucket[dev->bucket_finder].count < lowest) 
		if (m_obj_bucket[m_dev->bucket_finder].size() < lowest)
		{
			lowest = m_obj_bucket[m_dev->bucket_finder].size();
			l = m_dev->bucket_finder;
		}
	}
	return l;
}

bool CYafFs::SummaryRead(yaffs_summary_tags * st, int blk)
{
	struct yaffs_ext_tags tags;
	u8 *sum_buffer = (u8 *)st;
	bool result;
	int this_tx;
	struct yaffs_summary_header hdr;

	yaffs_block_info *bi = m_block_manager->GetBlockInfo(blk);
	int sum_bytes_per_chunk = m_dev->data_bytes_per_chunk - sizeof(hdr);

	BYTE * buffer = GetTempBuffer();
	size_t n_bytes = sizeof(struct yaffs_summary_tags) * m_dev->chunks_per_summary;
	int chunk_in_block = m_dev->chunks_per_summary;
	int chunk_in_nand = blk * m_dev->param.chunks_per_block + m_dev->chunks_per_summary;
	UINT32 chunk_id = 1;

	do
	{
		this_tx = boost::numeric_cast<int>(n_bytes);
		if (this_tx > sum_bytes_per_chunk)		this_tx = sum_bytes_per_chunk;
		result = ReadChunkTagsNand(chunk_in_nand, buffer, &tags);

		if (tags.chunk_id != chunk_id ||
			tags.obj_id != YAFFS_OBJECTID_SUMMARY ||
			tags.chunk_used == 0 ||
			tags.ecc_result > INandDriver::ECC_RESULT_FIXED ||
			tags.n_bytes != (this_tx + sizeof(hdr)))
			result = false;
		if (!result) break;

		if (st == m_dev->sum_tags)
		{	/* If we're scanning then update the block info */
			m_block_manager->SetChunkBit(blk, chunk_in_block);
			bi->pages_in_use++;
		}
		memcpy(&hdr, buffer, sizeof(hdr));
		memcpy(sum_buffer, buffer + sizeof(hdr), this_tx);
		n_bytes -= this_tx;
		sum_buffer += this_tx;
		chunk_in_nand++;
		chunk_in_block++;
		chunk_id++;
	} while (result == true && n_bytes > 0);
	ReleaseTempBuffer(buffer);

	if (result) 
	{	/* Verify header */
		if (hdr.version != YAFFS_SUMMARY_VERSION ||
			hdr.seq != bi->seq_number ||
			hdr.sum != SummarySum())
			result = false;
	}
	if (st == m_dev->sum_tags && result == true)	bi->has_summary = 1;
	return result;
}

UINT32 CYafFs::SummarySum(void)
{
	BYTE *sum_buffer = (BYTE *)m_dev->sum_tags;
	int i;
	UINT32 sum = 0;

	i = sizeof(struct yaffs_summary_tags) *	m_dev->chunks_per_summary;
	while (i > 0) 
	{
		sum += *sum_buffer;
		sum_buffer++;
		i--;
	}
	return sum;
}

void CYafFs::PackTags2TagsOnly(yaffs_packed_tags2_tags_only * ptt, yaffs_ext_tags * t)
{
	ptt->chunk_id = t->chunk_id;
	ptt->seq_number = t->seq_number;
	ptt->n_bytes = t->n_bytes;
	ptt->obj_id = t->obj_id;

	/* Only store extra tags for object headers.
	 * If it is a file then only store  if the file size is short\
	 * enough to fit.  */
	if (CheckTagsExtraPackable(t))
	{
		/* Store the extra header info instead */
		/* We save the parent object in the chunk_id */
		ptt->chunk_id = EXTRA_HEADER_INFO_FLAG | t->extra_parent_id;
		if (t->extra_is_shrink) ptt->chunk_id |= EXTRA_SHRINK_FLAG;
		if (t->extra_shadows) ptt->chunk_id |= EXTRA_SHADOWS_FLAG;

		ptt->obj_id &= ~EXTRA_OBJECT_TYPE_MASK;
		ptt->obj_id |= (t->extra_obj_type << EXTRA_OBJECT_TYPE_SHIFT);

		if (t->extra_obj_type == YAFFS_OBJECT_TYPE_HARDLINK)	ptt->n_bytes = t->extra_equiv_id;
		else if (t->extra_obj_type == YAFFS_OBJECT_TYPE_FILE)	ptt->n_bytes = (unsigned)t->extra_file_size;
		else	ptt->n_bytes = 0;
	}

	DumpPackedTags2TagsOnly(ptt);
	DumpTags2(t);
//	yaffs_do_endian_packed_tags2(dev, ptt);
}

void CYafFs::DumpPackedTags2TagsOnly(const struct yaffs_packed_tags2_tags_only *ptt)
{
	LOG_NOTICE(L"packed tags obj %d chunk %d byte %d seq %d",
		ptt->obj_id, ptt->chunk_id, ptt->n_bytes, ptt->seq_number);
}

//static void yaffs_dump_packed_tags2(const struct yaffs_packed_tags2 *pt)
//{
//	DumpPackedTags2TagsOnly(&pt->t);
//}

void CYafFs::DumpTags2(const struct yaffs_ext_tags *t)
{
	LOG_NOTICE(L"ext.tags eccres %d blkbad %d chused %d obj %d chunk%d byte %d del %d ser %d seq %d",
		t->ecc_result, t->block_bad, t->chunk_used, t->obj_id,
		t->chunk_id, t->n_bytes, 0,
		//t->is_deleted, 
		t->serial_number,
		t->seq_number);

}

bool CYafFs::CheckTagsExtraPackable(const struct yaffs_ext_tags *t)
{
	if (t->chunk_id != 0 || !t->extra_available)	return false;
	/* Check if the file size is too long to store */
	if (t->extra_obj_type == YAFFS_OBJECT_TYPE_FILE && (t->extra_file_size >> 31) != 0)
		return false;
	return true;
}

#if 0
void PackTags2(struct yaffs_dev *dev,
	struct yaffs_packed_tags2 *pt,
	const struct yaffs_ext_tags *t, int tags_ecc)
{
	yaffs_pack_tags2_tags_only(dev, &pt->t, t);

	if (tags_ecc)
		yaffs_ecc_calc_other((unsigned char *)&pt->t,
			sizeof(struct yaffs_packed_tags2_tags_only),
			&pt->ecc);
}
#endif

void CYafFs::UnpackTags2TagsOnly(struct yaffs_ext_tags *t, struct yaffs_packed_tags2_tags_only *ptt_ptr)
{
	struct yaffs_packed_tags2_tags_only ptt_copy = *ptt_ptr;

	memset(t, 0, sizeof(struct yaffs_ext_tags));

	if (ptt_copy.seq_number == 0xffffffff)
		return;

//	yaffs_do_endian_packed_tags2(dev, &ptt_copy);

	t->block_bad = 0;
	t->chunk_used = 1;
	t->obj_id = ptt_copy.obj_id;
	t->chunk_id = ptt_copy.chunk_id;
	t->n_bytes = ptt_copy.n_bytes;
	//t->is_deleted = 0;
	t->serial_number = 0;
	t->seq_number = ptt_copy.seq_number;

	/* Do extra header info stuff */
	if (ptt_copy.chunk_id & EXTRA_HEADER_INFO_FLAG) {
		t->chunk_id = 0;
		t->n_bytes = 0;

		t->extra_available = 1;
		t->extra_parent_id = ptt_copy.chunk_id & (~(ALL_EXTRA_FLAGS));
		t->extra_is_shrink = ptt_copy.chunk_id & EXTRA_SHRINK_FLAG ? 1 : 0;
		t->extra_shadows = ptt_copy.chunk_id & EXTRA_SHADOWS_FLAG ? 1 : 0;
		t->extra_obj_type = (yaffs_obj_type)(ptt_copy.obj_id >> EXTRA_OBJECT_TYPE_SHIFT);
		t->obj_id &= ~EXTRA_OBJECT_TYPE_MASK;

		if (t->extra_obj_type == YAFFS_OBJECT_TYPE_HARDLINK)
			t->extra_equiv_id = ptt_copy.n_bytes;
		else	t->extra_file_size = ptt_copy.n_bytes;
	}
	DumpPackedTags2TagsOnly(ptt_ptr);
	DumpTags2(t);
}

#if 0
void UnpackTags2(struct yaffs_dev *dev,
	struct yaffs_ext_tags *t,
	struct yaffs_packed_tags2 *pt,
	int tags_ecc)
{
	enum INandDriver::ECC_RESULT ecc_result = INandDriver::ECC_RESULT_NO_ERROR;

	if (pt->t.seq_number != 0xffffffff && tags_ecc) {
		/* Chunk is in use and we need to do ECC */

		struct yaffs_ecc_other ecc;
		int result;
		yaffs_ecc_calc_other((unsigned char *)&pt->t,
			sizeof(struct yaffs_packed_tags2_tags_only),
			&ecc);
		result =
			yaffs_ecc_correct_other((unsigned char *)&pt->t,
				sizeof(struct yaffs_packed_tags2_tags_only),
				&pt->ecc, &ecc);
		switch (result) {
		case 0:
			ecc_result = INandDriver::ECC_RESULT_NO_ERROR;
			break;
		case 1:
			ecc_result = INandDriver::ECC_RESULT_FIXED;
			break;
		case -1:
			ecc_result = INandDriver::ECC_RESULT_UNFIXED;
			break;
		default:
			ecc_result = INandDriver::ECC_RESULT_UNKNOWN;
		}
	}
	UnpackTags2TagsOnly(dev, t, &pt->t);

	t->ecc_result = ecc_result;

	yaffs_dump_packed_tags2(pt);
	DumpTags2(t);
}
#endif

bool CYafFs::SummaryWrite(int blk)
{
	struct yaffs_ext_tags tags;
	u8 *buffer;
	//int n_bytes;
	int chunk_in_nand;
	int chunk_in_block;
	bool result;
	int this_tx;
	struct yaffs_summary_header hdr;
	// summary所占用的大小，出去header
	int sum_bytes_per_chunk = m_dev->data_bytes_per_chunk - sizeof(hdr);
	yaffs_block_info *bi = m_block_manager->GetBlockInfo(blk);

	buffer = GetTempBuffer();
	// total byte to write
	size_t n_bytes = sizeof(struct yaffs_summary_tags) * m_dev->chunks_per_summary;
	memset(&tags, 0, sizeof(struct yaffs_ext_tags));
	tags.obj_id = YAFFS_OBJECTID_SUMMARY;
	tags.chunk_id = 1;
	chunk_in_block = m_dev->chunks_per_summary;
	chunk_in_nand = m_block_manager->GetAllocBlocks() * m_dev->param.chunks_per_block + m_dev->chunks_per_summary;
	hdr.version = YAFFS_SUMMARY_VERSION;
	hdr.block = blk;
	hdr.seq = bi->seq_number;
	hdr.sum = SummarySum();

	BYTE *sum_buffer = (BYTE *)m_dev->sum_tags;
	do 
	{
		this_tx = boost::numeric_cast<int>(n_bytes);
		if (this_tx > sum_bytes_per_chunk)	this_tx = sum_bytes_per_chunk;
		memcpy_s(buffer, m_dev->data_bytes_per_chunk, &hdr, sizeof(hdr));
		memcpy_s(buffer + sizeof(hdr), sum_bytes_per_chunk,  sum_buffer, this_tx);
		tags.n_bytes = this_tx + sizeof(hdr);
		result = WriteChunkTagsNand(chunk_in_nand, buffer, &tags);
		if (result != true)			break;
		m_block_manager->SetChunkBit(blk, chunk_in_block);

		bi->pages_in_use++;
//		m_dev->n_free_chunks--;
		m_block_manager->BlockUsed(0, 1);

		n_bytes -= this_tx;
		sum_buffer += this_tx;
		chunk_in_nand++;
		chunk_in_block++;
		tags.chunk_id++;
	} while (result == true && n_bytes > 0);

	ReleaseTempBuffer(buffer);
	if (result == true)	bi->has_summary = 1;
	return result;
}

void CYafFs::SummaryClear(void)
{
	if (!m_dev->sum_tags)		return;
	memset(m_dev->sum_tags, 0, m_dev->chunks_per_summary *
		sizeof(struct yaffs_summary_tags));

}

UINT32 CYafFs::GetNFreeChunks(void)
{
//	/* This is what we report to the outside world */
//	int n_free;
	UINT32 n_dirty_caches=0;
//	int blocks_for_checkpt;
//	u32 i;
//
//	n_free = m_dev->n_free_chunks;
////	n_free += m_dev->n_deleted_files;
//
//	/* Now count and subtract the number of dirty chunks in the cache. */
	for (UINT32 ii = 0; ii < m_dev->param.n_caches; ii++) 
	{
		if (m_dev->cache[ii].dirty)	n_dirty_caches++;
	}
//	n_free -= n_dirty_caches;
//	n_free -= ((m_dev->param.n_reserved_blocks + 1) * m_dev->param.chunks_per_block);
//	/* Now figure checkpoint space and report that... */
//	blocks_for_checkpt = CalcCheckptBlocksRequired();
//	n_free -= (blocks_for_checkpt * m_dev->param.chunks_per_block);
//
//	if (n_free < 0) n_free = 0;
//
//	return n_free;

	UINT32 blocks_for_checkpt =  CalcCheckptBlocksRequired();
	return m_block_manager->GetNFreeChunks(n_dirty_caches, blocks_for_checkpt);
}




#ifdef UNDER_MIGRATING
yaffs_dev * CYafFs::FindMountPoint(const YCHAR * path)
{
	struct yaffs_dev *dev;
	char *restOfPath = NULL;

	dev = FindDevice(path, &restOfPath);
	if (dev && restOfPath && *restOfPath)		dev = NULL;
	return dev;
}

yaffs_dev * CYafFs::FindDevice(const YCHAR * path, YCHAR ** restOfPath)
{
	struct list_head *cfg;
	const YCHAR *leftOver;
	const YCHAR *p;
	struct yaffs_dev *retval = NULL;
	struct yaffs_dev *dev = NULL;
	int thisMatchLength;
	int longestMatch = -1;
	int matching;

	/*
	 * Check all configs, choose the one that:
	 * 1) Actually matches a prefix (ie /a amd /abc will not match
	 * 2) Matches the longest.
	 */
	for (cfg = m_deviceList.next; cfg != (&m_deviceList); cfg = cfg->next)
	{
		dev = list_entry(cfg, struct yaffs_dev, dev_list);
		leftOver = path;
		p = m_dev->param.name;
		thisMatchLength = 0;
		matching = 1;
		if (!p)		continue;

		/* Skip over any leading  /s */
		while (IsPathDivider(*p))				p++;
		while (IsPathDivider(*leftOver))		leftOver++;

		while (matching && *p && *leftOver) 
		{
			/* Now match the text part */
			while (matching &&	*p && !IsPathDivider(*p) &&
					*leftOver && !IsPathDivider(*leftOver)) 
			{
				if (Match(*p, *leftOver)) 
				{
					p++;
					leftOver++;
					thisMatchLength++;
				}
				else {	matching = 0;		}
			}
			if ((*p && !IsPathDivider(*p)) ||
					(*leftOver && !IsPathDivider(*leftOver)))
					matching = 0;
			else 
			{
				while (IsPathDivider(*p))						p++;
				while (IsPathDivider(*leftOver))				leftOver++;
			}
		}

		/* Skip over any /s in leftOver */
		while (IsPathDivider(*leftOver))				leftOver++;
		/*Skip over any /s in p */
		while (IsPathDivider(*p))				p++;

		/* p should now be at the end of the string if fully matched */
		if (*p)		matching = 0;

		if (matching && (thisMatchLength > longestMatch)) 
		{
			/* Matched prefix */
			*restOfPath = (YCHAR *)leftOver;
			retval = dev;
			longestMatch = thisMatchLength;
		}
	}
	return retval;
}
#endif


yaffs_tnode * CYafFs::GetTnode(void)
{
	// yaffs的实现中，tnode通过allocator来分配。allocator预先申请一定数量的tnode，
	//	然后将未分配的tnode放入free tnode链表中。链表通过yaffs_tnode::internal实现。
	//	目前先简化设计，用new/delete管理。但是效率低下，需要优化。
	m_dev->n_tnodes++;
	//m_dev->checkpoint_blocks_required = 0;	/* force recalculation */
	return m_tnode_allocator.GetTnode();
}

void CYafFs::FreeTnode(yaffs_tnode * & tn)
{
	// 展开	yaffs_free_raw_tnode(dev, tn);
	JCASSERT(m_dev->allocator);
	// yaffs的实现中，tnode通过allocator来分配。allocator预先申请一定数量的tnode，
	//	然后将未分配的tnode放入free tnode链表中。链表通过yaffs_tnode::internal实现。
	//	目前先简化设计，用new/delete管理。但是效率低下，需要优化。
	m_tnode_allocator.FreeTnode(tn);
	m_dev->n_tnodes--;
	//m_dev->checkpoint_blocks_required = 0;	/* force recalculation */
}

#define FSIZE_LOW(fsize) ((fsize) & 0xffffffff)
#define FSIZE_HIGH(fsize)(((fsize) >> 32) & 0xffffffff)
#define FSIZE_COMBINE(high, low) ((((loff_t) (high)) << 32) | \
					(((loff_t) (low)) & 0xFFFFFFFF))

loff_t CYafFs::ObjhdrToSize(yaffs_obj_hdr * oh)
{
	loff_t retval;
	if (sizeof(loff_t) >= 8 && ~(oh->file_size_high)) 
	{
		u32 low = oh->file_size_low;
		u32 high = oh->file_size_high;
		retval = FSIZE_COMBINE(high, low);
	}
	else 
	{
		u32 low = oh->file_size_low;
		retval = (loff_t)low;
	}
	return retval;
}


//yaffs_block_info * CYafFs::GetBlockInfo(int blk)
//{
//	if (blk < (int)m_dev->internal_start_block || blk >(int)m_dev->internal_end_block) 
//	{
//		LOG_ERROR(L"[err] **>> yaffs: get_block_info block %d is not valid", blk);
//		JCASSERT(0);
//	}
//	return &m_dev->block_info[blk - m_dev->internal_start_block];
//}

//bool CYafFs::CheckChunkBit(int blk, int chunk)
//{
//	BYTE * blk_bits = BlockBits(blk);
//
//	VerifyChunkBitId(blk, chunk);
//	return (blk_bits[chunk / 8] & (1 << (chunk & 7))) ? 1 : 0;
//}

//BYTE* CYafFs::BlockBits(int blk)
//{
//	if (blk < (int)m_dev->internal_start_block ||
//		blk >(int)m_dev->internal_end_block) 
//	{
//		LOG_ERROR(L"[err] BlockBits block %d is not valid", blk);
//		JCASSERT(0);
//	}
//	return m_dev->chunk_bits + (m_dev->chunk_bit_stride * (blk - m_dev->internal_start_block));
//}

//void CYafFs::ClearChunkBit(int blk, int chunk)
//{
//	u8 *blk_bits = BlockBits(blk);
//	blk_bits[chunk / 8] &= ~(1 << (chunk & 7));
//}

void CYafFs::HandleChunkError(yaffs_block_info * bi)
{
	if (!bi->gc_prioritise) 
	{
		bi->gc_prioritise = 1;
		m_dev->has_pending_prioritised_gc = 1;
		bi->chunk_error_strikes++;

		if (bi->chunk_error_strikes > 3) 
		{
			bi->needs_retiring = 1;	/* Too many stikes, so retire */
			LOG_NOTICE(L"yaffs: Block struck out");
		}
	}
}

int CYafFs::AllocChunk(bool use_reserver, yaffs_block_info *& block_ptr)
{
	//int ret_val;
	//struct yaffs_block_info *bi;

	//if (m_dev->alloc_block < 0) 
	//{	/* Get next block to allocate off */
	//	m_dev->alloc_block = FindAllocBlock();
	//	m_dev->alloc_page = 0;
	//}

	if (!use_reserver && !CheckAllocAvailable(1))
	{	/* No space unless we're allowed to use the reserve. */
		LOG_ERROR(L"[err] no enough available chuck, reuqest=%d", 1);
		return -1;
	}

	return m_block_manager->AllocChunk(block_ptr);

	//if (m_dev->n_erased_blocks < (int)m_dev->param.n_reserved_blocks && m_dev->alloc_page == 0)
	//	LOG_NOTICE(L"Allocating reserve");

	///* Next page please.... */
	//if (m_dev->alloc_block >= 0) 
	//{
	//	bi = GetBlockInfo(m_dev->alloc_block);
	//	ret_val = (m_dev->alloc_block * m_dev->param.chunks_per_block) + m_dev->alloc_page;
	//	bi->pages_in_use++;
	//	SetChunkBit(m_dev->alloc_block, m_dev->alloc_page);
	//	m_dev->alloc_page++;	//下一个分配的page
	//	m_dev->n_free_chunks--;

	//	/* If the block is full set the state to full */
	//	if (m_dev->alloc_page >= m_dev->param.chunks_per_block) 
	//	{
	//		bi->block_state = YAFFS_BLOCK_STATE_FULL;
	//		m_dev->alloc_block = -1;
	//	}
	//	block_ptr = bi;
	//	return ret_val;
	//}
	//LOG_ERROR(L"[err] !!!!!!!!! Allocator out !!!!!!!!!!!!!!!!!");
	//return -1;
}

//int CYafFs::FindAllocBlock(void)
//{
//	struct yaffs_block_info *bi;
//	if (m_dev->n_erased_blocks < 1)
//	{	/* Hoosterman we've got a problem. Can't get space to gc		 */
//		LOG_ERROR(L"yaffs tragedy: no more erased blocks");
//		return -1;
//	}
//
//	/* Find an empty block. */
//	for (UINT32 ii = m_dev->internal_start_block; ii <= m_dev->internal_end_block; ii++) 
//	{
//		m_dev->alloc_block_finder++;
//		if (m_dev->alloc_block_finder < (int)m_dev->internal_start_block
//			|| m_dev->alloc_block_finder >(int)m_dev->internal_end_block) 
//		{
//			m_dev->alloc_block_finder = m_dev->internal_start_block;
//		}
//
//		bi = GetBlockInfo(m_dev->alloc_block_finder);
////			yaffs_get_block_info(dev, m_dev->alloc_block_finder);
//
//		if (bi->block_state == YAFFS_BLOCK_STATE_EMPTY) 
//		{
//			bi->block_state = YAFFS_BLOCK_STATE_ALLOCATING;
//			m_dev->seq_number++;
//			bi->seq_number = m_dev->seq_number;
//			m_dev->n_erased_blocks--;
//			LOG_NOTICE(L"Allocated block %d, seq  %d, %d left",
//				m_dev->alloc_block_finder, m_dev->seq_number, m_dev->n_erased_blocks);
//			return m_dev->alloc_block_finder;
//		}
//	}
//	LOG_NOTICE(L"yaffs tragedy: no more erased blocks, but there should have been %d",
//		m_dev->n_erased_blocks);
//	return -1;
//}


//void CYafFs::VerifyChunkBitId(int blk, int chunk)
//{
//	if (blk < (int)m_dev->internal_start_block ||
//			blk >(int)m_dev->internal_end_block ||
//			chunk < 0 || chunk >= (int)m_dev->param.chunks_per_block)
//	{
//		LOG_ERROR(L"[err] Chunk Id (%d:%d) invalid", blk, chunk);
//		JCASSERT(0);
//	}
//
//}

//void CYafFs::UpdateOldestDirtySeq(UINT32 block_no, yaffs_block_info *bi)
//{
//	//if (!m_dev->param.is_yaffs2)		return;
//	if (m_dev->oldest_dirty_seq) 
//	{
//		if (m_dev->oldest_dirty_seq > bi->seq_number) 
//		{
//			m_dev->oldest_dirty_seq = bi->seq_number;
//			m_dev->oldest_dirty_block = block_no;
//		}
//	}
//}

void CYafFs::BlockBecameDirty(int block_no)
{
	yaffs_block_info *bi = m_block_manager->GetBlockInfo(block_no);
	int erased_ok = 0;
	//u32 i;

	/* If the block is still healthy erase it and mark as clean.
	 * If the block has had a data failure, then retire it. */
	LOG_NOTICE(L"[GC] yaffs_block_became_dirty block %d state %d %s",
		block_no, bi->block_state,	(bi->needs_retiring) ? L"needs retiring" : L"");

//	ClearOldestDirtySeq(bi);
	m_block_manager->ClearOldestDirtySeq(block_no);

	bi->block_state = YAFFS_BLOCK_STATE_DIRTY;
	/* If this is the block being garbage collected then stop gc'ing */
	if (block_no == (int)m_dev->gc_block)	m_dev->gc_block = 0;
	/* If this block is currently the best candidate for gc then drop as a candidate */
	if (block_no == (int)m_dev->gc_dirtiest)
	{
		m_dev->gc_dirtiest = 0;
		m_dev->gc_pages_in_use = 0;
	}

	if (!bi->needs_retiring) 
	{
		m_checkpt->Invalidate();
		erased_ok = EraseBlock(block_no);
		if (!erased_ok) 
		{
			m_dev->n_erase_failures++;
			LOG_NOTICE(L"[fail] Erasure failed %d", block_no);
		}
	}

	/* Verify erasure if needed */
	if (erased_ok && !SkipVerification() )
	{
		for (UINT32 ii = 0; ii < m_dev->param.chunks_per_block; ii++) 
		{
			if (!CheckChunkErased(block_no * m_dev->param.chunks_per_block + ii) )
			{
				LOG_ERROR(L">>Block %d erasure supposedly OK, but chunk %d not erased", block_no, ii);
			}
		}
	}

	if (!erased_ok) 
	{	/* We lost a block of free space */
		//m_dev->n_free_chunks -= m_dev->param.chunks_per_block;
		m_block_manager->BlockUsed(0, m_dev->param.chunks_per_block);
		RetireBlock(block_no);
		LOG_ERROR(L"**>> Block %d retired", block_no);
		return;
	}

	/* Clean it up... */
	m_block_manager->CleanBlock(block_no);
	//bi->block_state = YAFFS_BLOCK_STATE_EMPTY;
	//bi->seq_number = 0;
	//m_dev->n_erased_blocks++;
	//bi->pages_in_use = 0;
	//bi->has_shrink_hdr = 0;
	//bi->skip_erased_check = 1;	/* Clean, so no need to check */
	//bi->gc_prioritise = 0;
	//bi->has_summary = 0;

	//m_block_manager->ClearChunkBit(block_no, 0);
	LOG_NOTICE(L"[GC] Erased block %d", block_no);
}

//<MIGRATE> yaffs_yaffs2.c:yaffs2_clear_oldest_dirty_seq
//void CYafFs::ClearOldestDirtySeq(const yaffs_block_info * bi)
//{
//	if (!bi || bi->seq_number == m_dev->oldest_dirty_seq) 
//	{
//		m_dev->oldest_dirty_seq = 0;
//		m_dev->oldest_dirty_block = 0;
//	}
//}

void CYafFs::AddToDirty(CYaffsObject * obj)
{
	JCASSERT(obj);
	m_dirty_dirs.push_back(obj);
	obj->AddRef();
}

bool CYafFs::EraseBlock(int block_no)
{
	bool result;
	//m_dev->n_erasures++;
	result = m_driver->Erase(block_no);
	return result;
}

bool CYafFs::CheckChunkErased(int nand_chunk)
{
	bool retval = true;
	u8 *data = GetTempBuffer();
	struct yaffs_ext_tags tags;
	int result;

//	result = yaffs_rd_chunk_tags_nand(dev, nand_chunk, data, &tags);
	result = ReadChunkTagsNand(nand_chunk, data, &tags);

	if (result == false || tags.ecc_result > INandDriver::ECC_RESULT_NO_ERROR)
		retval = false;

//	if (!yaffs_check_ff(data, m_dev->data_bytes_per_chunk) ||
	if (!CheckFF(data, m_dev->data_bytes_per_chunk) ||
		tags.chunk_used) 
	{
		LOG_ERROR(L"[err] Chunk %d not erased", nand_chunk);
		retval = false;
	}

	ReleaseTempBuffer(data);

	return retval;
}

bool CYafFs::CheckFF(BYTE * buffer, size_t n_bytes)
{	/* Horrible, slow implementation */
	while (n_bytes--) 
	{
		if (*buffer != 0xff)			return false;
		buffer++;
	}
	return true;
}


// Block retiring for handling a broken block.
//<MIGRATE> yaffs_guts.c yaffs_retire_block

void CYafFs::RetireBlock(int flash_block)
{
//	const yaffs_block_info *bi = m_block_manager->GetBlockInfo(flash_block);
	m_checkpt->Invalidate();
//	ClearOldestDirtySeq(bi);
	m_block_manager->ClearOldestDirtySeq(flash_block);

	if (!m_driver->MarkBad(flash_block) )
	{
		if (!m_driver->Erase(flash_block))
		{
			LOG_ERROR(L"yaffs: Failed to mark bad and erase block %d", 	flash_block);
		}
		else 
		{
			struct yaffs_ext_tags tags;
			int chunk_id = flash_block * m_dev->param.chunks_per_block;

			BYTE * buffer = GetTempBuffer();
			memset(buffer, 0xff, m_dev->data_bytes_per_chunk);
			memset(&tags, 0, sizeof(tags));
			tags.seq_number = YAFFS_SEQUENCE_BAD_BLOCK;
			if (!m_tagger->WriteChunkTags(chunk_id, buffer, &tags))
			{
				LOG_ERROR(L"yaffs: Failed to write bad block marker to block %d", flash_block);
			}
			ReleaseTempBuffer(buffer);
		}
	}

	//bi->block_state = YAFFS_BLOCK_STATE_DEAD;
	//bi->gc_prioritise = 0;
	//bi->needs_retiring = 0;

	//m_dev->n_retired_blocks++;
}

void CYafFs::InvalidateWholeCache(CYaffsObject * obj)
{
	if (m_dev->param.n_caches > 0) 
	{	/* Invalidate it. */
		for (UINT32 i = 0; i < m_dev->param.n_caches; i++) 
		{
			if (m_dev->cache[i].object == obj)	m_dev->cache[i].object = NULL;
		}
	}
}

void CYafFs::ChunkDel(int chunk_id, bool mark_flash, int lyn)
{
	UINT32 block;
	UINT32 page;

	if (chunk_id <= 0) return;
//	m_dev->n_deletions++;
	block = chunk_id / m_dev->param.chunks_per_block;
	page = chunk_id % m_dev->param.chunks_per_block;

	bool erase_block = m_block_manager->ChunkDel(block, page);
	if (erase_block) BlockBecameDirty(block);
}

// <MIGRATE>: yaffs_guts.c: yaffs_check_gc()
bool CYafFs::CheckGc(bool background)
{
	/* New garbage collector
	 * If we're very low on erased blocks then we do aggressive garbage collection
	 * otherwise we do "leasurely" garbage collection.
	 * Aggressive gc looks further (whole array) and will accept less dirty blocks.
	 * Passive gc only inspects smaller areas and only accepts more dirty blocks.
	 *
	 * The idea is to help clear out space in a more spread-out manner.
	 * Dunno if it really does anything useful. */

	bool aggressive = false;
	bool gc_ok = true;
	int max_tries = 0;
	int min_erased;
	int erased_chunks;
	int checkpt_block_adjust;

	// 外部gc，yaffs中在一下两处存在，但都未实现
	//	C:\Users\jingcheng\workspace\opensource\yaffs\yaffs2 - b4ce1bb\yaffs_vfs_multi.c : yaffs_gc_control_callback()
	//	  C:\Users\jingcheng\workspace\opensource\yaffs\yaffs2-b4ce1bb\yaffs_vfs_single.c : yaffs_gc_control_callback()
	//if (m_dev->param.gc_control_fn && (m_dev->param.gc_control_fn(dev) & 1) == 0)  return true;

	/* Bail out so we don't get recursive gc */
	if (m_dev->gc_disable) 	return true;

	/* This loop should pass the first time.
	 * Only loops here if the collection does not increase space. */
	UINT32 erased;
	do 
	{
		max_tries++;
		checkpt_block_adjust = CalcCheckptBlocksRequired();
		min_erased = m_dev->param.n_reserved_blocks + checkpt_block_adjust + 1;

		erased = m_block_manager->GetErasedBlocks();
		erased_chunks =erased * m_dev->param.chunks_per_block;
		/* If we need a block soon then do aggressive gc. */
		UINT32 free_chunks = m_block_manager->GetFreeChunks();
		if (erased < min_erased) aggressive = true;
		else 
		{
			if (!background && erased_chunks > (free_chunks / 4)) break;
			if (m_dev->gc_skip > 20) m_dev->gc_skip = 20;
			if (erased_chunks < free_chunks / 2 || m_dev->gc_skip < 1 || background)
				aggressive = false;
			else 
			{
				m_dev->gc_skip--;
				break;
			}
		}
		m_dev->gc_skip = 5;

		/* If we don't already have a block being gc'd then see if we should start another */
		if (m_dev->gc_block < 1 && !aggressive) 
		{
//			m_dev->gc_block = yaffs2_find_refresh_block(dev);
			m_dev->gc_block = FindRefreshBlock();
			m_dev->gc_chunk = 0;
			m_dev->n_clean_ups = 0;
		}
		if (m_dev->gc_block < 1) 
		{
//			m_dev->gc_block = yaffs_find_gc_block(dev, aggressive, background);
			m_dev->gc_block = FindGcBlock(aggressive, background);
			m_dev->gc_chunk = 0;
			m_dev->n_clean_ups = 0;
		}

		if (m_dev->gc_block > 0) 
		{
			m_dev->all_gcs++;
			if (!aggressive) m_dev->passive_gc_count++;
			LOG_NOTICE(L"yaffs: GC n_erased_blocks %d aggressive %d", erased, aggressive);
			gc_ok = GcBlock(m_dev->gc_block, aggressive);
		}

		if (erased < (int)m_dev->param.n_reserved_blocks && m_dev->gc_block > 0) 
		{
			LOG_NOTICE(L"yaffs: GC !!!no reclaim!!! n_erased_blocks %d after try %d block %d",
				erased, max_tries, m_dev->gc_block);
		}
	} while ((erased < (int)m_dev->param.n_reserved_blocks) &&
		(m_dev->gc_block > 0) && (max_tries < 2));
	return aggressive ? gc_ok : true;
}

//<MIGRATE>: yaffs_yaffs2.c : u32 yaffs2_find_refresh_block(struct yaffs_dev *dev)
UINT32 CYafFs::FindRefreshBlock(void)
{
	/* periodically finds the oldest full block by sequence number for refreshing. Only for yaffs2. */
	//UINT32 b;
	UINT32 oldest = 0;
	//UINT32 oldest_seq = 0;
	//struct yaffs_block_info *bi;

	/* If refresh period < 10 then refreshing is disabled. */
	if (m_dev->param.refresh_period < 10) return oldest;

	/* Fix broken values. */
	if (m_dev->refresh_skip > m_dev->param.refresh_period)
		m_dev->refresh_skip = m_dev->param.refresh_period;

	if (m_dev->refresh_skip > 0) return oldest;

	/* Refresh skip is now zero.
	 * We'll do a refresh this time around....
	 * Update the refresh skip and find the oldest block. */
	m_dev->refresh_skip = m_dev->param.refresh_period;
	m_dev->refresh_count++;
	//bi = m_dev->block_info;
	//for (b = m_dev->internal_start_block; b <= m_dev->internal_end_block; b++) 
	//{
	//	if (bi->block_state == YAFFS_BLOCK_STATE_FULL) 
	//	{
	//		if (oldest < 1 || bi->seq_number < oldest_seq) 
	//		{
	//			oldest = b;
	//			oldest_seq = bi->seq_number;
	//		}
	//	}
	//	bi++;
	//}
	oldest = m_block_manager->FindOldestBlock();

	if (oldest > 0)
	{
		LOG_NOTICE(L"GC refresh count %d selected block %d with seq_number ??",
			m_dev->refresh_count, oldest);
	}
	return oldest;
}

//<MIGRATE> yaffs_guts.c : yaffs_gc_block()
UINT32 CYafFs::FindGcBlock(bool aggressive, bool background)
{
	//u32 i;
	u32 iterations;
	u32 selected = 0;
	int prioritised = 0;
	u32 threshold;

	UINT32 first = m_block_manager->GetFirstBlock();
	UINT32 last = m_block_manager->GetLastBlock();
	/* First let's see if we need to grab a prioritised block */
	if (m_dev->has_pending_prioritised_gc && !aggressive) 
	{
		bool prioritised_exist = false;
		m_dev->gc_dirtiest = 0;
//		bi = m_dev->block_info;
		for (UINT32 ii = first; ii <= last && !selected; ii++) 
		{
			const yaffs_block_info *bi = m_block_manager->GetBlockInfo(ii);
			if (bi->gc_prioritise) 
			{
				prioritised_exist = true;
				if (bi->block_state == YAFFS_BLOCK_STATE_FULL && BlockOkForGc(bi)) 
				{
					selected = ii;
					prioritised = 1;
				}
			}
//			bi++;
		}
		/* If there is a prioritised block and none was selected then
		 * this happened because there is at least one old dirty block
		 * gumming up the works. Let's gc the oldest dirty block. */
		if (prioritised_exist && !selected) selected = m_block_manager->GetOldestDirtyBlock();
			//&& m_dev->oldest_dirty_block > 0)
			//selected = m_dev->oldest_dirty_block;
		/* None found, so we can clear this */
		if (!prioritised_exist)	m_dev->has_pending_prioritised_gc = 0;
	}

	/* If we're doing aggressive GC then we are happy to take a less-dirty block, and search harder.
	 * else (leasurely gc), then we only bother to do this if the block has only a few pages in use. */

	if (!selected) 
	{
		//u32 pages_used;
		int n_blocks = last - first + 1;
		if (aggressive) 
		{
			threshold = m_dev->param.chunks_per_block;
			iterations = n_blocks;
		}
		else 
		{
			u32 max_threshold;
			if (background) max_threshold = m_dev->param.chunks_per_block / 2;
			else			max_threshold = m_dev->param.chunks_per_block / 8;

			if (max_threshold < YAFFS_GC_PASSIVE_THRESHOLD)	max_threshold = YAFFS_GC_PASSIVE_THRESHOLD;

			threshold = background ? (m_dev->gc_not_done + 2) * 2 : 0;
			if (threshold < YAFFS_GC_PASSIVE_THRESHOLD) threshold = YAFFS_GC_PASSIVE_THRESHOLD;
			if (threshold > max_threshold)  threshold = max_threshold;

			iterations = n_blocks / 16 + 1;
			if (iterations > 100) 	iterations = 100;
		}

		for (UINT32 ii = 0; 
			ii < iterations && (m_dev->gc_dirtiest < 1 || m_dev->gc_pages_in_use > YAFFS_GC_GOOD_ENOUGH);
			ii++) 
		{
			m_dev->gc_block_finder++;
			if (m_dev->gc_block_finder < first ||
				m_dev->gc_block_finder > last)
				m_dev->gc_block_finder = first;

//			bi = yaffs_get_block_info(dev, m_dev->gc_block_finder);
			const yaffs_block_info * bi = m_block_manager->GetBlockInfo(m_dev->gc_block_finder);
			//pages_used = bi->pages_in_use - bi->soft_del_pages;
			UINT32 pages_used = bi->pages_in_use;
			if (bi->block_state == YAFFS_BLOCK_STATE_FULL &&
				pages_used < m_dev->param.chunks_per_block &&
				(m_dev->gc_dirtiest < 1 || pages_used < m_dev->gc_pages_in_use) &&
				BlockOkForGc(bi) ) 
			{
				m_dev->gc_dirtiest = m_dev->gc_block_finder;
				m_dev->gc_pages_in_use = pages_used;
			}
		}

		if (m_dev->gc_dirtiest > 0 && m_dev->gc_pages_in_use <= threshold)
			selected = m_dev->gc_dirtiest;
	}

	/* If nothing has been selected for a while, try the oldest dirty
	 * because that's gumming up the works. */

	UINT32 oldest_dirty_blk;
	if (!selected && m_dev->gc_not_done >= (background ? 10 : 20)) 
	{
		UINT32 oldest_seq;
		oldest_dirty_blk = m_block_manager->FindOldestDirtyBlock(oldest_seq);

		//FindOldestDirtySeq();
		//if (m_dev->oldest_dirty_block > 0) 
		//{
		if (oldest_dirty_blk)
		{
			selected = oldest_dirty_blk;
			m_dev->gc_dirtiest = selected;
			m_dev->oldest_dirty_gc_count++;
			const yaffs_block_info * bi = m_block_manager->GetBlockInfo(selected);
			m_dev->gc_pages_in_use = bi->pages_in_use;
		}
		else m_dev->gc_not_done = 0;
	}

	if (selected) 
	{
		LOG_NOTICE(L"GC Selected block %d, with %d free, prioritised:%d",
			selected, m_dev->param.chunks_per_block - m_dev->gc_pages_in_use, prioritised);

		m_dev->n_gc_blocks++;
		if (background) 	m_dev->bg_gcs++;

		m_dev->gc_dirtiest = 0;
		m_dev->gc_pages_in_use = 0;
		m_dev->gc_not_done = 0;
		if (m_dev->refresh_skip > 0) 	m_dev->refresh_skip--;
	}
	else 
	{
		m_dev->gc_not_done++;
		LOG_NOTICE(L"GC none: finder %d, skip %d, threshold %d, dirtiest %d, using %d oldest %d%s",
			m_dev->gc_block_finder, m_dev->gc_not_done, threshold,
			m_dev->gc_dirtiest, m_dev->gc_pages_in_use,
			oldest_dirty_blk, background ? L" bg" : L"");
	}
	return selected;
}

//<MIGRATE>: yaffs_guts.c : yaffs_gc_block()
bool CYafFs::GcBlock(int block, bool whole_block)
{
	int old_chunk;
	bool ret_val = true;
	int is_checkpt_block;
	int max_copies;
	int chunks_before = m_block_manager->GetErasedChunks();
	int chunks_after;
	struct yaffs_block_info *bi = m_block_manager->GetBlockInfo(block);
	UINT32 block_state = m_block_manager->GetBlockState(block);

	is_checkpt_block = (block_state == YAFFS_BLOCK_STATE_CHECKPOINT);
	LOG_NOTICE(L"[GC] Collecting block %d, in use %d, shrink %d, whole_block %d",
		block, bi->pages_in_use, bi->has_shrink_hdr, whole_block);

	if (block_state == YAFFS_BLOCK_STATE_FULL)
		block_state = YAFFS_BLOCK_STATE_COLLECTING;
	bi->has_shrink_hdr = 0;	/* clear the flag so that the block can erase */

	m_dev->gc_disable = 1;
	SummaryGc(block);

	if (is_checkpt_block || !m_block_manager->StillSomeChunks(block))
	{
		LOG_NOTICE(L"[GC] Collecting block %d that has no chunks in use", block);
		BlockBecameDirty(block);
	}
	else 
	{
		BYTE * buffer = GetTempBuffer();
		VerifyBlk(bi, block);
		max_copies = (whole_block) ? m_dev->param.chunks_per_block : 5;
		old_chunk = block * m_dev->param.chunks_per_block + m_dev->gc_chunk;

		for (/* init already done */; 
			ret_val == true && m_dev->gc_chunk < m_dev->param.chunks_per_block &&
			(bi->block_state == YAFFS_BLOCK_STATE_COLLECTING) && max_copies > 0;
			m_dev->gc_chunk++, old_chunk++) 
		{
			if (m_block_manager->CheckChunkBit(block, m_dev->gc_chunk) )
			{
				/* Page is in use and might need to be copied */
				max_copies--;
				ret_val = GcProcessChunk(bi, old_chunk, buffer);
			}
		}
		ReleaseTempBuffer(buffer);
	}
	VerifyCollectedBlk(bi, block);

	if (bi->block_state == YAFFS_BLOCK_STATE_COLLECTING) 
	{	/* The gc did not complete. Set block state back to FULL
		 * because checkpointing does not restore gc. */
		bi->block_state = YAFFS_BLOCK_STATE_FULL;
	}
	else 
	{	/* The gc completed. */
		/* Do any required cleanups */
		for (UINT32 ii = 0; ii < m_dev->n_clean_ups; ii++) 
		{
			/* Time to delete the file too */
			jcvos::auto_interface<CYaffsObject> object;
			FindByNumber(object, m_dev->gc_cleanup_list[ii]);
			if (object) 
			{
				object->FreeTonode();
				LOG_NOTICE(L"[yaffs] About to finally delete object %d", m_dev->gc_cleanup_list[ii]);
				object->DelObj();
			}

		}
		chunks_after = m_block_manager->GetErasedChunks();
		if (chunks_before >= chunks_after) LOG_NOTICE(
			L"[GC] gc did not increase free chunks before %d after %d",
			chunks_before, chunks_after);
		m_dev->gc_block = 0;
		m_dev->gc_chunk = 0;
		m_dev->n_clean_ups = 0;
	}

	m_dev->gc_disable = 0;

	return ret_val;
}

//int CYafFs::GetErasedChunks(void)
//{
//	int n;
//	n = m_dev->n_erased_blocks * m_dev->param.chunks_per_block;
//	if (m_dev->alloc_block > 0) n += (m_dev->param.chunks_per_block - m_dev->alloc_page);
//	return n;
//}

//<MIGRATE> yaffs_yaffs2.c : yaffs_block_ok_for_gc()
bool CYafFs::BlockOkForGc(const yaffs_block_info * bi)
{
	/* disqualification only applies to yaffs2. */
	//if (!m_dev->param.is_yaffs2)	return true;	
	if (!bi->has_shrink_hdr)	return true;	/* can gc */

//	yaffs2_find_oldest_dirty_seq(dev);
//	FindOldestDirtySeq();
	UINT32 oldest_seq;
	m_block_manager->FindOldestDirtyBlock(oldest_seq);

	/* Can't do gc of this block if there are any blocks older than this one that have discarded pages. */
	return (bi->seq_number <= oldest_seq);
}

//void CYafFs::FindOldestDirtySeq(void)
//{
//	if (!m_dev->oldest_dirty_seq)	CaleOldestDirtySeq();
//}

void CYafFs::SummaryGc(int blk)
{
	struct yaffs_block_info *bi = m_block_manager->GetBlockInfo(blk);
	if (!bi->has_summary)	return;

	for (UINT32 ii = m_dev->chunks_per_summary; ii < m_dev->param.chunks_per_block; ii++) 
	{
		m_block_manager->ClaimPage(blk, ii);
		//if (m_block_manager->CheckChunkBit(blk, ii))
		//{
		//	ClearChunkBit(blk, ii);
		//	bi->pages_in_use--;
		//	m_dev->n_free_chunks++;
		//}
	}
}

//bool CYafFs::StillSomeChunks(int blk)
//{
//	BYTE *blk_bits = BlockBits(blk);
//	for (int ii = 0; ii < m_dev->chunk_bit_stride; ii++) 
//	{
//		if (*blk_bits)	return true;
//		blk_bits++;
//	}
//	return false;
//}

// <MIGRATE> yaffs_guts.c: yaffs_gc_process_chunk()
bool CYafFs::GcProcessChunk(yaffs_block_info * bi, int old_chunk, BYTE * buffer)
{
	int new_chunk;
	bool mark_flash = true;
	struct yaffs_ext_tags tags;
	bool ret_val = true;

	memset(&tags, 0, sizeof(tags));
	ReadChunkTagsNand(old_chunk, buffer, &tags);
	jcvos::auto_interface<CYaffsObject> object;
	FindByNumber(object, tags.obj_id);

	LOG_NOTICE(L"[GC] Collecting chunk in block %d, %d %d %d ",
		m_dev->gc_chunk, tags.obj_id, tags.chunk_id, tags.n_bytes);

	if (!object)
	{
		THROW_ERROR(ERR_APP, L"[GC] page %d in gc has no object: %d %d %d ",
			old_chunk, tags.obj_id, tags.chunk_id, tags.n_bytes);
	}
	// Yaffs2不支持soft delete，简化
	m_dev->n_gc_copies++;
	new_chunk = object->RefreshChunk(old_chunk, tags, buffer);
	if (ret_val == true) ChunkDel(old_chunk, mark_flash, __LINE__);
	return ret_val;
}

//void CYafFs::CaleOldestDirtySeq(void)
//{
//	//u32 i;
//	unsigned seq;
//	unsigned block_no = 0;
//	struct yaffs_block_info *b;
//
//	seq = m_dev->seq_number + 1;
//	b = m_dev->block_info;
//	for (UINT32 ii = m_dev->internal_start_block; ii <= m_dev->internal_end_block; ii++) 
//	{
//		if (b->block_state == YAFFS_BLOCK_STATE_FULL &&
//			(UINT32)b->pages_in_use < m_dev->param.chunks_per_block &&
//			b->seq_number < seq) 
//		{
//			seq = b->seq_number;
//			block_no = ii;
//		}
//		b++;
//	}
//
//	if (block_no) 
//	{
//		m_dev->oldest_dirty_seq = seq;
//		m_dev->oldest_dirty_block = block_no;
//	}
//}

bool CYafFs::CheckAllocAvailable(int n_chunks)
{
//	//int reserved_chunks;
//	int reserved_blocks =m_dev->param.n_reserved_blocks;
//	//int checkpt_blocks;
//
////	checkpt_blocks = yaffs_calc_checkpt_blocks_required(dev);
	int checkpt_blocks = CalcCheckptBlocksRequired();
//	int reserved_chunks = (reserved_blocks + checkpt_blocks) *m_dev->param.chunks_per_block;
//	return (m_dev->n_free_chunks > (reserved_chunks + n_chunks));
	return m_block_manager->CheckAllocAvailable(n_chunks, checkpt_blocks);
}

void CYafFs::UseCache(yaffs_cache * cache, bool is_write)
{
	if (m_dev->param.n_caches < 1)	return;

	if (m_dev->cache_last_use < 0 ||
		m_dev->cache_last_use > 100000000) 
	{	/* Reset the cache usages */
		for (UINT32 ii = 1; ii < m_dev->param.n_caches; ii++)
			m_dev->cache[ii].last_use = 0;
		m_dev->cache_last_use = 0;
	}
	m_dev->cache_last_use++;
	cache->last_use = m_dev->cache_last_use;
	if (is_write)	cache->dirty = 1;
}


bool CYafFs::ObjectCacheDirty(CYaffsObject * obj)
{
//	struct yaffs_dev *dev = obj->my_dev;
	//int ii;
	//struct yaffs_cache *cache;
	UINT32 n_caches = m_dev->param.n_caches;
	for (UINT32 ii = 0; ii < n_caches; ii++) 
	{
		yaffs_cache * cache = &m_dev->cache[ii];
		if (cache->object == obj && cache->dirty)	return true;
	}
	return false;
}

yaffs_cache * CYafFs::FindChunkCache(CYaffsObject * obj, int chunk_id)
{
//	struct yaffs_dev *dev = obj->my_dev;
	//u32 i;
	if (m_dev->param.n_caches < 1)	return NULL;
	for (UINT32 ii = 0; ii < m_dev->param.n_caches; ii++) 
	{
		if (m_dev->cache[ii].object == obj && m_dev->cache[ii].chunk_id == chunk_id) 
		{
			m_dev->cache_hits++;
			return &m_dev->cache[ii];
		}
	}
	return NULL;
}

void CYafFs::InvalidateChunkCache(CYaffsObject * obj, int chunk_id)
{
	//struct yaffs_cache *cache;
	if (m_dev->param.n_caches > 0) 
	{
		yaffs_cache * cache = FindChunkCache(obj, chunk_id);
		if (cache)	cache->object = NULL;
	}
}

//void CYafFs::SkipRestOfBlock(void)
//{
//	struct yaffs_block_info *bi;
//	if (m_dev->alloc_block > 0) 
//	{
//		bi = GetBlockInfo(m_dev->alloc_block);
//		if (bi->block_state == YAFFS_BLOCK_STATE_ALLOCATING) 
//		{
//			bi->block_state = YAFFS_BLOCK_STATE_FULL;
//			m_dev->alloc_block = -1;
//		}
//	}
//
//}

void CYafFs::AddrToChunk(loff_t addr, int & chunk_out, size_t & offset_out)
{
	int chunk;
	u32 offset;

	chunk = (u32)(addr >> m_dev->chunk_shift);

	if (m_dev->chunk_div == 1) 
	{	/* easy power of 2 case */
		offset = (u32)(addr & m_dev->chunk_mask);
	}
	else 
	{	/* Non power-of-2 case */
		loff_t chunk_base;
		chunk /= m_dev->chunk_div;
		chunk_base = ((loff_t)chunk) * m_dev->data_bytes_per_chunk;
		offset = (u32)(addr - chunk_base);
	}
	chunk_out = chunk;
	offset_out = offset;
}

bool CYafFs::CreateTagHandler(void)
{
	JCASSERT(m_tagger==NULL);
	CMarshallTags * tag = jcvos::CDynamicInstance<CMarshallTags>::Create();
	tag->SetFileSystem(this, m_driver);
	m_tagger = static_cast<ITagsHandler*>(tag);
	return true;
}

//void CYafFs::ShowBlockStates(void)
//{
//	for (size_t bb = m_dev->internal_start_block; bb < m_dev->internal_end_block; ++bb)
//	{
//		yaffs_block_info * bi = GetBlockInfo(bb);
//		LOG_DEBUG(L"blk state: blk=%03d, state=%d", bb, bi->block_state);
//	}
//}

bool CYafFs::ReadChunkNand(int nand_chunk, BYTE * data, yaffs_spare * spare, INandDriver::ECC_RESULT & ecc_result, int correct_errors)
{
	//int ret_val;
	struct yaffs_spare local_spare;
	int data_size;
	int spare_size;
	//int ecc_result1, ecc_result2;
	//u8 calc_ecc[3];

	if (!spare) 
	{	/* If we don't have a real spare, then we use a local one. */
		/* Need this for the calculation of the ecc */
		spare = &local_spare;
	}
	data_size = m_dev->data_bytes_per_chunk;
	spare_size = sizeof(struct yaffs_spare);

	if (m_dev->param.use_nand_ecc)
	{
		return m_driver->ReadChunk(nand_chunk, data, data_size, (BYTE*)(spare), spare_size, ecc_result);
	}
	JCASSERT(0);
	return false;

/*<TODO> 目前不支持 yaffs级别的ECC
	// Handle the ECC at this level.
	ret_val = m_dev->drv.drv_read_chunk_fn(dev, nand_chunk,
		data, data_size,
		(u8 *)spare, spare_size,
		NULL);
	if (!data || !correct_errors)
		return ret_val;

	// Do ECC correction if needed.
	yaffs_ecc_calc(data, calc_ecc);
	ecc_result1 = yaffs_ecc_correct(data, spare->ecc1, calc_ecc);
	yaffs_ecc_calc(&data[256], calc_ecc);
	ecc_result2 = yaffs_ecc_correct(&data[256], spare->ecc2, calc_ecc);

	if (ecc_result1 > 0) {
		yaffs_trace(YAFFS_TRACE_ERROR,
			"**>>yaffs ecc error fix performed on chunk %d:0",
			nand_chunk);
		m_dev->n_ecc_fixed++;
	}
	else if (ecc_result1 < 0) {
		yaffs_trace(YAFFS_TRACE_ERROR,
			"**>>yaffs ecc error unfixed on chunk %d:0",
			nand_chunk);
		m_dev->n_ecc_unfixed++;
	}

	if (ecc_result2 > 0) {
		yaffs_trace(YAFFS_TRACE_ERROR,
			"**>>yaffs ecc error fix performed on chunk %d:1",
			nand_chunk);
		m_dev->n_ecc_fixed++;
	}
	else if (ecc_result2 < 0) {
		yaffs_trace(YAFFS_TRACE_ERROR,
			"**>>yaffs ecc error unfixed on chunk %d:1",
			nand_chunk);
		m_dev->n_ecc_unfixed++;
	}

	if (ecc_result1 || ecc_result2) {
		// We had a data problem on this page
		yaffs_handle_rd_data_error(dev, nand_chunk);
	}

	if (ecc_result1 < 0 || ecc_result2 < 0)
		*ecc_result = INandDriver::ECC_RESULT_UNFIXED;
	else if (ecc_result1 > 0 || ecc_result2 > 0)
		*ecc_result = INandDriver::ECC_RESULT_FIXED;
	else
		*ecc_result = INandDriver::ECC_RESULT_NO_ERROR;

	return ret_val;
*/
}

bool CYafFs::WriteChunkTagsNand(int nand_chunk, const BYTE * buffer, yaffs_ext_tags * tags)
{
	bool result;
//	int flash_chunk = ApplyChunkOffset(nand_chunk);
	int flash_chunk = nand_chunk;

	m_dev->n_page_writes++;

	if (!tags) 
	{
		LOG_ERROR(L"[err] Writing with no tags");
		return false;
	}
	tags->seq_number = m_block_manager->GetSeqNum();
	tags->chunk_used = 1;
	UINT32 blk = nand_chunk / m_dev->param.chunks_per_block;
	UINT32 chunk = nand_chunk % m_dev->param.chunks_per_block;
	LOG_NOTICE(L"Writing chunk (%03X:%03X) tags id=%d, chunk=%d", blk, chunk, tags->obj_id, tags->chunk_id);
	result = m_tagger->WriteChunkTags(flash_chunk, buffer, tags);
	SummaryAdd(tags, nand_chunk);
	return result;
}

bool CYafFs::WriteChunkNand(int nand_chunk, const BYTE * buffer, yaffs_spare * spare)
{
	int data_size = m_dev->data_bytes_per_chunk;
	return m_driver->WriteChunk(nand_chunk, buffer, data_size, (BYTE*)spare, sizeof(yaffs_spare));
}

int CYafFs::WriteNewChunk(const BYTE * data, yaffs_ext_tags * tags, bool use_reserver)
{
	JCASSERT(data && tags);
	u32 attempts = 0;
	bool write_ok = false;
	int chunk;

//	yaffs2_checkpt_invalidate(dev);
	m_checkpt->Invalidate();

	do
	{	// 此循环做write的retry，找到下一个chunk，确保其是空的，并且写入data
		struct yaffs_block_info *bi = 0;
		bool erased_ok =false;
		chunk = AllocChunk(use_reserver, bi);
		if (chunk < 0) 	break;		/* no space */

		/* First check this chunk is erased, if it needs checking. 
		   The checking policy (unless forced always on) is as follows:
		 *
		 * Check the first page we try to write in a block.
		 * If the check passes then we don't need to check any more.
		   If the check fails, we check again...
		 * If the block has been erased, we don't need to check.
		 *
		 * However, if the block has been prioritised for gc,
		 * then we think there might be something odd about this block and stop using it.
		 *
		 * Rationale: We should only ever see chunks that have
		 * not been erased if there was a partially written
		 * chunk due to power loss.  This checking policy should
		 * catch that case with very few checks and thus save a
		 * lot of checks that are most likely not needed.
		 *
		 * Mods to the above
		 * If an erase check fails or the write fails we skip the
		 * rest of the block.
		 */

		 /* let's give it a try */
		attempts++;

		// <TODO> 此处可进行优化：AllocChunk函数返回block info仅用于erase check。
		//	erase check的工作可以交给block manager完成。在每次分配block的chunk 0时检查。

		if (m_dev->param.always_check_erased)	bi->skip_erased_check = 0;

		if (!bi->skip_erased_check) 
		{
//			erased_ok = yaffs_check_chunk_erased(dev, chunk);
			// 检查page是否被erase了。目前的做法是：检查page是否为全0xff，这样的作法效率很低。
			//	优化方案1：用几个特定的字节作为判断依据。
			//	方案2：不检查erased, 而是在nand driver中检测over program
			erased_ok = CheckChunkErased(chunk);
			if (!erased_ok) 
			{
				LOG_ERROR(L"**>> yaffs chunk %d was not erased", chunk);
				/* If not erased, delete this one, skip rest of block and try another chunk */
				ChunkDel(chunk, true, __LINE__);
				m_block_manager->SkipRestOfBlock();
				continue;
			}
		}
		write_ok = WriteChunkTagsNand(chunk, data, tags);

		if (!bi->skip_erased_check)	write_ok = VerifyChunkWritten(chunk, data, tags);
			//yaffs_verify_chunk_written(dev, chunk, data, tags);

		if (!write_ok)
		{	/* Clean up aborted write, skip to next block and try another chunk */
//			yaffs_handle_chunk_wr_error(dev, chunk, erased_ok);
			HandleChunkWriteError(chunk, erased_ok);
			continue;
		}
		bi->skip_erased_check = 1;
		/* Copy the data into the robustification buffer */
//		yaffs_handle_chunk_wr_ok(dev, chunk, data, tags);
		HandleChunkWriteOk(chunk, data, tags);

	}
//<MIGRATE> 源代码中，yaffs_wr_attemps是一个全部变量，但是没有找到初始化和其他用处。有宏定义
// #define YAFFS_WR_ATTEMPTS		(5*64)
	//	while (!write_ok &&	(yaffs_wr_attempts == 0 || attempts <= yaffs_wr_attempts));
	while (!write_ok && attempts < 10);

	if (!write_ok)	chunk = -1;
	if (attempts > 1)
	{
		LOG_WARNING(L"[warning] **>> yaffs write required %d attempts",	attempts);
		m_dev->n_retried_writes += (attempts - 1);
	}
	return chunk;
}



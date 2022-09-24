#include "pch.h"
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#include <linux-fs-wrapper.h>
#include "../include/f2fs-filesystem.h"
#include <boost/property_tree/json_parser.hpp>
#include "f2fs/segment.h"

#ifdef _DEBUG
#include "unit_test.h"
#endif
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// == file system

LOCAL_LOGGER_ENABLE(L"f2fs.filesystem", LOGGER_LEVEL_DEBUGINFO);


CF2fsFileSystem::CF2fsFileSystem(void)
{
	// sanity check: for debug only => checked
	//size_t s1 = (offsetof(struct f2fs_inode, _u._s.i_extra_end) - F2FS_EXTRA_ISIZE_OFFSET);
	//size_t s2 = sizeof(f2fs_inode::_u._s);
	//JCASSERT(s1 == s2);

	// initialize f2fs_list (shinker)
	InitializeCriticalSection(&m_f2fs_list_lock);
	m_f2fs_list.next = &m_f2fs_list;
	m_f2fs_list.prev = &m_f2fs_list;
	//初始化 super_block

}

CF2fsFileSystem::~CF2fsFileSystem(void)
{
	dcache_release();
}

bool CF2fsFileSystem::ConnectToDevice(IVirtualDisk* dev)
{
	if (dev == NULL) THROW_ERROR(ERR_APP, L"device cannot be null");
	// for single device
	//m_sb_info->s_ndevs = 1;
	//m_sb_info->s_bdev = dev;
	//m_sb_info->s_bdev->AddRef();	// 在file system中，只在这里一次计数
	m_sb_info->SetDevice(dev);

	m_config.devices[0].m_fd = dev;
	m_config.devices[0].total_sectors = dev->GetCapacity();
	m_config.sector_size = dev->GetSectorSize();
	m_config.ndevs = 1;

	m_buffers.SetDisk(dev);
	return true;
}

void CF2fsFileSystem::Disconnect(void)
{
	m_buffers.Reset();
//	RELEASE(m_sb_info->s_bdev);
	m_config.devices[0].m_fd = NULL;
	m_config.ndevs = 0;
}

void CF2fsFileSystem::Reset(void)
{
	m_vol_name.clear();
	// clean m_config;
}

void f2fs_sb_info::destroy_device_list()
{
	int i;

	for (i = 0; i < s_ndevs; i++)
	{
//		blkdev_put(FDEV(i).bdev, FMODE_EXCL);
#ifdef CONFIG_BLK_DEV_ZONED
		kvfree(FDEV(i).blkz_seq);
		kfree(FDEV(i).zone_capacity_blocks);
#endif
	}
	// new in f2fs_scan_devices()
	if (devs) delete[] devs;
}

void f2fs_sb_info::destory_super(void)
{
//	f2fs_kvfree(m_sb_info->ckpt);
	free(ckpt);	//在 checkpoint.cpp: CF2fsFileSystem::f2fs_get_valid_checkpoint()中创建
	ckpt = NULL;

//	delete m_sb_info->ckpt;
	destroy_device_list();
#ifdef CONFIG_F2FS_FS_COMPRESSION
	f2fs_destroy_page_array_cache(m_sb_info);
#endif
#ifdef CONFIG_F2FS_FS_XATTR
	f2fs_destoy_xattr_caches(m_sb_info);
#endif
	// new in f2fs_fill_super()
	for (int ii = 0; ii < NR_PAGE_TYPE; ++ii) delete[] write_io[ii];
	// load in f2fs_setup_casefold()
//	if (m_sb_info->s_encoding)	utf8_unload(m_sb_info->s_encoding);
	s_encoding = NULL;
	// new in read_raw_super_block()
	if (raw_super) delete raw_super;
	// 不需要释放SRWLock
}

static const char* fs_name = "f2fs";

bool CF2fsFileSystem::Mount(IVirtualDisk* dev)
{
	LOG_STACK_TRACE();
	//m_sb_info 已经初始化过
//	memset(&m_sb, 0, sizeof(super_block));
	JCASSERT(m_sb_info == nullptr);

	// 初始化 file_system_type结构。原：super.cpp中静态初始化，在init_f2fs_fs()中注册到Linux fs
	file_system_type* fs_type = new file_system_type;
	fs_type->name = fs_name;
	fs_type->fs_flags = FS_REQUIRES_DEV;
	
	m_sb_info = new f2fs_sb_info(this, fs_type);
	ConnectToDevice(dev);
//	InitializeCriticalSection(m_sb_info->s_inode_list_lock);
//	INIT_LIST_HEAD(m_sb_info->s_inodes);

	int err = m_sb_info->f2fs_fill_super(L"", 0);
	if (err)
	{
		LOG_ERROR(L"[err] failed on calling fill super");
		return false;
	}
	return true;
}

void CF2fsFileSystem::Unmount(void)
{
	LOG_STACK_TRACE();
	if (m_sb_info)
	{
		m_sb_info->kill_f2fs_super();
		delete m_sb_info;
		m_sb_info = nullptr;
	}

	Disconnect();
}

bool CF2fsFileSystem::MakeFileSystem(IVirtualDisk* dev, UINT32 volume_size, const std::wstring& volume_name, const std::wstring & options)
{
#ifdef _DEBUG
	int err = run_unit_test();
	if (err != 0) THROW_ERROR(ERR_APP, L"failed in unit test, code=%d", err);
#endif
	LOG_STACK_TRACE();
	bool br = true;
//	struct f2fs_configuration config;
	f2fs_init_configuration(m_config);
	f2fs_parse_operation(volume_name, options);

	file_system_type* fs_type = new file_system_type;
	fs_type->name = fs_name;
	fs_type->fs_flags = FS_REQUIRES_DEV;

	m_sb_info = new f2fs_sb_info(this, fs_type);

	ConnectToDevice(dev);
	f2fs_get_device_info(m_config);

	int ir = f2fs_format_device(m_config);
	if (ir < 0)
	{
		LOG_ERROR(L"[err] failed on format device, code=%d", ir);
		br = false;
	}
	Disconnect();

	delete m_sb_info;
	m_sb_info = nullptr;
	return br;
}

bool CF2fsFileSystem::DokanGetDiskSpace(ULONGLONG& free_bytes, ULONGLONG& total_bytes, ULONGLONG& total_free_bytes)
{
	UINT64 block_count = le64_to_cpu(m_sb_info->raw_super->block_count);
	UINT32 block_size = le32_to_cpu(m_sb_info->raw_super->log_blocksize);

	total_bytes = block_count * block_size;
	free_bytes = total_bytes - total_bytes / 10;	//(90% 总容量)
	total_free_bytes = free_bytes;

	return true;
}

bool CF2fsFileSystem::GetVolumnInfo(std::wstring& vol_name, DWORD& sn, DWORD& max_fn_len, DWORD& fs_flag, std::wstring& fs_name)
{
	vol_name = m_sb_info->raw_super->volume_name;
	BYTE* uuid = m_sb_info->raw_super->uuid;
	sn = MAKELONG(MAKEWORD(uuid[3], uuid[2]), MAKEWORD(uuid[1], uuid[0]));
	max_fn_len = 256;

	fs_name = L"f2fs";

	fs_flag = 0 | FILE_CASE_SENSITIVE_SEARCH | FILE_UNICODE_ON_DISK;
	//FILE_CASE_PRESERVED_NAMES		//0x00000002	//	The specified volume supports preserved case of file names when it places a name on disk.
	//FILE_CASE_SENSITIVE_SEARCH	//0x00000001	//	The specified volume supports case-sensitive file names.
	//FILE_DAX_VOLUME				//0x20000000	//	The specified volume is a direct access(DAX) volume. Note  This flag was introduced in Windows 10, version 1607.
	//FILE_FILE_COMPRESSION			//0x00000010	//	The specified volume supports file - based compression.
	//FILE_NAMED_STREAMS			//0x00040000	//	The specified volume supports named streams.
	//FILE_PERSISTENT_ACLS			//0x00000008	//	The specified volume preserves and enforces access control lists(ACL).For example, the NTFS file system preservesand enforces ACLs, and the FAT file system does not.
	//FILE_READ_ONLY_VOLUME			//0x00080000	//	The specified volume is read - only.
	//FILE_SEQUENTIAL_WRITE_ONCE	//0x00100000	//	The specified volume supports a single sequential write.
	//FILE_SUPPORTS_ENCRYPTION		//0x00020000	//	The specified volume supports the Encrypted File System(EFS).For more information, see File Encryption.
	//FILE_SUPPORTS_EXTENDED_ATTRIBUTES	//	0x00800000	//	The specified volume supports extended attributes.An extended attribute is a piece of application - specific metadata that an application can associate with a file and is not part of the file's data. 
	//FILE_SUPPORTS_HARD_LINKS		//0x00400000	//	The specified volume supports hard links.For more information, see Hard Linksand Junctions.	
	//FILE_SUPPORTS_OBJECT_IDS		//0x00010000	//	The specified volume supports object identifiers.
	//FILE_SUPPORTS_OPEN_BY_FILE_ID	//0x01000000	//	The file system supports open by FileID.For more information, see FILE_ID_BOTH_DIR_INFO.
	//FILE_SUPPORTS_REPARSE_POINTS	//0x00000080	//	The specified volume supports reparse points. //	ReFS : ReFS supports reparse points but does not index them so FindFirstVolumeMountPoint and FindNextVolumeMountPoint will not function as expected.
	//FILE_SUPPORTS_SPARSE_FILES	//0x00000040	//	The specified volume supports sparse files.
	//FILE_SUPPORTS_TRANSACTIONS	//0x00200000	//	The specified volume supports transactions.For more information, see About KTM.
	//FILE_SUPPORTS_USN_JOURNAL		//0x02000000	//	The specified volume supports update sequence number(USN) journals.For more information, see Change Journal Records.

	//FILE_UNICODE_ON_DISK			//0x00000004	//	The specified volume supports Unicode in file names as they appear on disk.
	//FILE_VOLUME_IS_COMPRESSED		//0x00008000	//	The specified volume is a compressed volume, for example, a DoubleSpace volume.
	//FILE_VOLUME_QUOTAS			//0x00000020	//	The specified volume supports disk quotas.
	//FILE_SUPPORTS_BLOCK_REFCOUNTING	//0x08000000	//	The specified volume supports sharing logical clusters between files on the same volume.The file system reallocates on writes to shared clusters.Indicates that FSCTL_DUPLICATE_EXTENTS_TO_FILE is a supported operation.

	return true;
}

bool CF2fsFileSystem::DokanCreateFile(IFileInfo*& file, const std::wstring& path, ACCESS_MASK access_mask, DWORD attr, FsCreateDisposition disp, ULONG share, ULONG opt, bool isdir)
{
	LOG_STACK_TRACE();
	LOG_DEBUG(L"disp=%d, dir=%d, target fn=%s", disp, isdir, path.c_str());
	//	(1) 打开parent
	// 	(2) 在parent中查找目标
	//  (3) 如果是 OPEN_EXIST, OPEN_ALWAYS, TRUNCATE_EXIST: 
	//		文件存在：返回，否则报错
	// 	(4) 如果是 CREATE_ALWAYS
	// 
	// 
	// case 1: not exist, create
	// case 2: not exist, open -> error
	// case 3: exist, create -> error
	// case 4: exist, open
		// case 4.1: remain data
		// case 4.2: truck data

	fmode_t file_mode = 0;
	if (access_mask & GENERIC_READ) file_mode |= FMODE_READ;
	if (access_mask & GENERIC_WRITE) file_mode |= FMODE_WRITE;
	if (access_mask & GENERIC_EXECUTE) file_mode |= FMODE_EXEC;
	if (access_mask & GENERIC_ALL) file_mode |= (FMODE_READ | FMODE_WRITE | FMODE_EXEC);


	// 路径解析：得到父节点路径：str_path, 目标文件名：str_fn;
	//size_t path_len = path.size();
	//jcvos::auto_array<wchar_t> _str_path(path_len + 1);
	//wchar_t* str_path = (wchar_t*)_str_path;
	//wcscpy_s(str_path, path_len + 1, path.c_str());

	//wchar_t* str_fn;
	//wchar_t* ch = str_path + path_len - 1;
	//while (*ch != DIR_SEPARATOR && ch >= str_path) ch--;
	//str_fn = ch + 1;	// 排除根目录的"\"
	//size_t fn_len = path_len - (str_fn - str_path);
	//size_t parent_len = ch - str_path;
	//LOG_DEBUG(L"parent=%s, parent len=%zd, file name = %s, length = %zd", ch, parent_len, str_fn, fn_len);


	// TODO: 使用file缓存
//	jcvos::auto_interface<CF2fsFile> root_dir;
	jcvos::auto_interface<CF2fsFile> parent_dir;
	jcvos::auto_interface<IFileInfo> _file;
	//bool br = _GetRoot(root_dir);
	//if (!br || !root_dir) THROW_ERROR(ERR_APP, L"root does not exist");

	//if (parent_len == 0)
	//{
	//	parent_dir = root_dir;
	//	root_dir->AddRef();
	//}
	//else
	//{
	//	br = root_dir->OpenChildEx(parent_dir, str_path, parent_len);
	//	if (!br || !parent_dir || !parent_dir->IsDirectory())
	//	{
	//		LOG_ERROR(L"[err] cannot find parent path %s, or non directory", str_path);
	//		return false;
	//	}
	//}

	std::wstring str_fn;
	bool br = OpenParent(parent_dir, path, str_fn);
	if (!br || parent_dir == nullptr) THROW_ERROR(ERR_APP, L"parent dir of %s does not exist", path.c_str());

	if (str_fn.empty())
	{	// 打开根目录
		if (disp == OPEN_ALWAYS || disp == OPEN_EXISTING)
		{
			parent_dir.detach(file);
			return true;
		}
		else
		{
			LOG_ERROR(L"[err] cannot create root dir");
			return false;
		}

	}

	br = parent_dir->OpenChild(_file, str_fn.c_str(), file_mode);
	switch (disp)
	{
	case CREATE_NEW:
		if (br && _file)	{ LOG_ERROR(L"[err] file %s existed with create new", path.c_str()); return false; }
		// else: create new file
		break;

	case OPEN_ALWAYS:
		if (br && _file)	{	_file.detach(file);		return true;	}
		// else: create a new file
		break;

	case OPEN_EXISTING:
		if (br && _file)	{	_file.detach(file);		return true;	}
		else		{	LOG_ERROR(L"[err] file %s does not exist", path.c_str());		return false;	}
		break;
		
	case CREATE_ALWAYS:
		if (br && _file)	
		{	// <TODO> clear file
			_file.detach(file);		
			return true;	
		}
		break;
	case TRUNCATE_EXISTING:
		if (!br || !_file)	{	LOG_ERROR(L"[err] file %s does not exist", path.c_str()); return false;	}
		// <TODO> clear file
		return true;
	default:
		THROW_ERROR(ERR_PARAMETER, L"[err] unknow disp=%d", disp);
		break;
	}

	br = parent_dir->CreateChild(_file, str_fn.c_str(), isdir, file_mode);
	if (!br || !_file)
	{
		LOG_ERROR(L"[err] failed on creating new file %s", path.c_str());
		return false;
	}
	_file.detach(file);
	return true;
}

bool CF2fsFileSystem::DokanDeleteFile(const std::wstring& full_path, IFileInfo* file, bool isdir)
{
	// 如果是dir，检查是否为空
//	CF2fsFile* f2fs_file = nullptr;
//	if (!file)
//	{	// find file by fn
//		jcvos::auto_interface<IFileInfo> ff;
//		bool br = DokanCreateFile(ff, fn, GENERIC_READ | GENERIC_WRITE, 0, IFileSystem::FS_OPEN_EXISTING, 0, 0, isdir);
//		// file not found
//		if (!br || !ff)
//		{
//			LOG_ERROR(L"[err] file or dir (%s) is not found", fn.c_str());
//			return false;
//		}
//		ff.detach<CF2fsFile>(f2fs_file);	JCASSERT(f2fs_file);
////		ff->CloseFile();
//	}
//	else
//	{
//		f2fs_file = dynamic_cast<CF2fsFile*>(file);		JCASSERT(f2fs_file);
//		f2fs_file->AddRef();
//	}

	// 打开父节点，
	jcvos::auto_interface<CF2fsFile> parent_dir;
	std::wstring fn;
	OpenParent(parent_dir, full_path, fn);

	// 打开文件
	jcvos::auto_interface<CF2fsFile> cur_file;
	parent_dir->_OpenChild(cur_file, fn.c_str(), 0);

	// 调用unlink
	parent_dir->_DeleteChild(cur_file);

//	f2fs_file->Release();
	return true;

}

bool CF2fsFileSystem::Sync(void)
{
	LOG_STACK_TRACE();
	int err = m_sb_info->sync_fs(true);
	if (err) LOG_ERROR(L"[err] failed on sync fs, code=%d", err);
	return err==0;
}

bool CF2fsFileSystem::GetRoot(IFileInfo*& root)
{
	JCASSERT(root == nullptr);
	CF2fsFile* root_dir= nullptr;
	bool br = _GetRoot(root_dir);
	root = static_cast<IFileInfo*>(root_dir);
	return br;
}

bool CF2fsFileSystem::_GetRoot(CF2fsFile*& root)
{
	root = jcvos::CDynamicInstance<CF2fsFile>::Create();
	if (!root) THROW_ERROR(ERR_MEM, L"failed on creating root file");
	root->Init(m_sb_info->s_root, NULL, this, FMODE_READ | FMODE_WRITE);
	return true;
}

bool CF2fsFileSystem::OpenParent(CF2fsFile*& dir, const std::wstring& path, std::wstring& fn)
{
	// 路径解析：得到父节点路径：str_path, 目标文件名：str_fn;
	size_t path_len = path.size();
	jcvos::auto_array<wchar_t> _str_path(path_len + 1);
	wchar_t* str_path = (wchar_t*)_str_path;
	wcscpy_s(str_path, path_len + 1, path.c_str());

	wchar_t* str_fn;
	//	jcvos::auto_interface<IFileInfo> parent_dir;
	wchar_t* ch = str_path + path_len - 1;
	while (*ch != DIR_SEPARATOR && ch >= str_path) ch--;
	str_fn = ch + 1;	// 排除根目录的"\"
	size_t fn_len = path_len - (str_fn - str_path);		// 不包含斜杠
	size_t parent_len = ch - str_path;					// 包含斜杠
	LOG_DEBUG(L"parent=%s, parent len=%zd, file name = %s, length = %zd", ch, parent_len, str_fn, fn_len);

	fn = str_fn;

	jcvos::auto_interface<CF2fsFile> root_dir;
//	jcvos::auto_interface<IFileInfo> parent_dir;
//	jcvos::auto_interface<IFileInfo> _file;
	bool br = _GetRoot(root_dir);
	if (!br || !root_dir) THROW_ERROR(ERR_APP, L"root does not exist");

	if (parent_len == 0)
	{
		dir = root_dir;
		root_dir->AddRef();
	}
	else
	{
		br = root_dir->_OpenChildEx(dir, str_path, parent_len);
		if (!br || !dir || !dir->IsDirectory())
		{
			LOG_ERROR(L"[err] cannot find parent path %s, or non directory", str_path);
			return false;
		}
	}
	return true;
}

void CF2fsFileSystem::f2fs_parse_operation(const std::wstring& vol_name, const std::wstring& str_config)
{
	if (!str_config.empty())
	{
		boost::property_tree::wptree pt;
		std::wstringstream stream;
		stream.str(str_config);
		boost::property_tree::read_json(stream, pt);
	}
	m_config.feature |= F2FS_FEATURE_INODE_CHKSUM;
	m_config.feature |= F2FS_FEATURE_SB_CHKSUM;
	m_vol_name = vol_name;
	//<YUAN>由于num_cache_entry==0的话，不会分配cache，尝试一下
	m_config.cache_config.num_cache_entry = 1;

}

#define MAX_CHUNK_SIZE		(1 * 1024 * 1024 * 1024ULL)
#define MAX_CHUNK_COUNT		(MAX_CHUNK_SIZE / F2FS_BLKSIZE)
int CF2fsFileSystem::f2fs_finalize_device(void)
{
	int i;
	int ret = 0;

	/* We should call fsync() to flush out all the dirty pages in the block device page cache.	 */
	for (i = 0; i < m_config.ndevs; i++)
	{
		//ret = close(c.devices[i].fd);
		//if (ret < 0)
		//{
		//	MSG(0, "\tError: Failed to close device file!!!\n");
		//	break;
		//}
		//free(c.devices[i].path);
		RELEASE(m_config.devices[i].m_fd);
		free(m_config.devices[i].zone_cap_blocks);
	}
//	close(m_config.kd);
	return ret;
}

CBufferHead* CF2fsFileSystem::bread(sector_t block, size_t size)
{
	CBufferHead* bh = m_buffers.GetBlock(block, size);
	if (!bh->buffer_uptodate())
	{
		int err = bh->read_slow();
		if (err)
		{
			LOG_ERROR(L"[err] failed o update buffer");
			bh->Release();
			return NULL;
		}
	}

	return bh;
}


int CF2fsFileSystem::__blkdev_issue_discard(block_device* bdev, sector_t lba, sector_t secs, gfp_t gfp_mask, int flag, bio** bb)
{
	sector_t part_offset = 0;	// partition offset;
	UINT discard_granularity = 128;
	sector_t bio_aligned_discard_max_sectors = MAX_DISCARD_SECTOR;
	UINT op = REQ_OP_DISCARD;
	bio* bio_ptr = *bb;

	while (secs)
	{
		sector_t granularity_aligned_lba, req_sects;
		sector_t sector_mapped = lba + part_offset;

		granularity_aligned_lba = round_up<sector_t>(sector_mapped, (discard_granularity >> SECTOR_SHIFT));

		/* Check whether the discard bio starts at a discard_granularity aligned LBA,
		 * - If no: set (granularity_aligned_lba - sector_mapped) to bi_size of the first split bio, then the second 
		 bio will start at a discard_granularity aligned LBA on the device.
		 * - If yes: use bio_aligned_discard_max_sectors() as the max possible bi_size of the first split bio. Then when
		 this bio is split in device drive, the split ones are very probably to be aligned to discard_granularity of 
		 the device's queue. */
		if (granularity_aligned_lba == sector_mapped)	req_sects = min(secs, bio_aligned_discard_max_sectors);
		else											req_sects = min(secs, granularity_aligned_lba - sector_mapped);

		JCASSERT(!((req_sects << 9) > UINT_MAX));

		//bio_ptr = blk_next_bio(bio_ptr, 0, gfp_mask);
		//<YUAN> 展开blk_next_bio
		bio* new_bio = m_bio_set.bio_alloc_bioset(gfp_mask, 0/*, NULL*/);
		if (bio_ptr)
		{
			bio_chain(bio_ptr, new_bio);
			submit_bio(bio_ptr);
		}
		bio_ptr = new_bio;

		bio_ptr->bi_iter.bi_sector = lba;
		bio_set_dev(bio_ptr, bdev);
		bio_set_op_attrs(bio_ptr, op, 0);

		bio_ptr->bi_iter.bi_size = req_sects << 9;
		lba += req_sects;
		secs -= req_sects;

		/* We can loop for a long time in here, if someone does full device discards (like mkfs). Be nice and allow
		 * us to schedule out to avoid softlocking if preempt is disabled. */
//		cond_resched();
	}
	bb = &bio_ptr;
	return 0;
}

void f2fs_sb_info::dec_valid_block_count(struct inode* inode, block_t count)
{
	blkcnt_t sectors = count << F2FS_LOG_SECTORS_PER_BLOCK;

	spin_lock(&stat_lock);
	f2fs_bug_on(this, total_valid_block_count < (block_t)count);
	total_valid_block_count -= (block_t)count;
	if (reserved_blocks && current_reserved_blocks < reserved_blocks)
	{
		current_reserved_blocks = min(reserved_blocks, current_reserved_blocks + count);
	}
	spin_unlock(&stat_lock);
	if (unlikely(inode->i_blocks < sectors))
	{
		f2fs_warn(this, L"Inconsistent i_blocks, ino:%lu, iblocks:%llu, sectors:%llu",
			  inode->i_ino,  (unsigned long long)inode->i_blocks,  (unsigned long long)sectors);
		set_sbi_flag(SBI_NEED_FSCK);
		return;
	}
	f2fs_i_blocks_write(F2FS_I(inode), count, false, true);
}

//bio* f2fs_sb_info::__bio_alloc(f2fs_io_info* fio, int npages)
//{
//	return m_fs->__bio_alloc(fio, npages);
//}


///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// == factory

jcvos::CStaticInstance<CF2fsFactory> g_factory;

extern "C" __declspec(dllexport) bool GetFactory(IFsFactory * &fac)
{
	JCASSERT(fac == NULL);
	fac = static_cast<IFsFactory*>(&g_factory);
	return true;
}

bool CF2fsFactory::CreateFileSystem(IFileSystem*& fs, const std::wstring& fs_name)
{
	JCASSERT(fs == NULL);
	fs = jcvos::CDynamicInstance<CF2fsFileSystem>::Create();
	return true;
}

bool CF2fsFactory::CreateVirtualDisk(IVirtualDisk*& dev, const boost::property_tree::wptree& prop, bool create)
{
	return false;
}

bool f2fs_sb_info::f2fs_is_checkpoint_ready(void)
{
	if (likely(!is_sbi_flag_set(SBI_CP_DISABLED))) 	return true;
	if (likely(!has_not_enough_free_secs(this, 0, 0)))		return true;
	return false;
}
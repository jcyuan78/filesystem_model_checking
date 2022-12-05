///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
#pragma once

#include <dokanfs-lib.h>
#include <linux-fs-wrapper.h>
#include "f2fs_fs.h"
#include "f2fs.h"

//<YUAN>这是一个device的属性，一个命令最大可发送的sector数量。暂时用const代替
#define MAX_DISCARD_SECTOR		(256)

class CF2fsFileSystem;

class CF2fsFile : public IFileInfo
{
public:
	CF2fsFile(void):m_dentry(NULL), m_inode(NULL), m_fs(NULL) {};
	~CF2fsFile(void);
	void Init(dentry* de, inode* node, CF2fsFileSystem * fs, UINT32 mode);
public:
	virtual void Cleanup(void) { }
	virtual void CloseFile(void);
	virtual bool DokanReadFile(LPVOID buf, DWORD len, DWORD& read, LONGLONG offset);
	virtual bool DokanWriteFile(const void* buf, DWORD len, DWORD& written, LONGLONG offset);

	virtual bool LockFile(LONGLONG offset, LONGLONG len) {UNSUPPORT_1(bool);}
	virtual bool UnlockFile(LONGLONG offset, LONGLONG len) {UNSUPPORT_1(bool);}
	virtual bool EnumerateFiles(EnumFileListener* listener) const;

	virtual bool GetFileInformation(LPBY_HANDLE_FILE_INFORMATION fileinfo) const;
	virtual std::wstring GetFileName(void) const;

	virtual bool DokanGetFileSecurity(SECURITY_INFORMATION psinfo, PSECURITY_DESCRIPTOR psdesc, ULONG& buf_size) {UNSUPPORT_1(bool);}
	virtual bool DokanSetFileSecurity(PSECURITY_INFORMATION psinfo, PSECURITY_DESCRIPTOR psdesc, ULONG buf_size) {UNSUPPORT_1(bool);}
	// for dir only
	virtual bool IsDirectory(void) const {	return m_inode->is_dir();	}
	virtual bool IsEmpty(void) const;			// 对于目录，返回目录是否为空；对于非目录，返回true.

	virtual bool SetAllocationSize(LONGLONG size);
	virtual bool SetEndOfFile(LONGLONG);
	virtual void DokanSetFileAttributes(DWORD attr);

	virtual void SetFileTime(const FILETIME* ct, const FILETIME* at, const FILETIME* mt);
	virtual bool FlushFile(void);

	virtual void GetParent(IFileInfo*& parent);

	// 删除所有给文件分配的空间。如果是目录，删除目录下的所有文件。
	virtual void ClearData(void) { UNSUPPORT_0; }

	virtual bool OpenChild(IFileInfo*& file, const wchar_t* fn, UINT32 mode) const;
	virtual bool OpenChildEx(IFileInfo*& file, const wchar_t* fn, size_t len);
	virtual bool CreateChild(IFileInfo*& file, const wchar_t* fn, bool dir, UINT32 mode);
	// 当Close文件时，删除此文件。判断条件由DokanApp实现。有些应用（Explorer）会通过这个方法删除文件。
	virtual void SetDeleteOnClose(bool del) { m_delete_on_close = del; }

public:
	friend class CF2fsFileSystem;
	template <class T> T* GetInode(void) { return dynamic_cast<T*>(m_inode); }
	dentry* GetDentry(void) { return m_dentry; }
	int DeleteThis(void);	// 删除此文件
protected:
	bool _OpenChildEx(CF2fsFile*& file, const wchar_t* fn, size_t len);
	//void _DeleteChild(CF2fsFile* child);
	int _DeleteChild(const std::wstring & fn);
	bool _OpenChild(CF2fsFile*& file, const wchar_t* fn, UINT32 mode) const;
	static void InodeToInfo(BY_HANDLE_FILE_INFORMATION& info, f2fs_inode_info* iinode);

protected:
	dentry* m_dentry;
	f2fs_inode_info* m_inode;
	CF2fsFileSystem* m_fs;
	file m_file;
	bool m_delete_on_close = false;
	// 为防止多线程是 m_dentry和m_inode被删除的同时，进行其他操作。
	long m_valid = 0;
};

class CF2fsSpecialFile : public IFileInfo
{
public:
//	CF2fsSpecialFile(CF2fsFileSystem * fs) :m_fs(fs) {};
	CF2fsSpecialFile(void) : m_fs(nullptr) {};
	~CF2fsSpecialFile(void);
	void Init(CF2fsFileSystem* fs, const std::wstring& fn, UINT id)
	{
		m_fs = fs;
		m_fn = fn;
		m_fid = id;
	}

public:
	virtual void Cleanup(void) { }
	virtual void CloseFile(void) {};
	virtual bool DokanReadFile(LPVOID buf, DWORD len, DWORD& read, LONGLONG offset);
	virtual bool DokanWriteFile(const void* buf, DWORD len, DWORD& written, LONGLONG offset) {	return false; };

	virtual bool LockFile(LONGLONG offset, LONGLONG len) { UNSUPPORT_1(bool); }
	virtual bool UnlockFile(LONGLONG offset, LONGLONG len) { UNSUPPORT_1(bool); }
	virtual bool EnumerateFiles(EnumFileListener* listener) const { return false; }

	virtual bool GetFileInformation(LPBY_HANDLE_FILE_INFORMATION fileinfo) const;
	virtual std::wstring GetFileName(void) const;

	virtual bool DokanGetFileSecurity(SECURITY_INFORMATION psinfo, PSECURITY_DESCRIPTOR psdesc, ULONG& buf_size) { UNSUPPORT_1(bool); }
	virtual bool DokanSetFileSecurity(PSECURITY_INFORMATION psinfo, PSECURITY_DESCRIPTOR psdesc, ULONG buf_size) { UNSUPPORT_1(bool); }
	// for dir only
	virtual bool IsDirectory(void) const { return false; }
	virtual bool IsEmpty(void) const { return true; }			// 对于目录，返回目录是否为空；对于非目录，返回true.

	virtual bool SetAllocationSize(LONGLONG size) { UNSUPPORT_1(bool); }
	virtual bool SetEndOfFile(LONGLONG) { UNSUPPORT_1(bool); }
	virtual void DokanSetFileAttributes(DWORD attr) { UNSUPPORT_0; }

	virtual void SetFileTime(const FILETIME* ct, const FILETIME* at, const FILETIME* mt) { UNSUPPORT_0; }
	virtual bool FlushFile(void) { return true; }

	virtual void GetParent(IFileInfo*& parent) {}

	// 删除所有给文件分配的空间。如果是目录，删除目录下的所有文件。
	virtual void ClearData(void) { UNSUPPORT_0; }

	virtual bool OpenChild(IFileInfo*& file, const wchar_t* fn, UINT32 mode) const { UNSUPPORT_1(bool); }
	virtual bool OpenChildEx(IFileInfo*& file, const wchar_t* fn, size_t len) { UNSUPPORT_1(bool); }
	virtual bool CreateChild(IFileInfo*& file, const wchar_t* fn, bool dir, UINT32 mode) { UNSUPPORT_1(bool); }
	// 当Close文件时，删除此文件。判断条件由DokanApp实现。有些应用（Explorer）会通过这个方法删除文件。
	virtual void SetDeleteOnClose(bool del) { }

	template <typename T> T* GetData(void) { return (T*)(m_data_ptr); }
public:
	friend class CF2fsFileSystem;

protected:
	CF2fsFileSystem* m_fs;
	void* m_data_ptr;
	size_t m_data_size;
	std::wstring m_fn;
	DWORD m_fid;
};

template <typename T> class CSpecialFile : public CF2fsSpecialFile
{
public:
	CSpecialFile<T>(/*CF2fsFileSystem * fs, const std::wstring & name, UINT id*/) 
	{
		//m_fs = fs;
		m_data_ptr = (void*)(&m_data); 
		m_data_size = sizeof(T);
		//m_fn = name;
		//m_fid = id;
		memset(&m_data, 0, sizeof(T));
	}
public:
	T m_data;
};


typedef UINT64 off64_t;

#define DIR_SEPARATOR	('\\')

enum SPECIAL_FILE_ID
{
	SFID_HEALTH,
	SFID_MAX_FILE_NUM,
};

class CF2fsFileSystem : public IFileSystem
{
public:
	CF2fsFileSystem(void);
	virtual ~CF2fsFileSystem(void);

public:
	friend struct f2fs_sb_info;
	friend class CF2fsFile;

public:
	// 创建一个相同类型的file system object。创建的对象时空的，需要初始化。
	// 考虑将这个方法放到IJCInterface中
	virtual bool CreateObject(IJCInterface*& fs) { JCASSERT(0); return 0; }
	virtual ULONG GetFileSystemOption(void) const;
	virtual bool Mount(IVirtualDisk* dev);
	virtual void Unmount(void);
	virtual bool MakeFileSystem(IVirtualDisk* dev, UINT32 volume_size, const std::wstring& volume_name, const std::wstring & options);
	// fsck，检查文件系统，返回检查结果
	virtual FsCheckResult FileSystemCheck(IVirtualDisk* dev, bool repair) { JCASSERT(0); return CheckNoError; }

	virtual bool DokanGetDiskSpace(ULONGLONG& free_bytes, ULONGLONG& total_bytes, ULONGLONG& total_free_bytes);
	virtual bool GetVolumnInfo(std::wstring& vol_name, DWORD& sn, DWORD& max_fn_len, DWORD& fs_flag, std::wstring& fs_name);

	// file attribute (attr) and create disposition (disp) is in user mode 
	virtual NTSTATUS DokanCreateFile(IFileInfo*& file, const std::wstring& fn, ACCESS_MASK access_mask,
		DWORD attr, FsCreateDisposition disp, ULONG share, ULONG opt, bool isdir);
	virtual bool MakeDir(const std::wstring& dir) { JCASSERT(0); return 0; }

	virtual NTSTATUS DokanDeleteFile(const std::wstring& fn, IFileInfo* file, bool isdir);
	//virtual void FindFiles(void) = 0;
	virtual void FindStreams(void) { JCASSERT(0); }
	virtual NTSTATUS DokanMoveFile(const std::wstring& src_fn, const std::wstring& dst_fn, bool replace, IFileInfo* file);

	virtual bool HardLink(const std::wstring& src, const std::wstring& dst) { JCASSERT(0); return 0; }
	virtual bool Unlink(const std::wstring& fn) { JCASSERT(0); return 0; }
	virtual bool Sync(void);
	virtual bool ConfigFs(const boost::property_tree::wptree& pt);
	virtual int GetDebugMode(void) const { return m_debug_mode; }

public:
	virtual bool GetRoot(IFileInfo* &root);

protected:
	bool OpenParent(CF2fsFile*& dir, const std::wstring & path, std::wstring &fn);
	bool _GetRoot(CF2fsFile*& root);

	NTSTATUS OpenSpecialFile(IFileInfo*& file, const std::wstring& paht);
	void InitialSpecialFileList(void);
	void DeleteSpecialFileList(void);
	// special files
	typedef CF2fsSpecialFile* LP_SPECIALFILE;
	LP_SPECIALFILE* m_special_file_list;
	DokanHealthInfo m_health_info;
public:
	DWORD GetSpecialData(LPVOID buf, DWORD len, UINT id);
	void UpdateHostWriteNr(UINT64 bytes, UINT64 blocks) 
	{
		InterlockedAdd64((LONG64*)&(m_health_info.m_total_host_write), bytes);
		InterlockedAdd64((LONG64*)&(m_health_info.m_block_host_write), blocks);
	}
	void UpdateDiskWrite(size_t blocks) { InterlockedAdd64((LONG64*)&(m_health_info.m_block_disk_write), blocks); }


protected:
	// 文件系统参数, mount 参数
	MOUNT_OPTION m_mount_opt;
	int m_debug_mode=0;
	// other functions
public:
	inline int get_inline_xattr_addrs(f2fs_inode* inode)
	{
		if (m_config.feature & cpu_to_le32(F2FS_FEATURE_FLEXIBLE_INLINE_XATTR))	
			return le16_to_cpu(inode->_u._s.i_inline_xattr_size);
		else if (inode->i_inline & F2FS_INLINE_XATTR ||	inode->i_inline & F2FS_INLINE_DENTRY)	
			return DEFAULT_INLINE_XATTR_ADDRS;
		else			return 0;
	}
	inline int set_feature_bits(struct feature* table, char* features)
	{
		UINT32 mask = feature_map(table, features);
		if (mask) { m_config.feature |= cpu_to_le32(mask); }
		else
		{
//			LOG_ERROR(L"[err] Wrong features %s\n", features);
			wprintf_s(L"[err] Wrong features %S\n", features);
			return -1;
		}
		return 0;
	}
	unsigned int addrs_per_inode(f2fs_inode* i)
	{
		unsigned int addrs = CUR_ADDRS_PER_INODE_(i) - get_inline_xattr_addrs(i);
		if (!LINUX_S_ISREG(le16_to_cpu(i->i_mode)) || !(le32_to_cpu(i->i_flags) & F2FS_COMPR_FL))	return addrs;
		return ALIGN_DOWN(addrs, 1 << i->_u._s.i_log_cluster_size);
	}
	
	inline int parse_feature(struct feature* table, const char* features)
	{
		char* buf, * sub, * next;
		buf = _strdup(features);
		if (!buf)		return -1;
		for (sub = buf; sub && *sub; sub = next ? next + 1 : NULL)
		{
			/* Skip the beginning blanks */
			while (*sub && *sub == ' ')		sub++;
			next = sub;
			/* Skip a feature word */
			while (*next && *next != ' ' && *next != ',')	next++;
			if (*next == 0)		next = NULL;
			else				*next = 0;
			if (set_feature_bits(table, sub))
			{
				free(buf);
				return -1;
			}
		}
		free(buf);
		return 0;
	}


protected:
	virtual bool ConnectToDevice(IVirtualDisk* dev);
	virtual void Disconnect(void);
	void Reset(void);

// format related funcitons
protected:
	int f2fs_format_device(f2fs_configuration& config);
	int f2fs_prepare_super_block(f2fs_configuration& config);
	int f2fs_trim_devices(f2fs_configuration& config);
	int f2fs_init_sit_area(f2fs_configuration& config);
	int f2fs_init_nat_area(f2fs_configuration&);
	int f2fs_create_root_dir(f2fs_configuration& config);
	int f2fs_write_check_point_pack(f2fs_configuration& config);
	int f2fs_write_super_block(f2fs_configuration& config);
	int f2fs_add_default_dentry_root(f2fs_configuration & config);
	int f2fs_write_lpf_inode(f2fs_configuration & config);
	block_t f2fs_add_default_dentry_lpf(f2fs_configuration & config);
	int f2fs_update_nat_root(f2fs_configuration & config);
	int f2fs_write_qf_inode(f2fs_configuration & config, int qtype);
	int f2fs_write_root_inode(f2fs_configuration & config);
	int f2fs_discard_obsolete_dnode(f2fs_configuration & config);
	bool is_extension_exist(const char* name);
	void cure_extension_list(f2fs_configuration & config);

	void f2fs_init_configuration(f2fs_configuration& config);
	void f2fs_parse_operation(const std::wstring & vol_name, const std::wstring& str_config);
	int f2fs_get_device_info(f2fs_configuration& config);
	int f2fs_finalize_device(void);
	int f2fs_write_default_quota(int qtype, unsigned int blkaddr, __le32 raw_id);

public:
	inline __u32 f2fs_inode_chksum(page* pp) { return f2fs_inode_chksum(F2FS_NODE(pp)); }
protected:
	__u32 f2fs_inode_chksum(struct f2fs_node* node);

	inline int write_inode(f2fs_node* inode, UINT64 blkaddr)
	{
		if (m_config.feature & cpu_to_le32(F2FS_FEATURE_INODE_CHKSUM))
			inode->i._u._s.i_inode_checksum = cpu_to_le32(f2fs_inode_chksum(inode));
		return dev_write_block((BYTE*)inode, blkaddr);
	}


// io related functions
protected:
	int dev_read(BYTE* buf, __u64 offset, size_t len);
	int dev_read_block(void* buf, __u64 blk_addr);
	int dev_reada_block(__u64 blk_addr);
	int dev_write(BYTE* buf, __u64 offset, size_t len);
	int dev_write_block(BYTE* buf, __u64 blk_addr);
	int dev_write_dump(void* buf, __u64 offset, size_t len);
	int dev_fill(void* buf, __u64 offset, size_t len);
	int dev_fill_block(void* buf, __u64 blk_addr);
	int dev_read_version(void* buf, __u64 offset, size_t len);



	IVirtualDisk* __get_device(__u64* offset);
	int dcache_update_rw(IVirtualDisk* disk, BYTE* buf, UINT64 offset, size_t byte_count, bool is_write);
	void dcache_init(void);
	void dcache_release(void);
	int dcache_alloc_all(long n);
	void dcache_relocate_init(void);
	long dcache_find(UINT64 blk);
	int dcache_io_read(IVirtualDisk * disk, long entry, UINT64 offset, UINT64 blk);
	int dev_readahead(__u64 offset, size_t len);
	void dcache_print_statistics(void);

	/* relocate on (n+1)-th collision */
	inline long dcache_relocate(long entry, int n)
	{
		JCASSERT(m_dcache_config.num_cache_entry != 0);
		return (entry + m_dcache_relocate_offset[n]) % m_dcache_config.num_cache_entry;
	}

	inline char* dcache_addr(long entry)
	{
		return m_dcache_buf + F2FS_BLKSIZE * entry;
	}

	// == bio
public:
	static void f2fs_write_end_io(bio* bb);
	void write_end_io(bio* bb);

	/* submit_bio_wait - submit a bio, and wait until it completes
	 * @bio: The &struct bio which describes the I/O
	 * Simple wrapper around submit_bio(). Returns 0 on success, or the error from bio_endio() on failure.
	 * WARNING: Unlike to how submit_bio() is usually used, this function does not result in bio reference to be consumed. The caller must drop the reference on his own. */
	//int submit_bio_wait(bio* bbio);


//<YUAN> from block/blk-flush.c
/* blkdev_issue_flush - queue a flush
 * @bdev:	blockdev to issue flush for
 * Description:    Issue a flush for the block device in question. */

	inline int blkdev_issue_flush(block_device* bdev)
	{
		struct bio bio;

		bio_init(&bio, NULL, 0);
		bio_set_dev(&bio, bdev);
		bio.bi_opf = REQ_OP_WRITE | REQ_PREFLUSH;
		return m_sb_info->submit_bio_wait(&bio);
	}

	// 模拟Linux Block IO
	//void submit_bio(bio* bb);
	//inline void __submit_bio(bio* bio, enum page_type type);
//	bio* __bio_alloc(f2fs_io_info* fio, int npages);
	int __blkdev_issue_discard(block_device * , sector_t lba, sector_t len, gfp_t gfp_mask, int flag, bio **);
// == data.cpp
public:
//	int f2fs_submit_page_bio(f2fs_io_info* fio);
	bio* f2fs_grab_read_bio(f2fs_inode_info* inode, block_t blkaddr, unsigned nr_pages, unsigned op_flag, pgoff_t first_idx, bool for_write);

	//int sync_filesystem(void) { JCASSERT(0); return 0; }
	unsigned int sb_set_blocksize(unsigned int size)
	{
		// Linux原代码中调用set_blocksize(sb->bdev, size), 只是做sanity check，
		//UINT bits = blksize_bits(size);
		m_sb_info->s_blocksize = size;
		m_sb_info->s_blocksize_bits = blksize_bits(size);
		return m_sb_info->s_blocksize;
	}


protected:
	// ---------- dev_cache, Least Used First (LUF) policy  ------------------- 
	// Least used block will be the first victim to be replaced when max hash collision exceeds
	bool* m_dcache_valid; /* is the cached block valid? */
	UINT64* m_dcache_blk; /* which block it cached */
	uint64_t* m_dcache_lastused; /* last used ticks for cache entries */
	char* m_dcache_buf; /* cached block data */
	uint64_t m_dcache_usetick; /* current use tick */

	uint64_t m_dcache_raccess;
	uint64_t m_dcache_rhit;
	uint64_t m_dcache_rmiss;
	uint64_t m_dcache_rreplace;

	bool m_dcache_exit_registered = false;

	// Shadow config:
	// Active set of the configurations. Global configuration 'dcache_config' will be transferred here when when dcache_init() is called
	dev_cache_config_t m_dcache_config = { 0, 16, 1 };
	bool m_dcache_initialized = false;

	//<YUAN>这个有可能时常数
	long m_dcache_relocate_offset0[16] = {
		20, -20, 40, -40, 80, -80, 160, -160,
		320, -320, 640, -640, 1280, -1280, 2560, -2560,
	};
	int m_dcache_relocate_offset[16];

	//destory
protected:
//	void destory_super(void);
//	void destroy_device_list(void);

// == super.cpp
protected:
	int parse_mount_options(super_block* sb, const boost::property_tree::wptree& options, bool is_mount);
	// 从 buffer.c __bread_gfp()移植
	//	block为block地址，sector = block * size / sector_size. 参考"buffer.c" submit_bh_wbc()
	CBufferHead* bread(sector_t block, size_t size);

// == checkpoing.cpp

// == node.cpp
public:
//	int read_node_page(page* page, int op_flags);

// == shrinker.cpp
protected:
//	void f2fs_join_shrinker(void);
	//void f2fs_leave_shrinker(void);


public:


protected:
	list_head m_f2fs_list;
	CRITICAL_SECTION m_f2fs_list_lock;


public:


protected:

	
#if 0
	template <class NODE_TYPE> NODE_TYPE* NewInode(void)
	{
		NODE_TYPE* node = new NODE_TYPE(m_sb_info);
		inode* base_node = static_cast<inode*>(node);
		m_inodes.new_inode( base_node);
		return node;
	}
//	f2fs_inode_info* _internal_new_inode(f2fs_inode_info* new_node, f2fs_inode_info* dir, umode_t mode);


	template <class INODE_T>
	f2fs_inode_info* f2fs_new_inode(f2fs_inode_info* dir, umode_t mode)
	{
		INODE_T* new_node = NewInode<INODE_T>();
		f2fs_inode_info * node = static_cast<f2fs_inode_info*>(new_node);
		node->m_sbi = m_sb_info;
		int err = m_inodes.insert_inode_locked(node);
		if (err)
		{
			node->make_bad_inode();
			set_inode_flag(node, FI_FREE_NID);
			iput(node);
			return ERR_PTR<f2fs_inode_info>(err);
		}
		err = node->_internal_new_inode(dir, mode);
		if (err)
		{
			iput(node);
			return ERR_PTR<f2fs_inode_info>(err);
		}
		return node;
	}
#endif
public:
	CBioSet m_bio_set;

// ==== 全局变量局部化
public:
	kmem_cache* fsync_entry_slab;

protected:
	CBufferManager	m_buffers;

	f2fs_sb_info	* m_sb_info=nullptr;
	//super_block		m_super_block;

	f2fs_super_block m_raw_sb;
//	f2fs_super_block* sb = &m_raw_sb;

	f2fs_checkpoint* cp = nullptr;
	f2fs_configuration m_config;
	std::wstring m_vol_name;
};


class CF2fsFactory : public IFsFactory
{
public:
	virtual bool CreateFileSystem(IFileSystem*& fs, const std::wstring& fs_name);
	virtual bool CreateVirtualDisk(IVirtualDisk*& dev, const boost::property_tree::wptree& prop, bool create);
};

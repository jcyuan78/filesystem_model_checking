///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
#include "pch.h"
#include <linux-fs-wrapper.h>
#include "../include/config.h"
#include "../include/f2fs-filesystem.h"
#include <boost/property_tree/json_parser.hpp>

LOCAL_LOGGER_ENABLE(L"f2fs.fileinfo", LOGGER_LEVEL_DEBUGINFO);


CF2fsFile::~CF2fsFile(void)
{
	if (m_dentry)
	{
		dput(m_dentry);			// dput可以接受null参数，被忽略
		m_dentry = nullptr;
	}
}

void CF2fsFile::Init(dentry* de, inode* node, CF2fsFileSystem* fs, UINT32 mode)
{
	// 在CF2fsFile中引用dentry是的计数规则：（包括root)，
	//	以下三个调用，(1) f2fs_inode_info::create(), (2)  f2fs_inode_info::mkdir() (3)  f2fs_inode_info::lookup()会增加dentry计数。
	//	以及(4) d_alloc()中，对父节点进行计数增加。
	//	对于(1)~(3)当结果调用Init()后，需要减少计数。对于（4）在dput()中会对父节点减少计数
	// 对于CF2fsFile，在Init()中增加dentry计数，在Close()或者析构函数中减少计数。

	JCASSERT(de);
	m_dentry = dget(de);
	if (node == NULL)	{	node = de->d_inode;	}
	m_inode = dynamic_cast<f2fs_inode_info*>(node);
	if (m_inode == NULL) THROW_ERROR(ERR_APP, L"[err] inode is null or wrong type");
	m_file.init(m_inode);
	m_file.f_mode |= mode;
	m_file.f_ra.ra_pages = MAX_IO_BLOCKS;	// 一次最多读取256 sectors, 32 pages
	m_fs = m_inode->m_sbi->m_fs;
	LOG_TRACK(L"dentry", L"entry=%p,ref=%d", m_dentry, m_dentry->d_lockref.count);
	TRACK_INODE(m_inode, L"");
	InterlockedExchange(&m_valid, 1);
}

void CF2fsFile::operator delete(void* ptr)
{
	CF2fsFile *pp = (CF2fsFile*)ptr;
	//JCASSERT(pp == this);
	pp->m_manager->free_obj(ptr);
}

int CF2fsFile::DeleteThis(void)
{
	if (InterlockedExchange(&m_valid, 0)==0) return 0;
	if (m_inode->is_dir() && !m_inode->f2fs_empty_dir())
	{
		LOG_ERROR(L"[err] directory %S is not empty", m_dentry->d_name.name.c_str());
		return  -ENOTEMPTY;
	}
	dentry* parent_entry = m_dentry->get_parent();
	JCASSERT(parent_entry);
	f2fs_inode_info* parent_inode = parent_entry->get_inode<f2fs_inode_info>();
	JCASSERT(parent_inode);
	int err = parent_inode->unlink(m_dentry);
	if (err)
	{
		LOG_ERROR(L"[err] failed on delete file %S, error=%d", m_dentry->d_name.name.c_str(), err);
		return err;
	}
	d_delete(m_dentry);
	m_inode = nullptr;
	// d_delete之后，m_inode对象已经被删除。
	parent_inode->m_sbi->f2fs_balance_fs(true);
	dput(m_dentry);
	m_dentry = nullptr;
	return 0;
}

void CF2fsFile::CloseFile(void)
{
//	size_t page_cache_size, page_cache_free, page_cache_inactive, page_cache_active;
	m_fs->SendMarkToDrive(L"Close");

	CPageManager * pm = m_fs->m_sb_info->GetPageManager();

	LOG_STACK_TRACE();
	if (m_delete_on_close)
	{
		LOG_DEBUG_(1,L"delete on close");
		DeleteThis();
		return;
	}
	if (InterlockedExchange(&m_valid, 0))
	{
#ifdef _DEBUG
		// dump inode, direct nodes and indirect nodes
		m_inode->DumpInodeMapping();
		// dump data blocks
#endif
		JCASSERT(m_dentry && m_inode);
		TRACK_INODE(m_inode, L"closing file");
		m_inode->release_file(&m_file);
		dput(m_dentry);
		LOG_TRACK(L"dentry", L"entry=%p, inode=%p, ref=%d", m_dentry, m_dentry->d_inode, m_dentry->d_lockref.count);
		m_dentry = nullptr;
		m_inode = nullptr;
	}
//	m_fs->m_sb_info->DumpSegInfo();
}

bool CF2fsFile::DokanReadFile(LPVOID buf, DWORD len, DWORD& read, LONGLONG offset)
{
	LOG_STACK_TRACE();
	read = 0;
	if (InterlockedAdd(&m_valid, 0) == 0) return true;
	LOG_TRACK(L"fs", L",read,file=%S,offset=%lld,len=%d", m_dentry->d_name.name.c_str(), offset, len);
	Cf2fsFileNode* file_node = dynamic_cast<Cf2fsFileNode*>(m_inode);
	if (!file_node) THROW_ERROR(ERR_APP, L"the inode is not a file (%X) or is null", m_inode);

	iovec iov;
	iov.iov_base = buf;
	iov.iov_len = len;

	struct kiocb kiocb;
	iov_iter iter;	// 保存读取结果
	ssize_t ret;

	init_sync_kiocb(&kiocb, &m_file);
	kiocb.ki_pos = offset;
	iov_iter_init(&iter, READ, &iov, 1, len);

	ret = file_node->read_iter(&kiocb, &iter);
	read = (DWORD)ret;
	return (INT64)ret > 0;
}

bool CF2fsFile::DokanWriteFile(const void* buf, DWORD len, DWORD& written, LONGLONG offset)
{
	LOG_STACK_TRACE();
	written = 0;
	if (InterlockedAdd(&m_valid, 0) == 0) return true;

	LOG_TRACK(L"fs", L",write,file=%S,offset=%lld,len=%d", m_dentry->d_name.name.c_str(), offset, len);

	Cf2fsFileNode* file_node = dynamic_cast<Cf2fsFileNode*>(m_inode);
	if (!file_node) THROW_ERROR(ERR_APP, L"the inode is not a file (%X) or is null", m_inode);
	written = 0;
	size_t new_size = offset + len;
	size_t old_size = m_inode->GetFileSize();
	if (old_size < new_size)
	{	// Set new size
		iattr attr;
		memset(&attr, 0, sizeof(attr));
		attr.ia_valid |= ATTR_SIZE;
		attr.ia_size = new_size;

		int err = m_inode->setattr(nullptr, m_dentry, &attr);
		if (err)
		{
			LOG_ERROR(L"[err] failed on set file size to %zd, err=%d", new_size, err);
			return false;
		}
	}

	iovec iov;
	iov.iov_base = const_cast<void*>(buf);
	iov.iov_len = len;

	kiocb iob;
	iov_iter iter;
	ssize_t ret;

	init_sync_kiocb(&iob, &m_file);
	iob.ki_pos = offset;
	iov_iter_init(&iter, WRITE, &iov, 1, len);

	ret = file_node->write_iter(&iob, &iter);
	written = (DWORD)ret;

	// 计算写入量
	size_t start_block = offset / PAGE_SIZE;
	size_t end_block = ROUND_UP_DIV((offset + written), PAGE_SIZE);
	size_t block_nr = end_block - start_block;
	m_fs->UpdateHostWriteNr(written, block_nr);

	return (INT64)ret > 0;
}

inline void ToFileTime(FILETIME& ft, const time64_t tt)
{
	ft.dwHighDateTime = HIDWORD(tt);
	ft.dwLowDateTime = LODWORD(tt);
}
inline time64_t FromFileTime(const FILETIME& ft)
{
	time64_t tt = MAKEQWORD(ft.dwLowDateTime, ft.dwHighDateTime);
	return tt;
}

bool CF2fsFile::EnumerateFiles(EnumFileListener* listener) const
{
	JCASSERT(m_inode);
	Cf2fsDirInode* dir = dynamic_cast<Cf2fsDirInode*>(m_inode);
	if (dir == nullptr) THROW_ERROR(ERR_APP, L"only dir support enumerate");
	std::list<dentry*> child_list;
	dir->enum_childs(m_dentry, child_list);
	int index = 0;
	for (auto it = child_list.begin(); it != child_list.end(); ++it, ++index)
	{
		dentry* entry = *it;
		std::wstring fn;
		jcvos::Utf8ToUnicode(fn, entry->d_name.name);
		BY_HANDLE_FILE_INFORMATION info;
		InodeToInfo(info, F2FS_I(entry->d_inode));
		bool br = listener->EnumFileCallback(fn, entry->d_inode->i_ino, index, &info);
		dput(entry);
//		if (!br) break;
	}
	child_list.clear();
	return true;
}

bool CF2fsFile::GetFileInformation(LPBY_HANDLE_FILE_INFORMATION fileinfo) const
{
//	LOG_STACK_TRACE();
	LOG_TRACK(L"fs", L"file_info, %S", m_dentry->d_name.name.c_str());
	if (m_valid == 0) return true;

	if (fileinfo)
	{
		InodeToInfo(*fileinfo, m_inode);
#if 0 //<DEBUG>
		SYSTEMTIME t;
		FILETIME ft = fileinfo->ftCreationTime;
		FileTimeToSystemTime(&ft, &t);
		LOG_DEBUG_(1, L"create time, hi=%d, lo=%d, t=%d-%d-%d:%d:%d:%d", ft.dwLowDateTime, ft.dwHighDateTime, t.wYear, t.wMonth, t.wDay, t.wHour, t.wMinute, t.wSecond);

		ft = fileinfo->ftLastAccessTime;
		FileTimeToSystemTime(&ft, &t);
		LOG_DEBUG_(1, L"access time, hi=%d, lo=%d, t=%d-%d-%d:%d:%d:%d", ft.dwLowDateTime, ft.dwHighDateTime, t.wYear, t.wMonth, t.wDay, t.wHour, t.wMinute, t.wSecond);
		ft = fileinfo->ftLastWriteTime;
		FileTimeToSystemTime(&ft, &t);
		LOG_DEBUG_(1, L"modify time, hi=%d, lo=%d, t=%d-%d-%d:%d:%d:%d", ft.dwLowDateTime, ft.dwHighDateTime, t.wYear, t.wMonth, t.wDay, t.wHour, t.wMinute, t.wSecond);
#endif
	}
	return true;
}

std::wstring CF2fsFile::GetFileName(void) const
{
	if (m_valid == 0) return L"";
	std::wstring str_fn;
	jcvos::Utf8ToUnicode(str_fn, m_dentry->d_name.name);
	return str_fn;
}

bool CF2fsFile::IsEmpty(void) const
{
	if (m_valid == 0) return true;
	return m_inode->f2fs_empty_dir();
}

bool CF2fsFile::SetAllocationSize(LONGLONG size)
{
	if (InterlockedAdd(&m_valid, 0) == 0) return true;
	//Cf2fsFileNode* file = dynamic_cast<Cf2fsFileNode*>(m_inode);
	//if (!file) THROW_ERROR(ERR_APP, L"the inode is not a file (%X) or is null", m_inode);
	//// 获取文件现在的长度
	//loff_t offset = file->GetFileSize();
	//if ((loff_t)size < offset)
	//{
	//	LOG_ERROR(L"[err] not support truncate now, cur size=%lld, new size=%lld", offset, size);
	//	return false;
	//}
	//loff_t new_size = size - offset;
	////	int mode = FALLOC_FL_INSERT_RANGE;	// 增加文件长度，并且以洞填充，好像无法在文件末尾怎加长度
	//int mode = FALLOC_FL_ZERO_RANGE;	// 增加文件长度，并且以0填充
	//int err = file->fallocate(mode, offset, new_size);
	//return (err == 0);

	// SetAllocationSize: 相当于WinAPI的 SetFileValidData()，仅改变文件的逻辑长度。//<TODO>优化
	Cf2fsFileNode* file = dynamic_cast<Cf2fsFileNode*>(m_inode);
	if (!file) THROW_ERROR(ERR_APP, L"the inode is not a file (%X) or is null", m_inode);

	iattr attr;
	memset(&attr, 0, sizeof(attr));
	attr.ia_valid |= ATTR_SIZE;
	attr.ia_size = size;

	int err = m_inode->setattr(nullptr, m_dentry, &attr);
	return (err == 0);
}

bool CF2fsFile::SetEndOfFile(LONGLONG size)
{
	if (InterlockedAdd(&m_valid, 0) == 0) return true;
	Cf2fsFileNode* file = dynamic_cast<Cf2fsFileNode*>(m_inode);
	if (!file) THROW_ERROR(ERR_APP, L"the inode is not a file (%X) or is null", m_inode);

	iattr attr;
	memset(&attr, 0, sizeof(attr));
	attr.ia_valid |= ATTR_SIZE;
	attr.ia_size = size;

	int err = m_inode->setattr(nullptr, m_dentry, &attr);
	return (err==0);
}

//bool CF2fsFile::SetEndOfFile(LONGLONG size)
//{
//	Cf2fsFileNode* file = dynamic_cast<Cf2fsFileNode*>(m_inode);
//	if (!file) THROW_ERROR(ERR_APP, L"the inode is not a file (%X) or is null", m_inode);
//	// 获取文件现在的长度
//	loff_t offset = file->GetFileSize();
//	if ((loff_t)size < offset)
//	{
//		LOG_ERROR(L"[err] not support truncate now, cur size=%lld, new size=%lld", offset, size);
//		return false;
//	}
//	loff_t new_size = size - offset;
//	//	int mode = FALLOC_FL_INSERT_RANGE;	// 增加文件长度，并且以洞填充，好像无法在文件末尾怎加长度
//	int mode = FALLOC_FL_ZERO_RANGE;	// 增加文件长度，并且以0填充
//	int err = file->fallocate(mode, offset, new_size);
//	return (err == 0);
//
//
//
//	m_inode->setattr(nullptr, m_dentry, )
//}


void CF2fsFile::DokanSetFileAttributes(DWORD attr)
{
	if (InterlockedAdd(&m_valid, 0) == 0) return;

	fmode_t mode_add=0, mode_sub=0;
	if (attr & FILE_ATTRIBUTE_READONLY) mode_add |= FMODE_READONLY;
	else								mode_sub |= FMODE_READONLY;
	if (attr & FILE_ATTRIBUTE_HIDDEN)	mode_add |= FMODE_HIDDEN;
	else								mode_sub |= FMODE_HIDDEN;
	if (attr & FILE_ATTRIBUTE_SYSTEM)	mode_add |= FMODE_SYSTEM;
	else								mode_sub |= FMODE_SYSTEM;
	if (attr & FILE_ATTRIBUTE_ARCHIVE)	mode_add |= FMODE_ARCHIVE;
	else								mode_sub |= FMODE_ARCHIVE;

	m_inode->SetFileAttribute(mode_add, mode_sub);
	LOG_DEBUG_(1,L"new mode=0x%X", m_inode->i_mode);
}

void CF2fsFile::SetFileTime(const FILETIME* ct, const FILETIME* at, const FILETIME* mt)
{
	if (InterlockedAdd(&m_valid, 0) == 0) return;

#if 0 //<DEBUG>
	SYSTEMTIME t;
	FILETIME ft;
	if (ct)
	{
		ft = *ct;
		FileTimeToSystemTime(&ft, &t);
		LOG_DEBUG_(1,L"create time, hi=%d, lo=%d, t=%d-%d-%d:%d:%d:%d", ft.dwLowDateTime, ft.dwHighDateTime, t.wYear, t.wMonth, t.wDay, t.wHour, t.wMinute, t.wSecond);
	}
	else { LOG_DEBUG_(1,L"create time is not set"); }

	if (at)
	{
		ft = *at;
		FileTimeToSystemTime(&ft, &t);
		LOG_DEBUG_(1,L"access time, hi=%d, lo=%d, t=%d-%d-%d:%d:%d:%d", ft.dwLowDateTime, ft.dwHighDateTime, t.wYear, t.wMonth, t.wDay, t.wHour, t.wMinute, t.wSecond);
	}
	else { LOG_DEBUG_(1,L"access time is not set"); }
	if (mt)
	{
		ft = *mt;
		FileTimeToSystemTime(&ft, &t);
		LOG_DEBUG_(1,L"modify time, hi=%d, lo=%d, t=%d-%d-%d:%d:%d:%d", ft.dwLowDateTime, ft.dwHighDateTime, t.wYear, t.wMonth, t.wDay, t.wHour, t.wMinute, t.wSecond);
	}
	else { LOG_DEBUG_(1,L"modify time is not set"); }


#endif

	if (ct) { m_inode->i_ctime = FromFileTime(*ct); }
	if (at) { m_inode->i_atime = FromFileTime(*at); }
	if (mt) { m_inode->i_mtime = FromFileTime(*mt); }
	else { m_inode->i_mtime =current_time(m_inode); }
	m_inode->SetFileAttribute(0, 0);		// force inode dirty
	// <TODO> make inode dirty
}

bool CF2fsFile::FlushFile(void)
{
	if (InterlockedAdd(&m_valid, 0) == 0) return true;
	m_inode->fsync(&m_file, 0, m_inode->i_size, true);
	return true;
}

void CF2fsFile::GetParent(IFileInfo*& parent)
{
	dentry* dparent = m_dentry->d_parent;

//	CF2fsFile* _file = jcvos::CDynamicInstance<CF2fsFile>::Create();
	CF2fsFile* _file = m_manager->file_get();
	_file->Init(dparent, NULL, m_fs, 0);
	parent = static_cast<IFileInfo*>(_file);
}

// 查找直接子文件夹，不做递归处理
bool CF2fsFile::OpenChild(IFileInfo*& file, const wchar_t* fn, UINT32 mode) const
{
	CF2fsFile* _file = nullptr;
	bool br = _OpenChild(_file, fn, mode);
	file = static_cast<IFileInfo*>(_file);
	return br;
}

bool CF2fsFile::_OpenChild(CF2fsFile*& file, const wchar_t* fn, UINT32 mode) const
{
	LOG_STACK_TRACE();
	JCASSERT(file == NULL);
	qstr name(fn);
	dentry* entry = d_alloc(m_dentry, name);
	if (!entry) THROW_ERROR(ERR_MEM, L"failed on creating dentry");
	// lock_kernel
	// lookup不影响总体的计数。如果成功，则计数转移给next_entry，src_entry无效。如果没有找到，保持src_entry不变，需要再dput()
	dentry * new_entry = m_inode->lookup(entry, 0);
//	dput(entry);
	if (IS_ERR(new_entry) )
	{
		LOG_ERROR(L"[err] cannot find item %s", fn);
		dput(entry);
		return false;
	}
	// unlock_kernel
	//file = jcvos::CDynamicInstance<CF2fsFile>::Create();
	LOG_TRACK(L"dentry", "entry=%p, inode=%p, ref=%d", entry, entry->d_inode, entry->d_lockref.count);
	if (entry->d_inode) { TRACK_INODE(entry->d_inode, L"dentry=%p", entry); }

	file = m_manager->file_get();
	file->Init(new_entry, NULL, m_fs, mode);
	dput(new_entry);
//	new_entry->dentry_trace(__FUNCTIONW__, __LINE__);
	return true;
}

void CF2fsFile::InodeToInfo(BY_HANDLE_FILE_INFORMATION& info, f2fs_inode_info* iinode)
{
	info.dwFileAttributes = iinode->GetFileAttribute();
	ToFileTime(info.ftCreationTime, iinode->i_ctime);
	ToFileTime(info.ftLastAccessTime, iinode->i_atime);
	ToFileTime(info.ftLastWriteTime, iinode->i_mtime);
	info.dwVolumeSerialNumber = iinode->m_sbi->raw_super->magic;
	info.nFileSizeHigh = iinode->GetFileSizeHi();
	info.nFileSizeLow = iinode->GetFileSizeLo();
	info.nNumberOfLinks = iinode->i_nlink;
	info.nFileIndexHigh = 0;
	info.nFileIndexLow = iinode->i_ino;
}

// 递归打开文件夹
bool CF2fsFile::OpenChildEx(IFileInfo*& file, const wchar_t* fn, size_t len)
{
	CF2fsFile* _file = nullptr;
	bool br = _OpenChildEx(_file, fn, len);
	file = static_cast<IFileInfo*>(_file);
	return br;
}

bool CF2fsFile::_OpenChildEx(CF2fsFile*& file, const wchar_t* fn, size_t len)
{
	JCASSERT(file == nullptr);
	// 查找第一个斜杠，分离最上层路径
	const wchar_t* end_path = fn + len;
	const wchar_t* parent_start = fn + 1;		//指向父目录的起始位置，去掉开头的斜杠
	dentry* cur_entry = dget(m_dentry);
	while (1)
	{
		const wchar_t * parent_end = parent_start;
		while (*parent_end != DIR_SEPARATOR && parent_end < end_path) parent_end++;
		size_t parent_len = parent_end - parent_start;

		qstr name(parent_start, parent_len);
		dentry* src_entry = d_alloc(cur_entry, name);

		f2fs_inode_info * cur_inode = F2FS_I(cur_entry->d_inode);
		// lookup不影响总体的计数。如果成功，则计数转移给next_entry，src_entry无效。如果没有找到，保持src_entry不变，需要再dput()
		dentry * next_entry = cur_inode->lookup(src_entry, 0);
//		dput(src_entry);
		dput(cur_entry);
		if (IS_ERR(next_entry))
		{
			LOG_ERROR(L"[err] cannot find %S in %S", name.name.c_str(), cur_entry->d_name.name.c_str());
			dput(src_entry);
			return false;
		}

		cur_entry = next_entry;
		// 分析后续名称
		parent_start = parent_end;
		if (parent_start >= end_path) break;

		parent_start++;
	}

	//file = jcvos::CDynamicInstance<CF2fsFile>::Create();
	file = m_manager->file_get();
	LOG_TRACK(L"dentry", "entry=%p, inode=%p, ref=%d", cur_entry, cur_entry->d_inode, cur_entry->d_lockref.count);
	if (cur_entry->d_inode)
	{
		LOG_TRACK(L"inode", "inode=%p, ino=%d, ref=%d, ", cur_entry->d_inode, cur_entry->d_inode->i_ino, cur_entry->d_inode->i_count);
	}
	file->Init(cur_entry, NULL, m_fs, (FMODE_READ | FMODE_WRITE | FMODE_EXEC));
	dput(cur_entry);
	return true;
}

//void CF2fsFile::_DeleteChild(CF2fsFile* child)
//{
//	dentry* child_entry = child->m_dentry;
//	int err = m_inode->unlink(child_entry);
//	if (err) THROW_ERROR(ERR_APP, L"failed on delete file %S, error=%d", m_dentry->d_name.name.c_str(), err);
//}

int CF2fsFile::_DeleteChild(const std::wstring & fn)
{
	qstr child_name(fn);
	dentry* child_entry = d_alloc(m_dentry, child_name);
	// lookup不影响总体的计数。如果成功，则计数转移给next_entry，src_entry无效。如果没有找到，保持src_entry不变，需要再dput()
	dentry* new_entry = m_inode->lookup(child_entry, 0);
//	dput(child_entry);
	if (IS_ERR(new_entry))
	{
		LOG_ERROR(L"[err] cannot find %s in parent", fn.c_str());
		dput(child_entry);
		return -ENOENT;
	}
	// 如果目标文件是目录，检查是否为空。
	if (S_ISDIR(new_entry->d_inode->i_mode))
	{
		Cf2fsDirInode* dir = dynamic_cast<Cf2fsDirInode*>(new_entry->d_inode);
		if (!dir->f2fs_empty_dir())
		{
			dput(new_entry);
			return -ENOTEMPTY;
		}
	}
	LOG_DEBUG_(1,L"fn=%s, dentry=%p, inode=%p, ino=%d", fn.c_str(), new_entry, new_entry->d_inode, new_entry->d_inode->i_ino);
	int err = m_inode->unlink(new_entry);
	if (err) THROW_ERROR(ERR_APP, L"failed on delete file %S, error=%d", new_entry->d_name.name.c_str(), err);
	d_delete(new_entry);
	m_inode->m_sbi->f2fs_balance_fs(true);
	dput(new_entry);
	return 0;
}

bool CF2fsFile::CreateChild(IFileInfo*& file, const wchar_t* fn, bool dir, UINT32 mode)
{
	LOG_STACK_TRACE();
	JCASSERT(file == NULL);
	qstr name(fn);
	dentry* entry = d_alloc(m_dentry, name);
	if (!entry) THROW_ERROR(ERR_MEM, L"failed on creating dentry");

	int err = 0;
	if (!dir)	
	{
		mode |= (S_IFREG | FMODE_ARCHIVE);	
		err = m_inode->create(NULL, entry, mode, false);
		if (err)
		{
			LOG_ERROR(L"[err] failed on creating new file, code=%d", err);
			return false;
		}
	}
	else	
	{	// for dir
		mode |= (S_IFDIR);	
		err = m_inode->mkdir(NULL, entry, mode);
		if (err)
		{
			LOG_ERROR(L"[err] failed on creating sub dir, code=%d", err);
			return false;
		}
	}		
	//CF2fsFile* _file = jcvos::CDynamicInstance<CF2fsFile>::Create();
	CF2fsFile* _file = m_manager->file_get();
	LOG_TRACK(L"dentry", "entry=%p, inode=%p, ref=%d", entry, entry->d_inode, entry->d_lockref.count);
	if (entry->d_inode)
	{
		LOG_TRACK(L"inode", "inode=%p, ino=%d, ref=%d, ", entry->d_inode, entry->d_inode->i_ino, entry->d_inode->i_count);
	}

	_file->Init(entry, NULL, m_fs, mode);
	file = static_cast<IFileInfo*>(_file);
	dput(entry);
	return true;
}


///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
#include "pch.h"
#include <linux-fs-wrapper.h>
#include "../include/f2fs-filesystem.h"
#include <boost/property_tree/json_parser.hpp>

LOCAL_LOGGER_ENABLE(L"f2fs.fileinfo", LOGGER_LEVEL_DEBUGINFO);


CF2fsFile::~CF2fsFile(void)
{
	dput(m_dentry);			// dput可以接受null参数，被忽略
	m_dentry = nullptr;
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
//	iget(m_inode);
}

void CF2fsFile::CloseFile(void)
{
	LOG_STACK_TRACE();
	JCASSERT(m_inode);

	m_inode->fsync(&m_file, 0, m_inode->i_size, true);
	m_inode->release_file(&m_file);
	dput(m_dentry);
	m_dentry = nullptr;
	m_inode = nullptr;
}

bool CF2fsFile::DokanReadFile(LPVOID buf, DWORD len, DWORD& read, LONGLONG offset)
{
	LOG_STACK_TRACE();
	Cf2fsFileNode* file_node = dynamic_cast<Cf2fsFileNode*>(m_inode);
	if (!file_node) THROW_ERROR(ERR_APP, L"the inode is not a file (%X) or is null", m_inode);

	iovec iov;
	iov.iov_base = buf;
	iov.iov_len = len;

	struct kiocb kiocb;
	iov_iter iter;	// 保存读取结果
	ssize_t ret;

//	file filp(file_node);
//	filp.f_inode = static_cast<inode*>(file_node);

	init_sync_kiocb(&kiocb, &m_file);
	kiocb.ki_pos = offset;
	iov_iter_init(&iter, READ, &iov, 1, len);

//	ret = call_read_iter(filp, &kiocb, &iter);
	ret = file_node->read_iter(&kiocb, &iter);
//	JCASSERT(ret != -EIOCBQUEUED);
//	if (ppos) *ppos = kiocb.ki_pos;
	read = (DWORD)ret;
	return (INT64)ret > 0;
}

bool CF2fsFile::DokanWriteFile(const void* buf, DWORD len, DWORD& written, LONGLONG offset)
{
	LOG_STACK_TRACE();
	Cf2fsFileNode* file_node = dynamic_cast<Cf2fsFileNode*>(m_inode);
	if (!file_node) THROW_ERROR(ERR_APP, L"the inode is not a file (%X) or is null", m_inode);
	iovec iov;
	iov.iov_base = const_cast<void*>(buf);
	iov.iov_len = len;

	kiocb iob;
	iov_iter iter;
	ssize_t ret;

//	file filp(file_node);
	//filp.f_inode = static_cast<inode*>(file_node);

	init_sync_kiocb(&iob, &m_file);
	iob.ki_pos = offset;
	iov_iter_init(&iter, WRITE, &iov, 1, len);

	//	ret = call_read_iter(filp, &iob, &iter);
	ret = file_node->write_iter(&iob, &iter);
	//	JCASSERT(ret != -EIOCBQUEUED);
	//	if (ppos) *ppos = iob.ki_pos;
	written = (DWORD)ret;
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

bool CF2fsFile::GetFileInformation(LPBY_HANDLE_FILE_INFORMATION fileinfo) const
{
	if (fileinfo)
	{
//		m_inode->getattr();
		fileinfo->dwFileAttributes = m_inode->GetFileAttribute();
		ToFileTime(fileinfo->ftCreationTime, m_inode->i_ctime);
		ToFileTime(fileinfo->ftLastAccessTime, m_inode->i_atime);
		ToFileTime(fileinfo->ftLastWriteTime, m_inode->i_mtime);
		fileinfo->dwVolumeSerialNumber = m_inode->m_sbi->raw_super->magic;
		fileinfo->nFileSizeHigh = m_inode->GetFileSizeHi();
		fileinfo->nFileSizeLow = m_inode->GetFileSizeLo();
		fileinfo->nNumberOfLinks = m_inode->i_nlink;
		fileinfo->nFileIndexHigh = 0;
		fileinfo->nFileIndexLow = m_inode->i_ino;
	}
	return true;
}

std::wstring CF2fsFile::GetFileName(void) const
{
	std::wstring str_fn;
	jcvos::Utf8ToUnicode(str_fn, m_dentry->d_name.name);
	return str_fn;
}

bool CF2fsFile::SetAllocationSize(LONGLONG size)
{
	Cf2fsFileNode* file = dynamic_cast<Cf2fsFileNode*>(m_inode);
	if (!file) THROW_ERROR(ERR_APP, L"the inode is not a file (%X) or is null", m_inode);
	// 获取文件现在的长度
	loff_t offset = file->GetFileSize();
	if ((loff_t)size < offset)
	{
		LOG_ERROR(L"[err] not support truncate now, cur size=%lld, new size=%lld", offset, size);
		return false;
	}
	loff_t new_size = size - offset;
	//	int mode = FALLOC_FL_INSERT_RANGE;	// 增加文件长度，并且以洞填充，好像无法在文件末尾怎加长度
	int mode = FALLOC_FL_ZERO_RANGE;	// 增加文件长度，并且以0填充
	int err = file->fallocate(mode, offset, new_size);
	return (err == 0);
}

bool CF2fsFile::SetEndOfFile(LONGLONG size)
{
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
	m_inode->SetFileAttribute(attr);
}

void CF2fsFile::SetFileTime(const FILETIME* ct, const FILETIME* at, const FILETIME* mt)
{
	if (ct) { m_inode->i_ctime = FromFileTime(*ct); }
	if (at) { m_inode->i_atime = FromFileTime(*at); }
	if (mt) { m_inode->i_mtime = FromFileTime(*mt); }
	// <TODO> make inode dirty
}

bool CF2fsFile::FlushFile(void)
{
	JCASSERT(0);
	return false;
}

void CF2fsFile::GetParent(IFileInfo*& parent)
{
	dentry* dparent = m_dentry->d_parent;

	CF2fsFile* _file = jcvos::CDynamicInstance<CF2fsFile>::Create();
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
	dentry * new_entry = m_inode->lookup(entry, 0);
//	dput(entry);

	if ((INT64)new_entry < 0 )
	{
		LOG_ERROR(L"[err] cannot find item %s", fn);
		dput(entry);
		return false;
	}
	// unlock_kernel
	file = jcvos::CDynamicInstance<CF2fsFile>::Create();
	file->Init(entry, NULL, m_fs, mode);
	dput(entry);
	return true;
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
		dentry * next_entry = cur_inode->lookup(src_entry, 0);

		dput(cur_entry);
		if ((INT64)next_entry < 0)
		{
			LOG_ERROR(L"[err] cannot find %S in %S", name.name.c_str(), cur_entry->d_name.name.c_str());
			return false;
		}

		cur_entry = src_entry;
		// 分析后续名称
		parent_start = parent_end;
		if (parent_start >= end_path) break;

		parent_start++;
	}

	file = jcvos::CDynamicInstance<CF2fsFile>::Create();
	file->Init(cur_entry, NULL, m_fs, (FMODE_READ | FMODE_WRITE | FMODE_EXEC));
	dput(cur_entry);
	return true;
}

void CF2fsFile::_DeleteChild(CF2fsFile* child)
{
	dentry* child_entry = child->m_dentry;
	int err = m_inode->unlink(child_entry);
	if (err) THROW_ERROR(ERR_APP, L"failed on delete file %S, error=%d", m_dentry->d_name.name.c_str(), err);
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
		mode |= S_IFREG;	
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
	CF2fsFile* _file = jcvos::CDynamicInstance<CF2fsFile>::Create();
	_file->Init(entry, NULL, m_fs, mode);
	file = static_cast<IFileInfo*>(_file);
	dput(entry);
	return true;
}


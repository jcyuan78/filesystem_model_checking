///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
#include "pch.h"
#include <linux-fs-wrapper.h>
#include "../include/f2fs-filesystem.h"
#include <boost/property_tree/json_parser.hpp>

LOCAL_LOGGER_ENABLE(L"f2fs.fileinfo", LOGGER_LEVEL_DEBUGINFO);


void CF2fsFile::Init(dentry* de, inode* node, CF2fsFileSystem* fs, UINT32 mode)
{
	JCASSERT(de);
	m_dentry = de;
	if (node == NULL)	{	node = de->d_inode;	}
	m_inode = dynamic_cast<f2fs_inode_info*>(node);
	if (m_inode == NULL) THROW_ERROR(ERR_APP, L"[err] inode is null or wrong type");
	m_file.init(m_inode);
	m_file.f_mode |= mode;
}

void CF2fsFile::CloseFile(void)
{
	LOG_STACK_TRACE();
	Cf2fsFileNode* file_node = dynamic_cast<Cf2fsFileNode*>(m_inode);
	if (!file_node) THROW_ERROR(ERR_APP, L"the inode is not a file (%X) or is null", m_inode);
	file_node->fsync(&m_file, 0, file_node->i_size, true);
	//file filp(file_node);
	file_node->release_file(&m_file);
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

bool CF2fsFile::SetAllocationSize(LONGLONG size)
{
	return false;
}

bool CF2fsFile::SetEndOfFile(LONGLONG size)
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
	return (err==0);
}

bool CF2fsFile::FlushFile(void)
{
	return false;
}

bool CF2fsFile::OpenChild(IFileInfo*& file, const wchar_t* fn, UINT32 mode) const
{
	LOG_STACK_TRACE();
	JCASSERT(file == NULL);
	qstr name(fn);
	dentry* entry = d_alloc(m_dentry, name);
	if (!entry) THROW_ERROR(ERR_MEM, L"failed on creating dentry");
	// lock_kernel
	dentry * err = m_inode->lookup(entry, 0);
	if ((INT64)err < 0 )
	{
		LOG_ERROR(L"[err] cannot find item %s", fn);
		return false;
	}
	// unlock_kernel
	CF2fsFile * _file = jcvos::CDynamicInstance<CF2fsFile>::Create();
	_file->Init(entry, NULL, m_fs, mode);
	file = static_cast<IFileInfo*>(_file);
	return true;
}

bool CF2fsFile::OpenChildEx(IFileInfo*& file, const wchar_t* fn, size_t len)
{
	return false;
}

bool CF2fsFile::CreateChild(IFileInfo*& file, const wchar_t* fn, bool dir, UINT32 mode)
{
	LOG_STACK_TRACE();
	JCASSERT(file == NULL);
//	umode_t mode = 0;
	if (!dir)
	{
		mode |= S_IFREG;
		qstr name(fn);
		dentry* entry = d_alloc(m_dentry, name);
		if (!entry) THROW_ERROR(ERR_MEM, L"failed on creating dentry");
		int err = m_inode->create(NULL, entry, mode, false);
		if (err)
		{
			LOG_ERROR(L"[err] failed on creating new file, code=%d", err);
			return false;
		}
		CF2fsFile* _file = jcvos::CDynamicInstance<CF2fsFile>::Create();
		_file->Init(entry, NULL, m_fs, mode);
		file = static_cast<IFileInfo*>(_file);
		return true;
	}
	return false;
}

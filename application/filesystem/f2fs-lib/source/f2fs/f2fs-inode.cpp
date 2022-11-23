#include "pch.h"
#include "f2fs-inode.h"
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
#include "../pch.h"
// SPDX-License-Identifier: GPL-2.0
/*
 * fs/f2fs/inode.c
 *
 * Copyright (c) 2012 Samsung Electronics Co., Ltd.
 *             http://www.samsung.com/
 */

 //#include <linux/fs.h>
 //#include <linux/f2fs_fs.h>
 //#include <linux/buffer_head.h>
 //#include <linux/backing-dev.h>
 //#include <linux/writeback.h>
 //
#include "../../include/f2fs.h"
#include "node.h"
#include "segment.h"
#include "xattr.h"

#include "../../include/f2fs-filesystem.h"

#include "../mapping.h"
//
//#include <trace/events/f2fs.h>

LOCAL_LOGGER_ENABLE(L"f2fs.inode", LOGGER_LEVEL_DEBUGINFO);

f2fs_inode_info::f2fs_inode_info(f2fs_sb_info* sbi, UINT32 ino, address_space * _mapping)
{
	JCASSERT(sbi);
	address_space* mapping = nullptr;
	if (ino == sbi->F2FS_NODE_INO())
	{
		JCASSERT(_mapping == nullptr);
		mapping = new Cf2fsNodeMapping(this, sbi->GetPageManager());
	}
	else if (ino == sbi->F2FS_META_INO())
	{
		JCASSERT(_mapping == nullptr);
		mapping = new Cf2fsMetaMapping(this, sbi->GetPageManager());
	}
	else 
	{
		JCASSERT(ino ==0 || _mapping);
		mapping = _mapping;  
	}

	// 一次性初始化内容：在fs/inode.c : inode_init_once()
	// (1)清零
	// (2)配置 lists: i_hash, i_device, i_io_list, i_wb_list, i_lru;
	// (3) 初始化address_space : __address_space_init_once(&inode->i_data)
	i_mapping = NULL;
	// (4) 初始化 i_size_seqcount (可选）
	m_sbi = sbi;
	i_sb = m_sbi;

	InitializeCriticalSection(&i_lock);

	//	fi = kmem_cache_alloc(f2fs_inode_cachep, GFP_F2FS_ZERO);
	inode_init_once(static_cast<inode*>(this));

	/* Initialize f2fs-specific inode info */
	atomic_set(&dirty_pages, 0);
	atomic_set(&i_compr_blocks, 0);
	init_rwsem(&i_sem);
	spin_lock_init(&i_size_lock);
	//	INIT_LIST_HEAD(&dirty_list);
	//	INIT_LIST_HEAD(&gdirty_list);
	memset(m_in_list, 0, sizeof(m_in_list));
	INIT_LIST_HEAD(&inmem_ilist);
	INIT_LIST_HEAD(&inmem_pages);
	mutex_init(&inmem_lock);
	init_rwsem(&i_gc_rwsem[READ]);
	init_rwsem(&i_gc_rwsem[WRITE]);
	init_rwsem(&i_mmap_sem);
	init_rwsem(&i_xattr_sem);

	if (mapping) i_mapping = mapping;
	InitializeCriticalSection(&m_alias_lock);

	/* Will be used by directory only */
	// i_dir_level = m_sbi->dir_level;
}


f2fs_inode_info::~f2fs_inode_info(void)
{
	delete i_mapping;
	i_mapping = NULL;
	DeleteCriticalSection(&i_lock);
	DeleteCriticalSection(&m_alias_lock);
}

DWORD f2fs_inode_info::GetFileAttribute(void) const
{
	DWORD attr = 0;
	//FILE_ATTRIBUTE_ARCHIVE				//	32 (0x20)	//	A file or directory that is an archive file or directory.Applications typically use this attribute to mark files for backup or removal .
	if (i_mode & FMODE_ARCHIVE) attr |= FILE_ATTRIBUTE_ARCHIVE;
	//	FILE_ATTRIBUTE_DIRECTORY			//	16 (0x10)	//	The handle that identifies a directory.
	if (i_mode & S_IFDIR)	attr |= FILE_ATTRIBUTE_DIRECTORY;
	//	FILE_ATTRIBUTE_HIDDEN				//	2 (0x2)		// The file or directory is hidden.It is not included in an ordinary directory listing.
	if (i_mode & FMODE_HIDDEN)	attr |= FILE_ATTRIBUTE_HIDDEN;

	//	FILE_ATTRIBUTE_NORMAL				//	128 (0x80)	//	A file that does not have other attributes set.This attribute is valid only when used alone.
//	if (i_mode & S_IFREG)	attr |= FILE_ATTRIBUTE_NORMAL;
//	if (i_mode & S_IFREG)	attr |= 0x20;

	//	FILE_ATTRIBUTE_NOT_CONTENT_INDEXED	//	8192 (0x2000)	//	The file or directory is not to be indexed by the content indexing service.
	//	FILE_ATTRIBUTE_NO_SCRUB_DATA		//	131072 (0x20000)
	//	The user data stream not to be read by the background data integrity scanner(AKA scrubber).When set on a directory it only provides inheritance.This flag is only supported on Storage Spaces and ReFS volumes.It is not included in an ordinary directory listing.
	//	Windows Server 2008 R2, Windows 7, Windows Server 2008, Windows Vista, Windows Server 2003 and Windows XP : This flag is not supported until Windows 8 and Windows Server 2012.
	//	FILE_ATTRIBUTE_OFFLINE
	//	4096 (0x1000)
	//	The data of a file is not available immediately.This attribute indicates that the file data is physically moved to offline storage.This attribute is used by Remote Storage, which is the hierarchical storage management software.Applications should not arbitrarily change this attribute.

	//	FILE_ATTRIBUTE_READONLY				//	1 (0x1)		//	A file that is read - only.Applications can read the file, but cannot write to it or delete it.This attribute is not honored on directories.For more information, see You cannot view or change the Read - only or the System attributes of folders in Windows Server 2003, in Windows XP, in Windows Vista or in Windows 7.
	if (i_mode & FMODE_READONLY)	attr |= FILE_ATTRIBUTE_READONLY;
	//	FILE_ATTRIBUTE_RECALL_ON_DATA_ACCESS	//	4194304 (0x400000)
	//	When this attribute is set, it means that the file or directory is not fully present locally.For a file that means that not all of its data is on local storage(e.g.it may be sparse with some data still in remote storage).For a directory it means that some of the directory contents are being virtualized from another location.Reading the file / enumerating the directory will be more expensive than normal, e.g.it will cause at least some of the file / directory content to be fetched from a remote store.Only kernel - mode callers can set this bit.
	//	FILE_ATTRIBUTE_RECALL_ON_OPEN	//	262144 (0x40000)
	//	This attribute only appears in directory enumeration classes(FILE_DIRECTORY_INFORMATION, FILE_BOTH_DIR_INFORMATION, etc.).When this attribute is set, it means that the file or directory has no physical representation on the local system; the item is virtual.Opening the item will be more expensive than normal, e.g.it will cause at least some of it to be fetched from a remote store.
	//	FILE_ATTRIBUTE_REPARSE_POINT	//	1024 (0x400)
	//	A file or directory that has an associated reparse point, or a file that is a symbolic link.
	//	FILE_ATTRIBUTE_SPARSE_FILE		//	512 (0x200)		//	A file that is a sparse file.
	//	FILE_ATTRIBUTE_SYSTEM			//	4 (0x4)			//	A file or directory that the operating system uses a part of, or uses exclusively.
	if (i_mode & FMODE_SYSTEM)	attr |= FILE_ATTRIBUTE_SYSTEM;

	//	FILE_ATTRIBUTE_TEMPORARY		//	256 (0x100)		//	A file that is being used for temporary storage.File systems avoid writing data back to mass storage if sufficient cache memory is available, because typically, an application deletes a temporary file after the handle is closed.In that scenario, the system can entirely avoid writing the data.Otherwise, the data is written after the handle is closed.
	//	FILE_ATTRIBUTE_PINNED
	//	524288 (0x80000)
	//	This attribute indicates user intent that the file or directory should be kept fully present locally even when not being actively accessed.This attribute is for use with hierarchical storage management software.
	//	FILE_ATTRIBUTE_UNPINNED
	//	1048576 (0x100000)

	if (attr == 0) attr = FILE_ATTRIBUTE_NORMAL;

	return attr;
}

void f2fs_inode_info::SetFileAttribute(fmode_t mode_add, fmode_t mode_sub)
{
//	if (attr & FILE_ATTRIBUTE_READONLY) i_mode |= S_IREAD;
	i_mode &= ~(mode_sub);
	i_mode |= mode_add;
	/* file size may changed here */
	f2fs_mark_inode_dirty_sync(true);
	/* inode change will produce dirty node pages flushed by checkpoint */
	m_sbi->f2fs_balance_fs(true);
}


///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

Cf2fsDirInode::Cf2fsDirInode(f2fs_sb_info* sbi, UINT ino) 
	: f2fs_inode_info(sbi, ino, new Cf2fsDataMapping(this, sbi->GetPageManager()))
{
	F_LOG_DEBUG(L"inode", L" addr=%p, - construct dir inode", this);
	JCASSERT(i_mapping);
//	i_mapping = static_cast<address_space*>(new Cf2fsDataMapping(this));
	address_space_init_once(i_mapping);
	/* Will be used by directory only */
	i_dir_level = m_sbi->dir_level;
}

Cf2fsFileNode::Cf2fsFileNode(f2fs_sb_info* sbi, UINT ino) 
	: f2fs_inode_info(sbi, ino, new Cf2fsDataMapping(this, sbi->GetPageManager()))
{
//	i_mapping = static_cast<address_space*>(new Cf2fsDataMapping(this));
	F_LOG_DEBUG(L"inode", L" addr=%p, - construct file inode", this);
	JCASSERT(i_mapping);
	address_space_init_once(i_mapping);
}

Cf2fsSymbLink::Cf2fsSymbLink(f2fs_sb_info* sbi, UINT ino) 
	: f2fs_inode_info(sbi, ino, new Cf2fsDataMapping(this, sbi->GetPageManager()))
{
	F_LOG_DEBUG(L"inode", L" addr=%p, - construct symblink inode", this);
	JCASSERT(i_mapping);
	//i_mapping = static_cast<address_space*>(new Cf2fsDataMapping(this));
	address_space_init_once(i_mapping);
}

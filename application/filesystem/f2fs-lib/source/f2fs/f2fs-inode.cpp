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

f2fs_inode_info::f2fs_inode_info(f2fs_sb_info * sbi, address_space * mapping)
{
	JCASSERT(mapping == nullptr);
	JCASSERT(sbi);
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

	/* Will be used by directory only */
	// i_dir_level = m_sbi->dir_level;

//	return &fi->vfs_inode;

}

f2fs_inode_info::f2fs_inode_info(f2fs_sb_info* sbi, UINT32 ino)
{
	JCASSERT(sbi);
	address_space* mapping = nullptr;
	if (ino == F2FS_NODE_INO(sbi)) 				mapping = new Cf2fsNodeMapping(this);
	else if (ino == F2FS_META_INO(sbi)) 		mapping = new Cf2fsMetaMapping(this);
	else { JCASSERT(0); }

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

	/* Will be used by directory only */
	// i_dir_level = m_sbi->dir_level;

//	return &fi->vfs_inode;

}

f2fs_inode_info::f2fs_inode_info(const f2fs_inode_info & src)
{
	i_mode = src.i_mode;			//
	i_opflags = src.i_opflags;		
	i_flags = src.i_flags;
	i_sb = src.i_sb;				//
	i_mapping = src.i_mapping;
	i_ino = src.i_ino;				//
	__i_nlink = src.__i_nlink;		//
	i_rdev = src.i_rdev;
	i_size = src.i_size;			//
	i_atime = src.i_atime;			//
	i_mtime = src.i_mtime;			//
	i_ctime = src.i_ctime;			//

	InitializeCriticalSection(&i_lock);
	i_bytes = src.i_bytes;
	i_blkbits = src.i_blkbits;
	i_write_hint = src.i_write_hint;
	i_blocks = src.i_blocks;
	i_state = src.i_state;
	//i_rwsem;
	dirtied_when = src.dirtied_when;
	dirtied_time_when = src.dirtied_time_when;

	i_hash = src.i_hash;
	i_io_list = src.i_io_list;
	i_lru = src.i_lru;
	i_sb_list = src.i_sb_list;
	i_wb_list = src.i_wb_list;
	i_dentry = src.i_dentry;
	i_version = src.i_version;
	i_sequence = src.i_sequence;
	i_count = src.i_count;
	i_dio_count = src.i_dio_count;
	i_writecount = src.i_writecount;
	i_flctx = src.i_flctx;
	i_devices = src.i_devices;
	i_pipe = src.i_pipe;
	i_cdev = src.i_cdev;
	i_link = src.i_link;
	i_dir_seq = src.i_dir_seq;
	i_generation = src.i_generation;	//
	i_private = src.i_private;

	// for f2fs_inode_info
	i_current_depth = src.i_current_depth;
	i_gc_failures[0] = src.i_gc_failures[0];
	i_gc_failures[1] = src.i_gc_failures[1];
	i_xattr_nid = src.i_xattr_nid;
	i_flags = src.i_flags;
	i_advise = src.i_advise;
	i_pino = src.i_pino;
//	i_dir_level = src.i_dir_level;
	i_extra_isize = src.i_extra_isize;
	i_inline_xattr_size = src.i_inline_xattr_size;
	last_disk_size = src.last_disk_size;
}

f2fs_inode_info::~f2fs_inode_info(void)
{
	delete i_mapping;
	i_mapping = NULL;
	DeleteCriticalSection(&i_lock);
}



///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

Cf2fsDirInode::Cf2fsDirInode(f2fs_sb_info* sbi) : f2fs_inode_info(sbi)
{
	i_mapping = static_cast<address_space*>(new Cf2fsDataMapping(this));
	address_space_init_once(i_mapping);
	/* Will be used by directory only */
	i_dir_level = m_sbi->dir_level;
}

Cf2fsFileNode::Cf2fsFileNode(f2fs_sb_info* sbi) : f2fs_inode_info(sbi)
{
	i_mapping = static_cast<address_space*>(new Cf2fsDataMapping(this));
	address_space_init_once(i_mapping);
}

Cf2fsSymbLink::Cf2fsSymbLink(f2fs_sb_info* sbi) : f2fs_inode_info(sbi)
{
	i_mapping = static_cast<address_space*>(new Cf2fsDataMapping(this));
	address_space_init_once(i_mapping);
}

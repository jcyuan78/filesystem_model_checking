///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
#include "../pch.h"


// SPDX-License-Identifier: GPL-2.0
/*
 * fs/f2fs/namei.c
 *
 * Copyright (c) 2012 Samsung Electronics Co., Ltd.
 *             http://www.samsung.com/
 */
//#include <linux/fs.h>
//#include <linux/f2fs_fs.h>
//#include <linux/pagemap.h>
//#include <linux/sched.h>
//#include <linux/ctype.h>
//#include <linux/random.h>
//#include <linux/dcache.h>
//#include <linux/namei.h>
//#include <linux/quotaops.h>

#include "../../include/f2fs.h"
#include "node.h"
#include "segment.h"
#include "xattr.h"
#include "acl.h"
//#include <trace/events/f2fs.h>
#include "../../include/f2fs-filesystem.h"
#include "../../include/f2fs-inode.h"

LOCAL_LOGGER_ENABLE(L"f2fs.namei", LOGGER_LEVEL_DEBUGINFO);

//static struct inode* f2fs_new_inode(struct inode* dir, umode_t mode)	
f2fs_inode_info* f2fs_sb_info::f2fs_new_inode(Cf2fsDirInode* dir, umode_t mode, inode_type type)
{
//	struct f2fs_sb_info* sbi = F2FS_I_SB(dir);
	nid_t ino;
	f2fs_inode_info* iinode;
	bool nid_free = false;
	bool encrypt = false;
	int xattr_size = 0;
	int err;

//	iinode = new_inode(dir->i_sb);
	switch (type)
	{
	case DIR_INODE:		iinode = NewInode<Cf2fsDirInode>();		break;
	case FILE_INODE:	iinode = NewInode<Cf2fsFileNode>();		break;
//		case : iinode = NewInode<>();
//		case : iinode = NewInode<>();
		default: THROW_ERROR(ERR_APP, L"unknown inode type = %d", type);
	}
	if (!iinode) return ERR_PTR<f2fs_inode_info>(-ENOMEM);

	f2fs_lock_op();
	if (!nm_info->f2fs_alloc_nid(&ino))
	{
		f2fs_unlock_op();
		err = -ENOSPC;
		goto fail;
	}
	f2fs_unlock_op();

	nid_free = true;

#if 0 //<NOT SUPPORT> 权限管理，暂不支持
	inode_init_owner(&init_user_ns, iinode, dir, mode);
#endif
	iinode->i_mode = mode;

	iinode->i_ino = ino;
	iinode->i_blocks = 0;
	iinode->i_mtime = iinode->i_atime = iinode->i_ctime = current_time(iinode);
	iinode->i_crtime = iinode->i_mtime;
	iinode->i_generation = prandom_u32();

	LOG_DEBUG(L"[inode_track] add=%p, ino=%d, mode=%X, - new inode ", iinode, iinode->i_ino, iinode->i_mode);
	if (S_ISDIR(iinode->i_mode)) 	iinode->i_current_depth = 1;

	err = m_inodes.insert_inode_locked(iinode);
	if (err)
	{
		err = -EINVAL;
		goto fail;
	}

#if 0	// 磁盘配额，不支持
	if (f2fs_sb_has_project_quota(this) && (dir->i_flags & F2FS_PROJINHERIT_FL))	iinode->i_projid = dir->i_projid;
	else	iinode->i_projid = make_kprojid(&init_user_ns,	F2FS_DEF_PROJID);
#endif
	err = fscrypt_prepare_new_inode(dir, iinode, &encrypt);
	if (err)	goto fail_drop;

	err = dquot_initialize(iinode);
	if (err)	goto fail_drop;

	set_inode_flag(iinode, FI_NEW_INODE);

	if (encrypt)	f2fs_set_encrypted_inode(iinode);

	if (f2fs_sb_has_extra_attr(this))
	{
		set_inode_flag(iinode, FI_EXTRA_ATTR);
		iinode->i_extra_isize = F2FS_TOTAL_EXTRA_ATTR_SIZE;
	}

	if (test_opt(this, INLINE_XATTR))
		set_inode_flag(iinode, FI_INLINE_XATTR);

	if (test_opt(this, INLINE_DATA) && f2fs_may_inline_data(iinode))
		set_inode_flag(iinode, FI_INLINE_DATA);
	if (f2fs_may_inline_dentry(iinode))
		set_inode_flag(iinode, FI_INLINE_DENTRY);

	if (f2fs_sb_has_flexible_inline_xattr(this)) {
		f2fs_bug_on(this, !f2fs_has_extra_attr(iinode));
		if (f2fs_has_inline_xattr(iinode))
			xattr_size = F2FS_OPTION(this).inline_xattr_size;
		/* Otherwise, will be 0 */
	}
	else if (f2fs_has_inline_xattr(iinode) ||	iinode->f2fs_has_inline_dentry()) 
	{
		xattr_size = DEFAULT_INLINE_XATTR_ADDRS;
	}
	iinode->i_inline_xattr_size = xattr_size;

	f2fs_init_extent_tree(iinode, NULL);

	stat_inc_inline_xattr(iinode);
	stat_inc_inline_inode(iinode);
	stat_inc_inline_dir(iinode);

	iinode->i_flags = f2fs_mask_flags(mode, dir->i_flags & F2FS_FL_INHERITED);

	if (S_ISDIR(iinode->i_mode))
		iinode->i_flags |= F2FS_INDEX_FL;

	if (iinode->i_flags & F2FS_PROJINHERIT_FL)
		set_inode_flag(iinode, FI_PROJ_INHERIT);

	if (f2fs_sb_has_compression(this))
	{
		/* Inherit the compression flag in directory */
		if ((dir->i_flags & F2FS_COMPR_FL) && f2fs_may_compress(iinode))
			set_compress_context(iinode);
	}

	f2fs_set_inode_flags(iinode);

//	trace_f2fs_new_inode(iinode, 0);
	return iinode;

fail:
//	trace_f2fs_new_inode(iinode, err);
	iinode->make_bad_inode();
	if (nid_free)	set_inode_flag(iinode, FI_FREE_NID);
	iput(iinode);
	return ERR_PTR<f2fs_inode_info>(err);
fail_drop:
//	trace_f2fs_new_inode(iinode, err);
#if 0 //<NOT SUPPORT>
	dquot_drop(iinode);
#endif
	iinode->i_flags |= S_NOQUOTA;
	if (nid_free)	set_inode_flag(iinode, FI_FREE_NID);
	iinode->clear_nlink();
	unlock_new_inode(iinode);
	iput(iinode);
	return ERR_PTR<f2fs_inode_info>(err);
}
#if 0
//static f2fs_inode_info * f2fs_new_inode(inode * dir, umode_t mode)
f2fs_inode_info* CF2fsFileSystem::_internal_new_inode(f2fs_inode_info * new_node, f2fs_inode_info * dir, umode_t mode)
{
//	f2fs_sb_info *sbi = F2FS_I_SB(dir);
	f2fs_sb_info* sbi = m_sb_info;
	
	nid_t ino;
//	struct inode *new_node;
	bool nid_free = false;
	bool encrypt = false;
	int xattr_size = 0;
	int err;

//	new_inode(dir->i_sb);
//	f2fs_inode_info* new_node = sbi->m_fs->NewInode<Cf2fsFileNode>();
	if (!new_node)	return ERR_PTR<f2fs_inode_info>(-ENOMEM);

	{	auto_lock<semaphore_read_lock> lock_op(sbi->cp_rwsem);	//		sbi->f2fs_lock_op();
		//申请新的nid，返回到ino中；
		if (!NM_I(sbi)->f2fs_alloc_nid(&ino))
		{
			err = -ENOSPC;
			goto fail;
		}
	}

	nid_free = true;
	
	// not support uid/guid, 但是在inode_init_owner中设置了i_mode
	//inode_init_owner(&init_user_ns, new_node, dir, mode);
//	inode_fsuid_set(inode, mnt_userns);
//	if (dir && dir->i_mode & S_ISGID) {
//		inode->i_gid = dir->i_gid;
		/* Directories are special, and always inherit S_ISGID */
		if (S_ISDIR(mode))	mode |= S_ISGID;
		//else if ((mode & (S_ISGID | S_IXGRP)) == (S_ISGID | S_IXGRP) &&
		//	!in_group_p(i_gid_into_mnt(mnt_userns, dir)) &&
		//	!capable_wrt_inode_uidgid(mnt_userns, dir, CAP_FSETID))
		//	mode &= ~S_ISGID;
//	}
//	else
//		inode_fsgid_set(inode, mnt_userns);
	new_node->i_mode = mode;

	new_node->i_ino = ino;
	new_node->i_blocks = 0;
	new_node->i_mtime = new_node->i_atime = new_node->i_ctime = current_time(new_node);
	new_node->i_crtime = new_node->i_mtime;
	new_node->i_generation = prandom_u32();

	if (S_ISDIR(new_node->i_mode)) new_node->i_current_depth = 1;

//	err = insert_inode_locked(new_node);
	err = m_inodes.insert_inode_locked(new_node);

	if (err) 
	{
		err = -EINVAL;
		goto fail;
	}

#if 0 //磁盘配额设置，不支持
	if (f2fs_sb_has_project_quota(sbi) && (F2FS_I(dir)->i_flags & F2FS_PROJINHERIT_FL))
		new_node->i_projid = F2FS_I(dir)->i_projid;
	else
		new_node->i_projid = make_kprojid(&init_user_ns, F2FS_DEF_PROJID);
#endif

	err = fscrypt_prepare_new_inode(dir, new_node, &encrypt);
	if (err) goto fail_drop;

	err = dquot_initialize(new_node);
	if (err) goto fail_drop;

	set_inode_flag(new_node, FI_NEW_INODE);

	if (encrypt) f2fs_set_encrypted_inode(new_node);

	if (f2fs_sb_has_extra_attr(sbi)) 
	{
		set_inode_flag(new_node, FI_EXTRA_ATTR);
		new_node->i_extra_isize = F2FS_TOTAL_EXTRA_ATTR_SIZE;
	}

	if (test_opt(sbi, INLINE_XATTR)) set_inode_flag(new_node, FI_INLINE_XATTR);

	if (test_opt(sbi, INLINE_DATA) && f2fs_may_inline_data(new_node))
		set_inode_flag(new_node, FI_INLINE_DATA);
	if (f2fs_may_inline_dentry(new_node))
		set_inode_flag(new_node, FI_INLINE_DENTRY);

	if (f2fs_sb_has_flexible_inline_xattr(sbi)) 
	{
		f2fs_bug_on(sbi, !f2fs_has_extra_attr(new_node));
		if (f2fs_has_inline_xattr(new_node)) xattr_size = F2FS_OPTION(sbi).inline_xattr_size;
		/* Otherwise, will be 0 */
	} else if (f2fs_has_inline_xattr(new_node) || new_node->f2fs_has_inline_dentry()) 
	{
		xattr_size = DEFAULT_INLINE_XATTR_ADDRS;
	}
	new_node->i_inline_xattr_size = xattr_size;

	f2fs_init_extent_tree(new_node, NULL);

	stat_inc_inline_xattr(new_node);
	stat_inc_inline_inode(new_node);
	stat_inc_inline_dir(new_node);

	new_node->i_flags = f2fs_mask_flags(mode, dir->i_flags & F2FS_FL_INHERITED);

	if (S_ISDIR(new_node->i_mode)) 	new_node->i_flags |= F2FS_INDEX_FL;

	if (new_node->i_flags & F2FS_PROJINHERIT_FL) set_inode_flag(new_node, FI_PROJ_INHERIT);

	if (f2fs_sb_has_compression(sbi)) 
	{	/* Inherit the compression flag in directory */
		if ((dir->i_flags & F2FS_COMPR_FL) && f2fs_may_compress(new_node))
			set_compress_context(new_node);
	}

	f2fs_set_inode_flags(new_node);

//	trace_f2fs_new_inode(new_node, 0);
	return new_node;

fail:
//	trace_f2fs_new_inode(new_node, err);
	new_node->make_bad_inode();
	if (nid_free) set_inode_flag(new_node, FI_FREE_NID);
	iput(new_node);
	return ERR_PTR<f2fs_inode_info>(err);
fail_drop:
//	trace_f2fs_new_inode(new_node, err);
#if 0 //磁盘配额相关，不支持
	dquot_drop(new_node);
#endif
	new_node->i_flags |= S_NOQUOTA;
	if (nid_free) set_inode_flag(new_node, FI_FREE_NID);
	new_node->clear_nlink();
	unlock_new_inode(new_node);
	iput(new_node);
	return ERR_PTR<f2fs_inode_info>(err);
}
#else
//static f2fs_inode_info * f2fs_new_inode(inode * dir, umode_t mode)
int f2fs_inode_info::_internal_new_inode(f2fs_inode_info* dir, umode_t mode)
{
	//	f2fs_sb_info *sbi = F2FS_I_SB(dir);
	f2fs_sb_info* sbi = m_sbi;

	nid_t ino;
	//	struct inode *new_node;
	bool nid_free = false;
	bool encrypt = false;
	int xattr_size = 0;
	int err;

	//	new_inode(dir->i_sb);
	//	f2fs_inode_info* new_node = sbi->m_fs->NewInode<Cf2fsFileNode>();
	//if (!new_node)	return ERR_PTR<f2fs_inode_info>(-ENOMEM);

	{	auto_lock<semaphore_read_lock> lock_op(sbi->cp_rwsem);	//		sbi->f2fs_lock_op();
		//申请新的nid，返回到ino中；
		if (!NM_I(sbi)->f2fs_alloc_nid(&ino))
		{
			err = -ENOSPC;
			goto fail;
		}
	}

	nid_free = true;

	// not support uid/guid, 但是在inode_init_owner中设置了i_mode
	//inode_init_owner(&init_user_ns, new_node, dir, mode);
//	inode_fsuid_set(inode, mnt_userns);
//	if (dir && dir->i_mode & S_ISGID) {
//		inode->i_gid = dir->i_gid;
		/* Directories are special, and always inherit S_ISGID */

//	if (S_ISDIR(mode))	mode |= S_ISGID;


	//else if ((mode & (S_ISGID | S_IXGRP)) == (S_ISGID | S_IXGRP) &&
	//	!in_group_p(i_gid_into_mnt(mnt_userns, dir)) &&
	//	!capable_wrt_inode_uidgid(mnt_userns, dir, CAP_FSETID))
	//	mode &= ~S_ISGID;
//	}
//	else
//		inode_fsgid_set(inode, mnt_userns);
	i_mode = mode;

	i_ino = ino;
	i_blocks = 0;
	i_mtime = i_atime = i_ctime = current_time(this);
	i_crtime = i_mtime;
	i_generation = prandom_u32();

	LOG_DEBUG(L"[inode_track] add=%p, ino=%d, - init ", this, i_ino);

	if (S_ISDIR(i_mode)) i_current_depth = 1;

	//	err = insert_inode_locked(this);
	//err = m_inodes.insert_inode_locked(this);
	//if (err)
	//{
	//	err = -EINVAL;
	//	goto fail;
	//}

#if 0 //磁盘配额设置，不支持
	if (f2fs_sb_has_project_quota(sbi) && (F2FS_I(dir)->i_flags & F2FS_PROJINHERIT_FL))
		i_projid = F2FS_I(dir)->i_projid;
	else
		i_projid = make_kprojid(&init_user_ns, F2FS_DEF_PROJID);
#endif

	err = fscrypt_prepare_new_inode(dir, this, &encrypt);
	if (err) goto fail_drop;

	err = dquot_initialize(this);
	if (err) goto fail_drop;

	set_inode_flag(FI_NEW_INODE);

	if (encrypt) f2fs_set_encrypted_inode(this);

	if (f2fs_sb_has_extra_attr(sbi))
	{
		set_inode_flag(FI_EXTRA_ATTR);
		i_extra_isize = F2FS_TOTAL_EXTRA_ATTR_SIZE;
	}

	if (test_opt(sbi, INLINE_XATTR)) set_inode_flag(FI_INLINE_XATTR);

	if (test_opt(sbi, INLINE_DATA) && f2fs_may_inline_data(this))
		set_inode_flag(FI_INLINE_DATA);
	if (f2fs_may_inline_dentry(this))
		set_inode_flag(FI_INLINE_DENTRY);

	if (f2fs_sb_has_flexible_inline_xattr(sbi))
	{
		f2fs_bug_on(sbi, !f2fs_has_extra_attr(this));
		if (f2fs_has_inline_xattr(this)) xattr_size = F2FS_OPTION(sbi).inline_xattr_size;
		/* Otherwise, will be 0 */
	}
	else if (f2fs_has_inline_xattr(this) || f2fs_has_inline_dentry())
	{
		xattr_size = DEFAULT_INLINE_XATTR_ADDRS;
	}
	i_inline_xattr_size = xattr_size;

	f2fs_init_extent_tree(this, NULL);

	stat_inc_inline_xattr(this);
	stat_inc_inline_inode(this);
	stat_inc_inline_dir(this);

	i_flags = f2fs_mask_flags(mode, dir->i_flags & F2FS_FL_INHERITED);

	if (S_ISDIR(i_mode)) 	i_flags |= F2FS_INDEX_FL;

	if (i_flags & F2FS_PROJINHERIT_FL) set_inode_flag(FI_PROJ_INHERIT);

	if (f2fs_sb_has_compression(sbi))
	{	/* Inherit the compression flag in directory */
		if ((dir->i_flags & F2FS_COMPR_FL) && f2fs_may_compress(this))
			set_compress_context(this);
	}

	f2fs_set_inode_flags(this);

	//	trace_f2fs_new_inode(this, 0);
	return 0;

fail:
	//	trace_f2fs_new_inode(new_node, err);
	make_bad_inode();
	if (nid_free) set_inode_flag(FI_FREE_NID);
//	iput(this);
//	return ERR_PTR<f2fs_inode_info>(err);
	return err;
fail_drop:
	//	trace_f2fs_new_inode(this, err);
#if 0 //磁盘配额相关，不支持
	dquot_drop(this);
#endif
	i_flags |= S_NOQUOTA;
	if (nid_free) set_inode_flag(FI_FREE_NID);
	clear_nlink();
	unlock_new_inode(this);
//	iput(this);
//	return ERR_PTR<f2fs_inode_info>(err);
	return err;
}
#endif

static inline int is_extension_exist(const std::wstring &s, const wchar_t *sub, bool tmp_ext)
{
	size_t slen = s.size(); //strlen(s);
	size_t sublen = wcslen(sub);
	int i;

	if (sublen == 1 && *sub == '*') return 1;

	/* filename format of multimedia file should be defined as: 
	 * "filename + '.' + extension + (optional: '.' + temp extension)". */
	if (slen < sublen + 2) return 0;

	if (!tmp_ext)
	{	/* file has no temp extension */
		if (s[slen - sublen - 1] != '.') return 0;
		return !_wcsnicmp(s.c_str() + slen - sublen, sub, sublen);
	}

	for (i = 1; i < slen - sublen; i++) 
	{
		if (s[i] != '.') continue;
		if (!_wcsnicmp(s.c_str() + i + 1, sub, sublen))	return 1;
	}

	return 0;
}

/* Set file's temperature for hot/cold data separation */
//static inline void set_file_temperature(f2fs_sb_info *sbi, inode *node, const std::wstring & name)
void f2fs_inode_info::set_file_temperature(const std::wstring& name)
{
	__u8 (*extlist)[F2FS_EXTENSION_LEN] = m_sbi->raw_super->extension_list;
	int i, cold_count, hot_count;

	{
		auto_lock<semaphore_read_lock> lock(m_sbi->sb_lock);
//		down_read(&m_sbi->sb_lock);

		cold_count = le32_to_cpu(m_sbi->raw_super->extension_count);
		hot_count = m_sbi->raw_super->hot_ext_count;

		for (i = 0; i < cold_count + hot_count; i++)
		{
			//<TODO>优化：在mount时将extlist转换为Unicode
			wchar_t ext_fn[F2FS_EXTENSION_LEN];
			memset(ext_fn, 0, F2FS_EXTENSION_LEN * 2);
			char* ext_str = reinterpret_cast<char*>(extlist[i]);
			size_t len = jcvos::Utf8ToUnicode(ext_fn, F2FS_EXTENSION_LEN - 1, ext_str, strlen(ext_str));
			if (is_extension_exist(name, ext_fn, true))	break;
		}
//		up_read(&sbi->sb_lock);
	}
	if (i == cold_count + hot_count)		return;

	if (i < cold_count)		file_set_cold(this);
	else					file_set_hot(this);
}

#if 0 //TODO

int f2fs_update_extension_list(struct f2fs_sb_info *sbi, const char *name,
							bool hot, bool set)
{
	__u8 (*extlist)[F2FS_EXTENSION_LEN] = sbi->raw_super->extension_list;
	int cold_count = le32_to_cpu(sbi->raw_super->extension_count);
	int hot_count = sbi->raw_super->hot_ext_count;
	int total_count = cold_count + hot_count;
	int start, count;
	int i;

	if (set) {
		if (total_count == F2FS_MAX_EXTENSION)
			return -EINVAL;
	} else {
		if (!hot && !cold_count)
			return -EINVAL;
		if (hot && !hot_count)
			return -EINVAL;
	}

	if (hot) {
		start = cold_count;
		count = total_count;
	} else {
		start = 0;
		count = cold_count;
	}

	for (i = start; i < count; i++) {
		if (strcmp(name, extlist[i]))
			continue;

		if (set)
			return -EINVAL;

		memcpy(extlist[i], extlist[i + 1],
				F2FS_EXTENSION_LEN * (total_count - i - 1));
		memset(extlist[total_count - 1], 0, F2FS_EXTENSION_LEN);
		if (hot)
			sbi->raw_super->hot_ext_count = hot_count - 1;
		else
			sbi->raw_super->extension_count =
						cpu_to_le32(cold_count - 1);
		return 0;
	}

	if (!set)
		return -EINVAL;

	if (hot) {
		memcpy(extlist[count], name, strlen(name));
		sbi->raw_super->hot_ext_count = hot_count + 1;
	} else {
		char buf[F2FS_MAX_EXTENSION][F2FS_EXTENSION_LEN];

		memcpy(buf, &extlist[cold_count],
				F2FS_EXTENSION_LEN * hot_count);
		memset(extlist[cold_count], 0, F2FS_EXTENSION_LEN);
		memcpy(extlist[cold_count], name, strlen(name));
		memcpy(&extlist[cold_count + 1], buf,
				F2FS_EXTENSION_LEN * hot_count);
		sbi->raw_super->extension_count = cpu_to_le32(cold_count + 1);
	}
	return 0;
}

#endif

static void set_compress_inode(f2fs_sb_info *sbi, inode *inode, const std::wstring & name)
{
	__u8 (*extlist)[F2FS_EXTENSION_LEN] = sbi->raw_super->extension_list;

	wchar_t (*ext)[F2FS_EXTENSION_LEN];
	unsigned int ext_cnt = F2FS_OPTION(sbi).compress_ext_cnt;
	int /*i, */cold_count, hot_count;

	if (!f2fs_sb_has_compression(sbi) || is_inode_flag_set(inode, FI_COMPRESSED_FILE) ||
			F2FS_I(inode)->i_flags & F2FS_NOCOMP_FL || !f2fs_may_compress(inode))
		return;

	{
		auto_lock<semaphore_read_lock> lock(sbi->sb_lock);
//		down_read(&sbi->sb_lock);

		cold_count = le32_to_cpu(sbi->raw_super->extension_count);
		hot_count = sbi->raw_super->hot_ext_count;

		for (int i = cold_count; i < cold_count + hot_count; i++)
		{
			wchar_t ext_fn[F2FS_EXTENSION_LEN];
			char* ext_str = reinterpret_cast<char*>(extlist[i]);
			jcvos::Utf8ToUnicode(ext_fn, F2FS_EXTENSION_LEN - 1, ext_str, strlen(ext_str));
			if (is_extension_exist(name, ext_fn, false))
			{
				up_read(&sbi->sb_lock);
				return;
			}
		}
//		up_read(&sbi->sb_lock);
	}

	ext = F2FS_OPTION(sbi).extensions;

	for (UINT i = 0; i < ext_cnt; i++) 
	{
		if (!is_extension_exist(name, ext[i], false))		continue;
		set_compress_context(inode);
		return;
	}
}

//static int f2fs_create(user_namespace *mnt_userns, struct inode *dir, struct dentry *dentry, umode_t mode, bool excl)
int Cf2fsDirInode::create(user_namespace *mnt_userns, dentry *entry, umode_t mode, bool excl)
{
	f2fs_sb_info *sbi = F2FS_I_SB(this);
//	inode *new_inode;
	nid_t ino = 0;
	int err =0;

	if (unlikely(sbi->f2fs_cp_error()))		return -EIO;
	if (!sbi->f2fs_is_checkpoint_ready())		return -ENOSPC;

	err = dquot_initialize(this);
	if (err)		return err;

//	f2fs_inode_info * new_inode = m_sbi->f2fs_new_inode<Cf2fsFileNode>(this, mode);
	f2fs_inode_info * new_inode = m_sbi->f2fs_new_inode(this, mode, FILE_INODE);
	if (IS_ERR(new_inode))		return PTR_ERR(new_inode);
	std::wstring fn;
#ifdef _DEBUG
	jcvos::Utf8ToUnicode(fn, entry->d_name.name);
	new_inode->m_description = L"file of " + fn;
	//, new_inode->i_size_lock.LockCount);
#endif
	LOG_DEBUG(L"[inode track] add=%p, ino=%d, fn=%s, create for new file", new_inode, new_inode->i_ino, fn.c_str());

//	if (!test_opt(sbi, DISABLE_EXT_IDENTIFY)) new_inode->set_file_temperature(entry->d_name.name);
	if (!test_opt(sbi, DISABLE_EXT_IDENTIFY)) new_inode->set_file_temperature(fn);

//	set_compress_inode(sbi, new_inode, entry->d_name.name);
	set_compress_inode(sbi, new_inode, fn);
	ino = new_inode->i_ino;
	{	//sbi->f2fs_lock_op();
		auto_lock<semaphore_read_lock> lock_op(sbi->cp_rwsem);
		// 将inode添加到entry的父节点中
		err = f2fs_add_link(entry, new_inode);
		if (err)
		{
			f2fs_handle_failed_inode(new_inode);
			return err;
		}
		//sbi->f2fs_unlock_op();
	}

	sbi->nm_info->f2fs_alloc_nid_done(ino);
	d_instantiate_new(entry, new_inode);
	dentry * dd = new_inode->splice_alias(entry);
	dput(dd);

	if (IS_DIRSYNC(this))		sbi->sync_fs(1);
	sbi->f2fs_balance_fs(true);

#ifdef _DEBUG
	DebugListItems();
#endif

	return 0;
//out:
//	f2fs_handle_failed_inode(new_inode);
//	return err;
}

#if 0

static int f2fs_link(struct dentry *old_dentry, struct inode *dir,
		struct entry *dentry)
{
	struct inode *inode = d_inode(old_dentry);
	struct f2fs_sb_info *sbi = F2FS_I_SB(dir);
	int err;

	if (unlikely(sbi->f2fs_cp_error()))
		return -EIO;
	if (!sbi->f2fs_is_checkpoint_ready())
		return -ENOSPC;

	err = fscrypt_prepare_link(old_dentry, dir, dentry);
	if (err)
		return err;

	if (is_inode_flag_set(dir, FI_PROJ_INHERIT) &&
			(!projid_eq(F2FS_I(dir)->i_projid,
			F2FS_I(old_dentry->d_inode)->i_projid)))
		return -EXDEV;

	err = dquot_initialize(dir);
	if (err)
		return err;

	sbi->f2fs_balance_fs(true);

	inode->i_ctime = current_time(inode);
	ihold(inode);

	set_inode_flag(inode, FI_INC_LINK);
	sbi->f2fs_lock_op();
//	auto_lock<semaphore_read_lock> lock_op(sbi->cp_rwsem);
	err = f2fs_add_link(dentry, inode);
	if (err)
		goto out;
	sbi->f2fs_unlock_op();

	d_instantiate(dentry, inode);

	if (IS_DIRSYNC(dir))
		sbi->sync_fs(1);
	return 0;
out:
	clear_inode_flag(inode, FI_INC_LINK);
	iput(inode);
	sbi->f2fs_unlock_op();
	return err;
}

struct dentry *f2fs_get_parent(struct dentry *child)
{
	struct page *page;
	unsigned long ino = f2fs_inode_by_name(d_inode(child), &dotdot_name, &page);

	if (!ino) {
		if (IS_ERR(page))
			return ERR_CAST(page);
		return ERR_PTR<dentry>(-ENOENT);
	}
	return d_obtain_alias(f2fs_iget(child->d_sb, ino));
}

#endif

//static int __recover_dot_dentries(struct inode *dir, nid_t pino)
int Cf2fsDirInode::__recover_dot_dentries(nid_t pino)
{
	f2fs_sb_info *sbi = F2FS_I_SB(this);
	// qstr dot; dot.name = ".", // = QSTR_INIT(".", 1);
	const qstr dot(L".");
	const qstr dotdot(L"..");
//	struct f2fs_dir_entry *de;
//	struct page *page;
	int err = 0;

	if (sbi->f2fs_readonly()) 
	{
		LOG_NOTICE(L"skip recovering inline_dots inode (ino:%lu, pino:%u) in readonly mountpoint", i_ino, pino);
		return 0;
	}

	err = dquot_initialize(this);
	if (err) return err;

	sbi->f2fs_balance_fs(true);

	{auto_lock<semaphore_read_lock> lock_op(sbi->cp_rwsem);
	//	sbi->f2fs_lock_op();
		struct page *page;

		//Cf2fsDirInode* di = dynamic_cast<Cf2fsDirInode*>(this);
		//if (!di) THROW_ERROR(ERR_APP, L"[err] only dir inode support this feature");

		f2fs_dir_entry *de = f2fs_find_entry(&dot, &page);
		if (de) { f2fs_put_page(page, 0); }
		else if (IS_ERR(page))
		{
			return PTR_ERR(page);
		//	goto out;
		
		}
		else
		{
			err = f2fs_do_add_link(&dot, NULL, i_ino, S_IFDIR);
			if (err)		return err;
		}

		de = f2fs_find_entry(&dotdot, &page);
		if (de)						f2fs_put_page(page, 0);
		else if (IS_ERR(page)) 		err = PTR_ERR(page);
		else						err = f2fs_do_add_link(&dotdot, NULL, pino, S_IFDIR);
	//out:
		if (!err) clear_inode_flag(this, FI_INLINE_DOTS);

	//	sbi->f2fs_unlock_op();
	}
	return err;
}

//static struct dentry* f2fs_lookup(struct inode* dir, struct dentry* dentry,	unsigned int flags)
// 在当前的inode中查找子节点。src_entry作为名称的输入，返回的字节的也在src_entry中。函数返回值仅是错误代码；
//
dentry *Cf2fsDirInode::lookup(dentry *src_entry, unsigned int flags)
{
//	f2fs_inode_info *new_node = NULL;
	f2fs_dir_entry *de;
	page *page;
	dentry *new_entry;
	nid_t ino = -1;
	int err = 0;
	unsigned int root_ino = m_sbi->F2FS_ROOT_INO();
	
	f2fs_filename fname;
	LOG_DEBUG(L"looup for %S", src_entry->d_name.name.c_str());
//	trace_f2fs_lookup_start(dir, dentry, flags);

	if (src_entry->d_name.len() > F2FS_NAME_LEN)
	{
		LOG_ERROR(L"[err] file name is too long (%d), limit=%d", src_entry->d_name.len(), F2FS_NAME_LEN);
		return ERR_PTR<dentry>(-ENAMETOOLONG);
	}
	// 名称从src_entry复制到fname
	err = f2fs_prepare_lookup(src_entry, &fname);
	generic_set_encrypted_ci_d_ops(src_entry);
	if (err == -ENOENT)		return ERR_PTR<dentry>( err);	//goto out_splice;
	if (err)	{		return ERR_PTR<dentry>(-ENAMETOOLONG);	}
	de = __f2fs_find_entry(&fname, &page);
//	f2fs_free_filename(&fname);	//析构函数

	if (!de) 
	{	// 没有找到
		if (IS_ERR(page)) 		{/*	return ERR_PTR<dentry>(PTR_ERR(page));	*/}
		err = -ENOENT;
		return ERR_PTR<dentry>(err);
	}

	ino = le32_to_cpu(de->ino);
	f2fs_put_page(page, 0);

	LOG_DEBUG(L"find inode by ino=%d", ino);
	f2fs_inode_info * new_node = m_sbi->f2fs_iget(ino);

	if (IS_ERR(new_node)) 	{	return ERR_PTR<dentry>(PTR_ERR(new_node));	}
//#ifdef _DEBUG
//	std::wstring fn;
//	jcvos::Utf8ToUnicode(fn, fname.usr_fname->name);
//	new_node->m_description = L"file of " + fn;
	LOG_DEBUG(L"[inode_track] addr=%p, ino=%d, lock=%d, file=%S - got for file",
		new_node, new_node->i_ino, new_node->i_size_lock.LockCount, src_entry->d_name.name.c_str());
//#endif

	if ((i_ino == root_ino) && f2fs_has_inline_dots())
	{
		err = __recover_dot_dentries(root_ino);
		if (err) goto out_iput;
	}

	if (new_node->f2fs_has_inline_dots())
	{
		Cf2fsDirInode* di = dynamic_cast<Cf2fsDirInode*>(new_node);
		if (!di) THROW_ERROR(ERR_APP, L"[err] new_node is not a dir inode, only dir inode support this feature");
		err = di->__recover_dot_dentries(i_ino);
		if (err) goto out_iput;
	}
	if (IS_ENCRYPTED(this) && (S_ISDIR(new_node->i_mode) 
		|| S_ISLNK(new_node->i_mode)) && !fscrypt_has_permitted_context(this, new_node)) 
	{
		LOG_WARNING(L"Inconsistent encryption contexts: %lu/%lu", i_ino, new_node->i_ino);
		err = -EPERM;
		goto out_iput;
	}
//out_splice:
#ifdef CONFIG_UNICODE
	if (!new_node && IS_CASEFOLDED(this))
	{
		/* Eventually we want to call d_add_ci(src_entry, NULL) for negative dentries in the encoding case as
		 * well.  For now, prevent the negative dentry from being cached. */
//		trace_f2fs_lookup_end(dir, src_entry, ino, err);
		return NULL;
	}
#endif
	//new_entry = d_splice_alias(new_node, src_entry);
	new_entry = new_node->splice_alias(src_entry);
	dput(src_entry);
	err = IS_ERR(new_entry)?reinterpret_cast<UINT64>(new_entry):0;
//	trace_f2fs_lookup_end(dir, src_entry, ino, !new_entry ? -ENOENT : err);
	return new_entry;
out_iput:
	iput(new_node);
//	trace_f2fs_lookup_end(dir, src_entry, ino, err);
	return ERR_PTR<dentry>(err);
}

int Cf2fsDirInode::enum_from_dentry_ptr(dentry* parent, const f2fs_dentry_ptr* d, std::list<dentry*>& result)
{
//	f2fs_dir_entry* de;
	unsigned long bit_pos = 0;
	int max_len = 0;
	int res = 0;
	UINT root_ino = m_sbi->F2FS_ROOT_INO();
	int err = 0;

//	if (max_slots) 	*max_slots = 0;
	while (bit_pos < d->max)
	{
		if (!test_bit_le(bit_pos, (UINT8*)d->bitmap))
		{
			bit_pos++;
			max_len++;
			continue;
		}

		f2fs_dir_entry * de = &d->dentry[bit_pos];

		if (unlikely(!de->name_len))
		{
			bit_pos++;
			continue;
		}

		nid_t ino = le32_to_cpu(de->ino);
		//						f2fs_put_page(page, 0);
		f2fs_inode_info* new_node = m_sbi->f2fs_iget(ino);
		if (IS_ERR(new_node))			THROW_ERROR(ERR_MEM, L"failed on getting inode, ino=%d", ino);
		if ((i_ino == root_ino) && f2fs_has_inline_dots())
		{
			err = __recover_dot_dentries(root_ino);
			if (err)
			{
				iput(new_node);
				THROW_ERROR(ERR_APP, L"failed on recover dot dentries");
			}
		}

		if (new_node->f2fs_has_inline_dots())
		{
			Cf2fsDirInode* di = dynamic_cast<Cf2fsDirInode*>(new_node);
			if (!di) THROW_ERROR(ERR_APP, L"new_node is not a dir inode, only dir inode support this feature");
			err = di->__recover_dot_dentries(i_ino);
			if (err)
			{
				iput(new_node);
				THROW_ERROR(ERR_APP, L"failed on recover dot dentires");
			}
		}

		if (IS_ENCRYPTED(this) && (S_ISDIR(new_node->i_mode) || S_ISLNK(new_node->i_mode))
			&& !fscrypt_has_permitted_context(this, new_node))
		{
			LOG_WARNING(L"Inconsistent encryption contexts: %lu/%lu", i_ino, new_node->i_ino);
			err = -EPERM;
			iput(new_node);
		}

		//				new_entry = d_splice_alias(new_node, src_entry);
		qstr name((char*)d->filename[bit_pos], le16_to_cpu(de->name_len));
		dentry* new_entry = d_alloc(parent, name);
//		d_splice_alias(new_node, new_entry);
		dentry* _new_entry = new_node->splice_alias(new_entry);
		dput(new_entry);

		err = IS_ERR(_new_entry) ? reinterpret_cast<UINT64>(_new_entry) : 0;
		result.push_back(_new_entry);

		//if (max_slots && max_len > *max_slots)		*max_slots = max_len;
		max_len = 0;
		bit_pos += GET_DENTRY_SLOTS(le16_to_cpu(de->name_len));
	}
	return err;
}

int Cf2fsDirInode::enum_childs(dentry* parent, std::list<dentry*>& result)
{
	int err = 0;
//	unsigned int root_ino = m_sbi->F2FS_ROOT_INO();

//	de = __f2fs_find_entry(&fname, &page);
// <BEGIN> __f2fs_find_entry(&fname, &page);
	if (f2fs_has_inline_dentry())
	{
//		de = f2fs_find_in_inline_dir(this, fname, res_page);
//<BEGIN> f2fs_find_in_inline_dir()
//		f2fs_dir_entry* de;
		f2fs_dentry_ptr d;
		void* inline_dentry;

		page * ipage = m_sbi->f2fs_get_node_page(i_ino);
		if (IS_ERR(ipage)) THROW_ERROR(ERR_APP, L"failed on getting node page, ino=%d", i_ino);

		inline_dentry = inline_data_addr(ipage);

		make_dentry_ptr_inline(&d, inline_dentry);
//		de = f2fs_find_target_dentry(&d, fname, NULL);
		int max_slots = 0;
		err = enum_from_dentry_ptr(parent, &d, result);
		unlock_page(ipage);
		f2fs_put_page(ipage, 0);
//<END> f2fs_find_in_inline_dir
//		goto out;
	}
	else
	{
		UINT64 npages = dir_blocks();
		unsigned int max_depth;
		unsigned int level;
		if (npages == 0)		return err;
		max_depth = i_current_depth;
		if (unlikely(max_depth > MAX_DIR_HASH_DEPTH))
		{	// 错误处理
			LOG_WARNING(L"Corrupted max_depth of %lu: %u", i_ino, max_depth);
			max_depth = MAX_DIR_HASH_DEPTH;
			f2fs_i_depth_write(max_depth);
		}

		UINT bidx = 0;
		for (level = 0; level < max_depth; level++)
		{
			//		de = find_in_level(level, fname, res_page);
			// <BEGIN> find_in_level()
			UINT nbucket = dir_buckets(level, i_dir_level);		// 这一层中有多少 buckets
			UINT nblock = bucket_blocks(level);						// 每个buckets有多少block

			// 以 fname->hash值作为当前层bucket地址。计算bucket地址对应的inode中的logical block的地址
			// 此处检索所有block，因此不需要相应的转换。
			UINT nblock_per_level = nbucket * nblock;
			for (UINT bb = 0; bb < nblock_per_level; bb++, bidx++)
			{
				if (bidx >= npages) break;
				/* no need to allocate new dentry pages to all the indices */
				page* dentry_page = f2fs_find_data_page(bidx);
				if (IS_ERR(dentry_page))
				{
					if (PTR_ERR(dentry_page) == -ENOENT)
					{
//						room = true;
						continue;
					}
					else THROW_ERROR(ERR_APP, L"failed on open data page, blocks=%d, index=%d", npages, bidx);
				}

				//			de = find_in_block(dentry_page, fname, &max_slots);
// <BEGIN> find_in_block
				f2fs_dentry_block* dentry_blk = page_address<f2fs_dentry_block>(dentry_page);
				f2fs_dentry_ptr d;
				// 从ondisk到in mem数据转换
				make_dentry_ptr_block(&d, dentry_blk);
				err = enum_from_dentry_ptr(parent, &d, result);
				//			return f2fs_find_target_dentry(&d, fname, max_slots);
// <END> find_in_block

				//			if (max_slots >= s)		room = true;
				f2fs_put_page(dentry_page, 0);
			}
	// <END> find_in_level
		}
	}
// <END>  __f2fs_find_entry(&fname, &page);
	return err;
}

//static int f2fs_unlink(struct inode *dir, struct dentry *dentry)
// 删除文件。ddentry为要删除文件的dentry结构
int Cf2fsDirInode::unlink(dentry* ddentry)
{
	f2fs_sb_info* sbi = m_sbi;
	f2fs_inode_info *iinode= F2FS_I(d_inode(ddentry));		// 要删除文件的inode
	f2fs_dir_entry *de;
	page *ppage;
	int err;

//	trace_f2fs_unlink_enter(dir, ddentry);

	if (unlikely(sbi->f2fs_cp_error())) 
	{
		err = -EIO;
		goto fail;
	}

	err = dquot_initialize(this);
	if (err)	goto fail;
	err = dquot_initialize(iinode);
	if (err)	goto fail;

	de = f2fs_find_entry(&ddentry->d_name, &ppage);
	if (!de) 
	{
		if (IS_ERR(ppage))	err = PTR_ERR(ppage);
		goto fail;
	}

	sbi->f2fs_balance_fs(true);

	sbi->f2fs_lock_op();
	//auto_lock<semaphore_read_lock> lock_op(sbi->cp_rwsem);
	err = sbi->f2fs_acquire_orphan_inode();
	if (err) 
	{
		sbi->f2fs_unlock_op();
		f2fs_put_page(ppage, 0);
		goto fail;
	}
	f2fs_delete_entry(de, ppage, iinode);
//<TODO>
	// 按照rename中，replace新文件的处理方式，f2fs_delete_entry()以后，
	//		还要 inode->f2fs_i_links_write(flase) 减少inode的引用计数
	//		如果 inode的link数为0，这 f2fs_add_orphan_inode()，
	//		或者 link数不为0，则 f2fs_release_orphan_inode()
#ifdef CONFIG_UNICODE
	/* VFS negative dentries are incompatible with Encoding and Case-insensitiveness. Eventually we'll want avoid invalidating the dentries here, alongside with returning the negative dentries at f2fs_lookup(), when it is better supported by the VFS for the CI case. */
	if (IS_CASEFOLDED(this))	d_invalidate(ddentry);
#endif
	sbi->f2fs_unlock_op();

	if (IS_DIRSYNC(this))		sbi->sync_fs(1);
fail:
//	trace_f2fs_unlink_exit(iinode, err);
	return err;
}

#if 0 //TODO


static const char *f2fs_get_link(struct dentry *dentry,
				 struct inode *inode,
				 struct delayed_call *done)
{
	const char *link = page_get_link(dentry, inode, done);

	if (!IS_ERR(link) && !*link) {
		/* this is broken symlink case */
		do_delayed_call(done);
		clear_delayed_call(done);
		link = ERR_PTR(-ENOENT);
	}
	return link;
}

static int f2fs_symlink(struct user_namespace *mnt_userns, struct inode *dir,
			struct dentry *dentry, const char *symname)
{
	struct f2fs_sb_info *sbi = F2FS_I_SB(dir);
	struct inode *inode;
	size_t len = strlen(symname);
	struct fscrypt_str disk_link;
	int err;

	if (unlikely(sbi->f2fs_cp_error()))
		return -EIO;
	if (!sbi->f2fs_is_checkpoint_ready())
		return -ENOSPC;

	err = fscrypt_prepare_symlink(dir, symname, len, dir->i_sb->s_blocksize,
				      &disk_link);
	if (err)
		return err;

	err = dquot_initialize(dir);
	if (err)
		return err;

	inode = f2fs_new_inode(dir, S_IFLNK | S_IRWXUGO);
	if (IS_ERR(inode))
		return PTR_ERR(inode);

	if (IS_ENCRYPTED(inode))
		inode->i_op = &f2fs_encrypted_symlink_inode_operations;
	else
		inode->i_op = &f2fs_symlink_inode_operations;
	inode->inode_nohighmem();
	inode->i_mapping->a_ops = &f2fs_dblock_aops;

	sbi->f2fs_lock_op();
	//auto_lock<semaphore_read_lock> lock_op(sbi->cp_rwsem);

	err = f2fs_add_link(dentry, inode);
	if (err)
		goto out_f2fs_handle_failed_inode;
	sbi->f2fs_unlock_op();
	f2fs_alloc_nid_done(sbi, inode->i_ino);

	err = fscrypt_encrypt_symlink(inode, symname, len, &disk_link);
	if (err)
		goto err_out;

	err = page_symlink(inode, disk_link.name, disk_link.len);

err_out:
	d_instantiate_new(dentry, inode);

	/*
	 * Let's flush symlink data in order to avoid broken symlink as much as
	 * possible. Nevertheless, fsyncing is the best way, but there is no
	 * way to get a file descriptor in order to flush that.
	 *
	 * Note that, it needs to do dir->fsync to make this recoverable.
	 * If the symlink path is stored into inline_data, there is no
	 * performance regression.
	 */
	if (!err) {
		filemap_write_and_wait_range(inode->i_mapping, 0,
							disk_link.len - 1);

		if (IS_DIRSYNC(dir))
			sbi->sync_fs(1);
	} else {
		f2fs_unlink(dir, dentry);
	}

	sbi->f2fs_balance_fs(true);
	goto out_free_encrypted_link;

out_f2fs_handle_failed_inode:
	f2fs_handle_failed_inode(inode);
out_free_encrypted_link:
	if (disk_link.name != (unsigned char *)symname)
		kfree(disk_link.name);
	return err;
}

#endif //<TODO>

//static int f2fs_mkdir(struct user_namespace *mnt_userns, struct inode *dir,   struct dentry *dentry, umode_t mode)
int Cf2fsDirInode::mkdir(user_namespace* mnt_userns, dentry* ddentry, umode_t mode)
{
	//struct f2fs_sb_info* sbi = m_sbi; // F2FS_I_SB(dir);
//	struct inode *inode;
	int err;

	if (unlikely(m_sbi->f2fs_cp_error()))	return -EIO;

	err = dquot_initialize(this);
	if (err)	return err;

//	f2fs_inode_info * iinode = GetFs()->f2fs_new_inode<Cf2fsDirInode>(this, S_IFDIR | mode);
	f2fs_inode_info* iinode = m_sbi->f2fs_new_inode(this, S_IFDIR | mode, DIR_INODE);
	if (IS_ERR(iinode))	return PTR_ERR(iinode);

	LOG_DEBUG(L"[inode_track] addr=%p, ino=%d, fn=%S, - create for new dir", iinode, iinode->i_ino, ddentry->d_name.name.c_str());

	iinode->inode_nohighmem();
	iinode->set_inode_flag(FI_INC_LINK);
//	m_sbi->f2fs_lock_op();
	{
		auto_lock<semaphore_read_lock> lock_op(m_sbi->cp_rwsem);
		err = f2fs_add_link(ddentry, iinode);
		if (err)
		{
			clear_inode_flag(iinode, FI_INC_LINK);
			f2fs_handle_failed_inode(iinode);
			return err;
		}
//		m_sbi->f2fs_unlock_op();
	}
	m_sbi->nm_info->f2fs_alloc_nid_done(iinode->i_ino);
	d_instantiate_new(ddentry, iinode);
	dentry* dd = iinode->splice_alias(ddentry);
	dput(dd);

	if (IS_DIRSYNC(this))	m_sbi->sync_fs(1);
	m_sbi->f2fs_balance_fs(true);
	return 0;
}

#if 0 //<TODO>

static int f2fs_rmdir(struct inode *dir, struct dentry *dentry)
{
	struct inode *inode = d_inode(dentry);

	if (f2fs_empty_dir(inode))
		return f2fs_unlink(dir, dentry);
	return -ENOTEMPTY;
}

static int f2fs_mknod(struct user_namespace *mnt_userns, struct inode *dir,
		      struct dentry *dentry, umode_t mode, dev_t rdev)
{
	struct f2fs_sb_info *sbi = F2FS_I_SB(dir);
	struct inode *inode;
	int err = 0;

	if (unlikely(sbi->f2fs_cp_error()))
		return -EIO;
	if (!sbi->f2fs_is_checkpoint_ready())
		return -ENOSPC;

	err = dquot_initialize(dir);
	if (err)
		return err;

	inode = f2fs_new_inode(dir, mode);
	if (IS_ERR(inode))
		return PTR_ERR(inode);

	init_special_inode(inode, inode->i_mode, rdev);
	inode->i_op = &f2fs_special_inode_operations;

	sbi->f2fs_lock_op();
	//auto_lock<semaphore_read_lock> lock_op(sbi->cp_rwsem);

	err = f2fs_add_link(dentry, inode);
	if (err)
		goto out;
	sbi->f2fs_unlock_op();

	f2fs_alloc_nid_done(sbi, inode->i_ino);

	d_instantiate_new(dentry, inode);

	if (IS_DIRSYNC(dir))
		sbi->sync_fs(1);

	sbi->f2fs_balance_fs(true);
	return 0;
out:
	f2fs_handle_failed_inode(inode);
	return err;
}
#endif
//static int __f2fs_tmpfile(struct inode *dir, struct dentry *dentry, umode_t mode, struct inode **whiteout)
int Cf2fsDirInode::__f2fs_tmpfile(dentry* dentry, umode_t mode, f2fs_inode_info** whiteout)
{
#if 0
	f2fs_sb_info *sbi = m_sbi;
//	struct inode *iinode;
	int err;

	err = dquot_initialize(this);
	if (err) return err;

	f2fs_inode_info* iinode;

	if (whiteout) 
	{
		iinode = m_sbi->m_fs->f2fs_new_inode<Cf2fsSpecialInode>(this, mode);
		init_special_inode(iinode, iinode->i_mode, WHITEOUT_DEV);
		//iinode->i_op = &f2fs_special_inode_operations;
	}
	else 
	{
		iinode = m_sbi->m_fs->f2fs_new_inode<Cf2fsFileNode>(this, mode);
		//iinode->i_op = &f2fs_file_inode_operations;
		//iinode->i_fop = &f2fs_file_operations;
		//iinode->i_mapping->a_ops = &f2fs_dblock_aops;
	}
	if (IS_ERR(iinode))	return PTR_ERR(iinode);

	sbi->f2fs_lock_op();
	//auto_lock<semaphore_read_lock> lock_op(sbi->cp_rwsem);

	err = f2fs_acquire_orphan_inode(sbi);
	if (err)
		goto out;

	err = f2fs_do_tmpfile(iinode, this);
	if (err)	goto release_out;

	/* add this non-linked tmpfile to orphan list, in this way we could remove all unused data of tmpfile after abnormal power-off. */
	f2fs_add_orphan_inode(iinode);
	f2fs_alloc_nid_done(sbi, iinode->i_ino);

	if (whiteout) {
		f2fs_i_links_write(iinode, false);

		spin_lock(&iinode->i_lock);
		iinode->i_state |= I_LINKABLE;
		spin_unlock(&iinode->i_lock);

		*whiteout = iinode;
	} else {
		d_tmpfile(dentry, iinode);
	}
	/* link_count was changed by d_tmpfile as well. */
	sbi->f2fs_unlock_op();
	unlock_new_inode(iinode);

	sbi->f2fs_balance_fs(true);
	return 0;

release_out:
	f2fs_release_orphan_inode(sbi);
out:
	f2fs_handle_failed_inode(iinode);
	return err;
#else
	JCASSERT(0);
	return 0;
#endif

}
#if 0

static int f2fs_tmpfile(struct user_namespace *mnt_userns, struct inode *dir,
			struct dentry *dentry, umode_t mode)
{
	struct f2fs_sb_info *sbi = F2FS_I_SB(dir);

	if (unlikely(sbi->f2fs_cp_error()))
		return -EIO;
	if (!sbi->f2fs_is_checkpoint_ready())
		return -ENOSPC;

	return __f2fs_tmpfile(dir, dentry, mode, NULL);
}
#endif

//static int f2fs_create_whiteout(struct inode *dir, struct inode **whiteout)
int Cf2fsDirInode::f2fs_create_whiteout(f2fs_inode_info** whiteout)
{
	if (unlikely(m_sbi->f2fs_cp_error()))	return -EIO;
	return __f2fs_tmpfile(NULL, S_IFCHR | WHITEOUT_MODE, whiteout);
}

int f2fs_sb_info::f2fs_rename(Cf2fsDirInode *old_dir, dentry *old_dentry, Cf2fsDirInode*new_dir, dentry *new_dentry, unsigned int flags)
{
	LOG_STACK_TRACE();
	// old_dir:			源文件		所在 父目录的		inode
	// old_dir_entry:	源文件		所在父目录的		dentry, f2fs_dir_entry
	// old_dentry:		源文件的						dentry
	// old_inode:		源文件的						inode
	// f2fs_old_inode:	源文件的						inode, f2fs_inode_info
	// 
	// old_dir_entry
	// new_dir:    目标文件 所在 父目录的		inode
	// new_dentry: 目标文件					dentry (可能为存在，只有文件名）
	// new_inode:  目标文件的				inode (可能不存在，存在则为覆盖）

//	f2fs_sb_info *sbi = F2FS_I_SB(old_dir);
	inode *old_inode = d_inode(old_dentry);
	f2fs_inode_info* f2fs_old_inode = F2FS_I(old_inode);
	inode *new_inode = d_inode(new_dentry);
	f2fs_inode_info *whiteout = NULL;
	page *old_dir_page = NULL;
	page *old_page, *new_page = NULL;
	f2fs_dir_entry *old_dir_entry = NULL;
	f2fs_dir_entry *old_entry;
	f2fs_dir_entry *new_entry;
	int err;

	if (unlikely(f2fs_cp_error()))		return -EIO;
	if (!f2fs_is_checkpoint_ready())	return -ENOSPC;

	// 不支持磁盘配额
	//if (is_inode_flag_set(new_dir, FI_PROJ_INHERIT) &&
	//		(!projid_eq(F2FS_I(new_dir)->i_projid, F2FS_I(old_dentry->d_inode)->i_projid)))
	//	return -EXDEV;

	/* If new_inode is null, the below renaming flow will add a link in old_dir which can conver inline_dir. After then, if we failed to get the entry due to other reasons like ENOMEM, we had to remove the new entry. Instead of adding such the error handling routine, let's simply convert first here. */
	if (old_dir == new_dir && !new_inode) 
	{
		err = old_dir->f2fs_try_convert_inline_dir(new_dentry);
		if (err) return err;
	}

	if (flags & RENAME_WHITEOUT) 
	{
		err = old_dir->f2fs_create_whiteout(&whiteout);
		if (err) return err;
	}

	err = dquot_initialize(old_dir);
	if (err) goto out;

	err = dquot_initialize(new_dir);
	if (err) goto out;

	if (new_inode) 
	{
		err = dquot_initialize(new_inode);
		if (err) goto out;
	}

	err = -ENOENT;
	old_entry = old_dir->f2fs_find_entry(&old_dentry->d_name, &old_page);
	if (!old_entry) 
	{
		if (IS_ERR(old_page)) err = PTR_ERR(old_page);
		goto out;
	}

	if (S_ISDIR(old_inode->i_mode)) 
	{
		Cf2fsDirInode* old_inode_dir = dynamic_cast<Cf2fsDirInode*>(f2fs_old_inode);
		if (old_inode_dir == nullptr) THROW_ERROR(ERR_APP, L"type mismatch, old_inode is not a dir");
		old_dir_entry = old_inode_dir->f2fs_parent_dir(&old_dir_page);
		if (!old_dir_entry) 
		{
			if (IS_ERR(old_dir_page)) err = PTR_ERR(old_dir_page);
			goto out_old;
		}
	}

	if (new_inode) 
	{	// 目标文件已经存在
		if (flags & RENAME_NOREPLACE)
		{	// 目标文件已经存下，返回already exist
			err = -EEXIST;
			goto out_dir;
		}
		else
		{	// 否则覆盖旧的文件
			f2fs_inode_info* f2fs_new_inode = F2FS_I(new_inode);
			if (f2fs_new_inode->is_dir())
			{
				// 根据winfstest的测试结果，从文件移动覆盖到目录，不论目录是否为空，都返回ACCESS_DENIDED
#if 0		
				Cf2fsDirInode* new_inode_dir = dynamic_cast<Cf2fsDirInode*>(new_inode);
				if (new_inode_dir == nullptr) THROW_ERROR(ERR_APP, L"new inode is not a dir");
				err = -ENOTEMPTY;
				if (old_dir_entry && !new_inode_dir->f2fs_empty_dir())	goto out_dir;
#endif
				err = -EACCES;
				goto out_dir;
			}
			if (f2fs_new_inode->i_mode & FMODE_READONLY)
			{
				err = -EACCES;
				goto out_dir;
			}
			err = -ENOENT;
			// new_page是new_dir中，new_entry所在的page
			new_entry = new_dir->f2fs_find_entry(&new_dentry->d_name, &new_page);
			if (!new_entry)
			{
				if (IS_ERR(new_page))	err = PTR_ERR(new_page);
				goto					out_dir;
			}

			f2fs_balance_fs(true);
			f2fs_lock_op();
			//auto_lock<semaphore_read_lock> lock_op(this->cp_rwsem);

			err = f2fs_acquire_orphan_inode();
			if (err)		goto put_out_dir;

			// 替换new_entry中inode为old_inode
			new_dir->f2fs_set_link(new_entry, new_page, old_inode);
			new_page = NULL;

			new_inode->i_ctime = current_time(new_inode);
			down_write(&f2fs_new_inode->i_sem);
			if (old_dir_entry)
			{
				f2fs_new_inode->f2fs_i_links_write(false);
				LOG_DEBUG(L"[inode_track] addr=%p, ino=%d, link=%d - dec_link for replace old", f2fs_new_inode, f2fs_new_inode->i_ino, f2fs_new_inode->i_nlink);
			}
			f2fs_new_inode->f2fs_i_links_write(false);
			LOG_DEBUG(L"[inode_track] addr=%p, ino=%d, link=%d - dec_link for replace", f2fs_new_inode, f2fs_new_inode->i_ino, f2fs_new_inode->i_nlink);
			up_write(&f2fs_new_inode->i_sem);

			if (!new_inode->i_nlink)	f2fs_add_orphan_inode(f2fs_new_inode);
			else						f2fs_release_orphan_inode();
		}
	} 
	else	// new_inode == nullptr
	{	// 新的目标不存在，将文件添加到新的目标
		f2fs_balance_fs(true);
		f2fs_lock_op();
		//auto_lock<semaphore_read_lock> lock_op(this->cp_rwsem);
		//Cf2fsDirInode * parent_inode = dynamic_cast<Cf2fsDirInode*>(d_inode(new_dentry->d_parent));
		//if (parent_inode == nullptr) THROW_ERROR(ERR_APP, L"parent is not a dir");
		err = new_dir->f2fs_add_link(new_dentry, f2fs_old_inode );		// old_inode, f2fs_old_inode为需要移动的文件实体
		if (err) 
		{
			f2fs_unlock_op();
			goto out_dir;
		}

		if (old_dir_entry)
		{
			new_dir->f2fs_i_links_write(true);
			LOG_DEBUG(L"[inode_track] addr=%p, ino=%d, link=%d - inc_link for new dir", new_dir, new_dir->i_ino, new_dir->i_nlink);
		}
	}

	down_write(&f2fs_old_inode->i_sem);
	if (!old_dir_entry || whiteout)		file_lost_pino(f2fs_old_inode);
	/* adjust dir's i_pino to pass fsck check */
	else	f2fs_old_inode->f2fs_i_pino_write(new_dir->i_ino);
	up_write(&F2FS_I(old_inode)->i_sem);

	old_inode->i_ctime = current_time(old_inode);
	f2fs_old_inode->f2fs_mark_inode_dirty_sync(false);
	// 移除旧的连接
	old_dir->f2fs_delete_entry(old_entry, old_page, NULL);
	old_page = NULL;

	if (whiteout) 
	{
		set_inode_flag(whiteout, FI_INC_LINK);

		Cf2fsDirInode* parent_inode = dynamic_cast<Cf2fsDirInode*>(d_inode(old_dentry->d_parent));
		if (parent_inode == nullptr) THROW_ERROR(ERR_APP, L"parent is not a dir");

		err = parent_inode->f2fs_add_link(old_dentry, whiteout);
		if (err) goto put_out_dir;

		spin_lock(&whiteout->i_lock);
//		whiteout->i_state &= ~I_LINKABLE;
		whiteout->ClearState(I_LINKABLE);
		spin_unlock(&whiteout->i_lock);
		iput(whiteout);
	}

	if (old_dir_entry)
	{
		if (old_dir != new_dir && !whiteout)
		{
			Cf2fsDirInode* old_inode_dir = dynamic_cast<Cf2fsDirInode*>(f2fs_old_inode);
			old_inode_dir->f2fs_set_link(old_dir_entry, old_dir_page, new_dir);
		}
		else			f2fs_put_page(old_dir_page, 0);
		old_dir->f2fs_i_links_write(false);
		LOG_DEBUG(L"[inode_track] addr=%p, ino=%d, link=%d - dec_link for old dir", old_dir, old_dir->i_ino, old_dir->i_nlink);
	}
	if (F2FS_OPTION(this).fsync_mode == FSYNC_MODE_STRICT)
	{
		f2fs_add_ino_entry(this, new_dir->i_ino, TRANS_DIR_INO);
		if (S_ISDIR(old_inode->i_mode))
			f2fs_add_ino_entry(this, old_inode->i_ino,	TRANS_DIR_INO);
	}

	this->f2fs_unlock_op();

	if (IS_DIRSYNC(old_dir) || IS_DIRSYNC(new_dir))		this->sync_fs(1);

	this->f2fs_update_time( REQ_TIME);
	return 0;

put_out_dir:
	f2fs_unlock_op();
	f2fs_put_page(new_page, 0);
out_dir:
	if (old_dir_entry)	f2fs_put_page(old_dir_page, 0);
out_old:
	f2fs_put_page(old_page, 0);
out:
	if (whiteout) iput(whiteout);
	return err;
}

#if 0

static int f2fs_cross_rename(struct inode *old_dir, struct dentry *old_dentry, struct inode *new_dir, struct dentry *new_dentry)
{
	struct f2fs_sb_info *sbi = F2FS_I_SB(old_dir);
	struct inode *old_inode = d_inode(old_dentry);
	struct inode *new_inode = d_inode(new_dentry);
	struct page *old_dir_page, *new_dir_page;
	struct page *old_page, *new_page;
	struct f2fs_dir_entry *old_dir_entry = NULL, *new_dir_entry = NULL;
	struct f2fs_dir_entry *old_entry, *new_entry;
	int old_nlink = 0, new_nlink = 0;
	int err;

	if (unlikely(sbi->f2fs_cp_error()))
		return -EIO;
	if (!sbi->f2fs_is_checkpoint_ready())
		return -ENOSPC;

	if ((is_inode_flag_set(new_dir, FI_PROJ_INHERIT) &&
			!projid_eq(F2FS_I(new_dir)->i_projid,
			F2FS_I(old_dentry->d_inode)->i_projid)) ||
	    (is_inode_flag_set(new_dir, FI_PROJ_INHERIT) &&
			!projid_eq(F2FS_I(old_dir)->i_projid,
			F2FS_I(new_dentry->d_inode)->i_projid)))
		return -EXDEV;

	err = dquot_initialize(old_dir);
	if (err)
		goto out;

	err = dquot_initialize(new_dir);
	if (err)
		goto out;

	err = -ENOENT;
	old_entry = f2fs_find_entry(old_dir, &old_dentry->d_name, &old_page);
	if (!old_entry) {
		if (IS_ERR(old_page))
			err = PTR_ERR(old_page);
		goto out;
	}

	new_entry = f2fs_find_entry(new_dir, &new_dentry->d_name, &new_page);
	if (!new_entry) {
		if (IS_ERR(new_page))
			err = PTR_ERR(new_page);
		goto out_old;
	}

	/* prepare for updating ".." directory entry info later */
	if (old_dir != new_dir) {
		if (S_ISDIR(old_inode->i_mode)) {
			old_dir_entry = f2fs_parent_dir(old_inode,
							&old_dir_page);
			if (!old_dir_entry) {
				if (IS_ERR(old_dir_page))
					err = PTR_ERR(old_dir_page);
				goto out_new;
			}
		}

		if (S_ISDIR(new_inode->i_mode)) {
			new_dir_entry = f2fs_parent_dir(new_inode,
							&new_dir_page);
			if (!new_dir_entry) {
				if (IS_ERR(new_dir_page))
					err = PTR_ERR(new_dir_page);
				goto out_old_dir;
			}
		}
	}

	/*
	 * If cross rename between file and directory those are not
	 * in the same directory, we will inc nlink of file's parent
	 * later, so we should check upper boundary of its nlink.
	 */
	if ((!old_dir_entry || !new_dir_entry) &&
				old_dir_entry != new_dir_entry) {
		old_nlink = old_dir_entry ? -1 : 1;
		new_nlink = -old_nlink;
		err = -EMLINK;
		if ((old_nlink > 0 && old_dir->i_nlink >= F2FS_LINK_MAX) ||
			(new_nlink > 0 && new_dir->i_nlink >= F2FS_LINK_MAX))
			goto out_new_dir;
	}

	sbi->f2fs_balance_fs(true);

	sbi->f2fs_lock_op();
	//auto_lock<semaphore_read_lock> lock_op(sbi->cp_rwsem);

	/* update ".." directory entry info of old dentry */
	if (old_dir_entry)
		f2fs_set_link(old_inode, old_dir_entry, old_dir_page, new_dir);

	/* update ".." directory entry info of new dentry */
	if (new_dir_entry)
		f2fs_set_link(new_inode, new_dir_entry, new_dir_page, old_dir);

	/* update directory entry info of old dir inode */
	f2fs_set_link(old_dir, old_entry, old_page, new_inode);

	down_write(&F2FS_I(old_inode)->i_sem);
	if (!old_dir_entry)
		file_lost_pino(old_inode);
	else
		/* adjust dir's i_pino to pass fsck check */
		f2fs_i_pino_write(old_inode, new_dir->i_ino);
	up_write(&F2FS_I(old_inode)->i_sem);

	old_dir->i_ctime = current_time(old_dir);
	if (old_nlink) {
		down_write(&F2FS_I(old_dir)->i_sem);
		f2fs_i_links_write(old_dir, old_nlink > 0);
		up_write(&F2FS_I(old_dir)->i_sem);
	}
	f2fs_mark_inode_dirty_sync(old_dir, false);

	/* update directory entry info of new dir inode */
	f2fs_set_link(new_dir, new_entry, new_page, old_inode);

	down_write(&F2FS_I(new_inode)->i_sem);
	if (!new_dir_entry)
		file_lost_pino(new_inode);
	else
		/* adjust dir's i_pino to pass fsck check */
		f2fs_i_pino_write(new_inode, old_dir->i_ino);
	up_write(&F2FS_I(new_inode)->i_sem);

	new_dir->i_ctime = current_time(new_dir);
	if (new_nlink) {
		down_write(&F2FS_I(new_dir)->i_sem);
		f2fs_i_links_write(new_dir, new_nlink > 0);
		up_write(&F2FS_I(new_dir)->i_sem);
	}
	f2fs_mark_inode_dirty_sync(new_dir, false);

	if (F2FS_OPTION(sbi).fsync_mode == FSYNC_MODE_STRICT) {
		f2fs_add_ino_entry(sbi, old_dir->i_ino, TRANS_DIR_INO);
		f2fs_add_ino_entry(sbi, new_dir->i_ino, TRANS_DIR_INO);
	}

	sbi->f2fs_unlock_op();

	if (IS_DIRSYNC(old_dir) || IS_DIRSYNC(new_dir))
		sbi->sync_fs(1);

	sbi->f2fs_update_time( REQ_TIME);
	return 0;
out_new_dir:
	if (new_dir_entry) {
		f2fs_put_page(new_dir_page, 0);
	}
out_old_dir:
	if (old_dir_entry) {
		f2fs_put_page(old_dir_page, 0);
	}
out_new:
	f2fs_put_page(new_page, 0);
out_old:
	f2fs_put_page(old_page, 0);
out:
	return err;
}

static int f2fs_rename2(struct user_namespace *mnt_userns,
			struct inode *old_dir, struct dentry *old_dentry,
			struct inode *new_dir, struct dentry *new_dentry,
			unsigned int flags)
{
	int err;

	if (flags & ~(RENAME_NOREPLACE | RENAME_EXCHANGE | RENAME_WHITEOUT))
		return -EINVAL;

	err = fscrypt_prepare_rename(old_dir, old_dentry, new_dir, new_dentry, flags);
	if (err)	return err;

	if (flags & RENAME_EXCHANGE) 
	{
		return f2fs_cross_rename(old_dir, old_dentry, new_dir, new_dentry);
	}
	/* VFS has already handled the new dentry existence case, here, we just deal with "RENAME_NOREPLACE" as regular rename. */
	return f2fs_rename(old_dir, old_dentry, new_dir, new_dentry, flags);
}

static const char *f2fs_encrypted_get_link(struct dentry *dentry,
					   struct inode *inode,
					   struct delayed_call *done)
{
	struct page *page;
	const char *target;

	if (!dentry)
		return ERR_PTR(-ECHILD);

	page = read_mapping_page(inode->i_mapping, 0, NULL);
	if (IS_ERR(page))
		return ERR_CAST(page);

	target = fscrypt_get_symlink(inode, page_address(page),
				     inode->i_sb->s_blocksize, done);
	page->put_page();
	return target;
}

const struct inode_operations f2fs_encrypted_symlink_inode_operations = {
	.get_link	= f2fs_encrypted_get_link,
	.getattr	= f2fs_getattr,
	.setattr	= f2fs_setattr,
	.listxattr	= f2fs_listxattr,
};

const struct inode_operations f2fs_dir_inode_operations = {
	.create		= f2fs_create,
	.lookup		= f2fs_lookup,
	.link		= f2fs_link,
	.unlink		= f2fs_unlink,
	.symlink	= f2fs_symlink,
	.mkdir		= f2fs_mkdir,
	.rmdir		= f2fs_rmdir,
	.mknod		= f2fs_mknod,
	.rename		= f2fs_rename2,
	.tmpfile	= f2fs_tmpfile,
	.getattr	= f2fs_getattr,
	.setattr	= f2fs_setattr,
	.get_acl	= f2fs_get_acl,
	.set_acl	= f2fs_set_acl,
	.listxattr	= f2fs_listxattr,
	.fiemap		= f2fs_fiemap,
	.fileattr_get	= f2fs_fileattr_get,
	.fileattr_set	= f2fs_fileattr_set,
};

const struct inode_operations f2fs_symlink_inode_operations = {
	.get_link	= f2fs_get_link,
	.getattr	= f2fs_getattr,
	.setattr	= f2fs_setattr,
	.listxattr	= f2fs_listxattr,
};

const struct inode_operations f2fs_special_inode_operations = {
	.getattr	= f2fs_getattr,
	.setattr	= f2fs_setattr,
	.get_acl	= f2fs_get_acl,
	.set_acl	= f2fs_set_acl,
	.listxattr	= f2fs_listxattr,
};


#endif
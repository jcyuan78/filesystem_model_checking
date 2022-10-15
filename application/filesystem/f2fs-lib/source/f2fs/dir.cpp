///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
#include "../pch.h"

// SPDX-License-Identifier: GPL-2.0
/*
 * fs/f2fs/dir.c
 *
 * Copyright (c) 2012 Samsung Electronics Co., Ltd.
 *             http://www.samsung.com/
 */
//#include <asm/unaligned.h>
//#include <linux/fs.h>
//#include <linux/f2fs_fs.h>
//#include <linux/sched/signal.h>
//#include <linux/unicode.h>
#include "../../include/f2fs.h"
#include "node.h"
#include "acl.h"
#include "xattr.h"
//#include <trace/events/f2fs.h>
#include "../../include/f2fs-filesystem.h"
#include "../../include/f2fs-inode.h"

#include <boost/algorithm/string.hpp>

LOCAL_LOGGER_ENABLE(L"f2fs.dir", LOGGER_LEVEL_DEBUGINFO);



//static unsigned long dir_blocks(struct inode *inode)
//{
//	return ((unsigned long long) (i_size_read(inode) + PAGE_SIZE - 1))	>> PAGE_SHIFT;
//}


unsigned int Cf2fsDirInode::dir_buckets(unsigned int level, int dir_level)
{
	if (level + dir_level < MAX_DIR_HASH_DEPTH / 2)			return 1 << (level + dir_level);
	else		return MAX_DIR_BUCKETS;
}

unsigned int Cf2fsDirInode::bucket_blocks(unsigned int level)
{
	if (level < MAX_DIR_HASH_DEPTH / 2)		return 2;
	else		return 4;
}

static unsigned char f2fs_filetype_table[F2FS_FT_MAX];


static unsigned char f2fs_type_by_mode[S_IFMT >> S_SHIFT];

void init_dir_entry_data(void)
{
	f2fs_filetype_table[F2FS_FT_UNKNOWN] = DT_UNKNOWN;
	f2fs_filetype_table[F2FS_FT_REG_FILE] = DT_REG;
	f2fs_filetype_table[F2FS_FT_DIR] = DT_DIR;
	f2fs_filetype_table[F2FS_FT_CHRDEV] = DT_CHR;
	f2fs_filetype_table[F2FS_FT_BLKDEV] = DT_BLK;
	f2fs_filetype_table[F2FS_FT_FIFO] = DT_FIFO;
	f2fs_filetype_table[F2FS_FT_SOCK] = DT_SOCK;
	f2fs_filetype_table[F2FS_FT_SYMLINK] = DT_LNK;

	f2fs_type_by_mode[S_IFREG >> S_SHIFT] = F2FS_FT_REG_FILE;
	f2fs_type_by_mode[S_IFDIR >> S_SHIFT] = F2FS_FT_DIR;
	f2fs_type_by_mode[S_IFCHR >> S_SHIFT] = F2FS_FT_CHRDEV;
	f2fs_type_by_mode[S_IFBLK >> S_SHIFT] = F2FS_FT_BLKDEV;
	f2fs_type_by_mode[S_IFIFO >> S_SHIFT] = F2FS_FT_FIFO;
	f2fs_type_by_mode[S_IFSOCK >> S_SHIFT] = F2FS_FT_SOCK;
	f2fs_type_by_mode[S_IFLNK >> S_SHIFT] = F2FS_FT_SYMLINK;
}

static void set_de_type(f2fs_dir_entry *de, umode_t mode)
{
	de->file_type = f2fs_type_by_mode[(mode & S_IFMT) >> S_SHIFT];
}

unsigned char f2fs_get_de_type(f2fs_dir_entry *de)
{
	if (de->file_type < F2FS_FT_MAX) return f2fs_filetype_table[de->file_type];
	return DT_UNKNOWN;
}


/* If @dir is casefolded, initialize @fname->cf_name from @fname->usr_fname. */
int f2fs_inode_info::f2fs_init_casefolded_name(f2fs_filename *fname) const
{
#ifdef CONFIG_UNICODE
	super_block *sb = i_sb;
	f2fs_sb_info *sbi = F2FS_SB(sb);

	if (IS_CASEFOLDED(this))
	{
		fname->cf_name.name = f2fs_kmalloc<char>(sbi, F2FS_NAME_LEN/*, GFP_NOFS*/);
		if (!fname->cf_name.name) 		THROW_ERROR(ERR_MEM, L"failed on creating cf_name, size=%d", F2FS_NAME_LEN);
		//由于user_fname已经是UNICODE了，不用做转换
//		wcscpy_s(const_cast<wchar_t*>(fname->cf_name.name), F2FS_NAME_LEN, fname->usr_fname->name.c_str());
		strcpy_s(const_cast<char*>(fname->cf_name.name), F2FS_NAME_LEN, fname->usr_fname->name.c_str());
		fname->cf_name.len = fname->usr_fname->len();
		//fname->cf_name.len = utf8_casefold(sb->s_encoding, fname->usr_fname,  fname->cf_name.name,  F2FS_NAME_LEN);
		//if ((int)fname->cf_name.len <= 0) 
		//{
		//	f2fs_kvfree(fname->cf_name.name);
		//	fname->cf_name.name = NULL;
		//	if (sb_has_strict_encoding(sb))		return -EINVAL;
		//	/* fall back to treating name as opaque byte sequence */
		//}
	}
#endif
	return 0;
}

// 从crypt_name复制到fname
int f2fs_inode_info::__f2fs_setup_filename(const fscrypt_name* crypt_name, f2fs_filename* fname) const
{
	int err;
//	memset(fname, 0, sizeof(*fname));

	fname->usr_fname = crypt_name->usr_fname;
	fname->disk_name = crypt_name->disk_name;
#ifdef CONFIG_FS_ENCRYPTION
	fname->crypto_buf = crypt_name->crypto_buf;
#endif
	if (crypt_name->is_nokey_name) 
	{	/* hash was decoded from the no-key name */
		fname->hash = cpu_to_le32(crypt_name->hash);
	}
	else
	{
		err = f2fs_init_casefolded_name(fname);
		if (err)
		{
//			f2fs_free_filename(fname);
//			delete fname;
			return err;
		}
		f2fs_hash_filename(this, fname);
	}
	return 0;
}

/*
 * Prepare to search for @iname in @dir.  This is similar to
 * fscrypt_setup_filename(), but this also handles computing the casefolded name and the f2fs dirhash if needed, then
   packing all the information about this filename up into a 'struct f2fs_filename'. */
//int f2fs_setup_filename(struct inode *dir, const struct qstr *iname, int lookup, struct f2fs_filename *fname)
int f2fs_inode_info::f2fs_setup_filename(const qstr* iname, int lookup, f2fs_filename* fname)
{
	struct fscrypt_name crypt_name;
	int err;

	err = fscrypt_setup_filename(this, iname, lookup, &crypt_name);
	if (err)	return err;

	return __f2fs_setup_filename(&crypt_name, fname);
}

/* Prepare to look up @dentry in @dir.  This is similar to fscrypt_prepare_lookup(), but this also handles 
   computing the casefolded name and the f2fs dirhash if needed, then packing all the information about this
   filename up into a 'struct f2fs_filename'. */

//int f2fs_prepare_lookup(inode *dir, dentry *dentry, f2fs_filename *fname)
int Cf2fsDirInode::f2fs_prepare_lookup(dentry *dentry, f2fs_filename *fname)
{
	fscrypt_name crypt_name;
	int err;
	err = fscrypt_prepare_lookup(this, dentry, &crypt_name);		// 从dentry中的name复制到fname
	if (err) return err;
	return __f2fs_setup_filename(&crypt_name, fname);
}

//<YUAN> 通过析构函数实现
//void f2fs_free_filename(struct f2fs_filename *fname)
//{
//#ifdef CONFIG_FS_ENCRYPTION
//	kfree(fname->crypto_buf.name);
//	fname->crypto_buf.name = NULL;
//#endif
//#ifdef CONFIG_UNICODE
//	kfree(fname->cf_name.name);
//	fname->cf_name.name = NULL;
//#endif
//}


unsigned long Cf2fsDirInode::dir_block_index(unsigned int level, int dir_level, unsigned int idx)
{
	unsigned long i;
	unsigned long bidx = 0;

	for (i = 0; i < level; i++) bidx += dir_buckets(i, dir_level) * bucket_blocks(i);
	bidx += idx * bucket_blocks(level);
	return bidx;
}

f2fs_dir_entry *Cf2fsDirInode::find_in_block(page *dentry_page, const f2fs_filename *fname,int *max_slots)
{
	f2fs_dentry_block *dentry_blk;
	f2fs_dentry_ptr d;

	dentry_blk = page_address<f2fs_dentry_block>(dentry_page);

	make_dentry_ptr_block(&d, dentry_blk);
	return f2fs_find_target_dentry(&d, fname, max_slots);
}

#define WARN_ON_ONCE

#ifdef CONFIG_UNICODE
/*
 * Test whether a case-insensitive directory entry matches the filename being searched for.
 * Returns 1 for a match, 0 for no match, and -errno on an error.  */
static int f2fs_match_ci_name(const inode *dir, const qstr *name, const char *de_name, u32 de_name_len)
{
//	qstr entry((const char*)de_name, de_name_len);
	const struct super_block *sb = dir->i_sb;
	std::string entry;
	int res;

//	std::string str_fn;
//	jcvos::UnicodeToUtf8(str_fn, de_name);
//	size_t str_fn_len = str_fn.size();

	if (IS_ENCRYPTED(dir)) 
	{
		const struct unicode_map *um = sb->s_encoding;
		fscrypt_str decrypted_name(NULL, de_name_len);

		const fscrypt_str encrypted_name(de_name, de_name_len);
//		const fscrypt_str encrypted_name(str_fn.c_str(), str_fn_len);
		if (WARN_ON_ONCE(!fscrypt_has_encryption_key(dir)))	return -EINVAL;
//		decrypted_name.name = kmalloc(de_name_len, GFP_KERNEL);
		jcvos::auto_array<char> decrypted(de_name_len);
		decrypted_name.name = decrypted;
//		if (!decrypted_name.name)			return -ENOMEM;
		res = fscrypt_fname_disk_to_usr(dir, 0, 0, &encrypted_name, &decrypted_name);
		if (res < 0) return 0;
		entry = decrypted_name.name;
//		entry.name = (char*)decrypted_name.name;
//		entry._h.len = decrypted_name.len;
//		jcvos::Utf8ToUnicode(entry, std::string((char*)(decrypted_name.name), decrypted_name.len));
	}
	else
	{
		entry = de_name;
//		jcvos::Utf8ToUnicode(entry, std::string((const char*)de_name, str_fn_len));
	}

//	res = utf8_strncasecmp_folded(um, name, &entry);
	//std::wstring src_name;
	//jcvos::Utf8ToUnicode(src_name, std::string(name->name, name->_u._s.len));
	bool ci_eq = boost::iequals(entry, name->name);
	/* In strict mode, ignore invalid names.  In non-strict mode, fall back to treating them as opaque byte sequences. */
	if (!ci_eq && !sb_has_strict_encoding(sb)) {	res = (name->name == entry);	}
	else 
	{	/* utf8_strncasecmp_folded returns 0 on match */
		res = (ci_eq!=false);
	}
//out:
//	kfree(decrypted_name.name);
//	delete[] decrypted_name.name;
	return res;
}
#endif /* CONFIG_UNICODE */

// 比较文件名：fname是需要比较的文件名，de_name, de_name_len需要比较的dentry中的文件名
static inline int f2fs_match_name(const inode *dir, const f2fs_filename *fname, const char *de_name, u32 de_name_len)
{
	struct fscrypt_name f;

#ifdef CONFIG_UNICODE
	if (fname->cf_name.name)
	{
		qstr cf = FSTR_TO_QSTR(&fname->cf_name);
		return f2fs_match_ci_name(dir, &cf, de_name, de_name_len);
	}
#endif
	f.usr_fname = fname->usr_fname;
	f.disk_name = fname->disk_name;
#ifdef CONFIG_FS_ENCRYPTION
	f.crypto_buf = fname->crypto_buf;
#endif
	return fscrypt_match_name(&f, de_name, de_name_len);
}


f2fs_dir_entry *f2fs_find_target_dentry(const f2fs_dentry_ptr *d, const f2fs_filename *fname, int *max_slots)
{
	f2fs_dir_entry *de;
	unsigned long bit_pos = 0;
	int max_len = 0;
	int res = 0;

	if (max_slots) 	*max_slots = 0;
	while (bit_pos < d->max)
	{
		if (!test_bit_le(bit_pos, (UINT8*)d->bitmap))
		{
			bit_pos++;
			max_len++;
			continue;
		}

		de = &d->dentry[bit_pos];

		if (unlikely(!de->name_len)) 
		{
			bit_pos++;
			continue;
		}

		if (de->hash_code == fname->hash) 
		{
//			wchar_t str[NR_DENTRY_IN_BLOCK];		//这个用法有些怀疑
//			size_t len = jcvos::Utf8ToUnicode(str, NR_DENTRY_IN_BLOCK, (char*)d->filename[bit_pos], le16_to_cpu(de->name_len));
//			res = f2fs_match_name(d->inode, fname, str, len );
			res = f2fs_match_name(d->inode, fname, (char*)(d->filename[bit_pos]), le16_to_cpu(de->name_len));
			if (res < 0) return ERR_PTR<f2fs_dir_entry>(res);
			if (res)
			{
//				goto found;
				if (max_slots && max_len > *max_slots)	*max_slots = max_len;
				return de;
			} 
		}

		if (max_slots && max_len > *max_slots)
			*max_slots = max_len;
		max_len = 0;

		bit_pos += GET_DENTRY_SLOTS(le16_to_cpu(de->name_len));
	}

	de = NULL;
//found:
	if (max_slots && max_len > *max_slots)	*max_slots = max_len;
	return de;
//#undef ERR_PTR(x)
}

//static struct f2fs_dir_entry *find_in_level(struct inode *dir,
//					unsigned int level,
//					const struct f2fs_filename *fname,
//					struct page **res_page)
f2fs_dir_entry* Cf2fsDirInode::find_in_level(unsigned int level,	const f2fs_filename* fname,	page** res_page)
{
//#define ERR_CAST(x) reinterpret_cast<page*>(x)

	int s = GET_DENTRY_SLOTS(fname->disk_name.len);
	unsigned int nbucket, nblock;
	unsigned int bidx, end_block;
	struct page *dentry_page;
	f2fs_dir_entry *de = NULL;
	bool room = false;
	int max_slots;

	nbucket = dir_buckets(level, i_dir_level);
	nblock = bucket_blocks(level);

	bidx = dir_block_index(level, i_dir_level,  le32_to_cpu(fname->hash) % nbucket);
	end_block = bidx + nblock;

	for (; bidx < end_block; bidx++) 
	{
		/* no need to allocate new dentry pages to all the indices */
		dentry_page = f2fs_find_data_page(bidx);
		if (IS_ERR(dentry_page)) 
		{
			if (PTR_ERR(dentry_page) == -ENOENT) 
			{
				room = true;
				continue;
			} 
			else 
			{
				*res_page = dentry_page;
				break;
			}
		}
#ifdef _DEBUG
		jcvos::Utf8ToUnicode(dentry_page->m_type, "dentry");
		LOG_DEBUG(L"got page, page=%llX, addr=%llX, type=%s, index=%d",
			dentry_page, dentry_page->virtual_add, dentry_page->m_type.c_str(), dentry_page->index);
		std::wstring fn;
		jcvos::Utf8ToUnicode(fn, fname->usr_fname->name);
		dentry_page->m_description = L"dentry contains " + fn;
#endif

		de = find_in_block(dentry_page, fname, &max_slots);
		if (IS_ERR(de)) 
		{
			LOG_ERROR(L"[err] failed in find_in_block");
			*res_page = ERR_PTR<page>((INT64)de);
			de = NULL;
			break;
		} 
		else if (de) 
		{
			*res_page = dentry_page;
			break;
		}

		if (max_slots >= s)		room = true;
		f2fs_put_page(dentry_page, 0);
	}

	if (!de && room && chash != fname->hash) 
	{
		chash = fname->hash;
		clevel = level;
	}

	return de;
//#undef ERR_CAST
}

//struct f2fs_dir_entry *__f2fs_find_entry(inode *dir, const struct f2fs_filename *fname, page **res_page)
f2fs_dir_entry * Cf2fsDirInode::__f2fs_find_entry(const f2fs_filename *fname, page **res_page)
{
	unsigned long npages = dir_blocks();
	f2fs_dir_entry *de = NULL;
	unsigned int max_depth;
	unsigned int level;

	*res_page = NULL;

	if (f2fs_has_inline_dentry()) 
	{
		de = f2fs_find_in_inline_dir(fname, res_page);
		goto out;
	}

	if (npages == 0)		goto out;

	max_depth = i_current_depth;
	if (unlikely(max_depth > MAX_DIR_HASH_DEPTH)) 
	{
		LOG_WARNING(L"Corrupted max_depth of %lu: %u", i_ino, max_depth);
		max_depth = MAX_DIR_HASH_DEPTH;
		f2fs_i_depth_write(max_depth);
	}

	for (level = 0; level < max_depth; level++)
	{
		de = find_in_level(level, fname, res_page);
		if (de || IS_ERR(*res_page)) break;
	}
out:
	/* This is to increase the speed of f2fs_create */
#if 0 //TODO
	if (!de) task = current;
#endif
	return de;
}


/*
 * Find an entry in the specified directory with the wanted name.
 * It returns the page where the entry was found (as a parameter - res_page), and the entry itself. Page is returned
   mapped and unlocked. Entry is guaranteed to be valid. */
//struct f2fs_dir_entry *f2fs_find_entry(inode *dir, const qstr *child, page **res_page)
f2fs_dir_entry* Cf2fsDirInode::f2fs_find_entry(const qstr* child, page** res_page)
{
	f2fs_dir_entry *de = NULL;
	f2fs_filename fname;
	int err;

	err = f2fs_setup_filename(child, 1, &fname);
	if (err) 
	{
		if (err == -ENOENT) 		*res_page = NULL;
		else						*res_page = ERR_PTR<page>(err);
		return NULL;
	}
	de = __f2fs_find_entry(&fname, res_page);
//	f2fs_free_filename(&fname);	//析构函数
	return de;
}


//struct f2fs_dir_entry *f2fs_parent_dir(struct inode *dir, struct page **p)
f2fs_dir_entry* Cf2fsDirInode::f2fs_parent_dir(page** p)
{
	return f2fs_find_entry(&dotdot_name, p);
}
#if 0 //TODO

ino_t f2fs_inode_by_name(struct inode *dir, const struct qstr *qstr,
							struct page **page)
{
	ino_t res = 0;
	struct f2fs_dir_entry *de;

	de = f2fs_find_entry(dir, qstr, page);
	if (de) {
		res = le32_to_cpu(de->ino);
		f2fs_put_page(*page, 0);
	}

	return res;
}
#endif

// 在this（父目录的inode）中，将de的inode设置为参数指定的inode。其中page为raw de所在的data page
//void f2fs_set_link(struct inode *dir, struct f2fs_dir_entry *de, struct page *page, struct inode *inode)
void Cf2fsDirInode::f2fs_set_link(f2fs_dir_entry* de, page* ppage, inode* iinode)
{
	enum page_type type = f2fs_has_inline_dentry() ? NODE : DATA;

	lock_page(ppage);
	f2fs_wait_on_page_writeback(ppage, type, true, true);
	de->ino = cpu_to_le32(iinode->i_ino);
	set_de_type(de, iinode->i_mode);
	set_page_dirty(ppage);

	i_mtime = i_ctime = current_time(this);
	f2fs_mark_inode_dirty_sync(false);
	f2fs_put_page(ppage, 1);
}

static void init_dent_inode(inode *dir, f2fs_inode_info *node, const f2fs_filename *fname, page *ipage)
{
//	struct f2fs_inode *ri;
	if (!fname) /* tmpfile case? */		return;
	f2fs_wait_on_page_writeback(ipage, NODE, true, true);

	/* copy name info. to this inode page */
	f2fs_inode * ri = F2FS_INODE(ipage);
	ri->i_namelen = cpu_to_le32(fname->disk_name.len);
	memcpy(ri->i_name, fname->disk_name.name, fname->disk_name.len);
	if (IS_ENCRYPTED(dir))
	{
		file_set_enc_name(node);
		/* Roll-forward recovery doesn't have encryption keys available, so it can't compute the dirhash for 
		   encrypted+casefolded filenames.  Append it to i_name if possible.  Else, disable roll-forward recovery of
		   the dentry (i.e., make fsync'ing the file force a checkpoint) by setting LOST_PINO.	 */
		if (IS_CASEFOLDED(dir)) 
		{
			if (fname->disk_name.len + sizeof(f2fs_hash_t) <= F2FS_NAME_LEN)
			{
			//	put_unaligned(fname->hash, (f2fs_hash_t*)&ri->i_name[fname->disk_name.len]);
				ri->i_name[fname->disk_name.len] = fname->hash;
			}
			else		file_lost_pino(node);
		}
	}
	set_page_dirty(ipage);
}

//void f2fs_do_make_empty_dir(struct inode *inode, struct inode *parent, struct f2fs_dentry_ptr *d)
void Cf2fsDirInode::f2fs_do_make_empty_dir(inode* parent, f2fs_dentry_ptr* d)
{
	//struct fscrypt_str dotdot = FSTR_INIT("..", 2);
	fscrypt_str dot(".", 1);
	fscrypt_str dotdot("..", 2);
	/* update dirent of "." */
	f2fs_update_dentry(i_ino, i_mode, d, &dot, 0, 0);
	/* update dirent of ".." */
	f2fs_update_dentry(parent->i_ino, parent->i_mode, d, &dotdot, 0, 1);
}

//static int make_empty_dir(struct inode *inode, struct inode *parent, struct page *page)
int Cf2fsDirInode::make_empty_dir(inode* parent, struct page* page)
{
	struct page *dentry_page;
	struct f2fs_dentry_block *dentry_blk;
	struct f2fs_dentry_ptr d;

	if (f2fs_has_inline_dentry()) return f2fs_make_empty_inline_dir(parent, page);

	dentry_page = f2fs_get_new_data_page(page, 0, true);
	if (IS_ERR(dentry_page)) return PTR_ERR(dentry_page);

	dentry_blk = page_address<f2fs_dentry_block>(dentry_page);

	make_dentry_ptr_block(&d, dentry_blk);
	f2fs_do_make_empty_dir(parent, &d);

	set_page_dirty(dentry_page);
	f2fs_put_page(dentry_page, 1);
	return 0;
}


//struct page *f2fs_init_inode_metadata(struct inode *node, struct inode *dir,	const struct f2fs_filename *fname, struct page *dpage)
// 初始化inode的metadata
// @dir: parent dir
page* f2fs_inode_info::f2fs_init_inode_metadata(inode* dir, const f2fs_filename* fname, page* dpage)
{
	page *ppage;
	int err;

	if (is_inode_flag_set(FI_NEW_INODE)) 
	{
		ppage = f2fs_new_inode_page();
		if (IS_ERR(ppage))		return ppage;
		if (S_ISDIR(i_mode)) 
		{
			/* in order to handle error case */
			ppage->get_page();
			Cf2fsDirInode* di = dynamic_cast<Cf2fsDirInode*>(this);
			if (!di) THROW_ERROR(ERR_APP, L"inode type does not match. mode=DIR");
			err = di->make_empty_dir(dir, ppage);
			if (err)
			{
				lock_page(ppage);
				goto put_error;
			}
			ppage->put_page();
		}

		err = f2fs_init_acl(this, dir, ppage, dpage);
		if (err)		goto put_error;

		err = f2fs_init_security(this, dir, fname ? fname->usr_fname : NULL, ppage);
		if (err)		goto put_error;

		if (IS_ENCRYPTED(this)) 
		{
			err = fscrypt_set_context(this, ppage);
			if (err) goto put_error;
		}
	} 
	else
	{
		ppage = m_sbi->f2fs_get_node_page(i_ino);
		if (IS_ERR(ppage))		return ppage;
	}
	init_dent_inode(dir, this, fname, ppage);
	/* This file should be checkpointed during fsync. We lost i_pino from now on.	 */
	if (is_inode_flag_set(FI_INC_LINK)) 
	{
		if (!S_ISDIR(i_mode)) file_lost_pino(this);
		/* If link the tmpfile to alias through linkat path, we should remove this inode from orphan list. */
		if (i_nlink == 0)	f2fs_remove_orphan_inode(F2FS_I_SB(dir), i_ino);
		f2fs_i_links_write(true);
		LOG_DEBUG(L"[inode_track] addr=%p, ino=%d, link=%d - inc_link", this, i_ino, i_nlink);
	}
	return ppage;

put_error:
	clear_nlink();
	f2fs_update_inode(ppage);
	f2fs_put_page(ppage, 1);
	return ERR_PTR<page>(err);
}

//void f2fs_update_parent_metadata(Cf2fsDirInode *dir, f2fs_inode_info *inode, unsigned int current_depth)
void Cf2fsDirInode::f2fs_update_parent_metadata(f2fs_inode_info *inode, unsigned int current_depth)
{
	if (inode && inode->is_inode_flag_set(FI_NEW_INODE)) 
	{
		if (S_ISDIR(inode->i_mode))
		{
			f2fs_i_links_write(true);
			LOG_DEBUG(L"[inode_track] addr=%p, ino=%d, link=%d - inc_link", this, i_ino, i_nlink);
		}
		clear_inode_flag(inode, FI_NEW_INODE);
	}
	i_mtime = i_ctime = current_time(this);
	f2fs_mark_inode_dirty_sync(false);

	if (i_current_depth != current_depth)	f2fs_i_depth_write(current_depth);

	if (inode && inode->is_inode_flag_set(FI_INC_LINK))		clear_inode_flag(inode, FI_INC_LINK);
}

int f2fs_room_for_filename(const void *bitmap, int slots, int max_slots)
{
	int bit_start = 0;
	int zero_start, zero_end;
	const UINT8* bm = reinterpret_cast<const UINT8*>(bitmap);
next:
	zero_start = find_next_zero_bit_le(bm, max_slots, bit_start);
	if (zero_start >= max_slots)	return max_slots;

	zero_end = find_next_bit_le(bm, max_slots, zero_start);
	if (zero_end - zero_start >= slots)
		return zero_start;

	bit_start = zero_end + 1;

	if (zero_end + 1 >= max_slots)
		return max_slots;
	goto next;
}

//bool f2fs_has_enough_room(struct inode *dir, struct page *ipage, const struct f2fs_filename *fname)
bool Cf2fsDirInode::f2fs_has_enough_room(page* ipage, const f2fs_filename* fname)
{
	f2fs_dentry_ptr d;
	unsigned int bit_pos;
	int slots = GET_DENTRY_SLOTS(fname->disk_name.len);
	make_dentry_ptr_inline(&d, inline_data_addr(ipage));
	bit_pos = f2fs_room_for_filename(d.bitmap, slots, d.max);
	return bit_pos < d.max;
}

void f2fs_update_dentry(nid_t ino, umode_t mode, f2fs_dentry_ptr *d, const fscrypt_str *name, f2fs_hash_t name_hash,
			unsigned int bit_pos)
{
//	std::string str_fn(name->name);
//	jcvos::UnicodeToUtf8(str_fn, name->name);

	f2fs_dir_entry * de = &d->dentry[bit_pos];
	de->hash_code = name_hash;

	de->name_len = cpu_to_le16(name->len);
//	UINT16 name_len = boost::numeric_cast<UINT16>(str_fn.size());
//	de->name_len = cpu_to_le16(name_len);
	memcpy(d->filename[bit_pos], name->name, name->len);
//	memcpy(d->filename[bit_pos], str_fn.c_str(), name_len);
	de->ino = cpu_to_le32(ino);
	set_de_type(de, mode);

	int slots = GET_DENTRY_SLOTS(name->len);
//	int slots = GET_DENTRY_SLOTS(name_len);
	for (int i = 0; i < slots; i++)
	{
//		__set_bit_le(bit_pos + i, (UINT8 *)d->bitmap);
		__set_bit(bit_pos + i, d->bitmap);
		/* avoid wrong garbage data for readdir */
		if (i)		(de + i)->name_len = 0;
	}
}

// 当前目录(this)为父目录，iinode为子目录的inode
//int f2fs_add_regular_entry(struct inode *dir, const struct f2fs_filename *fname, struct inode *inode, nid_t ino, umode_t mode)
int Cf2fsDirInode::f2fs_add_regular_entry(const f2fs_filename* fname, f2fs_inode_info* iinode, nid_t ino, umode_t mode)
{
	unsigned int bit_pos;
	unsigned int level;
	unsigned int current_depth;
	unsigned long bidx, block;
	unsigned int nbucket, nblock;
	struct page *dentry_page = NULL;
	f2fs_dentry_block *dentry_blk = NULL;
	struct f2fs_dentry_ptr d;
	struct page *page = NULL;
	int slots, err = 0;

//	f2fs_inode_info* fi = dynamic_cast<f2fs_inode_info*>(iinode);
	//Cf2fsDirInode* di = dynamic_cast<Cf2fsDirInode*>(iinode);
	//JCASSERT(di);

	level = 0;
	slots = GET_DENTRY_SLOTS(fname->disk_name.len);

	current_depth = i_current_depth;
	if (chash == fname->hash) 
	{
		level = clevel;
		chash = 0;
	}

start:
	if (time_to_inject(m_sbi, FAULT_DIR_DEPTH)) 
	{
		f2fs_show_injection_info(m_sbi, FAULT_DIR_DEPTH);
		return -ENOSPC;
	}

	if (unlikely(current_depth == MAX_DIR_HASH_DEPTH))		return -ENOSPC;

	/* Increase the depth, if required */
	if (level == current_depth)		++current_depth;

	nbucket = dir_buckets(level, i_dir_level);
	nblock = bucket_blocks(level);

	bidx = dir_block_index(level, i_dir_level, (le32_to_cpu(fname->hash) % nbucket));

	for (block = bidx; block <= (bidx + nblock - 1); block++) 
	{
		dentry_page = f2fs_get_new_data_page(NULL, block, true);
		if (IS_ERR(dentry_page))	return PTR_ERR(dentry_page);
#ifdef _DEBUG
		jcvos::Utf8ToUnicode(dentry_page->m_type , "dentry");
		LOG_DEBUG(L"new page, page=%llX, addr=%llX, type=%s, index=%d",
			dentry_page, dentry_page->virtual_add, dentry_page->m_type.c_str(), dentry_page->index);
#endif
		dentry_blk = page_address<f2fs_dentry_block>(dentry_page);
		bit_pos = f2fs_room_for_filename(&dentry_blk->dentry_bitmap, slots, NR_DENTRY_IN_BLOCK);
		if (bit_pos < NR_DENTRY_IN_BLOCK)	goto add_dentry;

		f2fs_put_page(dentry_page, 1);
	}

	/* Move to next level to find the empty slot for new dentry */
	++level;
	goto start;
add_dentry:
	f2fs_wait_on_page_writeback(dentry_page, DATA, true, true);

	if (iinode)
	{
		down_write(&iinode->i_sem);
		page = iinode->f2fs_init_inode_metadata(this, fname, NULL);
		if (IS_ERR(page)) 
		{
			err = PTR_ERR(page);
			goto fail;
		}
	}

	make_dentry_ptr_block(&d, dentry_blk);
	f2fs_update_dentry(ino, mode, &d, &fname->disk_name, fname->hash, bit_pos);

	set_page_dirty(dentry_page);

	if (iinode) 
	{
		iinode->f2fs_i_pino_write(i_ino);
		/* synchronize inode page's data from inode cache */
		if (iinode->is_inode_flag_set(FI_NEW_INODE)) iinode->f2fs_update_inode(page);
		f2fs_put_page(page, 1);
	}

	//更新iinode的父节点(this)的meta
	f2fs_update_parent_metadata(iinode, current_depth);
fail:
	if (iinode)	up_write(&iinode->i_sem);

	f2fs_put_page(dentry_page, 1);

	return err;
}

//int f2fs_add_dentry(struct inode *dir, const struct f2fs_filename *fname, struct inode *inode, nid_t ino, umode_t mode)
int Cf2fsDirInode::f2fs_add_dentry(const f2fs_filename* fname, f2fs_inode_info* iinode, nid_t ino, umode_t mode)
{
	int err = -EAGAIN;
	if (f2fs_has_inline_dentry())		
		err = f2fs_add_inline_entry(fname, iinode, ino, mode);
	if (err == -EAGAIN)		
		err = f2fs_add_regular_entry(fname, iinode, ino, mode);
	m_sbi->f2fs_update_time(REQ_TIME);
	return err;
}

/*
 * Caller should grab and release a rwsem by calling f2fs_lock_op() and f2fs_unlock_op().  */
// 将inode以name作为文件名添加到当前inode(this)中
//int f2fs_do_add_link(struct inode *dir, const struct qstr *name, struct inode *inode, nid_t ino, umode_t mode)
int Cf2fsDirInode::f2fs_do_add_link(const qstr *name, f2fs_inode_info *inode, nid_t ino, umode_t mode)
{
	f2fs_filename fname;
	struct page *page = NULL;
	struct f2fs_dir_entry *de = NULL;
	int err;

	err = f2fs_setup_filename(name, 0, &fname);
	if (err) return err;

	/*An immature stackable filesystem shows a race condition between lookup and create. If we have same task when doing lookup and create, it's definitely fine as expected by VFS normally. Otherwise, let's just verify on-disk dentry one more time, which guarantees filesystem consistency more.	 */
#if 1 //TODO 处理current和task
//	if (current != task) 
	if (true)
	{
		de = __f2fs_find_entry(&fname, &page);
		task = NULL;
	}
#endif
	//	这里有个问题，当create调用此函数时，应该没有找到相应的文件名，de返回false，调用f2fs_add_dentry()。
	//	但是当没有找到是，de返回NULL，page也被设为0，则直接返回error.
	if (de) 
	{
		f2fs_put_page(page, 0);
		err = -EEXIST;
		LOG_ERROR(L"[err] name %s has already existed", name->name.c_str());
	} 
//	else if (IS_ERR(page)) 
//	{
//		err = PTR_ERR(page); 
////		THROW_ERROR(ERR_APP, L"failed on finding entry of %s, get page err, code =%d", fname.cf_name.name, err);
//		LOG_ERROR(L"[err] failed on finding entry of %s, get page err, code =%d", fname.disk_name.name, err);
//	} 
	else { err = f2fs_add_dentry(&fname, inode, ino, mode);	}
//	f2fs_free_filename(&fname);	//析构函数
	return err;
}

#if 0

int f2fs_do_tmpfile(struct inode *inode, struct inode *dir)
{
	struct page *page;
	int err = 0;

	down_write(&F2FS_I(inode)->i_sem);
	page = f2fs_init_inode_metadata(inode, dir, NULL, NULL);
	if (IS_ERR(page)) {
		err = PTR_ERR(page);
		goto fail;
	}
	f2fs_put_page(page, 1);

	clear_inode_flag(inode, FI_NEW_INODE);
	f2fs_update_time(F2FS_I_SB(inode), REQ_TIME);
fail:
	up_write(&F2FS_I(inode)->i_sem);
	return err;
}

#endif //<TOOD>

void f2fs_drop_nlink(f2fs_inode_info *dir, f2fs_inode_info *iinode)
{
	f2fs_sb_info *sbi = F2FS_I_SB(dir);
	down_write(&iinode->i_sem);

	if (S_ISDIR(iinode->i_mode))
	{
		dir->f2fs_i_links_write(false);
		LOG_DEBUG(L"[inode_track] addr=%p, ino=%d, link=%d - dec_link", dir, dir->i_ino, dir->i_nlink);
	}
	iinode->i_ctime = current_time(iinode);

	iinode->f2fs_i_links_write(false);
	LOG_DEBUG(L"[inode_track] addr=%p, ino=%d, link=%d - dec_link", dir, dir->i_ino, dir->i_nlink);
	if (S_ISDIR(iinode->i_mode)) 
	{
		iinode->f2fs_i_links_write(false);
		LOG_DEBUG(L"[inode_track] addr=%p, ino=%d, link=%d - dec_link", dir, dir->i_ino, dir->i_nlink);
		iinode->f2fs_i_size_write(0);
	}
	up_write(&iinode->i_sem);

	if (iinode->i_nlink == 0)		sbi->f2fs_add_orphan_inode(iinode);
	else		sbi->f2fs_release_orphan_inode();
}

/* It only removes the dentry from the dentry page, corresponding name entry in name page does not need to be touched during deletion. */
// dirw为父目录，iinode是要删除的节点
// 从当前目录中删除dentry，page为dentry所在的page。iinode是dentry对应的inode，如果需要同时删除inode的话，则设置，否则为null
//void f2fs_delete_entry(f2fs_dir_entry *dentry, page *ppage, f2fs_inode_info *dir, f2fs_inode_info *iinode)
void Cf2fsDirInode::f2fs_delete_entry(f2fs_dir_entry* dentry, page* ppage, f2fs_inode_info* iinode)
{
	f2fs_dentry_block *dentry_blk;
	unsigned int bit_pos;
	int slots = GET_DENTRY_SLOTS(le16_to_cpu(dentry->name_len));
	int i;

	m_sbi->f2fs_update_time(REQ_TIME);

	if (F2FS_OPTION(m_sbi).fsync_mode == FSYNC_MODE_STRICT)
		f2fs_add_ino_entry(m_sbi, this->i_ino, TRANS_DIR_INO);

	if (f2fs_has_inline_dentry())
		return f2fs_delete_inline_entry(dentry, ppage, iinode);

	lock_page(ppage);
	f2fs_wait_on_page_writeback(ppage, DATA, true, true);

	dentry_blk = page_address<f2fs_dentry_block>(ppage);
	bit_pos = dentry - dentry_blk->dentry;
	for (i = 0; i < slots; i++)
		__clear_bit_le(bit_pos + i, &dentry_blk->dentry_bitmap);

	/* Let's check and deallocate this dentry page */
	bit_pos = find_next_bit_le(dentry_blk->dentry_bitmap, NR_DENTRY_IN_BLOCK, 0);
	set_page_dirty(ppage);

	if (bit_pos == NR_DENTRY_IN_BLOCK && !this->f2fs_truncate_hole(ppage->index, ppage->index + 1))
	{
		f2fs_clear_page_cache_dirty_tag(ppage);
		clear_page_dirty_for_io(ppage);
		f2fs_clear_page_private(ppage);
		ClearPageUptodate(ppage);
		clear_cold_data(ppage);
		inode_dec_dirty_pages(this);
		f2fs_remove_dirty_inode(this);
	}
	f2fs_put_page(ppage, 1);

	this->i_ctime = this->i_mtime = current_time(this);
	this->f2fs_mark_inode_dirty_sync(false);

	if (iinode)	f2fs_drop_nlink(this, iinode);
}


//bool f2fs_empty_dir(struct inode *dir)
bool Cf2fsDirInode::f2fs_empty_dir(void) const

{
//	unsigned long bidx;
//	struct page *dentry_page;
//	unsigned int bit_pos;
//	struct f2fs_dentry_block *dentry_blk;

	if (f2fs_has_inline_dentry()) return f2fs_empty_inline_dir();

	unsigned long nblock = dir_blocks();
	for (UINT bidx = 0; bidx < nblock; bidx++) 
	{
		page * dentry_page = const_cast<Cf2fsDirInode*>(this)->f2fs_get_lock_data_page(bidx, false);
		if (IS_ERR(dentry_page))
		{
			if (PTR_ERR(dentry_page) == -ENOENT)	continue;
			else									return false;
		}

		UINT bit_pos;
		f2fs_dentry_block * dentry_blk = page_address<f2fs_dentry_block>(dentry_page);
		if (bidx == 0)	bit_pos = 2;
		else			bit_pos = 0;
		bit_pos = find_next_bit_le(dentry_blk->dentry_bitmap,	NR_DENTRY_IN_BLOCK,	bit_pos);
		f2fs_put_page(dentry_page, 1);
		if (bit_pos < NR_DENTRY_IN_BLOCK)		return false;
	}
	return true;
}

#if 0 //<TODO>

int f2fs_fill_dentries(struct dir_context *ctx, struct f2fs_dentry_ptr *d,
			unsigned int start_pos, struct fscrypt_str *fstr)
{
	unsigned char d_type = DT_UNKNOWN;
	unsigned int bit_pos;
	struct f2fs_dir_entry *de = NULL;
	struct fscrypt_str de_name = FSTR_INIT(NULL, 0);
	struct f2fs_sb_info *sbi = F2FS_I_SB(d->inode);
	struct blk_plug plug;
	bool readdir_ra = sbi->readdir_ra == 1;
	int err = 0;

	bit_pos = ((unsigned long)ctx->pos % d->max);

	if (readdir_ra)
		blk_start_plug(&plug);

	while (bit_pos < d->max) {
		bit_pos = find_next_bit_le(d->bitmap, d->max, bit_pos);
		if (bit_pos >= d->max)
			break;

		de = &d->dentry[bit_pos];
		if (de->name_len == 0) {
			bit_pos++;
			ctx->pos = start_pos + bit_pos;
			printk_ratelimited(
				"%sF2FS-fs (%s): invalid namelen(0), ino:%u, run fsck to fix.",
				KERN_WARNING, sbi->s_id,
				le32_to_cpu(de->ino));
			sbi->set_sbi_flag(SBI_NEED_FSCK);
			continue;
		}

		d_type = f2fs_get_de_type(de);

		de_name.name = d->filename[bit_pos];
		de_name.len = le16_to_cpu(de->name_len);

		/* check memory boundary before moving forward */
		bit_pos += GET_DENTRY_SLOTS(le16_to_cpu(de->name_len));
		if (unlikely(bit_pos > d->max ||
				le16_to_cpu(de->name_len) > F2FS_NAME_LEN)) {
			f2fs_warn(sbi, "%s: corrupted namelen=%d, run fsck to fix.",
				  __func__, le16_to_cpu(de->name_len));
			sbi->set_sbi_flag(SBI_NEED_FSCK);
			err = -EFSCORRUPTED;
			goto out;
		}

		if (IS_ENCRYPTED(d->inode)) {
			int save_len = fstr->len;

			err = fscrypt_fname_disk_to_usr(d->inode,
						(u32)le32_to_cpu(de->hash_code),
						0, &de_name, fstr);
			if (err)
				goto out;

			de_name = *fstr;
			fstr->len = save_len;
		}

		if (!dir_emit(ctx, de_name.name, de_name.len,
					le32_to_cpu(de->ino), d_type)) {
			err = 1;
			goto out;
		}

		if (readdir_ra)
			f2fs_ra_node_page(sbi, le32_to_cpu(de->ino));

		ctx->pos = start_pos + bit_pos;
	}
out:
	if (readdir_ra)
		blk_finish_plug(&plug);
	return err;
}

static int f2fs_readdir(struct file *file, struct dir_context *ctx)
{
	struct inode *inode = file_inode(file);
	unsigned long npages = dir_blocks(inode);
	struct f2fs_dentry_block *dentry_blk = NULL;
	struct page *dentry_page = NULL;
	struct file_ra_state *ra = &file->f_ra;
	loff_t start_pos = ctx->pos;
	unsigned int n = ((unsigned long)ctx->pos / NR_DENTRY_IN_BLOCK);
	struct f2fs_dentry_ptr d;
	struct fscrypt_str fstr = FSTR_INIT(NULL, 0);
	int err = 0;

	if (IS_ENCRYPTED(inode)) {
		err = fscrypt_prepare_readdir(inode);
		if (err)
			goto out;

		err = fscrypt_fname_alloc_buffer(F2FS_NAME_LEN, &fstr);
		if (err < 0)
			goto out;
	}

	if (inode->f2fs_has_inline_dentry()) {
		err = f2fs_read_inline_dir(file, ctx, &fstr);
		goto out_free;
	}

	for (; n < npages; n++, ctx->pos = n * NR_DENTRY_IN_BLOCK) {

		/* allow readdir() to be interrupted */
		if (fatal_signal_pending(current)) {
			err = -ERESTARTSYS;
			goto out_free;
		}
		cond_resched();

		/* readahead for multi pages of dir */
		if (npages - n > 1 && !ra_has_index(ra, n))
			page_cache_sync_readahead(inode->i_mapping, ra, file, n,
				min(npages - n, (pgoff_t)MAX_DIR_RA_PAGES));

		dentry_page = f2fs_find_data_page(inode, n);
		if (IS_ERR(dentry_page)) {
			err = PTR_ERR(dentry_page);
			if (err == -ENOENT) {
				err = 0;
				continue;
			} else {
				goto out_free;
			}
		}

		dentry_blk = page_address(dentry_page);

		make_dentry_ptr_block(inode, &d, dentry_blk);

		err = f2fs_fill_dentries(ctx, &d,
				n * NR_DENTRY_IN_BLOCK, &fstr);
		if (err) {
			f2fs_put_page(dentry_page, 0);
			break;
		}

		f2fs_put_page(dentry_page, 0);
	}
out_free:
	fscrypt_fname_free_buffer(&fstr);
out:
	trace_f2fs_readdir(inode, start_pos, ctx->pos, err);
	return err < 0 ? err : 0;
}

const struct file_operations f2fs_dir_operations = {
	.llseek		= generic_file_llseek,
	.read		= generic_read_dir,
	.iterate_shared	= f2fs_readdir,
	.fsync		= f2fs_sync_file,
	.unlocked_ioctl	= f2fs_ioctl,
#ifdef CONFIG_COMPAT
	.compat_ioctl   = f2fs_compat_ioctl,
#endif
};

#endif

// for Debug
#ifdef _DEBUG
void Cf2fsDirInode::DebugListItems(void)
{
	UINT max_depth = i_current_depth;
	for (UINT level = 0; level < max_depth; level++)
	{// travel in level
		UINT nbucket = dir_buckets(level, i_dir_level);
		UINT nblock = bucket_blocks(level);
		LOG_DEBUG(L"travel in level %d, bucket=%d, block=%d", level, nbucket, nblock);
		for (UINT bidx = 0; bidx < nblock; bidx++)
		{
			page* dentry_page = f2fs_find_data_page(bidx);
			if (!dentry_page)
			{
				LOG_DEBUG(L"dentry page bidx=%d is empty", bidx);
				continue;
			}
			// travel in block
			f2fs_dentry_block* dentry_blk = page_address<f2fs_dentry_block>(dentry_page);
			f2fs_dentry_ptr d;
			make_dentry_ptr_block(&d, dentry_blk);
//			UINT bit_pos = 0;
			int max_len = 0;

			for (UINT bit_pos =0; bit_pos < d.max; bit_pos++)
			{
				if (!test_bit_le(bit_pos, (UINT8*)d.bitmap))	{	max_len++;	continue;}
				f2fs_dir_entry& de = d.dentry[bit_pos];
				if (!de.name_len) continue;
				// output item info
				wchar_t fn[256];
				size_t len = jcvos::Utf8ToUnicode(fn, 256, (char*)d.filename[bit_pos], le16_to_cpu(de.name_len));
				fn[len] = 0;
				LOG_DEBUG(L"item: ino=%d, type=%02X, name=%s", de.ino, de.file_type, fn);
				max_len = 0;
				//bit_pos += GET_DENTRY_SLOTS(le16_to_cpu(de.name_len));
			}
		}
	}
}

#endif
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
#include "../pch.h"
// SPDX-License-Identifier: GPL-2.0
/*
 * fs/f2fs/recovery.c
 *
 * Copyright (c) 2012 Samsung Electronics Co., Ltd.
 *             http://www.samsung.com/
 */
//#include <asm/unaligned.h>
//#include <linux/fs.h>
//#include <linux/f2fs_fs.h>
//#include "f2fs.h"
#include "../../include/f2fs_fs.h"
#include "../../include/f2fs-filesystem.h"
#include "node.h"
#include "segment.h"
LOCAL_LOGGER_ENABLE(L"f2fs.recovery", LOGGER_LEVEL_DEBUGINFO);

/*
 * Roll forward recovery scenarios.
 *
 * [Term] F: fsync_mark, D: dentry_mark
 *
 * 1. inode(x) | CP | inode(x) | dnode(F)
 * -> Update the latest inode(x).
 *
 * 2. inode(x) | CP | inode(F) | dnode(F)
 * -> No problem.
 *
 * 3. inode(x) | CP | dnode(F) | inode(x)
 * -> Recover to the latest dnode(F), and drop the last inode(x)
 *
 * 4. inode(x) | CP | dnode(F) | inode(F)
 * -> No problem.
 *
 * 5. CP | inode(x) | dnode(F)
 * -> The inode(DF) was missing. Should drop this dnode(F).
 *
 * 6. CP | inode(DF) | dnode(F)
 * -> No problem.
 *
 * 7. CP | dnode(F) | inode(DF)
 * -> If f2fs_iget fails, then goto next to find inode(DF).
 *
 * 8. CP | dnode(F) | inode(x)
 * -> If f2fs_iget fails, then goto next to find inode(DF).
 *    But it will fail due to no inode(DF).
 */

//static struct kmem_cache *fsync_entry_slab;

bool f2fs_space_for_roll_forward(f2fs_sb_info *sbi)
{
	s64 nalloc = percpu_counter_sum_positive(&sbi->alloc_valid_block_count);

	if (sbi->last_valid_block_count + nalloc > sbi->user_block_count)
		return false;
	return true;
}

static struct fsync_inode_entry *get_fsync_inode(list_head *head, nid_t ino)
{
	fsync_inode_entry *entry;
	list_for_each_entry(fsync_inode_entry, entry, head, list)
	{
		if (entry->inode->i_ino == ino)	return entry;
	}
	return NULL;
}

static fsync_inode_entry *add_fsync_inode(f2fs_sb_info *sbi, list_head *head, nid_t ino, bool quota_inode)
{
	f2fs_inode_info *iinode;
	fsync_inode_entry *entry;
	int err;

	iinode = sbi->f2fs_iget_retry(ino);
	if (IS_ERR(iinode)) return ERR_PTR<fsync_inode_entry>((UINT64)iinode);

	err = dquot_initialize(iinode);
	if (err)		goto err_out;

	if (quota_inode) 
	{
		JCASSERT(0);
		err = dquot_alloc_inode(iinode);
		if (err)	goto err_out;
	}

	entry = f2fs_kmem_cache_alloc<fsync_inode_entry>(sbi->m_fs->fsync_entry_slab, GFP_F2FS_ZERO);
	entry->inode = iinode;
	list_add_tail(&entry->list, head);

	return entry;
err_out:
	iput(iinode);
	return ERR_PTR<fsync_inode_entry>(err);
}

static void del_fsync_inode(struct fsync_inode_entry *entry, int drop)
{
	f2fs_sb_info* sbi = F2FS_I_SB(entry->inode);
	CF2fsFileSystem* fs = sbi->m_fs;

	if (drop)
	{
		/* inode should not be recovered, drop it */
		entry->inode->f2fs_inode_synced();
	}
	iput(entry->inode);
	list_del(&entry->list);
	kmem_cache_free(fs->fsync_entry_slab, entry);
}

static int init_recovered_filename(const inode *dir, f2fs_inode *raw_inode, f2fs_filename *fname, qstr *usr_fname)
{
	int err;

//	memset(fname, 0, sizeof(*fname));
//	fname->disk_name.len = le32_to_cpu(raw_inode->i_namelen);
//	fname->disk_name.name = raw_inode->i_name;

//	if ((fname->disk_name.len > F2FS_NAME_LEN))
	if (raw_inode->i_namelen > F2FS_NAME_LEN)
	{
		JCASSERT(0);
		return -ENAMETOOLONG;
	}

	if (!IS_ENCRYPTED(dir))
	{
//		jcvos::Utf8ToUnicode(usr_fname->name, (const char*)(raw_inode->i_name));
		usr_fname->name = (const char*)raw_inode->i_name;
		fname->disk_name.name = usr_fname->name.c_str();
//		usr_fname->name = (char*) fname->disk_name.name;
//		usr_fname->_u._s.len = fname->disk_name.len;
		fname->usr_fname = usr_fname;
	}

	/* Compute the hash of the filename */
	if (IS_ENCRYPTED(dir) && IS_CASEFOLDED(dir)) 
	{
		/* In this case the hash isn't computable without the key, so it was saved on-disk. */
//		if (fname->disk_name.len + sizeof(f2fs_hash_t) > F2FS_NAME_LEN)		return -EINVAL;
//		fname->hash = get_unaligned((f2fs_hash_t *)	&raw_inode->i_name[fname->disk_name.len]);
		//<YUAN> get_unalinged:用于调整结构地址。暂时忽略
//		jcvos::Utf8ToUnicode(usr_fname->name, (const char*)(raw_inode->i_name));
		usr_fname->name = (const char*)raw_inode->i_name;
		fname->disk_name.name = usr_fname->name.c_str();
		fname->disk_name.len = usr_fname->len();
		if (fname->disk_name.len > F2FS_NAME_LEN) return -EINVAL;
		fname->hash = *((f2fs_hash_t*)&raw_inode->i_name[fname->disk_name.len]);
	} 
	else if (IS_CASEFOLDED(dir)) 
	{
		err = f2fs_init_casefolded_name(dir, fname);
		if (err)		return err;
		f2fs_hash_filename(dir, fname);
#ifdef CONFIG_UNICODE
		/* Case-sensitive match is fine for recovery */
//		kfree(fname->cf_name.name);
		delete[] fname->cf_name.name;
		fname->cf_name.name = NULL;
#endif
	} 
	else 
	{
		f2fs_hash_filename(dir, fname);
	}
	return 0;
}

static int recover_dentry(f2fs_inode_info *inode_buf, page *ipage, list_head *dir_list)
{
	f2fs_inode *raw_inode = F2FS_INODE(ipage);
	nid_t pino = le32_to_cpu(raw_inode->i_pino);
	f2fs_dir_entry *de;
	f2fs_filename fname;
	qstr usr_fname;
	page *ppage;
//	inode *einode;
	struct fsync_inode_entry *entry;
	int err = 0;
	//char *name;

	entry = get_fsync_inode(dir_list, pino);
	if (!entry) 
	{
		entry = add_fsync_inode(F2FS_I_SB(inode_buf), dir_list, pino, false);
		if (IS_ERR(entry)) 
		{
//			dir = ERR_CAST(entry);
			err = (int)PTR_ERR(entry);
			LOG_ERROR(L"[err] failed on add_fsync_inode, ino=0x%X, name=%S, err=%d", ino_of_node(ipage), raw_inode->i_name, err);
//			goto out;
		}
	}

	Cf2fsDirInode * dir = dynamic_cast<Cf2fsDirInode*>(entry->inode);
	if (!dir) THROW_ERROR(ERR_APP, L"inode is not a dir inode");
	err = init_recovered_filename(dir, raw_inode, &fname, &usr_fname);
	if (err) goto out;
retry:
	de = dir->__f2fs_find_entry(&fname, &ppage);
	if (de && inode_buf->i_ino == le32_to_cpu(de->ino))
		goto out_put;

	if (de) 
	{
		f2fs_inode_info *einode = inode_buf->m_sbi->f2fs_iget_retry(le32_to_cpu(de->ino));
		if (IS_ERR(einode)) 
		{
			WARN_ON(1);
			err = PTR_ERR(einode);
			if (err == -ENOENT)		err = -EEXIST;
			goto out_put;
		}

		err = dquot_initialize(einode);
		if (err) 
		{
			iput(einode);
			goto out_put;
		}

		err = inode_buf->m_sbi->f2fs_acquire_orphan_inode();
		if (err) 
		{
			iput(einode);
			goto out_put;
		}
		dir->f2fs_delete_entry(de, ppage, einode);
		iput(einode);
		goto retry;
	} 
	else if (IS_ERR(ppage)) 	{		err = PTR_ERR(ppage);	}
	else {	err = f2fs_add_dentry(dir, &fname, inode_buf, inode_buf->i_ino, inode_buf->i_mode); 	}
	if (err == -ENOMEM)		goto retry;
	goto out;

out_put:
	f2fs_put_page(ppage, 0);
out:
	const char* name = NULL;
	if (file_enc_name(inode_buf))	name = "<encrypted>";
	else							name = (char*)raw_inode->i_name;
	LOG_NOTICE(L"ino=%x, name=%S, dir = %lx, err = %d", ino_of_node(ipage), name, IS_ERR(dir) ? 0 : dir->i_ino, err);
	return err;

//#undef ERR_CAST
}

static int recover_quota_data(struct inode *inode, struct page *page)
{
	f2fs_inode *raw = F2FS_INODE(page);
	iattr attr;
	//uid_t i_uid = le32_to_cpu(raw->i_uid);
	//gid_t i_gid = le32_to_cpu(raw->i_gid);
	int err;

	memset(&attr, 0, sizeof(attr));

	//attr.ia_uid = make_kuid(inode->i_sb->s_user_ns, i_uid);
	//attr.ia_gid = make_kgid(inode->i_sb->s_user_ns, i_gid);

	//if (!uid_eq(attr.ia_uid, inode->i_uid))		attr.ia_valid |= ATTR_UID;
	//if (!gid_eq(attr.ia_gid, inode->i_gid))		attr.ia_valid |= ATTR_GID;

	if (!attr.ia_valid)		return 0;

	err = dquot_transfer(inode, &attr);
	if (err)		set_sbi_flag(F2FS_I_SB(inode), SBI_QUOTA_NEED_REPAIR);
	return err;
}
static void recover_inline_flags(f2fs_inode_info *inode, f2fs_inode *ri)
{
	if (ri->i_inline & F2FS_PIN_FILE)		set_inode_flag(inode, FI_PIN_FILE);
	else									clear_inode_flag(inode, FI_PIN_FILE);

	if (ri->i_inline & F2FS_DATA_EXIST)		set_inode_flag(inode, FI_DATA_EXIST);
	else									clear_inode_flag(inode, FI_DATA_EXIST);
}

static int recover_inode(f2fs_inode_info *inode, struct page *page)
{
	struct f2fs_inode *raw = F2FS_INODE(page);
	//char *name;
	int err;

	inode->i_mode = le16_to_cpu(raw->i_mode);

	err = recover_quota_data(inode, page);
	if (err) return err;

#if 0	//IGNOR
	i_uid_write(inode, le32_to_cpu(raw->i_uid));
	i_gid_write(inode, le32_to_cpu(raw->i_gid));
#endif

	if (raw->i_inline & F2FS_EXTRA_ATTR) 
	{
		if (f2fs_sb_has_project_quota(F2FS_I_SB(inode)) &&
			F2FS_FITS_IN_INODE(raw, le16_to_cpu(raw->_u._s.i_extra_isize), _u._s.i_projid)) 
		{
#if 0 //TODO
			projid_t i_projid;
			kprojid_t kprojid;

			i_projid = (projid_t)le32_to_cpu(raw->i_projid);
			kprojid = make_kprojid(&init_user_ns, i_projid);

			if (!projid_eq(kprojid, inode->i_projid))
			{
				err = f2fs_transfer_project_quota(inode, kprojid);
				if (err)		return err;
				inode->i_projid = kprojid;
			}
#else
			JCASSERT(0);
#endif
		}
	}

	inode->f2fs_i_size_write(le64_to_cpu(raw->i_size));
	//inode->i_atime.tv_sec = le64_to_cpu(raw->i_atime);
	//inode->i_ctime.tv_sec = le64_to_cpu(raw->i_ctime);
	//inode->i_mtime.tv_sec = le64_to_cpu(raw->i_mtime);
	//inode->i_atime.tv_nsec = le32_to_cpu(raw->i_atime_nsec);
	//inode->i_ctime.tv_nsec = le32_to_cpu(raw->i_ctime_nsec);
	//inode->i_mtime.tv_nsec = le32_to_cpu(raw->i_mtime_nsec);
	inode->i_atime = le64_to_cpu(raw->i_atime);
	inode->i_ctime = le64_to_cpu(raw->i_ctime);
	inode->i_mtime = le64_to_cpu(raw->i_mtime);
	//inode->i_atime.tv_nsec = le32_to_cpu(raw->i_atime_nsec);
	//inode->i_ctime.tv_nsec = le32_to_cpu(raw->i_ctime_nsec);
	//inode->i_mtime.tv_nsec = le32_to_cpu(raw->i_mtime_nsec);


	inode->i_advise = raw->i_advise;
	inode->i_flags = le32_to_cpu(raw->i_flags);
	f2fs_set_inode_flags(inode);
	inode->i_gc_failures[GC_FAILURE_PIN] = le16_to_cpu(raw->i_gc_failures);

	recover_inline_flags(inode, raw);

	inode->f2fs_mark_inode_dirty_sync( true);

	const char* name;
	if (file_enc_name(inode))	name = "<encrypted>";
	else		name = reinterpret_cast<char*>(F2FS_INODE(page)->i_name);

	LOG_NOTICE(L"recover_inode: ino = %x, name = %s, inline = %x", ino_of_node(page), name, raw->i_inline);
	return 0;
}

static int find_fsync_dnodes(f2fs_sb_info *sbi, list_head *head, bool check_only)
{
	struct curseg_info *curseg;
	struct page *page = NULL;
	block_t blkaddr;
	unsigned int loop_cnt = 0;
	unsigned int free_blocks = sbi->MAIN_SEGS() * sbi->blocks_per_seg - sbi->valid_user_blocks();
	int err = 0;

	/* get node pages in the current segment */
	curseg = sbi->CURSEG_I(CURSEG_WARM_NODE);
	blkaddr = NEXT_FREE_BLKADDR(sbi, curseg);

	while (1) {
		struct fsync_inode_entry *entry;

		if (!sbi->f2fs_is_valid_blkaddr(blkaddr, META_POR))
			return 0;

		page = f2fs_get_tmp_page(sbi, blkaddr);
		if (IS_ERR(page)) {
			err = PTR_ERR(page);
			break;
		}

		if (!is_recoverable_dnode(page))
		{
			f2fs_put_page(page, 1);
			break;
		}

		if (!is_fsync_dnode(page))
			goto next;

		entry = get_fsync_inode(head, ino_of_node(page));
		if (!entry) {
			bool quota_inode = false;

			if (!check_only &&	IS_INODE(page) && is_dent_dnode(page)) {
				err = f2fs_recover_inode_page(sbi, page);
				if (err) {
					f2fs_put_page(page, 1);
					break;
				}
				quota_inode = true;
			}

			/*
			 * CP | dnode(F) | inode(DF)
			 * For this case, we should not give up now.
			 */
			entry = add_fsync_inode(sbi, head, ino_of_node(page),
								quota_inode);
			if (IS_ERR(entry)) {
				err = PTR_ERR(entry);
				if (err == -ENOENT) {
					err = 0;
					goto next;
				}
				f2fs_put_page(page, 1);
				break;
			}
		}
		entry->blkaddr = blkaddr;

		if (IS_INODE(page) && is_dent_dnode(page))
			entry->last_dentry = blkaddr;
next:
		/* sanity check in order to detect looped node chain */
		if (++loop_cnt >= free_blocks ||
			blkaddr == next_blkaddr_of_node(page)) {
			LOG_NOTICE(L"detect looped node chain, blkaddr:%u, next:%u",  blkaddr, next_blkaddr_of_node(page));
			f2fs_put_page(page, 1);
			err = -EINVAL;
			break;
		}

		/* check next segment */
		blkaddr = next_blkaddr_of_node(page);
		f2fs_put_page(page, 1);

		f2fs_ra_meta_pages_cond(sbi, blkaddr);
	}
	return err;
}

static void destroy_fsync_dnodes(struct list_head *head, int drop)
{
	fsync_inode_entry *entry, *tmp;
	list_for_each_entry_safe(fsync_inode_entry, entry, tmp, head, list)
		del_fsync_inode(entry, drop);
}

static int check_index_in_prev_nodes(struct f2fs_sb_info *sbi, block_t blkaddr, struct dnode_of_data *dn)
{
	struct seg_entry *sentry;
	unsigned int segno = GET_SEGNO(sbi, blkaddr);
	unsigned short blkoff = GET_BLKOFF_FROM_SEG0(sbi, blkaddr);
	struct f2fs_summary_block *sum_node;
	struct f2fs_summary sum;
	struct page *sum_page, *node_page;
	struct dnode_of_data tdn = *dn;
	nid_t ino, nid;
//	struct inode *inode;
	f2fs_inode_info* iinode = nullptr;
	unsigned int offset;
	block_t bidx;
	int i;

	sentry = sbi->get_seg_entry( segno);
	if (!f2fs_test_bit(blkoff, sentry->cur_valid_map))		return 0;

	/* Get the previous summary */
	for (i = CURSEG_HOT_DATA; i <= CURSEG_COLD_DATA; i++) 
	{
		curseg_info *curseg = sbi->CURSEG_I(i);
		if (curseg->segno == segno) 
		{
			sum = curseg->sum_blk.entries[blkoff];
			goto got_it;
		}
	}

	sum_page = f2fs_get_sum_page(sbi, segno);
	if (IS_ERR(sum_page))		return PTR_ERR(sum_page);
	sum_node = page_address<f2fs_summary_block>(sum_page);
	sum = sum_node->entries[blkoff];
	f2fs_put_page(sum_page, 1);
got_it:
	/* Use the locked dnode page and inode */
	nid = le32_to_cpu(sum.nid);
	if (dn->inode->i_ino == nid) {
		tdn.nid = nid;
		if (!dn->inode_page_locked)
			lock_page(dn->inode_page);
		tdn.node_page = dn->inode_page;
		tdn.ofs_in_node = le16_to_cpu(sum._u._s.ofs_in_node);
		goto truncate_out;
	} 
	else if (dn->nid == nid) 
	{
		tdn.ofs_in_node = le16_to_cpu(sum._u._s.ofs_in_node);
		goto truncate_out;
	}

	/* Get the node page */
	node_page = sbi->f2fs_get_node_page(nid);
	if (IS_ERR(node_page))		return PTR_ERR(node_page);

	offset = ofs_of_node(node_page);
	ino = ino_of_node(node_page);
	f2fs_put_page(node_page, 1);

	if (ino != dn->inode->i_ino) 
	{
		int ret;

		/* Deallocate previous index in the node page */
		iinode = sbi->f2fs_iget_retry(ino);
		if (IS_ERR(iinode))
			return PTR_ERR(iinode);

		ret = dquot_initialize(iinode);
		if (ret) 
		{
			iput(iinode);
			return ret;
		}
	} else {
		iinode = dn->inode;
	}

	bidx = f2fs_start_bidx_of_node(offset, iinode) +le16_to_cpu(sum._u._s.ofs_in_node);

	/* if inode page is locked, unlock temporarily, but its reference count keeps alive. */
	if (ino == dn->inode->i_ino && dn->inode_page_locked)
		unlock_page(dn->inode_page);

	tdn.set_new_dnode( iinode, NULL, NULL, 0);
	if (f2fs_get_dnode_of_data(&tdn, bidx, LOOKUP_NODE))
		goto out;

	if (tdn.data_blkaddr == blkaddr)
		f2fs_truncate_data_blocks_range(&tdn, 1);

	f2fs_put_dnode(&tdn);
out:
	if (ino != dn->inode->i_ino)
		iput(iinode);
	else if (dn->inode_page_locked)
		lock_page(dn->inode_page);
	return 0;

truncate_out:
	if (f2fs_data_blkaddr(&tdn) == blkaddr)
		f2fs_truncate_data_blocks_range(&tdn, 1);
	if (dn->inode->i_ino == nid && !dn->inode_page_locked)
		unlock_page(dn->inode_page);
	return 0;
}
static int do_recover_data(f2fs_sb_info *sbi, inode *inode, page *page)
{
	struct dnode_of_data dn;
	struct node_info ni;
	unsigned int start, end;
	int err = 0, recovered = 0;

	/* step 1: recover xattr */
	if (IS_INODE(page)) 
	{
		err = f2fs_recover_inline_xattr(inode, page);
		if (err)			goto out;
	}
	else if (f2fs_has_xattr_block(ofs_of_node(page))) 
	{
		err = f2fs_recover_xattr_data(inode, page);
		if (!err)			recovered++;
		goto out;
	}

	/* step 2: recover inline data */
	err = f2fs_recover_inline_data(F2FS_I(inode), page);
	if (err) {
		if (err == 1)
			err = 0;
		goto out;
	}

	/* step 3: recover data indices */
	start = f2fs_start_bidx_of_node(ofs_of_node(page), inode);
	end = start + ADDRS_PER_PAGE(page, inode);

	dn.set_new_dnode(inode, NULL, NULL, 0);
retry_dn:
	err = f2fs_get_dnode_of_data(&dn, start, ALLOC_NODE);
	if (err) 
	{
		if (err == -ENOMEM) 
		{
#if 0	//<YUAN> congestion_wait 相当于sleep()
			congestion_wait(BLK_RW_ASYNC, DEFAULT_IO_TIMEOUT);
#endif
			goto retry_dn;
		}
		goto out;
	}

	f2fs_wait_on_page_writeback(dn.node_page, NODE, true, true);

	err = NM_I(sbi)->f2fs_get_node_info( dn.nid, &ni);
	if (err)
		goto err;

	f2fs_bug_on(sbi, ni.ino != ino_of_node(page));

	if (ofs_of_node(dn.node_page) != ofs_of_node(page)) {
		f2fs_warn(sbi, L"Inconsistent ofs_of_node, ino:%lu, ofs:%u, %u",
			  inode->i_ino, ofs_of_node(dn.node_page),
			  ofs_of_node(page));
		err = -EFSCORRUPTED;
		goto err;
	}

	for (; start < end; start++, dn.ofs_in_node++) {
		block_t src, dest;

		src = f2fs_data_blkaddr(&dn);
		dest = data_blkaddr(dn.inode, page, dn.ofs_in_node);

		if (__is_valid_data_blkaddr(src) &&
			!sbi->f2fs_is_valid_blkaddr(src, META_POR)) {
			err = -EFSCORRUPTED;
			goto err;
		}

		if (__is_valid_data_blkaddr(dest) &&
			!sbi->f2fs_is_valid_blkaddr(dest, META_POR)) {
			err = -EFSCORRUPTED;
			goto err;
		}

		/* skip recovering if dest is the same as src */
		if (src == dest)
			continue;

		/* dest is invalid, just invalidate src block */
		if (dest == NULL_ADDR)
		{
			f2fs_truncate_data_blocks_range(&dn, 1);
			continue;
		}

		if (!file_keep_isize(inode) && (i_size_read(inode) <= ((loff_t)start << PAGE_SHIFT)))
			F2FS_I(inode)->f2fs_i_size_write( (loff_t)(start + 1) << PAGE_SHIFT);

		/* dest is reserved block, invalidate src block and then reserve one new block in dnode page. */
		if (dest == NEW_ADDR) {
			f2fs_truncate_data_blocks_range(&dn, 1);
			f2fs_reserve_new_block(&dn);
			continue;
		}

		/* dest is valid block, try to recover from src to dest */
		if (sbi->f2fs_is_valid_blkaddr(dest, META_POR)) {

			if (src == NULL_ADDR) {
				err = f2fs_reserve_new_block(&dn);
				while (err && /*IS_ENABLED(CONFIG_F2FS_FAULT_INJECTION)*/ false)	err = f2fs_reserve_new_block(&dn);
				/* We should not get -ENOSPC */
				f2fs_bug_on(sbi, err);
				if (err) goto err;
			}
retry_prev:
			/* Check the previous node page having this index */
			err = check_index_in_prev_nodes(sbi, dest, &dn);
			if (err)
			{
				if (err == -ENOMEM) 
				{
#if 0		//<YUAN> congestion_wait相当于Sleep()，此处忽略
					congestion_wait(BLK_RW_ASYNC, DEFAULT_IO_TIMEOUT);
#endif
					goto retry_prev;
				}
				goto err;
			}

			/* write dummy data page */
			f2fs_replace_block(sbi, &dn, src, dest,
						ni.version, false, false);
			recovered++;
		}
	}

	copy_node_footer(dn.node_page, page);
	fill_node_footer(dn.node_page, dn.nid, ni.ino, ofs_of_node(page), false);
	set_page_dirty(dn.node_page);
err:
	f2fs_put_dnode(&dn);
out:
	f2fs_notice(sbi, L"recover_data: ino = %lx (i_size: %s) recovered = %d, err = %d",
		    inode->i_ino, file_keep_isize(inode) ? "keep" : "recover",    recovered, err);
	return err;
}

static int recover_data(f2fs_sb_info *sbi, list_head *inode_list, list_head *tmp_inode_list, list_head *dir_list)
{
	struct curseg_info *curseg;
	struct page *page = NULL;
	int err = 0;
	block_t blkaddr;

	/* get node pages in the current segment */
	curseg = sbi->CURSEG_I(CURSEG_WARM_NODE);
	blkaddr = NEXT_FREE_BLKADDR(sbi, curseg);

	while (1) 
	{
		fsync_inode_entry *entry;

		if (!sbi->f2fs_is_valid_blkaddr(blkaddr, META_POR))
			break;

		f2fs_ra_meta_pages_cond(sbi, blkaddr);

		page = f2fs_get_tmp_page(sbi, blkaddr);
		if (IS_ERR(page)) {
			err = PTR_ERR(page);
			break;
		}

		if (!is_recoverable_dnode(page)) {
			f2fs_put_page(page, 1);
			break;
		}

		entry = get_fsync_inode(inode_list, ino_of_node(page));
		if (!entry)
			goto next;
		/* inode(x) | CP | inode(x) | dnode(F)
		 * In this case, we can lose the latest inode(x). So, call recover_inode for the inode update. */
		if (IS_INODE(page)) 
		{
			err = recover_inode(F2FS_I(entry->inode), page);
			if (err) {
				f2fs_put_page(page, 1);
				break;
			}
		}
		if (entry->last_dentry == blkaddr) {
			err = recover_dentry(entry->inode, page, dir_list);
			if (err) {
				f2fs_put_page(page, 1);
				break;
			}
		}
		err = do_recover_data(sbi, entry->inode, page);
		if (err) {
			f2fs_put_page(page, 1);
			break;
		}

		if (entry->blkaddr == blkaddr)
			list_move_tail(&entry->list, tmp_inode_list);
next:
		/* check next segment */
		blkaddr = next_blkaddr_of_node(page);
		f2fs_put_page(page, 1);
	}
	if (!err)
		f2fs_allocate_new_segments(sbi);
	return err;
}

int f2fs_recover_fsync_data(struct f2fs_sb_info *sbi, bool check_only)
{
	struct list_head inode_list, tmp_inode_list;
	struct list_head dir_list;
	int err;
	int ret = 0;
	unsigned long s_flags = sbi->s_flags;
	bool need_writecp = false;
	bool fix_curseg_write_pointer = false;
#ifdef CONFIG_QUOTA
	int quota_enabled;
#endif

	if (s_flags & SB_RDONLY) 
	{
		f2fs_info(sbi, L"recover fsync data on readonly fs");
		sbi->s_flags &= ~SB_RDONLY;
	}

#ifdef CONFIG_QUOTA
	/* Needed for iput() to work correctly and not trash data */
	sbi->s_flags |= SB_ACTIVE;
	/* Turn on quotas so that they are updated correctly */
	quota_enabled = f2fs_enable_quota_files(sbi, s_flags & SB_RDONLY);
#endif

	INIT_LIST_HEAD(&inode_list);
	INIT_LIST_HEAD(&tmp_inode_list);
	INIT_LIST_HEAD(&dir_list);

	/* prevent checkpoint */
	down_write(&sbi->cp_global_sem);

	/* step #1: find fsynced inode numbers */
	err = find_fsync_dnodes(sbi, &inode_list, check_only);
	if (err || list_empty(&inode_list))		goto skip;

	if (check_only) {
		ret = 1;
		goto skip;
	}

	need_writecp = true;

	/* step #2: recover data */
	err = recover_data(sbi, &inode_list, &tmp_inode_list, &dir_list);
	if (!err)
		f2fs_bug_on(sbi, !list_empty(&inode_list));
	else {
		/* restore s_flags to let iput() trash data */
		sbi->s_flags = s_flags;
	}
skip:
	fix_curseg_write_pointer = !check_only || list_empty(&inode_list);

	destroy_fsync_dnodes(&inode_list, err);
	destroy_fsync_dnodes(&tmp_inode_list, err);

#if 1 //<TODO> truncate: 删除指定page，暂时忽略
	/* truncate meta pages to be used by the recovery */
	truncate_inode_pages_range(META_MAPPING(sbi), (loff_t)MAIN_BLKADDR(sbi) << PAGE_SHIFT, -1);

	if (err) 
	{
		truncate_inode_pages_final(NODE_MAPPING(sbi));
		truncate_inode_pages_final(META_MAPPING(sbi));
	}
#else
	JCASSERT(0);
#endif

	/*
	 * If fsync data succeeds or there is no fsync data to recover,
	 * and the f2fs is not read only, check and fix zoned block devices'
	 * write pointer consistency.
	 */
	if (!err && fix_curseg_write_pointer && !sbi->f2fs_readonly() &&
			f2fs_sb_has_blkzoned(sbi)) {
		err = f2fs_fix_curseg_write_pointer(sbi);
		ret = err;
	}

	if (!err)
		clear_sbi_flag(sbi, SBI_POR_DOING);

	up_write(&sbi->cp_global_sem);

	/* let's drop all the directory inodes for clean checkpoint */
	destroy_fsync_dnodes(&dir_list, err);

	if (need_writecp)
	{
		sbi->set_sbi_flag(SBI_IS_RECOVERED);
		if (!err)
		{
			struct cp_control cpc;
			//= {	.reason = CP_RECOVERY,		};
			cpc.reason = CP_RECOVERY;
			err = sbi->f2fs_write_checkpoint(&cpc);
		}
	}

#ifdef CONFIG_QUOTA
	/* Turn quotas off */
	if (quota_enabled)
		f2fs_quota_off_umount(sbi->sb);
#endif
	sbi->s_flags = s_flags; /* Restore SB_RDONLY status */

	return ret ? ret : err;
}

#if 0

int __init f2fs_create_recovery_cache(void)
{
	fsync_entry_slab = f2fs_kmem_cache_create("f2fs_fsync_inode_entry",
					sizeof(struct fsync_inode_entry));
	if (!fsync_entry_slab)
		return -ENOMEM;
	return 0;
}

void f2fs_destroy_recovery_cache(void)
{
	kmem_cache_destroy(fsync_entry_slab);
}


#endif //TODO

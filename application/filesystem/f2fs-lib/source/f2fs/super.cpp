///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
#include "../pch.h"
// SPDX-License-Identifier: GPL-2.0
/*
 * fs/f2fs/super.c
 *
 * Copyright (c) 2012 Samsung Electronics Co., Ltd.
 *             http://www.samsung.com/
 */

#include <linux-fs-wrapper.h>
//#include <linux/module.h>
//#include <linux/init.h>
//#include "../../include/fs.h"
//#include <linux/statfs.h>
//#include <linux/CBufferHead.h>
//#include <linux/backing-dev.h>
//#include <linux/kthread.h>
//#include <linux/parser.h>
//#include <linux/mount.h>
//#include <linux/seq_file.h>
//#include <linux/proc_fs.h>
//#include <linux/random.h>
//#include <linux/exportfs.h>
//#include <linux/blkdev.h>
//#include <linux/quotaops.h>
//#include <linux/f2fs_fs.h>
//#include <linux/sysfs.h>
//#include <linux/quota.h>
//#include <linux/unicode.h>
//#include <linux/part_stat.h>
//#include <linux/zstd.h>
//#include <linux/lz4.h>
#include "../../include/config.h"
#include "../../include/f2fs_fs.h"
#include "../../include/f2fs-filesystem.h"

//#include "2fs.h"
#include "node.h"
#include "segment.h"
#include "xattr.h"
#include "gc.h"
#include <boost/property_tree/json_parser.hpp>

LOCAL_LOGGER_ENABLE(L"f2fs.super", LOGGER_LEVEL_DEBUGINFO);


#define CREATE_TRACE_POINTS
//#include <trace/events/f2fs.h>

//static struct kmem_cache *f2fs_inode_cachep;

#ifdef CONFIG_F2FS_FAULT_INJECTION

const char *f2fs_fault_name[FAULT_MAX] = {
	[FAULT_KMALLOC]		= "kmalloc",
	[FAULT_KVMALLOC]	= "kvmalloc",
	[FAULT_PAGE_ALLOC]	= "page alloc",
	[FAULT_PAGE_GET]	= "page get",
	[FAULT_ALLOC_NID]	= "alloc nid",
	[FAULT_ORPHAN]		= "orphan",
	[FAULT_BLOCK]		= "no more block",
	[FAULT_DIR_DEPTH]	= "too big dir depth",
	[FAULT_EVICT_INODE]	= "evict_inode fail",
	[FAULT_TRUNCATE]	= "truncate fail",
	[FAULT_READ_IO]		= "read IO error",
	[FAULT_CHECKPOINT]	= "checkpoint error",
	[FAULT_DISCARD]		= "discard error",
	[FAULT_WRITE_IO]	= "write IO error",
};

void f2fs_build_fault_attr(struct f2fs_sb_info *sbi, unsigned int rate,
							unsigned int type)
{
	struct f2fs_fault_info *ffi = &F2FS_OPTION(sbi).fault_info;

	if (rate) {
		atomic_set(&ffi->inject_ops, 0);
		ffi->inject_rate = rate;
	}

	if (type)
		ffi->inject_type = type;

	if (!rate && !type)
		memset(ffi, 0, sizeof(struct f2fs_fault_info));
}
#endif

/* f2fs-wide shrinker description */
#if 0
static struct shrinker f2fs_shrinker_info = {
	.scan_objects = f2fs_shrink_scan,
	.count_objects = f2fs_shrink_count,
	.seeks = DEFAULT_SEEKS,
};
#endif

enum {
	Opt_gc_background,
	Opt_disable_roll_forward,
	Opt_norecovery,
	Opt_discard,
	Opt_nodiscard,
	Opt_noheap,
	Opt_heap,
	Opt_user_xattr,
	Opt_nouser_xattr,
	Opt_acl,
	Opt_noacl,
	Opt_active_logs,
	Opt_disable_ext_identify,
	Opt_inline_xattr,
	Opt_noinline_xattr,
	Opt_inline_xattr_size,
	Opt_inline_data,
	Opt_inline_dentry,
	Opt_noinline_dentry,
	Opt_flush_merge,
	Opt_noflush_merge,
	Opt_nobarrier,
	Opt_fastboot,
	Opt_extent_cache,
	Opt_noextent_cache,
	Opt_noinline_data,
	Opt_data_flush,
	Opt_reserve_root,
	Opt_resgid,
	Opt_resuid,
	Opt_mode,
	Opt_io_size_bits,
	Opt_fault_injection,
	Opt_fault_type,
	Opt_lazytime,
	Opt_nolazytime,
	Opt_quota,
	Opt_noquota,
	Opt_usrquota,
	Opt_grpquota,
	Opt_prjquota,
	Opt_usrjquota,
	Opt_grpjquota,
	Opt_prjjquota,
	Opt_offusrjquota,
	Opt_offgrpjquota,
	Opt_offprjjquota,
	Opt_jqfmt_vfsold,
	Opt_jqfmt_vfsv0,
	Opt_jqfmt_vfsv1,
	Opt_whint,
	Opt_alloc,
	Opt_fsync,
	Opt_test_dummy_encryption,
	Opt_inlinecrypt,
	Opt_checkpoint_disable,
	Opt_checkpoint_disable_cap,
	Opt_checkpoint_disable_cap_perc,
	Opt_checkpoint_enable,
	Opt_checkpoint_merge,
	Opt_nocheckpoint_merge,
	Opt_compress_algorithm,
	Opt_compress_log_size,
	Opt_compress_extension,
	Opt_compress_chksum,
	Opt_compress_mode,
	Opt_atgc,
	Opt_gc_merge,
	Opt_nogc_merge,
	Opt_err,
};

#if 0
static match_table_t f2fs_tokens = {
	{Opt_gc_background, "background_gc=%s"},
	{Opt_disable_roll_forward, "disable_roll_forward"},
	{Opt_norecovery, "norecovery"},
	{Opt_discard, "discard"},
	{Opt_nodiscard, "nodiscard"},
	{Opt_noheap, "no_heap"},
	{Opt_heap, "heap"},
	{Opt_user_xattr, "user_xattr"},
	{Opt_nouser_xattr, "nouser_xattr"},
	{Opt_acl, "acl"},
	{Opt_noacl, "noacl"},
	{Opt_active_logs, "active_logs=%u"},
	{Opt_disable_ext_identify, "disable_ext_identify"},
	{Opt_inline_xattr, "inline_xattr"},
	{Opt_noinline_xattr, "noinline_xattr"},
	{Opt_inline_xattr_size, "inline_xattr_size=%u"},
	{Opt_inline_data, "inline_data"},
	{Opt_inline_dentry, "inline_dentry"},
	{Opt_noinline_dentry, "noinline_dentry"},
	{Opt_flush_merge, "flush_merge"},
	{Opt_noflush_merge, "noflush_merge"},
	{Opt_nobarrier, "nobarrier"},
	{Opt_fastboot, "fastboot"},
	{Opt_extent_cache, "extent_cache"},
	{Opt_noextent_cache, "noextent_cache"},
	{Opt_noinline_data, "noinline_data"},
	{Opt_data_flush, "data_flush"},
	{Opt_reserve_root, "reserve_root=%u"},
	{Opt_resgid, "resgid=%u"},
	{Opt_resuid, "resuid=%u"},
	{Opt_mode, "mode=%s"},
	{Opt_io_size_bits, "io_bits=%u"},
	{Opt_fault_injection, "fault_injection=%u"},
	{Opt_fault_type, "fault_type=%u"},
	{Opt_lazytime, "lazytime"},
	{Opt_nolazytime, "nolazytime"},
	{Opt_quota, "quota"},
	{Opt_noquota, "noquota"},
	{Opt_usrquota, "usrquota"},
	{Opt_grpquota, "grpquota"},
	{Opt_prjquota, "prjquota"},
	{Opt_usrjquota, "usrjquota=%s"},
	{Opt_grpjquota, "grpjquota=%s"},
	{Opt_prjjquota, "prjjquota=%s"},
	{Opt_offusrjquota, "usrjquota="},
	{Opt_offgrpjquota, "grpjquota="},
	{Opt_offprjjquota, "prjjquota="},
	{Opt_jqfmt_vfsold, "jqfmt=vfsold"},
	{Opt_jqfmt_vfsv0, "jqfmt=vfsv0"},
	{Opt_jqfmt_vfsv1, "jqfmt=vfsv1"},
	{Opt_whint, "whint_mode=%s"},
	{Opt_alloc, "alloc_mode=%s"},
	{Opt_fsync, "fsync_mode=%s"},
	{Opt_test_dummy_encryption, "test_dummy_encryption=%s"},
	{Opt_test_dummy_encryption, "test_dummy_encryption"},
	{Opt_inlinecrypt, "inlinecrypt"},
	{Opt_checkpoint_disable, "checkpoint=disable"},
	{Opt_checkpoint_disable_cap, "checkpoint=disable:%u"},
	{Opt_checkpoint_disable_cap_perc, "checkpoint=disable:%u%%"},
	{Opt_checkpoint_enable, "checkpoint=enable"},
	{Opt_checkpoint_merge, "checkpoint_merge"},
	{Opt_nocheckpoint_merge, "nocheckpoint_merge"},
	{Opt_compress_algorithm, "compress_algorithm=%s"},
	{Opt_compress_log_size, "compress_log_size=%u"},
	{Opt_compress_extension, "compress_extension=%s"},
	{Opt_compress_chksum, "compress_chksum"},
	{Opt_compress_mode, "compress_mode=%s"},
	{Opt_atgc, "atgc"},
	{Opt_gc_merge, "gc_merge"},
	{Opt_nogc_merge, "nogc_merge"},
	{Opt_err, NULL},
};

void f2fs_printk(struct f2fs_sb_info *sbi, const char *fmt, ...)
{
	struct va_format vaf;
	va_list args;
	int level;

	va_start(args, fmt);

	level = printk_get_level(fmt);
	vaf.fmt = printk_skip_level(fmt);
	vaf.va = &args;
	printk("%c%cF2FS-fs (%s): %pV\n", KERN_SOH_ASCII, level, sbi->s_id, &vaf);

	va_end(args);
}
#endif

#ifdef CONFIG_UNICODE
struct f2fs_sb_encodings 
{
	UINT16 magic;
	const char *name;
	const char *version;
};

static const f2fs_sb_encodings f2fs_sb_encoding_map[1] = {
	{F2FS_ENC_UTF8_12_1, "utf8", "12.1.0"},
};

static int f2fs_sb_read_encoding(const struct f2fs_super_block *sb, const struct f2fs_sb_encodings **encoding, UINT16 *flags)
{
	UINT16 magic = le16_to_cpu(sb->s_encoding);
	int i;

	for (i = 0; i < ARRAY_SIZE(f2fs_sb_encoding_map); i++)
		if (magic == f2fs_sb_encoding_map[i].magic)	break;

	if (i >= ARRAY_SIZE(f2fs_sb_encoding_map))	return -EINVAL;

	*encoding = &f2fs_sb_encoding_map[i];
	*flags = le16_to_cpu(sb->s_encoding_flags);

	return 0;
}
#endif

static inline void limit_reserve_root(struct f2fs_sb_info *sbi)
{
	block_t limit = min((sbi->user_block_count << 1) / 1000, sbi->user_block_count - sbi->reserved_blocks);

	/* limit is 0.2% */
	if (test_opt(sbi, RESERVE_ROOT) && F2FS_OPTION(sbi).root_reserved_blocks > limit) 
	{
		F2FS_OPTION(sbi).root_reserved_blocks = limit;
		f2fs_info(sbi, L"Reduce reserved blocks for root = %u", F2FS_OPTION(sbi).root_reserved_blocks);
	}
#if 0 // <TODO> 暂不处理和uid，gid相关内容
	if (!test_opt(sbi, RESERVE_ROOT) &&
		(!uid_eq(F2FS_OPTION(sbi).s_resuid,	make_kuid(&init_user_ns, F2FS_DEF_RESUID)) ||
		!gid_eq(F2FS_OPTION(sbi).s_resgid, make_kgid(&init_user_ns, F2FS_DEF_RESGID))))
		f2fs_info(sbi, L"Ignore s_resuid=%u, s_resgid=%u w/o reserve_root",
			  from_kuid_munged(&init_user_ns, F2FS_OPTION(sbi).s_resuid),
			  from_kgid_munged(&init_user_ns, F2FS_OPTION(sbi).s_resgid));
#endif
}

static inline void adjust_unusable_cap_perc(struct f2fs_sb_info *sbi)
{
	if (!F2FS_OPTION(sbi).unusable_cap_perc)	return;

	if (F2FS_OPTION(sbi).unusable_cap_perc == 100)		F2FS_OPTION(sbi).unusable_cap = sbi->user_block_count;
	else		F2FS_OPTION(sbi).unusable_cap = (sbi->user_block_count / 100) * F2FS_OPTION(sbi).unusable_cap_perc;

	f2fs_info(sbi, L"Adjust unusable cap for checkpoint=disable = %u / %u%%",
			F2FS_OPTION(sbi).unusable_cap,
			F2FS_OPTION(sbi).unusable_cap_perc);
}

static void init_once(void *foo)
{
	f2fs_inode_info *fi =reinterpret_cast<f2fs_inode_info *>( foo);
	inode_init_once(static_cast<inode*>(fi));
}

#ifdef CONFIG_QUOTA
static const char * const quotatypes[] = INITQFNAMES;
#define QTYPE2NAME(t) (quotatypes[t])
static int f2fs_set_qf_name(struct super_block *sb, int qtype,
							substring_t *args)
{
	struct f2fs_sb_info *sbi = F2FS_SB(sb);
	char *qname;
	int ret = -EINVAL;

	if (sb_any_quota_loaded(sb) && !F2FS_OPTION(sbi).s_qf_names[qtype]) {
		f2fs_err(sbi, "Cannot change journaled quota options when quota turned on");
		return -EINVAL;
	}
	if (f2fs_sb_has_quota_ino(sbi)) {
		f2fs_info(sbi, "QUOTA feature is enabled, so ignore qf_name");
		return 0;
	}

	qname = match_strdup(args);
	if (!qname) {
		f2fs_err(sbi, "Not enough memory for storing quotafile name");
		return -ENOMEM;
	}
	if (F2FS_OPTION(sbi).s_qf_names[qtype]) {
		if (strcmp(F2FS_OPTION(sbi).s_qf_names[qtype], qname) == 0)
			ret = 0;
		else
			f2fs_err(sbi, "%s quota file already specified",
				 QTYPE2NAME(qtype));
		goto errout;
	}
	if (strchr(qname, '/')) {
		f2fs_err(sbi, "quotafile must be on filesystem root");
		goto errout;
	}
	F2FS_OPTION(sbi).s_qf_names[qtype] = qname;
	set_opt(sbi, QUOTA);
	return 0;
errout:
	kfree(qname);
	return ret;
}

static int f2fs_clear_qf_name(struct super_block *sb, int qtype)
{
	struct f2fs_sb_info *sbi = F2FS_SB(sb);

	if (sb_any_quota_loaded(sb) && F2FS_OPTION(sbi).s_qf_names[qtype]) {
		f2fs_err(sbi, "Cannot change journaled quota options when quota turned on");
		return -EINVAL;
	}
	kfree(F2FS_OPTION(sbi).s_qf_names[qtype]);
	F2FS_OPTION(sbi).s_qf_names[qtype] = NULL;
	return 0;
}

static int f2fs_check_quota_options(struct f2fs_sb_info *sbi)
{
	/*
	 * We do the test below only for project quotas. 'usrquota' and
	 * 'grpquota' mount options are allowed even without quota feature
	 * to support legacy quotas in quota files.
	 */
	if (test_opt(sbi, PRJQUOTA) && !f2fs_sb_has_project_quota(sbi)) {
		f2fs_err(sbi, "Project quota feature not enabled. Cannot enable project quota enforcement.");
		return -1;
	}
	if (F2FS_OPTION(sbi).s_qf_names[USRQUOTA] ||
			F2FS_OPTION(sbi).s_qf_names[GRPQUOTA] ||
			F2FS_OPTION(sbi).s_qf_names[PRJQUOTA]) {
		if (test_opt(sbi, USRQUOTA) &&
				F2FS_OPTION(sbi).s_qf_names[USRQUOTA])
			clear_opt(sbi, USRQUOTA);

		if (test_opt(sbi, GRPQUOTA) &&
				F2FS_OPTION(sbi).s_qf_names[GRPQUOTA])
			clear_opt(sbi, GRPQUOTA);

		if (test_opt(sbi, PRJQUOTA) &&
				F2FS_OPTION(sbi).s_qf_names[PRJQUOTA])
			clear_opt(sbi, PRJQUOTA);

		if (test_opt(sbi, GRPQUOTA) || test_opt(sbi, USRQUOTA) ||
				test_opt(sbi, PRJQUOTA)) {
			f2fs_err(sbi, "old and new quota format mixing");
			return -1;
		}

		if (!F2FS_OPTION(sbi).s_jquota_fmt) {
			f2fs_err(sbi, "journaled quota format not specified");
			return -1;
		}
	}

	if (f2fs_sb_has_quota_ino(sbi) && F2FS_OPTION(sbi).s_jquota_fmt) {
		f2fs_info(sbi, "QUOTA feature is enabled, so ignore jquota_fmt");
		F2FS_OPTION(sbi).s_jquota_fmt = 0;
	}
	return 0;
}
#endif

static int f2fs_set_test_dummy_encryption(struct super_block *sb, const char *opt,  const char *arg, bool is_remount)
{
	struct f2fs_sb_info *sbi = F2FS_SB(sb);
#ifdef CONFIG_FS_ENCRYPTION
	int err;

	if (!f2fs_sb_has_encrypt(sbi)) {
		f2fs_err(sbi, "Encrypt feature is off");
		return -EINVAL;
	}

	/*
	 * This mount option is just for testing, and it's not worthwhile to
	 * implement the extra complexity (e.g. RCU protection) that would be
	 * needed to allow it to be set or changed during remount.  We do allow
	 * it to be specified during remount, but only if there is no change.
	 */
	if (is_remount && !F2FS_OPTION(sbi).dummy_enc_policy.policy) {
		f2fs_warn(sbi, "Can't set test_dummy_encryption on remount");
		return -EINVAL;
	}
	err = fscrypt_set_test_dummy_encryption(
		sb, arg->from, &F2FS_OPTION(sbi).dummy_enc_policy);
	if (err) {
		if (err == -EEXIST)
			f2fs_warn(sbi,
				  "Can't change test_dummy_encryption on remount");
		else if (err == -EINVAL)
			f2fs_warn(sbi, "Value of option \"%s\" is unrecognized",
				  opt);
		else
			f2fs_warn(sbi, "Error processing option \"%s\" [%d]",
				  opt, err);
		return -EINVAL;
	}
	f2fs_warn(sbi, "Test dummy encryption mode enabled");
#else
	LOG_WARNING(L"Test dummy encryption mount option ignored");
#endif
	return 0;
}

#ifdef CONFIG_F2FS_FS_COMPRESSION
#ifdef CONFIG_F2FS_FS_LZ4
static int f2fs_set_lz4hc_level(struct f2fs_sb_info *sbi, const char *str)
{
#ifdef CONFIG_F2FS_FS_LZ4HC
	unsigned int level;
#endif

	if (strlen(str) == 3) {
		F2FS_OPTION(sbi).compress_level = 0;
		return 0;
	}

#ifdef CONFIG_F2FS_FS_LZ4HC
	str += 3;

	if (str[0] != ':') {
		f2fs_info(sbi, "wrong format, e.g. <alg_name>:<compr_level>");
		return -EINVAL;
	}
	if (kstrtouint(str + 1, 10, &level))
		return -EINVAL;

	if (level < LZ4HC_MIN_CLEVEL || level > LZ4HC_MAX_CLEVEL) {
		f2fs_info(sbi, "invalid lz4hc compress level: %d", level);
		return -EINVAL;
	}

	F2FS_OPTION(sbi).compress_level = level;
	return 0;
#else
	f2fs_info(sbi, "kernel doesn't support lz4hc compression");
	return -EINVAL;
#endif
}
#endif

#ifdef CONFIG_F2FS_FS_ZSTD
static int f2fs_set_zstd_level(struct f2fs_sb_info *sbi, const char *str)
{
	unsigned int level;
	int len = 4;

	if (strlen(str) == len) {
		F2FS_OPTION(sbi).compress_level = 0;
		return 0;
	}

	str += len;

	if (str[0] != ':') {
		f2fs_info(sbi, "wrong format, e.g. <alg_name>:<compr_level>");
		return -EINVAL;
	}
	if (kstrtouint(str + 1, 10, &level))
		return -EINVAL;

	if (!level || level > ZSTD_maxCLevel()) {
		f2fs_info(sbi, "invalid zstd compress level: %d", level);
		return -EINVAL;
	}

	F2FS_OPTION(sbi).compress_level = level;
	return 0;
}
#endif

#endif


#if 0 // move to CF2fsFileSystem::parse_ount_options()
static int parse_options(struct super_block *sb, char *options, bool is_remount)
{
	struct f2fs_sb_info *sbi = F2FS_SB(sb);
	substring_t args[MAX_OPT_ARGS];
#ifdef CONFIG_F2FS_FS_COMPRESSION
	unsigned char (*ext)[F2FS_EXTENSION_LEN];
	int ext_cnt;
#endif
	char *p, *name;
	int arg = 0;
	kuid_t uid;
	kgid_t gid;
	int ret;

	if (!options)		return 0;

	while ((p = strsep(&options, ",")) != NULL) 
	{
		int token;

		if (!*p)			continue;
		/*
		 * Initialize args struct so we know whether arg was
		 * found; some options take optional arguments.
		 */
		args[0].to = args[0].from = NULL;
		token = match_token(p, f2fs_tokens, args);

		switch (token) {
		case Opt_gc_background:
			name = match_strdup(&args[0]);

			if (!name)
				return -ENOMEM;
			if (!strcmp(name, "on")) {
				F2FS_OPTION(sbi).bggc_mode = BGGC_MODE_ON;
			} else if (!strcmp(name, "off")) {
				F2FS_OPTION(sbi).bggc_mode = BGGC_MODE_OFF;
			} else if (!strcmp(name, "sync")) {
				F2FS_OPTION(sbi).bggc_mode = BGGC_MODE_SYNC;
			} else {
				kfree(name);
				return -EINVAL;
			}
			kfree(name);
			break;
		case Opt_disable_roll_forward:
			set_opt(sbi, DISABLE_ROLL_FORWARD);
			break;
		case Opt_norecovery:
			/* this option mounts f2fs with ro */
			set_opt(sbi, NORECOVERY);
			if (!f2fs_readonly(sb))
				return -EINVAL;
			break;
		case Opt_discard:
			set_opt(sbi, DISCARD);
			break;
		case Opt_nodiscard:
			if (f2fs_sb_has_blkzoned(sbi)) {
				f2fs_warn(sbi, "discard is required for zoned block devices");
				return -EINVAL;
			}
			clear_opt(sbi, DISCARD);
			break;
		case Opt_noheap:
			set_opt(sbi, NOHEAP);
			break;
		case Opt_heap:
			clear_opt(sbi, NOHEAP);
			break;
#ifdef CONFIG_F2FS_FS_XATTR
		case Opt_user_xattr:
			set_opt(sbi, XATTR_USER);
			break;
		case Opt_nouser_xattr:
			clear_opt(sbi, XATTR_USER);
			break;
		case Opt_inline_xattr:
			set_opt(sbi, INLINE_XATTR);
			break;
		case Opt_noinline_xattr:
			clear_opt(sbi, INLINE_XATTR);
			break;
		case Opt_inline_xattr_size:
			if (args->from && match_int(args, &arg))
				return -EINVAL;
			set_opt(sbi, INLINE_XATTR_SIZE);
			F2FS_OPTION(sbi).inline_xattr_size = arg;
			break;
#else
		case Opt_user_xattr:
			f2fs_info(sbi, "user_xattr options not supported");
			break;
		case Opt_nouser_xattr:
			f2fs_info(sbi, "nouser_xattr options not supported");
			break;
		case Opt_inline_xattr:
			f2fs_info(sbi, "inline_xattr options not supported");
			break;
		case Opt_noinline_xattr:
			f2fs_info(sbi, "noinline_xattr options not supported");
			break;
#endif
#ifdef CONFIG_F2FS_FS_POSIX_ACL
		case Opt_acl:
			set_opt(sbi, POSIX_ACL);
			break;
		case Opt_noacl:
			clear_opt(sbi, POSIX_ACL);
			break;
#else
		case Opt_acl:
			f2fs_info(sbi, "acl options not supported");
			break;
		case Opt_noacl:
			f2fs_info(sbi, "noacl options not supported");
			break;
#endif
		case Opt_active_logs:
			if (args->from && match_int(args, &arg))
				return -EINVAL;
			if (arg != 2 && arg != 4 &&
				arg != NR_CURSEG_PERSIST_TYPE)
				return -EINVAL;
			F2FS_OPTION(sbi).active_logs = arg;
			break;
		case Opt_disable_ext_identify:
			set_opt(sbi, DISABLE_EXT_IDENTIFY);
			break;
		case Opt_inline_data:
			set_opt(sbi, INLINE_DATA);
			break;
		case Opt_inline_dentry:
			set_opt(sbi, INLINE_DENTRY);
			break;
		case Opt_noinline_dentry:
			clear_opt(sbi, INLINE_DENTRY);
			break;
		case Opt_flush_merge:
			set_opt(sbi, FLUSH_MERGE);
			break;
		case Opt_noflush_merge:
			clear_opt(sbi, FLUSH_MERGE);
			break;
		case Opt_nobarrier:
			set_opt(sbi, NOBARRIER);
			break;
		case Opt_fastboot:
			set_opt(sbi, FASTBOOT);
			break;
		case Opt_extent_cache:
			set_opt(sbi, EXTENT_CACHE);
			break;
		case Opt_noextent_cache:
			clear_opt(sbi, EXTENT_CACHE);
			break;
		case Opt_noinline_data:
			clear_opt(sbi, INLINE_DATA);
			break;
		case Opt_data_flush:
			set_opt(sbi, DATA_FLUSH);
			break;
		case Opt_reserve_root:
			if (args->from && match_int(args, &arg))
				return -EINVAL;
			if (test_opt(sbi, RESERVE_ROOT)) {
				f2fs_info(sbi, "Preserve previous reserve_root=%u",
					  F2FS_OPTION(sbi).root_reserved_blocks);
			} else {
				F2FS_OPTION(sbi).root_reserved_blocks = arg;
				set_opt(sbi, RESERVE_ROOT);
			}
			break;
		case Opt_resuid:
			if (args->from && match_int(args, &arg))
				return -EINVAL;
			uid = make_kuid(current_user_ns(), arg);
			if (!uid_valid(uid)) {
				f2fs_err(sbi, "Invalid uid value %d", arg);
				return -EINVAL;
			}
			F2FS_OPTION(sbi).s_resuid = uid;
			break;
		case Opt_resgid:
			if (args->from && match_int(args, &arg))
				return -EINVAL;
			gid = make_kgid(current_user_ns(), arg);
			if (!gid_valid(gid)) {
				f2fs_err(sbi, "Invalid gid value %d", arg);
				return -EINVAL;
			}
			F2FS_OPTION(sbi).s_resgid = gid;
			break;
		case Opt_mode:
			name = match_strdup(&args[0]);

			if (!name)
				return -ENOMEM;
			if (!strcmp(name, "adaptive")) {
				if (f2fs_sb_has_blkzoned(sbi)) {
					f2fs_warn(sbi, "adaptive mode is not allowed with zoned block device feature");
					kfree(name);
					return -EINVAL;
				}
				F2FS_OPTION(sbi).fs_mode = FS_MODE_ADAPTIVE;
			} else if (!strcmp(name, "lfs")) {
				F2FS_OPTION(sbi).fs_mode = FS_MODE_LFS;
			} else {
				kfree(name);
				return -EINVAL;
			}
			kfree(name);
			break;
		case Opt_io_size_bits:
			if (args->from && match_int(args, &arg))
				return -EINVAL;
			if (arg <= 0 || arg > __ilog2_u32(BIO_MAX_VECS)) {
				f2fs_warn(sbi, "Not support %d, larger than %d",
					  1 << arg, BIO_MAX_VECS);
				return -EINVAL;
			}
			F2FS_OPTION(sbi).write_io_size_bits = arg;
			break;
#ifdef CONFIG_F2FS_FAULT_INJECTION
		case Opt_fault_injection:
			if (args->from && match_int(args, &arg))
				return -EINVAL;
			f2fs_build_fault_attr(sbi, arg, F2FS_ALL_FAULT_TYPE);
			set_opt(sbi, FAULT_INJECTION);
			break;

		case Opt_fault_type:
			if (args->from && match_int(args, &arg))
				return -EINVAL;
			f2fs_build_fault_attr(sbi, 0, arg);
			set_opt(sbi, FAULT_INJECTION);
			break;
#else
		case Opt_fault_injection:
			f2fs_info(sbi, "fault_injection options not supported");
			break;

		case Opt_fault_type:
			f2fs_info(sbi, "fault_type options not supported");
			break;
#endif
		case Opt_lazytime:
			sb->s_flags |= SB_LAZYTIME;
			break;
		case Opt_nolazytime:
			sb->s_flags &= ~SB_LAZYTIME;
			break;
#ifdef CONFIG_QUOTA
		case Opt_quota:
		case Opt_usrquota:
			set_opt(sbi, USRQUOTA);
			break;
		case Opt_grpquota:
			set_opt(sbi, GRPQUOTA);
			break;
		case Opt_prjquota:
			set_opt(sbi, PRJQUOTA);
			break;
		case Opt_usrjquota:
			ret = f2fs_set_qf_name(sb, USRQUOTA, &args[0]);
			if (ret)
				return ret;
			break;
		case Opt_grpjquota:
			ret = f2fs_set_qf_name(sb, GRPQUOTA, &args[0]);
			if (ret)
				return ret;
			break;
		case Opt_prjjquota:
			ret = f2fs_set_qf_name(sb, PRJQUOTA, &args[0]);
			if (ret)
				return ret;
			break;
		case Opt_offusrjquota:
			ret = f2fs_clear_qf_name(sb, USRQUOTA);
			if (ret)
				return ret;
			break;
		case Opt_offgrpjquota:
			ret = f2fs_clear_qf_name(sb, GRPQUOTA);
			if (ret)
				return ret;
			break;
		case Opt_offprjjquota:
			ret = f2fs_clear_qf_name(sb, PRJQUOTA);
			if (ret)
				return ret;
			break;
		case Opt_jqfmt_vfsold:
			F2FS_OPTION(sbi).s_jquota_fmt = QFMT_VFS_OLD;
			break;
		case Opt_jqfmt_vfsv0:
			F2FS_OPTION(sbi).s_jquota_fmt = QFMT_VFS_V0;
			break;
		case Opt_jqfmt_vfsv1:
			F2FS_OPTION(sbi).s_jquota_fmt = QFMT_VFS_V1;
			break;
		case Opt_noquota:
			clear_opt(sbi, QUOTA);
			clear_opt(sbi, USRQUOTA);
			clear_opt(sbi, GRPQUOTA);
			clear_opt(sbi, PRJQUOTA);
			break;
#else
		case Opt_quota:
		case Opt_usrquota:
		case Opt_grpquota:
		case Opt_prjquota:
		case Opt_usrjquota:
		case Opt_grpjquota:
		case Opt_prjjquota:
		case Opt_offusrjquota:
		case Opt_offgrpjquota:
		case Opt_offprjjquota:
		case Opt_jqfmt_vfsold:
		case Opt_jqfmt_vfsv0:
		case Opt_jqfmt_vfsv1:
		case Opt_noquota:
			f2fs_info(sbi, "quota operations not supported");
			break;
#endif
		case Opt_whint:
			name = match_strdup(&args[0]);
			if (!name)
				return -ENOMEM;
			if (!strcmp(name, "user-based")) {
				F2FS_OPTION(sbi).whint_mode = WHINT_MODE_USER;
			} else if (!strcmp(name, "off")) {
				F2FS_OPTION(sbi).whint_mode = WHINT_MODE_OFF;
			} else if (!strcmp(name, "fs-based")) {
				F2FS_OPTION(sbi).whint_mode = WHINT_MODE_FS;
			} else {
				kfree(name);
				return -EINVAL;
			}
			kfree(name);
			break;
		case Opt_alloc:
			name = match_strdup(&args[0]);
			if (!name)
				return -ENOMEM;

			if (!strcmp(name, "default")) {
				F2FS_OPTION(sbi).alloc_mode = ALLOC_MODE_DEFAULT;
			} else if (!strcmp(name, "reuse")) {
				F2FS_OPTION(sbi).alloc_mode = ALLOC_MODE_REUSE;
			} else {
				kfree(name);
				return -EINVAL;
			}
			kfree(name);
			break;
		case Opt_fsync:
			name = match_strdup(&args[0]);
			if (!name)
				return -ENOMEM;
			if (!strcmp(name, "posix")) {
				F2FS_OPTION(sbi).fsync_mode = FSYNC_MODE_POSIX;
			} else if (!strcmp(name, "strict")) {
				F2FS_OPTION(sbi).fsync_mode = FSYNC_MODE_STRICT;
			} else if (!strcmp(name, "nobarrier")) {
				F2FS_OPTION(sbi).fsync_mode =
							FSYNC_MODE_NOBARRIER;
			} else {
				kfree(name);
				return -EINVAL;
			}
			kfree(name);
			break;
		case Opt_test_dummy_encryption:
			ret = f2fs_set_test_dummy_encryption(sb, p, &args[0],
							     is_remount);
			if (ret)
				return ret;
			break;
		case Opt_inlinecrypt:
#ifdef CONFIG_FS_ENCRYPTION_INLINE_CRYPT
			sb->s_flags |= SB_INLINECRYPT;
#else
			f2fs_info(sbi, "inline encryption not supported");
#endif
			break;
		case Opt_checkpoint_disable_cap_perc:
			if (args->from && match_int(args, &arg))
				return -EINVAL;
			if (arg < 0 || arg > 100)
				return -EINVAL;
			F2FS_OPTION(sbi).unusable_cap_perc = arg;
			set_opt(sbi, DISABLE_CHECKPOINT);
			break;
		case Opt_checkpoint_disable_cap:
			if (args->from && match_int(args, &arg))
				return -EINVAL;
			F2FS_OPTION(sbi).unusable_cap = arg;
			set_opt(sbi, DISABLE_CHECKPOINT);
			break;
		case Opt_checkpoint_disable:
			set_opt(sbi, DISABLE_CHECKPOINT);
			break;
		case Opt_checkpoint_enable:
			clear_opt(sbi, DISABLE_CHECKPOINT);
			break;
		case Opt_checkpoint_merge:
			set_opt(sbi, MERGE_CHECKPOINT);
			break;
		case Opt_nocheckpoint_merge:
			clear_opt(sbi, MERGE_CHECKPOINT);
			break;
#ifdef CONFIG_F2FS_FS_COMPRESSION
		case Opt_compress_algorithm:
			if (!f2fs_sb_has_compression(sbi)) {
				f2fs_info(sbi, "Image doesn't support compression");
				break;
			}
			name = match_strdup(&args[0]);
			if (!name)
				return -ENOMEM;
			if (!strcmp(name, "lzo")) {
#ifdef CONFIG_F2FS_FS_LZO
				F2FS_OPTION(sbi).compress_level = 0;
				F2FS_OPTION(sbi).compress_algorithm =
								COMPRESS_LZO;
#else
				f2fs_info(sbi, "kernel doesn't support lzo compression");
#endif
			} else if (!strncmp(name, "lz4", 3)) {
#ifdef CONFIG_F2FS_FS_LZ4
				ret = f2fs_set_lz4hc_level(sbi, name);
				if (ret) {
					kfree(name);
					return -EINVAL;
				}
				F2FS_OPTION(sbi).compress_algorithm =
								COMPRESS_LZ4;
#else
				f2fs_info(sbi, "kernel doesn't support lz4 compression");
#endif
			} else if (!strncmp(name, "zstd", 4)) {
#ifdef CONFIG_F2FS_FS_ZSTD
				ret = f2fs_set_zstd_level(sbi, name);
				if (ret) {
					kfree(name);
					return -EINVAL;
				}
				F2FS_OPTION(sbi).compress_algorithm =
								COMPRESS_ZSTD;
#else
				f2fs_info(sbi, "kernel doesn't support zstd compression");
#endif
			} else if (!strcmp(name, "lzo-rle")) {
#ifdef CONFIG_F2FS_FS_LZORLE
				F2FS_OPTION(sbi).compress_level = 0;
				F2FS_OPTION(sbi).compress_algorithm =
								COMPRESS_LZORLE;
#else
				f2fs_info(sbi, "kernel doesn't support lzorle compression");
#endif
			} else {
				kfree(name);
				return -EINVAL;
			}
			kfree(name);
			break;
		case Opt_compress_log_size:
			if (!f2fs_sb_has_compression(sbi)) {
				f2fs_info(sbi, "Image doesn't support compression");
				break;
			}
			if (args->from && match_int(args, &arg))
				return -EINVAL;
			if (arg < MIN_COMPRESS_LOG_SIZE ||
				arg > MAX_COMPRESS_LOG_SIZE) {
				f2fs_err(sbi,
					"Compress cluster log size is out of range");
				return -EINVAL;
			}
			F2FS_OPTION(sbi).compress_log_size = arg;
			break;
		case Opt_compress_extension:
			if (!f2fs_sb_has_compression(sbi)) {
				f2fs_info(sbi, "Image doesn't support compression");
				break;
			}
			name = match_strdup(&args[0]);
			if (!name)
				return -ENOMEM;

			ext = F2FS_OPTION(sbi).extensions;
			ext_cnt = F2FS_OPTION(sbi).compress_ext_cnt;

			if (strlen(name) >= F2FS_EXTENSION_LEN ||
				ext_cnt >= COMPRESS_EXT_NUM) {
				f2fs_err(sbi,
					"invalid extension length/number");
				kfree(name);
				return -EINVAL;
			}

			strcpy(ext[ext_cnt], name);
			F2FS_OPTION(sbi).compress_ext_cnt++;
			kfree(name);
			break;
		case Opt_compress_chksum:
			F2FS_OPTION(sbi).compress_chksum = true;
			break;
		case Opt_compress_mode:
			name = match_strdup(&args[0]);
			if (!name)
				return -ENOMEM;
			if (!strcmp(name, "fs")) {
				F2FS_OPTION(sbi).compress_mode = COMPR_MODE_FS;
			} else if (!strcmp(name, "user")) {
				F2FS_OPTION(sbi).compress_mode = COMPR_MODE_USER;
			} else {
				kfree(name);
				return -EINVAL;
			}
			kfree(name);
			break;
#else
		case Opt_compress_algorithm:
		case Opt_compress_log_size:
		case Opt_compress_extension:
		case Opt_compress_chksum:
		case Opt_compress_mode:
			f2fs_info(sbi, "compression options not supported");
			break;
#endif
		case Opt_atgc:
			set_opt(sbi, ATGC);
			break;
		case Opt_gc_merge:
			set_opt(sbi, GC_MERGE);
			break;
		case Opt_nogc_merge:
			clear_opt(sbi, GC_MERGE);
			break;
		default:
			f2fs_err(sbi, "Unrecognized mount option \"%s\" or missing value",
				 p);
			return -EINVAL;
		}
	}
#ifdef CONFIG_QUOTA
	if (f2fs_check_quota_options(sbi))
		return -EINVAL;
#else
	if (f2fs_sb_has_quota_ino(sbi) && !sbi->f2fs_readonly()) {
		f2fs_info(sbi, "Filesystem with quota feature cannot be mounted RDWR without CONFIG_QUOTA");
		return -EINVAL;
	}
	if (f2fs_sb_has_project_quota(sbi) && !sbi->f2fs_readonly(->sb)) {
		f2fs_err(sbi, "Filesystem with project quota feature cannot be mounted RDWR without CONFIG_QUOTA");
		return -EINVAL;
	}
#endif
#ifndef CONFIG_UNICODE
	if (f2fs_sb_has_casefold(sbi)) {
		f2fs_err(sbi,
			"Filesystem with casefold feature cannot be mounted without CONFIG_UNICODE");
		return -EINVAL;
	}
#endif
	/*
	 * The BLKZONED feature indicates that the drive was formatted with
	 * zone alignment optimization. This is optional for host-aware
	 * devices, but mandatory for host-managed zoned block devices.
	 */
#ifndef CONFIG_BLK_DEV_ZONED
	if (f2fs_sb_has_blkzoned(sbi)) {
		f2fs_err(sbi, "Zoned block device support is not enabled");
		return -EINVAL;
	}
#endif

	if (F2FS_IO_SIZE_BITS(sbi) && !f2fs_lfs_mode(sbi)) {
		f2fs_err(sbi, "Should set mode=lfs with %uKB-sized IO",
			 F2FS_IO_SIZE_KB(sbi));
		return -EINVAL;
	}

	if (test_opt(sbi, INLINE_XATTR_SIZE)) {
		int min_size, max_size;

		if (!f2fs_sb_has_extra_attr(sbi) ||
			!f2fs_sb_has_flexible_inline_xattr(sbi)) {
			f2fs_err(sbi, "extra_attr or flexible_inline_xattr feature is off");
			return -EINVAL;
		}
		if (!test_opt(sbi, INLINE_XATTR)) {
			f2fs_err(sbi, "inline_xattr_size option should be set with inline_xattr option");
			return -EINVAL;
		}

		min_size = sizeof(struct f2fs_xattr_header) / sizeof(__le32);
		max_size = MAX_INLINE_XATTR_SIZE;

		if (F2FS_OPTION(sbi).inline_xattr_size < min_size ||
				F2FS_OPTION(sbi).inline_xattr_size > max_size) {
			f2fs_err(sbi, "inline xattr size is out of range: %d ~ %d",
				 min_size, max_size);
			return -EINVAL;
		}
	}

	if (test_opt(sbi, DISABLE_CHECKPOINT) && f2fs_lfs_mode(sbi)) {
		f2fs_err(sbi, "LFS not compatible with checkpoint=disable\n");
		return -EINVAL;
	}

	/* Not pass down write hints if the number of active logs is lesser
	 * than NR_CURSEG_PERSIST_TYPE.
	 */
	if (F2FS_OPTION(sbi).active_logs != NR_CURSEG_TYPE)
		F2FS_OPTION(sbi).whint_mode = WHINT_MODE_OFF;
	return 0;
}
#endif

int CF2fsFileSystem::parse_mount_options(super_block* sb, const boost::property_tree::wptree& options, bool is_mount)
{
	struct f2fs_sb_info* sbi = F2FS_SB(sb);
	const std::wstring& val = options.get<std::wstring>(L"background_gc");
	if (val == L"on")			F2FS_OPTION(sbi).bggc_mode = BGGC_MODE_ON;
	else if (val == L"off")		F2FS_OPTION(sbi).bggc_mode = BGGC_MODE_OFF;
	else if (val == L"sync")	F2FS_OPTION(sbi).bggc_mode = BGGC_MODE_SYNC;
	else return -EINVAL;

	if (options.get<int>(L"disable_roll_forward", 0))	set_opt(sbi, DISABLE_ROLL_FORWARD);
	if (options.get<int>(L"norecovery", 0))
	{
		set_opt(sbi, NORECOVERY);
		if (!sbi->f2fs_readonly()) return -EINVAL;
	}
	if (options.get<int>(L"discard", 0))				set_opt(sbi, DISCARD);
	if (options.get<int>(L"nodiscard", 0))
	{
		if (f2fs_sb_has_blkzoned(sbi))
		{
			LOG_ERROR(L"[err] discard is required for zoned block devices");
			return -EINVAL;
		}
		clear_opt(sbi, DISCARD);
	}

	std::wstring str_mode = options.get<std::wstring>(L"mode", L"");
	if ( str_mode == L"adaptive") 		F2FS_OPTION(sbi).fs_mode = FS_MODE_ADAPTIVE;
	else if (str_mode == L"lfs") 		F2FS_OPTION(sbi).fs_mode = FS_MODE_LFS;
	else { LOG_ERROR(L"[err] unkonwn mode=%s", str_mode.c_str()); return -EINVAL; }

#if 0 //<TODO>
		case Opt_noheap:
			set_opt(sbi, NOHEAP);
			break;
		case Opt_heap:
			clear_opt(sbi, NOHEAP);
			break;
#ifdef CONFIG_F2FS_FS_XATTR
		case Opt_user_xattr:
			set_opt(sbi, XATTR_USER);
			break;
		case Opt_nouser_xattr:
			clear_opt(sbi, XATTR_USER);
			break;
		case Opt_inline_xattr:
			set_opt(sbi, INLINE_XATTR);
			break;
		case Opt_noinline_xattr:
			clear_opt(sbi, INLINE_XATTR);
			break;
		case Opt_inline_xattr_size:
			if (args->from && match_int(args, &arg))
				return -EINVAL;
			set_opt(sbi, INLINE_XATTR_SIZE);
			F2FS_OPTION(sbi).inline_xattr_size = arg;
			break;
#else
		case Opt_user_xattr:
			f2fs_info(sbi, "user_xattr options not supported");
			break;
		case Opt_nouser_xattr:
			f2fs_info(sbi, "nouser_xattr options not supported");
			break;
		case Opt_inline_xattr:
			f2fs_info(sbi, "inline_xattr options not supported");
			break;
		case Opt_noinline_xattr:
			f2fs_info(sbi, "noinline_xattr options not supported");
			break;
#endif
#ifdef CONFIG_F2FS_FS_POSIX_ACL
		case Opt_acl:
			set_opt(sbi, POSIX_ACL);
			break;
		case Opt_noacl:
			clear_opt(sbi, POSIX_ACL);
			break;
#else
		case Opt_acl:
			f2fs_info(sbi, "acl options not supported");
			break;
		case Opt_noacl:
			f2fs_info(sbi, "noacl options not supported");
			break;
#endif
		case Opt_active_logs:
			if (args->from && match_int(args, &arg))
				return -EINVAL;
			if (arg != 2 && arg != 4 &&
				arg != NR_CURSEG_PERSIST_TYPE)
				return -EINVAL;
			F2FS_OPTION(sbi).active_logs = arg;
			break;
		case Opt_disable_ext_identify:
			set_opt(sbi, DISABLE_EXT_IDENTIFY);
			break;
		case Opt_inline_data:
			set_opt(sbi, INLINE_DATA);
			break;
		case Opt_inline_dentry:
			set_opt(sbi, INLINE_DENTRY);
			break;
		case Opt_noinline_dentry:
			clear_opt(sbi, INLINE_DENTRY);
			break;
		case Opt_flush_merge:
			set_opt(sbi, FLUSH_MERGE);
			break;
		case Opt_noflush_merge:
			clear_opt(sbi, FLUSH_MERGE);
			break;
		case Opt_nobarrier:
			set_opt(sbi, NOBARRIER);
			break;
		case Opt_fastboot:
			set_opt(sbi, FASTBOOT);
			break;
		case Opt_extent_cache:
			set_opt(sbi, EXTENT_CACHE);
			break;
		case Opt_noextent_cache:
			clear_opt(sbi, EXTENT_CACHE);
			break;
		case Opt_noinline_data:
			clear_opt(sbi, INLINE_DATA);
			break;
		case Opt_data_flush:
			set_opt(sbi, DATA_FLUSH);
			break;
		case Opt_reserve_root:
			if (args->from && match_int(args, &arg))
				return -EINVAL;
			if (test_opt(sbi, RESERVE_ROOT))
			{
				f2fs_info(sbi, "Preserve previous reserve_root=%u",
					F2FS_OPTION(sbi).root_reserved_blocks);
			}
			else
			{
				F2FS_OPTION(sbi).root_reserved_blocks = arg;
				set_opt(sbi, RESERVE_ROOT);
			}
			break;
		case Opt_resuid:
			if (args->from && match_int(args, &arg))
				return -EINVAL;
			uid = make_kuid(current_user_ns(), arg);
			if (!uid_valid(uid))
			{
				f2fs_err(sbi, "Invalid uid value %d", arg);
				return -EINVAL;
			}
			F2FS_OPTION(sbi).s_resuid = uid;
			break;
		case Opt_resgid:
			if (args->from && match_int(args, &arg))
				return -EINVAL;
			gid = make_kgid(current_user_ns(), arg);
			if (!gid_valid(gid))
			{
				f2fs_err(sbi, "Invalid gid value %d", arg);
				return -EINVAL;
			}
			F2FS_OPTION(sbi).s_resgid = gid;
			break;
		case Opt_mode:
			name = match_strdup(&args[0]);

			if (!name)
				return -ENOMEM;
			if (!strcmp(name, "adaptive"))
			{
				if (f2fs_sb_has_blkzoned(sbi))
				{
					f2fs_warn(sbi, "adaptive mode is not allowed with zoned block device feature");
					kfree(name);
					return -EINVAL;
				}
				F2FS_OPTION(sbi).fs_mode = FS_MODE_ADAPTIVE;
			}
			else if (!strcmp(name, "lfs"))
			{
				F2FS_OPTION(sbi).fs_mode = FS_MODE_LFS;
			}
			else
			{
				kfree(name);
				return -EINVAL;
			}
			kfree(name);
			break;
		case Opt_io_size_bits:
			if (args->from && match_int(args, &arg))
				return -EINVAL;
			if (arg <= 0 || arg > __ilog2_u32(BIO_MAX_VECS))
			{
				f2fs_warn(sbi, "Not support %d, larger than %d",
					1 << arg, BIO_MAX_VECS);
				return -EINVAL;
			}
			F2FS_OPTION(sbi).write_io_size_bits = arg;
			break;
#ifdef CONFIG_F2FS_FAULT_INJECTION
		case Opt_fault_injection:
			if (args->from && match_int(args, &arg))
				return -EINVAL;
			f2fs_build_fault_attr(sbi, arg, F2FS_ALL_FAULT_TYPE);
			set_opt(sbi, FAULT_INJECTION);
			break;

		case Opt_fault_type:
			if (args->from && match_int(args, &arg))
				return -EINVAL;
			f2fs_build_fault_attr(sbi, 0, arg);
			set_opt(sbi, FAULT_INJECTION);
			break;
#else
		case Opt_fault_injection:
			f2fs_info(sbi, "fault_injection options not supported");
			break;

		case Opt_fault_type:
			f2fs_info(sbi, "fault_type options not supported");
			break;
#endif
		case Opt_lazytime:
			sb->s_flags |= SB_LAZYTIME;
			break;
		case Opt_nolazytime:
			sb->s_flags &= ~SB_LAZYTIME;
			break;
#ifdef CONFIG_QUOTA
		case Opt_quota:
		case Opt_usrquota:
			set_opt(sbi, USRQUOTA);
			break;
		case Opt_grpquota:
			set_opt(sbi, GRPQUOTA);
			break;
		case Opt_prjquota:
			set_opt(sbi, PRJQUOTA);
			break;
		case Opt_usrjquota:
			ret = f2fs_set_qf_name(sb, USRQUOTA, &args[0]);
			if (ret)
				return ret;
			break;
		case Opt_grpjquota:
			ret = f2fs_set_qf_name(sb, GRPQUOTA, &args[0]);
			if (ret)
				return ret;
			break;
		case Opt_prjjquota:
			ret = f2fs_set_qf_name(sb, PRJQUOTA, &args[0]);
			if (ret)
				return ret;
			break;
		case Opt_offusrjquota:
			ret = f2fs_clear_qf_name(sb, USRQUOTA);
			if (ret)
				return ret;
			break;
		case Opt_offgrpjquota:
			ret = f2fs_clear_qf_name(sb, GRPQUOTA);
			if (ret)
				return ret;
			break;
		case Opt_offprjjquota:
			ret = f2fs_clear_qf_name(sb, PRJQUOTA);
			if (ret)
				return ret;
			break;
		case Opt_jqfmt_vfsold:
			F2FS_OPTION(sbi).s_jquota_fmt = QFMT_VFS_OLD;
			break;
		case Opt_jqfmt_vfsv0:
			F2FS_OPTION(sbi).s_jquota_fmt = QFMT_VFS_V0;
			break;
		case Opt_jqfmt_vfsv1:
			F2FS_OPTION(sbi).s_jquota_fmt = QFMT_VFS_V1;
			break;
		case Opt_noquota:
			clear_opt(sbi, QUOTA);
			clear_opt(sbi, USRQUOTA);
			clear_opt(sbi, GRPQUOTA);
			clear_opt(sbi, PRJQUOTA);
			break;
#else
		case Opt_quota:
		case Opt_usrquota:
		case Opt_grpquota:
		case Opt_prjquota:
		case Opt_usrjquota:
		case Opt_grpjquota:
		case Opt_prjjquota:
		case Opt_offusrjquota:
		case Opt_offgrpjquota:
		case Opt_offprjjquota:
		case Opt_jqfmt_vfsold:
		case Opt_jqfmt_vfsv0:
		case Opt_jqfmt_vfsv1:
		case Opt_noquota:
			f2fs_info(sbi, "quota operations not supported");
			break;
#endif
		case Opt_whint:
			name = match_strdup(&args[0]);
			if (!name)
				return -ENOMEM;
			if (!strcmp(name, "user-based"))
			{
				F2FS_OPTION(sbi).whint_mode = WHINT_MODE_USER;
			}
			else if (!strcmp(name, "off"))
			{
				F2FS_OPTION(sbi).whint_mode = WHINT_MODE_OFF;
			}
			else if (!strcmp(name, "fs-based"))
			{
				F2FS_OPTION(sbi).whint_mode = WHINT_MODE_FS;
			}
			else
			{
				kfree(name);
				return -EINVAL;
			}
			kfree(name);
			break;
		case Opt_alloc:
			name = match_strdup(&args[0]);
			if (!name)
				return -ENOMEM;

			if (!strcmp(name, "default"))
			{
				F2FS_OPTION(sbi).alloc_mode = ALLOC_MODE_DEFAULT;
			}
			else if (!strcmp(name, "reuse"))
			{
				F2FS_OPTION(sbi).alloc_mode = ALLOC_MODE_REUSE;
			}
			else
			{
				kfree(name);
				return -EINVAL;
			}
			kfree(name);
			break;
		case Opt_fsync:
			name = match_strdup(&args[0]);
			if (!name)
				return -ENOMEM;
			if (!strcmp(name, "posix"))
			{
				F2FS_OPTION(sbi).fsync_mode = FSYNC_MODE_POSIX;
			}
			else if (!strcmp(name, "strict"))
			{
				F2FS_OPTION(sbi).fsync_mode = FSYNC_MODE_STRICT;
			}
			else if (!strcmp(name, "nobarrier"))
			{
				F2FS_OPTION(sbi).fsync_mode =
					FSYNC_MODE_NOBARRIER;
			}
			else
			{
				kfree(name);
				return -EINVAL;
			}
			kfree(name);
			break;
		case Opt_test_dummy_encryption:
			ret = f2fs_set_test_dummy_encryption(sb, p, &args[0],
				is_remount);
			if (ret)
				return ret;
			break;
		case Opt_inlinecrypt:
#ifdef CONFIG_FS_ENCRYPTION_INLINE_CRYPT
			sb->s_flags |= SB_INLINECRYPT;
#else
			f2fs_info(sbi, "inline encryption not supported");
#endif
			break;
		case Opt_checkpoint_disable_cap_perc:
			if (args->from && match_int(args, &arg))
				return -EINVAL;
			if (arg < 0 || arg > 100)
				return -EINVAL;
			F2FS_OPTION(sbi).unusable_cap_perc = arg;
			set_opt(sbi, DISABLE_CHECKPOINT);
			break;
		case Opt_checkpoint_disable_cap:
			if (args->from && match_int(args, &arg))
				return -EINVAL;
			F2FS_OPTION(sbi).unusable_cap = arg;
			set_opt(sbi, DISABLE_CHECKPOINT);
			break;
		case Opt_checkpoint_disable:
			set_opt(sbi, DISABLE_CHECKPOINT);
			break;
		case Opt_checkpoint_enable:
			clear_opt(sbi, DISABLE_CHECKPOINT);
			break;
		case Opt_checkpoint_merge:
			set_opt(sbi, MERGE_CHECKPOINT);
			break;
		case Opt_nocheckpoint_merge:
			clear_opt(sbi, MERGE_CHECKPOINT);
			break;
#ifdef CONFIG_F2FS_FS_COMPRESSION
		case Opt_compress_algorithm:
			if (!f2fs_sb_has_compression(sbi))
			{
				f2fs_info(sbi, "Image doesn't support compression");
				break;
			}
			name = match_strdup(&args[0]);
			if (!name)
				return -ENOMEM;
			if (!strcmp(name, "lzo"))
			{
#ifdef CONFIG_F2FS_FS_LZO
				F2FS_OPTION(sbi).compress_level = 0;
				F2FS_OPTION(sbi).compress_algorithm =
					COMPRESS_LZO;
#else
				f2fs_info(sbi, "kernel doesn't support lzo compression");
#endif
			}
			else if (!strncmp(name, "lz4", 3))
			{
#ifdef CONFIG_F2FS_FS_LZ4
				ret = f2fs_set_lz4hc_level(sbi, name);
				if (ret)
				{
					kfree(name);
					return -EINVAL;
				}
				F2FS_OPTION(sbi).compress_algorithm =
					COMPRESS_LZ4;
#else
				f2fs_info(sbi, "kernel doesn't support lz4 compression");
#endif
			}
			else if (!strncmp(name, "zstd", 4))
			{
#ifdef CONFIG_F2FS_FS_ZSTD
				ret = f2fs_set_zstd_level(sbi, name);
				if (ret)
				{
					kfree(name);
					return -EINVAL;
				}
				F2FS_OPTION(sbi).compress_algorithm =
					COMPRESS_ZSTD;
#else
				f2fs_info(sbi, "kernel doesn't support zstd compression");
#endif
			}
			else if (!strcmp(name, "lzo-rle"))
			{
#ifdef CONFIG_F2FS_FS_LZORLE
				F2FS_OPTION(sbi).compress_level = 0;
				F2FS_OPTION(sbi).compress_algorithm =
					COMPRESS_LZORLE;
#else
				f2fs_info(sbi, "kernel doesn't support lzorle compression");
#endif
			}
			else
			{
				kfree(name);
				return -EINVAL;
			}
			kfree(name);
			break;
		case Opt_compress_log_size:
			if (!f2fs_sb_has_compression(sbi))
			{
				f2fs_info(sbi, "Image doesn't support compression");
				break;
			}
			if (args->from && match_int(args, &arg))
				return -EINVAL;
			if (arg < MIN_COMPRESS_LOG_SIZE ||
				arg > MAX_COMPRESS_LOG_SIZE)
			{
				f2fs_err(sbi,
					"Compress cluster log size is out of range");
				return -EINVAL;
			}
			F2FS_OPTION(sbi).compress_log_size = arg;
			break;
		case Opt_compress_extension:
			if (!f2fs_sb_has_compression(sbi))
			{
				f2fs_info(sbi, "Image doesn't support compression");
				break;
			}
			name = match_strdup(&args[0]);
			if (!name)
				return -ENOMEM;

			ext = F2FS_OPTION(sbi).extensions;
			ext_cnt = F2FS_OPTION(sbi).compress_ext_cnt;

			if (strlen(name) >= F2FS_EXTENSION_LEN ||
				ext_cnt >= COMPRESS_EXT_NUM)
			{
				f2fs_err(sbi,
					"invalid extension length/number");
				kfree(name);
				return -EINVAL;
			}

			strcpy(ext[ext_cnt], name);
			F2FS_OPTION(sbi).compress_ext_cnt++;
			kfree(name);
			break;
		case Opt_compress_chksum:
			F2FS_OPTION(sbi).compress_chksum = true;
			break;
		case Opt_compress_mode:
			name = match_strdup(&args[0]);
			if (!name)
				return -ENOMEM;
			if (!strcmp(name, "fs"))
			{
				F2FS_OPTION(sbi).compress_mode = COMPR_MODE_FS;
			}
			else if (!strcmp(name, "user"))
			{
				F2FS_OPTION(sbi).compress_mode = COMPR_MODE_USER;
			}
			else
			{
				kfree(name);
				return -EINVAL;
			}
			kfree(name);
			break;
#else
		case Opt_compress_algorithm:
		case Opt_compress_log_size:
		case Opt_compress_extension:
		case Opt_compress_chksum:
		case Opt_compress_mode:
			f2fs_info(sbi, "compression options not supported");
			break;
#endif
		case Opt_atgc:
			set_opt(sbi, ATGC);
			break;
		case Opt_gc_merge:
			set_opt(sbi, GC_MERGE);
			break;
		case Opt_nogc_merge:
			clear_opt(sbi, GC_MERGE);
			break;
		default:
			f2fs_err(sbi, "Unrecognized mount option \"%s\" or missing value",
				p);
			return -EINVAL;
}
	}
#ifdef CONFIG_QUOTA
	if (f2fs_check_quota_options(sbi))
		return -EINVAL;
#else
	if (f2fs_sb_has_quota_ino(sbi) && !sbi->f2fs_readonly())
	{
		f2fs_info(sbi, "Filesystem with quota feature cannot be mounted RDWR without CONFIG_QUOTA");
		return -EINVAL;
	}
	if (f2fs_sb_has_project_quota(sbi) && !sbi->f2fs_readonly(->sb))
	{
		f2fs_err(sbi, "Filesystem with project quota feature cannot be mounted RDWR without CONFIG_QUOTA");
		return -EINVAL;
	}
#endif
#ifndef CONFIG_UNICODE
	if (f2fs_sb_has_casefold(sbi))
	{
		f2fs_err(sbi,
			"Filesystem with casefold feature cannot be mounted without CONFIG_UNICODE");
		return -EINVAL;
	}
#endif
	/*
	 * The BLKZONED feature indicates that the drive was formatted with
	 * zone alignment optimization. This is optional for host-aware
	 * devices, but mandatory for host-managed zoned block devices.
	 */
#ifndef CONFIG_BLK_DEV_ZONED
	if (f2fs_sb_has_blkzoned(sbi))
	{
		f2fs_err(sbi, "Zoned block device support is not enabled");
		return -EINVAL;
	}
#endif

	if (F2FS_IO_SIZE_BITS(sbi) && !f2fs_lfs_mode(sbi))
	{
		f2fs_err(sbi, "Should set mode=lfs with %uKB-sized IO",
			F2FS_IO_SIZE_KB(sbi));
		return -EINVAL;
	}

	if (test_opt(sbi, INLINE_XATTR_SIZE))
	{
		int min_size, max_size;

		if (!f2fs_sb_has_extra_attr(sbi) ||
			!f2fs_sb_has_flexible_inline_xattr(sbi))
		{
			f2fs_err(sbi, "extra_attr or flexible_inline_xattr feature is off");
			return -EINVAL;
		}
		if (!test_opt(sbi, INLINE_XATTR))
		{
			f2fs_err(sbi, "inline_xattr_size option should be set with inline_xattr option");
			return -EINVAL;
		}

		min_size = sizeof(struct f2fs_xattr_header) / sizeof(__le32);
		max_size = MAX_INLINE_XATTR_SIZE;

		if (F2FS_OPTION(sbi).inline_xattr_size < min_size ||
			F2FS_OPTION(sbi).inline_xattr_size > max_size)
		{
			f2fs_err(sbi, "inline xattr size is out of range: %d ~ %d",
				min_size, max_size);
			return -EINVAL;
		}
	}

	if (test_opt(sbi, DISABLE_CHECKPOINT) && f2fs_lfs_mode(sbi))
	{
		f2fs_err(sbi, "LFS not compatible with checkpoint=disable\n");
		return -EINVAL;
	}

	/* Not pass down write hints if the number of active logs is lesser
	 * than NR_CURSEG_PERSIST_TYPE.
	 */
	if (F2FS_OPTION(sbi).active_logs != NR_CURSEG_TYPE)
		F2FS_OPTION(sbi).whint_mode = WHINT_MODE_OFF;

#endif


	return 0;
}


#if 0
static struct inode *f2fs_alloc_inode(struct super_block *sb)
{
	f2fs_inode_info *fi;

//	fi = kmem_cache_alloc(f2fs_inode_cachep, GFP_F2FS_ZERO);
	fi = new f2fs_inode_info;
	if (!fi)		return NULL;

	init_once((void *) fi);

	/* Initialize f2fs-specific inode info */
	atomic_set(&fi->dirty_pages, 0);
	atomic_set(&fi->i_compr_blocks, 0);
	init_rwsem(&fi->i_sem);
	spin_lock_init(&fi->i_size_lock);
	INIT_LIST_HEAD(&fi->dirty_list);
	INIT_LIST_HEAD(&fi->gdirty_list);
	INIT_LIST_HEAD(&fi->inmem_ilist);
	INIT_LIST_HEAD(&fi->inmem_pages);
	mutex_init(&fi->inmem_lock);
	init_rwsem(&fi->i_gc_rwsem[READ]);
	init_rwsem(&fi->i_gc_rwsem[WRITE]);
	init_rwsem(&fi->i_mmap_sem);
	init_rwsem(&fi->i_xattr_sem);

	/* Will be used by directory only */
	fi->i_dir_level = F2FS_SB(sb)->dir_level;

	return &fi->vfs_inode;
}

static int f2fs_drop_inode(struct inode *inode)
{
	struct f2fs_sb_info *sbi = F2FS_I_SB(inode);
	int ret;

	/*
	 * during filesystem shutdown, if checkpoint is disabled,
	 * drop useless meta/node dirty pages.
	 */
	if (unlikely(sbi->is_sbi_flag_set( SBI_CP_DISABLED))) {
		if (inode->i_ino == F2FS_NODE_INO(sbi) ||
			inode->i_ino == F2FS_META_INO(sbi)) {
			trace_f2fs_drop_inode(inode, 1);
			return 1;
		}
	}

	/*
	 * This is to avoid a deadlock condition like below.
	 * writeback_single_inode(inode)
	 *  - f2fs_write_data_page
	 *    - f2fs_gc -> iput -> evict
	 *       - inode_wait_for_writeback(inode)
	 */
	if ((!inode_unhashed(inode) && inode->i_state & I_SYNC)) {
		if (!inode->i_nlink && !is_bad_inode(inode)) {
			/* to avoid evict_inode call simultaneously */
			atomic_inc(&inode->i_count);
			spin_unlock(&inode->i_lock);

			/* some remained atomic pages should discarded */
			if (f2fs_is_atomic_file(inode))
				f2fs_drop_inmem_pages(inode);

			/* should remain fi->extent_tree for writepage */
			f2fs_destroy_extent_node(inode);

			sb_start_intwrite(inode->i_sb);
			f2fs_i_size_write(inode, 0);

			f2fs_submit_merged_write_cond(F2FS_I_SB(inode),
					inode, NULL, 0, DATA);
			truncate_inode_pages_final(inode->i_mapping);

			if (F2FS_HAS_BLOCKS(inode))
				f2fs_truncate(inode);

			sb_end_intwrite(inode->i_sb);

			spin_lock(&inode->i_lock);
			atomic_dec(&inode->i_count);
		}
		trace_f2fs_drop_inode(inode, 0);
		return 0;
	}
	ret = generic_drop_inode(inode);
	if (!ret)
		ret = fscrypt_drop_inode(inode);
	trace_f2fs_drop_inode(inode, ret);
	return ret;
}
#endif

int f2fs_inode_dirtied(inode *iinode, bool sync)
{
	LOG_STACK_TRACE()
	f2fs_inode_info* finode = F2FS_I(iinode);
	f2fs_sb_info *sbi = F2FS_I_SB(iinode);
	int ret = 0;

	spin_lock(&sbi->inode_lock[DIRTY_META]);
	if (is_inode_flag_set(iinode, FI_DIRTY_INODE)) {	ret = 1;}
	else
	{
		F2FS_I(iinode)->set_inode_flag(FI_DIRTY_INODE);
		stat_inc_dirty_inode(sbi, DIRTY_META);
	}
//	if (list_empty(&F2FS_I(iinode)->gdirty_list))
	if (sync && !finode->m_in_list[DIRTY_META])
	{
		LOG_DEBUG(L"add inode %d to DIRTY_META list", finode->i_ino);
//		list_add_tail(&F2FS_I(iinode)->gdirty_list, &sbi->inode_list[DIRTY_META]);
		sbi->list_add_tail(finode, DIRTY_META);
		sbi->inc_page_count(F2FS_DIRTY_IMETA);
	}
	spin_unlock(&sbi->inode_lock[DIRTY_META]);
	return ret;
}

void f2fs_inode_synced(inode *iinode)
{
	f2fs_inode_info* finode = F2FS_I(iinode);
	f2fs_sb_info *sbi = F2FS_I_SB(iinode);

	auto_lock<spin_locker> locker(sbi->inode_lock[DIRTY_META]);
//	spin_lock(&sbi->inode_lock[DIRTY_META]);
	if (!is_inode_flag_set(iinode, FI_DIRTY_INODE))
	{
//		spin_unlock(&sbi->inode_lock[DIRTY_META]);
		return;
	}
//	if (!list_empty(&F2FS_I(iinode)->gdirty_list))
	if (!sbi->list_empty(DIRTY_META) && finode->m_in_list[DIRTY_META])
	{
//		list_del_init(&F2FS_I(iinode)->gdirty_list);
		sbi->list_del_init(finode, DIRTY_META);
		sbi->dec_page_count(F2FS_DIRTY_IMETA);
	}
	clear_inode_flag(F2FS_I(iinode), FI_DIRTY_INODE);
	clear_inode_flag(F2FS_I(iinode), FI_AUTO_RECOVER);
	stat_dec_dirty_inode(sbi, DIRTY_META);
//	spin_unlock(&sbi->inode_lock[DIRTY_META]);
}


/* f2fs_dirty_inode() is called from __mark_inode_dirty()
 * We should call set_dirty_inode to write the dirty inode through write_inode. */
//static void f2fs_dirty_inode(struct inode *inode, int flags)
void f2fs_sb_info::dirty_inode(struct inode* iinode, int flags)
{
//	struct f2fs_sb_info *sbi = F2FS_I_SB(inode);

	if (iinode->i_ino == F2FS_NODE_INO(this) || iinode->i_ino == F2FS_META_INO(this))	return;

	if (is_inode_flag_set(iinode, FI_AUTO_RECOVER))		clear_inode_flag(F2FS_I(iinode), FI_AUTO_RECOVER);

	f2fs_inode_dirtied(iinode, false);
}
//int f2fs_sb_info::sync_fs(int wait)
//{
//	return 0;
//}

//static void f2fs_free_inode(struct inode *inode)
void f2fs_sb_info::free_inode(inode *iinode)
{
	fscrypt_free_inode(iinode);
	kmem_cache_free(/*f2fs_inode_cachep*/NULL, F2FS_I(iinode));
}
#if 0 //TODO

static void destroy_percpu_info(struct f2fs_sb_info *sbi)
{
	percpu_counter_destroy(&sbi->alloc_valid_block_count);
	percpu_counter_destroy(&sbi->total_valid_inode_count);
}



static void f2fs_put_super(struct super_block *sb)
{
	struct f2fs_sb_info *sbi = F2FS_SB(sb);
	int i;
	bool dropped;

	/* unregister procfs/sysfs entries in advance to avoid race case */
	f2fs_unregister_sysfs(sbi);

	f2fs_quota_off_umount(sb);

	/* prevent remaining shrinker jobs */
	mutex_lock(&sbi->umount_mutex);

	/*
	 * flush all issued checkpoints and stop checkpoint issue thread.
	 * after then, all checkpoints should be done by each process context.
	 */
	f2fs_stop_ckpt_thread(sbi);

	/*
	 * We don't need to do checkpoint when superblock is clean.
	 * But, the previous checkpoint was not done by umount, it needs to do
	 * clean checkpoint again.
	 */
	if ((sbi->is_sbi_flag_set( SBI_IS_DIRTY) ||
			!sbi->is_set_ckpt_flags( CP_UMOUNT_FLAG))) {
		struct cp_control cpc = {
			.reason = CP_UMOUNT,
		};
		sbi->f2fs_write_checkpoint( &cpc);
	}

	/* be sure to wait for any on-going discard commands */
	dropped = f2fs_issue_discard_timeout(sbi);

	if ((f2fs_hw_support_discard(sbi) || f2fs_hw_should_discard(sbi)) &&
					!sbi->discard_blks && !dropped) {
		struct cp_control cpc = {
			.reason = CP_UMOUNT | CP_TRIMMED,
		};
		sbi->f2fs_write_checkpoint( &cpc);
	}

	/*
	 * normally superblock is clean, so we need to release this.
	 * In addition, EIO will skip do checkpoint, we need this as well.
	 */
	f2fs_release_ino_entry(sbi, true);

	f2fs_leave_shrinker(sbi);
	mutex_unlock(&sbi->umount_mutex);

	/* our cp_error case, we can wait for any writeback page */
	f2fs_flush_merged_writes(sbi);

	f2fs_wait_on_all_pages(sbi, F2FS_WB_CP_DATA);

	f2fs_bug_on(sbi, sbi->fsync_node_num);

	iput(sbi->node_inode);
	sbi->node_inode = NULL;

	iput(sbi->meta_inode);
	sbi->meta_inode = NULL;

	/*
	 * iput() can update stat information, if f2fs_write_checkpoint()
	 * above failed with error.
	 */
	f2fs_destroy_stats(sbi);

	/* destroy f2fs internal modules */
	f2fs_destroy_node_manager(sbi);
	f2fs_destroy_segment_manager(sbi);

	f2fs_destroy_post_read_wq(sbi);

	kvfree(sbi->ckpt);

	sb->s_fs_info = NULL;
	if (sbi->s_chksum_driver)
		crypto_free_shash(sbi->s_chksum_driver);
	kfree(sbi->raw_super);

	destroy_device_list(sbi);
	f2fs_destroy_page_array_cache(sbi);
	f2fs_destroy_xattr_caches(sbi);
	mempool_destroy(sbi->write_io_dummy);
#ifdef CONFIG_QUOTA
	for (i = 0; i < MAXQUOTAS; i++)
		kfree(F2FS_OPTION(sbi).s_qf_names[i]);
#endif
	fscrypt_free_dummy_policy(&F2FS_OPTION(sbi).dummy_enc_policy);
	destroy_percpu_info(sbi);
	for (i = 0; i < NR_PAGE_TYPE; i++)
		kvfree(sbi->write_io[i]);
#ifdef CONFIG_UNICODE
	utf8_unload(sb->s_encoding);
#endif
	kfree(sbi);
}
#endif

//int f2fs_sync_fs(struct super_block *sb, int sync)
int f2fs_sb_info::sync_fs(int sync)
{
//	struct f2fs_sb_info *sbi = F2FS_SB(sb);
	int err = 0;
	if (unlikely(f2fs_cp_error()))		return 0;
	if (unlikely(is_sbi_flag_set( SBI_CP_DISABLED)))		return 0;
//	trace_f2fs_sync_fs(sb, sync);
	if (unlikely(is_sbi_flag_set( SBI_POR_DOING)))		return -EAGAIN;
	if (sync)		err = f2fs_issue_checkpoint(this);
	return err;
}

#if 0 //TODO

static int f2fs_freeze(struct super_block *sb)
{
	if (f2fs_readonly(sb))
		return 0;

	/* IO error happened before */
	if (unlikely(f2fs_cp_error(F2FS_SB(sb))))
		return -EIO;

	/* must be clean, since sync_filesystem() was already called */
	if (is_sbi_flag_set(F2FS_SB(sb), SBI_IS_DIRTY))
		return -EINVAL;

	/* ensure no checkpoint required */
	if (!llist_empty(&F2FS_SB(sb)->cprc_info.issue_list))
		return -EINVAL;
	return 0;
}

static int f2fs_unfreeze(struct super_block *sb)
{
	return 0;
}

#ifdef CONFIG_QUOTA
static int f2fs_statfs_project(struct super_block *sb,
				kprojid_t projid, struct kstatfs *buf)
{
	struct kqid qid;
	struct dquot *dquot;
	u64 limit;
	u64 curblock;

	qid = make_kqid_projid(projid);
	dquot = dqget(sb, qid);
	if (IS_ERR(dquot))
		return PTR_ERR(dquot);
	spin_lock(&dquot->dq_dqb_lock);

	limit = min_not_zero(dquot->dq_dqb.dqb_bsoftlimit,
					dquot->dq_dqb.dqb_bhardlimit);
	if (limit)
		limit >>= sb->s_blocksize_bits;

	if (limit && buf->f_blocks > limit) {
		curblock = (dquot->dq_dqb.dqb_curspace +
			    dquot->dq_dqb.dqb_rsvspace) >> sb->s_blocksize_bits;
		buf->f_blocks = limit;
		buf->f_bfree = buf->f_bavail =
			(buf->f_blocks > curblock) ?
			 (buf->f_blocks - curblock) : 0;
	}

	limit = min_not_zero(dquot->dq_dqb.dqb_isoftlimit,
					dquot->dq_dqb.dqb_ihardlimit);

	if (limit && buf->f_files > limit) {
		buf->f_files = limit;
		buf->f_ffree =
			(buf->f_files > dquot->dq_dqb.dqb_curinodes) ?
			 (buf->f_files - dquot->dq_dqb.dqb_curinodes) : 0;
	}

	spin_unlock(&dquot->dq_dqb_lock);
	dqput(dquot);
	return 0;
}
#endif

static int f2fs_statfs(struct dentry *dentry, struct kstatfs *buf)
{
	struct super_block *sb = dentry->d_sb;
	struct f2fs_sb_info *sbi = F2FS_SB(sb);
	u64 id = huge_encode_dev(sb->s_bdev->bd_dev);
	block_t total_count, user_block_count, start_count;
	u64 avail_node_count;

	total_count = le64_to_cpu(sbi->raw_super->block_count);
	user_block_count = sbi->user_block_count;
	start_count = le32_to_cpu(sbi->raw_super->segment0_blkaddr);
	buf->f_type = F2FS_SUPER_MAGIC;
	buf->f_bsize = sbi->blocksize;

	buf->f_blocks = total_count - start_count;
	buf->f_bfree = user_block_count - sbi->valid_user_blocks() -
						sbi->current_reserved_blocks;

	spin_lock(&sbi->stat_lock);
	if (unlikely(buf->f_bfree <= sbi->unusable_block_count))
		buf->f_bfree = 0;
	else
		buf->f_bfree -= sbi->unusable_block_count;
	spin_unlock(&sbi->stat_lock);

	if (buf->f_bfree > F2FS_OPTION(sbi).root_reserved_blocks)
		buf->f_bavail = buf->f_bfree -
				F2FS_OPTION(sbi).root_reserved_blocks;
	else
		buf->f_bavail = 0;

	avail_node_count = sbi->total_node_count - F2FS_RESERVED_NODE_NUM;

	if (avail_node_count > user_block_count) {
		buf->f_files = user_block_count;
		buf->f_ffree = buf->f_bavail;
	} else {
		buf->f_files = avail_node_count;
		buf->f_ffree = min(avail_node_count - sbi->valid_node_count(),
					buf->f_bavail);
	}

	buf->f_namelen = F2FS_NAME_LEN;
	buf->f_fsid    = u64_to_fsid(id);

#ifdef CONFIG_QUOTA
	if (is_inode_flag_set(dentry->d_inode, FI_PROJ_INHERIT) &&
			sb_has_quota_limits_enabled(sb, PRJQUOTA)) {
		f2fs_statfs_project(sb, F2FS_I(dentry->d_inode)->i_projid, buf);
	}
#endif
	return 0;
}

static inline void f2fs_show_quota_options(struct seq_file *seq,
					   struct super_block *sb)
{
#ifdef CONFIG_QUOTA
	struct f2fs_sb_info *sbi = F2FS_SB(sb);

	if (F2FS_OPTION(sbi).s_jquota_fmt) {
		char *fmtname = "";

		switch (F2FS_OPTION(sbi).s_jquota_fmt) {
		case QFMT_VFS_OLD:
			fmtname = "vfsold";
			break;
		case QFMT_VFS_V0:
			fmtname = "vfsv0";
			break;
		case QFMT_VFS_V1:
			fmtname = "vfsv1";
			break;
		}
		seq_printf(seq, ",jqfmt=%s", fmtname);
	}

	if (F2FS_OPTION(sbi).s_qf_names[USRQUOTA])
		seq_show_option(seq, "usrjquota",
			F2FS_OPTION(sbi).s_qf_names[USRQUOTA]);

	if (F2FS_OPTION(sbi).s_qf_names[GRPQUOTA])
		seq_show_option(seq, "grpjquota",
			F2FS_OPTION(sbi).s_qf_names[GRPQUOTA]);

	if (F2FS_OPTION(sbi).s_qf_names[PRJQUOTA])
		seq_show_option(seq, "prjjquota",
			F2FS_OPTION(sbi).s_qf_names[PRJQUOTA]);
#endif
}

#ifdef CONFIG_F2FS_FS_COMPRESSION
static inline void f2fs_show_compress_options(struct seq_file *seq,
							struct super_block *sb)
{
	struct f2fs_sb_info *sbi = F2FS_SB(sb);
	char *algtype = "";
	int i;

	if (!f2fs_sb_has_compression(sbi))
		return;

	switch (F2FS_OPTION(sbi).compress_algorithm) {
	case COMPRESS_LZO:
		algtype = "lzo";
		break;
	case COMPRESS_LZ4:
		algtype = "lz4";
		break;
	case COMPRESS_ZSTD:
		algtype = "zstd";
		break;
	case COMPRESS_LZORLE:
		algtype = "lzo-rle";
		break;
	}
	seq_printf(seq, ",compress_algorithm=%s", algtype);

	if (F2FS_OPTION(sbi).compress_level)
		seq_printf(seq, ":%d", F2FS_OPTION(sbi).compress_level);

	seq_printf(seq, ",compress_log_size=%u",
			F2FS_OPTION(sbi).compress_log_size);

	for (i = 0; i < F2FS_OPTION(sbi).compress_ext_cnt; i++) {
		seq_printf(seq, ",compress_extension=%s",
			F2FS_OPTION(sbi).extensions[i]);
	}

	if (F2FS_OPTION(sbi).compress_chksum)
		seq_puts(seq, ",compress_chksum");

	if (F2FS_OPTION(sbi).compress_mode == COMPR_MODE_FS)
		seq_printf(seq, ",compress_mode=%s", "fs");
	else if (F2FS_OPTION(sbi).compress_mode == COMPR_MODE_USER)
		seq_printf(seq, ",compress_mode=%s", "user");
}
#endif

static int f2fs_show_options(struct seq_file *seq, struct dentry *root)
{
	struct f2fs_sb_info *sbi = F2FS_SB(root->d_sb);

	if (F2FS_OPTION(sbi).bggc_mode == BGGC_MODE_SYNC)
		seq_printf(seq, ",background_gc=%s", "sync");
	else if (F2FS_OPTION(sbi).bggc_mode == BGGC_MODE_ON)
		seq_printf(seq, ",background_gc=%s", "on");
	else if (F2FS_OPTION(sbi).bggc_mode == BGGC_MODE_OFF)
		seq_printf(seq, ",background_gc=%s", "off");

	if (test_opt(sbi, GC_MERGE))
		seq_puts(seq, ",gc_merge");

	if (test_opt(sbi, DISABLE_ROLL_FORWARD))
		seq_puts(seq, ",disable_roll_forward");
	if (test_opt(sbi, NORECOVERY))
		seq_puts(seq, ",norecovery");
	if (test_opt(sbi, DISCARD))
		seq_puts(seq, ",discard");
	else
		seq_puts(seq, ",nodiscard");
	if (test_opt(sbi, NOHEAP))
		seq_puts(seq, ",no_heap");
	else
		seq_puts(seq, ",heap");
#ifdef CONFIG_F2FS_FS_XATTR
	if (test_opt(sbi, XATTR_USER))
		seq_puts(seq, ",user_xattr");
	else
		seq_puts(seq, ",nouser_xattr");
	if (test_opt(sbi, INLINE_XATTR))
		seq_puts(seq, ",inline_xattr");
	else
		seq_puts(seq, ",noinline_xattr");
	if (test_opt(sbi, INLINE_XATTR_SIZE))
		seq_printf(seq, ",inline_xattr_size=%u",
					F2FS_OPTION(sbi).inline_xattr_size);
#endif
#ifdef CONFIG_F2FS_FS_POSIX_ACL
	if (test_opt(sbi, POSIX_ACL))
		seq_puts(seq, ",acl");
	else
		seq_puts(seq, ",noacl");
#endif
	if (test_opt(sbi, DISABLE_EXT_IDENTIFY))
		seq_puts(seq, ",disable_ext_identify");
	if (test_opt(sbi, INLINE_DATA))
		seq_puts(seq, ",inline_data");
	else
		seq_puts(seq, ",noinline_data");
	if (test_opt(sbi, INLINE_DENTRY))
		seq_puts(seq, ",inline_dentry");
	else
		seq_puts(seq, ",noinline_dentry");
	if (!sbi->f2fs_readonly() && test_opt(sbi, FLUSH_MERGE))
		seq_puts(seq, ",flush_merge");
	if (test_opt(sbi, NOBARRIER))
		seq_puts(seq, ",nobarrier");
	if (test_opt(sbi, FASTBOOT))
		seq_puts(seq, ",fastboot");
	if (test_opt(sbi, EXTENT_CACHE))
		seq_puts(seq, ",extent_cache");
	else
		seq_puts(seq, ",noextent_cache");
	if (test_opt(sbi, DATA_FLUSH))
		seq_puts(seq, ",data_flush");

	seq_puts(seq, ",mode=");
	if (F2FS_OPTION(sbi).fs_mode == FS_MODE_ADAPTIVE)
		seq_puts(seq, "adaptive");
	else if (F2FS_OPTION(sbi).fs_mode == FS_MODE_LFS)
		seq_puts(seq, "lfs");
	seq_printf(seq, ",active_logs=%u", F2FS_OPTION(sbi).active_logs);
	if (test_opt(sbi, RESERVE_ROOT))
		seq_printf(seq, ",reserve_root=%u,resuid=%u,resgid=%u",
				F2FS_OPTION(sbi).root_reserved_blocks,
				from_kuid_munged(&init_user_ns,
					F2FS_OPTION(sbi).s_resuid),
				from_kgid_munged(&init_user_ns,
					F2FS_OPTION(sbi).s_resgid));
	if (F2FS_IO_SIZE_BITS(sbi))
		seq_printf(seq, ",io_bits=%u",
				F2FS_OPTION(sbi).write_io_size_bits);
#ifdef CONFIG_F2FS_FAULT_INJECTION
	if (test_opt(sbi, FAULT_INJECTION)) {
		seq_printf(seq, ",fault_injection=%u",
				F2FS_OPTION(sbi).fault_info.inject_rate);
		seq_printf(seq, ",fault_type=%u",
				F2FS_OPTION(sbi).fault_info.inject_type);
	}
#endif
#ifdef CONFIG_QUOTA
	if (test_opt(sbi, QUOTA))
		seq_puts(seq, ",quota");
	if (test_opt(sbi, USRQUOTA))
		seq_puts(seq, ",usrquota");
	if (test_opt(sbi, GRPQUOTA))
		seq_puts(seq, ",grpquota");
	if (test_opt(sbi, PRJQUOTA))
		seq_puts(seq, ",prjquota");
#endif
	f2fs_show_quota_options(seq, sbi->sb);
	if (F2FS_OPTION(sbi).whint_mode == WHINT_MODE_USER)
		seq_printf(seq, ",whint_mode=%s", "user-based");
	else if (F2FS_OPTION(sbi).whint_mode == WHINT_MODE_FS)
		seq_printf(seq, ",whint_mode=%s", "fs-based");

	fscrypt_show_test_dummy_encryption(seq, ',', sbi->sb);

	if (sbi->s_flags & SB_INLINECRYPT)
		seq_puts(seq, ",inlinecrypt");

	if (F2FS_OPTION(sbi).alloc_mode == ALLOC_MODE_DEFAULT)
		seq_printf(seq, ",alloc_mode=%s", "default");
	else if (F2FS_OPTION(sbi).alloc_mode == ALLOC_MODE_REUSE)
		seq_printf(seq, ",alloc_mode=%s", "reuse");

	if (test_opt(sbi, DISABLE_CHECKPOINT))
		seq_printf(seq, ",checkpoint=disable:%u",
				F2FS_OPTION(sbi).unusable_cap);
	if (test_opt(sbi, MERGE_CHECKPOINT))
		seq_puts(seq, ",checkpoint_merge");
	else
		seq_puts(seq, ",nocheckpoint_merge");
	if (F2FS_OPTION(sbi).fsync_mode == FSYNC_MODE_POSIX)
		seq_printf(seq, ",fsync_mode=%s", "posix");
	else if (F2FS_OPTION(sbi).fsync_mode == FSYNC_MODE_STRICT)
		seq_printf(seq, ",fsync_mode=%s", "strict");
	else if (F2FS_OPTION(sbi).fsync_mode == FSYNC_MODE_NOBARRIER)
		seq_printf(seq, ",fsync_mode=%s", "nobarrier");

#ifdef CONFIG_F2FS_FS_COMPRESSION
	f2fs_show_compress_options(seq, sbi->sb);
#endif

	if (test_opt(sbi, ATGC))
		seq_puts(seq, ",atgc");
	return 0;
}
#endif


static void default_options(struct f2fs_sb_info *sbi)
{
	/* init some FS parameters */
	F2FS_OPTION(sbi).active_logs = NR_CURSEG_PERSIST_TYPE;
	F2FS_OPTION(sbi).inline_xattr_size = DEFAULT_INLINE_XATTR_ADDRS;
	F2FS_OPTION(sbi).whint_mode = WHINT_MODE_OFF;
	F2FS_OPTION(sbi).alloc_mode = ALLOC_MODE_DEFAULT;
	F2FS_OPTION(sbi).fsync_mode = FSYNC_MODE_POSIX;
	//F2FS_OPTION(sbi).s_resuid = make_kuid(&init_user_ns, F2FS_DEF_RESUID);
	//F2FS_OPTION(sbi).s_resgid = make_kgid(&init_user_ns, F2FS_DEF_RESGID);
	F2FS_OPTION(sbi).compress_algorithm = COMPRESS_LZ4;
	F2FS_OPTION(sbi).compress_log_size = MIN_COMPRESS_LOG_SIZE;
	F2FS_OPTION(sbi).compress_ext_cnt = 0;
	F2FS_OPTION(sbi).compress_mode = COMPR_MODE_FS;
	F2FS_OPTION(sbi).bggc_mode = BGGC_MODE_ON;

	sbi->s_flags &= ~SB_INLINECRYPT;

	set_opt(sbi, INLINE_XATTR);
	set_opt(sbi, INLINE_DATA);
	set_opt(sbi, INLINE_DENTRY);
	set_opt(sbi, EXTENT_CACHE);
	set_opt(sbi, NOHEAP);
	clear_opt(sbi, DISABLE_CHECKPOINT);
	set_opt(sbi, MERGE_CHECKPOINT);
	F2FS_OPTION(sbi).unusable_cap = 0;
	sbi->s_flags |= SB_LAZYTIME;
	set_opt(sbi, FLUSH_MERGE);
	set_opt(sbi, DISCARD);
	if (f2fs_sb_has_blkzoned(sbi))		F2FS_OPTION(sbi).fs_mode = FS_MODE_LFS;
	else								F2FS_OPTION(sbi).fs_mode = FS_MODE_ADAPTIVE;

#ifdef CONFIG_F2FS_FS_XATTR
	set_opt(sbi, XATTR_USER);
#endif
#ifdef CONFIG_F2FS_FS_POSIX_ACL
	set_opt(sbi, POSIX_ACL);
#endif

	f2fs_build_fault_attr(sbi, 0, 0);
}

#ifdef CONFIG_QUOTA
static int f2fs_enable_quotas(struct super_block *sb);
#endif

int f2fs_sb_info::f2fs_disable_checkpoint(void)
{
//	unsigned int s_flags = this->s_flags;
	struct cp_control cpc;
	int err = 0;
	int ret;
	block_t unusable;

	if (s_flags & SB_RDONLY) 
	{
		f2fs_err(sbi, L"checkpoint=disable on readonly fs");
		return -EINVAL;
	}
	s_flags |= SB_ACTIVE;

	f2fs_update_time( DISABLE_TIME);

	while (!f2fs_time_over(this, DISABLE_TIME)) 
	{
		down_write(&gc_lock);
		err = f2fs_gc(this, true, false, false, NULL_SEGNO);
		if (err == -ENODATA) 
		{
			err = 0;
			break;
		}
		if (err && err != -EAGAIN)	break;
		//<YUAN> SRW_Lock不允许递归
		up_write(&gc_lock);
	}

//	ret = sync_filesystem();
	ret = sync_fs(true);
	if (ret || err) 
	{
		err = ret ? ret : err;
		goto restore_flag;
	}

	unusable = f2fs_get_unusable_blocks(this);
	if (f2fs_disable_cp_again(this, unusable)) 
	{
		err = -EAGAIN;
		goto restore_flag;
	}

	down_write(&gc_lock);
	cpc.reason = CP_PAUSE;
	set_sbi_flag(SBI_CP_DISABLED);
	err = f2fs_write_checkpoint(&cpc);
	if (err)		goto out_unlock;

	spin_lock(&stat_lock);
	unusable_block_count = unusable;
	spin_unlock(&stat_lock);

out_unlock:
	up_write(&gc_lock);
restore_flag:
	s_flags = s_flags;	/* Restore SB_RDONLY status */
	return err;
}

void f2fs_sb_info::SetDevice(IVirtualDisk* dev)
{
	if (dev)
	{
		s_ndevs++;
		s_bdev = dev;
		s_bdev->AddRef();
	}
}

static void f2fs_enable_checkpoint(f2fs_sb_info *sbi)
{
	/* we should flush all the data to keep data consistency */
#if 0 //TODO
	sync_inodes_sb(sbi->sb);
#endif
	down_write(&sbi->gc_lock);
	f2fs_dirty_to_prefree(sbi);

	clear_sbi_flag(sbi, SBI_CP_DISABLED);
	sbi->set_sbi_flag(SBI_IS_DIRTY);
	up_write(&sbi->gc_lock);

	sbi->sync_fs(1);
}

#if 0

static int f2fs_remount(struct super_block *sb, int *flags, char *data)
{
	struct f2fs_sb_info *sbi = F2FS_SB(sb);
	struct f2fs_mount_info org_mount_opt;
	unsigned long old_sb_flags;
	int err;
	bool need_restart_gc = false, need_stop_gc = false;
	bool need_restart_ckpt = false, need_stop_ckpt = false;
	bool need_restart_flush = false, need_stop_flush = false;
	bool no_extent_cache = !test_opt(sbi, EXTENT_CACHE);
	bool disable_checkpoint = test_opt(sbi, DISABLE_CHECKPOINT);
	bool no_io_align = !F2FS_IO_ALIGNED(sbi);
	bool no_atgc = !test_opt(sbi, ATGC);
	bool checkpoint_changed;
#ifdef CONFIG_QUOTA
	int i, j;
#endif

	/*
	 * Save the old mount options in case we
	 * need to restore them.
	 */
	org_mount_opt = sbi->mount_opt;
	old_sb_flags = sb->s_flags;

#ifdef CONFIG_QUOTA
	org_mount_opt.s_jquota_fmt = F2FS_OPTION(sbi).s_jquota_fmt;
	for (i = 0; i < MAXQUOTAS; i++) {
		if (F2FS_OPTION(sbi).s_qf_names[i]) {
			org_mount_opt.s_qf_names[i] =
				kstrdup(F2FS_OPTION(sbi).s_qf_names[i],
				GFP_KERNEL);
			if (!org_mount_opt.s_qf_names[i]) {
				for (j = 0; j < i; j++)
					kfree(org_mount_opt.s_qf_names[j]);
				return -ENOMEM;
			}
		} else {
			org_mount_opt.s_qf_names[i] = NULL;
		}
	}
#endif

	/* recover superblocks we couldn't write due to previous RO mount */
	if (!(*flags & SB_RDONLY) && sbi->is_sbi_flag_set( SBI_NEED_SB_WRITE)) {
		err = f2fs_commit_super(sbi, false);
		f2fs_info(sbi, "Try to recover all the superblocks, ret: %d",
			  err);
		if (!err)
			clear_sbi_flag(sbi, SBI_NEED_SB_WRITE);
	}

	default_options(sbi);

	/* parse mount options */
	err = parse_options(sb, data, true);
	if (err)
		goto restore_opts;
	checkpoint_changed =
			disable_checkpoint != test_opt(sbi, DISABLE_CHECKPOINT);

	/*
	 * Previous and new state of filesystem is RO,
	 * so skip checking GC and FLUSH_MERGE conditions.
	 */
	if (f2fs_readonly(sb) && (*flags & SB_RDONLY))
		goto skip;

#ifdef CONFIG_QUOTA
	if (!f2fs_readonly(sb) && (*flags & SB_RDONLY)) {
		err = dquot_suspend(sb, -1);
		if (err < 0)
			goto restore_opts;
	} else if (f2fs_readonly(sb) && !(*flags & SB_RDONLY)) {
		/* dquot_resume needs RW */
		sb->s_flags &= ~SB_RDONLY;
		if (sb_any_quota_suspended(sb)) {
			dquot_resume(sb, -1);
		} else if (f2fs_sb_has_quota_ino(sbi)) {
			err = f2fs_enable_quotas(sb);
			if (err)
				goto restore_opts;
		}
	}
#endif
	/* disallow enable atgc dynamically */
	if (no_atgc == !!test_opt(sbi, ATGC)) {
		err = -EINVAL;
		f2fs_warn(sbi, "switch atgc option is not allowed");
		goto restore_opts;
	}

	/* disallow enable/disable extent_cache dynamically */
	if (no_extent_cache == !!test_opt(sbi, EXTENT_CACHE)) {
		err = -EINVAL;
		f2fs_warn(sbi, "switch extent_cache option is not allowed");
		goto restore_opts;
	}

	if (no_io_align == !!F2FS_IO_ALIGNED(sbi)) {
		err = -EINVAL;
		f2fs_warn(sbi, "switch io_bits option is not allowed");
		goto restore_opts;
	}

	if ((*flags & SB_RDONLY) && test_opt(sbi, DISABLE_CHECKPOINT)) {
		err = -EINVAL;
		f2fs_warn(sbi, "disabling checkpoint not compatible with read-only");
		goto restore_opts;
	}

	/*
	 * We stop the GC thread if FS is mounted as RO
	 * or if background_gc = off is passed in mount
	 * option. Also sync the filesystem.
	 */
	if ((*flags & SB_RDONLY) ||
			(F2FS_OPTION(sbi).bggc_mode == BGGC_MODE_OFF &&
			!test_opt(sbi, GC_MERGE))) {
		if (sbi->gc_thread) {
			f2fs_stop_gc_thread(sbi);
			need_restart_gc = true;
		}
	} else if (!sbi->gc_thread) {
		err = f2fs_start_gc_thread(sbi);
		if (err)
			goto restore_opts;
		need_stop_gc = true;
	}

	if (*flags & SB_RDONLY ||
		F2FS_OPTION(sbi).whint_mode != org_mount_opt.whint_mode) {
		sync_inodes_sb(sb);

		sbi->set_sbi_flag(SBI_IS_DIRTY);
		sbi->set_sbi_flag(SBI_IS_CLOSE);
		sb->sync_fs(1);
		clear_sbi_flag(sbi, SBI_IS_CLOSE);
	}

	if ((*flags & SB_RDONLY) || test_opt(sbi, DISABLE_CHECKPOINT) ||
			!test_opt(sbi, MERGE_CHECKPOINT)) {
		f2fs_stop_ckpt_thread(sbi);
		need_restart_ckpt = true;
	} else {
		err = f2fs_start_ckpt_thread(sbi);
		if (err) {
			f2fs_err(sbi,
			    "Failed to start F2FS issue_checkpoint_thread (%d)",
			    err);
			goto restore_gc;
		}
		need_stop_ckpt = true;
	}

	/*
	 * We stop issue flush thread if FS is mounted as RO
	 * or if flush_merge is not passed in mount option.
	 */
	if ((*flags & SB_RDONLY) || !test_opt(sbi, FLUSH_MERGE)) {
		clear_opt(sbi, FLUSH_MERGE);
		f2fs_destroy_flush_cmd_control(sbi, false);
		need_restart_flush = true;
	} else {
		err = f2fs_create_flush_cmd_control(sbi);
		if (err)
			goto restore_ckpt;
		need_stop_flush = true;
	}

	if (checkpoint_changed) {
		if (test_opt(sbi, DISABLE_CHECKPOINT)) {
			err = f2fs_disable_checkpoint(sbi);
			if (err)
				goto restore_flush;
		} else {
			f2fs_enable_checkpoint(sbi);
		}
	}

skip:
#ifdef CONFIG_QUOTA
	/* Release old quota file names */
	for (i = 0; i < MAXQUOTAS; i++)
		kfree(org_mount_opt.s_qf_names[i]);
#endif
	/* Update the POSIXACL Flag */
	sb->s_flags = (sb->s_flags & ~SB_POSIXACL) |
		(test_opt(sbi, POSIX_ACL) ? SB_POSIXACL : 0);

	limit_reserve_root(sbi);
	adjust_unusable_cap_perc(sbi);
	*flags = (*flags & ~SB_LAZYTIME) | (sb->s_flags & SB_LAZYTIME);
	return 0;
restore_flush:
	if (need_restart_flush) {
		if (f2fs_create_flush_cmd_control(sbi))
			f2fs_warn(sbi, "background flush thread has stopped");
	} else if (need_stop_flush) {
		clear_opt(sbi, FLUSH_MERGE);
		f2fs_destroy_flush_cmd_control(sbi, false);
	}
restore_ckpt:
	if (need_restart_ckpt) {
		if (f2fs_start_ckpt_thread(sbi))
			f2fs_warn(sbi, "background ckpt thread has stopped");
	} else if (need_stop_ckpt) {
		f2fs_stop_ckpt_thread(sbi);
	}
restore_gc:
	if (need_restart_gc) {
		if (f2fs_start_gc_thread(sbi))
			f2fs_warn(sbi, "background gc thread has stopped");
	} else if (need_stop_gc) {
		f2fs_stop_gc_thread(sbi);
	}
restore_opts:
#ifdef CONFIG_QUOTA
	F2FS_OPTION(sbi).s_jquota_fmt = org_mount_opt.s_jquota_fmt;
	for (i = 0; i < MAXQUOTAS; i++) {
		kfree(F2FS_OPTION(sbi).s_qf_names[i]);
		F2FS_OPTION(sbi).s_qf_names[i] = org_mount_opt.s_qf_names[i];
	}
#endif
	sbi->mount_opt = org_mount_opt;
	sb->s_flags = old_sb_flags;
	return err;
}

#ifdef CONFIG_QUOTA
/* Read data from quotafile */
static ssize_t f2fs_quota_read(struct super_block *sb, int type, char *data,
			       size_t len, loff_t off)
{
	struct inode *inode = sb_dqopt(sb)->files[type];
	address_space *mapping = inode->i_mapping;
	block_t blkidx = F2FS_BYTES_TO_BLK(off);
	int offset = off & (sb->s_blocksize - 1);
	int tocopy;
	size_t toread;
	loff_t i_size = i_size_read(inode);
	struct page *page;
	char *kaddr;

	if (off > i_size)
		return 0;

	if (off + len > i_size)
		len = i_size - off;
	toread = len;
	while (toread > 0) {
		tocopy = min_t(unsigned long, sb->s_blocksize - offset, toread);
repeat:
		page = read_cache_page_gfp(mapping, blkidx, GFP_NOFS);
		if (IS_ERR(page)) {
			if (PTR_ERR(page) == -ENOMEM) {
				congestion_wait(BLK_RW_ASYNC,
						DEFAULT_IO_TIMEOUT);
				goto repeat;
			}
			set_sbi_flag(F2FS_SB(sb), SBI_QUOTA_NEED_REPAIR);
			return PTR_ERR(page);
		}

		lock_page(page);

		if (unlikely(page->mapping != mapping)) {
			f2fs_put_page(page, 1);
			goto repeat;
		}
		if (unlikely(!PageUptodate(page))) {
			f2fs_put_page(page, 1);
			set_sbi_flag(F2FS_SB(sb), SBI_QUOTA_NEED_REPAIR);
			return -EIO;
		}

		kaddr = kmap_atomic(page);
		memcpy(data, kaddr + offset, tocopy);
		kunmap_atomic(kaddr);
		f2fs_put_page(page, 1);

		offset = 0;
		toread -= tocopy;
		data += tocopy;
		blkidx++;
	}
	return len;
}

/* Write to quotafile */
static ssize_t f2fs_quota_write(struct super_block *sb, int type,
				const char *data, size_t len, loff_t off)
{
	struct inode *inode = sb_dqopt(sb)->files[type];
	address_space *mapping = inode->i_mapping;
	const struct address_space_operations *a_ops = mapping->a_ops;
	int offset = off & (sb->s_blocksize - 1);
	size_t towrite = len;
	struct page *page;
	void *fsdata = NULL;
	char *kaddr;
	int err = 0;
	int tocopy;

	while (towrite > 0) {
		tocopy = min_t(unsigned long, sb->s_blocksize - offset,
								towrite);
retry:
		err = a_ops->write_begin(NULL, mapping, off, tocopy, 0,
							&page, &fsdata);
		if (unlikely(err)) {
			if (err == -ENOMEM) {
				congestion_wait(BLK_RW_ASYNC,
						DEFAULT_IO_TIMEOUT);
				goto retry;
			}
			set_sbi_flag(F2FS_SB(sb), SBI_QUOTA_NEED_REPAIR);
			break;
		}

		kaddr = kmap_atomic(page);
		memcpy(kaddr + offset, data, tocopy);
		kunmap_atomic(kaddr);
		flush_dcache_page(page);

		a_ops->write_end(NULL, mapping, off, tocopy, tocopy,
						page, fsdata);
		offset = 0;
		towrite -= tocopy;
		off += tocopy;
		data += tocopy;
		cond_resched();
	}

	if (len == towrite)
		return err;
	inode->i_mtime = inode->i_ctime = current_time(inode);
	inode->f2fs_mark_inode_dirty_sync( false);
	return len - towrite;
}

static struct dquot **f2fs_get_dquots(struct inode *inode)
{
	return F2FS_I(inode)->i_dquot;
}

static qsize_t *f2fs_get_reserved_space(struct inode *inode)
{
	return &F2FS_I(inode)->i_reserved_quota;
}

static int f2fs_quota_on_mount(struct f2fs_sb_info *sbi, int type)
{
	if (sbi->is_set_ckpt_flags( CP_QUOTA_NEED_FSCK_FLAG)) {
		f2fs_err(sbi, "quota sysfile may be corrupted, skip loading it");
		return 0;
	}

	return dquot_quota_on_mount(sbi->sb, F2FS_OPTION(sbi).s_qf_names[type],
					F2FS_OPTION(sbi).s_jquota_fmt, type);
}

int f2fs_enable_quota_files(struct f2fs_sb_info *sbi, bool rdonly)
{
	int enabled = 0;
	int i, err;

	if (f2fs_sb_has_quota_ino(sbi) && rdonly) {
		err = f2fs_enable_quotas(sbi->sb);
		if (err) {
			f2fs_err(sbi, "Cannot turn on quota_ino: %d", err);
			return 0;
		}
		return 1;
	}

	for (i = 0; i < MAXQUOTAS; i++) {
		if (F2FS_OPTION(sbi).s_qf_names[i]) {
			err = f2fs_quota_on_mount(sbi, i);
			if (!err) {
				enabled = 1;
				continue;
			}
			f2fs_err(sbi, "Cannot turn on quotas: %d on %d",
				 err, i);
		}
	}
	return enabled;
}

static int f2fs_quota_enable(struct super_block *sb, int type, int format_id,
			     unsigned int flags)
{
	struct inode *qf_inode;
	unsigned long qf_inum;
	int err;

	BUG_ON(!f2fs_sb_has_quota_ino(F2FS_SB(sb)));

	qf_inum = f2fs_qf_ino(sb, type);
	if (!qf_inum)
		return -EPERM;

	qf_inode = f2fs_iget(sb, qf_inum);
	if (IS_ERR(qf_inode)) {
		f2fs_err(F2FS_SB(sb), "Bad quota inode %u:%lu", type, qf_inum);
		return PTR_ERR(qf_inode);
	}

	/* Don't account quota for quota files to avoid recursion */
	qf_inode->i_flags |= S_NOQUOTA;
	err = dquot_load_quota_inode(qf_inode, type, format_id, flags);
	iput(qf_inode);
	return err;
}

static int f2fs_enable_quotas(struct super_block *sb)
{
	struct f2fs_sb_info *sbi = F2FS_SB(sb);
	int type, err = 0;
	unsigned long qf_inum;
	bool quota_mopt[MAXQUOTAS] = {
		test_opt(sbi, USRQUOTA),
		test_opt(sbi, GRPQUOTA),
		test_opt(sbi, PRJQUOTA),
	};

	if (is_set_ckpt_flags(F2FS_SB(sb), CP_QUOTA_NEED_FSCK_FLAG)) {
		f2fs_err(sbi, "quota file may be corrupted, skip loading it");
		return 0;
	}

	sb_dqopt(sb)->flags |= DQUOT_QUOTA_SYS_FILE;

	for (type = 0; type < MAXQUOTAS; type++) {
		qf_inum = f2fs_qf_ino(sb, type);
		if (qf_inum) {
			err = f2fs_quota_enable(sb, type, QFMT_VFS_V1,
				DQUOT_USAGE_ENABLED |
				(quota_mopt[type] ? DQUOT_LIMITS_ENABLED : 0));
			if (err) {
				f2fs_err(sbi, "Failed to enable quota tracking (type=%d, err=%d). Please run fsck to fix.",
					 type, err);
				for (type--; type >= 0; type--)
					dquot_quota_off(sb, type);
				set_sbi_flag(F2FS_SB(sb),
						SBI_QUOTA_NEED_REPAIR);
				return err;
			}
		}
	}
	return 0;
}

int f2fs_quota_sync(struct super_block *sb, int type)
{
	struct f2fs_sb_info *sbi = F2FS_SB(sb);
	struct quota_info *dqopt = sb_dqopt(sb);
	int cnt;
	int ret;

	/*
	 * do_quotactl
	 *  f2fs_quota_sync
	 *  down_read(quota_sem)
	 *  dquot_writeback_dquots()
	 *  f2fs_dquot_commit
	 *                            block_operation
	 *                            down_read(quota_sem)
	 */
	sbi->f2fs_lock_op();
	//auto_lock<semaphore_read_lock> lock_op(sbi->cp_rwsem);

	down_read(&sbi->quota_sem);
	ret = dquot_writeback_dquots(sb, type);
	if (ret)
		goto out;

	/*
	 * Now when everything is written we can discard the pagecache so
	 * that userspace sees the changes.
	 */
	for (cnt = 0; cnt < MAXQUOTAS; cnt++) {
		address_space *mapping;

		if (type != -1 && cnt != type)
			continue;
		if (!sb_has_quota_active(sb, cnt))
			continue;

		mapping = dqopt->files[cnt]->i_mapping;

		ret = mapping->filemap_fdatawrite();
		if (ret)
			goto out;

		/* if we are using journalled quota */
		if (is_journalled_quota(sbi))
			continue;

		ret = filemap_fdatawait(mapping);
		if (ret)
			set_sbi_flag(F2FS_SB(sb), SBI_QUOTA_NEED_REPAIR);

		inode_lock(dqopt->files[cnt]);
		truncate_inode_pages(&dqopt->files[cnt]->i_data, 0);
		inode_unlock(dqopt->files[cnt]);
	}
out:
	if (ret)
		set_sbi_flag(F2FS_SB(sb), SBI_QUOTA_NEED_REPAIR);
	up_read(&sbi->quota_sem);
	sbi->f2fs_unlock_op();
	return ret;
}

static int f2fs_quota_on(struct super_block *sb, int type, int format_id,
							const struct path *path)
{
	struct inode *inode;
	int err;

	/* if quota sysfile exists, deny enabling quota with specific file */
	if (f2fs_sb_has_quota_ino(F2FS_SB(sb))) {
		f2fs_err(F2FS_SB(sb), "quota sysfile already exists");
		return -EBUSY;
	}

	err = f2fs_quota_sync(sb, type);
	if (err)
		return err;

	err = dquot_quota_on(sb, type, format_id, path);
	if (err)
		return err;

	inode = d_inode(path->dentry);

	inode_lock(inode);
	F2FS_I(inode)->i_flags |= F2FS_NOATIME_FL | F2FS_IMMUTABLE_FL;
	f2fs_set_inode_flags(inode);
	inode_unlock(inode);
	inode->f2fs_mark_inode_dirty_sync( false);

	return 0;
}

static int __f2fs_quota_off(struct super_block *sb, int type)
{
	struct inode *inode = sb_dqopt(sb)->files[type];
	int err;

	if (!inode || !igrab(inode))
		return dquot_quota_off(sb, type);

	err = f2fs_quota_sync(sb, type);
	if (err)
		goto out_put;

	err = dquot_quota_off(sb, type);
	if (err || f2fs_sb_has_quota_ino(F2FS_SB(sb)))
		goto out_put;

	inode_lock(inode);
	F2FS_I(inode)->i_flags &= ~(F2FS_NOATIME_FL | F2FS_IMMUTABLE_FL);
	f2fs_set_inode_flags(inode);
	inode_unlock(inode);
	inode->f2fs_mark_inode_dirty_sync( false);
out_put:
	iput(inode);
	return err;
}

static int f2fs_quota_off(struct super_block *sb, int type)
{
	struct f2fs_sb_info *sbi = F2FS_SB(sb);
	int err;

	err = __f2fs_quota_off(sb, type);

	/*
	 * quotactl can shutdown journalled quota, result in inconsistence
	 * between quota record and fs data by following updates, tag the
	 * flag to let fsck be aware of it.
	 */
	if (is_journalled_quota(sbi))
		sbi->set_sbi_flag(SBI_QUOTA_NEED_REPAIR);
	return err;
}

void f2fs_quota_off_umount(struct super_block *sb)
{
	int type;
	int err;

	for (type = 0; type < MAXQUOTAS; type++) {
		err = __f2fs_quota_off(sb, type);
		if (err) {
			int ret = dquot_quota_off(sb, type);

			f2fs_err(F2FS_SB(sb), "Fail to turn off disk quota (type: %d, err: %d, ret:%d), Please run fsck to fix it.",
				 type, err, ret);
			set_sbi_flag(F2FS_SB(sb), SBI_QUOTA_NEED_REPAIR);
		}
	}
	/*
	 * In case of checkpoint=disable, we must flush quota blocks.
	 * This can cause NULL exception for node_inode in end_io, since
	 * put_super already dropped it.
	 */
	sync_filesystem(sb);
}

static void f2fs_truncate_quota_inode_pages(struct super_block *sb)
{
	struct quota_info *dqopt = sb_dqopt(sb);
	int type;

	for (type = 0; type < MAXQUOTAS; type++) {
		if (!dqopt->files[type])
			continue;
		f2fs_inode_synced(dqopt->files[type]);
	}
}

static int f2fs_dquot_commit(struct dquot *dquot)
{
	struct f2fs_sb_info *sbi = F2FS_SB(dquot->dq_sb);
	int ret;

	down_read_nested(&sbi->quota_sem, SINGLE_DEPTH_NESTING);
	ret = dquot_commit(dquot);
	if (ret < 0)
		sbi->set_sbi_flag(SBI_QUOTA_NEED_REPAIR);
	up_read(&sbi->quota_sem);
	return ret;
}

static int f2fs_dquot_acquire(struct dquot *dquot)
{
	struct f2fs_sb_info *sbi = F2FS_SB(dquot->dq_sb);
	int ret;

	down_read(&sbi->quota_sem);
	ret = dquot_acquire(dquot);
	if (ret < 0)
		sbi->set_sbi_flag(SBI_QUOTA_NEED_REPAIR);
	up_read(&sbi->quota_sem);
	return ret;
}

static int f2fs_dquot_release(struct dquot *dquot)
{
	struct f2fs_sb_info *sbi = F2FS_SB(dquot->dq_sb);
	int ret = dquot_release(dquot);

	if (ret < 0)
		sbi->set_sbi_flag(SBI_QUOTA_NEED_REPAIR);
	return ret;
}

static int f2fs_dquot_mark_dquot_dirty(struct dquot *dquot)
{
	struct super_block *sb = dquot->dq_sb;
	struct f2fs_sb_info *sbi = F2FS_SB(sb);
	int ret = dquot_mark_dquot_dirty(dquot);

	/* if we are using journalled quota */
	if (is_journalled_quota(sbi))
		sbi->set_sbi_flag(SBI_QUOTA_NEED_FLUSH);

	return ret;
}

static int f2fs_dquot_commit_info(struct super_block *sb, int type)
{
	struct f2fs_sb_info *sbi = F2FS_SB(sb);
	int ret = dquot_commit_info(sb, type);

	if (ret < 0)
		sbi->set_sbi_flag(SBI_QUOTA_NEED_REPAIR);
	return ret;
}

static int f2fs_get_projid(struct inode *inode, kprojid_t *projid)
{
	*projid = F2FS_I(inode)->i_projid;
	return 0;
}

static const struct dquot_operations f2fs_quota_operations = {
	.get_reserved_space = f2fs_get_reserved_space,
	.write_dquot	= f2fs_dquot_commit,
	.acquire_dquot	= f2fs_dquot_acquire,
	.release_dquot	= f2fs_dquot_release,
	.mark_dirty	= f2fs_dquot_mark_dquot_dirty,
	.write_info	= f2fs_dquot_commit_info,
	.alloc_dquot	= dquot_alloc,
	.destroy_dquot	= dquot_destroy,
	.get_projid	= f2fs_get_projid,
	.get_next_id	= dquot_get_next_id,
};

static const struct quotactl_ops f2fs_quotactl_ops = {
	.quota_on	= f2fs_quota_on,
	.quota_off	= f2fs_quota_off,
	.quota_sync	= f2fs_quota_sync,
	.get_state	= dquot_get_state,
	.set_info	= dquot_set_dqinfo,
	.get_dqblk	= dquot_get_dqblk,
	.set_dqblk	= dquot_set_dqblk,
	.get_nextdqblk	= dquot_get_next_dqblk,
};
#else
int f2fs_quota_sync(struct super_block *sb, int type)
{
	return 0;
}

void f2fs_quota_off_umount(struct super_block *sb)
{
}
#endif

static const struct super_operations f2fs_sops = {
	.alloc_inode	= f2fs_alloc_inode,
	.free_inode	= f2fs_free_inode,
	.drop_inode	= f2fs_drop_inode,
	.write_inode	= f2fs_write_inode,
	.dirty_inode	= f2fs_dirty_inode,
	.show_options	= f2fs_show_options,
#ifdef CONFIG_QUOTA
	.quota_read	= f2fs_quota_read,
	.quota_write	= f2fs_quota_write,
	.get_dquots	= f2fs_get_dquots,
#endif
	.evict_inode	= f2fs_evict_inode,
	.put_super	= f2fs_put_super,
	.sync_fs	= f2fs_sync_fs,
	.freeze_fs	= f2fs_freeze,
	.unfreeze_fs	= f2fs_unfreeze,
	.statfs		= f2fs_statfs,
	.remount_fs	= f2fs_remount,
};

#ifdef CONFIG_FS_ENCRYPTION
static int f2fs_get_context(struct inode *inode, void *ctx, size_t len)
{
	return f2fs_getxattr(inode, F2FS_XATTR_INDEX_ENCRYPTION,
				F2FS_XATTR_NAME_ENCRYPTION_CONTEXT,
				ctx, len, NULL);
}

static int f2fs_set_context(struct inode *inode, const void *ctx, size_t len,
							void *fs_data)
{
	struct f2fs_sb_info *sbi = F2FS_I_SB(inode);

	/*
	 * Encrypting the root directory is not allowed because fsck
	 * expects lost+found directory to exist and remain unencrypted
	 * if LOST_FOUND feature is enabled.
	 *
	 */
	if (f2fs_sb_has_lost_found(sbi) &&
			inode->i_ino == F2FS_ROOT_INO(sbi))
		return -EPERM;

	return f2fs_setxattr(inode, F2FS_XATTR_INDEX_ENCRYPTION,
				F2FS_XATTR_NAME_ENCRYPTION_CONTEXT,
				ctx, len, fs_data, XATTR_CREATE);
}

static const union fscrypt_policy *f2fs_get_dummy_policy(struct super_block *sb)
{
	return F2FS_OPTION(F2FS_SB(sb)).dummy_enc_policy.policy;
}

static bool f2fs_has_stable_inodes(struct super_block *sb)
{
	return true;
}

static void f2fs_get_ino_and_lblk_bits(struct super_block *sb,
				       int *ino_bits_ret, int *lblk_bits_ret)
{
	*ino_bits_ret = 8 * sizeof(nid_t);
	*lblk_bits_ret = 8 * sizeof(block_t);
}

static int f2fs_get_num_devices(struct super_block *sb)
{
	struct f2fs_sb_info *sbi = F2FS_SB(sb);

	if (sbi->f2fs_is_multi_device())
		return sbi->s_ndevs;
	return 1;
}

static void f2fs_get_devices(struct super_block *sb,
			     struct request_queue **devs)
{
	struct f2fs_sb_info *sbi = F2FS_SB(sb);
	int i;

	for (i = 0; i < sbi->s_ndevs; i++)
		devs[i] = bdev_get_queue(FDEV(i).bdev);
}

static const struct fscrypt_operations f2fs_cryptops = {
	.key_prefix		= "f2fs:",
	.get_context		= f2fs_get_context,
	.set_context		= f2fs_set_context,
	.get_dummy_policy	= f2fs_get_dummy_policy,
	.empty_dir		= f2fs_empty_dir,
	.max_namelen		= F2FS_NAME_LEN,
	.has_stable_inodes	= f2fs_has_stable_inodes,
	.get_ino_and_lblk_bits	= f2fs_get_ino_and_lblk_bits,
	.get_num_devices	= f2fs_get_num_devices,
	.get_devices		= f2fs_get_devices,
};
#endif

static struct inode *f2fs_nfs_get_inode(struct super_block *sb,
		u64 ino, u32 generation)
{
	struct f2fs_sb_info *sbi = F2FS_SB(sb);
	struct inode *inode;

	if (f2fs_check_nid_range(sbi, ino))
		return ERR_PTR(-ESTALE);

	/*
	 * f2fs_iget isn't quite right if the inode is currently unallocated!
	 * However f2fs_iget currently does appropriate checks to handle stale
	 * inodes so everything is OK.
	 */
	inode = f2fs_iget(sb, ino);
	if (IS_ERR(inode))
		return ERR_CAST(inode);
	if (unlikely(generation && inode->i_generation != generation)) {
		/* we didn't find the right inode.. */
		iput(inode);
		return ERR_PTR(-ESTALE);
	}
	return inode;
}

static struct dentry *f2fs_fh_to_dentry(struct super_block *sb, struct fid *fid,
		int fh_len, int fh_type)
{
	return generic_fh_to_dentry(sb, fid, fh_len, fh_type,
				    f2fs_nfs_get_inode);
}

static struct dentry *f2fs_fh_to_parent(struct super_block *sb, struct fid *fid,
		int fh_len, int fh_type)
{
	return generic_fh_to_parent(sb, fid, fh_len, fh_type,
				    f2fs_nfs_get_inode);
}

static const struct export_operations f2fs_export_ops = {
	.fh_to_dentry = f2fs_fh_to_dentry,
	.fh_to_parent = f2fs_fh_to_parent,
	.get_parent = f2fs_get_parent,
};
#endif

loff_t max_file_blocks(inode *inode)
{
	loff_t result = 0;
	loff_t leaf_count;

	/* note: previously, result is equal to (DEF_ADDRS_PER_INODE - DEFAULT_INLINE_XATTR_ADDRS), but now f2fs try to
		reserve more space in inode.i_addr, it will be more safe to reassign result as zero. */

	if (inode && f2fs_compressed_file(inode))		leaf_count = ADDRS_PER_BLOCK(inode);
	else		leaf_count = DEF_ADDRS_PER_BLOCK;

	/* two direct node blocks */
	result += (leaf_count * 2);

	/* two indirect node blocks */
	leaf_count *= NIDS_PER_BLOCK;
	result += (leaf_count * 2);

	/* one double indirect node block */
	leaf_count *= NIDS_PER_BLOCK;
	result += leaf_count;

	return result;
}

static int __f2fs_commit_super(CBufferHead *bh, struct f2fs_super_block *super)
{
	BYTE* data = bh->Lock();
	if (super)	memcpy_s(data + F2FS_SUPER_OFFSET, sizeof(f2fs_super_block), super, sizeof(f2fs_super_block));
	bh->SetDirty();
	bh->Unlock();
	return bh->SyncDirty(REQ_SYNC | REQ_PREFLUSH | REQ_FUA);


	//lock_buffer(bh);
	//if (super)	memcpy(bh->b_data + F2FS_SUPER_OFFSET, super, sizeof(*super));
	//set_buffer_dirty(bh);
	//unlock_buffer(bh);

	///* it's rare case, we can do fua all the time */
	//return __sync_dirty_buffer(bh, REQ_SYNC | REQ_PREFLUSH | REQ_FUA);
}

bool CF2fsFileSystem::sanity_check_area_boundary(f2fs_sb_info *sbi, CBufferHead *bh)
{
	struct f2fs_super_block *raw_super = (struct f2fs_super_block *) (bh->Data() + F2FS_SUPER_OFFSET);
	super_block *sb = static_cast<super_block*>(sbi);
	u32 segment0_blkaddr = le32_to_cpu(raw_super->segment0_blkaddr);
	u32 cp_blkaddr = le32_to_cpu(raw_super->cp_blkaddr);
	u32 sit_blkaddr = le32_to_cpu(raw_super->sit_blkaddr);
	u32 nat_blkaddr = le32_to_cpu(raw_super->nat_blkaddr);
	u32 ssa_blkaddr = le32_to_cpu(raw_super->ssa_blkaddr);
	u32 main_blkaddr = le32_to_cpu(raw_super->main_blkaddr);
	u32 segment_count_ckpt = le32_to_cpu(raw_super->segment_count_ckpt);
	u32 segment_count_sit = le32_to_cpu(raw_super->segment_count_sit);
	u32 segment_count_nat = le32_to_cpu(raw_super->segment_count_nat);
	u32 segment_count_ssa = le32_to_cpu(raw_super->segment_count_ssa);
	u32 segment_count_main = le32_to_cpu(raw_super->segment_count_main);
	u32 segment_count = le32_to_cpu(raw_super->segment_count);
	u32 log_blocks_per_seg = le32_to_cpu(raw_super->log_blocks_per_seg);

	u64 main_end_blkaddr = main_blkaddr + (segment_count_main << log_blocks_per_seg);
	u64 seg_end_blkaddr = segment0_blkaddr + (segment_count << log_blocks_per_seg);

	if (segment0_blkaddr != cp_blkaddr) {
		f2fs_info(sbi, L"Mismatch start address, segment0(%u) cp_blkaddr(%u)",
			  segment0_blkaddr, cp_blkaddr);
		return true;
	}

	if (cp_blkaddr + (segment_count_ckpt << log_blocks_per_seg) !=
							sit_blkaddr) {
		f2fs_info(sbi, L"Wrong CP boundary, start(%u) end(%u) blocks(%u)",
			  cp_blkaddr, sit_blkaddr,
			  segment_count_ckpt << log_blocks_per_seg);
		return true;
	}

	if (sit_blkaddr + (segment_count_sit << log_blocks_per_seg) !=
							nat_blkaddr) {
		f2fs_info(sbi, L"Wrong SIT boundary, start(%u) end(%u) blocks(%u)",
			  sit_blkaddr, nat_blkaddr,
			  segment_count_sit << log_blocks_per_seg);
		return true;
	}

	if (nat_blkaddr + (segment_count_nat << log_blocks_per_seg) !=
							ssa_blkaddr) {
		f2fs_info(sbi, L"Wrong NAT boundary, start(%u) end(%u) blocks(%u)",
			  nat_blkaddr, ssa_blkaddr,
			  segment_count_nat << log_blocks_per_seg);
		return true;
	}

	if (ssa_blkaddr + (segment_count_ssa << log_blocks_per_seg) !=
							main_blkaddr) {
		f2fs_info(sbi, L"Wrong SSA boundary, start(%u) end(%u) blocks(%u)",
			  ssa_blkaddr, main_blkaddr,
			  segment_count_ssa << log_blocks_per_seg);
		return true;
	}
	//<YUAN>错误时返回true，正常返回0
	if (main_end_blkaddr > seg_end_blkaddr) 
	{
		f2fs_info(sbi, L"Wrong MAIN_AREA boundary, start(%u) end(%llu) block(%u)",
			  main_blkaddr, seg_end_blkaddr,
			  segment_count_main << log_blocks_per_seg);
		return true;
	}
	else if (main_end_blkaddr < seg_end_blkaddr) 
	{
		int err = 0;
		const wchar_t *res;

		/* fix in-memory information all the time */
		raw_super->segment_count = cpu_to_le32((main_end_blkaddr - segment0_blkaddr) >> log_blocks_per_seg);

		if (sbi->f2fs_readonly() || bdev_read_only(sb->s_bdev)) 
		{
			sbi->set_sbi_flag(SBI_NEED_SB_WRITE);
			res = L"internally";
		} 
		else 
		{
			err = __f2fs_commit_super(bh, NULL);
			res = err ? L"failed" : L"done";
		}
		f2fs_info(sbi, L"Fix alignment : %s, start(%u) end(%llu) block(%u)",
			  res, main_blkaddr, seg_end_blkaddr,
			  segment_count_main << log_blocks_per_seg);
		if (err)
			return true;
	}
	return false;
}


int CF2fsFileSystem::sanity_check_raw_super(f2fs_sb_info *sbi, CBufferHead *bh)
{
	block_t segment_count, segs_per_sec, secs_per_zone, segment_count_main;
	block_t total_sections, blocks_per_seg;
	struct f2fs_super_block *raw_super = (struct f2fs_super_block *) (bh->Data() + F2FS_SUPER_OFFSET);
	size_t crc_offset = 0;
	__u32 crc = 0;

	if (le32_to_cpu(raw_super->magic) != F2FS_SUPER_MAGIC) 
	{
		f2fs_info(sbi, L"Magic Mismatch, valid(0x%x) - read(0x%x)", F2FS_SUPER_MAGIC, le32_to_cpu(raw_super->magic));
		return -EINVAL;
	}

	/* Check checksum_offset and crc in superblock */
	if (__F2FS_HAS_FEATURE(raw_super, F2FS_FEATURE_SB_CHKSUM)) 
	{
		crc_offset = le32_to_cpu(raw_super->checksum_offset);
		if (crc_offset != offsetof(struct f2fs_super_block, crc)) 
		{
			f2fs_info(sbi, L"Invalid SB checksum offset: %zu", crc_offset);
			return -EFSCORRUPTED;
		}
		crc = le32_to_cpu(raw_super->crc);
		if (!f2fs_crc_valid(sbi, crc, raw_super, crc_offset)) 
		{
			f2fs_info(sbi, L"Invalid SB checksum value: %u", crc);
			return -EFSCORRUPTED;
		}
	}

	/* Currently, support only 4KB block size */
	if (le32_to_cpu(raw_super->log_blocksize) != F2FS_BLKSIZE_BITS) 
	{
		f2fs_info(sbi, L"Invalid log_blocksize (%u), supports only %u", le32_to_cpu(raw_super->log_blocksize),  F2FS_BLKSIZE_BITS);
		return -EFSCORRUPTED;
	}

	/* check log blocks per segment */
	if (le32_to_cpu(raw_super->log_blocks_per_seg) != 9) {
		f2fs_info(sbi, L"Invalid log blocks per segment (%u)", le32_to_cpu(raw_super->log_blocks_per_seg));
		return -EFSCORRUPTED;
	}

	/* Currently, support 512/1024/2048/4096 bytes sector size */
	if (le32_to_cpu(raw_super->log_sectorsize) > F2FS_MAX_LOG_SECTOR_SIZE 
		|| le32_to_cpu(raw_super->log_sectorsize) <	F2FS_MIN_LOG_SECTOR_SIZE) 
	{
		f2fs_info(sbi, L"Invalid log sectorsize (%u)", le32_to_cpu(raw_super->log_sectorsize));
		return -EFSCORRUPTED;
	}
	if (le32_to_cpu(raw_super->log_sectors_per_block) +	le32_to_cpu(raw_super->log_sectorsize) !=	F2FS_MAX_LOG_SECTOR_SIZE) 
	{
		f2fs_info(sbi, L"Invalid log sectors per block(%u) log sectorsize(%u)",
			  le32_to_cpu(raw_super->log_sectors_per_block),
			  le32_to_cpu(raw_super->log_sectorsize));
		return -EFSCORRUPTED;
	}

	segment_count = le32_to_cpu(raw_super->segment_count);
	segment_count_main = le32_to_cpu(raw_super->segment_count_main);
	segs_per_sec = le32_to_cpu(raw_super->segs_per_sec);
	secs_per_zone = le32_to_cpu(raw_super->secs_per_zone);
	total_sections = le32_to_cpu(raw_super->section_count);

	/* blocks_per_seg should be 512, given the above check */
	blocks_per_seg = 1 << le32_to_cpu(raw_super->log_blocks_per_seg);

	if (segment_count > F2FS_MAX_SEGMENT || segment_count < F2FS_MIN_SEGMENTS) 
	{
		f2fs_info(sbi, L"Invalid segment count (%u)", segment_count);
		return -EFSCORRUPTED;
	}

	if (total_sections > segment_count_main || total_sections < 1 ||
			segs_per_sec > segment_count || !segs_per_sec) 
	{
		f2fs_info(sbi, L"Invalid segment/section count (%u, %u x %u)",  segment_count, total_sections, segs_per_sec);
		return -EFSCORRUPTED;
	}

	if (segment_count_main != total_sections * segs_per_sec) 
	{
		f2fs_info(sbi, L"Invalid segment/section count (%u != %u * %u)",  segment_count_main, total_sections, segs_per_sec);
		return -EFSCORRUPTED;
	}

	if ((segment_count / segs_per_sec) < total_sections) {
		f2fs_info(sbi, L"Small segment_count (%u < %u * %u)",			  segment_count, segs_per_sec, total_sections);
		return -EFSCORRUPTED;
	}

	if (segment_count > (le64_to_cpu(raw_super->block_count) >> 9)) {
		f2fs_info(sbi, L"Wrong segment_count / block_count (%u > %llu)",  segment_count, le64_to_cpu(raw_super->block_count));
		return -EFSCORRUPTED;
	}

//	if (raw_super->devs[0].total_segments != 0xFFFFFFFF)
	if (raw_super->devs[0].path[0] != 0)
	{
		block_t dev_seg_count = le32_to_cpu(RDEV(0).total_segments);
		int i = 1;

		while (i < MAX_DEVICES && raw_super->devs[i].path[0] != 0)
		{
			dev_seg_count += le32_to_cpu(raw_super->devs[i].total_segments);
			i++;
		}
		if (segment_count != dev_seg_count) 
		{
			LOG_ERROR(L"[err] Segment count (%u) mismatch with total segments from devices (%u)", segment_count, dev_seg_count);
			return -EFSCORRUPTED;
		}
	} 
	else 
	{
		if (__F2FS_HAS_FEATURE(raw_super, F2FS_FEATURE_BLKZONED) && !bdev_is_zoned(sbi->s_bdev)) 
		{
			f2fs_info(sbi, L"Zoned block device path is missing");
			return -EFSCORRUPTED;
		}
	}

	if (secs_per_zone > total_sections || !secs_per_zone) 
	{
		f2fs_info(sbi, L"Wrong secs_per_zone / total_sections (%u, %u)", secs_per_zone, total_sections);
		return -EFSCORRUPTED;
	}
	if (le32_to_cpu(raw_super->extension_count) > F2FS_MAX_EXTENSION ||
			raw_super->hot_ext_count > F2FS_MAX_EXTENSION ||
			(le32_to_cpu(raw_super->extension_count) +
			raw_super->hot_ext_count) > F2FS_MAX_EXTENSION) 
	{
		f2fs_info(sbi, L"Corrupted extension count (%u + %u > %u)",
			  le32_to_cpu(raw_super->extension_count),
			  raw_super->hot_ext_count,
			  F2FS_MAX_EXTENSION);
		return -EFSCORRUPTED;
	}

	if (le32_to_cpu(raw_super->cp_payload) > (blocks_per_seg - F2FS_CP_PACKS)) 
	{
		f2fs_info(sbi, L"Insane cp_payload (%u > %u)",
			  le32_to_cpu(raw_super->cp_payload),
			  blocks_per_seg - F2FS_CP_PACKS);
		return -EFSCORRUPTED;
	}

	/* check reserved ino info */
	if (le32_to_cpu(raw_super->node_ino) != 1 ||
		le32_to_cpu(raw_super->meta_ino) != 2 ||
		le32_to_cpu(raw_super->root_ino) != 3) {
		f2fs_info(sbi, L"Invalid Fs Meta Ino: node(%u) meta(%u) root(%u)",
			  le32_to_cpu(raw_super->node_ino),
			  le32_to_cpu(raw_super->meta_ino),
			  le32_to_cpu(raw_super->root_ino));
		return -EFSCORRUPTED;
	}

	/* check CP/SIT/NAT/SSA/MAIN_AREA area boundary */
	if (sanity_check_area_boundary(sbi, bh))	return -EFSCORRUPTED;

	return 0;
}


int f2fs_sanity_check_ckpt(struct f2fs_sb_info *sbi)
{
	unsigned int total, fsmeta;
	struct f2fs_super_block *raw_super = sbi->F2FS_RAW_SUPER();
	struct f2fs_checkpoint *ckpt = sbi->F2FS_CKPT();
	unsigned int ovp_segments, reserved_segments;
	unsigned int main_segs, blocks_per_seg;
	unsigned int sit_segs, nat_segs;
	unsigned int sit_bitmap_size, nat_bitmap_size;
	unsigned int log_blocks_per_seg;
	unsigned int segment_count_main;
	unsigned int cp_pack_start_sum, cp_payload;
	block_t user_block_count, valid_user_blocks;
	block_t avail_node_count, valid_node_count;
	int i, j;

	total = le32_to_cpu(raw_super->segment_count);
	fsmeta = le32_to_cpu(raw_super->segment_count_ckpt);
	sit_segs = le32_to_cpu(raw_super->segment_count_sit);
	fsmeta += sit_segs;
	nat_segs = le32_to_cpu(raw_super->segment_count_nat);
	fsmeta += nat_segs;
	fsmeta += le32_to_cpu(ckpt->rsvd_segment_count);
	fsmeta += le32_to_cpu(raw_super->segment_count_ssa);

	if (unlikely(fsmeta >= total))
		return 1;

	ovp_segments = le32_to_cpu(ckpt->overprov_segment_count);
	reserved_segments = le32_to_cpu(ckpt->rsvd_segment_count);

	if (unlikely(fsmeta < F2FS_MIN_META_SEGMENTS ||	ovp_segments == 0 || reserved_segments == 0))
	{
		f2fs_err(sbi, L"Wrong layout: check mkfs.f2fs version");
		return 1;
	}

	user_block_count = boost::numeric_cast<block_t>(le64_to_cpu(ckpt->user_block_count));
	segment_count_main = le32_to_cpu(raw_super->segment_count_main);
	log_blocks_per_seg = le32_to_cpu(raw_super->log_blocks_per_seg);
	if (!user_block_count || user_block_count >= segment_count_main << log_blocks_per_seg) 
	{
		f2fs_err(sbi, L"Wrong user_block_count: %u",	 user_block_count);
		return 1;
	}

	valid_user_blocks = boost::numeric_cast<block_t>(le64_to_cpu(ckpt->valid_block_count));
	if (valid_user_blocks > user_block_count)
	{
		LOG_ERROR(L"[err] Wrong valid_user_blocks: %u, user_block_count: %u", valid_user_blocks, user_block_count);
		return 1;
	}

	valid_node_count = le32_to_cpu(ckpt->valid_node_count);
	avail_node_count = sbi->total_node_count - F2FS_RESERVED_NODE_NUM;
	if (valid_node_count > avail_node_count) {
		f2fs_err(sbi, L"Wrong valid_node_count: %u, avail_node_count: %u",
			 valid_node_count, avail_node_count);
		return 1;
	}

	main_segs = le32_to_cpu(raw_super->segment_count_main);
	blocks_per_seg = sbi->blocks_per_seg;

	for (i = 0; i < NR_CURSEG_NODE_TYPE; i++) {
		if (le32_to_cpu(ckpt->cur_node_segno[i]) >= main_segs ||
			le16_to_cpu(ckpt->cur_node_blkoff[i]) >= blocks_per_seg)
			return 1;
		for (j = i + 1; j < NR_CURSEG_NODE_TYPE; j++) {
			if (le32_to_cpu(ckpt->cur_node_segno[i]) ==
				le32_to_cpu(ckpt->cur_node_segno[j])) {
				f2fs_err(sbi, L"Node segment (%u, %u) has the same segno: %u",
					 i, j, le32_to_cpu(ckpt->cur_node_segno[i]));
				return 1;
			}
		}
	}
	for (i = 0; i < NR_CURSEG_DATA_TYPE; i++) {
		if (le32_to_cpu(ckpt->cur_data_segno[i]) >= main_segs ||
			le16_to_cpu(ckpt->cur_data_blkoff[i]) >= blocks_per_seg)
			return 1;
		for (j = i + 1; j < NR_CURSEG_DATA_TYPE; j++) {
			if (le32_to_cpu(ckpt->cur_data_segno[i]) ==
				le32_to_cpu(ckpt->cur_data_segno[j])) {
				f2fs_err(sbi, L"Data segment (%u, %u) has the same segno: %u",
					 i, j, le32_to_cpu(ckpt->cur_data_segno[i]));
				return 1;
			}
		}
	}
	for (i = 0; i < NR_CURSEG_NODE_TYPE; i++) {
		for (j = 0; j < NR_CURSEG_DATA_TYPE; j++) {
			if (le32_to_cpu(ckpt->cur_node_segno[i]) ==
				le32_to_cpu(ckpt->cur_data_segno[j])) {
				f2fs_err(sbi, L"Node segment (%u) and Data segment (%u) has the same segno: %u",
					 i, j, le32_to_cpu(ckpt->cur_node_segno[i]));
				return 1;
			}
		}
	}

	sit_bitmap_size = le32_to_cpu(ckpt->sit_ver_bitmap_bytesize);
	nat_bitmap_size = le32_to_cpu(ckpt->nat_ver_bitmap_bytesize);

	if (sit_bitmap_size != ((sit_segs / 2) << log_blocks_per_seg) / 8 ||
		nat_bitmap_size != ((nat_segs / 2) << log_blocks_per_seg) / 8) {
		f2fs_err(sbi, L"Wrong bitmap size: sit: %u, nat:%u", sit_bitmap_size, nat_bitmap_size);
		return 1;
	}

	cp_pack_start_sum = __start_sum_addr(sbi);
	cp_payload = sbi->__cp_payload();
	if (cp_pack_start_sum < cp_payload + 1 ||
		cp_pack_start_sum > blocks_per_seg - 1 -
			NR_CURSEG_PERSIST_TYPE) {
		f2fs_err(sbi, L"Wrong cp_pack_start_sum: %u", cp_pack_start_sum);
		return 1;
	}

	if (__is_set_ckpt_flags(ckpt, CP_LARGE_NAT_BITMAP_FLAG) &&
		le32_to_cpu(ckpt->checksum_offset) != CP_MIN_CHKSUM_OFFSET) {
		f2fs_warn(sbi, L"using deprecated layout of large_nat_bitmap, "
			  "please run fsck v1.13.0 or higher to repair, chksum_offset: %u, "
			  "fixed with patch: \"f2fs-tools: relocate chksum_offset for large_nat_bitmap feature\"",
			  le32_to_cpu(ckpt->checksum_offset));
		return 1;
	}

	if (unlikely(sbi->f2fs_cp_error())) 
	{
		f2fs_err(sbi, L"A bug case: need to run fsck");
		return 1;
	}
	return 0;
}
#if 0
#endif


//static void init_sb_info(struct f2fs_sb_info *sbi)
void f2fs_sb_info::init_sb_info()
{
//	struct f2fs_super_block *raw_super = sbi->raw_super;
	f2fs_sb_info* sbi = this;
	int i;

	sbi->log_sectors_per_block = le32_to_cpu(raw_super->log_sectors_per_block);
	sbi->log_blocksize = le32_to_cpu(raw_super->log_blocksize);
	sbi->blocksize = 1 << sbi->log_blocksize;
	sbi->log_blocks_per_seg = le32_to_cpu(raw_super->log_blocks_per_seg);
	sbi->blocks_per_seg = 1 << sbi->log_blocks_per_seg;
	sbi->segs_per_sec = le32_to_cpu(raw_super->segs_per_sec);
	sbi->secs_per_zone = le32_to_cpu(raw_super->secs_per_zone);
	sbi->total_sections = le32_to_cpu(raw_super->section_count);
	sbi->total_node_count =	(le32_to_cpu(raw_super->segment_count_nat) / 2)	* sbi->blocks_per_seg * NAT_ENTRY_PER_BLOCK;
	F2FS_ROOT_INO(sbi) = le32_to_cpu(raw_super->root_ino);
	F2FS_NODE_INO(sbi) = le32_to_cpu(raw_super->node_ino);
	F2FS_META_INO(sbi) = le32_to_cpu(raw_super->meta_ino);
	sbi->cur_victim_sec = NULL_SECNO;
	sbi->next_victim_seg[BG_GC] = NULL_SEGNO;
	sbi->next_victim_seg[FG_GC] = NULL_SEGNO;
	sbi->max_victim_search = DEF_MAX_VICTIM_SEARCH;
	sbi->migration_granularity = sbi->segs_per_sec;

	sbi->dir_level = DEF_DIR_LEVEL;
	sbi->interval_time[CP_TIME] = DEF_CP_INTERVAL;
	sbi->interval_time[REQ_TIME] = DEF_IDLE_INTERVAL;
	sbi->interval_time[DISCARD_TIME] = DEF_IDLE_INTERVAL;
	sbi->interval_time[GC_TIME] = DEF_IDLE_INTERVAL;
	sbi->interval_time[DISABLE_TIME] = DEF_DISABLE_INTERVAL;
	sbi->interval_time[UMOUNT_DISCARD_TIMEOUT] = DEF_UMOUNT_DISCARD_TIMEOUT;
	clear_sbi_flag(sbi, SBI_NEED_FSCK);

	for (i = 0; i < NR_COUNT_TYPE; i++)
		atomic_set(&sbi->nr_pages[i], 0);

	for (i = 0; i < META; i++)
		atomic_set(&sbi->wb_sync_req[i], 0);

	INIT_LIST_HEAD(&sbi->s_list);
	mutex_init(&sbi->umount_mutex);
	init_rwsem(&sbi->io_order_lock);
	spin_lock_init(&sbi->cp_lock);

	sbi->dirty_device = 0;
	spin_lock_init(&sbi->dev_lock);

	init_rwsem(&sbi->sb_lock);
	init_rwsem(&sbi->pin_sem);
}

static int init_percpu_info(struct f2fs_sb_info *sbi)
{
	int err = 0;

#if 0
	err = percpu_counter_init(&sbi->alloc_valid_block_count, 0, GFP_KERNEL);
	if (err)	return err;

	err = percpu_counter_init(&sbi->total_valid_inode_count, 0,	GFP_KERNEL);
	if (err) percpu_counter_destroy(&sbi->alloc_valid_block_count);
#endif

	return err;
}

#if 0

#ifdef CONFIG_BLK_DEV_ZONED

struct f2fs_report_zones_args {
	struct f2fs_dev_info *dev;
	bool zone_cap_mismatch;
};

static int f2fs_report_zone_cb(struct blk_zone *zone, unsigned int idx,
			      void *data)
{
	struct f2fs_report_zones_args *rz_args = data;

	if (zone->type == BLK_ZONE_TYPE_CONVENTIONAL)
		return 0;

	set_bit(idx, rz_args->dev->blkz_seq);
	rz_args->dev->zone_capacity_blocks[idx] = zone->capacity >>
						F2FS_LOG_SECTORS_PER_BLOCK;
	if (zone->len != zone->capacity && !rz_args->zone_cap_mismatch)
		rz_args->zone_cap_mismatch = true;

	return 0;
}

static int init_blkz_info(struct f2fs_sb_info *sbi, int devi)
{
	block_device *bdev = FDEV(devi).bdev;
	sector_t nr_sectors = bdev_nr_sectors(bdev);
	struct f2fs_report_zones_args rep_zone_arg;
	int ret;

	if (!f2fs_sb_has_blkzoned(sbi))
		return 0;

	if (sbi->blocks_per_blkz && sbi->blocks_per_blkz !=
				SECTOR_TO_BLOCK(bdev_zone_sectors(bdev)))
		return -EINVAL;
	sbi->blocks_per_blkz = SECTOR_TO_BLOCK(bdev_zone_sectors(bdev));
	if (sbi->log_blocks_per_blkz && sbi->log_blocks_per_blkz !=
				__ilog2_u32(sbi->blocks_per_blkz))
		return -EINVAL;
	sbi->log_blocks_per_blkz = __ilog2_u32(sbi->blocks_per_blkz);
	FDEV(devi).nr_blkz = SECTOR_TO_BLOCK(nr_sectors) >>
					sbi->log_blocks_per_blkz;
	if (nr_sectors & (bdev_zone_sectors(bdev) - 1))
		FDEV(devi).nr_blkz++;

	FDEV(devi).blkz_seq = f2fs_kvzalloc(sbi,
					BITS_TO_LONGS(FDEV(devi).nr_blkz)
					* sizeof(unsigned long),
					GFP_KERNEL);
	if (!FDEV(devi).blkz_seq)
		return -ENOMEM;

	/* Get block zones type and zone-capacity */
	FDEV(devi).zone_capacity_blocks = f2fs_kzalloc(sbi,
					FDEV(devi).nr_blkz * sizeof(block_t),
					GFP_KERNEL);
	if (!FDEV(devi).zone_capacity_blocks)
		return -ENOMEM;

	rep_zone_arg.dev = &FDEV(devi);
	rep_zone_arg.zone_cap_mismatch = false;

	ret = blkdev_report_zones(bdev, 0, BLK_ALL_ZONES, f2fs_report_zone_cb,
				  &rep_zone_arg);
	if (ret < 0)
		return ret;

	if (!rep_zone_arg.zone_cap_mismatch) {
		kfree(FDEV(devi).zone_capacity_blocks);
		FDEV(devi).zone_capacity_blocks = NULL;
	}

	return 0;
}
#endif

#endif

/* Read f2fs raw super block.
 * Because we have two copies of super block, so read both of them to get the first valid one. If any one of them is
	broken, we pass them recovery flag back to the caller. */
int CF2fsFileSystem::read_raw_super_block(f2fs_sb_info *sbi, f2fs_super_block * &raw_super, int *valid_super_block, 
		int *recovery)
{
	super_block* sb = static_cast<super_block*>(sbi);
	int block;
	f2fs_super_block *super;
	int err = 0;

//	super = kzalloc(sizeof(struct f2fs_super_block), GFP_KERNEL);
	super = new f2fs_super_block;
	if (!super)	return -ENOMEM;

	for (block = 0; block < 2; block++) 
	{
		CBufferHead * bh = bread(block, m_sb_info->s_blocksize);
		if (!bh) 
		{
			f2fs_err(sbi, L"Unable to read %dth superblock", block + 1);
			err = -EIO;
			*recovery = 1;
			continue;
		}

		/* sanity checking of raw super */
		//<YUAN>优化：向下传递f2fs_super_block，返回是否需要更新。如果有更新在这里调用
		err = sanity_check_raw_super(sbi, bh);
		if (err) 
		{
			f2fs_err(sbi, L"Can't find valid F2FS filesystem in %dth superblock", block + 1);
			//brelse(bh);
			bh->Release();
			*recovery = 1;
			continue;
		}

		if (!raw_super) 
		{
			BYTE* data = bh->Lock();
			memcpy(super, data + F2FS_SUPER_OFFSET, sizeof(*super));
			bh->Unlock();
			*valid_super_block = block;
			raw_super = super;
		}
		//brelse(bh);
		bh->Release();
	}

	/* No valid superblock */
	if (!raw_super)	delete super;
	else				err = 0;
	return err;
}

#if 0

int f2fs_commit_super(struct f2fs_sb_info *sbi, bool recover)
{
	struct CBufferHead *bh;
	__u32 crc = 0;
	int err;

	if ((recover && sbi->f2fs_readonly(->sb)) ||
				bdev_read_only(sbi->s_bdev)) {
		sbi->set_sbi_flag(SBI_NEED_SB_WRITE);
		return -EROFS;
	}

	/* we should update superblock crc here */
	if (!recover && f2fs_sb_has_sb_chksum(sbi)) {
		crc = f2fs_crc32(sbi, sbi->F2FS_RAW_SUPER(),
				offsetof(struct f2fs_super_block, crc));
		sbi->F2FS_RAW_SUPER()->crc = cpu_to_le32(crc);
	}

	/* write back-up superblock first */
	bh = sb_bread(sbi->sb, sbi->valid_super_block ? 0 : 1);
	if (!bh)
		return -EIO;
	err = __f2fs_commit_super(bh, sbi->F2FS_RAW_SUPER());
	brelse(bh);

	/* if we are in recovery path, skip writing valid superblock */
	if (recover || err)
		return err;

	/* write current valid superblock */
	bh = sb_bread(sbi->sb, sbi->valid_super_block);
	if (!bh)
		return -EIO;
	err = __f2fs_commit_super(bh, sbi->F2FS_RAW_SUPER());
	brelse(bh);
	return err;
}
#endif

int f2fs_sb_info::f2fs_scan_devices(void)
{
	struct f2fs_super_block *raw_super = this->F2FS_RAW_SUPER();
	unsigned int max_devices = MAX_DEVICES;

	/* Initialize single device information */
	if (!RDEV(0).path[0])
	{
		if (!bdev_is_zoned(this->s_bdev))	return 0;
		max_devices = 1;
	}

	/* Initialize multiple devices information, or single zoned block device information. */
//	this->devs = new f2fs_dev_info[max_devices];
	this->devs = f2fs_kzalloc<f2fs_dev_info>(this, max_devices/*, GFP_KERNEL*/);
	if (!this->devs)	return -ENOMEM;

	for (UINT i = 0; i < max_devices; i++) 
	{
		if (i > 0 && !RDEV(i).path[0])			break;
		if (max_devices == 1) 
		{		/* Single zoned block device mount */
//			FDEV(0).bdev = blkdev_get_by_dev(this->s_bdev->bd_dev, this->s_mode, this->s_type);
			this->devs[0].m_disk = m_fs->m_config.devices[0].m_fd;
		} 
		else 
		{	/* Multi-device mount */
			memcpy(devs[i].path, RDEV(i).path, MAX_PATH_LEN);
			devs[i].total_segments = le32_to_cpu(RDEV(i).total_segments);
			if (i == 0) 
			{
				devs[i].start_blk = 0;
				devs[i].end_blk = devs[i].start_blk + (devs[i].total_segments << this->log_blocks_per_seg) - 1 +
				    le32_to_cpu(raw_super->segment0_blkaddr);
			} 
			else 
			{
				devs[i].start_blk = devs[i - 1].end_blk + 1;
				devs[i].end_blk = devs[i].start_blk + (devs[i].total_segments << this->log_blocks_per_seg) - 1;
			}
//			devs[i].bdev = blkdev_get_by_path(devs[i].path,	this->s_mode, this->s_type);
			this->devs[i].m_disk = m_fs->m_config.devices[i].m_fd;
		}
		if (IS_ERR(this->devs[i].m_disk)) THROW_ERROR(ERR_APP, L"disk %d is invalid", i);
			//return PTR_ERR(this->devs[i].m_disk);

		/* to release errored devices */
		this->s_ndevs = i + 1;

#ifdef CONFIG_BLK_DEV_ZONED
		if (bdev_zoned_model(devs[i].bdev) == BLK_ZONED_HM &&
				!f2fs_sb_has_blkzoned(this)) {
			f2fs_err(this, "Zoned block device feature not enabled\n");
			return -EINVAL;
		}
		if (bdev_zoned_model(devs[i].bdev) != BLK_ZONED_NONE) {
			if (init_blkz_info(this, i)) {
				f2fs_err(this, "Failed to initialize F2FS blkzone information");
				return -EINVAL;
			}
			if (max_devices == 1)
				break;
			f2fs_info(this, "Mount Device [%2d]: %20s, %8u, %8x - %8x (zone: %s)",
				  i, devs[i].path,
				  devs[i].total_segments,
				  devs[i].start_blk, devs[i].end_blk,
				  bdev_zoned_model(devs[i].bdev) == BLK_ZONED_HA ?
				  "Host-aware" : "Host-managed");
			continue;
		}
#endif
		LOG_NOTICE(L"Mount Device [%2d]: Seg=%8u, Blk:%8X - %8X", i, devs[i].total_segments, devs[i].start_blk, devs[i].end_blk);
	}
	LOG_NOTICE(L"IO Block Size: %8d KB", F2FS_IO_SIZE_KB(this));
	return 0;
}


static int f2fs_setup_casefold(struct f2fs_sb_info *sbi)
{
#ifdef CONFIG_UNICODE
	if (f2fs_sb_has_casefold(sbi) && !sbi->s_encoding) 
	{
		const struct f2fs_sb_encodings *encoding_info;
		unicode_map *encoding = NULL;
		UINT16 encoding_flags;

		if (f2fs_sb_read_encoding(sbi->raw_super, &encoding_info,  &encoding_flags)) 
		{
			f2fs_err(sbi, L"Encoding requested by superblock is unknown");
			return -EINVAL;
		}

#if 0
		encoding = utf8_load(encoding_info->version);
		if (IS_ERR(encoding)) 
		{
			f2fs_err(sbi, L"can't mount with superblock charset: %s-%s not supported by the kernel. flags: 0x%x.",
				 encoding_info->name, encoding_info->version,
				 encoding_flags);
			return PTR_ERR(encoding);
		}
		LOG_NOTICE(L"Using encoding defined by superblock: %S-%S with flags 0x%hx", encoding_info->name,
			 encoding_info->version?encoding_info->version:"\b", encoding_flags);
#endif
		sbi->s_encoding = encoding;
		sbi->s_encoding_flags = encoding_flags;
	}
#else
	if (f2fs_sb_has_casefold(sbi)) {
		f2fs_err(sbi, "Filesystem with casefold feature cannot be mounted without CONFIG_UNICODE");
		return -EINVAL;
	}
#endif
	return 0;
}

static void f2fs_tuning_parameters(struct f2fs_sb_info *sbi)
{
	f2fs_sm_info* sm_i = sbi->sm_info;

	/* adjust parameters according to the volume size */
	if (sm_i->main_segments <= SMALL_VOLUME_SEGMENTS) 
	{
		F2FS_OPTION(sbi).alloc_mode = ALLOC_MODE_REUSE;
//		sm_i->dcc_info->discard_granularity = 1;
		sm_i->dcc_info->set_discard_granularity(1);
		sm_i->ipu_policy = 1 << F2FS_IPU_FORCE;
	}

	sbi->readdir_ra = 1;
}


int f2fs_sb_info::f2fs_fill_super(const std::wstring & str_option, int silent)
{
	JCASSERT(m_fs);
	f2fs_sb_info *sbi = this;
	super_block* sb = static_cast<super_block*>(sbi);
//	sbi->m_fs = this;

	f2fs_super_block *raw_super;
	int err;
	bool skip_recovery = false, need_fsck = false;
//	char *options = NULL;
	int recovery, i, valid_super_block;
	struct curseg_info *seg_i;
	int retry_cnt = 1;

	enum {STATE_SUCCEED = 0, STATE_SYNC_FREE_META} state;

	//try_onemore:
	while (retry_cnt > 0)
	{
		err = -EINVAL;
		raw_super = NULL;
		valid_super_block = -1;
		recovery = 0;
		//<YUAN>用于表示内存管理状态，错误是需要清除那些内存。
		state = STATE_SUCCEED;
		try
		{
			/* allocate memory for f2fs-specific super block info */
			m_fs->m_inodes.Init(sb);
#if 0 //<TODO>
			/* Load the checksum driver */
			sbi->s_chksum_driver = crypto_alloc_shash("crc32", 0, 0);
			if (IS_ERR(sbi->s_chksum_driver))
			{
				f2fs_err(sbi, "Cannot load crc32 driver.");
				err = PTR_ERR(sbi->s_chksum_driver);
				sbi->s_chksum_driver = NULL;
				goto free_sbi;
			}
#endif
			/* set a block size */
			if (unlikely(!m_fs->sb_set_blocksize(F2FS_BLKSIZE)))
			{
				f2fs_err(sbi, L"unable to set blocksize");
				THROW_ERROR(ERR_APP, L"unable to set blocksize");
				//		goto free_sbi;
			}

			err = m_fs->read_raw_super_block(sbi, raw_super, &valid_super_block, &recovery);
			//	if (err) goto free_sbi;
			if (err) THROW_ERROR(ERR_APP, L"failed on reading raw super block");

//			sb->s_fs_info = sbi;
			sbi->raw_super = raw_super;

			/* precompute checksum seed for metadata */
			if (f2fs_sb_has_inode_chksum(sbi))
				sbi->s_chksum_seed = f2fs_chksum(sbi, ~0, raw_super->uuid, sizeof(raw_super->uuid));

			default_options(sbi);
			/* parse mount options */
		//	options = kstrdup((const char *)data, GFP_KERNEL);
		//	std::string options((char*)data);
			//if (data && !options) {
			//	err = -ENOMEM;
			//	goto free_sb_buf;
			//}

			if (!str_option.empty())
			{
				boost::property_tree::wptree pt_option;
				std::wstringstream stream;
				stream.str(str_option);
				boost::property_tree::read_json(stream, pt_option);
				err = m_fs->parse_mount_options(sb, pt_option, false);
				//		if (err)	goto free_options;
				if (err)	THROW_ERROR(ERR_APP, L"failed on set options");
			}
			//else load_default_options(true);

			//err = parse_options(sb, options.c_str(), false);
			//if (err)		goto free_options;

			sb->s_maxbytes = max_file_blocks(NULL) << le32_to_cpu(raw_super->log_blocksize);
			sb->s_max_links = F2FS_LINK_MAX;

			err = f2fs_setup_casefold(sbi);
//			if (err)	goto free_options;
			if (err)	THROW_ERROR(ERR_APP, L"failed on setting case fold");


#ifdef CONFIG_QUOTA
			sb->dq_op = &f2fs_quota_operations;
			sb->s_qcop = &f2fs_quotactl_ops;
			sb->s_quota_types = QTYPE_MASK_USR | QTYPE_MASK_GRP | QTYPE_MASK_PRJ;

			if (f2fs_sb_has_quota_ino(sbi))
			{
				for (i = 0; i < MAXQUOTAS; i++)
				{
					if (f2fs_qf_ino(sbi->sb, i))
						sbi->nquota_files++;
				}
			}
#endif

//			sb->s_op = &f2fs_sops;

#ifdef CONFIG_FS_ENCRYPTION
			sb->s_cop = &f2fs_cryptops;
#endif
#ifdef CONFIG_FS_VERITY
			sb->s_vop = &f2fs_verityops;
#endif
			sb->s_xattr = f2fs_xattr_handlers;
//			sb->s_export_op = &f2fs_export_ops;
			sb->s_magic = F2FS_SUPER_MAGIC;
			sb->s_time_gran = 1;
			sb->s_flags = (sb->s_flags & ~SB_POSIXACL) | (test_opt(sbi, POSIX_ACL) ? SB_POSIXACL : 0);
			memcpy(&sb->s_uuid, raw_super->uuid, sizeof(raw_super->uuid));
			sb->s_iflags |= SB_I_CGROUPWB;

			/* init f2fs-specific super block info */
			sbi->valid_super_block = valid_super_block;
			init_rwsem(&sbi->gc_lock);
			mutex_init(&sbi->writepages);
			init_rwsem(&sbi->cp_global_sem);
			init_rwsem(&sbi->node_write);
			init_rwsem(&sbi->node_change);

			/* disallow all the data/node/meta page writes */
			set_sbi_flag(SBI_POR_DOING);
			spin_lock_init(&sbi->stat_lock);

			/* init iostat info */
			spin_lock_init(&sbi->iostat_lock);
			sbi->iostat_enable = false;
			sbi->iostat_period_ms = DEFAULT_IOSTAT_PERIOD_MS;

			for (i = 0; i < NR_PAGE_TYPE; i++)
			{
				int n = (i == META) ? 1 : NR_TEMP_TYPE;
				int j;

				//		sbi->write_io[i] = (f2fs_bio_info*)f2fs_kmalloc(sbi, array_size(n, sizeof(struct f2fs_bio_info))/*, GFP_KERNEL*/);
				sbi->write_io[i] = f2fs_kmalloc<f2fs_bio_info>(sbi, n);
				if (!sbi->write_io[i])
				{
					err = -ENOMEM;
					THROW_ERROR(ERR_MEM, L"failed on allocating f2fs_bio_info, n=%d", i);
					//goto free_bio_info;
				}

				for (j = HOT; j < n; j++)
				{
					init_rwsem(&sbi->write_io[i][j].io_rwsem);
					sbi->write_io[i][j].sbi = sbi;
					sbi->write_io[i][j].bio = NULL;
					spin_lock_init(&sbi->write_io[i][j].io_lock);
					INIT_LIST_HEAD(&sbi->write_io[i][j].io_list);
					INIT_LIST_HEAD(&sbi->write_io[i][j].bio_list);
					init_rwsem(&sbi->write_io[i][j].bio_list_lock);
				}
			}

			init_rwsem(&sbi->cp_rwsem);
			init_rwsem(&sbi->quota_sem);
			//	init_waitqueue_head(&sbi->cp_wait);
			init_sb_info();

			err = init_percpu_info(sbi);
//			if (err) goto free_bio_info;
			if (err) THROW_ERROR(ERR_APP, L"failed on init percup info");

			if (F2FS_IO_ALIGNED(sbi))
			{
#if 0 //<TODO>
				sbi->write_io_dummy = mempool_create_page_pool(2 * (F2FS_IO_SIZE(sbi) - 1), 0);
				if (!sbi->write_io_dummy)
				{
					err = -ENOMEM;
					goto free_percpu;
				}
#endif
			}

			/* init per sbi slab cache */ //<YUAN>暂时不支持xattr
			err = f2fs_init_xattr_caches(sbi);
//			if (err)		goto free_io_dummy;
			if (err)	THROW_ERROR(ERR_APP, L"failed on init xatr_caches");
			err = f2fs_init_page_array_cache(sbi);	//<YUAN>暂不支持compress
			//if (err)		goto free_xattr_cache;
			if (err)	THROW_ERROR(ERR_APP, L"failed on init page array cache");

			/* get an inode for meta space */
			sbi->meta_inode = f2fs_iget(F2FS_META_INO(sbi));
			if (IS_ERR(sbi->meta_inode))
			{
//				f2fs_err(sbi, L"Failed to read F2FS meta data inode");
//				err = PTR_ERR(sbi->meta_inode);
				//goto free_page_array_cache;
				THROW_ERROR(ERR_APP, L"Failed to read F2FS meta data inode");
			}

			err = f2fs_get_valid_checkpoint();
			if (err)
			{
				f2fs_err(sbi, L"Failed to get valid F2FS checkpoint");
				//goto free_meta_inode;
				THROW_ERROR(ERR_APP, L"Failed to get valid F2FS checkpoint");
			}

			if (__is_set_ckpt_flags(sbi->F2FS_CKPT(), CP_QUOTA_NEED_FSCK_FLAG))		
				sbi->set_sbi_flag(SBI_QUOTA_NEED_REPAIR);
			if (__is_set_ckpt_flags(sbi->F2FS_CKPT(), CP_DISABLED_QUICK_FLAG))
			{
				sbi->set_sbi_flag(SBI_CP_DISABLED_QUICK);
				sbi->interval_time[DISABLE_TIME] = DEF_DISABLE_QUICK_INTERVAL;
			}

			if (__is_set_ckpt_flags(sbi->F2FS_CKPT(), CP_FSCK_FLAG))		sbi->set_sbi_flag(SBI_NEED_FSCK);

			/* Initialize device list */
			err = f2fs_scan_devices();
			if (err)	{	THROW_ERROR(ERR_APP, L"Failed to find devices");	}		//goto free_devices;

			err = f2fs_init_post_read_wq(sbi);
			if (err)
			{
				f2fs_err(sbi, L"Failed to initialize post read workqueue");
				THROW_ERROR(ERR_APP, L"Failed to initialize post read workqueue");
			}	//goto free_devices;

			sbi->total_valid_node_count = le32_to_cpu(sbi->ckpt->valid_node_count);
#if 0 //<TODO>
			percpu_counter_set(&sbi->total_valid_inode_count, le32_to_cpu(sbi->ckpt->valid_inode_count));
#endif
			sbi->user_block_count = boost::numeric_cast<block_t>(le64_to_cpu(sbi->ckpt->user_block_count));
			sbi->total_valid_block_count = boost::numeric_cast<block_t>(le64_to_cpu(sbi->ckpt->valid_block_count));
			sbi->last_valid_block_count = sbi->total_valid_block_count;
			sbi->reserved_blocks = 0;
			sbi->current_reserved_blocks = 0;
			limit_reserve_root(sbi);
			adjust_unusable_cap_perc(sbi);

			for (i = 0; i < NR_INODE_TYPE; i++)
			{
//				INIT_LIST_HEAD(&sbi->inode_list[i]);
				spin_lock_init(&sbi->inode_lock[i]);
			}
			mutex_init(&sbi->flush_lock);

			f2fs_init_extent_cache_info(sbi);
			f2fs_init_ino_entry_info(sbi);
			f2fs_init_fsync_node_info(sbi);
			/* setup checkpoint request control and start checkpoint issue thread */
			//f2fs_init_ckpt_req_control(sbi);
			new(&sbi->cprc_info) ckpt_req_control(sbi);
//			sbi->cprc_info.f2fs_init_ckpt_req_control(sbi);
			if (!sbi->f2fs_readonly() && !test_opt(sbi, DISABLE_CHECKPOINT) && test_opt(sbi, MERGE_CHECKPOINT))
			{
				//err = sbi->f2fs_start_ckpt_thread();
#ifndef CONFIG_SYNC_CHECKPT
				bool br = sbi->cprc_info.Start(THREAD_PRIORITY_BELOW_NORMAL);
				if (!br)	{THROW_ERROR(ERR_APP, L"Failed to start F2FS issue_checkpoint_thread (%d)", err);	}
					//goto stop_ckpt_thread;
#endif // !CONFIG_SYNC_CHECKPT

			}
			/* setup f2fs internal modules */
			err = sbi->f2fs_build_segment_manager();
			if (err)
			{
				f2fs_err(sbi, L"Failed to initialize F2FS segment manager (%d)", err);
//				goto free_sm;
				THROW_ERROR(ERR_APP, L"Failed to initialize F2FS segment manager (%d)", err);

			}
			err = sbi->f2fs_build_node_manager();
			if (err)
			{
				f2fs_err(sbi, L"Failed to initialize F2FS node manager (%d)", err);
				THROW_ERROR(ERR_APP, L"Failed to initialize F2FS node manager (%d)", err);
//				goto free_nm;
			}

			/* For write statistics */
			sbi->sectors_written_start = f2fs_get_sectors_written(sbi);

			/* Read accumulated write IO statistics if exists */
			seg_i = sbi->CURSEG_I(CURSEG_HOT_NODE);
			if (sbi->__exist_node_summaries())
				sbi->kbytes_written = le64_to_cpu(seg_i->journal.info.kbytes_written);

			f2fs_build_gc_manager(sbi);

			err = f2fs_build_stats(sbi);
			if (err)				THROW_ERROR(ERR_APP, L"failed on build stats");
			//goto free_nm;

			/* get an inode for node space */
			sbi->node_inode = f2fs_iget(F2FS_NODE_INO(sbi));
			if (IS_ERR(sbi->node_inode))
			{
//				err = PTR_ERR(sbi->node_inode);
				THROW_ERROR(ERR_APP, L"Failed to read node inode");
				//goto free_stats;
			}

			f2fs_inode_info* root;
			/* read root inode and dentry */
			root = f2fs_iget(F2FS_ROOT_INO(sbi));
			if (IS_ERR(root))
			{
				//err = PTR_ERR(root);
				THROW_ERROR(ERR_APP, L"Failed to read root inode");
				//goto free_node_inode;
			}
#ifdef _DEBUG
			root->m_description = L"inode for root.";
#endif
			if (!S_ISDIR(root->i_mode) || !root->i_blocks || !root->i_size || !root->i_nlink)
			{
				iput(root);
				err = -EINVAL;
				THROW_ERROR(ERR_APP, L"root is not a directory");
				//goto free_node_inode;
			}

			sb->s_root = d_make_root(root); /* allocate root dentry */
			if (!sb->s_root)
			{
				err = -ENOMEM;
				THROW_ERROR(ERR_APP, L"failed on getting root");
				//goto free_node_inode;
			}

			//<YUAN>向Linux注册fs，省略
			err = f2fs_register_sysfs(sbi);
			if (err)				THROW_ERROR(ERR_APP, L"failed on register sysfs");
				//goto free_root_inode;

#ifdef CONFIG_QUOTA
			/* Enable quota usage during mount */
			if (f2fs_sb_has_quota_ino(sbi) && !f2fs_readonly(sb))
			{
				err = f2fs_enable_quotas(sb);
				if (err)
					f2fs_err(sbi, "Cannot turn on quotas: error %d", err);
			}
#endif
			/* if there are any orphan inodes, free them */
			err = f2fs_recover_orphan_inodes(sbi);
			if (err)				THROW_ERROR(ERR_APP, L"failed on recover orphan inodes");
						//goto free_meta;

			if (unlikely(sbi->is_set_ckpt_flags(CP_DISABLED_FLAG)))
				goto reset_checkpoint;

			/* recover fsynced data */
			if (!test_opt(sbi, DISABLE_ROLL_FORWARD) &&	!test_opt(sbi, NORECOVERY))
			{	/* mount should be failed, when device has readonly mode, and previous checkpoint was not done by 
				clean system shutdown. */
				if (f2fs_hw_is_readonly(sbi))
				{
					if (!sbi->is_set_ckpt_flags(CP_UMOUNT_FLAG))
					{
						err = f2fs_recover_fsync_data(sbi, true);
						if (err > 0)
						{
							err = -EROFS;
							f2fs_err(sbi, L"Need to recover fsync data, but write access unavailable, please try "
								"mount w/ disable_roll_forward or norecovery");
						}
						if (err < 0)					THROW_ERROR(ERR_APP, L"failed on recovery fsync data");
						//goto free_meta;
					}
					f2fs_info(sbi, L"write access unavailable, skipping recovery");
					goto reset_checkpoint;
				}

				if (need_fsck)
					sbi->set_sbi_flag(SBI_NEED_FSCK);

				if (skip_recovery)
					goto reset_checkpoint;

				err = f2fs_recover_fsync_data(sbi, false);
				if (err < 0)
				{
					if (err != -ENOMEM)
						skip_recovery = true;
					need_fsck = true;
					f2fs_err(sbi, L"Cannot recover all fsync data errno=%d", err);
					THROW_ERROR(ERR_APP, L"Cannot recover all fsync data errno=%d", err);

					//goto free_meta;
				}
			}
			else
			{
				err = f2fs_recover_fsync_data(sbi, true);

				if (!sbi->f2fs_readonly() && err > 0)
				{
					err = -EINVAL;
					f2fs_err(sbi, L"Need to recover fsync data");	
					THROW_ERROR(ERR_APP, L"Need to recover fsync data");

					//goto free_meta;
				}
			}

			/* If the f2fs is not readonly and fsync data recovery succeeds, check zoned block devices' write pointer consistency. */
			if (!err && !sbi->f2fs_readonly() && f2fs_sb_has_blkzoned(sbi))
			{
				err = f2fs_check_write_pointer(sbi);
				if (err)					THROW_ERROR(ERR_APP, L"failed on check write pointer");
				//goto free_meta;
			}

		reset_checkpoint:
			f2fs_init_inmem_curseg(sbi);

			/* f2fs_recover_fsync_data() cleared this already */
			clear_sbi_flag(sbi, SBI_POR_DOING);

			if (test_opt(sbi, DISABLE_CHECKPOINT))
			{
				err = f2fs_disable_checkpoint();
				//if (err)	goto sync_free_meta;
				if (err) THROW_ERROR(ERR_APP, L"failed on disable checkpoint");
			}
			else if (sbi->is_set_ckpt_flags(CP_DISABLED_FLAG)) { f2fs_enable_checkpoint(sbi); }

			/* If filesystem is not mounted as read-only then do start the gc_thread. */
			if ((F2FS_OPTION(sbi).bggc_mode != BGGC_MODE_OFF || test_opt(sbi, GC_MERGE)) && !sbi->f2fs_readonly())
			{
				/* After POR, we can run background GC thread.*/
				err = f2fs_start_gc_thread(sbi);
				//if (err)	goto sync_free_meta;
				if (err)	THROW_ERROR(ERR_APP, L"failed on starting gc");
			}
			//kvfree(options);

					/* recover broken superblock */
			if (recovery)
			{
				err = f2fs_commit_super(sbi, true);
				f2fs_info(sbi, L"Try to recover %dth superblock, ret: %d", sbi->valid_super_block ? 1 : 2, err);
			}

			f2fs_join_shrinker();
			f2fs_tuning_parameters(sbi);

			f2fs_notice(sbi, L"Mounted with checkpoint version = %llx", cur_cp_version(sbi->F2FS_CKPT()));
			sbi->f2fs_update_time( CP_TIME);
			sbi->f2fs_update_time( REQ_TIME);
			clear_sbi_flag(sbi, SBI_CP_DISABLED_QUICK);
			return 0;
		}
		catch (...)
		{
			if (state >= STATE_SYNC_FREE_META)
			{
				sync_fs(true);
				retry_cnt = 0;
			}
			destory_super();
		}

		if (retry_cnt <= 0 || skip_recovery)	break;
		retry_cnt--;
		shrink_dcache_sb(sb);
	}
	//<YUAN>附加初始化：
	init_dir_entry_data();

//	}
	//catch (...)
	//{

//sync_free_meta:
	/* safe to flush all the data */
	return err;
	//	throw;
	//}

#if 0//TODO

free_meta:
#ifdef CONFIG_QUOTA
	f2fs_truncate_quota_inode_pages(sb);
	if (f2fs_sb_has_quota_ino(sbi) && !f2fs_readonly(sb))
		f2fs_quota_off_umount(sbi->sb);
#endif
	/* Some dirty meta pages can be produced by f2fs_recover_orphan_inodes() failed by EIO. Then, iput(node_inode) 
	can trigger balance_fs_bg() followed by f2fs_write_checkpoint() through f2fs_write_node_pages(), which falls into 
	an infinite loop in f2fs_sync_meta_pages(). */
	truncate_inode_pages_final(META_MAPPING(sbi));
	/* evict some inodes being cached by GC */
	evict_inodes(sb);
	f2fs_unregister_sysfs(sbi);
free_root_inode:
	dput(sb->s_root);
	sb->s_root = NULL;
free_node_inode:
	f2fs_release_ino_entry(sbi, true);
	truncate_inode_pages_final(NODE_MAPPING(sbi));
	iput(sbi->node_inode);
	sbi->node_inode = NULL;
free_stats:
	f2fs_destroy_stats(sbi);
free_nm:
	f2fs_destroy_node_manager(sbi);
#endif //<TODO>
free_sm:
	f2fs_destroy_segment_manager();
	f2fs_destroy_post_read_wq(sbi);
#if 0 //<TODO>
stop_ckpt_thread:
	f2fs_stop_ckpt_thread(sbi);
//free_devices:
//	destroy_device_list(sbi);
	//kvfree(sbi->ckpt);
free_meta_inode:
	make_bad_inode(sbi->meta_inode);
	iput(sbi->meta_inode);
	sbi->meta_inode = NULL;
//free_page_array_cache:
//	f2fs_destroy_page_array_cache(sbi);
//free_xattr_cache:
//	f2fs_destroy_xattr_caches(sbi);
free_io_dummy:
#if 0 //<TODO>
	mempool_destroy(sbi->write_io_dummy);
#endif
free_percpu:
	destroy_percpu_info(sbi);
//free_bio_info:
//	for (i = 0; i < NR_PAGE_TYPE; i++)	kvfree(sbi->write_io[i]);

//#ifdef CONFIG_UNICODE
//#if 0 //<TODO>
//	utf8_unload(sb->s_encoding);
//#endif
//	sb->s_encoding = NULL;
//#endif

free_options:
#ifdef CONFIG_QUOTA
	for (i = 0; i < MAXQUOTAS; i++)
		kfree(F2FS_OPTION(sbi).s_qf_names[i]);
#endif
	//fscrypt_free_dummy_policy(&F2FS_OPTION(sbi).dummy_enc_policy);
	//kvfree(options);

//free_sb_buf:
//	kfree(raw_super);

//free_sbi:
//	if (sbi->s_chksum_driver)	crypto_free_shash(sbi->s_chksum_driver);
//	kfree(sbi);
#endif //TODO
	/* give only one another chance */
	//if (retry_cnt > 0 && skip_recovery) 
	//{
	//	retry_cnt--;
	//	shrink_dcache_sb(sb);
	//	goto try_onemore;
	//}
	//return err;
}


//static struct dentry *f2fs_mount(struct file_system_type *fs_type, int flags, const char *dev_name, void *data)
//{
//	return mount_bdev(fs_type, flags, dev_name, data, f2fs_fill_super);
//}

//static void kill_f2fs_super(struct super_block *sb)
void f2fs_sb_info::kill_f2fs_super(void)
{
	if (s_root) 
	{
//		struct f2fs_sb_info *sbi = F2FS_SB(sb);
		set_sbi_flag(SBI_IS_CLOSE);
		f2fs_stop_gc_thread();
//		f2fs_stop_discard_thread();
		sm_info->dcc_info->Stop();

		if (is_sbi_flag_set( SBI_IS_DIRTY) ||  !is_set_ckpt_flags( CP_UMOUNT_FLAG)) 
		{
			cp_control cpc;
			cpc.reason = CP_UMOUNT,
			f2fs_write_checkpoint( &cpc);
		}

		if (is_sbi_flag_set( SBI_IS_RECOVERED) && f2fs_readonly())
			s_flags &= ~SB_RDONLY;
	}
	kill_block_super(this);
}

#if 0 //<TODO>

static struct file_system_type f2fs_fs_type = {
	.owner		= THIS_MODULE,
	.name		= "f2fs",
	.mount		= f2fs_mount,
	.kill_sb	= kill_f2fs_super,
	.fs_flags	= FS_REQUIRES_DEV,
};
MODULE_ALIAS_FS("f2fs");

int init_inodecache(void)
{
	f2fs_inode_cachep = kmem_cache_create("f2fs_inode_cache", sizeof(struct f2fs_inode_info), 0, SLAB_RECLAIM_ACCOUNT|SLAB_ACCOUNT, NULL);
	if (!f2fs_inode_cachep)		return -ENOMEM;
	return 0;
}

static void destroy_inodecache(void)
{
	/*
	 * Make sure all delayed rcu free inodes are flushed before we
	 * destroy cache.
	 */
	rcu_barrier();
	kmem_cache_destroy(f2fs_inode_cachep);
}

static int __init init_f2fs_fs(void)
{
	int err;

	if (PAGE_SIZE != F2FS_BLKSIZE) 
	{
		printk("F2FS not supported on PAGE_SIZE(%lu) != %d\n", PAGE_SIZE, F2FS_BLKSIZE);
		return -EINVAL;
	}

	err = init_inodecache();
	if (err) goto fail;
	err = f2fs_create_node_manager_caches();
	if (err) goto free_inodecache;
	err = f2fs_create_segment_manager_caches();
	if (err) goto free_node_manager_caches;
	err = f2fs_create_checkpoint_caches();
	if (err) goto free_segment_manager_caches;
	err = f2fs_create_recovery_cache();
	if (err) goto free_checkpoint_caches;
	err = f2fs_create_extent_cache();
	if (err) goto free_recovery_cache;
	err = f2fs_create_garbage_collection_cache();
	if (err) goto free_extent_cache;
	err = f2fs_init_sysfs();
	if (err) goto free_garbage_collection_cache;
	err = register_shrinker(&f2fs_shrinker_info);
	if (err) goto free_sysfs;
	err = register_filesystem(&f2fs_fs_type);
	if (err) goto free_shrinker;
	f2fs_create_root_stats();
	err = f2fs_init_post_read_processing();
	if (err) goto free_root_stats;
	err = f2fs_init_bio_entry_cache();
	if (err) goto free_post_read;
	err = f2fs_init_bioset();
	if (err) goto free_bio_enrty_cache;
	err = f2fs_init_compress_mempool();
	if (err) goto free_bioset;
	err = f2fs_init_compress_cache();
	if (err) goto free_compress_mempool;
	return 0;

free_compress_mempool:
	f2fs_destroy_compress_mempool();
free_bioset:
	f2fs_destroy_bioset();
free_bio_enrty_cache:
	f2fs_destroy_bio_entry_cache();
free_post_read:
	f2fs_destroy_post_read_processing();
free_root_stats:
	f2fs_destroy_root_stats();
	unregister_filesystem(&f2fs_fs_type);
free_shrinker:
	unregister_shrinker(&f2fs_shrinker_info);
free_sysfs:
	f2fs_exit_sysfs();
free_garbage_collection_cache:
	f2fs_destroy_garbage_collection_cache();
free_extent_cache:
	f2fs_destroy_extent_cache();
free_recovery_cache:
	f2fs_destroy_recovery_cache();
free_checkpoint_caches:
	f2fs_destroy_checkpoint_caches();
free_segment_manager_caches:
	f2fs_destroy_segment_manager_caches();
free_node_manager_caches:
	f2fs_destroy_node_manager_caches();
free_inodecache:
	destroy_inodecache();
fail:
	return err;
}

static void __exit exit_f2fs_fs(void)
{
	f2fs_destroy_compress_cache();
	f2fs_destroy_compress_mempool();
	f2fs_destroy_bioset();
	f2fs_destroy_bio_entry_cache();
	f2fs_destroy_post_read_processing();
	f2fs_destroy_root_stats();
	unregister_filesystem(&f2fs_fs_type);
	unregister_shrinker(&f2fs_shrinker_info);
	f2fs_exit_sysfs();
	f2fs_destroy_garbage_collection_cache();
	f2fs_destroy_extent_cache();
	f2fs_destroy_recovery_cache();
	f2fs_destroy_checkpoint_caches();
	f2fs_destroy_segment_manager_caches();
	f2fs_destroy_node_manager_caches();
	destroy_inodecache();
}

//module_init(init_f2fs_fs)
//module_exit(exit_f2fs_fs)
//
//MODULE_AUTHOR("Samsung Electronics's Praesto Team");
//MODULE_DESCRIPTION("Flash Friendly File System");
//MODULE_LICENSE("GPL");
//MODULE_SOFTDEP("pre: crc32");

#endif

f2fs_sb_info::f2fs_sb_info(CF2fsFileSystem* fs, file_system_type *fs_type) 
	: nm_info (NULL), m_fs(fs)
	, cprc_info(this)
{
	s_ndevs = 0;
	s_bdev = nullptr;
	

	s_type = fs_type;
	InitializeCriticalSection(&s_inode_list_lock);
	INIT_LIST_HEAD(&s_inodes);

	//rw_semaphore  sb_lock;		/* lock for raw super block */
	//mutex writepages;			/* mutex for writepages() */
	ZeroInit(write_io);			/* for write bios */
	/* keep migration IO order for LFS mode */
	//rw_semaphore io_order_lock;

	//CRITICAL_SECTION cp_lock;			/* for flag in ckpt */
	//rw_semaphore  cp_global_sem;	/* checkpoint procedure lock */
	//rw_semaphore  cp_rwsem;		/* blocking FS operations */
	//rw_semaphore  node_write;		/* locking node writes */
	//rw_semaphore  node_change;	/* locking node change */
//	ZeroInit(cp_wait);

	//<YUAN> 用CPU时钟替换
	ZeroInit(last_time);
	ZeroInit(interval_time);


	//CRITICAL_SECTION fsync_node_lock;		/* for node entry lock */
	//struct list_head fsync_node_list;	/* node list head */

	///* for inode management */
	//struct list_head inode_list[NR_INODE_TYPE];	/* dirty inode list */
	//CRITICAL_SECTION inode_lock[NR_INODE_TYPE];	/* for dirty inode list lock */
	//HANDLE /*mutex*/ flush_lock;		/* for flush exclusion */

	///* for extent tree cache */
	//radix_tree_root extent_tree_root;/* cache extent cache entries */
	//HANDLE /*mutex*/ extent_tree_lock;	/* locking extent radix tree */
	//struct list_head extent_list;		/* lru list for shrinker */
	//CRITICAL_SECTION extent_lock;			/* locking extent lru list */
	//atomic_t total_ext_tree;		/* extent tree count */
	//struct list_head zombie_list;		/* extent zombie tree list */

	//SRWLOCK /*rw_semaphore*/  quota_sem;		/* blocking cp for flags */

	///* # of pages, see count_type */
	ZeroInit(nr_pages);
	///* # of allocated blocks */
	//percpu_counter alloc_valid_block_count;
	///* writeback control */
	//atomic_t wb_sync_req[META];	/* count # of WB_SYNC threads */

	///* valid inode count */
	total_valid_inode_count.count = 0;
	ZeroInit(mount_opt);
	///* for cleaning operations */
	//rw_semaphore gc_lock;			/* semaphore for GC, avoid race between GC and GC or CP */
	//struct atgc_management am;		/* atgc management */
	//rw_semaphore  pin_sem;


	////CRITICAL_SECTION stat_lock;			/* lock for stat operations */
	///* For app/fs IO statistics */
	//CRITICAL_SECTION iostat_lock;
	ZeroInit(rw_iostat);
	ZeroInit(prev_rw_iostat);
	/* For shrinker support */
	ZeroInit(s_list);
	//CRITICAL_SECTION dev_lock;			/* protect dirty_device */

}

f2fs_sb_info::~f2fs_sb_info(void)
{
	RELEASE(s_bdev);

	DeleteCriticalSection(&s_inode_list_lock);
	//INIT_LIST_HEAD(&s_inodes);

	delete s_type;
	delete nm_info;		nm_info = NULL;

}


////===============================================================================================================////
// 为调试输出，将函数放在cpp中，完成调试可移动到.h
inline void f2fs_sb_info::inc_page_count(int count_type)
{
	LOG_DEBUG(L"increment page count, type=%d, count=%d", count_type, nr_pages[count_type]);
	atomic_inc(&nr_pages[count_type]);
	if (count_type == F2FS_DIRTY_DENTS || count_type == F2FS_DIRTY_NODES ||
			count_type == F2FS_DIRTY_META || count_type == F2FS_DIRTY_QDATA ||	count_type == F2FS_DIRTY_IMETA)
		set_sbi_flag(SBI_IS_DIRTY);
}

inline void f2fs_sb_info::dec_page_count(int count_type)
{
	LOG_DEBUG(L"decrease page count, type=%d, count=%d", count_type, nr_pages[count_type]);
	atomic_dec(&nr_pages[count_type]);
}
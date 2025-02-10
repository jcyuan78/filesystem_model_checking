///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// libf2fs_fsck.cpp : 定义静态库的函数。
//

#include "pch.h"
#include "framework.h"

#include <include/f2fs.h>

#include "fsck.h"
//#include <libgen.h>
#include <ctype.h>
#include <time.h>
//#include <getopt.h>
//#include <stdbool.h>
//#include "quotaio.h"
//#include "compress.h"

LOCAL_LOGGER_ENABLE(L"f2fs.fsck", LOGGER_LEVEL_DEBUGINFO);

IFileSystem::FSCK_RESULT do_fsck(fsck_f2fs_sb_info * sbi)
{
	f2fs_checkpoint* ckpt = sbi->F2FS_CKPT();

	u32 flag = le32_to_cpu(ckpt->ckpt_flags);
	u32 blk_cnt;
//	errcode_t ret;
	fsck_init(sbi);

	print_cp_state(flag);

	fsck_chk_and_fix_write_pointers(sbi);

	fsck_chk_curseg_info(sbi);

	if (!c.fix_on && !c.bug_on) {
		switch (c.preen_mode) {
		case PREEN_MODE_1:
			if (fsck_chk_meta(sbi)) {
				MSG(0, "[FSCK] F2FS metadata   [Fail]");
				MSG(0, "\tError: meta does not match, force check all\n");
			}
			else {
				MSG(0, "[FSCK] F2FS metadata   [Ok..]");
				fsck_free(sbi);
				return IFileSystem::FSCK_SUCCESS;
			}
			if (!c.ro)			c.fix_on = 1;
			break;
		}
	}
	else if (c.preen_mode) {
		/* we can hit this in 3 situations:
		 *  1. fsck -f, fix_on has already been set to 1 when parsing options;
		 *  2. fsck -a && CP_FSCK_FLAG is set, fix_on has already been set to 1 when checking CP_FSCK_FLAG;
		 *  3. fsck -p 1 && error is detected, then bug_on is set, we set fix_on = 1 here, so that fsck can fix errors automatically */
		c.fix_on = 1;
	}

	fsck_chk_checkpoint(sbi);

	//fsck_chk_quota_node(sbi);

	/* Traverse all block recursively from root inode */
	blk_cnt = 1;

	//if (c.feature & cpu_to_le32(F2FS_FEATURE_QUOTA_INO)) {
	//	ret = quota_init_context(sbi);
	//	if (ret) {
	//		ASSERT_MSG("quota_init_context failure: %d", ret);
	//		return IFileSystem::FSCK_OPERATIONAL_ERROR;
	//	}
	//}
	fsck_chk_orphan_node(sbi);
	sbi->m_call_depth = 0;
	fsck_chk_node_blk(sbi, NULL, sbi->root_ino_num, F2FS_FT_DIR, TYPE_INODE, &blk_cnt, NULL);
	//fsck_chk_quota_files(sbi);

	int ret = fsck_verify(sbi);
	fsck_free(sbi);

	if (!c.bug_on)		return IFileSystem::FSCK_SUCCESS;
	if (!ret)			return IFileSystem::FSCK_ERROR_CORRECTED;
	return IFileSystem::FSCK_ERRORS_LEFT_UNCORRECTED;
}

f2fs_configuration c;

IFileSystem::FSCK_RESULT run_fsck(CF2fsFileSystem* fs)
{
	f2fs_fsck fsck(fs);
	fsck.sbi.fsck = &fsck;
	//fsck_f2fs_sb_info _sbi(fs);
	fsck_f2fs_sb_info* sbi = &fsck.sbi;
	IFileSystem::FSCK_RESULT ir = IFileSystem::FSCK_SUCCESS;

	memcpy_s(&c, sizeof(c), fs->GetConfiguration(), sizeof(f2fs_configuration));

	int ii = f2fs_do_mount(sbi);
	if (ii < 0)
	{
		LOG_ERROR(L"failed on mounting fs, res=%d", ii);
		wprintf_s(L"failed on mounting fs, res=%d", ii);
		return IFileSystem::FSCK_OPERATIONAL_ERROR;
	}
	ir = do_fsck(sbi);

	f2fs_do_umount(sbi);
	return ir;
}

unsigned int addrs_per_inode(f2fs_inode* inode)
{
	int default_addr_per_inode = DEF_ADDRS_PER_INODE;
	int cur_addr = CUR_ADDRS_PER_INODE(inode);
	int xattr_addr = get_inline_xattr_addrs(inode);
//	LOG_DEBUG(L"default addr=%d, extra size=%d, cur addr=%d, xattr_addr=%d",
//		default_addr_per_inode, inode->_u._s.i_extra_isize, cur_addr, xattr_addr);

	unsigned int addrs = CUR_ADDRS_PER_INODE(inode) - get_inline_xattr_addrs(inode);
	return addrs;
}


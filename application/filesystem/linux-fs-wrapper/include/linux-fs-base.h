#pragma once

#include "linux_comm.h"
#include "fs.h"
#include <dokanfs-lib.h>

class CLinuxFsBase
{
public:
	int sync_filesystem(void) { JCASSERT(0); return 0; }
	unsigned int sb_set_blocksize(unsigned int size)
	{
		JCASSERT(0);
		// Linuxԭ�����е���set_blocksize(sb->bdev, size), ֻ����sanity check��
		//UINT bits = blksize_bits(size);
		m_sb->s_blocksize = size;
		m_sb->s_blocksize_bits = blksize_bits(size);
		return m_sb->s_blocksize;
	}

protected:
	super_block* m_sb = nullptr;
};


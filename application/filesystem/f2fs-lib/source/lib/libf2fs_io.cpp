#include "../pch.h"
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/**
 * libf2fs.c
 *
 * Copyright (c) 2013 Samsung Electronics Co., Ltd.
 *             http://www.samsung.com/
 * Copyright (c) 2019 Google Inc.
 *             http://www.google.com/
 * Copyright (c) 2020 Google Inc.
 *   Robin Hsu <robinhsu@google.com>
 *  : add quick-buffer for sload compression support
 *
 * Dual licensed under the GPL or LGPL version 2 licenses.
 */
#define _LARGEFILE64_SOURCE

#include "../../include/config.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
//#include <unistd.h>
#include <fcntl.h>
#ifdef HAVE_MNTENT_H
#include <mntent.h>
#endif
#include <time.h>
#ifndef ANDROID_WINDOWS_HOST
#include <sys/stat.h>
//#include <sys/mount.h>
//#include <sys/ioctl.h>
#endif
#ifdef HAVE_LINUX_HDREG_H
#include <linux/hdreg.h>
#endif

#include <boost/cast.hpp>

#include <stdbool.h>
#include <assert.h>
#include <inttypes.h>
#include "../../include/f2fs_fs.h"
#include "../../include/f2fs-filesystem.h"

//struct f2fs_configuration c;

#ifdef WITH_ANDROID
#include <sparse/sparse.h>
struct sparse_file *f2fs_sparse_file;
static char **blocks;
u_int64_t blocks_count;
static char *zeroed_block;
#endif

LOCAL_LOGGER_ENABLE(L"f2fs.io", LOGGER_LEVEL_DEBUGINFO);


//static int __get_device_fd(__u64 *offset)
//{
//	__u64 blk_addr = *offset >> F2FS_BLKSIZE_BITS;
//	int i;
//
//	for (i = 0; i < m_config.ndevs; i++) 
//	{
//		if (m_config.devices[i].start_blkaddr <= blk_addr && c.devices[i].end_blkaddr >= blk_addr) 
//		{
//			*offset -= c.devices[i].start_blkaddr << F2FS_BLKSIZE_BITS;
//			return c.devices[i].fd;
//		}
//	}
//	return -1;
//}

IVirtualDisk* CF2fsFileSystem::__get_device(__u64* offset)
{
	__u64 blk_addr = *offset >> F2FS_BLKSIZE_BITS;
	for (int i = 0; i < m_config.ndevs; i++) 
	{
		if (m_config.devices[i].start_blkaddr <= blk_addr && m_config.devices[i].end_blkaddr >= blk_addr) 
		{
			*offset -= m_config.devices[i].start_blkaddr << F2FS_BLKSIZE_BITS;
			return m_config.devices[i].m_fd;
		}
	}
	return NULL;
}


//#ifndef HAVE_LSEEK64
#if 0
typedef off_t	off64_t;

static inline off64_t lseek64(int fd, __u64 offset, int set)
{
	return lseek(fd, offset, set);
}
#endif

//<YUAN>变量局部化，全局变量变为fs的成员变量
/* ---------- dev_cache, Least Used First (LUF) policy  ------------------- */
/*
 * Least used block will be the first victim to be replaced when max hash
 * collision exceeds
 */
//static bool *dcache_valid; /* is the cached block valid? */
//static off64_t  *dcache_blk; /* which block it cached */
//static uint64_t *dcache_lastused; /* last used ticks for cache entries */
//static char *dcache_buf; /* cached block data */
//static uint64_t dcache_usetick; /* current use tick */
//
//static uint64_t dcache_raccess;
//static uint64_t dcache_rhit;
//static uint64_t dcache_rmiss;
//static uint64_t dcache_rreplace;
//
//static bool dcache_exit_registered = false;
//
/*
 *  Shadow config:
 *
 *  Active set of the configurations.
 *  Global configuration 'm_dcache_config' will be transferred here when
 *  when dcache_init() is called
 */
//static dev_cache_config_t dcache_config = {0, 16, 1};
//static bool m_dcache_initialized = false;

#define MIN_NUM_CACHE_ENTRY  1024L
#define MAX_MAX_HASH_COLLISION  16

//static long dcache_relocate_offset0[] = {
//	20, -20, 40, -40, 80, -80, 160, -160,
//	320, -320, 640, -640, 1280, -1280, 2560, -2560,
//};
//static int dcache_relocate_offset[16];

void CF2fsFileSystem::dcache_print_statistics(void)
{
	/* Number of used cache entries */
	long useCnt = 0;
	for (long i = 0; i < m_dcache_config.num_cache_entry; i++)
	{
		if (m_dcache_valid[i])		++useCnt;
	}

	/*
	 *  c: number of cache entries
	 *  u: used entries
	 *  RA: number of read access blocks
	 *  CH: cache hit
	 *  CM: cache miss
	 *  Repl: read cache replaced
	 */
	printf ("\nc, u, RA, CH, CM, Repl=\n");
	printf ("%ld %ld %" PRIu64 " %" PRIu64 " %" PRIu64 " %" PRIu64 "\n",
			m_dcache_config.num_cache_entry, useCnt, m_dcache_raccess,
			m_dcache_rhit,	m_dcache_rmiss, m_dcache_rreplace);
}

void CF2fsFileSystem::dcache_release(void)
{
	if (!m_dcache_initialized)		return;

	m_dcache_initialized = false;

	if (m_config.cache_config.dbg_en)	dcache_print_statistics();

	if (m_dcache_blk != NULL)			free(m_dcache_blk);
	if (m_dcache_lastused != NULL)	free(m_dcache_lastused);
	if (m_dcache_buf != NULL)			free(m_dcache_buf);
	if (m_dcache_valid != NULL)		free(m_dcache_valid);
	m_dcache_config.num_cache_entry = 0;
	m_dcache_blk = NULL;
	m_dcache_lastused = NULL;
	m_dcache_buf = NULL;
	m_dcache_valid = NULL;
}

// return 0 for success, error code for failure.
int CF2fsFileSystem::dcache_alloc_all(long n)
{
	if (n <= 0)		return -1;
	if ((m_dcache_blk = (UINT64 *) malloc(sizeof(UINT64) * n)) == NULL
		|| (m_dcache_lastused = (uint64_t *) malloc(sizeof(uint64_t) * n)) == NULL
		|| (m_dcache_buf = (char *) malloc (F2FS_BLKSIZE * n)) == NULL
		|| (m_dcache_valid = (bool *) malloc(sizeof(bool) * n)) == NULL)
	{
		dcache_release();
		return -1;
	}
	m_dcache_config.num_cache_entry = n;
	return 0;
}

void CF2fsFileSystem::dcache_relocate_init(void)
{
//	int i;
	UINT n0 = (sizeof(m_dcache_relocate_offset0) / sizeof(m_dcache_relocate_offset0[0]));
	UINT n = (sizeof(m_dcache_relocate_offset) / sizeof(m_dcache_relocate_offset[0]));

	ASSERT(n == n0);
	for (UINT i = 0; i < n && i < m_dcache_config.max_hash_collision; i++) 
	{
		if (labs(m_dcache_relocate_offset0[i]) > m_dcache_config.num_cache_entry / 2) 
		{
			m_dcache_config.max_hash_collision = i;
			break;
		}
		m_dcache_relocate_offset[i] = m_dcache_config.num_cache_entry	+ m_dcache_relocate_offset0[i];
	}
}

void CF2fsFileSystem::dcache_init(void)
{
	long n;
	if (m_config.cache_config.num_cache_entry <= 0)		return;
	/* release previous cache init, if any */
	dcache_release();

	m_dcache_blk = NULL;
	m_dcache_lastused = NULL;
	m_dcache_buf = NULL;
	m_dcache_valid = NULL;

	m_dcache_config = m_config.cache_config;
	n = max(MIN_NUM_CACHE_ENTRY, m_dcache_config.num_cache_entry);

	/* halve alloc size until alloc succeed, or min cache reached */
	while (dcache_alloc_all(n) != 0 && n !=  MIN_NUM_CACHE_ENTRY)	n = max(MIN_NUM_CACHE_ENTRY, n/2);

	/* must be the last: data dependent on num_cache_entry */
	dcache_relocate_init();
	m_dcache_initialized = true;

	if (!m_dcache_exit_registered) 
	{
		m_dcache_exit_registered = true;
//		atexit(dcache_release); /* auto release */	转为析构函数实现
	}

	m_dcache_raccess = 0;
	m_dcache_rhit = 0;
	m_dcache_rmiss = 0;
	m_dcache_rreplace = 0;
}


#if 0		//<YUAN> 移入头文件中
static inline char *dcache_addr(long entry)
{
	return m_dcache_buf + F2FS_BLKSIZE * entry;
}

/* relocate on (n+1)-th collision */
inline long dcache_relocate(long entry, int n)
{
	assert(m_dcache_config.num_cache_entry != 0);
	return (entry + dcache_relocate_offset[n]) %
			dcache_config.num_cache_entry;
}
#endif


long CF2fsFileSystem::dcache_find(UINT64 blk)
{
	register long n = m_dcache_config.num_cache_entry;
	register unsigned m = m_dcache_config.max_hash_collision;
	long entry, least_used, target;
	unsigned try_count;

	assert(n > 0);
	target = least_used = entry = blk % n; /* simple modulo hash */

	for (try_count = 0; try_count < m; try_count++) 
	{
		/* found target or empty cache slot */
		if (!m_dcache_valid[target] || m_dcache_blk[target] == blk)		return target;  
		if (m_dcache_lastused[target] < m_dcache_lastused[least_used])	least_used = target;
		target = dcache_relocate(entry, try_count); /* next target */
	}
	return least_used;  /* max search reached, return least used slot */
}

/* Physical read into cache */
int CF2fsFileSystem::dcache_io_read(IVirtualDisk * disk, long entry, UINT64 offset, UINT64 blk)
{
//	if (lseek64(fd, offset, SEEK_SET) < 0) 
//	{
//		LOG_ERROR(L"[err] lseek64 fail.");
//	//	MSG(0, "\n lseek64 fail.\n");
//		return -1;
//	}
//	if (read(fd, dcache_buf + entry * F2FS_BLKSIZE, F2FS_BLKSIZE) < 0) 
//	{
////		MSG(0, "\n read() fail.\n");
//		LOG_ERROR(L"[err] read() fail.")
//		return -1;
//	}
	UINT64 lba = BYTE_TO_SECTOR(offset);
	static const UINT64 secs = BYTE_TO_SECTOR(F2FS_BLKSIZE);
	disk->ReadSectors(m_dcache_buf + entry * F2FS_BLKSIZE, lba, secs);
	m_dcache_lastused[entry] = ++m_dcache_usetick;
	m_dcache_valid[entry] = true;
	m_dcache_blk[entry] = blk;
	return 0;
}

/*  - Note: Read/Write are not symmetric:
 *       For read, we need to do it block by block, due to the cache nature: some blocks may be cached, and others don't.
 *       For write, since we always do a write-thru, we can join all writes into one, and write it once at the caller.  This function updates the cache for write, but not the do a physical write.  The caller is responsible for the physical write.
 *  - Note: We concentrate read/write together, due to the fact of similar structure to find the relavant cache entries
 *  - Return values:
 *       0: success
 *       1: cache not available (uninitialized)
 *      -1: error	 */
int CF2fsFileSystem::dcache_update_rw(IVirtualDisk * disk, BYTE *buf, UINT64 offset, size_t byte_count, bool is_write)
{
	off64_t blk;
	int addr_in_blk;
	off64_t start;

	if (!m_dcache_initialized)		dcache_init(); /* auto initialize */
	if (!m_dcache_initialized)		return 1; /* not available */

	blk = offset / F2FS_BLKSIZE;
	addr_in_blk = offset % F2FS_BLKSIZE;
	start = blk * F2FS_BLKSIZE;

	while (byte_count != 0) 
	{
		size_t cur_size = min(byte_count, (size_t)(F2FS_BLKSIZE - addr_in_blk));
		long entry = dcache_find(blk);

		if (!is_write) ++m_dcache_raccess;

		if (m_dcache_valid[entry] && m_dcache_blk[entry] == blk) 
		{	/* cache hit */
			if (is_write)	memcpy(dcache_addr(entry) + addr_in_blk, buf, cur_size);	/* write: update cache */
			else			++m_dcache_rhit;
		} 
		else 
		{	/* cache miss */
			if (!is_write) 
			{
				int err;
				++m_dcache_rmiss;
				if (m_dcache_valid[entry])	++m_dcache_rreplace;
				/* read: physical I/O read into cache */
				err = dcache_io_read(disk, entry, start, blk);
				if (err)	return err;
			}
		}

		/* read: copy data from cache */
		/* write: nothing to do, since we don't do physical write. */
		if (!is_write)	memcpy_s(buf, byte_count, dcache_addr(entry) + addr_in_blk, cur_size);

		/* next block */
		++blk;
		buf += cur_size;
		start += F2FS_BLKSIZE;
		byte_count -= cur_size;
		addr_in_blk = 0;
	}
	return 0;
}

#if 0
/*
 * dcache_update_cache() just update cache, won't do physical I/O.
 * Thus even no error, we need normal non-cache I/O for actual write
 *
 * return value: 1: cache not available
 *               0: success, -1: I/O error
 */
int dcache_update_cache(int fd, void *buf, off64_t offset, size_t count)
{
	return dcache_update_rw(fd, buf, offset, count, true);
}

/* handles read into cache + read into buffer  */
int dcache_read(int fd, void *buf, off64_t offset, size_t count)
{
	return dcache_update_rw(fd, buf, offset, count, false);
}
#endif

/* IO interfaces */
int CF2fsFileSystem::dev_read_version(void *buf, __u64 offset, size_t len)
{
#if 0 //<TO BE IMPLEMENT>
	if (m_config.sparse_mode)		return 0;
	if (lseek64(m_config.kd, (off64_t)offset, SEEK_SET) < 0)		return -1;
	if (read(m_config.kd, buf, len) < 0)		return -1;
#endif
//	JCASSERT(0);
	// dummy version
	strcpy_s((char*)buf, len, "DEBUG_DISK\n");
	return 0;
}

#ifdef WITH_ANDROID
static int sparse_read_blk(__u64 block, int count, void *buf)
{
	int i;
	char *out = buf;
	__u64 cur_block;

	for (i = 0; i < count; ++i) {
		cur_block = block + i;
		if (blocks[cur_block])
			memcpy(out + (i * F2FS_BLKSIZE),
					blocks[cur_block], F2FS_BLKSIZE);
		else if (blocks)
			memset(out + (i * F2FS_BLKSIZE), 0, F2FS_BLKSIZE);
	}
	return 0;
}

static int sparse_write_blk(__u64 block, int count, const void *buf)
{
	int i;
	__u64 cur_block;
	const char *in = buf;

	for (i = 0; i < count; ++i) {
		cur_block = block + i;
		if (blocks[cur_block] == zeroed_block)
			blocks[cur_block] = NULL;
		if (!blocks[cur_block]) {
			blocks[cur_block] = calloc(1, F2FS_BLKSIZE);
			if (!blocks[cur_block])
				return -ENOMEM;
		}
		memcpy(blocks[cur_block], in + (i * F2FS_BLKSIZE),
				F2FS_BLKSIZE);
	}
	return 0;
}

static int sparse_write_zeroed_blk(__u64 block, int count)
{
	int i;
	__u64 cur_block;

	for (i = 0; i < count; ++i) {
		cur_block = block + i;
		if (blocks[cur_block])
			continue;
		blocks[cur_block] = zeroed_block;
	}
	return 0;
}

#ifdef SPARSE_CALLBACK_USES_SIZE_T
static int sparse_import_segment(void *UNUSED(priv), const void *data,
		size_t len, unsigned int block, unsigned int nr_blocks)
#else
static int sparse_import_segment(void *UNUSED(priv), const void *data, int len,
		unsigned int block, unsigned int nr_blocks)
#endif
{
	/* Ignore chunk headers, only write the data */
	if (!nr_blocks || len % F2FS_BLKSIZE)
		return 0;

	return sparse_write_blk(block, nr_blocks, data);
}

static int sparse_merge_blocks(uint64_t start, uint64_t num, int zero)
{
	char *buf;
	uint64_t i;

	if (zero) {
		blocks[start] = NULL;
		return sparse_file_add_fill(f2fs_sparse_file, 0x0,
					F2FS_BLKSIZE * num, start);
	}

	buf = calloc(num, F2FS_BLKSIZE);
	if (!buf) {
		fprintf(stderr, "failed to alloc %llu\n",
			(unsigned long long)num * F2FS_BLKSIZE);
		return -ENOMEM;
	}

	for (i = 0; i < num; i++) {
		memcpy(buf + i * F2FS_BLKSIZE, blocks[start + i], F2FS_BLKSIZE);
		free(blocks[start + i]);
		blocks[start + i] = NULL;
	}

	/* free_sparse_blocks will release this buf. */
	blocks[start] = buf;

	return sparse_file_add_data(f2fs_sparse_file, blocks[start],
					F2FS_BLKSIZE * num, start);
}
#else
static int sparse_read_blk(__u64 block, int count, void *buf) { return 0; }
static int sparse_write_blk(__u64 block, int count, const void *buf) { return 0; }
static int sparse_write_zeroed_blk(__u64 block, int count) { return 0; }
#endif

int CF2fsFileSystem::dev_read(BYTE *buf, __u64 offset, size_t len)
{
	//int fd;
	//int err;

	if (m_config.max_size < (offset + len))		m_config.max_size = offset + len;
	if (m_config.sparse_mode)		
		return sparse_read_blk(offset / F2FS_BLKSIZE, boost::numeric_cast<int>(len / F2FS_BLKSIZE), buf);
	IVirtualDisk * disk = __get_device(&offset);
	if (!disk) THROW_ERROR(ERR_APP, L"failed on getting device");
	//if (fd < 0)		return fd;

	/* err = 1: cache not available, fall back to non-cache R/W */
	/* err = 0: success, err=-1: I/O error */
	//<YUAN> 简化设计，直接展开到dcache_update_rw, 但需要所有展开
	//int err = dcache_read(fd, buf, (off64_t)offset, len);
	int err = dcache_update_rw(disk, buf, offset, len, false);
	if (err <= 0)		return err;
	//if (lseek64(fd, (off64_t)offset, SEEK_SET) < 0)		return -1;
	//if (read(fd, buf, len) < 0)		return -1;
	UINT64 lba = BYTE_TO_SECTOR(offset);
	UINT64 secs = BYTE_TO_SECTOR(len);
	disk->ReadSectors(buf, lba, secs);
	return 0;
}

#ifdef POSIX_FADV_WILLNEED
int dev_readahead(__u64 offset, size_t len)
#else
int CF2fsFileSystem::dev_readahead(__u64 offset, size_t UNUSED(len))
#endif
{
	//int fd = __get_device_fd(&offset);
	//if (fd < 0)		return fd;
	IVirtualDisk* disk = __get_device(&offset);
	if (!disk) THROW_ERROR(ERR_APP, L"failed on getting device");
	return 1;
#ifdef POSIX_FADV_WILLNEED
	return posix_fadvise(fd, offset, len, POSIX_FADV_WILLNEED);
#else
	return 0;
#endif
}

int CF2fsFileSystem::dev_write(BYTE *buf, __u64 offset, size_t len)
{
	//int fd;
	if (m_config.max_size < (offset + len)) m_config.max_size = offset + len;
	if (m_config.dry_run)		return 0;

	if (m_config.sparse_mode)	
		return sparse_write_blk(offset / F2FS_BLKSIZE, boost::numeric_cast<int>(len / F2FS_BLKSIZE), buf);

	//fd = __get_device_fd(&offset);
	//if (fd < 0)		return fd;
	IVirtualDisk * disk = __get_device(&offset);
	if (!disk) THROW_ERROR(ERR_APP, L"failed on getting device");

	/* dcache_update_cache() just update cache, won't do I/O. Thus even no error, we need normal non-cache I/O for actual write */
	//if (dcache_update_cache(fd, buf, (off64_t)offset, len) < 0)	return -1;
	//<YUAN> 简化设计，直接展开到dcache_update_rw, 但需要所有展开
	int err = dcache_update_rw(disk, buf, offset, len, true);
	if (err < 0) return -1;

	//if (lseek64(fd, (off64_t)offset, SEEK_SET) < 0)		return -1;
	//if (write(fd, buf, len) < 0)		return -1;
	UINT64 lba = BYTE_TO_SECTOR(offset);
	UINT64 secs = BYTE_TO_SECTOR(len);
	disk->WriteSectors(buf, lba, secs);
	return 0;
}

int CF2fsFileSystem::dev_write_block(BYTE *buf, __u64 blk_addr)
{
	return dev_write(buf, blk_addr << F2FS_BLKSIZE_BITS, F2FS_BLKSIZE);
}

int CF2fsFileSystem::dev_write_dump(void *buf, __u64 offset, size_t len)
{
	//if (lseek64(m_config.dump_fd, (off64_t)offset, SEEK_SET) < 0)		return -1;
	//if (write(m_config.dump_fd, buf, len) < 0)		return -1;
	UINT64 lba = BYTE_TO_SECTOR(offset);
	UINT64 secs = BYTE_TO_SECTOR(len);
	m_config.m_dump_fd->WriteSectors(buf, lba, secs);
	return 0;
}

int CF2fsFileSystem::dev_fill(void *buf, __u64 offset, size_t len)
{
//	int fd;

	if (m_config.max_size < (offset + len)) m_config.max_size = offset + len;
	if (m_config.sparse_mode)	
		return sparse_write_zeroed_blk(offset / F2FS_BLKSIZE, boost::numeric_cast<int>(len / F2FS_BLKSIZE));
	//fd = __get_device_fd(&offset);
	//if (fd < 0) return fd;
	IVirtualDisk* disk = __get_device(&offset);
	if (!disk) THROW_ERROR(ERR_APP, L"failed on getting device");

	/* Only allow fill to zero */
	if (*((__u8*)buf))	return -1;
	//if (lseek64(fd, (off64_t)offset, SEEK_SET) < 0)	return -1;
	//if (write(fd, buf, len) < 0) return -1;
	UINT64 lba = BYTE_TO_SECTOR(offset);
	UINT64 secs = BYTE_TO_SECTOR(len);
	disk->WriteSectors(buf, lba, secs);
	return 0;
}

int CF2fsFileSystem::dev_fill_block(void *buf, __u64 blk_addr)
{
	return dev_fill(buf, blk_addr << F2FS_BLKSIZE_BITS, F2FS_BLKSIZE);
}

int CF2fsFileSystem::dev_read_block(void *buf, __u64 blk_addr)
{
	return dev_read((BYTE*)buf, blk_addr << F2FS_BLKSIZE_BITS, F2FS_BLKSIZE);
}

int CF2fsFileSystem::dev_reada_block(__u64 blk_addr)
{
	return dev_readahead(blk_addr << F2FS_BLKSIZE_BITS, F2FS_BLKSIZE);
}

int f2fs_fsync_device(void)
{
#ifndef ANDROID_WINDOWS_HOST
	int i;

	for (i = 0; i < m_config.ndevs; i++) {
		if (fsync(m_config.devices[i].fd) < 0) {
			MSG(0, "\tError: Could not conduct fsync!!!\n");
			return -1;
		}
	}
#endif
	return 0;
}

int f2fs_init_sparse_file(void)
{
#ifdef WITH_ANDROID
	if (c.func == MKFS)
	{
		f2fs_sparse_file = sparse_file_new(F2FS_BLKSIZE, c.device_size);
		if (!f2fs_sparse_file)
			return -1;
	}
	else
	{
		f2fs_sparse_file = sparse_file_import(c.devices[0].fd,
			true, false);
		if (!f2fs_sparse_file)
			return -1;

		c.device_size = sparse_file_len(f2fs_sparse_file, 0, 0);
		c.device_size &= (~((u_int64_t)(F2FS_BLKSIZE - 1)));
	}

	if (sparse_file_block_size(f2fs_sparse_file) != F2FS_BLKSIZE)
	{
		MSG(0, "\tError: Corrupted sparse file\n");
		return -1;
	}
	blocks_count = c.device_size / F2FS_BLKSIZE;
	blocks = calloc(blocks_count, sizeof(char*));
	if (!blocks)
	{
		MSG(0, "\tError: Calloc Failed for blocks!!!\n");
		return -1;
	}

	zeroed_block = calloc(1, F2FS_BLKSIZE);
	if (!zeroed_block)
	{
		MSG(0, "\tError: Calloc Failed for zeroed block!!!\n");
		return -1;
	}

	return sparse_file_foreach_chunk(f2fs_sparse_file, true, false,
		sparse_import_segment, NULL);
#else
	JCASSERT(0);
	LOG_ERROR(L"[err]: Sparse mode is only supported for android");
	return -1;
#endif
}


#define MAX_CHUNK_SIZE		(1 * 1024 * 1024 * 1024ULL)
#define MAX_CHUNK_COUNT		(MAX_CHUNK_SIZE / F2FS_BLKSIZE)


#define LOGGER_LEVEL LOGGER_LEVEL_DEBUGINFO
#include <stdext.h>

LOCAL_LOGGER_ENABLE(L"fat.fsck", LOGGER_LEVEL_DEBUGINFO);



#include <stdlib.h>
#include <string.h>

extern "C" {
#include "fat_defs.h"
#include "fat_access.h"
#include "fat_table.h"
#include "fat_write.h"
#include "fat_misc.h"
#include "fat_string.h"
#include "fat_filelib.h"
#include "fat_cache.h"
}

#define CHECKED_MARK	(0xFFF0)

static uint32 cluster_to_fat(struct fatfs * fs, uint32 cluster)
{
	uint32 lba;

	// FAT16 Root directory
	if (fs->fat_type == FAT_TYPE_16 && cluster == 0)
	{
		lba = fs->lba_begin + fs->rootdir_first_sector;
	}
	// FAT16/32 Other
	else
	{	// Set start of cluster chain to initial value
		// If end of cluster chain then return false
		if (cluster == FAT32_LAST_CLUSTER)	return 0;
		// Calculate sector address
		lba = fatfs_lba_of_cluster(fs, cluster);
	}
	return lba;

}

int fsck_check_files(fatfs * fs, BYTE * fat_buf, uint32 start_cluster, bool isdir)
{
	LOG_DEBUG(L"checking item, start=%d", start_cluster);
	JCASSERT(fs && fat_buf)
	bool failed = false;
	size_t cluster_num = fs->fat_sectors * SECTOR_SIZE / 2;		// for FAT16;

	// check sub items
	// read dir content (entries)
	jcvos::auto_array<BYTE> cluster_buf(fs->sectors_per_cluster*SECTOR_SIZE);

	// check this file (for fat 16)
	uint16 * fat = reinterpret_cast<uint16*>(fat_buf);
	uint16 cur_cluster = (uint16)start_cluster;
	size_t index = 0;
	while (1)
	{	// travel in the file
		// check sub-items
		if (isdir)
		{
			uint32 lba = cluster_to_fat(fs, cur_cluster);
			fatfs_sector_read(fs, lba, cluster_buf, fs->sectors_per_cluster);
			for (int item = 0; item < (fs->sectors_per_cluster* FAT_DIR_ENTRIES_PER_SECTOR); item++)
			{
				fat_dir_entry* entry = (fat_dir_entry*)(cluster_buf + item * FAT_DIR_ENTRY_SIZE);
				bool dir = fatfs_entry_is_dir(entry);
				bool file = fatfs_entry_is_file(entry);
				if (dir || file)
				{
					std::string fn((char*)entry->Name, FAT_SFN_SIZE_FULL);
					uint32 len = entry->FileSize;
					uint32 start = ((FAT_HTONS((uint32)entry->FstClusHI)) << 16) + FAT_HTONS(entry->FstClusLO);
					LOG_DEBUG(L"checking sub-item %S, start cluster=%d", fn.c_str(), start);
					int ir = fsck_check_files(fs, fat_buf, start, dir);
					if (!ir)
					{
						LOG_ERROR(L"[fsck err] failed when checking file %S", fn.c_str());
						failed = true;
					}
				}
			}
		}

		uint16 &next = fat[cur_cluster];
		if (next == 0)
		{	// found lost block
			LOG_ERROR(L"[fsck err] lost block: start=%d, index=%d, cluster=%d", start_cluster, index, cur_cluster);
			failed = true;
		}
		else if (next == CHECKED_MARK)
		{	// found doule block
			LOG_ERROR(L"[fsck err] doubled block: start=%d, index=%d, cluster=%d", start_cluster, index, cur_cluster);
			failed = true;
		}
		cur_cluster = next;
		next = CHECKED_MARK;
		if (cur_cluster > cluster_num) break;
		index++;
	}
	return failed ? 0 : 1;
}

int fl_fsck(int repair)
{
	// check lost block
	fatfs * fs = fl_get_fs();
	if (!fs)
	{
		LOG_ERROR(L"[err] failed on getting fs");
		return 0;
	}
	// load fat table into memory
//	uint32 cluster_num = 
	jcvos::auto_array<BYTE> fat_buf(SECTOR_SIZE *fs->fat_sectors);
	fs->disk_io.read_media(fs->fat_begin_lba, fat_buf, fs->fat_sectors);
	// for each file
	int ir = fsck_check_files(fs, fat_buf, fs->rootdir_first_cluster, true);

	// check doubled block
	// check dead block
	size_t cluster_num = fs->fat_sectors * SECTOR_SIZE / 2;		// for FAT16;
	WORD * fat = reinterpret_cast<WORD*>((BYTE*)fat_buf);
	for (size_t ii = 0; ii < cluster_num; ++ii)
	{
		if (fat[ii] != 0 && fat[ii] != CHECKED_MARK)
//		if (fat[ii] != 0 && fat[ii] < cluster_num)
		{
			LOG_ERROR(L"[fsck err] dead block cluster=%d, fat=0x%X", ii, fat[ii]);
			ir = 0;
		}
	}
	return ir;
}
#ifndef __FAT_ACCESS_H__
#define __FAT_ACCESS_H__

#include "fat_defs.h"
#include "fat_opts.h"
#include "fat_list.h"

//-----------------------------------------------------------------------------
// Defines
//-----------------------------------------------------------------------------
#define FAT_INIT_OK                         0
#define FAT_INIT_MEDIA_ACCESS_ERROR         (-1)
#define FAT_INIT_INVALID_SECTOR_SIZE        (-2)
#define FAT_INIT_INVALID_SIGNATURE          (-3)
#define FAT_INIT_ENDIAN_ERROR               (-4)
#define FAT_INIT_WRONG_FILESYS_TYPE         (-5)
#define FAT_INIT_WRONG_PARTITION_TYPE       (-6)
#define FAT_INIT_STRUCT_PACKING             (-7)

#define FAT_DIR_ENTRIES_PER_SECTOR          (FAT_SECTOR_SIZE / FAT_DIR_ENTRY_SIZE)

//-----------------------------------------------------------------------------
// Function Pointers
//-----------------------------------------------------------------------------
typedef int (*fn_diskio_read) (uint32 sector, uint8 *buffer, uint32 sector_count);
typedef int (*fn_diskio_write)(uint32 sector, uint8 *buffer, uint32 sector_count);
//<YUAN>
typedef int (*fn_sync)(void);

struct cluster_lookup
{
    uint32 ClusterIdx;
    uint32 CurrentCluster;
};

typedef struct sFL_FILE
{
    uint32                  parentcluster;
    uint32                  startcluster;
    uint32                  bytenum;
    uint32                  filelength;
    int                     filelength_changed;
    char                    path[FATFS_MAX_LONG_FILENAME];
    char                    filename[FATFS_MAX_LONG_FILENAME];
    uint8                   shortfilename[11];

#ifdef FAT_CLUSTER_CACHE_ENTRIES
    uint32                  cluster_cache_idx[FAT_CLUSTER_CACHE_ENTRIES];
    uint32                  cluster_cache_data[FAT_CLUSTER_CACHE_ENTRIES];
#endif

    // Cluster Lookup
    struct cluster_lookup   last_fat_lookup;

    // Read/Write sector buffer
    uint8                   file_data_sector[FAT_SECTOR_SIZE];
    uint32                  file_data_address;
    int                     file_data_dirty;

    // File fopen flags
    uint8                   flags;
#define FILE_READ           (1 << 0)
#define FILE_WRITE          (1 << 1)
#define FILE_APPEND         (1 << 2)
#define FILE_BINARY         (1 << 3)
#define FILE_ERASE          (1 << 4)
#define FILE_CREATE         (1 << 5)

    struct fat_node         list_node;
} FL_FILE;

//-----------------------------------------------------------------------------
// Structures
//-----------------------------------------------------------------------------
//struct disk_if
//{
//    // User supplied function pointers for disk IO
//    //fn_diskio_read          read_media;
//    //fn_diskio_write         write_media;
//    //fn_sync                 sync_media;
//    int read_media(uint32 sector, uint8* buffer, uint32 sector_count);
//
//};
struct disk_if;

// Forward declaration
struct fat_buffer;

struct fat_buffer
{
    uint8                   sector[FAT_SECTOR_SIZE * FAT_BUFFER_SECTORS];
    uint32                  address;
    int                     dirty;
    uint8 *                 ptr;

    // Next in chain of sector buffers
    struct fat_buffer       *next;
};

typedef enum eFatType
{
    FAT_TYPE_16,
    FAT_TYPE_32
} tFatType;

struct fatfs
{
    // Filesystem globals
    uint8                   sectors_per_cluster;
    uint32                  cluster_begin_lba;
    uint32                  rootdir_first_cluster;
    uint32                  rootdir_first_sector;
    uint32                  rootdir_sectors;
    uint32                  fat_begin_lba;
    uint16                  fs_info_sector;
    uint32                  lba_begin;
    uint32                  fat_sectors;
    uint32                  next_free_cluster;
    uint16                  root_entry_count;
    uint16                  reserved_sectors;
    uint8                   num_of_fats;
    tFatType                fat_type;

    // Disk/Media API
//    struct disk_if          disk_io;
    struct disk_if* m_disk_io;

    // [Optional] Thread Safety
    void                    (*fl_lock)(void);
    void                    (*fl_unlock)(void);

    // Working buffer
    struct fat_buffer        currentsector;

    // FAT Buffer
    struct fat_buffer        *fat_buffer_head;
    struct fat_buffer        fat_buffers[FAT_BUFFERS];

// 原来的全局变量局部化
    FL_FILE         m_files[FATFS_MAX_OPEN_FILES];
    int             m_filelib_init;
    int             m_filelib_valid;
    struct fat_list m_open_file_list;
    struct fat_list m_free_file_list;
};

struct fs_dir_list_status
{
    uint32                  sector;
    uint32                  cluster;
    uint8                   offset;
};

struct fs_dir_ent
{
    char                    filename[FATFS_MAX_LONG_FILENAME];
    uint8                   is_dir;
    uint32                  cluster;
    uint32                  size;

#if FATFS_INC_TIME_DATE_SUPPORT
    uint16                  access_date;
    uint16                  write_time;
    uint16                  write_date;
    uint16                  create_date;
    uint16                  create_time;
#endif
};

//-----------------------------------------------------------------------------
// Prototypes
//-----------------------------------------------------------------------------
int     fatfs_init(struct fatfs *fs);
uint32  fatfs_lba_of_cluster(struct fatfs *fs, uint32 Cluster_Number);
int     fatfs_sector_reader(struct fatfs *fs, uint32 Startcluster, uint32 offset, uint8 *target);
int     fatfs_sector_read(struct fatfs *fs, uint32 lba, uint8 *target, uint32 count);
int     fatfs_sector_write(struct fatfs *fs, uint32 lba, uint8 *target, uint32 count);
int     fatfs_sync(struct fatfs* fs);
int     fatfs_read_sector(struct fatfs *fs, uint32 cluster, uint32 sector, uint8 *target);
int     fatfs_write_sector(struct fatfs *fs, uint32 cluster, uint32 sector, uint8 *target);
void    fatfs_show_details(struct fatfs *fs);
uint32  fatfs_get_root_cluster(struct fatfs *fs);
uint32  fatfs_get_file_entry(struct fatfs *fs, uint32 Cluster, char *nametofind, struct fat_dir_entry *sfEntry);
int     fatfs_sfn_exists(struct fatfs *fs, uint32 Cluster, char *shortname);
int     fatfs_update_file_length(struct fatfs *fs, uint32 Cluster, char *shortname, uint32 fileLength);
int     fatfs_mark_file_deleted(struct fatfs *fs, uint32 Cluster, char *shortname);
void    fatfs_list_directory_start(struct fatfs *fs, struct fs_dir_list_status *dirls, uint32 StartCluster);
int     fatfs_list_directory_next(struct fatfs *fs, struct fs_dir_list_status *dirls, struct fs_dir_ent *entry);
int     fatfs_update_timestamps(struct fat_dir_entry *directoryEntry, int create, int modify, int access);

#endif

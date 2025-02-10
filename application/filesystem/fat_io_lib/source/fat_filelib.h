#ifndef __FAT_FILELIB_H__
#define __FAT_FILELIB_H__

#include "fat_opts.h"
#include "fat_access.h"
#include "fat_list.h"

//-----------------------------------------------------------------------------
// Defines
//-----------------------------------------------------------------------------
#ifndef SEEK_CUR
    #define SEEK_CUR    1
#endif

#ifndef SEEK_END
    #define SEEK_END    2
#endif

#ifndef SEEK_SET
    #define SEEK_SET    0
#endif

#ifndef EOF
    #define EOF         (-1)
#endif

//-----------------------------------------------------------------------------
// Structures
//-----------------------------------------------------------------------------
struct sFL_FILE;



//-----------------------------------------------------------------------------
// Prototypes
//-----------------------------------------------------------------------------

// External
void                fl_init(struct fatfs* fs/*, struct disk_if * disk*/);
void                fl_attach_locks(struct fatfs* fs, void (*lock)(void), void (*unlock)(void));
//int                 fl_attach_media(struct fatfs* fs, fn_diskio_read rd, fn_diskio_write wr, fn_sync sync);
//int					pre_attach_media(struct fatfs* fs, fn_diskio_read rd, fn_diskio_write wr, fn_sync sync);
//int                 fl_attach_media(struct fatfs* fs, struct disk_if * io);
//int					pre_attach_media(struct fatfs* fs, struct disk_if * io);
void                fl_shutdown(struct fatfs* fs);

// Standard API
void*               fl_fopen(struct fatfs* fs, const char *path, const char *modifiers);
void                fl_fclose(struct fatfs* fs, void *file);
int                 fl_fflush(struct fatfs* fs, void *file);
int                 fl_fgetc(struct fatfs* fs, void *file);
char *              fl_fgets(struct fatfs* fs, char *s, int n, void *f);
int                 fl_fputc(struct fatfs* fs, int c, void *file);
int                 fl_fputs(struct fatfs* fs, const char * str, void *file);
int                 fl_fwrite(struct fatfs* fs, const void * data, int size, int count, void *file );
int                 fl_fread(struct fatfs* fs, void * data, int size, int count, void *file );
int                 fl_fseek(struct fatfs* fs, void *file , long offset , int origin );
int                 fl_fgetpos(struct fatfs* fs, void *file , uint32 * position);
long                fl_ftell(struct fatfs* fs, void *f);
int                 fl_feof(struct fatfs* fs, void *f);
int                 fl_remove(struct fatfs* fs, const char * filename);

// Equivelant dirent.h
typedef struct fs_dir_list_status    FL_DIR;
typedef struct fs_dir_ent            fl_dirent;

FL_DIR*             fl_opendir(struct fatfs* fs, const char* path, FL_DIR *dir);
int                 fl_readdir(struct fatfs* fs, FL_DIR *dirls, fl_dirent *entry);
int                 fl_closedir(struct fatfs* fs, FL_DIR* dir);

// Extensions
void                fl_listdirectory(struct fatfs* fs, const char *path);
int                 fl_createdirectory(struct fatfs* fs, const char *path);
int                 fl_is_dir(struct fatfs* fs, const char *path);

int                 fl_format(struct fatfs* fs, uint32 volume_sectors, const char *name);

int					fl_fsck(struct fatfs * fs, int repair);

// Test hooks
//#ifdef FATFS_INC_TEST_HOOKS
//struct fatfs*       fl_get_fs(void);
//#endif

//-----------------------------------------------------------------------------
// Stdio file I/O names
//-----------------------------------------------------------------------------
#ifdef USE_FILELIB_STDIO_COMPAT_NAMES

#define FILE            FL_FILE

#define fopen(a,b)      fl_fopen(a, b)
#define fclose(a)       fl_fclose(a)
#define fflush(a)       fl_fflush(a)
#define fgetc(a)        fl_fgetc(a)
#define fgets(a,b,c)    fl_fgets(a, b, c)
#define fputc(a,b)      fl_fputc(a, b)
#define fputs(a,b)      fl_fputs(a, b)
#define fwrite(a,b,c,d) fl_fwrite(a, b, c, d)
#define fread(a,b,c,d)  fl_fread(a, b, c, d)
#define fseek(a,b,c)    fl_fseek(a, b, c)
#define fgetpos(a,b)    fl_fgetpos(a, b)
#define ftell(a)        fl_ftell(a)
#define feof(a)         fl_feof(a)
#define remove(a)       fl_remove(a)
#define mkdir(a)        fl_createdirectory(a)
#define rmdir(a)        0

#endif

#endif

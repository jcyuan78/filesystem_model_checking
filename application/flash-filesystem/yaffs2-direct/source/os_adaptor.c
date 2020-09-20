#include "../yaffs-guts/yaffsfs.h"
#include "../yaffs-guts/yaffs_guts.h"






// OS adaptor
void *yaffsfs_malloc(size_t size) 
{
	return malloc(size); 
};
	
void yaffsfs_free(void *ptr) 
{
	free(ptr); 
};


// dummy implement


#define JCASSERT(a)
// dummy implement for OS
void yaffsfs_Lock(void) { JCASSERT(0); };
void yaffsfs_Unlock(void) { JCASSERT(0); };

u32 yaffsfs_CurrentTime(void) { JCASSERT(0); return 0; };

void yaffsfs_SetError(int err) { JCASSERT(0); };


int yaffsfs_CheckMemRegion(const void *addr, size_t size, int write_request) { JCASSERT(0); return 0; };
void yaffsfs_OSInitialisation(void) { JCASSERT(0); };
int yaffsfs_GetLastError(void) { JCASSERT(0); return 0; };

const YCHAR *yaffs_error_to_str(int err) { JCASSERT(0); return NULL; }

int yaffs_checkpoint_restore(struct yaffs_dev *dev) { JCASSERT(0); return 0; };

unsigned int yaffs_trace_mask = 0;
unsigned int yaffs_wr_attempts = 0;
void yaffs_bug_fn(const char *file_name, int line_no) { JCASSERT(0); }

void yaffs_tags_compat_install(struct yaffs_dev *dev) { JCASSERT(0); }
void yaffs_calc_tags_ecc(struct yaffs_tags *tags) { JCASSERT(0); }
int yaffs_check_tags_ecc(struct yaffs_tags *tags) { JCASSERT(0); return 0; };











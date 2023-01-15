#pragma once

#include "include/linux_comm.h"
#include "include/fs.h"

#include "include/falloc.h"
#include "include/list.h"
#include "include/buffer_head.h"
#include "include/dcache.h"
#include "include/mm_types.h"
#include "include/blkdev.h"
#include "include/blk_types.h"
#include "include/llist.h"
#include "include/bitops.h"
#include "include/rbtree.h"
#include "include/stat.h"
#include "include/fscrypt.h"
#include "include/pagemap.h"
#include "include/uio.h"

#include "include/address-space.h"
#include "include/buffer-manager.h"
#include "include/inode-manager.h"
#include "include/linux-fs-base.h"

#include "include/writeback.h"

#include "include/xarray.h"
#include "include/bio.h"
#include "include/page-manager.h"
#include "include/allocator.h"

#pragma comment (lib, "linux-fs-wrapper.lib")

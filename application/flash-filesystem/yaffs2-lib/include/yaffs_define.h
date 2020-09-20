#pragma once


#include <dokanfs-lib.h>
#include <nand_driver.h>


#define CONFIG_YAFFS_WINCE


typedef BYTE	u8;
typedef UINT16	u16;
typedef UINT32	u32;
typedef INT32	s32;
typedef size_t	loff_t;

typedef wchar_t			YCHAR;
typedef unsigned char	YUCHAR;

#define YAFFS_SHORT_NAME_LENGTH		15
#define YAFFS_NOBJECT_BUCKETS		256
#define YAFFS_N_TEMP_BUFFERS		6


#define NAME_MAX	256
#define YAFFS_PATH_DIVIDERS  "//"
#define YAFFS_PATH_DIVIDERS_CHAR  '//'
// for unicode
#define YAFFS_MAX_NAME_LENGTH		127		
#define YAFFS_MAX_ALIAS_LENGTH		79

#define YAFFSFS_MAX_SYMLINK_DEREFERENCES 5

//#define YAFFS_LOSTNFOUND_NAME		L"lost+found"

/* Some special object ids for pseudo objects */
#define YAFFS_OBJECTID_ROOT		1
#define YAFFS_OBJECTID_LOSTNFOUND	2
#define YAFFS_OBJECTID_UNLINKED		3
#define YAFFS_OBJECTID_DELETED		4

#define YAFFS_LOSTNFOUND_NAME		L"$lost+found"
#define YAFFS_LOSTNFOUND_PREFIX		L"obj"
#define YAFFS_DELETEDIR_NAME		L"$delete"
#define YAFFS_ROOTDIR_NAME			L""
#define YAFFS_UNLINKEDIR_NAME		L"$unlinked"


#define YAFFS_NTNODES_LEVEL0		16
#define YAFFS_NTNODES_INTERNAL		(YAFFS_NTNODES_LEVEL0 / 2)


#define YAFFS_MAX_SHORT_OP_CACHES	20

/*
 * Tnodes form a tree with the tnodes in "levels"
 * Levels greater than 0 hold 8 slots which point to other tnodes.
 * Those at level 0 hold 16 slots which point to chunks in NAND.
 *
 * A maximum level of 8 thust supports files of size up to:
 *
 * 2^(3*MAX_LEVEL+4)
 *
 * Thus a max level of 8 supports files with up to 2^^28 chunks which gives
 * a maximum file size of around 512Gbytees with 2k chunks.
 */
#define YAFFS_NTNODES_LEVEL0		16
#define YAFFS_TNODES_LEVEL0_BITS	4
#define YAFFS_TNODES_LEVEL0_MASK	0xf

#define YAFFS_NTNODES_INTERNAL		(YAFFS_NTNODES_LEVEL0 / 2)
#define YAFFS_TNODES_INTERNAL_BITS	(YAFFS_TNODES_LEVEL0_BITS - 1)
#define YAFFS_TNODES_INTERNAL_MASK	0x7
#define YAFFS_TNODES_MAX_LEVEL		8
#define YAFFS_TNODES_MAX_BITS		(YAFFS_TNODES_LEVEL0_BITS + \
					YAFFS_TNODES_INTERNAL_BITS * \
					YAFFS_TNODES_MAX_LEVEL)
#define YAFFS_MAX_CHUNK_ID		((1 << YAFFS_TNODES_MAX_BITS) - 1)


#define YAFFS_MAX_FILE_SIZE_32		0x7fffffff


#define YAFFS_ROOT_MODE			0666
#define YAFFS_LOSTNFOUND_MODE		0666


 /* Note YAFFS_GC_GOOD_ENOUGH must be <= YAFFS_GC_PASSIVE_THRESHOLD */
#define YAFFS_GC_GOOD_ENOUGH 2
#define YAFFS_GC_PASSIVE_THRESHOLD 4


class CYaffsObject;


/*
 * This is a simple doubly linked list implementation that matches the
 * way the Linux kernel doubly linked list implementation works.
 */


/*--------------------------- Tnode -------------------------- */
// tnode是一个对称树结构，每个节点拥有YAFFS_NTNODES_INTERNAL个节点（缺省为8）
// tnode的根节点为顶层(top)。
struct yaffs_tnode 
{
	yaffs_tnode *internal[YAFFS_NTNODES_INTERNAL];
};


/* yaffs1 tags structures in RAM
 * NB This uses bitfield. Bitfields should not straddle a u32 boundary
 * otherwise the structure size will get blown out.
 */


struct yaffs_tags 
{
	u32 chunk_id : 20;
	u32 serial_number : 2;
	u32 n_bytes_lsb : 10;
	u32 obj_id : 18;
	u32 ecc : 12;
	u32 n_bytes_msb : 2;
};

union yaffs_tags_union 
{
	struct yaffs_tags as_tags;
	u8  as_bytes[8];
	u32 as_u32[2];
};



/* Spare structure for YAFFS1 */
struct yaffs_spare 
{
	u8 tb0;
	u8 tb1;
	u8 tb2;
	u8 tb3;
	u8 page_status;		/* set to 0 to delete the chunk */
	u8 block_status;
	u8 tb4;
	u8 tb5;
	u8 ecc1[3];
	u8 tb6;
	u8 tb7;
	u8 ecc2[3];
};


/*------------------------  Object -----------------------------*/
/* An object can be one of:
 * - a directory (no data, has children links
 * - a regular file (data.... not prunes :->).
 * - a symlink [symbolic link] (the alias).
 * - a hard link
 */

 /* The file variant has three file sizes:
  *  - file_size : size of file as written into Yaffs - including data in cache.
  *  - stored_size - size of file as stored on media.
  *  - shrink_size - size of file that has been shrunk back to.
  *
  * The stored_size and file_size might be different because the data written
  * into the cache will increase the file_size but the stored_size will only
  * change when the data is actually stored.
  *
  */

struct yaffs_symlink_var {
	YCHAR *alias;
};

struct yaffs_hardlink_var {
	struct yaffs_obj *equiv_obj;
	u32 equiv_id;
};

struct yaffs_obj {
	u8 deleted : 1;			/* This should only apply to unlinked files. */
	u8 unlinked : 1;		/* An unlinked file.*/
	u8 fake : 1;			/* A fake object has no presence on NAND. */
	u8 rename_allowed : 1;	/* Some objects cannot be renamed. */
	u8 unlink_allowed : 1;
	u8 dirty : 1;			/* the object needs to be written to flash */
	u8 valid : 1;			/* When the file system is being loaded up, this
							 * object might be created before the data is available
							 * ie. file data chunks encountered before the header. */
	/* This object has been lazy loaded and is missing some detail */
	u8 lazy_loaded : 1;	
	u8 defered_free : 1;	/* Object is removed from NAND, but is still in the inode cache.
							 * Free of object is defered. until the inode is released.  */
	u8 being_created : 1;	/* This object is still being created so skip some verification checks. */
	u8 is_shadowed : 1;		/* This object is shadowed on the way to being renamed. */
	u8 xattr_known : 1;		/* We know if this has object has xattribs or not. */
	u8 has_xattr : 1;		/* This object has xattribs. Only valid if xattr_known. */
	u8 serial;				/* serial number of chunk in NAND.*/
	u16 sum;				/* sum of the name to speed searching */

	/* directory structure stuff */
	/* also used for linking up the free list */
	/* Where's my object header in NAND? */
	int hdr_chunk;
	//int n_data_chunks;		/* Number of data chunks for this file. */
	u32 obj_id;				/* the object id value */
	u32 yst_mode;
	YCHAR short_name[YAFFS_SHORT_NAME_LENGTH + 1];

#ifdef CONFIG_YAFFS_WINCE
	FILETIME win_ctime;
	FILETIME win_mtime;
	FILETIME win_atime;
#else
	u32 yst_uid;
	u32 yst_gid;
	u32 yst_atime;
	u32 yst_mtime;
	u32 yst_ctime;
#endif

	//u32 yst_rdev;
	//void *my_inode;
};


struct yaffs_buffer {
	u8 *buffer;
	int in_use;
};


/*---------------------------------------------------------------------------*/
// -- yaffs driver and yaffs dev

/* Stuff used for extended tags in YAFFS2 */

enum yaffs_block_state {
	YAFFS_BLOCK_STATE_UNKNOWN = 0,

	/* Being scanned */
	YAFFS_BLOCK_STATE_SCANNING,

	/* The block might have something on it (ie it is allocating or full,
	 * perhaps empty) but it needs to be scanned to determine its true state.
	 * This state is only valid during scanning.
	 * NB We tolerate empty because the pre-scanner might be incapable of  deciding
	 * However, if this state is returned on a YAFFS2 device, then we expect a sequence number	 */
	YAFFS_BLOCK_STATE_NEEDS_SCAN,

	/* This block is empty */
	YAFFS_BLOCK_STATE_EMPTY,

	/* This block is partially allocated. At least one page holds valid data.
	 * This is the one currently being used for page allocation. Should never 
	 be more than one of these. If a block is only partially allocated at mount 
	 it is treated as full.	 */
	YAFFS_BLOCK_STATE_ALLOCATING,

	/* All the pages in this block have been allocated. If a block was only 
	partially allocated when mounted we treat it as fully allocated. */
	YAFFS_BLOCK_STATE_FULL,

	/* The block was full and now all chunks have been deleted. Erase me, reuse me.	 */
	YAFFS_BLOCK_STATE_DIRTY,

	/* This block is assigned to holding checkpoint data. */
	YAFFS_BLOCK_STATE_CHECKPOINT,

	/* This block is being garbage collected */
	YAFFS_BLOCK_STATE_COLLECTING,

	/* This block has failed and is not in use */
	YAFFS_BLOCK_STATE_DEAD
};



struct yaffs_param 
{
	const YCHAR *name;

	/* Entry parameters set up way early. Yaffs sets up the rest.
	 * The structure should be zeroed out before use so that unused and default values are zero.	 */

	//bool inband_tags;			/* Use unband tags */ //yaffs2 不支持inband tags
	UINT32 total_bytes_per_chunk;	/* Should be >= 512, does not need to be a power of 2 */
	UINT32 chunks_per_block;		/* does not need to be a power of 2 */
	UINT32 spare_bytes_per_chunk;	/* spare area size */
	UINT32 start_block;			/* Start block we're allowed to use */
	UINT32 end_block;				/* End block we're allowed to use */
	UINT32 n_reserved_blocks;		/* Tuneable so that we can reduce reserved blocks on NOR and RAM. */
	UINT32 n_caches;				/* If == 0, then short op caching is disabled,
								 * else the number of short op caches. */
	bool cache_bypass_aligned;	/* If non-zero then bypass the cache for aligned writes. */
	bool use_nand_ecc;			/* Flag to decide whether or not to use NAND driver ECC on data (yaffs1) */
	bool tags_9bytes;			/* Use 9 byte tags */
	bool no_tags_ecc;			/* Flag to decide whether or not to do ECC on packed tags (yaffs2) */
	//bool is_yaffs2;				/* Use yaffs2 mode on this device */	// 只支持yaffs2
	bool empty_lost_n_found;	/* Auto-empty lost+found directory on mount */
	int refresh_period;			/* How often to check for a block refresh */

	/* Checkpoint control. Can be set before or after initialisation */
	u8 skip_checkpt_rd;
	u8 skip_checkpt_wr;

	int enable_xattr;			/* Enable xattribs */
	int max_objects;			/* Set to limit the number of objects created. 0 = no limit.*/
	bool hide_lost_n_found;		/* Set non-zero to hide the lost-n-found dir. */

	int stored_endian;			/* 0=cpu endian, 1=little endian, 2=big endian */

	/* The remove_obj_fn function must be supplied by OS flavours that need it.
	 * yaffs direct uses it to implement the faster readdir.
	 * Linux uses it to protect the directory during unlocking.	 */
	void(*remove_obj_fn) (struct yaffs_obj *obj);
	/* Callback to mark the superblock dirty */
	void(*sb_dirty_fn) (struct yaffs_dev *dev);
	/*  Callback to control garbage collection. */
	unsigned(*gc_control_fn) (struct yaffs_dev *dev);
	/* Debug control flags. Don't use unless you know what you're doing */
	bool use_header_file_size;	/* Flag to determine if we should use file sizes from the header */
	bool disable_lazy_load;		/* Disable lazy loading on this device */
	bool wide_tnodes_disabled;	/* Set to disable wide tnodes */
	bool disable_soft_del;		/* yaffs 1 only: Set to disable the use of softdeletion. */
	int defered_dir_update;		/* Set to defer directory updates */

#ifdef CONFIG_YAFFS_AUTO_UNICODE
	bool auto_unicode;
#endif
	bool always_check_erased;	/* Force chunk erased check always on */
	bool disable_summary;
	bool disable_bad_block_marking;
};


/* ChunkCache is used for short read/write operations.*/
struct yaffs_cache 
{
	CYaffsObject *object;
	int chunk_id;
	int last_use;
	bool dirty;
	int n_bytes;		/* Only valid if the cache is dirty */
	bool locked;		/* Can't push out or flush while locked. */
	u8 *data;
};

struct yaffs_allocator
{
	int n_tnodes_created;
	struct yaffs_tnode *free_tnodes;
	int n_free_tnodes;
	struct yaffs_tnode_list *alloc_tnode_list;
	int n_obj_created;
	int n_free_objects;
	struct yaffs_obj_list *allocated_obj_list;
};

struct yaffs_ext_tags
{
	unsigned chunk_used;		/*  Status of the chunk: used or unused */
	unsigned obj_id;			/* If 0 this is not used */
	unsigned chunk_id;			/* If 0 this is a header, else a data chunk */
	unsigned n_bytes;			/* Only valid for data chunks */

	/* The following stuff only has meaning when we read */
	INandDriver::ECC_RESULT ecc_result;
	unsigned block_bad;

	/* YAFFS 1 stuff */	// yaffs2 ONLY
	//unsigned is_deleted;		/* The chunk is marked deleted */			
	unsigned serial_number;		/* Yaffs1 2-bit serial number */

	/* YAFFS2 stuff */
	unsigned seq_number;		/* The sequence number of this block */

	/* Extra info if this is an object header (YAFFS2 only) */
	unsigned extra_available;	/* Extra info available if not zero */
	unsigned extra_parent_id;	/* The parent object */
	unsigned extra_is_shrink;	/* Is it a shrink header? */
	unsigned extra_shadows;		/* Does this shadow another object? */

	enum yaffs_obj_type extra_obj_type;	/* What object type? */

	loff_t extra_file_size;		/* Length if it is a file */
	unsigned extra_equiv_id;	/* Equivalent object for a hard link */
};


/* yaffs_checkpt_obj holds the definition of an object as dumped
 * by checkpointing. */

 /*  Checkpint object bits in bitfield: offset, length */
#define CHECKPOINT_VARIANT_BITS				0, 3
#define CHECKPOINT_DELETED_BITS				3, 1
#define CHECKPOINT_SOFT_DEL_BITS			4, 1
#define CHECKPOINT_UNLINKED_BITS			5, 1
#define CHECKPOINT_FAKE_BITS				6, 1
#define CHECKPOINT_RENAME_ALLOWED_BITS		7, 1
#define CHECKPOINT_UNLINK_ALLOWED_BITS		8, 1
#define CHECKPOINT_SERIAL_BITS				9, 8

struct yaffs_checkpt_obj 
{
	int struct_type;
	u32 obj_id;
	u32 parent_id;
	int hdr_chunk;
	u32 bit_field;
	int n_data_chunks;
	loff_t size_or_equiv_obj;
};


#define ERROR_HANDLE(act, ...)	\
	{LOG_ERROR(__VA_ARGS__); act;}

struct yaffs_block_info
{
	s32 _soft_del_pages : 10;	/* number of soft deleted pages */ // Yaffs2不支持softdelete
	s32 pages_in_use : 10;	/* number of pages in use */
	u32 block_state : 4;	/* One of the above block states. */
				/* NB use unsigned because enum is sometimes
				 * an int */
	u32 needs_retiring : 1;	/* Data has failed on this block, */
				/*need to get valid data off and retire*/
	u32 skip_erased_check : 1;/* Skip the erased check on this block */
	u32 gc_prioritise : 1;	/* An ECC check or blank check has failed.
				   Block should be prioritised for GC */
	u32 chunk_error_strikes : 3;	/* How many times we've had ecc etc
				failures on this block and tried to reuse it */
	u32 has_summary : 1;	/* The block has a summary */

	u32 has_shrink_hdr : 1;	/* This block has at least one shrink header */
	u32 seq_number;		/* block sequence number for yaffs2 */
};

enum yaffs_obj_type
{
	YAFFS_OBJECT_TYPE_UNKNOWN,
	YAFFS_OBJECT_TYPE_FILE,
	YAFFS_OBJECT_TYPE_SYMLINK,
	YAFFS_OBJECT_TYPE_DIRECTORY,
	YAFFS_OBJECT_TYPE_HARDLINK,
	YAFFS_OBJECT_TYPE_SPECIAL
};


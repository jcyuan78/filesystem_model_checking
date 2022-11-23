///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
#pragma once


#include "linux_comm.h"

#include "xarray.h"
#include "rbtree.h"
//#include "blk_types.h"
#include "list.h"
//#include "writeback.h"
#include <map>
//#include "page-manager.h"

struct page;
struct inode;
struct file;
struct kiocb;
struct iov_iter;
struct swap_info_struct;





struct writeback_control;
struct readahead_control;
class CPageManager;

/**
 * address_space - Contents of a cacheable, mappable object.
 * @host: Owner, either the inode or the block_device.
 * @i_pages: Cached pages.
 * @gfp_mask: Memory allocation flags to use for allocating pages.
 * @i_mmap_writable: Number of VM_SHARED mappings.
 * @nr_thps: Number of THPs in the pagecache (non-shmem only).
 * @i_mmap: Tree of private and shared mappings.
 * @i_mmap_rwsem: Protects @i_mmap and @i_mmap_writable.
 * @nrpages: Number of page entries, protected by the i_pages lock.
 * @writeback_index: Writeback starts here.
 * @a_ops: Methods.
 * @flags: Error bits and flags (AS_*).
 * @wb_err: The most recent error which has occurred.
 * @private_lock: For use by the owner of the address_space.
 * @private_list: For use by the owner of the address_space.
 * @private_data: For use by the owner of the address_space.
 */
class address_space
{
public:
	address_space(CPageManager * manager): m_page_manager(manager) {}
public:
	virtual int write_page(page* page, writeback_control* wbc) = 0;
	virtual int write_pages(/*address_space* mapping,*/ writeback_control* wbc) = 0;
	virtual int set_node_page_dirty(page* page) = 0;
	// default = block_invalidatepage()
	virtual void invalidate_page(page* ppage, unsigned int offset, unsigned int length);
	virtual int release_page(page* page, gfp_t wait) = 0;
	virtual void freepage(page*) {}
	virtual int migrate_page(page* newpage, page* page, enum migrate_mode mode)	UNSUPPORT_1(int);
	virtual int read_page(file* file, struct page* page) = 0;
	/* Reads in the requested pages. Unlike ->readpage(), this is PURELY used for read-ahead!. */
	virtual bool support_readpages(void) { return false; }
	virtual int readpages(file* filp, address_space* mapping, list_head* pages, unsigned nr_pages) { UNSUPPORT_1(int); }

	virtual bool support_read_ahead(void) { return false; }
	virtual void read_ahead(readahead_control* rac)=0;

	virtual int write_begin(file* file, loff_t pos, unsigned len, unsigned flags, page** page, void** fsdata)=0;
	virtual int write_end(file* file, loff_t pos, unsigned len, unsigned copied, page* page, void* fsdata)=0;
	virtual ssize_t direct_IO(kiocb* iocb, iov_iter* iter)=0;
	virtual sector_t bmap(sector_t block)=0;
	virtual int swap_activate(swap_info_struct* sis, file* file, sector_t* span)=0;
	virtual void swap_deactivate(file* file)=0;

	virtual bool support_is_paritially_uptodate() { return false; }
	virtual int is_partially_uptodate(struct page*, unsigned long, unsigned long) { UNSUPPORT_1(int); }

public:
	void unmap_mapping_pages(pgoff_t start, pgoff_t nr, bool even_cows) {};		// for MMU only

// ==== fs.h ====
public:
	bool mapping_tagged(xa_mark_t tag) 	{ return xa_marked(&i_pages, tag); }

// ==== filemap.cpp ====
public:
	int do_writepages(writeback_control* wbc);
	int __filemap_fdatawrite_range(loff_t start, loff_t end, int sync_mode);
	int filemap_fdatawrite(void);
	CPageManager* GetPageManager(void) { return m_page_manager; }

protected:
	inline int __filemap_fdatawrite(int sync_mode)	{return __filemap_fdatawrite_range(0, LLONG_MAX, sync_mode); }

public:
	inode* host;
	xarray			i_pages;	//page映射
//	std::map<UINT64, page*>		i_pages;
	gfp_t			gfp_mask;
	atomic_t		i_mmap_writable;
#ifdef CONFIG_READ_ONLY_THP_FOR_FS
	/* number of thp, only for non-shmem files */
	atomic_t		nr_thps;
#endif
	rb_root_cached				i_mmap;	//VMA映射
	SRWLOCK /*rw_semaphore*/	i_mmap_rwsem;
	unsigned long				nrpages; //page frames的总数
	pgoff_t						writeback_index;
	//<YUAN> 虚函数实现op
//	const struct address_space_operations *a_ops;
	unsigned long		flags;
	errseq_t		wb_err;
	spinlock_t		private_lock;
	struct list_head	private_list;
	void* private_data;

protected:
	CPageManager* m_page_manager;
};


void __xa_clear_mark(xarray* xa, unsigned long index, xa_mark_t mark);

//<YUAN> defined in linux/writeback.h
/* struct readahead_control - Describes a readahead request.
 *
 * A readahead request is for consecutive pages.  Filesystems which implement the ->readahead method should call
 readahead_page() or readahead_page_batch() in a loop and attempt to start I/O against each page in the request.
 *
 * Most of the fields in this struct are private and should be accessed by the functions below.
 *
 * @file: The file, used primarily by network filesystems for authentication. May be NULL if invoked internally by
 the filesystem.
 * @mapping: Readahead this filesystem object.
 * @ra: File readahead state.  May be NULL. */
//struct readahead_control {
//	struct file* file;
//	address_space* mapping;
//	struct file_ra_state* ra;
//	/* private: use the readahead_* accessors instead */
//	pgoff_t _index;
//	unsigned int _nr_pages;
//	unsigned int _batch_count;
//};

/* readahead_page - Get the next page to read.
 * @rac: The current readahead request.
 *
 * Context: The page is locked and has an elevated refcount.  The caller should decreases the refcount once the page
 has been submitted for I/O and unlock the page once all I/O to that page has completed.
 * Return: A pointer to the next page, or %NULL if we are done. */
//static inline page* readahead_page(readahead_control* rac)
//{
//	page* ppage;
//
//	BUG_ON(rac->_batch_count > rac->_nr_pages);
//	rac->_nr_pages -= rac->_batch_count;
//	rac->_index += rac->_batch_count;
//
//	if (!rac->_nr_pages)
//	{
//		rac->_batch_count = 0;
//		return NULL;
//	}
//
//	ppage = xa_load(&rac->mapping->i_pages, rac->_index);
//	//	VM_BUG_ON_PAGE(!PageLocked(ppage), ppage);
////	rac->_batch_count = thp_nr_pages(ppage);
//	rac->_batch_count = 1;
//
//	return ppage;
//}

/**readahead_count - The number of pages in this readahead request.
 * @rac: The readahead request.*/
//static inline unsigned int readahead_count(readahead_control* rac)
//{
//	return rac->_nr_pages;
//}
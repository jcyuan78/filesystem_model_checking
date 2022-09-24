///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

#include <linux-fs-wrapper.h>

class Cf2fsMappingBase : public address_space
{
public:
	Cf2fsMappingBase(f2fs_inode_info * inode);
public:
	virtual void invalidate_page(page* page, unsigned int offset, unsigned int length);
	virtual int release_page(page* page, gfp_t wait);

	virtual int read_page(file* file, struct page* page) { UNSUPPORT_1(int); }
	virtual void read_ahead(readahead_control* rac) { UNSUPPORT_0; }
	virtual int write_begin(file* file, loff_t pos, unsigned len, unsigned flags, page** page, void** fsdata) { UNSUPPORT_1(int); }
	virtual int write_end(file* file, loff_t pos, unsigned len, unsigned copied, page* page, void* fsdata) { UNSUPPORT_1(int); }
	virtual ssize_t direct_IO(kiocb* iocb, iov_iter* iter) { UNSUPPORT_1(int); }
	virtual sector_t bmap(sector_t block) { UNSUPPORT_1(int); }
	virtual int swap_activate(swap_info_struct* sis, file* file, sector_t* span) { UNSUPPORT_1(int); }
	virtual void swap_deactivate(file* file) { UNSUPPORT_0; }


};

class Cf2fsNodeMapping : public Cf2fsMappingBase
{
public:
	Cf2fsNodeMapping(f2fs_inode_info* inode) : Cf2fsMappingBase(inode) {}

public:
	virtual void invalidate_page(page* page, unsigned int offset, unsigned int length);

	virtual int write_page(page* page, writeback_control* wbc);
	virtual int write_pages(/*address_space* mapping,*/ writeback_control* wbc);
	virtual int set_node_page_dirty(page* page);

};

class Cf2fsMetaMapping : public Cf2fsMappingBase
{
public:
	Cf2fsMetaMapping(f2fs_inode_info* inode) : Cf2fsMappingBase(inode) {}

public:
	virtual void invalidate_page(page* page, unsigned int offset, unsigned int length);

	virtual int write_page(page* page, writeback_control* wbc);
	virtual int write_pages(writeback_control* wbc);
	virtual int set_node_page_dirty(page* page);
	//virtual int release_page(page* page, gfp_t wait) UNSUPPORT_1(int);
	//virtual int migrate_page(page* newpage, page* page, enum migrate_mode mode) UNSUPPORT_1(int);

};

class Cf2fsDataMapping : public Cf2fsMappingBase
{
public:
	Cf2fsDataMapping(f2fs_inode_info* inode) : Cf2fsMappingBase(inode) {}

public:
	virtual int write_page(page* page, writeback_control* wbc);
	virtual int write_pages(/*address_space* mapping,*/ writeback_control* wbc);
	virtual int set_node_page_dirty(page* page);

	virtual int read_page(file* file, struct page* page);

	virtual bool support_read_ahead(void) { return true; }
	virtual void read_ahead(readahead_control* rac);

	virtual int write_begin(file* file, loff_t pos, unsigned len, unsigned flags, page** page, void** fsdata);
	virtual int write_end(file* file, loff_t pos, unsigned len, unsigned copied, page* page, void* fsdata);
	virtual ssize_t direct_IO(kiocb* iocb, iov_iter* iter);
	virtual sector_t bmap(sector_t block);
	virtual int swap_activate(swap_info_struct* sis, file* file, sector_t* span);
	virtual void swap_deactivate(file* file);

	//
	//const struct address_space_operations f2fs_dblock_aops = {
	//	.readpage	= f2fs_read_data_page,
	//	.readahead	= f2fs_readahead,
	//	.writepage	= f2fs_write_data_page,
	//	.writepages	= f2fs_write_data_pages,
	//	.write_begin	= f2fs_write_begin,
	//	.write_end	= f2fs_write_end,
	//	.set_page_dirty	= f2fs_set_data_page_dirty,
	//	.invalidatepage	= f2fs_invalidate_page,
	//	.releasepage	= f2fs_release_page,
	//	.direct_IO	= f2fs_direct_IO,
	//	.bmap		= f2fs_bmap,
	//	.swap_activate  = f2fs_swap_activate,
	//	.swap_deactivate = f2fs_swap_deactivate,
	//#ifdef CONFIG_MIGRATION
	//	.migratepage    = f2fs_migrate_page,
	//#endif
	//};
};


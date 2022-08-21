///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "fs.h"
#include <list>

/*
 * This hash multiplies the input by a large odd number and takes the high bits.  Since multiplication propagates 
   changes to the most significant end only, it is essential that the high bits of the product be used for the hash 
   value.
 *
 * Chuck Lever verified the effectiveness of this technique:
 * http://www.citi.umich.edu/techreports/reports/citi-tr-00-1.pdf
 *
 * Although a random odd number will do, it turns out that the golden ratio phi = (sqrt(5)-1)/2, or its negative, has
   particularly nice properties.  (See Knuth vol 3, section 6.4, exercise 9.)
 *
 * These are the negative, (1 - phi) = phi**2 = (3 - sqrt(5))/2, which is very slightly easier to multiply by and makes
   no difference to the hash distribution. */
#define GOLDEN_RATIO_32 0x61C88647
#define GOLDEN_RATIO_64 0x61C8864680B583EBull

#define GOLDEN_RATIO_PRIME GOLDEN_RATIO_32
#define L1_CACHE_BYTES 1024


 /*
  * Inode locking rules:
  *
  * inode->i_lock protects:
  *   inode->i_state, inode->i_hash, __iget()
  * Inode LRU list locks protect:
  *   inode->i_sb->s_inode_lru, inode->i_lru
  * inode->i_sb->s_inode_list_lock protects:
  *   inode->i_sb->s_inodes, inode->i_sb_list
  * bdi->wb.list_lock protects:
  *   bdi->wb.b_{dirty,io,more_io,dirty_time}, inode->i_io_list
  * inode_hash_lock protects:
  *   inode_hashtable, inode->i_hash
  *
  * Lock ordering:
  *
  * inode->i_sb->s_inode_list_lock
  *   inode->i_lock
  *     Inode LRU list locks
  *
  * bdi->wb.list_lock
  *   inode->i_lock
  *
  * inode_hash_lock
  *   inode->i_sb->s_inode_list_lock
  *   inode->i_lock
  *
  * iunique_lock
  *   inode_hash_lock
  */

class CInodeManager
{
public:
	CInodeManager(void);
	~CInodeManager(void);
protected:
	typedef std::list<inode*> inode_hash_list;
public:
	void Init(super_block* sb) { m_sb = sb; }
	// 不能直接调用iget_locked；需要创建对象，然后调用internal_iget_locked
	inode* iget_locked(bool thp_support, unsigned long ino) { JCASSERT(0); return NULL; };
	void iget_failed(inode* node);
	void init_inode_mapping(inode* node, address_space* mapping, bool thp_support);
	int insert_inode_locked(inode* node);
	void internal_iget_locked(inode* ptr, bool thp_support, unsigned long ino);
	void new_inode(inode* node);

	inode* ilookup(unsigned long ino);
	inode* find_inode_nowait(unsigned long hashval, int (*match)(struct inode*, unsigned long, void*),
		void* data);

protected:
	inode* find_inode_fast(inode_hash_list& head, unsigned long ino);
	int inode_init_always(bool thp_support, inode* ptr_node);
	static UINT32 hash(/*super_block* sb,*/ unsigned long hashval)
	{	// 在同一个文件系统中，sb是常量
		UINT32 sb = 1;
		unsigned long tmp;
		tmp = (hashval * (unsigned long)sb) ^ (GOLDEN_RATIO_PRIME + hashval) / L1_CACHE_BYTES;
		tmp = tmp ^ ((tmp ^ GOLDEN_RATIO_PRIME) >> i_hash_shift);
		return tmp & i_hash_mask;
	}

protected:
	static const UINT32 i_hash_mask = 255;
	static const UINT32 i_hash_shift = 8;

	super_block* m_sb;
	inode_hash_list m_inode_hash[i_hash_mask + 1];
	CRITICAL_SECTION m_inode_hash_lock;
};
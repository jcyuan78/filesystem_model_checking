///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
#include "pch.h"
#include "../include/inode-manager.h"
#include "../include/writeback.h"

LOCAL_LOGGER_ENABLE(L"linuxfs.inode", LOGGER_LEVEL_DEBUGINFO);

/*
inode* CInodeManager::iget_locked(bool thp_support, unsigned long ino)
{
	//	f2fs_inode_info* ptr_inode = new f2fs_inode_info;
	inode * ptr_node = new inode;
		// initialize inode
	//	memset(node, 0, sizeof(inode));
	inode_init_always(thp_support, static_cast<inode*>(ptr_node));
	ptr_node->i_ino = ino;
	ptr_node->i_state |= I_NEW;
	return ptr_node;
}
*/
CInodeManager::CInodeManager(void)
{
	InitializeCriticalSection(&m_inode_hash_lock);
}

CInodeManager::~CInodeManager(void)
{
	for (int ii = 0; ii < (i_hash_mask + 1); ++ii)
	{
		for (auto it = m_inode_hash[ii].begin(); it != m_inode_hash[ii].end(); ++it)
		{
//			if (*it) delete (*it);
		}
		m_inode_hash->clear();
	}
	DeleteCriticalSection(&m_inode_hash_lock);
}

void CInodeManager::new_inode(inode* iinode)
{
	JCASSERT(iinode);
	// alloc_inode()
	inode_init_always(false, iinode);
	{	
		LOG_TRACK(L"inode_lock", L" inode=%p, waiting for lock, auto", iinode);
		auto_lock<spin_locker> lock(iinode->i_lock);
		LOG_TRACK(L"inode_lock", L" inode=%p, locked, auto", iinode);
		//spin_lock(&iinode->i_lock);
		iinode->i_state = 0;
		//spin_unlock(&iinode->i_lock);
	}
	INIT_LIST_HEAD(&iinode->i_sb_list);
	LOG_TRACK(L"inode", L", add=%p, - add to sb", iinode);
	inode_sb_list_add(iinode);
}

inode* CInodeManager::ilookup(unsigned long ino)
{
//	struct hlist_head* head = inode_hashtable + hash(sb, ino);
	inode_hash_list & head = m_inode_hash[hash(ino)];
	struct inode* inode;
again:
	spin_lock(&m_inode_hash_lock);
	inode = find_inode_fast(head, ino);
	spin_unlock(&m_inode_hash_lock);

	if (inode)
	{
		if (IS_ERR(inode))		return NULL;
		wait_on_inode(inode);
		if (unlikely(inode->inode_unhashed()))
		{
			iput(inode);
			goto again;
		}
	}
	return inode;
}

inode* CInodeManager::find_inode_nowait(unsigned long hashval, int(*match)(inode*, unsigned long, void*), void* data)
{
	//struct hlist_head* head = inode_hashtable + hash(sb, hashval);
	inode_hash_list& head = m_inode_hash[hash(hashval)];
	inode* i_node = NULL, * ret_inode = NULL;
	int mval;

	spin_lock(&m_inode_hash_lock);
//	hlist_for_each_entry(i_node, head, i_hash)
	for (auto it=head.begin(); it!=head.end(); ++it)
	{
//		if (i_node->i_sb != sb)			continue;
		i_node = *it;
		mval = match(i_node, hashval, data);
		if (mval == 0)			continue;
		if (mval == 1)			ret_inode = i_node;
		goto out;
	}
out:
	spin_unlock(&m_inode_hash_lock);
	return ret_inode;
}


void CInodeManager::internal_iget_locked(inode * iinode, bool thp_support, unsigned long ino)
{
	LOG_TRACK(L"inode", L" app=%p, ino=%d, - get locked", iinode, iinode->i_ino);
//	f2fs_inode_info* ptr_inode = new f2fs_inode_info;
//	inode * node = new inode;
	// initialize inode
//	memset(node, 0, sizeof(inode));
	inode_init_always(thp_support, static_cast<inode*>(iinode));
	iinode->i_ino = ino;
	iinode->i_state |= I_NEW;

	INIT_LIST_HEAD(&iinode->i_sb_list);
	LOG_TRACK(L"inode", L", add=%p, - add to sb", iinode);
	inode_sb_list_add(iinode);

//	return ptr_node;
}

void CInodeManager::init_inode_mapping(inode* ptr_node, address_space* , bool thp_support)
{
	//<YUAN> 如果ptr_node的i_mapping不空，则使用它。此时mapping必须为空。
	//	否则将输入的mapping赋值给ptr_node->i_mapping
	JCASSERT(ptr_node && ptr_node->i_mapping);
	address_space * mapping = ptr_node->i_mapping;
//	ptr_node->i_mapping = mapping;

	mapping->host = ptr_node;
	mapping->flags = 0;
	//	if (m_sb->s_type->fs_flags & FS_THP_SUPPORT)	__set_bit(AS_THP_SUPPORT, &mapping->flags);
	if (thp_support) set_bit(AS_THP_SUPPORT, mapping->flags);
	mapping->wb_err = 0;
	atomic_set(&mapping->i_mmap_writable, 0);
#ifdef CONFIG_READ_ONLY_THP_FOR_FS
	atomic_set(&mapping->nr_thps, 0);
#endif
//	mapping_set_gfp_mask(mapping, GFP_HIGHUSER_MOVABLE);
	mapping->private_data = NULL;
	mapping->writeback_index = 0;
}

// 将inode放入hash表中，以inode ino为索引
int CInodeManager::insert_inode_locked(inode* iinode)
{
	super_block* sb = iinode->i_sb;
	unsigned long ino = iinode->i_ino;
//	struct hlist_head* head = inode_hashtable + hash(ino);
	UINT32 hash_val = hash(ino);
	inode_hash_list& hash_head = m_inode_hash[hash_val];
	LOG_DEBUG_(1,L"try to insert inode addr=%p, ino=%d, hash=%d", iinode, ino, hash_val);

	while (1)
	{
		inode* old = NULL;
		{	auto_lock<CriticalSection> section(m_inode_hash_lock);
			//spin_lock(&inode_hash_lock);
			// 源代码用hlist实现。在hlist的循环中，以old作为循环变量，当old为null时，循环结束。
			// 改用iterlater作为循环变量的话，当没有找到而循环结束时，需要将old设为null。
//			hlist_for_each_entry(old, head, i_hash)
			for (auto it=hash_head.begin(); it!=hash_head.end(); ++it)
			{
//				old = *it;
				inode* node = *it;
				if (node->i_ino != ino || node->i_sb != sb)			continue;
//				if (node->i_sb != sb)			continue;
				spin_lock(&node->i_lock);
				if (node->TestState(I_FREEING | I_WILL_FREE) )
				{
					spin_unlock(&node->i_lock);
					continue;
				}
				// 找到对应的inode
				old = *it;
				LOG_DEBUG_(1,L"found the same inode addr=%p, ino=%d", old, old->i_ino);
				break;		
			}
			if (likely(!old))
			{	// hash table中没有找到inode，将node插入hash tab
				spin_lock(&iinode->i_lock);
				iinode->SetState(I_NEW | I_CREATING);
				hash_head.push_back(iinode);
				iinode->m_manager = this;
//				hlist_add_head_rcu(&iinode->i_hash, head);
				spin_unlock(&iinode->i_lock);
//				spin_unlock(&inode_hash_lock);
				LOG_TRACK(L"inode", L" addr=%p, ino=%d, hash=%d - insert to hash", iinode, ino, hash_val);
				return 0;
			}
			if (unlikely(old->TestState(I_CREATING) ))
			{
				spin_unlock(&old->i_lock);
//				spin_unlock(&inode_hash_lock);
				return -EBUSY;
			}
			__iget(old);
			spin_unlock(&old->i_lock);
//			spin_unlock(&inode_hash_lock);
		}
//		wait_on_inode(old);
		if (unlikely(!old->inode_unhashed()))
		{
			iput(old);
			return -EBUSY;
		}
		iput(old);
	}
}

/* iget_failed - Mark an under-construction inode as dead and release it
 * @inode: The inode to discard
 * Mark an under-construction inode as dead and release it.	 */
void CInodeManager::iget_failed(inode* iinode)
{
	iinode->make_bad_inode();
	unlock_new_inode(iinode);
	iput(iinode);
}



inode* CInodeManager::find_inode_fast(inode_hash_list& head, unsigned long ino)
{
	inode* iinode;

repeat:
//	hlist_for_each_entry(inode, iinode, head, i_hash)
	for (auto it = head.begin(); it!=head.end(); ++it)
	{
		iinode = *it;
		if (iinode->i_ino != ino)			continue;
//		if (iinode->i_sb != sb)			continue;		// 对于单一文件系统，省略
		spin_lock(&iinode->i_lock);
		LOG_TRACK(L"inode", L" addr=%p, ino=%d, state=%X - found inode", iinode, iinode->i_ino, iinode->i_state);
		if (iinode->i_state & (I_FREEING | I_WILL_FREE))
		{
			// 由于调用过程中 iinode 会被删除，
			__wait_on_freeing_inode(iinode);
			LOG_DEBUG_(1,L"waiting for inode free completed, total inode=%d, state=%X", head.size(), iinode->i_state);
			goto repeat;
		}
		if (unlikely(iinode->i_state & I_CREATING))
		{
			spin_unlock(&iinode->i_lock);
			return ERR_PTR<inode>(-10070L);
		}
		__iget(iinode);
		spin_unlock(&iinode->i_lock);
		return iinode;
	}
	return NULL;
}

/**inode_init_always - perform inode structure initialisation
 * @m_sb: superblock inode belongs to
 * @inode: inode to initialise
 *
 * These are initializations that need to be done on every inode allocation as the fields are not initialised by slab
 allocation. */
int CInodeManager::inode_init_always(bool thp_support, inode* ptr_node)
{
	static const struct inode_operations empty_iops;
	//static const struct file_operations no_open_fops = { .open = no_open };
//	address_space* mapping = &ptr_node->i_data;
//	ptr_node->i_sb = m_sb;
	ptr_node->i_blkbits = m_sb->s_blocksize_bits;
	ptr_node->i_flags = 0;
	atomic64_set(&ptr_node->i_sequence, 0);
	atomic_set(&ptr_node->i_count, 1);
	//ptr_node->i_op = &empty_iops;
	//ptr_node->i_fop = &no_open_fops;
	ptr_node->i_ino = 0;
	ptr_node->__i_nlink = 1;
	ptr_node->i_opflags = 0;
	if (m_sb->s_xattr) ptr_node->i_opflags |= IOP_XATTR;
	//i_uid_write(ptr_node, 0);
	//i_gid_write(ptr_node, 0);
	atomic_set(&ptr_node->i_writecount, 0);
	ptr_node->i_size = 0;
	ptr_node->i_write_hint = WRITE_LIFE_NOT_SET;
	ptr_node->i_blocks = 0;
	ptr_node->i_bytes = 0;
	ptr_node->i_generation = 0;
	ptr_node->i_pipe = NULL;
	ptr_node->i_cdev = NULL;
	ptr_node->i_link = NULL;
	ptr_node->i_dir_seq = 0;
	ptr_node->i_rdev = 0;
	ptr_node->dirtied_when = 0;

#ifdef CONFIG_CGROUP_WRITEBACK
	ptr_node->i_wb_frn_winner = 0;
	ptr_node->i_wb_frn_avg_time = 0;
	ptr_node->i_wb_frn_history = 0;
#endif

//	if (security_inode_alloc(ptr_node))		THROW_ERROR(ERR_MEM, L"failed on alloc_obj security inode");
	spin_lock_init(&ptr_node->i_lock);
	//lockdep_set_class(&ptr_node->i_lock, &m_sb->s_type->i_lock_key);

	init_rwsem(&ptr_node->i_rwsem);
	//lockdep_set_class(&ptr_node->i_rwsem, &m_sb->s_type->i_mutex_key);

	atomic_set(&ptr_node->i_dio_count, 0);
	
#if 0 //移动到 init_inode_mapping, 用于针对虚类初始化
//	mapping->a_ops = &empty_aops;
	mapping->host = ptr_node;
	mapping->flags = 0;
//	if (m_sb->s_type->fs_flags & FS_THP_SUPPORT)	__set_bit(AS_THP_SUPPORT, &mapping->flags);
	if (thp_support) set_bit(AS_THP_SUPPORT, &mapping->flags);
	mapping->wb_err = 0;
	atomic_set(&mapping->i_mmap_writable, 0);
#ifdef CONFIG_READ_ONLY_THP_FOR_FS
	atomic_set(&mapping->nr_thps, 0);
#endif
//	mapping_set_gfp_mask(mapping, GFP_HIGHUSER_MOVABLE);
	mapping->private_data = NULL;
	mapping->writeback_index = 0;
	ptr_node->i_private = NULL;
	ptr_node->i_mapping = mapping;
#endif
	INIT_HLIST_HEAD(&ptr_node->i_dentry);	/* buggered by rcu freeing */
#ifdef CONFIG_FS_POSIX_ACL
	ptr_node->i_acl = ptr_node->i_default_acl = ACL_NOT_CACHED;
#endif

#ifdef CONFIG_FSNOTIFY
	ptr_node->i_fsnotify_mask = 0;
#endif
	ptr_node->i_flctx = NULL;
//	this_cpu_inc(nr_inodes);

	return 0;
//out:
//	return -ENOMEM;
}

void iput_final(struct inode* inode);


void iput(inode* iinode)
{
	if (!iinode)	return;
	JCASSERT(iinode->i_count > 0);
	BUG_ON(iinode->TestState(I_CLEAR));
retry:
//	if (atomic_dec_and_lock(&iinode->i_count, &iinode->i_lock))
	if (iinode->atomic_decount_and_lock())
	{
		if (iinode->i_nlink && iinode->TestState(I_DIRTY_TIME))
		{
			atomic_inc(&iinode->i_count);
			//spin_unlock(&iinode->i_lock);
			iinode->unlock();
//			trace_writeback_lazytime_iput(iinode);
			iinode->mark_inode_dirty_sync();
			goto retry;
		}
		iput_final(iinode);
//		delete iinode;
	}
//<YUAN> node->i_mapping由析构函数负责删除
//	delete node->i_mapping;	
//	delete node;
}



/** iget_locked - obtain an inode from a mounted file system
 * @sb:		super block of file system
 * @ino:	inode number to get
 *
 * Search for the inode specified by @ino in the inode cache and if present return it with an increased reference count. This is for file systems where the inode number is sufficient for unique identification of an inode.
 *
 * If the inode is not in cache, alloc_obj a new inode and return it locked, hashed, and with the I_NEW flag set.  The file system gets to fill it in before unlocking it via unlock_new_inode(). */
//struct inode* iget_locked(struct super_block* sb, unsigned long ino)
inode* CInodeManager::_iget_locked(bool thp_support, UINT ino)
{
	inode_hash_list & head = m_inode_hash[hash(ino)];
	inode* iinode = nullptr;
again:
	spin_lock(&m_inode_hash_lock);
	iinode = find_inode_fast(head, ino);
	spin_unlock(&m_inode_hash_lock);
	if (iinode) 
	{
		TRACK_INODE(iinode, L"found from hash");
		if (IS_ERR(iinode))		return NULL;
		wait_on_inode(iinode);
//		if (unlikely(inode_unhashed(iinode))) 
		if (unlikely(iinode->inode_unhashed()) )
		{
			iput(iinode);
			goto again;
		}
		return iinode;
	}

	// (*) 由于重新unlock以后，其他线程会先于当前线程创建创建inode，需要再检查一下，是否有有相同的ino存在。
	// <TODO>需要考虑如何处理这个问题。
#if 0
	iinode = alloc_inode(sb);
	if (iinode) 
	{
		struct iinode* old;

		spin_lock(&m_inode_hash_lock);
		/* We released the lock, so.. */
		old = find_inode_fast(sb, head, ino);
		if (!old) 
		{
			iinode->i_ino = ino;
			spin_lock(&iinode->i_lock);
			iinode->i_state = I_NEW;
			hlist_add_head_rcu(&iinode->i_hash, head);
			spin_unlock(&iinode->i_lock);
			inode_sb_list_add(iinode);
			spin_unlock(&m_inode_hash_lock);

			/* Return the locked inode with I_NEW set, the caller is responsible for filling in the contents */
			return iinode;
		}

		/* Uhhuh, somebody else created the same inode under us. Use the old inode instead of the one we just allocated.	 */
		spin_unlock(&m_inode_hash_lock);
		destroy_inode(iinode);
		if (IS_ERR(old))			return NULL;
		iinode = old;
		wait_on_inode(iinode);
		if (unlikely(iinode->inode_unhashed()))
		{
			iput(iinode);
			goto again;
		}
	}
#endif
	return iinode;
}

#ifdef _TRACK_INODE_LOCK_

void inode::lock(void)
{
	//LOG_STACK_TRACE();
	spin_lock(&i_lock); 
}
void inode::unlock(void) 
{
	spin_unlock(&i_lock); 
	LOG_TRACK(L"inode_lock", L" inode=%p, unlocked", this);
}

bool inode::trylock(void)
{
	LOG_TRACK(L"inode_lock", L" inode=%p, try to lock", this);
	bool br = spin_trylock(&i_lock);
	LOG_TRACK(L"inode_lock", L" inode=%p, res=%d, try to lock", this, br);
	return br;
}

int inode::atomic_decount_and_lock(void)
{
	int c = i_count;
	do
	{
		if (unlikely(c == 1))			break;
	} while (!InterlockedCompareExchange(&i_count, c - 1, c));
	if (c != 1) return 0;

	LOG_TRACK(L"inode_lock", L" inode=%p, count=%d, waiting for lock and decount", this, i_count);
	spin_lock(&i_lock);
	LOG_TRACK(L"inode_lock", L" inode=%p, count=%d, locked", this, i_count);
	//	if (c) return c;
	if (InterlockedDecrement(&i_count) == 0) return 1;
	spin_unlock(&i_lock);
	LOG_TRACK(L"inode_lock", L" inode=%p, count=%d, unlocked", this, i_count);
	return 0;
}

#endif
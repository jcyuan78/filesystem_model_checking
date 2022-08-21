///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "../include/f2fs-inode.h"

class f2fs_file_inode : public f2fs_inode_info
{
public:
	virtual int setattr(user_namespace*, dentry*, iattr*);
	virtual int getattr(user_namespace*, const path*, kstat*, u32, unsigned int);
	//	virtual posix_acl* get_acl(/*struct inode*,*/ int, bool);
	//	virtual int set_acl(user_namespace*, /*struct inode*,*/ posix_acl*, int);
	virtual ssize_t listxattr(dentry*, char*, size_t);
	virtual int fiemap(/*struct inode*, */fiemap_extent_info*, u64 start, u64 len);
	virtual int fileattr_set(user_namespace* mnt_userns, dentry* dentry, fileattr* fa);
	virtual int fileattr_get(dentry* dentry, fileattr* fa);


	//virtual dentry* lookup(dentry*, unsigned int);
	//virtual const char* get_link(dentry*, /*struct inode*,*/ delayed_call*);
	//virtual int permission(user_namespace*, /*struct inode*,*/ int);
	//virtual int readlink(dentry*, char __user*, int);
	//virtual int create(user_namespace*, /*struct inode*,*/ dentry*, umode_t, bool);
	//virtual int link(dentry*, /*struct inode*,*/ dentry*);
	//virtual int unlink(/*struct inode*, */ dentry*);
	//virtual int symlink(user_namespace*, /*struct inode*, */dentry*, const char*);
	//virtual int mkdir(user_namespace*, /*struct inode*, */dentry*, umode_t);
	//virtual int rmdir(/*struct inode*, */dentry*);
	//virtual int mknod(user_namespace*, /*struct inode*, */dentry*, umode_t, dev_t);
	//virtual int rename(user_namespace*, /*struct inode*, */dentry*, inode*, dentry*, unsigned int);
	//virtual int update_time(/*struct inode*, */timespec64*, int);
	//virtual int atomic_open(/*struct inode*, */dentry*, file*, unsigned open_flag, umode_t create_mode);
	//virtual int tmpfile(user_namespace*, /*struct inode*,*/ dentry*, umode_t);

};
#pragma once

#include <dokanfs-lib.h>
#include "error.h"
#include "yaffs_define.h"
#include <list>


// help functions

int yaffs_hweight8(BYTE x);
int yaffs_hweight32(DWORD x);
UINT16 yaffs_calc_name_sum(const YCHAR * name);
YCHAR * yaffs_clone_str(const YCHAR *str);


#define YAFFS_OBJECT_SPACE		0x40000
#define YAFFS_MAX_OBJECT_ID		(YAFFS_OBJECT_SPACE - 1)



/* Structure for doing xattr modifications */
struct yaffs_xattr_mod {
	int set;		/* If 0 then this is a deletion */
	const YCHAR *name;
	const void *data;
	int size;
	int flags;
	int result;
};


/* -------------------------- Object structure -------------------------------*/
/* This is the object structure as stored on NAND */

struct yaffs_obj_hdr 
{
	u32 type;  /* enum yaffs_obj_type  */

	/* Apply to everything  */
	u32 parent_obj_id;
	u16 sum_no_longer_used;	/* checksum of name. No longer used */
	YCHAR name[YAFFS_MAX_NAME_LENGTH + 1];

	/* The following apply to all object types except for hard links */
	u32 yst_mode;		/* protection */

	u32 yst_uid;
	u32 yst_gid;
	u32 yst_atime;
	u32 yst_mtime;
	u32 yst_ctime;

	/* File size  applies to files only */
	u32 file_size_low;

	/* Equivalent object id applies to hard links only. */
	int equiv_id;

	/* Alias is for symlinks only. */
	YCHAR alias[YAFFS_MAX_ALIAS_LENGTH + 1];

	//u32 yst_rdev;	/* stuff for block and char devices (major/min) */

	FILETIME win_ctime;
	FILETIME win_atime;
	FILETIME win_mtime;

	u32 inband_shadowed_obj_id;
	u32 inband_is_shrink;

	u32 file_size_high;
	u32 reserved[1];

	int shadows_obj;	/* This object header shadows the	specified object if > 0 */
	u32 is_shrink;		/* is_shrink applies to object headers written when wemake a hole. */
};



#define YAFFS_OBJECT_TYPE_MAX YAFFS_OBJECT_TYPE_SPECIAL




struct dirent 
{
	long d_ino;					/* inode number */
	loff_t d_off;				/* offset to this dirent */
	unsigned short d_reclen;	/* length of this dirent */
	YUCHAR d_type;				/* type of this record */
	YCHAR d_name[NAME_MAX + 1];	/* file name (null-terminated) */
	unsigned d_dont_use;		/* debug: not for public consumption */
};

/*
 * Handle management.
 * There are open inodes in struct yaffsfs_Inode.
 * There are open file descriptors in FileDes.
 * There are open handles in FileDes.
 *
 * Things are structured this way to be like the Linux VFS model
 * so that interactions with the yaffs guts calls are similar.
 * That means more common code paths and less special code.
 * That means better testing etc.
 *
 * We have 3 layers because:
 * A handle is different than an fd because you can use dup()
 * to create a new handle that accesses the *same* fd. The two
 * handles will use the same offset (part of the fd). We only close
 * down the fd when there are no more handles accessing it.
 *
 * More than one fd can currently access one file, but each fd
 * has its own permsiions and offset.
 */

struct Inode {
	int count;		/* Number of handles accessing this inode */
	struct yaffs_obj *iObj;
};



struct DirSearchContext {
	struct dirent de;				/* directory entry */
	YCHAR name[NAME_MAX + 1];		/* name of directory being searched */
	struct yaffs_obj *dirObj;		/* ptr to directory being searched */
	struct yaffs_obj *nextReturn;	/* obj  returned by next readddir */
	//struct list_head others;
	s32 offset : 20;
	u8 inUse : 1;
};

struct FileDes {
	u8 isDir : 1; 		/* This s a directory */
	u8 reading : 1;
	u8 writing : 1;
	u8 append : 1;
	u8 shareRead : 1;
	u8 shareWrite : 1;
	s32 inodeId : 12;		/* Index to corresponding Inode */
	s32 handleCount : 10;	/* Number of handles for this fd */
	union 
	{
		loff_t position;	/* current position in file */
	} v;
};

struct Handle 
{
	short int fdId;
	short int useCount;
};

/*
 * Verification code
 */


//  Simple hash function. Needs to have a reasonable spread
inline int yaffs_hash_fn(int n)
{
	if (n < 0)	n = -n;
	return n % YAFFS_NOBJECT_BUCKETS;
}

class CYafFs;
class CYaffsDir;

class CYaffsObject : public IFileInfo
{
public:
	friend class CYaffsDir;

public:
	CYaffsObject(void);
	virtual ~CYaffsObject(void);

public:
	virtual void Cleanup(void) {};
	virtual void CloseFile(void) {};
	virtual bool DokanReadFile(LPVOID buf, DWORD len, DWORD & read, LONGLONG offset) { return false; }
	virtual bool DokanWriteFile(const void * buf, DWORD len, DWORD & written, LONGLONG offset) { return false; }

	virtual bool LockFile(LONGLONG offset, LONGLONG len) { return false; };
	virtual bool UnlockFile(LONGLONG offset, LONGLONG len) { return false; };

	virtual bool GetFileInformation(LPBY_HANDLE_FILE_INFORMATION fileinfo) const;
	virtual std::wstring GetFileName(void) const { return m_obj.short_name; }

	virtual bool DokanGetFileSecurity(SECURITY_INFORMATION psinfo, PSECURITY_DESCRIPTOR psdesc, ULONG & buf_size) { return false; };
	virtual bool DokanSetFileSecurity(PSECURITY_INFORMATION psinfo, PSECURITY_DESCRIPTOR psdesc, ULONG buf_size) { return false; };

	virtual bool SetAllocationSize(LONGLONG size) { return false; }
	virtual bool SetEndOfFile(LONGLONG) { return false; }
	virtual void DokanSetFileAttributes(DWORD attr) { JCASSERT(0); };

	virtual void SetFileTime(const FILETIME * ct, const FILETIME * at, const FILETIME* mt) {};
	virtual bool FlushFile(void) { return false; };

	virtual void GetParent(IFileInfo * & parent) 
	{
		parent = static_cast<IFileInfo*>(m_parent);
		if (parent) parent->AddRef();
	};

	// 删除所有给文件分配的空间。如果是目录，删除目录下的所有文件。
	virtual void ClearData(void) {};
	virtual void GetParent(CYaffsObject* &parent) 
	{ 
		parent = m_parent;
		if (parent) parent->AddRef();
	}

	// for dir only
	virtual bool IsDirectory(void) const;
	virtual bool EnumerateFiles(EnumFileListener * listener) const { return false; }
	virtual bool OpenChild(IFileInfo * &file, const wchar_t * fn) const {
		JCASSERT(0); return false;
	}
	virtual bool CreateChild(IFileInfo * &file, const wchar_t * fn, bool dir) { return false; }

public:
////<MIGRATE> yaffs_guts.c : void yaffs_add_obj_to_dir()
//	virtual void AddObjToDir(CYaffsObject * obj) {JCASSERT(0)};

//<MIGRATE> yaffs_verify_obj_in_dir()
	void VerifyObjectInDir() {}
//<MIGRATE> yaffs_verify_oh()
	void VerifyObjHdr(yaffs_obj_hdr * oh, yaffs_ext_tags * tags, bool parent_check) {};

public:
	// for dir only. 检查文件夹是否为空
	virtual bool IsEmpty(void) { JCASSERT(0); return false; };

	virtual void FreeTonode(void) {};

	void RemoveObjFromDir(void);
	//static void AllocEmptyObject(CYaffsObject *& file, CYafFs * fs);
//<MIGRATE> yaffs_guts.c :  *yaffs_new_obj()
	static void NewObject(CYaffsObject *& file, CYafFs * fs, int number, yaffs_obj_type type);

//<MIGRATE> yaffs_guts.c : static void yaffs_check_obj_details_loaded(struct yaffs_obj *in)
	void CheckObjDetailsLoaded(void);
//<MIGRATE> yaffs_guts.c : yaffs_del_obj
	virtual bool DelObj(void);
//<MIGRATE> yaffs_guts.c : static int yaffs_change_obj_name()
	bool ChangeObjName(CYaffsObject * new_dir, const YCHAR * new_name, bool force, bool shadows);
//<MIGRATE> yaffs_guts.c void yaffs_get_obj_name(struct yaffs_obj *obj, const YCHAR * name)
	size_t GetObjName(YCHAR * name, int buffer_size);
	UINT16 GetNameSum(void) const { return m_obj.sum; }
	bool IsValid(void) const { return (m_fs != NULL) && m_data_valid; }
//<MIGRATE> yaffs_attribs_init
	// dummy for windows
	void AttribusInit(UINT32 gid, UINT32 uid, UINT32 rdev) {}
//<MIGRATE> yaffs_update_parent()
	void UpdateParent(void);

	virtual bool SetObjectByHeaderTag(CYafFs * fs, UINT32 chunk, yaffs_obj_hdr * oh, yaffs_ext_tags & tags);

protected:
	static CYaffsObject * AllocRawObject(CYafFs * fs);
	//void HashObj(void);
//<MIGRATE> yaffs_guts.c : void yaffs_load_current_time()
	void LoadCurrentTime(bool do_a, bool do_c);
//<MIGRATE> yaffs_attribs.c : void yaffs_load_attribs()
	void LoadAttribs(yaffs_obj_hdr * oh);
//<MIGRATE> yaffs_load_attributes_oh
	void LoadAttribsObjectHeader(yaffs_obj_hdr * oh);

	/*
	 *  A note or two on object names.
	 *  * If the object name is missing, we then make one up in the form objnnn
	 *
	 *  * ASCII names are stored in the object header's name field from byte zero
	 *  * Unicode names are historically stored starting from byte zero.
	 *
	 * Then there are automatic Unicode names...
	 * The purpose of these is to save names in a way that can be read as
	 * ASCII or Unicode names as appropriate, thus allowing a Unicode and ASCII
	 * system to share files.
	 *
	 * These automatic unicode are stored slightly differently...
	 *  - If the name can fit in the ASCII character space then they are saved as
	 *    ascii names as per above.
	 *  - If the name needs Unicode then the name is saved in Unicode
	 *    starting at oh->name[1].

	 */
//<MIGRATE> yaffs_guts.c void yaffs_set_obj_name(struct yaffs_obj *obj, const YCHAR * name)
	void SetObjName(const YCHAR * name);
	void LoadNameFromOh(YCHAR * name, const YCHAR * oh_name, int buffer_size);
	void FixFullName(YCHAR * name, int buffer_size);
	//void SetObjNameFromOh(const yaffs_obj_hdr * oh) { SetObjName(oh->name); }
	void UnlinkObj(void) {/*<TODO> tobe implement*/};

//<MIGRATE> yaffs_guts.c : yaffs_update_oh()
	/* UpdateObjectHeader updates the header on NAND for an object.
	 * If name is not NULL, then that new name is used.
	 *
	 * We're always creating the obj header from scratch (except reading
	 * the old name) so first set up in cpu endianness then run it through
	 * endian fixing at the end.
	 *
	 * However, a twist: If there are xattribs we leave them as they were.
	 *
	 * Careful! The buffer holds the whole chunk. Part of the chunk holds the
	 * object header and the rest holds the xattribs, therefore we use a buffer
	 * pointer and an oh pointer to point to the same memory.
	 */

//	int yaffs_update_oh(struct yaffs_obj *in, const YCHAR *name, int force,
//		int is_shrink, int shadows, struct yaffs_xattr_mod *xmod)
	int UpdateObjectHeader(const YCHAR * name, bool force, bool is_shrink, bool shadows, yaffs_xattr_mod *xmod);

protected:
//<MIGRATE> yaffs_verify_oh()
	void VerifyObjectHeader(yaffs_obj_hdr * oh, yaffs_ext_tags * tags, int) {};
	virtual void LoadObjectHeader(yaffs_obj_hdr * oh) {};
//<MIGRATE> yaffs_apply_xattrib_mod
	int ApplyXattribMod(BYTE * buf, yaffs_xattr_mod * xmod) { return 0; }

public:
	virtual void LoadObjectFromCheckpt(yaffs_checkpt_obj * cp);
	//	static void yaffs2_obj_checkpt_obj()
	virtual void ObjToCheckptObj(struct yaffs_checkpt_obj *cp);
	virtual int RefreshChunk(int old_chunk, yaffs_ext_tags & tags, BYTE * buffer);

	template <typename T>
	static T CheckptObjBitGet(yaffs_checkpt_obj * cp, int bit_offset, int bit_width)
	{
		UINT32 and_mask = ((1 << bit_width) - 1);
		return (T)((cp->bit_field >> bit_offset) & and_mask);
	}

	template <typename T>
	static void CheckptObjBitAssign(yaffs_checkpt_obj * cp, int bit_offset, int bit_width, const T & value)
	{
		u32 and_mask;
		and_mask = ((1 << bit_width) - 1) << bit_offset;
		cp->bit_field &= ~and_mask;
		cp->bit_field |= (((UINT32)value << bit_offset) & and_mask);
	}

	
// properties
public:
	inline UINT32 GetObjectId(void) const { return m_obj.obj_id; }
	inline bool IsDeferedFree(void)const { return m_obj.defered_free != 0; }
	inline yaffs_obj_type GetType(void) const { return m_type; }
	//inline bool IsSoftDeleted(void) const { return m_obj.soft_del && m_obj.deleted; }

protected:
	CYaffsObject * m_parent;
	yaffs_obj_type m_type;
	yaffs_obj m_obj;
	CYafFs * m_fs;
	bool m_data_valid;
};




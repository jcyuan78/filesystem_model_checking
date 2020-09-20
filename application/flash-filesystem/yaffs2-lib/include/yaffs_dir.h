#pragma once

#include "yaffs_obj.h"

#define MAX_CHILDREN  32
#define CHILD_HASH    17

class CYaffsDir : public CYaffsObject
{
public:
	CYaffsDir(void);
	virtual ~CYaffsDir(void);

	enum DIR_TYPE {
		NORMAL_DIR, UNLINK_DIR, DEL_DIR, ROOT_DIR, LOSTNFOUND_DIR,
	};
	static void CreateFakeDir(CYaffsDir * & dir, CYafFs * fs, int number, UINT32 mode, const wchar_t * name);

public:
	//<MIGRATE> yaffs_guts.c/yaffs_del_dir_contents()
	void DeleteDirContents(void);
	void DeleteChildren(void);
	bool IsDirOf(DIR_TYPE type) const { return m_dir_type == type; }
	bool FindByName(CYaffsObject* & obj, const YCHAR * new_name) const;

protected:
	//<MIGRATE> yaffs_guts.c : yaffs_create_obj
	bool CreateObject(CYaffsObject* & out_obj, yaffs_obj_type type, const YCHAR * name, UINT32 mode, UINT32 uid, UINT32 gid, CYaffsObject * equiv_obj, const YCHAR * alias_str, UINT32 rdev);

public:
	virtual void FreeTnode(void) { JCASSERT(0); }
	virtual void AddObjToDir(CYaffsObject * obj);
	void RemoveObject(CYaffsObject * obj);
	virtual bool SetObjectByHeaderTag(CYafFs * fs, UINT32 chunk, yaffs_obj_hdr * oh, yaffs_ext_tags & tags);

protected:
	virtual bool DelObj(void);
	virtual bool OpenChild(IFileInfo* &file, const wchar_t *name) const;
	virtual bool CreateChild(IFileInfo * &file, const wchar_t * fn, bool dir);
	virtual void LoadObjectHeader(yaffs_obj_hdr * oh) {};
	virtual bool IsEmpty(void) { return m_child_num == 0; };
	virtual bool GetFileInformation(LPBY_HANDLE_FILE_INFORMATION fileinfo) const;

	//	virtual void LoadObjectFromCheckpt(yaffs_checkpt_obj * cp) {};

public:
	//<MIGRATE> yaffs_verify_dir()
	void VerifyDir() {};

protected:
	DIR_TYPE m_dir_type;
	//std::list<CYaffsObject*> m_children;
	CYaffsObject * m_children[MAX_CHILDREN];
	size_t m_child_num;
	std::list<CYaffsObject*> m_dirty;
};

#pragma once

#include <dokanfs-lib.h>

class CYaffsDirect;
struct yaffs_obj;

class CYaffsObject : public IFileInfo
{
public:
	friend class CYaffsDir;

public:
	CYaffsObject(void) {};
	virtual ~CYaffsObject(void) {};
	//	void GeneralObjDel(void);

public:
	virtual void Cleanup(void) {};
	virtual void CloseFile(void) {};
	virtual bool DokanReadFile(LPVOID buf, DWORD len, DWORD & read, LONGLONG offset);
	virtual bool DokanWriteFile(const void * buf, DWORD len, DWORD & written, LONGLONG offset);

	virtual bool LockFile(LONGLONG offset, LONGLONG len) { return false; };
	virtual bool UnlockFile(LONGLONG offset, LONGLONG len) { return false; };

	virtual bool GetFileInformation(LPBY_HANDLE_FILE_INFORMATION fileinfo) const;
	virtual std::wstring GetFileName(void) const { return L""; }

	virtual bool DokanGetFileSecurity(SECURITY_INFORMATION psinfo, PSECURITY_DESCRIPTOR psdesc, ULONG & buf_size) { return false; };
	virtual bool DokanSetFileSecurity(PSECURITY_INFORMATION psinfo, PSECURITY_DESCRIPTOR psdesc, ULONG buf_size) { return false; };

	virtual bool SetAllocationSize(LONGLONG size);
	virtual bool SetEndOfFile(LONGLONG size);
	virtual void DokanSetFileAttributes(DWORD attr) { JCASSERT(0); };

	virtual void SetFileTime(const FILETIME * ct, const FILETIME * at, const FILETIME* mt) {};
	virtual bool FlushFile(void) { return false; };

	virtual void GetParent(IFileInfo * & parent);

	// 删除所有给文件分配的空间。如果是目录，删除目录下的所有文件。
	virtual void ClearData(void) {};
	virtual void GetParent(CYaffsObject* &parent);

	// for dir only
	virtual bool IsDirectory(void) const;
	virtual bool EnumerateFiles(EnumFileListener * listener) const { return false; }
	virtual bool OpenChild(IFileInfo * &file, const wchar_t * fn) const {
		JCASSERT(0); return false;
	}
	virtual bool CreateChild(IFileInfo * &file, const wchar_t * fn, bool dir) { return false; }

public:
	//void Initialize(CYaffsDirect * fs, bool dir, int handle)
	//{
	//	m_fs = fs; m_isdir = dir; m_handle = handle;
	//}
	void Initialize(CYaffsDirect * fs, yaffs_obj * obj);
//protected:
//	bool m_isdir;
	//int m_handle;
public:
	CYaffsDirect *m_fs;
	yaffs_obj * m_obj;
};


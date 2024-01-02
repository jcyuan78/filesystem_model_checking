///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// ���fs�ṩһ��������ļ�ϵͳ������ͳ��trace���ļ��Ķ�д����
#pragma once
#include <dokanfs-lib.h>

#include <map>
#include <vector>


class CStatisticFileInfo : public IFileInfo
{
public:
	CStatisticFileInfo(void);
	virtual ~CStatisticFileInfo(void);

public:
	virtual void Cleanup(void) {}
	virtual void CloseFile(void);
	virtual bool DokanReadFile(LPVOID buf, DWORD len, DWORD& read, LONGLONG offset);
	virtual bool DokanWriteFile(const void* buf, DWORD len, DWORD& written, LONGLONG offset);

	virtual bool LockFile(LONGLONG offset, LONGLONG len) { EnterCriticalSection(&m_file_lock); return true; }
	virtual bool UnlockFile(LONGLONG offset, LONGLONG len) { LeaveCriticalSection(&m_file_lock); return true; }
	virtual bool EnumerateFiles(EnumFileListener* listener) const { return true; }

	virtual bool GetFileInformation(LPBY_HANDLE_FILE_INFORMATION fileinfo) const { return false; }
	virtual std::wstring GetFileName(void) const { return m_fn; }

	virtual bool DokanGetFileSecurity(SECURITY_INFORMATION psinfo, PSECURITY_DESCRIPTOR psdesc, ULONG& buf_size) { return true; }
	virtual bool DokanSetFileSecurity(PSECURITY_INFORMATION psinfo, PSECURITY_DESCRIPTOR psdesc, ULONG buf_size) { return true; }
	// for dir only
	virtual bool IsDirectory(void) const { return false; }
	virtual bool IsEmpty(void) const { return true; }			// ����Ŀ¼������Ŀ¼�Ƿ�Ϊ�գ����ڷ�Ŀ¼������true.
	virtual bool SetAllocationSize(LONGLONG size) { return SetEndOfFile(size); }
	virtual bool SetEndOfFile(LONGLONG);
	virtual void DokanSetFileAttributes(DWORD attr) {}

	virtual void SetFileTime(const FILETIME* ct, const FILETIME* at, const FILETIME* mt) {}
	virtual bool FlushFile(void);

	virtual void GetParent(IFileInfo*& parent) {}

	// ɾ�����и��ļ�����Ŀռ䡣�����Ŀ¼��ɾ��Ŀ¼�µ������ļ���
	virtual void ClearData(void) {}

	virtual bool OpenChild(IFileInfo*& file, const wchar_t* fn, UINT32 mode) const { return false; }
	// �Ӵ˽ڵ㿪ʼ���Ҳ��򿪽ڵ㣬·�����԰����ӽڵ㡣
	virtual bool OpenChildEx(IFileInfo*& file, const wchar_t* fn, size_t len) { return false; }
	virtual bool CreateChild(IFileInfo*& file, const wchar_t* fn, bool dir, UINT32 mode) { return false; }

	// ��Close�ļ�ʱ��ɾ�����ļ����ж�������DokanAppʵ�֡���ЩӦ�ã�Explorer����ͨ���������ɾ���ļ���
	virtual void SetDeleteOnClose(bool del) {}
	friend class CStatisticFileSystem;
protected:
	std::wstring m_fn;
	size_t m_size;		// �ֽ�Ϊ��λ�Ĵ�С
	size_t m_blk_nr;	// �������
	std::vector<UINT> m_blk_access;
	CRITICAL_SECTION m_file_lock;
	UINT m_fid;
	bool m_dir;
};


class CStatisticFileSystem : public IFileSystem
{
public:
	CStatisticFileSystem(void);
	virtual ~CStatisticFileSystem(void);
public:
	virtual bool CreateObject(IJCInterface*& fs) { return false; };
	virtual ULONG GetFileSystemOption(void) const { return false; };
	virtual bool Mount(IVirtualDisk* dev);
	virtual void Unmount(void);
	virtual bool MakeFileSystem(IVirtualDisk* dev, size_t volume_size, const std::wstring& volume_name, const std::wstring& options = L"") { JCASSERT(0);  return false; }
	// fsck������ļ�ϵͳ�����ؼ����
	virtual FsCheckResult FileSystemCheck(IVirtualDisk* dev, bool repair) { return IFileSystem::CheckError; }

	virtual bool DokanGetDiskSpace(ULONGLONG& free_bytes, ULONGLONG& total_bytes, ULONGLONG& total_free_bytes) { return false; }
	virtual bool GetVolumnInfo(std::wstring& vol_name, DWORD& sn, DWORD& max_fn_len, DWORD& fs_flag,
		std::wstring& fs_name) {
		return false;
	}

	// file attribute (attr) and create disposition (disp) is in user mode 
	virtual NTSTATUS DokanCreateFile(IFileInfo*& file, const std::wstring& fn, ACCESS_MASK access_mask,
		DWORD attr, FsCreateDisposition disp, ULONG share, ULONG opt, bool isdir);
	virtual bool MakeDir(const std::wstring& dir);

	virtual NTSTATUS DokanDeleteFile(const std::wstring& fn, IFileInfo* file, bool isdir);
	virtual void FindStreams(void) {}
	virtual NTSTATUS DokanMoveFile(const std::wstring& src_fn, const std::wstring& dst_fn, bool replace, IFileInfo* file) { JCASSERT(0);  return STATUS_UNSUPPORTED_COMPRESSION; }

	virtual bool HardLink(const std::wstring& src, const std::wstring& dst) { JCASSERT(0); return false; }
	virtual bool Unlink(const std::wstring& fn) { JCASSERT(0); return false; }
	virtual bool Sync(void) { JCASSERT(0); return false; }

	virtual bool ConfigFs(const boost::property_tree::wptree& pt) { return true; }
	// ����debug���ͣ�һ����fs��config �ļ��ж�ȡ��
	virtual int GetDebugMode(void) const { return 0; }

protected:
	CRITICAL_SECTION m_fs_lock;
	std::map<std::wstring, CStatisticFileInfo*> m_files;
	UINT m_last_fid;
};


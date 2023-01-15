///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
#include "stdafx.h"
#include "../include/dokan_callback.h"

LOCAL_LOGGER_ENABLE(_T("dokan"), LOGGER_LEVEL_NOTICE);

inline IFileSystem * AchieveFileSystem(PDOKAN_FILE_INFO info)
{
	JCASSERT(info);
	IFileSystem *fs = reinterpret_cast<IFileSystem*>(info->DokanOptions->GlobalContext);		JCASSERT(fs);
	fs->AddRef();
	return fs;
}

inline IFileInfo* AchieveFileInfo(PDOKAN_FILE_INFO info)
{
	JCASSERT(info);
	IFileInfo * file = reinterpret_cast<IFileInfo*>(info->Context);
	if (file) file->AddRef();
	return file;
}

///////////////////////////////////////////////////////////////////////////////
// --
CDokanFindFileCallback::CDokanFindFileCallback(PFillFindData fill_data, PDOKAN_FILE_INFO info)
	: m_fill_data(fill_data), m_file_info(info)
{
	JCASSERT(m_fill_data); JCASSERT(m_file_info);
	m_fs = AchieveFileSystem(m_file_info);
}

CDokanFindFileCallback::~CDokanFindFileCallback(void)
{
	if (m_fs) m_fs->Release();
}

bool CDokanFindFileCallback:: EnumFileCallback(const std::wstring & fn, 
	UINT32 ino, UINT32 entry, // entry 在父目录中的位置
	BY_HANDLE_FILE_INFORMATION * finfo)
{
	size_t fn_len = fn.size();
	//return EnumFileCallback(fn.c_str(), fn_len, ino, entry, finfo);
	JCASSERT(m_fill_data);	JCASSERT(m_fs);
	JCASSERT(finfo);

	WIN32_FIND_DATA find_file;
	memset(&find_file, 0, sizeof(find_file));

	find_file.dwFileAttributes = finfo->dwFileAttributes;
	memcpy_s(&find_file.ftCreationTime, sizeof(FILETIME), &finfo->ftCreationTime, sizeof(FILETIME));
	memcpy_s(&find_file.ftLastAccessTime, sizeof(FILETIME), &finfo->ftLastAccessTime, sizeof(FILETIME));
	memcpy_s(&find_file.ftLastWriteTime, sizeof(FILETIME), &finfo->ftLastWriteTime, sizeof(FILETIME));

	find_file.nFileSizeHigh = finfo->nFileSizeHigh;
	find_file.nFileSizeLow = finfo->nFileSizeLow;
	// copy file name
	fn_len = min(MAX_PATH - 1, fn_len);
	size_t ii = 0;
	const wchar_t * str_fn = fn.c_str();
	for (ii = 0; (ii<fn_len); ++ii)	 find_file.cFileName[ii] = str_fn[ii];
	m_fill_data(&find_file, m_file_info);

	LOG_DEBUG_(1, L"found file: %s", find_file.cFileName);
	return true;
}

/*
bool CDokanFindFileCallback::EnumFileCallback(const wchar_t * fn, size_t fn_len, 
	UINT32 ino, UINT32 entry, // entry 在父目录中的位置
	BY_HANDLE_FILE_INFORMATION * finfo)
{
	JCASSERT(m_fill_data);	JCASSERT(m_fs);
	JCASSERT(finfo);

	WIN32_FIND_DATA find_file;
	memset(&find_file, 0, sizeof(find_file));

	find_file.dwFileAttributes = finfo->dwFileAttributes;
	memcpy_s(&find_file.ftCreationTime, sizeof(FILETIME), &finfo->ftCreationTime, sizeof(FILETIME));
	memcpy_s(&find_file.ftLastAccessTime, sizeof(FILETIME), &finfo->ftLastAccessTime, sizeof(FILETIME));
	memcpy_s(&find_file.ftLastWriteTime, sizeof(FILETIME), &finfo->ftLastWriteTime, sizeof(FILETIME));

	find_file.nFileSizeHigh = finfo->nFileSizeHigh;
	find_file.nFileSizeLow = finfo->nFileSizeLow;
	// copy file name
	fn_len = min(MAX_PATH - 1, fn_len);
	size_t ii = 0;
	for (ii = 0; (ii<fn_len); ++ii)	 find_file.cFileName[ii] = fn[ii];
	m_fill_data(&find_file, m_file_info);

	LOG_DEBUG_(1, L"found file: %s", find_file.cFileName);

	return true;	// continue enumerating
}
*/

const wchar_t* DispToString(IFileSystem::FsCreateDisposition disp)
{
	switch (disp)
	{
	case IFileSystem::FS_CREATE_NEW: return L"CREATE_NEW";
	case IFileSystem::FS_OPEN_ALWAYS: return L"OPEN_ALWAYS";
	case IFileSystem::FS_OPEN_EXISTING: return L"OPEN_EXISTING";
	case IFileSystem::FS_CREATE_ALWAYS: return L"CREATE_ALWAYS";
	case IFileSystem::FS_TRUNCATE_EXISTING: return L"TRUNCATE_EXISTING";
	default: return L"Unknown";
	}
}

///////////////////////////////////////////////////////////////////////////////
// -- wrap of DOKAN_OPERATIONS
NTSTATUS DOKAN_CALLBACK dokancb_CreateFile(LPCWSTR fn,                    // FileName
	PDOKAN_IO_SECURITY_CONTEXT context, // SecurityContext, see https://msdn.microsoft.com/en-us/library/windows/hardware/ff550613(v=vs.85).aspx
	ACCESS_MASK access,                // DesiredAccess
	ULONG attri,                      // FileAttributes
	ULONG share,                      // ShareAccess
	ULONG disp,                      // CreateDisposition
	ULONG option,                      // CreateOptions
	PDOKAN_FILE_INFO info)
{
	LOG_STACK_TRACE();
	jcvos::auto_interface<IFileSystem> fs(AchieveFileSystem(info));
//	IFileSystem * fs = AchieveFileSystem(info);

//	LOG_TRACK(L"dokan", L"Create file: %s, attri=%X, opt=%X, disp=%X, share=%X", fn, attri, option, disp, share);

	DWORD user_attribute;
	DWORD user_disposition;
	ACCESS_MASK out_access=NULL;
	DokanMapKernelToUserCreateFileFlags(access, attri, option, disp, &out_access, &user_attribute, &user_disposition);
//	LOG_TRACK(L"dokan", L"attribute=%X, dispo=%X, access=%X", user_attribute, user_disposition, out_access);

	HANDLE handle = DokanOpenRequestorToken(info);
	IFileSystem::FsCreateDisposition create_disposition = (IFileSystem::FsCreateDisposition)(user_disposition);
	if (handle == INVALID_HANDLE_VALUE) LOG_WIN32_ERROR(L" failed on getting dokan token")
	else
	{
		BOOL br;
		DWORD return_len = 0;
		jcvos::auto_array<BYTE> buf(1024);
		br = GetTokenInformation(handle, TokenUser, buf, 1024, &return_len);
		if (!br) LOG_WIN32_ERROR(_T(" failed on getting token user info"))
		else
		{
			LOG_DEBUG_(1, L"token_user size=%d, returned=%d", sizeof(TOKEN_USER), return_len);
			PTOKEN_USER token_user = (PTOKEN_USER)((BYTE*)buf);
			TCHAR account_name[256];
			TCHAR domain_name[256];
			DWORD account_len = 256;
			DWORD domain_len = 256;
			SID_NAME_USE snu;
			br = LookupAccountSid(NULL, token_user->User.Sid, account_name, &account_len, domain_name, &domain_len, &snu);
			if (!br) LOG_WIN32_ERROR(_T(" failed on getting account info"))
			else LOG_DEBUG_(1, L"account: %s, domain: %s", account_name, domain_name)
		}
		CloseHandle(handle);
	}


//	IFileInfo * file = NULL;
	jcvos::auto_interface<IFileInfo> file;
	bool dir = option & FILE_DIRECTORY_FILE;
	LOG_DEBUG_(1, L"is directory info=%d, option=%d, %s", info->IsDirectory, dir,
		user_attribute & FILE_FLAG_DELETE_ON_CLOSE?L"delete":L"-");
	LOG_TRACK(L"dokan", L"Create %s, disp=%s, fn=%s, attri=%X, opt=%X, share=%X", dir ? L"Dir_" : L"File", DispToString(create_disposition), fn, attri, option, share);
	NTSTATUS st = fs->DokanCreateFile(file, fn, out_access, user_attribute, create_disposition, share, option, dir);

	info->Context = 0;
	if (file != NULL)
	{	
#if 0
		// 检查打开的文件或者目录是否与指定的相符，
		// 有可能是test case的设计问题：尝试调用DeleteFile()取删除一个目录。test case中，先调用Create File打开目录，然后调用Delete Dir删除
		if (!dir && file->IsDirectory())
		{
			file->CloseFile();
			LOG_TRACK(L"dokan", L" Create, opened item is a dir, status=0x%X", STATUS_ACCESS_DENIED);
			return STATUS_ACCESS_DENIED;
		}
#endif
		if (user_attribute & FILE_FLAG_DELETE_ON_CLOSE)
		{
			LOG_DEBUG(L"set delete on close, file=%p", (IFileInfo*)file);
			file->SetDeleteOnClose(true);
		}
		info->IsDirectory = file->IsDirectory();
		info->Context = reinterpret_cast<ULONG64>((IFileInfo*)file);
		file->AddRef();
	}
	else {	LOG_NOTICE(L"[err] failed on creating file %s, err=0x%X", fn, st); }

	LOG_TRACK(L"dokan", L"Create, Status = 0x % X", st);
	return st;
}

void DOKAN_CALLBACK dokancb_Cleanup(LPCWSTR fn, // FileName
	PDOKAN_FILE_INFO info)
{
	LOG_STACK_TRACE();
	LOG_TRACK(L"dokan", L"Cleanup, fn = % s", fn);

	IFileInfo * file = AchieveFileInfo(info);
	if (!file) return;
	file->Cleanup();
	file->Release();
}

void DOKAN_CALLBACK dokancb_CloseFile(LPCWSTR fn, PDOKAN_FILE_INFO info)
{
	LOG_STACK_TRACE();
	IFileInfo * file = AchieveFileInfo(info);
	LOG_TRACK(L"dokan", L" Close File, fn=%s, , file=%p", fn, file);
	if (!file) return;
	file->CloseFile();
	info->Context = NULL;
	file->Release();
	file->Release();	// 对应CreateFile的AddRef
}

NTSTATUS DOKAN_CALLBACK dokancb_ReadFile(LPCWSTR fn,  // FileName
	LPVOID buf,   // Buffer
	DWORD len,    // NumberOfBytesToRead
	LPDWORD read,  // NumberOfBytesRead
	LONGLONG offset, // Offset
	PDOKAN_FILE_INFO info)
{
	LOG_STACK_TRACE();
	LOG_TRACK(L"dokan", L" ReadFile, fn=%s, offset=%d, len=%d", fn, offset, len);
	IFileInfo * file = AchieveFileInfo(info);
	if (!file) { LOG_ERROR(L"[err] file %s is not opened.", fn); return STATUS_ACCESS_DENIED; }
	bool br = file->DokanReadFile(buf, len, *read, offset);
	file->Release();
	//LOG_TRACK(L"dokan", L" ReadFile: %d bytes read", *read);
	return (br) ? (STATUS_SUCCESS) : (STATUS_ACCESS_DENIED);
}

NTSTATUS DOKAN_CALLBACK dokancb_WriteFile
(	LPCWSTR fn,  // FileName
	LPCVOID buf,  // Buffer
	DWORD len,    // NumberOfBytesToWrite
	LPDWORD written,  // NumberOfBytesWritten
	LONGLONG offset, // Offset
	PDOKAN_FILE_INFO info)
{
	LOG_STACK_TRACE();
	LOG_TRACK(L"dokan", L" WriteFile, fn=%s, offset=%d, len=%d", fn, offset, len);
	IFileInfo * file = AchieveFileInfo(info);
	if (!file) { LOG_ERROR(L"[err] file %s is not opened.", fn); return STATUS_ACCESS_DENIED; }
	bool br = file->DokanWriteFile(buf, len, *written, offset);
	file->Release();
	//LOG_TRACK(L"dokan", L" WriteFIle, %d bytes written", *written);
	return (br) ? (STATUS_SUCCESS) : (STATUS_ACCESS_DENIED);
}

NTSTATUS DOKAN_CALLBACK dokancb_FlushFileBuffers
(LPCWSTR, // FileName
	PDOKAN_FILE_INFO)
{
	return STATUS_SUCCESS;
}

NTSTATUS DOKAN_CALLBACK dokancb_GetFileInformation(LPCWSTR fn,   // [in] FileName
		LPBY_HANDLE_FILE_INFORMATION file_info,				// [out] Buffer
		PDOKAN_FILE_INFO info)
{
	LOG_STACK_TRACE();
	IFileInfo * file = AchieveFileInfo(info);
	if (!file)
	{
		LOG_ERROR(L"[err] file %s is not opened", fn);
		return STATUS_UNSUCCESSFUL;
	}
	bool br = file->GetFileInformation(file_info);
	file->Release();
	LOG_TRACK(L"dokan", L" GetFileInfo, fn=%s, atrr=%08X, size=%d", fn, file_info->dwFileAttributes, file_info->nFileSizeLow);
	return (br) ? STATUS_SUCCESS : STATUS_UNSUCCESSFUL;
}

// FindFilesWithPattern is checking first. If it is not implemented or returns STATUS_NOT_IMPLEMENTED, then FindFiles is called, if implemented.
NTSTATUS DOKAN_CALLBACK dokancb_FindFiles(LPCWSTR fn,	// PathName
	PFillFindData fill_data,					// call this function with PWIN32_FIND_DATAW
	PDOKAN_FILE_INFO info)						//  (see PFillFindData definition)
{
	LOG_STACK_TRACE();
	LOG_TRACK(L"dokan", L" FindFiles: fn=%s", fn);
	IFileInfo * file = AchieveFileInfo(info);
	if (!file) return STATUS_UNSUCCESSFUL;

	CDokanFindFileCallback call_back(fill_data, info);
	bool br = file->EnumerateFiles(static_cast<EnumFileListener*>(&call_back));
	file->Release();
	return STATUS_SUCCESS;

}

// SetFileAttributes and SetFileTime are called only if both of them are implemented.
NTSTATUS DOKAN_CALLBACK dokancb_SetFileAttributes
(	LPCWSTR fn, // FileName
	DWORD attr,   // FileAttributes
	PDOKAN_FILE_INFO info)
{
	LOG_STACK_TRACE();
	LOG_TRACK(L"dokan", L" SetAttribute, fn=%s, attr = 0x%08X", fn, attr);
	IFileInfo * file = AchieveFileInfo(info);
	if (!file) return STATUS_UNSUCCESSFUL;
	file->DokanSetFileAttributes(attr);
	file->Release();

	return STATUS_SUCCESS;
}

NTSTATUS DOKAN_CALLBACK dokancb_SetFileTime
(	LPCWSTR fn,          // FileName
	CONST FILETIME * create_time, // CreationTime
	CONST FILETIME * access_time, // LastAccessTime
	CONST FILETIME * modify_time, // LastWriteTime
	PDOKAN_FILE_INFO info)
{
	LOG_STACK_TRACE();
	LOG_TRACK(L"dokan", L" SetFileTime, fn=%s, ct=0x%p, at=0x%p, mt=0x%p", fn, create_time, access_time, modify_time);
	IFileInfo * file = AchieveFileInfo(info);
	if (!file) return STATUS_UNSUCCESSFUL;
	if (create_time && create_time->dwHighDateTime == 0 && create_time->dwLowDateTime == 0) create_time = nullptr;
	if (access_time && access_time->dwHighDateTime == 0 && access_time->dwLowDateTime == 0) access_time = nullptr;
	if (modify_time && modify_time->dwHighDateTime == 0 && modify_time->dwLowDateTime == 0) modify_time = nullptr;
	file->SetFileTime(create_time, access_time, modify_time);
	file->Release();

	return STATUS_SUCCESS;
}

NTSTATUS DOKAN_CALLBACK dokancb_DeleteFile
(	LPCWSTR fn, // FileName
	PDOKAN_FILE_INFO info)
{
	LOG_STACK_TRACE();
	IFileSystem * fs = AchieveFileSystem(info);
	IFileInfo * file = AchieveFileInfo(info);
	LOG_TRACK(L"dokan", L" Delete File, fn=%s", fn);

//	LOG_DEBUG(L"delete file=%s, object=0x%p", fn, file);
	if (file->IsDirectory())
	{
		LOG_ERROR(L"[err] try to delete a dir.");
		fs->Release();
		file->Release();
		return STATUS_ACCESS_DENIED;
	}


	NTSTATUS err = fs->DokanDeleteFile(fn, file, false);
	file->Release();
	fs->Release();
	if (err != STATUS_SUCCESS) 
	{
		LOG_ERROR(L"[err] failed on deleting file %s, err=0x%X", fn, err);
		return err;
	}
	// 对于已经删除的文件，删除相关的对象
	file->Release();
	InterlockedExchange64((LONG64*)(&info->Context), 0);
	return err;
}

NTSTATUS DOKAN_CALLBACK dokancb_DeleteDirectory
(	LPCWSTR fn, // FileName
	PDOKAN_FILE_INFO info)
{
	LOG_STACK_TRACE();
	IFileSystem * fs = AchieveFileSystem(info);
	IFileInfo * file = AchieveFileInfo(info);

//	LOG_DEBUG(L"delete directory=%s, object=0x%p", fn, file);
	LOG_TRACK(L"dokan", L" Delete Dir, fn=%s", fn);

	if (!file->IsDirectory())
	{
		LOG_ERROR(L"[err] try to delete a file.");
		fs->Release();
		file->Release();
		return STATUS_ACCESS_DENIED;
	}
	if (!file->IsEmpty())
	{
		LOG_ERROR(L"[err] dir is not empty");
		fs->Release();
		file->Release();
		return STATUS_DIRECTORY_NOT_EMPTY;
	}
	NTSTATUS err = fs->DokanDeleteFile(fn, file, true);
	file->Release();
	fs->Release();
	if (err != STATUS_SUCCESS)
	{
		LOG_ERROR(L"[err] failed on deleting file %s, err=0x%X", fn, err);
		return err;
	}

	file->Release();
	InterlockedExchange64((LONG64*)(&info->Context), 0);
	return err;
}

NTSTATUS DOKAN_CALLBACK dokancb_MoveFile
(	LPCWSTR src_fn, // ExistingFileName
	LPCWSTR dst_fn, // NewFileName
	BOOL replace,    // ReplaceExisiting
	PDOKAN_FILE_INFO info)
{
	LOG_STACK_TRACE();
	LOG_TRACK(L"dokan", L" MoveFile src=%s to dst=%s, replace=%d", src_fn, dst_fn, replace);
	IFileSystem * fs = AchieveFileSystem(info);
	IFileInfo * file = AchieveFileInfo(info);

	LOG_DEBUG(L"fs=0x%p, file=0x%p", fs, file);
	//LOG_DEGUB(L"file name=%s", file->)
	NTSTATUS st = fs->DokanMoveFile(src_fn, dst_fn, replace, file);

	RELEASE(file);
	RELEASE(fs);

	return st;
}

NTSTATUS DOKAN_CALLBACK dokancb_SetEndOfFile
(	LPCWSTR fn,  // FileName
	LONGLONG flen, // Length
	PDOKAN_FILE_INFO info)
{
	LOG_STACK_TRACE();
	LOG_TRACK(L"dokan", L" set end of file, fn=%s, length=%d", fn, (DWORD)flen);
	NTSTATUS st = STATUS_SUCCESS;
	IFileInfo * file = AchieveFileInfo(info);
	if (!file)
	{
		LOG_ERROR(L"[err] file is not opened.");
		return STATUS_UNSUCCESSFUL;
	}
	bool br = file->SetEndOfFile(flen);
	if (!br)	st = STATUS_DISK_FULL;
	RELEASE(file);
	return st;
}

NTSTATUS DOKAN_CALLBACK dokancb_SetAllocationSize
(	LPCWSTR fn,  // FileName
	LONGLONG flen, // Length
	PDOKAN_FILE_INFO info)
{
	LOG_STACK_TRACE();
	LOG_TRACK(L"dokan", L" SetAllocationSize, fn=%s, length=%d", fn, (DWORD)flen);
	IFileInfo * file = AchieveFileInfo(info);
	if (!file) return STATUS_UNSUCCESSFUL;
	bool br = file->SetAllocationSize(flen);
	RELEASE(file);
	return br ? STATUS_SUCCESS : STATUS_UNSUCCESSFUL;
}

NTSTATUS DOKAN_CALLBACK dokancb_LockFile(LPCWSTR fn,		// FileName
	LONGLONG offset,							// ByteOffset
	LONGLONG len,								// Length
	PDOKAN_FILE_INFO info)
{
	LOG_STACK_TRACE();
	LOG_TRACK(L"dokan", L" LockFile, fn=%s, length=%zd, offset=%zd", fn, len, offset);
	IFileInfo * file = AchieveFileInfo(info);
	if (!file) return STATUS_UNSUCCESSFUL;
	NTSTATUS st = file->LockFile(offset, len);
	file->Release();
	return st;
}

NTSTATUS DOKAN_CALLBACK dokancb_UnlockFile(LPCWSTR fn,	// FileName
	LONGLONG offset,							// ByteOffset
	LONGLONG len,								// Length
	PDOKAN_FILE_INFO info)
{
	LOG_STACK_TRACE();
	LOG_TRACK(L"dokan", L" UnlockFile, fn=%s, length=%zd, offset=%zd", fn, len, offset);
	IFileInfo * file = AchieveFileInfo(info);
	if (!file) return STATUS_UNSUCCESSFUL;
	NTSTATUS st = file->UnlockFile(offset, len);
	file->Release();
	return st;
}

// Neither GetDiskFreeSpace nor GetVolumeInformation save the DokanFileContext->Context.
// Before these methods are called, CreateFile may not be called. (ditto CloseFile and Cleanup)

// see Win32 API GetDiskFreeSpaceEx
NTSTATUS DOKAN_CALLBACK dokancb_GetDiskFreeSpace(
	PULONGLONG pfree_bytes,			// [out] FreeBytesAvailable
	PULONGLONG ptotal_bytes,			// [out] TotalNumberOfBytes
	PULONGLONG ptotal_free_bytes,	// [out]TotalNumberOfFreeBytes
	PDOKAN_FILE_INFO info)
{
	LOG_STACK_TRACE();
	IFileSystem * fs = AchieveFileSystem(info);
	ULONGLONG free_bytes, total_bytes, total_free_bytes;
	bool br = fs->DokanGetDiskSpace(free_bytes, total_bytes, total_free_bytes);
	if (ptotal_bytes) *ptotal_bytes = total_bytes;
	if (pfree_bytes) *pfree_bytes = free_bytes;
	if (ptotal_free_bytes) * ptotal_free_bytes = total_free_bytes;

	fs->Release();
	return STATUS_SUCCESS;
}

// Note: FILE_READ_ONLY_VOLUME is automatically added to the FileSystemFlags if DOKAN_OPTION_WRITE_PROTECT was specified in DOKAN_OPTIONS when the volume was mounted.

// see Win32 API GetVolumeInformation
NTSTATUS DOKAN_CALLBACK dokancb_GetVolumeInformation(LPWSTR name,  // [out] VolumeNameBuffer
	DWORD name_len,			// [in] VolumeNameSize in num of chars
	LPDWORD psn,			// [out] VolumeSerialNumber
	LPDWORD pmax_len,		// MaximumComponentLength in num of chars
	LPDWORD pfs_flag,		// FileSystemFlags
	LPWSTR fs_name,			// FileSystemNameBuffer
	DWORD fs_name_len,		// FileSystemNameSize in num of chars
	PDOKAN_FILE_INFO info)
{
	LOG_STACK_TRACE();
	IFileSystem * fs = AchieveFileSystem(info);
	DWORD sn, max_len, fs_flag;

	std::wstring str_vol, str_fs_name;
	bool br = fs->GetVolumnInfo(str_vol, sn, max_len, fs_flag, str_fs_name);

	wcscpy_s(fs_name, fs_name_len, str_fs_name.c_str());
	wcscpy_s(name, name_len, str_vol.c_str());

	if (psn) *psn=sn;
	if (pmax_len) *pmax_len=max_len;
	if (pfs_flag) *pfs_flag = fs_flag;

	fs->Release();
	return br ? (STATUS_SUCCESS) : (STATUS_UNSUCCESSFUL);
}

NTSTATUS DOKAN_CALLBACK dokancb_Mounted(LPCWSTR MountPoint, PDOKAN_FILE_INFO info)
{
	LOG_STACK_TRACE();
	LOG_DEBUG(L"mount point = %s, optional mount point=%s", MountPoint, info->DokanOptions->MountPoint);
	return STATUS_SUCCESS;
}

NTSTATUS DOKAN_CALLBACK dokancb_Unmounted(PDOKAN_FILE_INFO info)
{
	LOG_STACK_TRACE();
	LOG_DEBUG(L"global_context = %016I64X", info->DokanOptions->GlobalContext);
	IFileSystem * fs = AchieveFileSystem(info);
	JCASSERT(fs);
	fs->Unmount();
	fs->Release();	// 和Run()中 set dokan options时的AddRef()对应。
	return STATUS_SUCCESS;
}

// Suported since 0.6.0. You must specify the version at
// DOKAN_OPTIONS.Version.
NTSTATUS DOKAN_CALLBACK dokancb_GetFileSecurity
(	LPCWSTR,						// FileName
	PSECURITY_INFORMATION psinfo,	// A pointer to SECURITY_INFORMATION value being requested
	PSECURITY_DESCRIPTOR psdesc,	// A pointer to SECURITY_DESCRIPTOR buffer to be filled
	ULONG size,						// length of Security descriptor buffer
	PULONG needed,					// LengthNeeded
	PDOKAN_FILE_INFO context)
{
	LOG_STACK_TRACE();
	//if (!psinfo) return STATUS_SUCCESS;
	//IFileInfo * file = AchieveFileInfo(context);
	//if (!file) return STATUS_UNSUCCESSFUL;

	//bool br = file->DokanGetFileSecurity(*psinfo, psdesc, size);
	//if (needed) *needed = size;
	//file->Release();

	//return br?STATUS_SUCCESS:STATUS_UNSUCCESSFUL;

	return STATUS_NOT_IMPLEMENTED;
}

NTSTATUS DOKAN_CALLBACK dokancb_SetFileSecurity
(	LPCWSTR,					// FileName
	PSECURITY_INFORMATION psinfo,
	PSECURITY_DESCRIPTOR psdesc, // SecurityDescriptor
	ULONG size,                // SecurityDescriptor length
	PDOKAN_FILE_INFO context)
{
	LOG_STACK_TRACE();
	//IFileInfo * file = AchieveFileInfo(context);
	//if (!file) return STATUS_UNSUCCESSFUL;

	//bool br = file->DokanSetFileSecurity(psinfo, psdesc, size);
	//file->Release();
	//return br ? STATUS_SUCCESS : STATUS_UNSUCCESSFUL;
	return STATUS_NOT_IMPLEMENTED;
}

// Supported since 0.8.0. You must specify the version at
// DOKAN_OPTIONS.Version.
NTSTATUS DOKAN_CALLBACK dokancb_FindStreams(LPCWSTR FileName, PFillFindStreamData FillFindStreamData,
	PVOID FindStreamContext, PDOKAN_FILE_INFO DokanFileInfo)
{
	return STATUS_SUCCESS;
}

void PrepareDokan(IFileSystem* fs, const std::wstring& mount, DOKAN_OPTIONS & opt, DOKAN_OPERATIONS & oper)
{
	memset(&opt, 0, sizeof(opt));
	opt.Version = DOKAN_VERSION;
	//	opt.ThreadCount = 1;	// for debug
								//opt.Options = DOKAN_OPTION_WRITE_PROTECT | DOKAN_OPTION_DEBUG | DOKAN_OPTION_STDERR;
#ifdef _DEBUG
	opt.Options = fs->GetFileSystemOption() /*| DOKAN_OPTION_DEBUG | DOKAN_OPTION_STDERR*/;
	opt.Options |= DOKAN_OPTION_MOUNT_MANAGER;
	int debug_mode = fs->GetDebugMode();
	if (debug_mode) opt.Options |= (DOKAN_OPTION_DEBUG | DOKAN_OPTION_STDERR);
#else
	opt.Options = fs->GetFileSystemOption();
#endif
	opt.GlobalContext = (ULONG64)(fs);
	fs->AddRef();
	opt.MountPoint = mount.c_str();
	opt.UNCName = L"";
	opt.Timeout = 10000000;

	memset(&oper, 0, sizeof(oper));
	oper.ZwCreateFile = dokancb_CreateFile;
	oper.Cleanup = dokancb_Cleanup;
	oper.CloseFile = dokancb_CloseFile;
	oper.ReadFile = dokancb_ReadFile;
	oper.WriteFile = dokancb_WriteFile;
	oper.FlushFileBuffers = dokancb_FlushFileBuffers;
	oper.GetFileInformation = dokancb_GetFileInformation;
	oper.FindFiles = dokancb_FindFiles;
	oper.FindFilesWithPattern = NULL;
	oper.SetFileAttributesW = dokancb_SetFileAttributes;
	oper.SetFileTime = dokancb_SetFileTime;
	oper.DeleteFile = dokancb_DeleteFile;
	oper.DeleteDirectory = dokancb_DeleteDirectory;
	oper.MoveFile = dokancb_MoveFile;
	oper.SetEndOfFile = dokancb_SetEndOfFile;
	oper.SetAllocationSize = dokancb_SetAllocationSize;
	oper.LockFile = dokancb_LockFile;
	oper.UnlockFile = dokancb_UnlockFile;
	oper.GetDiskFreeSpace = dokancb_GetDiskFreeSpace;
	oper.GetVolumeInformation = dokancb_GetVolumeInformation;
	oper.Mounted = dokancb_Mounted;
	oper.Unmounted = dokancb_Unmounted;
	oper.GetFileSecurityW = dokancb_GetFileSecurity;
	oper.SetFileSecurityW = dokancb_SetFileSecurity;
	oper.FindStreams = dokancb_FindStreams;
}

int StartDokan(IFileSystem* fs, const std::wstring & mount)
{
	DOKAN_OPTIONS opt;
	DOKAN_OPERATIONS oper;
	PrepareDokan(fs, mount, opt, oper);

	DokanInit();
	int ir = 0;
	ir = DokanMain(&opt, &oper);
	LOG_NOTICE(L"DokanMain returns %d", ir);
	fs->Release();
	return ir;
}

int StartDokanAsync(IFileSystem* fs, const std::wstring& mount)
{
	DOKAN_OPTIONS opt;
	DOKAN_OPERATIONS oper;
	PrepareDokan(fs, mount, opt, oper);

	DokanInit();
	DOKAN_HANDLE hh;
	int err = DokanCreateFileSystem(&opt, &oper, &hh);
	if (err)
	{
		LOG_ERROR(L"failed on creating dokan file system, err=%d", err);
		return err;
	}
	DokanWaitForFileSystemClosed(hh, INFINITE);
	int ir = 0;
	LOG_NOTICE(L"DokanMain returns %d", ir);
	return ir;
}

#include "stdafx.h"
#include "..\include\dokanfs_base.h"

LOCAL_LOGGER_ENABLE(L"dokanfs", LOGGER_LEVEL_NOTICE);
#define DIR_SEPARATOR	('\\')


CDokanFsBase::CDokanFsBase()
{
}


CDokanFsBase::~CDokanFsBase()
{
}

bool CDokanFsBase::OpenFileDir(IFileInfo *& file, wchar_t * path, size_t path_len)
{
	LOG_STACK_TRACE();
	LOG_DEBUG(L"path=%s", path);

	JCASSERT(file == NULL);
	if (path_len == 0 || (path[0] == DIR_SEPARATOR && path[1] == 0) )
	{	// open root dir
		GetRoot(file);
		return true;
	}
	// search fn
	// 解析path，分解为路径(path)和文件名(str_fn)
	jcvos::auto_interface<IFileInfo> parent_dir;
	wchar_t * str_fn = path + path_len - 1;
	while (*str_fn != DIR_SEPARATOR && str_fn >= path) str_fn--;

	JCASSERT(str_fn >= path);	// path必须是全路径，以/开头。前面已经处理掉根目录的情况
	size_t new_path_len = str_fn - path;
	*str_fn = 0, str_fn++;
	// 打开父目录
	LOG_DEBUG(L"open parent %s", path);
	bool br = OpenFileDir(parent_dir, path, new_path_len);
	if (!br || parent_dir == NULL) return false;
	JCASSERT(parent_dir->IsDirectory());

	// 在父目录中搜索文件
	LOG_DEBUG(L"find file: %s", str_fn);
	br = parent_dir->OpenChild(file, str_fn);
	if (!br || file == NULL) return false;	// 未找到文件
	return true;
}

bool CDokanFsBase::DokanCreateFile(IFileInfo *& file, const std::wstring & path, ACCESS_MASK access_mask, DWORD attr, DWORD disp, ULONG share, ULONG opt, bool isdir)
{
	LOG_STACK_TRACE();
	LOG_DEBUG(L"disp = %d, dir = %d", disp, isdir);
	//	LOG_DEBUG_(2, L"root cluster=%d", m_root_dir->GetStartCluster());

		// case 1: not exist, create
		// case 2: not exist, open -> error
		// case 3: exist, create -> error
		// case 4: exist, open
			// case 4.1: remain data
			// case 4.2: truck data

	size_t path_len = path.size();
	jcvos::auto_array<wchar_t> _str_path(path_len + 1);
	wchar_t * str_path = (wchar_t*)_str_path;
	wcscpy_s(str_path, path_len + 1, path.c_str());
	//	NTSTATUS st = STATUS_UNSUCCESSFUL;
	bool br = false;

	wchar_t * str_fn;
	// 尝试打开文件，如果成功，返回文件，否则返回父目录
//	jcvos::auto_interface<CDropBoxFile> parent_dir;
	jcvos::auto_interface<IFileInfo> parent_dir;
	wchar_t * ch = str_path + path_len - 1;
	while (*ch != DIR_SEPARATOR && ch >= str_path) ch--;
	str_fn = ch + 1;	// 排除根目录的"\"
	size_t fn_len = path_len - (str_fn - str_path);
	LOG_DEBUG(L"file name = %s, length = %d", str_fn, fn_len);

	if (ch == str_path)
	{	// parent is root
		LOG_DEBUG(L"parent is root");
		GetRoot(parent_dir);
	}
	else
	{
		size_t parent_len = ch - str_path;
		*ch = 0;
		LOG_DEBUG(L"open parent %s, length=%d", ch, parent_len);
		bool br = OpenFileDir(parent_dir, str_path, parent_len);
		if (!br || parent_dir == NULL)
		{
			LOG_ERROR(L"[err] parent dir %s is not exist", ch);
			return false;
		}
		JCASSERT(parent_dir->IsDirectory());
	}

	// try to open file
	br = false;
	jcvos::auto_interface<IFileInfo> _file;
	if (*str_fn == 0)	// file = parent
	{
		br = true;
		_file = parent_dir;
		_file->AddRef();
	}
	else { br = parent_dir->OpenChild(_file, str_fn); }

	if (br && _file)
	{	// 打开成功
		LOG_DEBUG(L"open file success");
		// <TODO> 处理文件读些属性
		//if (access_mask & GENERIC_READ) _file->m_file.flags |= FILE_READ;
		//if (access_mask & GENERIC_WRITE) _file->m_file.flags |= FILE_WRITE;

		//if (disp == CREATE_NEW)
		//{
		//	LOG_ERROR(L"[err] file %s existed with create new", path.c_str());
		//	return false;
		//}

		switch (disp)
		{
		case CREATE_NEW:
			LOG_ERROR(L"[err] file %s existed with create new", path.c_str());
			br = false;
			break;

		case OPEN_ALWAYS:	br = true; break;
		case OPEN_EXISTING:	br = true; break;
		case CREATE_ALWAYS:
			// <TODO> clear file
			br = true;
			break;
		case TRUNCATE_EXISTING:
			// <TODO> clear file
			br = true;
			break;
		default:
			LOG_ERROR(L"[err] unknow disp=%d", disp);
			br = false;
			break;
		}
		if (br)		_file.detach(file);
		return br;
	}
	else
	{	// 没有找到文件
		LOG_NOTICE(L"file: %s is not found", path.c_str());
		if ((disp == OPEN_EXISTING) || (disp == TRUNCATE_EXISTING)) return false;
//		Lock();
		bool br = parent_dir->CreateChild(_file, str_fn, isdir);
		if (br && _file)
		{
			br = true;
			//if (access_mask & GENERIC_READ) _file->m_file.flags |= FILE_READ;
			//if (access_mask & GENERIC_WRITE) _file->m_file.flags |= FILE_WRITE;
		}
//		Unlock();
		_file.detach(file);
		return br;
	}
	//	LOG_DEBUG_(2, L"root cluster=%d", m_root_dir->GetStartCluster());
	return br;
}

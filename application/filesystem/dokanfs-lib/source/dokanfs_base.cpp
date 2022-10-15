///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
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
	if (!br || !parent_dir) return false;
	JCASSERT(parent_dir->IsDirectory());

	// 在父目录中搜索文件
	LOG_DEBUG(L"find file: %s", str_fn);
	br = parent_dir->OpenChild(file, str_fn, 0);
	if (!br || file == NULL) return false;	// 未找到文件
	return true;
}

NTSTATUS CDokanFsBase::DokanCreateFile(IFileInfo *& file, const std::wstring & path, ACCESS_MASK access_mask, DWORD attr, DWORD disp, ULONG share, ULONG opt, bool isdir)
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
		if (!br || !parent_dir)
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
	else { br = parent_dir->OpenChild(_file, str_fn, 0); }

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
		bool br = parent_dir->CreateChild(_file, str_fn, isdir, 0);
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

typedef bool(*PLUGIN_GET_FACTORY)(IFsFactory*&);




int CDokanFsBase::LoadDisk(IVirtualDisk*& disk, const std::wstring& working_dir, const boost::property_tree::wptree& config)
{
	LOG_STACK_TRACE();
	jcvos::auto_interface<IFsFactory> disk_factory;
	const std::wstring& disk_lib = config.get<std::wstring>(L"library", L"");
	HMODULE plugin_disk = LoadLibrary(disk_lib.c_str());
	if (plugin_disk == NULL) THROW_WIN32_ERROR(L" failure on loading lib %s ", disk_lib.c_str());
	PLUGIN_GET_FACTORY gf = (PLUGIN_GET_FACTORY)(GetProcAddress(plugin_disk, "GetFactory"));
	if (gf == NULL)	THROW_WIN32_ERROR(L"file %s is not a file system plugin.", disk_lib.c_str());
	bool br = (gf)(disk_factory);
	if (!br || !disk_factory) THROW_ERROR(ERR_USER, L"failed on getting plugin register in %s", disk_lib.c_str());
	br = disk_factory->CreateVirtualDisk(disk, config, true);
	if (!br || !disk)	THROW_ERROR(ERR_APP, L"failed on creating device");
//	m_capacity = disk->GetCapacity();
	return 0;
}

int CDokanFsBase::LoadFilesystem(IFileSystem*& fs, IVirtualDisk* disk, const std::wstring& working_dir, const boost::property_tree::wptree& config)
{
	LOG_STACK_TRACE();
	JCASSERT(fs == nullptr && disk);
	const std::wstring str_lib = config.get<std::wstring>(L"library");
	const std::wstring fs_name = config.get<std::wstring>(L"file_system", L"");

	if (str_lib.empty())	THROW_ERROR(ERR_PARAMETER, L"missing DLL.");
	LOG_DEBUG(L"loading dll: %s...", str_lib.c_str());
	HMODULE plugin = LoadLibrary(str_lib.c_str());
	if (plugin == NULL) THROW_WIN32_ERROR(L" failure on loading driver %s ", str_lib.c_str());

	LOG_DEBUG(L"getting entry...");
	PLUGIN_GET_FACTORY get_factory = (PLUGIN_GET_FACTORY)(GetProcAddress(plugin, "GetFactory"));
	if (get_factory == NULL)	THROW_WIN32_ERROR(L"file %s is not a file system plugin.", str_lib.c_str());

	jcvos::auto_interface<IFsFactory> factory;
	bool br = (get_factory)(factory);
	if (!br || !factory.valid()) THROW_ERROR(ERR_USER, L"failed on getting plugin register in %s", str_lib.c_str());

	br = factory->CreateFileSystem(fs, fs_name);
	if (!br || !fs) THROW_ERROR(ERR_APP, L"failed on creating file system");
	return 0;
}

int CDokanFsBase::LoadFilesystemByConfig(IFileSystem*& fs, IVirtualDisk*& disk, const std::wstring& working_dir, const boost::property_tree::wptree& config)
{
	LOG_STACK_TRACE();
	JCASSERT(disk == NULL);
	//m_op_id = 0;
	//// 解析config file的路径，将其设置为缺省路径
	//wchar_t path[MAX_PATH];
	//wchar_t filename[MAX_PATH];
	//wchar_t cur_dir[MAX_PATH];
	//GetCurrentDirectory(MAX_PATH - 1, cur_dir);
	//GetFullPathName(m_config_file.c_str(), MAX_PATH, path, NULL);

	//wcscpy_s(filename, path);
	//PathRemoveFileSpec(path);
	//SetCurrentDirectory(path);
	//PathStripPath(filename);

	//std::wstring config_path = working_dir + L"\\" + config_fn;

	// load configuration
	//std::string str_fn;
	////jcvos::UnicodeToUtf8(str_fn, m_config_file);
	//jcvos::UnicodeToUtf8(str_fn, config_fn);
	//boost::property_tree::wptree pt;
	//boost::property_tree::json_parser::read_json(str_fn, pt);

	int err = 0;

	// load device
	const auto& pt_device = config.get_child_optional(L"config.device");
	if (pt_device) err = LoadDisk(disk, working_dir, *pt_device);
	if (err)
	{
		LOG_ERROR(L"[err] failed on creating virtial disk, err=%d", err);
		return err;
	}

	// load file system
	const auto& pt_filesystem = config.get_child_optional(L"config.filesystem");
	if (!pt_filesystem) THROW_ERROR(ERR_APP, L"missing file system section in the config file");
	err = LoadFilesystem(fs, disk, working_dir, *pt_filesystem);
	if (err)
	{
		LOG_ERROR(L"[err] failed on creating file system, err=%d", err);
		return err;
	}
	return err;
}

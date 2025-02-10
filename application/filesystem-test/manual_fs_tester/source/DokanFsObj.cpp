#include "stdafx.h"

#include "DokanFsObj.h"
#include "global_init.h"

#include "utility.h"
#include <map>

#include <boost/property_tree/xml_parser.hpp>
#include <boost/property_tree/json_parser.hpp>

#pragma comment (lib, "ole32.lib")
#pragma comment (lib, "VssApi.lib")
#pragma comment (lib, "Advapi32.lib")
#pragma comment (lib, "wbemuuid.lib")
#pragma comment (lib, "OleAut32.lib")

using namespace System;
using namespace ManualFsTester;

LOCAL_LOGGER_ENABLE(L"fs_tester", LOGGER_LEVEL_NOTICE);

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// == DokanFsObj ==

ManualFsTester::DokanFsObj::DokanFsObj(System::String^ _config_fn)
{
	std::wstring str_lib = global.m_module_path + L"\\f2fs-lib.dll";

	std::string config_fn;
	ToUtf8String(config_fn, _config_fn);
	boost::property_tree::wptree config;
	boost::property_tree::xml_parser::read_xml(config_fn, config);

	m_fs = CDokanFsBase::CreateFs(str_lib, config);
}

ManualFsTester::DokanFsObj::!DokanFsObj(void)
{
	//printf_s("call !DokanFsObj()\n"); 
	if (m_fs) m_fs->Release();
}

bool ManualFsTester::DokanFsObj::Mount(DokanStorage^ _disk)
{
	IVirtualDisk* disk = _disk->get_disk();
	if (disk == nullptr) throw gcnew System::ApplicationException(L"disk object is null");

	size_t secs = disk->GetCapacity();
	bool br = m_fs->Mount(disk);
	return br;
}

bool ManualFsTester::DokanFsObj::MakeFs(DokanStorage^ _disk)
{
	IVirtualDisk* disk = _disk->get_disk();
	if (disk == nullptr) throw gcnew System::ApplicationException(L"disk object is null");

	size_t secs = disk->GetCapacity();
	bool ir = m_fs->MakeFileSystem(disk, secs, L"FSTEST");
	return ir;
}

void ManualFsTester::DokanFsObj::Unmount(void)
{
	m_fs->Unmount();
}

bool ManualFsTester::DokanFsObj::CheckFs(DokanStorage^ _disk, System::String ^ _config_fn)
{
	boost::property_tree::wptree option;
	if (_config_fn)
	{
		std::string config_fn;
		ToUtf8String(config_fn, _config_fn);
//		boost::property_tree::wptree config;
		boost::property_tree::json_parser::read_json(config_fn, option);
	}

	IVirtualDisk* disk = _disk->get_disk();
	if (disk == nullptr) throw gcnew System::ApplicationException(L"disk object is null");
	IFileSystem::FSCK_RESULT  ir = m_fs->FileSystemCheck(disk, false, option);
	return ir == IFileSystem::FSCK_SUCCESS;
}


DokanFileObj^ ManualFsTester::DokanFsObj::CreateFile(System::String^ file_name, FileType type, CreateFileOption option)
{
	IFileInfo* file = nullptr;
	std::wstring fn;
	ToStdString(fn, file_name);

	IFileSystem::FsCreateDisposition op;
	switch (option)
	{
	case ManualFsTester::CreateFileOption::FS_CREATE_NEW:       op = IFileSystem::FS_CREATE_NEW;	break;
	case ManualFsTester::CreateFileOption::FS_CREATE_ALWAYS:	op = IFileSystem::FS_CREATE_ALWAYS; break;
	case ManualFsTester::CreateFileOption::FS_OPEN_EXISTING:	op = IFileSystem::FS_OPEN_EXISTING; break;
	case ManualFsTester::CreateFileOption::FS_OPEN_ALWAYS:		op = IFileSystem::FS_OPEN_ALWAYS;	break;
	case ManualFsTester::CreateFileOption::FS_TRUNCATE_EXISTING:op = IFileSystem::FS_TRUNCATE_EXISTING;	break;
	}
	bool dir;
	if (type == FileType::FILE_DIR) dir = true;
	else dir = false;

	m_fs->DokanCreateFile(file, fn, GENERIC_ALL, 0, op, 0, 0, dir);
	if (file == nullptr) return nullptr;

	if (type == FileType::FILE_NORMAL)
	{
		DokanFileObj^ file_obj = gcnew DokanFileObj(file);
		file->Release();
		return file_obj;
	}
	else return nullptr;
}

DokanFileObj^ ManualFsTester::DokanFsObj::OpenFile(System::String^ file_name)
{
	IFileInfo* file = nullptr;
	std::wstring fn;
	ToStdString(fn, file_name);
	m_fs->DokanCreateFile(file, fn, GENERIC_ALL, 0, IFileSystem::FS_OPEN_EXISTING, 0, 0, false);
	if (file == nullptr) return nullptr;

	DokanFileObj^ file_obj = gcnew DokanFileObj(file);
	file->Release();

	return file_obj;
}

bool ManualFsTester::DokanFsObj::Verify(void)
{
	bool br = CheckAllChilds(L"\\");
	return br;
}


class Listener : public EnumFileListener
{
public:
	virtual bool EnumFileCallback(const std::wstring& fn,
		UINT32 ino, UINT32 entry, // entry 在父目录中的位置
		BY_HANDLE_FILE_INFORMATION* info)
	{
		bool is_dir = (info->dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
		files.insert(std::make_pair(fn, is_dir));
		return true;
	}
	std::map<std::wstring, bool> files;
};


bool ManualFsTester::DokanFsObj::CheckAllChilds(const std::wstring& fn)
{
	IFileInfo* file = nullptr;
	m_fs->DokanCreateFile(file, fn, GENERIC_ALL, 0, IFileSystem::FS_OPEN_EXISTING, 0, 0, true);
	if (file == nullptr)
	{
		std::wstring msg = L"Filed on open file " + fn;
		throw gcnew System::ApplicationException(gcnew System::String(msg.c_str()) );
	}
	
	Listener listener;
	file->EnumerateFiles(static_cast<EnumFileListener*>(&listener));

	//
	wprintf_s(L"Files in directory: %s\n", fn.c_str());
	for (auto it = listener.files.begin(); it != listener.files.end(); ++it)
	{
		wprintf_s(L"\t%s:\t%s\n", it->first.c_str(), it->second ? L"Dir_" : L"File");
	}

	for (auto it = listener.files.begin(); it != listener.files.end(); ++it)
	{
		const std::wstring& sub_item = it->first;
		if (it->second && sub_item != L"." && sub_item != L"..")
		{
			std::wstring full_fn;
			if (fn == L"\\") full_fn = L"\\" + sub_item;
			else full_fn = fn + L"\\" + sub_item;
			CheckAllChilds(full_fn);
		}
	}
	file->Release();
	return true;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// == DokanStorage ==

ManualFsTester::DokanStorage::DokanStorage(System::String^ _lib_path, System::String ^ _drive_name)
{
	std::wstring lib_name;
	ToStdString(lib_name, _lib_path);

	std::wstring drive_name;
	ToStdString(drive_name, _drive_name);

	std::wstring str_lib = global.m_module_path + L"\\" + lib_name;
	m_disk = CDokanFsBase::CreateStorage(str_lib, drive_name, L"");
}

ManualFsTester::DokanStorage::!DokanStorage(void)
{
	//printf_s("call !DokanStorage()\n"); 
	if (m_disk) m_disk->Release();
}

bool ManualFsTester::DokanStorage::Initialize(System::String^ _config_fn, System::String^ data_file)
{
	if (m_disk == nullptr) throw gcnew System::ApplicationException(L"disk object is null");
	std::string config_fn;
	ToUtf8String(config_fn, _config_fn);
	boost::property_tree::wptree config;
	boost::property_tree::xml_parser::read_xml(config_fn, config);
	boost::property_tree::wptree& drive_config = config.get_child(L"config.device");

	if (data_file)
	{
		std::wstring str_data_file;
		ToStdString(str_data_file, data_file);
		drive_config.get_child(L"file_name").put_value(str_data_file);
//		drive_config.put(L"file_name", str_data_file);
	}

	bool br = m_disk->InitializeDevice(drive_config);
	return br;
}

bool ManualFsTester::DokanStorage::Save(System::String^ fn)
{
	if (m_disk == nullptr) throw gcnew System::ApplicationException(L"disk object is null");
	std::wstring str_fn;
	ToStdString(str_fn, fn);
	bool br = m_disk->SaveToFile(str_fn);
	return br;
}

bool ManualFsTester::DokanStorage::Load(System::String^ fn)
{
	if (m_disk == nullptr) throw gcnew System::ApplicationException(L"disk object is null");
	std::wstring str_fn;
	ToStdString(str_fn, fn);
	bool br = m_disk->LoadFromFile(str_fn);
	return br;
}

bool ManualFsTester::DokanStorage::Rollback(int blk_nr)
{
	if (m_disk == nullptr) throw gcnew System::ApplicationException(L"disk object is null");
	m_disk->BackLog(blk_nr);
	return true;
}

array<PSObject^>^ ManualFsTester::DokanStorage::ListIOs(void)
{
	if (m_disk == nullptr) throw gcnew System::ApplicationException(L"disk object is null");

	size_t io_nr = m_disk->GetLogNumber();
	IO_ENTRY* entries = new IO_ENTRY[io_nr];
	io_nr = m_disk->GetIoLogs(entries, io_nr);
//	System::Array 
	array<PSObject^>^ cmd_list = gcnew array<PSObject^>((int)io_nr);
	for (size_t ii = 0; ii < io_nr; ++ii)
	{
		System::Management::Automation::PSObject^ entry = gcnew System::Management::Automation::PSObject;

		//AddPropertyMember(entry, L"lba", entries[ii].lba);
		//AddPropertyMember<UInt32>(entry, L"secs",	entries[ii].secs);
		//AddPropertyMember<UInt32, UINT>(entry, L"index",	entries[ii].block_index);
		//AddPropertyMember<UInt32, UINT>(entry, L"blk_nr", entries[ii].blk_nr);
		//AddPropertyMember<String, const wchar_t *>(entry, L"op",		entries[ii].op.c_str());

		PSNoteProperty^ item = gcnew PSNoteProperty(L"lba", gcnew UInt32(entries[ii].lba));
		entry->Members->Add(item);
		item = gcnew PSNoteProperty(L"secs", gcnew UInt32(entries[ii].secs));
		entry->Members->Add(item);
		item = gcnew PSNoteProperty(L"blk_index", gcnew UInt32(entries[ii].block_index));
		entry->Members->Add(item);
		item = gcnew PSNoteProperty(L"blk_nr", gcnew UInt32(entries[ii].blk_nr));
		entry->Members->Add(item);
		item = gcnew PSNoteProperty(L"op", gcnew String(entries[ii].op.c_str()));
		entry->Members->Add(item);

		cmd_list[ii] = entry;
			

//		wprintf_s(L"%s, %d, %d\n", entries[ii].cmd == IO_ENTRY::READ_SECTOR ? L"READ " : L"WRITE", entries[ii].lba, entries[ii].secs);
	}
	return cmd_list;
}

//ManualFsTester::DokanStorage::~DokanStorage(void)
//{
//	printf_s("call ~DokanStorage()"); 
//	if (m_disk) m_disk->Release();
//}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// == DokanFileObj ==



ManualFsTester::DokanFileObj::DokanFileObj(IFileInfo* file)
{
	m_file = file;
	m_file->AddRef();
	name = gcnew System::String (m_file->GetFileName().c_str());
}

ManualFsTester::DokanFileObj::!DokanFileObj(void)
{
	if (m_file) {
		m_file->CloseFile();
		m_file->Release();
	}
}

UINT ManualFsTester::DokanFileObj::WriteFile(UINT offset, UINT length)
{
	if (m_file == nullptr) throw gcnew System::ApplicationException(L"file is close not exist");
	DWORD written = 0;
	BYTE* buf = new BYTE[length];
	// fill buffer
	wmemset((wchar_t*)buf, 0x55AA, length / 2);
	m_file->DokanWriteFile(buf, length, written, offset);
	return written;
}

void ManualFsTester::DokanFileObj::CloseFile(void)
{
	if (m_file == nullptr) throw gcnew System::ApplicationException(L"file is close not exist");
	m_file->CloseFile();
	m_file->Release();
	m_file = nullptr;
}

UINT ManualFsTester::DokanFileObj::GetFileIndex(void)
{
	if (m_file == nullptr) throw gcnew System::ApplicationException(L"file is close not exist");
	BY_HANDLE_FILE_INFORMATION finfo;
	m_file->GetFileInformation(&finfo);
	return finfo.nFileIndexLow;
}

UINT ManualFsTester::DokanFileObj::GetFileSize(void)
{
	if (m_file == nullptr) throw gcnew System::ApplicationException(L"file is close not exist");
	BY_HANDLE_FILE_INFORMATION finfo;
	m_file->GetFileInformation(&finfo);
	return finfo.nFileSizeLow;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// == CmdLets ==


void ManualFsTester::NewDokanF2FS::ProcessRecord()
{
	DokanFsObj^ fs = gcnew DokanFsObj(config);
	WriteObject(fs);
	//	throw gcnew System::NotImplementedException();
}


void ManualFsTester::NewDokanStorage::ProcessRecord()
{
	DokanStorage^ storage = gcnew DokanStorage(lib_path, name);
	WriteObject(storage);
}
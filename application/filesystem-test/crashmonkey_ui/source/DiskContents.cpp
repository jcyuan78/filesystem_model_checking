///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
#include "pch.h"

#include "DiskContents.h"

#include <boost/algorithm/hex.hpp>

using std::endl;
using std::cout;
using std::wstring;
//using std::ofstream;

//namespace fs_testing {
using fs_testing::fileAttributes;
using fs_testing::DiskContents;

LOCAL_LOGGER_ENABLE(L"crashmonke.diskcotents", LOGGER_LEVEL_DEBUGINFO);


fileAttributes::fileAttributes() : m_fs(NULL)
{
	//md5sum = L"";
	// Initialize dir_attr entries
	//dir_attr.d_ino = -1;
	//dir_attr.d_off = -1;
	//dir_attr.d_reclen = -1;
	//dir_attr.d_type = -1;
	//dir_attr.d_name[0] = '\0';
	// Initialize stat_attr entried
	//stat_attr.st_ino == -1;
	//stat_attr.st_mode = -1;
	//stat_attr.st_nlink = -1;
	//stat_attr.st_uid = -1;
	//stat_attr.st_gid = -1;
	//stat_attr.st_size = -1;
	//stat_attr.st_blksize = -1;
	//stat_attr.st_blocks = -1;
	memset(&m_stat_attr, 0, sizeof(BY_HANDLE_FILE_INFORMATION));
}

fileAttributes::~fileAttributes()
{
	//if (m_file) m_file->CloseFile();
	RELEASE(m_fs);
}

void fileAttributes::set_dir_attr(struct dirent* a)
{
	//dir_attr.d_ino = a->d_ino;
	//dir_attr.d_off = a->d_off;
	//dir_attr.d_reclen = a->d_reclen;
	//dir_attr.d_type = a->d_type;
	//strncpy(dir_attr.d_name, a->d_name, sizeof(a->d_name));
	//dir_attr.d_name[sizeof(a->d_name) - 1] = '\0';
}

//void fileAttributes::set_stat_attr(std::wstring path, bool islstat)
//{
//	if (islstat)
//	{
//		//lstat(path.c_str(), &stat_attr);
//	}
//	else
//	{
//		_wstat(path.c_str(), &stat_attr);
//	}
//	return;
//}


void fileAttributes::set_md5sum(void)
{
	JCASSERT(m_fs);
	jcvos::auto_interface<IFileInfo> ff;
	bool br = m_fs->DokanCreateFile(ff, m_path, GENERIC_ALL, 0, IFileSystem::FS_OPEN_EXISTING, 0, 0, false);
	if (!br || !ff)
	{
		LOG_ERROR(L"[err] Failed opening file %s", m_path.c_str());
		return;
	}

	using boost::uuids::detail::md5;

	md5 hash;

	size_t file_size = MakeLongLong(m_stat_attr.nFileSizeLow, m_stat_attr.nFileSizeHigh);
	const DWORD buf_size = 4 * 1024;
	jcvos::auto_array<BYTE> buf(buf_size);
	DWORD remain = boost::numeric_cast<DWORD>(file_size);
	size_t offset = 0;
	while (remain > 0)
	{
		DWORD rr = min(buf_size, remain);
		DWORD read = 0;

		ff->DokanReadFile(buf, rr, read, offset);
		hash.process_bytes(buf.get_ptr(), read);
		remain -= read;
		offset += read;
	}
	ff->CloseFile();
	hash.get_digest(m_md5sum);
}

#ifdef _TO_BE_IMPLEMENTED_
bool fileAttributes::compare_dir_attr(struct dirent a)
{

	return ((dir_attr.d_ino == a.d_ino) &&
		(dir_attr.d_off == a.d_off) &&
		(dir_attr.d_reclen == a.d_reclen) &&
		(dir_attr.d_type == a.d_type) &&
		(strcmp(dir_attr.d_name, a.d_name) == 0));
}
#endif // _TO_BE_IMPLEMENTED_

//bool fileAttributes::compare_stat_attr(const struct _stat64i32& a)
//{
//	return ((stat_attr.st_ino == a.st_ino) &&
//		(stat_attr.st_mode == a.st_mode) &&
//		(stat_attr.st_nlink == a.st_nlink) &&
//		(stat_attr.st_uid == a.st_uid) &&
//		(stat_attr.st_gid == a.st_gid) &&
//		// (stat_attr.st_rdev == a.st_rdev) &&
//		// (stat_attr.st_dev == a.st_dev) &&
//		(stat_attr.st_size == a.st_size) &&
//		//(stat_attr.st_blksize == a.st_blksize) &&
//		//(stat_attr.st_blocks == a.st_blocks));
//		true);
//}

bool fs_testing::fileAttributes::compare_stat_attr(const BY_HANDLE_FILE_INFORMATION& fa)
{
	bool res = true
		&& (m_stat_attr.dwFileAttributes == fa.dwFileAttributes)
		&& (m_stat_attr.nFileIndexHigh == fa.nFileIndexHigh)
		&& (m_stat_attr.nFileIndexLow == fa.nFileIndexLow)
		&& (m_stat_attr.nFileSizeHigh == fa.nFileSizeHigh)
		&& (m_stat_attr.nFileSizeLow == fa.nFileSizeLow)
		&& (m_stat_attr.nNumberOfLinks == fa.nNumberOfLinks);

	return res;
}

//bool fileAttributes::compare_md5sum(const boost::uuids::detail::md5::digest_type& digest) const 
//{
//	return m_md5sum == digest;
//}

bool fileAttributes::is_regular_file()
{
	return !(m_stat_attr.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY);
}

bool fs_testing::fileAttributes::set_path(IFileSystem* fs, const std::wstring& path)
{
	JCASSERT(fs);
	m_fs = fs;
	m_fs->AddRef();
	m_path = path;
	jcvos::auto_interface<IFileInfo> ff;
	bool br = fs->DokanCreateFile(ff, path, GENERIC_ALL, 0, IFileSystem::FS_OPEN_EXISTING, 0, 0, false);
	if (!br || !ff)
	{
		LOG_ERROR(L"[err] Failed opening file %s", path.c_str());
		return false;
	}
	if (!ff->GetFileInformation(&m_stat_attr))
	{
		LOG_ERROR(L"[err] Failed geting file info, %s", path.c_str());
		return false;
	}
	//if (is_regular_file())		set_md5sum(ff);
	ff->CloseFile();
	return true;
}

bool fs_testing::fileAttributes::compare_content(fileAttributes& compare, size_t offset, size_t len)
{
	//JCASSERT(m_file);
	DWORD read = 0;
	jcvos::auto_array<BYTE> buf1(len);
	jcvos::auto_interface<IFileInfo> f1;

	bool br = m_fs->DokanCreateFile(f1, m_path, GENERIC_ALL, 0, IFileSystem::FS_OPEN_EXISTING, 0, 0, false);
	if (!br || !f1)
	{
		LOG_ERROR(L"[err] Failed opening file %s", m_path.c_str());
		return false;
	}
	br = f1->DokanReadFile(buf1, boost::numeric_cast<DWORD>(len), read, offset);
	if (!br)
	{
		LOG_ERROR(L"[err] failed on reading source file");
		return false;
	}
	f1->CloseFile();


	jcvos::auto_interface<IFileInfo> f2;
	jcvos::auto_array<BYTE> buf2(len);
	br = compare.m_fs->DokanCreateFile(f2, m_path, GENERIC_ALL, 0, IFileSystem::FS_OPEN_EXISTING, 0, 0, false);
	if (!br || !f2)
	{
		LOG_ERROR(L"[err] Failed opening file %s", m_path.c_str());
		return false;
	}
	br = f2->DokanReadFile(buf2, boost::numeric_cast<DWORD>(len), read, offset);
	if (!br)
	{
		LOG_ERROR(L"[err] failed on reading reference file");
		return false;
	}
	f2->CloseFile();
	return memcmp(buf1, buf2, len) == 0;
}

bool fs_testing::fileAttributes::compare_md5sum(fileAttributes& compare)
{
	//JCASSERT(m_file);
	set_md5sum();
	compare.set_md5sum();
	return m_md5sum == compare.m_md5sum;
}



std::wofstream& operator<< (std::wofstream& os, fileAttributes& a)
{
	// print dir_attr
	os << L"---Directory Atrributes---" << endl;
	//os << L"Name   : " << (a.dir_attr).d_name << endl;
	//os << L"Inode  : " << (a.dir_attr).d_ino << endl;
	//os << L"Offset : " << (a.dir_attr).d_off << endl;
	//os << L"Length : " << (a.dir_attr).d_reclen << endl;
	//os << L"Type   : " << (a.dir_attr).d_type << endl;
	// print stat_attr
	os << L"---File Stat Atrributes---" << endl;
	os << L"Inode     : " << MakeLongLong(a.m_stat_attr.nFileIndexLow, a.m_stat_attr.nFileIndexHigh) << endl;
	os << L"TotalSize : " << MakeLongLong(a.m_stat_attr.nFileSizeLow, a.m_stat_attr.nFileSizeHigh) << endl;
	//os << L"BlockSize : " << (a.stat_attr).st_blksize << endl;
	//os << L"#Blocks   : " << (a.stat_attr).st_blocks << endl;
	os << L"#HardLinks: " << a.m_stat_attr.nNumberOfLinks << endl;
	//os << L"Mode      : " << (a.stat_attr).st_mode << endl;
	//os << L"User ID   : " << (a.stat_attr).st_uid << endl;
	//os << L"Group ID  : " << (a.stat_attr).st_gid << endl;
	//os << L"Device ID : " << (a.stat_attr).st_rdev << endl;
	//os << L"RootDev ID: " << (a.stat_attr).st_dev << endl;
	return os;
}

DiskContents::DiskContents(IVirtualDisk* dev, IFileSystem* fs, std::wstring path, std::wstring type)
{
	if (dev == NULL || fs == NULL) THROW_ERROR(ERR_APP, L"dev or fs cannot be null");
	m_dev = dev;
	m_dev->AddRef();
	m_fs = fs;
	m_fs->AddRef();

	disk_path = path;
	fs_type = type;
	m_device_mounted = false;
}

DiskContents::~DiskContents()
{
	if (m_device_mounted) m_fs->Unmount();
	RELEASE(m_fs);
	RELEASE(m_dev);
}

int DiskContents::mount_disk()
{
	bool br = m_fs->Mount(m_dev);
	if (!br)
	{
		LOG_ERROR(L"failed on mount filesystem");
		return -1;
	}

	// Construct and set mount_point
	mount_point = L"/mnt/";
	mount_point += disk_path.substr(5);
	LOG_DEBUG(L"mount point = %s", mount_point.c_str());
	// Create the mount directory with read/write/search permissions for owner and group, and with read/search permissions for others.
	LOG_DEBUG(L"[linux] make dir: %s", mount_point.c_str());
	LOG_DEBUG(L"[linux] mount dev %s to dir %s, fs type = %s", disk_path.c_str(), mount_point.c_str(), fs_type.c_str());
	m_device_mounted = true;
	return 0;

}

int DiskContents::unmount_and_delete_mount_point()
{
	// umount till successful
	if (m_device_mounted)
	{
		m_fs->Unmount();
		m_device_mounted = false;
	}
	return 0;
}

void DiskContents::set_mount_point(std::wstring path)
{
	mount_point = path;
}

void DiskContents::get_contents(const wchar_t* path)
{
#if 0

	DIR* directory;
	struct dirent* dir_entry;
	// open both the directories
	if (!(directory = opendir(path)))	{		return;	}
	// get the contents in both the directories
	if (!(dir_entry = readdir(directory)))
	{
		closedir(directory);
		return;
	}
	do
	{
		std::wstring parent_path(path);
		std::wstring filename(dir_entry->d_name);
		std::wstring current_path = parent_path + "/" + filename;
		std::wstring relative_path = current_path;
		relative_path.erase(0, mount_point.length());
		struct stat statbuf;
		fileAttributes fa;
		if (stat(current_path.c_str(), &statbuf) == -1)		{			continue;		}
		if (dir_entry->d_type == DT_DIR)
		{
			if ((strcmp(dir_entry->d_name, ".") == 0) || (strcmp(dir_entry->d_name, "..") == 0)){continue;	}
			fa.set_dir_attr(dir_entry);
			fa.set_stat_attr(current_path, false);
			contents[relative_path] = fa;
			// If the entry is a directory and not . or .. make a recursive call
			get_contents(current_path.c_str());
		}
		else if (dir_entry->d_type == DT_LNK)
		{
			// compare lstat outputs
			struct stat lstatbuf;
			if (lstat(current_path.c_str(), &lstatbuf) == -1)	{		continue;	}
			fa.set_stat_attr(current_path, true);
			contents[relative_path] = fa;
		}
		else if (dir_entry->d_type == DT_REG)
		{
			fa.set_md5sum(current_path);
			fa.set_stat_attr(current_path, false);
			contents[relative_path] = fa;
		}
		else
		{
			fa.set_stat_attr(current_path, false);
			contents[relative_path] = fa;
		}
	} while (dir_entry = readdir(directory));
	closedir(directory);
#endif
}

std::wstring DiskContents::get_mount_point()
{
	return mount_point;
}

bool DiskContents::compare_disk_contents(DiskContents& compare_disk, std::wofstream& diff_file)
{
#if 1
	bool retValue = true;
	if (disk_path.compare(compare_disk.disk_path) == 0)	{	return retValue;	}

	std::wstring base_path = L"/mnt/snapshot";

	if (mount_disk() != 0)
	{
		LOG_ERROR(L"[err] failed on mounting test fs to drive: %s", disk_path.c_str());
		return false;
	}
	std::vector<std::wstring> files_src; 
	JCASSERT(0);	//<TODO> implement ListAllFiles
//	fs_testing::utility::ListAllFiles(m_fs, L"\\", files_src);
//	get_contents(base_path.c_str());

	if (compare_disk.mount_disk() != 0)
	{
		LOG_ERROR(L"[err] failed on mounting reference fs to drive: %s", compare_disk.disk_path.c_str());
		//std::wcout << L"Mounting " << compare_disk.disk_path << L" failed" << endl;
		return false;
	}
	std::vector<std::wstring> files_ref;
//	fs_testing::utility::ListAllFiles(compare_disk.m_fs, L"\\", files_ref);
//	compare_disk.get_contents(compare_disk.get_mount_point().c_str());

	// Compare the size of contents
//	if (contents.size() != compare_disk.contents.size())
	if (files_src.size() != files_ref.size())
	{
		diff_file << "DIFF: Mismatch" << endl;
		diff_file << "Unequal #entries in " << disk_path << ", " << compare_disk.disk_path;
		diff_file << endl << endl;
		diff_file << disk_path << " contains:" << endl;
		for (auto& i : files_src)
		{
			diff_file << i << endl;
		}
		diff_file << endl;

		diff_file << compare_disk.disk_path << " contains:" << endl;
		for (auto& i : files_ref)
		{
			diff_file << i << endl;
		}
		diff_file << endl;
		retValue = false;
	}

	// entry-wise comparision
	for (auto& i : files_src)
	{
//		fileAttributes i_fa = i.second;
		fileAttributes base_fa, compare_fa;
		bool br = base_fa.set_path(m_fs, i);
		br = compare_fa.set_path(compare_disk.m_fs, i);
//		if (compare_disk.contents.find((i.first)) == compare_disk.contents.end())
		if (!br)
		{
			diff_file << "DIFF: Missing " << i << endl;
			diff_file << "Found in " << disk_path << " only" << endl;
			diff_file << base_fa <<endl << endl;
			retValue = false;
			continue;
		}
		//fileAttributes j_fa = compare_disk.contents[(i.first)];
		//if (/*!(i_fa.compare_dir_attr(j_fa.dir_attr)) ||*/
		//	!(i_fa.compare_stat_attr(j_fa.stat_attr)))
		if (base_fa.compare_stat_attr(compare_fa.m_stat_attr))
		{
			diff_file << "DIFF: Content Mismatch " << i << endl << endl;
			diff_file << disk_path << ":" << endl;
			diff_file << base_fa << endl << endl;
			diff_file << compare_disk.disk_path << ":" << endl;
			diff_file << compare_fa << endl << endl;
			retValue = false;
			continue;
		}
		// compare user data if the entry corresponds to a regular files
		if (base_fa.is_regular_file())
		{
			// check md5sum of the file contents
			//if (i_fa.compare_md5sum(j_fa.md5sum) != 0)
			if (base_fa.compare_md5sum(compare_fa)!=0)
			{
				diff_file << "DIFF : Data Mismatch of " << (i) << endl;
				diff_file << disk_path << " has md5sum " << base_fa.m_md5sum << endl;
				diff_file << compare_disk.disk_path << " has md5sum " << compare_fa.m_md5sum;
				diff_file << endl << endl;
				retValue = false;
			}
		}
	}
	//compare_disk.unmount_and_delete_mount_point();
	return retValue;
#endif
}

// TODO(P.S.) Cleanup the code and pull out redundant code into separate functions
bool DiskContents::compare_entries_at_path(DiskContents& compare_disk, std::wstring& path, std::wofstream& diff_file)
{
	bool retValue = true;
	if (disk_path.compare(compare_disk.disk_path) == 0) { return retValue; }
	std::wstring base_path = path;
	if (mount_disk() != 0)
	{
		LOG_ERROR(L"[err] failed on mounting test fs to drive: %s", disk_path.c_str());
		return false;
	}

	if (compare_disk.mount_disk() != 0)
	{
		LOG_ERROR(L"[err] failed on mounting reference fs to drive: %s", compare_disk.disk_path.c_str());
		//std::wcout << L"Mounting " << compare_disk.disk_path << L" failed" << endl;
		return false;
	}

	//<YUAN>虚拟文件系统没有mount点，使用相同的路径
	std::wstring compare_path = base_path;

	fileAttributes base_fa, compare_fa;
	bool failed_stat = false;
	bool br = false;
#ifdef _DEBUG
	std::wstring compare_disk_mount_point(compare_disk.get_mount_point());
	LOG_DEBUG(L"compare point=%s, compare path=%s", compare_disk_mount_point.c_str(), compare_path.c_str());
	std::vector<std::wstring> files;
	JCASSERT(0) //<TODO>
//	fs_testing::utility::ListAllFiles(m_fs, L"\\", files);
#endif

	//jcvos::auto_interface<IFileInfo> base_dir;
	//br = m_fs->DokanCreateFile(base_dir, base_path, GENERIC_ALL, 0, IFileSystem::FS_OPEN_EXISTING, 0, 0, false);
	//if (!br || !base_dir)
	//{
	//	diff_file << L"Failed opening file " << base_path << std::endl;
	//	return false;
	//}
	//if (!base_dir->GetFileInformation(&base_fa.m_stat_attr))
	//{
	//	diff_file << L"Failed stating the file " << base_path << std::endl;
	//	return false;
	//}
	//if (base_fa.is_regular_file())		base_fa.set_md5sum(base_dir);
	//base_dir->CloseFile();
	br = base_fa.set_path(m_fs, base_path);
	if (!br)
	{
		LOG_ERROR(L"[err] failed on getting file info on test fs");
		return false;
	}

	//jcvos::auto_interface<IFileInfo> compare_dir;
	//br = m_fs->DokanCreateFile(compare_dir, compare_path, GENERIC_ALL, 0, IFileSystem::FS_OPEN_EXISTING, 0, 0, false);
	//if (!br || !base_dir)
	//{
	//	diff_file << L"Failed opening file " << compare_path << std::endl;
	//	return false;
	//}
	//if (!compare_dir->GetFileInformation(&compare_fa.m_stat_attr))
	//{
	//	diff_file << L"Failed stating the file " << compare_path << std::endl;
	//	return false;
	//}
	//if (compare_fa.is_regular_file()) compare_fa.set_md5sum(compare_dir);
	//compare_dir->CloseFile();
	br = compare_fa.set_path(compare_disk.m_fs, base_path);
	if (!br)
	{
		LOG_ERROR(L"[err] failed on getting file info on reference fs");
		return false;
	}

	LOG_DEBUG(L"stat file: base=%s, compare=%s", base_path.c_str(), compare_path.c_str());

	if (!(base_fa.compare_stat_attr(compare_fa.m_stat_attr)))
	{
		diff_file << L"DIFF: Content Mismatch " << path << endl << endl;
		diff_file << base_path << L":" << endl;
		diff_file << base_fa << endl << endl;
		diff_file << compare_path << ":" << endl;
		diff_file << compare_fa << endl << endl;
		return false;
	}

	if (base_fa.is_regular_file())
	{
		if (base_fa.compare_md5sum(compare_fa) != 0)
		{
			diff_file << L"DIFF : Data Mismatch of " << path << endl;
			diff_file << base_path << L" has md5sum " << base_fa.m_md5sum << endl;
			diff_file << compare_path << L" has md5sum " << compare_fa.m_md5sum;
			diff_file << endl << endl;
			return false;
		}
	}
	return retValue;
}

// TODO[P.S]: Compare fixed sized segments of files, to support comparing very large files.
bool DiskContents::compare_file_contents(DiskContents& compare_disk, std::wstring path,
	int offset, int length, std::wofstream& diff_file)
{
	bool retValue = true;
	if (disk_path.compare(compare_disk.disk_path) == 0)	{		return retValue;	}

	std::wstring base_path = path;
	if (mount_disk() != 0)
	{
		LOG_ERROR(L"[err] failed on mounting test fs to drvie : %s", disk_path.c_str());
		return false;
	}

	if (compare_disk.mount_disk() != 0)
	{
		LOG_ERROR(L"[err] failed on mounting reference fs to drvie : %s", compare_disk.disk_path.c_str());
//		std::wcout << L"Mounting " << compare_disk.disk_path << L" failed" << endl;
		return false;
	}
	std::wstring compare_disk_mount_point(compare_disk.get_mount_point());
	std::wstring compare_path = compare_disk_mount_point + path;

	fileAttributes base_fa, compare_fa;
	bool failed_stat = false;
	bool br = base_fa.set_path(m_fs, base_path);
	if (!br)
	{
		LOG_ERROR(L"[err] failed on getting file info on test fs");
		return false;
	}

	br = compare_fa.set_path(compare_disk.m_fs, base_path);
	if (!br)
	{
		LOG_ERROR(L"[err] failed on getting file info on reference fs");
		return false;
	}

	//struct _stat64i32 base_statbuf, compare_statbuf;
	//if (_wstat(base_path.c_str(), &base_statbuf) == -1)
	//{
	//	diff_file << "Failed stating the file " << base_path << endl;
	//	failed_stat = true;
	//}
	//if (_wstat(compare_path.c_str(), &compare_statbuf) == -1)
	//{
	//	diff_file << "Failed stating the file " << compare_path << endl;
	//	failed_stat = true;
	//}

	//if (failed_stat)
	//{
	//	compare_disk.unmount_and_delete_mount_point();
	//	return false;
	//}

	//std::ifstream f1(base_path, std::ios::binary);
	//std::ifstream f2(compare_path, std::ios::binary);

	//if (!f1 || !f2)
	//{
	//	std::wcout << L"Error opening input file streams " << base_path << L" and ";
	//	std::wcout << compare_path << endl;
	//	compare_disk.unmount_and_delete_mount_point();
	//	return false;
	//}

	//f1.seekg(offset, std::ifstream::beg);
	//f2.seekg(offset, std::ifstream::beg);

	//char* buffer_f1 = new char[length + 1];
	//char* buffer_f2 = new char[length + 1];

	//f1.read(buffer_f1, length);
	//f2.read(buffer_f2, length);
	//f1.close();
	//f2.close();

	//buffer_f1[length] = '\0';
	//buffer_f2[length] = '\0';

	//if (strcmp(buffer_f1, buffer_f2) == 0)
	//{
	//	compare_disk.unmount_and_delete_mount_point();
	//	return true;
	//}

	if (base_fa.compare_content(compare_fa, offset, length)) return true;

	diff_file << __func__ << " failed" << endl;
	diff_file << "Content Mismatch of file " << path << " from ";
	diff_file << offset << " of length " << length << endl;
	//diff_file << base_path << " has " << buffer_f1 << endl;
	//diff_file << compare_path << " has " << buffer_f2 << endl;
	compare_disk.unmount_and_delete_mount_point();
	return false;
}

bool DiskContents::isEmptyDirOrFile(std::wstring path)
{

#if 1
	//jcvos::auto_interface<IFileInfo> ff;
	//bool br = m_fs->DokanCreateFile(ff, path, GENERIC_ALL, 0, IFileSystem::FS_OPEN_EXISTING, 0, 0, false);
	//if (!br || !ff) return false;
	//if (!ff->IsDirectory())
	//{
	//	ff->CloseFile();
	//	return true;
	//}

	std::vector<std::wstring> files;
	JCASSERT(0);	//<TODO>
//	fs_testing::utility::ListAllFiles(m_fs, path, files);
	bool is_empty = (files.size() == 0);
	return is_empty;

	//DIR* directory = opendir(path.c_str());
	//if (directory == NULL)
	//{
	//	return true;
	//}

	//struct dirent* dir_entry;
	//int num_dir_entries = 0;
	//while (dir_entry = readdir(directory))
	//{
	//	if (++num_dir_entries > 2)
	//	{
	//		break;
	//	}
	//}
	//closedir(directory);
	//if (num_dir_entries <= 2)
	//{
	//	return true;
	//}
	//return false;
#endif
}

bool isFile(std::wstring path)
{
	struct _stat64i32 sb;
	if (_wstat(path.c_str(), &sb) < 0)
	{
		std::wcout << __func__ << L": Failed stating " << path << endl;
		return false;
	}
#ifdef _TO_BE_IMPLEMENTED_
	if (S_ISDIR(sb.st_mode))
	{
		return false;
	}
#endif
	return true;
}

bool DiskContents::deleteFiles(std::wstring path, std::wofstream& diff_file)
{

	if (path.empty())	{		return true;	}

	if (isEmptyDirOrFile(path) == true)
	{
		if (path.compare(L"\\") == 0)		{			return true;		}
		//if (isFile(path) == true)	{	return (_wunlink(path.c_str()) == 0);	}
		//else		{			return (rmdir(path.c_str()) == 0);		}
	}

#if 0
	DIR* directory = opendir(path.c_str());
	if (directory == NULL)
	{
		cout << "Couldn't open the directory " << path << endl;
		diff_file << "Couldn't open the directory " << path << endl;
		return false;
	}

	struct dirent* dir_entry;
	while (dir_entry = readdir(directory))
	{
		if ((strcmp(dir_entry->d_name, ".") == 0) ||
			(strcmp(dir_entry->d_name, "..") == 0))
		{
			continue;
		}

		std::wstring subpath = path + "/" + std::wstring(dir_entry->d_name);
		bool subpathIsFile = isFile(subpath);
		bool res = deleteFiles(subpath, diff_file);
		if (!res)
		{
			closedir(directory);
			diff_file << "Couldn't remove directory " << subpath << " " << strerror(errno) << endl;
			cout << "Couldn't remove directory " << subpath << " " << strerror(errno) << endl;
			return res;
		}

		if (!subpathIsFile)
		{
			if (rmdir(subpath.c_str()) < 0)
			{
				diff_file << "Couldn't remove directory " << subpath << " " << strerror(errno) << endl;
				cout << "Couldn't remove directory " << subpath << " " << strerror(errno) << endl;
				return false;
			}
		}
	}
	closedir(directory);
#endif
	return true;
}

bool DiskContents::makeFiles(std::wstring base_path, std::wofstream& diff_file)
{
#if 1
	std::vector<std::wstring> files;
	JCASSERT(0); // <TODO>
//	fs_testing::utility::ListAllFiles(m_fs, L"\\", files);

//	get_contents(base_path.c_str());
	for (auto& i : files)
	{
		fileAttributes fa;
		fa.set_path(m_fs, i);
//		if (S_ISDIR((i.second).stat_attr.st_mode))
		if (!fa.is_regular_file())
		{
			std::wstring filepath = base_path + i + L"/" + L"_dummy";
			LOG_NOTICE(L"create file %s under %s", filepath.c_str(), i.c_str());

			std::wstring path = i + L"\\_dummy";
			jcvos::auto_interface<IFileInfo> ff;
			bool br = m_fs->DokanCreateFile(ff, path, GENERIC_ALL, 0, IFileSystem::FS_CREATE_NEW, 0, 0, false);
			if (!br || !ff)
			{
				diff_file << "Couldn't create file " << filepath << endl;
				LOG_ERROR(L"[err] Couldn't create file %s ", path.c_str());
				return false;
			}
			ff->CloseFile();

			//int fd = open(filepath.c_str(), O_CREAT | O_RDWR);
			//if (fd < 0)
			//{
			//	diff_file << "Couldn't create file " << filepath << endl;
			//	cout << "Couldn't create file " << filepath << endl;
			//	return false;
			//}
			//close(fd);
		}
	}
	return true;
#endif
}

bool DiskContents::sanity_checks(std::wofstream& diff_file)
{
	cout << __func__ << endl;

//	std::wstring base_path = L"/mnt/snapshot";
	std::wstring base_path = L"\\";
	if (!makeFiles(base_path, diff_file))
	{
		cout << "Failed: Couldn't create files in all directories" << endl;
		diff_file << "Failed: Couldn't create files in all directories" << endl;
		return false;
	}

	if (!deleteFiles(base_path, diff_file))
	{
		cout << "Failed: Couldn't delete all the existing directories" << endl;
		diff_file << "Failed: Couldn't delete all the existing directories" << endl;
		return false;
	}

	cout << "Passed sanity checks" << endl;
	return true;
}



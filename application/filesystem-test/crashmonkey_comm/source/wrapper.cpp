#include "pch.h"
#include "../include/wrapper.h"

#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>

#include <utility>

#include <iostream>
#include <boost/cast.hpp>
//#include "../api/actions.h"
#include "../include/ClientCommandSender.h"

using fs_testing::user_tools::api::RecordCmFsOps;
using fs_testing::user_tools::api::PassthroughCmFsOps;

using std::pair;
using std::shared_ptr;
using std::tuple;
using std::unordered_map;
using std::vector;

using fs_testing::utils::DiskMod;
LOCAL_LOGGER_ENABLE(L"crashmonke.wrapper", LOGGER_LEVEL_DEBUGINFO);



#if 0
using fs_testing::user_tools::api::DefaultFsFns;

int DefaultFsFns::FnMknod(const std::std::wstring& pathname, mode_t mode, dev_t dev)
{
	return mknod(pathname.c_str(), mode, dev);
}

int DefaultFsFns::FnMkdir(const std::std::wstring& pathname, mode_t mode)
{
	return mkdir(pathname.c_str(), mode);
}

int DefaultFsFns::FnOpen(const std::std::wstring& pathname, int flags)
{
	return open(pathname.c_str(), flags);
}

int DefaultFsFns::FnOpen2(const std::std::wstring& pathname, int flags, mode_t mode)
{
	return open(pathname.c_str(), flags, mode);
}

off_t DefaultFsFns::FnLseek(int fd, off_t offset, int whence)
{
	return lseek(fd, offset, whence);
}

ssize_t DefaultFsFns::FnWrite(int fd, const void* buf, size_t count)
{
	return write(fd, buf, count);
}

ssize_t DefaultFsFns::FnPwrite(int fd, const void* buf, size_t count,
	off_t offset)
{
	return pwrite(fd, buf, count, offset);
}

void* DefaultFsFns::FnMmap(void* addr, size_t length, int prot, int flags,
	int fd, off_t offset)
{
	return mmap(addr, length, prot, flags, fd, offset);
}

int DefaultFsFns::FnMsync(void* addr, size_t length, int flags)
{
	return msync(addr, length, flags);
}

int DefaultFsFns::FnMunmap(void* addr, size_t length)
{
	return munmap(addr, length);
}

int DefaultFsFns::FnFallocate(int fd, int mode, off_t offset, off_t len)
{
	return fallocate(fd, mode, offset, len);
}

int DefaultFsFns::FnClose(int fd)
{
	return close(fd);
}

int DefaultFsFns::FnRename(const std::wstring& old_path, const std::wstring& new_path)
{
	return rename(old_path.c_str(), new_path.c_str());
}

int DefaultFsFns::FnUnlink(const std::std::wstring& pathname)
{
	return unlink(pathname.c_str());
}

int DefaultFsFns::FnRemove(const std::std::wstring& pathname)
{
	return remove(pathname.c_str());
}


int DefaultFsFns::FnStat(const std::std::wstring& pathname, struct stat* buf)
{
	return stat(pathname.c_str(), buf);
}

bool DefaultFsFns::FnPathExists(const std::std::wstring& pathname)
{
	const int res = access(pathname.c_str(), F_OK);
	// TODO(ashmrtn): Should probably have some better way to handle errors.
	if (res != 0)
	{
		return false;
	}

	return true;
}

int DefaultFsFns::FnFsync(const int fd)
{
	return fsync(fd);
}

int DefaultFsFns::FnFdatasync(const int fd)
{
	return fdatasync(fd);
}

void DefaultFsFns::FnSync()
{
	sync();
}

// int DefaultFsFns::FnSyncfs(const int fd) {
//   return syncfs(fd);
// }

int DefaultFsFns::FnSyncFileRange(const int fd, size_t offset, size_t nbytes,
	unsigned int flags)
{
	return sync_file_range(fd, offset, nbytes, flags);
}

int DefaultFsFns::CmCheckpoint()
{
	return Checkpoint();
}
#endif

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// ==== RecordCmFsOps


RecordCmFsOps::RecordCmFsOps(IFileSystem* fs) : PassthroughCmFsOps(fs)
{
}

//int RecordCmFsOps::CmMknod(const std::wstring& pathname, const mode_t mode, const dev_t dev)
//{
//	//return fns_->FnMknod(pathname.c_str(), mode, dev);
//	THROW_ERROR(ERR_APP, L"unsuppor");
//	return 0;
//}

int RecordCmFsOps::CmMkdir(const std::wstring& pathname, const mode_t mode)
{
	//const int res = fns_->FnMkdir(pathname.c_str(), mode);
	//if (res < 0)
	//{
	//	return res;
	//}
	bool br = m_fs->MakeDir(pathname.c_str());

	DiskMod mod;
	mod.directory_mod = true;
	mod.path = pathname;
	mod.mod_type = DiskMod::kCreateMod;
	mod.mod_opts = DiskMod::kNoneOpt;

	mods_.push_back(mod);

	return br;
}

bool fs_testing::user_tools::api::RecordCmFsOps::CmCreateFile(IFileInfo*& file, const std::wstring& fn, 
	ACCESS_MASK access_mask, DWORD attr, IFileSystem::FsCreateDisposition disp, ULONG share, ULONG opt, bool isdir)
{
	JCASSERT(file == NULL);
	bool br = m_fs->DokanCreateFile(file, fn, access_mask, attr, disp, share, opt, isdir);
	if (!br || file == NULL) return false;
	DiskMod mod;
	mod.directory_mod = isdir;
	if (disp == IFileSystem::FS_OPEN_EXISTING || disp == IFileSystem::FS_TRUNCATE_EXISTING)
	{
		mod.mod_type = DiskMod::kDataMetadataMod;
		mod.mod_opts = DiskMod::kTruncateOpt;
	}
	else
	{
		mod.mod_type = DiskMod::kCreateMod;
		mod.mod_opts = DiskMod::kNoneOpt;
	}
	mod.path = fn;
	mods_.push_back(mod);
	m_fd_map.insert({ file, fn });
//	file->AddRef();
	return true;
}

//void RecordCmFsOps::CmOpenCommon(const int fd, const std::wstring& pathname, const bool exists, const int flags)
//{
//	fd_map_.insert({ fd, pathname });
//
//	if (!exists || (flags & O_TRUNC))
//	{
//		// We only want to record this op if we changed something on the file system.
//
//		DiskMod mod;
//
//		// Need something like stat() because umask could affect the file permissions.
//		const int post_stat_res = fns_->FnStat(pathname.c_str(), &mod.post_mod_stats);
//		if (post_stat_res < 0)
//		{
//			// TODO(ashmrtn): Some sort of warning here?
//			return;
//		}
//
//		mod.directory_mod = S_ISDIR(mod.post_mod_stats.st_mode);
//
//		if (!exists)
//		{
//			mod.mod_type = DiskMod::kCreateMod;
//			mod.mod_opts = DiskMod::kNoneOpt;
//		}
//		else
//		{
//			mod.mod_type = DiskMod::kDataMetadataMod;
//			mod.mod_opts = DiskMod::kTruncateOpt;
//		}
//
//		mod.path = pathname;
//
//		mods_.push_back(mod);
//	}
//}

//int RecordCmFsOps::CmOpen(const std::wstring& pathname, const int flags)
//{
//	// Will this make a new file or is this path a directory?
//	const bool exists = fns_->FnPathExists(pathname.c_str());
//
//	const int res = fns_->FnOpen(pathname.c_str(), flags);
//	if (res < 0)
//	{
//		return res;
//	}
//
//	CmOpenCommon(res, pathname, exists, flags);
//
//	return res;
//}
//
//int RecordCmFsOps::CmOpen(const std::wstring& pathname, const int flags, const mode_t mode)
//{
//	// Will this make a new file or is this path a directory?
//	const bool exists = fns_->FnPathExists(pathname.c_str());
//
//	const int res = fns_->FnOpen2(pathname.c_str(), flags, mode);
//	if (res < 0)
//	{
//		return res;
//	}
//
//	CmOpenCommon(res, pathname, exists, flags);
//
//	return res;
//}

//off_t RecordCmFsOps::CmLseek(IFileInfo * fd, const off_t offset, const int whence)
//{
////	return fns_->FnLseek(fd, offset, whence);
//	return 0;
//}

#if 0
int RecordCmFsOps::CmWrite(IFileInfo * fd, const void* buf, const size_t count)
{
	THROW_ERROR(ERR_APP, L"unsuppor, using CmPWrite");
	return 0;

	DiskMod mod;
	mod.mod_opts = DiskMod::kNoneOpt;
	// Get current file position and size. If stat fails, then assume lseek will
	// fail too and just bail out.
	struct stat pre_stat_buf;
	// This could be an fstat(), but I don't see a reason to add another call that
	// does only reads to the already large interface of FsFns.
	int res = fns_->FnStat(fd_map_.at(fd), &pre_stat_buf);
	if (res < 0)
	{
		return res;
	}

	mod.file_mod_location = fns_->FnLseek(fd, 0, SEEK_CUR);
	if (mod.file_mod_location < 0)
	{
		return mod.file_mod_location;
	}

	const int write_res = fns_->FnWrite(fd, buf, count);
	if (write_res < 0)
	{
		return write_res;
	}

	mod.directory_mod = S_ISDIR(pre_stat_buf.st_mode);

	// TODO(ashmrtn): Support calling write directly on a directory.
	if (!mod.directory_mod)
	{
		// Copy over as much data as was written and see what the new file size is.
		// This will determine how we set the type of the DiskMod.
		mod.file_mod_len = write_res;
		mod.path = fd_map_.at(fd);

		res = fns_->FnStat(fd_map_.at(fd), &mod.post_mod_stats);
		if (res < 0)
		{
			return write_res;
		}

		if (pre_stat_buf.st_size != mod.post_mod_stats.st_size)
		{
			mod.mod_type = DiskMod::kDataMetadataMod;
		}
		else
		{
			mod.mod_type = DiskMod::kDataMod;
		}

		if (write_res > 0)
		{
			mod.file_mod_data.reset(new char[write_res], [](char* c) {delete[] c; });
			memcpy(mod.file_mod_data.get(), buf, write_res);
		}
	}

	mods_.push_back(mod);

	return write_res;
}
#endif

bool RecordCmFsOps::CmPwrite(IFileInfo * fd, const void* buf, const size_t count, DWORD & written, const off_t offset)
{
	DiskMod mod;
	mod.mod_opts = DiskMod::kNoneOpt;
	// Get current file position and size. If stat fails, then assume lseek will fail too and just bail out.
	//struct stat pre_stat_buf;
	// This could be an fstat(), but I don't see a reason to add another call that
	// does only reads to the already large interface of FsFns.

	//int res = fns_->FnStat(fd_map_.at(fd), &pre_stat_buf);
	//if (res < 0)
	//{
	//	return res;
	//}
	BY_HANDLE_FILE_INFORMATION file_info;
	fd->GetFileInformation(&file_info);
	size_t pre_size = (((size_t)file_info.nFileSizeHigh) << 32) | file_info.nFileIndexLow;

	JCASSERT(fd);
	bool br = fd->DokanWriteFile(buf, boost::numeric_cast<DWORD>(count), written, offset);
	if (!br) return false;
	//const int write_res = fns_->FnPwrite(fd, buf, count, offset);
	//if (write_res < 0)
	//{
	//	return write_res;
	//}
	fd->GetFileInformation(&file_info);
	size_t post_size = (((size_t)file_info.nFileSizeHigh) << 32) | file_info.nFileIndexLow;

//	mod.directory_mod = S_ISDIR(pre_stat_buf.st_mode);
	mod.directory_mod = false;

	// TODO(ashmrtn): Support calling write directly on a directory.
	if (!mod.directory_mod)
	{
		// Copy over as much data as was written and see what the new file size is.
		// This will determine how we set the type of the DiskMod.
		mod.m_file_mod_location = offset;
		mod.m_file_mod_len = written;
//		mod.path = m_fd_map.at(fd);
		mod.path = fd->GetFileName();
		//res = fns_->FnStat(fd_map_.at(fd), &mod.post_mod_stats);
		//if (res < 0)
		//{
		//	return write_res;
		//}

	//	if (pre_stat_buf.st_size != mod.post_mod_stats.st_size)
		if (pre_size != post_size)		{		mod.mod_type = DiskMod::kDataMetadataMod;		}
		else		{			mod.mod_type = DiskMod::kDataMod;		}

		if (written > 0)
		{
			mod.file_mod_data.reset(new BYTE[written], [](BYTE* c) {delete[] c; });
			memcpy_s(mod.file_mod_data.get(), written, buf, written);
		}
	}

	mods_.push_back(mod);

	return true;
}

#if 0
void* RecordCmFsOps::CmMmap(void* addr, const size_t length, const int prot, 
	const int flags, const int fd, const off_t offset)
{

	void* res = fns_->FnMmap(addr, length, prot, flags, fd, offset);
	if (res == (void*)-1)
	{
		return res;
	}

	if (!(prot & PROT_WRITE) || flags & MAP_PRIVATE || flags & MAP_ANON ||
		flags & MAP_ANONYMOUS)
	{
		// In these cases, the user cannot write to the mmap-ed region, the region
		// is not backed by a file, or the changes the user makes are not reflected
		// in the file, so we can just act like this never happened.
		return res;
	}

	// All other cases we actually need to keep track of the fact that we mmap-ed
	// this region.
	mmap_map_.insert({ (long long)res,
		tuple<std::wstring, unsigned int, unsigned int>(
			fd_map_.at(fd), offset, length) });
	return res;
}
#endif

#if 0
int RecordCmFsOps::CmMsync(void* addr, const size_t length, const int flags)
{
	const int res = fns_->FnMsync(addr, length, flags);
	if (res < 0)
	{
		return res;
	}

	// Check which file this belongs to. We need to do a search because they may
	// not have passed the address that was returned in mmap.
	for (const pair<long long, tuple<std::wstring, unsigned int, unsigned int>>& kv :
		mmap_map_)
	{
		if (addr >= (void*)kv.first &&
			addr < (void*)(kv.first + std::get<2>(kv.second)))
		{
			// This is the mapping you're looking for.
			DiskMod mod;
			mod.mod_type = DiskMod::kDataMod;
			mod.mod_opts = (flags & MS_ASYNC) ?
				DiskMod::kMsAsyncOpt : DiskMod::kMsSyncOpt;
			mod.path = std::get<0>(kv.second);
			// Offset into the file is the offset given in mmap plus the how far addr
			// is from the pointer returned by mmap.
			mod.file_mod_location =
				std::get<1>(kv.second) + ((long long)addr - kv.first);
			mod.file_mod_len = length;

			// Copy over the data that is being sync-ed. We don't know how it is
			// different than what was there to start with, but we'll have it!
			mod.file_mod_data.reset(new char[length], [](char* c) {delete[] c; });
			memcpy(mod.file_mod_data.get(), addr, length);

			mods_.push_back(mod);
			break;
		}
	}

	return res;
}

int RecordCmFsOps::CmMunmap(void* addr, const size_t length)
{
	const int res = fns_->FnMunmap(addr, length);
	if (res < 0)
	{
		return res;
	}

	// TODO(ashmrtn): Assume that we always munmap with the same pointer and
	// length that we mmap-ed with. May not actually remove anything if the
	// mapping was not something that caused writes to be reflected in the
	// underlying file (i.e. the key wasn't present to begin with).
	mmap_map_.erase((long long int) addr);

	return res;
}

int RecordCmFsOps::CmFallocate(const int fd, const int mode, const off_t offset,
	off_t len)
{
	struct stat pre_stat;
	const int pre_stat_res = fns_->FnStat(fd_map_[fd].c_str(), &pre_stat);
	if (pre_stat_res < 0)
	{
		return pre_stat_res;
	}

	const int res = fns_->FnFallocate(fd, mode, offset, len);
	if (res < 0)
	{
		return res;
	}

	struct stat post_stat;
	const int post_stat_res = fns_->FnStat(fd_map_[fd].c_str(), &post_stat);
	if (post_stat_res < 0)
	{
		return post_stat_res;
	}

	DiskMod mod;

	if (pre_stat.st_size != post_stat.st_size ||
		pre_stat.st_blocks != post_stat.st_blocks)
	{
		mod.mod_type = DiskMod::kDataMetadataMod;
	}
	else
	{
		mod.mod_type = DiskMod::kDataMod;
	}

	mod.path = fd_map_[fd];
	mod.file_mod_location = offset;
	mod.file_mod_len = len;

	if (mode & FALLOC_FL_PUNCH_HOLE)
	{
		// TODO(ashmrtn): Do we want this here? I'm not sure if it will cause
		// failures that we don't want, though man fallocate(2) says that
		// FALLOC_FL_PUNCH_HOLE must also have FALLOC_FL_KEEP_SIZE.
		assert(mode & FALLOC_FL_KEEP_SIZE);
		mod.mod_opts = DiskMod::kPunchHoleKeepSizeOpt;
	}
	else if (mode & FALLOC_FL_COLLAPSE_RANGE)
	{
		mod.mod_opts = DiskMod::kCollapseRangeOpt;
	}
	else if (mode & FALLOC_FL_ZERO_RANGE)
	{
		if (mode & FALLOC_FL_KEEP_SIZE)
		{
			mod.mod_opts = DiskMod::kZeroRangeKeepSizeOpt;
		}
		else
		{
			mod.mod_opts = DiskMod::kZeroRangeOpt;
		}
		/*
	  } else if (mode & FALLOC_FL_INSERT_RANGE) {
		// TODO(ashmrtn): Figure out how to check with glibc defines.
		mod.mod_opts = DiskMod::kInsertRangeOpt;
		*/
	}
	else if (mode & FALLOC_FL_KEEP_SIZE)
	{
		mod.mod_opts = DiskMod::kFallocateKeepSizeOpt;
	}
	else
	{
		mod.mod_opts = DiskMod::kFallocateOpt;
	}

	mods_.push_back(mod);

	return res;
}
#endif

int RecordCmFsOps::CmClose(IFileInfo* fd)
{
	JCASSERT(fd);
	fd->CloseFile();
	m_fd_map.erase(fd);
	return 0;
}

int RecordCmFsOps::CmRename(const std::wstring& old_path, const std::wstring& new_path)
{
	// check if there are any open files with the old path change the file descriptors to point to the new path
	for (auto it = m_fd_map.begin(); it != m_fd_map.end(); it++)
	{
		std::wstring& open_fd_old_path = it->second;
		if (open_fd_old_path.compare(old_path) == 0)
		{
			JCASSERT(0);
			//<YUAN> 尝试it->second是否可以改变数据。如果可以的话，可以优化。减少一次查找
			it->second = new_path;
			LOG_DEBUG(L"new path = %s", m_fd_map[it->first].c_str());
//			m_fd_map[it->first] = new_path
			continue;
		}
		// if we are renaming a directory that is open; we want to change the mapping of the open files in that directory
		auto found = open_fd_old_path.find(old_path);
		if (found != std::wstring::npos)
		{
			m_fd_map[it->first].replace(found, old_path.length(), new_path);
		}
	}
	bool br = m_fs->DokanMoveFile(old_path, new_path, false, NULL);
	if (!br) return -1;
	else return 0;
}

int RecordCmFsOps::CmUnlink(const std::wstring& pathname)
{
	bool br = m_fs->Unlink(pathname);
	if (!br)	return -1;

	DiskMod mod;
	mod.mod_type = DiskMod::kRemoveMod;
	mod.mod_opts = DiskMod::kNoneOpt;
	mod.path = pathname;
	mods_.push_back(mod);
	return 0;
}

bool RecordCmFsOps::CmRemove(const std::wstring& pathname)
{
	bool br = m_fs->DokanDeleteFile(pathname, NULL, false);
	if (!br) return false;

	DiskMod mod;
	mod.mod_type = DiskMod::kRemoveMod;
	mod.mod_opts = DiskMod::kNoneOpt;
	mod.path = pathname;
	mods_.push_back(mod);

	return true;
}

bool RecordCmFsOps::CmFsync(IFileInfo* fd)
{
	bool br = fd->FlushFile();
	if (!br) return false;

	DiskMod mod;
	mod.mod_type = DiskMod::kFsyncMod;
	mod.mod_opts = DiskMod::kNoneOpt;
//	mod.path = m_fd_map.at(fd);
	mod.path = fd->GetFileName();
	mods_.push_back(mod);
	return true;
}

#if 0
int RecordCmFsOps::CmFdatasync(const int fd)
{
	const int res = fns_->FnFdatasync(fd);
	if (res < 0)
	{
		return res;
	}

	DiskMod mod;
	mod.mod_type = DiskMod::kFsyncMod;
	mod.mod_opts = DiskMod::kNoneOpt;
	mod.path = fd_map_.at(fd);
	mods_.push_back(mod);

	return res;
}
#endif

void RecordCmFsOps::CmSync()
{
	m_fs->Sync();

	DiskMod mod;
	mod.mod_type = DiskMod::kSyncMod;
	mod.mod_opts = DiskMod::kNoneOpt;
	mods_.push_back(mod);
}

// int RecordCmFsOps::CmSyncfs(const int fd) {
//   const int res = fns_->FnSyncfs(fd);
//   if (res < 0) {
//     return res;
//   }

//   DiskMod mod;
//   // Or should probably have a kSyncMod type with filepath (?)
//   mod.mod_type = DiskMod::kFsyncMod;
//   mod.mod_opts = DiskMod::kNoneOpt;
//   mod.path = fd_map_.at(fd);
//   mods_.push_back(mod);

//   return res;
// }

#if 0
int RecordCmFsOps::CmSyncFileRange(const int fd, size_t offset, size_t nbytes,
	unsigned int flags)
{
	const int res = fns_->FnSyncFileRange(fd, offset, nbytes, flags);
	if (res < 0)
	{
		return res;
	}

	DiskMod mod;
	mod.mod_type = DiskMod::kSyncFileRangeMod;
	mod.mod_opts = DiskMod::kNoneOpt;
	mod.path = fd_map_.at(fd);
	const int post_stat_res = fns_->FnStat(fd_map_.at(fd), &mod.post_mod_stats);
	if (post_stat_res < 0)
	{
		// TODO(ashmrtn): Some sort of warning here?
		return post_stat_res;
	}
	mod.file_mod_location = offset;
	mod.file_mod_len = nbytes;
	mods_.push_back(mod);
	return res;
}
#endif

int RecordCmFsOps::CmCheckpoint(const wchar_t* msg)
{
	DiskMod mod;
	mod.mod_type = DiskMod::kCheckpointMod;
	mod.mod_opts = DiskMod::kNoneOpt;
	mods_.push_back(mod);
	return 0;
}

int RecordCmFsOps::WriteWhole(FILE * fd, const unsigned long long size, shared_ptr<BYTE> data)
{
	unsigned long long written = 0;
	//<YUAN> fwrite以element size为单位写入，确保写入完成
	size_t ir = fwrite(data.get() + written, size-written, 1, fd);
	if (ir != 1) return -1;
//
//	while (written < size)
//	{
//		size_t ir = fwrite(data.get() + written, size-written, 1, fd);
////		const int res = write(fd, data.get() + written, size - written);
//		if (ir <= 0) return -1;
//		written += ir;
//	}
	return 0;
}

int RecordCmFsOps::Serialize(FILE* fd)
{
#ifdef _DEBUG
	int index = 0;
	LOG_DEBUG(L"Serialize ops, size=%d", mods_.size());
#endif
	for (auto& mod : mods_)
	{
		LOG_DEBUG(L"serialize #%d", index);
		unsigned long long size;
		shared_ptr<BYTE> serial_mod = mod.Serialize(/*mod,*/ &size);
		if (serial_mod == nullptr)	{	return -1;	}

		const int res = WriteWhole(fd, size, serial_mod);
		if (res < 0)	{	return -1;		}
		index++;
	}

	return 0;
}


///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// ==== PassthroughCmFsOps ====

PassthroughCmFsOps::PassthroughCmFsOps(IFileSystem* fs)
{
	if (fs == NULL) THROW_ERROR(ERR_APP, L"file system cannot be null");
	m_fs = fs;
	m_fs->AddRef();
}

int PassthroughCmFsOps::CmMknod(const std::wstring& pathname, const mode_t mode, const dev_t dev)
{
	THROW_ERROR(ERR_APP, L"unsuppor");
	return 0;
}

int PassthroughCmFsOps::CmMkdir(const std::wstring& pathname, const mode_t mode)
{
	bool br = m_fs->MakeDir(pathname.c_str());
	return br;
}

//int PassthroughCmFsOps::CmOpen(const std::wstring& pathname, const int flags)
//{
//	return fns_->FnOpen(pathname.c_str(), flags);
//}
//
//int PassthroughCmFsOps::CmOpen(const std::wstring& pathname, const int flags,
//	const mode_t mode)
//{
//	return fns_->FnOpen2(pathname.c_str(), flags, mode);
//}

bool fs_testing::user_tools::api::PassthroughCmFsOps::CmCreateFile(IFileInfo*& file, const std::wstring& fn, 
	ACCESS_MASK access_mask, DWORD attr, IFileSystem::FsCreateDisposition disp, ULONG share, ULONG opt, bool isdir)
{
	JCASSERT(m_fs);
	return m_fs->DokanCreateFile(file, fn, access_mask, attr, disp, share, opt, isdir);
}

off_t PassthroughCmFsOps::CmLseek(IFileInfo* fd, const off_t offset,	const int whence)
{
	return 0;
}

int PassthroughCmFsOps::CmWrite(IFileInfo* fd, const void* buf, const size_t count)
{
	THROW_ERROR(ERR_APP, L"unsuppor, using CmPWrite");
	return 0;
}

bool PassthroughCmFsOps::CmPwrite(IFileInfo* fd, const void* buf, const size_t count, DWORD & written, const off_t offset)
{
	JCASSERT(fd);
	return fd->DokanWriteFile(buf, boost::numeric_cast<DWORD>(count), written, offset);
}

void* PassthroughCmFsOps::CmMmap(void* addr, const size_t length, const int prot, const int flags, const int fd, 
	const off_t offset)
{
	THROW_ERROR(ERR_APP, L"unsuppor");
	return 0;
}

int PassthroughCmFsOps::CmMsync(void* addr, const size_t length,
	const int flags)
{
	THROW_ERROR(ERR_APP, L"unsuppor");
	return 0;
}

int PassthroughCmFsOps::CmMunmap(void* addr, const size_t length)
{
	THROW_ERROR(ERR_APP, L"unsuppor");
	return 0;
}

int PassthroughCmFsOps::CmFallocate(const int fd, const int mode, const off_t offset, off_t len)
{
	THROW_ERROR(ERR_APP, L"unsuppor");
	return 0;
}

int PassthroughCmFsOps::CmClose(IFileInfo* fd)
{
	JCASSERT(fd);
	fd->CloseFile();
	return 0;
}

int PassthroughCmFsOps::CmRename(const std::wstring& old_path, const std::wstring& new_path)
{
	bool br = m_fs->DokanMoveFile(old_path, new_path, false, NULL);
	if (!br) return -1;
	else return 0;
}

int PassthroughCmFsOps::CmUnlink(const std::wstring& pathname)
{
	m_fs->Unlink(pathname);
	return 0;
}

bool PassthroughCmFsOps::CmRemove(const std::wstring& pathname)
{
	bool br = m_fs->DokanDeleteFile(pathname, NULL, false);
	return br;
}

bool PassthroughCmFsOps::CmFsync(IFileInfo* fd)
{
	bool br = fd->FlushFile();
	return br;
}

int PassthroughCmFsOps::CmFdatasync(IFileInfo* fd)
{
	THROW_ERROR(ERR_APP, L"unsuppor");
	return 0;
}

void PassthroughCmFsOps::CmSync()
{
	m_fs->Sync();
}

// int PassthroughCmFsOps::CmSyncfs(const int fd) {
//   return fns_->FnSyncfs(fd);
// }

int PassthroughCmFsOps::CmSyncFileRange(IFileInfo* fd, size_t offset, size_t nbytes, unsigned int flags)
{
	THROW_ERROR(ERR_APP, L"unsuppor");
	return 0;
}

int PassthroughCmFsOps::CmCheckpoint(const wchar_t* msg)
{
	return 0;
}

//} // api
//} // user_tools
//} // fs_testing

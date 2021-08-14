#ifndef USER_TOOLS_API_WRAPPER_H
#define USER_TOOLS_API_WRAPPER_H

//#include <fcntl.h>
//#include <stdio.h>
//#include <sys/mman.h>
//#include <sys/stat.h>
//#include <sys/types.h>
//#include <unistd.h>
#include <dokanfs-lib.h>

#include <memory>
//#include <wstring>
#include <tuple>
#include <unordered_map>
#include <vector>

#include "DiskMod.h"

typedef int mode_t;
typedef int64_t ssize_t;

namespace fs_testing {
	namespace user_tools {
		namespace api {
#if 0	//<YUAN>由于IFileSystem提供同意接口，不需要FsFns做wrapper层
			// So that things can be tested in a somewhat reasonable fashion by swapping out the functions called.
			class FsFns
			{
			public:
				virtual ~FsFns() {};
				virtual int FnMknod(const std::wstring& pathname, mode_t mode, dev_t dev) = 0;
				virtual int FnMkdir(const std::wstring& pathname, mode_t mode) = 0;
				virtual int FnOpen(const std::wstring& pathname, int flags) = 0;
				virtual int FnOpen2(const std::wstring& pathname, int flags, mode_t mode) = 0;
				virtual off_t FnLseek(int fd, off_t offset, int whence) = 0;
				virtual ssize_t FnWrite(int fd, const void* buf, size_t count) = 0;
				virtual ssize_t FnPwrite(int fd, const void* buf, size_t count, off_t offset) = 0;
				virtual void* FnMmap(void* addr, size_t length, int prot, int flags, int fd, off_t offset) = 0;
				virtual int FnMsync(void* addr, size_t length, int flags) = 0;
				virtual int FnMunmap(void* addr, size_t length) = 0;
				virtual int FnFallocate(int fd, int mode, off_t offset, off_t len) = 0;
				virtual int FnClose(int fd) = 0;
				virtual int FnRename(const std::wstring& old_path, const std::wstring& new_path) = 0;
				virtual int FnUnlink(const std::wstring& pathname) = 0;
				virtual int FnRemove(const std::wstring& pathname) = 0;

				virtual int FnStat(const std::wstring& pathname, struct stat* buf) = 0;
				virtual bool FnPathExists(const std::wstring& pathname) = 0;

				virtual int FnFsync(const int fd) = 0;
				virtual int FnFdatasync(const int fd) = 0;
				virtual void FnSync() = 0;
				// TODO(P.S.) check if we want to have syncfs
				// virtual int FnSyncfs(const int fd) = 0;
				virtual int FnSyncFileRange(const int fd, size_t offset, size_t nbytes, unsigned int flags) = 0;
				virtual int CmCheckpoint() = 0;
			};

			class DefaultFsFns : public FsFns
			{
			public:
				virtual int FnMknod(const std::wstring& pathname, mode_t mode, dev_t dev) override;
				virtual int FnMkdir(const std::wstring& pathname, mode_t mode) override;
				virtual int FnOpen(const std::wstring& pathname, int flags) override;
				virtual int FnOpen2(const std::wstring& pathname, int flags, mode_t mode) override;
				virtual off_t FnLseek(int fd, off_t offset, int whence) override;
				virtual ssize_t FnWrite(int fd, const void* buf, size_t count) override;
				virtual ssize_t FnPwrite(int fd, const void* buf, size_t count, off_t offset) override;
				virtual void* FnMmap(void* addr, size_t length, int prot, int flags, int fd, off_t offset) override;
				virtual int FnMsync(void* addr, size_t length, int flags) override;
				virtual int FnMunmap(void* addr, size_t length) override;
				virtual int FnFallocate(int fd, int mode, off_t offset, off_t len) override;
				virtual int FnClose(int fd) override;
				virtual int FnRename(const std::wstring& old_path, const std::wstring& new_path);
				virtual int FnUnlink(const std::wstring& pathname) override;
				virtual int FnRemove(const std::wstring& pathname) override;

				virtual int FnStat(const std::wstring& pathname, struct stat* buf) override;
				virtual bool FnPathExists(const std::wstring& pathname) override;

				virtual int FnFsync(const int fd) override;
				virtual int FnFdatasync(const int fd) override;
				virtual void FnSync() override;
				// TODO(P.S.) check if we want to have syncfs
				// virtual int FnSyncfs(const int fd) override;
				virtual int FnSyncFileRange(const int fd, size_t offset, size_t nbytes, unsigned int flags) override;
				virtual int CmCheckpoint() override;
			};
#endif

			class CmFsOps
			{
			public:
				virtual ~CmFsOps() {};

				virtual int CmMknod(const std::wstring& pathname, const mode_t mode, const dev_t dev) = 0;
				virtual int CmMkdir(const std::wstring& pathname, const mode_t mode) = 0;
				//virtual int CmOpen(const std::wstring& pathname, const int flags) = 0;
				//virtual int CmOpen(const std::wstring& pathname, const int flags, const mode_t mode) = 0;
				virtual bool CmCreateFile(IFileInfo*& file, const std::wstring& fn, ACCESS_MASK access_mask,
					DWORD attr, IFileSystem::FsCreateDisposition disp, ULONG share, ULONG opt, bool isdir) = 0;
				virtual off_t CmLseek(IFileInfo * fd, const off_t offset, const int whence) = 0;
				virtual int CmWrite(IFileInfo * fd, const void* buf, const size_t count) = 0;
				virtual bool CmPwrite(IFileInfo * fd, const void* buf, const size_t count, DWORD &written, 
					const off_t offset) = 0;
				virtual void* CmMmap(void* addr, const size_t length, const int prot, const int flags, const int fd, const off_t offset) = 0;
				virtual int CmMsync(void * addr, const size_t length, const int flags) = 0;
				virtual int CmMunmap(void* addr, const size_t length) = 0;
				virtual int CmFallocate(const int fd, const int mode, const off_t offset, off_t len) = 0;
				virtual int CmClose(IFileInfo * fd) = 0;
				virtual int CmRename(const std::wstring& old_path, const std::wstring& new_path) = 0;
				virtual int CmUnlink(const std::wstring& pathname) = 0;
				virtual bool CmRemove(const std::wstring& pathname) = 0;

				virtual bool CmFsync(IFileInfo * fd) = 0;
				virtual int CmFdatasync(IFileInfo* fd) = 0;
				virtual void CmSync() = 0;
				// TODO(P.S.) check if we want to have syncfs
				// virtual int CmSyncfs(const int fd) = 0;
				virtual int CmSyncFileRange(IFileInfo* fd, size_t offset, size_t nbytes, unsigned int flags) = 0;
				virtual int CmCheckpoint(const wchar_t * msg) = 0;
			};

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// ==== PassthroughCmFsOps ====
// Provides an interface that will record all the changes the user makes to the file system.
			/*
			 * Provides just a passthrough to the actual system calls. This is good for
			 * situations where we don't want to record the individual changes a user makes
			 * to the file system.
			 */
			class PassthroughCmFsOps : public CmFsOps
			{
			public:
				PassthroughCmFsOps(IFileSystem* functions);
				virtual ~PassthroughCmFsOps() { RELEASE(m_fs); };

				virtual int CmMknod(const std::wstring& pathname, const mode_t mode,
					const dev_t dev);
				virtual int CmMkdir(const std::wstring& pathname, const mode_t mode);
				//virtual int CmOpen(const std::wstring& pathname, const int flags);
				//virtual int CmOpen(const std::wstring& pathname, const int flags,	const mode_t mode);
				virtual bool CmCreateFile(IFileInfo*& file, const std::wstring& fn, ACCESS_MASK access_mask,
					DWORD attr, IFileSystem::FsCreateDisposition disp, ULONG share, ULONG opt, bool isdir);

				virtual off_t CmLseek(IFileInfo* fd, const off_t offset, const int whence);
				virtual int CmWrite(IFileInfo* fd, const void* buf, const size_t count);
				virtual bool CmPwrite(IFileInfo* fd, const void* buf, const size_t count, DWORD & written, 
					const off_t offset);
				virtual void* CmMmap(void* addr, const size_t length, const int prot, const int flags, const int fd, 
					const off_t offset);
				virtual int CmMsync(void* addr, const size_t length, const int flags);
				virtual int CmMunmap(void* addr, const size_t length);
				virtual int CmFallocate(const int fd, const int mode, const off_t offset, off_t len);
				virtual int CmClose(IFileInfo* fd);
				virtual int CmRename(const std::wstring& old_path, const std::wstring& new_path);
				virtual int CmUnlink(const std::wstring& pathname);
				virtual bool CmRemove(const std::wstring& pathname);

				virtual bool CmFsync(IFileInfo* fd);
				virtual int CmFdatasync(IFileInfo* fd);
				virtual void CmSync();
				// TODO(P.S.) check if we want to have syncfs
				// virtual int CmSyncfs(const int fd);
				virtual int CmSyncFileRange(IFileInfo* fd, size_t offset, size_t nbytes, unsigned int flags);

				virtual int CmCheckpoint(const wchar_t* msg);

			protected:
				// Set of functions to call for different file system operations. Tracked as a
				// set of function pointers so that this class can be tested in a somewhat sane manner.
//				FsFns* fns_;
				IFileSystem* m_fs;
			};
			///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
			// ==== RecordCmFsOps ====
			// Provides an interface that will record all the changes the user makes to the file system.
			class RecordCmFsOps : public PassthroughCmFsOps
			{
			public:
				//				RecordCmFsOps(FsFns* functions);
				RecordCmFsOps(IFileSystem* fs);
				//virtual ~RecordCmFsOps() { RELEASE(m_fs); };

				//int CmMknod(const std::wstring& pathname, const mode_t mode, const dev_t dev);
				virtual int CmMkdir(const std::wstring& pathname, const mode_t mode);
				//int CmOpen(const std::wstring& pathname, const int flags);
				//int CmOpen(const std::wstring& pathname, const int flags, const mode_t mode);
				virtual bool CmCreateFile(IFileInfo*& file, const std::wstring& fn, ACCESS_MASK access_mask,
					DWORD attr, IFileSystem::FsCreateDisposition disp, ULONG share, ULONG opt, bool isdir);

				//off_t CmLseek(IFileInfo* fd, const off_t offset, const int whence);
				//int CmWrite(IFileInfo* fd, const void* buf, const size_t count);
				virtual bool CmPwrite(IFileInfo* fd, const void* buf, const size_t count, DWORD& written, 
					const off_t offset);
				//void* CmMmap(void* addr, const size_t length, const int prot, const int flags, const int fd, 
				//	const off_t offset);
				//int CmMsync(void* addr, const size_t length, const int flags);
				//int CmMunmap(void* addr, const size_t length);
				//int CmFallocate(const int fd, const int mode, const off_t offset, off_t len);
				virtual int CmClose(IFileInfo* fd);
				virtual int CmRename(const std::wstring& old_path, const std::wstring& new_path);
				virtual int CmUnlink(const std::wstring& pathname);
				virtual bool CmRemove(const std::wstring& pathname);

				virtual bool CmFsync(IFileInfo* fd);
				//int CmFdatasync(const int fd);
				virtual void CmSync();
				// TODO(P.S.) check if we want to have syncfs
				// int CmSyncfs(const int fd);
				//virtual int CmSyncFileRange(IFileInfo* fd, size_t offset, size_t nbytes, unsigned int flags);

				virtual int CmCheckpoint(const wchar_t* msg);

				int Serialize(FILE* fd);


				// Protected for testing purposes.
			protected:
				// So that things that require fd can be mapped to pathnames.
//				std::unordered_map<int, std::wstring> fd_map_;
				// 由于IFileInfo是文件对象指针，关闭打开文件，可能导致指针变化
				//	尝试使用IFileInfo::GetName()替换
				std::unordered_map<IFileInfo*, std::wstring> m_fd_map;

				// So that mmap pointers can be mapped to pathnames and mmap offset and length.
				std::unordered_map<long long, std::tuple<std::wstring, unsigned long long, unsigned long long>> mmap_map_;
				std::vector<fs_testing::utils::DiskMod> mods_;

				// Set of functions to call for different file system operations. Tracked as a
				// set of function pointers so that this class can be tested in a somewhat sane manner.
//				FsFns* fns_;
				//IFileSystem* m_fs;

			private:
				// Common code for open with 2 and 3 arguments.
				//void CmOpenCommon(const int fd, const std::wstring& pathname, const bool exists, const int flags);

				// Write data out to the given file descriptor. Automatically retires until all the requested data is
				//	written.
				int WriteWhole(FILE * fd, const unsigned long long size, std::shared_ptr<wchar_t> data);
			};
		} // api
	} // user_tools
} // fs_testing

#endif // USER_TOOLS_API_WRAPPER_H

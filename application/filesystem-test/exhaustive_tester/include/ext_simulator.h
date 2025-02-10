///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "fs_simulator.h"
#include "storage.h"
#include "pages.h"


class CExtSimulator : public IFsSimulator
{
protected:
	CExtSimulator(void);
	virtual ~CExtSimulator(void);


public:
	virtual void add_ref(void) = 0;
	virtual void release(void) = 0;
	// 文件系统初始化
	virtual bool Initialzie(const boost::property_tree::wptree& config, const std::wstring& log_path) = 0;
	// 获取文件系统的配置
	virtual void GetConfig(boost::property_tree::wptree& config, const std::wstring& config_name) = 0;
	virtual bool Mount(void) = 0;
	virtual bool Unmount(void) = 0;
	virtual bool Reset(UINT rollback) = 0;
	virtual ERROR_CODE fsck(bool fix) = 0;


	// 文件系统基本操作
	virtual ERROR_CODE  FileCreate(_NID& fid, const std::string& fn) = 0;
	virtual ERROR_CODE  DirCreate(_NID& fid, const std::string& fn) = 0;
	virtual ERROR_CODE  FileOpen(_NID& fid, const std::string& fn, bool delete_on_close = false) = 0;
	virtual void FileClose(_NID fid) = 0;
	// 设置和获取文件大小，以sector为单位。
	virtual void SetFileSize(_NID fid, FSIZE secs) = 0;
	virtual FSIZE GetFileSize(_NID fid) = 0;
	// 返回实际写入的 byte
	virtual FSIZE FileWrite(_NID fid, FSIZE offset, FSIZE secs) = 0;
	// 返回读取到的 page数量
	virtual size_t FileRead(FILE_DATA blks[], _NID fid, FSIZE offset, FSIZE secs) = 0;
	virtual void FileTruncate(_NID fid, FSIZE offset, FSIZE secs) = 0;
	// delete 根据文件名删除文件。FileRemove()根据ID，删除文件相关内容，但是不删除path map
	virtual void FileDelete(const std::string& fn) = 0;
	virtual ERROR_CODE DirDelete(const std::string& fn) = 0;
	virtual void FileFlush(_NID fid) = 0;
	// 文件能够支持的最大长度（block单位）
	virtual DWORD MaxFileSize(void) const = 0;
	virtual void GetFsInfo(FS_INFO& space_info) = 0;

	// 测试支持
	// virtual bool CopyFrom(IFsSimulator* src) = 0;
	virtual void Clone(IFsSimulator*& dst) = 0;
	virtual void CopyFrom(const IFsSimulator* src) = 0;
	virtual void GetHealthInfo(FsHealthInfo& info) const = 0;
	// 用于调试，不需要打开文件。size：文件大小。node block：包括inode在内，index block数量；data_blk：实际占用block数量
	virtual void GetFileInfo(_NID fid, FSIZE& size, FSIZE& node_blk, FSIZE& data_blk) = 0;

	// 对storage，用于storage相关测试
	virtual UINT GetCacheNum(void) = 0;
	virtual void GetFileDirNum(_NID fid, UINT& file_nr, UINT& dir_nr) = 0;

	// for debug

	virtual void DumpSegments(const std::wstring& fn, bool sanity_check) = 0;
	virtual void DumpSegmentBlocks(const std::wstring& fn) = 0;
	virtual void DumpFileMap(FILE* out, _NID fid) = 0;
	virtual void DumpAllFileMap(const std::wstring& fn) = 0;
	virtual void DumpBlockWAF(const std::wstring& fn) = 0;
	virtual size_t DumpFileIndex(_NID index[], size_t buf_size, _NID fid) = 0;

	virtual void GetGcTrace(std::vector<GC_TRACE>&) = 0;
};

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
	// �ļ�ϵͳ��ʼ��
	virtual bool Initialzie(const boost::property_tree::wptree& config, const std::wstring& log_path) = 0;
	// ��ȡ�ļ�ϵͳ������
	virtual void GetConfig(boost::property_tree::wptree& config, const std::wstring& config_name) = 0;
	virtual bool Mount(void) = 0;
	virtual bool Unmount(void) = 0;
	virtual bool Reset(UINT rollback) = 0;
	virtual ERROR_CODE fsck(bool fix) = 0;


	// �ļ�ϵͳ��������
	virtual ERROR_CODE  FileCreate(NID& fid, const std::string& fn) = 0;
	virtual ERROR_CODE  DirCreate(NID& fid, const std::string& fn) = 0;
	virtual ERROR_CODE  FileOpen(NID& fid, const std::string& fn, bool delete_on_close = false) = 0;
	virtual void FileClose(NID fid) = 0;
	// ���úͻ�ȡ�ļ���С����sectorΪ��λ��
	virtual void SetFileSize(NID fid, FSIZE secs) = 0;
	virtual FSIZE GetFileSize(NID fid) = 0;
	// ����ʵ��д��� byte
	virtual FSIZE FileWrite(NID fid, FSIZE offset, FSIZE secs) = 0;
	// ���ض�ȡ���� page����
	virtual size_t FileRead(FILE_DATA blks[], NID fid, FSIZE offset, FSIZE secs) = 0;
	virtual void FileTruncate(NID fid, FSIZE offset, FSIZE secs) = 0;
	// delete �����ļ���ɾ���ļ���FileRemove()����ID��ɾ���ļ�������ݣ����ǲ�ɾ��path map
	virtual void FileDelete(const std::string& fn) = 0;
	virtual ERROR_CODE DirDelete(const std::string& fn) = 0;
	virtual void FileFlush(NID fid) = 0;
	// �ļ��ܹ�֧�ֵ���󳤶ȣ�block��λ��
	virtual DWORD MaxFileSize(void) const = 0;
	virtual void GetFsInfo(FS_INFO& space_info) = 0;

	// ����֧��
	// virtual bool CopyFrom(IFsSimulator* src) = 0;
	virtual void Clone(IFsSimulator*& dst) = 0;
	virtual void CopyFrom(const IFsSimulator* src) = 0;
	virtual void GetHealthInfo(FsHealthInfo& info) const = 0;
	// ���ڵ��ԣ�����Ҫ���ļ���size���ļ���С��node block������inode���ڣ�index block������data_blk��ʵ��ռ��block����
	virtual void GetFileInfo(NID fid, FSIZE& size, FSIZE& node_blk, FSIZE& data_blk) = 0;

	// ��storage������storage��ز���
	virtual UINT GetCacheNum(void) = 0;
	virtual void GetFileDirNum(NID fid, UINT& file_nr, UINT& dir_nr) = 0;

	// for debug

	virtual void DumpSegments(const std::wstring& fn, bool sanity_check) = 0;
	virtual void DumpSegmentBlocks(const std::wstring& fn) = 0;
	virtual void DumpFileMap(FILE* out, NID fid) = 0;
	virtual void DumpAllFileMap(const std::wstring& fn) = 0;
	virtual void DumpBlockWAF(const std::wstring& fn) = 0;
	virtual size_t DumpFileIndex(NID index[], size_t buf_size, NID fid) = 0;

	virtual void GetGcTrace(std::vector<GC_TRACE>&) = 0;
};

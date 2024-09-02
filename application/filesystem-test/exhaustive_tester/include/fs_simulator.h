///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
#pragma once

#include <stdext.h>
#include <boost/property_tree/ptree.hpp>
#include <vector>
#include "reference_fs.h"
#include "config.h"

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// == pages  ==
#define INVALID_BLK		(0xFFFFFFFF)
#define NID_IN_USE		(0xFFFFFFF0)
#define INVALID_FID		(0xFFFF)
#define BLOCK_SIZE		(512)			// ��Ĵ�С

//typedef UINT FSIZE;

//typedef DWORD FID;
typedef DWORD NID;		// node id
typedef DWORD PHY_BLK;
typedef DWORD LBLK_T;
typedef UINT	PAGE_INDEX;

enum BLK_TEMP
{
	BT_COLD_DATA = 0, BT_COLD_NODE = 1,
	BT_WARM_DATA = 2, BT_WARM_NODE = 3,
	BT_HOT__DATA = 4, BT_HOT__NODE = 5,
	BT_TEMP_NR
};

struct FILE_DATA
{
	NID		fid;
	UINT	offset;
	UINT	ver;
};

/// <summary>
/// �����ļ�ϵͳ������״̬��ͨ�����⣨$health���ļ���ȡ
/// </summary>
struct FsHealthInfo
{
	UINT m_seg_nr;	// �ܵ�segment����
	UINT m_blk_nr;	// �ܵ�block����
	UINT m_logical_blk_nr;			// �߼������ǡ�makefsʱ������߼�������
//	UINT m_free_seg;	// free segment�����е�����segmeng������GC���ж�
	UINT m_free_blk;		// �����߼�������������һ����ȷ��ֵ����ʼֵΪ������߼����Ͷȡ�������д��ʱ�򣬿��ܵ��¸�����

	LONG64 m_total_host_write;	// �Կ�Ϊ��λ��host��д������������Ĵ�С�ɸ����ļ�ϵͳ������һ��Ϊ4KB��
	LONG64 m_total_media_write;	// д����ʵ�������������blockΪ��λ
	//LONG64 m_media_write_node;
	//LONG64 m_media_write_data;

	UINT m_logical_saturation;	// �߼����Ͷȡ���д�����߼���������������metadata
	UINT m_physical_saturation;	// �����Ͷȡ���Ч�������������

	UINT m_node_nr;		// nid, direct node������
	UINT m_used_node;	// ��ʹ�õ�node����
	UINT m_file_num, m_dir_num;		// �ļ�������Ŀ¼����
};


struct FS_INFO
{
	FSIZE total_seg, free_seg;
	FSIZE total_blks;
	FSIZE used_blks;	// �߼����Ͷȣ���Чblock������
	FSIZE free_blks;
	FSIZE physical_blks;	// �����Ͷ�

	UINT max_file_nr;	// ���֧�ֵ��ļ�����
//	UINT created_files;	// Ŀǰ�ļ�ϵͳ���ɶ��ٸ��ļ�������Ŀ¼����Ŀ¼
	UINT dir_nr, file_nr;		// Ŀ¼�������ļ�����
	LONG64 total_host_write;
	LONG64 total_media_write;

	UINT total_page_nr;
	UINT free_page_nr;
	UINT total_data_nr;
	UINT free_data_nr;
};

struct GC_TRACE
{
	NID fid;
	UINT offset;
	PHY_BLK org_phy;
	PHY_BLK new_phy;
};

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// == interface  ==


class IFsSimulator //: public IJCInterface
{
public:
	virtual ~IFsSimulator(void) {}
	// �ļ�ϵͳ��ʼ��
	virtual bool Initialzie(const boost::property_tree::wptree& config, const std::wstring & log_path) = 0;
		// ��ȡ�ļ�ϵͳ������
	virtual void GetConfig(boost::property_tree::wptree& config, const std::wstring& config_name) = 0;
	virtual bool Mount(void) = 0;
	virtual bool Unmount(void) = 0;
	virtual bool Reset(void) = 0;

	// �ļ�ϵͳ��������
//	virtual NID  FileCreate(const std::wstring& fn) = 0;
	virtual NID  FileCreate(const std::string& fn) = 0;
	virtual NID  DirCreate(const std::string& fn) = 0;
	virtual NID  FileOpen(const std::string& fn, bool delete_on_close = false) = 0;
	virtual void FileClose(NID fid) = 0;
	// ���úͻ�ȡ�ļ���С����sectorΪ��λ��
	virtual void SetFileSize(NID fid, FSIZE secs) = 0;
	virtual FSIZE GetFileSize(NID fid) = 0;

	virtual void FileWrite(NID fid, FSIZE offset, FSIZE secs) = 0;
	// ���ض�ȡ���� page����
	virtual size_t FileRead(FILE_DATA blks[], NID fid, FSIZE offset, FSIZE secs) = 0;
	virtual void FileTruncate(NID fid, FSIZE offset, FSIZE secs) = 0;
	// delete �����ļ���ɾ���ļ���FileRemove()����ID��ɾ���ļ�������ݣ����ǲ�ɾ��path map
	virtual void FileDelete(const std::string & fn) = 0;		
	virtual void FileFlush(NID fid) = 0;
	// �ļ��ܹ�֧�ֵ���󳤶ȣ�block��λ��
	virtual DWORD MaxFileSize(void) const = 0;
	virtual void GetFsInfo(FS_INFO& space_info) = 0;
	
	// ����֧��
	// virtual bool CopyFrom(IFsSimulator* src) = 0;
	virtual void Clone(IFsSimulator*& dst) = 0;
	virtual void CopyFrom(const IFsSimulator* src) = 0;
	virtual void GetHealthInfo(FsHealthInfo& info) const = 0;

	// for debug
	virtual void DumpSegments(const std::wstring& fn, bool sanity_check) = 0;
	virtual void DumpSegmentBlocks(const std::wstring& fn) = 0;
	virtual void DumpFileMap(FILE* out, NID fid) = 0;
	virtual void DumpAllFileMap(const std::wstring& fn) = 0;
	virtual void DumpBlockWAF(const std::wstring& fn) = 0;
	virtual size_t DumpFileIndex(NID index[], size_t buf_size, NID fid) = 0;

	virtual void GetGcTrace(std::vector<GC_TRACE>&) = 0;


};
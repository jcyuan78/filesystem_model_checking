///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
#pragma once

#include <stdext.h>
#include <boost/property_tree/ptree.hpp>
#include <vector>
#include "reference_fs.h"

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// == pages  ==
#define INVALID_BLK		0xFFFFFFFF
#define BLOCK_SIZE				(512)			// ��Ĵ�С

//typedef UINT FSIZE;

typedef DWORD FID;
typedef DWORD NID;		// node id
typedef DWORD PHY_BLK;
typedef DWORD LBLK_T;

enum BLK_TEMP
{
	BT_COLD_DATA = 0, BT_COLD_NODE = 1,
	BT_WARM_DATA = 2, BT_WARM_NODE = 3,
	BT_HOT__DATA = 4, BT_HOT__NODE = 5,
	BT_TEMP_NR
};


class CPageInfo
{
public:
	void Init();

public:
	PHY_BLK phy_blk = INVALID_BLK;	// page��������λ��
	// ���page���¶ȣ���page��д��SSDʱ���¡�����¶Ȳ���ʵ�ʷ��䵽�¶ȣ������㷨�¶���ͬ��������ͳ�ơ�
	BLK_TEMP ttemp;
	//���ļ��е�λ��
	NID	inode;
	LBLK_T offset = INVALID_BLK;
	// ����(����inode ���� direct node)
	UINT data_index;	// ָ�����ݻ����������
	bool dirty = false;
	enum PAGE_TYPE { PAGE_DATA, PAGE_NODE } type;
public:
	// ��������ͳ��
	UINT host_write = 0;
	UINT media_write = 0;
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
//	int m_free_blk;		// �����߼�������������һ����ȷ��ֵ����ʼֵΪ������߼����Ͷȡ�������д��ʱ�򣬿��ܵ��¸�����

	LONG64 m_total_host_write;	// �Կ�Ϊ��λ��host��д������������Ĵ�С�ɸ����ļ�ϵͳ������һ��Ϊ4KB��
	LONG64 m_total_media_write;	// д����ʵ�������������blockΪ��λ
	LONG64 m_media_write_node;
	LONG64 m_media_write_data;

	UINT m_logical_saturation;	// �߼����Ͷȡ���д�����߼���������������metadata
	UINT m_physical_saturation;	// �����Ͷȡ���Ч�������������

	UINT m_node_nr;		// inode, direct node������
	UINT m_used_node;	// ��ʹ�õ�node����
};


struct FsSpaceInfo
{
	FSIZE total_blks;
	FSIZE used_blks;
	FSIZE free_blks;

	FSIZE max_file_nr;
	FSIZE created_files;
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
	virtual FID  FileCreate(const std::wstring& fn) = 0;
	virtual FID  DirCreate(const std::wstring& fn) = 0;
	virtual void FileOpen(FID fid, bool delete_on_close = false) = 0;
	virtual FID  FileOpen(const std::wstring& fn, bool delete_on_close = false) = 0;
	virtual void FileClose(FID fid) = 0;
	// ���úͻ�ȡ�ļ���С����sectorΪ��λ��
	virtual void SetFileSize(FID fid, FSIZE secs) = 0;
	virtual FSIZE GetFileSize(FID fid) = 0;

	virtual void FileWrite(FID fid, FSIZE offset, FSIZE secs) = 0;
	virtual void FileRead(std::vector<CPageInfo*>& blks, FID fid, FSIZE offset, FSIZE secs) = 0;
	virtual void FileTruncate(FID fid, FSIZE offset, FSIZE secs) = 0;
//	virtual void FileDelete(FID fid) = 0;
	// delete �����ļ���ɾ���ļ���FileRemove()����ID��ɾ���ļ�������ݣ����ǲ�ɾ��path map
	virtual void FileDelete(const std::wstring & fn) = 0;		
	virtual void FileFlush(FID fid) = 0;
	// �ļ��ܹ�֧�ֵ���󳤶ȣ�block��λ��
	virtual DWORD MaxFileSize(void) const = 0;
	virtual void GetSpaceInfo(FsSpaceInfo& space_info) = 0;
	
	// ����֧��
	// virtual bool CopyFrom(IFsSimulator* src) = 0;
	virtual void Clone(IFsSimulator*& dst) = 0;
	virtual void GetHealthInfo(FsHealthInfo& info) const = 0;

	virtual void DumpSegments(const std::wstring& fn, bool sanity_check) = 0;
	virtual void DumpSegmentBlocks(const std::wstring& fn) = 0;
	virtual void DumpFileMap(FILE* out, FID fid) = 0;
	virtual void DumpAllFileMap(const std::wstring& fn) = 0;
//	virtual void SetLogFolder(const std::wstring& fn) = 0;
	virtual void DumpBlockWAF(const std::wstring& fn) = 0;


};
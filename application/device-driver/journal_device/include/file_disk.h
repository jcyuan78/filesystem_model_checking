///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
#pragma once
#include <dokanfs-lib.h>
#include <boost/cast.hpp>

class CFileDiskBase : public IVirtualDisk
{
public:
	CFileDiskBase(void);
	virtual ~CFileDiskBase(void);

protected:
	bool CreateFileImage(size_t secs, size_t journal_size, size_t log_buf, bool read_only = false);
	bool CreateDriveImage(size_t secs, const std::wstring journal_file, size_t journal_size, size_t log_buf);
	virtual bool InitializeDevice(const boost::property_tree::wptree& config);
	virtual bool LoadFromFile(const std::wstring& fn) { return false; }
	virtual bool SaveToFile(const std::wstring& fn) { return false; }

	virtual size_t GetCapacity(void) {	return BYTE_TO_SECTOR(m_capacity);	};		// in sector
	virtual UINT GetSectorSize(void) const { return m_sector_size; }
	virtual void SetSectorSize(UINT size) { m_sector_size = size; }

	enum DRIVE_TYPE
	{
		PHYSICAL_DRIVE,		// ������̣�			\\.\PhysicalDriveX
		LOGICAL_DRIVE,		// �߼����̻��߷�����		\\?\X:
		FILE_DRIVE				// �ļ���				image.bin
	};

protected:
	// ����create image, ��ʼ��ʱ����
	bool m_async_io;
	std::wstring m_dev_path;
	DRIVE_TYPE m_type;
	bool m_journal_enable = false;	// �Ƿ��¼log


	HANDLE m_log_file = nullptr;
	HANDLE m_data_file = nullptr;
	UINT m_sector_size = SECTOR_SIZE;
	size_t m_capacity;		// in byte
};

class CFileDiskSync : public CFileDiskBase
{
public:
	CFileDiskSync(void);
	virtual ~CFileDiskSync(void);

public:
	//bool LoadJournal(FILE* journal, HANDLE data, size_t steps);
	//bool LoadJournal(const std::wstring& fn, size_t steps);
	//virtual bool MakeSnapshot(IVirtualDisk*& dev, size_t steps);
	//virtual size_t GetSteps(void) const;


public:
	virtual bool ReadSectors(void* buf, size_t lba, size_t secs);
	virtual bool WriteSectors(void* buf, size_t lba, size_t secs);
	// offset��overlap�ж���
	virtual bool AsyncWriteSectors(void* buf, size_t secs, OVERLAPPED* overlap);
	virtual bool AsyncReadSectors(void* buf, size_t secs, OVERLAPPED* overlap);

	virtual bool Trim(UINT lba, size_t secs) { return true; }
	virtual bool FlushData(UINT lba, size_t secs) { return true; }
	virtual void CopyFrom(IVirtualDisk* dev) {};
	virtual int  IoCtrl(int mode, UINT cmd, void* arg) { return 0; }

	virtual bool SaveSnapshot(const std::wstring& fn) { return false; }
	virtual bool BackLog(size_t num) { return false; }
	virtual void ResetLog(void) {}


	// journal device��interface
public:
	virtual size_t GetLogNumber(void) const { return m_journal_id; }

	virtual bool GetHealthInfo(HEALTH_INFO& info) {
		info.empty_block = 0;
		info.pure_spare = 0;
		info.host_write = boost::numeric_cast<UINT>(m_host_write);
		info.host_read = boost::numeric_cast<UINT>(m_host_read);
		info.media_write = boost::numeric_cast<UINT>(m_host_write);
		info.media_read = boost::numeric_cast<UINT>(m_host_read);
		return true;
	}

protected:
	void InternalWrite(void* buf, size_t lba, size_t secs);
protected:
	//	jcvos::IFileMapping * m_file;
	//BYTE* m_buf;
	//BYTE* m_snapshot;

	//size_t m_data_offset;

	//size_t m_log_ptr;

	size_t m_host_write, m_host_read;
	int m_queue_depth = 0;

	bool m_async_io = false;
	//// Log���ݽṹ
	//class LOG_STRUCTURE
	//{
	//public:
	//	UINT lba, secs;
	//	BYTE* data_ptr;
	//};
	//size_t m_log_capacity;		// log֧�ֵ���������
	size_t m_journal_id;		// ��ǰlogָ��
	//size_t m_log_buf_secs;		// buffer�Ĵ�С��sector��λ
	//LOG_STRUCTURE* m_logs;
	//BYTE* m_log_buf;
	//BYTE* m_log_data_ptr;		// ��ǰlog buffer��ָ��
};

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// == �첽�汾 ==
class CFileDiskAsync : public CFileDiskBase
{
public:
	CFileDiskAsync(void);
	virtual ~CFileDiskAsync(void);

public:
	enum DRIVE_TYPE
	{
		PHYSICAL_DRIVE,		// ������̣�			\\.\PhysicalDriveX
		LOGICAL_DRIVE,		// �߼����̻��߷�����		\\?\X:
		FILE_DRIVE				// �ļ���				image.bin
	};

public:
	virtual bool ReadSectors(void* buf, size_t lba, size_t secs);
	virtual bool WriteSectors(void* buf, size_t lba, size_t secs);
	// offset��overlap�ж���
	virtual bool AsyncWriteSectors(void* buf, size_t secs, OVERLAPPED* overlap);
	virtual bool AsyncReadSectors(void* buf, size_t secs, OVERLAPPED* overlap);

	virtual bool Trim(UINT lba, size_t secs) { return true; }
	virtual bool FlushData(UINT lba, size_t secs) { return true; }
	virtual void CopyFrom(IVirtualDisk* dev) {};
	virtual int  IoCtrl(int mode, UINT cmd, void* arg) { return 0; }

	virtual bool SaveSnapshot(const std::wstring& fn) { return false; }
	virtual bool BackLog(size_t num) { return false; }
	virtual void ResetLog(void) {}


	// journal device��interface
public:
	virtual size_t GetLogNumber(void) const { return m_journal_id; }

	virtual bool GetHealthInfo(HEALTH_INFO& info) {
		info.empty_block = 0;
		info.pure_spare = 0;
		info.host_write = boost::numeric_cast<UINT>(m_host_write);
		info.host_read = boost::numeric_cast<UINT>(m_host_read);
		info.media_write = boost::numeric_cast<UINT>(m_host_write);
		info.media_read = boost::numeric_cast<UINT>(m_host_read);
		return true;
	}

protected:
	void InternalWrite(void* buf, size_t lba, size_t secs);
protected:
	//	jcvos::IFileMapping * m_file;

	size_t m_host_write, m_host_read;

	//// Log���ݽṹ
	//class LOG_STRUCTURE
	//{
	//public:
	//	UINT lba, secs;
	//	BYTE* data_ptr;
	//};
	//size_t m_log_capacity;		// log֧�ֵ���������
	size_t m_journal_id;		// ��ǰlogָ��
	//size_t m_log_buf_secs;		// buffer�Ĵ�С��sector��λ
	//LOG_STRUCTURE* m_logs;
	//BYTE* m_log_buf;
	//BYTE* m_log_data_ptr;		// ��ǰlog buffer��ָ��
};
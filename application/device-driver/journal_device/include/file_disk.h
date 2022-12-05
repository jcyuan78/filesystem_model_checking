///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
#pragma once
#include <dokanfs-lib.h>
#include <boost/cast.hpp>


class CFileDisk : public IVirtualDisk
{
public:
	CFileDisk(void);
	virtual ~CFileDisk(void);

public:
	bool CreateFileImage(const std::wstring& fn, size_t secs, size_t journal_size, size_t log_buf, bool read_only = false);
	bool CreateDriveImage(const std::wstring& path, size_t secs, const std::wstring journal_file, size_t journal_size, size_t log_buf);
	//bool LoadJournal(FILE* journal, HANDLE data, size_t steps);
	//bool LoadJournal(const std::wstring& fn, size_t steps);
	//virtual bool MakeSnapshot(IVirtualDisk*& dev, size_t steps);
	//virtual size_t GetSteps(void) const;
	enum DRIVE_TYPE
	{
		PHYSICAL_DRIVE,		// 物理磁盘：			\\.\PhysicalDriveX
		LOGICAL_DRIVE,		// 逻辑磁盘或者分区：		\\?\X:
		FILE_DRIVE				// 文件：				image.bin
	};

public:
	virtual size_t GetCapacity(void);		// in sector
	virtual UINT GetSectorSize(void) const { return m_sector_size; }
	virtual bool ReadSectors(void* buf, size_t lba, size_t secs);
	virtual bool WriteSectors(void* buf, size_t lba, size_t secs);
	// offset在overlap中定义
	virtual bool AsyncWriteSectors(void* buf, size_t secs, OVERLAPPED* overlap, LPOVERLAPPED_COMPLETION_ROUTINE callback);
	virtual bool AsyncReadSectors(void* buf, size_t secs, OVERLAPPED* overlap, LPOVERLAPPED_COMPLETION_ROUTINE callback);

	virtual bool Trim(UINT lba, size_t secs) { return true; }
	virtual bool FlushData(UINT lba, size_t secs) { return true; }
	virtual void CopyFrom(IVirtualDisk* dev) {};
	virtual int  IoCtrl(int mode, UINT cmd, void* arg) { return 0; }

	virtual bool SaveSnapshot(const std::wstring& fn) { return false; }
	virtual bool BackLog(size_t num) { return false; }
	virtual void ResetLog(void) {}


	// journal device的interface
public:
	virtual bool InitializeDevice(const boost::property_tree::wptree& config);

	virtual void SetSectorSize(UINT size) { m_sector_size = size; }
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
	DRIVE_TYPE m_type;
	//	jcvos::IFileMapping * m_file;
	bool m_journal_enable = false;	// 是否记录log
	//BYTE* m_buf;
	//BYTE* m_snapshot;
	UINT m_sector_size = SECTOR_SIZE;
	size_t m_capacity;		// in byte

	HANDLE m_log_file = nullptr;
	HANDLE m_data_file = nullptr;
	//size_t m_data_offset;

	//size_t m_log_ptr;

	std::wstring m_dev_path;
	size_t m_host_write, m_host_read;
	int m_queue_depth = 0;

	bool m_async_io = false;
	//// Log数据结构
	//class LOG_STRUCTURE
	//{
	//public:
	//	UINT lba, secs;
	//	BYTE* data_ptr;
	//};
	//size_t m_log_capacity;		// log支持的最大的数量
	size_t m_journal_id;		// 当前log指针
	//size_t m_log_buf_secs;		// buffer的大小，sector单位
	//LOG_STRUCTURE* m_logs;
	//BYTE* m_log_buf;
	//BYTE* m_log_data_ptr;		// 当前log buffer的指针
};

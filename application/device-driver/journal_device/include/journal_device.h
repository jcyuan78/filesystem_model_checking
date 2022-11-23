#pragma once

#include <dokanfs-lib.h>
#include <jcfile.h>
#include <boost/cast.hpp>

#pragma comment (lib, "journal_device.lib")

class CJournalDevice : public IVirtualDisk
{
public:
	CJournalDevice(void);
	virtual ~CJournalDevice(void);

public:
	bool CreateFileImage(const std::wstring & fn, size_t secs, size_t journal_size, size_t log_buf, bool read_only = false);
	bool LoadJournal(FILE * journal, HANDLE data, size_t steps);
	bool LoadJournal(const std::wstring & fn, size_t steps);
	virtual bool MakeSnapshot(IVirtualDisk * &dev, size_t steps);
	virtual size_t GetSteps(void) const;

public:
	virtual size_t GetCapacity(void);		// in sector
	virtual UINT GetSectorSize(void) const { return m_sector_size; }
	virtual bool ReadSectors(void * buf, size_t lba, size_t secs);
	virtual bool WriteSectors(void * buf, size_t lba, size_t secs);
	virtual bool Trim(UINT lba, size_t secs) {	return true; }
	virtual bool FlushData(UINT lba, size_t secs) {	return true; }
	virtual void CopyFrom(IVirtualDisk * dev) {};
	virtual bool SaveSnapshot(const std::wstring &fn);
	virtual int  IoCtrl(int mode, UINT cmd, void* arg) { return 0; }


	// journal device的interface
public:
	bool InitializeDevice(const boost::property_tree::wptree& config);

	virtual void SetSectorSize(UINT size) {	m_sector_size = size;	}
	virtual size_t GetLogNumber(void) const { return m_journal_id; }
	virtual bool BackLog(size_t num);
	virtual void ResetLog(void);

	virtual bool GetHealthInfo(HEALTH_INFO & info) { 
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
	bool m_journal_enable;	// 是否记录log
	BYTE * m_buf;
	BYTE* m_snapshot;
	UINT m_sector_size;
	size_t m_capacity;		// in byte

	FILE * m_log_file;
	HANDLE m_data_file;
	size_t m_data_offset;

	size_t m_log_ptr;

	std::wstring m_dev_name;
	size_t m_host_write, m_host_read;

	// Log数据结构
	class LOG_STRUCTURE
	{
	public:
		UINT lba, secs;
		BYTE* data_ptr;
	};
	size_t m_log_capacity;		// log支持的最大的数量
	size_t m_journal_id;		// 当前log指针
	size_t m_log_buf_secs;		// buffer的大小，sector单位
	LOG_STRUCTURE * m_logs;
	BYTE* m_log_buf;
	BYTE* m_log_data_ptr;		// 当前log buffer的指针
};

//bool CreateVirtualDevice(IVirtualDisk *& dev, const std::wstring & fn, size_t secs);

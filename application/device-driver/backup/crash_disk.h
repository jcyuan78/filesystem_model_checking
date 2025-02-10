///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
#pragma once

#include <dokanfs-lib.h>
#include <list>
#include <vector>

// crash disk��һ������rollback��drive, ����file system��crash���ԡ�����ԭ���journal drive���ƣ�����ʵ��Ч��Ҫ����journal drive

struct LBA_ENTRY
{
public:
	UINT lba;
	UINT offset;	// ��image�ļ��е�offset
	UINT cache_next;
	UINT cache_prev;
};

class CCrashDisk : public IVirtualDisk
{
public:
	CCrashDisk(void);
	virtual ~CCrashDisk(void);
	// �����������ݣ���������init����load
	void Reset(void);

public:
//	bool CreateFileImage(const std::wstring& fn, size_t secs, size_t journal_size, size_t log_buf, bool read_only = false);
//	bool LoadJournal(FILE* journal, HANDLE data, size_t steps);
//	bool LoadJournal(const std::wstring& fn, size_t steps);
	// �������ڵ�״̬������һ������ӵ�����ڵ�snapshot
	virtual bool MakeSnapshot(IVirtualDisk*& dev, size_t steps) { return false; }
	virtual size_t GetSteps(void) const { return m_cache.size(); };

public:
	virtual size_t GetCapacity(void) { return m_disk_size; }		// in sector
	virtual UINT GetSectorSize(void) const { return m_sector_size ; }
	virtual bool ReadSectors(void* buf, size_t lba, size_t secs);
	virtual bool WriteSectors(void* buf, size_t lba, size_t secs);
	virtual bool AsyncWriteSectors(void* buf, size_t secs, OVERLAPPED* overlap);
	virtual bool AsyncReadSectors(void* buf, size_t secs, OVERLAPPED* overlap);

	virtual bool Trim(UINT lba, size_t secs) { return true; }
	virtual bool FlushData(UINT lba, size_t secs) { return true; }
	virtual void CopyFrom(IVirtualDisk* dev) {};
	// �����еĴ���״̬�������ļ� fn �С�
	virtual bool SaveSnapshot(const std::wstring& fn) { return false; }
	virtual int  IoCtrl(int mode, UINT cmd, void* arg) { return 0; }


	// journal device��interface
public:
	virtual bool InitializeDevice(const boost::property_tree::wptree& config);
	virtual bool LoadFromFile(const std::wstring& fn);
	virtual bool SaveToFile(const std::wstring& fn);

	virtual void SetSectorSize(UINT size) { m_sector_size = size; }
	virtual size_t GetLogNumber(void) const { return m_cache.size(); }
	virtual size_t GetIoLogs(IO_ENTRY* entries, size_t io_nr);

	virtual bool BackLog(size_t num);	// rollback
	virtual void ResetLog(void) {};

	virtual bool GetHealthInfo(HEALTH_INFO& info) {
		//info.empty_block = 0;
		//info.pure_spare = 0;
		//info.host_write = boost::numeric_cast<UINT>(m_host_write);
		//info.host_read = boost::numeric_cast<UINT>(m_host_read);
		//info.media_write = boost::numeric_cast<UINT>(m_host_write);
		//info.media_read = boost::numeric_cast<UINT>(m_host_read);
		return true;
	}

protected:
	void cache_enque(UINT lba, UINT index);
	void cache_deque(UINT cache_index);
	void ReadSingleBlock(BYTE* buf, UINT blk_no);

protected:
	UINT m_sector_size;
	UINT m_block_size;
	UINT m_cache_size;
	UINT m_disk_size;		// sector ��λ
	UINT* m_lba_mapping;

	std::vector<LBA_ENTRY *> m_cache;
	HANDLE m_data_file;
//	UINT m_cur_cache_size;

	std::wstring m_data_fn;
//	std::wstring m_journal_fn;

	// ��ǰ�ļ�ָ��
	LONG m_file_pos;
};



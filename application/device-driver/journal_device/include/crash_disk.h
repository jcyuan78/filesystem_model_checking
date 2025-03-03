///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
#pragma once

#include <dokanfs-lib.h>
#include <list>
#include <vector>

// crash disk是一个可以rollback的drive, 用于file system的crash测试。基本原理和journal drive类似，但是实现效率要高于journal drive

struct LBA_ENTRY
{
public:
//	UINT lba;
	UINT offset;	// 在image文件中的offset
//	UINT cache_next;
//	UINT cache_prev;
	LBA_ENTRY* cache_next;
	LBA_ENTRY* cache_prev;
};

struct CMD_ENTRY
{
public:
	UINT start_lba;
	UINT secs;
	LBA_ENTRY* blocks;
	std::wstring op;
//	UINT block_index;
};

class CCrashDisk : public IVirtualDisk
{
public:
	CCrashDisk(void);
	virtual ~CCrashDisk(void);
	// 重置所有数据，用于重新init或者load
	void Reset(void);

public:
//	bool CreateFileImage(const std::wstring& fn, size_t secs, size_t journal_size, size_t log_buf, bool read_only = false);
//	bool LoadJournal(FILE* journal, HANDLE data, size_t steps);
//	bool LoadJournal(const std::wstring& fn, size_t steps);
	// 根据现在的状态，创建一个对象，拥有现在的snapshot
	virtual bool MakeSnapshot(IVirtualDisk*& dev, size_t steps) { return false; }
	virtual size_t GetSteps(void) const { return m_cmd_cache.size(); };

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
	// 将现有的磁盘状态导出到文件 fn 中。
	virtual bool SaveSnapshot(const std::wstring& fn) { return false; }
	virtual int  IoCtrl(int mode, UINT cmd, void* arg);


	// journal device的interface
public:
	virtual bool InitializeDevice(const boost::property_tree::wptree& config);
	virtual bool LoadFromFile(const std::wstring& fn);
	virtual bool SaveToFile(const std::wstring& fn);

	virtual void SetSectorSize(UINT size) { m_sector_size = size; }
	virtual size_t GetLogNumber(void) const { return m_cmd_cache.size(); }
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
//	void cache_enque(UINT lba, UINT index);
//	void cache_deque(UINT cache_index);
	void cache_enque(LBA_ENTRY * lba_entry, UINT blk);
	void cache_deque(UINT blk);
	void ReadSingleBlock(BYTE* buf, UINT blk_no);
	void build_lba_mapping(void);
	void add_command(UINT start_lba, UINT secs, UINT start_blk, UINT end_blk, UINT & start_offset);
	UINT lba_to_block(UINT& start_blk, UINT& end_blk, UINT lba, UINT secs);

	typedef LBA_ENTRY* LP_LBA_ENTRY;

protected:
	UINT m_sector_size;
	UINT m_block_size;
	UINT m_disk_size;		// sector 单位
	LP_LBA_ENTRY *m_lba_mapping;

	std::vector<CMD_ENTRY> m_cmd_cache;
	HANDLE m_data_file;
	UINT m_cache_size;

	// 这两个变量只是现在cache的top位置，以及cache中最上面block的位置。
	std::vector<CMD_ENTRY>::reverse_iterator m_cache_top;
	UINT m_cache_top_index;

	std::wstring m_data_fn;

	// 当前文件指针
	LONG m_file_pos;
};



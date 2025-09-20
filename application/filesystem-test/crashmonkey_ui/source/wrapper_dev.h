#pragma once

#include <ifilesystem.h>
#include <crashmonkey_comm.h>
#include "utils/utils.h"


struct disk_write_op
{
public:
	disk_write_op(void) /*: data(NULL)*/ {};
	~disk_write_op(void) {/* delete[] data;*/ }
	struct disk_write_op_meta metadata;
//	void* data=nullptr;
	struct disk_write_op* next = nullptr;
	std::shared_ptr<BYTE> m_data;
};

static int major_num = 0;

struct hwm_device
{
	unsigned long size;
//	spinlock_t lock;
	BYTE* data;
	struct gendisk* gd;
	bool log_on;
//	struct gendisk* target_dev;
//	BYTE target_partno;
	//struct block_device* target_bd;

	// Pointer to first write op in the chain.
	struct disk_write_op* writes;
	// Pointer to last write op in the chain.
	struct disk_write_op* current_write;
	// Pointer to log entry to be sent to user-land next.
	struct disk_write_op* current_log_write;
	unsigned long current_checkpoint;
};


class CWrapperDisk : public IVirtualDisk
{
public:
	CWrapperDisk(void) { m_hwm_dev.current_write = NULL; m_hwm_dev.writes = NULL; m_hwm_dev.current_log_write = NULL; };
	~CWrapperDisk(void);
public:
	virtual bool InitializeDevice(const boost::property_tree::wptree& config) { return false; }
	virtual bool LoadFromFile(const std::wstring& fn) { return false; }
	virtual bool SaveToFile(const std::wstring& fn) { return false; }
	virtual UINT GetSectorSize(void) const { return 512; }
	// offset在overlap中定义
	virtual bool AsyncWriteSectors(void* buf, size_t secs, OVERLAPPED* overlap) { return false; }
	virtual bool AsyncReadSectors(void* buf, size_t secs, OVERLAPPED* overlap) { return false; }
	virtual size_t GetIoLogs(IO_ENTRY* entries, size_t io_nr) { return 0; }


	virtual size_t GetCapacity(void) { return m_target_dev->GetCapacity(); }		// in sector
	virtual bool ReadSectors(void* buf, size_t lba, size_t secs);
	virtual bool WriteSectors(void* buf, size_t lba, size_t secs);
	virtual bool Trim(UINT lba, size_t secs) { return false; }
	virtual bool FlushData(UINT lba, size_t secs);
	virtual void SetSectorSize(UINT size) { return; }
	virtual void CopyFrom(IVirtualDisk* dev) { return; }
	// 将当前的image保存到文件中. => TODO: 这些特定驱动其用到的函数可以通过IoCtrol实现。
	virtual bool SaveSnapshot(const std::wstring& fn) { return false; }

	virtual bool GetHealthInfo(HEALTH_INFO& info) { return false; }

	virtual size_t GetLogNumber(void) const { return 0; }

	virtual bool BackLog(size_t num) { return false; }
	virtual void ResetLog(void) { return; }
	virtual int  IoCtrl(int mode, UINT cmd, void* arg);

public:
	int GetWriteLog(fs_testing::utils::disk_write& log);

public:
	bool Initialize(IVirtualDisk* target_dev);

protected:
	void FreeLogs(void);
	void RemoveAllLogs(void);
	void InsertLog(disk_write_op* write);

protected:
	IVirtualDisk* m_target_dev;
	hwm_device m_hwm_dev;

};

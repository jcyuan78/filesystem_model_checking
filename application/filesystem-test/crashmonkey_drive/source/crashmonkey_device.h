#pragma once
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#include <ifilesystem.h>
#include <vector>

///<summary>
// Each block ramdisk device has a radix_tree brd_pages of pages that stores the pages containing the block device's
//	contents. A brd page's ->index is its offset in PAGE_SIZE units. This is similar to, but in no way connected with, 
//	the kernel's pagecache or buffer cache (which sit above our block device).
///</summary>
struct brd_device
{
	int   brd_number;
	struct brd_device* parent_brd;
	// Denotes whether or not a cow_ram is writable and snapshots are active.
	bool  is_writable;
	bool  is_snapshot;
	/*
	 * Backing store of pages and lock to protect it. This is the contents
	 * of the block device.
	 */
	//spinlock_t    brd_lock;
	// brd device实现的并非连续的空间，而是将数据保存在一组page中，page按照radix_tree来组织
	//struct radix_tree_root  brd_pages;
};



class CRamDisk : public IVirtualDisk
{
public:
	CRamDisk(void): m_data (NULL) {};
	~CRamDisk(void) 
	{
		delete[]m_data;
		delete[] m_bitmap;
	}

	friend class CCrashMonkeyCtrl;

	//<YUAN> snapshot的实现。snapshot有一个与其对应的raw device（snapshot的parent）。raw device与snapshot为1对多关系。
	// 读写raw device时，不影响snapshot。写入snapshot时，如果对应块被写过，则直接写入snapshot。否者从raw device复制块数据到
	// snapshot，然后写入snapshot。数据保存在snapshot中。读取snapshot时，首先判断对应的块是否被写过，如果被写过则直接返回，
	// 否则返回raw device中相应块的数据。
	// 在CrashMonkey中，通过radix tree管理块(page）的写入情况，数据块动态分配。在这里简化，数据块静态分配，用bitmap管理写入
	// 情况。以sector为单位管理。

public:
	virtual size_t GetCapacity(void) { return m_secs; }		// in sector
	virtual bool ReadSectors(void* buf, size_t lba, size_t secs);
	virtual bool WriteSectors(void* buf, size_t lba, size_t secs);
	virtual bool Trim(UINT lba, size_t secs) { return false; }
	virtual bool FlushData(UINT lba, size_t secs) { return false; }
	virtual void SetSectorSize(UINT size) { return; }
	virtual void CopyFrom(IVirtualDisk* dev) { return; }
	// 将当前的image保存到文件中.
	virtual bool SaveSnapshot(const std::wstring& fn) { return false; }

	virtual bool GetHealthInfo(HEALTH_INFO& info) { return false; }

	virtual size_t GetLogNumber(void) const { return 0; }
	virtual bool BackLog(size_t num) { return false; }
	virtual void ResetLog(void) { return; }
	virtual int  IoCtrl(int mode, UINT cmd, void* arg);

	// sector_size：一个sector的大小，字节单位。cap：磁盘容量，sector单位。
	void Initialize(size_t sector_size, size_t cap, int id,  CRamDisk * raw_disk);

protected:
	BYTE* GetDataSource(size_t lba, size_t bmp, UINT64 mask);
	void FreePages(void);

protected:
	size_t m_sector_size = 512;
	size_t m_secs;
	size_t m_size = 0;		// capacity in byte
	BYTE* m_data;
	brd_device m_brd_device;
	CRamDisk* m_raw_disk = NULL;	// 与snapshot对应的raw disk，如果时raw_disk则为NULL
	UINT64 * m_bitmap; // 管理数据块的使用情况。
	size_t m_bitmap_len;
};

// 主控设备，用于管理ram disk以及snapshot。并且用于执行控制操作。

class CCrashMonkeyCtrl : public IVirtualDisk
{
public:
	CCrashMonkeyCtrl(void);
	~CCrashMonkeyCtrl(void);

	friend class CCrashMonkeyFactory;

public:
	virtual size_t GetCapacity(void) { return m_secs; }		// in sector
	virtual bool ReadSectors(void* buf, size_t lba, size_t secs);
	virtual bool WriteSectors(void* buf, size_t lba, size_t secs);
	virtual bool Trim(UINT lba, size_t secs) { return false; }
	virtual bool FlushData(UINT lba, size_t secs) { return false; }
	virtual void SetSectorSize(UINT size) { return; }
	virtual void CopyFrom(IVirtualDisk* dev) { return; }
	// 将当前的image保存到文件中.
	virtual bool SaveSnapshot(const std::wstring& fn) { return false; }

	virtual bool GetHealthInfo(HEALTH_INFO& info) { return false; }

	virtual size_t GetLogNumber(void) const { return 0; }
	virtual bool BackLog(size_t num) { return false; }
	virtual void ResetLog(void) { return; }
	virtual int  IoCtrl(int mode, UINT cmd, void* arg);

protected:
	void Initialize(void);
	
protected:
	size_t m_sector_size = 512;
	size_t m_secs = 0;

	//<YUAN>由于每个checkpoint需要一个snapshot，snapshot需要动态管理
	std::vector<CRamDisk*> m_disks;
	//CRamDisk* *m_disk = NULL;
	int m_snapshot_num = 0;
	CRamDisk* m_raw_disk;	// m_raw_disk只是m_disks[0]的缓存，不需计数。
};



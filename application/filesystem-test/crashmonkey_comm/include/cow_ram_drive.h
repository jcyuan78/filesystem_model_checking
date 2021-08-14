#pragma once

#include <ifilesystem.h>

class CCowRamDrive : public IVirtualDisk
{
public:
	CCowRamDrive(void);
	~CCowRamDrive(void);

	void Initialize(int disk_num, int snapshot_num, size_t disk_size) { JCASSERT(0); };


public:
	virtual size_t GetCapacity(void) ;		// in sector
	virtual bool ReadSectors(void* buf, UINT lba, size_t secs) ;
	virtual bool WriteSectors(void* buf, UINT lba, size_t secs);
	virtual bool Trim(UINT lba, size_t secs) ;
	virtual bool FlushData(UINT lba, size_t secs) ;
	virtual void SetSectorSize(UINT size) ;
	virtual void CopyFrom(IVirtualDisk* dev) ;
	// 将当前的image保存到文件中.
	virtual bool SaveSnapshot(const std::wstring& fn);

	virtual bool GetHealthInfo(HEALTH_INFO& info) ;

	virtual size_t GetLogNumber(void) const ;
	virtual bool BackLog(size_t num) ;
	virtual void ResetLog(void) ;
	virtual int  IoCtrl(int mode, UINT cmd, void* arg) ;

protected:

};
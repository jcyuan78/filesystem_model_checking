#pragma once

#include <dokanfs-lib.h>
#include <jcfile.h>

class CImageDevice : public IVirtualDisk
{
public:
	CImageDevice(void);
	virtual ~CImageDevice(void);

public:
	bool CreateFileImage(const std::wstring & fn, size_t size);
	

public:
	virtual bool InitializeDevice(const boost::property_tree::wptree& config) { return false; }
	virtual bool LoadFromFile(const std::wstring& fn) { return false; }
	virtual bool SaveToFile(const std::wstring& fn) { return false; }


	virtual size_t GetCapacity(void);		// in sector
	//virtual bool ReadBytes(void * buf, UINT offset, size_t len);
	//virtual bool WriteBytes(void * buf, UINT offset, size_t len);
	virtual bool ReadSectors(void * buf, size_t lba, size_t secs);
	virtual bool WriteSectors(void * buf, size_t lba, size_t secs);
	virtual bool AsyncWriteSectors(void* buf, size_t secs, OVERLAPPED* overlap) {return false;}
	virtual bool AsyncReadSectors(void* buf, size_t secs, OVERLAPPED* overlap)  {return false;}

	virtual bool Trim(UINT lba, size_t secs) {	return true; }
	virtual bool FlushData(UINT lba, size_t secs) {	return true; }
	virtual void SetSectorSize(UINT size) {	m_sector_size = size;	}
	virtual void CopyFrom(IVirtualDisk * dev) {};

	virtual UINT GetSectorSize(void) const { return m_sector_size; }

	// 将当前的image保存到文件中.
	virtual bool SaveSnapshot(const std::wstring& fn) { return false; }
	virtual bool GetHealthInfo(HEALTH_INFO& info) { return false; }
	virtual size_t GetLogNumber(void) const { return 0; }
	virtual size_t GetIoLogs(IO_ENTRY* entries, size_t io_nr) { return 0; }

	virtual bool BackLog(size_t num) { return false; }
	virtual void ResetLog(void) {}
	virtual int  IoCtrl(int mode, UINT cmd, void* arg) { return 0; }

protected:
	jcvos::IFileMapping * m_file;
	UINT m_sector_size;
	size_t m_capacity;		// in byte
};

bool CreateVirtualDevice(IVirtualDisk *& dev, const std::wstring & fn, size_t secs);

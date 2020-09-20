#pragma once

#include "../../dokanfs-lib/include/ifilesystem.h"
#include <jcfile.h>

class CImageDevice : public IVirtualDisk
{
public:
	CImageDevice(void);
	virtual ~CImageDevice(void);

public:
	bool CreateFileImage(const std::wstring & fn, size_t size);
	

public:
	virtual UINT GetCapacity(void);		// in sector
	//virtual bool ReadBytes(void * buf, UINT offset, size_t len);
	//virtual bool WriteBytes(void * buf, UINT offset, size_t len);
	virtual bool ReadSectors(void * buf, UINT lba, size_t secs);
	virtual bool WriteSectors(void * buf, UINT lba, size_t secs);
	virtual bool Trim(UINT lba, size_t secs) {	return true; }
	virtual bool FlushData(UINT lba, size_t secs) {	return true; }
	virtual void SetSectorSize(UINT size) {	m_sector_size = size;	}
	virtual void CopyFrom(IVirtualDisk * dev) {};

protected:
	jcvos::IFileMapping * m_file;
	UINT m_sector_size;
	size_t m_capacity;		// in byte
};

bool CreateVirtualDevice(IVirtualDisk *& dev, const std::wstring & fn, size_t secs);

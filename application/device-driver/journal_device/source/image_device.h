#pragma once

#include "../../dokan-app/include/ifilesystem.h"
#include <jcfile.h>

class CMemoryDevice : public IVirtualDevice
{
public:
	CMemoryDevice(void);
	virtual ~CMemoryDevice(void);

public:
	bool CreateFileImage(size_t size);

public:
	virtual UINT GetCapacity(void);		// in sector
	virtual bool ReadSectors(void * buf, UINT lba, size_t secs);
	virtual bool WriteSectors(void * buf, UINT lba, size_t secs);
	virtual bool Trim(UINT lba, size_t secs) {	return true; }
	virtual bool FlushData(UINT lba, size_t secs) {	return true; }
	virtual void SetSectorSize(UINT size) {	m_sector_size = size;	}
	virtual void CopyFrom(IVirtualDevice * dev);

protected:
	UINT m_sector_size;
	size_t m_capacity;		// in byte
	BYTE * m_data;
};
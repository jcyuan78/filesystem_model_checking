#pragma once

#include "../../dokan-app/include/ifilesystem.h"
#include <jcfile.h>

class CJournalDevice : public IVirtualDevice
{
public:
	CJournalDevice(void);
	virtual ~CJournalDevice(void);

public:
	bool CreateFileImage(const std::wstring & fn, size_t size);
	bool LoadJournal(const std::wstring & fn, size_t steps);
	bool MakeSnapshot(const std::wstring &fn);

public:
	virtual UINT GetCapacity(void);		// in sector
	virtual bool ReadSectors(void * buf, UINT lba, size_t secs);
	virtual bool WriteSectors(void * buf, UINT lba, size_t secs);
	virtual bool Trim(UINT lba, size_t secs) {	return true; }
	virtual bool FlushData(UINT lba, size_t secs) {	return true; }
	virtual void SetSectorSize(UINT size) {	m_sector_size = size;	}
	virtual void CopyFrom(IVirtualDevice * dev) {};

protected:
//	jcvos::IFileMapping * m_file;
	BYTE * m_buf;
	UINT m_sector_size;
	size_t m_capacity;		// in byte

	FILE * m_log_file;
	HANDLE m_data_file;
	size_t m_journal_id;
	size_t m_data_offset;
};

//bool CreateVirtualDevice(IVirtualDevice *& dev, const std::wstring & fn, size_t secs);

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
#include "pch.h"
//#include "stdafx.h"
#include "image_device.h"

LOCAL_LOGGER_ENABLE(_T("fat.image"), LOGGER_LEVEL_DEBUGINFO);


CImageDevice::CImageDevice(void)
	: m_file(NULL), m_sector_size(512)
{
}

CImageDevice::~CImageDevice(void)
{
	RELEASE(m_file);
}

bool CImageDevice::CreateFileImage(const std::wstring & fn, size_t secs)
{
	HANDLE file = CreateFileW(fn.c_str(), 
		GENERIC_READ | GENERIC_WRITE, 
		FILE_SHARE_READ, NULL, 
		OPEN_ALWAYS, FILE_FLAG_RANDOM_ACCESS, NULL);
	if (file == INVALID_HANDLE_VALUE) THROW_WIN32_ERROR(L"failed on open %s", fn.c_str());
	bool br = jcvos::CreateFileMappingObject(file, secs * SECTOR_SIZE, m_file);
	JCASSERT(br && m_file);
	m_capacity = m_file->GetSize()/* / SECTOR_SIZE*/;
	LOG_DEBUG(L"created image file, sectors=%d", m_capacity);
	return true;
}

size_t CImageDevice::GetCapacity(void)
{
	return m_capacity;
}

bool CImageDevice::ReadSectors(void * buf, size_t lba, size_t secs)
{
	JCASSERT(m_sector_size);
	jcvos::auto_interface<jcvos::IBinaryBuffer> _buf;
	// 以sector为单位:
	//bool br = jcvos::CreateFileMappingBuf(m_file, lba, secs, _buf);
	size_t offset = lba * m_sector_size;
	size_t len = secs * m_sector_size;
	bool br = jcvos::CreateFileMappingBufByte(_buf, m_file, offset, len);
	JCASSERT(br && _buf);
	BYTE * data = _buf->Lock();
	memcpy_s(buf, len, data, len);
	_buf->Unlock(data);
	return true;
}

bool CImageDevice::WriteSectors(void * buf, size_t lba, size_t secs)
{
	JCASSERT(m_sector_size);
	jcvos::auto_interface<jcvos::IBinaryBuffer> _buf;
//	bool br = jcvos::CreateFileMappingBuf(m_file, lba, secs, _buf);
	size_t offset = lba * m_sector_size;
	size_t len = secs * m_sector_size;
	bool br = jcvos::CreateFileMappingBufByte(_buf, m_file, offset, len);
	JCASSERT(br && _buf);
	BYTE * data = _buf->Lock();
	memcpy_s(data, len, buf, len);
	_buf->Unlock(data);
	return true;
}

#if 0
bool CreateVirtualDevice(IVirtualDisk *& dev, const std::wstring & fn, size_t secs)
{
	JCASSERT(dev == NULL);

	CImageDevice * _dev = jcvos::CDynamicInstance<CImageDevice>::Create();
	if (!_dev) return false;
	_dev->CreateFileImage(fn, secs);
	dev = static_cast<IVirtualDisk *>(_dev);

	return true;
}
#endif
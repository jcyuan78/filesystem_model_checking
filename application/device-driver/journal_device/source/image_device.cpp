#include "stdafx.h"
#include "image_device.h"

LOCAL_LOGGER_ENABLE(L"minifat.image", LOGGER_LEVEL_DEBUGINFO);


CMemoryDevice::CMemoryDevice(void)
	: m_sector_size(0), m_capacity(0), m_data(NULL)
{
}

CMemoryDevice::~CMemoryDevice(void)
{
	delete[] m_data;
}

bool CMemoryDevice::CreateFileImage(size_t size)
{
	m_data = new BYTE[size];
	m_capacity = size;
	return true;
}

UINT CMemoryDevice::GetCapacity(void)
{
	return m_capacity;
}

bool CMemoryDevice::ReadSectors(void * buf, UINT lba, size_t secs)
{
	JCASSERT(m_sector_size);
	size_t size = secs * m_sector_size;
	size_t offset = lba * m_sector_size;
	JCASSERT((offset + size) <= m_capacity);
	memcpy_s(buf, size, m_data + offset, size);
	return true;
}

bool CMemoryDevice::WriteSectors(void * buf, UINT lba, size_t secs)
{
	JCASSERT(m_sector_size);
	size_t size = secs * m_sector_size;
	size_t offset = lba * m_sector_size;
	JCASSERT((offset + size) <= m_capacity);
	memcpy_s(m_data + offset, size, buf, size);
	return true;
}

void CMemoryDevice::CopyFrom(IVirtualDevice * dev)
{
	CMemoryDevice * _dev = dynamic_cast<CMemoryDevice *>(dev);
	JCASSERT(_dev);
	JCASSERT(_dev->m_capacity == m_capacity);
	memcpy_s(m_data, m_capacity, _dev->m_data, _dev->m_capacity);
}

#if 1
bool CreateVirtualDevice(IVirtualDevice *& dev, const std::wstring & fn, size_t capacity)
{
	JCASSERT(dev == NULL);
	CMemoryDevice * _dev = jcvos::CDynamicInstance<CMemoryDevice>::Create();
	if (!_dev) return false;
	_dev->CreateFileImage(capacity);
	dev = static_cast<IVirtualDevice *>(_dev);
	return true;
}

#endif
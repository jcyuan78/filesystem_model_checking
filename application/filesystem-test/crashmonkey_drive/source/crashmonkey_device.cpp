#include "pch.h"
#include "crashmonkey_device.h"

#include "factory.h"

#include <crashmonkey_comm.h>

LOCAL_LOGGER_ENABLE(L"crashmonkey.drive", LOGGER_LEVEL_DEBUGINFO);


///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// == Factory
jcvos::CStaticInstance<CCrashMonkeyFactory> g_factory;

extern "C" __declspec(dllexport) bool GetFactory(IFsFactory * &fac)
{
	JCASSERT(fac == NULL);
	fac = static_cast<IFsFactory*>(&g_factory);
	return true;
}

bool CCrashMonkeyFactory::CreateFileSystem(IFileSystem*& fs, const std::wstring& fs_name)
{
	LOG_ERROR(L"[err] no supported file systems")
	return false;
}

bool CCrashMonkeyFactory::CreateVirtualDisk(IVirtualDisk*& dev, const boost::property_tree::wptree& prop, bool create)
{
	JCASSERT(dev == NULL);
	const std::wstring& dev_name = prop.get<std::wstring>(L"name", L"");
	if (dev_name == L"crashmonkey")
	{
		size_t cap = prop.get<size_t>(L"dev.sectors");
		int snapshot = prop.get<int>(L"dev.snapshot_num");

		//auto& dev_pt = prop.get_child(L"dev");
		//size_t secs = dev_pt.get<size_t>(L"sectors");
		//const std::wstring& fn = dev_pt.get<std::wstring>(L"image_file");
		//bool journal = dev_pt.get<bool>(L"enable_journal");
		//CJournalDevice* _dev = jcvos::CDynamicInstance<CJournalDevice>::Create();
		//size_t log_cap = dev_pt.get<size_t>(L"log_capacity");
		//size_t log_buf = log_cap * 16;
		//_dev->CreateFileImage(fn, secs, log_cap, log_buf);

		CCrashMonkeyCtrl* _dev = jcvos::CDynamicInstance<CCrashMonkeyCtrl>::Create();
		_dev->m_secs = cap;
		_dev->m_snapshot_num = snapshot;
		_dev->Initialize();

		dev = static_cast<IVirtualDisk*>(_dev);
		return true;
	}
	return false;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// == Crash Monkey Device

CCrashMonkeyCtrl::CCrashMonkeyCtrl(void)
{
}

CCrashMonkeyCtrl::~CCrashMonkeyCtrl(void)
{
	if (m_disk)
	{
		for (int ii = 0; ii <= m_snapshot_num; ++ii) RELEASE(m_disk[ii]);
		delete[] m_disk;
	}
}

bool CCrashMonkeyCtrl::ReadSectors(void* buf, size_t lba, size_t secs)
{
	JCASSERT(m_disk && m_disk[0]);
	return m_disk[0]->ReadSectors(buf, lba, secs);
}

bool CCrashMonkeyCtrl::WriteSectors(void* buf, size_t lba, size_t secs)
{
	JCASSERT(m_disk && m_disk[0]);
	return m_disk[0]->WriteSectors(buf, lba, secs);
}

int CCrashMonkeyCtrl::IoCtrl(int mode, UINT cmd, void* arg)
{
	int error = 0;
//	struct brd_device* brd = bdev->bd_disk->private_data;
	if ((cmd & 0xFF00) == COW_BRD_GET_SNAPSHOT)
	{
		int disk_id = cmd & 0xFF;
		if (disk_id > m_snapshot_num) THROW_ERROR(ERR_APP, L"disk id (%d) overflow. disk_num = %d", m_snapshot_num+1);
		IVirtualDisk** snapshot = reinterpret_cast<IVirtualDisk**>(arg);
		*snapshot = static_cast<IVirtualDisk*>(m_disk[disk_id]);
		if (*snapshot) (*snapshot)->AddRef();
	}
	else
	{
		return m_disk[0]->IoCtrl(mode, cmd, arg);
		//switch (cmd)
		//{
		//case COW_BRD_SNAPSHOT:			return m_disk[0]->IoCtrl(mode, cmd, arg);
		//	//if (m_brd_device.is_snapshot)	return -ENOTTY;
		//	//m_brd_device.is_writable = false;
		//	break;
		//case COW_BRD_UNSNAPSHOT:		return m_disk[0]->IoCtrl(mode, cmd, arg);
		//	//if (m_brd_device.is_snapshot)	return -ENOTTY;
		//	//m_brd_device.is_writable = true;
		//	break;
		//case COW_BRD_RESTORE_SNAPSHOT:	return m_disk[0]->IoCtrl(mode, cmd, arg);
		//	//if (!m_brd_device.is_snapshot)	return -ENOTTY;
		//	//brd_free_pages(brd);
		//	break;
		//case COW_BRD_WIPE:				return m_disk[0]->IoCtrl(mode, cmd, arg);
		//	//if (m_brd_device.is_snapshot)	return -ENOTTY;
		//	// Assumes no snapshots are being used right now.
		//	//brd_free_pages(brd);
		//	break;

		//default:
		//	error = -ENOTTY;
		//}
	}
	return error;
}

void CCrashMonkeyCtrl::Initialize(void)
{
	if (m_secs == 0) THROW_ERROR(ERR_APP, L"wrong capacity, %zd secs", m_secs);
	// create sub disk
	m_disk = new CRamDisk*[m_snapshot_num + 1];
	memset(m_disk, 0, sizeof(CRamDisk*) * (m_snapshot_num + 1));
	// create the first disk (ram disk)
	for (int ii = 0; ii <= m_snapshot_num; ++ii)
	{
		m_disk[ii] = jcvos::CDynamicInstance<CRamDisk>::Create();
		JCASSERT(m_disk[ii]);
		// index=0为raw disk，其他均为raw disk的snapshot
		CRamDisk* raw = NULL;
		if (ii != 0) raw = m_disk[0];
		m_disk[ii]->Initialize(m_sector_size, m_secs, ii, raw);
	}
}


///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// == Crash Monkey Device

bool CRamDisk::ReadSectors(void* buf, size_t lba, size_t secs)
{
	//JCASSERT(m_sector_size);
	// 以sector为单位:
	size_t offset = lba * m_sector_size;
	size_t len = secs * m_sector_size;
	//	JCASSERT((offset + len) <= m_size);
	if ((offset + len) > m_size) THROW_ERROR(ERR_APP,
		L"read over capacity cap=%d, lba=%d, secs=%zd", m_secs, lba, secs);

	BYTE* _buf = reinterpret_cast<BYTE*>(buf);

	size_t bb = lba / 64, bi = lba % 64;
	UINT64 mask = (1LL << bi);
	size_t ii = 0;
	for (size_t ii = 0; ii < secs; ++ii)
	{
		BYTE* src = GetDataSource(offset, bb, mask);
		if (src) memcpy_s(_buf, m_sector_size, src, m_sector_size);
		else memset(_buf, 0, m_sector_size);

		_buf += m_sector_size;
		offset += m_sector_size;
		mask <<= 1;
		if (mask == 0)	{	bb++;	mask = 1;	}
	}
//	memcpy_s(buf, len, m_data + offset, len);
	//	m_host_read += secs;
	return true;
}

bool CRamDisk::WriteSectors(void* buf, size_t lba, size_t secs)
{
	JCASSERT(m_sector_size);
	//	LOG_DEBUG(L"write lba=%08X, secs=%d", lba, secs);
	size_t offset = lba * m_sector_size;
	size_t len = secs * m_sector_size;
	if ((offset + len) > m_size) THROW_ERROR(ERR_APP,
		L"write over capacity cap=%d, lba=%d, secs=%zd", m_secs, lba, secs);

	memcpy_s(m_data + offset, len, buf, len);
	size_t bb = lba / 64, bi = lba % 64;
	UINT64 mask = (1LL << bi);
	size_t ii = 0;
	for (size_t ii = 0; ii < secs; ++ii)
	{	// 由于sector就是块大小，不会出现补头补尾的问题，直接写入并且标记bitmap
		m_bitmap[bb] |= mask;
		mask <<= 1;
		if (mask == 0)	{	bb++;	mask = 1;	}
	}	
	return true;
}

int CRamDisk::IoCtrl(int mode, UINT cmd, void* arg)
{
	int error = 0;
	//	struct brd_device* brd = bdev->bd_disk->private_data;

	switch (cmd)
	{
	case COW_BRD_SNAPSHOT:
		if (m_brd_device.is_snapshot)	return -ENOTTY;
		m_brd_device.is_writable = false;
		break;
	case COW_BRD_UNSNAPSHOT:
		if (m_brd_device.is_snapshot)	return -ENOTTY;
		m_brd_device.is_writable = true;
		break;
	case COW_BRD_RESTORE_SNAPSHOT:
		if (!m_brd_device.is_snapshot)	return -ENOTTY;
		//<YUAN> 通过删除page，已恢复raw device状态。
		FreePages();
		//brd_free_pages(brd);
		break;
	case COW_BRD_WIPE:
		if (m_brd_device.is_snapshot)	return -ENOTTY;
		// Assumes no snapshots are being used right now.
		FreePages();
		//brd_free_pages(brd);
		break;
	default:
		error = -ENOTTY;
	}
	return error;
}

void CRamDisk::Initialize(size_t sector_size, size_t cap, int id, CRamDisk* raw_disk)
{
	JCASSERT(m_data == NULL);
	m_sector_size = sector_size;
	m_secs = cap;
	m_size = m_sector_size * m_secs;

	m_data = new BYTE[m_size];
	// 计算块个数
//	size_t blk_num = (m_secs - 1) / 8 + 1;
	m_bitmap_len = (m_secs - 1) / 64 + 1;
	m_bitmap = new UINT64[m_bitmap_len];
	memset(m_bitmap, 0, sizeof(UINT64) * m_bitmap_len);

	if (raw_disk)
	{
		m_raw_disk = raw_disk;
		m_brd_device.is_snapshot = true;
	}
	else m_brd_device.is_snapshot = false;

	m_brd_device.brd_number = id;
	m_brd_device.is_writable = true;
}

BYTE* CRamDisk::GetDataSource(size_t offset, size_t bmp, UINT64 mask)
{
	if (m_bitmap[bmp] & mask) return m_data + offset;		// 数据已被写入，返回数据，
	else if (m_raw_disk) return m_raw_disk->GetDataSource(offset, bmp, mask);	// 否则返回raw disk的数据
	else return NULL;
}

void CRamDisk::FreePages(void)
{
	// 通过清除bitmap，起到清楚已写入数据的目的。对于snapshot, 相当于恢复到raw device状态。
	memset(m_bitmap, 0, sizeof(UINT64) * m_bitmap_len);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
#include "pch.h"
#include "../include/tester_base.h"

#include <Psapi.h>

LOCAL_LOGGER_ENABLE(L"fstester", LOGGER_LEVEL_DEBUGINFO);



bool CTesterBase::PrintProgress(INT64 ts)
{
	bool health_valid = false;
	DokanHealthInfo health;
	memset(&health, 0, sizeof(DokanHealthInfo));
	if (m_fsinfo_file)
	{
		DWORD read = 0;
		BOOL br = ReadFile(m_fsinfo_file, &health, sizeof(DokanHealthInfo), &read, nullptr);
		if (br && read > 0) health_valid = true;
	}

	HANDLE handle = GetCurrentProcess();
	PROCESS_MEMORY_COUNTERS_EX pmc = { 0 };
	GetProcessMemoryInfo(handle, (PROCESS_MEMORY_COUNTERS*)&pmc, sizeof(pmc));

	//IVirtualDisk::HEALTH_INFO hinfo;
	//bool br1 = m_dev->GetHealthInfo(hinfo);
	//if (!br1) LOG_ERROR(L"failed on getting disk health info");


	ULONGLONG free_bytes = 0, total_bytes = 0, total_free_bytes = 0;
	//bool br2 = m_fs->DokanGetDiskSpace(free_bytes, total_bytes, total_free_bytes);
	//if (!br2) LOG_ERROR(L"failed on getting fs space");

	float usage = (float)(total_bytes - free_bytes) / total_bytes * 100;
	//wprintf_s(L"ts=%llds, op=%d, fs_usage=%.1f%%, disk_usage=%d, write=%d, mem=%.1fMB \n",
	//	ts, m_op_sn, usage, /*m_total_block - hinfo.empty_block*/0, /*hinfo.media_write*/0,
	//	(float)pmc.WorkingSetSize / 1024.0);

	if (health_valid)
	{
		wprintf_s(L"ts=%llds, op=%d, total_blocks=%lld, host_write=%lld(blk), media_write=%lld(blk), mem=%.1fMB \n",
			ts, m_op_sn, health.m_total_block_nr, health.m_block_host_write, health.m_block_disk_write, (float)pmc.WorkingSetSize / (1024.0*1024.0));
	}
	else
	{
		wprintf_s(L"ts=%llds, op=%d, mem=%.1fMB \n", ts, m_op_sn, (float)pmc.WorkingSetSize / (1024.0*1024.0));

	}
	return true;
}

DWORD CTesterBase::Monitor(void)
{
	wprintf_s(L"start monitoring, message=%d, timeout=%d\n", m_message_interval, m_timeout);
	boost::posix_time::ptime ts_update = boost::posix_time::microsec_clock::local_time();;

	while (InterlockedAdd(&m_running, 0))
	{
		DWORD ir = WaitForSingleObject(m_monitor_event, m_timeout);
		boost::posix_time::ptime ts_cur = boost::posix_time::microsec_clock::local_time();
		INT64 ts = (ts_cur - m_ts_start).total_seconds();
		if ((ts_cur - ts_update).total_seconds() > m_message_interval)
		{	// update lot
			// get memory info
			bool br = PrintProgress(ts);
			if (!br) THROW_ERROR(ERR_USER, L"failed on getting space or health");
			ts_update = ts_cur;
		}
		if (ir == WAIT_TIMEOUT)
		{
			wprintf_s(L"ts=%llds, test failed: timeout.\n", ts);
			break;
		}
	}
	if (m_fsinfo_file) CloseHandle(m_fsinfo_file);
	wprintf_s(L"finished testing\n");

	return 0;
}

void CTesterBase::SetLogFile(const std::wstring& log_fn)
{
	if (!log_fn.empty())
	{
		m_log_file = _wfsopen(log_fn.c_str(), L"w+", _SH_DENYNO);
		if (!m_log_file) THROW_ERROR(ERR_USER, L"failed on opening log file %s", log_fn.c_str());
	}
}

void CTesterBase::FillFile(IFileInfo* file, DWORD fid, DWORD revision, size_t start, size_t len)
{
	LOG_DEBUG(L"sizeof FILE_FILL_DATA = %lld", sizeof(FILE_FILL_DATA));
	JCASSERT(sizeof(FILE_FILL_DATA) == 16);
	static const size_t data_nr = FILE_BUF_SIZE / sizeof(FILE_FILL_DATA);
	static FILE_FILL_DATA buf[data_nr];

	size_t aligned_start = round_down<size_t>(start, FILE_BUF_SIZE);
	size_t offset = start - aligned_start;
	//LONG start_lo = (LONG)(start & 0xFFFFFFFF);
	//LONG start_hi = (LONG)(start >> 32);
	//SetFilePointer(file, start_lo, &start_hi, FILE_BEGIN);

	UINT64 remain = len;
	DWORD index = 0;
	while (remain > 0)	// for each buffer
	{
		// fill buffer
		for (size_t ii = 0; ii < data_nr; ii++)
		{
			buf[ii].fid = fid;
			buf[ii].rev = (WORD)revision;
			buf[ii].offset = ii + index * data_nr;
			buf[ii].dummy = 0x55;
			buf[ii].checksum = 0;
			BYTE* c = (BYTE*)(buf + ii);
			for (size_t jj = 0; jj < 15; ++jj) buf[ii].checksum += c[jj];
		}

		DWORD to_write = (DWORD)min(remain, FILE_BUF_SIZE);
		DWORD written = 0;
		//		BOOL br = WriteFile(file, buf + offset, to_write, &written, nullptr);
		//LOG_TRACK(L"trace", L",WRITE,fn=%s,fid=%d,remain=%lld,len=%d", info.file_name.c_str(), info.fid, remain, to_write);
		bool br = file->DokanWriteFile(buf + offset, to_write, written, start);
		if (!br || written < to_write)
		{
			THROW_ERROR(ERR_APP, L"failed on write to file:fid=%d, request size=%d, written size=%d", fid, to_write, written);
		}
		index++;
		start += written;
		offset = 0;
		remain -= written;
	}
}

// offset和len必须与FILE_FILL_DATA对齐
void CTesterBase::FillBuffer(void* buf, DWORD fid, DWORD revision, size_t offset, size_t len)
{
	JCASSERT(sizeof(FILE_FILL_DATA) == 16);
	size_t index = offset / sizeof(FILE_FILL_DATA);
	size_t slot_num = len / sizeof(FILE_FILL_DATA);

	FILE_FILL_DATA* data = (FILE_FILL_DATA*)(buf);
	for (size_t ii = 0; ii < slot_num; ++ii)
	{
		data[ii].fid = fid;
		data[ii].rev = (WORD)revision;
		data[ii].offset = index + ii;
		data[ii].dummy = 0x55;
		data[ii].checksum = 0;
		BYTE* c = (BYTE*)(data + ii);
		for (size_t jj = 0; jj < 15; ++jj) data[ii].checksum += c[jj];
	}
}

void CTesterBase::FileVerify(const std::wstring& fn, DWORD fid, DWORD revision, size_t len)
{
	jcvos::auto_interface<IFileInfo> file;
	m_fs->DokanCreateFile(file, fn, GENERIC_READ, 0, IFileSystem::FS_OPEN_EXISTING, 0, 0, false);

	if (file == nullptr) THROW_ERROR(ERR_APP, L"failed on opening file: %s", fn.c_str());


	JCASSERT(sizeof(FILE_FILL_DATA) == 16);
	static const size_t data_nr = FILE_BUF_SIZE / sizeof(FILE_FILL_DATA);
	static FILE_FILL_DATA buf[data_nr];

	//LONG start_lo = (LONG)(start & 0xFFFFFFFF);
	//LONG start_hi = (LONG)(start >> 32);
	//SetFilePointer(file, start_lo, &start_hi, FILE_BEGIN);
	size_t offset = 0;
	UINT64 remain = len;
	DWORD index = 0;
	while (remain > 0)	// for each buffer
	{
		DWORD read;
		DWORD to_read = (DWORD) min(remain, FILE_BUF_SIZE);
		bool br = file->DokanReadFile(buf, to_read, read, offset);
		if (!br || read < to_read)
		{
			THROW_ERROR(ERR_APP, L"failed on write to file:fid=%d, request size=%d, written size=%d", fid, to_read, read);
		}
		// fill buffer
		for (size_t ii = 0; (ii < data_nr) && (remain >=sizeof(FILE_FILL_DATA)); ii++)
		{
			BYTE checksum = 0;
			BYTE* c = (BYTE*)(buf + ii);
			for (size_t jj = 0; jj < 15; ++jj) checksum += c[jj];
			if ((buf[ii].fid != fid) || (revision != 0 && buf[ii].rev != revision) || (buf[ii].offset != ii + index * data_nr) || (buf[ii].checksum != checksum))
			{
				wprintf_s(L"[err] miscompare at index = %lld\n", (index * data_nr + ii));
				wprintf_s(L"fid: ref=%d, file=%d\n", fid, buf[ii].fid);
				wprintf_s(L"rev: ref=%d, file=%d\n", revision, buf[ii].rev);
				wprintf_s(L"offset : ref=%lld, file=%lld\n", (index*data_nr +ii), buf[ii].offset);
				wprintf_s(L"checksum: ref=%d, file=%d\n", checksum,buf[ii].checksum);	
				THROW_ERROR(ERR_APP, L"data mismatch");
			}
			remain -= sizeof(FILE_FILL_DATA);
		}
		index ++;
		offset += read;
	}
	file->CloseFile();
}

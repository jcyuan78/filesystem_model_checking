#include "pch.h"

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#include "wrapper_dev.h"
#include <boost/cast.hpp>

LOCAL_LOGGER_ENABLE(L"crashmonkey.wrapper", LOGGER_LEVEL_DEBUGINFO);



#ifdef _TO_BE_IMPLEMENTED_
static bool should_log(struct bio* bio)
{
	return
		((bio->BI_RW & REQ_FUA) || (bio->BI_RW & REQ_PREFLUSH) ||
			(bio->BI_RW & REQ_OP_FLUSH) || (bio_op(bio) == REQ_OP_WRITE) ||
			(bio_op(bio) == BIO_DISCARD_FLAG) ||
			(bio_op(bio) == REQ_OP_SECURE_ERASE) ||
			(bio_op(bio) == REQ_OP_WRITE_SAME) ||
			(bio_op(bio) == REQ_OP_WRITE_ZEROES));

}

/*
 * Debug output to dmesg to see what is happening. Only tested on 3.13 and 4.4
 * kernels (and mostly accurrate on 4.4). Only enabled for <= 4.4 kernels
 * because I'm too lazy to do all the flag translations for this in 4.15. See
 * the output log of a workload for the textual values CrashMonkey spits out.
 */
static void print_rw_flags(unsigned long rw, unsigned long flags)
{
	int i;
	LOG_NOTICE(L "\traw rw flags: 0x%.8lx\n", rw);
	for (i = __REQ_WRITE; i < __REQ_NR_BITS; i++)
	{
		if (rw & (1ULL << i))
		{
			LOG_NOTICE(L "\t%s\n", flag_names[i]);
		}
	}
	LOG_NOTICE(L "\traw flags flags: %.8lx\n", flags);
	for (i = __REQ_WRITE; i < __REQ_NR_BITS; i++)
	{
		if (flags & (1ULL << i))
		{
			LOG_NOTICE(L "\t%s\n", flag_names[i]);
		}
	}
}

#endif


CWrapperDisk::~CWrapperDisk(void)
{
	RemoveAllLogs();
	RELEASE(m_target_dev);
}

bool CWrapperDisk::ReadSectors(void* buf, size_t lba, size_t secs)
{
	JCASSERT(m_target_dev);
	return m_target_dev->ReadSectors(buf, lba, secs);
}

bool CWrapperDisk::WriteSectors(void* buf, size_t lba, size_t secs)
{
	/*
	LOG_NOTICE(L "hwm: bio rw of size %u headed for 0x%lx (sector 0x%lx)"
		" has flags:\n", bio->BI_SIZE, bio->BI_SECTOR * 512, bio->BI_SECTOR);
	print_rw_flags(bio->BI_RW, bio->bi_flags);
	*/
	// Log information about writes, fua, and flush/flush_seq events in kernel memory.
	try
	{
		if (m_hwm_dev.log_on)
		{
			LOG_DEBUG_(1, L"hwm: bio rw of size %zd headed for sector 0x%zX has flags : ", secs, lba);
//			print_rw_flags(bio->BI_RW, bio->bi_flags);

			// Log data to disk logs.
			jcvos::auto_ptr<disk_write_op> 	write(new disk_write_op);
			if (write == NULL)	THROW_ERROR(ERR_MEM, L"hwm: unable to make new write node ");
			write->metadata.bi_flags = HWM_WRITE_FLAG;
			write->metadata.bi_rw = HWM_WRITE_FLAG;
			write->metadata.write_sector = boost::numeric_cast<UINT>(lba);
			write->metadata.size = boost::numeric_cast<UINT>(secs);
			write->metadata.time_ns = jcvos::GetTimeStamp();

			//<YUAN> 内存处理，交换顺序。
			size_t data_size = secs * SECTOR_SIZE;
			BYTE * data = new BYTE[data_size];
			if (data == NULL)	THROW_ERROR(ERR_MEM, L"hwm: unable to get memory for data logging");
			memcpy_s(data, data_size, buf, data_size);

			write->m_data.reset(data, [](BYTE* c) {delete[] c; });
			// Sanity check which prints data copied to the log.
			InsertLog(write.detach());

//			// Protect playing around with our list of logged bios.
//#ifdef  _TO_BE_IMPLEMENTED_
//			spin_lock(&m_hwm_dev.lock);
//#endif //  _TO_BE_IMPLEMENTED_
//
//			if (m_hwm_dev.current_write == NULL)
//			{	// With the default first checkpoint, this case should never happen.
//				LOG_WARNING(L"hwm: found empty list of previous disk ops");
//				// This is the first write in the log.
//				m_hwm_dev.writes = write;
//				// Set the first write in the log so that it's picked up later.
//				m_hwm_dev.current_log_write = write;
//			}
//			else
//			{	// Some write(s) was/were already made so add this to the back of the chain and update pointers.
//				m_hwm_dev.current_write->next = write;
//			}
////			m_hwm_dev.current_write = write;
//			m_hwm_dev.current_write = write.detach();
//#ifdef  _TO_BE_IMPLEMENTED_
//			spin_unlock(&m_hwm_dev.lock);
//#endif //  _TO_BE_IMPLEMENTED_
		}
	}
	catch (jcvos::CJCException& err)
	{
	}
	JCASSERT(m_target_dev);
	return m_target_dev->WriteSectors(buf, lba, secs);
}

bool CWrapperDisk::FlushData(UINT lba, size_t secs)
{
	LOG_STACK_TRACE();
	if (m_hwm_dev.log_on)
	{
		jcvos::auto_ptr<disk_write_op> write(new disk_write_op);
		if (write == NULL) THROW_ERROR(ERR_MEM, L"hwm: unable to make new write node");
		write->metadata.bi_flags = HWM_SYNC_FLAG | HWM_FLUSH_FLAG;
		write->metadata.bi_rw = HWM_SYNC_FLAG | HWM_FLUSH_FLAG;
		write->metadata.write_sector = 0;
		write->metadata.size = 0;
		write->metadata.time_ns = jcvos::GetTimeStamp();
		write->m_data.reset();
//		write->data = NULL;

		InsertLog(write.detach());
	}
	return true;
}

int CWrapperDisk::IoCtrl(int mode, UINT cmd, void* arg)
{
	int ret = 0;
	switch (cmd)
	{
	case HWM_LOG_OFF:
		LOG_NOTICE(L"hwm: turning off data logging");
		m_hwm_dev.log_on = false;
		break;

	case HWM_LOG_ON:
		LOG_NOTICE(L"hwm: turning on data logging");
		m_hwm_dev.log_on = true;
		break;

	case HWM_GET_LOG_META:
		//LOG_NOTICE(L "hwm: getting next log entry meta\n");
		if (m_hwm_dev.current_log_write == NULL)
		{
			LOG_ERROR(L"[err] hwm: no log entry here");
			return -ENODATA;
		}

		//<YUAN> 由于在用户空间运行，不需要检查权限。
		//if (!access_ok(VERIFY_WRITE, (void*)arg, sizeof(struct disk_write_op_meta)))
		//{	// TODO(ashmrtn): Find right error code.
		//	LOG_WARNING(L"hwm: bad user land memory pointer in log entry size\n");
		//	return -EFAULT;
		//}
		// Copy metadata.
		memcpy_s(arg, sizeof(disk_write_op_meta), &m_hwm_dev.current_log_write->metadata, sizeof(disk_write_op_meta));
		break;
	case HWM_GET_LOG_DATA: {
		JCASSERT(0);
		//if (m_hwm_dev.current_log_write == NULL)
		//{
		//	LOG_ERROR(L"[err] hwm: no log entries to report data for");
		//	return -ENODATA;
		//}
		//size_t data_size = m_hwm_dev.current_log_write->metadata.size * SECTOR_SIZE;
		//memcpy_s(arg, data_size, m_hwm_dev.current_log_write->data, data_size);
	} break;

	case HWM_NEXT_ENT:
		//LOG_NOTICE(L "hwm: moving to next log entry\n");
		if (m_hwm_dev.current_log_write == NULL)
		{
			LOG_ERROR(L"[err] hwm: no next log entry");
			return -ENODATA;
		}
		m_hwm_dev.current_log_write = m_hwm_dev.current_log_write->next;
		break;

	case HWM_CLR_LOG:
		LOG_NOTICE(L"hwm: clearing data logs");
		FreeLogs();
		break;

	case HWM_CHECKPOINT: {
		LOG_NOTICE(L"hwm: making checkpoint in log");
		// Create a new log entry that just says we got a checkpoint.
		disk_write_op* checkpoint = new disk_write_op;
		if (checkpoint == NULL)	THROW_ERROR(ERR_MEM, L"hwm: error allocating checkpoint");

		checkpoint->metadata.bi_rw = HWM_CHECKPOINT_FLAG;
		checkpoint->metadata.bi_flags = HWM_CHECKPOINT_FLAG;
		checkpoint->metadata.time_ns = jcvos::GetTimeStamp();
		//<YUAN> checkpoing增加注释功能
		const std::wstring* msg = reinterpret_cast<std::wstring*>(arg);
		if (msg && !msg->empty())
		{	//表示message时，size为byte单位
			size_t len = msg->size() + 1;
			checkpoint->metadata.size = boost::numeric_cast<unsigned int>(len * sizeof(wchar_t));
			wchar_t* str = new wchar_t[len * sizeof(wchar_t)];
			wcscpy_s(str, len, msg->c_str());
			checkpoint->m_data.reset((BYTE*)str, [](BYTE* c) {delete[] c; });
//			checkpoint->data = new wchar_t[len];
		}
		else checkpoint->metadata.size = 0;
		// Aquire lock and add the new entry to the end of the list.
//		spin_lock(&m_hwm_dev.lock);
		// Assuming spinlock keeps the compiler from reordering this before the lock is aquired...
		checkpoint->metadata.write_sector = m_hwm_dev.current_checkpoint;
		++m_hwm_dev.current_checkpoint;
		InsertLog(checkpoint);
		//m_hwm_dev.current_write->next = checkpoint;
		//m_hwm_dev.current_write = checkpoint;
		// Drop lock and return success.
//		spin_unlock(&m_hwm_dev.lock);
		} break;
	default:
		ret = -EINVAL;
	}
	return ret;
}

int CWrapperDisk::GetWriteLog(fs_testing::utils::disk_write& log)
{
	if (m_hwm_dev.current_log_write == NULL)
	{
		LOG_ERROR(L"[err] hwm: no log entry here");
		return -ENODATA;
	}
	log.metadata = m_hwm_dev.current_log_write->metadata;
	if (log.metadata.size > 0) log.set_data(m_hwm_dev.current_log_write->m_data);
	return 0;
}

bool CWrapperDisk::Initialize(IVirtualDisk* target_dev)
{
	LOG_STACK_TRACE(L"hwm: Hello World from module\n");

	if (target_dev == NULL) THROW_ERROR(ERR_APP, L"target device cannot be null");
	m_target_dev = target_dev;
	m_target_dev->AddRef();

	// Get memory for our starting disk epoch node.
	m_hwm_dev.log_on = false;
	// Make a checkpoint marking the beginning of the log. This will be useful when watches are implemented and people
	//	begin a watch at the very start of a test.
	m_hwm_dev.current_checkpoint = 1;

	struct disk_write_op* first = new disk_write_op;

	// Create default first checkpoint at start of log.
	if (first == NULL)	THROW_ERROR(ERR_MEM, L"hwm: error allocating default checkpoint");
	first->metadata.bi_flags = HWM_CHECKPOINT_FLAG;
	first->metadata.bi_rw = HWM_CHECKPOINT_FLAG;
	first->metadata.write_sector = 0;
	first->metadata.size = 0;
	first->metadata.time_ns = jcvos::GetTimeStamp();

	m_hwm_dev.current_write = NULL;
	InsertLog(first);
	//m_hwm_dev.current_write = first;
	//m_hwm_dev.writes = first;
	//m_hwm_dev.current_log_write = first;
	m_hwm_dev.current_checkpoint = 1;


	// Get registered.
	//target_device = blkdev_get_by_path(target_device_path, FMODE_READ, &m_hwm_dev);
	//if (!target_device || IS_ERR(target_device))
	//{
	//	LOG_ERROR(L "hwm: unable to grab underlying device\n");
	//	goto out;
	//}
	//if (!target_device->bd_queue)
	//{
	//	LOG_ERROR(L "hwm: attempt to wrap device with no request queue\n");
	//	goto out;
	//}
	//if (!target_device->bd_queue->make_request_fn)
	//{
	//	LOG_ERROR(L "hwm: attempt to wrap device with no "
	//		"make_request_fn\n");
	//	goto out;
	//}

	//m_hwm_dev.target_dev = target_device->bd_disk;
	//m_hwm_dev.target_partno = target_device->bd_partno;
	//m_hwm_dev.target_bd = target_device;

//<YUAN> 目前不太确定flag device的用处
#ifdef _TO_BE_IMPLEMENTED_
	// Get the device we should copy flags from and copy those flags into locals.
	flags_device = blkdev_get_by_path(flags_device_path, FMODE_READ, &m_hwm_dev);
	if (!flags_device || IS_ERR(flags_device))
	{
		LOG_ERROR(L "hwm: unable to grab device to clone flags\n");
		goto out;
	}
	if (!flags_device->bd_queue)
	{
		LOG_ERROR(L "hwm: attempt to wrap device with no request queue\n");
		goto out;
	}
	queue_flags = flags_device->bd_queue->queue_flags;
	blkdev_put(flags_device, FMODE_READ);
#endif
	// Set up our internal device.
//	spin_lock_init(&m_hwm_dev.lock);

	// And the gendisk structure.
	//m_hwm_dev.gd = alloc_disk(1);
	//if (!m_hwm_dev.gd) { goto out; }

	//m_hwm_dev.gd->private_data = &m_hwm_dev;
	//m_hwm_dev.gd->major = major_num;
	//m_hwm_dev.gd->first_minor = target_device->bd_disk->first_minor;
	//m_hwm_dev.gd->minors = target_device->bd_disk->minors;
	//set_capacity(m_hwm_dev.gd, get_capacity(target_device->bd_disk));
	//strcpy(m_hwm_dev.gd->disk_name, "hwm");
	//m_hwm_dev.gd->fops = &disk_wrapper_ops;

	// Get a request queue.
//	m_hwm_dev.gd->queue = blk_alloc_queue(GFP_KERNEL);
//	if (m_hwm_dev.gd->queue == NULL) { goto out; }
//	blk_queue_make_request(m_hwm_dev.gd->queue, disk_wrapper_bio);
#ifdef _TO_BE_IMPLEMENTED_
	// Make this queue have the same flags as the queue we're feeding into.
	m_hwm_dev.gd->queue->queue_flags = queue_flags;
	m_hwm_dev.gd->queue->queuedata = &m_hwm_dev;
	LOG_NOTICE(L"hwm: working with queue with:\n\tflags 0x%lx\n", m_hwm_dev.gd->queue->queue_flags);
	add_disk(m_hwm_dev.gd);
#endif
	LOG_NOTICE(L"hwm: initialized");
	return 0;

//out:
//	unregister_blkdev(major_num, "hwm");
//	return -ENOMEM;
}

void CWrapperDisk::FreeLogs(void)
{
	// Remove all writes.
	RemoveAllLogs();
	struct disk_write_op* first = new disk_write_op;

	// Create default first checkpoint at start of log.
	if (first == NULL)	THROW_ERROR(ERR_MEM, L"hwm: error allocating default checkpoint");

	first->metadata.write_sector = 0;
	first->metadata.bi_flags = HWM_CHECKPOINT_FLAG;
	first->metadata.bi_rw = HWM_CHECKPOINT_FLAG;
	first->metadata.size = 0;
	first->metadata.time_ns = jcvos::GetTimeStamp();
	//m_hwm_dev.current_write = first;
	//m_hwm_dev.writes = first;
	//m_hwm_dev.current_log_write = first;
	InsertLog(first);
	m_hwm_dev.current_checkpoint = 1;
}

void CWrapperDisk::RemoveAllLogs(void)
{
	struct disk_write_op* w = m_hwm_dev.writes;
	struct disk_write_op* tmp_w;
	while (w != NULL)
	{
		tmp_w = w;
		w = w->next;
		delete tmp_w;
	}
	m_hwm_dev.writes = NULL;
	m_hwm_dev.current_write = NULL;
	m_hwm_dev.current_log_write = NULL;

}

void CWrapperDisk::InsertLog(disk_write_op* write)
{
#ifdef _DEBUG
	static int index = 0;
#endif
	// Protect playing around with our list of logged bios.
#ifdef  _TO_BE_IMPLEMENTED_
	spin_lock(&m_hwm_dev.lock);
#endif //  _TO_BE_IMPLEMENTED_

	if (m_hwm_dev.current_write == NULL)
	{	// With the default first checkpoint, this case should never happen.
//			LOG_WARNING(L"hwm: found empty list of previous disk ops");
			// This is the first write in the log.
		m_hwm_dev.writes = write;
		// Set the first write in the log so that it's picked up later.
		m_hwm_dev.current_log_write = write;
	}
	else
	{	// Some write(s) was/were already made so add this to the back of the chain and update pointers.
		m_hwm_dev.current_write->next = write;
	}
	//			m_hwm_dev.current_write = write;
	m_hwm_dev.current_write = write;
	LOG_DEBUG(L"insert log:%d, type=0x%X, lba=0x%X, secs=0x%0X", index++, write->metadata.bi_rw,
		write->metadata.write_sector, write->metadata.size);
#ifdef  _TO_BE_IMPLEMENTED_
	spin_unlock(&m_hwm_dev.lock);
#endif //  _TO_BE_IMPLEMENTED_

}
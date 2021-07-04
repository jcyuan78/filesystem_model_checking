#include "pch.h"
//#include <endian.h>
//#include <errno.h>
//#include <fcntl.h>
//#include <string.h>
//#include <sys/mount.h>
//#include <sys/stat.h>
//#include <sys/types.h>
//#include <unistd.h>
//
//#include <cassert>
//#include <cerrno>
//#include <cstdio>
//#include <cstdlib>
//
//#include <algorithm>
#include <chrono>
//#include <fstream>
#include <iomanip>
//#include <iostream>
//#include <memory>
//#include <string>
//#include <utility>

#include "FsSpecific.h"
#include "Tester.h"
//#include "../disk_wrapper_ioctl.h"
#include <crashmonkey_drive.h>

#include "DiskContents.h"

LOCAL_LOGGER_ENABLE(L"crashmonke.tester", LOGGER_LEVEL_DEBUGINFO);


#define TEST_CLASS_FACTORY          L"test_case_get_instance"
#define TEST_CLASS_DEFACTORY        L"test_case_delete_instance"
#define PERMUTER_CLASS_FACTORY      L"permuter_get_instance"
#define PERMUTER_CLASS_DEFACTORY    L"permuter_delete_instance"

#define DIRTY_EXPIRE_TIME_PATH      L"/proc/sys/vm/dirty_expire_centisecs"
#define DROP_CACHES_PATH            L"/proc/sys/vm/drop_caches"

#define FULL_WRAPPER_PATH           L"/dev/hwm"

// TODO(ashmrtn): Make a quiet and regular version of commands.
// TODO(ashmrtn): Make so that commands work with user given device path.
#define SILENT                      L" > /dev/null 2>&1"

#define MNT_WRAPPER_DEV_PATH FULL_WRAPPER_PATH
#define MNT_MNT_POINT               L"/mnt/snapshot"

#define PART_PART_DRIVE             L"fdisk "
#define PART_PART_DRIVE_2           L" << EOF\no\nn\np\n1\n\n\nw\nEOF\n"
#define PART_DEL_PART_DRIVE         L"fdisk "
#define PART_DEL_PART_DRIVE_2       L" << EOF\no\nw\nEOF\n"

#define WRAPPER_MODULE_NAME         L"../build/disk_wrapper.ko"
#define WRAPPER_INSMOD              L"insmod " WRAPPER_MODULE_NAME L" target_device_path="
#define WRAPPER_INSMOD2             L" flags_device_path="
#define WRAPPER_RMMOD               L"rmmod " WRAPPER_MODULE_NAME

#define COW_BRD_MODULE_NAME L"../build/cow_brd.ko"
#define COW_BRD_INSMOD      L"insmod " COW_BRD_MODULE_NAME L" num_disks="
#define COW_BRD_INSMOD2     L" num_snapshots="
#define COW_BRD_INSMOD3     L" disk_size="
#define COW_BRD_RMMOD       L"rmmod " COW_BRD_MODULE_NAME
#define NUM_DISKS           1
#define NUM_SNAPSHOTS       20
#define COW_BRD_PATH        L"/dev/cow_ram0"

#define DEV_SECTORS_PATH    "/sys/block/"
#define DEV_SECTORS_PATH_2  "/size"

//#define SECTOR_SIZE 512

//namespace fs_testing
//{
using fs_testing::Tester;

using std::calloc;
using std::cerr;
using std::chrono::steady_clock;
using std::chrono::duration;
using std::chrono::duration_cast;
using std::chrono::milliseconds;
using std::chrono::time_point;
using std::cout;
using std::endl;
using std::free;
using std::ifstream;
using std::ios;
//    using std::ostream;
using std::wofstream;
using std::pair;
using std::shared_ptr;
//using std::string;
using std::to_string;
using std::vector;

using fs_testing::tests::test_create_t;
using fs_testing::tests::test_destroy_t;
using fs_testing::permuter::Permuter;
using fs_testing::permuter::permuter_create_t;
using fs_testing::permuter::permuter_destroy_t;
using fs_testing::utils::disk_write;
using fs_testing::utils::DiskMod;
using fs_testing::utils::DiskWriteData;

Tester::Tester(const size_t dev_size, const unsigned int sector_size, const bool verbosity)
	: device_size(dev_size), sector_size_(sector_size), verbose(verbosity)
{
	snapshot_path_ = L"/dev/cow_ram_snapshot1_0";
	m_cow_brd = nullptr;
	m_ioctl_dev = nullptr;
	m_ram_drive = nullptr;
}

Tester::~Tester()
{
	//if (fs_specific_ops_ != NULL) { delete fs_specific_ops_; }
	RELEASE(m_cow_brd);
	RELEASE(m_ioctl_dev);
	RELEASE(m_ram_drive);
}

void Tester::set_fs_type(const std::wstring type)
{
	fs_type = type;
	fs_specific_ops_ = GetFsSpecific(fs_type);
	assert(fs_specific_ops_ != NULL);
}

void Tester::set_device(const std::wstring device_path)
{
	device_raw = device_path;
	device_mount = device_raw;
}

void Tester::set_flag_device(const std::wstring device_path)
{
	flags_device = device_path;
}

void Tester::StartTestSuite()
{       // Construct a new element at the end of our vector.
	test_results_.emplace_back();
	current_test_suite_ = &test_results_.back();
}

void Tester::EndTestSuite()
{
	current_test_suite_ = NULL;
}

unsigned int Tester::GetPostRunDelay()
{
	return fs_specific_ops_->GetPostRunDelaySeconds();
}

int Tester::clone_device()
{
	JCASSERT(m_cow_brd);
	std::wcout << L"cloning device " << device_raw << std::endl;
	// <YUAN> COW_BRD_SNAPSHOT：只是清除writable标志。
//        if (ioctl(cow_brd_fd, COW_BRD_SNAPSHOT) < 0)
	if (m_cow_brd->IoCtrl(0, COW_BRD_SNAPSHOT, nullptr) < 0) { return DRIVE_CLONE_ERR; }
	return SUCCESS;
}

#define BLKRRPART	0

//    int Tester::clone_device_restore(int snapshot_fd, bool reread) 
int Tester::clone_device_restore(IVirtualDisk* snapshot, bool reread)
{
	JCASSERT(snapshot);
	//        if (ioctl(snapshot_fd, COW_BRD_RESTORE_SNAPSHOT) < 0) {
	if (snapshot->IoCtrl(0, COW_BRD_RESTORE_SNAPSHOT, nullptr) < 0)
	{
		return DRIVE_CLONE_RESTORE_ERR;
	}
	int res;
	if (reread)
	{   // TODO(ashmrtn): Fixme by moving me to a better place.
		do
		{	//<YUAN> 重新读取分区表。
			//res = ioctl(snapshot_fd, BLKRRPART, NULL);
			res = snapshot->IoCtrl(0, BLKRRPART, nullptr);
		} while (errno == EBUSY);
		if (res < 0)
		{
			int errnum = errno;
			cerr << "Error re-reading partition table " << errnum << endl;
		}
	}
	return SUCCESS;
}

int Tester::mount_device_raw(const wchar_t* opts)
{
	if (device_mount.empty())
	{
		return MNT_BAD_DEV_ERR;
	}
	return mount_device(device_mount.c_str(), opts);
}

int Tester::mount_wrapper_device(const wchar_t* opts)
{
	// TODO(ashmrtn): Make some sort of boolean that tracks if we should use the
	// first parition or not?
	std::wstring dev(MNT_WRAPPER_DEV_PATH);
	//dev += "1";
	return mount_device(dev.c_str(), opts);
}

int Tester::mount_device(const wchar_t* dev, const wchar_t* opts)
{
	LOG_NOTICE(L"mount device %s to %s, opts=%s", dev, MNT_MNT_POINT, opts);
	//if (mount(dev, MNT_MNT_POINT, fs_type.c_str(), 0, (void*)opts) < 0) 
	//{
	//    disk_mounted = false;
	//    return MNT_MNT_ERR;
	//}
	disk_mounted = true;
	return SUCCESS;
}

int Tester::umount_device()
{
	LOG_NOTICE(L"unmount device from %s", MNT_MNT_POINT);
	//if (disk_mounted) {
	//    if (umount(MNT_MNT_POINT) < 0) {
	//        disk_mounted = true;
	//        return MNT_UMNT_ERR;
	//    }
	//}
	disk_mounted = false;
	return SUCCESS;
}

int Tester::mount_snapshot()
{
	LOG_NOTICE(L"mount snapshot %s to %s", snapshot_path_.c_str(), MNT_MNT_POINT);
	//if (mount(snapshot_path_.c_str(), MNT_MNT_POINT, fs_type.c_str(), 0, NULL) < 0) {
	//    return MNT_MNT_ERR;
	//}
	return SUCCESS;
}

int Tester::umount_snapshot()
{
	LOG_NOTICE(L"umount snapshot from %s", MNT_MNT_POINT);
	//if (umount(MNT_MNT_POINT) < 0) {
	//    return MNT_UMNT_ERR;
	//}
	return SUCCESS;
}

int Tester::mapCheckpointToSnapshot(int checkpoint)
{
	if (checkpointToSnapshot_.find(checkpoint) != checkpointToSnapshot_.end())
	{
		return -1;
	}
	checkpointToSnapshot_[checkpoint] = snapshot_path_;
	std::wcout << L"Mapping " << snapshot_path_ << L" to checkpoint " << checkpoint << std::endl;
	return 0;
}

int Tester::getNewDiskClone(int checkpoint)
{
	std::wstring new_snapshot_path;
	std::wstring path(snapshot_path_);
	std::wstring device_number = path.substr(path.rfind('_'));
	std::wstring snapshot_number = std::to_wstring(checkpoint + 2);
	new_snapshot_path = L"/dev/cow_ram_snapshot";
	new_snapshot_path += snapshot_number;
	new_snapshot_path += device_number;
	// Finally set snapshot_path_ to the new snapshot path
	snapshot_path_ = new_snapshot_path;
	std::wstring command = fs_specific_ops_->GetNewUUIDCommand(new_snapshot_path);
	LOG_NOTICE(L"invoke sys cmd: %s", command.c_str());
	_wsystem(command.c_str());
	return 0;
}

void Tester::getCompleteRunDiskClone()
{
	snapshot_path_ = checkpointToSnapshot_[0];
}

int Tester::insert_cow_brd(IVirtualDisk* cow_brd)
{
	JCASSERT(m_cow_brd == NULL);
	//m_cow_brd = cow_brd;
	//m_cow_brd->AddRef();

	JCASSERT(0);	//<YUAN> _TO_BE_IMPLEMENTED_
//	CCowRamDrive* drive = jcvos::CDynamicInstance<CCowRamDrive>::Create();
	CCowRamDrive* drive =NULL;
	drive->Initialize(NUM_DISKS, NUM_SNAPSHOTS, device_size);

	//if (cow_brd_fd < 0)
	//{
	std::wstring command(COW_BRD_INSMOD);
	//command += NUM_DISKS;
	command += std::to_wstring(NUM_DISKS);
	command += COW_BRD_INSMOD2;
	//command += NUM_SNAPSHOTS;
	command += std::to_wstring(NUM_SNAPSHOTS);
	command += COW_BRD_INSMOD3;
	command += std::to_wstring(device_size);
	if (!verbose) { command += SILENT; }
	//<YUAN> cmd= insmod ../build/cow_brd.ko num_disks=1 num_snapshots=20 disk_size=<device_size>
	//if (_wsystem(command.c_str()) != 0)
	//{
	//    cow_brd_fd = -1;
	//    return WRAPPER_INSERT_ERR;
	//}
	LOG_NOTICE(L"[linux] system call: %s", command.c_str());
	//}
	cow_brd_inserted = true;
	//cow_brd_fd = open("/dev/cow_ram0", O_RDONLY);
	LOG_NOTICE(L"[linux] open device: /dev/cow_ram0, O_RDONLY");
	//if (cow_brd_fd < 0)
	//{
	//    if (_wsystem(COW_BRD_RMMOD) != 0)
	//    {
	//        cow_brd_fd = -1;
	//        cow_brd_inserted = false;
	//        return WRAPPER_REMOVE_ERR;
	//    }
	//}
	return SUCCESS;
}

int Tester::remove_cow_brd()
{
	// Sometimes the disk wrapper module takes time to unload.
	// So retry cow-brd unload for upto a second.
	//milliseconds elapsed;
	if (cow_brd_inserted)
	{
		//    if (cow_brd_fd != -1) {
		//        close(cow_brd_fd);
		//        cow_brd_fd = -1;
		cow_brd_inserted = false;
		//    }
		//    int res, num_tries = 0;
		std::wstring command = COW_BRD_RMMOD SILENT;
		//    time_point<steady_clock> rmmod_start_time = steady_clock::now();
		//    do {
		//        res = system(command.c_str());
		//        time_point<steady_clock> rmmod_end_time = steady_clock::now();
		//        elapsed = duration_cast<milliseconds>(rmmod_end_time - rmmod_start_time);
		//        if (res != 0) {
		//            usleep(500);
		//            num_tries++;
		//        }
		//    } while (res != 0 && elapsed.count() < 1000);

		//    if (res != 0) {
		//        cow_brd_inserted = true;
		//        return WRAPPER_REMOVE_ERR;
		//    }
		//    //cout << "Time to rmmod " << elapsed.count() << " num tries = " << num_tries << endl;
		LOG_NOTICE(L"[linux] system call: %s", command.c_str());
		RELEASE(m_cow_brd);
	}
	return SUCCESS;
}

int Tester::insert_wrapper(IVirtualDisk* wrapper)
{
	if (!wrapper_inserted)
	{
		//JCASSERT(m_ioctl_dev == NULL && wrapper);
		//m_ioctl_dev = wrapper;
		//m_ioctl_dev->AddRef();


		std::wstring command(WRAPPER_INSMOD);
		// TODO(ashmrtn): Make this much MUCH cleaner...
		command += L"/dev/cow_ram_snapshot1_0";
		command += WRAPPER_INSMOD2;
		command += flags_device;
		if (!verbose) { command += SILENT; }
		//<YUAN> cmd= insmod ../build/disk_wrapper.ko target_device_path=/dev/cow_ram_snapshot1_0 flags_device_path=<flags_device>
		LOG_NOTICE(L"[linux] system call: %s", command.c_str());
		//if (_wsystem(command.c_str()) != 0)
		//{
		//    wrapper_inserted = false;
		//    return WRAPPER_INSERT_ERR;
		//}
	}
	wrapper_inserted = true;
	return SUCCESS;
}

int Tester::remove_wrapper()
{
	//milliseconds elapsed;
	if (wrapper_inserted)
	{
		RELEASE(m_ioctl_dev);
		//    int res, num_tries = 0;
		std::wstring command = WRAPPER_RMMOD SILENT;
		//    time_point<steady_clock> rmmod_start_time = steady_clock::now();
		//    do {
		//        res = _wsystem(command.c_str());
		LOG_NOTICE(L"[linux] system call: %s", command.c_str());
		//        time_point<steady_clock> rmmod_end_time = steady_clock::now();
		//        elapsed = duration_cast<milliseconds>(rmmod_end_time - rmmod_start_time);
		//        if (res != 0)
		//        {
		//            usleep(500);
		//            num_tries++;
		//        }
		//    } while (res != 0 && elapsed.count() < 1000);

		//    if (res != 0)
		//    {
		//        wrapper_inserted = true;
		//        return WRAPPER_REMOVE_ERR;
		//    }
		//    cout << "Time to rmmod " << elapsed.count() << " num tries = " << num_tries << endl;
	}
	wrapper_inserted = false;
	return SUCCESS;
}

int Tester::get_wrapper_ioctl()
{
	LOG_NOTICE(L"[linux] open %s, O_RDONLY | O_CLOEXEC", FULL_WRAPPER_PATH);
	//ioctl_fd = open(FULL_WRAPPER_PATH, O_RDONLY | O_CLOEXEC);
	//if (ioctl_fd == -1) { return WRAPPER_OPEN_DEV_ERR; }
	return SUCCESS;
}

void Tester::put_wrapper_ioctl()
{
	LOG_NOTICE(L"[linux] close ioctl_fd");
	//if (ioctl_fd != -1) 
	//{
	//    close(ioctl_fd);
	//    ioctl_fd = -1;
	//}
}

void Tester::begin_wrapper_logging()
{
	//if (ioctl_fd != -1)       {            ioctl(ioctl_fd, HWM_LOG_ON);        }
	if (m_ioctl_dev) m_ioctl_dev->IoCtrl(0, HWM_LOG_ON, nullptr);
}

void Tester::end_wrapper_logging()
{
	//if (ioctl_fd != -1) { ioctl(ioctl_fd, HWM_LOG_OFF); }
	if (m_ioctl_dev) m_ioctl_dev->IoCtrl(0, HWM_LOG_OFF, nullptr);
}

int Tester::get_wrapper_log()
{
	if (!m_ioctl_dev) return SUCCESS;
	//if (ioctl_fd != -1) 
	//{
	while (1)
	{
		disk_write_op_meta meta;
		//                int result = ioctl(ioctl_fd, HWM_GET_LOG_META, &meta);
		int result = m_ioctl_dev->IoCtrl(0, HWM_GET_LOG_META, &meta);
		if (result == -1)
		{
			if (errno == ENODATA) { break; }
			else if (errno == EFAULT)
			{
				cerr << "efault occurred\n";
				log_data.clear();
				return WRAPPER_DATA_ERR;
			}
		}
//		BYTE* data = new BYTE[meta.size];
		jcvos::auto_array<BYTE> data(meta.size);
		//                result = ioctl(ioctl_fd, HWM_GET_LOG_DATA, data);
		result = m_ioctl_dev->IoCtrl(0, HWM_GET_LOG_DATA, data.get_ptr());
		if (result == -1)
		{
			if (errno == ENODATA)
			{      // Should never reach here as loop will break when getting the size above.
				break;
			}
			else if (errno == EFAULT)
			{
				cerr << "efault occurred\n";
//				delete[] data;
				log_data.clear();
				return WRAPPER_MEM_ERR;
			}
		}
		log_data.emplace_back(meta, data);
//		delete[] data;

		//                result = ioctl(ioctl_fd, HWM_NEXT_ENT);
		result = m_ioctl_dev->IoCtrl(0, HWM_NEXT_ENT, 0);
		if (result == -1)
		{
			if (errno == ENODATA)
			{     // Should never reach here as loop will break when getting the size above.
				break;
			}
			else
			{
				cerr << "Error getting next log entry\n";
				log_data.clear();
				break;
			}
		}
	}
	//}
	std::wcout << L"fetched " << log_data.size() << L" log data entries" << std::endl;
	return SUCCESS;
}

void Tester::clear_wrapper_log()
{
	//if (ioctl_fd != -1) {    ioctl(ioctl_fd, HWM_CLR_LOG);  }
	if (m_ioctl_dev) m_ioctl_dev->IoCtrl(0, HWM_CLR_LOG, nullptr);
}

//int Tester::GetChangeData(const int fd)
int Tester::GetChangeData(FILE * fd)
{   // Need to read a 64-bit value, switch it to big endian to figure out how much
	// we need to read, read that new data amount, and add it all to a buffer.
	while (true)
	{       // Get the next DiskMod size.
		uint64_t buf;
//		const int read_res = read(fd, (void*)&buf, sizeof(uint64_t));
		size_t read_res = fread(&buf, sizeof(uint64_t), 1, fd);
		if (read_res == 0) { break; }      // No more data to read.
		else if (read_res != sizeof(uint64_t))
		{
			LOG_ERROR(L"[err] failed on reading change file, read=%d", read_res);
			return -1;
		}
		uint64_t next_chunk_size = be64toh(buf);

		// Read the next DiskMod.
		shared_ptr<wchar_t> data(new wchar_t[next_chunk_size], [](wchar_t* c) {delete[] c; });
		memcpy(data.get(), (void*)&buf, sizeof(uint64_t));
		unsigned long long int read_data = sizeof(uint64_t);
		while (read_data < next_chunk_size)
		{
//			const int res = read(fd, data.get() + read_data, next_chunk_size - read_data);
			size_t res = fread(data.get() + read_data, next_chunk_size - read_data, 1, fd);
			if (res != (next_chunk_size - read_data) )
			{        // We shouldn't find a size for a DiskMod without the rest of the DiskMod.
				LOG_ERROR(L"[err] failed on reading change file, read=%d", res);
				return -1;
			}
			read_data += res;
		}

		DiskMod mod;
		const int res = DiskMod::Deserialize(data, mod);
		if (res < 0) { return res; }
		if (mod.mod_type == DiskMod::kCheckpointMod)
		{      // We found a checkpoint, so switch to a new set of DiskMods.
			mods_.push_back(vector<DiskMod>());
		}
		else
		{
			if (mods_.empty())
			{        // We're just starting, so give us a place to put the mods.
				mods_.push_back(vector<DiskMod>());
			}
			// Just append this DiskMod to the end of the last set of DiskMods.
			mods_.back().push_back(mod);
		}
	}
	return SUCCESS;
}

int Tester::CreateCheckpoint()
{
	//if (ioctl_fd == -1) { return WRAPPER_DATA_ERR; }
	//if (ioctl(ioctl_fd, HWM_CHECKPOINT) == 0) { return SUCCESS; }
	if (!m_ioctl_dev) return WRAPPER_DATA_ERR;
	if (m_ioctl_dev->IoCtrl(0, HWM_CHECKPOINT, NULL) == 0) return SUCCESS;
	return WRAPPER_DATA_ERR;
}

int Tester::test_load_class(const wchar_t* path)
{
	return test_loader.load_class<test_create_t*>(path, TEST_CLASS_FACTORY, TEST_CLASS_DEFACTORY);
}

void Tester::test_unload_class()
{
	test_loader.unload_class<test_destroy_t*>();
}

int Tester::permuter_load_class(const wchar_t* path)
{
	return permuter_loader.load_class<permuter_create_t*>(path,
		PERMUTER_CLASS_FACTORY, PERMUTER_CLASS_DEFACTORY);
}

void Tester::permuter_unload_class()
{
	permuter_loader.unload_class<permuter_destroy_t*>();
}

const wchar_t* Tester::update_dirty_expire_time(const wchar_t* time)
{
	LOG_NOTICE(L"[linux] open " DIRTY_EXPIRE_TIME_PATH " to update dirty time=%s", time);
	//const int expire_fd = open(DIRTY_EXPIRE_TIME_PATH, O_RDWR | O_CLOEXEC);
	//if (!read_dirty_expire_time(expire_fd)) 
	//{
	//    close(expire_fd);
	//    return NULL;
	//}
	//if (!write_dirty_expire_time(expire_fd, time)) 
	//{
	//    // Attempt to restore old dirty expire time.
	//    if (!write_dirty_expire_time(expire_fd, dirty_expire_time)) {           }
	//    close(expire_fd);
	//    return NULL;
	//}
	//close(expire_fd);
	return dirty_expire_time;
}

bool Tester::read_dirty_expire_time(const int fd)
{
	//int bytes_read = 0;
	//do 
	//{
	//    const int res = read(fd, (void*) ((long) dirty_expire_time + bytes_read),
	//        DIRTY_EXPIRE_TIME_SIZE - 1 - bytes_read);
	//    if (res == 0) {      break;    } 
	//    else if (res < 0) {      return false;    }
	//    bytes_read += res;
	//} while (bytes_read < DIRTY_EXPIRE_TIME_SIZE - 1);
	//// Null terminate character array.
	//dirty_expire_time[bytes_read] = '\0';
	return true;
}

bool Tester::write_dirty_expire_time(const int fd, const wchar_t* time)
{
	//const int size = strnlen(time, DIRTY_EXPIRE_TIME_SIZE);
	//int bytes_written = 0;
	//do {
	//    const int res = write(fd, time + bytes_written, size - bytes_written);
	//    if (res < 0) {     return false;        }
	//    bytes_written += res;
	//} while (bytes_written < size);
	return true;
}

int Tester::partition_drive()
{
	if (device_raw.empty()) { return PART_PART_ERR; }
	std::wstring command(PART_PART_DRIVE + device_raw);
	if (!verbose) { command += SILENT; }
	command += PART_PART_DRIVE_2;
	//   if (system(command.c_str()) != 0) {   return PART_PART_ERR;  }
	   // Since we added a parition on the drive we should use the first partition.
	device_mount = device_raw + L"1";
	LOG_NOTICE(L"[linux] system command: %s, device_mount=%s", command.c_str(), device_mount.c_str());
	return SUCCESS;
}

int Tester::wipe_partitions()
{
	if (device_raw.empty()) { return PART_PART_ERR; }
	std::wstring command(PART_DEL_PART_DRIVE + device_raw);
	if (!verbose) { command += SILENT; }
	command += PART_DEL_PART_DRIVE_2;
	//if (system(command.c_str()) != 0) {   return PART_PART_ERR;  }
	LOG_NOTICE(L"[linux] system command: %s", command.c_str());
	return SUCCESS;
}

int Tester::format_drive()
{
	if (device_raw.empty()) { return PART_PART_ERR; }
	std::wstring command = fs_specific_ops_->GetMkfsCommand(device_mount);
	if (!verbose) { command += SILENT; }
	//if (system(command.c_str()) != 0) {    return FMT_FMT_ERR;  }
	LOG_NOTICE(L"[linux] system command: %s", command.c_str());
	return SUCCESS;
}

int Tester::test_setup()
{
	return test_loader.get_instance()->setup();
}

int Tester::test_init_values(std::wstring mount_dir, size_t filesys_size, IFileSystem * fs)
{
	return test_loader.get_instance()->init_values(mount_dir, filesys_size, fs);
}

int Tester::test_run(FILE * change_fd, const int checkpoint)
{
	return test_loader.get_instance()->Run(change_fd, checkpoint);
}

int Tester::async_test_run(FILE * change_fd, const int checkpoint)
{
	LOG_DEBUG(L"start async test, checkpoint=%d", checkpoint);
	if (m_test_thread != NULL) THROW_ERROR(ERR_APP, L"test thread is running");
	m_test_change_fd = change_fd;
	m_test_checkpoint = checkpoint;
	m_test_thread = CreateThread(NULL, 0, &Tester::_start_async_test_run, reinterpret_cast<LPVOID>(this), 0, 0);
	if (!m_test_thread)
	{
		LOG_WIN32_ERROR(L"failed on creatint test thread");
		return -1;
	}
	return 0;
}

int Tester::get_test_running_status(void)
{
	if (m_test_thread == NULL) THROW_ERROR(ERR_APP, L"test thread is not running");
	DWORD exit_code;
	BOOL br = GetExitCodeThread(m_test_thread, &exit_code);
	if (!br)    THROW_WIN32_ERROR(L"failed on getting thread exit code");
	return (int)(exit_code);
}

DWORD __stdcall Tester::_start_async_test_run(LPVOID param)
{
	Tester* test = reinterpret_cast<Tester*>(param);
	JCASSERT(test);
	JCASSERT(test->m_test_change_fd);
	int ir = test->test_run(test->m_test_change_fd, test->m_test_checkpoint);
	fclose(test->m_test_change_fd);
	test->m_test_change_fd = NULL;
	return ir;
}



/*
 * Tests a block device with fsck (or equivalent) and the user test case provided.
 *
 * This function assumes that the block device is *not* mounted when the
 * function is entered. The function will take care of mounting the device as
 * needed and will unmount the device before exiting.
 *
 * On return, the amount of time it took for fsck (or equivalent), the user test
 * case, and the time it took to mount/unmount the file system  are returned in
 * the vector <fsck, user_test, mount/umount>. If one or both of fsck and the
 * user test is not run, then those values are returned as -1. Furthermore, the
 * SingleTestInfo object is modified to reflect the results of fsck and the user
 * test case.
 */
vector<milliseconds> Tester::test_fsck_and_user_test(const std::wstring device_path,
	const unsigned int last_checkpoint, SingleTestInfo& test_info, bool automate_check_test)
{
	JCASSERT(0);
	return std::vector<milliseconds>();
#ifdef _TO_BE_IMPLEMENTED_
	vector<milliseconds> res(3, duration<int, std::milli>(-1));
	// Try mounting the file system so that the kernel can clean up orphan lists
	// and anything else it may need to so that fsck does a better job later if we run it.
	time_point<steady_clock> mount_start_time = steady_clock::now();
	if (mount_device(device_path.c_str(), fs_specific_ops_->GetPostReplayMntOpts().c_str()) != SUCCESS)
	{
		test_info.fs_test.SetError(FileSystemTestResult::kKernelMount);
	}
	time_point<steady_clock> mount_end_time = steady_clock::now();
	res.at(2) = duration_cast<milliseconds>(mount_end_time - mount_start_time);

	// Only run fsck if we failed when mounting the file system above.
	if (test_info.fs_test.GetError() & FileSystemTestResult::kKernelMount)
	{
		// Take the comamnd we are given and redirect stderr to stdout so we can pull it all out with popen below.
		std::wstring command(fs_specific_ops_->GetFsckCommand(device_path) + L" 2>&1");
		// Begin fsck timing.
		time_point<steady_clock> fsck_start_time = steady_clock::now();

		// Use popen so that we can throw all the output from fsck into the log that
		// we are keeping. This information will go just before the summary of what
		// went wrong in the test.
		FILE* pipe = popen(command.c_str(), "r");
		wchar_t tmp[128];
		if (!pipe)
		{
			test_info.fs_test.SetError(FileSystemTestResult::kOther);
			test_info.fs_test.error_description = L"error running fsck";
			time_point<steady_clock> fsck_end_time = steady_clock::now();
			res.at(0) = duration_cast<milliseconds>(fsck_end_time - fsck_start_time);
			return res;
		}
		while (!feof(pipe))
		{
			wchar_t* r = fgetws(tmp, 128, pipe);
			// NULL can be returned on error.
			if (r != NULL) { test_info.fs_test.fsck_result += tmp; }
		}
		test_info.fs_test.fs_check_return = pclose(pipe);
		time_point<steady_clock> fsck_end_time = steady_clock::now();
		res.at(0) = duration_cast<milliseconds>(fsck_end_time - fsck_start_time);
		// End fsck timing.

		if (!WIFEXITED(test_info.fs_test.fs_check_return))
		{      // Processes exited abnormally (no exit(3) or _exit(2) call (from wait(2) manpage).
			test_info.fs_test.SetError(FileSystemTestResult::kCheck);
			// This may not be valid for this case.
			test_info.fs_test.error_description = std::wstring(L"exit status ") +
				to_string(WEXITSTATUS(test_info.fs_test.fs_check_return));
			return res;
		}

		// Fsck (or equivalent) finished without anything major going wrong. Record
		// this and remount the file system so that we're ready to run the user test case.
		test_info.fs_test.SetError(fs_specific_ops_->GetFsckReturn(
			WEXITSTATUS(test_info.fs_test.fs_check_return)));

		// TODO(ashmrtn): Consider mounting with options specified for test profile?
		mount_start_time = steady_clock::now();
		if (mount_device(device_path.c_str(), NULL) != SUCCESS)
		{
			test_info.fs_test.SetError(FileSystemTestResult::kUnmountable);
			return res;
		}
		mount_end_time = steady_clock::now();
		res.at(2) += duration_cast<milliseconds>(mount_end_time - mount_start_time);
	}

	// Begin test case timing.
	time_point<steady_clock> test_case_start_time = steady_clock::now();
	if (automate_check_test)
	{
		bool retVal = check_disk_and_snapshot_contents(snapshot_path_, last_checkpoint);
		if (!retVal)
		{
			test_info.data_test.SetError(fs_testing::tests::DataTestResult::kAutoCheckFailed);
		}
	}
	else
	{
		const int test_check_res =
			test_loader.get_instance()->check_test(last_checkpoint, &test_info.data_test);
	}
	time_point<steady_clock> test_case_end_time = steady_clock::now();
	res.at(1) = duration_cast<milliseconds>(test_case_end_time - test_case_start_time);
	// End test case timing.

	// File system was either mounted at the very start of this segment or after
	// fsck was run. Unmount it before moving on.
	mount_start_time = steady_clock::now();
	// Retry unmount while the device is busy. Hopefully this will only actually
	// execute the loop more than once on only a few occasions.
	int umount_res;
	int err;
	do
	{
		umount_res = umount_device();
		if (umount_res < 0)
		{
			err = errno;
			usleep(500);
		}
	} while (umount_res < 0 && err == EBUSY);
	mount_end_time = steady_clock::now();
	res.at(2) += duration_cast<milliseconds>(mount_end_time - mount_start_time);
	return res;
#endif
}

bool Tester::check_disk_and_snapshot_contents(std::wstring disk_path, int last_checkpoint)
{
	JCASSERT(0);
	return false;
#ifdef _TO_BE_IMPLEMENTED_
	if (checkpointToSnapshot_.find(last_checkpoint) == checkpointToSnapshot_.end())
	{
		std::wcout << L"ERROR: no saved snapshot at checkpoint " << last_checkpoint << endl;
		return false;
	}

	std::wstring snapshot_path;
	snapshot_path = checkpointToSnapshot_[last_checkpoint];
	std::wofstream diff_file;
	diff_file.open("diff-at-check" + to_string(last_checkpoint),
		std::fstream::out | std::fstream::app);

	DiskContents disk1(disk_path, fs_type), disk2(snapshot_path, fs_type);
	disk1.set_mount_point(L"/mnt/snapshot");

	assert(last_checkpoint < mods_.size() && (last_checkpoint > 0));
	for (auto i : mods_.at(last_checkpoint - 1))
	{
		if (i.mod_type == DiskMod::kFsyncMod)
		{
			std::wstring path(i.path);
			path.erase(0, 13);
			std::wcout << path << std::endl;
			bool ret = disk1.compare_entries_at_path(disk2, path, diff_file);
			if (ret && (last_checkpoint == mods_.size() - 1))
			{
				if (disk1.sanity_checks(diff_file) == false)
				{
					std::wcout << L"Failed: Sanity checks on " << disk_path << endl;
					return false;
				}
			}
			return ret;
		}
		else if (i.mod_type == DiskMod::kSyncMod)
		{
			bool retVal = disk1.compare_disk_contents(disk2, diff_file);
			if (retVal && (last_checkpoint == mods_.size() - 1))
			{
				if (disk1.sanity_checks(diff_file) == false)
				{
					std::wcout << L"Failed: Sanity checks on " << disk_path << endl;
					return false;
				}
			}
			return retVal;
		}
		else if (i.mod_type == DiskMod::kDataMod || i.mod_type == DiskMod::kSyncFileRangeMod)
		{
			std::wstring path(i.path);
			path.erase(0, 13);
			bool retVal = disk1.compare_file_contents(disk2, path, i.file_mod_location, i.file_mod_len, diff_file);
			if (retVal && (last_checkpoint == mods_.size() - 1))
			{
				if (disk1.sanity_checks(diff_file) == false)
				{
					std::cout << "Failed: Sanity checks on " << disk_path << endl;
					return false;
				}
			}
			return retVal;
		}
	}
	std::cout << "ERROR: " << __func__ << std::endl;
	return false;
#endif
}

int Tester::test_check_random_permutations(bool full_bio_replay, const int num_rounds, std::wofstream& log)
{
	JCASSERT(0);
	return false;
#ifdef _TO_BE_IMPLEMENTED_
	assert(current_test_suite_ != NULL);
	time_point<steady_clock> start_time = steady_clock::now();
	Permuter* p = permuter_loader.get_instance();
	p->InitDataVector(sector_size_, log_data);
	vector<DiskWriteData> permutes;
	for (int rounds = 0; rounds < num_rounds; ++rounds)
	{
		// Print status every 1024 iterations.
		if (rounds & (~((1 << 10) - 1)) && !(rounds & ((1 << 10) - 1)))
		{
			cout << rounds << std::endl;
		}

		/***************************************************************************
		 * Generate and write out a crash state.
		 **************************************************************************/
		SingleTestInfo test_info;
		// So we get 1-indexed test numbers.
		test_info.test_num = rounds + 1;

		// Begin permute timing.
		time_point<steady_clock> permute_start_time = steady_clock::now();
		bool new_state = false;
		if (full_bio_replay)
		{
			new_state = p->GenerateCrashState(permutes, test_info.permute_data);
		}
		else
		{
			new_state = p->GenerateSectorCrashState(permutes, test_info.permute_data);
		}

		time_point<steady_clock> permute_end_time = steady_clock::now();
		timing_stats[PERMUTE_TIME] +=
			duration_cast<milliseconds>(permute_end_time - permute_start_time);
		// End permute timing.

		if (!new_state)
		{
			break;
		}

		// Restore disk clone.
		int cow_brd_snapshot_fd = open(snapshot_path_.c_str(), O_WRONLY);
		if (cow_brd_snapshot_fd < 0)
		{
			test_info.fs_test.SetError(FileSystemTestResult::kSnapshotRestore);
			test_info.PrintResults(log);
			current_test_suite_->TallyReorderingResult(test_info);
			continue;
		}
		// Begin snapshot timing.
		time_point<steady_clock> snapshot_start_time = steady_clock::now();
		if (clone_device_restore(cow_brd_snapshot_fd, false) != SUCCESS)
		{
			test_info.fs_test.SetError(FileSystemTestResult::kSnapshotRestore);
			test_info.PrintResults(log);
			current_test_suite_->TallyReorderingResult(test_info);
			continue;
		}
		time_point<steady_clock> snapshot_end_time = steady_clock::now();
		timing_stats[SNAPSHOT_TIME] +=
			duration_cast<milliseconds>(snapshot_end_time - snapshot_start_time);
		// End snapshot timing.

		// Write recorded data out to block device in different orders so that we
		// can if they are all valid or not.
		time_point<steady_clock> bio_write_start_time = steady_clock::now();
		const int write_data_res =
			test_write_data(cow_brd_snapshot_fd, permutes.begin(), permutes.end());
		time_point<steady_clock> bio_write_end_time = steady_clock::now();
		timing_stats[BIO_WRITE_TIME] +=
			duration_cast<milliseconds>(bio_write_end_time - bio_write_start_time);
		if (!write_data_res)
		{
			test_info.fs_test.SetError(FileSystemTestResult::kBioWrite);
			close(cow_brd_snapshot_fd);
			test_info.PrintResults(log);
			current_test_suite_->TallyReorderingResult(test_info);
			continue;
		}
		close(cow_brd_snapshot_fd);

		// Test the crash state that was just written out.
		vector<milliseconds> check_res = test_fsck_and_user_test(snapshot_path_,
			test_info.permute_data.last_checkpoint, test_info, false);
		test_info.PrintResults(log);
		current_test_suite_->TallyReorderingResult(test_info);

		// Accounting for time it took to run the test.
		if (check_res.at(0).count() > -1)
		{
			timing_stats[FSCK_TIME] += check_res.at(0);
		}
		if (check_res.at(1).count() > -1)
		{
			timing_stats[TEST_CASE_TIME] += check_res.at(1);
		}
		if (check_res.at(2).count() > -1)
		{
			timing_stats[MOUNT_TIME] += check_res.at(2);
		}
	}

	time_point<steady_clock> end_time = steady_clock::now();
	timing_stats[TOTAL_TIME] = duration_cast<milliseconds>(end_time - start_time);

	if (current_test_suite_->GetReorderingCompleted() < num_rounds)
	{
		cout << "=============== Unable to find new unique state, stopping at " <<
			current_test_suite_->GetReorderingCompleted() <<
			" tests ===============" << endl << endl;
		log << "=============== Unable to find new unique state, stopping at " <<
			current_test_suite_->GetReorderingCompleted() <<
			" tests ===============" << endl << endl;
	}
	return SUCCESS;
#endif
}

/*
 * Replays the operations in the recorded workload, stopping at each Checkpoint
 * found in the workload. At each Checkpoint, the user test case is called so
 * that it can check that things are in order.
 *
 * TODO(ashmrtn): Should this call a separate method in the user test case or
 * should it still call the check_test() method?
 *
 * TODO(ashmrtn): Convert other code in this file to handle epoch and epoch_op
 * instead of disk_write so that we can have the same test_write_data function
 * for this and for the replays created by the permuters.
 */
int Tester::test_check_log_replay(std::wofstream& log, bool automate_check_test)
{
	assert(current_test_suite_ != NULL);

	// A single entry in the log data would just be the leading Checkpoint in the
	// log. In this case, there are no tests to run, so just return.
	if (log_data.size() == 1) { return SUCCESS; }

	// Skip the first disk write as it is just the Checkpoint at the start of the log.
	auto log_iter = log_data.begin() + 1;
	unsigned int last_checkpoint = 0;
	unsigned int test_num = 1;
	unsigned int op_index = 1;
	vector<DiskWriteData> crash_state;

	while (log_iter != log_data.end())
	{
		// Keep going through the workload data log until we reach a Checkpoint.
		// Also, skip the very first checkpoint which occurs at the very beginning of the log.
		while (log_iter != log_data.end() && !log_iter->is_checkpoint())
		{
			DiskWriteData wd = DiskWriteData(true, op_index, 0,
				log_iter->metadata.write_sector * SECTOR_SIZE,
				log_iter->metadata.size, log_iter->get_data(), 0);
			crash_state.push_back(wd);
			++log_iter;
			++op_index;
		}

		// When we see a Checkpoint, we need to do several things:
		// 0. Setup the test result struct with info about this test
		// 1. Restore the disk so that we start from a clean state
		// 2. Write out the data in the log from the start until the checkpoint we found
		// 3. Test the resulting disk state with fsck and the user test case

		// 0. Update checkpoint. It's not always the log_iter's value as log_iter may
		// point to the end of the recorded workload, not a checkpoint.
		if (log_iter->is_checkpoint()) { last_checkpoint = log_iter->metadata.write_sector; }
		SingleTestInfo test_info;
		test_info.permute_data.crash_state = crash_state;
		// We reached the checkpoint, so it is indeed the last one we saw, unless we have replayed the entire log.
		test_info.permute_data.last_checkpoint = last_checkpoint;
		// Tests for this portion will be numbered starting from 1.
		test_info.test_num = test_num++;

		// 1. Restore disk clone.
		//int cow_brd_snapshot_fd = open(snapshot_path_.c_str(), O_WRONLY);
		//if (cow_brd_snapshot_fd == 0) 
		if (m_snapshot_dev == NULL)
		{
			test_info.fs_test.SetError(FileSystemTestResult::kSnapshotRestore);
			test_info.PrintResults(log);
			current_test_suite_->TallyTimingResult(test_info);
			continue;
		}

		if (clone_device_restore(m_snapshot_dev, false) != SUCCESS)
		{
			test_info.fs_test.SetError(FileSystemTestResult::kSnapshotRestore);
			test_info.PrintResults(log);
			current_test_suite_->TallyTimingResult(test_info);
			continue;
		}

		// 2. Write recorded data out to block device. If the iterator points to the
		// end of the log, we are alright because the function is [begin, end) and
		// the end iterator is a sentinal value. The same logic applies for
		// checkpoints (which we don't really want to replay).
//		const int write_data_res = test_write_data(cow_brd_snapshot_fd, crash_state.begin(), crash_state.end());
		const int write_data_res = test_write_data(m_cow_brd, crash_state.begin(), crash_state.end());
		if (!write_data_res)
		{
			test_info.fs_test.SetError(FileSystemTestResult::kBioWrite);
			//            close(cow_brd_snapshot_fd);
			test_info.PrintResults(log);
			current_test_suite_->TallyTimingResult(test_info);
			continue;
		}
		//        close(cow_brd_snapshot_fd);

				// 3. Check the resulting disk image with fsck and the user test. For now,
				// just ignore the timing data that we can get from this function.
		if (log_iter->is_checkpoint())
		{
			test_fsck_and_user_test(snapshot_path_,
				test_info.permute_data.last_checkpoint, test_info, automate_check_test);

			test_info.PrintResults(log);
			current_test_suite_->TallyTimingResult(test_info);
		}

		// Exit loop after doing final test.
		if (log_iter == log_data.end()) { break; }

		// Increment our end pointer iterater passed the Checkpoint we just stopped at.
		++log_iter;
		++op_index;
	}
	return SUCCESS;
}

// TODO(ashmrtn): Should probably just remove this function.
int Tester::test_restore_log()
{
	JCASSERT(0); return 0;
#ifdef _TO_BE_IMPLEMENTED_
	// We need to mount the original device because we intercept bios after they
	// have been traslated to the current disk and lose information about sector
	// offset from the partition. If we were to mount and write into the partition
	// we would clobber the disk state in unknown ways.
	const int sn_fd = open(device_raw.c_str(), O_WRONLY);
	if (sn_fd < 0)
	{
		cout << endl;
		return TEST_CASE_FILE_ERR;
	}
	if (!test_write_data_dw(sn_fd, log_data.begin(), log_data.end()))
	{
		cout << "test errored in writing data" << endl;
		close(sn_fd);
		return TEST_TEST_ERR;
	}
	close(sn_fd);
	return SUCCESS;
#endif
}

bool Tester::test_write_data_dw(const int disk_fd,
	const vector<disk_write>::iterator& start,
	const vector<disk_write>::iterator& end)
{
	JCASSERT(0); return false;
#ifdef _TO_BE_IMPLEMENTED_
	for (auto current = start; current != end; ++current)
	{    // Operation is not a write so skip it.
		if (!current->has_write_flag()) { continue; }
		const unsigned long int byte_addr = current->metadata.write_sector * SECTOR_SIZE;
		if (lseek(disk_fd, byte_addr, SEEK_SET) < 0) { return false; }
		unsigned int bytes_written = 0;
		void* data_base_addr = current->get_data().get();
		while (bytes_written < current->metadata.size)
		{
			int res = write(disk_fd, (void*)((unsigned long)data_base_addr + bytes_written),
				current->metadata.size - bytes_written);
			if (res < 0) { return false; }
			bytes_written += res;
		}
	}
	return true;
#endif
}

//bool Tester::test_write_data(const int disk_fd,
//<YUAN> 将数据写入文件中。目前只有restore snapshot调用次函数。
//  <讨论>如何实现snapshot设备。方案1：作为一般文件实现。方案二：作为虚拟设备的一类实现。方案三：专用设备。
bool Tester::test_write_data(IVirtualDisk* disk,
	const vector<DiskWriteData>::iterator& start, const vector<DiskWriteData>::iterator& end)
{
	for (auto current = start; current != end; ++current)
	{
		if (current->size == 0)
		{   // It's *possible* that zero length sectors could have an invalid
			// disk_offset (I have not tested/confirmed).
			continue;
		}
		//if (lseek(disk_fd, current->disk_offset, SEEK_SET) < 0)
		//{
		//	return false;
		//}
		JCASSERT((current->disk_offset % SECTOR_SIZE) == 0 && (current->size % SECTOR_SIZE) == 0);
		unsigned int bytes_written = 0;
		BYTE* data_base_addr = reinterpret_cast<BYTE*>(current->GetData());
		disk->WriteSectors(data_base_addr + bytes_written, BYTE_TO_SECTOR(current->disk_offset), BYTE_TO_SECTOR(current->size));

		//while (bytes_written < current->size)
		//{
		//	int res = write(disk_fd, (void*)((unsigned long)data_base_addr + bytes_written),
		//		current->size - bytes_written);
		//	if (res < 0) { return false; }
		//	bytes_written += res;
		//}
	}
	return true;
}

void Tester::cleanup_harness()
{
	int umount_res;
	int err;
	do
	{
		umount_res = umount_device();
		if (umount_res < 0)
		{
			err = errno;
			//      usleep(500);
			Sleep(500);
		}
	} while (umount_res < 0 && err == EBUSY);
	if (umount_res < 0)
	{
		cerr << "Unable to unmount device" << endl;
		permuter_unload_class();
		test_unload_class();
		return;
	}

	if (remove_wrapper() != SUCCESS)
	{
		cerr << "Unable to remove wrapper device" << endl;
		permuter_unload_class();
		test_unload_class();
		return;
	}

	if (remove_cow_brd() != SUCCESS)
	{
		cerr << "Unable to remove cow_brd device" << endl;
		permuter_unload_class();
		test_unload_class();
		return;
	}

	permuter_unload_class();
	test_unload_class();
	if (fs_specific_ops_ != NULL)
	{
		delete fs_specific_ops_;
		fs_specific_ops_ = NULL;
	}
}

int Tester::clear_caches()
{
	JCASSERT(0); return 0;
#ifdef _TO_BE_IMPLEMENTED_
	sync();
	const int cache_fd = open(DROP_CACHES_PATH, O_WRONLY);
	if (cache_fd < 0)
	{
		return CLEAR_CACHE_ERR;
	}

	int res;
	do
	{
		res = write(cache_fd, "3", 1);
		if (res < 0)
		{
			close(cache_fd);
			return CLEAR_CACHE_ERR;
		}
	} while (res < 1);
	close(cache_fd);
	return SUCCESS;
#endif
}

int Tester::log_profile_save(std::wstring log_file)
{
	// TODO(ashmrtn): What happens if this fails?
	// Open with append flags. We should remove the log file argument and use a
	// class specific one that is set at class creation time. That way people
	// don't break our logging system.
	std::cout << "saving " << log_data.size() << " disk operations" << endl;
	wofstream log(log_file, std::ofstream::trunc | ios::binary);
	for (const disk_write& dw : log_data)
	{
		disk_write::serialize(log, dw);
	}
	log.close();
	return SUCCESS;
}

int Tester::log_profile_load(std::wstring log_file)
{
	ifstream log(log_file, ios::binary);
	while (log.peek() != EOF)
	{
		log_data.push_back(disk_write::deserialize(log));
	}
	bool err = log.fail();
	int errnum = errno;
	log.close();
	if (err)
	{
//		std::cout << "error " << strerror_s((errnum) << std::endl;
		std::cout << "error code= " << errnum << std::endl;
		return LOG_CLONE_ERR;
	}
	std::cout << "loaded " << log_data.size() << " disk operations" << endl;
	return SUCCESS;
}

int Tester::log_snapshot_save(std::wstring log_file)
{
	// TODO(ashmrtn): What happens if this fails?
	// TODO(ashmrtn): Change device_clone to be an mmap of the disk we need to get stuff on.
	// device_size happens to be the number of 1k blocks on cow_brd (from original
	// brd behavior...), so convert it to a number of bytes.
	unsigned int dev_bytes = device_size * 2 * 512;
	unsigned int bytes_done = 0;
	const unsigned int buf_size = 4096;
	unsigned int buf[buf_size];
	FILE* log_fd = NULL;
	_wfopen_s(&log_fd, log_file.c_str(), L"w+");
	//int log_fd = open(log_file.c_str(), O_WRONLY | O_CREAT | O_TRUNC, S_IRUSR | S_IWUSR);
	if (log_fd == 0)
	{
		cerr << "error opening log file" << endl;
		return LOG_CLONE_ERR;
	}

	//int res = lseek(cow_brd_fd, 0, SEEK_SET);
	//if (res < 0)
	//{
	//	cerr << "error seeking to start of test device" << endl;
	//	return LOG_CLONE_ERR;
	//}
	size_t lba = 0;
	while (bytes_done < dev_bytes)
	{
		// Read a block of data from the base disk image.
		size_t bytes = 0;
		unsigned int new_amount = (dev_bytes < bytes_done + buf_size)
			? dev_bytes - bytes_done
			: buf_size;
//		JCASSERT(new_amount % 512 == 0);
		size_t secs = BYTE_TO_SECTOR(new_amount);
		m_cow_brd->ReadSectors(buf, lba, secs);
		lba += secs;
		//do
		//{
		//	int res = read(cow_brd_fd, buf + bytes, new_amount - bytes);
		//	if (res < 0)
		//	{
		//		cerr << "error reading from raw device to log disk snapshot" << endl;
		//		return LOG_CLONE_ERR;
		//	}
		//	bytes += res;
		//} while (bytes < new_amount);

		// Write a block of data to the log file.
		bytes = 0;
		do
		{
			size_t res = fwrite(buf + bytes, new_amount - bytes, 1, log_fd);
//			int res = write(log_fd, buf + bytes, new_amount - bytes);
			if (res == 0)
			{
				cerr << "error reading from raw device to log disk snapshot" << endl;
				return LOG_CLONE_ERR;
			}
			bytes += res;
		} while (bytes < new_amount);
		bytes_done += new_amount;
	}
	//fsync(log_fd);
	fflush(log_fd);
	fclose(log_fd);
	return SUCCESS;
}

int Tester::log_snapshot_load(std::wstring log_file)
{
	// TODO(ashmrtn): What happens if this fails?
	// TODO(ashmrtn): Change device_clone to be an mmap of the disk we need to get stuff on.
//	int res = ioctl(cow_brd_fd, COW_BRD_WIPE);
	int res = m_cow_brd->IoCtrl(0, COW_BRD_WIPE, NULL);
	if (res < 0)
	{
		cerr << "error wiping old disk snapshot" << endl;
		return LOG_CLONE_ERR;
	}

	// device_size happens to be the number of 1k blocks on cow_brd (from original
	// brd behavior...), so convert it to a number of bytes.
	unsigned int dev_bytes = device_size * 2 * 512;
	unsigned int bytes_done = 0;
	const unsigned int buf_size = 4096;
	unsigned int buf[buf_size];
//	int log_fd = open(log_file.c_str(), O_RDONLY);
	FILE* log_fd = NULL;
	_wfopen_s(&log_fd, log_file.c_str(), L"r");
	if (log_fd == 0)
	{
		cerr << "error opening log file" << endl;
		return LOG_CLONE_ERR;
	}

	JCASSERT(m_ram_drive);
	// cow_brd_fd is RDONLY.
	//int device_path = open(COW_BRD_PATH, O_WRONLY);
	//if (device_path < 0)
	//{
	//	cerr << "error opening log file" << endl;
	//	return LOG_CLONE_ERR;
	//}

	//res = lseek(device_path, 0, SEEK_SET);
	//if (res < 0)
	//{
	//	cerr << "error seeking to start of test device" << endl;
	//	return LOG_CLONE_ERR;
	//}

//	res = lseek(log_fd, 0, SEEK_SET);
	res = fseek(log_fd, 0, SEEK_SET);
	if (res < 0)
	{
		cerr << "error seeking to start of log file" << endl;
		return LOG_CLONE_ERR;
	}
	size_t lba = 0;
	while (bytes_done < dev_bytes)
	{
		// Read a block of data from the base disk image.
		size_t bytes = 0;
		unsigned int new_amount = (dev_bytes < bytes_done + buf_size)
			? dev_bytes - bytes_done
			: buf_size;
		size_t secs = BYTE_TO_SECTOR(new_amount);
		do
		{
			size_t res = fread(buf, new_amount - bytes, 1, log_fd);
			//int res = read(log_fd, buf + bytes, new_amount - bytes);
			if (res == 0)
			{
				cerr << "error reading from raw device to log disk snapshot" << endl;
				return LOG_CLONE_ERR;
			}
			bytes += res;
		} while (bytes < new_amount);

		// Write a block of data to the log file.
		bytes = 0;
		m_ram_drive->WriteSectors(buf, lba, secs);
		lba += secs;
		//do
		//{
		//	int res = write(device_path, buf + bytes, new_amount - bytes);
		//	if (res < 0)
		//	{
		//		cerr << "error reading from raw device to log disk snapshot" << endl;
		//		return LOG_CLONE_ERR;
		//	}
		//	bytes += res;
		//} while (bytes < new_amount);
		bytes_done += new_amount;
	}
//	close(device_path);

//	fsync(cow_brd_fd);
//	res = ioctl(cow_brd_fd, COW_BRD_SNAPSHOT);
	res = m_cow_brd->IoCtrl(0, COW_BRD_SNAPSHOT, NULL);
	if (res < 0)
	{
		cerr << "error restoring snapshot from log" << endl;
		return LOG_CLONE_ERR;
	}
	return SUCCESS;
}

void Tester::log_disk_write_data(std::wostream& log)
{
	int digits = log.precision();
	std::ios_base::fmtflags fflags = log.flags();
	log.precision(6);
	log << std::left << std::setw(5) << L"bio #" << " " << std::setw(18) <<
		L"time" << " " << std::setw(18) << L"sector" << " " << std::setw(18) <<
		L"size" << std::endl;
	for (unsigned int i = 0; i < log_data.size(); ++i)
	{
		log << std::setw(5) << i << ' ' << log_data.at(i);
	}
	log.precision(digits);
	log.flags(fflags);
}

void Tester::PrintTestStats(std::wostream& os)
{
	for (const auto& suite : test_results_)
	{
		suite.PrintResults(os);
	}
}

std::chrono::milliseconds Tester::get_timing_stat(time_stats timing_stat)
{
	return timing_stats[timing_stat];
}

std::wostream& operator<<(std::wostream& os, Tester::time_stats time)
{
	switch (time)
	{
	case fs_testing::Tester::PERMUTE_TIME:
		os << "permute time";
		break;
	case fs_testing::Tester::SNAPSHOT_TIME:
		os << "snapshot restore time";
		break;
	case fs_testing::Tester::BIO_WRITE_TIME:
		os << "bio write time";
		break;
	case fs_testing::Tester::FSCK_TIME:
		os << "fsck time";
		break;
	case fs_testing::Tester::TEST_CASE_TIME:
		os << "test case time";
		break;
	case fs_testing::Tester::MOUNT_TIME:
		os << "mount/umount time";
		break;
	case fs_testing::Tester::TOTAL_TIME:
		os << "total time";
		break;
	default:
		os.setstate(std::ios_base::failbit);
	}
	return os;
}

//}  // namespace fs_testing

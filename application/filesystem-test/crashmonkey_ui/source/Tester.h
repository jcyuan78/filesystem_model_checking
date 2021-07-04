#ifndef TESTER_H
#define TESTER_H

#include <chrono>
//#include <fstream>
//#include <iostream>
//#include <string>
//#include <utility>
//#include <vector>
//#include <map>

#include "results/FileSystemTestResult.h"

#include "FsSpecific.h"
#include "permuter/Permuter.h"
#include "results/TestSuiteResult.h"
#include "tests/BaseTestCase.h"
#include "utils/ClassLoader.h"
#include "utils/DiskMod.h"
#include "utils/utils.h"

#include <dokanfs-lib.h>

#define SUCCESS                  0
#define DRIVE_CLONE_ERR          -1
#define DRIVE_CLONE_RESTORE_ERR  -2
#define DRIVE_CLONE_EXISTS_ERR   -3
#define TEST_TEST_ERR            -4
#define LOG_CLONE_ERR            -5
#define TEST_CASE_FILE_ERR       -11
#define MNT_BAD_DEV_ERR          -12
#define MNT_MNT_ERR              -13
#define MNT_UMNT_ERR             -14
#define FMT_FMT_ERR              -15
#define WRAPPER_INSERT_ERR       -16
#define WRAPPER_REMOVE_ERR       -17
#define WRAPPER_OPEN_DEV_ERR     -18
#define WRAPPER_DATA_ERR         -19
#define WRAPPER_MEM_ERR          -20
#define CLEAR_CACHE_ERR          -21
#define PART_PART_ERR            -22

#define FMT_EXT4               0

#define DIRTY_EXPIRE_TIME_SIZE 11

namespace fs_testing
{
#ifdef TEST_CASE
	namespace test
	{
		class TestTester;
	}  // namespace test
#endif

	class Tester
	{
#ifdef TEST_CASE
		friend class fs_testing::test::TestTester;
#endif

	public:
		enum time_stats
		{
			PERMUTE_TIME, SNAPSHOT_TIME, BIO_WRITE_TIME, FSCK_TIME,
			TEST_CASE_TIME, MOUNT_TIME, TOTAL_TIME, NUM_TIME,
		};

		Tester(const size_t device_size, const unsigned int sector_size, const bool verbosity);
		~Tester();

		const bool verbose = false;
		void set_fs_type(const std::wstring type);
		void set_device(const std::wstring device_path);
		void set_flag_device(const std::wstring device_path);

		const wchar_t* update_dirty_expire_time(const wchar_t* time);

		int partition_drive();
		int wipe_partitions();
		int format_drive();
		int clone_device();
		//        int clone_device_restore(int snapshot_fd, bool reread);
		int clone_device_restore(IVirtualDisk* snapshot, bool reread);

		int permuter_load_class(const wchar_t* path);
		void permuter_unload_class();

		int test_load_class(const wchar_t* path);
		void test_unload_class();

//<YUAN> 测试线程相关函数
		int test_setup();
		int test_init_values(std::wstring mountDir, size_t filesysSize, IFileSystem * fs);
		int test_run(FILE * change_fd, const int checkpoint);
		int async_test_run(FILE * chage_fd, const int checkpoint);
		int get_test_running_status(void);

	protected:
		static DWORD WINAPI _start_async_test_run(LPVOID param);
		HANDLE m_test_thread;
		FILE* m_test_change_fd;
		int m_test_checkpoint;

	public:

		int test_check_random_permutations(const bool full_bio_replay,
			const int num_rounds, std::wofstream& log);
		int test_check_log_replay(std::wofstream& log, bool automate_check_test);
		int test_restore_log();
		int test_check_current();

		int mount_device_raw(const wchar_t* opts);
		int mount_wrapper_device(const wchar_t* opts);
		int umount_device();

		int mount_snapshot();
		int umount_snapshot();

		int mapCheckpointToSnapshot(int checkpoint);
		int getNewDiskClone(int checkpoint);
		void getCompleteRunDiskClone();

		int insert_cow_brd(IVirtualDisk* cow_brd);
		int remove_cow_brd();

		int insert_wrapper(IVirtualDisk* wrapper);
		int remove_wrapper();
		int get_wrapper_ioctl();
		void put_wrapper_ioctl();

		void begin_wrapper_logging();
		void end_wrapper_logging();
		int get_wrapper_log();
		void clear_wrapper_log();
//		int GetChangeData(const int fd);
		int GetChangeData(FILE* fd);

		int CreateCheckpoint();

		int clear_caches();
		void cleanup_harness();
		// TODO(ashmrtn): Save the fstype in the log file so that we don't
		// accidentally mix logs of one fs type with mount options for another?
		int log_profile_save(std::wstring log_file);
		int log_profile_load(std::wstring log_file);
		int log_snapshot_save(std::wstring log_file);
		int log_snapshot_load(std::wstring log_file);
		void log_disk_write_data(std::wostream& log);

		std::chrono::milliseconds get_timing_stat(time_stats timing_stat);
		void PrintTimingStats(std::wostream& os);
		void PrintTestStats(std::wostream& os);
		void StartTestSuite();
		void EndTestSuite();

		unsigned int GetPostRunDelay();

		// TODO(ashmrtn): Figure out why making these private slows things down a lot.
		  //<YUAN> 通过 IFileSystem统一操作界面
	private:
		FsSpecific* fs_specific_ops_ = NULL;

		const size_t device_size;
		fs_testing::utils::ClassLoader<fs_testing::tests::BaseTestCase> test_loader;
		fs_testing::utils::ClassLoader<fs_testing::permuter::Permuter> permuter_loader;

		wchar_t dirty_expire_time[DIRTY_EXPIRE_TIME_SIZE];
		std::wstring fs_type;
		std::wstring device_raw;
		std::wstring device_mount;
		std::wstring flags_device;

		TestSuiteResult* current_test_suite_ = NULL;

		bool wrapper_inserted = false;
		bool cow_brd_inserted = false;  // RAM DISK已经初始化
//        int cow_brd_fd = -1;            // RAM DISK的设别号
		IVirtualDisk* m_cow_brd = nullptr;

		bool disk_mounted = false;
		//        int ioctl_fd = -1;              // disk wrape的设备号
		IVirtualDisk* m_ioctl_dev = nullptr;

		IVirtualDisk* m_ram_drive = nullptr;

		const unsigned int sector_size_;
		std::vector<fs_testing::utils::disk_write> log_data;
		std::vector<std::vector<fs_testing::utils::DiskMod> > mods_;

		int mount_device(const wchar_t* dev, const wchar_t* opts);

		bool read_dirty_expire_time(int fd);
		bool write_dirty_expire_time(int fd, const wchar_t* time);

		bool test_write_data_dw(const int disk_fd,
			const std::vector<fs_testing::utils::disk_write>::iterator& start,
			const std::vector<fs_testing::utils::disk_write>::iterator& end);

		//    bool test_write_data(const int disk_fd,
		bool test_write_data(IVirtualDisk* disk,
			const std::vector<fs_testing::utils::DiskWriteData>::iterator& start,
			const std::vector<fs_testing::utils::DiskWriteData>::iterator& end);

		std::vector<std::chrono::milliseconds> test_fsck_and_user_test(
			const std::wstring device_path, const unsigned int last_checkpoint,
			SingleTestInfo& test_info, bool automate_check_test);

		bool check_disk_and_snapshot_contents(std::wstring disk_path, int last_checkpoint);

		std::vector<TestSuiteResult> test_results_;
		std::chrono::milliseconds timing_stats[NUM_TIME] = { std::chrono::milliseconds(0) };

		std::map<int, std::wstring> checkpointToSnapshot_;

		std::wstring snapshot_path_;    //<YUAN> 原设计中，snapshot_path_指向设备的路径，更改为虚拟设备
		IVirtualDisk* m_snapshot_dev = nullptr;

	};

	std::wostream& operator<<(std::wostream& os, Tester::time_stats time);

}  // namespace fs_testing

#endif

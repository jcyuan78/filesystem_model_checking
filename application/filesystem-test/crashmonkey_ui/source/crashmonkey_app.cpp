
#include "pch.h"

#include <fstream>
#include <iostream>

#include "Tester.h"
#include "ThreadMessageQue.h"
#include "crashmonkey_app.h"
#include <boost/cast.hpp>
#include <boost/property_tree/xml_parser.hpp>

using fs_testing::utils::communication::SocketMessage;
using fs_testing::utils::communication::SocketError;

// <YUAN> Start configuraiton
//#define ENABLE_BACKGROUND

LOCAL_LOGGER_ENABLE(L"fstester.app", LOGGER_LEVEL_DEBUGINFO);

const TCHAR CCrashMonkeyApp::LOG_CONFIG_FN[] = L"fstester.cfg";
typedef jcvos::CJCApp<CCrashMonkeyApp>	CApplication;
CApplication _app;

#define _class_name_	CApplication
BEGIN_ARGU_DEF_TABLE()


ARGU_DEF(L"config", 'c', m_config_file, L"configuration xml file name")
ARGU_DEF(L"test_dev", 'd', m_test_dev_name, L"test device")
ARGU_DEF(L"test_case", 'x', m_test_case_name, L"test case name")
ARGU_DEF(L"test_dev_size", 's', m_test_dev_size, L"test device size", (size_t)0)
ARGU_DEF(L"disk_size", 'e', m_disk_size, L"disk size", (size_t)10240)
//ARGU_DEF(L"log_file", 'l', m_log_fn, L"specify test log file name")
END_ARGU_DEF_TABLE()

int _tmain(int argc, _TCHAR* argv[])
{
	return jcvos::local_main(argc, argv);
}

BOOL WINAPI HandlerRoutine(DWORD dwCtrlType);

CCrashMonkeyApp::CCrashMonkeyApp(void)
    :m_fs_factory(NULL), m_dev_factory(NULL), m_tester_factory(NULL), m_test_dev(NULL)
{

}

CCrashMonkeyApp::~CCrashMonkeyApp(void)
{
    RELEASE(m_fs_factory);
    RELEASE(m_dev_factory);
    RELEASE(m_tester_factory);
    RELEASE(m_test_dev);
}

int CCrashMonkeyApp::Initialize(void)
{
	//	EnableSrcFileParam('i');
	EnableDstFileParam('o');
    m_test_dev_name = L"/dev/ram0";
	return 0;
}

void CCrashMonkeyApp::CleanUp(void)
{
}



#define STRINGIFY(x) L#x
#define TO_STRING(x) STRINGIFY(x)

#define TEST_SO_PATH "tests/"
#define PERMUTER_SO_PATH L"permuter/"
// TODO(ashmrtn): Find a good delay time to use for tests.
#define TEST_DIRTY_EXPIRE_TIME_CENTISECS 3000
#define TEST_DIRTY_EXPIRE_TIME_STRING TO_STRING(TEST_DIRTY_EXPIRE_TIME_CENTISECS)
#define MOUNT_DELAY 1

#define DIRECTORY_PERMS \
  (S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP | S_IROTH | S_IWOTH)

#define OPTS_STRING "bd:cf:e:l:m:np:r:s:t:vFIPS:"

static const unsigned int kSocketQueueDepth = 2;
static constexpr wchar_t kChangePath[] = L"run_changes.dat";

typedef bool(*PLUGIN_GET_FACTORY)(IFsFactory*&);

bool CCrashMonkeyApp::LoadConfig(void)
{
    std::string str_config;
    jcvos::UnicodeToUtf8(str_config, m_config_file);
    boost::property_tree::wptree pt;
    boost::property_tree::read_xml(str_config, pt);

    std::wstring fs_lib = pt.get<std::wstring>(L"config.filesystem.lib");
    m_fs_name = pt.get<std::wstring>(L"config.filesystem.name", L"");
//    auto& device_pt = pt.get_child_optional(L"config.device");

//    if (fs_lib.empty())	THROW_ERROR(ERR_PARAMETER, L"missing DLL.");
//    LOG_DEBUG(L"loading dll: %s...", fs_lib.c_str());
//    HMODULE plugin = LoadLibrary(fs_lib.c_str());
//    if (plugin == NULL) THROW_WIN32_ERROR(L" failure on loading driver %s ", fs_lib.c_str());
//
//    LOG_DEBUG(L"getting entry...");
//    PLUGIN_GET_FACTORY get_factory = (PLUGIN_GET_FACTORY)(GetProcAddress(plugin, "GetFactory"));
//    if (get_factory == NULL)	THROW_WIN32_ERROR(L"file %s is not a file system plugin.", fs_lib.c_str());
//
////    jcvos::auto_interface<IFsFactory> factory;
//    bool br = (get_factory)(m_fs_factory);
//    if (!br || !m_fs_factory) THROW_ERROR(ERR_USER, L"failed on getting plugin register in %s", fs_lib.c_str());
    LoadFactory(m_fs_factory, fs_lib);


    const boost::property_tree::wptree & device_pt = pt.get_child(L"config.device");
    m_dev_name = device_pt.get<std::wstring>(L"name");
    fs_lib = device_pt.get<std::wstring>(L"lib");
    LoadFactory(m_dev_factory, fs_lib);
    m_dev_factory->CreateVirtualDisk(m_test_dev, device_pt, true);
    

    //bool br = m_fs_factory->CreateFileSystem(m_fs, m_fs_name);
    //if (!br || !m_fs) THROW_ERROR(ERR_APP, L"failed on creating file system");

    //if (device_pt)
    //{
    //    br = factory->CreateVirtualDisk(m_dev, (*device_pt), true);
    //    m_capacity = m_dev->GetCapacity();
    //}

    //// load config
    //m_test_spor = pt.get < bool>(L"test_sopr", false);
    //m_support_trunk = pt.get<bool>(L"support_trunk", true);

    return true;
}

void CCrashMonkeyApp::LoadFactory(IFsFactory*& factory, const std::wstring& lib_name)
{
    if (lib_name.empty())	THROW_ERROR(ERR_PARAMETER, L"missing DLL.");
    LOG_DEBUG(L"loading dll: %s...", lib_name.c_str());
    HMODULE plugin = LoadLibrary(lib_name.c_str());
    if (plugin == NULL) THROW_WIN32_ERROR(L" failure on loading driver %s ", lib_name.c_str());

    LOG_DEBUG(L"getting entry...");
    PLUGIN_GET_FACTORY get_factory = (PLUGIN_GET_FACTORY)(GetProcAddress(plugin, "GetFactory"));
    if (get_factory == NULL)	THROW_WIN32_ERROR(L"file %s is not a file system plugin.", lib_name.c_str());

    //    jcvos::auto_interface<IFsFactory> factory;
    bool br = (get_factory)(m_fs_factory);
    if (!br || !m_fs_factory) THROW_ERROR(ERR_USER, L"failed on getting plugin register in %s", lib_name.c_str());
}

int CCrashMonkeyApp::Run(void)
{
	std::wstring dirty_expire_time_centisecs(TEST_DIRTY_EXPIRE_TIME_STRING);
	std::wstring fs_type(L"ext4");
	std::wstring flags_dev(L"/dev/vda");
//	std::wstring test_dev(L"/dev/ram0");
	std::wstring mount_opts(L"");
	std::wstring log_file_save(L"");
	std::wstring log_file_load(L"");
	std::wstring permuter(PERMUTER_SO_PATH L"RandomPermuter.so");
	bool background = false;
	bool automate_check_test = false;
	bool dry_run = false;
	bool no_lvm = false;
	bool verbose = false;
	bool in_order_replay = true;
	bool permuted_order_replay = true;
	bool full_bio_replay = false;
	int iterations = 10000;
//	int disk_size = 10240;
	unsigned int sector_size = 512;
	int option_idx = 0;

    LoadConfig();

	fs_testing::utils::communication::ServerSocket* background_com = NULL;


 /*****************************************************************************
 * PHASE 0:
 * Basic setup of the test harness:
 * 1. check arguments are sane
 * 2. load up socket connections if/when needed
 * 3. load basic kernel modules
 * 4. load static objects for permuter and test case
 ****************************************************************************/
//    const unsigned int test_case_idx = optind;
//    const std::wstring path = argv[test_case_idx];
    const std::wstring path = m_test_case_name;

    // Get the name of the test being run.
    size_t begin = path.rfind('/');
    // Remove everything before the last /.
    std::wstring test_name = path.substr(begin + 1);
    // Remove the extension.
    test_name = test_name.substr(0, test_name.length() - 3);
    // Get the date and time stamp and format.
    time_t now = time(0);
    tm local_now;
    localtime_s(&local_now, &now);
    wchar_t time_st[40];
    wcsftime(time_st, sizeof(time_st), L"%Y%m%d_%H%M%S", &local_now);
    std::wstring s = std::wstring(time_st) + L"-" + test_name + L".log";
    std::wofstream logfile(s);

    //<YUAN> Create file system for test
    jcvos::auto_interface<IFileSystem> filesystem;
    bool br = m_fs_factory->CreateFileSystem(filesystem, m_fs_name);
    if (!br | !filesystem) THROW_ERROR(ERR_APP, L"failed on creating file system %s", m_fs_name.c_str());
    JCASSERT(m_test_dev);
    filesystem->ConnectToDevice(m_test_dev);
    filesystem->Mount();
    


    // This should be changed in the option is added to mount tests in other directories.
    std::wstring mount_dir = L"/mnt/snapshot";
    //if (setenv("MOUNT_FS", mount_dir.c_str(), 1) == -1) 
    //{
    //    std::cerr << "Error setting environment variable MOUNT_FS" << std::endl;
    //}

    std::cout << "========== PHASE 0: Setting up CrashMonkey basics =========="   << std::endl;
    logfile << "========== PHASE 0: Setting up CrashMonkey basics =========="     << std::endl;
    //if (test_case_idx == argc) 
    //{
    //    std::cerr << "Please give a .so test case to load" << std::endl;
    //    return -1;
    //}

    if (iterations < 0) 
    {
        std::cerr << "Please give a positive number of iterations to run" << std::endl;
        return -1;
    }

    if (m_disk_size <= 0) 
    {
        std::cerr << "Please give a positive number for the RAM disk size to use"
            << std::endl;
        return -1;
    }

    if (sector_size <= 0) 
    {
        std::cerr << "Please give a positive number for the sector size" << std::endl;
        return -1;
    }

    background_com = new fs_testing::utils::communication::ServerSocket(
        fs_testing::utils::communication::kSocketNameOutbound);
    if (background_com->Init(kSocketQueueDepth) < 0) 
    {
        int err_no = errno;
        std::cerr << "Error starting socket to listen on " << err_no << std::endl;
        delete background_com;
        return -1;
    }

    fs_testing::Tester test_harness(m_disk_size, sector_size, verbose);
    test_harness.StartTestSuite();      // 准备一个测试结果的空间到 container

    std::cout << "Inserting RAM disk module" << std::endl;
    logfile << "Inserting RAM disk module" << std::endl;
    if (test_harness.insert_cow_brd(NULL) != SUCCESS)       // 安装RAM DISK驱动（cow brd），并打开设备，句柄保存在Tester::cow_brd_fd
    {
        std::cerr << "Error inserting RAM disk module" << std::endl;
        return -1;
    }
    test_harness.set_fs_type(fs_type);
    test_harness.set_device(m_test_dev_name);
    //<YUAN>: 获取文件系统所在分区的大小， 
    /*
    FILE* input;
    wchar_t buf[512];
    if (!(input = popen(("fdisk -l " + m_test_dev + " | grep " + m_test_dev + ": ").c_str(), "r"))) 
    {
        std::cerr << "Error finding the filesize of mounted filesystem" << std::endl;
    }
    std::wstring filesize;
    while (fgets(buf, 512, input)) 
    {
        filesize += buf;
    }
    pclose(input);
    wchar_t* filesize_cstr = new wchar_t[filesize.length() + 1];
    strcpy(filesize_cstr, filesize.c_str());
    wchar_t* tok = strtok(filesize_cstr, " ");
    int pos = 0;
    while (pos < 4 && tok != NULL) 
    {
        pos++;
        tok = strtok(NULL, " ");
    }
    long test_dev_size = 0;
    if (tok != NULL) 
    {
        test_dev_size = atol(tok);
    }
    delete[] filesize_cstr;
    */
    
    //if (setenv("FILESYS_SIZE", filesize.c_str(), 1) == -1) 
    //{
    //    std::cerr << "Error setting environment variable FILESYS_SIZE" << std::endl;
    //}

    // Load the class being tested.
    std::cout << "Loading test case" << std::endl;
//    if (test_harness.test_load_class(argv[test_case_idx]) != SUCCESS) 
    if (test_harness.test_load_class(m_test_case.c_str()) != SUCCESS) 
    {
        test_harness.cleanup_harness();
        return -1;
    }

    test_harness.test_init_values(mount_dir, m_test_dev_size, filesystem);

    // Load the permuter to use for the test.
    // TODO(ashmrtn): Consider making a line in the test file which specifies the permuter to use?
    std::cout << "Loading permuter" << std::endl;
    logfile << "Loading permuter" << std::endl;
    if (test_harness.permuter_load_class(permuter.c_str()) != SUCCESS) 
    {
        test_harness.cleanup_harness();
        return -1;
    }

    // Update dirty_expire_time.
    std::wcout << L"Updating dirty_expire_time_centisecs to "   << dirty_expire_time_centisecs << std::endl;
    logfile << "Updating dirty_expire_time_centisecs to "   << dirty_expire_time_centisecs << std::endl;
    const wchar_t* old_expire_time = test_harness.update_dirty_expire_time(dirty_expire_time_centisecs.c_str());
    if (old_expire_time == NULL) 
    {
        std::cerr << "Error updating dirty_expire_time_centisecs" << std::endl;
        test_harness.cleanup_harness();
        return -1;
    }


    /*****************************************************************************
     * PHASE 1:
     * Setup the base image of the disk for snapshots later. This could happen in
     * one of several ways:
     * 1. The -r flag specifies that there are log files which contain the disk
     *    image. These will now be loaded from disk
     * 2. The -b flag specifies that CrashMonkey is running as a "background"
     *    process of sorts and should listen on its socket for commands from the
     *    user telling it when to perform certain actions. The user is responsible
     *    for running their own pre-test setup methods at the proper times
     * 3. CrashMonkey is running as a standalone program. It will run the pre-test
     *    setup methods defined in the test case static object it loaded
     ****************************************************************************/

    std::cout << std::endl << "========== PHASE 1: Creating base disk image =========="        << std::endl;
    logfile << std::endl << "========== PHASE 1: Creating base disk image =========="        << std::endl;
    // Run the normal test setup stuff if we don't have a log file.
    if (log_file_load.empty()) 
    {
        /***************************************************************************
         * Setup for both background operation and standalone mode operation.
         **************************************************************************/
        if (flags_dev.empty()) 
        {
            std::cerr << "No device to copy flags from given" << std::endl;
            return -1;
        }

        // Device flags only need set if we are logging requests.
        test_harness.set_flag_device(flags_dev);

        // Format test drive to desired type.
        std::cout << "Formatting test drive" << std::endl;
        logfile << "Formatting test drive" << std::endl;
        if (test_harness.format_drive() != SUCCESS) 
        {
            std::cerr << "Error formatting test drive" << std::endl;
            test_harness.cleanup_harness();
            return -1;
        }

        // Mount test file system for pre-test setup.
        std::cout << "Mounting test file system for pre-test setup" << std::endl;
        logfile << "Mounting test file system for pre-test setup" << std::endl;
        if (test_harness.mount_device_raw(mount_opts.c_str()) != SUCCESS) 
        {
            std::cerr << "Error mounting test device" << std::endl;
            test_harness.cleanup_harness();
            return -1;
        }

        // TODO(ashmrtn): Close startup socket fd here.

#ifdef ENABLE_BACKGROUND
        //if (background) 
        //{
            std::cout << "+++++ Please run any needed pre-test setup +++++" << std::endl;
            logfile << "+++++ Please run any needed pre-test setup +++++" << std::endl;
            /*************************************************************************
             * Background mode user setup. Wait for the user to tell use that they
             * have finished the pre-test setup phase.
             ************************************************************************/
            SocketMessage command;
            do 
            {
                if (background_com->WaitForMessage(&command) != SocketError::kNone) 
                {
                    std::cerr << "Error getting message from socket" << std::endl;
                    delete background_com;
                    test_harness.cleanup_harness();
                    return -1;
                }

                if (command.type != SocketMessage::kBeginLog) 
                {
                    if (background_com->SendCommand(SocketMessage::kInvalidCommand) !=
                        SocketError::kNone) 
                    {
                        std::cerr << "Error sending response to client" << std::endl;
                        delete background_com;
                        test_harness.cleanup_harness();
                        return -1;
                    }
                    background_com->CloseClient();
                }
            } while (command.type != SocketMessage::kBeginLog);
        //}
#else
        //else 
        //{
            /*************************************************************************
             * Standalone mode user setup. Run the pre-test "setup()" method defined
             * in the test case. Run as a separate process for the sake of
             * cleanliness.
             ************************************************************************/
            std::cout << "Running pre-test setup" << std::endl;
            logfile << "Running pre-test setup" << std::endl;
            // <YUAN> 在子进程中执行test_setup，父进程等待执行完成。然后继续
            // <YUAN> 暂时改为单线程执行
/*
            const pid_t child = fork();
            if (child < 0) 
            {
                std::cerr << "Error creating child process to run pre-test setup" << std::endl;
                test_harness.cleanup_harness();
                return -1;
            }
            else if (child != 0) 
            {   // Parent process should wait for child to terminate before proceeding.
                pid_t status;
                wait(&status);
                if (status != 0) 
                {
                    std::cerr << "Error in pre-test setup" << std::endl;
                    test_harness.cleanup_harness();
                    return -1;
                }
            }
            else {   return test_harness.test_setup();       }
            */
            test_harness.test_setup();
        //}
#endif
        /***************************************************************************
         * Pre-test setup complete. Unmount the test file system and snapshot the
         * disk for use in workload and tests.
         **************************************************************************/
         // Unmount the test file system after pre-test setup.
        std::cout << "Unmounting test file system after pre-test setup" << std::endl;
        logfile << "Unmounting test file system after pre-test setup" << std::endl;
        if (test_harness.umount_device() != SUCCESS) 
        {
            test_harness.cleanup_harness();
            return -1;
        }

        // Create snapshot of disk for testing.
        std::cout << "Making new snapshot" << std::endl;
        logfile << "Making new snapshot" << std::endl;
        if (test_harness.clone_device() != SUCCESS) 
        {
            test_harness.cleanup_harness();
            return -1;
        }

        // If we're logging this test run then also save the snapshot.
        if (!log_file_save.empty()) 
        {
            /*************************************************************************
             * The -l flag specifies that we should save the information for this
             * harness execution. Therefore, save the disk image we are using as the
             * base image for our snapshots.
             ************************************************************************/
            std::cout << "Saving snapshot to log file" << std::endl;
            logfile << "Saving snapshot to log file" << std::endl;
            if (test_harness.log_snapshot_save(log_file_save + L"_snap")  != SUCCESS) 
            {
                test_harness.cleanup_harness();
                return -1;
            }
        }
    }
    else
    {
        /***************************************************************************
         * The -r flag specifies that we should load information from the provided
         * log file. Load the base disk image for snapshots here.
         **************************************************************************/
         // Load the snapshot in the log file and then write it to disk.
        std::cout << "Loading saved snapshot" << std::endl;
        logfile << "Loading saved snapshot" << std::endl;
        if (test_harness.log_snapshot_load(log_file_load + L"_snap") != SUCCESS) 
        {
            test_harness.cleanup_harness();
            return -1;
        }
    }

    /*****************************************************************************
     * PHASE 2:
     * Obtain a series of disk epochs to operate on in the test phase of the
     * harness. Again, this could happen in one of several ways:
     * 1. The -r flag specifies that there are log files which contain the disk
     *    epochs. These will now be loaded from disk
     * 2. The -b flag specifies that CrashMonkey is running as a "background"
     *    process of sorts and should listen on its socket for commands from the
     *    user telling it when to perform certain actions. The user is responsible
     *    for running their own workload methods at the proper times
     * 3. CrashMonkey is running as a standalone program. It will run the workload
     *    methods defined in the test case static object it loaded
     ****************************************************************************/

    std::cout << std::endl << "========== PHASE 2: Recording user workload =========="        << std::endl;
    logfile << std::endl << "========== PHASE 2: Recording user workload =========="        << std::endl;
    // TODO(ashmrtn): Consider making a flag for this?
    std::cout << "Clearing caches" << std::endl;
    logfile << "Clearing caches" << std::endl;
    if (test_harness.clear_caches() != SUCCESS) 
    {
        std::cerr << "Error clearing caches" << std::endl;
        test_harness.cleanup_harness();
        return -1;
    }

    // No log file given so run the test profile.
    if (log_file_load.empty()) 
    {
        /***************************************************************************
         * Preparations for both background operation and standalone mode operation.
         **************************************************************************/

        // Insert the disk block wrapper into the kernel.
        std::cout << "Inserting wrapper module into kernel" << std::endl;
        logfile << "Inserting wrapper module into kernel" << std::endl;
        if (test_harness.insert_wrapper(NULL) != SUCCESS) 
        {
            std::cerr << "Error inserting kernel wrapper module" << std::endl;
            test_harness.cleanup_harness();
            return -1;
        }

        // Get access to wrapper module ioctl functions via FD.
        std::cout << "Getting wrapper device ioctl fd" << std::endl;
        logfile << "Getting wrapper device ioctl fd" << std::endl;
        if (test_harness.get_wrapper_ioctl() != SUCCESS) 
        {
            std::cerr << "Error opening device file" << std::endl;
            test_harness.cleanup_harness();
            return -1;
        }

        // Clear wrapper module logs prior to test profiling.
        std::cout << "Clearing wrapper device logs" << std::endl;
        logfile << "Clearing wrapper device logs" << std::endl;
        test_harness.clear_wrapper_log();
        std::cout << "Enabling wrapper device logging" << std::endl;
        logfile << "Enabling wrapper device logging" << std::endl;
        test_harness.begin_wrapper_logging();

        // We also need to log the changes made by mount of the FS
        // because the snapshot is taken after an unmount.

        // Mount the file system under the wrapper module for profiling.
        std::cout << "Mounting wrapper file system" << std::endl;
        if (test_harness.mount_wrapper_device(mount_opts.c_str()) != SUCCESS) 
        {
            std::cerr << "Error mounting wrapper file system" << std::endl;
            test_harness.cleanup_harness();
            return -1;
        }

        /***************************************************************************
         * Run the actual workload that we will be testing.
         **************************************************************************/
#ifdef ENABLE_BACKGROUND

        if (background) 
        {
            /************************************************************************
             * Background mode user workload. Tell the user we have finished workload
             * preparations and are ready for them to run the workload since we are
             * now logging requests.
             ***********************************************************************/
            if (background_com->SendCommand(SocketMessage::kBeginLogDone) != SocketError::kNone) 
            {
                std::cerr << "Error telling user ready for workload" << std::endl;
                delete background_com;
                test_harness.cleanup_harness();
                return -1;
            }
            background_com->CloseClient();

            std::cout << "+++++ Please run workload +++++" << std::endl;
            logfile << "+++++ Please run workload +++++" << std::endl;

            // Wait for the user to tell us they are done with the workload.
            SocketMessage command;
            bool done = false;
            do {
                if (background_com->WaitForMessage(&command) != SocketError::kNone) {
                    std::cerr << "Error getting command from socket" << std::endl;
                    delete background_com;
                    test_harness.cleanup_harness();
                    return -1;
                }

                switch (command.type) {
                case SocketMessage::kEndLog:
                    done = true;
                    break;
                case SocketMessage::kCheckpoint:
                    if (test_harness.CreateCheckpoint() == SUCCESS) {
                        if (background_com->SendCommand(SocketMessage::kCheckpointDone) !=
                            SocketError::kNone) {
                            std::cerr << "Error telling user done with checkpoint" << std::endl;
                            delete background_com;
                            test_harness.cleanup_harness();
                            return -1;
                        }
                    }
                    else {
                        if (background_com->SendCommand(SocketMessage::kCheckpointFailed)
                            != SocketError::kNone) {
                            std::cerr << "Error telling user checkpoint failed" << std::endl;
                            delete background_com;
                            test_harness.cleanup_harness();
                            return -1;
                        }
                    }
                    background_com->CloseClient();
                    break;
                default:
                    if (background_com->SendCommand(SocketMessage::kInvalidCommand) !=
                        SocketError::kNone) {
                        std::cerr << "Error sending response to client" << std::endl;
                        delete background_com;
                        test_harness.cleanup_harness();
                        return -1;
                    }
                    background_com->CloseClient();
                    break;
                }
            } while (!done);
        }
#else
        //else {
        /*************************************************************************
         * Standalone mode user workload. Fork off a new process and run test
         * profiling. Forking makes it easier to handle making sure everything is
         * taken care of in profiling and ensures that even if all file handles
         * aren't closed in the process running the worload, the parent won't hang
         * due to a busy mount point.
         ************************************************************************/
        std::cout << "Running test profile" << std::endl;
        logfile << "Running test profile" << std::endl;
        bool last_checkpoint = false;
        int checkpoint = 0;
        /*************************************************************************
         * If automated_check_test is enabled, a snapshot is taken at every checkpoint
         * in the run() workload. The first iteration is the complete execution of run()
         * and is profiled. Subsequent iterations save snapshots at every checkpoint()
         * present in the run() workload.
         ************************************************************************/
        do 
        {
            //const pid_t child = fork();
            //if (child < 0) 
            //{
            //    std::cerr << "Error spinning off test process" << std::endl;
            //    test_harness.cleanup_harness();
            //    return -1;
            //}
            FILE * change_fd=0;
            if (checkpoint == 0)
            {
                //change_fd = open(kChangePath, O_CREAT | O_WRONLY | O_TRUNC, S_IRUSR | S_IWUSR);
                //if (change_fd < 0) { return change_fd; }
                _wfopen_s(&change_fd, kChangePath, L"w+");
                if (change_fd == NULL) THROW_ERROR(ERR_APP, L"[err] failed on opening file %s", kChangePath);
            }
            //const int res = test_harness.test_run(change_fd, checkpoint);
            //if (checkpoint == 0) { close(change_fd); }
            //return res;
            int res = test_harness.async_test_run(change_fd, checkpoint);


            //else if (child != 0) 
            //{   
                //<YUAN> 父进程
                //pid_t status = -1;
                //pid_t wait_res = 0;
            DWORD exit_code;
                do 
                {
                    SocketMessage m;
                    SocketError se;
                    se = background_com->TryForMessage(&m);

                    if (se == SocketError::kNone) 
                    {
                        if (m.type == SocketMessage::kCheckpoint) 
                        {
                            if (test_harness.CreateCheckpoint() == SUCCESS) 
                            {
                                if (background_com->SendCommand(SocketMessage::kCheckpointDone)
                                                != SocketError::kNone) 
                                {
                                    // TODO(ashmrtn): Handle better.
                                    std::cerr << "Error telling user done with checkpoint" << std::endl;
                                    delete background_com;
                                    test_harness.cleanup_harness();
                                    return -1;
                                }
                            }
                            else 
                            {
                                if (background_com->SendCommand(SocketMessage::kCheckpointFailed)
                                                != SocketError::kNone) 
                                {
                                    // TODO(ashmrtn): Handle better.
                                    std::cerr << "Error telling user checkpoint failed" << std::endl;
                                    delete background_com;
                                    test_harness.cleanup_harness();
                                    return -1;
                                }
                            }
                        }
                        else 
                        {
                            if (background_com->SendCommand(SocketMessage::kInvalidCommand)
                                        != SocketError::kNone) 
                            {
                                std::cerr << "Error sending response to client" << std::endl;
                                delete background_com;
                                test_harness.cleanup_harness();
                                return -1;
                            }
                        }
                        background_com->CloseClient();
                    }
                    //<YUAN> waitpid：等待子进程结束或者其他信号。WNOHANG：表示如果没有子进程结束，则不等待立刻返回。
                    // 此处用于检测子进程是否已经结束。
                    //wait_res = waitpid(child, &status, WNOHANG);
                    exit_code = test_harness.get_test_running_status();
                } while (exit_code == 259);     // STILL_ACTIVE

                // 调用GetThreadExitCode的错误处理已经在get_test_running_status中处理了；
                //if (WIFEXITED(status) == 0) 
                //{
                //    std::cerr << "Error terminating test_run process, status: " << status << std::endl;
                //    test_harness.cleanup_harness();
                //    return -1;
                //}
                //else 
                //{
                    //if (WEXITSTATUS(status) == 1) {last_checkpoint = true; }
                if (exit_code == 1) last_checkpoint = true;
                else if (exit_code == 0)
                    //else if (WEXITSTATUS(status) == 0) 
                    {
                        if (checkpoint == 0) { std::cout << "Completely executed run process" << std::endl;  }
                        else {  std::cout << "Run process hit checkpoint " << checkpoint << std::endl;   }
                    }
                    else 
                    {
                        std::cerr << "Error in test run, exits with status: " << exit_code << std::endl;
                        test_harness.cleanup_harness();
                        return -1;
                    }
                //}
            //}
            //else 
            //{   //<YUAN> 子进程                   // Forked process' stuff.
            //    int change_fd;
            //    if (checkpoint == 0) 
            //    {
            //        change_fd = open(kChangePath, O_CREAT | O_WRONLY | O_TRUNC, S_IRUSR | S_IWUSR);
            //        if (change_fd < 0) {       return change_fd;                     }
            //    }
            //    const int res = test_harness.test_run(change_fd, checkpoint);
            //    if (checkpoint == 0) {   close(change_fd);   }
            //    return res;
            //}
            //<YUAN> 父进程：测试结束，处理结果
            // End wrapper logging for profiling the complete execution of run process
            if (checkpoint == 0) 
            {
                std::cout << "Waiting for writeback delay" << std::endl;
                logfile << "Waiting for writeback delay" << std::endl;
                unsigned int sleep_time = test_harness.GetPostRunDelay();
                //while (sleep_time > 0) 
                //{
                //    sleep_time = sleep(sleep_time);
                //}
                Sleep(sleep_time);

                std::cout << "Disabling wrapper device logging" << std::endl;
                logfile << "Disabling wrapper device logging" << std::endl;
                test_harness.end_wrapper_logging();
                std::cout << "Getting wrapper data" << std::endl;
                logfile << "Getting wrapper data" << std::endl;
                if (test_harness.get_wrapper_log() != SUCCESS) 
                {
                    test_harness.cleanup_harness();
                    return -1;
                }

                std::cout << "Unmounting wrapper file system after test profiling" << std::endl;
                logfile << "Unmounting wrapper file system after test profiling" << std::endl;
                if (test_harness.umount_device() != SUCCESS) 
                {
                    std::cerr << "Error unmounting wrapper file system" << std::endl;
                    test_harness.cleanup_harness();
                    return -1;
                }

                std::cout << "Close wrapper ioctl fd" << std::endl;
                logfile << "Close wrapper ioctl fd" << std::endl;
                test_harness.put_wrapper_ioctl();
                std::cout << "Removing wrapper module from kernel" << std::endl;
                logfile << "Removing wrapper module from kernel" << std::endl;
                if (test_harness.remove_wrapper() != SUCCESS) 
                {
                    std::cerr << "Error cleaning up: remove wrapper module" << std::endl;
                    test_harness.cleanup_harness();
                    return -1;
                }

                // Getting the tracking data
                std::cout << "Getting change data" << std::endl;
                logfile << "Getting change data" << std::endl;
//                const int change_fd = open(kChangePath, O_RDONLY);
                FILE* change_fd = NULL;
                _wfopen_s(&change_fd, kChangePath, L"r");
                if (change_fd == NULL) 
                {
                    std::cerr << "Error reading change data" << std::endl;
                    test_harness.cleanup_harness();
                    return -1;
                }

//                if (lseek(change_fd, 0, SEEK_SET) < 0) 
                if (fseek(change_fd, 0, SEEK_SET)!= 0)
                {
                    std::cerr << "Error reading change data" << std::endl;
                    test_harness.cleanup_harness();
                    return -1;
                }

                if (test_harness.GetChangeData(change_fd) != SUCCESS) 
                {
                    test_harness.cleanup_harness();
                    return -1;
                }
            }
                
            if (automate_check_test) 
            {
                // Map snapshot of the disk to the current checkpoint and unmount the clone
                test_harness.mapCheckpointToSnapshot(checkpoint);
                if (checkpoint != 0) 
                {
                    if (test_harness.umount_snapshot() != SUCCESS) 
                    {
                        test_harness.cleanup_harness();
                        return -1;
                    }
                }
                // get a new diskclone and mount it for next the checkpoint
                test_harness.getNewDiskClone(checkpoint);
                if (!last_checkpoint) 
                {
                    if (test_harness.mount_snapshot() != SUCCESS) 
                    {
                        test_harness.cleanup_harness();
                        return -1;
                    }
                }
            }
            // reset the snapshot path if we completed all the executions
            if (automate_check_test && last_checkpoint) 
            {
                test_harness.getCompleteRunDiskClone();
            }
            // Increment the checkpoint at which run exits
            checkpoint += 1;
        } while (!last_checkpoint && automate_check_test);
        //}
#endif
        /***************************************************************************
         * Worload complete, Clean up things and end logging.
         **************************************************************************/

         // Wait a small amount of time for writes to propogate to the block
         // layer and then stop logging writes.
         // TODO (P.S.) pull out the common code between the code path when
         // checkpoint is zero above and if background mode is on here
#ifdef ENABLE_BACKGROUND
        if (background) {
            std::cout << "Waiting for writeback delay" << std::endl;
            logfile << "Waiting for writeback delay" << std::endl;
            unsigned int sleep_time = test_harness.GetPostRunDelay();
            while (sleep_time > 0) {
                sleep_time = sleep(sleep_time);
            }

            std::cout << "Disabling wrapper device logging" << std::endl;
            logfile << "Disabling wrapper device logging" << std::endl;
            test_harness.end_wrapper_logging();
            std::cout << "Getting wrapper data" << std::endl;
            logfile << "Getting wrapper data" << std::endl;
            if (test_harness.get_wrapper_log() != SUCCESS) {
                test_harness.cleanup_harness();
                return -1;
            }

            std::cout << "Unmounting wrapper file system after test profiling" << std::endl;
            logfile << "Unmounting wrapper file system after test profiling" << std::endl;
            if (test_harness.umount_device() != SUCCESS) {
                std::cerr << "Error unmounting wrapper file system" << std::endl;
                test_harness.cleanup_harness();
                return -1;
            }

            std::cout << "Close wrapper ioctl fd" << std::endl;
            logfile << "Close wrapper ioctl fd" << std::endl;
            test_harness.put_wrapper_ioctl();
            std::cout << "Removing wrapper module from kernel" << std::endl;
            logfile << "Removing wrapper module from kernel" << std::endl;
            if (test_harness.remove_wrapper() != SUCCESS) {
                std::cerr << "Error cleaning up: remove wrapper module" << std::endl;
                test_harness.cleanup_harness();
                return -1;
            }

            // Getting the tracking data
            std::cout << "Getting change data" << std::endl;
            logfile << "Getting change data" << std::endl;
            const int change_fd = open(kChangePath, O_RDONLY);
            if (change_fd < 0) {
                std::cerr << "Error reading change data" << std::endl;
                test_harness.cleanup_harness();
                return -1;
            }

            if (lseek(change_fd, 0, SEEK_SET) < 0) {
                std::cerr << "Error reading change data" << std::endl;
                test_harness.cleanup_harness();
                return -1;
            }

            if (test_harness.GetChangeData(change_fd) != SUCCESS) {
                test_harness.cleanup_harness();
                return -1;
            }
        }
#endif

        logfile << std::endl << std::endl << "Recorded workload:" << std::endl;
        test_harness.log_disk_write_data(logfile);
        logfile << std::endl << std::endl;

        // Write log data out to file if we're given a file.
        if (!log_file_save.empty()) 
        {
            /*************************************************************************
             * The -l flag specifies that we should save the information for this
             * harness execution. Therefore, save the series of disk epochs we just
             * logged so they can be reused later if the -r flag is given.
             ************************************************************************/
            std::cout << "Saving logged profile data to disk" << std::endl;
            logfile << "Saving logged profile data to disk" << std::endl;
            if (test_harness.log_profile_save(log_file_save + L"_profile") != SUCCESS) 
            {
                std::cerr << "Error saving logged test file" << std::endl;
                // TODO(ashmrtn): Remove this in later versions?
                test_harness.cleanup_harness();
                return -1;
            }
        }

#ifdef ENABLE_BACKGROUND
        /***************************************************************************
         * Background mode. Tell the user we have finished logging and cleaning up
         * and that, if they need to, they can do a bit of cleanup on their end
         * before beginning testing.
         **************************************************************************/
        if (background) 
        {
            if (background_com->SendCommand(SocketMessage::kEndLogDone) != SocketError::kNone) 
            {
                std::cerr << "Error telling user done logging" << std::endl;
                delete background_com;
                test_harness.cleanup_harness();
                return -1;
            }
            background_com->CloseClient();
        }
#endif
    }
    else //<YUAN> log_file_load not empty
    {
        /***************************************************************************
         * The -r flag specifies that we should load information from the provided
         * log file. Load the series of disk epochs which we will be operating on.
         **************************************************************************/
        std::cout << "Loading logged profile data from disk" << std::endl;
        logfile << "Loading logged profile data from disk" << std::endl;
        if (test_harness.log_profile_load(log_file_load + L"_profile") != SUCCESS) 
        {
            std::cerr << "Error loading logged test file" << std::endl;
            test_harness.cleanup_harness();
            return -1;
        }
    }


    /*****************************************************************************
     * PHASE 3:
     * Now that we have finished gathering data, run tests to see if we can find
     * file system inconsistencies. Either:
     * 1. The -b flag specifies that CrashMonkey is running as a "background"
     *    process of sorts and should listen on its socket for the command telling
     *    it to begin testing
     * 2. CrashMonkey is running as a standalone program. It should immediately
     *    begin testing
     ****************************************************************************/

#ifdef ENABLE_BACKGROUND
    if (background) 
    {
        /***************************************************************************
         * Background mode. Wait for the user to tell us to start testing.
         **************************************************************************/
        SocketMessage command;
        do {
            std::cout << "+++++ Ready to run tests, please confirm start +++++" << std::endl;
            logfile << "+++++ Ready to run tests, please confirm start +++++" << std::endl;
            if (background_com->WaitForMessage(&command) != SocketError::kNone) {
                std::cerr << "Error getting command from socket" << std::endl;
                delete background_com;
                test_harness.cleanup_harness();
                return -1;
            }

            if (command.type != SocketMessage::kRunTests) {
                if (background_com->SendCommand(SocketMessage::kInvalidCommand) !=
                    SocketError::kNone) {
                    std::cerr << "Error sending response to client" << std::endl;
                    delete background_com;
                    test_harness.cleanup_harness();
                    return -1;
                }
                background_com->CloseClient();
            }
        } while (command.type != SocketMessage::kRunTests);
    }
#endif

    std::cout << std::endl << "========== PHASE 3: Running tests based on recorded data =========="  << std::endl;
    logfile << std::endl << "========== PHASE 3: Running tests based on recorded data ==========" << std::endl;

    // TODO(ashmrtn): Fix the meaning of "dry-run". Right now it means do
    // everything but run tests (i.e. run setup and profiling but not testing.)
    /***************************************************************************
     * Run tests and print the results of said tests.
     **************************************************************************/
    if (permuted_order_replay) 
    {
        std::cout << "Writing profiled data to block device and checking with fsck" <<  std::endl;
        logfile << "Writing profiled data to block device and checking with fsck" <<   std::endl;

        test_harness.test_check_random_permutations(full_bio_replay, iterations, logfile);

        for (unsigned int i = 0; i < fs_testing::Tester::NUM_TIME; ++i) 
        {
            std::cout << "\t" << (fs_testing::Tester::time_stats)i << ": " <<
                test_harness.get_timing_stat((fs_testing::Tester::time_stats)i).count() << " ms" << std::endl;
        }
    }

    if (in_order_replay) 
    {
        std::cout << std::endl << std::endl <<    "Writing data out to each Checkpoint and checking with fsck" << std::endl;
        logfile << std::endl << std::endl <<   "Writing data out to each Checkpoint and checking with fsck" << std::endl;
        test_harness.test_check_log_replay(logfile, automate_check_test);
    }

    std::cout << std::endl;
    logfile << std::endl;
    test_harness.PrintTestStats(std::wcout);
    test_harness.PrintTestStats(logfile);
    test_harness.EndTestSuite();

    std::cout << std::endl << "========== PHASE 4: Cleaning up ==========" << std::endl;
    logfile << std::endl << "========== PHASE 4: Cleaning up ==========" << std::endl;

    /*****************************************************************************
     * PHASE 4:
     * We have finished. Clean up the test harness. Tell the user we have finished
     * testing if the -b flag was given and we are running in background mode.
     ****************************************************************************/
    logfile.close();
    test_harness.remove_cow_brd();
    test_harness.cleanup_harness();
#ifdef ENABLE_BACKGROUND
    if (background) 
    {
        if (background_com->SendCommand(SocketMessage::kRunTestsDone) !=
            SocketError::kNone) {
            std::cerr << "Error telling user done testing" << std::endl;
            delete background_com;
            test_harness.cleanup_harness();
            return -1;
        }
    }
#endif
    delete background_com;

    return 0;


}
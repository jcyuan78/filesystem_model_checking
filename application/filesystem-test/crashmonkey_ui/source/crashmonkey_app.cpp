
#include "pch.h"

#include <fstream>
#include <iostream>

#include "Tester.h"
#include "ThreadMessageQue.h"
#include "crashmonkey_app.h"
#include <boost/cast.hpp>
#include <boost/property_tree/xml_parser.hpp>
#include "wrapper_dev.h"

using fs_testing::utils::communication::SocketMessage;
using fs_testing::utils::communication::SocketError;

// <YUAN> Start configuraiton
//#define ENABLE_BACKGROUND

LOCAL_LOGGER_ENABLE(L"fstester.app", LOGGER_LEVEL_DEBUGINFO);

const TCHAR CCrashMonkeyApp::LOG_CONFIG_FN[] = L"crashmonkey.cfg";
typedef jcvos::CJCApp<CCrashMonkeyApp>	CApplication;
CApplication _app;

#define _class_name_	CApplication
BEGIN_ARGU_DEF_TABLE()


ARGU_DEF(L"config", 'g', m_config_file, L"configuration xml file name")

//ARGU_DEF(L"test_dev", 'd', m_test_dev_name, L"test device")
//ARGU_DEF(L"test_case", 'x', m_test_case_name, L"test case name")
ARGU_DEF(L"test_dev_size", 's', m_test_dev_size, L"test device size", (size_t)0)
ARGU_DEF(L"disk_size", 'e', m_disk_size, L"disk size", (size_t)10240)

ARGU_DEF(L"disable_permuted_order", 'P', m_disable_permuted_order_replay, L"Disable permuted oerder replay")
ARGU_DEF(L"automate_check_test", 'c', m_automate_check_test, L"Enable automate check test")
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
//    m_test_dev_name = L"/dev/ram0";
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

    LoadFactory(m_fs_factory, fs_lib);


    const boost::property_tree::wptree & device_pt = pt.get_child(L"config.device");
    m_dev_name = device_pt.get<std::wstring>(L"name");
    fs_lib = device_pt.get<std::wstring>(L"lib");
    LoadFactory(m_dev_factory, fs_lib);
    m_dev_factory->CreateVirtualDisk(m_test_dev, device_pt, true);
    
    m_test_module = pt.get<std::wstring>(L"config.testcase.lib");
    m_test_case = pt.get<std::wstring>(L"config.testcase.name");

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
    bool br = (get_factory)(factory);
    if (!br || !factory) THROW_ERROR(ERR_USER, L"failed on getting plugin register in %s", lib_name.c_str());
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
//	std::wstring permuter(PERMUTER_SO_PATH L"RandomPermuter.so");
    std::wstring permuter_module;
    std::wstring permuter_class(L"RandomPermuter");
	bool background = false;
//	bool m_automate_check_test = false;
	bool dry_run = false;
	bool no_lvm = false;
	bool verbose = false;
	bool in_order_replay = true;
    bool permuted_order_replay = !m_disable_permuted_order_replay;
	bool full_bio_replay = false;
	int iterations = 10000;
//	int disk_size = 10240;
	unsigned int sector_size = 512;
	int option_idx = 0;

    LoadConfig();

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
    const std::wstring path = m_test_module;

#ifdef _TO_BE_IMPLEMENTED_
    // Get the name of the test being run.
    size_t begin = path.rfind('/');
    // Remove everything before the last /.
    std::wstring test_name = path.substr(begin + 1);
    // Remove the extension.
    test_name = test_name.substr(0, test_name.length() - 3);
#endif
    std::wstring test_name = m_test_case;
    // Get the date and time stamp and format.
    time_t now = time(0);
    tm local_now;
    localtime_s(&local_now, &now);
    wchar_t time_st[40];
    wcsftime(time_st, sizeof(time_st), L"%Y%m%d_%H%M%S", &local_now);
#ifdef _DEBUG
    std::wstring s = test_name + L".log";
#else
    std::wstring s = std::wstring(time_st) + L"-" + test_name + L".log";
#endif
    std::wofstream logfile(s);

    //<YUAN> Create file system for test
    jcvos::auto_interface<IFileSystem> filesystem;
    bool br = m_fs_factory->CreateFileSystem(filesystem, m_fs_name);
    if (!br | !filesystem) THROW_ERROR(ERR_APP, L"failed on creating file system %s", m_fs_name.c_str());
    JCASSERT(m_test_dev);

    // 从raw disk获取snapshot
    jcvos::auto_interface<IVirtualDisk> snapshot;
    int ir = m_test_dev->IoCtrl(0, COW_BRD_GET_SNAPSHOT+1, snapshot);
    if (ir != SUCCESS || !snapshot) THROW_ERROR(ERR_APP, L"failed on getting snapshot device");

    jcvos::auto_interface<IVirtualDisk> raw_disk;
    ir = m_test_dev->IoCtrl(0, COW_BRD_GET_SNAPSHOT, raw_disk);
    if (ir != SUCCESS || !raw_disk) THROW_ERROR(ERR_APP, L"failed on getting raw device");
    //创建 wrapper device 
    jcvos::auto_interface<CWrapperDisk> wrapper_disk(jcvos::CDynamicInstance<CWrapperDisk>::Create());
    if (!wrapper_disk) THROW_ERROR(ERR_MEM, L"Failed on creating wrapper device");
    wrapper_disk->Initialize(snapshot);

    // This should be changed in the option is added to mount tests in other directories.
    //<YUAN> 由于后续需要format file system, 此处先不要mount
    std::wstring mount_dir = L"/mnt/snapshot";

    MESSAGE("========== PHASE 0: Setting up CrashMonkey basics ==========" << std::endl);
    if (iterations < 0) 
    {
        std::cerr << "Please give a positive number of iterations to run" << std::endl;
        return -1;
    }

    m_disk_size = m_test_dev->GetCapacity();
    if (m_disk_size <= 0) 
    {
        std::cerr << "Please give a positive number for the RAM disk size to use"       << std::endl;
        return -1;
    }

    if (sector_size <= 0) 
    {
        std::cerr << "Please give a positive number for the sector size" << std::endl;
        return -1;
    }

    jcvos::auto_ptr<fs_testing::utils::communication::ServerSocket> background_com(
        new fs_testing::utils::communication::ServerSocket(fs_testing::utils::communication::kSocketNameOutbound) );
    if (background_com->Init(kSocketQueueDepth) < 0) 
    {
        int err_no = errno;
        std::cerr << "Error starting socket to listen on " << err_no << std::endl;
        //delete background_com;
        return -1;
    }

    fs_testing::Tester test_harness(m_disk_size, sector_size, verbose, logfile);
    test_harness.StartTestSuite();      // 准备一个测试结果的空间到 container

    MESSAGE("Inserting RAM disk module" << std::endl);
    // 安装RAM DISK驱动（cow brd），并打开设备，句柄保存在Tester::cow_brd_fd
    if (test_harness.insert_cow_brd(m_test_dev) != SUCCESS)       
    {
        std::cerr << "Error inserting RAM disk module" << std::endl;
        return -1;
    }
    test_harness.set_fs_type(fs_type);
    test_harness.set_device(L"/dev/ram0", m_test_dev);
    size_t filesystem_size = m_disk_size;

    //<YUAN>: 获取文件系统所在分区的大小， 并写入环境变量
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
    
    // Load the class being tested.
    std::cout   << "Loading test case" << std::endl;
    if (test_harness.test_load_class(m_test_module.c_str(), m_test_case.c_str()) != SUCCESS) 
        THROW_ERROR(ERR_APP, L"failed on loading test class: %s", m_test_module.c_str());

    test_harness.test_init_values(mount_dir, m_test_dev_size, filesystem);

    // Load the permuter to use for the test.
    // TODO(ashmrtn): Consider making a line in the test file which specifies the permuter to use?
    MESSAGE("Loading permuter" << std::endl);
    if (test_harness.permuter_load_class(permuter_module.c_str(), permuter_class.c_str()) != SUCCESS)
        THROW_ERROR(ERR_APP, L"failed on loading permuter class: %s", permuter_module.c_str());

    // Update dirty_expire_time.
    MESSAGE(L"Updating dirty_expire_time_centisecs to " << dirty_expire_time_centisecs << std::endl);
    const wchar_t* old_expire_time = test_harness.update_dirty_expire_time(dirty_expire_time_centisecs.c_str());
    if (old_expire_time == NULL) THROW_ERROR(ERR_APP, L"Error updating dirty_expire_time_centisecs");

    /*****************************************************************************
     * PHASE 1:
     * Setup the base image of the disk for snapshots later. This could happen in
     * one of several ways:
     * 1. The -r flag specifies that there are log files which contain the disk image. These will now be loaded from disk
     * 2. The -b flag specifies that CrashMonkey is running as a "background" process of sorts and should listen on its
            socket for commands from the user telling it when to perform certain actions. The user is responsible
            for running their own pre-test setup methods at the proper times
     * 3. CrashMonkey is running as a standalone program. It will run the pre-test
     *    setup methods defined in the test case static object it loaded
     ****************************************************************************/

    MESSAGE(std::endl << "========== PHASE 1: Creating base disk image ==========" << std::endl);
    // Run the normal test setup stuff if we don't have a log file.
    if (log_file_load.empty()) 
    {
        /***************************************************************************
         * Setup for both background operation and standalone mode operation.
         **************************************************************************/
        if (flags_dev.empty())  THROW_ERROR(ERR_APP, L"No device to copy flags from given");

        // Device flags only need set if we are logging requests.
        test_harness.set_flag_device(flags_dev);

        // Format test drive to desired type.
        MESSAGE(L"Formatting test drive" << std::endl);
        if (test_harness.format_drive() != SUCCESS)    THROW_ERROR(ERR_APP, L"Error formatting test drive");

        // Mount test file system for pre-test setup.
        MESSAGE(L"Mounting test file system for pre-test setup" << std::endl);
        if (test_harness.mount_device_raw(mount_opts.c_str()) != SUCCESS)
            THROW_ERROR(ERR_APP, L"Error mounting test device");

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
                    //delete background_com;
                    test_harness.cleanup_harness();
                    return -1;
                }

                if (command.type != SocketMessage::kBeginLog) 
                {
                    if (background_com->SendCommand(SocketMessage::kInvalidCommand) !=
                        SocketError::kNone) 
                    {
                        std::cerr << "Error sending response to client" << std::endl;
                        //delete background_com;
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
             * in the test case. Run as a separate process for the sake of cleanliness.
             ************************************************************************/
            MESSAGE(L"Running pre-test setup" << std::endl);
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
        MESSAGE(L"Unmounting test file system after pre-test setup" << std::endl);
        if (test_harness.umount_device() != SUCCESS) return -1;

        // Create snapshot of disk for testing.
        MESSAGE(L"Making new snapshot" << std::endl);
        if (test_harness.clone_device() != SUCCESS)  return -1;

        // If we're logging this test run then also save the snapshot.
        if (!log_file_save.empty()) 
        {
            /*************************************************************************
             * The -l flag specifies that we should save the information for this harness execution. Therefore, 
             save the disk image we are using as the base image for our snapshots.
             ************************************************************************/
            MESSAGE(L"Saving snapshot to log file" << std::endl);
            if (test_harness.log_snapshot_save(log_file_save + L"_snap") != SUCCESS)   return -1;
        }
    }
    else
    {
        /***************************************************************************
         * The -r flag specifies that we should load information from the provided
         * log file. Load the base disk image for snapshots here.
         **************************************************************************/
         // Load the snapshot in the log file and then write it to disk.
        MESSAGE(L"Loading saved snapshot" << std::endl);
        if (test_harness.log_snapshot_load(log_file_load + L"_snap") != SUCCESS) return -1;
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

    MESSAGE(std::endl << "========== PHASE 2: Recording user workload ==========" << std::endl);
    // TODO(ashmrtn): Consider making a flag for this?
    MESSAGE("Clearing caches" << std::endl);
    if (test_harness.clear_caches() != SUCCESS) THROW_ERROR(ERR_APP, L"Error clearing caches");

    // No log file given so run the test profile.
    if (log_file_load.empty()) 
    {
        /***************************************************************************
         * Preparations for both background operation and standalone mode operation.
         **************************************************************************/

        // Insert the disk block wrapper into the kernel.
        MESSAGE(L"Inserting wrapper module into kernel" << std::endl);
        if (test_harness.insert_wrapper(wrapper_disk) != SUCCESS)   THROW_ERROR(ERR_APP, 
            L"Error inserting kernel wrapper module");

        // Get access to wrapper module ioctl functions via FD.
        MESSAGE(L"Getting wrapper device ioctl fd" << std::endl);
        if (test_harness.get_wrapper_ioctl() != SUCCESS) THROW_ERROR(ERR_APP, L"Error opening device file");

        // Clear wrapper module logs prior to test profiling.
        MESSAGE(L"Clearing wrapper device logs" << std::endl);
        test_harness.clear_wrapper_log();

        MESSAGE(L"Enabling wrapper device logging" << std::endl)
        test_harness.begin_wrapper_logging();

        // We also need to log the changes made by mount of the FS because the snapshot is taken after an unmount.
        // Mount the file system under the wrapper module for profiling.
        MESSAGE("Mounting wrapper file system" << std::endl)
        if (test_harness.mount_wrapper_device(mount_opts.c_str()) != SUCCESS)   THROW_ERROR(ERR_APP, 
            L"Error mounting wrapper file system");

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
                //delete background_com;
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
                    //delete background_com;
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
                            //delete background_com;
                            test_harness.cleanup_harness();
                            return -1;
                        }
                    }
                    else {
                        if (background_com->SendCommand(SocketMessage::kCheckpointFailed)
                            != SocketError::kNone) {
                            std::cerr << "Error telling user checkpoint failed" << std::endl;
                            //delete background_com;
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
                        //delete background_com;
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
        MESSAGE(L"Running test profile" << std::endl);
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
            FILE * change_fd=0;
            if (checkpoint == 0)
            {
                _wfopen_s(&change_fd, kChangePath, L"w+");
                if (change_fd == NULL) THROW_ERROR(ERR_APP, L"[err] failed on opening file %s", kChangePath);
            }
            //<YUAN> checkpoint:告诉测试程序，需要测试到哪里，0表示测试全部。
            int res = test_harness.async_test_run(change_fd, checkpoint);
            SocketError se = background_com->WaitingForClient();
            int exit_code;
            do
            {
                SocketMessage m;
                se = background_com->TryForMessage(&m);
                if (m.type == SocketMessage::kCheckpoint)
                {
                    LOG_DEBUG(L"receive checkpoint");
                    if (test_harness.CreateCheckpoint(m.string_value) == SUCCESS)
                    {
                        LOG_DEBUG(L"reply for checkpoint");
                        if (background_com->SendCommand(SocketMessage::kCheckpointDone) != SocketError::kNone)
                            THROW_ERROR(ERR_APP, L"Error telling user done with checkpoint");
                        //{
                         //    // TODO(ashmrtn): Handle better.
                         //    std::cerr << "Error telling user done with checkpoint" << std::endl;
                         //    //delete background_com;
                         //    test_harness.cleanup_harness();
                         //    return -1;
                         //}
                    }
                    else
                    {
                        LOG_DEBUG(L"reply for error of checkpoint");
                        if (background_com->SendCommand(SocketMessage::kCheckpointFailed) != SocketError::kNone)
                            THROW_ERROR(ERR_APP, L"Error telling user checkpoint failed");
                        //{
                        //       // TODO(ashmrtn): Handle better.
                        //        std::cerr << "Error telling user checkpoint failed" << std::endl;
                        //        //delete background_com;
                        //        test_harness.cleanup_harness();
                        //        return -1;
                        //}
                    }
                }
                else if (m.type == SocketMessage::kDisconnect)
                {
                    LOG_DEBUG(L"receive disconnect");
                    break;
                }
                else
                {
                    LOG_DEBUG(L"reply for invalid message");
                    if (background_com->SendCommand(SocketMessage::kInvalidCommand) != SocketError::kNone)
                        THROW_ERROR(ERR_APP, L"Error sending response to client");
                    //{
                    //        std::cerr << "Error sending response to client" << std::endl;
                    //        //delete background_com;
                    //        test_harness.cleanup_harness();
                    //        return -1;
                    //}
                }
            // waiting for disconnect
            //<YUAN> waitpid：等待子进程结束或者其他信号。WNOHANG：表示如果没有子进程结束，则不等待立刻返回。
            // 此处用于检测子进程是否已经结束。
            } while (1);

            // 等待进程结束, 返回执行代码
            exit_code = test_harness.wait_test_complete();
//            exit_code = test_harness.get_test_running_status();

            background_com->DisconnectClient();
            if (checkpoint == 0 && change_fd!=0)           fclose(change_fd);

            // 调用GetThreadExitCode的错误处理已经在get_test_running_status中处理了；
            if (exit_code == 1) last_checkpoint = true;
            else if (exit_code == 0)
            {
                if (checkpoint == 0) {  MESSAGE(L"Completely executed run process" << std::endl); }
                else {                  MESSAGE(L"Run process hit checkpoint " << checkpoint << std::endl); }
            }
            else THROW_ERROR(ERR_APP, L"Error in test run, exits with status: %d", exit_code);
            //<YUAN> 父进程：测试结束，处理结果
            // End wrapper logging for profiling the complete execution of run process
            if (checkpoint == 0) 
            {
                MESSAGE(L"Waiting for writeback delay" << std::endl);
                unsigned int sleep_time = test_harness.GetPostRunDelay();

                Sleep(sleep_time);
                MESSAGE(L"Disabling wrapper device logging" << std::endl)
                test_harness.end_wrapper_logging();
                MESSAGE(L"Getting wrapper data" << std::endl);
                if (test_harness.get_wrapper_log() != SUCCESS) THROW_ERROR(ERR_APP, L"faild on get wrapper log");

                MESSAGE(L"Unmounting wrapper file system after test profiling" << std::endl);
                if (test_harness.umount_device() != SUCCESS) THROW_ERROR(ERR_APP, 
                    L"Error unmounting wrapper file system");

                MESSAGE(L"Close wrapper ioctl fd" << std::endl);
                test_harness.put_wrapper_ioctl();
                MESSAGE(L"Removing wrapper module from kernel" << std::endl);
                if (test_harness.remove_wrapper() != SUCCESS) THROW_ERROR(ERR_APP,
                    L"Error cleaning up: remove wrapper module");

                // Getting the tracking data
                MESSAGE(L"Getting change data" << std::endl);
                FILE* change_fd = NULL;
                _wfopen_s(&change_fd, kChangePath, L"r");
                if (change_fd == NULL) THROW_ERROR(ERR_APP, L"Error reading change data");
                if (fseek(change_fd, 0, SEEK_SET) != 0)  THROW_ERROR(ERR_APP, L"Error reading change data");
                if (test_harness.GetChangeData(change_fd) != SUCCESS)   THROW_ERROR(ERR_APP, 
                    L"failed on getting change data");
                fclose(change_fd);
            }
                
            if (m_automate_check_test) 
            {
                // Map snapshot of the disk to the current checkpoint and unmount the clone
                //<YUAN> 保存checkpoing (int)到snapshot (string, path)的映射
                test_harness.mapCheckpointToSnapshot(checkpoint);
                if (checkpoint != 0) 
                {
                    LOG_NOTICE(L"checkpoint (%d)!=0, unmount snapshot", checkpoint);
                    if (test_harness.umount_snapshot() != SUCCESS) THROW_ERROR(ERR_APP, L"failed on unmounting snapshot");
                }
                // get a new diskclone and mount it for next the checkpoint
                test_harness.getNewDiskClone(checkpoint);
                if (!last_checkpoint) 
                {
                    if (test_harness.mount_snapshot() != SUCCESS) THROW_ERROR(ERR_APP, L"failed on mount snapshot");
                }
            }
            // reset the snapshot path if we completed all the executions
            if (m_automate_check_test && last_checkpoint) 
            {
                test_harness.getCompleteRunDiskClone();
            }
            // Increment the checkpoint at which run exits
            checkpoint += 1;
        } while (!last_checkpoint && m_automate_check_test);
        //}
#endif
        /***************************************************************************
         * Worload complete, Clean up things and end logging.
         **************************************************************************/

         // Wait a small amount of time for writes to propogate to the block layer and then stop logging writes.
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

        MESSAGE(std::endl << std::endl << L"Recorded workload:" << std::endl);
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
            MESSAGE(L"Saving logged profile data to disk" << std::endl);
            if (test_harness.log_profile_save(log_file_save + L"_profile") != SUCCESS) THROW_ERROR(ERR_APP, 
                L"Error saving logged test file");
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
                //delete background_com;
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
        MESSAGE(L"Loading logged profile data from disk" << std::endl);
        if (test_harness.log_profile_load(log_file_load + L"_profile") != SUCCESS) THROW_ERROR(ERR_APP,
            L"Error loading logged test file");
    }


    /*****************************************************************************
     * PHASE 3:
     * Now that we have finished gathering data, run tests to see if we can find file system inconsistencies. Either:
     * 1. The -b flag specifies that CrashMonkey is running as a "background" process of sorts and should listen on 
            its socket for the command telling it to begin testing
     * 2. CrashMonkey is running as a standalone program. It should immediately begin testing
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
                //delete background_com;
                test_harness.cleanup_harness();
                return -1;
            }

            if (command.type != SocketMessage::kRunTests) {
                if (background_com->SendCommand(SocketMessage::kInvalidCommand) !=
                    SocketError::kNone) {
                    std::cerr << "Error sending response to client" << std::endl;
                    //delete background_com;
                    test_harness.cleanup_harness();
                    return -1;
                }
                background_com->CloseClient();
            }
        } while (command.type != SocketMessage::kRunTests);
    }
#endif

    MESSAGE(std::endl << L"========== PHASE 3: Running tests based on recorded data ==========" << std::endl);
    // TODO(ashmrtn): Fix the meaning of "dry-run". Right now it means do everything but run tests (i.e. run setup and profiling but not testing.)
    /***************************************************************************
     * Run tests and print the results of said tests.
     **************************************************************************/
//    test_harness.set_snapshot_device(0, snapshot);
    if (permuted_order_replay) 
    {
        MESSAGE(L"Writing profiled data to block device and checking with fsck" << std::endl)
        test_harness.test_check_random_permutations(full_bio_replay, iterations, logfile);

        for (unsigned int i = 0; i < fs_testing::Tester::NUM_TIME; ++i) 
        {
            std::cout << "\t" << (fs_testing::Tester::time_stats)i << ": " <<
                test_harness.get_timing_stat((fs_testing::Tester::time_stats)i).count() << " ms" << std::endl;
        }
    }

    if (in_order_replay) 
    {
        MESSAGE(std::endl << std::endl << "Writing data out to each Checkpoint and checking with fsck" << std::endl);
        test_harness.test_check_log_replay(logfile, m_automate_check_test);
    }

    std::cout << std::endl;
    logfile << std::endl;
    test_harness.PrintTestStats(std::wcout);
    test_harness.PrintTestStats(logfile);
    test_harness.EndTestSuite();

    MESSAGE(std::endl << "========== PHASE 4: Cleaning up ==========" << std::endl);

    /*****************************************************************************
     * PHASE 4:
     * We have finished. Clean up the test harness. Tell the user we have finished
     * testing if the -b flag was given and we are running in background mode.
     ****************************************************************************/
    logfile.close();
    test_harness.remove_cow_brd();
    //<YUAN> cleanup_harness()在Tester的析构函数中处理
//    test_harness.cleanup_harness();
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
    //delete background_com;
    return 0;
}
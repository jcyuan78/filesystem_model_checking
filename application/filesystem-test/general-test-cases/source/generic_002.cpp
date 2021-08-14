///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
#include "pch.h"
#include <crashmonkey_comm.h>
#include <dokanfs-lib.h>
/*
Reproducing xfstest generic/002

1. Create file foo
2. Create 10 links for file foo in the same dir
    (link_0 to link_9) (sync and checkpoint after each create)
3. Remove the 10 links created (sync and checkpoint after each remove)

After a crash at random point, the file should have the correct number of links
corresponding to the checkpoint number.

https://github.com/kdave/xfstests/blob/master/tests/generic/002
*/

//#include "actions.h"
#define TEST_FILE_FOO       L"foo"
#define TEST_FILE_FOO_LINK  L"foo_link_"
#define TEST_DIR_A          L"test_dir_a"
#define NUM_LINKS           5


using fs_testing::tests::DataTestResult;
//using fs_testing::user_tools::api::WriteData;
//using fs_testing::user_tools::api::WriteDataMmap;
using fs_testing::user_tools::api::Checkpoint;
using std::string;

#define TEST_FILE_PERMS  ((mode_t) (S_IRWXU | S_IRWXG | S_IRWXO))

LOCAL_LOGGER_ENABLE(L"crashmonke.testcase", LOGGER_LEVEL_DEBUGINFO);


namespace fs_testing {
namespace tests {


class Generic002: public BaseTestCase 
{
public:
    virtual int setup() override 
    {
        JCASSERT(m_fs);
        init_paths();

        // Create test directory A.
        std::wstring dir_path = L"\\" TEST_DIR_A;
        jcvos::auto_interface<IFileInfo> dir;
        bool br = m_fs->DokanCreateFile(dir, dir_path, GENERIC_ALL, 0, IFileSystem::FS_CREATE_NEW, 0, 0, true);
        if (!br) {  return -1;  }
        dir->CloseFile();
        dir.release();

        //Create file foo in TEST_DIR_A 
        jcvos::auto_interface<IFileInfo> fd_foo;
        br = m_fs->DokanCreateFile(fd_foo, foo_path, GENERIC_ALL, 0, IFileSystem::FS_CREATE_NEW, 0, 0, false);
        if (!br || !fd_foo) { return -1; }

        m_fs->Sync();
        fd_foo->CloseFile();
        return 0;
    }

//#define USING_CM

#ifdef USING_CM
    virtual int run(int checkpoint) override 
    {
        JCASSERT(m_fs);
	    init_paths();

        int local_checkpoint = 0;
        wchar_t msg[100];

        jcvos::auto_interface<IFileInfo> fd_foo;
 //       bool br = m_fs->DokanCreateFile(fd_foo, foo_path, GENERIC_ALL, 0, IFileSystem::FS_OPEN_EXISTING, 0, 0, false);
        bool br = cm_->CmCreateFile(fd_foo, foo_path, GENERIC_ALL, 0, IFileSystem::FS_OPEN_EXISTING, 0, 0, false);
        if (!br || !fd_foo) return -1;
        //const int fd_foo = open(foo_path.c_str(), O_RDWR | O_CREAT, TEST_FILE_PERMS);
        //if (fd_foo < 0) {  return -1; }

        // Create new set of links
        for (int i = 0; i < NUM_LINKS; i++)
        {
            std::wstring foo_link = foo_link_path + std::to_wstring(i);
            jcvos::auto_interface<IFileInfo> ff;
//            br = m_fs->DokanCreateFile(ff, foo_link, GENERIC_ALL, 0, IFileSystem::FS_CREATE_NEW, 0, 0, false);
            br = cm_->CmCreateFile(ff, foo_link, GENERIC_ALL, 0, IFileSystem::FS_CREATE_NEW, 0, 0, false);
            if (!br || !ff) return -2;
            // fsync foo
//            br = fd_foo->FlushFile();
            br = cm_->CmFsync(fd_foo);
            if (!br)  {     return -3;      }

            // Make a user checkpoint here
            LOG_NOTICE(L"send checkpoint");
            swprintf_s(msg, L"create file:%d", i);
//            if (Checkpoint(msg) < 0){  return -4;    }
            if (cm_->CmCheckpoint(msg) < 0) { return -4; }
            local_checkpoint += 1;
            if (local_checkpoint == checkpoint) {   return 0;    }
//            ff->CloseFile();
            cm_->CmClose(ff);
        }
//        m_fs->Sync();

        // Remove the set of added links
        for (int i = 0; i < NUM_LINKS; i++)
        {
            std::wstring foo_link = foo_link_path + std::to_wstring(i);
//            br = m_fs->DokanDeleteFile(foo_link, NULL, false);
            br = cm_->CmRemove(foo_link);
            if (!br) return -5;
            //if(remove(foo_link.c_str()) < 0) { return -5;   }

            // fsync foo
//            br = fd_foo->FlushFile();
            br = cm_->CmFsync(fd_foo);
            if (!br){      return -6;     }

            // Make a user checkpoint here
            swprintf_s(msg, L"delete file:%d", i);
//            if (Checkpoint(msg) < 0){   return -7;    }
            if (cm_->CmCheckpoint(msg) < 0) { return -7; }
            local_checkpoint += 1;
            if (local_checkpoint == checkpoint) 
            {
                if (i == (NUM_LINKS - 1)) { return 1;  }
                return 0;
            }
        }

        //Close open files  
        //fd_foo->CloseFile();
        cm_->CmClose(fd_foo);
        return 0;
    }
#else
    virtual int run(int checkpoint) override
    {
        JCASSERT(m_fs);
        init_paths();

        int local_checkpoint = 0;
        wchar_t msg[100];

        jcvos::auto_interface<IFileInfo> fd_foo;
        bool br = m_fs->DokanCreateFile(fd_foo, foo_path, GENERIC_ALL, 0, IFileSystem::FS_OPEN_EXISTING, 0, 0, false);
        if (!br || !fd_foo) return -1;

        // Create new set of links
        for (int i = 0; i < NUM_LINKS; i++)
        {
            std::wstring foo_link = foo_link_path + std::to_wstring(i);
            jcvos::auto_interface<IFileInfo> ff;
            br = m_fs->DokanCreateFile(ff, foo_link, GENERIC_ALL, 0, IFileSystem::FS_CREATE_NEW, 0, 0, false);
            if (!br || !ff) return -2;
            // fsync foo
            br = fd_foo->FlushFile();
            if (!br) { return -3; }

            // Make a user checkpoint here
            LOG_NOTICE(L"send checkpoint");
            swprintf_s(msg, L"create file:%d", i);
            if (Checkpoint(msg) < 0){  return -4;    }
            local_checkpoint += 1;
            if (local_checkpoint == checkpoint) { return 0; }
            ff->CloseFile();
        }
        //        m_fs->Sync();

        // Remove the set of added links
        for (int i = 0; i < NUM_LINKS; i++)
        {
            std::wstring foo_link = foo_link_path + std::to_wstring(i);
            br = m_fs->DokanDeleteFile(foo_link, NULL, false);
            if (!br) return -5;

            // fsync foo
            br = fd_foo->FlushFile();
            if (!br) { return -6; }

            // Make a user checkpoint here
            swprintf_s(msg, L"delete file:%d", i);
            if (Checkpoint(msg) < 0){   return -7;    }
            local_checkpoint += 1;
            if (local_checkpoint == checkpoint)
            {
                if (i == (NUM_LINKS - 1)) { return 1; }
                return 0;
            }
        }

        //Close open files  
        fd_foo->CloseFile();
        return 0;
    }
#endif


    virtual int check_test(unsigned int last_checkpoint, DataTestResult *test_result) override 
    {
        init_paths();

        LOG_STACK_TRACE();
        char mode[] = "0777";
        int i_mode = strtol(mode,0,8);
//        chmod(foo_path.c_str(), i_mode);

        // Since crash could have happened between a fsync and Checkpoint, last_checkpoint only tells the minimum
        //  number of operations that have happened. In reality, one more fsync could have happened, which might
        //  alter the link count, delta is used to keep track of that. For example, during linking phase, if crash
        //  occured between fsync and checkpoint, number of links will be 1 more than what checkpoint says while
        //  during remove phase, the same will be 1 less than what the checkpoint says.
        int delta = 0;
            
        if (last_checkpoint == 2 * NUM_LINKS)   delta = 0;  // crash after all fsyncs completed
        else if (last_checkpoint < NUM_LINKS)   delta = 1; // crash during link phase
        else                                    delta = -1; // crash during remove phase

        int num_expected_links;
        if (last_checkpoint <= NUM_LINKS)   num_expected_links = last_checkpoint;
        else                                num_expected_links = 2 * NUM_LINKS - last_checkpoint;

        //<YUAN> 源设计中，此处通过检查链接的数量，判断是否创建或者删除冷正确数量的链接。
        //  此处通过枚举子文件并且计算文件数量，用于替代计算链接数量。
        std::wstring dir_path = L"\\" TEST_DIR_A;
        jcvos::auto_interface<IFileInfo> fd_foo;
        bool br = m_fs->DokanCreateFile(fd_foo, dir_path, GENERIC_ALL, 0, IFileSystem::FS_OPEN_EXISTING, 0, 0, true);
        if (!br || !fd_foo) return -1;
        int actual_count = check_item_number(fd_foo);
        fd_foo->CloseFile();

        LOG_NOTICE(L"check file system: link num = %d, checkpoint=%d, expected links=%d, actual count=%d",
            NUM_LINKS, last_checkpoint, num_expected_links, actual_count);

 /*       struct stat st;
        stat(foo_path.c_str(), &st);
        int actual_count = st.st_nlink;*/
        actual_count--; // subtracting one for the file itself

        // If actual count doesn't match with expected number of links
        if (actual_count != num_expected_links && actual_count != num_expected_links + delta) 
        {
            test_result->SetError(DataTestResult::kFileMetadataCorrupted);
            test_result->error_description = L" : Number of links not matching with expected count";
        }
        return 0;
    }

private:
    std::wstring foo_path;
    std::wstring foo_link_path;
    
    void init_paths() 
    {
        //foo_path = mnt_dir_ + "/" TEST_DIR_A "/" TEST_FILE_FOO;
        //foo_link_path = mnt_dir_ + "/" TEST_DIR_A "/" TEST_FILE_FOO_LINK;
        foo_path = L"\\" TEST_DIR_A L"\\" TEST_FILE_FOO;
        foo_link_path = L"\\" TEST_DIR_A L"\\" TEST_FILE_FOO_LINK;
    }

    int check_item_number(IFileInfo* dir)
    {
        class Listener : public EnumFileListener
        {
        public:
            virtual bool EnumFileCallback(const std::wstring& fn,
                UINT32 ino, UINT32 entry, // entry 在父目录中的位置
                BY_HANDLE_FILE_INFORMATION*)
            {
                LOG_DEBUG(L"got child item: %s", fn.c_str());
                m_file_num++;
                return true;
            }
        public:
            int m_file_num = 0;
        } listener;
        listener.m_file_num = 0;

        dir->EnumerateFiles(static_cast<EnumFileListener*>(&listener));
        return listener.m_file_num;
    }


};

class CGeneralTestFactory : public fs_testing::tests::ITestCaseFactory
{
    virtual void CreateObject(BaseTestCase*& obj)
    {
        Generic002* tt = new Generic002; JCASSERT(tt);
        obj = static_cast<fs_testing::tests::BaseTestCase*>(tt);
    }

    virtual void DeleteObject(BaseTestCase* obj)
    {
        delete obj;
    }
};


}  // namespace tests
}  // namespace fs_testing

extern "C" __declspec(dllexport) bool GetTesterFactory(fs_testing::utils::ITesterFactory * &fac, const wchar_t * name)
{
    if (wcscmp(name, L"General02") == 0)
    {
        fac = static_cast<fs_testing::utils::ITesterFactory*>(
            jcvos::CDynamicInstance<fs_testing::tests::CGeneralTestFactory>::Create());
        return true;
    }
    return false;
}


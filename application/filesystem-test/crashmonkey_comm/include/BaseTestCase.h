#ifndef BASE_TEST_CASE_H
#define BASE_TEST_CASE_H

#include <string>
//#include <dokanfs-lib.h>
#include <ifilesystem.h>

#include "DataTestResult.h"
#include "itester_factory.h"
#include "wrapper.h"

namespace fs_testing 
{
    namespace tests 
    {
        class BaseTestCase;

        class ITestCaseFactory : public utils::ITesterFactory
        {
        public:
            virtual void CreateObject(BaseTestCase*& obj) = 0;
            virtual void DeleteObject(BaseTestCase* obj) = 0;
        };


        class BaseTestCase 
        {
        public:
            typedef ITestCaseFactory Factory;
        public:
            BaseTestCase(void) : m_fs(NULL) {}
            virtual ~BaseTestCase() { RELEASE(m_fs); }
            virtual int setup() = 0;
            virtual int run(const int checkpoint) = 0;
            virtual int check_test(unsigned int last_checkpoint, DataTestResult *test_result) = 0;
            virtual int init_values(std::wstring mount_dir, size_t filesys_size, IFileSystem * fs);

            int Run(FILE * change_fd, const int checkpoint);

        protected:
//            std::wstring mnt_dir_;
            IFileSystem* m_fs;
            size_t filesys_size_;
            fs_testing::user_tools::api::CmFsOps *cm_;
        };

        typedef BaseTestCase *test_create_t();
        typedef void test_destroy_t(BaseTestCase *instance);

    }  // namespace tests
}  // namespace fs_testing

#endif

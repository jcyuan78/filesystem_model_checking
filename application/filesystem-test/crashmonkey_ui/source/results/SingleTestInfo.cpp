#include "../pch.h"

#include <crashmonkey_comm.h>
//#include "DataTestResult.h"
#include "FileSystemTestResult.h"
#include "SingleTestInfo.h"

namespace fs_testing {

using std::endl;
using std::ostream;
using std::string;
using std::vector;

using fs_testing::tests::DataTestResult;
using fs_testing::FileSystemTestResult;

SingleTestInfo::SingleTestInfo() 
{
    fs_test.ResetError();
    data_test.ResetError();
}

SingleTestInfo::ResultType SingleTestInfo::GetTestResult() const 
{
    if ((fs_test.GetError() == FileSystemTestResult::kClean || fs_test.GetError() == FileSystemTestResult::kCheckNotRun)
        && data_test.GetError() == DataTestResult::kClean) 
    {
        return SingleTestInfo::kPassed;
    } else if (fs_test.GetError() == FileSystemTestResult::kFixed && data_test.GetError() == DataTestResult::kClean) 
    {
        return SingleTestInfo::kFsckFixed;
    } else if (fs_test.GetError() == FileSystemTestResult::kKernelMount) 
    {
        return SingleTestInfo::kFsckRequired;
    }
    return SingleTestInfo::kFailed;
}

void SingleTestInfo::PrintResults(std::wostream& os) const 
{
    os << L"Test #" << test_num << L": " << GetTestResult() << L": ";
    data_test.PrintErrors(os);
    if (GetTestResult() == SingleTestInfo::kFailed &&
        fs_test.GetError() == FileSystemTestResult::kCheck) 
    {
        os << fs_test.error_description << endl;
    } 
    else {   os << data_test.error_description << endl; }
    os << L"\tcrash state (";
    permute_data.PrintCrashStateSize(os);
    os << L"): ";
    permute_data.PrintCrashState(os) << endl;
    os << L"\tlast checkpoint: " << permute_data.last_checkpoint << endl;
    os << L"\tfsck result: ";
    fs_test.PrintErrors(os);
    os << endl;
    os << L"\tfsck output:" << endl << fs_test.fsck_result << endl << endl;
}

std::wostream& operator<<(std::wostream& os, SingleTestInfo::ResultType rt) {
  switch (rt) 
  {
  case SingleTestInfo::kPassed:
      os << L"PASSED";
      break;
    case SingleTestInfo::kFsckFixed:
      os << L"FSCK_FIXED";
      break;
    case SingleTestInfo::kFsckRequired:
      os << L"FSCK_REQUIRED";
      break;
    case SingleTestInfo::kFailed:
      os << L"FAILED";
      break;
    default:
      os.setstate(std::ios_base::failbit);
  }
  return os;
}

}  // namespace fs_testing

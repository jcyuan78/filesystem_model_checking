#include "pch.h"
#include "../include/DataTestResult.h"

using fs_testing::tests::DataTestResult;

//using std::ostream;

DataTestResult::DataTestResult() {
  error_summary_ = DataTestResult::kClean;
}

void DataTestResult::ResetError() {
  error_summary_ = DataTestResult::kClean;
}

void DataTestResult::SetError(ErrorType err) {
  error_summary_ = err;
}

DataTestResult::ErrorType DataTestResult::GetError() const {
  return error_summary_;
}

std::wostream& DataTestResult::PrintErrors(std::wostream& os) const 
{
    unsigned int noted_errors = error_summary_;
    unsigned int shift = 0;
    while (noted_errors != 0) 
    {
        if (noted_errors & 1) { os << (DataTestResult::ErrorType) (1 << shift);  }
        ++shift;
        noted_errors >>= 1;
    }
    return os;
}
//namespace fs_testing {
//namespace tests {


std::wostream& fs_testing::tests::operator<<(std::wostream& os, DataTestResult::ErrorType err) 
{
    switch (err) 
    {
    case DataTestResult::kClean:      break;
    case DataTestResult::kOldFilePersisted:
      os << L"old_file_persisted";
      break;
    case DataTestResult::kFileMissing:
      os << L"file_missing";
      break;
    case DataTestResult::kFileDataCorrupted:
      os << L"file_data_corrupted";
      break;
    case DataTestResult::kFileMetadataCorrupted:
      os << L"file_metadata_corrupted";
      break;
    case DataTestResult::kIncorrectBlockCount:
      os << L"incorrect_block_count";
      break;      
    case DataTestResult::kOther:
      os << L"other_error";
      break;
    case DataTestResult::kAutoCheckFailed:
      os << L"auto_check_test_failed";
      break;
    default:
      os.setstate(std::ios_base::failbit);
    }
    return os;
}

//}  // namespace tests
//}  // namespace fs_testing

#include "FileSystemTestResult.h"

//namespace fs_testing 
//{
using fs_testing::FileSystemTestResult;

//using std::ostream;

FileSystemTestResult::FileSystemTestResult()
{
	error_summary_ = FileSystemTestResult::kCheckNotRun;
}

void FileSystemTestResult::ResetError()
{
	error_summary_ = FileSystemTestResult::kCheckNotRun;
}

void FileSystemTestResult::SetError(ErrorType err)
{
	error_summary_ = error_summary_ | err;
}

unsigned int FileSystemTestResult::GetError() const
{
	return error_summary_;
}

void FileSystemTestResult::PrintErrors(std::wostream& os) const
{
	if (error_summary_ == 0)
	{
		os << FileSystemTestResult::kCheckNotRun;
		return;
	}

	unsigned int noted_errors = error_summary_;
	unsigned int shift = 0;
	while (noted_errors != 0)
	{
		if (noted_errors & 1)
		{
			os << (FileSystemTestResult::ErrorType)(1 << shift) << " ";
		}
		++shift;
		noted_errors >>= 1;
	}
}

std::wostream& fs_testing::operator<<(std::wostream& os, FileSystemTestResult::ErrorType err)
{
	switch (err)
	{
	case fs_testing::FileSystemTestResult::kCheckNotRun:        os << L"fsck_not_run";       break;
	case fs_testing::FileSystemTestResult::kClean:              os << L"fsck_no_errors";     break;
	case fs_testing::FileSystemTestResult::kUnmountable:        os << L"unmountable_file_system";      break;
	case fs_testing::FileSystemTestResult::kCheck:              os << L"fsck_error";      break;
	case fs_testing::FileSystemTestResult::kFixed:              os << L"file_system_fixed";      break;
	case fs_testing::FileSystemTestResult::kSnapshotRestore:    os << L"restoring_snapshot";      break;
	case fs_testing::FileSystemTestResult::kBioWrite:           os << L"writing_crash_state";     break;
	case fs_testing::FileSystemTestResult::kOther:              os << L"other_error";      break;
	case fs_testing::FileSystemTestResult::kKernelMount:        os << L"kernel_mount";      break;
	case fs_testing::FileSystemTestResult::kCheckUnfixed:       os << L"unfixed_fsck_errors";      break;
	default:      os.setstate(std::ios_base::failbit);
	}
	return os;
}
//}  // namespace fs_testing

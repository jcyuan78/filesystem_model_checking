#include "../pch.h"
#include "PermuteTestResult.h"

namespace fs_testing {

using std::ostream;
using std::to_string;

std::wostream& PermuteTestResult::PrintCrashStateSize(std::wostream& os) const 
{
    if (crash_state.empty()) {    os << L"0 bios/sectors";  } 
    else {   os << std::to_wstring(crash_state.size()) << L" bios/sectors";
  }
  return os;
}

std::wostream& PermuteTestResult::PrintCrashState(std::wostream& os) const 
{
    if (crash_state.empty()) {   return os;  }

    for (unsigned int i = 0; i < crash_state.size() - 1; ++i) 
    {
        os << L"(" << std::to_wstring(crash_state.at(i).bio_index);
        if (!crash_state.at(i).full_bio) 
        {
            os << L", " << std::to_wstring(crash_state.at(i).bio_sector_index);
        }
        os << L"), ";
    }

    os << L"(" << std::to_wstring(crash_state.back().bio_index);
    if (!crash_state.back().full_bio) 
    {
        os << L", " << std::to_wstring(crash_state.back().bio_sector_index);
    }
    os << L")";

    return os;
}

}  // namespace fs_testing

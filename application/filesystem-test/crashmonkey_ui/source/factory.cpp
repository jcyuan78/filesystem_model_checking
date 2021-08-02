#include "pch.h"
#include "utils/ClassLoader.h"
#include "permuter/RandomPermuter.h"

bool fs_testing::utils::GetTesterFactory(fs_testing::utils::ITesterFactory*& fac, const wchar_t* name)
{
	if (wcscmp(name, L"RandomPermuter") == 0)
	{
		fac = static_cast<fs_testing::utils::ITesterFactory*>(
			jcvos::CDynamicInstance<fs_testing::permuter::CRandomPermuterFactory>::Create() );
		return true;
	}
	return false;
}

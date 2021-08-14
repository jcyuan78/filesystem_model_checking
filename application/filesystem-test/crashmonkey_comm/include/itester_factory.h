#pragma once

namespace fs_testing
{
	namespace utils
	{
		class ITesterFactory : public IJCInterface
		{

		};

		typedef bool(*GET_FACTORY)(ITesterFactory*& f, const wchar_t* name);
	}
}

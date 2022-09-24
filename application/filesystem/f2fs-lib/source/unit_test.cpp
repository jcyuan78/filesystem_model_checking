///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// 单元测试：对部分函数，模块做单元测试。仅调试时使用。
#include "pch.h"

#include "unit_test.h"
#include <linux-fs-wrapper.h>

LOCAL_LOGGER_ENABLE(L"f2fs.unit_test", LOGGER_LEVEL_DEBUGINFO);


int run_unit_test(void)
{

	// 检查BIT_TO_xxx
	int bytes;
	bytes = BITS_TO_LONGS(35);
	bytes = BITS_TO_BYTES(35);

	return 0;
}
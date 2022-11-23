///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// 单元测试：对部分函数，模块做单元测试。仅调试时使用。
#include "pch.h"

#include "unit_test.h"
#include <linux-fs-wrapper.h>

LOCAL_LOGGER_ENABLE(L"f2fs.unit_test", LOGGER_LEVEL_DEBUGINFO);

inline unsigned long find_next_bit_(const unsigned long* addr, unsigned long size, unsigned long offset)
{
	unsigned long tmp, mask;

	if (unlikely(offset >= size))	return size;

	tmp = addr[offset / BITS_PER_LONG];

	/* Handle 1st word. */
	mask = BITMAP_FIRST_WORD_MASK(offset);
	//if (le)		mask = swab(mask);

	tmp &= mask;

	offset = round_down<unsigned long>(offset, BITS_PER_LONG);

	while (!tmp)
	{
		offset += BITS_PER_LONG;
		if (offset >= size)
			return size;

		tmp = addr[offset / BITS_PER_LONG];
	}

	//if (le)		tmp = swab(tmp);

	return min(offset + __ffs(tmp), size);
}

static const unsigned long BITMAP_1[] = {0xFFFF0100, 0x00008000, };


int run_unit_test(void)
{
	// 检查BIT_TO_xxx
	int bytes;
	bytes = BITS_TO_LONGS(35);
	bytes = BITS_TO_BYTES(35);

	// 检查 find_next_bit函数
	UINT pos;
	pos = __ffs(0x80000000);
	pos = __ffs(0x00000001);

	pos = find_next_bit_((const unsigned long*)BITMAP_1, 64, 0);		JCASSERT(pos == 8);
	pos = find_next_bit_((const unsigned long*)BITMAP_1, 64, 33);		JCASSERT(pos == 47);

	long a = 2;
	long b = atomic_dec_and_test(&a);
	b = atomic_dec_and_test(&a);


	return 0;
}
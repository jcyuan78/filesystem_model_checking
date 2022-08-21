///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
#pragma once
/* SPDX-License-Identifier: GPL-2.0 */

#include "linux_comm.h"

//#include <asm/types.h>
//#include <linux/bits.h>
//
//#include <uapi/linux/kernel.h>

/* Set bits in the first 'n' bytes when loaded from memory */
#ifdef __LITTLE_ENDIAN
#  define aligned_byte_mask(n) ((1UL << 8*(n))-1)
#else
#  define aligned_byte_mask(n) (~0xffUL << (BITS_PER_LONG - 8 - 8*(n)))
#endif



//#define BITS_PER_TYPE(type)	(sizeof(type) * BITS_PER_BYTE)
//#define BITS_TO_LONGS(nr)	round_up(nr, BITS_PER_LONG)
//inline const int BITS_TO_LONGS(nr) { return round_up(nr, BITS_PER_TYPE(long)); }





template <typename T1>
int BITS_TO_TYPE(int nr) { return round_up(nr, BITS_PER_T<T1>()); }

extern unsigned int __sw_hweight8(unsigned int w);
extern unsigned int __sw_hweight16(unsigned int w);
extern unsigned int __sw_hweight32(unsigned int w);
extern unsigned long __sw_hweight64(__u64 w);


/** __ffs - find first bit in word.
 * @word: The word to search
 * Undefined if no bit exists, so code should check against 0 first. */
static inline unsigned long __ffs(unsigned long word)
{
	int num = 0;

#if BITS_PER_LONG == 64
	if ((word & 0xffffffff) == 0)
	{
		num += 32;
		word >>= 32;
	}
#endif
	if ((word & 0xffff) == 0)	{		num += 16;		word >>= 16;	}
	if ((word & 0xff) == 0)		{		num += 8;		word >>= 8;		}
	if ((word & 0xf) == 0)		{		num += 4;		word >>= 4;		}
	if ((word & 0x3) == 0)		{		num += 2;		word >>= 2;		}
	if ((word & 0x1) == 0)		num += 1;
	return num;
}

/* Find the first set bit in a memory region. */
inline unsigned long _find_first_bit(const unsigned long* addr, unsigned long size)
{
	unsigned long idx;
	for (idx = 0; idx * BITS_PER_LONG < size; idx++)
	{
		if (addr[idx])		return min(idx * BITS_PER_LONG + __ffs(addr[idx]), size);
	}
	return size;
}

/** find_first_bit - find the first set bit in a memory region
 * @addr: The address to start the search at
 * @size: The maximum number of bits to search
 *
 * Returns the bit number of the first set bit. If no bits are set, returns @size. */
inline unsigned long find_first_bit(const unsigned long* addr, unsigned long size)
{
	//if (small_const_nbits(size))
	//{
	//	unsigned long val = *addr & GENMASK(size - 1, 0);
	//	return val ? __ffs(val) : size;
	//}

	return _find_first_bit(addr, size);
}
#define BITMAP_FIRST_WORD_MASK(start) (~0UL << ((start) & (BITS_PER_LONG - 1)))

static inline bool bitmap_empty(const unsigned long* src, unsigned nbits)
{
//	if (small_const_nbits(nbits)) eturn !(*src & BITMAP_LAST_WORD_MASK(nbits));
	return find_first_bit(src, nbits) == nbits;
}

#define BIT_ULL(nr)		(ULL(1) << (nr))
#define BIT_MASK(nr)		(UL(1) << ((nr) % BITS_PER_LONG))
#define BIT_WORD(nr)		((nr) / BITS_PER_LONG)
#define BIT_ULL_MASK(nr)	(ULL(1) << ((nr) % BITS_PER_LONG_LONG))
#define BIT_ULL_WORD(nr)	((nr) / BITS_PER_LONG_LONG)
//#define BITS_PER_BYTE		8

#define BITMAP_FIRST_WORD_MASK(start) (~0UL << ((start) & (BITS_PER_LONG - 1)))
#define BITMAP_LAST_WORD_MASK(nbits) (~0UL >> ((0-nbits) & (BITS_PER_LONG - 1)))

inline void bitmap_clear(unsigned long* map, unsigned int start, int len)
{
	unsigned long* p = map + BIT_WORD(start);
	const unsigned int size = start + len;
	int bits_to_clear = BITS_PER_LONG - (start % BITS_PER_LONG);
	unsigned long mask_to_clear = BITMAP_FIRST_WORD_MASK(start);

	while (len - bits_to_clear >= 0) {
		*p &= ~mask_to_clear;
		len -= bits_to_clear;
		bits_to_clear = BITS_PER_LONG;
		mask_to_clear = ~0UL;
		p++;
	}
	if (len) {
		mask_to_clear &= BITMAP_LAST_WORD_MASK(size);
		*p &= ~mask_to_clear;
	}
}

/* This is a common helper function for find_next_bit, find_next_zero_bit, and find_next_and_bit. The differences are:
 *  - The "invert" argument, which is XORed with each fetched word before searching it for one bits.
 *  - The optional "addr2", which is anded with "addr1" if present. */
inline unsigned long _find_next_bit(const unsigned long* addr1, const unsigned long* addr2, unsigned long nbits,
	unsigned long start, unsigned long invert, unsigned long le)
{
	unsigned long tmp, mask;

	if (unlikely(start >= nbits))	return nbits;

	tmp = addr1[start / BITS_PER_LONG];
	if (addr2)		tmp &= addr2[start / BITS_PER_LONG];
	tmp ^= invert;

	/* Handle 1st word. */
	mask = BITMAP_FIRST_WORD_MASK(start);
	//if (le)		mask = swab(mask);

	tmp &= mask;

	start = round_down<unsigned long>(start, BITS_PER_LONG);

	while (!tmp)
	{
		start += BITS_PER_LONG;
		if (start >= nbits)
			return nbits;

		tmp = addr1[start / BITS_PER_LONG];
		if (addr2)		tmp &= addr2[start / BITS_PER_LONG];
		tmp ^= invert;
	}

	//if (le)		tmp = swab(tmp);

	return min(start + __ffs(tmp), nbits);
}

/* find_next_bit - find the next set bit in a memory region
 * @addr: The address to base the search on
 * @offset: The bitnumber to start searching at
 * @size: The bitmap size in bits
 *
 * Returns the bit number for the next set bit If no bits are set, returns @size. */
inline unsigned long find_next_bit(const unsigned long* addr, unsigned long size, unsigned long offset)
{
	//if (small_const_nbits(size))
	//{
	//	unsigned long val;

	//	if (unlikely(offset >= size))
	//		return size;

	//	val = *addr & GENMASK(size - 1, offset);
	//	return val ? __ffs(val) : size;
	//}

	return _find_next_bit(addr, NULL, size, offset, 0UL, 0);
}

/* find_next_zero_bit - find the next cleared bit in a memory region
 * @addr: The address to base the search on
 * @offset: The bitnumber to start searching at
 * @size: The bitmap size in bits
 *
 * Returns the bit number of the next zero bit If no bits are zero, returns @size. */
inline unsigned long find_next_zero_bit(const unsigned long* addr, unsigned long size,	unsigned long offset)
{
	//if (small_const_nbits(size))
	//{
	//	unsigned long val;

	//	if (unlikely(offset >= size))
	//		return size;

	//	val = *addr | ~GENMASK(size - 1, offset);
	//	return val == ~0UL ? size : ffz(val);
	//}

	return _find_next_bit(addr, NULL, size, offset, ~0UL, 0);
}

/* Include this here because some architectures need generic_ffs/fls in scope */
//#include <asm/bitops.h>

#define for_each_set_bit(bit, addr, size) \
	for ((bit) = find_first_bit((addr), (size));    (bit) < (size);	 (bit) = find_next_bit((addr), (size), (bit) + 1))

/* same as for_each_set_bit() but use bit as value to start with */
#define for_each_set_bit_from(bit, addr, size) \
	for ((bit) = find_next_bit((addr), (size), (bit));	\
	     (bit) < (size);					\
	     (bit) = find_next_bit((addr), (size), (bit) + 1))

#define for_each_clear_bit(bit, addr, size) \
	for ((bit) = find_first_zero_bit((addr), (size));	\
	     (bit) < (size);					\
	     (bit) = find_next_zero_bit((addr), (size), (bit) + 1))

/* same as for_each_clear_bit() but use bit as value to start with */
#define for_each_clear_bit_from(bit, addr, size) \
	for ((bit) = find_next_zero_bit((addr), (size), (bit));	\
	     (bit) < (size);					\
	     (bit) = find_next_zero_bit((addr), (size), (bit) + 1))

/**
 * for_each_set_clump8 - iterate over bitmap for each 8-bit clump with set bits
 * @start: bit offset to start search and to store the current iteration offset
 * @clump: location to store copy of current 8-bit clump
 * @bits: bitmap address to base the search on
 * @size: bitmap size in number of bits
 */
#define for_each_set_clump8(start, clump, bits, size) \
	for ((start) = find_first_clump8(&(clump), (bits), (size)); \
	     (start) < (size); \
	     (start) = find_next_clump8(&(clump), (bits), (size), (start) + 8))
#if 0

static inline int get_bitmask_order(unsigned int count)
{
	int order;

	order = fls(count);
	return order;	/* We could be slightly more clever with -1 here... */
}

static inline unsigned long hweight_long(unsigned long w)
{
	return sizeof(w) == 4 ? hweight32(w) : hweight64((__u64)w);
}
#endif

/**
 * rol64 - rotate a 64-bit value left
 * @word: value to rotate
 * @shift: bits to roll
 */
static inline __u64 rol64(__u64 word, int shift)
{
	return (word << (shift & 63)) | (word >> ((-shift) & 63));
}

/**
 * ror64 - rotate a 64-bit value right
 * @word: value to rotate
 * @shift: bits to roll
 */
static inline __u64 ror64(__u64 word, int shift)
{
	return (word >> (shift & 63)) | (word << ((-shift) & 63));
}

/**
 * rol32 - rotate a 32-bit value left
 * @word: value to rotate
 * @shift: bits to roll
 */
static inline __u32 rol32(__u32 word, int shift)
{
	return (word << (shift & 31)) | (word >> ((-shift) & 31));
}

/**
 * ror32 - rotate a 32-bit value right
 * @word: value to rotate
 * @shift: bits to roll
 */
static inline __u32 ror32(__u32 word,  int shift)
{
	return (word >> (shift & 31)) | (word << ((-shift) & 31));
}

/**
 * rol16 - rotate a 16-bit value left
 * @word: value to rotate
 * @shift: bits to roll
 */
static inline __u16 rol16(__u16 word, int shift)
{
	return (word << (shift & 15)) | (word >> ((-shift) & 15));
}

/**
 * ror16 - rotate a 16-bit value right
 * @word: value to rotate
 * @shift: bits to roll
 */
static inline __u16 ror16(__u16 word, int shift)
{
	return (word >> (shift & 15)) | (word << ((-shift) & 15));
}

/**
 * rol8 - rotate an 8-bit value left
 * @word: value to rotate
 * @shift: bits to roll
 */
static inline __u8 rol8(__u8 word, int shift)
{
	return (word << (shift & 7)) | (word >> ((-shift) & 7));
}

/**
 * ror8 - rotate an 8-bit value right
 * @word: value to rotate
 * @shift: bits to roll
 */
static inline __u8 ror8(__u8 word, int shift)
{
	return (word >> (shift & 7)) | (word << ((-shift) & 7));
}

/**
 * sign_extend32 - sign extend a 32-bit value using specified bit as sign-bit
 * @value: value to sign extend
 * @index: 0 based bit index (0<=index<32) to sign bit
 *
 * This is safe to use for 16- and 8-bit types as well.
 */
static __s32 sign_extend32(__u32 value, int index)
{
	__u8 shift = 31 - index;
	return (__s32)(value << shift) >> shift;
}

/**
 * sign_extend64 - sign extend a 64-bit value using specified bit as sign-bit
 * @value: value to sign extend
 * @index: 0 based bit index (0<=index<64) to sign bit
 */
static __s64 sign_extend64(__u64 value, int index)
{
	__u8 shift = 63 - index;
	return (__s64)(value << shift) >> shift;
}

#if 0
static inline unsigned fls_long(unsigned long l)
{
	if (sizeof(l) == 4)		return fls(l);
	return fls64(l);
}

static inline int get_count_order(unsigned int count)
{
	if (count == 0)		return -1;
	return fls(--count);
}

/**
 * get_count_order_long - get order after rounding @l up to power of 2
 * @l: parameter
 *
 * it is same as get_count_order() but with long type parameter
 */
static inline int get_count_order_long(unsigned long l)
{
	if (l == 0UL)		return -1;
	return (int)fls_long(--l);
}

/**
 * __ffs64 - find first set bit in a 64 bit word
 * @word: The 64 bit word
 *
 * On 64 bit arches this is a synonym for __ffs
 * The result is not defined if no bits are set, so check that @word
 * is non-zero before calling this.
 */
static inline unsigned long __ffs64(u64 word)
{
#if BITS_PER_LONG == 32
	if (((u32)word) == 0UL)
		return __ffs((u32)(word >> 32)) + 32;
#elif BITS_PER_LONG != 64
#error BITS_PER_LONG not 32 or 64
#endif
	return __ffs((unsigned long)word);
}

/**
 * assign_bit - Assign value to a bit in memory
 * @nr: the bit to set
 * @addr: the address to start counting from
 * @value: the value to assign
 */
static __always_inline void assign_bit(long nr, volatile unsigned long *addr,     bool value)
{
	if (value)		set_bit(nr, addr);
	else		clear_bit(nr, addr);
}

static __always_inline void __assign_bit(long nr, volatile unsigned long *addr,	 bool value)
{
	if (value)		__set_bit(nr, addr);
	else		__clear_bit(nr, addr);
}
#endif


#ifdef __KERNEL__

#ifndef set_mask_bits
#define set_mask_bits(ptr, mask, bits)	\
({								\
	const typeof(*(ptr)) mask__ = (mask), bits__ = (bits);	\
	typeof(*(ptr)) old__, new__;				\
								\
	do {							\
		old__ = READ_ONCE(*(ptr));			\
		new__ = (old__ & ~mask__) | bits__;		\
	} while (cmpxchg(ptr, old__, new__) != old__);		\
								\
	old__;							\
})
#endif

#ifndef bit_clear_unless
#define bit_clear_unless(ptr, clear, test)	\
({								\
	const typeof(*(ptr)) clear__ = (clear), test__ = (test);\
	typeof(*(ptr)) old__, new__;				\
								\
	do {							\
		old__ = READ_ONCE(*(ptr));			\
		new__ = old__ & ~clear__;			\
	} while (!(old__ & test__) &&				\
		 cmpxchg(ptr, old__, new__) != old__);		\
								\
	!(old__ & test__);					\
})
#endif

#endif /* __KERNEL__ */

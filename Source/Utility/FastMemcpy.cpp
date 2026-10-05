/*
Copyright (C) 2012 Corn

aligned case & byte copy (except ASM) by
Copyright (C) 2009 Raphael
E-mail:   raphael@fx-world.org
homepage: http://wordpress.fx-world.org
*/

#include "stdafx.h"
#include "FastMemcpy.h"

#include "Utility/DaedalusTypes.h"
#include "Utility/Endian.h"
#include "Utility/Timing.h"

#include <stdio.h>

#ifdef DAEDALUS_CTR
// ARM11/ARMv6K bulk-copy primitive. It is only called for aligned pointers
// and a byte count rounded down to a 32-byte multiple; the portable path below
// handles alignment and any remaining bytes.
extern "C" void memcpy_words_arm( void* dst, const void* src, size_t size );
#endif

//*****************************************************************************
//Copy native N64 memory with CPU only //Corn
//Little Endian
//*****************************************************************************
void memcpy_byteswap( void* dst, const void* src, size_t size )
{

	u8* src8 = (u8*)src;
	u8* dst8 = (u8*)dst;
	u32* src32;
	u32* dst32;

#ifdef DAEDALUS_CTR
	// ARM11 can transfer aligned words efficiently with LDM/STM. Keep the
	// assembly fast path to complete 32-byte blocks; the existing C++ code
	// remains the fallback for unaligned buffers and the final partial block.
	if( size >= 32 && (((uintptr_t)dst8 | (uintptr_t)src8) & 3) == 0 )
	{
		const size_t bulk_size = size & ~(size_t)31;
		memcpy_words_arm( dst8, src8, bulk_size );
		dst8 += bulk_size;
		src8 += bulk_size;
		size -= bulk_size;
	}
#endif

	// < 4 isn't worth trying any optimisations...
	if(size>=4)
	{
		// Align dst on 4 bytes or just resume if already done
		while (((((uintptr_t)dst8) & 0x3)!=0) )
		{
			*(u8*)((uintptr_t)dst8++ ^ U8_TWIDDLE) = *(u8*)((uintptr_t)src8++ ^ U8_TWIDDLE);
			size--;
		}
		// We are dst aligned now but need at least 4 bytes to copy
		if(size>=4)
		{
			src32 = (u32*)src8;
			dst32 = (u32*)dst8;
			u32 srcTmp;
			u32 dstTmp;
			u32 size32 = size >> 2;
			size &= 0x3;
			switch( (uintptr_t)src8&0x3 )
			{
				case 0:	//Both src and dst are aligned to 4 bytes
					{
#if 0 //DAEDALUS_W32
						// MSVC11 memcpy is almost 50% faster! It takes advantage of SSE2
						// Mmm breaks wipeout
						memcpy(dst, src, size);
#else
						//This is faster than PSP's GCC memcpy
						//ToDo: Profile for other plaforms to see if memcpy is faster
						while (size32&0x3)
						{
							*dst32++ = *src32++;
							size32--;
						}

						u32 size128 = size32 >> 2;
						while (size128--)
						{
							*dst32++ = *src32++;
							*dst32++ = *src32++;
							*dst32++ = *src32++;
							*dst32++ = *src32++;
						}

						src8 = (u8*)src32;
#endif
					}
					break;

				case 1:	//Handle offset by 1
					{
						src32 = (u32*)((u32)src8 & ~0x3);
						srcTmp = *src32++;
						while(size32--)
						{
							dstTmp = srcTmp << 8;
							srcTmp = *src32++;
							dstTmp |= srcTmp >> 24;
							*dst32++ = dstTmp;
						}
						src8 = (u8*)src32 - 3;
					}
					break;

				case 2:	//Handle offset by 2
					{
						src32 = (u32*)((u32)src8 & ~0x3);
						srcTmp = *src32++;
						while(size32--)
						{
							dstTmp = srcTmp << 16;
							srcTmp = *src32++;
							dstTmp |= srcTmp >> 16;
							*dst32++ = dstTmp;
						}
						src8 = (u8*)src32 - 2;
					}
					break;

				case 3:	//Handle offset by 3
					{
						src32 = (u32*)((u32)src8 & ~0x3);
						srcTmp = *src32++;
						while(size32--)
						{
							dstTmp = srcTmp << 24;
							srcTmp = *src32++;
							dstTmp |= srcTmp >> 8;
							*dst32++ = dstTmp;
						}
						src8 = (u8*)src32 - 1;
					}
					break;
			}
			dst8 = (u8*)dst32;
		}
	}

	// Copy the remaing byte by byte...
	while(size--)
	{
		*(u8*)((uintptr_t)dst8++ ^ U8_TWIDDLE) = *(u8*)((uintptr_t)src8++ ^ U8_TWIDDLE);
	}
}
#ifdef PROFILE_MEMCPY
static inline u64 GetCurrent()
{
    u64 tick;
	NTiming::GetPreciseTime(&tick);
    return tick;
}

#define MEMCPY_TEST(d, s, n) {													\
	u32 _fast_memcpy_swizzle = 0;												\
	{																			\
		u64 time;																\
		NTiming::GetPreciseTime(&time);											\
		for (u32 j=0; j<100; ++j)												\
			fast_memcpy_swizzle(d, s, n);										\
		_fast_memcpy_swizzle = (u32)(GetCurrent()-time);						\
	}																			\
	u32 _memcpy_byteswap = 0;													\
	{																			\
		u64 time = GetCurrent();												\
		for (u32 j=0; j<100; ++j)												\
			memcpy_byteswap(d, s, n);											\
		_memcpy_byteswap = (u32)(GetCurrent()-time);							\
	}																			\
	printf("%d bytes | SWIZZLE %d | FASTSWIZZLE %d\n", n, _memcpy_byteswap, _fast_memcpy_swizzle); \
	}

void memcpy_test( void * dst, const void * src, size_t size )
{
	MEMCPY_TEST(dst, src, size);
}
#endif // PROFILE_MEMCPY
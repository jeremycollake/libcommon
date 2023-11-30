#include "pch.h"
#include "bitOperations.h"

void bitOperations::unit_tests()
{
	_ASSERT(bitOperations::CountBitsSet(0xffff0000ffff0000) == 32);
	_ASSERT(bitOperations::CountBitsSet(0xfffff000ffff0000) == 32 + 4);
	_ASSERT(bitOperations::CountBitsSet(0xffffffffffff0000) == 64 - 16);
	_ASSERT(bitOperations::CountBitsSet(0xffffffffffffffff) == 64);
}

unsigned int bitOperations::CountBitsSet(const unsigned long long mask)
{
	unsigned int set_bits = 0;
	for (unsigned int nI = 0; nI < sizeof(mask) * 8; nI++)
	{
		if (mask & (1ULL << nI))
		{
			set_bits++;
		}
	}
	return set_bits;
}

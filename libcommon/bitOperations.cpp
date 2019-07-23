#include "pch.h"

unsigned int CountBitsSet(unsigned long long mask)
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

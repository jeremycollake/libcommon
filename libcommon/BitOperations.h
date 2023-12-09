#pragma once

class BitOperations
{
public:
	// create a fully set mask of the given size
	static unsigned long long BuildFullMask(const int nBitsize)
	{
		unsigned long long mask = 0;
		for (int nI = 0; nI < nBitsize; nI++)
		{
			mask |= (1ULL << nI);
		}
		return mask;
	}

	// returns the number of bits set in the mask
	static unsigned int CountBitsSet(const unsigned long long mask)
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
};

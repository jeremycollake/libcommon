// libcommon.cpp : Defines the functions for the static library.
//

#include "pch.h"
#include "framework.h"
#include "libCommon.h"

DWORD GetWindows10Build()
{
	DWORD dwBulldNum = 0;
	if (IsWindows10OrGreater())
	{
		fnRtlGetNtVersionNumbers RtlGetNtVersionNumbers = reinterpret_cast<fnRtlGetNtVersionNumbers>(GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "RtlGetNtVersionNumbers"));
		if (RtlGetNtVersionNumbers)
		{
			DWORD major, minor;
			RtlGetNtVersionNumbers(&major, &minor, &dwBulldNum);
			dwBulldNum &= ~0xF0000000;
		}		
	}	
	return dwBulldNum;
}

bool wildcmpEx(const TCHAR* wild, const TCHAR* str) {
	int slen = (int)_tcslen(str);
	int wlen = (int)_tcslen(wild);

	// MUST exist
	int reqLen = 0;
	for (int i = 0; i < wlen; ++i)
	{
		if (wild[i] != _T('*'))
		{
			++reqLen;
		}
	}

	if (slen < reqLen)
	{
		return false;
	}

	// length is okay; now we do the comparing
	int w = 0, s = 0;

	for (; s < slen && w < wlen; ++s, ++w) {

		// if we hit a '?' just go to the next char in both `str` and `wild`
		if (wild[w] == _T('?'))
		{
			continue;
		}

		// we hit an unlimited wildcard
		else if (wild[w] == _T('*')) {
			// if it's the last char in the string, we're done
			if ((w + 1) == wlen)
				return true;

			bool ret = true;

			// for each remaining character in `wild`
			while (++w < wlen && ret)
			{
				// for each remaining character in `str`
				int i;
				for (i = 0; i < (slen - s); ++i)
				{
					// looking to match next character after wildcard
					// failure occurs here.. if it does NOT match, then it will incorrectly return true
					if (str[s + i] == wild[w])
					{
						ret = wildcmpEx(wild + w, str + s + i);
						// if successful, we're done
						if (ret)
						{
							return true;
						}
					}
				}
				// my critical fix to this code
				if (i >= (slen - s))
				{
					return false;
				}
			}
			return ret;
		}
		else   if ((wild[w] == str[s]))
		{
			continue;
		}
		else
		{
			return false;
		}
	}

	return s >= slen ? true : false;
}


bool wildicmpEx(const TCHAR* wild, const TCHAR* str) {

	_ASSERT(str);
	int slen = (int)_tcslen(str);
	int wlen = (int)_tcslen(wild);
	if (!slen || !wlen) return false;

	// if inverse operator, return inverted wildcard
	if (wild[0] == '~' || wild[0] == '!')
	{
		return !wildicmpEx(wild + 1, str);
	}


	// MUST exist
	int reqLen = 0;
	for (int i = 0; i < wlen; ++i)
	{
		if (wild[i] != _T('*'))
		{
			++reqLen;
		}
	}

	if (slen < reqLen)
	{
		return false;
	}

	// length is okay; now we do the comparing
	int w = 0, s = 0;

	for (; s < slen && w < wlen; ++s, ++w) {

		// if we hit a '?' just go to the next char in both `str` and `wild`
		if (wild[w] == _T('?'))
		{
			continue;
		}

		// we hit an unlimited wildcard
		else if (wild[w] == _T('*')) {
			// if it's the last char in the string, we're done
			if ((w + 1) == wlen)
				return true;

			bool ret = true;

			// for each remaining character in `wild`
			while (++w < wlen && ret)
			{
				// for each remaining character in `str`
				int i;
				for (i = 0; i < (slen - s); ++i)
				{
					// if same as the next after wildcard
					if ((str[s + i] == wild[w])
						|| (tolower(wild[w]) == tolower(str[s + i])))
					{
						// compare from these points on
						ret = wildicmpEx(wild + w, str + s + i);
						// if successful, we're done
						if (ret)
						{
							return true;
						}
					}
				}
				// my critical fix to this code
				if (i >= (slen - s))
				{
					return false;
				}
			}
			return ret;
		}
		else   if ((wild[w] == str[s])
			// or if not wildcard (or other special char) and we compare lowercase forms
			|| (tolower(wild[w]) == tolower(str[s])))
		{
			continue;
		}
		else
		{
			return false;
		}
	}

	return s >= slen ? true : false;
}

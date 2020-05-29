// libcommon.cpp : Defines the functions for the static library.
//

#include "pch.h"
#include "framework.h"
#include "libCommon.h"

void ListView_SetSingleSelection(const HWND hWndListview, const int nIndex)
{
	ListView_UnselectAll(hWndListview);
	ListView_EnsureVisible(hWndListview, nIndex, FALSE);	
	ListView_SetItemState(hWndListview, nIndex, LVIS_FOCUSED | LVIS_SELECTED, LVIS_FOCUSED | LVIS_SELECTED);	
	ListView_SetSelectionMark(hWndListview, nIndex);
}

void ListView_UnselectAll(const HWND hWndListview)
{
	int nSearchPos = -1;
	do
	{
		nSearchPos = ListView_GetNextItem(hWndListview, nSearchPos, LVNI_ALL);
		if (nSearchPos != -1)
		{
			ListView_SetItemState(hWndListview, nSearchPos, 0, LVIS_FOCUSED | LVIS_SELECTED);
		}
	} while (nSearchPos != -1);	
}

bool IsFileWritable(const WCHAR *pwszFilepath)
{
	HANDLE hFile = CreateFile(pwszFilepath, GENERIC_WRITE, 0, NULL, OPEN_EXISTING, 0, NULL);
	if (hFile != INVALID_HANDLE_VALUE)
	{
		CloseHandle(hFile);
		return true;
	}
	return false;
}

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

void RemoveTrailingBackslash(ATL::CString& csStr)
{
	int nLen = csStr.GetLength();
	bool bNeedsAppend = true;
	if (nLen)
	{
		if (csStr[nLen - 1] == '\\' || csStr[nLen - 1] == '/')
		{
			csStr.Truncate(nLen - 1);			
		}
	}	
}

// for appending backslash or forward-slash only if missing
void AppendCharacterIfMissing(ATL::CString& csStr, const WCHAR wChar)
{
	int nLen = csStr.GetLength();
	bool bNeedsAppend = true;
	if (nLen)
	{
		if (csStr[nLen - 1] == wChar)
		{
			bNeedsAppend = false;
		}
	}
	if (bNeedsAppend)
	{
		csStr += wChar;
	}
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

// source: https://stackoverflow.com/a/8196291/191514
BOOL IsElevated()
{
	BOOL fRet = FALSE;
	HANDLE hToken = NULL;
	if (OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &hToken)) 
	{
		TOKEN_ELEVATION Elevation;
		DWORD cbSize = sizeof(TOKEN_ELEVATION);
		if (GetTokenInformation(hToken, TokenElevation, &Elevation, sizeof(Elevation), &cbSize)) 
		{
			fRet = Elevation.TokenIsElevated;
		}
	}
	if (hToken) 
	{
		CloseHandle(hToken);
	}
	return fRet;
}

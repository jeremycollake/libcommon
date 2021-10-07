// libcommon.cpp : Defines the functions for the static library.
//

#include "pch.h"
#include "framework.h"
#include "libCommon.h"
#include <sddl.h>
#include <algorithm>
#include <string>
#include <cctype>

bool ListView_InitColumns(const HWND hWndListview, const HMODULE hResourceModule,
	const std::vector<std::pair<int, int>>& vecStringIDAndWidthPairs)
{
	bool bSuccess = true;

	RECT r;
	GetClientRect(hWndListview, &r);

	// we'll leave room for the vertical scrollbar, even if it doesn't exist
	// if we don't, then a horizontal scrollbar will also occur whenever a vertical scrollbar is present
	int nScrollBarWidth = GetSystemMetrics(SM_CXVSCROLL);

	unsigned int nTotalRunningWidth = 0;
	unsigned int nIndex = 0;
	for (auto& columnInfo : vecStringIDAndWidthPairs)
	{
		WCHAR wszBuf[1024] = { 0 };
		if (!LoadString(hResourceModule, columnInfo.first, wszBuf, _countof(wszBuf)))
		{
			wcscpy_s(wszBuf, L"[Error: Missing string]");
			bSuccess = false;
			// continue on, non-critical failure
		}

		LVCOLUMN lvCol;
		lvCol.mask = LVCF_TEXT | LVCF_WIDTH;
		lvCol.pszText = wszBuf;
		if (columnInfo.second != -1)
		{
			lvCol.cx = columnInfo.second;
			nTotalRunningWidth += columnInfo.second;
		}
		else
		{
			// calculate remainding listview width, minus vertical scrollbar width (even if it doesn't exist)
			int nSizeAdjust = nTotalRunningWidth + nScrollBarWidth;
			if ((r.right - r.left) >= nSizeAdjust)
			{
				lvCol.cx = (r.right - r.left) - nSizeAdjust;
			}
			else
			{
				// no room for column, so use some constant size, and indicate failure
				const static int FAILSAFE_LV_COL_WIDTH = 100;
				lvCol.cx = FAILSAFE_LV_COL_WIDTH;
				bSuccess = false;
			}			
		}
		if (ListView_InsertColumn(hWndListview, nIndex, &lvCol) == -1)
		{
			bSuccess = false;
			// critical failure since it will impact listview column indices
			break;
		}
		nIndex++;
	}

	_ASSERT(bSuccess);
	return bSuccess;
}

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

ATL::CString ListView_GetTextAtPosition(const HWND hWndListview, const int nRow, const int nCol)
{
	LVITEM lvItem;
	memset(&lvItem, 0, sizeof(lvItem));
	lvItem.mask = LVIF_TEXT;
	lvItem.iItem = nRow;
	lvItem.iSubItem = nCol;
	WCHAR wszText[1024] = { 0 };
	lvItem.pszText = wszText;
	lvItem.cchTextMax = _countof(wszText);
	ListView_GetItem(hWndListview, &lvItem);
	return wszText;
}

bool ListView_DoesTextExistAtColumnInAnyRow(const HWND hWndListview, const int nColumn, const WCHAR* pwszText)
{
	if (ListView_GetRowContainingTextAtColumn(hWndListview, nColumn, pwszText) == -1)
	{
		return false;
	}
	return true;
}

int ListView_GetRowContainingTextAtColumn(const HWND hWndListview, const int nColumn, const WCHAR* pwszText)
{
	int nRowCount = ListView_GetItemCount(hWndListview);
	for (int nI = 0; nI < nRowCount; nI++)
	{
		CString csText = ListView_GetTextAtPosition(hWndListview, nI, nColumn);
		if (csText.CompareNoCase(pwszText) == 0)
		{
			return nI;
		}
	}
	return -1;
}

bool IsFileWritable(const WCHAR* pwszFilepath)
{
	// if doesn't exist, return true
	if (GetFileAttributes(pwszFilepath) == INVALID_FILE_ATTRIBUTES)
	{
		return true;
	}
	HANDLE hFile = CreateFile(pwszFilepath, GENERIC_WRITE, 0, NULL, OPEN_EXISTING, 0, NULL);
	if (hFile != INVALID_HANDLE_VALUE)
	{
		CloseHandle(hFile);
		return true;
	}
	return false;
}

// build Windows version string for 10+ or Server 2019+, returns false if can't be built (e.g. Win7)
bool BuildWindows10OrGreaterVersionString(ATL::CString& csVersionString)
{
	if (IsWindows10OrGreater())
	{
		csVersionString.Format(L"Windows %s (%d)", (IsWindowsServer() ? L"Server" : (IsWindows11OrGreater() ? L"11" : L"10")),
			GetWindows10Build());
		return true;
	}
	csVersionString.Empty();
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

// temporary, pending addition to WinSDK
bool IsWindows11OrGreater()
{
	if (!IsWindows10OrGreater())
	{
		return false;
	}
	const static int WIN11_MIN_BUILD_NUMBER = 22000;
	return GetWindows10Build() >= WIN11_MIN_BUILD_NUMBER ? true : false;
}

void RemoveTrailingBackslash(ATL::CString& csStr)
{
	int nLen = csStr.GetLength();
	if (nLen)
	{
		if (csStr[nLen - 1] == '\\' || csStr[nLen - 1] == '/')
		{
			csStr.Truncate(nLen - 1);
		}
	}
}

void AppendBackslashIfMissing(ATL::CString& csStr)
{
	AppendCharacterIfMissing(csStr, '\\');
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

bool wildcmpEx(const TCHAR* wild, const TCHAR* str) 
{
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


bool wildicmpEx(const TCHAR* wild, const TCHAR* str) 
{
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

// from MSDN https://docs.microsoft.com/en-us/previous-versions/dotnet/articles/bb625960(v=msdn.10)?redirectedfrom=MSDN
// see https://stackoverflow.com/questions/9912534/how-to-create-a-new-process-with-a-lower-integrity-level-il
// "There appears to be a bug in the example code, since the correct SID for low integrity level is S-1-16-4096 rather than S-1-16-1024, but you'll want medium integrity level anyway, which is S-1-16-8192. These can be found here."
// https://support.microsoft.com/en-us/help/243330/well-known-security-identifiers-in-windows-operating-systems
BOOL CreateMediumProcess(const WCHAR* pwszProcessName, WCHAR* pwszCommandLine, const WCHAR* pwszCWD, PROCESS_INFORMATION* pInfo)
{
	BOOL                  fRet;
	HANDLE                hToken = NULL;
	HANDLE                hNewToken = NULL;
	PSID                  pIntegritySid = NULL;
	TOKEN_MANDATORY_LABEL TIL = { 0 };
	STARTUPINFO           StartupInfo = { 0 };
	_ASSERT(pInfo);
	memset(pInfo, 0, sizeof(PROCESS_INFORMATION));

	// Medium integrity SID
	WCHAR wszIntegritySid[20] = L"S-1-16-4096";

	fRet = OpenProcessToken(GetCurrentProcess(),
		TOKEN_DUPLICATE |
		TOKEN_ADJUST_DEFAULT |
		TOKEN_QUERY |
		TOKEN_ASSIGN_PRIMARY,
		&hToken);

	if (!fRet)
	{
		goto CleanExit;
	}

	fRet = DuplicateTokenEx(hToken,
		0,
		NULL,
		SecurityImpersonation,
		TokenPrimary,
		&hNewToken);

	if (!fRet)
	{
		goto CleanExit;
	}

	fRet = ConvertStringSidToSid(wszIntegritySid, &pIntegritySid);

	if (!fRet)
	{
		goto CleanExit;
	}

	TIL.Label.Attributes = SE_GROUP_INTEGRITY;
	TIL.Label.Sid = pIntegritySid;

	//
	// Set the process integrity level
	//

	fRet = SetTokenInformation(hNewToken,
		TokenIntegrityLevel,
		&TIL,
		sizeof(TOKEN_MANDATORY_LABEL) + GetLengthSid(pIntegritySid));

	if (!fRet)
	{
		goto CleanExit;
	}

	//
	// Create the new process at Low integrity
	//

	fRet = CreateProcessAsUser(hNewToken,
		pwszProcessName,
		pwszCommandLine,
		NULL,
		NULL,
		FALSE,
		0,
		NULL,
		pwszCWD,
		&StartupInfo,
		pInfo);

CleanExit:

	if (pInfo->hProcess != NULL)
	{
		CloseHandle(pInfo->hProcess);
	}

	if (pInfo->hThread != NULL)
	{
		CloseHandle(pInfo->hThread);
	}

	LocalFree(pIntegritySid);

	if (hNewToken != NULL)
	{
		CloseHandle(hNewToken);
	}

	if (hToken != NULL)
	{
		CloseHandle(hToken);
	}

	return fRet;
}

size_t ExplodeString(const ATL::CString& str, const WCHAR delim, std::vector<ATL::CString>& vecOut)
{
	int nPos = 0;
	ATL::CString token = str.Tokenize(L";", nPos);
	while (-1 != nPos)
	{
		vecOut.push_back(token);
		token = str.Tokenize(L";", nPos);
	};
	return vecOut.size();
}

bool IsStringMatchInVector(const WCHAR* string, const std::vector<ATL::CString>& vecPatterns)
{
	for (auto& i : vecPatterns)
	{
		if (wildicmpEx(i, string))
		{
			return true;
		}
	}
	return false;
}

size_t wstringFindNoCase(const std::wstring& strHaystack, const std::wstring& strNeedle)
{
	auto it = std::search(
		strHaystack.begin(), strHaystack.end(),
		strNeedle.begin(), strNeedle.end(),
		[](wchar_t ch1, wchar_t ch2) { return std::toupper(ch1) == std::toupper(ch2); }
	);
	if (it == strHaystack.end())
	{
		return std::wstring::npos;
	}
	return strHaystack.end() - it;
}
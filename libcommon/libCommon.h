#pragma once

#include <atlstr.h>
#include <vector>
#include <string>

// ListView_InitColumns - Insert listview columns with calculated width of final column
// accepts vector of paired string IDs and widths for columns
//  supports -1 for column size, in which case it will be remaining width of listview
// returns true if no errors occurred
bool ListView_InitColumns(const HWND hWndListview, const HMODULE hResourceModule,
	const std::vector<std::pair<int, int>>& vecStringIDAndWidthPairs);

void ListView_SetSingleSelection(const HWND hWndListview, const int nIndex);
void ListView_UnselectAll(const HWND hWndListview);
ATL::CString ListView_GetTextAtPosition(const HWND hWndListview, const int nRow, const int nCol);
bool ListView_DoesTextExistAtColumnInAnyRow(const HWND hWndListview, const int nColumn, const WCHAR* pwszText);
int ListView_GetRowContainingTextAtColumn(const HWND hWndListview, const int nColumn, const WCHAR* pwszText);

bool IsFileWritable(const WCHAR* pwszFilepath);

void RemoveTrailingBackslash(ATL::CString& csStr);
void AppendBackslashIfMissing(ATL::CString& csStr);
// for appending backslash or forward-slash only if missing
void AppendCharacterIfMissing(ATL::CString& csStr, const WCHAR wChar);

using fnRtlGetNtVersionNumbers = void (WINAPI*)(LPDWORD major, LPDWORD minor, LPDWORD build);

// build Windows version string for 10+ or Server 2019+, returns false if can't be built (e.g. Win7)
bool BuildWindows10OrGreaterVersionString(ATL::CString& csVersionString);
// returns 0 if not Windows 10
DWORD GetWindows10Build();
// temporary, pending addition to WinSDK
bool IsWindows11OrGreater();

bool wildcmpEx(const TCHAR* wild, const TCHAR* str);
bool wildicmpEx(const TCHAR* wild, const TCHAR* str);
#define wildcmp wildcmpEx
#define wildcmpi wildicmpEx

BOOL IsElevated();
// launch a medium IL process (unelevated from elevated)
BOOL CreateMediumProcess(const WCHAR* pwszProcessName, WCHAR* pwszCommandLine, const WCHAR* pwszCWD, PROCESS_INFORMATION* pInfo);

size_t ExplodeString(const ATL::CString& str, const WCHAR delim, std::vector<ATL::CString>& vecOut);
bool IsStringMatchInVector(const WCHAR* string, const std::vector<ATL::CString>& vecPatterns);

size_t wstringFindNoCase(const std::wstring& strHaystack, const std::wstring& strNeedle);

// if below min, set to min. If above max, set to max.
#define LIMIT_RANGE(V, MIN, MAX) ((V) < (MIN) ? (MIN) : (V) > (MAX) ? (MAX) : (V))
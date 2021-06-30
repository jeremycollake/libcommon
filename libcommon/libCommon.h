#pragma once

#include <atlstr.h>
#include <vector>
#include <string>

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
#pragma once

#include <atlstr.h>

void ListView_SetSingleSelection(const HWND hWndListview, const int nIndex);
void ListView_UnselectAll(const HWND hWndListview);
ATL::CString ListView_GetTextAtPosition(const HWND hWndListview, const int nRow, const int nCol);

bool IsFileWritable(const WCHAR* pwszFilepath);

void RemoveTrailingBackslash(ATL::CString& csStr);
// for appending backslash or forward-slash only if missing
void AppendCharacterIfMissing(ATL::CString& csStr, const WCHAR wChar);

using fnRtlGetNtVersionNumbers = void (WINAPI*)(LPDWORD major, LPDWORD minor, LPDWORD build);

// returns 0 if not Windows 10
DWORD GetWindows10Build();

bool wildcmpEx(const TCHAR* wild, const TCHAR* str);
bool wildicmpEx(const TCHAR* wild, const TCHAR* str);
#define wildcmp wildcmpEx
#define wildcmpi wildicmpEx

BOOL IsElevated();
// launch a medium IL process (unelevated from elevated)
BOOL CreateMediumProcess(const WCHAR* pwszProcessName, WCHAR* pwszCommandLine, const WCHAR *pwszCWD, PROCESS_INFORMATION* pInfo);
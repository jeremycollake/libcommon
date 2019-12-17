#pragma once

using fnRtlGetNtVersionNumbers = void (WINAPI*)(LPDWORD major, LPDWORD minor, LPDWORD build);

// returns 0 if not Windows 10
DWORD GetWindows10Build();

bool wildcmpEx(const TCHAR* wild, const TCHAR* str);
bool wildicmpEx(const TCHAR* wild, const TCHAR* str);
#define wildcmp wildcmpEx
#define wildcmpi wildicmpEx
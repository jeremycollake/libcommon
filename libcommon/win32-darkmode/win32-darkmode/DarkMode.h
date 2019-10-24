// from https://github.com/ysc3839/win32-darkmode.git
#pragma once

#include "IatHook.h"

enum IMMERSIVE_HC_CACHE_MODE
{
	IHCM_USE_CACHED_VALUE,
	IHCM_REFRESH
};

// Insider 18334
enum PreferredAppMode
{
	Default,
	AllowDark,
	ForceDark,
	ForceLight,
	Max
};

using fnRtlGetNtVersionNumbers = void (WINAPI *)(LPDWORD major, LPDWORD minor, LPDWORD build);
// 1809 17763
using fnShouldAppsUseDarkMode = bool (WINAPI *)(); // ordinal 132
using fnAllowDarkModeForWindow = bool (WINAPI *)(HWND hWnd, bool allow); // ordinal 133
using fnAllowDarkModeForApp = bool (WINAPI *)(bool allow); // ordinal 135, removed since 18334
using fnFlushMenuThemes = void (WINAPI *)(); // ordinal 136
using fnRefreshImmersiveColorPolicyState = void (WINAPI *)(); // ordinal 104
using fnIsDarkModeAllowedForWindow = bool (WINAPI *)(HWND hWnd); // ordinal 137
using fnGetIsImmersiveColorUsingHighContrast = bool (WINAPI *)(IMMERSIVE_HC_CACHE_MODE mode); // ordinal 106
using fnOpenNcThemeData = HTHEME(WINAPI *)(HWND hWnd, LPCWSTR pszClassList); // ordinal 49
// Insider 18290
using fnShouldSystemUseDarkMode = bool (WINAPI *)(); // ordinal 138
// Insider 18334
using fnSetPreferredAppMode = PreferredAppMode (WINAPI *)(PreferredAppMode appMode); // ordinal 135, since 18334
using fnIsDarkModeAllowedForApp = bool (WINAPI *)(); // ordinal 139

extern fnShouldAppsUseDarkMode _ShouldAppsUseDarkMode;
extern fnAllowDarkModeForWindow _AllowDarkModeForWindow;
extern fnAllowDarkModeForApp _AllowDarkModeForApp;
extern fnFlushMenuThemes _FlushMenuThemes;
extern fnRefreshImmersiveColorPolicyState _RefreshImmersiveColorPolicyState;
extern fnIsDarkModeAllowedForWindow _IsDarkModeAllowedForWindow;
extern fnGetIsImmersiveColorUsingHighContrast _GetIsImmersiveColorUsingHighContrast;
extern fnOpenNcThemeData _OpenNcThemeData;
// Insider 18290
extern fnShouldSystemUseDarkMode _ShouldSystemUseDarkMode;
// Insider 18334
extern fnSetPreferredAppMode _SetPreferredAppMode;

extern bool g_darkModeSupported;
extern bool g_darkModeEnabled;
extern DWORD g_buildNumber;

bool AllowDarkModeForWindow(HWND hWnd, bool allow);
bool IsHighContrast();
void RefreshTitleBarThemeColor(HWND hWnd);
bool IsColorSchemeChangeMessage(LPARAM lParam);
bool IsColorSchemeChangeMessage(UINT message, LPARAM lParam);
void AllowDarkModeForApp(bool allow);
void FixDarkScrollBar();
void InitDarkMode(bool bAllowFutureWin10Builds_Unsafe = false);

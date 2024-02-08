// code based on work from:
//  https://github.com/ysc3839/win32-darkmode.git
//  https://github.com/ysc3839/win32-darkmode/pull/17
//  https://github.com/notepad-plus-plus/notepad-plus-plus/pull/9587

#include "../../pch.h"
#include "darkmode.h"

fnSetWindowCompositionAttribute _SetWindowCompositionAttribute = nullptr;
fnShouldAppsUseDarkMode _ShouldAppsUseDarkMode = nullptr;
fnAllowDarkModeForWindow _AllowDarkModeForWindow = nullptr;
fnAllowDarkModeForApp _AllowDarkModeForApp = nullptr;
fnFlushMenuThemes _FlushMenuThemes = nullptr;
fnRefreshImmersiveColorPolicyState _RefreshImmersiveColorPolicyState = nullptr;
fnIsDarkModeAllowedForWindow _IsDarkModeAllowedForWindow = nullptr;
fnGetIsImmersiveColorUsingHighContrast _GetIsImmersiveColorUsingHighContrast = nullptr;
fnOpenNcThemeData _OpenNcThemeData = nullptr;
// 1903 18362
fnShouldSystemUseDarkMode _ShouldSystemUseDarkMode = nullptr;
fnSetPreferredAppMode _SetPreferredAppMode = nullptr;

bool g_darkModeSupported = false;
bool g_darkModeEnabled = false;
DWORD g_buildNumber = 0;

bool ShouldAppsUseDarkModeSafe()
{
	if (g_darkModeSupported && _ShouldAppsUseDarkMode)
		return _ShouldAppsUseDarkMode();
	return false;
}

bool AllowDarkModeForWindowSafe(const HWND hWnd, const bool allow)
{
	if (g_darkModeSupported && _AllowDarkModeForWindow)
		return _AllowDarkModeForWindow(hWnd, allow);
	return false;
}

bool IsHighContrast()
{
	HIGHCONTRASTW highContrast = { sizeof(highContrast) };
	if (SystemParametersInfoW(SPI_GETHIGHCONTRAST, sizeof(highContrast), &highContrast, FALSE))
		return highContrast.dwFlags & HCF_HIGHCONTRASTON;
	return false;
}

void RefreshTitleBarThemeColor(HWND hWnd)
{
	BOOL dark = FALSE;
	if (_IsDarkModeAllowedForWindow(hWnd) &&
		_ShouldAppsUseDarkMode() &&
		!IsHighContrast())
	{
		dark = TRUE;
	}
	if (g_buildNumber < 18362)
		SetPropW(hWnd, L"UseImmersiveDarkModeColors", reinterpret_cast<HANDLE>(static_cast<INT_PTR>(dark)));
	else if (_SetWindowCompositionAttribute)
	{
		WINDOWCOMPOSITIONATTRIBDATA data = { WCA_USEDARKMODECOLORS, &dark, sizeof(dark) };
		_SetWindowCompositionAttribute(hWnd, &data);
	}
}

bool IsColorSchemeChangeMessage(LPARAM lParam)
{
	bool is = false;
	if (lParam && CompareStringOrdinal(reinterpret_cast<LPCWCH>(lParam), -1, L"ImmersiveColorSet", -1, TRUE) == CSTR_EQUAL)
	{
		_RefreshImmersiveColorPolicyState();
		is = true;
	}
	_GetIsImmersiveColorUsingHighContrast(IHCM_REFRESH);
	return is;
}

bool IsColorSchemeChangeMessage(UINT message, LPARAM lParam)
{
	if (message == WM_SETTINGCHANGE)
		return IsColorSchemeChangeMessage(lParam);
	return false;
}

void AllowDarkModeForApp(bool allow)
{
	if (_AllowDarkModeForApp)
		_AllowDarkModeForApp(allow);
	else if (_SetPreferredAppMode)
		_SetPreferredAppMode(allow ? AllowDark : Default);
}

void FixDarkScrollBar()
{
	HMODULE hComctl = LoadLibraryExW(L"comctl32.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
	if (hComctl)
	{
		auto addr = FindDelayLoadThunkInModule(hComctl, "uxtheme.dll", 49); // OpenNcThemeData
		if (addr)
		{
			DWORD oldProtect;
			if (VirtualProtect(addr, sizeof(IMAGE_THUNK_DATA), PAGE_READWRITE, &oldProtect))
			{
				auto MyOpenThemeData = [](HWND hWnd, LPCWSTR classList) -> HTHEME {
					if (g_darkModeEnabled)
					{
						if (wcscmp(classList, L"ScrollBar") == 0)
						{
							hWnd = nullptr;
							classList = L"Explorer::ScrollBar";
						}
					}
					return _OpenNcThemeData(hWnd, classList);
					};

				addr->u1.Function = reinterpret_cast<ULONG_PTR>(static_cast<fnOpenNcThemeData>(MyOpenThemeData));
				VirtualProtect(addr, sizeof(IMAGE_THUNK_DATA), oldProtect, &oldProtect);
			}
		}
	}
}

bool CheckWindowsBuildNumber(const DWORD dwBuildNumber)
{
	// check minimum supported Win10 build number	
	static const int DARKMODE_MINIMUM_WIN10_BUILD = 17763;
	if (DARKMODE_MINIMUM_WIN10_BUILD <= dwBuildNumber)
	{
		return true;
	}
	return false;
}

void InitDarkMode()
{
	fnRtlGetNtVersionNumbers RtlGetNtVersionNumbers = reinterpret_cast<fnRtlGetNtVersionNumbers>(GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "RtlGetNtVersionNumbers"));
	if (RtlGetNtVersionNumbers)
	{
		DWORD major, minor;
		RtlGetNtVersionNumbers(&major, &minor, &g_buildNumber);
		g_buildNumber &= ~0xF0000000;
		if (major == 10 && minor == 0 && CheckWindowsBuildNumber(g_buildNumber))
		{
			HMODULE hUxtheme = LoadLibraryExW(L"uxtheme.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
			if (hUxtheme)
			{
				_OpenNcThemeData = reinterpret_cast<fnOpenNcThemeData>(GetProcAddress(hUxtheme, MAKEINTRESOURCEA(49)));
				_RefreshImmersiveColorPolicyState = reinterpret_cast<fnRefreshImmersiveColorPolicyState>(GetProcAddress(hUxtheme, MAKEINTRESOURCEA(104)));
				_GetIsImmersiveColorUsingHighContrast = reinterpret_cast<fnGetIsImmersiveColorUsingHighContrast>(GetProcAddress(hUxtheme, MAKEINTRESOURCEA(106)));
				_ShouldAppsUseDarkMode = reinterpret_cast<fnShouldAppsUseDarkMode>(GetProcAddress(hUxtheme, MAKEINTRESOURCEA(132)));
				_AllowDarkModeForWindow = reinterpret_cast<fnAllowDarkModeForWindow>(GetProcAddress(hUxtheme, MAKEINTRESOURCEA(133)));

				auto ord135 = GetProcAddress(hUxtheme, MAKEINTRESOURCEA(135));
				if (g_buildNumber < 18362)
					_AllowDarkModeForApp = reinterpret_cast<fnAllowDarkModeForApp>(ord135);
				else
					_SetPreferredAppMode = reinterpret_cast<fnSetPreferredAppMode>(ord135);

				//_FlushMenuThemes = reinterpret_cast<fnFlushMenuThemes>(GetProcAddress(hUxtheme, MAKEINTRESOURCEA(136)));
				_IsDarkModeAllowedForWindow = reinterpret_cast<fnIsDarkModeAllowedForWindow>(GetProcAddress(hUxtheme, MAKEINTRESOURCEA(137)));

				_SetWindowCompositionAttribute = reinterpret_cast<fnSetWindowCompositionAttribute>(GetProcAddress(GetModuleHandleW(L"user32.dll"), "SetWindowCompositionAttribute"));

				if (_OpenNcThemeData &&
					_RefreshImmersiveColorPolicyState &&
					_ShouldAppsUseDarkMode &&
					_AllowDarkModeForWindow &&
					(_AllowDarkModeForApp || _SetPreferredAppMode) &&
					//_FlushMenuThemes &&
					_IsDarkModeAllowedForWindow)
				{
					g_darkModeSupported = true;

					AllowDarkModeForApp(true);
					_RefreshImmersiveColorPolicyState();

					g_darkModeEnabled = _ShouldAppsUseDarkMode() && !IsHighContrast();

					FixDarkScrollBar();
				}
			}
		}
	}
}

bool ShouldThisAppUseDarkModeNow()
{
	return (g_darkModeSupported && g_darkModeEnabled
		&& _ShouldAppsUseDarkMode && _ShouldAppsUseDarkMode());
}

// dark progress bars
LRESULT CALLBACK ProgressBarSubclassProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam, UINT_PTR uIdSubclass, DWORD_PTR dwRefData)
{
	auto pColorInfo = (DarkProgressBarBrushes*)dwRefData;
	switch (uMsg)
	{
	case WM_ERASEBKGND:
	{
		// we paint entire background regardless in WM_PAINT, so this isn't necessary, and is called every time the progressbar value changes, so may cause flicker
		return FALSE;		// non-zero if bg painted
		/*
		LIBCOMMON_DEBUG_PRINT(L"ProgressBarSubclassProc WM_ERASEBKGND");
		if (ShouldThisAppUseDarkModeNow())
		{
			RECT rc;
			GetClientRect(hWnd, &rc);
			FillRect((HDC)wParam, &rc, pColorInfo->hBrushBackground);
			return TRUE;		// non-zero if bg painted
		}
		break;*/
	}
	case WM_PAINT:
	{
		// always paint if disabled progress bar
		if (SendMessage(hWnd, PBM_GETSTATE, 0, 0) == PBST_ERROR)
		{
			RECT rcClient;
			GetClientRect(hWnd, &rcClient);

			PAINTSTRUCT ps;
			HDC hdc = BeginPaint(hWnd, &ps);

			HDC hMemDC = CreateCompatibleDC(hdc);
			HBITMAP hBitmap = CreateCompatibleBitmap(hdc, rcClient.right - rcClient.left, rcClient.bottom - rcClient.top);
			HBITMAP hOldBitmap = static_cast<HBITMAP>(SelectObject(hMemDC, hBitmap));

			FillRect(hMemDC, &rcClient, pColorInfo->hBrushDisabledBackground);
			FrameRect(hMemDC, &rcClient, pColorInfo->hBrushBorder);

			BitBlt(hdc, 0, 0, rcClient.right - rcClient.left, rcClient.bottom - rcClient.top, hMemDC, 0, 0, SRCCOPY);
			SelectObject(hMemDC, hOldBitmap);
			DeleteBitmap(hBitmap);
			DeleteDC(hMemDC);

			EndPaint(hWnd, &ps);
			return 0;	// 0 if handled
		}
		// only paint if dark mode is enabled, else leave to control class
		else if (ShouldThisAppUseDarkModeNow())
		{
			RECT rcClient;
			GetClientRect(hWnd, &rcClient);

			PAINTSTRUCT ps;
			HDC hdc = BeginPaint(hWnd, &ps);

			HDC hMemDC = CreateCompatibleDC(hdc);
			HBITMAP hBitmap = CreateCompatibleBitmap(hdc, rcClient.right - rcClient.left, rcClient.bottom - rcClient.top);
			HBITMAP hOldBitmap = static_cast<HBITMAP>(SelectObject(hMemDC, hBitmap));

			FillRect(hMemDC, &rcClient, pColorInfo->hBrushBackground);
			FrameRect(hMemDC, &rcClient, pColorInfo->hBrushBorder);

			const int PB_BORDER_WIDTH = 1;
			if ((rcClient.bottom - PB_BORDER_WIDTH) > (rcClient.top + PB_BORDER_WIDTH) && (rcClient.right - PB_BORDER_WIDTH) > (rcClient.left + PB_BORDER_WIDTH))
			{
				unsigned int nHeight = (rcClient.bottom - PB_BORDER_WIDTH) - (rcClient.top + PB_BORDER_WIDTH);
				_ASSERT(nHeight > 0 && nHeight < 32 * 1024);
				PBRANGE pbRange = { 0, 0 };
				SendMessage(hWnd, PBM_GETRANGE, FALSE, (LPARAM)&pbRange);
				_ASSERT(pbRange.iHigh > pbRange.iLow);
				if (pbRange.iHigh > pbRange.iLow)
				{
					unsigned long nPos = static_cast<unsigned long>(SendMessage(hWnd, PBM_GETPOS, FALSE, (LPARAM)&pbRange));
					unsigned long nRangeDiff = pbRange.iHigh - pbRange.iLow;
					unsigned long nFilledToPercentx100 = (nPos - static_cast<unsigned long>(pbRange.iLow)) * 100 / nRangeDiff;
					unsigned long nTopFilled = (nHeight * nFilledToPercentx100) / 100;
					RECT rFilled;
					rFilled.bottom = rcClient.bottom;
					rFilled.top = rcClient.bottom - nTopFilled;
					rFilled.left = rcClient.left + PB_BORDER_WIDTH;
					rFilled.right = rcClient.right - PB_BORDER_WIDTH;
					FillRect(hMemDC, &rFilled, pColorInfo->hBrushForeground);
				}
			}
			else
			{
				_ASSERT(0);
			}

			BitBlt(hdc, 0, 0, rcClient.right - rcClient.left, rcClient.bottom - rcClient.top, hMemDC, 0, 0, SRCCOPY);
			SelectObject(hMemDC, hOldBitmap);
			DeleteBitmap(hBitmap);
			DeleteDC(hMemDC);

			EndPaint(hWnd, &ps);
			return 0;	// 0 if handled
		}
		// else pass on
		break;
	}
	}
	return DefSubclassProc(hWnd, uMsg, wParam, lParam);
}

// pColors must remain allocated for the duration of the subclass
void InitDarkProgressBar(const HWND hWnd, const int subclassId, DarkProgressBarBrushes* pColors)
{
	if (pColors)
	{
		SetWindowSubclass(hWnd, ProgressBarSubclassProc, subclassId, reinterpret_cast<DWORD_PTR>(pColors));
	}
}

void DeinitDarkProgressBar(const HWND hWnd, const int subclassId)
{
	RemoveWindowSubclass(hWnd, ProgressBarSubclassProc, subclassId);
}

///////////////////////////////////////////////////////////////////
// dark menu bar drawing
// MIT license, see LICENSE
// Copyright(c) 2021 adzm / Adam D. Walling - adapted

static HTHEME g_menuTheme = nullptr;

void InitDarkMenuBar(const HWND hWnd, const int subclassId)
{
	SetWindowSubclass(hWnd, [](HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam, UINT_PTR uIdSubclass, DWORD_PTR dwRefData) -> LRESULT {
		// only owner dark menubar in dark mode, issue when mode is non-dark: https://github.com/jeremycollake/processlasso/issues/1339 and https://github.com/ysc3839/win32-darkmode/pull/17#issuecomment-845428664
		if (ShouldThisAppUseDarkModeNow())
		{
			switch (uMsg)
			{
			case WM_UAHDRAWMENU:
			{
				LIBCOMMON_DEBUG_PRINT(L"WM_UAHDRAWMENU");
				UAHMENU* pUDM = (UAHMENU*)lParam;
				RECT rc = { 0 };

				// get the menubar rect
				{
					MENUBARINFO mbi = { sizeof(mbi) };
					GetMenuBarInfo(hWnd, OBJID_MENU, 0, &mbi);

					RECT rcWindow;
					GetWindowRect(hWnd, &rcWindow);

					// the rcBar is offset by the window rect
					rc = mbi.rcBar;
					OffsetRect(&rc, -rcWindow.left, -rcWindow.top);

					rc.top -= 1;
				}

				if (!g_menuTheme) {
					g_menuTheme = OpenThemeData(hWnd, L"Menu");
					_ASSERT(g_menuTheme);
				}

				DrawThemeBackground(g_menuTheme, pUDM->hdc, MENU_POPUPITEM, MPI_NORMAL, &rc, nullptr);

				return 0;
			}
			case WM_UAHDRAWMENUITEM:
			{
				LIBCOMMON_DEBUG_PRINT(L"WM_UAHDRAWMENUITEM");
				UAHDRAWMENUITEM* pUDMI = (UAHDRAWMENUITEM*)lParam;

				// get the menu item string
				wchar_t menuString[256] = { 0 };
				MENUITEMINFO mii = { sizeof(mii), MIIM_STRING };
				{
					mii.dwTypeData = menuString;
					mii.cch = (sizeof(menuString) / 2) - 1;

					GetMenuItemInfo(pUDMI->um.hmenu, pUDMI->umi.iPosition, TRUE, &mii);
				}

				// get the item state for drawing

				DWORD dwFlags = DT_CENTER | DT_SINGLELINE | DT_VCENTER;

				int iTextStateID = 0;
				int iBackgroundStateID = 0;
				{
					if ((pUDMI->dis.itemState & ODS_INACTIVE) | (pUDMI->dis.itemState & ODS_DEFAULT)) {
						// normal display
						iTextStateID = MPI_NORMAL;
						iBackgroundStateID = MPI_NORMAL;
					}
					if (pUDMI->dis.itemState & ODS_HOTLIGHT) {
						// hot tracking
						iTextStateID = MPI_HOT;
						iBackgroundStateID = MPI_HOT;
					}
					if (pUDMI->dis.itemState & ODS_SELECTED) {
						// clicked -- MENU_POPUPITEM has no state for this, though MENU_BARITEM does
						iTextStateID = MPI_HOT;
						iBackgroundStateID = MPI_HOT;
					}
					if ((pUDMI->dis.itemState & ODS_GRAYED) || (pUDMI->dis.itemState & ODS_DISABLED)) {
						// disabled / grey text
						iTextStateID = MPI_DISABLED;
						iBackgroundStateID = MPI_DISABLED;
					}
					if (pUDMI->dis.itemState & ODS_NOACCEL) {
						dwFlags |= DT_HIDEPREFIX;
					}
				}

				if (!g_menuTheme) {
					g_menuTheme = OpenThemeData(hWnd, L"Menu");
					_ASSERT(g_menuTheme);
				}

				DrawThemeBackground(g_menuTheme, pUDMI->um.hdc, MENU_POPUPITEM, iBackgroundStateID, &pUDMI->dis.rcItem, nullptr);
				DrawThemeText(g_menuTheme, pUDMI->um.hdc, MENU_POPUPITEM, iTextStateID, menuString, mii.cch, dwFlags, 0, &pUDMI->dis.rcItem);

				return 0;
			}
			case WM_THEMECHANGED:
			{
				LIBCOMMON_DEBUG_PRINT(L"dark menu bar, WM_THEMECHANGED");
				if (g_menuTheme) {
					CloseThemeData(g_menuTheme);
					g_menuTheme = nullptr;
				}
				// propogate the message to DefSubclassProc
				break;
			}
			default:
				break;
			}
		}
		return DefSubclassProc(hWnd, uMsg, wParam, lParam);
		}, subclassId, 0);
}

// dark mode status bar code
// derived from Azdm's work at https://github.com/notepad-plus-plus/notepad-plus-plus/pull/9587/commits/ac8d27714add2e4f9eb0d79bbbefe60883689d6c

// this subclass not a lambda so it can be removed (RemoveWindowSubclass matches on combo of subclassId and callback address)
LRESULT CALLBACK StatusBarSubclassProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam, UINT_PTR uIdSubclass, DWORD_PTR dwRefData)
{
	auto pDarkInfo = (DARKSUBCLASSPAINTINFO*)dwRefData;
	switch (uMsg)
	{
	case WM_ERASEBKGND:
	{
		if (ShouldThisAppUseDarkModeNow())
		{
			RECT rc;
			GetClientRect(hWnd, &rc);
			FillRect((HDC)wParam, &rc, pDarkInfo->hBrushBackground);
			return TRUE;	// non-zero if handled
		}
		break;
	}
	case WM_PAINT:
	{
		if (ShouldThisAppUseDarkModeNow())
		{
			struct {
				int horizontal;
				int vertical;
				int between;
			} borders = { 0 };

			SendMessage(hWnd, SB_GETBORDERS, 0, (LPARAM)&borders);

			PAINTSTRUCT ps;
			HDC hdc = BeginPaint(hWnd, &ps);

			HFONT holdFont = (HFONT)::SelectObject(hdc, pDarkInfo->hFont);

			RECT rcClient;
			GetClientRect(hWnd, &rcClient);

			FillRect(hdc, &ps.rcPaint, pDarkInfo->hBrushBackground);

			int nParts = static_cast<int>(SendMessage(hWnd, SB_GETPARTS, 0, 0));
			std::vector<wchar_t> str;
			for (int i = 0; i < nParts; ++i)
			{
				RECT rcPart = { 0 };
				SendMessage(hWnd, SB_GETRECT, i, (LPARAM)&rcPart);
				RECT rcIntersect = { 0 };
				if (!IntersectRect(&rcIntersect, &rcPart, &ps.rcPaint))
				{
					continue;
				}

				RECT rcDivider = { rcPart.right - borders.vertical, rcPart.top, rcPart.right, rcPart.bottom };

				DWORD cchText = LOWORD(SendMessage(hWnd, SB_GETTEXTLENGTH, i, 0));
				str.resize(cchText + 1);
				SendMessage(hWnd, SB_GETTEXT, i, (LPARAM)str.data());
				SetBkMode(hdc, TRANSPARENT);
				SetTextColor(hdc, pDarkInfo->colorText);

				rcPart.left += borders.between;
				rcPart.right -= borders.vertical;

				DrawText(hdc, str.data(), cchText, &rcPart, DT_SINGLELINE | DT_VCENTER | DT_LEFT);

				FillRect(hdc, &rcDivider, pDarkInfo->hBrushDivider);
			}

			::SelectObject(hdc, holdFont);

			EndPaint(hWnd, &ps);
			return 0;		// 0 if processed
		}
		break;
	}
	}
	return DefSubclassProc(hWnd, uMsg, wParam, lParam);
}

void InitDarkStatusBar(const HWND hWnd, const int subclassId, DARKSUBCLASSPAINTINFO* pDarkInfo, bool bRemoveOnly)
{
	static DARKSUBCLASSPAINTINFO* pDarkPaintInfo = nullptr;
	if (pDarkPaintInfo)
	{
		// ensure we remove any prior instance
		RemoveWindowSubclass(hWnd, StatusBarSubclassProc, subclassId);

		DeleteObject(pDarkPaintInfo->hBrushBackground);
		DeleteObject(pDarkPaintInfo->hBrushDivider);
		delete pDarkPaintInfo;
		pDarkPaintInfo = nullptr;
	}

	if (!bRemoveOnly && pDarkInfo)
	{
		pDarkPaintInfo = new DARKSUBCLASSPAINTINFO{ pDarkInfo->hBrushBackground, pDarkInfo->hBrushDivider, pDarkInfo->hFont, pDarkInfo->colorText };
		SetWindowSubclass(hWnd, StatusBarSubclassProc, subclassId, reinterpret_cast<DWORD_PTR>(pDarkPaintInfo));
	}
}

void DeinitDarkStatusBar(const HWND hWnd, const int subclassId)
{
	InitDarkStatusBar(hWnd, subclassId, nullptr, true);
}

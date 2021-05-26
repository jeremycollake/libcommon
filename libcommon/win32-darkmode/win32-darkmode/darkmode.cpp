// from https://github.com/ysc3839/win32-darkmode.git
#include "pch.h"
#include <windows.h>
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


bool AllowDarkModeForWindow(HWND hWnd, bool allow)
{
	if (g_darkModeSupported)
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

#define LAST_VERIFIED_SUPPORTED_DARKMODE_WIN10_BUILD 21354
#define BUILD_ALLOWABLE_MARGIN 3000		// max build # over last known supported

bool CheckWin10BuildNumber(const DWORD dwBuildNumber, const bool bAllowFutureWin10Builds_Unsafe)
{	
	if (17763 <= dwBuildNumber && (dwBuildNumber <= (LAST_VERIFIED_SUPPORTED_DARKMODE_WIN10_BUILD + BUILD_ALLOWABLE_MARGIN) || bAllowFutureWin10Builds_Unsafe))
	{
		return true;
	}
	return false;
}

// returns false if Windows build doesn't match compatible
// intended for use to show user message box allowing over-ride
bool IsWindowsBuildNewerThanKnownDarkModeCompatible()
{
	fnRtlGetNtVersionNumbers RtlGetNtVersionNumbers = reinterpret_cast<fnRtlGetNtVersionNumbers>(GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "RtlGetNtVersionNumbers"));
	if (RtlGetNtVersionNumbers)
	{
		DWORD major, minor, build;
		RtlGetNtVersionNumbers(&major, &minor, &build);
		build &= ~0xF0000000;
		// ensure global is set, for safety on later maintenance
		g_buildNumber = build;
		if (major == 10 && minor == 0)
		{			
			if (build > (LAST_VERIFIED_SUPPORTED_DARKMODE_WIN10_BUILD + BUILD_ALLOWABLE_MARGIN))
			{
				return true;
			}
		}
		else if (major >= 10 && minor > 0)
		{
			return true;
		}
		else if (major > 10)
		{
			return true;
		}
	}
	return false;
}

void InitDarkMode(const bool bAllowFutureWin10Builds_Unsafe)
{	
	fnRtlGetNtVersionNumbers RtlGetNtVersionNumbers = reinterpret_cast<fnRtlGetNtVersionNumbers>(GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "RtlGetNtVersionNumbers"));
	if (RtlGetNtVersionNumbers)
	{
		DWORD major, minor;
		RtlGetNtVersionNumbers(&major, &minor, &g_buildNumber);
		g_buildNumber &= ~0xF0000000;
		if (major == 10 && minor == 0 && CheckWin10BuildNumber(g_buildNumber, bAllowFutureWin10Builds_Unsafe))
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

///////////////////////////////////////////////////////////////////
// dark menu bar drawing
// MIT license, see LICENSE
// Copyright(c) 2021 adzm / Adam D. Walling - adapted

static HTHEME g_menuTheme = nullptr;

// window messages related to menu bar drawing
#define WM_UAHDESTROYWINDOW    0x0090	// handled by DefWindowProc
#define WM_UAHDRAWMENU         0x0091	// lParam is UAHMENU
#define WM_UAHDRAWMENUITEM     0x0092	// lParam is UAHDRAWMENUITEM
#define WM_UAHINITMENU         0x0093	// handled by DefWindowProc
#define WM_UAHMEASUREMENUITEM  0x0094	// lParam is UAHMEASUREMENUITEM
#define WM_UAHNCPAINTMENUPOPUP 0x0095	// handled by DefWindowProc

// describes the sizes of the menu bar or menu item
typedef union tagUAHMENUITEMMETRICS
{
	// cx appears to be 14 / 0xE less than rcItem's width!
	// cy 0x14 seems stable, i wonder if it is 4 less than rcItem's height which is always 24 atm
	struct {
		DWORD cx;
		DWORD cy;
	} rgsizeBar[2];
	struct {
		DWORD cx;
		DWORD cy;
	} rgsizePopup[4];
} UAHMENUITEMMETRICS;

// not really used in our case but part of the other structures
typedef struct tagUAHMENUPOPUPMETRICS
{
	DWORD rgcx[4];
	DWORD fUpdateMaxWidths : 2; // from kernel symbols, padded to full dword
} UAHMENUPOPUPMETRICS;

// hmenu is the main window menu; hdc is the context to draw in
typedef struct tagUAHMENU
{
	HMENU hmenu;
	HDC hdc;
	DWORD dwFlags; // no idea what these mean, in my testing it's either 0x00000a00 or sometimes 0x00000a10
} UAHMENU;

// menu items are always referred to by iPosition here
typedef struct tagUAHMENUITEM
{
	int iPosition; // 0-based position of menu item in menubar
	UAHMENUITEMMETRICS umim;
	UAHMENUPOPUPMETRICS umpm;
} UAHMENUITEM;

// the DRAWITEMSTRUCT contains the states of the menu items, as well as
// the position index of the item in the menu, which is duplicated in
// the UAHMENUITEM's iPosition as well
typedef struct UAHDRAWMENUITEM
{
	DRAWITEMSTRUCT dis; // itemID looks uninitialized
	UAHMENU um;
	UAHMENUITEM umi;
} UAHDRAWMENUITEM;

// the MEASUREITEMSTRUCT is intended to be filled with the size of the item
// height appears to be ignored, but width can be modified
typedef struct tagUAHMEASUREMENUITEM
{
	MEASUREITEMSTRUCT mis;
	UAHMENU um;
	UAHMENUITEM umi;
} UAHMEASUREMENUITEM;

bool ShouldThisAppuseDarkModeNow()
{	
	return (g_darkModeSupported && g_darkModeEnabled
		&& _ShouldAppsUseDarkMode && _ShouldAppsUseDarkMode());	// see painting issue with this code when app mode is non-dark - https://github.com/jeremycollake/processlasso/issues/1339 and https://github.com/ysc3839/win32-darkmode/pull/17#issuecomment-845428664
}

void InitDarkMenuBar(const HWND hWnd, const int subclassId)
{
	SetWindowSubclass(hWnd, [](HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam, UINT_PTR uIdSubclass, DWORD_PTR dwRefData) -> LRESULT {
		if (ShouldThisAppuseDarkModeNow()) 
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
		if (!ShouldThisAppuseDarkModeNow())
		{
			return DefSubclassProc(hWnd, uMsg, wParam, lParam);
		}

		RECT rc;
		GetClientRect(hWnd, &rc);
		FillRect((HDC)wParam, &rc, pDarkInfo->hBrushBackground);
		return TRUE;
	}
	case WM_PAINT:
	{
		if (!ShouldThisAppuseDarkModeNow())
		{
			return DefSubclassProc(hWnd, uMsg, wParam, lParam);
		}

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
		return 0;
	}
	}
	return DefSubclassProc(hWnd, uMsg, wParam, lParam);
}

void InitDarkStatusBar(const HWND hWnd, const int subclassId, DARKSUBCLASSPAINTINFO *pDarkInfo, bool bRemoveOnly)
{	
	static DARKSUBCLASSPAINTINFO* pDarkPaintInfo = nullptr;
	if (pDarkPaintInfo)
	{
		DeleteObject(pDarkPaintInfo->hBrushBackground);
		DeleteObject(pDarkPaintInfo->hBrushDivider);
		delete pDarkPaintInfo;
		pDarkPaintInfo = nullptr;

		// ensure we remove any prior instance
		RemoveWindowSubclass(hWnd, StatusBarSubclassProc, subclassId);
	}		
	
	if (!bRemoveOnly && pDarkInfo)
	{
		pDarkPaintInfo = new DARKSUBCLASSPAINTINFO { pDarkInfo->hBrushBackground, pDarkInfo->hBrushDivider, pDarkInfo->hFont, pDarkInfo->colorText };
		SetWindowSubclass(hWnd, StatusBarSubclassProc, subclassId, reinterpret_cast<DWORD_PTR>(pDarkPaintInfo));
	}
}

void DeinitDarkStatusBar(const HWND hWnd, const int subclassId)
{
	InitDarkStatusBar(hWnd, subclassId, nullptr, true);
}

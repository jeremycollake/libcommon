#pragma once

struct SubclassInfo
{
	COLORREF colorHeaderText;
};

void InitListView(const HWND hListView, const int subclassId)
{
	HWND hHeader = ListView_GetHeader(hListView);

	SetWindowSubclass(hListView, [](HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam, UINT_PTR /*uIdSubclass*/, DWORD_PTR dwRefData) -> LRESULT {
		switch (uMsg)
		{
		case WM_NOTIFY:
		{
			if (reinterpret_cast<LPNMHDR>(lParam)->code == NM_CUSTOMDRAW)
			{
				LPNMCUSTOMDRAW nmcd = reinterpret_cast<LPNMCUSTOMDRAW>(lParam);
				switch (nmcd->dwDrawStage)
				{
				case CDDS_PREPAINT:
					return CDRF_NOTIFYITEMDRAW;
				case CDDS_ITEMPREPAINT:
				{
					// if app has dark mode enabled (tracked by g_darkModeEnabled), includes case where dark mode unsupported by OS
					// or Windows 10 dark mode supported and enabled at OS level, BUT we have dark mode locally turned off
					// and themes enabled (would be disabled in Windows 7 Classic where header background will be white)
					// (generally we want to avoid changing the header text color unless we have to)

					// complexity here is because Windows has varying behavior regarding the listview header background
					// and we support app dark mode even when OS doesn't have dark mode					
					// ... In Win7 non-classic theme, it'll go dark with our local dark mode
					// ... In Win10 w/o dark mode (e.g. 1809), it'll stay white
					// test any change against Win7, Win7-Classic, Win10-1809(nodarksupport), Win10-Light+AppDark, Win10-Dark+AppDark, Win10-Dark+AppLight, All-HighContrast					
					if ((g_darkModeEnabled ||
						(g_darkModeSupported && _ShouldAppsUseDarkMode() && !IsHighContrast()))
						&&
						IsThemeActive())
					{
						auto info = reinterpret_cast<SubclassInfo*>(dwRefData);
						SetTextColor(nmcd->hdc, info->colorHeaderText);
					}
					return CDRF_DODEFAULT;
				}
				}
			}
		}
		break;
		case WM_THEMECHANGED:
		{
			HWND hHeader = ListView_GetHeader(hWnd);
			if (g_darkModeSupported)
			{
				AllowDarkModeForWindow(hWnd, g_darkModeEnabled);
				AllowDarkModeForWindow(hHeader, g_darkModeEnabled);
			}

			// fail-safe to header text color same as listview			
			COLORREF color = ListView_GetTextColor(hWnd);

			// get listview header text color from theme				
			HTHEME hTheme = OpenThemeData(hHeader, L"Header");
			if (hTheme)
			{
				auto info = reinterpret_cast<SubclassInfo*>(dwRefData);
				if (S_OK == GetThemeColor(hTheme, HP_HEADERITEM, 0, TMT_TEXTCOLOR, &color))
				{
					//
				}
				CloseThemeData(hTheme);
			}

			auto info = reinterpret_cast<SubclassInfo*>(dwRefData);
			info->colorHeaderText = color;

			SendMessageW(hHeader, WM_THEMECHANGED, wParam, lParam);
			RedrawWindow(hWnd, nullptr, nullptr, RDW_FRAME | RDW_INVALIDATE);
		}
		break;
		case WM_DESTROY:
		{
			auto info = reinterpret_cast<SubclassInfo*>(dwRefData);
			delete info;
		}
		break;
		}
		return DefSubclassProc(hWnd, uMsg, wParam, lParam);
		}, subclassId, reinterpret_cast<DWORD_PTR>(new SubclassInfo{}));

	// Hide focus dots
	SendMessage(hListView, WM_CHANGEUISTATE, MAKELONG(UIS_SET, UISF_HIDEFOCUS), 0);

	SetWindowTheme(hHeader, L"ItemsView", nullptr); // DarkMode
	SetWindowTheme(hListView, L"ItemsView", nullptr); // DarkMode
}

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
					if (!g_darkModeSupported || (_ShouldAppsUseDarkMode() && !IsHighContrast()))
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
			auto info = reinterpret_cast<SubclassInfo*>(dwRefData);
			info->colorHeaderText = ListView_GetTextColor(hWnd);
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

	//ListView_SetExtendedListViewStyle(hListView, LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER | LVS_EX_HEADERDRAGDROP);

	// Hide focus dots
	SendMessage(hListView, WM_CHANGEUISTATE, MAKELONG(UIS_SET, UISF_HIDEFOCUS), 0);

	SetWindowTheme(hHeader, L"ItemsView", nullptr); // DarkMode
	SetWindowTheme(hListView, L"ItemsView", nullptr); // DarkMode
}

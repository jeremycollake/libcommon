// from https://github.com/ysc3839/win32-darkmode.git
#include "pch.h"
#include "DarkMode.h"
#include "ListViewUtil.h"

HINSTANCE g_hInst;
HWND g_hWndListView;

INT_PTR CALLBACK About(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam)
{
	UNREFERENCED_PARAMETER(lParam);
	constexpr COLORREF darkBkColor = 0x383838;
	constexpr COLORREF darkTextColor = 0xFFFFFF;
	static HBRUSH hbrBkgnd = nullptr;
	switch (message)
	{
	case WM_INITDIALOG:
	{
		if (g_darkModeSupported)
		{
			SetWindowTheme(GetDlgItem(hDlg, IDOK), L"Explorer", nullptr);
			SendMessageW(hDlg, WM_THEMECHANGED, 0, 0);
		}
		return (INT_PTR)TRUE;
	}
	case WM_COMMAND:
		if (LOWORD(wParam) == IDOK || LOWORD(wParam) == IDCANCEL)
		{
			EndDialog(hDlg, LOWORD(wParam));
			return (INT_PTR)TRUE;
		}
		break;
	case WM_CTLCOLORDLG:
	case WM_CTLCOLORSTATIC:
	{
		if (g_darkModeSupported && g_darkModeEnabled)
		{
			HDC hdc = reinterpret_cast<HDC>(wParam);
			SetTextColor(hdc, darkTextColor);
			SetBkColor(hdc, darkBkColor);
			if (!hbrBkgnd)
				hbrBkgnd = CreateSolidBrush(darkBkColor);
			return reinterpret_cast<INT_PTR>(hbrBkgnd);
		}
	}
	break;
	case WM_DESTROY:
		if (hbrBkgnd)
		{
			DeleteObject(hbrBkgnd);
			hbrBkgnd = nullptr;
		}
		break;
	case WM_SETTINGCHANGE:
	{
		if (g_darkModeSupported && IsColorSchemeChangeMessage(lParam))
			SendMessageW(hDlg, WM_THEMECHANGED, 0, 0);
	}
	break;
	case WM_THEMECHANGED:
	{
		if (g_darkModeSupported)
		{
			_AllowDarkModeForWindow(hDlg, g_darkModeEnabled);
			RefreshTitleBarThemeColor(hDlg);

			HWND hButton = GetDlgItem(hDlg, IDOK);
			_AllowDarkModeForWindow(hButton, g_darkModeEnabled);
			SendMessageW(hButton, WM_THEMECHANGED, 0, 0);

			UpdateWindow(hDlg);
		}
	}
	break;
	}
	return (INT_PTR)FALSE;
}

LRESULT CALLBACK WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
	switch (message)
	{
	case WM_CREATE:
	{
		if (g_darkModeSupported)
		{
			_AllowDarkModeForWindow(hWnd, true);
			RefreshTitleBarThemeColor(hWnd);
		}

		g_hWndListView = CreateWindowW(WC_LISTVIEWW, nullptr, WS_CHILD | WS_CLIPSIBLINGS | WS_VISIBLE | LVS_REPORT | LVS_SHOWSELALWAYS, 0, 0, 0, 0, hWnd, nullptr, g_hInst, nullptr);
		if (g_hWndListView)
		{
			InitListView(g_hWndListView);

			LVCOLUMNW lvc;
			lvc.mask = LVCF_WIDTH | LVCF_TEXT;
			lvc.cx = 300;
			lvc.pszText = const_cast<LPWSTR>(L"Test");
			for (int i = 0; i < 3; ++i)
				ListView_InsertColumn(g_hWndListView, i, &lvc);

			LVITEMW lvi;
			lvi.mask = LVIF_TEXT;
			lvi.iItem = INT_MAX;
			lvi.iSubItem = 0;
			lvi.pszText = const_cast<LPWSTR>(L"Test");
			for (int i = 0; i < 50; ++i)
			{
				int index = ListView_InsertItem(g_hWndListView, &lvi);
				for (int j = 1; j < 3; ++j)
					ListView_SetItemText(g_hWndListView, index, j, const_cast<LPWSTR>(L"Test"));
			}
		}
	}
	break;
	case WM_SIZE:
	{
		int clientWidth = GET_X_LPARAM(lParam), clientHeight = GET_Y_LPARAM(lParam);
		HDWP hDWP = BeginDeferWindowPos(1);
		if (hDWP != nullptr)
		{
			DeferWindowPos(hDWP, g_hWndListView, 0, 0, 0, clientWidth, clientHeight, SWP_NOZORDER);
			EndDeferWindowPos(hDWP);
		}
	}
	break;
	case WM_SETTINGCHANGE:
	{
		if (IsColorSchemeChangeMessage(lParam))
		{
			g_darkModeEnabled = _ShouldAppsUseDarkMode() && !IsHighContrast();

			RefreshTitleBarThemeColor(hWnd);
			SendMessageW(g_hWndListView, WM_THEMECHANGED, 0, 0);
		}
	}
	break;
	case WM_DESTROY:
		PostQuitMessage(0);
		break;
	default:
		return DefWindowProcW(hWnd, message, wParam, lParam);
	}
	return 0;
}


int APIENTRY wWinMain(_In_ HINSTANCE hInstance,
	_In_opt_ HINSTANCE hPrevInstance,
	_In_ LPWSTR    lpCmdLine,
	_In_ int       nCmdShow)
{
	UNREFERENCED_PARAMETER(hPrevInstance);
	UNREFERENCED_PARAMETER(lpCmdLine);

	InitDarkMode();

	if (!g_darkModeSupported)
	{
		int retval;
		TaskDialog(nullptr, hInstance, L"Error", nullptr, L"Darkmode is not supported.", TDCBF_OK_BUTTON, TD_ERROR_ICON, &retval);
	}
	
	return 0;
}

// NOTE: Perhaps belongs in win32-darkmdoe project, though we are now trying to isolate our changes and additions that project.

#include "pch.h"
#include "DarkModeDialogSubclass.h"
#include "win32-darkmode/win32-darkmode/DarkMode.h"

// DarkModeSubclassDialog
// - common message handler (subclass) for dialogs supporting dark mode. Should be called from the dialog's message handler, before other message processing.
// - vControlIDs contains the ids of controls that should be themed.
// if return is non-zero, then that should be returned by the dialog's message handler.
BOOL InitDarkModeDialogSubclass(const HWND hDlg, const int nSubclassId, const COLORREF colorForeground, const COLORREF colorBackground, const std::vector<int>& vControlIds)
{
	struct SubclassInfo
	{
		COLORREF colorForeground = 0;
		COLORREF colorBackground = 0;
		std::vector<int> vControlIds;
		HBRUSH hbrBkgnd = NULL;
	};

	auto* pInfo = new SubclassInfo;
	pInfo->colorForeground = colorForeground;
	pInfo->colorBackground = colorBackground;
	pInfo->vControlIds = vControlIds;

	bool bR = SetWindowSubclass(hDlg, [](HWND hDlg, UINT uMsg, WPARAM wParam, LPARAM lParam, UINT_PTR /*uIdSubclass*/, DWORD_PTR dwRefData) -> LRESULT {

		switch (uMsg)
		{
		case WM_DESTROY:
		{
			auto pInfo = reinterpret_cast<SubclassInfo*>(dwRefData);
			if (pInfo->hbrBkgnd)
			{
				DeleteObject(pInfo->hbrBkgnd);
				pInfo->hbrBkgnd = NULL;
			}
			delete pInfo;
			break;
		}
		case WM_CTLCOLORBTN:	// unused by pushbuttons due to multiple colors being used
		case WM_CTLCOLOREDIT:
		case WM_CTLCOLORSTATIC:
		case WM_CTLCOLORMSGBOX:
		case WM_CTLCOLORDLG:
		case WM_CTLCOLORLISTBOX:
		case WM_CTLCOLORSCROLLBAR:
		{
			auto pInfo = reinterpret_cast<SubclassInfo*>(dwRefData);
			if (g_darkModeSupported && g_darkModeEnabled)
			{
				HDC hdc = reinterpret_cast<HDC>(wParam);
				SetTextColor(hdc, pInfo->colorForeground);
				SetBkColor(hdc, pInfo->colorBackground);
				if (!pInfo->hbrBkgnd)
				{
					pInfo->hbrBkgnd = CreateSolidBrush(pInfo->colorBackground);
				}
				return reinterpret_cast<INT_PTR>(pInfo->hbrBkgnd);
			}
		}
		break;
		case WM_SETTINGCHANGE:
		{
			if (g_darkModeSupported && IsColorSchemeChangeMessage(lParam))
			{
				SendMessage(hDlg, WM_THEMECHANGED, 0, 0);
			}
		}
		break;
		case WM_THEMECHANGED:
		{
			if (g_darkModeSupported)
			{
				auto pInfo = reinterpret_cast<SubclassInfo*>(dwRefData);

				AllowDarkModeForWindowSafe(hDlg, g_darkModeEnabled);
				RefreshTitleBarThemeColor(hDlg);

				for (auto i : pInfo->vControlIds)
				{
					HWND hWndCtrl = GetDlgItem(hDlg, i);
					if (hWndCtrl)
					{
						AllowDarkModeForWindowSafe(hWndCtrl, g_darkModeEnabled);

						// for some types of buttons use Explorer theme
						bool bUseExplorerTheme = false;
						WCHAR wszClassName[256] = { 0 };
						if (GetClassName(hWndCtrl, wszClassName, _countof(wszClassName)))
						{
							if (_wcsicmp(wszClassName, L"Button") == 0)
							{
								DWORD dwButtonStyle = (GetWindowLong(hWndCtrl, GWL_STYLE) & BS_TYPEMASK);
								if (dwButtonStyle == BS_PUSHBUTTON || dwButtonStyle == BS_DEFPUSHBUTTON)
								{
									bUseExplorerTheme = true;
								}
							}
						}
						if (bUseExplorerTheme)
						{
							SetWindowTheme(hWndCtrl, L"Explorer", nullptr);
						}
						else
						{
							SetWindowTheme(hWndCtrl, L"", L"");	// empty string is treated differently than nullptr by SetWindowTheme						
						}

						SendMessage(hWndCtrl, WM_THEMECHANGED, 0, 0);
					}
				}

				RedrawWindow(hDlg, NULL, NULL, RDW_INVALIDATE | RDW_ALLCHILDREN);
			}
		}
		break;
		}
		return DefSubclassProc(hDlg, uMsg, wParam, lParam);
		},
		nSubclassId, reinterpret_cast<DWORD_PTR>(pInfo));

	// initiate first-time init
	SendMessage(hDlg, WM_THEMECHANGED, 0, 0);

	return bR;
}

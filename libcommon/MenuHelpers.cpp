#include "pch.h"
#include "MenuHelpers.h"

int GetMenuItemText(const HMENU hmenu, const int nID, const bool bByPos, __out std::wstring& wstrText)
{
	wstrText.clear();
	MENUITEMINFO mii = {};
	mii.cbSize = sizeof(mii);
	mii.fMask = MIIM_STRING;
	mii.dwTypeData = NULL;
	if (GetMenuItemInfo(hmenu, nID, bByPos ? TRUE : FALSE, &mii) && mii.cch)
	{
		// increment of mii.cch is pecularity of GetMenuItemInfo.
		// it returns required buffer size minus null terminator, but takes buffer size including null terminator.
		// see dwTypeData at https://learn.microsoft.com/en-us/windows/win32/api/winuser/ns-winuser-menuiteminfoa
		auto pwsz = new WCHAR[++mii.cch];
		if (pwsz)
		{
			mii.cbSize = sizeof(mii);
			mii.fMask = MIIM_STRING;
			mii.dwTypeData = pwsz;
			if (GetMenuItemInfo(hmenu, nID, bByPos ? TRUE : FALSE, &mii) && mii.cch)
			{
				wstrText = pwsz;
			}
			delete[] pwsz;
		}
	}
	return static_cast<int>(wstrText.length());
}
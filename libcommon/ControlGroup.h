#pragma once
// ControlGroup class
// for management of a group of dialog controls, e.g. to hide/show advanced options

#include <windows.h>
#include <vector>

class ControlGroup
{
	HWND m_hWndDialog;
	std::vector<int> m_vControlIds;
public:
	ControlGroup(const HWND hWndDialog, const std::vector<int>& vControlIds)
		: m_hWndDialog(hWndDialog), m_vControlIds(vControlIds) {}

	bool AreVisible()
	{
		if (!m_vControlIds.size())
		{
			return false;
		}
		_ASSERT(GetDlgItem(m_hWndDialog, m_vControlIds[0]));
		// just test the first control
		return IsWindowVisible(GetDlgItem(m_hWndDialog, m_vControlIds[0]));
	}
	bool SetVisible(const bool bVisible)
	{
		for (auto& i : m_vControlIds)
		{
			_ASSERT(GetDlgItem(m_hWndDialog, i));
			ShowWindow(GetDlgItem(m_hWndDialog, i), bVisible ? SW_NORMAL : SW_HIDE);
		}
		return AreVisible();
	}
};

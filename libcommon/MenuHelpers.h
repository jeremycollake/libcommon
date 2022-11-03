#pragma once
#include <windows.h>
#include <string>

int GetMenuItemText(const HMENU hmenu, const int nID, const bool bByPos, __out std::wstring& wstrText);
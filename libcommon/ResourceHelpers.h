#pragma once

#include <windows.h>
#include <string>

namespace ResourceHelpers
{
	std::wstring LoadResourceString(HMODULE hMod, UINT nId);
};

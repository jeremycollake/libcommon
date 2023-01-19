#include "pch.h"
#include "ResourceHelpers.h"

std::wstring ResourceHelpers::LoadResourceString(HMODULE hMod, UINT nId)
{
	// use a static buffer to avoid heap allocations
	WCHAR wszBuffer[1024] = { 0 };
	if (LoadString(hMod, nId, wszBuffer, _countof(wszBuffer)))
	{
		return wszBuffer;
	}
	return L"ERROR";
}

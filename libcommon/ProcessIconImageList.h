#pragma once

#include <windows.h>
#include <commctrl.h>
#include <shellapi.h>
#include <atlstr.h>
#include <map>
#include <set>
#include <mutex>
#include "DebugOutToggles.h"

// manages a process icon imagelist for a listview
class ProcessIconImageList
{
	const int _Imagelist_MaxSize = 256;
	HIMAGELIST hImageList;
	int nFailsafeIconIndex;
	std::mutex mutexMaps;
	std::map<const int, int> mapImgIdxToRefCount;
	std::map<const ATL::CString, int> mapFilenameToImgIdx;

	HICON GetIconForFilename(const WCHAR* pwszFilename)
	{
		if (!pwszFilename || !pwszFilename[0])
		{
			return NULL;
		}
		ICON_DEBUG_PRINT(L"Extracting icon for %s", pwszFilename);
		WORD wIndex = 0;
		// we must make a copy of the filename because ExtractAssociatedIcon can modify it
		WCHAR wszBuffer[MAX_PATH] = { 0 };
		wcscpy_s(wszBuffer, MAX_PATH, pwszFilename);
		return ExtractAssociatedIcon(GetModuleHandle(NULL), wszBuffer, &wIndex);
	}
public:
	ProcessIconImageList(const HICON hSuppliedFailsafeIcon = NULL)
	{
		// create imagelist and init with failsafe icon, for when an app icon is not avail
		hImageList = ImageList_Create(16, 16, ILC_COLOR32, 1, _Imagelist_MaxSize);
		_ASSERT(hImageList);

		// if failsafe icon was not supplied, load one from disk - svchost.exe
		HICON hSelectedFailsafeIcon = hSuppliedFailsafeIcon;
		if (NULL == hSelectedFailsafeIcon)
		{
			ICON_DEBUG_PRINT(L"No failsafe icon supplied, inferring icon");
			WCHAR wszServiceHostPath[MAX_PATH * 2] = { 0 };
			GetSystemDirectory(wszServiceHostPath, _countof(wszServiceHostPath));
			ATL::CString csSvcHostPath;
			csSvcHostPath.Format(L"%s\\svchost.exe", wszServiceHostPath);
			hSelectedFailsafeIcon = GetIconForFilename(csSvcHostPath);
		}

		// failsafe icon is now expected to exist
		_ASSERT(hSelectedFailsafeIcon);

		// add failsafe icon and 1 ref to it, since it will always persist
		nFailsafeIconIndex = ImageList_AddIcon(hImageList, hSelectedFailsafeIcon);
		_ASSERT(nFailsafeIconIndex == 0);

		mapFilenameToImgIdx[L""] = nFailsafeIconIndex;
		mapImgIdxToRefCount[nFailsafeIconIndex] = 1;

		// if failsafe icon was loaded above instead of supplied by caller, then needs destroyed (imagelist addition duplicates)
		if (hSelectedFailsafeIcon
			&& hSelectedFailsafeIcon != hSuppliedFailsafeIcon)
		{
			DestroyIcon(hSelectedFailsafeIcon);
		}
	}
	~ProcessIconImageList()
	{
		ImageList_Destroy(hImageList);
	}
	void SetFailsafeIcon(HICON hIcon)
	{
		_ASSERT(hIcon && hImageList);
		if (hIcon)
		{
			ImageList_ReplaceIcon(hImageList, nFailsafeIconIndex, hIcon);
		}
	}
	HIMAGELIST GetImageList()
	{
		return hImageList;
	}
	int GetImageListIndexForFilename(const WCHAR* pwszFile)
	{
		ATL::CString csFile = pwszFile;
		csFile.MakeLower();
		std::lock_guard<std::mutex> guard(mutexMaps);
		auto i = mapFilenameToImgIdx.find(csFile);
		if (i != mapFilenameToImgIdx.end())
		{
			return i->second;
		}
		// no prior call AddTrackedFilename (add/remove should always be called to keep list coherent)
		ICON_DEBUG_PRINT(L"\n ! WARNING: No icon for %s!", csFile);
		return 0;
	}
	void AddTrackedFilename(const WCHAR* pwszFile, bool* pbOutWentToDisk = NULL)
	{
		ATL::CString csFile = pwszFile;
		csFile.MakeLower();
		ICON_DEBUG_PRINT(L"\n AddTrackedFilename %s", csFile.GetString());
		std::lock_guard<std::mutex> guard(mutexMaps);

		auto i = mapFilenameToImgIdx.find(csFile);
		if (i != mapFilenameToImgIdx.end())
		{
			mapImgIdxToRefCount[i->second]++;
			if (pbOutWentToDisk) *pbOutWentToDisk = false;
			ICON_DEBUG_PRINT(L" - already have icon for %s, incrementing reference count of %d to %d", csFile, i->second, mapImgIdxToRefCount[i->second]);
		}
		else
		{
			ICON_DEBUG_PRINT(L"Loading icon for %s", csFile.GetString());
			// icon not loaded, load it and add to imagelist
			HICON hIcon = GetIconForFilename(pwszFile);
			int nImageIndex = nFailsafeIconIndex; // default to failsafe icon
			if (hIcon)
			{
				// icon loaded, add it to the imagelist and use
				nImageIndex = ImageList_AddIcon(hImageList, hIcon);
				// destroy icon since imagelist addition duplicates
				DestroyIcon(hIcon);
			}
			mapImgIdxToRefCount[nImageIndex]++;
			mapFilenameToImgIdx[csFile] = nImageIndex;
			if (pbOutWentToDisk) *pbOutWentToDisk = true;
			ICON_DEBUG_PRINT(L" - loaded new image index %d for %s", nImageIndex, csFile);
		}
		ICON_DEBUG_PRINT(L"icon map sizes: %d %d", mapImgIdxToRefCount.size(), mapFilenameToImgIdx.size());
		_ASSERT(mapImgIdxToRefCount.size() < 200 && mapImgIdxToRefCount.size() < 200);
		//DumpMaps();
	}
	void RemoveTrackedFilename(const WCHAR* pwszFile)
	{
		ATL::CString csFile = pwszFile;
		csFile.MakeLower();
		ICON_DEBUG_PRINT(L"\n RemoveTrackedFilename %s", csFile.GetString());
		std::lock_guard<std::mutex> guard(mutexMaps);

		// should be in the map
		// TODO: this gets signalled, processes not always in the map
		//_ASSERT(mapFilenameToImgIdx.find(csFile) != mapFilenameToImgIdx.end());

		int nImageIndex = mapFilenameToImgIdx[csFile];
		if (--mapImgIdxToRefCount[nImageIndex] == 0)
		{
			ICON_DEBUG_PRINT(L"Reference count for %d %s now 0. Removing!", nImageIndex, csFile);
			_ASSERT(nImageIndex != nFailsafeIconIndex);

			ImageList_Remove(hImageList, nImageIndex);

			mapFilenameToImgIdx.erase(csFile);

			// removing an icon from the imagelist changes the higher indices, so adjust prior refs
			for (auto& itFilesnameToIdx : mapFilenameToImgIdx)
			{
				_ASSERT(itFilesnameToIdx.second != nImageIndex);
				if (itFilesnameToIdx.second > nImageIndex)
				{
					mapFilenameToImgIdx[itFilesnameToIdx.first] = itFilesnameToIdx.second - 1;
				}
			}

			// erased when we copy the map
			//mapImgIdxToRefCount.erase(nImageIndex);

			// also decrements indices in reference counts if > deleted index
			std::map<const int, int> mapNewRefCounts;
			for (auto& i : mapImgIdxToRefCount)
			{
				if (i.first > nImageIndex)
				{
					mapNewRefCounts[i.first - 1] = mapImgIdxToRefCount[i.first];
				}
				else
				{
					mapNewRefCounts[i.first] = mapImgIdxToRefCount[i.first];
				}
			}
			mapImgIdxToRefCount = mapNewRefCounts;
		}
		else
		{
			ICON_DEBUG_PRINT(L"Reference count for %s now %d!", csFile, mapImgIdxToRefCount[nImageIndex]);
		}
		ICON_DEBUG_PRINT(L"icon map sizes: %d %d", mapImgIdxToRefCount.size(), mapFilenameToImgIdx.size());
		//DumpMaps();
	}
private:
	void DumpMaps()
	{
		ICON_DEBUG_PRINT(L"map dump --------------");
		for (auto i : mapFilenameToImgIdx)
		{
			ICON_DEBUG_PRINT(L" %s (%d) -> %d", i.first, i.second, mapImgIdxToRefCount[i.second]);
		}
		ICON_DEBUG_PRINT(L"map dump ends ---------------");
	}
};
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
	std::mutex mutexMaps;
	std::map<const int, int> mapImgIdxToRefCount;
	std::map<const ATL::CString, int> mapFilenameToImgIdx;	

	HICON GetIconForFilename(const WCHAR* pwszFilename)
	{
		ICON_DEBUG_PRINT(L"Extracting icon for %s", pwszFilename);
		WORD wIndex = 0;
		return ExtractAssociatedIcon(GetModuleHandle(NULL), const_cast<LPWSTR>(pwszFilename), &wIndex);		
	}
public:
	ProcessIconImageList(HICON hIconFailsafe=NULL)
	{
		hImageList = ImageList_Create(16, 16, ILC_COLOR32, 1, _Imagelist_MaxSize);
		_ASSERT(hImageList);
		bool bNeedsUnload = false;
		// index 0 will always be fail-safe icon		
		if (NULL == hIconFailsafe)
		{
			ICON_DEBUG_PRINT(L"No failsafe icon supplied, inferring icon");
			WCHAR wszServiceHostPath[MAX_PATH * 2] = { 0 };
			GetSystemDirectory(wszServiceHostPath, _countof(wszServiceHostPath));
			ATL::CString csSvcHostPath;
			csSvcHostPath.Format(L"%s\\svchost.exe", wszServiceHostPath);
			hIconFailsafe = GetIconForFilename(csSvcHostPath);	
			bNeedsUnload = true;
		}
		_ASSERT(hIconFailsafe);
		ImageList_AddIcon(hImageList, hIconFailsafe);
		if (hIconFailsafe && bNeedsUnload)
		{
			DestroyIcon(hIconFailsafe);
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
			ImageList_ReplaceIcon(hImageList, 0, hIcon);
		}
	}
	HIMAGELIST GetImageList()
	{
		return hImageList;
	}
	int GetImageListIndexForFilename(const WCHAR *pwszFile)
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
	void AddTrackedFilename(const WCHAR* pwszFile, bool *pbOutWentToDisk=NULL)
	{
		ATL::CString csFile = pwszFile;
		csFile.MakeLower();
		ICON_DEBUG_PRINT(L"\n AddTrackedFilename %s", csFile);
		std::lock_guard<std::mutex> guard(mutexMaps);

		auto i = mapFilenameToImgIdx.find(csFile);
		if (i != mapFilenameToImgIdx.end())
		{
			mapImgIdxToRefCount[i->second]++;
			if(pbOutWentToDisk) *pbOutWentToDisk = false;
			ICON_DEBUG_PRINT(L" - already have icon for %s, incrementing reference count to %d", csFile, mapImgIdxToRefCount[i->second]);			
		}
		else
		{
			// icon not loaded, load it and add to imagelist
			HICON hIcon = GetIconForFilename(pwszFile);
			int nImageIndex = 0;
			if (!hIcon)
			{
				// icon can't be loaded, use index 0 (fail-safe icon)				
				nImageIndex = 0;
			}
			else
			{
				nImageIndex = ImageList_AddIcon(hImageList, hIcon);				
				DestroyIcon(hIcon);					
				// to ensure exists before increment
				mapImgIdxToRefCount[nImageIndex] = 0;
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
		ICON_DEBUG_PRINT(L"\n RemoveTrackedFilename %s", csFile);
		std::lock_guard<std::mutex> guard(mutexMaps);

		// should be in the map
		_ASSERT(mapFilenameToImgIdx.find(csFile) != mapFilenameToImgIdx.end());

		int nImageIndex = mapFilenameToImgIdx[csFile];
		if (--mapImgIdxToRefCount[nImageIndex] == 0)
		{
			ICON_DEBUG_PRINT(L"Reference count for %d %s now 0. Removing!", nImageIndex, csFile);
			ImageList_Remove(hImageList, nImageIndex);
			ICON_DEBUG_PRINT(L"Adjusting indices > %d", nImageIndex);			

			mapFilenameToImgIdx.erase(csFile);

			// decrement mapFilenameToImgIdx if > deleted index
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
	private: void DumpMaps()
	{
		ICON_DEBUG_PRINT(L"map dump --------------");
		for (auto i : mapFilenameToImgIdx)
		{
			ICON_DEBUG_PRINT(L" %s (%d) -> %d", i.first, i.second, mapImgIdxToRefCount[i.second]);
		}
		ICON_DEBUG_PRINT(L"map dump ends ---------------");

	}
};
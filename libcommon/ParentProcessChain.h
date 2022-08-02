#pragma once
// ParentProcessChain
// tracks parent/child process relationships

#include <map>
#include <set>
#include <thread>
#include <mutex>
#include <algorithm>
#include <atlstr.h>
#include "libcommon.h"
#include <unordered_map>
#include "DebugOutToggles.h"

class ParentProcessChain
{
	static const DWORD INVALID_PID_VALUE = 0;	// use 0 (system idle process) instead of -1 to keep simple
	std::map<DWORD, ATL::CString> mapPIDtoBasenames;
	std::map<DWORD, DWORD> mapPIDtoParentPID;
	std::map<DWORD, std::set<DWORD>> mapPIDtoChildPIDs;
	std::map<DWORD, unsigned __int64> mapPIDToCreationTime;
	const int MaxDepth = 50;		// set a max depth in case of some errant circular resolution (should never occur, but.. e.g. 4->0 0->4)
	std::mutex processMaps;
public:
	size_t Size()
	{
		return mapPIDtoParentPID.size();
	}
	void AddPID(const DWORD dwPid, const WCHAR* pwszBasename, unsigned __int64 timeCreateChild, DWORD dwParentPid, unsigned __int64 timeCreateParentIfKnown = 0 /* optional, but preferred */)
	{
		std::lock_guard<std::mutex> lock(processMaps);
		_ASSERT(mapPIDtoParentPID.find(dwPid) == mapPIDtoParentPID.end());
		_ASSERT(mapPIDtoBasenames.find(dwPid) == mapPIDtoBasenames.end());
		mapPIDtoBasenames[dwPid] = pwszBasename;
		mapPIDtoBasenames[dwPid].MakeLower();

		// validate that our supposed parent PID is indeed older than our child PID
		// if not, then it may be a case of PID reuse, and the parent is no longer valid
		// see https://devblogs.microsoft.com/oldnewthing/?p=44313		
		if (0 == timeCreateParentIfKnown)
		{
			GetCreationTime(dwParentPid, timeCreateParentIfKnown);
		}
		if (timeCreateParentIfKnown
			&&
			timeCreateParentIfKnown > timeCreateChild)
		{
			dwParentPid = 0;
		}
		mapPIDtoParentPID[dwPid] = dwParentPid;

		// create an entry to track children of this process		
		mapPIDtoChildPIDs[dwPid] = {};
		// then add it to its parent's children set
		mapPIDtoChildPIDs[dwParentPid].insert(dwPid);

		// check for infinite growth (assumes process count < 4096)
		_ASSERT(mapPIDtoBasenames.size() < 4096 && mapPIDtoParentPID.size() < 4096
			&& mapPIDtoChildPIDs.size() < 4096 && mapPIDtoChildPIDs[dwParentPid].size() < 4096);
	}
	void RemovePID(const DWORD dwPid)
	{
		std::lock_guard<std::mutex> lock(processMaps);
		// erase from any occurrences in mapPIDToChildPIDs children sets
		_ASSERT(mapPIDtoParentPID.find(dwPid) != mapPIDtoParentPID.end());
		DWORD dwParentPID = mapPIDtoParentPID[dwPid];
		// parent may no longer exist
		if (mapPIDtoChildPIDs.find(dwParentPID) != mapPIDtoChildPIDs.end())
		{
			mapPIDtoChildPIDs[dwParentPID].erase(dwPid);
		}

		// and its parent entry
		if (mapPIDtoChildPIDs.find(dwPid) != mapPIDtoChildPIDs.end())
		{
			mapPIDtoChildPIDs.erase(dwPid);
		}

		// now erase from other maps
		mapPIDtoParentPID.erase(dwPid);
		mapPIDtoBasenames.erase(dwPid);
		mapPIDToCreationTime.erase(dwPid);
	}
	int GetNestLevelOfPID(const DWORD dwPID)
	{
		int nNestLevel = 0;
		for (DWORD dwParentPID = GetParent(dwPID); dwParentPID != INVALID_PID_VALUE; dwParentPID = GetParent(dwParentPID))
		{
			nNestLevel++;
		}
		return nNestLevel;
	}
	int SortHierarchically(std::vector<DWORD>& vecOrderedByHierarchyPIDs)
	{
		// build a sorted set of PIDs, with children immediately following their parent
		// this can be used, combined with nest level (number of children) to model a treeview
		vecOrderedByHierarchyPIDs.clear();

		// must start with sort by ascending nest level, then PID --- PID done beforehand by std::map
		std::vector<std::pair<int, std::set<DWORD>>> vPIDToChildPIDsSorted;
		{
			std::lock_guard<std::mutex> lock(processMaps);
			// sort ascending by nest level
			for (auto i = mapPIDtoChildPIDs.begin(); i != mapPIDtoChildPIDs.end(); ++i)
			{
				// insert before the next highest nest level				
				int nNestLevel = GetNestLevelOfPID(i->first);
				auto iInsert = vPIDToChildPIDsSorted.begin();
				for (; iInsert != vPIDToChildPIDsSorted.end(); ++iInsert)
				{
					// PERF NOTE: this computation (map traversal) done repetitively. 
					//  We could cache computed nest levels, or maintain a persistent map on AddPID.
					//  But, it is not evident that either would actually be advantageous, and map overhead may cause it to perform worse.
					//  Also, if we use persistent nest level storage, consider nest level may change when parent processes terminate.
					if (GetNestLevelOfPID(iInsert->first) > nNestLevel)
					{
						break;
					}
				}
				vPIDToChildPIDsSorted.insert(iInsert, *i);
			}
		}

		for (auto& i : vPIDToChildPIDsSorted)
		{
			// recursively insert all children			
			InsertChildPIDsToHierarchicalSort(vecOrderedByHierarchyPIDs, i.first);
		}

		return static_cast<int>(vecOrderedByHierarchyPIDs.size());
	}
private:
	int InsertChildPIDsToHierarchicalSort(std::vector<DWORD>& vecOrderedByHierarchyPIDs, const DWORD dwPID)
	{
		int nInsertedItems = 1;

		// only insert into vector if this PID doesn't already exist (will exist if it was a child of some other process)	
		if (std::find(vecOrderedByHierarchyPIDs.begin(), vecOrderedByHierarchyPIDs.end(), dwPID) == vecOrderedByHierarchyPIDs.end())
		{
			vecOrderedByHierarchyPIDs.push_back(dwPID);
		}

		// then recursively insert all children
		for (auto& i : mapPIDtoChildPIDs[dwPID])
		{
			nInsertedItems += InsertChildPIDsToHierarchicalSort(vecOrderedByHierarchyPIDs, i);
		}
		return nInsertedItems;
	}
public:
	DWORD GetParent(const DWORD dwPid, ATL::CString* pcsParentBasename = nullptr)
	{
		// don't acquire mutex due to SortHierarchically...GetNestLevelOfPID...GetParent
		//std::lock_guard<std::mutex> lock(processMaps);
		auto it = mapPIDtoParentPID.find(dwPid);
		if (it != mapPIDtoParentPID.end())
		{
			auto parentname = mapPIDtoBasenames.find(it->second);
			if (parentname != mapPIDtoBasenames.end())
			{
				if (pcsParentBasename)
				{
					*pcsParentBasename = parentname->second;
				}
				return it->second;
			}
		}
		return INVALID_PID_VALUE;
	}
	bool IsChildOf(const DWORD dwPid, const WCHAR* pwszParentBasename)
	{
		std::lock_guard<std::mutex> lock(processMaps);
		auto it = mapPIDtoParentPID.find(dwPid);
		int nDepth = 0;
		while (it != mapPIDtoParentPID.end())
		{
			DWORD dwParentPID = it->second;
			auto parentname = mapPIDtoBasenames.find(dwParentPID);
			if (parentname != mapPIDtoBasenames.end()
				&&
				!parentname->second.IsEmpty()
				&&
				wildcmpi(pwszParentBasename, mapPIDtoBasenames[dwParentPID]))
			{
				return true;
			}

			// we could have a circular dependency if we haven't checked for PID reuse of a parent that no longer exists.
			// since we do that check in AddPID, this shouldn't happen.
			nDepth++;
			//_ASSERT(nDepth <= MaxDepth);
			if (nDepth > MaxDepth)
			{
				return false;
			}
			DWORD dwLastChildPID = it->first;
			it = mapPIDtoParentPID.find(dwParentPID);
			if (it != mapPIDtoParentPID.end() && dwLastChildPID == it->second)
			{
				_ASSERT(0);
				LIBCOMMON_DEBUG_PRINT(L"Circular chain at %d:%s is child of %d:%s, depth now %d", it->first, mapPIDtoBasenames[it->first], it->second, parentname->second, nDepth);
				return false;
			}
		}
		return false;
	}
	bool GetCreationTime(DWORD dwPid, unsigned __int64& creationTime)
	{
		auto it = mapPIDToCreationTime.find(dwPid);
		if (it != mapPIDToCreationTime.end())
		{
			creationTime = it->second;
			return true;
		}
		HANDLE hProcess = OpenProcess(PROCESS_QUERY_INFORMATION, FALSE, dwPid);
		if (hProcess)
		{
			unsigned __int64 exitTime = 0, kernelTime = 0, userTime = 0;
			GetProcessTimes(hProcess, (FILETIME*)&creationTime, (FILETIME*)&exitTime, (FILETIME*)&kernelTime, (FILETIME*)&userTime);
			CloseHandle(hProcess);
			mapPIDToCreationTime[dwPid] = creationTime;
			return true;
		}
		else
		{
			creationTime = 0;
		}
		return false;
	}
};
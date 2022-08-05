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

// safety, don't use for beta or internal builds to so we can identify any cases
//#define CIRCULAR_CHAIN_SAFETIES_ENABLED

class ParentProcessChain
{
	static const DWORD INVALID_PID_VALUE = 0;	// use 0 (system idle process) instead of -1 to keep simple
	std::map<DWORD, ATL::CString> mapPIDtoBasenames;
	std::map<DWORD, DWORD> mapPIDtoParentPID;
	std::map<DWORD, std::set<DWORD>> mapPIDtoChildPIDs;
	std::map<DWORD, unsigned long long> mapPIDToCreationTime;
#ifdef CIRCULAR_CHAIN_SAFETIES_ENABLED	
	const int MAX_VALID_DEPTH = 50;		// set a max depth in case of some errant circular resolution (should never occur, but.. e.g. 4->0 0->4)
#endif
#ifdef DEBUG
	const int DEBUG_MAP_SIZE_MAX_CHECK = 4096;
#endif
	std::mutex processMaps;
public:
	size_t Size()
	{
		return mapPIDtoParentPID.size();
	}
	void AddPID(const DWORD dwPid, const WCHAR* pwszBasename, DWORD dwParentPid)
	{
		std::lock_guard<std::mutex> lock(processMaps);
		_ASSERT(mapPIDtoParentPID.find(dwPid) == mapPIDtoParentPID.end());
		_ASSERT(mapPIDtoBasenames.find(dwPid) == mapPIDtoBasenames.end());
		mapPIDtoBasenames[dwPid] = pwszBasename;
		mapPIDtoBasenames[dwPid].MakeLower();

		// get creation times to validate that it is the actual parent, and not a reused PID
		// see https://devblogs.microsoft.com/oldnewthing/?p=44313	
		unsigned long long timeCreateParent = 0;
		unsigned long long timeCreateChild = 0;
		RecordCreationTime(dwPid, timeCreateChild);
		RecordCreationTime(dwParentPid, timeCreateParent);
		if (timeCreateParent
			&&
			timeCreateParent > timeCreateChild)
		{
			dwParentPid = 0;
		}
		mapPIDtoParentPID[dwPid] = dwParentPid;

		// create an entry to track children of this process		
		mapPIDtoChildPIDs[dwPid] = {};
		// then add it to its parent's children set
		mapPIDtoChildPIDs[dwParentPid].insert(dwPid);

		// debug check for infinite map growth (leakage)
		_ASSERT(mapPIDtoBasenames.size() < DEBUG_MAP_SIZE_MAX_CHECK && mapPIDtoParentPID.size() < DEBUG_MAP_SIZE_MAX_CHECK
			&& mapPIDtoChildPIDs.size() < DEBUG_MAP_SIZE_MAX_CHECK && mapPIDtoChildPIDs[dwParentPid].size() < DEBUG_MAP_SIZE_MAX_CHECK
			&& mapPIDToCreationTime.size() < DEBUG_MAP_SIZE_MAX_CHECK);
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
		EraseCreationTime(dwPid);
	}
	int GetNestLevelOfPID(const DWORD dwPID)
	{
		int nNestLevel = 0;
		for (DWORD dwParentPID = GetParent(dwPID); dwParentPID != INVALID_PID_VALUE; dwParentPID = GetParent(dwParentPID))
		{
			nNestLevel++;
#ifdef CIRCULAR_CHAIN_SAFETIES_ENABLED
			if (nNestLevel > MAX_VALID_DEPTH)
			{
				LIBCOMMON_DEBUG_PRINT(L"Circular chain found, last at %u -> %u", dwPID, dwParentPID);
				_ASSERT(0);
				return 0;
			}
#endif
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
			_ASSERT(mapPIDToCreationTime.find(it->second) != mapPIDToCreationTime.end());
			if (mapPIDToCreationTime[dwPid] <= mapPIDToCreationTime[it->second])
			{
				LIBCOMMON_DEBUG_PRINT(L"Invalid parent PID for %u (%u, a reused PID - original parent terminated)", dwPid, it->second);
				return INVALID_PID_VALUE;
			}
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
	// check if PID a child of process matching given basename (wildcards accepted)
	bool IsChildOf(const DWORD dwPid, const WCHAR* pwszParentBasenameMatch)
	{
		std::lock_guard<std::mutex> lock(processMaps);
#ifdef CIRCULAR_CHAIN_SAFETIES_ENABLED		
		int nNestLevel = 0;
#endif
		for (DWORD dwParentPID = GetParent(dwPid); dwParentPID != INVALID_PID_VALUE; dwParentPID = GetParent(dwParentPID))
		{
			auto parentname = mapPIDtoBasenames.find(dwParentPID);
			if (parentname != mapPIDtoBasenames.end()
				&&
				!parentname->second.IsEmpty()
				&&
				wildcmpi(pwszParentBasenameMatch, parentname->second))
			{
				LIBCOMMON_DEBUG_PRINT(L"%u is child of %s", dwPid, pwszParentBasenameMatch);
				return true;
			}
#ifdef CIRCULAR_CHAIN_SAFETIES_ENABLED
			if (++nNestLevel > MAX_VALID_DEPTH)
			{
				LIBCOMMON_DEBUG_PRINT(L"Circular chain found, last at %u -> %u", dwPID, dwParentPID);
				_ASSERT(0);
				return false;
			}
#endif
		}
		return false;
	}
	bool RecordCreationTime(DWORD dwPid, unsigned long long& creationTime)
	{
		bool bR = false;
		auto it = mapPIDToCreationTime.find(dwPid);
		if (it != mapPIDToCreationTime.end())
		{
			creationTime = it->second;
			return true;
		}
		HANDLE hProcess = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, dwPid);
		if (hProcess)
		{
			unsigned long long exitTime = 0, kernelTime = 0, userTime = 0;
			GetProcessTimes(hProcess, (FILETIME*)&creationTime, (FILETIME*)&exitTime, (FILETIME*)&kernelTime, (FILETIME*)&userTime);
			CloseHandle(hProcess);
			bR = true;
		}
		else
		{
			// If we don't have access to get creation time (if ever?), use the current time so we are able to validate parent PIDs (checking for reuse of parent PID).
			// If a process we don't have query access to were to launch and immediately spawn children, it's possible this could cause an artificial orphaning (only in our app).
			// However, that case, if it ever were to occur, is less than the risk of a circular parent process chain in the case where we don't have any access to creation times, 
			//  when we therefore couldn't ever validate the parent PID.
			LIBCOMMON_DEBUG_PRINT(L"No limited query access to PID %u, can't get creation time!", dwPid);
			GetSystemTimeAsFileTime(reinterpret_cast<LPFILETIME>(&creationTime));
		}
		mapPIDToCreationTime[dwPid] = creationTime;
		return bR;
	}
	void EraseCreationTime(const DWORD dwPid)
	{
		_ASSERT(mapPIDToCreationTime.find(dwPid) != mapPIDToCreationTime.end());
		mapPIDToCreationTime.erase(dwPid);
	}
};
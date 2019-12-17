#pragma once

// tracks parent/child process relationships and can derive a child at any depth

#include <map>
#include <thread>
#include <mutex>
#include <atlstr.h>
#include "libcommon.h"

class ParentProcessChain
{
	const DWORD INVALID_PID_VALUE = 0;	// use 0 (system idle process) instead of -1 to keep simple
	std::map<DWORD, ATL::CString> mapPIDtoBasenames;
	std::map<DWORD, DWORD> mapPIDtoParentPID;
	std::map<DWORD, unsigned __int64> mapPIDToCreationTime;
	const int MaxDepth = 50;		// set a max depth in case of some errant circular resolution (should never occur, but.. e.g. 4->0 0->4)
	std::mutex processMaps;
public:
	void AddPID(const DWORD dwPid, const WCHAR* pwszBasename, unsigned __int64 timeCreateChild, const DWORD dwParentPid, unsigned __int64 timeCreateParentIfKnown=0 /* optional, but preferred */)
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
		if (0 == timeCreateChild
			||
			0 == timeCreateParentIfKnown)
		{
			// if failed to get either, make parent newer than child to invalidate
			//NEW_DEBUG_PRINT(L"Failed to get time for PID %d or PID %d", dwParentPid, dwPid);
			timeCreateParentIfKnown = 1;
			timeCreateChild = 0;
		}
		DWORD dwFixedParentPID = dwParentPid;
		if (timeCreateParentIfKnown > timeCreateChild)
		{
			dwFixedParentPID = 0;
		}
		mapPIDtoParentPID[dwPid] = dwFixedParentPID;
		
		// check for infinite growth (assumes process count < 4096)
		_ASSERT(mapPIDtoBasenames.size() < 4096 && mapPIDtoParentPID.size() < 4096);
	}
	void RemovePID(const DWORD dwPid)
	{
		std::lock_guard<std::mutex> lock(processMaps);
		mapPIDtoParentPID.erase(dwPid);
		mapPIDtoBasenames.erase(dwPid);
		mapPIDToCreationTime.erase(dwPid);
	}
	DWORD GetParent(const DWORD dwPid, ATL::CString& csParentBasename)
	{
		std::lock_guard<std::mutex> lock(processMaps);
		auto it = mapPIDtoParentPID.find(dwPid);
		if (it != mapPIDtoParentPID.end())
		{
			auto parentname = mapPIDtoBasenames.find(it->second);
			if (parentname != mapPIDtoBasenames.end())
			{
				csParentBasename = parentname->second;
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

			// we CAN have a situation where there is a circular dependency if we haven't properly checked for PID ruse in the case of a parent that long ago terminated.
			// we must check to ensure the parent process creation time is indeed older than the child
			nDepth++;
			//_ASSERT(nDepth <= MaxDepth);
			if (nDepth > MaxDepth)
			{
				return false;
			}			
			DWORD dwLastChildPID = it->first;
			it = mapPIDtoParentPID.find(dwParentPID);
			if (it!=mapPIDtoParentPID.end() && dwLastChildPID == it->second)
			{
				_ASSERT(0);
				//NEW_DEBUG_PRINT(L"Circular chain at %d:%s is child of %d:%s, depth now %d", it->first, mapPIDtoBasenames[it->first], it->second, parentname->second, nDepth);
				return false;
			}
		}
		return false;
	}	
	bool GetCreationTime(DWORD dwPid, unsigned __int64& creationTime)
	{		
		auto it = mapPIDToCreationTime.find(dwPid);
		if(it!=mapPIDToCreationTime.end())
		{
			creationTime = it->second;
			return true;
		}
		HANDLE hProcess = OpenProcess(PROCESS_QUERY_INFORMATION, FALSE, dwPid);
		if (hProcess)
		{
			//DEBUG_PRINT(L"Opened PID %d for creation time check", dwPid);
			unsigned __int64 exitTime = 0, kernelTime = 0, userTime = 0;
			GetProcessTimes(hProcess, (FILETIME*)& creationTime, (FILETIME*)& exitTime, (FILETIME*)& kernelTime, (FILETIME*)& userTime);
			CloseHandle(hProcess);
			mapPIDToCreationTime[dwPid] = creationTime;
			return true;
		}
		else
		{
			//DEBUG_PRINT(L"Could not open PID %d for creation time check", dwPid);
			creationTime = 0;			
		}
		return false;
	}
};
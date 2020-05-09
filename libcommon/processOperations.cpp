#include "pch.h"
#include "processOperations.h"

bool processOperations::SetAffinityMask(const unsigned long pid, const unsigned long long bitMask, const int group)
{
	HANDLE hProcess = OpenProcess(PROCESS_SET_INFORMATION, FALSE, pid);
	if (NULL == hProcess) return false;
	// if no process group specified, use default group and standard API
	bool bR = false;
	if (group == NO_PROCESSOR_GROUP)
	{
		bR = ::SetProcessAffinityMask(hProcess, bitMask) ? true : false;
	}
	else
	{
		// if process is multi-group, first try to move its threads to the target group
		if (IsMultiGroupProcess(pid))
		{
			SetGroupAffinityForAllThreads(pid, group, bitMask);
		}
		// must call SetProcessGroupAffinity as well to change default group (not that it works for new threads)
		// use NT native API to set group and affinity
		bR = SetProcessGroupAffinity(pid, group, bitMask);					
		// backup safety, unnecessary
		::SetProcessAffinityMask(hProcess, bitMask);
	}
	CloseHandle(hProcess);
	return bR;
}

bool processOperations::SetGroupAffinityForAllThreads(const unsigned long pid, const int group, const unsigned long long bitMask)
{
	//LIBCOMMON_DEBUG_PRINT(L"SetGroupAffinityForAllThreads");
	HANDLE hSnapshot = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
	if (hSnapshot == INVALID_HANDLE_VALUE)
	{		
		LIBCOMMON_DEBUG_PRINT(L"ERROR: Snapshot failure");
		return false;
	}

	bool bR = true;	
	THREADENTRY32 te32;
	te32.dwSize = sizeof(THREADENTRY32);
	if (!Thread32First(hSnapshot, &te32))
	{
		CloseHandle(hSnapshot);
		return false;
	}
	do
	{
		if (te32.th32OwnerProcessID == pid)
		{
			//LIBCOMMON_DEBUG_PRINT(L"Adjusting affinity of TID %u", te32.th32ThreadID);
			HANDLE hThread = OpenThread(THREAD_SET_INFORMATION | THREAD_QUERY_INFORMATION, FALSE, te32.th32ThreadID);			
			if (hThread)
			{
				GROUP_AFFINITY groupAff, prevGroupAff;
				groupAff.Group = group;
				groupAff.Mask = bitMask;
				if (false == SetThreadGroupAffinity(hThread, &groupAff, &prevGroupAff))
				{
					LIBCOMMON_DEBUG_PRINT(L"ERROR: Setting group affinity of TID %u", te32.th32ThreadID);
					// don't return error just because we failed on some threads ...
				}
				else
				{
					// don't return error if we were succesful in *any* of the TIDs of this process
					bR = true;
				}
				CloseHandle(hThread);
			}
			else
			{
				LIBCOMMON_DEBUG_PRINT(L"ERROR: Opening thread TID %u", te32.th32ThreadID);
			}
		}		
	} while (Thread32Next(hSnapshot, &te32));
	CloseHandle(hSnapshot);
	return bR;
}

bool processOperations::GetAffinityMask(const unsigned long pid, unsigned long long& bitMask)
{
	// do NOT call GetGroupAffinity here, circular dependency
	HANDLE hProcess = OpenProcess(PROCESS_QUERY_INFORMATION, FALSE, pid);
	if (NULL == hProcess) hProcess = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
	if (NULL == hProcess) return false;
	// if no process group specified, use default group and standard API
	unsigned long long sysAffinity = 0;
	bool bR = ::GetProcessAffinityMask(hProcess, reinterpret_cast<PDWORD_PTR>(&bitMask), reinterpret_cast<PDWORD_PTR>(&sysAffinity)) ? true : false;
	CloseHandle(hProcess);
	return bR;
}

bool processOperations::GetGroupAffinity(const unsigned long pid, GroupAffinity& aff)
{
	// we can either use NT native API to get the default group, or enumerate groups and assume first is the default
	// for now use standard documented API
	std::vector<USHORT> vGroups;
	bool bR=GetAffinityMask(pid, aff.mask);
	if (GetActiveProcessorGroupCount() > 1)
	{
		if (GetProcessProcessorGroups(pid, vGroups) == 0)
		{
			_ASSERT(0);
			return false;
		}
		aff.nGroupId = vGroups[0];
	}	
	else
	{
		aff.nGroupId = NO_PROCESSOR_GROUP;
	}
	return bR;
}

bool processOperations::IsMultiGroupProcess(const unsigned long pid)
{
	std::vector<USHORT> vuGroups;
	if (GetProcessProcessorGroups(pid, vuGroups) > 1)
	{
		return true;
	}
	return false;
}

size_t processOperations::GetProcessProcessorGroups(const unsigned long pid, std::vector<USHORT> &vGroups)
{
	vGroups.clear();
	
	HANDLE hProcess = OpenProcess(PROCESS_QUERY_INFORMATION, FALSE, pid);
	if (NULL==hProcess) hProcess=OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
	if (NULL == hProcess) return 0;
	
	USHORT GroupCount = 0;
	PUSHORT pGroupArray = nullptr;
	// first get the buffer size
	if (FALSE != GetProcessGroupAffinity(hProcess, &GroupCount, NULL))
	{
		// we got a sucessful return with an empty output buffer!		
		CloseHandle(hProcess);
		return 0;
	}
	if (ERROR_INSUFFICIENT_BUFFER != GetLastError())
	{		
		CloseHandle(hProcess);
		return 0;
	}
	pGroupArray = new USHORT[GroupCount];
	if (FALSE == GetProcessGroupAffinity(hProcess, &GroupCount, pGroupArray))
	{		
		delete pGroupArray;
		CloseHandle(hProcess);
		return 0;
	}
	// got the groups, populate vector and return
	for (USHORT nI = 0; nI < GroupCount; nI++)
	{
		vGroups.push_back(pGroupArray[nI]);
	}
	delete pGroupArray;

	CloseHandle(hProcess);
	return vGroups.size();
}

bool processOperations::SetPriorityBoost(const unsigned long pid, const bool bPriorityBoostEnabled)
{
	HANDLE hProcess = OpenProcess(PROCESS_SET_INFORMATION, FALSE, pid);
	if (NULL == hProcess) return false;
	bool bR = ::SetProcessPriorityBoost(hProcess, bPriorityBoostEnabled ? FALSE : TRUE) ? true : false;
	CloseHandle(hProcess);	
	return bR;
}

bool processOperations::SetPriorityClass(const unsigned long pid, const long priorityClass)
{	
	HANDLE hProcess = OpenProcess(PROCESS_SET_INFORMATION, FALSE, pid);
	if (NULL == hProcess) return false;
	bool bR = ::SetPriorityClass(hProcess, priorityClass) ? true : false;
	CloseHandle(hProcess);
	return bR;
}

bool processOperations::TrimWorkingSetSize(const unsigned long pid)
{
	HANDLE hProcess = OpenProcess(PROCESS_SET_QUOTA, FALSE, pid);
	if (NULL == hProcess) return false;
	bool bR = ::SetProcessWorkingSetSize(hProcess, -1, -1) ? true : false;
	CloseHandle(hProcess);
	return bR;
}

// returns true if process terminates, or is already terminated
bool processOperations::Terminate(const unsigned long pid, const unsigned long exitCode)
{
	HANDLE hProcess = OpenProcess(SYNCHRONIZE | PROCESS_TERMINATE, FALSE, pid);
	if (NULL == hProcess) return true;  // return successful	
	::TerminateProcess(hProcess, exitCode);
	bool bR = (WaitForSingleObject(hProcess, 0) == WAIT_OBJECT_0) ? true : false;
	CloseHandle(hProcess);
	return bR;
}

bool processOperations::SuspendProcess(const unsigned long pid)
{
	HANDLE hProcess = OpenProcess(PROCESS_SUSPEND_RESUME, FALSE, pid);
	if (NULL == hProcess) return false;
	auto pfnNtSuspendProcess = reinterpret_cast<NtSuspendProcess>(GetProcAddress(
		GetModuleHandle(L"ntdll"), "NtSuspendProcess"));
	if (!pfnNtSuspendProcess) return false;
	bool bR = pfnNtSuspendProcess(hProcess) ? false : true;
	CloseHandle(hProcess);
	return bR;
}

bool processOperations::ResumeProcess(const unsigned long pid)
{
	HANDLE hProcess = OpenProcess(PROCESS_SUSPEND_RESUME, FALSE, pid);
	if (NULL == hProcess) return false;
	auto pfnNtResumeProcess = reinterpret_cast<NtResumeProcess>(GetProcAddress(
		GetModuleHandle(L"ntdll"), "NtResumeProcess"));
	if (NULL == pfnNtResumeProcess) return false;
	bool bR = pfnNtResumeProcess(hProcess) ? false : true;
	CloseHandle(hProcess);
	return bR;
}

bool processOperations::SetProcessGroupAffinity(const unsigned long pid, int nProcessorGroup, unsigned long long maskAff)
{
	_ASSERT(nProcessorGroup >= 0 && nProcessorGroup < 256);

	unsigned short nActiveGroupCount = static_cast<unsigned short>(GetActiveProcessorGroupCount());
	if (nActiveGroupCount < 2
		||
		nProcessorGroup>256
		||
		// ONLY use this API if we are setting the processor group, else use standard SetProcessAffinityMask
		nProcessorGroup < 0)
	{
		return false;
	}
	
	using fnZwSetInformationProcess = NTSTATUS(NTAPI*)(__in HANDLE, __in ULONG, __in PVOID, __in ULONG);
	static fnZwSetInformationProcess _ZwSetInformationProcess = nullptr;	
	if (!_ZwSetInformationProcess)
	{
		_ZwSetInformationProcess = reinterpret_cast<fnZwSetInformationProcess>(GetProcAddress(GetModuleHandle(L"ntdll.dll"), "ZwSetInformationProcess"));		
	}
	_ASSERT(_ZwSetInformationProcess);
	if (!_ZwSetInformationProcess)
	{		
		return false;
	}

	HANDLE hProcess = OpenProcess(PROCESS_SET_INFORMATION, FALSE, pid);

	// API monitor in Win10 19577 shows correct size seems to be 12 based on API monitor (see _research folder)
	// but it is clearly 16 by data structure definition and error codes

	GROUP_AFFINITY group_affinity = {};
	group_affinity.Group = nProcessorGroup;
	group_affinity.Mask = maskAff;
	NTSTATUS ntstatus = _ZwSetInformationProcess(hProcess, 0x15, &group_affinity, sizeof(group_affinity));

	CloseHandle(hProcess);
	return (ntstatus == 0) ? true : false;
}
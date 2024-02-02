#include "pch.h"
#include "ProcessOperations.h"

using fnGetProcessInformation = BOOL(WINAPI*)(HANDLE, PROCESS_INFORMATION_CLASS, LPVOID, DWORD);
using fnSetProcessInformation = BOOL(WINAPI*)(HANDLE, PROCESS_INFORMATION_CLASS, LPVOID, DWORD);

// globals so we don't have to import APIs every instantiation
bool g_bImportAPIAttempted = false;
fnGetProcessInformation	_GetProcessInformation = nullptr;	// Win8+
fnSetProcessInformation _SetProcessInformation = nullptr;	// Win8+

void ProcessOperations::ImportAPIs()
{
	if (!g_bImportAPIAttempted)
	{
		g_bImportAPIAttempted = true;
		HMODULE hKernelDll = GetModuleHandle(L"kernel32.dll");
		if (hKernelDll)
		{
			_GetProcessInformation = (fnGetProcessInformation)GetProcAddress(hKernelDll, "GetProcessInformation");
			_SetProcessInformation = (fnSetProcessInformation)GetProcAddress(hKernelDll, "SetProcessInformation");
		}
	}
	_ASSERT(!IsWindows10OrGreater() || (_GetProcessInformation && _SetProcessInformation));
}

HANDLE ProcessOperations::OpenQueryHandle(const unsigned long pid)
{
	HANDLE hProcess = OpenProcess(PROCESS_QUERY_INFORMATION, FALSE, pid);
	if (NULL == hProcess) hProcess = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
	return hProcess;
}

// this is used when CPU core count changes (e.g. as the result of UEFI adjustment) but we still want to apply CPU affinity rules
unsigned long long ProcessOperations::LimitAffinityToInstalledCPUCores(unsigned long long bitMask)
{
	// bitwise AND by available CPU cores		
	_ASSERT(sysInfo.dwActiveProcessorMask);
	unsigned long long newBitMask = bitMask;
	if (sysInfo.dwActiveProcessorMask)
	{
		newBitMask &= sysInfo.dwActiveProcessorMask;
	}
	return newBitMask;
}

bool ProcessOperations::SetAffinityMask(const unsigned long pid, const unsigned long long bitMask, const int group)
{
	HANDLE hProcess = OpenProcess(PROCESS_SET_INFORMATION, FALSE, pid);
	if (NULL == hProcess) return false;
	// if no process group specified, use default group and standard API
	bool bR = false;
	if (group == NO_PROCESSOR_GROUP)
	{
		// TODO: this can (should) also be applied when group affinity used - but be precise about available CPUs in a group, assume bitmask could differ between groups
		unsigned long long newBitMask = LimitAffinityToInstalledCPUCores(bitMask);
		bR = ::SetProcessAffinityMask(hProcess, newBitMask) ? true : false;
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

bool ProcessOperations::SetGroupAffinityForAllThreads(const unsigned long pid, const int group, const unsigned long long bitMask)
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

bool ProcessOperations::GetAffinityMask(const unsigned long pid, unsigned long long& bitMask)
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

bool ProcessOperations::GetGroupAffinity(const unsigned long pid, GroupAffinity& aff)
{
	// we can either use NT native API to get the default group, or enumerate groups and assume first is the default
	// for now use standard documented API
	std::vector<USHORT> vGroups;
	bool bR = GetAffinityMask(pid, aff.mask);
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

bool ProcessOperations::IsMultiGroupProcess(const unsigned long pid)
{
	std::vector<USHORT> vuGroups;
	if (GetProcessProcessorGroups(pid, vuGroups) > 1)
	{
		return true;
	}
	return false;
}

size_t ProcessOperations::GetProcessProcessorGroups(const unsigned long pid, std::vector<USHORT>& vGroups)
{
	vGroups.clear();

	HANDLE hProcess = OpenProcess(PROCESS_QUERY_INFORMATION, FALSE, pid);
	if (NULL == hProcess) hProcess = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
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

bool ProcessOperations::SetPriorityBoost(const unsigned long pid, const bool bPriorityBoostEnabled)
{
	HANDLE hProcess = OpenProcess(PROCESS_SET_INFORMATION, FALSE, pid);
	if (NULL == hProcess) return false;
	bool bR = ::SetProcessPriorityBoost(hProcess, bPriorityBoostEnabled ? FALSE : TRUE) ? true : false;
	CloseHandle(hProcess);
	return bR;
}

unsigned long ProcessOperations::GetPriorityClass(const unsigned long pid)
{
	HANDLE hProcess = OpenQueryHandle(pid);
	if (NULL == hProcess) return static_cast<unsigned long>(-1);

	unsigned long nRet = ::GetPriorityClass(hProcess);
	CloseHandle(hProcess);
	return nRet;
}

bool ProcessOperations::SetPriorityClass(const unsigned long pid, const long priorityClass)
{
	HANDLE hProcess = OpenProcess(PROCESS_SET_INFORMATION, FALSE, pid);
	if (NULL == hProcess) return false;
	bool bR = ::SetPriorityClass(hProcess, priorityClass) ? true : false;
	CloseHandle(hProcess);
	return bR;
}

bool ProcessOperations::TrimWorkingSetSize(const unsigned long pid)
{
	HANDLE hProcess = OpenProcess(PROCESS_SET_QUOTA, FALSE, pid);
	if (NULL == hProcess) return false;
	bool bR = ::SetProcessWorkingSetSize(hProcess, -1, -1) ? true : false;
	CloseHandle(hProcess);
	return bR;
}

// returns true if process terminates, or is already terminated
bool ProcessOperations::Terminate(const unsigned long pid, const unsigned long exitCode)
{
	HANDLE hProcess = OpenProcess(SYNCHRONIZE | PROCESS_TERMINATE, FALSE, pid);
	if (NULL == hProcess) return true;  // return successful	
	bool bR = ::TerminateProcess(hProcess, exitCode) ? true : false;
	if (WaitForSingleObject(hProcess, 0) == WAIT_OBJECT_0)
	{
		bR = true;
	}
	CloseHandle(hProcess);
	return bR;
}

bool ProcessOperations::SuspendProcess(const unsigned long pid)
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

bool ProcessOperations::ResumeProcess(const unsigned long pid)
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

bool ProcessOperations::SetProcessGroupAffinity(const unsigned long pid, int nProcessorGroup, unsigned long long maskAff)
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

unsigned long ProcessOperations::GetParentOfProcess(const unsigned long pid)
{
	HANDLE hSnapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, pid);
	if (INVALID_HANDLE_VALUE == hSnapshot)
	{
		return 0;
	}
	unsigned long retParentPid = 0;
	PROCESSENTRY32 procEntry = {};
	procEntry.dwSize = sizeof(procEntry);
	if (!Process32First(hSnapshot, &procEntry))
	{
		return 0;
	}
	do
	{
		if (procEntry.th32ProcessID == pid)
		{
			retParentPid = procEntry.th32ParentProcessID;
			break;
		}

	} while (Process32Next(hSnapshot, &procEntry));

	CloseHandle(hSnapshot);
	return retParentPid;
}

bool ProcessOperations::GetLogonFromToken(HANDLE hToken, ATL::CString& csUser, ATL::CString& csDomain)
{
	csUser.Empty();
	csDomain.Empty();

	DWORD dwSizeUser = UNLEN * 2;
	DWORD dwSizeDomain = UNLEN * 2;	// should use DNLEN, but so small
	WCHAR lpName[UNLEN * 2] = { 0 };
	WCHAR lpDomain[UNLEN * 2] = { 0 };
	SID_NAME_USE SidType;

	bool bSuccess = false;
	DWORD dwLength = 0;
	PTOKEN_USER ptu = NULL;

	if (!hToken)
	{
		return false;
	}
	if (!GetTokenInformation(hToken, TokenUser, (LPVOID)ptu, 0, &dwLength))
	{
		if (GetLastError() != ERROR_INSUFFICIENT_BUFFER)
		{
			return false;
		}

		ptu = (PTOKEN_USER)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, dwLength);
		if (!ptu)
		{
			return false;
		}
	}

	if (!GetTokenInformation(hToken, TokenUser, (LPVOID)ptu, dwLength, &dwLength))
	{
		if (ptu)
		{
			HeapFree(GetProcessHeap(), 0, (LPVOID)ptu);
		}
		return false;
	}

	if (LookupAccountSid(NULL, ptu->User.Sid, lpName, &dwSizeUser, lpDomain, &dwSizeDomain, &SidType))
	{
		csUser = lpName;
		csDomain = lpDomain;
		bSuccess = true;
	}

	if (ptu)
	{
		HeapFree(GetProcessHeap(), 0, (LPVOID)ptu);
	}
	return bSuccess;
}

bool ProcessOperations::GetUserNameByToken(const unsigned long pid, ATL::CString& csUser, ATL::CString& csDomain)
{
	HANDLE hProcess = NULL;
	hProcess = OpenProcess(PROCESS_QUERY_INFORMATION, FALSE, pid);
	if (!hProcess)
	{
		hProcess = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
	}
	if (!hProcess)
	{
		return false;
	}

	HANDLE hToken = NULL;
	if (FALSE == OpenProcessToken(hProcess, TOKEN_QUERY, &hToken))
	{
		return false;
	}
	bool bR = GetLogonFromToken(hToken, csUser, csDomain);

	CloseHandle(hToken);
	return bR;
}


bool ProcessOperations::GetUserNameForProcess(const unsigned long pid, ATL::CString& csUser, ATL::CString& csDomain)
{
	csUser.Empty();
	csDomain.Empty();
	if (GetUserNameByToken(pid, csUser, csDomain))
	{
		return csUser.GetLength();
	}
	return 0;
}


HWND ProcessOperations::GetLikelyPrimaryWindow(const unsigned long pid)
{
	struct WindowFinderData {
		unsigned long pid;
		HWND hWndLikelyPrimary;
	};
	WindowFinderData data = { pid, NULL };
	EnumWindows([](HWND hWnd, LPARAM lParam) -> BOOL {
		auto& data = *(WindowFinderData*)lParam;
		unsigned long pid = 0;
		GetWindowThreadProcessId(hWnd, &pid);
		if (data.pid != pid || !(GetWindow(hWnd, GW_OWNER) == NULL && IsWindowVisible(hWnd)))
		{
			return TRUE;
		}
		data.hWndLikelyPrimary = hWnd;
		return FALSE;
		}, (LPARAM)&data);
	return data.hWndLikelyPrimary;
}

bool ProcessOperations::CloseApp(const unsigned long pid, const unsigned long exitCode, const unsigned long millisecondsMaxWait)
{
	bool bR = false;
	HANDLE hProcess = NULL;
	hProcess = OpenProcess(SYNCHRONIZE, FALSE, pid);		// only open with SYNCHRONIZE here, we call Terminate as necessary that opens with TERMINATE
	if (NULL == hProcess)
	{
		LIBCOMMON_DEBUG_PRINT(L"CloseApp can't open process handle");
		return true;
	}

	// first make sure the process isn't already terminated
	if (WaitForSingleObject(hProcess, 0) != WAIT_OBJECT_0)
	{
		//
		// first try graceful close, then forced close
		//

		HWND hWndMain = GetLikelyPrimaryWindow(pid);
		bool bTimedOut = false;
		if (hWndMain)
		{
			LIBCOMMON_DEBUG_PRINT(L"Found main window for %d", pid);
			PostMessage(hWndMain, WM_CLOSE, 0, 0);
			DWORD dwWaitMs = millisecondsMaxWait;
			if (WaitForSingleObject(hProcess, dwWaitMs) != WAIT_OBJECT_0)
			{
				LIBCOMMON_DEBUG_PRINT(L"Timeout");
				bTimedOut = true;
			}
			else
			{
				LIBCOMMON_DEBUG_PRINT(L"Wait on process handle satisfied");
				bR = true;
			}
		}
		// force secondary termination try, won't hurt anything
		if (NULL == hWndMain || true == bTimedOut)
		{
			LIBCOMMON_DEBUG_PRINT(L"WARNING: Wait timed out or no main window. Forceful termination");
			bR = Terminate(pid, exitCode);
		}
	}
	CloseHandle(hProcess);

	return bR;
}

bool ProcessOperations::SetEfficiencyMode(const unsigned long pid, const EfficiencyMode efficiencyMode)
{
	if (!_SetProcessInformation)
	{
		return false;
	}
	HANDLE hProcess = OpenProcess(PROCESS_SET_INFORMATION, FALSE, pid);
	if (NULL == hProcess)
	{
		return false;
	}
	PROCESS_POWER_THROTTLING_STATE state = { 0 };
	state.Version = PROCESS_POWER_THROTTLING_CURRENT_VERSION;
	state.ControlMask = PROCESS_POWER_THROTTLING_EXECUTION_SPEED;
	state.StateMask = efficiencyMode == EM_ON ? PROCESS_POWER_THROTTLING_EXECUTION_SPEED : 0;
	if (!_SetProcessInformation(hProcess, ProcessPowerThrottling, &state, sizeof(state)))
	{
		LIBCOMMON_DEBUG_PRINT(L"Failed to set efficiency mode for %u", pid);
		CloseHandle(hProcess);
		return false;
	}
	CloseHandle(hProcess);
	return true;
}

bool ProcessOperations::GetEfficiencyMode(const unsigned long pid, __out EfficiencyMode& efficiencyMode)
{
	efficiencyMode = EM_UNSET;
	if (!_GetProcessInformation)
	{
		return false;
	}
	HANDLE hProcess = OpenQueryHandle(pid);
	if (NULL == hProcess)
	{
		return false;
	}
	PROCESS_POWER_THROTTLING_STATE state = { 0 };
	state.Version = PROCESS_POWER_THROTTLING_CURRENT_VERSION;
	state.ControlMask = PROCESS_POWER_THROTTLING_EXECUTION_SPEED;
	state.StateMask = PROCESS_POWER_THROTTLING_EXECUTION_SPEED;
	if (!_GetProcessInformation(hProcess, ProcessPowerThrottling, &state, sizeof(state)))
	{
		LIBCOMMON_DEBUG_PRINT(L"Failed to get efficiency mode for %u", pid);
		CloseHandle(hProcess);
		return false;
	}
	CloseHandle(hProcess);
	efficiencyMode = (state.StateMask & PROCESS_POWER_THROTTLING_EXECUTION_SPEED) ? EM_ON : EM_OFF;
	return true;
}

bool ProcessOperations::SetIgnoreTimerResolution(const unsigned long pid, const bool bEnabled)
{
	if (!_SetProcessInformation)
	{
		return false;
	}
	HANDLE hProcess = OpenProcess(PROCESS_SET_INFORMATION, FALSE, pid);
	if (NULL == hProcess)
	{
		return false;
	}
	PROCESS_POWER_THROTTLING_STATE PowerThrottling = { 0 };
	PowerThrottling.Version = PROCESS_POWER_THROTTLING_CURRENT_VERSION;
	PowerThrottling.ControlMask = PROCESS_POWER_THROTTLING_IGNORE_TIMER_RESOLUTION;
	PowerThrottling.StateMask = bEnabled ? PROCESS_POWER_THROTTLING_IGNORE_TIMER_RESOLUTION : 0;
	if (!_SetProcessInformation(hProcess, ProcessPowerThrottling, &PowerThrottling, sizeof(PowerThrottling)))
	{
		LIBCOMMON_DEBUG_PRINT(L"Failed to set ignore timer resolutino mode for %u", pid);
		CloseHandle(hProcess);
		return false;
	}
	CloseHandle(hProcess);
	return true;
}
#pragma once
#include <windows.h>
#include <lmcons.h>
#include <tlhelp32.h>
#include <vector>
#include <atlstr.h>

typedef LONG(NTAPI* NtSuspendProcess)(IN HANDLE ProcessHandle);
typedef LONG(NTAPI* NtResumeProcess)(IN HANDLE ProcessHandle);

// some functions for process manipulation isolated for portability and ease of use.
#define NO_PROCESSOR_GROUP (-1)

class GroupAffinity
{
public:
	int nGroupId;		// -1 if no group set
	unsigned __int64 mask;
	GroupAffinity()
	{
		nGroupId = NO_PROCESSOR_GROUP;
		mask = 0;
	}
	GroupAffinity(const GROUP_AFFINITY& g)
	{
		*this = g;
	}
	GroupAffinity& operator = (const GROUP_AFFINITY& g) {
		mask = g.Mask;
		nGroupId = g.Group;
		return *this;
	}
	bool operator == (const GroupAffinity& o) const {
		return (mask == o.mask
			&& (nGroupId == o.nGroupId || nGroupId == NO_PROCESSOR_GROUP || o.nGroupId == NO_PROCESSOR_GROUP));
	}
	bool operator != (const GroupAffinity& o) const {
		return !(*this == o);
	}
};

class ProcessOperations
{
	SYSTEM_INFO sysInfo;
public:
	ProcessOperations()
	{
		GetSystemInfo(&sysInfo);
	}
	HANDLE OpenQueryHandle(const unsigned long pid);
	bool GetAffinityMask(const unsigned long pid, unsigned long long& bitMask);
	bool SetAffinityMask(const unsigned long pid, const unsigned long long bitMask, const int group = NO_PROCESSOR_GROUP);
	// get CPU affinity mask with group
	bool GetGroupAffinity(const unsigned long pid, GroupAffinity& aff);
	bool SetProcessGroupAffinity(const unsigned long pid, int nProcessorGroup, unsigned long long maskAff);
	// get processor groups for a process
	size_t GetProcessProcessorGroups(const unsigned long pid, std::vector<USHORT>& vGroups);
	bool IsMultiGroupProcess(const unsigned long pid);
	bool SetGroupAffinityForAllThreads(const unsigned long pid, const int group, const unsigned long long bitMask);

	bool SetPriorityBoost(const unsigned long pid, const bool bPriorityBoostEnabled);
	unsigned long GetPriorityClass(const unsigned long pid);
	bool SetPriorityClass(const unsigned long pid, const long priorityClass);
	bool TrimWorkingSetSize(const unsigned long pid);
	bool Terminate(const unsigned long pid, const unsigned long exitCode);
	bool CloseApp(const unsigned long pid, const unsigned long exitCode, const unsigned long millisecondsMaxWait);
	bool SuspendProcess(const unsigned long pid);
	bool ResumeProcess(const unsigned long pid);

	bool GetUserNameByToken(const unsigned long pid, ATL::CString& csUser, ATL::CString& csDomain);
	unsigned long GetParentOfProcess(const unsigned long pid);
	bool GetLogonFromToken(HANDLE hToken, ATL::CString& csUser, ATL::CString& csDomain);
	bool GetUserNameForProcess(const unsigned long pid, ATL::CString& csUser, ATL::CString& csDomain);

	unsigned long long LimitAffinityToInstalledCPUCores(unsigned long long bitmask);

	HWND GetLikelyPrimaryWindow(const unsigned long pid);

	bool SetEfficiencyMode(const unsigned long pid, const int nEfficiencyMode);
	bool GetEfficiencyMode(const unsigned long pid, __out int& nEfficiencyMode);
};
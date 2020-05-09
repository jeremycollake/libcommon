#pragma once
#include <windows.h>
#include <tlhelp32.h>
#include <vector>

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
};

class processOperations
{	
public:
	bool GetAffinityMask(const unsigned long pid, unsigned long long& bitMask);
	bool SetAffinityMask(const unsigned long pid, const unsigned long long bitMask, const int group=NO_PROCESSOR_GROUP);
	// get CPU affinity mask with group
	bool GetGroupAffinity(const unsigned long pid, GroupAffinity& aff);	
	bool SetProcessGroupAffinity(const unsigned long pid, int nProcessorGroup, unsigned long long maskAff);
	// get processor groups for a process
	size_t GetProcessProcessorGroups(const unsigned long pid, std::vector<USHORT>& vGroups);
	bool IsMultiGroupProcess(const unsigned long pid);
	bool SetGroupAffinityForAllThreads(const unsigned long pid, const int group, const unsigned long long bitMask);

	bool SetPriorityBoost(const unsigned long pid, const bool bPriorityBoostEnabled);
	bool SetPriorityClass(const unsigned long pid, const long priorityClass);
	bool TrimWorkingSetSize(const unsigned long pid);
	bool Terminate(const unsigned long pid, const unsigned long exitCode);		
	bool SuspendProcess(const unsigned long pid);
	bool ResumeProcess(const unsigned long pid);


};
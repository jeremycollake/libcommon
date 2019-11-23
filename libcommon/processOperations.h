#pragma once
#include <windows.h>

typedef LONG(NTAPI* NtSuspendProcess)(IN HANDLE ProcessHandle);
typedef LONG(NTAPI* NtResumeProcess)(IN HANDLE ProcessHandle);

// some functions for process manipulation isolated for portability and ease of use.
class processOperations
{
public:
	bool SetAffinityMask(const unsigned long pid, const unsigned long long bitMask);
	bool SetPriorityBoost(const unsigned long pid, const bool bPriorityBoostEnabled);
	bool SetPriorityClass(const unsigned long pid, const long priorityClass);
	bool TrimWorkingSetSize(const unsigned long pid);
	bool Terminate(const unsigned long pid, const unsigned long exitCode);		
	bool SuspendProcess(const unsigned long pid);
	bool ResumeProcess(const unsigned long pid);
};
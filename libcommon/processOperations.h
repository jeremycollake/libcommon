#pragma once
#include <windows.h>

// some functions for process manipulation isolated for portability and ease of use.
class processOperations
{
public:
	bool SetAffinityMask(const unsigned long pid, const unsigned long long bitMask);
	bool SetPriorityBoost(const unsigned long pid, const bool bPriorityBoostEnabled);
	bool SetPriorityClass(const unsigned long pid, const long priorityClass);
	bool TrimWorkingSetSize(const unsigned long pid);
	bool Terminate(const unsigned long pid, const unsigned long exitCode);	
};
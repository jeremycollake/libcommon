#include "pch.h"
#include "processOperations.h"

bool processOperations::SetAffinityMask(const unsigned long pid, const unsigned long long bitMask)
{
	HANDLE hProcess = OpenProcess(PROCESS_SET_INFORMATION, FALSE, pid);
	if (NULL == hProcess) return false;
	bool bR = ::SetProcessAffinityMask(hProcess, bitMask) ? true : false;
	CloseHandle(hProcess);
	return bR;
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
	HANDLE hProcess = OpenProcess(PROCESS_SET_INFORMATION, FALSE, pid);
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

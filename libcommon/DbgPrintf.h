#pragma once

#include <tchar.h>

// What a debug line may be used for beyond OutputDebugString. A sink (below) gets it with each line.
// Persist: the line may be kept outside the process, for example in a file that other programs of the user can read.
// Tag a line Persist only when it can never hold license or customer data, a process command line, or the path or
// name of a process that belongs to another user or to SYSTEM.
enum class DbgPrintfTag : unsigned int
{
	None = 0,
	Persist = 1
};

void DbgPrintf(LPCTSTR fmt, ...);
void DbgPrintf(DbgPrintfTag tag, LPCTSTR fmt, ...);

// An optional in-process copy of everything DbgPrintf emits, for a program that wants to show its own debug output
// to the user (Process Lasso: Help > View Debug Output). The sink gets each formatted line, without the newline, and
// its tag, on the calling thread, so it must be thread-safe and must not call DbgPrintf. nullptr (the default)
// removes it.
typedef void (*DbgPrintfSink)(LPCTSTR ptszLine, DbgPrintfTag tag);
void DbgPrintfSetSink(DbgPrintfSink pSink);

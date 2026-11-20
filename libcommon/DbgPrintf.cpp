#include "pch.h"
#include "DbgPrintf.h"
#include "atlstr.h"
#include <atomic>

// constant-initialized, so a DbgPrintf from a static initializer finds nullptr
static std::atomic<DbgPrintfSink> g_pDbgPrintfSink{ nullptr };

void DbgPrintfSetSink(DbgPrintfSink pSink)
{
	g_pDbgPrintfSink.store(pSink);
}

static void DbgPrintfV(DbgPrintfTag tag, LPCTSTR fmt, va_list marker)
{
	ATL::CString csTemp;
	csTemp.FormatV(fmt, marker);
	const DbgPrintfSink pSink = g_pDbgPrintfSink.load();
	if (pSink)
	{
		pSink(csTemp, tag);
	}
	csTemp += L"\n";
	OutputDebugStringW(csTemp);
}

// primary debug emission
void DbgPrintf(LPCTSTR fmt, ...)
{
	va_list marker;
	va_start(marker, fmt);
	DbgPrintfV(DbgPrintfTag::None, fmt, marker);
	va_end(marker);
}

void DbgPrintf(DbgPrintfTag tag, LPCTSTR fmt, ...)
{
	va_list marker;
	va_start(marker, fmt);
	DbgPrintfV(tag, fmt, marker);
	va_end(marker);
}

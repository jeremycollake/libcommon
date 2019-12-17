#include "pch.h"
#include "DbgPrintf.h"
#include "atlstr.h"

// primary debug emission
void DbgPrintf(LPCTSTR fmt, ...)
{
	ATL::CString csTemp;
	va_list marker;
	va_start(marker, fmt);
	csTemp.FormatV(fmt, marker);
	va_end(marker);
	csTemp += L"\n";
	OutputDebugStringW(csTemp);	
}

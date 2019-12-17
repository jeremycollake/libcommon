#pragma once

#include "DbgPrintf.h"

//#define PL_SHOW_ICON_DEBUG_PRINT

#ifdef PL_SHOW_ICON_DEBUG_PRINT
#define ICON_DEBUG_PRINT(LPCTSTR, ...) DbgPrintf(LPCTSTR, __VA_ARGS__)
#else
#define ICON_DEBUG_PRINT(LPCTSTR, ...)
#endif
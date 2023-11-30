#pragma once

#include "DbgPrintf.h"

//#define ENABLE_DEBUG_OUTPUT
//#define ENABLE_IPC_DEBUG_OUTPUT

#ifdef ENABLE_DEBUG_OUTPUT
#define LIBCOMMON_DEBUG_PRINT(LPCTSTR, ...) DbgPrintf(LPCTSTR, __VA_ARGS__)
#else
#define LIBCOMMON_DEBUG_PRINT(LPCTSTR, ...)
#endif

#ifdef ENABLE_IPC_DEBUG_OUTPUT
#define IPC_DEBUG_PRINT(LPCTSTR, ...) DbgPrintf(LPCTSTR, __VA_ARGS__)
#else
#define IPC_DEBUG_PRINT(LPCTSTR, ...)
#endif
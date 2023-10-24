#pragma once
#include <windows.h>
#include <iostream>
#include <vector>

// output to log or debug
class LogOut
{	
public:
	enum LOG_TARGET
	{
		LTARGET_NONE = 0,
		LTARGET_DEBUG,
		LTARGET_STDOUT,
		LTARGET_FILE
	};
	LOG_TARGET logTarget;
	
	LogOut(const LOG_TARGET logTarget = LTARGET_STDOUT);
	
	void SetTarget(const LOG_TARGET logTarget);

	void Write(LPCTSTR fmt, ...);

	void FormattedErrorOut(LPCTSTR msg);
};

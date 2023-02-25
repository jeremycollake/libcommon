#pragma once

#include <windows.h>
#include <shellapi.h>

namespace WindowsState
{
	bool IsFullScreenAppOpen()
	{
		QUERY_USER_NOTIFICATION_STATE quns;
		if (SHQueryUserNotificationState(&quns) == S_OK)
		{
			switch (quns)
			{
			case QUNS_BUSY:
			case QUNS_RUNNING_D3D_FULL_SCREEN:
			case QUNS_PRESENTATION_MODE:
				LIBCOMMON_DEBUG_PRINT(L"full screen app DETECTED");
				return true;
			}
		}
		LIBCOMMON_DEBUG_PRINT(L"full screen app NOT detected");
		return false;
	}
};
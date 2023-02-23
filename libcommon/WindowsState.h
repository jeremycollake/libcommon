#pragma once

#include <windows.h>
#include <shellapi.h>

namespace WindowsState
{
	bool IsFullScreenAppOpen()
	{
		QUERY_USER_NOTIFICATION_STATE pquns;
		if (SHQueryUserNotificationState(&pquns) == S_OK)
		{
			switch (pquns)
			{
			case QUNS_BUSY:
			case QUNS_RUNNING_D3D_FULL_SCREEN:
			case QUNS_PRESENTATION_MODE:
				return true;
			}
		}
		return false;
	}
};
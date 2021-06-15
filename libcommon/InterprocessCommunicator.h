#pragma once
#include "libCommon.h"
#include "DebugOutToggles.h"
#include <string>

// WARNING: This code was fleshed out, but not used, so it has never been tested or refined.


// do Windows interprocess communication by a memory-mapped file
// creates in global namespace unless insufficient access (or sec token not acquired), in which case it uses local namespace
// for global, be sure to acquire SE_CREATE_GLOBAL_PRIV
template <class MSG>
class InterprocessCommunicator
{
	std::wstring strName;
	HANDLE hMapFile;
	HANDLE hMutex_Sender;	// mutex created only by sender. Receiver opens each time. Opened by secondary senders.
	void* pView;
	unsigned int nViewSizeElementCount;
private:
	void Close()
	{
		if (pView)
		{
			UnmapViewOfFile(pView);
			pView = nullptr;
		}
		if (hMapFile)
		{
			CloseHandle(hMapFile);
			hMapFile = NULL;
		}
		if (hMutex_Sender)
		{
			CloseHandle(hMutex_Sender);
			hMutex_Sender = NULL;
		}
	}
public:
	InterprocessCommunicator(const WCHAR* pwszName, const bool bSender, const unsigned int nMaxMessages)
	{
		_ASSERT(nMaxMessages && pwszName);

		strName = pwszName;
		pView = nullptr;
		hMapFile = NULL;
		hMutex_Sender = NULL;
		nViewSizeElementCount = nMaxMessages;

		SECURITY_DESCRIPTOR sd;
		InitializeSecurityDescriptor(&sd, SECURITY_DESCRIPTOR_REVISION);
		SetSecurityDescriptorDacl(&sd, TRUE, NULL, FALSE);

		SECURITY_ATTRIBUTES saEveryone = { 0 };
		saEveryone.nLength = sizeof(saEveryone);
		saEveryone.bInheritHandle = FALSE;
		saEveryone.lpSecurityDescriptor = &sd;

		// name should *not* have local or global prefix, we'll append that. However, mark if it does so we can not do that.
		// note the Global and Local namespaces are case sensitive, so case senstive find is appropriate
		if (strName.find(L"Global\\") != std::wstring::npos
			|| strName.find(L"Local\\") != std::wstring::npos)
		{
			IPC_DEBUG_PRINT(L"WARNING: Namespace was passed in to name inappropriately!");
			_ASSERT(0);
		}
		else
		{
			IPC_DEBUG_PRINT(L"Creating mapped file size %d bytes to have %d messages", nMaxMessages * sizeof(MSG), nMaxMessages);
			std::wstring strNameWithNamespace = L"Global\\" + strName;
			if (bSender)
			{
				hMapFile = CreateFileMapping(
					INVALID_HANDLE_VALUE,    // use paging file
					NULL,                    // default security
					PAGE_READWRITE,          // read/write access
					0,                       // maximum object size (high-order DWORD)
					nMaxMessages * sizeof(MSG),                // maximum object size (low-order DWORD)
					strNameWithNamespace);                 // name of mapping object
				if (!hMapFile)
				{
					IPC_DEBUG_PRINT(L"WARNING: Could not create memory mapped file in Global namespace, trying Local");
					strNameWithNamespace = strName;		// no namespace specified implies Local
					hMapFile = CreateFileMapping(
						INVALID_HANDLE_VALUE,    // use paging file
						&saEveryone,                    // default security
						PAGE_READWRITE,          // read/write access
						0,                       // maximum object size (high-order DWORD)
						nMaxMessages * sizeof(MSG),                // maximum object size (low-order DWORD)
						strNameWithNamespace);                 // name of mapping object
				}
			}
			else
			{
				hMapFile = OpenFileMapping(FILE_MAP_ALL_ACCESS, FALSE, strNameWithNamespace);
				if (!hMapFile)
				{
					IPC_DEBUG_PRINT(L"WARNING: Could not open memory mapped file in Global namespace, trying Local");
					strNameWithNamespace = strName;		// no namespace specified implies Local
					hMapFile = OpenFileMapping(FILE_MAP_ALL_ACCESS, FALSE, strNameWithNamespace);
				}
			}

			if (!hMapFile)
			{
				IPC_DEBUG_PRINT(L"ERROR: Memory mapped file creation or open failed");
			}
			else
			{
				pView = MapViewOfFile(hMapFile,   // handle to map object
					FILE_MAP_ALL_ACCESS, // read/write permission
					0,
					0,
					BUF_SIZE);
				if (!pView)
				{
					IPC_DEBUG_PRINT(L"ERROR: Could not map view of file (%d).\n", GetLastError());
				}
				else
				{
					// finally create the mutex in the same namespace
					// build mutex name from mapped file name
					std::wstring strMutexName = strNameWithNamespace + L"_m";
					IPC_DEBUG_PRINT(L"Creating or opening mutex %s", strMutexName);
					if (bSender)
					{
						// if mutex is existing, it will open
						hMutex_Sender = CreateMutex(&saEveryone, FALSE, strMutexName);
						if (!hMutex_Sender)
						{
							IPC_DEBUG_PRINT(L"ERROR: Mutex create or open failed");
							// with no mutex, close the mapped view now so we have no mistakes
							Close();
						}
					}
					else
					{
						hMutex_Sender = NULL;
					}
				}
			}
		}
	}
	~InterprocessCommunicator()
	{
		Close();
	}

	bool IsReady()
	{
		return (hMapFile && pView) ? true : false;
	}
	// pop off every message in the view
	// view is prefixed by a DWORD indicating current member count
	int Read(std::vector<MSG>& vecMessages)
	{
		if (!IsReady())
		{
			IPC_DEBUG_PRINT(L"ERROR: IPC not ready!");
			return 0;
		}
		HANDLE hMutex;
		if (hMutex_Sender)
		{
			hMutex = hMutex_Sender;
		}
		else
		{
			hMutex = OpenMutex(SYNCHRONIZE, FALSE, strMutexName);
		}
		if (hMutex)
		{
			if (WaitForSingleObject(hMutex, INFINITE) == WAIT_OBJECT_0)
			{
				IPC_DEBUG_PRINT(L"IPC: Reading %u messages", *pdwMemberCount);
				DWORD* pdwMemberCount = static_cast<DWORD*>(pView);
				char* pBuf = static_cast<char*>(pView) + sizeof(DWORD);
				MSG* pMessage = static_cast<MSG*>pBuf;
				for (int n = 0; n < *pdwMemberCount; n++, pMessage++)
				{
					vecMessages.push_back(*pMessage);
				}
				*pdwMemberCount = 0;
				ReleaseMutex(hMutex);
			}
		}
		else
		{
			IPC_DEBUG_PRINT(L"WARNING: Error opening mutex");
		}
		return static_cast<int>(vecMessages.size());
	}
	// write a message to the view
	// view is prefixed by a DWORD indicating current member count
	bool Write(const MSG& msg)
	{
		if (!IsReady())
		{
			IPC_DEBUG_PRINT(L"ERROR: IPC not ready!");
			return false;
		}
		HANDLE hMutex;
		if (hMutex_Sender)
		{
			hMutex = hMutex_Sender;
		}
		else
		{
			hMutex = OpenMutex(SYNCHRONIZE, FALSE, strMutexName);
		}
		if (hMutex)
		{
			if (WaitForSingleObject(hMutex, INFINITE) == WAIT_OBJECT_0)
			{
				DWORD* pdwMemberCount = static_cast<DWORD*>(pView);
				char* pBuf = static_cast<char*>(pView) + sizeof(DWORD);
				MSG* pMessage = static_cast<MSG*>pBuf;
				if (*pdwMemberCount >= nViewSizeElementCount)
				{
					IPC_DEBUG_PRINT(L"WARNING: IPC is full! Size is %d, max is %d", *pdwMemberCount, nViewSizeElementCount);
				}
				else
				{
					// copy to the next position in the buffer
					pMessage += *pdwMemberCount;
					memcpy(pMessage, &msg, sizeof(MSG));
					*pdwMemberCount++;
				}
				ReleaseMutex(hMutex);
				return true;
			}
		}
		else
		{
			IPC_DEBUG_PRINT(L"WARNING: Error opening mutex");
		}
		return false;
	}
};
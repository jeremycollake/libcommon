#pragma once

// Standards note: Trying to start migrating away from hungarian notation

#include <atlstr.h>
#include <map>
#include <vector>
#include <mutex>
#include <sstream>
#include "CSVUtil.h"
#include "CSVReader.h"
#include "DebugOutToggles.h"

using namespace std;
using namespace ATL;

class CSVReader
{
	wstring sourceFilePath;

	// to know if the source file is still the same one (or has been rotated/deleted)
	mutex mutexBookmark;
	FILETIME timeCreatedLastAccessedFile = { 0, 0 };
	DWORD positionBookmark = 0;

public:
	void SetSourceFilePath(const WCHAR* sourcePath)
	{
		timeCreatedLastAccessedFile = { 0, 0 };
		positionBookmark = 0;
		sourceFilePath = sourcePath;
	}
	wstring GetSourceFilePath()
	{
		return sourceFilePath;
	}
	// start or continue a read from the last read index to EOF
	// returns vector of vectors of fields to values, e.g. { row1 { Val1 , Val2 }, row2 { Val1 , Val2 } }
	int ReadRows(_Out_ vector<vector<wstring>>& rows)
	{
		lock_guard<mutex> lock(mutexBookmark);

		LOG_DEBUG_PRINT(L"ReadSourceToEOF at index %d", positionBookmark);
		HANDLE hFile = CreateFile(sourceFilePath.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, NULL);
		if (hFile == INVALID_HANDLE_VALUE)
		{
			LOG_DEBUG_PRINT(L"ERROR opening %s", sourceFilePath.c_str());
			return 0;
		}
		// check to ensure same file we have a bookmark to
		bool isSameFile = true;
		FILETIME timeCreated, timeLastAccess, timeLastWrite;
		DWORD sizeHigh = 0, sizeLow = 0;
		if (!GetFileTime(hFile, &timeCreated, &timeLastAccess, &timeLastWrite))
		{
			// abort here to prevent undefined behavior
			LOG_DEBUG_PRINT(L"Error getting file times");
			CloseHandle(hFile);
			return 0;
		}
		else
		{
			if (0 != CompareFileTime(&timeCreated, &timeCreatedLastAccessedFile))
			{
				LOG_DEBUG_PRINT(L"Created time doesn't match, tossing booking");
				isSameFile = false;
			}
			// also check file size to ensure it isn't < our index				
			sizeLow = GetFileSize(hFile, &sizeHigh);
			if (sizeLow == INVALID_FILE_SIZE)
			{
				// abort here to prevent undefined behavior
				LOG_DEBUG_PRINT(L"Error getting file size");
				CloseHandle(hFile);
				return 0;
			}
			if (sizeLow < positionBookmark)
			{
				LOG_DEBUG_PRINT(L"File size < bookmark, tossing bookmark");
				isSameFile = false;
			}
		}

		if (false == isSameFile)
		{
			LOG_DEBUG_PRINT(L"File appears different. Tossing bookmark");
			positionBookmark = 3;	// BOM size
			timeCreatedLastAccessedFile.dwHighDateTime = timeCreated.dwHighDateTime;
			timeCreatedLastAccessedFile.dwLowDateTime = timeCreated.dwLowDateTime;
		}

		// read to EOF

		// if file size high is non-zero, this file is too big for us to handle, so abort
		if (sizeHigh
			|| sizeLow < positionBookmark)
		{
			LOG_DEBUG_PRINT(L"WARNING: file size out of bounds or nothing to read. Aborting");
			CloseHandle(hFile);
			return 0;
		}
		int bytesToRead = sizeLow - positionBookmark;
		if (bytesToRead > 0)
		{
			LOG_DEBUG_PRINT(L"Bytes to read is %d", bytesToRead);
			SetFilePointer(hFile, positionBookmark, nullptr, FILE_BEGIN);
			std::string bytes(bytesToRead + 1, '\0');
			DWORD bytesRead = 0;
			if (!ReadFile(hFile, (LPVOID)bytes.c_str(), bytesToRead, &bytesRead, nullptr) || bytesRead != bytesToRead)
			{
				LOG_DEBUG_PRINT(L"ReadFile failure. Aborting");
				CloseHandle(hFile);
				return 0;
			}
			static CSVUtil csvUtil;
			wistringstream datastream(csvUtil.ConvertUTF8ToWSTR(bytes));
			int numRows = 0;
			for (wstring line;
				std::getline(datastream, line);
				)
			{
				if (datastream.bad() || datastream.fail() || datastream.eof())
				{
					break;
				}
				LOG_DEBUG_PRINT(L"Read line: %s", line.c_str());
				// if header, skip
				if (positionBookmark > 3 || numRows > 0)
				{
					vector<wstring> fields;
					csvUtil.ParseCSVRow(line, fields);
					rows.push_back(fields);
				}
				numRows++;
			}

			positionBookmark = sizeLow;
			LOG_DEBUG_PRINT(L"Returned %u rows", rows.size());
		}

		CloseHandle(hFile);
		return static_cast<int>(rows.size());
	}
};
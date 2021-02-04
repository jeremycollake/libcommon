#pragma once
#include<atlstr.h>
#include<map>
#include<vector>
#include<mutex>
#include "CSVUtil.h"
#include "DebugOutToggles.h"

using namespace std;

#define SAMPLING_DEBUG_PRINT LIBCOMMON_DEBUG_PRINT

class CSVEmitter
{
	ATL::CString outFilepath;
	mutex mutexFields;
	vector<ATL::CString> vectorOrderedFields;				  // ordered field list so we know field column positions
	map<ATL::CString, ATL::CString> mapCurrentLineValues; // fieldname -> value
	CSVUtil csvUtil;

	const unsigned char bom[3] = { 0xEF,0xBB,0xBF };
	const int bomSize = _countof(bom);

	CString BuildHeaderString()
	{
		_ASSERT(vectorOrderedFields.size());
		CString csRet;
		for (auto i : vectorOrderedFields)
		{
			if (!csRet.IsEmpty())
			{
				csRet += L",";
			}
			csRet += L"\"" + i + L"\"";
		}
		return csRet;
	}

	// case insensitive std::string compare
	bool iequals(const std::string& a, const std::string& b)
	{
		return std::equal(a.begin(), a.end(), b.begin(),
			[](char a, char b) {
				return tolower(a) == tolower(b);
			});
	}

	// check to see if header on existing file matches what we would place there. If not, count or order of columns has changed
	bool CompareHeaderString(const HANDLE hFile)
	{
		_ASSERT(vectorOrderedFields.size());
		bool bHeaderWouldMatch = false;
		if (hFile == INVALID_HANDLE_VALUE)
		{
			return bHeaderWouldMatch;
		}
		// NOTE: never use a BOM since Excel doesn't like it
		string sNewHeaderString = csvUtil.ConvertUTF16ToUTF8(BuildHeaderString());
		string sOldHeaderString;
		if (sNewHeaderString.length())
		{
			SAMPLING_DEBUG_PRINT(L"BOM size %d", bomSize);
			size_t nLen = sNewHeaderString.length();
			// ensure file size is sufficient
			DWORD dwSizeHigh = 0;
			if (static_cast<size_t>(GetFileSize(hFile, &dwSizeHigh)) - bomSize >= nLen)	// minus size of BOM
			{
				SetFilePointer(hFile, bomSize, nullptr, FILE_BEGIN);		// move past BOM
				char* pszExistingHeader = new char[nLen + 1];
				if (pszExistingHeader)
				{
					// then read line
					DWORD dwBytesRead;
					if (ReadFile(hFile, pszExistingHeader, static_cast<DWORD>(nLen), &dwBytesRead, nullptr))
					{
						pszExistingHeader[nLen] = 0;
						if (iequals(sNewHeaderString, pszExistingHeader))
						{
							bHeaderWouldMatch = true;
						}
					}
					delete pszExistingHeader;
				}
			}
		}
		return bHeaderWouldMatch;
	}
	void WriteLineFeed(const HANDLE hFile)
	{
		// separately write CRLF in case changed to LF
		DWORD dwBytesWrote;
		const char* pszCRLF = "\r\n";		
		WriteFile(hFile, pszCRLF, 2, &dwBytesWrote, nullptr);
	}

public:
	CSVEmitter(const WCHAR* outFilepath, const vector<ATL::CString>& fields) : outFilepath(outFilepath) 
	{
		AddFields(fields);
	}
	// add a field, orders as received - only needs to be done once
	void AddFields(const vector<ATL::CString>& fields)
	{
		lock_guard<mutex> lock(mutexFields);
		for (auto& i : fields)
		{
			AddField(i);
		}
	}
	void ClearFields()
	{
		vectorOrderedFields.clear();
	}
	void AddField(const WCHAR* fieldname)
	{
		vectorOrderedFields.push_back(fieldname);
	}
	// add value to current line (add all values for each line, then output with WriteCurrentLine)
	void AddValue(const WCHAR* fieldname, const WCHAR* value)
	{
#ifdef DEBUG
		bool bFoundField = false;
		for (auto& i : vectorOrderedFields)
		{
			if (i.Compare(fieldname) == 0)
			{
				bFoundField = true;
			}
		}
		_ASSERT(bFoundField);
		if (!bFoundField)
		{
			SAMPLING_DEBUG_PRINT(L"WARNING: Sampling field %s not defined", fieldname);
		}
#endif
		mapCurrentLineValues[fieldname] = value;
	}
	void AddValue(const WCHAR* fieldname, const DWORD value)
	{
		CString csTemp;
		csTemp.Format(L"%lu", value);
		mapCurrentLineValues[fieldname] = csTemp;
	}
	void AddValue(const WCHAR* fieldname, const unsigned long long value)
	{
		CString csTemp;
		csTemp.Format(L"%llu", value);
		mapCurrentLineValues[fieldname] = csTemp;
	}
	bool WriteCurrentLine(const HANDLE hFile)
	{
		lock_guard<mutex> lock(mutexFields);
		DWORD dwBytesWrote;		
		if (hFile == INVALID_HANDLE_VALUE)
		{
			SAMPLING_DEBUG_PRINT(L"ERROR: Sampling can't write to %s (no handle)", outFilepath);
			// try ReadyOutputFile in case the error is due to OPEN_EXISTING disposition above (file was deleted)
			HANDLE hFile = OpenOutputFile(false);
			if (hFile == INVALID_HANDLE_VALUE)
			{
				SAMPLING_DEBUG_PRINT(L"ERROR: Sampling can't write to %s (no handle after retry)", outFilepath);
				return false;
			}
			SAMPLING_DEBUG_PRINT(L"Sampling error resolved by ReadyOutputFile");
			// fall-through with valid handle to now fixed file
		}
		SetFilePointer(hFile, 0, nullptr, FILE_END);
		// build up row with field values
		CString csRow;
		for (auto i : vectorOrderedFields)
		{
			auto value = mapCurrentLineValues.find(i);
			CString csValue = L"";
			if (value == mapCurrentLineValues.end())
			{
				//SAMPLING_DEBUG_PRINT(L"WARNING: No value for field %s", i.GetBuffer());
			}
			else
			{
				csValue = mapCurrentLineValues[i];
			}
			if (!csRow.IsEmpty())
			{
				csRow += L",";
			}			
			csRow += L"\"" + csvUtil.EscapeField(csValue.GetString(), true) + L"\"";
		}
		string sOut = csvUtil.ConvertUTF16ToUTF8(csRow);	
		if (!WriteFile(hFile, &sOut[0], static_cast<DWORD>(sOut.length()), &dwBytesWrote, nullptr))
		{
			SAMPLING_DEBUG_PRINT(L"ERROR: Sampling can't write to %s", outFilepath);
		}
		WriteLineFeed(hFile);
		
		mapCurrentLineValues.clear();

		return true;
	}

	// if header doesn't match, then wipe and start fresh
	// if it does, then start appending
	// should be called only *after* defining fields with AddField
	HANDLE OpenOutputFile(bool bEmptyFile=false)
	{
		// do NOT acquire mutex since this may be called from within WriteCurrentLine under some error conditions
		//lock_guard<mutex> DoNot(mutexFields);		

		_ASSERT(vectorOrderedFields.size());
		DWORD dwBytesWrote;
		HANDLE hFile = CreateFile(outFilepath, GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
		if (hFile == INVALID_HANDLE_VALUE)
		{
			SAMPLING_DEBUG_PRINT(L"ERROR: Sampling can't open %s", outFilepath);
			return INVALID_HANDLE_VALUE;
		}		
		if (true==bEmptyFile 
			|| !CompareHeaderString(hFile))
		{
			SAMPLING_DEBUG_PRINT(L"WARNING: Sampling output file header didn't match or empty requested, starting fresh");

			SetFilePointer(hFile, 0, nullptr, FILE_BEGIN);
			
			WriteFile(hFile, bom, _countof(bom), &dwBytesWrote, nullptr);
	
			CString csHeader = BuildHeaderString();
			if (!csHeader.IsEmpty())
			{
				string sOut=csvUtil.ConvertUTF16ToUTF8(csHeader);
				WriteFile(hFile, &sOut[0], static_cast<DWORD>(sOut.length()), &dwBytesWrote, nullptr);
				WriteLineFeed(hFile);
			}

			SetEndOfFile(hFile);
		}
		else
		{
			SAMPLING_DEBUG_PRINT(L"Sampling CSV out file header matches, appending");
		}
		return hFile;
	}
	void CloseOutputFile(const HANDLE hFile)
	{
		_ASSERT(hFile && hFile != INVALID_HANDLE_VALUE);
		CloseHandle(hFile);
	}

};


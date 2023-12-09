
#pragma once

#include <windows.h>
#include <string>

class SystemReservedCPUSets
{
	std::wstring strReservedCpuSetsKeyName = L"System\\CurrentControlSet\\Control\\Session Manager\\kernel";
	std::wstring strReservedCpuSetsValueName = L"ReservedCpuSets";
public:
	bool Get(unsigned long long& mask)
	{
		HKEY hKey;
		if (RegOpenKeyEx(HKEY_LOCAL_MACHINE, strReservedCpuSetsKeyName.c_str(), 0, KEY_READ, &hKey) == ERROR_SUCCESS)
		{
			DWORD dwSize = sizeof(mask);
			if (RegQueryValueEx(hKey, strReservedCpuSetsValueName.c_str(), NULL, NULL, reinterpret_cast<LPBYTE>(&mask), &dwSize) == ERROR_SUCCESS && dwSize == sizeof(mask))
			{
				RegCloseKey(hKey);
				return true;
			}
			RegCloseKey(hKey);
		}
		return false;
	}
	bool Set(unsigned long long mask)
	{
		HKEY hKey;
		if (RegOpenKeyEx(HKEY_LOCAL_MACHINE, strReservedCpuSetsKeyName.c_str(), 0, KEY_WRITE, &hKey) == ERROR_SUCCESS)
		{
			if (RegSetValueEx(hKey, strReservedCpuSetsValueName.c_str(), NULL, REG_BINARY, reinterpret_cast<BYTE*>(&mask), sizeof(mask)) == ERROR_SUCCESS)
			{
				RegCloseKey(hKey);
				return true;
			}
			RegCloseKey(hKey);
		}
		return false;
	}
	bool Clear()
	{
		HKEY hKey;
		if (RegOpenKeyEx(HKEY_LOCAL_MACHINE, strReservedCpuSetsKeyName.c_str(), 0, KEY_WRITE, &hKey) == ERROR_SUCCESS)
		{
			if (RegDeleteValue(hKey, strReservedCpuSetsValueName.c_str()) == ERROR_SUCCESS)
			{
				RegCloseKey(hKey);
				return true;
			}
			RegCloseKey(hKey);
		}
		return false;
	}
};

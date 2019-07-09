/*
* (c)2019 Jeremy Collake <jeremy@bitsum.com>
* https://bitsum.com/portfolio/coreprio
* See LICENSE.TXT
*/
#include "pch.h"
#include "ProductOptions.h"

ProductOptions::ProductOptions(const HKEY hHive, const WCHAR* pwszProductName) : hHive(hHive)
{
	csKeyname.Format(L"Software\\%s", pwszProductName);
}

// force a reload of any referenced options
void ProductOptions::refresh()
{
	mapBools.clear();
	mapstr.clear();
	mapuint32.clear();
	mapuint64.clear();
}

bool& ProductOptions::operator[] (const WCHAR* pwszValueName)
{
	if (mapBools.find(pwszValueName) == mapBools.end())
	{
		bool bVal = false;
		if (read_value(pwszValueName, bVal))
		{
			mapBools[pwszValueName] = bVal;
		}
	}
	return mapBools[pwszValueName];
}

// returns default if doesn't exist in registry
bool ProductOptions::get_value(const WCHAR* pwszValueName, bool bDefault)
{
	bool bVal = bDefault;
	if (mapBools.find(pwszValueName) == mapBools.end())
	{
		if (read_value(pwszValueName, bVal))
		{
			// only update map if read was successful (forces try to re-read next get)
			mapBools[pwszValueName] = bVal;			
		}
		return bVal;
	}
	bVal = mapBools[pwszValueName];
	return bVal;
}

BOOL ProductOptions::get_value(const WCHAR* pwszValueName, BOOL bDefault)
{
	return get_value(pwszValueName, static_cast<bool>(bDefault));
}

unsigned ProductOptions::get_value(const WCHAR* pwszValueName, unsigned nDefault)
{
	unsigned nVal = nDefault;
	if (mapuint32.find(pwszValueName) == mapuint32.end())
	{
		if (read_value(pwszValueName, nVal))
		{
			// only update map if read was successful (forces try to re-read next get)
			mapuint32[pwszValueName] = nVal;			
		}
		return nVal;
	}
	nVal = mapuint32[pwszValueName];
	return nVal;
}

DWORD ProductOptions::get_value(const WCHAR* pwszValueName, DWORD nDefault)
{
	return get_value(pwszValueName, static_cast<unsigned>(nDefault));
}

unsigned long long ProductOptions::get_value(const WCHAR* pwszValueName, unsigned long long nDefault)
{
	unsigned long long nVal = nDefault;
	if (mapuint64.find(pwszValueName) == mapuint64.end())
	{
		if (read_value(pwszValueName, nVal))
		{
			// only update map if read was successful (forces try to re-read next get)
			mapuint64[pwszValueName] = nVal;			
		}
		return nVal;
	}
	nVal = mapuint64[pwszValueName];
	return true;
}

CString ProductOptions::get_value(const WCHAR* pwszValueName, const WCHAR *pwszDefault)
{
	CString csVal = pwszDefault;
	if (mapstr.find(pwszValueName) == mapstr.end())
	{
		if (read_value(pwszValueName, csVal))
		{
			// only update map if read was successful (forces try to re-read next get)
			mapstr[pwszValueName] = csVal;			
		}
		return csVal;
	}
	csVal = mapstr[pwszValueName];
	return csVal;
}

// erasure from map forces reload next time get_value is called
bool ProductOptions::set_value(const WCHAR* pwszValueName, const bool bVal)
{
	mapBools.erase(pwszValueName);
	return write_value(pwszValueName, bVal);
}

bool ProductOptions::set_value(const WCHAR* pwszValueName, const BOOL bVal)
{
	return set_value(pwszValueName, static_cast<const bool>(bVal));
}

bool ProductOptions::set_value(const WCHAR* pwszValueName, const unsigned nVal)
{
	mapuint32.erase(pwszValueName);
	return write_value(pwszValueName, nVal);
}

bool ProductOptions::set_value(const WCHAR* pwszValueName, const DWORD bVal)
{
	return set_value(pwszValueName, static_cast<const unsigned>(bVal));
}

bool ProductOptions::set_value(const WCHAR* pwszValueName, const unsigned long long nVal)
{
	mapuint64.erase(pwszValueName);
	return write_value(pwszValueName, nVal);
}

bool ProductOptions::set_value(const WCHAR* pwszValueName, const WCHAR* val)
{
	mapstr.erase(pwszValueName);
	return write_value(pwszValueName, val);
}

bool ProductOptions::read_value(const WCHAR* pwszValueName, bool& bResult)
{
	// boolean is stored as REG_DWORD, defer to it
	unsigned dwVal = 0;
	bool bRet = read_value(pwszValueName, dwVal);
	bResult = dwVal ? true : false;
	return bRet;
}

bool ProductOptions::read_value(const WCHAR* pwszValueName, unsigned& nResult)
{
	bool bRet = false;
	HKEY hKey;
	DWORD dwDispo = 0;

	nResult = 0;

	if (RegCreateKeyEx(hHive,
		csKeyname.GetBuffer(),
		0, NULL, 0, KEY_QUERY_VALUE, NULL, &hKey, &dwDispo) == ERROR_SUCCESS)
	{
		DWORD dwVal = 0;
		DWORD dwSize = sizeof(DWORD);
		DWORD dwType = REG_DWORD;

		if (RegQueryValueEx(hKey, pwszValueName, 0, &dwType, (LPBYTE)& dwVal, &dwSize) == ERROR_SUCCESS)
		{
			bRet = true;
			nResult = static_cast<unsigned>(dwVal);
		}
		RegCloseKey(hKey);
	}
	return bRet;
}

bool ProductOptions::read_value(const WCHAR* pwszValueName, unsigned long long& nResult)
{
	bool bRet = false;
	HKEY hKey;
	DWORD dwDispo = 0;

	nResult = 0;

	if (RegCreateKeyEx(hHive,
		csKeyname.GetBuffer(),
		0, NULL, 0, KEY_QUERY_VALUE, NULL, &hKey, &dwDispo) == ERROR_SUCCESS)
	{
		unsigned long long nLoaded = 0;
		DWORD dwSize = sizeof(nLoaded);
		DWORD dwType = REG_DWORD;

		if (RegQueryValueEx(hKey, pwszValueName, 0, &dwType, (LPBYTE)& nLoaded, &dwSize) == ERROR_SUCCESS)
		{
			bRet = true;
			nResult = static_cast<unsigned long long>(nLoaded);
		}
		RegCloseKey(hKey);
	}
	return bRet;

}

bool ProductOptions::read_value(const WCHAR* pwszValueName, CString& csResult)
{
	bool bRet = false;
	HKEY hKey;
	DWORD dwDispo = 0;

	csResult.Empty();

	if (RegCreateKeyEx(hHive,
		csKeyname.GetBuffer(),
		0, NULL, 0, KEY_QUERY_VALUE, NULL, &hKey, &dwDispo) == ERROR_SUCCESS)
	{
		LPWSTR pwszVal;
		DWORD dwSize = 0;
		DWORD dwType = REG_SZ;

		if (RegQueryValueEx(hKey, pwszValueName, 0, &dwType, NULL, &dwSize) == ERROR_SUCCESS
			&& dwSize)
		{
			pwszVal = new WCHAR[dwSize + 1];
			memset(pwszVal, 0, (dwSize + 1) * sizeof(WCHAR));
			if (RegQueryValueEx(hKey, pwszValueName, 0, &dwType, (LPBYTE)pwszVal, &dwSize) == ERROR_SUCCESS)
			{
				bRet = true;
				csResult = pwszVal;
			}
			delete pwszVal;
		}
		RegCloseKey(hKey);
	}
	return bRet;
}

bool ProductOptions::write_value(const WCHAR* pwszValueName, const bool bVal)
{
	// boolean stored as REG_DWORD, defer to it
	unsigned dwVal = bVal ? 1 : 0;
	return write_value(pwszValueName, dwVal);
}

bool ProductOptions::write_value(const WCHAR* pwszValueName, const unsigned nVal)
{
	bool bRet = false;
	HKEY hKey;
	DWORD dwDispo = 0;

	if (RegCreateKeyEx(hHive,
		csKeyname.GetBuffer(),
		0, NULL, 0, KEY_SET_VALUE, NULL, &hKey, &dwDispo) == ERROR_SUCCESS)
	{
		if (RegSetValueEx(hKey, pwszValueName, 0, REG_DWORD, (LPBYTE)& nVal, sizeof(nVal)) == ERROR_SUCCESS)
		{
			bRet = true;
		}
		RegCloseKey(hKey);
	}
	return bRet;
}

bool ProductOptions::write_value(const WCHAR* pwszValueName, const unsigned long long nVal)
{
	bool bRet = false;
	HKEY hKey;
	DWORD dwDispo = 0;

	if (RegCreateKeyEx(hHive,
		csKeyname.GetBuffer(),
		0, NULL, 0, KEY_SET_VALUE, NULL, &hKey, &dwDispo) == ERROR_SUCCESS)
	{
		if (RegSetValueEx(hKey, pwszValueName, 0, REG_QWORD, (LPBYTE)& nVal, sizeof(nVal)) == ERROR_SUCCESS)
		{
			bRet = true;
		}
		RegCloseKey(hKey);
	}
	return bRet;

}

bool ProductOptions::write_value(const WCHAR* pwszValueName, const WCHAR* val)
{
	bool bRet = false;
	HKEY hKey;
	DWORD dwDispo = 0;

	if (RegCreateKeyEx(hHive,
		csKeyname.GetBuffer(),
		0, NULL, 0, KEY_SET_VALUE, NULL, &hKey, &dwDispo) == ERROR_SUCCESS)
	{
		if (val && val[0])
		{
			if (RegSetValueEx(hKey, pwszValueName, 0, REG_SZ, (LPBYTE)val, static_cast<DWORD>(wcslen(val) * sizeof(WCHAR))) == ERROR_SUCCESS)
			{
				bRet = true;
			}
		}
		else
		{
			// set empty value
			if (RegSetValueEx(hKey, pwszValueName, 0, REG_SZ, NULL, 0) == ERROR_SUCCESS)
			{
				bRet = true;
			}
		}
		RegCloseKey(hKey);
	}

	return bRet;
}

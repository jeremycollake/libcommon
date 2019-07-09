#pragma once

/*
* (c)2019 Jeremy Collake <jeremy@bitsum.com>
* https://bitsum.com/portfolio/coreprio
* See LICENSE.TXT
*/
#pragma once

#include <map>
#include <atlstr.h>

// registry access with local cache
// ::get_value uses in-memory maps, filled dynamically as required by read_value
// ::refresh forces reload of maps on next get of each option
// ::write_value writes directly and updates map

class ProductOptions
{
	HKEY hHive;
	CString csKeyname;

	// map of options of various data types
	std::map<CString, bool> mapBools;
	std::map<CString, unsigned> mapuint32;
	std::map<CString, unsigned long long> mapuint64;
	std::map<CString, CString> mapstr;

	// overloaded for specific data types
	bool read_value(const WCHAR* pwszValueName, bool& bResult);
	bool read_value(const WCHAR* pwszValueName, unsigned& nResult);
	bool read_value(const WCHAR* pwszValueName, unsigned long long& nResult);
	bool read_value(const WCHAR* pwszValueName, CString& csResult);

	bool write_value(const WCHAR* pwszValueName, const bool bVal);
	bool write_value(const WCHAR* pwszValueName, const unsigned nVal);
	bool write_value(const WCHAR* pwszValueName, const unsigned long long nVal);
	bool write_value(const WCHAR* pwszValueName, const WCHAR* val);
public:
	ProductOptions(const HKEY hHive, const WCHAR* pwszProductName);

	// force a reload of any referenced options
	void refresh();

	// returns false if doesn't exist in registry
	bool& operator[] (const WCHAR* pwszValueName);	// let boolvals (only) get referenced by subscript
	bool get_value(const WCHAR* pwszValueName, bool bDefault=false);
	BOOL get_value(const WCHAR* pwszValueName, BOOL bDefault=FALSE);
	unsigned get_value(const WCHAR* pwszValueName, unsigned nDefault=0);
	DWORD get_value(const WCHAR* pwszValueName, DWORD nDefault=0);
	unsigned long long get_value(const WCHAR* pwszValueName, unsigned long long nDefault=0);
	CString get_value(const WCHAR* pwszValueName, const WCHAR *pwszDefault=NULL);

	// returns false if registry write failed
	bool set_value(const WCHAR* pwszValueName, const bool bVal);
	bool set_value(const WCHAR* pwszValueName, const BOOL bVal);
	bool set_value(const WCHAR* pwszValueName, const unsigned nVal);
	bool set_value(const WCHAR* pwszValueName, const DWORD nVal);
	bool set_value(const WCHAR* pwszValueName, const unsigned long long nVal);
	bool set_value(const WCHAR* pwszValueName, const WCHAR* val);
};

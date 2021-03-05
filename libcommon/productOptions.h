#pragma once
/*
* (c)2021 Jeremy Collake <jeremy@bitsum.com>
* https://bitsum.com
* See LICENSE.TXT
*/

#include <map>
// ATL::CString has built-in formatting and tokenization that std::string lacks, and this *is* Windows-centric code
#include <atlstr.h>

// registry access with local cache
// ::get_value uses in-memory maps, filled dynamically as required by read_value
// ::refresh forces reload of maps on next get of each option
// ::write_value writes directly and updates map
// ::delete_value removes value from backing store (registry hive)

class ProductOptions
{
	HKEY _hHive;
	DWORD _Wow64Access;	// e.g. KEY_WOW64_32KEY, see https://docs.microsoft.com/en-us/windows/win32/winprog64/accessing-an-alternate-registry-view
	ATL::CString csKeyname;

	// map of options of various data types
	std::map<ATL::CString, bool> mapBools;
	std::map<ATL::CString, unsigned> mapuint32;
	std::map<ATL::CString, unsigned long long> mapuint64;
	std::map<ATL::CString, ATL::CString> mapstr;

	// overloaded for specific data types
	bool read_value(const WCHAR* pwszValueName, bool& bResult);
	bool read_value(const WCHAR* pwszValueName, unsigned& nResult);
	bool read_value(const WCHAR* pwszValueName, unsigned long long& nResult);
	bool read_value(const WCHAR* pwszValueName, ATL::CString& csResult);

	bool write_value(const WCHAR* pwszValueName, const bool bVal);
	bool write_value(const WCHAR* pwszValueName, const unsigned nVal);
	bool write_value(const WCHAR* pwszValueName, const unsigned long long nVal);
	bool write_value(const WCHAR* pwszValueName, const WCHAR* val);
public:
	ProductOptions(const HKEY hHive, const WCHAR* pwszProductName, const DWORD Wow64Access=0);

	// force a reload of any referenced options
	void clear_cache();
	// remove a specific option from cache
	void clear_cached_value(const WCHAR* pwszValueName);

	// returns false if doesn't exist in registry
	bool& operator[] (const WCHAR* pwszValueName);	// let boolvals (only) get referenced by subscript

	// gets returns false if default used, otherwise true
	bool get_value(const WCHAR* pwszValueName, bool& bVal, const bool bDefault=false);
	bool get_value(const WCHAR* pwszValueName, int& nVal, const int nDefault=0);
	bool get_value(const WCHAR* pwszValueName, unsigned& nVal, const unsigned nDefault=0);
	bool get_value(const WCHAR* pwszValueName, DWORD& nVal, const DWORD nDefault=0);	
	bool get_value(const WCHAR* pwszValueName, unsigned long long &nVal, const unsigned long long nDefault=0);
	bool get_value(const WCHAR* pwszValueName, ATL::CString& csVal, const WCHAR *pwszDefault=NULL);

	// returns false if registry write failed
	bool set_value(const WCHAR* pwszValueName, const bool bVal);
	bool set_value(const WCHAR* pwszValueName, const int nVal);
	bool set_value(const WCHAR* pwszValueName, const unsigned nVal);
	bool set_value(const WCHAR* pwszValueName, const DWORD nVal);
	bool set_value(const WCHAR* pwszValueName, const unsigned long long nVal);
	bool set_value(const WCHAR* pwszValueName, const WCHAR* val);	

	bool delete_value(const WCHAR* pwszValueName);
};

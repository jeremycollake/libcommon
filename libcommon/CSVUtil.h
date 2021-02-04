#pragma once
#include <string>
#include <locale>
#include <codecvt>
#include <vector>
#include "DebugOutToggles.h"

class CSVUtil
{
public:
	std::wstring ConvertUTF8ToWSTR(const std::string& source)
	{		                
        int size = MultiByteToWideChar(CP_UTF8, 0, source.c_str(),
            static_cast<int>(source.length()), nullptr, 0);
        std::wstring utf16_str(size, '\0');
        MultiByteToWideChar(CP_UTF8, 0, source.c_str(),
			static_cast<int>(source.length()), &utf16_str[0], size);
        return utf16_str;
        /*		
		try {
        std::u16string u16_conv = std::wstring_convert<
            std::codecvt_utf8_utf16<char16_t>, char16_t>{}.from_bytes(source);
        std::wstring retStr(u16_conv.begin(), u16_conv.end());        
		}
		catch (const std::range_error&) {
			LIBCOMMON_DEBUG_PRINT(L"utf range error");
		}
		return strRet;*/
	}
	std::string ConvertUTF16ToUTF8(const std::wstring& source)
	{
		std::string strRet;
		try	{
			std::u16string u16str(source.begin(), source.end());
			std::wstring_convert<std::codecvt_utf8_utf16<char16_t>, char16_t> convert;
			strRet=convert.to_bytes(u16str);
		}
		catch (const std::range_error&) {
			LIBCOMMON_DEBUG_PRINT(L"ERROR: utf range error");
		}
		return strRet;
	}	
    std::string ConvertUTF16ToUTF8(const ATL::CString& source)
    {
		std::string strRet;
		try {
			std::wstring wsource(source);
			std::u16string u16str(wsource.begin(), wsource.end());
			std::wstring_convert<std::codecvt_utf8_utf16<char16_t>, char16_t> convert;
			strRet=convert.to_bytes(u16str);
		}
		catch (const std::range_error&) {
			LIBCOMMON_DEBUG_PRINT(L"ERROR: utf range error");
		}
		return strRet;
    }
	ATL::CString EscapeField(const WCHAR* pwszOriginal, bool bEscapeCommas = false)
	{
		CString csRet = pwszOriginal;
		csRet.Replace(L"'", L"''");
		csRet.Replace(L"\"", L"\"\"");
		// not normally necessary for most csv tooling
		// use of this will cause strings with commas in CrowdIn to be changed to have embedded escaped commas
		if (bEscapeCommas)
		{
			csRet.Replace(L",", L"\\,");	
		}
		return csRet;
	}
	ATL::CString UnescapeField(const WCHAR* pwszOriginal)
	{
		CString csRet = pwszOriginal;
		csRet.Replace(L"\\,", L",");
		csRet.Replace(L"\"\"", L"\"");
		csRet.Replace(L"''", L"'");		
		return csRet;
	}
    size_t ParseCSVRow(_In_ const std::wstring& row, _Out_ std::vector<std::wstring>& fields) 
    {
		const wchar_t *SpecialCharForEmbeddedCommas = L"\xffff";
		ATL::CString rowCopy(row.c_str());
        rowCopy.Replace(L"\\,", SpecialCharForEmbeddedCommas);
		rowCopy.Replace(L"\r", L"");
		rowCopy.Replace(L"\n", L"");
        int pos = 0;
        ATL::CString token = rowCopy.Tokenize(L",", pos);
        while (!token.IsEmpty())
        {
            token.Replace(SpecialCharForEmbeddedCommas, L",");
			token.TrimRight();
			token.TrimLeft();
			UnescapeField(token);
			// strip leading and trailing quotes, if both present
			int len = token.GetLength();
			if (token[0] == '"' && token[len - 1] == '"')
			{
				token.Truncate(len - 1);
				token.Delete(0, 1);
			}
            fields.push_back(std::wstring(token));
            token = rowCopy.Tokenize(L",", pos);			
        }        
        return fields.size();
    }


};

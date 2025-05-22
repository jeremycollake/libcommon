#pragma once

#include <string>

namespace libcommon
{
	// tokenizer that returns empty tokens if there are consecutive delimiters, unlike strtok, etc...
	std::vector<std::wstring> tokenize(const wchar_t* str, const wchar_t* delimiters);
}
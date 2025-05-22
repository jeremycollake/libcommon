#pragma once

#include "pch.h"
#include "stringFuncs.h"
#include <string>
#include <vector>

namespace libcommon
{
	// tokenizer that returns empty tokens if there are consecutive delimiters, unlike strtok, etc...	
	std::vector<std::wstring> tokenize(const wchar_t* str, const wchar_t* delimiters)
	{
		std::vector<std::wstring> tokens;
		if (str == nullptr)
		{
			return tokens;
		}
		const wchar_t* current = str;
		const wchar_t* next = nullptr;
		while (*current != '\0')
		{
			next = wcspbrk(current, delimiters);
			if (next == nullptr)
			{
				tokens.push_back(current);
				break;
			}
			else
			{
				if (next == current)
				{
					tokens.push_back(L"");
				}
				else
				{
					tokens.push_back(std::wstring(current, next - current));
				}
				current = next + 1;
			}
		}
		return tokens;
	}
}

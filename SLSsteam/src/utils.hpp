#pragma once

#include <cstdint>
#include <string>
#include <vector>


namespace Utils
{
	bool isNumber(const char* str);

	template<typename T>
	bool tryConvertToNumber(const char* str, T& out)
	{
		if constexpr (std::is_same_v<T, int32_t>)
		{
			if (!isNumber(str))
			{
				return false;
			}

			out = std::stoi(str);
			return true;
		}

		else if constexpr (std::is_same_v<T, uint32_t>)
		{
			if (!isNumber(str))
			{
				return false;
			}

			out = std::stoul(str);
			return true;
		}

		//TODO: Add GCC error when compiling this path
		return false;
	}

	std::vector<std::string> strsplit(char* str, const char* delimeter);
	std::string getFileSHA256(const char* filePath);
}

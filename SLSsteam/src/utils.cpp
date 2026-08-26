#include "utils.hpp"

#include <cctype>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <string>
#include <vector>

#include <openssl/sha.h>


bool Utils::isNumber(const char* str)
{
	const unsigned int len = strlen(str);
	if (len < 1)
	{
		return false;
	}

	for (unsigned int i = 0; i < len; i++)
	{
		const char c = str[i];

		if (!std::isdigit(c))
		{
			return false;
		}
	}

	return true;
}

std::vector<std::string> Utils::strsplit(char *str, const char *delimeter)
{
	auto splits = std::vector<std::string>();

	char* split = strtok(str, delimeter);
	splits.emplace(splits.end(), std::string(split));

	while(split)
	{
		split = strtok(nullptr, delimeter);
		if (!split)
		{
			break;
		}

		splits.emplace(splits.end(), std::string(split));
	}

	return splits;
}

std::string Utils::getFileSHA256(const char *filePath)
{
	std::ifstream fs(filePath, std::ios::binary);
	if (!fs.is_open())
	{
		//TODO: Read more about error types in C++ :)
		throw std::runtime_error("Unable to read file!");
	}

	const auto bytes = std::vector<unsigned char>(std::istreambuf_iterator(fs), {});
	unsigned char sha256Bytes[SHA256_DIGEST_LENGTH];
	SHA256(bytes.data(), bytes.size(), sha256Bytes);

	std::ostringstream sha256;
	for (int i = 0; i < SHA256_DIGEST_LENGTH; i++)
	{
		sha256 << std::hex << std::setw(2) << std::setfill('0') << (int)sha256Bytes[i];
	}

	fs.close();
	return sha256.str();
}


#include "utils.hpp"

#include <cstring>
#include <fstream>
#include <iomanip>
#include <string>
#include <vector>

#include <openssl/sha.h>

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

	std::vector<unsigned char> bytes(std::istreambuf_iterator<char>(fs), {});
	unsigned char sha256Bytes[SHA256_DIGEST_LENGTH];
	SHA256(bytes.data(), bytes.size(), sha256Bytes);

	std::stringstream sha256;
	for(int i = 0; i < SHA256_DIGEST_LENGTH; i++)
	{
		sha256 << std::hex << std::setw(2) << std::setfill('0') << (int)sha256Bytes[i];
	}

	fs.close();
	return sha256.str();
}


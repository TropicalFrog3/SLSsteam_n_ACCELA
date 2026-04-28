#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <link.h>
#include <set>
#include <string>
#include <unordered_set>


//std::ofstream logFile("/tmp/env-mod.log");

static std::unordered_set<std::string> LIBS =
{
	"libcurl.so",
	"libcurl.so.3",
	"libcurl.so.4",
	"libcurl.so.4.0.0",
	"libcurl.so.4.1.0",
	"libcurl.so.4.2.0",
	"libcurl.so.4.3.0",
	"libcurl.so.4.4.0",
	"libcurl.so.4.5.0",
	"libcurl.so.4.6.0",
	"libcurl.so.4.7.0",
	"libcurl.so.4.8.0",
};

static std::unordered_set<std::string> DIRS =
{
	//Fallback for 32 bit OS?
	"/usr/local/lib",
	"/usr/lib",

	"/usr/local/lib32", //Bazzite
	"/usr/lib/i386-linux-gnu/", //Mint
	"/usr/lib32", //SteamOS
};

char *la_objsearch(const char *name, __attribute__((unused)) uintptr_t *cookie, unsigned int flag)
{
	if (flag != LA_SER_ORIG)
	{
		return const_cast<char*>(name);
	}

	//logFile << "objopen: " << name << std::endl;

	if(LIBS.contains(name))
	{
		//logFile << "Replacing: " << name << std::endl;

		for(const auto& dir : DIRS)
		{
			char path[255];
			snprintf(path, sizeof(path), "%s/%s", dir.c_str(), name);
			//logFile << "Path: " << path << std::endl;

			if (!std::filesystem::exists(path))
			{
				continue;
			}

			char* n = reinterpret_cast<char*>(malloc(sizeof(path)));
			if (!n)
			{
				//logFile << "Failed to allocate char*!\n";
				break;
			}

			strcpy(n, path);

			return n;
		}
	}

	return const_cast<char*>(name);
}

unsigned int la_version(unsigned int)
{
	return LAV_CURRENT;
}

//void la_preinit(__attribute__((unused)) uintptr_t *cookie)
//{
//}

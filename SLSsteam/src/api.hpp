#pragma once

#include <fstream>


class CFileWatcher;

namespace SLSAPI
{
	extern const char* path;
	extern std::fstream fstream;
	extern CFileWatcher* watcher;

	bool isEnabled();
	void onFileChange(const char* filename);
	void init();
}

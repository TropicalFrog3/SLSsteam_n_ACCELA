#pragma once

#include "sdk/sdk.hpp"

#include <cstdint>
#include <fstream>
#include <mutex>
#include <vector>


class CFileWatcher;

namespace SLSAPI
{
	struct CompatOp_t
	{
		enum class OpType
		{
			Dump,
			Get,
			Set
		};

		OpType type;
		AppId_t appId;
		std::string tool;
	};

	struct LibraryOp_t
	{
		enum class OpType
		{
			Dump,
			Install,
			Uninstall
		};

		OpType type;
		AppId_t appId;
		uint32_t libraryIndex;
	};

	extern const char* path;
	extern std::fstream fstream;
	extern CFileWatcher* watcher;
	extern bool initialized;

	extern std::mutex cmdMutex;

	extern std::vector<CompatOp_t> compatOps;
	extern std::vector<LibraryOp_t> libraryOps;

	bool isEnabled();
	void onFileChange(const char* filename);
	void init();

	void runCompatOps();
	void runInstallOps();
	void runIPCFrame();
}

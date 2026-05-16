#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>

class CProtoBufMsgBase;

namespace DepotKeys
{
	// depot_id -> hex-encoded decryption key
	using KeyMap = std::unordered_map<uint32_t, std::string>;

	// Parse all *.lua files in stplug-in/ and extract depot keys
	// from addappid(depot_id, 0, "hex_key") calls
	void scanLuaPluginsForDepotKeys();

	// Get the cached depot key for a given depot_id (returns empty string if not found)
	std::string getDepotKey(uint32_t depotId);

	// Check if an app is managed by ACCELA (has a .lua plugin file)
	bool isAccelaApp(uint32_t appId);

	// Handle incoming protobuf messages — intercept failed depot decryption key responses
	void recvMsg(CProtoBufMsgBase* msg);

	// Handle outgoing protobuf messages — log depot key requests for debugging
	void sendMsg(CProtoBufMsgBase* msg);
}

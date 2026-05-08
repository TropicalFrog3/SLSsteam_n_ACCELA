#pragma once

#include <cstdint>

class CProtoBufMsgBase;

namespace Achievements
{
	void getPlayerStats(uint32_t& eresult);
	void recvMessage(const CProtoBufMsgBase* msg);
}

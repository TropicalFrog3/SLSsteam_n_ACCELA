#pragma once

#include "../sdk/sdk.hpp"
class CProtoBufMsgBase;

namespace Misc
{
	bool shouldFakeOffline();
	void recvMsg(CNetPacket* pkt);
	void sendMsg(CNetPacket* pkt);
}

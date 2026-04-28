#pragma once

class CProtoBufMsgBase;

namespace Misc
{
	bool shouldFakeOffline();
	void recvMsg(CProtoBufMsgBase* msg);
	void sendMsg(CProtoBufMsgBase* msg);
}

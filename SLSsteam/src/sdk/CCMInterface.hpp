#pragma once

#include "CNetPacket.hpp"
#include "types.hpp"


SDK_Class CCMInterface
{
public:
	//Before using make sure to:
	//Create header with steamId & realm
	//Create body
	//Serialize
	//Set type with ProtoBuf mask
	//Set refs to 1 (not doing this will debugbreak() in a failed assert)
	void recvPkt(CNetPacket* pkt);
	void recvPktHk(CNetPacket* pkt);
};

extern CCMInterface* g_pCMInterface;

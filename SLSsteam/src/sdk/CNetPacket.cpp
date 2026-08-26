#include "CNetPacket.hpp"


std::string CNetPacket::getProtoBufTypeName() const
{
	auto name = std::string("Unknown");

	if (!isProtoBuf())
	{
		return name;
	}

	const EMsg type = getProtoBufType();
	if (!EMsg_IsValid(type))
	{
		return name;
	}

	return EMsg_Name(type);
}

CMsgProtoBufHeader CNetPacket::deserializeHeader() const
{
	const uintptr_t headerOffset = sizeof(CNetPacketBody);
	uint8_t* mem = reinterpret_cast<uint8_t*>(body) + headerOffset;

	CMsgProtoBufHeader header;
	if (!header.ParseFromArray(mem, body->headerSize))
	{
		LOG_ERROR("Failed to parse header!\n");
	}

	return header;
}

void CNetPacket::free()
{
	Steam::Plat_Free(body);

	size = 0;
	body = nullptr;
	originalBody = nullptr;
}

#include "CProtoBufMsgBase.hpp"

#include "../hooks.hpp"


uint32_t CProtoBufMsgBase::send()
{
	return Hooks::CProtoBufMsgBase_Send.tramp.fn(this);
}

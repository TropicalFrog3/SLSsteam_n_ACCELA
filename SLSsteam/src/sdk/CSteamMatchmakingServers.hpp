#pragma once

#include <cstdint>


struct servernetadr_t
{
	uint16_t connectPort; //0x0
	uint16_t queryPort; //0x2
	uint32_t ip; //0x4
}; //0x8

struct gameserverdetails_t
{
public:
	servernetadr_t address; //0x0
	char __pad_0x0[0x88]; //0x8
	uint32_t appId;
};

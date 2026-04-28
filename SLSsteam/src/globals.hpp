#pragma once

#include "libmem/libmem.h"

#include <cstdint>
#include <cstdio>


extern lm_module_t g_modSteamClient;
extern lm_module_t g_modSteamUI;

//Don't assign a pointer to IClientUser::GetSteamID! It's returned pointer
//always points to the same address, but it's lifetime is very short
extern uint32_t g_currentSteamId;

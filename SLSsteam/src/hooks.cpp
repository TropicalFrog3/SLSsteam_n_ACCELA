#include "hooks.hpp"

#include "feats/achievements.hpp"
#include "feats/apps.hpp"
#include "feats/dlc.hpp"
#include "feats/misc.hpp"
#include "feats/fakeappid.hpp"
#include "feats/ticket.hpp"

#include "api.hpp"
#include "config.hpp"
#include "decompiler.hpp"
#include "globals.hpp"
#include "log.hpp"
#include "memhlp.hpp"
#include "patterns.hpp"
#include "vftableinfo.hpp"
#include "feats/depotkeys.hpp"
#include "libmem/libmem.h"

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <memory>
#include <mutex>
#include <pthread.h>
#include <strings.h>
#include <unistd.h>
#include <vector>
#include <sys/wait.h>
#include <sys/types.h>

#include <curl/curl.h>
#include <map>
#include <mutex>
#include <thread>
#include <string>

static std::map<uint32_t, std::string> g_appSizes;
static std::mutex g_appSizesMutex;

static size_t WriteCallback(void *contents, size_t size, size_t nmemb, void *userp)
{
    ((std::string*)userp)->append((char*)contents, size * nmemb);
    return size * nmemb;
}

static void FetchAppSizeThread(uint32_t appId)
{
    CURL *curl;
    CURLcode res;
    std::string readBuffer;

    curl = curl_easy_init();
    if(curl) {
        char url[128];
        snprintf(url, sizeof(url), "https://api.steamcmd.net/v1/info/%u", appId);
        curl_easy_setopt(curl, CURLOPT_URL, url);
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &readBuffer);
        curl_easy_setopt(curl, CURLOPT_TIMEOUT, 5L); 
        res = curl_easy_perform(curl);
        curl_easy_cleanup(curl);
        
        if (res == CURLE_OK) {
            uint64_t totalSize = 0;
            size_t depotPos = 0;
            const char* depotsStart = "\"depots\": {";
            size_t depotsIdx = readBuffer.find(depotsStart);
            
            if (depotsIdx != std::string::npos) {
                uint64_t commonSize = 0;
                uint64_t linuxSize = 0;
                uint64_t windowsSize = 0;
                uint64_t macosSize = 0;
                uint64_t otherTotal = 0;
                uint32_t otherCount = 0;

                size_t pos = depotsIdx + strlen(depotsStart);
                while (true) {
                    size_t sizeIdx = readBuffer.find("\"size\": \"", pos);
                    if (sizeIdx == std::string::npos) break;

                    size_t blockStart = readBuffer.rfind("{", sizeIdx);
                    std::string block = (blockStart != std::string::npos) ? readBuffer.substr(blockStart, 1024) : "";

                    std::string oslist;
                    size_t oslistIdx = block.find("\"oslist\": \"");
                    if (oslistIdx != std::string::npos) {
                        size_t osValStart = oslistIdx + 11;
                        size_t osValEnd = block.find("\"", osValStart);
                        if (osValEnd != std::string::npos) {
                            oslist = block.substr(osValStart, osValEnd - osValStart);
                        }
                    }

                    uint64_t size = 0;
                    size_t valStart = sizeIdx + 9;
                    size_t valEnd = readBuffer.find("\"", valStart);
                    if (valEnd != std::string::npos) {
                        try { size = std::stoull(readBuffer.substr(valStart, valEnd - valStart)); } catch(...) {}
                    }

                    if (oslist.empty()) {
                        commonSize += size;
                    } else if (oslist.find("linux") != std::string::npos) {
                        linuxSize += size;
                    } else if (oslist.find("windows") != std::string::npos) {
                        windowsSize += size;
                    } else if (oslist.find("macos") != std::string::npos) {
                        macosSize += size;
                    } else {
                        otherTotal += size;
                        otherCount++;
                    }
                    pos = sizeIdx + 9;
                }

                if (linuxSize > 0) totalSize = commonSize + linuxSize;
                else if (windowsSize > 0) totalSize = commonSize + windowsSize;
                else if (macosSize > 0) totalSize = commonSize + macosSize;
                else if (otherCount > 0) totalSize = commonSize + (otherTotal / otherCount);
                else totalSize = commonSize;
            }

            if (totalSize > 0) {
                LOG_INFO("Fetched size for appId %u: %llu\n", appId, totalSize);
                std::lock_guard<std::mutex> lock(g_appSizesMutex);
                g_appSizes[appId] = std::to_string(totalSize);
            } else {
                LOG_INFO("Fetched size for appId %u was 0\n", appId);
            }
        } else {
            LOG_INFO("Failed to fetch size for appId %u: %s\n", appId, curl_easy_strerror(res));
        }
    }
}

static int32_t hkClientApps_GetAppData(void* pClientApps, uint32_t appId, const char* name, char* pChOut, uint32_t outSize)
{


    int32_t ret = Hooks::IClientApps_GetAppData.originalFn.fn(pClientApps, appId, name, pChOut, outSize);
    
    // Log ALL calls to GetAppData to verify it's being called at all
    LOG_ONCE("GetAppData CALLED (appId: %u, name: %s) -> ret: %d\n", appId, name ? name : "NULL", ret);

    if (name && (strcmp(name, "common/oslist") == 0 || strcmp(name, "oslist") == 0))
    {
        if (!pChOut || outSize < 6) return 6;
        strncpy(pChOut, "linux", outSize);
        pChOut[outSize - 1] = '\0';
        LOG_INFO("Spoofed oslist for app %u to 'linux'\n", appId);
        return 6;
    }

    if (name && (strcmp(name, "common/size_on_disk") == 0 || strcmp(name, "SizeOnDisk") == 0 || strcmp(name, "install_size") == 0))
    {
        if (ret <= 0 || !pChOut || strlen(pChOut) == 0 || strcmp(pChOut, "0") == 0)
        {
            std::lock_guard<std::mutex> lock(g_appSizesMutex);
            if (g_appSizes.find(appId) != g_appSizes.end())
            {
                if (g_appSizes[appId] != "0") {
                    LOG_INFO("Injecting size for appId %u (%s): %s\n", appId, name, g_appSizes[appId].c_str());
                    if (outSize > g_appSizes[appId].length()) {
                        strncpy(pChOut, g_appSizes[appId].c_str(), outSize);
                        return strlen(pChOut);
                    }
                }
            }
            else
            {
                LOG_INFO("Fetching size for appId %u\n", appId);
                g_appSizes[appId] = "0"; // prevent multiple threads
                std::thread(FetchAppSizeThread, appId).detach();
            }
        }
    }
    return ret;
}

// Hook for getAppDataSection (VFT[5]) - intercepts bulk VDF section queries
static uint32_t hkClientApps_GetAppDataSection(void* pClientApps, uint32_t appId, int section, char* pChOut, uint32_t outSize)
{


    uint32_t ret = Hooks::IClientApps_GetAppDataSection.originalFn.fn(pClientApps, appId, section, pChOut, outSize);
    
    if (ret > 0 && pChOut && (section == 2 || section == 4))
    {
        std::string path = "/tmp/section_" + std::to_string(section) + "_" + std::to_string(appId) + ".bin";
        FILE* f = fopen(path.c_str(), "wb");
        if (f)
        {
            fwrite(pChOut, 1, ret, f);
            fclose(f);
        }
    }
    
    return ret;
}
struct PendingInstallTask {
    IClientAppManager* pClientAppManager;
    uint32_t appId;
    uint32_t library;
    uint8_t a4;
    pid_t pid;
};
static PendingInstallTask g_pendingInstallQueue[16];
static int g_pendingInstallCount = 0;

template<typename T>
Hook<T>::Hook(const char* name)
{
	this->name = std::string(name);
}

template<typename T>
DetourHook<T>::DetourHook(const char* name) : Hook<T>::Hook(name)
{
	this->size = 0;
}

//TODO: Fix this ungodly mess
template<typename T>
DetourHook<T>::DetourHook() : DetourHook<T>("")
{

}

template<typename T>
VFTHook<T>::VFTHook(const char* name) : Hook<T>::Hook(name)
{
	this->hooked = false;
}

template<typename T>
VFTHook<T>::VFTHook() : VFTHook<T>("")
{

}

template<typename T>
bool DetourHook<T>::setup(const char* name, const lm_address_t fn, T hookFn)
{
	this->name = name;
	this->originalFn.address = fn;
	this->hookFn.fn = hookFn;

	return true;
}

template<typename T>
bool DetourHook<T>::setup(const Pattern_t& pattern, T hookFn)
{
	this->name = pattern.name;
	this->originalFn.address = pattern.address;
	this->hookFn.fn = hookFn;

	return true;
}

template<typename T>
bool DetourHook<T>::setup(const VFTableInfo_t& info, T hookFn)
{
	this->name = info.getPrintName();
	this->originalFn.address = info.address;
	this->hookFn.fn = hookFn;

	return true;
}

template<typename T>
void DetourHook<T>::place()
{
	this->size = LM_HookCode(this->originalFn.address, this->hookFn.address, &this->tramp.address);
	MemHlp::fixPICThunkCall(this->name.c_str(), this->originalFn.address, this->tramp.address);

	LOG_DEBUG
	(
		"Detour hooked %s (%p) with hook at 0x%x and tramp at 0x%x\n",
		this->name.c_str(),
		reinterpret_cast<void*>(this->originalFn.address),
		this->hookFn.address,
		this->tramp.address
	);
}

template<typename T>
void DetourHook<T>::remove()
{
	if (!this->size)
	{
		return;
	}

	LM_UnhookCode(this->originalFn.address, this->tramp.address, this->size);
	this->size = 0;

	LOG_DEBUG("Unhooked %s\n", this->name.c_str());
}

template<typename T>
void VFTHook<T>::place()
{
	LM_VmtHook(this->vft.get(), this->index, this->hookFn.address);
	this->hooked = true;

	LOG_DEBUG
	(
		"VFT hooked %s (0x%x) with hook at 0x%x\n",
		this->name.c_str(),
		this->originalFn.address,
		this->hookFn.address
	);
}

template<typename T>
void VFTHook<T>::remove()
{
	//No clue how libmem reacts when unhooking a non existent hook
	//so we do this
	if (!this->hooked)
	{
		return;
	}

	LM_VmtUnhook(this->vft.get(), this->index);
	this->hooked = false;

	LOG_DEBUG("Unhooked %s!\n", this->name.c_str());
}

template<typename T>
void VFTHook<T>::setup(std::shared_ptr<lm_vmt_t> vft, const VFTableInfo_t& info, T hookFn)
{
	this->vft = vft;
	this->index = info.index;
	this->name = info.getPrintName();

	this->originalFn.address = LM_VmtGetOriginal(this->vft.get(), this->index);
	this->hookFn.fn = hookFn;
}

__attribute__((hot))
static void hkTraceIPC(const char* iface, const char* fn)
{
	if (g_config.extendedLogging.get())
	{
		LOG_DEBUG
		(
			"%s(%s, %s)\n",

			Hooks::TraceIPC.name.c_str(),
			iface,
			fn
		);
	}

	LOG_TRACE("Calling tramp\n");
	Hooks::TraceIPC.tramp.fn(iface, fn);
}

static uint32_t hkAPIJob_SendAndRecv(CAPIJob* pAPIJob, CProtoBufMsgBase* send, uint32_t a2, uint32_t timeOut, CProtoBufMsgBase* recv, EMsg targetType)
{
	uint32_t ret = Achievements::sendAndRecvGetUserStats(pAPIJob, send, timeOut, recv, targetType);

	if (!ret)
	{
		LOG_TRACE("Calling tramp\n");
		ret = Hooks::CAPIJob_SendAndRecv.tramp.fn(pAPIJob, send, a2, timeOut, recv, targetType);
	}

	LOG_DEBUG
	(
		"%s(%p, %s, %u, %u, %s, %u) -> %u\n",

		Hooks::CAPIJob_SendAndRecv.name.c_str(),
		reinterpret_cast<void*>(pAPIJob),
		MemHlp::getTypeName(send),
		a2,
		timeOut,
		MemHlp::getTypeName(recv),
		targetType,
		ret
	);

	return ret;
}

static uint32_t hkAppDataCache_BParseResponseFromMessage(void* pAppDataCache, CProtoBufMsgBase* pMsg)
{
	LOG_TRACE("Calling tramp\n");
	const uint32_t ret = Hooks::CAppDataCache_BParseResponseFromMessage.tramp.fn(pAppDataCache, pMsg);

	LOG_DEBUG
	(
		"%s(%p, %p) -> %i\n",
		Patterns::CAppDataCache::BParseResponseMessage.name.c_str(),
		reinterpret_cast<void*>(pAppDataCache),
		reinterpret_cast<void*>(pMsg),
		ret
	);

	Apps::parseProductInfoFromResponse(pMsg->getBody<CMsgClientPICSProductInfoResponse>());

	return ret;
}

static uint32_t hkClientUnifiedServiceTransport_SendAndRecvMsg(CClientUnifiedServiceTransport* pUnifiedServiceTransport, const char* name, void* send, void* recv, void* arg4)
{
	uint32_t ret = Achievements::sendAndRecvGetPlayerStats
	(
		pUnifiedServiceTransport,
		name,
		reinterpret_cast<CPlayer_GetUserStats_Request*>(send),
		reinterpret_cast<CPlayer_GetUserStats_Response*>(recv)
	);

	if (ret == k_EResultNoResult)
	{
		LOG_TRACE("Calling tramp\n");
		ret = Hooks::CClientUnifiedServiceMethod_SendAndRecvMsg.tramp.fn(pUnifiedServiceTransport, name, send, recv, arg4);
	}

	Apps::sendAndRecvLastPlayedTimes(name, reinterpret_cast<CPlayer_GetLastPlayedTimes_Response*>(recv));

	LOG_DEBUG
	(
		"%s(%p, %s, %s, %s, %p) -> %u\n",

		Hooks::CClientUnifiedServiceMethod_SendAndRecvMsg.name.c_str(),
		reinterpret_cast<void*>(pUnifiedServiceTransport),
		name,
		MemHlp::getTypeName(send),
		MemHlp::getTypeName(recv),
		arg4,
		ret
	);

	return ret;

//	DepotKeys::recvMsg(pMsg);
//	DepotKeys::sendMsg(pMsg);


}

static void hkCMInterface_RecvPkt(CCMInterface* pCMInterface, CNetPacket* pNetPacket)
{
	LOG_DEBUG
	(
		"RecvPkt with CMInterface at %p %s -> 0x%x\n",
		reinterpret_cast<void*>(pCMInterface),
		pNetPacket->getProtoBufTypeName().c_str(),
		pNetPacket->getType()
	);

	if (pNetPacket->isValid() && pNetPacket->isProtoBuf())
	{
		const uint32_t type = pNetPacket->getProtoBufType();

		//Short observation reveals only one CMInterface, but just to be sure
		if (!g_pCMInterface && type == k_EMsgClientLogOnResponse)
		{
			g_pCMInterface = reinterpret_cast<CCMInterface*>(pCMInterface);
		}

		const auto header = pNetPacket->deserializeHeader();
		const bool disableFamilyShareLock = g_config.disableFamilyLock.get();

		if (disableFamilyShareLock && type == k_EMsgClientSharedLibraryStopPlaying)
		{
			LOG_DEBUG("Choking %s\n", pNetPacket->getProtoBufTypeName().c_str());
			pNetPacket->free();
			return;
		}

		if
		(
			disableFamilyShareLock
			&& type == k_EMsgServiceMethod
			&& header.has_target_job_name() //Do not modify header by blindly requesting the target_job_name
			&& header.target_job_name() == "FamilyGroupsClient.NotifyRunningApps#1"
		)
		{
			LOG_DEBUG("Choking %s\n", header.target_job_name().c_str());
			pNetPacket->free();
			return;
		}

		Misc::recvMsg(pNetPacket);
		Ticket::recvMsg(pNetPacket);
	}

	LOG_TRACE("Calling tramp\n");
	Hooks::CCMInterface_RecvPkt.tramp.fn(pCMInterface, pNetPacket);
}

//I don't like forward declerations, but with the current style & hooks layout it's a necessity
static CSteamId hkClientUser_GetSteamId(const CSteamId& steamId);

static uint32_t hkSteamEngine_ProcessIPCFrame(CSteamEngine* pSteamEngine, HSteamPipe hPipe, CUtlBuffer* pBufIn, CUtlBuffer* pBufOut)
{
	if (!g_pSteamEngine)
	{
		g_pSteamEngine = reinterpret_cast<CSteamEngine*>(pSteamEngine);
		LOG_DEBUG("g_pSteamEngine at %p\n", reinterpret_cast<void*>(g_pSteamEngine));
	}

	uint32_t ret;
	const bool log = g_config.extendedLogging.get();

	//pBufIn
	//mem + 0 : 1 = IPCCommand

	const EIPCCmd cmd = *reinterpret_cast<EIPCCmd*>(pBufIn->mem.base + 0);

	if (log)
	{
		LOG_DEBUG("ProcessIPCFrame 0x%x -> %s\n", hPipe, EIPCCmd_ToString(cmd).c_str());
	}

	if (cmd == EIPCCmd::RunInterface)
	{
		//We do not initialize with the CSteamEngine because first run CUser is null
		Hooks::placeVFTHooks();

		//While hooking this function to replace the other hooks might seem attractive
		//we do not do so. Many calls straight up bypass the IPC layer and go
		//straight for the original VFT implementations (IClientAppManager comes to mind)
		//Although it's a great spot to quickly test things

		//pBufIn
		//mem + 1 : 1 = interfaceType
		//mem + 2 : 4 = hSteamUser
		//mem + 6 : 4 = function Id
		//arguments follow
		//then fencepost?
		
		const EIPCInterface interface = *reinterpret_cast<EIPCInterface*>(pBufIn->mem.base + 1);
		const uint32_t function = *reinterpret_cast<uint32_t*>(pBufIn->mem.base + 6);

		if (log)
		{
			const auto utils = g_pSteamEngine->getUtils();
			LOG_DEBUG
			(
				"RunInterface %s 0x%x for %u (%u)\n",

				EIPCInterface_ToString(interface).c_str(),
				function,
				FakeAppIds::getRealAppIdForCurrentPipe(),
				utils ? utils->getAppId() : 0
			);
		}

		FakeAppIds::runIPCFrame(false, interface);

		LOG_TRACE("Calling tramp\n");
		ret = Hooks::CSteamEngine_ProcessIPCFrame.tramp.fn(pSteamEngine, hPipe, pBufIn, pBufOut);

		//LOG_DEBUG("In\n%s\n", MemHlp::hexdump(pBufIn->mem.base, pBufIn->offset).c_str());

		FakeAppIds::runIPCFrame(true, interface);

		//pBufOut
		//mem + 0 : 1 = EIPCExitCode
		//return values follow
		const EIPCExitCode exitCode = *reinterpret_cast<EIPCExitCode*>(pBufOut->mem.base + 0);

		//IClientUser::GetSteamID has been optimized to hell and back
		//So to hook it we need a naked function hook that requires quite the
		//complex logic to get the full steamId. So I made an exception for this function,
		//since it seems to always get called from RunInterface anyway
		//53                                      push    ebx
		//8B 54 24 0C                             mov     edx, [esp+4+arg_4]
		//8B 44 24 08                             mov     eax, [esp+4+arg_0]
		//8B 9A B2 E8 FF FF                       mov     ebx, [edx-174Eh] //SteamId low
		//8B 8A AE E8 FF FF                       mov     ecx, [edx-1752h] //SteamId high
		//89 58 04                                mov     [eax+4], ebx
		//89 08                                   mov     [eax], ecx
		//                                        //Optimally inject here, grab eax, copy into g_currentSteamId
		//5B                                      pop     ebx
		//C2 04 00                                retn    4
		if (interface == EIPCInterface::User && exitCode == EIPCExitCode::Success && function == 0xD6FC3200)
		{
			//Universe always set, steamId gets filled in after login
			CSteamId* id = reinterpret_cast<CSteamId*>(pBufOut->mem.base + 1);

			if (!g_currentSteamId.isSet() && id->isSet())
			{
				g_currentSteamId = CSteamId(id->steamId64);
			}

			*id = hkClientUser_GetSteamId(g_currentSteamId);
		}

		//LOG_DEBUG("Out\n%s\n", MemHlp::hexdump(pBufOut->mem.base, pBufOut->offset).c_str());

		// Constant background monitoring for accela-download
		if (g_pendingInstallCount > 0) {
			for (int i = 0; i < g_pendingInstallCount; ) {
				int status;
				pid_t res = waitpid(g_pendingInstallQueue[i].pid, &status, WNOHANG);
				if (res > 0) {
					uint32_t pendingAppId = g_pendingInstallQueue[i].appId;
					LOG_INFO("waitpid detected exit for app %u (status: %d) in IPC frame\n", pendingAppId, status);
					if (WIFEXITED(status) && WEXITSTATUS(status) == 0) {
						LOG_INFO("accela-download for app %u finished successfully!\n", pendingAppId);
						
						if (Hooks::IClientAppManager_InstallApp.originalFn.fn) {
							Hooks::IClientAppManager_InstallApp.originalFn.fn(
								g_pendingInstallQueue[i].pClientAppManager, 
								pendingAppId, 
								g_pendingInstallQueue[i].library, 
								g_pendingInstallQueue[i].a4
							);
						}
						
						Apps::setInstalled(pendingAppId);
					} else {
						LOG_INFO("accela-download for app %u failed or cancelled\n", pendingAppId);
					}
					
					g_pendingInstallQueue[i] = g_pendingInstallQueue[--g_pendingInstallCount];
				} else {
					i++;
				}
			}
		}

		Apps::runIPCFrame();
		SLSAPI::runIPCFrame();
	}
	else
	{
		LOG_TRACE("Calling tramp\n");
		ret = Hooks::CSteamEngine_ProcessIPCFrame.tramp.fn(pSteamEngine, hPipe, pBufIn, pBufOut);
	}

	if (cmd == EIPCCmd::ClosePipe)
	{
		FakeAppIds::closePipe(hPipe);
	}

	//LOG_DEBUG("In\n%s\n", MemHlp::hexdump(pBufIn->mem.base, pBufIn->offset).c_str());
	//LOG_DEBUG("Out\n%s\n", MemHlp::hexdump(pBufOut->mem.base, pBufOut->offset).c_str());

	return ret;
}

static AppId_t hkSteamEngine_SetAppIdForCurrentPipe(CSteamEngine* pSteamEngine, AppId_t appId, bool a2)
{
	FakeAppIds::setAppIdForCurrentPipe(appId);

	LOG_TRACE("Calling tramp\n");
	const AppId_t ret = Hooks::CSteamEngine_SetAppIdForCurrentPipe.tramp.fn(pSteamEngine, appId, a2);

	LOG_DEBUG
	(
		"%s(%p, %u, %i) -> %i\n",

		Hooks::CSteamEngine_SetAppIdForCurrentPipe.name.c_str(),
		reinterpret_cast<void*>(pSteamEngine),
		appId,
		a2,
		ret
	);

	return ret;
}

static bool hkWebSocketConnection_BBuildAndAsyncSendFrame(CWebSocketConnection* pWebSocketConnection, EWebSocketConnectionSendType type, void* pData, uint32_t dataSize)
{
	if (type == k_EWebSocketConnectionSendRaw)
	{
		//Freeing pData royally fucks up memory since
		//it gets freed sometime after the function returns
		//(going up the callstack around 5 times)
		CNetPacket packet {};
		packet.body = reinterpret_cast<CNetPacketBody*>(Steam::Plat_Alloc(dataSize));

		if (packet.body)
		{
			memcpy(packet.body, pData, dataSize);
			packet.size = dataSize;

			LOG_DEBUG
			(
				"SendPkt with CWebSocketConnection at %p %s -> 0x%x\n",
				reinterpret_cast<void*>(pWebSocketConnection),
				packet.getProtoBufTypeName().c_str(),
				packet.getType()
			);

			if (packet.isValid() && packet.isProtoBuf())
			{
				//Multiple sockets in use, we grab the main one
				if (!g_pWebSocketConnection && packet.getProtoBufType() == k_EMsgClientLogon)
				{
					g_pWebSocketConnection = reinterpret_cast<CWebSocketConnection*>(pWebSocketConnection);
				}

				Apps::sendMsg(&packet);
				FakeAppIds::sendMsg(&packet);
			}

			LOG_TRACE("Calling tramp\n");
			const bool success = Hooks::CWebSocketConnection_BBuildAndAsyncSendFrame.tramp.fn(pWebSocketConnection, type, packet.body, packet.size);
			packet.free();
			return success;
		}
	}

	LOG_TRACE("Calling tramp\n");
	return Hooks::CWebSocketConnection_BBuildAndAsyncSendFrame.tramp.fn(pWebSocketConnection, type, pData, dataSize);
}

static gameserverdetails_t* hkSteamMatchmakingServers_GetServerDetails(void* pSteamMatchmakingServers, uint32_t handle, uint32_t serverIdx)
{
	LOG_TRACE("Calling tramp\n");
	gameserverdetails_t* ret = Hooks::CSteamMatchmakingServers_GetServerDetails.tramp.fn(pSteamMatchmakingServers, handle, serverIdx);

	LOG_DEBUG
	(
		"%s(%p, 0x%x, %u) -> %p\n",

		Hooks::CSteamMatchmakingServers_GetServerDetails.name.c_str(),
		pSteamMatchmakingServers,
		handle,
		serverIdx,
		reinterpret_cast<void*>(ret)
	);

	if (ret)
	{
		FakeAppIds::getServerDetails(handle, *ret);
	}

	return ret;
}

static uint32_t hkSteamMatchmakingServers_RequestInternetServerList(void* pSteamMatchmakingServers, AppId_t appId, uint32_t a2, uint32_t a3, uint32_t a4)
{
	const uint32_t fake = FakeAppIds::requestInternetServerList(appId);

	LOG_TRACE("Calling tramp\n");
	uint32_t handle = Hooks::CSteamMatchmakingServers_RequestInternetServerList.tramp.fn(pSteamMatchmakingServers, fake ? fake : appId, a2, a3, a4);

	LOG_DEBUG
	(
		"%s(%p, %u, 0x%x, 0x%x, 0x%x)->0x%x\n",

		Hooks::CSteamMatchmakingServers_RequestInternetServerList.name.c_str(),
		pSteamMatchmakingServers,
		appId,
		a2,
		a3,
		a4,
		handle
	);

	FakeAppIds::fakeAppIdMapServer[handle] = appId;

	return handle;
}

__attribute__((hot))
static uint32_t hkUser_CheckAppOwnership(CUser* pUser, AppId_t appId, AppOwnershipInfo_t* pOwnershipInfo)
{
	LOG_TRACE("Calling tramp\n");
	const uint32_t ret = Hooks::CUser_CheckAppOwnership.tramp.fn(pUser, appId, pOwnershipInfo);

	//Do not log pOwnershipInfo because it gets deleted very quickly, so it's pretty much useless in the logs
	LOG_ONCE
	(
		"%s(%p, %u) -> %i\n",

		Hooks::CUser_CheckAppOwnership.name.c_str(),
		reinterpret_cast<void*>(pUser),
		appId,
		ret
	);

	if (Apps::checkAppOwnership(appId, pOwnershipInfo) || DLC::checkAppOwnership(appId, pOwnershipInfo))
	{
		return true;
	}

	return ret;
}

static uint32_t hkUser_GetSubscribedApps(CUser* pUser, AppId_t* pAppList, uint32_t size, uint8_t a3)
{
	LOG_TRACE("Calling tramp\n");
	uint32_t count = Hooks::CUser_GetSubscribedApps.tramp.fn(pUser, pAppList, size, a3);

	Apps::getSubscribedApps(pAppList, size, count);

	LOG_DEBUG
	(
		"%s(%p, %p, %i, %i) -> %i\n",

		Hooks::CUser_GetSubscribedApps.name.c_str(),
		reinterpret_cast<void*>(pUser),
		reinterpret_cast<void*>(pAppList),
		size,
		a3,
		count
	);

	return count;
}

static uint32_t hkUser_PostCallbackToAppId(CUser* pUser, AppId_t appId, uint32_t type, void* pCallback, uint32_t callbackSize)
{
	const AppId_t fakeAppId = FakeAppIds::getFakeAppId(appId);
	if (fakeAppId)
	{
		LOG_DEBUG("Rerouting callback from %u to %u\n", appId, fakeAppId);
		appId = fakeAppId;
	}

	LOG_TRACE("Calling tramp\n");
	const uint32_t ret = Hooks::CUser_PostCallbackToAppId.tramp.fn(pUser, appId, type, pCallback, callbackSize);

	LOG_DEBUG
	(
		"%s(%p, %u, %u, %p, %u) -> %u\n",

		Hooks::CUser_PostCallbackToAppId.name.c_str(),
		reinterpret_cast<void*>(pUser),
		appId,
		type,
		pCallback,
		callbackSize,
		ret
	);

	return ret;
}

static bool hkUserAppManager_BuildDepotDependency
(
	IClientAppManager* pAppManager,
	AppId_t appId,
	void* a2,
	CUtlVector<DepotInfo_t>* depots,
	CUtlVector<DepotInfo_t>* sharedDepots,
	void* a5,
	uint32_t* pBuildId,
	bool* a7
)
{
	LOG_TRACE("Calling tramp\n");
	const bool success = Hooks::CUserAppManager_BuildDepotDependency.tramp.fn(pAppManager, appId, a2, depots, sharedDepots, a5, pBuildId, a7);

	LOG_DEBUG
	(
		"%s(%p, %u) -> %i\n",
		Hooks::CUserAppManager_BuildDepotDependency.name.c_str(),
		reinterpret_cast<void*>(pAppManager),
		appId,
		success
	);

	Apps::buildDepotDependency(depots, sharedDepots);

	return success;
}

static bool hkClientAppManager_BCanRemotePlayTogether(IClientAppManager* pAppManager, AppId_t appId)
{
	LOG_TRACE("Calling original\n");
	const bool ret = Hooks::IClientAppManager_BCanRemotePlayTogether.originalFn.fn(pAppManager, appId);

	LOG_DEBUG
	(
		"%s(%p, %u) -> %u\n",
		Hooks::IClientAppManager_BCanRemotePlayTogether.name.c_str(),
		reinterpret_cast<void*>(pAppManager),
		appId,
		ret
	);

	return true;
}

static void* hkClientAppManager_LaunchApp(IClientAppManager* pAppManager, AppId_t* pAppId, void* a2, void* a3, void* a4)
{
	if (pAppId)
	{
		LOG_ONCE
		(
			"%s(%p, %u, %p, %p, %p)\n",

			Hooks::IClientAppManager_LaunchApp.name.c_str(),
			reinterpret_cast<void*>(pAppManager),
			*pAppId,
			a2,
			a3,
			a4
		);

		Ticket::launchApp(*pAppId);
	}

	LOG_TRACE("Calling original\n");
	//Do not do anything in post! Otherwise App launching will break
	return Hooks::IClientAppManager_LaunchApp.originalFn.fn(pAppManager, pAppId, a2, a3, a4);
}

static bool hkClientAppManager_IsAppDlcInstalled(IClientAppManager* pAppManager, AppId_t appId, AppId_t dlcId)
{
	LOG_TRACE("Calling original\n");
	const bool ret = Hooks::IClientAppManager_IsAppDlcInstalled.originalFn.fn(pAppManager, appId, dlcId);

	LOG_ONCE
	(
		"%s(%p, %u, %u) -> %i\n",

		Hooks::IClientAppManager_IsAppDlcInstalled.name.c_str(),
		reinterpret_cast<void*>(pAppManager),
		appId,
		dlcId,
		ret
	);

	if (DLC::isAppDlcInstalled(dlcId))
	{
		return true;
	}

	return ret;
}

static bool hkClientAppManager_BIsDlcEnabled(IClientAppManager* pAppManager, AppId_t appId, AppId_t dlcId, void* a3)
{
	LOG_TRACE("Calling original\n");

	const bool ret = Hooks::IClientAppManager_BIsDlcEnabled.originalFn.fn(pAppManager, appId, dlcId, a3);
	LOG_ONCE
	(
		"%s(%p, %u, %u, %p) -> %i\n",

		Hooks::IClientAppManager_BIsDlcEnabled.name.c_str(),
		reinterpret_cast<void*>(pAppManager),
		appId,
		dlcId,
		a3,
		ret
	);

	
	if (DLC::isDlcEnabled(appId))
	{
		return true;
	}

	return ret;
}

static bool hkClientAppManager_GetAppUpdateInfo(IClientAppManager* pClientAppManager, uint32_t appId, uint32_t* a2)
{
	const bool success = Hooks::IClientAppManager_GetAppUpdateInfo.originalFn.fn(pClientAppManager, appId, a2);
	LOG_ONCE("IClientAppManager::GetAppUpdateInfo(%p, %u, %p) -> %i\n", (void*)pClientAppManager, appId, (void*)a2, success);

	if (Apps::shouldDisableUpdates(appId))
	{
		LOG_ONCE("Disabled updates for %u\n", appId);
		return false;
	}

	return success;
}

static uint32_t hkClientAppManager_InstallApp(IClientAppManager* pClientAppManager, uint32_t appId, uint32_t library, uint8_t a4)
{
    LOG_INFO("hkClientAppManager_InstallApp(%p, %u, %u, %u)\n", (void*)pClientAppManager, appId, library, a4);

    if (Apps::isInstalled(appId))
    {
        LOG_INFO("App %u already downloaded via ACCELA. Finalizing installation.\n", appId);
        return Hooks::IClientAppManager_InstallApp.originalFn.fn(pClientAppManager, appId, library, a4);
    }

    bool locallyOwned = g_pSteamEngine && g_pSteamEngine->getUser(0) && g_pSteamEngine->getUser(0)->isSubscribed(appId);
    
	if(locallyOwned)
	{
		return Hooks::IClientAppManager_InstallApp.originalFn.fn(pClientAppManager, appId, library, a4);
	}

    LOG_DEBUG("App %u (locallyOwned=%d). Launching accela-download.\n", appId, locallyOwned);

    pid_t pid = fork();
    if (pid == 0) {
        setsid();
        for (int fd = 3; fd < 256; fd++) close(fd);
        unsetenv("LD_AUDIT");
        unsetenv("LD_PRELOAD");
        unsetenv("LD_LIBRARY_PATH");
        unsetenv("STEAM_RUNTIME");
        unsetenv("STEAM_RUNTIME_LIBRARY_PATH");

        char appid_str[32];
        snprintf(appid_str, sizeof(appid_str), "%u", appId);
        char cmd[512];
        // Improved command: ensures terminal stays open on error so the user can see what happened
        snprintf(cmd, sizeof(cmd), "if accela-download %s; then echo '\n[ACCELA] Download finished! Press Enter to close.'; else echo '\n[ACCELA] Download failed! Press Enter to close.'; fi; read", appid_str);

        LOG_INFO("Child process (PID: %d) executing: %s\n", getpid(), cmd);

        // Try different terminal emulators
        execlp("x-terminal-emulator", "x-terminal-emulator", "--title", "ACCELA Download", "-e", "bash", "-c", cmd, (char*)NULL);
        execlp("gnome-terminal", "gnome-terminal", "--title", "ACCELA Download", "--", "bash", "-c", cmd, (char*)NULL);
        execlp("konsole", "konsole", "--title", "ACCELA Download", "-e", "bash", "-c", cmd, (char*)NULL);
        execlp("xterm", "xterm", "-T", "ACCELA Download", "-e", "bash", "-c", cmd, (char*)NULL);
        
        // Fallback: try bash directly if no terminal found (user won't see output though)
        execlp("bash", "bash", "-c", cmd, (char*)NULL);
        _exit(1);
    } else if (pid > 0) {
        if (g_pendingInstallCount < 16) {
            g_pendingInstallQueue[g_pendingInstallCount++] = {pClientAppManager, appId, library, a4, pid};
            LOG_INFO("Tracking accela-download (PID: %d) for app %u.\n", pid, appId);
        } else {
            LOG_WARN("Pending install queue full!\n");
        }
    } else {
        LOG_WARN("Failed to fork accela-download!\n");
    }
    
	return 0;
}

static uint32_t hkClientAppManager_UninstallApp(IClientAppManager* pClientAppManager, uint32_t appId, bool bComplete)
{
    LOG_INFO("hkClientAppManager_UninstallApp(%p, %u)\n", (void*)pClientAppManager, appId);

    // Remove from our installed tracking so GetAppInstallState stops forcing FULLY_INSTALLED
    Apps::removeInstalled(appId);
    
    // Actually delete the files from disk, as Steam doesn't have proper depot info for ACCELA games
    Apps::deleteGameFiles(appId);

    return Hooks::IClientAppManager_UninstallApp.originalFn.fn(pClientAppManager, appId, bComplete);
}

static EAppState hkClientAppManager_GetAppInstallState(IClientAppManager* pClientAppManager, uint32_t appId)
{
    EAppState state = Hooks::IClientAppManager_GetAppInstallState.originalFn.fn(pClientAppManager, appId);

    if (Apps::isInstalled(appId))
    {
        LOG_DEBUG("Forcing FULLY_INSTALLED for app %u (Was: 0x%X)\n", appId, (int)state);
        state = (EAppState)(((int)state | k_EAppStateFullyInstalled) & ~k_EAppStateUninstalled & ~k_EAppStateUpdateRequired);
    }

    return state;
}

// static void hkClientAppManager_RunIPCFrame(void* pClientAppManager, void* a1, void* a2, void* a3)
// {
// 	g_pClientAppManager = reinterpret_cast<IClientAppManager*>(pClientAppManager);

// 	std::shared_ptr<lm_vmt_t> vft = std::make_shared<lm_vmt_t>();
// 	LM_VmtNew(*reinterpret_cast<lm_address_t**>(pClientAppManager), vft.get());

// 	Hooks::IClientAppManager_BIsDlcEnabled.setup(vft, VFTIndexes::IClientAppManager::BIsDlcEnabled, hkClientAppManager_BIsDlcEnabled);
// 	Hooks::IClientAppManager_GetAppUpdateInfo.setup(vft, VFTIndexes::IClientAppManager::GetUpdateInfo, hkClientAppManager_GetAppUpdateInfo);
// 	Hooks::IClientAppManager_LaunchApp.setup(vft, VFTIndexes::IClientAppManager::LaunchApp, hkClientAppManager_LaunchApp);
// 	Hooks::IClientAppManager_IsAppDlcInstalled.setup(vft, VFTIndexes::IClientAppManager::IsAppDlcInstalled, hkClientAppManager_IsAppDlcInstalled);
// 	Hooks::IClientAppManager_InstallApp.setup(vft, VFTIndexes::IClientAppManager::InstallApp, hkClientAppManager_InstallApp);
// 	Hooks::IClientAppManager_UninstallApp.setup(vft, VFTIndexes::IClientAppManager::UninstallApp, hkClientAppManager_UninstallApp);
// 	Hooks::IClientAppManager_GetAppInstallState.setup(vft, VFTIndexes::IClientAppManager::GetAppInstallState, hkClientAppManager_GetAppInstallState);
// 	Hooks::IClientAppManager_GetAppInstallState_Backup.setup(vft, VFTIndexes::IClientAppManager::GetAppInstallState_Backup, hkClientAppManager_GetAppInstallState);


// 	Hooks::IClientAppManager_BIsDlcEnabled.place();
// 	Hooks::IClientAppManager_GetAppUpdateInfo.place();
// 	Hooks::IClientAppManager_LaunchApp.place();
// 	Hooks::IClientAppManager_IsAppDlcInstalled.place();
// 	Hooks::IClientAppManager_InstallApp.place();
// 	Hooks::IClientAppManager_UninstallApp.place();
// 	Hooks::IClientAppManager_GetAppInstallState.place();
// 	Hooks::IClientAppManager_GetAppInstallState_Backup.place();

// 	LOG_INFO("Dumping IClientAppManager VFT:\n");
// 	for (int i = 0; i < 40; i++) {
// 		LOG_INFO("  VFT[%d] = %p\n", i, vft->vtable[i]);
// 	}

// 	Hooks::IClientAppManager_RunIPCFrame.remove();
// 	Hooks::IClientAppManager_RunIPCFrame.originalFn.fn(pClientAppManager, a1, a2, a3);
// }

static unsigned int hkClientApps_GetDLCCount(IClientApps* pClientApps, AppId_t appId)
{
	LOG_TRACE("Calling original\n");

	uint32_t count = Hooks::IClientApps_GetDLCCount.originalFn.fn(pClientApps, appId);
	LOG_ONCE
	(
		"%s(%p, %u) -> %u\n",

		Hooks::IClientApps_GetDLCCount.name.c_str(),
		reinterpret_cast<void*>(pClientApps),
		appId,
		count
	);
	
	const uint32_t override = DLC::getDlcCount(appId);
	if (override)
	{
		return override;
	}

	return count;
}

static bool hkClientApps_GetDLCDataByIndex(IClientApps* pClientApps, AppId_t appId, int dlcIndex, AppId_t* pDlcId, bool* pIsAvailable, char* pChDlcName, size_t dlcNameLen)
{
	LOG_TRACE("Calling original\n");
	//Preserve original call to populate stuff
	const bool ret = DLC::getDlcDataByIndex(appId, dlcIndex, pDlcId, pIsAvailable, pChDlcName, dlcNameLen)
		|| Hooks::IClientApps_GetDLCDataByIndex.originalFn.fn(pClientApps, appId, dlcIndex, pDlcId, pIsAvailable, pChDlcName, dlcNameLen);


	LOG_ONCE
	(
		"%s(%p, %u, %i, %p, %p, %s, %i) -> %i\n",

		Hooks::IClientApps_GetDLCDataByIndex.name.c_str(),
		reinterpret_cast<void*>(pClientApps),
		appId,
		dlcIndex,
		reinterpret_cast<void*>(pDlcId),
		reinterpret_cast<void*>(pIsAvailable),
		pChDlcName,
		dlcNameLen,
		ret
	);

	return ret;
}

__attribute__((hot))
static void hkClientApps_RunIPCFrame(void* pClientApps, void* a1, void* a2, void* a3)
{
	static bool hooked = false;
	if (!hooked)
	{
		g_pClientApps = reinterpret_cast<IClientApps*>(pClientApps);

		std::shared_ptr<lm_vmt_t> vft = std::make_shared<lm_vmt_t>();
		LM_VmtNew(*reinterpret_cast<lm_address_t**>(pClientApps), vft.get());

		Hooks::IClientApps_GetAppData.setup(vft, VFTIndexes::IClientApps::GetAppData, hkClientApps_GetAppData);
		Hooks::IClientApps_GetAppDataSection.setup(vft, VFTIndexes::IClientApps::GetAppDataSection, hkClientApps_GetAppDataSection);
		Hooks::IClientApps_GetDLCDataByIndex.setup(vft, VFTIndexes::IClientApps::GetDLCDataByIndex, hkClientApps_GetDLCDataByIndex);
		Hooks::IClientApps_GetDLCCount.setup(vft, VFTIndexes::IClientApps::GetDLCCount, hkClientApps_GetDLCCount);

		Hooks::IClientApps_GetAppData.place();
		Hooks::IClientApps_GetAppDataSection.place();
		Hooks::IClientApps_GetDLCDataByIndex.place();
		Hooks::IClientApps_GetDLCCount.place();

		// Pre-fetch sizes for all unlocked games in a background thread
		std::thread([]() {
			auto apps = g_config.addedAppIds.get();
			for (auto appId : apps) {
				FetchAppSizeThread(appId);
				// Small sleep to be nice to the API
				std::this_thread::sleep_for(std::chrono::milliseconds(100));
			}
		}).detach();


		LOG_DEBUG("IClientApps->vft at %p\n", (void*)vft->vtable);

		hooked = true;
	}

	Hooks::IClientApps_RunIPCFrame.tramp.fn(pClientApps, a1, a2, a3);
}

static uint32_t hkClientFriends_GetFriendGamePlayed(void* pClientFriends, uint64_t steamId, GamePlayed_t* gamePlayed)
{
	LOG_TRACE("Calling original\n");
	const uint32_t ret = Hooks::IClientFriends_GetFriendGamePlayed.tramp.fn(pClientFriends, steamId, gamePlayed);

	const AppId_t realAppId = FakeAppIds::getRealAppIdForCurrentPipe();
	const AppId_t fakeAppId = FakeAppIds::getFakeAppId(realAppId);

	if (fakeAppId && fakeAppId == gamePlayed->appId)
	{
		LOG_DEBUG("Set friend GamePlayed from %u to %u\n", gamePlayed->appId, realAppId);
		gamePlayed->appId = realAppId;
	}

	//We do not log this function, it's basically useless since we don't want any SteamIds in the logs
	
	return ret;
}

static bool hkClientRemoteStorage_IsCloudEnabledForApp(void* pClientRemoteStorage, AppId_t appId)
{
	LOG_TRACE("Calling tramp\n");
	const bool enabled = Hooks::IClientRemoteStorage_IsCloudEnabledForApp.tramp.fn(pClientRemoteStorage, appId);
	LOG_ONCE
	(
		"%s(%p, %u) -> %i\n",

		Hooks::IClientRemoteStorage_IsCloudEnabledForApp.name.c_str(),
		pClientRemoteStorage,
		appId,
		enabled
	);

	if (Apps::shouldDisableCloud(appId))
	{
		LOG_ONCE("Disabled cloud for %u\n", appId);
		return false;
	}

	return enabled;
}

static void hkClientRemoteStorage_RunIPCFrame(void* pClientRemoteStorage, void* a1, void* a2, void* a3)
{

	static bool hooked = false;
	if (!hooked)
	{
		std::shared_ptr<lm_vmt_t> vft = std::make_shared<lm_vmt_t>();
		LM_VmtNew(*reinterpret_cast<lm_address_t**>(pClientRemoteStorage), vft.get());

		LOG_DEBUG("IClientRemoteStorage->vft at %p\n", (void*)vft->vtable);

		hooked = true;
	}
	
	//Cloud & Workshop
	FakeAppIds::runIPCFrame(false, EIPCInterface::RemoteStorage);
	Hooks::IClientRemoteStorage_RunIPCFrame.tramp.fn(pClientRemoteStorage, a1, a2, a3);
	FakeAppIds::runIPCFrame(true, EIPCInterface::RemoteStorage);
}

static void hkClientUGC_RunIPCFrame(void* pClientUGC, void* a1, void* a2, void* a3)
{
	//Workshop items
	FakeAppIds::runIPCFrame(false, EIPCInterface::UGC);
	Hooks::IClientUGC_RunIPCFrame.tramp.fn(pClientUGC, a1, a2, a3);
	FakeAppIds::runIPCFrame(true, EIPCInterface::UGC);
}

static uint32_t hkClientUtils_GetAppId(IClientUtils* pClientUtils);
static bool hkClientUtils_GetOfflineMode(IClientUtils* pClientUtils);

static void hkClientUtils_RunIPCFrame(void* pClientUtils, void* a1, void* a2, void* a3)
{
	// Constant background monitoring moved to hkSteamEngine_ProcessIPCFrame

	static bool hooked = false;
	if (!hooked)
	{
		g_pClientUtils = reinterpret_cast<IClientUtils*>(pClientUtils);

		std::shared_ptr<lm_vmt_t> vft = std::make_shared<lm_vmt_t>();
		LM_VmtNew(*reinterpret_cast<lm_address_t**>(pClientUtils), vft.get());

		Hooks::IClientUtils_GetAppId.setup(vft, VFTIndexes::IClientUtils::GetAppId, hkClientUtils_GetAppId);
		Hooks::IClientUtils_GetOfflineMode.setup(vft, VFTIndexes::IClientUtils::GetOfflineMode, hkClientUtils_GetOfflineMode);

		Hooks::IClientUtils_GetAppId.place();
		Hooks::IClientUtils_GetOfflineMode.place();

		LOG_DEBUG("IClientUtils->vft at %p\n", (void*)vft->vtable);

		hooked = true;
	}

	Hooks::IClientUtils_RunIPCFrame.tramp.fn(pClientUtils, a1, a2, a3);
}


static bool hkClientUser_BLoggedOn(IClientUser* pClientUser)
{
	LOG_TRACE("Calling original\n");
	const bool ret = Hooks::IClientUser_BLoggedOn.originalFn.fn(pClientUser);
	//Useless logging
	//LOG_DEBUG
	//(
	//	"%s(0x%x) -> %i\n",
	//	Hooks::IClientUser_BLoggedOn.name.c_str(),
	//	pClientUser,
	//	ret
	//);
	
	if (Misc::shouldFakeOffline())
	{
		return false;
	}

	return ret;
}

static uint32_t hkClientUser_BUpdateAppOwnershipTicket(IClientUser* pClientUser, AppId_t appId, bool staleOnly)
{
	const auto cached = Ticket::getCachedTicket(appId);
	if (g_pSteamEngine->getUser(0)->isSubscribed(appId) && !cached)
	{
		staleOnly = false;
		LOG_DEBUG("Force re-requesting OwnershipInfo for %u\n", appId);
	}

	LOG_TRACE("Calling original\n");
	const uint32_t ret = Hooks::IClientUser_BUpdateAppOwnershipTicket.originalFn.fn(pClientUser, appId, staleOnly);

	LOG_DEBUG
	(
		"%s(%p, %u, %i) -> %u\n",

		Hooks::IClientUser_BUpdateAppOwnershipTicket.name.c_str(),
		reinterpret_cast<void*>(pClientUser),
		appId,
		staleOnly,
		ret
	);

	return ret;
}

static uint32_t hkClientUser_GetAppOwnershipTicketExtendedData
(
	IClientUser* pClientUser,
	AppId_t appId,
	void* pTicket,
	uint32_t ticketSize,
	uint32_t* pOffAppId,
	uint32_t* pOffSteamId,
	uint32_t* pOffSig,
	uint32_t* pSigSize
)
{
	LOG_TRACE("Calling original\n");
	const uint32_t size = Hooks::IClientUser_GetAppOwnershipTicketExtendedData.originalFn.fn
	(
		pClientUser,
		appId,
		pTicket,
		ticketSize,
		pOffAppId,
		pOffSteamId,
		pOffSig,
		pSigSize
   );

	LOG_ONCE("%s(%u)->%u\n", Hooks::IClientUser_GetAppOwnershipTicketExtendedData.name.c_str(), appId, size);

	if (size)
	{
		Ticket::getTicketOwnershipExtendedData(appId);
	}

	return size;
}

static bool hkClientUser_GetEncryptedAppTicket(IClientUser* pClientUser, void* pTicket, uint32_t ticketSize, uint32_t* pTicketSize)
{
	LOG_TRACE("Calling original\n");
	const bool success = Hooks::IClientUser_GetEncryptedAppTicket.originalFn.fn(pClientUser, pTicket, ticketSize, pTicketSize);

	LOG_DEBUG
	(
		"%s(%p, %p, %u, %p) -> %u\n",

		Hooks::IClientUser_GetEncryptedAppTicket.name.c_str(),
		reinterpret_cast<void*>(pClientUser),
		pTicket,
		ticketSize,
		reinterpret_cast<void*>(pTicketSize),
		success
	);

	if (success)
	{
		Ticket::getEncryptedAppTicket(FakeAppIds::getRealAppIdForCurrentPipe());
	}

	return success;
}

static bool hkClientUser_GetLegacyCDKey(IClientUser* pClientUser, AppId_t appId, char* pChKey, uint32_t keySize)
{
	Apps::getLegacyCDKey(appId);

	return Hooks::IClientUser_GetLegacyCDKey.originalFn.fn(pClientUser, appId, pChKey, keySize);
}

static uint8_t hkClientUser_IsUserSubscribedAppInTicket(IClientUser* pClientUser, uint64_t steamId, AppId_t appId)
{
	LOG_TRACE("Calling original\n");
	const uint8_t ticketState = Hooks::IClientUser_IsUserSubscribedAppInTicket.originalFn.fn(pClientUser, steamId, appId);
	//LOG_ONCE("IClientUser::IsUserSubscribedAppInTicket(0x%x, %u, %u, %u, %u) -> %i\n", pClientUser, steamId, a2, a3, appId, ticketState);
	//Don't log the steamId, protect users from themselves and stuff
	LOG_ONCE
	(
		"%s(%p, %u) -> %i\n",

		Hooks::IClientUser_IsUserSubscribedAppInTicket.name.c_str(),
		reinterpret_cast<void*>(pClientUser),
		appId,
		ticketState
	);
	
	if (DLC::userSubscribedInTicket(appId))
	{
		//Owned and subscribed hehe :)
		return 0;
	}

	return ticketState;
}

static CSteamId hkClientUser_GetSteamId(const CSteamId& steamId)
{
	const auto utils = g_pSteamEngine->getUtils();
	if (!utils)
	{
		return steamId;
	}

	//Never spoof inside the Steamclient
	const AppId_t realAppId = FakeAppIds::getRealAppIdForCurrentPipe();
	if (!realAppId)
	{
		return steamId;
	}

	const auto overrides = g_config.steamIdOverride.get();
	if (overrides.contains(realAppId))
	{
		const uint64_t& id64 = overrides.at(realAppId);
		if (id64)
		{
			return CSteamId(id64);
		}

		const auto cached = Ticket::getCachedTicket(realAppId);
		if (cached)
		{
			return cached->steamId;
		}

		LOG_ONCE
		(
			"SteamIdOverride for %u is set with automatic mode, but no AppOwnershipTicket exists in cache! Falling through to normal operation\n",
			realAppId
		);
	}

	if (Ticket::oneTimeSteamIdSpoof.contains(realAppId))
	{
		const CSteamId newId = Ticket::oneTimeSteamIdSpoof.at(realAppId);
		Ticket::oneTimeSteamIdSpoof.erase(realAppId);

		return newId;
	}

	//Use pipe AppId, getCachedEncryptedTicket handles FakeAppIds internally
	const auto ticket = Ticket::getCachedEncryptedTicket(utils->getAppId());
	if (ticket)
	{
		return ticket->steamId;
	}

	return steamId;
}

static AppId_t hkClientUtils_GetAppId(IClientUtils* pClientUtils)
{
	LOG_TRACE("Calling original\n");
	AppId_t appId = Hooks::IClientUtils_GetAppId.originalFn.fn(pClientUtils);

	LOG_DEBUG
	(
		"%s(%p) -> %u\n",

		Hooks::IClientUtils_GetAppId.name.c_str(),
		reinterpret_cast<void*>(pClientUtils),
		appId
	);

	const AppId_t real = FakeAppIds::getRealAppIdForCurrentPipe(false);
	if (real)
	{
		LOG_DEBUG("Overwriting appId with %u\n", real);
		return real;
	}

	return appId;
}

static bool hkClientUtils_GetOfflineMode(IClientUtils* pClientUtils)
{
	LOG_TRACE("Calling original\n");
	const bool ret = Hooks::IClientUtils_GetOfflineMode.originalFn.fn(pClientUtils);

	if (Misc::shouldFakeOffline())
	{
		return true;
	}

	return ret;
}

static void hkCGameInfoDialog_ServerResponded(void* pSteamMatchingPingResponse, gameserverdetails_t* details)
{
	FakeAppIds::pingResponse(details);

	LOG_TRACE("Calling tramp\n");
	Hooks::CGameInfoDialog_ServerResponded.tramp.fn(pSteamMatchingPingResponse, details);

	LOG_DEBUG
	(
		"%s(%p, %p) for %u\n",
		Hooks::CGameInfoDialog_ServerResponded.name.c_str(),
		pSteamMatchingPingResponse,
		reinterpret_cast<void*>(details),
		details ? details->appId : 0
	);
}

static bool hkClientCompat_BIsCompatLayerEnabled(IClientCompat* pClientCompat)
{
	if (!g_pClientCompat)
	{
		g_pClientCompat = pClientCompat;
		LOG_DEBUG("g_pClientCompat at %p\n", reinterpret_cast<void*>(g_pClientCompat));
	}

	return Hooks::IClientCompat_BIsCompatLayerEnabled.tramp.fn(pClientCompat);
}

static bool hkClientConfigStore_SetString(void* pClientConfigStore, uint32_t store, const char* key, const char* value)
{
	LOG_TRACE("Calling tramp\n");
	const bool success = Hooks::IClientConfigStore_SetString.tramp.fn(pClientConfigStore, store, key, value);

	//LOG_DEBUG
	//(
	//	"%s(0x%x, %u, %s, %s) -> %u\n",

	//	Hooks::IClientConfigStore_SetString.name.c_str(),
	//	pClientConfigStore,
	//	store,
	//	key,
	//	value,
	//	success
	//);

	if (success)
	{
		Apps::setConfigStoreString(key, value);
	}

	return success;
}

namespace Hooks
{
	//TODO: Lazily intialize in a different way, or preload glibc
	DetourHook<TraceIPC_t> TraceIPC;

	DetourHook<CAPIJob_SendAndRecv_t> CAPIJob_SendAndRecv;

	DetourHook<CAppDataCache_BParseResponseFromMessage_t> CAppDataCache_BParseResponseFromMessage;

	DetourHook<CClientUnifiedServiceMethod_SendAndRecvMsg_t> CClientUnifiedServiceMethod_SendAndRecvMsg;

	DetourHook<CCMInterface_RecvPkt_t> CCMInterface_RecvPkt;

	DetourHook<CSteamMatchmakingServers_GetServerDetails_t> CSteamMatchmakingServers_GetServerDetails;
	DetourHook<CSteamMatchmakingServers_RequestInternetServerList_t> CSteamMatchmakingServers_RequestInternetServerList;

	DetourHook<CSteamEngine_ProcessIPCFrame_t> CSteamEngine_ProcessIPCFrame;
	DetourHook<CSteamEngine_SetAppIdForCurrentPipe_t> CSteamEngine_SetAppIdForCurrentPipe;

	DetourHook<CUser_CheckAppOwnership_t> CUser_CheckAppOwnership;
	DetourHook<CUser_GetSubscribedApps_t> CUser_GetSubscribedApps;
	DetourHook<CUser_PostCallbackToAppId_t> CUser_PostCallbackToAppId;

	DetourHook<CUserAppManager_BuildDepotDependency_t> CUserAppManager_BuildDepotDependency;

	DetourHook<CWebSocketConnection_BBuildAndAsyncSendFrame_t> CWebSocketConnection_BBuildAndAsyncSendFrame;

	DetourHook<IClientCompat_BIsCompatLayerEnabled_t> IClientCompat_BIsCompatLayerEnabled;
	DetourHook<IClientConfigStore_SetString_t> IClientConfigStore_SetString;

	DetourHook<IClientFriends_GetFriendGamePlayed_t> IClientFriends_GetFriendGamePlayed;

	DetourHook<IClientRemoteStorage_IsCloudEnabledForApp_t> IClientRemoteStorage_IsCloudEnabledForApp;

	DetourHook<IClientApps_RunIPCFrame_t> IClientApps_RunIPCFrame;
	DetourHook<IClientRemoteStorage_RunIPCFrame_t> IClientRemoteStorage_RunIPCFrame;
	DetourHook<IClientUGC_RunIPCFrame_t> IClientUGC_RunIPCFrame;
	DetourHook<IClientUtils_RunIPCFrame_t> IClientUtils_RunIPCFrame;

	VFTHook<IClientAppManager_BCanRemotePlayTogether_t> IClientAppManager_BCanRemotePlayTogether;
	VFTHook<IClientAppManager_BIsDlcEnabled_t> IClientAppManager_BIsDlcEnabled;
	VFTHook<IClientAppManager_GetAppUpdateInfo_t> IClientAppManager_GetAppUpdateInfo;
	VFTHook<IClientAppManager_LaunchApp_t> IClientAppManager_LaunchApp;
	VFTHook<IClientAppManager_IsAppDlcInstalled_t> IClientAppManager_IsAppDlcInstalled;
	
	VFTHook<IClientAppManager_InstallApp_t> IClientAppManager_InstallApp;
	VFTHook<IClientAppManager_UninstallApp_t> IClientAppManager_UninstallApp;
	VFTHook<IClientAppManager_GetAppInstallState_t> IClientAppManager_GetAppInstallState;
	VFTHook<IClientAppManager_GetAppInstallState_t> IClientAppManager_GetAppInstallState_Backup;

	VFTHook<IClientApps_GetAppData_t> IClientApps_GetAppData;
	VFTHook<IClientApps_GetAppDataSection_t> IClientApps_GetAppDataSection;
	VFTHook<IClientApps_GetDLCDataByIndex_t> IClientApps_GetDLCDataByIndex;
	VFTHook<IClientApps_GetDLCCount_t> IClientApps_GetDLCCount;

	VFTHook<IClientUser_BLoggedOn_t> IClientUser_BLoggedOn;
	VFTHook<IClientUser_BUpdateAppOwnershipTicket_t> IClientUser_BUpdateAppOwnershipTicket;
	VFTHook<IClientUser_GetAppOwnershipTicketExtendedData_t> IClientUser_GetAppOwnershipTicketExtendedData;
	VFTHook<IClientUser_GetEncryptedAppTicket_t> IClientUser_GetEncryptedAppTicket;
	VFTHook<IClientUser_GetLegacyCDKey_t> IClientUser_GetLegacyCDKey;
	VFTHook<IClientUser_IsUserSubscribedAppInTicket_t> IClientUser_IsUserSubscribedAppInTicket;

	VFTHook<IClientUtils_GetAppId_t> IClientUtils_GetAppId;
	VFTHook<IClientUtils_GetOfflineMode_t> IClientUtils_GetOfflineMode;


	//steamui.so
	DetourHook<CGameInfoDialog_ServerResponded_t> CGameInfoDialog_ServerResponded;
}

bool Hooks::setup()
{
	LOG_DEBUG("Hooks::setup()\n");

	{
		const auto name = std::string("14CCompatManager");
		if (!Decompiler::vftables.contains(name))
		{
			LOG_ERROR("Failed to get %s VFTable!\n", name.c_str());
			return false;
		}

		auto& compatMan = Decompiler::vftables.at(name);

		IClientCompat_BIsCompatLayerEnabled.setup
		(
			VFTIndexes::IClientCompat::BIsCompatLayerEnabled.getPrintName().c_str(),
			compatMan.functions[VFTIndexes::IClientCompat::BIsCompatLayerEnabled.index],
			hkClientCompat_BIsCompatLayerEnabled
		);
	}

	{
		const auto name = std::string("12CConfigStore");
		if (!Decompiler::vftables.contains(name))
		{
			LOG_ERROR("Failed to get %s VFTable!\n", name.c_str());
			return false;
		}

		auto& store = Decompiler::vftables.at(name);

		IClientConfigStore_SetString.setup
		(
			VFTIndexes::IClientConfigStore::SetString.getPrintName().c_str(),
			store.functions[VFTIndexes::IClientConfigStore::SetString.index],
			hkClientConfigStore_SetString
		);
	}

	{
		const auto name = std::string("12CUserFriends");
		if (!Decompiler::vftables.contains(name))
		{
			LOG_ERROR("Failed to get %s VFTable!\n", name.c_str());
			return false;
		}

		auto& friends = Decompiler::vftables.at(name);

		IClientFriends_GetFriendGamePlayed.setup
		(
			VFTIndexes::IClientFriends::GetFriendGamePlayed.getPrintName().c_str(),
			friends.functions[VFTIndexes::IClientFriends::GetFriendGamePlayed.index],
			hkClientFriends_GetFriendGamePlayed
		);
	}

	{
		const auto name = std::string("18CUserRemoteStorage");
		if (!Decompiler::vftables.contains(name))
		{
			LOG_ERROR("Failed to get %s VFTable!\n", name.c_str());
			return false;
		}

		auto& storage = Decompiler::vftables.at(name);

		//We detourhook because the vftable seems to get relocated, so at this point in time
		//the pointers are all wrong and would need manual adjustment which breaks
		//current assumptions by VFTHook<T>
		IClientRemoteStorage_IsCloudEnabledForApp.setup
		(
			VFTIndexes::IClientRemoteStorage::IsCloudEnabledForApp.getPrintName().c_str(),
			storage.functions[VFTIndexes::IClientRemoteStorage::IsCloudEnabledForApp.index],
			hkClientRemoteStorage_IsCloudEnabledForApp
		);
	}

	bool succeeded =
		TraceIPC.setup(Patterns::TraceIPC, &hkTraceIPC)

		&& CAPIJob_SendAndRecv.setup(Patterns::CAPIJob::SendAndRecv, hkAPIJob_SendAndRecv)

		&& CAppDataCache_BParseResponseFromMessage.setup(Patterns::CAppDataCache::BParseResponseMessage, hkAppDataCache_BParseResponseFromMessage)

		//We detour hook this virtual function out of respect for my friend Selectively11. His amazing project
		//CloudRedirect hooks the same function using a VFT hook already
		&& CClientUnifiedServiceMethod_SendAndRecvMsg.setup(VFTIndexes::CClientUnifiedServiceTransport::SendAndRecvMsg, hkClientUnifiedServiceTransport_SendAndRecvMsg)

		//To lazy to move this for now. Doesn't really matter wheter we detour or vft hook
		&& CCMInterface_RecvPkt.setup(VFTIndexes::CCMInterface::RecvPkt, hkCMInterface_RecvPkt)

		&& CSteamMatchmakingServers_GetServerDetails.setup(VFTIndexes::CSteamMatchmakingServers::GetServerDetails, hkSteamMatchmakingServers_GetServerDetails)
		&& CSteamMatchmakingServers_RequestInternetServerList.setup(VFTIndexes::CSteamMatchmakingServers::RequestInternetServerList, hkSteamMatchmakingServers_RequestInternetServerList)

		&& CUser_CheckAppOwnership.setup(Patterns::CUser::CheckAppOwnership, hkUser_CheckAppOwnership)
		&& CUser_GetSubscribedApps.setup(Patterns::CUser::GetSubscribedApps, hkUser_GetSubscribedApps)
		&& CUser_PostCallbackToAppId.setup(Patterns::CUser::PostCallbackToAppId, hkUser_PostCallbackToAppId)

		&& CUserAppManager_BuildDepotDependency.setup(Patterns::CUserAppManager::BuildDepotDependency, hkUserAppManager_BuildDepotDependency)

		&& CSteamEngine_ProcessIPCFrame.setup(Patterns::CSteamEngine::ProcessIPCFrame, hkSteamEngine_ProcessIPCFrame)
		&& CSteamEngine_SetAppIdForCurrentPipe.setup(Patterns::CSteamEngine::SetAppIdForCurrentPipe, hkSteamEngine_SetAppIdForCurrentPipe)

		&& CWebSocketConnection_BBuildAndAsyncSendFrame.setup(Patterns::CWebSocketConnection::BBuildAndAsyncSendFrame, hkWebSocketConnection_BBuildAndAsyncSendFrame)

		&& CGameInfoDialog_ServerResponded.setup(VFTIndexes::CGameInfoDialog::ServerResponded, hkCGameInfoDialog_ServerResponded);


	Hooks::place();
	//This is unnecessary but I'll keep this for now in case I wanna improve error checks
	return succeeded;
}

void Hooks::place()
{
	//Detours
	TraceIPC.place();

	CAPIJob_SendAndRecv.place();

	CAppDataCache_BParseResponseFromMessage.place();

	CClientUnifiedServiceMethod_SendAndRecvMsg.place();

	CCMInterface_RecvPkt.place();

	CSteamEngine_ProcessIPCFrame.place();
	CSteamEngine_SetAppIdForCurrentPipe.place();

	CSteamMatchmakingServers_GetServerDetails.place();
	CSteamMatchmakingServers_RequestInternetServerList.place();

	CUser_CheckAppOwnership.place();
	CUser_GetSubscribedApps.place();
	CUser_PostCallbackToAppId.place();

	CUserAppManager_BuildDepotDependency.place();

	CWebSocketConnection_BBuildAndAsyncSendFrame.place();

	CGameInfoDialog_ServerResponded.place();

	IClientCompat_BIsCompatLayerEnabled.place();

	IClientConfigStore_SetString.place();

	IClientFriends_GetFriendGamePlayed.place();

	IClientRemoteStorage_IsCloudEnabledForApp.place();
}

void Hooks::placeVFTHooks()
{
	static bool hooked = false;
	if (hooked)
	{
		return;
	}

	const auto usr = g_pSteamEngine->getUser();
	if (!usr)
	{
		return;
	}

	//I don't think the IPC layer is multithreaded but better safe than sorry
	static std::mutex mutex;
	std::lock_guard guard(mutex);

	#pragma GCC diagnostic push
	#pragma GCC diagnostic ignored "-Waddress-of-packed-member"

	LOG_DEBUG("CUser at %p\n", reinterpret_cast<void*>(usr));

	{
		const auto appManager = usr->getAppManager();
		LOG_DEBUG("CUserAppManager at %p\n", reinterpret_cast<void*>(appManager));

		std::shared_ptr<lm_vmt_t> vft = std::make_shared<lm_vmt_t>();
		LM_VmtNew(*reinterpret_cast<lm_address_t**>(appManager), vft.get());

		Hooks::IClientAppManager_BCanRemotePlayTogether.setup(vft, VFTIndexes::IClientAppManager::BCanRemotePlayTogether, hkClientAppManager_BCanRemotePlayTogether);
		Hooks::IClientAppManager_BIsDlcEnabled.setup(vft, VFTIndexes::IClientAppManager::BIsDlcEnabled, hkClientAppManager_BIsDlcEnabled);
		Hooks::IClientAppManager_GetAppUpdateInfo.setup(vft, VFTIndexes::IClientAppManager::GetUpdateInfo, hkClientAppManager_GetAppUpdateInfo);
		Hooks::IClientAppManager_LaunchApp.setup(vft, VFTIndexes::IClientAppManager::LaunchApp, hkClientAppManager_LaunchApp);
		Hooks::IClientAppManager_IsAppDlcInstalled.setup(vft, VFTIndexes::IClientAppManager::IsAppDlcInstalled, hkClientAppManager_IsAppDlcInstalled);
		Hooks::IClientAppManager_InstallApp.setup(vft, VFTIndexes::IClientAppManager::InstallApp, hkClientAppManager_InstallApp);
		Hooks::IClientAppManager_UninstallApp.setup(vft, VFTIndexes::IClientAppManager::UninstallApp, hkClientAppManager_UninstallApp);

		Hooks::IClientAppManager_BCanRemotePlayTogether.place();
		Hooks::IClientAppManager_BIsDlcEnabled.place();
		Hooks::IClientAppManager_GetAppUpdateInfo.place();
		Hooks::IClientAppManager_LaunchApp.place();
		Hooks::IClientAppManager_IsAppDlcInstalled.place();
		Hooks::IClientAppManager_InstallApp.place();
		Hooks::IClientAppManager_UninstallApp.place();

		LOG_DEBUG("IClientAppManager->vft at %p\n", reinterpret_cast<void*>(vft->vtable));
	}

	{
		const auto clientApps = usr->getClientApps();
		LOG_DEBUG("CUserAppInfo at %p\n", reinterpret_cast<void*>(clientApps));

		std::shared_ptr<lm_vmt_t> vft = std::make_shared<lm_vmt_t>();
		LM_VmtNew(*reinterpret_cast<lm_address_t**>(clientApps), vft.get());

		Hooks::IClientApps_GetDLCDataByIndex.setup(vft, VFTIndexes::IClientApps::GetDLCDataByIndex, hkClientApps_GetDLCDataByIndex);
		Hooks::IClientApps_GetDLCCount.setup(vft, VFTIndexes::IClientApps::GetDLCCount, hkClientApps_GetDLCCount);

		Hooks::IClientApps_GetDLCDataByIndex.place();
		Hooks::IClientApps_GetDLCCount.place();

		LOG_DEBUG("IClientApps->vft at %p\n", reinterpret_cast<void*>(vft->vtable));
	}

	{
		const auto clientUser = usr->getClientUser();
		LOG_DEBUG("IClientUser at %p\n", reinterpret_cast<void*>(clientUser));

		std::shared_ptr<lm_vmt_t> vft = std::make_shared<lm_vmt_t>();
		LM_VmtNew(*reinterpret_cast<lm_address_t**>(clientUser), vft.get());

		Hooks::IClientUser_BLoggedOn.setup(vft, VFTIndexes::IClientUser::BLoggedOn, &hkClientUser_BLoggedOn);
		Hooks::IClientUser_BUpdateAppOwnershipTicket.setup(vft, VFTIndexes::IClientUser::BUpdateAppOwnershipTicket, hkClientUser_BUpdateAppOwnershipTicket);
		Hooks::IClientUser_GetAppOwnershipTicketExtendedData.setup(vft, VFTIndexes::IClientUser::GetAppOwnershipTicketExtendedData, hkClientUser_GetAppOwnershipTicketExtendedData);
		//GetEncryptedAppTicket is just a wrapper for CUser::GetEncryptedAppTicket. But there is no need to go deeper
		//since we load the encrypted ticket in the Networking layer. We just need this function to spoof our steamId once
		Hooks::IClientUser_GetEncryptedAppTicket.setup(vft, VFTIndexes::IClientUser::GetEncryptedAppTicket, hkClientUser_GetEncryptedAppTicket);
		Hooks::IClientUser_GetLegacyCDKey.setup(vft, VFTIndexes::IClientUser::GetLegacyCDKey, hkClientUser_GetLegacyCDKey);
		Hooks::IClientUser_IsUserSubscribedAppInTicket.setup(vft, VFTIndexes::IClientUser::IsUserSubscribedAppInTicket, hkClientUser_IsUserSubscribedAppInTicket);

		Hooks::IClientUser_BLoggedOn.place();
		Hooks::IClientUser_BUpdateAppOwnershipTicket.place();
		Hooks::IClientUser_GetAppOwnershipTicketExtendedData.place();
		//Hooks::IClientUser_GetEncryptedAppTicket.place();
		Hooks::IClientUser_GetLegacyCDKey.place();
		Hooks::IClientUser_IsUserSubscribedAppInTicket.place();

		LOG_DEBUG("IClientUser->vft at %p\n", reinterpret_cast<void*>(vft->vtable));
	}

	{
		const auto utils = g_pSteamEngine->getUtils();
		LOG_DEBUG("IClientUtils at %p\n", reinterpret_cast<void*>(utils));

		std::shared_ptr<lm_vmt_t> vft = std::make_shared<lm_vmt_t>();
		LM_VmtNew(*reinterpret_cast<lm_address_t**>(utils), vft.get());

		Hooks::IClientUtils_GetAppId.setup(vft, VFTIndexes::IClientUtils::GetAppId, hkClientUtils_GetAppId);
		Hooks::IClientUtils_GetOfflineMode.setup(vft, VFTIndexes::IClientUtils::GetOfflineMode, hkClientUtils_GetOfflineMode);

		Hooks::IClientUtils_GetAppId.place();
		Hooks::IClientUtils_GetOfflineMode.place();

		LOG_DEBUG("IClientUtils->vft at %p\n", reinterpret_cast<void*>(vft->vtable));
	}

	#pragma GCC diagnostic pop

	hooked = true;
}

void Hooks::remove()
{
	//Detours
	TraceIPC.remove();

	CAPIJob_SendAndRecv.remove();

	CAppDataCache_BParseResponseFromMessage.remove();

	CClientUnifiedServiceMethod_SendAndRecvMsg.remove();

	CCMInterface_RecvPkt.remove();

	CSteamEngine_ProcessIPCFrame.remove();
	CSteamEngine_SetAppIdForCurrentPipe.remove();

	CSteamMatchmakingServers_GetServerDetails.remove();
	CSteamMatchmakingServers_RequestInternetServerList.remove();

	CUser_CheckAppOwnership.remove();
	CUser_GetSubscribedApps.remove();
	CUser_PostCallbackToAppId.remove();

	CUserAppManager_BuildDepotDependency.remove();

	CWebSocketConnection_BBuildAndAsyncSendFrame.remove();

	CGameInfoDialog_ServerResponded.remove();

	IClientCompat_BIsCompatLayerEnabled.remove();

	IClientConfigStore_SetString.remove();

	IClientFriends_GetFriendGamePlayed.remove();

	IClientRemoteStorage_IsCloudEnabledForApp.remove();

	//VFT Hooks
	IClientAppManager_BCanRemotePlayTogether.remove();
	IClientAppManager_BIsDlcEnabled.remove();
	IClientAppManager_GetAppUpdateInfo.remove();
	IClientAppManager_LaunchApp.remove();
	IClientAppManager_IsAppDlcInstalled.remove();
	IClientAppManager_InstallApp.remove();
	IClientAppManager_UninstallApp.remove();
	IClientAppManager_GetAppInstallState.remove();
	IClientAppManager_GetAppInstallState_Backup.remove();

	IClientApps_GetAppData.remove();
	IClientApps_GetAppDataSection.remove();
	IClientApps_GetDLCDataByIndex.remove();
	IClientApps_GetDLCCount.remove();

	IClientUser_BLoggedOn.remove();
	IClientUser_BUpdateAppOwnershipTicket.remove();
	IClientUser_GetAppOwnershipTicketExtendedData.remove();
	//IClientUser_GetEncryptedAppTicket.remove();
	IClientUser_GetLegacyCDKey.remove();
	IClientUser_IsUserSubscribedAppInTicket.remove();

	IClientUtils_GetAppId.remove();
	IClientUtils_GetOfflineMode.remove();

}

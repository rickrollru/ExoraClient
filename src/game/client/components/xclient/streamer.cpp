#include "streamer.h"

#include <base/str.h>

#include <engine/shared/config.h>

#include <game/client/gameclient.h>

namespace XcStreamer
{
	bool Hides(const CGameClient *pGameClient, int ClientId)
	{
		const int Mode = g_Config.m_XcStreamerNames;
		if(Mode == HIDE_OFF || ClientId < 0 || ClientId >= MAX_CLIENTS)
			return false;
		if(Mode == HIDE_ALL)
			return true;
		if(ClientId == pGameClient->m_aLocalIds[0] || ClientId == pGameClient->m_aLocalIds[1])
			return true;
		return Mode == HIDE_FRIENDS && pGameClient->m_aClients[ClientId].m_Friend;
	}

	static char *NextBuffer()
	{
		static char s_aaBuffers[16][MAX_NAME_LENGTH + 16];
		static int s_Next = 0;
		s_Next = (s_Next + 1) % 16;
		return s_aaBuffers[s_Next];
	}

	const char *Name(const CGameClient *pGameClient, int ClientId, const char *pRealName)
	{
		if(!Hides(pGameClient, ClientId))
			return pRealName;
		char *pBuf = NextBuffer();
		const bool Own = ClientId == pGameClient->m_aLocalIds[0] || ClientId == pGameClient->m_aLocalIds[1];
		if(Own && ClientId == pGameClient->m_aLocalIds[0] && g_Config.m_XcStreamerReplacement[0])
			str_copy(pBuf, g_Config.m_XcStreamerReplacement, MAX_NAME_LENGTH + 16);
		else
			str_format(pBuf, MAX_NAME_LENGTH + 16, "%s %d", Own ? "Dummy" : "Player", ClientId);
		return pBuf;
	}

	const char *OwnName(const char *pRealName)
	{
		if(g_Config.m_XcStreamerNames == HIDE_OFF)
			return pRealName;
		return g_Config.m_XcStreamerReplacement[0] ? g_Config.m_XcStreamerReplacement : "Player";
	}
}

#ifndef GAME_CLIENT_COMPONENTS_XCLIENT_STREAMER_H
#define GAME_CLIENT_COMPONENTS_XCLIENT_STREAMER_H

class CGameClient;

// Streamer mode for names (from the Flower client's NameProtect): the own name, friends or
// everyone are shown as a replacement on name plates, in the scoreboards, the chat and the HUD.
// Only what this client shows changes, the server still knows the real names.
namespace XcStreamer
{
	enum
	{
		HIDE_OFF = 0,
		HIDE_OWN, // you and your dummy
		HIDE_FRIENDS, // you, your dummy and your friends
		HIDE_ALL,
	};
	bool Hides(const CGameClient *pGameClient, int ClientId);
	// the name to show; pRealName when nothing is hidden. The text lives until a few more calls
	const char *Name(const CGameClient *pGameClient, int ClientId, const char *pRealName);
	// the own name for places without a client id (the watermark)
	const char *OwnName(const char *pRealName);
}

#endif

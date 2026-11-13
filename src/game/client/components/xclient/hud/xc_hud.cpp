#include "xc_hud.h"

#include <base/math.h>
#include <base/str.h>

#include <engine/client.h>
#include <engine/external/imgui/imgui_internal.h>
#include <engine/graphics.h>
#include <engine/shared/config.h>

#include <generated/client_data.h>
#include <generated/protocol.h>

#include <game/client/components/xclient/ui/feedback.h>
#include <game/client/components/xclient/ui/icons.h>
#include <game/client/components/xclient/ui/lang.h>
#include <game/client/components/xclient/ui/theme.h>
#include <game/client/components/xclient/ui/widgets.h>
#include <game/client/components/xclient/streamer.h>
#include <game/client/gameclient.h>
#include <game/collision.h>

#include <algorithm>
#include <cmath>
#include <ctime>

using namespace XcUi;

// design sizes are for 1080p at 100 %; the fonts are rasterized at the size they are mostly drawn at:
// the atlas has no mipmaps, so text drawn much smaller than rasterized breaks up on small screens
static constexpr float FONT_OVERSAMPLE = 1.0f;

void CXcHud::OnConsoleInit()
{
	Console()->Register("xc_hud_edit", "", CFGFLAG_CLIENT, ConEditHud, this, "Open or close the HUD editor");
	Console()->Register("xc_focus", "", CFGFLAG_CLIENT, ConFocus, this, "Turn the focus mode on or off");
	Console()->Register("xc_island_player", "", CFGFLAG_CLIENT, ConIslandPlayer, this, "Open or close the player on the HUD island");
	Console()->Register("xc_friend_aim", "", CFGFLAG_CLIENT, ConFriendAim, this, "Add the player under the crosshair to friends, or remove them");
}

void CXcHud::ConEditHud(IConsole::IResult *pResult, void *pUserData)
{
	CXcHud *pSelf = static_cast<CXcHud *>(pUserData);
	pSelf->SetEditing(!pSelf->m_Editing);
}

void CXcHud::ConFocus(IConsole::IResult *pResult, void *pUserData)
{
	g_Config.m_XcFocusMode ^= 1;
	Toast(g_Config.m_XcFocusMode ? Tr("Focus mode on") : Tr("Focus mode off"), TOAST_INFO);
}

void CXcHud::ConFriendAim(IConsole::IResult *pResult, void *pUserData)
{
	// the player closest to the crosshair, like Flower's click friend
	CXcHud *pSelf = static_cast<CXcHud *>(pUserData);
	CGameClient *pGameClient = pSelf->GameClient();
	const vec2 Target = pGameClient->m_Controls.m_aTargetPos[g_Config.m_ClDummy];
	int Best = -1;
	float BestDistance = 56.0f;
	for(int i = 0; i < MAX_CLIENTS; i++)
	{
		if(!pGameClient->m_Snap.m_aCharacters[i].m_Active || i == pGameClient->m_aLocalIds[0] || i == pGameClient->m_aLocalIds[1])
			continue;
		const float Distance = distance(pGameClient->m_aClients[i].m_RenderPos, Target);
		if(Distance < BestDistance)
		{
			BestDistance = Distance;
			Best = i;
		}
	}
	if(Best < 0)
	{
		Toast(Tr("Aim at a player first"), TOAST_INFO);
		return;
	}
	const CGameClient::CClientData &Player = pGameClient->m_aClients[Best];
	char aBuf[128];
	if(Player.m_Friend)
	{
		pGameClient->Friends()->RemoveFriend(Player.m_aName, Player.m_aClan);
		str_format(aBuf, sizeof(aBuf), Tr("%s is no longer a friend"), XcStreamer::Name(pGameClient, Best, Player.m_aName));
	}
	else
	{
		pGameClient->Friends()->AddFriend(Player.m_aName, Player.m_aClan);
		str_format(aBuf, sizeof(aBuf), Tr("%s added to friends"), XcStreamer::Name(pGameClient, Best, Player.m_aName));
	}
	Toast(aBuf, TOAST_SUCCESS);
}

void CXcHud::OnInit()
{
	// the effects list used to start behind the editor panel on the right, move it to the left once
	if(g_Config.m_XcHudEffectsX == 940 && g_Config.m_XcHudEffectsY == 330)
	{
		g_Config.m_XcHudEffectsX = 80;
		g_Config.m_XcHudEffectsY = 420;
	}
	// the island took the top middle, where the timer and the input delay were: they move down once
	if(g_Config.m_XcHudTimerX == 500 && g_Config.m_XcHudTimerY == 38)
		g_Config.m_XcHudTimerY = 98;
	if(g_Config.m_XcHudLatencyX == 500 && g_Config.m_XcHudLatencyY == 95)
		g_Config.m_XcHudLatencyY = 150;
	// the island got the connection part: added once to the saved parts
	if(!g_Config.m_XcHudIslandConnAdded)
	{
		g_Config.m_XcHudIslandConnAdded = 1;
		if(!str_find(g_Config.m_XcHudIslandParts, "c"))
			str_append(g_Config.m_XcHudIslandParts, "c");
	}
	m_Backend.Init(Graphics(), Input(), Storage());
}

void CXcHud::OnShutdown()
{
	m_Backend.Shutdown();
}

void CXcHud::OnStateChange(int NewState, int OldState)
{
	// the editor needs the game behind it
	if(NewState != IClient::STATE_ONLINE && NewState != IClient::STATE_DEMOPLAYBACK)
		m_Editing = false;
	(void)OldState;
}

void CXcHud::SetEditing(bool Editing)
{
	if(Editing && Client()->State() != IClient::STATE_ONLINE && Client()->State() != IClient::STATE_DEMOPLAYBACK)
	{
		Toast(Tr("Join a server to edit the HUD"), TOAST_INFO);
		return;
	}
	m_Editing = Editing;
	m_Dragging = -1;
	m_EditorOpen = 0.0f;
	m_MenuKind = MENU_NONE;
	m_MotionValid = false;
	for(float &Pop : m_aPop)
		Pop = 1.0f;
	if(Editing)
	{
		GameClient()->m_XcMenu.SetActive(false);
		GameClient()->m_Menus.SetActive(false);
	}
}

CXcHud::SElementConfig CXcHud::Element(int Index) const
{
	switch(Index)
	{
	case ELEMENT_WATERMARK: return {"Watermark", &g_Config.m_XcHudWatermark, &g_Config.m_XcHudWatermarkX, &g_Config.m_XcHudWatermarkY, &g_Config.m_XcHudWatermarkScale, 110, 32};
	case ELEMENT_TIMER: return {"Timer", &g_Config.m_XcHudTimer, &g_Config.m_XcHudTimerX, &g_Config.m_XcHudTimerY, &g_Config.m_XcHudTimerScale, 500, 98};
	case ELEMENT_SPEED: return {"Speed", &g_Config.m_XcHudSpeed, &g_Config.m_XcHudSpeedX, &g_Config.m_XcHudSpeedY, &g_Config.m_XcHudSpeedScale, 455, 905};
	case ELEMENT_WEAPON: return {"Weapon", &g_Config.m_XcHudWeapon, &g_Config.m_XcHudWeaponX, &g_Config.m_XcHudWeaponY, &g_Config.m_XcHudWeaponScale, 565, 905};
	case ELEMENT_MUSIC: return {"Music", &g_Config.m_XcHudMusic, &g_Config.m_XcHudMusicX, &g_Config.m_XcHudMusicY, &g_Config.m_XcHudMusicScale, 880, 60};
	case ELEMENT_KEYS: return {"Keystrokes", &g_Config.m_XcHudKeys, &g_Config.m_XcHudKeysX, &g_Config.m_XcHudKeysY, &g_Config.m_XcHudKeysScale, 120, 640};
	case ELEMENT_LATENCY: return {"Input delay", &g_Config.m_XcHudLatency, &g_Config.m_XcHudLatencyX, &g_Config.m_XcHudLatencyY, &g_Config.m_XcHudLatencyScale, 500, 150};
	case ELEMENT_EFFECTS: return {"Effects", &g_Config.m_XcHudEffects, &g_Config.m_XcHudEffectsX, &g_Config.m_XcHudEffectsY, &g_Config.m_XcHudEffectsScale, 80, 420};
	case ELEMENT_ISLAND: return {"Island", &g_Config.m_XcHudIsland, &g_Config.m_XcHudIslandX, &g_Config.m_XcHudIslandY, &g_Config.m_XcHudIslandScale, 500, 38};
	default: return {"Top of the server", &g_Config.m_XcHudMinitop, &g_Config.m_XcHudMinitopX, &g_Config.m_XcHudMinitopY, &g_Config.m_XcHudMinitopScale, 920, 830};
	}
}

bool CXcHud::Visible() const
{
	if(Client()->State() != IClient::STATE_ONLINE && Client()->State() != IClient::STATE_DEMOPLAYBACK)
		return false;
	return m_Editing || (g_Config.m_XcHud && g_Config.m_ClShowhud);
}

bool CXcHud::HidesClassicState() const
{
	return g_Config.m_XcHud && g_Config.m_XcHudHideClassic;
}

bool CXcHud::HidesClassicTimer() const
{
	return g_Config.m_XcHud && g_Config.m_XcHudTimer;
}

float CXcHud::ClassicTopLeftShift(float InterfaceHeight) const
{
	// only when the watermark sits in the top left corner, where the classic HUD is
	if(!g_Config.m_XcHud || !g_Config.m_XcHudWatermark || g_Config.m_XcHudWatermarkX > 250 || g_Config.m_XcHudWatermarkY > 150)
		return 0.0f;
	const float Bottom = m_aaRects[ELEMENT_WATERMARK][3];
	if(Bottom <= 0.0f)
		return 0.0f;
	return (Bottom + 6.0f * Graphics()->ScreenHeight() / 1080.0f) / Graphics()->ScreenHeight() * InterfaceHeight;
}

float CXcHud::Unit(int Index) const
{
	return Graphics()->ScreenHeight() / 1080.0f * (*Element(Index).m_pScale) / 100.0f;
}

ImVec2 CXcHud::Center(int Index) const
{
	// in the editor the element is drawn where its spring is
	if(m_Editing && m_MotionValid)
		return ImVec2(m_aaDrawPos[Index][0], m_aaDrawPos[Index][1]);
	const SElementConfig Config = Element(Index);
	return ImVec2(*Config.m_pX / 1000.0f * Graphics()->ScreenWidth(), *Config.m_pY / 1000.0f * Graphics()->ScreenHeight());
}

// top left corner for an element of this size around its center, kept on the screen
ImVec2 CXcHud::PlaceRect(int Index, ImVec2 Size)
{
	const ImVec2 C = Center(Index);
	const float Margin = 10.0f * Graphics()->ScreenHeight() / 1080.0f;
	ImVec2 Min(C.x - Size.x * 0.5f, C.y - Size.y * 0.5f);
	Min.x = std::clamp(Min.x, Margin, std::max(Graphics()->ScreenWidth() - Size.x - Margin, Margin));
	Min.y = std::clamp(Min.y, Margin, std::max(Graphics()->ScreenHeight() - Size.y - Margin, Margin));
	m_aaRects[Index][0] = Min.x;
	m_aaRects[Index][1] = Min.y;
	m_aaRects[Index][2] = Min.x + Size.x;
	m_aaRects[Index][3] = Min.y + Size.y;
	return Min;
}

int CXcHud::LocalClientId() const
{
	const auto &Snap = GameClient()->m_Snap;
	if(Snap.m_SpecInfo.m_Active && Snap.m_SpecInfo.m_SpectatorId >= 0)
		return Snap.m_SpecInfo.m_SpectatorId;
	return Snap.m_LocalClientId;
}

int CXcHud::RaceTimeCentis(bool *pRacing) const
{
	const CNetObj_GameInfo *pInfo = GameClient()->m_Snap.m_pGameInfoObj;
	*pRacing = false;
	if(!pInfo)
		return 0;
	const int Conn = g_Config.m_ClDummy;
	float Ticks;
	if(pInfo->m_GameStateFlags & GAMESTATEFLAG_RACETIME)
	{
		// the warmup timer is negative here, see CHud::RenderGameTimer
		Ticks = Client()->GameTick(Conn) + pInfo->m_WarmupTimer + Client()->IntraGameTick(Conn);
		*pRacing = true;
	}
	else
		Ticks = Client()->GameTick(Conn) - pInfo->m_RoundStartTick + Client()->IntraGameTick(Conn);
	return std::max(0, (int)(Ticks * 100.0f / Client()->GameTickSpeed()));
}

float CXcHud::SpeedBps() const
{
	const int ClientId = LocalClientId();
	if(ClientId < 0 || !GameClient()->m_Snap.m_aCharacters[ClientId].m_Active)
		return 0.0f;
	const CNetObj_Character &Prev = GameClient()->m_Snap.m_aCharacters[ClientId].m_Prev;
	const CNetObj_Character &Cur = GameClient()->m_Snap.m_aCharacters[ClientId].m_Cur;
	const vec2 Vel = mix(vec2(Prev.m_VelX, Prev.m_VelY), vec2(Cur.m_VelX, Cur.m_VelY), Client()->IntraGameTick(g_Config.m_ClDummy));
	// velocity is sent in 1/256 units per tick, a block is 32 units
	return length(Vel) / 256.0f * Client()->GameTickSpeed() / 32.0f;
}

void CXcHud::RenderPlate(ImDrawList *pDraw, ImVec2 Min, ImVec2 Max, float Unit, float Alpha, float Rounding)
{
	const STheme &t = Theme();
	if(Rounding < 0.0f)
		Rounding = std::min(16.0f * Unit, (Max.y - Min.y) * 0.5f);
	// glass like the island: the game blurred under it when the render module runs, so the tint can be lighter
	const bool Glass = GameClient()->m_FxRenderer.ModuleActive() && Alpha > 0.5f && !m_Editing;
	if(Glass)
		m_vChatBlurRects.push_back({Min.x, Min.y, Max.x, Max.y, Rounding});
	pDraw->AddRectFilled(Min, Max, Col(ImVec4(t.m_Panel.x, t.m_Panel.y, t.m_Panel.z, Glass ? 0.74f : 0.86f), Alpha), Rounding);
	pDraw->AddRect(Min, Max, Col(t.m_Border, Alpha), Rounding, 0, 1.0f);
}

void CXcHud::RenderWatermark(ImDrawList *pDraw, float Alpha)
{
	const STheme &t = Theme();
	const float U = Unit(ELEMENT_WATERMARK);
	ImFont *pBold = m_Backend.Fonts().m_pTitle;
	ImFont *pLabel = m_Backend.Fonts().m_pLabel;
	const float TextSize = 14.5f * U;

	// the parts after the logo, each with an icon
	char aFps[32];
	char aPing[32];
	char aTime[16];
	// the frame rate is averaged and shown twice a second, so the number can be read and does not flicker
	m_FpsFrames++;
	m_FpsTime += Client()->RenderFrameTime();
	if(m_FpsTime >= 0.5f || m_FpsShown <= 0)
	{
		m_FpsShown = (int)std::round(m_FpsFrames / std::max(m_FpsTime, 0.0001f));
		m_FpsFrames = 0;
		m_FpsTime = 0.0f;
	}
	str_format(aFps, sizeof(aFps), "%d fps", m_FpsShown);
	const int Local = GameClient()->m_Snap.m_LocalClientId;
	const CNetObj_PlayerInfo *pInfo = Local >= 0 ? GameClient()->m_Snap.m_apPlayerInfos[Local] : nullptr;
	if(Client()->State() == IClient::STATE_DEMOPLAYBACK)
		str_copy(aPing, "demo");
	else
		str_format(aPing, sizeof(aPing), "%d ms", pInfo ? pInfo->m_Latency : 0);
	const time_t Now = std::time(nullptr);
	const struct tm *pTm = localtime(&Now);
	str_format(aTime, sizeof(aTime), "%02d:%02d", pTm ? pTm->tm_hour : 0, pTm ? pTm->tm_min : 0);
	const char *apTexts[] = {XcStreamer::OwnName(Client()->PlayerName()), aFps, aPing, aTime};
	// numbers get a fixed place (as wide as zeros, at least a few digits), so the plate does not wobble
	const auto Slot = [&](const char *pText, int MinDigits) {
		char aWidth[32] = "";
		int Digits = 0;
		for(const char *p = pText; *p; p++)
			Digits += *p >= '0' && *p <= '9';
		for(int i = Digits; i < MinDigits; i++)
			str_append(aWidth, "0");
		for(const char *p = pText; *p; p++)
		{
			const char aChar[2] = {*p >= '0' && *p <= '9' ? '0' : *p, '\0'};
			str_append(aWidth, aChar);
		}
		return pLabel->CalcTextSizeA(TextSize, FLT_MAX, 0.0f, aWidth).x;
	};
	const float aTextWidths[] = {pLabel->CalcTextSizeA(TextSize, FLT_MAX, 0.0f, apTexts[0]).x, Slot(aFps, 3), Slot(aPing, 3), Slot(aTime, 4)};

	// a small pill: the logo, the name, the frame rate, the ping with a colored dot (the time is on the island)
	const float Height = 36.0f * U;
	const float LogoSize = 22.0f * U;
	const float Gap = 12.0f * U;
	const char *pName = apTexts[0];
	const float NameW = pBold->CalcTextSizeA(TextSize, FLT_MAX, 0.0f, pName).x;
	const float DotGap = 12.0f * U;
	const float Width = 7.0f * U + LogoSize + 10.0f * U + NameW + (Gap * 2.0f + 1.0f + aTextWidths[1]) + (Gap * 2.0f + 1.0f + DotGap + aTextWidths[2]) + 14.0f * U;
	const ImVec2 Min = PlaceRect(ELEMENT_WATERMARK, ImVec2(Width, Height));
	const ImVec2 Max(Min.x + Width, Min.y + Height);
	RenderPlate(pDraw, Min, Max, U, Alpha, Height * 0.5f);
	const float MidY = Min.y + Height * 0.5f;
	float x = Min.x + 7.0f * U;
	DrawLogo(pDraw, ImVec2(x + LogoSize * 0.5f, MidY), LogoSize, t.m_Accent, Alpha);
	x += LogoSize + 10.0f * U;
	pDraw->AddText(pBold, TextSize, ImVec2(x, MidY - TextSize * 0.55f), Col(t.m_Text, Alpha), pName);
	x += NameW;
	const auto Sep = [&]() {
		x += Gap;
		pDraw->AddLine(ImVec2(x, MidY - 7.0f * U), ImVec2(x, MidY + 7.0f * U), Col(t.m_Border, 2.0f * Alpha), 1.0f);
		x += 1.0f + Gap;
	};
	Sep();
	pDraw->AddText(pLabel, TextSize, ImVec2(x, MidY - TextSize * 0.55f), Col(t.m_TextSecondary, Alpha), apTexts[1]);
	x += aTextWidths[1];
	Sep();
	const int Ping = pInfo ? pInfo->m_Latency : 0;
	const ImVec4 PingColor = Ping < 80 ? ImVec4(0.24f, 0.84f, 0.64f, 1.0f) : (Ping < 160 ? ImVec4(1.0f, 0.7f, 0.25f, 1.0f) : ImVec4(1.0f, 0.3f, 0.3f, 1.0f));
	pDraw->AddCircleFilled(ImVec2(x + 3.0f * U, MidY), 3.0f * U, Col(PingColor, Alpha), 12);
	x += DotGap;
	pDraw->AddText(pLabel, TextSize, ImVec2(x, MidY - TextSize * 0.55f), Col(t.m_TextSecondary, Alpha), apTexts[2]);
}

void CXcHud::RenderTimer(ImDrawList *pDraw, float Alpha)
{
	const STheme &t = Theme();
	const float U = Unit(ELEMENT_TIMER);
	ImFont *pBold = m_Backend.Fonts().m_pHeader;
	ImFont *pSmall = m_Backend.Fonts().m_pSmall;
	bool Racing;
	const int Centis = RaceTimeCentis(&Racing);
	char aTime[32];
	if(Centis >= 360000)
		str_format(aTime, sizeof(aTime), "%d:%02d:%02d.%02d", Centis / 360000, (Centis / 6000) % 60, (Centis / 100) % 60, Centis % 100);
	else
		str_format(aTime, sizeof(aTime), "%02d:%02d.%02d", Centis / 6000, (Centis / 100) % 60, Centis % 100);

	// the wide face for the digits, the best time of this map quietly under them
	ImFont *pDisplay = m_Backend.Fonts().m_pDisplay ? m_Backend.Fonts().m_pDisplay : pBold;
	const float TimeSize = 24.0f * U;
	// the width is taken from zeros, so the plate does not wobble while the digits change
	char aWidth[32];
	str_copy(aWidth, aTime);
	for(char *p = aWidth; *p; p++)
		if(*p >= '0' && *p <= '9')
			*p = '0';
	const float TextWidth = pDisplay->CalcTextSizeA(TimeSize, FLT_MAX, 0.0f, aWidth).x;
	char aBest[64] = "";
	const int Local = LocalClientId();
	if(Local >= 0 && GameClient()->m_ReceivedDDNetPlayerFinishTimes)
	{
		const auto &Me = GameClient()->m_aClients[Local];
		if(Me.m_FinishTimeSeconds != FinishTime::UNSET && Me.m_FinishTimeSeconds != FinishTime::NOT_FINISHED_MILLIS && Me.m_FinishTimeSeconds >= 0)
			str_format(aBest, sizeof(aBest), "%s %02d:%02d.%02d", Tr("best"), Me.m_FinishTimeSeconds / 60, Me.m_FinishTimeSeconds % 60, std::clamp(Me.m_FinishTimeMillis / 10, 0, 99));
	}
	const float SubSize = 11.5f * U;
	const float SubW = aBest[0] ? pSmall->CalcTextSizeA(SubSize, FLT_MAX, 0.0f, aBest).x : 0.0f;
	const float Width = std::max(TextWidth, SubW) + 32.0f * U;
	const float Height = aBest[0] ? 58.0f * U : 46.0f * U;
	const ImVec2 Min = PlaceRect(ELEMENT_TIMER, ImVec2(Width, Height));
	const ImVec2 Max(Min.x + Width, Min.y + Height);
	RenderPlate(pDraw, Min, Max, U, Alpha);
	const float Cx = (Min.x + Max.x) * 0.5f;
	const float TimeW = pDisplay->CalcTextSizeA(TimeSize, FLT_MAX, 0.0f, aTime).x;
	const float TimeY = Min.y + (aBest[0] ? 8.0f : 11.0f) * U;
	pDraw->AddText(pDisplay, TimeSize, ImVec2(Cx - TimeW * 0.5f, TimeY), Col(Racing ? t.m_Text : t.m_TextSecondary, Alpha), aTime);
	if(aBest[0])
		pDraw->AddText(pSmall, SubSize, ImVec2(Cx - SubW * 0.5f, Max.y - SubSize - 8.0f * U), Col(t.m_TextMuted, Alpha), aBest);
}

void CXcHud::RenderSpeed(ImDrawList *pDraw, float Alpha)
{
	const STheme &t = Theme();
	const float U = Unit(ELEMENT_SPEED);
	ImFont *pBold = m_Backend.Fonts().m_pHeader;
	ImFont *pSmall = m_Backend.Fonts().m_pSmall;
	const float Speed = SpeedBps();
	char aSpeed[32];
	str_format(aSpeed, sizeof(aSpeed), "%.1f", Speed);

	// the speed of the last three seconds, 30 samples a second (the same at any frame rate)
	m_SpeedHistoryTime += std::min(Client()->RenderFrameTime(), 0.1f);
	while(m_SpeedHistoryTime >= 1.0f / 30.0f)
	{
		m_SpeedHistoryTime -= 1.0f / 30.0f;
		m_aSpeedHistory[m_SpeedHistoryPos] = Speed;
		m_SpeedHistoryPos = (m_SpeedHistoryPos + 1) % (int)std::size(m_aSpeedHistory);
	}
	ImFont *pDisplay = m_Backend.Fonts().m_pDisplay ? m_Backend.Fonts().m_pDisplay : pBold;
	const float Width = 168.0f * U;
	const float Height = 72.0f * U;
	const ImVec2 Min = PlaceRect(ELEMENT_SPEED, ImVec2(Width, Height));
	const ImVec2 Max(Min.x + Width, Min.y + Height);
	RenderPlate(pDraw, Min, Max, U, Alpha);

	const float Pad = 14.0f * U;
	const float ValueSize = 21.0f * U;
	const float SmallSize = 12.0f * U;
	pDraw->AddText(pDisplay, ValueSize, ImVec2(Min.x + Pad, Min.y + 8.0f * U), Col(t.m_Text, Alpha), aSpeed);
	const float ValueWidth = pDisplay->CalcTextSizeA(ValueSize, FLT_MAX, 0.0f, aSpeed).x;
	pDraw->AddText(pSmall, SmallSize, ImVec2(Min.x + Pad + ValueWidth + 5.0f * U, Min.y + 8.0f * U + ValueSize - SmallSize - 2.0f * U), Col(t.m_TextMuted, Alpha), Tr("blocks/s"));

	// the graph: a line in the accent with a soft fill under it; full height at 40 blocks per second
	const int Num = (int)std::size(m_aSpeedHistory);
	const float GL = Min.x + Pad, GR = Max.x - Pad, GB = Max.y - 10.0f * U, GT = GB - 24.0f * U;
	const auto Point = [&](int i) {
		const float v = m_aSpeedHistory[(m_SpeedHistoryPos + i) % Num];
		return ImVec2(GL + (GR - GL) * i / (Num - 1), GB - (GB - GT) * std::clamp(v / 40.0f, 0.0f, 1.0f));
	};
	const ImU32 Top = Col(t.m_Accent, 0.35f * Alpha), Bottom = Col(t.m_Accent, 0.0f);
	for(int i = 0; i + 1 < Num; i++)
	{
		const ImVec2 P0 = Point(i), P1 = Point(i + 1);
		pDraw->PrimReserve(6, 4);
		const ImVec2 Uv = ImGui::GetFontTexUvWhitePixel();
		const ImDrawIdx Idx = (ImDrawIdx)pDraw->_VtxCurrentIdx;
		pDraw->PrimWriteVtx(P0, Uv, Top);
		pDraw->PrimWriteVtx(P1, Uv, Top);
		pDraw->PrimWriteVtx(ImVec2(P1.x, GB), Uv, Bottom);
		pDraw->PrimWriteVtx(ImVec2(P0.x, GB), Uv, Bottom);
		pDraw->PrimWriteIdx(Idx);
		pDraw->PrimWriteIdx(Idx + 1);
		pDraw->PrimWriteIdx(Idx + 2);
		pDraw->PrimWriteIdx(Idx);
		pDraw->PrimWriteIdx(Idx + 2);
		pDraw->PrimWriteIdx(Idx + 3);
	}
	for(int i = 0; i < Num; i++)
		pDraw->PathLineTo(Point(i));
	pDraw->PathStroke(Col(t.m_Accent, Alpha), ImDrawFlags_None, 2.0f * U);
}

void CXcHud::RenderWeapon(ImDrawList *pDraw, float Alpha)
{
	const STheme &t = Theme();
	const float U = Unit(ELEMENT_WEAPON);
	const int ClientId = LocalClientId();
	int Weapon = WEAPON_HAMMER;
	int Ammo = 0;
	if(ClientId >= 0 && GameClient()->m_Snap.m_aCharacters[ClientId].m_Active)
	{
		Weapon = std::clamp(GameClient()->m_Snap.m_aCharacters[ClientId].m_Cur.m_Weapon, 0, NUM_WEAPONS - 1);
		Ammo = GameClient()->m_Snap.m_aCharacters[ClientId].m_Cur.m_AmmoCount;
		// the tee you play changes its weapon at once (prediction), the server snapshot comes a ping later:
		// show the predicted one like the cursor does, so the HUD switches together with the tee
		if(Client()->State() != IClient::STATE_DEMOPLAYBACK && ClientId == GameClient()->m_Snap.m_LocalClientId)
		{
			const int Predicted = GameClient()->m_aClients[ClientId].m_Predicted.m_ActiveWeapon;
			if(Predicted >= 0 && Predicted < NUM_WEAPONS)
			{
				if(Predicted != Weapon)
					Ammo = 0;
				Weapon = Predicted;
			}
		}
	}
	static const char *const s_apNames[NUM_WEAPONS] = {"Hammer", "Gun", "Shotgun", "Grenade", "Laser", "Ninja"};
	ImFont *pLabel = m_Backend.Fonts().m_pLabel;
	const float TextSize = 13.0f * U;

	const CDataSprite *pSprite = g_pData->m_Weapons.m_aId[Weapon].m_pSpriteBody;
	const float Aspect = pSprite && pSprite->m_H > 0 ? (float)pSprite->m_W / pSprite->m_H : 2.0f;
	const float IconHeight = 26.0f * U;
	const float IconWidth = std::min(IconHeight * Aspect, 64.0f * U);
	const char *pName = Tr(s_apNames[Weapon]);
	// ammo as small dots when the weapon has limited ammo
	const int Dots = (Ammo > 0 && Ammo <= 10 && Weapon != WEAPON_HAMMER && Weapon != WEAPON_NINJA) ? Ammo : 0;
	const float NameWidth = pLabel->CalcTextSizeA(TextSize, FLT_MAX, 0.0f, pName).x;
	const float DotsWidth = Dots * 8.0f * U;

	// all weapons the tee has and its jumps, instead of the classic HUD in the corner (xc_hud_weapon_list)
	bool aGot[NUM_WEAPONS] = {};
	int NumGot = 0;
	int TotalJumps = 0, FreeJumps = 0;
	const bool List = g_Config.m_XcHudWeaponList && ClientId >= 0 && GameClient()->m_Snap.m_aCharacters[ClientId].m_Active;
	if(List)
	{
		const CCharacterCore &Core = GameClient()->m_aClients[ClientId].m_Predicted;
		for(int i = 0; i < NUM_WEAPONS; i++)
		{
			aGot[i] = Core.m_aWeapons[i].m_Got;
			NumGot += aGot[i];
		}
		const auto &Snap = GameClient()->m_Snap.m_aCharacters[ClientId];
		if(Snap.m_HasExtendedDisplayInfo)
		{
			// the same count as the classic jump indicator (CHud::RenderPlayerState)
			const CNetObj_Character &Player = GameClient()->m_aClients[ClientId].m_RenderCur;
			const bool Grounded = Collision()->IsOnGround(vec2(Player.m_X, Player.m_Y), CCharacterCore::PhysicalSize());
			int Used = Core.m_JumpedTotal;
			if(Core.m_Jumps > 1)
				Used += !Grounded;
			else if(Core.m_Jumps == 1)
				Used = Player.m_Jumped & 2;
			else if(Core.m_Jumps == -1)
				Used = !Grounded;
			if(Core.m_EndlessJump && Used >= absolute(Core.m_Jumps))
				Used = absolute(Core.m_Jumps) - 1;
			int Unused = absolute(Core.m_Jumps) - Used;
			if(!(Player.m_Jumped & 2) && Unused <= 0)
				Unused = 1;
			TotalJumps = std::clamp(absolute(Core.m_Jumps), 0, 10);
			FreeJumps = std::clamp(Unused, 0, TotalJumps);
		}
		else if(Snap.m_HasExtendedData)
			TotalJumps = FreeJumps = std::clamp(absolute(Snap.m_ExtendedData.m_Jumps), 0, 10);
	}
	// one row: the weapons the tee has as tiles, the one in the hand lit (its ammo as dots under it), then the jumps
	// as dots after a thin line; without the list only the weapon in the hand
	if(!List || NumGot == 0)
	{
		for(bool &Got : aGot)
			Got = false;
		aGot[Weapon] = true;
		NumGot = 1;
	}
	const float TileW = 42.0f * U, TileH = 36.0f * U, TileGap = 4.0f * U, Pad = 8.0f * U;
	const float JumpsW = TotalJumps > 0 ? 12.0f * U + 1.0f + TotalJumps * 12.0f * U + 2.0f * U : 0.0f;
	const float Width = Pad * 2.0f + NumGot * TileW + (NumGot - 1) * TileGap + JumpsW;
	const float Height = TileH + Pad * 2.0f;
	const ImVec2 Min = PlaceRect(ELEMENT_WEAPON, ImVec2(Width, Height));
	const ImVec2 Max(Min.x + Width, Min.y + Height);
	RenderPlate(pDraw, Min, Max, U, Alpha);
	const float Dt = std::min(Client()->RenderFrameTime(), 0.1f);
	float x = Min.x + Pad;
	const float y = Min.y + Pad;
	for(int i = 0; i < NUM_WEAPONS; i++)
	{
		if(!aGot[i])
			continue;
		// the light moves softly to the weapon taken
		m_aWeaponLit[i] = i == Weapon ? std::min(m_aWeaponLit[i] + Dt * 10.0f, 1.0f) : std::max(m_aWeaponLit[i] - Dt * 10.0f, 0.0f);
		const float Lit = m_aWeaponLit[i];
		if(Lit > 0.0f)
			pDraw->AddRectFilled(ImVec2(x, y), ImVec2(x + TileW, y + TileH), Col(ImVec4(t.m_Accent.x, t.m_Accent.y, t.m_Accent.z, 0.2f * Lit), Alpha), 10.0f * U);
		const CDataSprite *pSprite2 = g_pData->m_Weapons.m_aId[i].m_pSpriteBody;
		const float SpriteAspect = pSprite2 && pSprite2->m_H > 0 ? (float)pSprite2->m_W / pSprite2->m_H : 2.0f;
		const float IconW = std::min(TileW - 8.0f * U, (TileH - 14.0f * U) * SpriteAspect);
		const float IconH = IconW / SpriteAspect;
		const IGraphics::CTextureHandle Tex = GameClient()->m_GameSkin.m_aSpriteWeapons[i];
		const float IconY = y + (TileH - IconH) * 0.5f - (i == Weapon && Dots ? 3.0f * U : 0.0f);
		if(Tex.IsValid())
			pDraw->AddImage((ImTextureID)(uintptr_t)m_Backend.TextureId(Tex), ImVec2(x + (TileW - IconW) * 0.5f, IconY), ImVec2(x + (TileW + IconW) * 0.5f, IconY + IconH), ImVec2(0, 0), ImVec2(1, 1), Col(ImVec4(1, 1, 1, 1), (0.45f + 0.55f * Lit) * Alpha));
		if(i == Weapon && Dots)
		{
			const float DotStep = std::min(4.0f * U, (TileW - 8.0f * U) / Dots);
			const float DX = x + TileW * 0.5f - (Dots - 1) * DotStep * 0.5f;
			for(int d = 0; d < Dots; d++)
				pDraw->AddCircleFilled(ImVec2(DX + d * DotStep, y + TileH - 5.0f * U), 1.4f * U, Col(t.m_Accent, Alpha), 8);
		}
		x += TileW + TileGap;
	}
	if(TotalJumps > 0)
	{
		x += 12.0f * U - TileGap;
		pDraw->AddLine(ImVec2(x, y + 8.0f * U), ImVec2(x, y + TileH - 8.0f * U), Col(t.m_Border, 2.0f * Alpha), 1.0f);
		x += 1.0f + 6.0f * U;
		for(int i = 0; i < TotalJumps; i++)
		{
			const ImVec2 C(x + i * 12.0f * U + 4.0f * U, y + TileH * 0.5f);
			if(i < FreeJumps)
				pDraw->AddCircleFilled(C, 4.0f * U, Col(t.m_Accent, Alpha), 16);
			else
				pDraw->AddCircleFilled(C, 3.2f * U, Col(ImVec4(1, 1, 1, 0.16f), Alpha), 16);
		}
	}
	(void)pName;
	(void)NameWidth;
	(void)DotsWidth;
	(void)IconWidth;
	(void)pLabel;
	(void)TextSize;
}

void CXcHud::RenderKeys(ImDrawList *pDraw, float Alpha)
{
	const STheme &t = Theme();
	const float U = Unit(ELEMENT_KEYS);
	ImFont *pLabel = m_Backend.Fonts().m_pLabel;
	const CNetObj_PlayerInput &Input = GameClient()->m_Controls.m_aInputData[g_Config.m_ClDummy];
	// left, jump, right on top; fire and hook below
	const bool aDown[5] = {Input.m_Direction < 0, Input.m_Jump != 0, Input.m_Direction > 0, (Input.m_Fire & 1) != 0, Input.m_Hook != 0};
	const float Dt = std::min(Client()->RenderFrameTime(), 0.1f);
	for(int i = 0; i < 5; i++)
		m_aKeyPress[i] = aDown[i] ? std::min(m_aKeyPress[i] + Dt * 22.0f, 1.0f) : std::max(m_aKeyPress[i] - Dt * 7.0f, 0.0f);

	const float Key = 38.0f * U;
	const float Gap = 6.0f * U;
	const float Pad = 8.0f * U;
	const float Width = Key * 3.0f + Gap * 2.0f + Pad * 2.0f;
	const float Height = Key * 2.0f - 6.0f * U + Gap + Pad * 2.0f;
	const ImVec2 Min = PlaceRect(ELEMENT_KEYS, ImVec2(Width, Height));
	RenderPlate(pDraw, Min, ImVec2(Min.x + Width, Min.y + Height), U, Alpha);
	const float Small = Key - 6.0f * U;
	const float WideKey = (Width - Pad * 2.0f - Gap) * 0.5f;
	const ImVec2 aMin[5] = {
		ImVec2(Min.x + Pad, Min.y + Pad),
		ImVec2(Min.x + Pad + Key + Gap, Min.y + Pad),
		ImVec2(Min.x + Pad + (Key + Gap) * 2.0f, Min.y + Pad),
		ImVec2(Min.x + Pad, Min.y + Pad + Key + Gap),
		ImVec2(Min.x + Pad + WideKey + Gap, Min.y + Pad + Key + Gap),
	};
	const ImVec2 aSize[5] = {ImVec2(Key, Key), ImVec2(Key, Key), ImVec2(Key, Key), ImVec2(WideKey, Small), ImVec2(WideKey, Small)};
	const char *apLabels[5] = {XC_ICON_ARROW_LEFT, XC_ICON_ARROW_UP, XC_ICON_ARROW_RIGHT, Tr("Fire"), Tr("Hook")};
	for(int i = 0; i < 5; i++)
	{
		const float Press = m_aKeyPress[i];
		// a pressed key sinks a little and fills with the accent color
		const float Sink = Press * 2.0f * U;
		const ImVec2 KeyMin(aMin[i].x, aMin[i].y + Sink);
		const ImVec2 KeyMax(aMin[i].x + aSize[i].x, aMin[i].y + aSize[i].y + Sink * 0.5f);
		if(Press > 0.0f)
			pDraw->AddRectFilled(ImVec2(KeyMin.x - 3.0f * U, KeyMin.y - 3.0f * U), ImVec2(KeyMax.x + 3.0f * U, KeyMax.y + 3.0f * U), Col(t.m_Accent, 0.22f * Press * Alpha), 10.0f * U);
		pDraw->AddRectFilled(KeyMin, KeyMax, Col(Mix(t.m_Track, t.m_Accent, Press), Alpha), 8.0f * U);
		pDraw->AddRect(KeyMin, KeyMax, Col(Mix(t.m_Border, t.m_Accent, Press), Alpha), 8.0f * U, 0, 1.0f);
		const float TextSize = (i < 3 ? 15.0f : 12.5f) * U;
		const ImVec2 TextDim = pLabel->CalcTextSizeA(TextSize, FLT_MAX, 0.0f, apLabels[i]);
		pDraw->AddText(pLabel, TextSize, ImVec2((KeyMin.x + KeyMax.x - TextDim.x) * 0.5f, (KeyMin.y + KeyMax.y - TextDim.y) * 0.5f), Col(Mix(t.m_TextSecondary, t.m_OnAccent, Press), Alpha), apLabels[i]);
	}
}

void CXcHud::RenderLatency(ImDrawList *pDraw, float Alpha)
{
	const STheme &t = Theme();
	const float U = Unit(ELEMENT_LATENCY);
	// a new key or mouse event since the last frame: how old it is now, when this frame is built
	unsigned KeyTicks, MouseTicks, NowTicks;
	Input()->XcInputTimes(&KeyTicks, &MouseTicks, &NowTicks);
	const auto Sample = [&](unsigned Ticks, unsigned &Last, float &Average) {
		if(Ticks == Last || Ticks == 0)
			return;
		Last = Ticks;
		const float Delay = (float)(NowTicks - Ticks);
		if(Delay < 0.0f || Delay > 500.0f)
			return;
		Average = Average < 0.0f ? Delay : mix(Average, Delay, 0.15f);
	};
	Sample(KeyTicks, m_LastKeyTicks, m_KeyDelay);
	Sample(MouseTicks, m_LastMouseTicks, m_MouseDelay);
	m_FrameMs = mix(m_FrameMs, Client()->RenderFrameTime() * 1000.0f, 0.05f);

	ImFont *pLabel = m_Backend.Fonts().m_pLabel;
	ImFont *pSmall = m_Backend.Fonts().m_pSmall;
	const float ValueSize = 15.0f * U;
	const float CaptionSize = 10.5f * U;
	struct SCell
	{
		const char *m_pCaption;
		char m_aValue[16];
		float m_Ms;
		int m_Kind; // 0 = a delay in ms, 1 = the prediction ahead in ms, 2 = corrections per second
	};
	const CXcPrediction &Prediction = GameClient()->m_XcPrediction;
	const bool Online = Client()->State() == IClient::STATE_ONLINE;
	SCell aCells[5] = {{Tr("Keys"), "", m_KeyDelay, 0}, {Tr("Mouse"), "", m_MouseDelay, 0}, {Tr("Frame time"), "", m_FrameMs, 0},
		{Tr("Prediction"), "", Online ? (float)Client()->GetPredictionTime() : -1.0f, 1}, {Tr("Fixes/s"), "", Online ? Prediction.CorrectionsPerSecond() : -1.0f, 2}};
	const int NumCells = g_Config.m_XcHudLatencyPred ? 5 : 3;
	// one quiet line: the caption, then the value bright; a value that lags turns orange or red
	const float CaptionGap = 5.0f * U, CellGap = 14.0f * U;
	float aCellW[5] = {};
	float Width = 14.0f * U * 2.0f - CellGap;
	char aaText[5][32];
	for(int i = 0; i < NumCells; i++)
	{
		SCell &Cell = aCells[i];
		if(Cell.m_Ms < 0.0f)
			str_copy(Cell.m_aValue, "–");
		else if(Cell.m_Kind == 2)
			str_format(Cell.m_aValue, sizeof(Cell.m_aValue), "%.1f", Cell.m_Ms);
		else
			str_format(Cell.m_aValue, sizeof(Cell.m_aValue), Cell.m_Ms < 10.0f ? "%.1f" : "%.0f", Cell.m_Ms);
		if(Cell.m_Kind == 2 || Cell.m_Ms < 0.0f)
			str_copy(aaText[i], Cell.m_aValue);
		else
			str_format(aaText[i], sizeof(aaText[i]), "%s %s", Cell.m_aValue, Tr("ms"));
		// the value gets the room of its widest form, so the line does not wobble
		const float ValueW = std::max(pLabel->CalcTextSizeA(CaptionSize * 1.15f, FLT_MAX, 0.0f, Cell.m_Kind == 2 ? "00.0" : "00.0 ms").x, pLabel->CalcTextSizeA(CaptionSize * 1.15f, FLT_MAX, 0.0f, aaText[i]).x);
		aCellW[i] = pSmall->CalcTextSizeA(CaptionSize * 1.1f, FLT_MAX, 0.0f, Cell.m_pCaption).x + CaptionGap + ValueW;
		Width += aCellW[i] + CellGap;
	}
	const float Height = 28.0f * U;
	const ImVec2 Min = PlaceRect(ELEMENT_LATENCY, ImVec2(Width, Height));
	RenderPlate(pDraw, Min, ImVec2(Min.x + Width, Min.y + Height), U, Alpha, Height * 0.5f);
	const float MidY = Min.y + Height * 0.5f;
	float x = Min.x + 14.0f * U;
	(void)ValueSize;
	for(int i = 0; i < NumCells; i++)
	{
		const SCell &Cell = aCells[i];
		const ImVec4 Orange(1.0f, 0.62f, 0.3f, 1.0f), Red(1.0f, 0.36f, 0.36f, 1.0f);
		ImVec4 Color = t.m_Text;
		if(Cell.m_Ms < 0.0f)
			Color = t.m_TextMuted;
		else if(Cell.m_Kind == 0 && Cell.m_Ms >= 12.0f)
			Color = Cell.m_Ms < 25.0f ? Orange : Red;
		else if(Cell.m_Kind == 2 && Cell.m_Ms >= 0.3f)
			Color = Cell.m_Ms < 2.0f ? Orange : Red;
		const float CapSize = CaptionSize * 1.1f, ValSize = CaptionSize * 1.15f;
		pDraw->AddText(pSmall, CapSize, ImVec2(x, MidY - CapSize * 0.55f), Col(t.m_TextMuted, Alpha), Cell.m_pCaption);
		const float CapW = pSmall->CalcTextSizeA(CapSize, FLT_MAX, 0.0f, Cell.m_pCaption).x;
		pDraw->AddText(pLabel, ValSize, ImVec2(x + CapW + CaptionGap, MidY - ValSize * 0.55f), Col(Color, Alpha), aaText[i]);
		x += aCellW[i] + CellGap;
	}
}

void CXcHud::ShowBanner(const char *pTitle, const char *pSubtitle, int Kind)
{
	if(IslandTakesNotes())
	{
		IslandNotify(pTitle, pSubtitle, Kind);
		PlayUiSound(SOUND_NOTIFY);
		return;
	}
	str_copy(m_aBannerTitle, pTitle);
	str_copy(m_aBannerSubtitle, pSubtitle);
	m_BannerKind = Kind;
	m_BannerStart = Client()->GlobalTime();
	PlayUiSound(SOUND_NOTIFY);
}

void CXcHud::RenderBanner(ImDrawList *pDraw)
{
	constexpr float IN = 0.35f, STAY = 3.4f, OUT = 0.5f;
	const float Age = Client()->GlobalTime() - m_BannerStart;
	if(Age < 0.0f || Age > IN + STAY + OUT)
		return;
	const STheme &t = Theme();
	// pops in a bit too big and settles, then fades up and away
	const float In = std::clamp(Age / IN, 0.0f, 1.0f);
	const float Out = std::clamp((Age - IN - STAY) / OUT, 0.0f, 1.0f);
	const float Pop = 1.0f + 0.12f * std::sin(In * pi) * (1.0f - In * 0.5f);
	const float Alpha = In * (1.0f - Out);
	const float U = Graphics()->ScreenHeight() / 1080.0f * Pop;
	const ImVec4 Gold(1.0f, 0.78f, 0.25f, 1.0f);
	const ImVec4 Color = m_BannerKind == 2 ? Gold : (m_BannerKind == 1 ? t.m_Accent : t.m_TextSecondary);
	ImFont *pTitleFont = m_Backend.Fonts().m_pHeader;
	ImFont *pSubFont = m_Backend.Fonts().m_pTitle;
	const float TitleSize = 30.0f * U;
	const float SubSize = 19.0f * U;
	const ImVec2 TitleDim = pTitleFont->CalcTextSizeA(TitleSize, FLT_MAX, 0.0f, m_aBannerTitle);
	const ImVec2 SubDim = pSubFont->CalcTextSizeA(SubSize, FLT_MAX, 0.0f, m_aBannerSubtitle);
	const float Width = std::max(TitleDim.x, SubDim.x) + 80.0f * U;
	const float Height = 96.0f * U;
	const float CenterX = Graphics()->ScreenWidth() * 0.5f;
	const float Top = Graphics()->ScreenHeight() * 0.16f - Out * 30.0f * U;
	const ImVec2 Min(CenterX - Width * 0.5f, Top);
	const ImVec2 Max(CenterX + Width * 0.5f, Top + Height);
	const float Rounding = 18.0f * U;
	// soft colored glow behind (the render module blurs it; without it a few wider plates), the plate, a colored rim
	if(GameClient()->m_FxRenderer.ModuleActive())
	{
		// drawn now, before the HUD frame, so it lies under the plate
		const CScreenRect Saved = Graphics()->GetScreen();
		Graphics()->MapScreen(CScreenRect(0.0f, 0.0f, (float)Graphics()->ScreenWidth(), (float)Graphics()->ScreenHeight()));
		const vec2 Center((Min.x + Max.x) * 0.5f, (Min.y + Max.y) * 0.5f);
		const vec2 Half((Max.x - Min.x) * 0.5f, (Max.y - Min.y) * 0.5f);
		const std::vector<SFxShape> vGlow = {FxShape::RoundRect(Center, Half, Rounding, ColorRGBA(Color.x, Color.y, Color.z, 0.55f * Alpha)).GlowOnly(30.0f * U, 0.8f).Additive()};
		GameClient()->m_FxRenderer.Draw(vGlow, false);
		Graphics()->MapScreen(Saved);
	}
	else
	{
		for(int i = 4; i >= 1; i--)
		{
			const float Grow = i * 7.0f * U;
			pDraw->AddRectFilled(ImVec2(Min.x - Grow, Min.y - Grow), ImVec2(Max.x + Grow, Max.y + Grow), Col(Color, 0.05f * Alpha), Rounding + Grow);
		}
	}
	pDraw->AddRectFilled(Min, Max, Col(ImVec4(t.m_Panel.x, t.m_Panel.y, t.m_Panel.z, 0.92f), Alpha), Rounding);
	pDraw->AddRect(Min, Max, Col(Color, 0.8f * Alpha), Rounding, 0, 2.0f * U);
	// a light sweep across records
	if(m_BannerKind > 0)
	{
		const float Sweep = std::fmod(Age, 1.6f) / 1.6f;
		const float X = Min.x + (Width + 120.0f * U) * Sweep - 60.0f * U;
		pDraw->PushClipRect(Min, Max, true);
		const ImU32 Clear = Col(ImVec4(1, 1, 1, 1), 0.0f);
		const ImU32 Light = Col(ImVec4(1, 1, 1, 1), 0.10f * Alpha);
		pDraw->AddRectFilledMultiColor(ImVec2(X - 60.0f * U, Min.y), ImVec2(X, Max.y), Clear, Light, Light, Clear);
		pDraw->AddRectFilledMultiColor(ImVec2(X, Min.y), ImVec2(X + 60.0f * U, Max.y), Light, Clear, Clear, Light);
		pDraw->PopClipRect();
	}
	pDraw->AddText(pTitleFont, TitleSize, ImVec2(CenterX - TitleDim.x * 0.5f, Min.y + 16.0f * U), Col(m_BannerKind ? Color : t.m_Text, Alpha), m_aBannerTitle);
	pDraw->AddText(pSubFont, SubSize, ImVec2(CenterX - SubDim.x * 0.5f, Min.y + 16.0f * U + TitleSize + 8.0f * U), Col(t.m_TextSecondary, Alpha), m_aBannerSubtitle);
}

// cuts the text with "…" so it fits into MaxWidth
void XcFitText(ImFont *pFont, float Size, const char *pText, float MaxWidth, char *pBuf, int BufSize)
{
	str_copy(pBuf, pText, BufSize);
	if(pFont->CalcTextSizeA(Size, FLT_MAX, 0.0f, pBuf).x <= MaxWidth)
		return;
	int Length = str_length(pBuf);
	while(Length > 0)
	{
		// step back one whole UTF-8 character
		Length = str_utf8_rewind(pBuf, Length);
		char aTry[256];
		str_format(aTry, sizeof(aTry), "%.*s…", Length, pBuf);
		if(pFont->CalcTextSizeA(Size, FLT_MAX, 0.0f, aTry).x <= MaxWidth)
		{
			str_copy(pBuf, aTry, BufSize);
			return;
		}
	}
	pBuf[0] = '\0';
}

void CXcHud::RenderMusic(ImDrawList *pDraw, float Alpha)
{
	const STheme &t = Theme();
	const CXcMusic &Music = GameClient()->m_XcMusic;
	const bool HasTrack = Music.HasTrack();
	// shows while something plays, fades out a few seconds after a pause; always shown in the editor
	const bool Wanted = m_Editing || (HasTrack && (!g_Config.m_XcHudMusicAutohide || Music.PausedFor() < 6.0f));
	const float Dt = std::min(Client()->RenderFrameTime(), 0.1f);
	m_MusicAlpha = Wanted ? std::min(m_MusicAlpha + Dt * 4.0f, 1.0f) : std::max(m_MusicAlpha - Dt * 2.0f, 0.0f);
	if(m_MusicAlpha <= 0.0f)
	{
		m_aaRects[ELEMENT_MUSIC][0] = m_aaRects[ELEMENT_MUSIC][2] = -1.0f;
		return;
	}
	Alpha *= m_Editing ? 1.0f : m_MusicAlpha;
	const SXcMediaState &State = Music.State();
	const float U = Unit(ELEMENT_MUSIC);
	ImFont *pLabel = m_Backend.Fonts().m_pLabel;
	ImFont *pSmall = m_Backend.Fonts().m_pSmall;
	const float Width = 300.0f * U;
	const float Height = 66.0f * U;
	// slides up a little while fading in
	const ImVec2 Placed = PlaceRect(ELEMENT_MUSIC, ImVec2(Width, Height));
	const ImVec2 Min(Placed.x, Placed.y + (1.0f - m_MusicAlpha) * 10.0f * U * (m_Editing ? 0.0f : 1.0f));
	const ImVec2 Max(Min.x + Width, Min.y + Height);
	RenderPlate(pDraw, Min, Max, U, Alpha);

	// cover, or an accent square with a note
	const float Pad = 10.0f * U;
	const float ArtSize = Height - Pad * 2.0f;
	const ImVec2 ArtMin(Min.x + Pad, Min.y + Pad);
	const ImVec2 ArtMax(ArtMin.x + ArtSize, ArtMin.y + ArtSize);
	const float ArtRounding = 9.0f * U;
	const IGraphics::CTextureHandle Art = Music.Art();
	if(HasTrack && Art.IsValid() && !Art.IsNullTexture())
		pDraw->AddImageRounded((ImTextureID)(uintptr_t)m_Backend.TextureId(Art), ArtMin, ArtMax, ImVec2(0, 0), ImVec2(1, 1), Col(ImVec4(1, 1, 1, 1), Alpha), ArtRounding);
	else
	{
		const ImVec4 Dark = Mix(t.m_Accent, ImVec4(0, 0, 0, 1), 0.45f);
		pDraw->AddRectFilledMultiColor(ArtMin, ArtMax, Col(t.m_Accent, Alpha), Col(t.m_Accent, Alpha), Col(Dark, Alpha), Col(Dark, Alpha));
		pDraw->AddRect(ArtMin, ArtMax, Col(t.m_Panel, Alpha), ArtRounding, 0, 3.0f * U); // rounds the gradient corners off
		const float IconSize = 18.0f * U;
		DrawIcon(pDraw, pLabel, IconSize, ImVec2((ArtMin.x + ArtMax.x) * 0.5f, (ArtMin.y + ArtMax.y) * 0.5f), Col(t.m_OnAccent, Alpha), XC_ICON_MUSIC);
	}

	// equalizer bars on the right while playing, a pause sign otherwise
	const float BarsWidth = 22.0f * U;
	const float BarsRight = Max.x - 14.0f * U;
	const float TextX = ArtMax.x + 12.0f * U;
	const float TextRight = BarsRight - BarsWidth - 10.0f * U;
	const float Time = Client()->GlobalTime();
	const bool Playing = HasTrack && State.m_Playing;
	const float MidY = Min.y + Height * 0.5f - 4.0f * U;
	for(int i = 0; i < 4; i++)
	{
		const float Wave = Playing ? 0.5f + 0.5f * std::sin(Time * (6.0f + i * 1.7f) + i * 1.3f) * std::sin(Time * (2.3f + i * 0.9f)) : 0.0f;
		const float BarHeight = (4.0f + 14.0f * std::abs(Wave)) * U;
		const float x = BarsRight - BarsWidth + i * 6.0f * U;
		pDraw->AddRectFilled(ImVec2(x, MidY + 9.0f * U - BarHeight), ImVec2(x + 3.5f * U, MidY + 9.0f * U), Col(Playing ? t.m_Accent : t.m_TextMuted, Alpha), 2.0f * U);
	}

	// title scrolls when it is too long, the artist is cut
	const char *pTitle = HasTrack ? State.m_Title.c_str() : Tr("Nothing playing");
	const char *pArtist = HasTrack ? (State.m_Artist.empty() ? Music.AppName() : State.m_Artist.c_str()) : (Music.Supported() ? Tr("Play music in the browser") : Tr("Not supported on this system"));
	const float TitleSize = 14.5f * U;
	const float ArtistSize = 12.0f * U;
	const float Room = TextRight - TextX;
	const float TitleWidth = pLabel->CalcTextSizeA(TitleSize, FLT_MAX, 0.0f, pTitle).x;
	float Shift = 0.0f;
	if(TitleWidth > Room)
	{
		// there and back with a rest at both ends
		const float Travel = TitleWidth - Room;
		const float Period = 3.0f + Travel / (30.0f * U);
		const float Phase = std::fmod(Time, Period * 2.0f) / Period;
		const float Move = Phase < 1.0f ? Phase : 2.0f - Phase;
		Shift = Travel * std::clamp((Move - 0.15f) / 0.7f, 0.0f, 1.0f);
	}
	const float TitleY = Min.y + 13.0f * U;
	pDraw->PushClipRect(ImVec2(TextX, Min.y), ImVec2(TextRight, Max.y), true);
	pDraw->AddText(pLabel, TitleSize, ImVec2(TextX - Shift, TitleY), Col(t.m_Text, Alpha), pTitle);
	pDraw->PopClipRect();
	char aArtist[256];
	XcFitText(pSmall, ArtistSize, pArtist, Room, aArtist, sizeof(aArtist));
	pDraw->AddText(pSmall, ArtistSize, ImVec2(TextX, TitleY + TitleSize + 4.0f * U), Col(t.m_TextSecondary, Alpha), aArtist);

	// progress line under the text
	const double Position = Music.Position();
	if(HasTrack && Position >= 0.0)
	{
		const float LineY = Max.y - 11.0f * U;
		const float Fill = (float)(Position / State.m_Duration);
		pDraw->AddRectFilled(ImVec2(TextX, LineY), ImVec2(BarsRight, LineY + 3.0f * U), Col(t.m_Track, Alpha), 2.0f * U);
		pDraw->AddRectFilled(ImVec2(TextX, LineY), ImVec2(TextX + (BarsRight - TextX) * Fill, LineY + 3.0f * U), Col(t.m_Accent, Alpha), 2.0f * U);
	}
}

void CXcHud::RenderWheelBackground(vec2 Center, float InnerRadius, float OuterRadius, int NumItems, int Selected, float Alpha) const
{
	const STheme &t = Theme();
	IGraphics *pGraphics = Graphics();
	const int Segments = 96;
	const auto Ring = [&](float R0, float R1, float A0, float A1, ColorRGBA Color) {
		// a ring piece from angle A0 to A1 as a strip of quads
		const int Steps = std::max(2, (int)(Segments * (A1 - A0) / (2.0f * pi)));
		pGraphics->TextureClear();
		pGraphics->QuadsBegin();
		pGraphics->SetColor(Color);
		for(int i = 0; i < Steps; i++)
		{
			const float a = A0 + (A1 - A0) * i / Steps;
			const float b = A0 + (A1 - A0) * (i + 1) / Steps;
			const vec2 Da = direction(a);
			const vec2 Db = direction(b);
			const IGraphics::CFreeformItem Item(Center + Da * R0, Center + Db * R0, Center + Da * R1, Center + Db * R1);
			pGraphics->QuadsDrawFreeform(&Item, 1);
		}
		pGraphics->QuadsEnd();
	};
	const ColorRGBA Panel(t.m_Panel.x, t.m_Panel.y, t.m_Panel.z, 0.88f * Alpha);
	const ColorRGBA Accent(t.m_Accent.x, t.m_Accent.y, t.m_Accent.z, Alpha);
	Ring(InnerRadius, OuterRadius, 0.0f, 2.0f * pi, Panel);
	// thin rim lines
	Ring(OuterRadius - 1.5f, OuterRadius, 0.0f, 2.0f * pi, ColorRGBA(1.0f, 1.0f, 1.0f, 0.10f * Alpha));
	Ring(InnerRadius, InnerRadius + 1.5f, 0.0f, 2.0f * pi, ColorRGBA(1.0f, 1.0f, 1.0f, 0.08f * Alpha));
	if(Selected >= 0 && NumItems > 0)
	{
		// the selected sector glows in the accent color, with a bright edge outside
		const float Half = pi / NumItems;
		const float Mid = 2.0f * pi * Selected / NumItems;
		Ring(InnerRadius, OuterRadius, Mid - Half, Mid + Half, Accent.WithAlpha(0.22f * Alpha));
		Ring(OuterRadius - 4.0f, OuterRadius, Mid - Half, Mid + Half, Accent);
	}
}

ColorRGBA CXcHud::WheelPanelColor() const
{
	const STheme &t = Theme();
	return ColorRGBA(t.m_Panel.x, t.m_Panel.y, t.m_Panel.z, 0.88f);
}

ColorRGBA CXcHud::ScoreboardPanelColor() const
{
	const STheme &t = Theme();
	return ColorRGBA(t.m_Panel.x, t.m_Panel.y, t.m_Panel.z, 0.86f);
}

ColorRGBA CXcHud::ScoreboardHighlightColor() const
{
	const STheme &t = Theme();
	return ColorRGBA(t.m_Accent.x, t.m_Accent.y, t.m_Accent.z, 0.32f);
}

ColorRGBA CXcHud::ScoreboardHoverColor() const
{
	const STheme &t = Theme();
	return ColorRGBA(t.m_Accent.x, t.m_Accent.y, t.m_Accent.z, 0.55f);
}

ColorRGBA CXcHud::ChatLineColor() const
{
	const STheme &t = Theme();
	return ColorRGBA(t.m_Panel.x, t.m_Panel.y, t.m_Panel.z, g_Config.m_XcChatOpacity / 100.0f);
}

ColorRGBA CXcHud::ChatMarkColor(int Kind) const
{
	const STheme &t = Theme();
	switch(Kind)
	{
	case 2: return ColorRGBA(0.24f, 0.84f, 0.64f, 1.0f); // team: mint
	case 3: return ColorRGBA(1.0f, 0.36f, 0.54f, 1.0f); // whisper: pink
	case 4: return ColorRGBA(0.55f, 0.57f, 0.61f, 1.0f); // server: muted
	default: return ColorRGBA(t.m_Accent.x, t.m_Accent.y, t.m_Accent.z, 1.0f);
	}
}

ColorRGBA CXcHud::WheelSelectColor() const
{
	const STheme &t = Theme();
	return ColorRGBA(t.m_Accent.x, t.m_Accent.y, t.m_Accent.z, 0.35f);
}

void CXcHud::OnRender()
{
	if(!m_Backend.IsReady())
		return;
	const bool Online = Client()->State() == IClient::STATE_ONLINE || Client()->State() == IClient::STATE_DEMOPLAYBACK;
	// the own scoreboard shows even with the HUD turned off, like the classic one
	const bool Scoreboard = Online && (m_ScoreboardOpen > 0.0f || (GameClient()->m_Scoreboard.IsActive() && DrawsScoreboard()));
	const bool HudVisible = Visible();
	// the new chat shows even with the HUD turned off, like the classic one (cl_showchat decides)
	const bool ChatVisible = DrawsChat() && (g_Config.m_ClShowChat || GameClient()->m_Chat.IsActive()) && !m_Editing && (!g_Config.m_XcFocusMode || GameClient()->m_Chat.IsActive());
	// XClient lyrics: the song words on the map and the plate, in the HUD fonts
	const bool Lyrics = Online && GameClient()->m_XcLyrics.WantsHud();
	if(!HudVisible && !Scoreboard && !ChatVisible && !Lyrics)
		return;
	const float Scale = std::round(Graphics()->ScreenHeight() / 1080.0f * FONT_OVERSAMPLE * 20.0f) / 20.0f;
	if(m_Backend.FontFamily() != g_Config.m_XcUiFont || m_Backend.FontScale() != Scale)
	{
		m_Backend.LoadFonts(g_Config.m_XcUiFont, Scale);
		m_Backend.MakeCurrent();
		ApplyStyle();
	}
	SetTheme(g_Config.m_XcUiTheme, g_Config.m_XcUiAccent);

	const float Dt = std::min(Client()->RenderFrameTime(), 0.1f);
	m_Backend.NewFrame(Dt, Ui()->UpdatedMousePos());
	m_vChatBlurRects.clear(); // the glass panels of this frame add their rects
	ImDrawList *pDraw = ImGui::GetBackgroundDrawList();
	// the HUD steps back while the menu is open
	const float FadeSpeed = AnimationSpeed() > 0.0f ? 8.0f * AnimationSpeed() : 1000.0f;
	const auto Approach = [&](float &Value, float Target) {
		Value = Value < Target ? std::min(Value + Dt * FadeSpeed, Target) : std::max(Value - Dt * FadeSpeed, Target);
	};
	Approach(m_MenuFade, GameClient()->m_XcMenu.IsActive() || GameClient()->m_XcMenu.DemoBarShown() ? 0.0f : 1.0f);
	const float Alpha = m_MenuFade * m_MenuFade * (3.0f - 2.0f * m_MenuFade);
	// focus mode keeps only the timer and the speed (the editor shows everything)
	const bool Focus = g_Config.m_XcFocusMode && !m_Editing;
	const bool aWanted[NUM_ELEMENTS] = {
		g_Config.m_XcHudWatermark && !Focus,
		(bool)g_Config.m_XcHudTimer,
		(bool)g_Config.m_XcHudSpeed,
		g_Config.m_XcHudWeapon && !Focus,
		g_Config.m_XcHudMusic && !Focus,
		g_Config.m_XcHudKeys && !Focus,
		g_Config.m_XcHudMinitop && !Focus && (m_Editing || m_ScoreboardOpen < 1.0f),
		g_Config.m_XcHudEffects && !Focus,
		(bool)g_Config.m_XcHudLatency,
		(bool)g_Config.m_XcHudIsland,
	};
	for(int i = 0; i < NUM_ELEMENTS; i++)
		Approach(m_aShown[i], aWanted[i] ? 1.0f : 0.0f);
	if(Lyrics && !m_Editing)
		GameClient()->m_XcLyrics.RenderHud(pDraw, m_Backend.Fonts().m_pHeader, m_Backend.Fonts().m_pLabel, Alpha);
	if(HudVisible && (Alpha > 0.0f || m_Editing))
	{
		const float a = m_Editing ? 1.0f : Alpha;
		if(m_Editing)
			pDraw->AddRectFilled(ImVec2(0, 0), ImGui::GetIO().DisplaySize, Col(ImVec4(0, 0, 0, 1), 0.25f));
		const auto Shown = [&](int Element) { return m_Editing ? (aWanted[Element] ? 1.0f : 0.0f) : m_aShown[Element]; };
		UpdateEditorMotion(Dt);
		// each element is drawn, then turned and scaled a little while it is held in the editor
		const auto Draw = [&](int Element, auto &&Fn) {
			if(Shown(Element) <= 0.0f)
				return;
			const int VtxStart = pDraw->VtxBuffer.Size;
			Fn(a * Shown(Element));
			TransformElement(pDraw, VtxStart, Element);
		};
		Draw(ELEMENT_WATERMARK, [&](float A) { RenderWatermark(pDraw, A); });
		Draw(ELEMENT_TIMER, [&](float A) { RenderTimer(pDraw, A); });
		Draw(ELEMENT_SPEED, [&](float A) { RenderSpeed(pDraw, A); });
		Draw(ELEMENT_WEAPON, [&](float A) { RenderWeapon(pDraw, A); });
		Draw(ELEMENT_MUSIC, [&](float A) { RenderMusic(pDraw, A); });
		Draw(ELEMENT_KEYS, [&](float A) { RenderKeys(pDraw, A); });
		// the top list steps aside while the full scoreboard is open
		Draw(ELEMENT_MINITOP, [&](float A) { RenderMiniTop(pDraw, A * (m_Editing ? 1.0f : 1.0f - m_ScoreboardOpen)); });
		Draw(ELEMENT_LATENCY, [&](float A) { RenderLatency(pDraw, A); });
		Draw(ELEMENT_EFFECTS, [&](float A) { RenderEffects(pDraw, A, Dt); });
		// the island does not step back for the menu: over an open menu it is drawn after it (RenderIslandOverMenu)
		if(m_Editing || !GameClient()->m_XcMenu.IsActive())
			Draw(ELEMENT_ISLAND, [&](float A) { RenderIsland(pDraw, m_Editing ? A : Shown(ELEMENT_ISLAND), Dt); });
		if(!m_Editing)
			RenderBanner(ImGui::GetForegroundDrawList());
		if(m_Editing)
			RenderEditor(ImGui::GetForegroundDrawList());
		else if(g_Config.m_XcHudToasts && HasToasts())
			RenderToasts(ImGui::GetForegroundDrawList(), ImGui::GetIO().DisplaySize, Client()->GlobalTime(), a, m_Backend.Fonts().m_pLabel, m_Backend.Fonts().m_pSmall);
	}
	if(ChatVisible)
		RenderChat(ImGui::GetBackgroundDrawList(), Dt, Alpha);
	if(Online)
		RenderScoreboard(ImGui::GetForegroundDrawList(), Dt);
	BlurChat();
	m_Backend.Render();
	// XClient lyrics: the neon words and line go through the render module, after the HUD frame
	if(Lyrics && !m_Editing)
		GameClient()->m_XcLyrics.RenderNeon(m_Backend, m_Backend.Fonts().m_pHeader, Alpha);

	if(m_Editing || ScoreboardMouse())
	{
		const float Width = (float)Graphics()->ScreenWidth();
		const float Height = (float)Graphics()->ScreenHeight();
		const vec2 Mouse = Ui()->UpdatedMousePos() * vec2(Width / std::max((float)Graphics()->WindowWidth(), 1.0f), Height / std::max((float)Graphics()->WindowHeight(), 1.0f));
		Graphics()->MapScreen(CScreenRect(0.0f, 0.0f, Width, Height));
		RenderTools()->RenderCursor(Mouse, 24.0f * Height / 1080.0f);
	}
}

void CXcHud::CloseMenuIsland()
{
	m_IslandMenuPressed = false;
	if(m_IslandPlayerMenuPinned)
	{
		m_IslandPlayerMenuPinned = false;
		m_IslandPlayerUntil = -1.0f;
	}
}

bool CXcHud::OnIslandMenuInput(const IInput::CEvent &Event)
{
	if(Event.m_Key != KEY_MOUSE_1 || !(Event.m_Flags & IInput::FLAG_PRESS) ||
		Client()->State() != IClient::STATE_ONLINE || !m_Backend.IsReady() || m_Editing ||
		!g_Config.m_XcHud || !g_Config.m_XcHudIsland || !g_Config.m_ClShowhud || m_aShown[ELEMENT_ISLAND] <= 0.5f)
		return false;

	const vec2 Mouse = Ui()->UpdatedMousePos() * vec2(
		Graphics()->ScreenWidth() / std::max((float)Graphics()->WindowWidth(), 1.0f),
		Graphics()->ScreenHeight() / std::max((float)Graphics()->WindowHeight(), 1.0f));
	const auto &aRect = m_aaRects[ELEMENT_ISLAND];
	if(Mouse.x < aRect[0] || Mouse.y < aRect[1] || Mouse.x >= aRect[2] || Mouse.y >= aRect[3])
		return false;

	m_IslandMenuPressed = true;
	return true;
}

void CXcHud::RenderIslandOverMenu(float Open)
{
	if(!m_Backend.IsReady() || m_Editing || !g_Config.m_XcHud || !g_Config.m_XcHudIsland || !g_Config.m_ClShowhud || !GameClient()->m_XcMenu.IsActive())
		return;
	(void)Open;
	m_Backend.MakeCurrent();
	const float Dt = std::min(Client()->RenderFrameTime(), 0.1f);
	// a second small frame of the HUD context: only the island (its time already moved this frame)
	m_Backend.NewFrame(0.0001f, Ui()->UpdatedMousePos());
	m_vChatBlurRects.clear();
	RenderIsland(ImGui::GetForegroundDrawList(), m_aShown[ELEMENT_ISLAND], Dt, true);
	m_vChatBlurRects.clear(); // no blur over the menu
	m_Backend.Render();
}

bool CXcHud::OnCursorMove(float x, float y, IInput::ECursorType CursorType)
{
	if(!m_Editing && !ScoreboardMouse())
		return false;
	Ui()->ConvertMouseMove(&x, &y, CursorType);
	Ui()->OnCursorMove(x, y);
	m_Backend.OnMouseMove(Ui()->UpdatedMousePos());
	return true;
}

bool CXcHud::OnInput(const IInput::CEvent &Event)
{
	if(!m_Editing && ScoreboardMouse())
	{
		// the scoreboard with the mouse: clicks go to its rows and popup, the rest (Esc, the unlock key)
		// stays with the scoreboard
		if(Event.m_Key == KEY_MOUSE_1 || Event.m_Key == KEY_MOUSE_2)
		{
			m_Backend.MakeCurrent();
			m_Backend.OnInput(Event);
			return true;
		}
		if(Event.m_Key == KEY_ESCAPE && (Event.m_Flags & IInput::FLAG_PRESS) && m_ScoreboardPopupId >= 0)
		{
			m_ScoreboardPopupId = -1;
			return true;
		}
		return false;
	}
	if(!m_Editing)
		return false;
	if((Event.m_Flags & IInput::FLAG_PRESS) && (Event.m_Key == KEY_ESCAPE || Event.m_Key == KEY_RETURN))
	{
		SetEditing(false);
		return true;
	}
	// the game gets nothing while the editor is open
	m_Backend.OnInput(Event);
	return true;
}

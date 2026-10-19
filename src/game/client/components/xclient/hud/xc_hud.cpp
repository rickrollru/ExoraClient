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
#include <game/client/gameclient.h>

#include <algorithm>
#include <cmath>
#include <ctime>

using namespace XcUi;

// design sizes are for 1080p at 100 %, the fonts are rasterized bigger so scaling up stays sharp
static constexpr float FONT_OVERSAMPLE = 1.5f;
static constexpr float SNAP_DISTANCE = 10.0f;

void CXcHud::OnConsoleInit()
{
	Console()->Register("xc_hud_edit", "", CFGFLAG_CLIENT, ConEditHud, this, "Open or close the HUD editor");
	Console()->Register("xc_focus", "", CFGFLAG_CLIENT, ConFocus, this, "Turn the focus mode on or off");
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

void CXcHud::OnInit()
{
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
	case ELEMENT_TIMER: return {"Timer", &g_Config.m_XcHudTimer, &g_Config.m_XcHudTimerX, &g_Config.m_XcHudTimerY, &g_Config.m_XcHudTimerScale, 500, 38};
	case ELEMENT_SPEED: return {"Speed", &g_Config.m_XcHudSpeed, &g_Config.m_XcHudSpeedX, &g_Config.m_XcHudSpeedY, &g_Config.m_XcHudSpeedScale, 455, 905};
	case ELEMENT_WEAPON: return {"Weapon", &g_Config.m_XcHudWeapon, &g_Config.m_XcHudWeaponX, &g_Config.m_XcHudWeaponY, &g_Config.m_XcHudWeaponScale, 565, 905};
	case ELEMENT_MUSIC: return {"Music", &g_Config.m_XcHudMusic, &g_Config.m_XcHudMusicX, &g_Config.m_XcHudMusicY, &g_Config.m_XcHudMusicScale, 880, 60};
	case ELEMENT_KEYS: return {"Keystrokes", &g_Config.m_XcHudKeys, &g_Config.m_XcHudKeysX, &g_Config.m_XcHudKeysY, &g_Config.m_XcHudKeysScale, 120, 640};
	default: return {"Top of the server", &g_Config.m_XcHudMinitop, &g_Config.m_XcHudMinitopX, &g_Config.m_XcHudMinitopY, &g_Config.m_XcHudMinitopScale, 920, 830};
	}
}

bool CXcHud::Visible() const
{
	if(Client()->State() != IClient::STATE_ONLINE && Client()->State() != IClient::STATE_DEMOPLAYBACK)
		return false;
	return m_Editing || (g_Config.m_XcHud && g_Config.m_ClShowhud);
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

void CXcHud::RenderPlate(ImDrawList *pDraw, ImVec2 Min, ImVec2 Max, float Unit, float Alpha) const
{
	const STheme &t = Theme();
	const float Rounding = 12.0f * Unit;
	// soft shadow in a few layers
	for(int i = 3; i >= 1; i--)
	{
		const float Grow = i * 2.5f * Unit;
		pDraw->AddRectFilled(ImVec2(Min.x - Grow, Min.y - Grow + 3.0f * Unit), ImVec2(Max.x + Grow, Max.y + Grow + 3.0f * Unit), Col(ImVec4(0, 0, 0, 1), 0.07f * Alpha), Rounding + Grow);
	}
	pDraw->AddRectFilled(Min, Max, Col(ImVec4(t.m_Panel.x, t.m_Panel.y, t.m_Panel.z, 0.86f), Alpha), Rounding);
	pDraw->AddRect(Min, Max, Col(t.m_Border, Alpha), Rounding, 0, 1.0f);
}

void CXcHud::RenderWatermark(ImDrawList *pDraw, float Alpha)
{
	const STheme &t = Theme();
	const float U = Unit(ELEMENT_WATERMARK);
	ImFont *pBold = m_Backend.Fonts().m_pTitle;
	ImFont *pLabel = m_Backend.Fonts().m_pLabel;
	const float TitleSize = 17.0f * U;
	const float TextSize = 14.5f * U;

	// the parts after the logo, each with an icon
	char aFps[32];
	char aPing[32];
	char aTime[16];
	str_format(aFps, sizeof(aFps), "%d fps", (int)std::round(1.0f / std::max(Client()->FrameTimeAverage(), 0.0001f)));
	const int Local = GameClient()->m_Snap.m_LocalClientId;
	const CNetObj_PlayerInfo *pInfo = Local >= 0 ? GameClient()->m_Snap.m_apPlayerInfos[Local] : nullptr;
	if(Client()->State() == IClient::STATE_DEMOPLAYBACK)
		str_copy(aPing, "demo");
	else
		str_format(aPing, sizeof(aPing), "%d ms", pInfo ? pInfo->m_Latency : 0);
	const time_t Now = std::time(nullptr);
	const struct tm *pTm = localtime(&Now);
	str_format(aTime, sizeof(aTime), "%02d:%02d", pTm ? pTm->tm_hour : 0, pTm ? pTm->tm_min : 0);
	const char *apIcons[] = {XC_ICON_USER, XC_ICON_BOLT, XC_ICON_SIGNAL, XC_ICON_CLOCK};
	const char *apTexts[] = {Client()->PlayerName(), aFps, aPing, aTime};

	const float Pad = 14.0f * U;
	const float Gap = 10.0f * U;
	const float IconGap = 7.0f * U;
	const float Height = 42.0f * U;
	const float LogoSize = 24.0f * U;
	const char *pName = "Exora Client";
	float Width = Pad + LogoSize + 8.0f * U + pBold->CalcTextSizeA(TitleSize, FLT_MAX, 0.0f, pName).x;
	for(int i = 0; i < 4; i++)
		Width += Gap * 2.0f + 1.0f + pLabel->CalcTextSizeA(TextSize, FLT_MAX, 0.0f, apIcons[i]).x + IconGap + pLabel->CalcTextSizeA(TextSize, FLT_MAX, 0.0f, apTexts[i]).x;
	Width += Pad;

	const ImVec2 Min = PlaceRect(ELEMENT_WATERMARK, ImVec2(Width, Height));
	const ImVec2 Max(Min.x + Width, Min.y + Height);
	RenderPlate(pDraw, Min, Max, U, Alpha);

	// logo, then the name
	float x = Min.x + Pad;
	const float MidY = Min.y + Height * 0.5f;
	DrawLogo(pDraw, ImVec2(x + LogoSize * 0.5f, MidY), LogoSize, t.m_Accent, Alpha);
	x += LogoSize + 8.0f * U;
	pDraw->AddText(pBold, TitleSize, ImVec2(x, MidY - TitleSize * 0.55f), Col(t.m_Text, Alpha), pName);
	x += pBold->CalcTextSizeA(TitleSize, FLT_MAX, 0.0f, pName).x;

	for(int i = 0; i < 4; i++)
	{
		x += Gap;
		pDraw->AddLine(ImVec2(x, MidY - 8.0f * U), ImVec2(x, MidY + 8.0f * U), Col(t.m_Border, 2.0f * Alpha), 1.0f);
		x += 1.0f + Gap;
		pDraw->AddText(pLabel, TextSize, ImVec2(x, MidY - TextSize * 0.55f), Col(t.m_Accent, Alpha), apIcons[i]);
		x += pLabel->CalcTextSizeA(TextSize, FLT_MAX, 0.0f, apIcons[i]).x + IconGap;
		pDraw->AddText(pLabel, TextSize, ImVec2(x, MidY - TextSize * 0.55f), Col(i == 0 ? t.m_Text : t.m_TextSecondary, Alpha), apTexts[i]);
		x += pLabel->CalcTextSizeA(TextSize, FLT_MAX, 0.0f, apTexts[i]).x;
	}
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

	const float TimeSize = 24.0f * U;
	// the width is taken from zeros, so the plate does not wobble while the digits change
	char aWidth[32];
	str_copy(aWidth, aTime);
	for(char *p = aWidth; *p; p++)
		if(*p >= '0' && *p <= '9')
			*p = '0';
	const float TextWidth = pBold->CalcTextSizeA(TimeSize, FLT_MAX, 0.0f, aWidth).x;
	const float IconSize = 14.0f * U;
	const float Width = 18.0f * U + IconSize + 10.0f * U + TextWidth + 20.0f * U;
	const float Height = 44.0f * U;
	const ImVec2 Min = PlaceRect(ELEMENT_TIMER, ImVec2(Width, Height));
	const ImVec2 Max(Min.x + Width, Min.y + Height);
	RenderPlate(pDraw, Min, Max, U, Alpha);

	const float MidY = Min.y + Height * 0.5f;
	const ImVec4 IconColor = Racing ? t.m_Accent : t.m_TextMuted;
	pDraw->AddText(pSmall, IconSize, ImVec2(Min.x + 18.0f * U, MidY - IconSize * 0.55f), Col(IconColor, Alpha), XC_ICON_CLOCK);
	pDraw->AddText(pBold, TimeSize, ImVec2(Min.x + 18.0f * U + IconSize + 10.0f * U, MidY - TimeSize * 0.56f), Col(t.m_Text, Alpha), aTime);
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

	const float Width = 168.0f * U;
	const float Height = 54.0f * U;
	const ImVec2 Min = PlaceRect(ELEMENT_SPEED, ImVec2(Width, Height));
	const ImVec2 Max(Min.x + Width, Min.y + Height);
	RenderPlate(pDraw, Min, Max, U, Alpha);

	const float Pad = 14.0f * U;
	const float ValueSize = 20.0f * U;
	const float SmallSize = 12.0f * U;
	pDraw->AddText(pSmall, SmallSize, ImVec2(Min.x + Pad, Min.y + 10.0f * U), Col(t.m_Accent, Alpha), XC_ICON_GAUGE);
	pDraw->AddText(pBold, ValueSize, ImVec2(Min.x + Pad + 20.0f * U, Min.y + 6.0f * U), Col(t.m_Text, Alpha), aSpeed);
	const float ValueWidth = pBold->CalcTextSizeA(ValueSize, FLT_MAX, 0.0f, aSpeed).x;
	pDraw->AddText(pSmall, SmallSize, ImVec2(Min.x + Pad + 24.0f * U + ValueWidth, Min.y + 13.0f * U), Col(t.m_TextMuted, Alpha), Tr("blocks/s"));

	// bar: full at 40 blocks per second (fast hook and jump speed)
	const float BarY = Max.y - 14.0f * U;
	const float BarHeight = 5.0f * U;
	const ImVec2 BarMin(Min.x + Pad, BarY - BarHeight);
	const ImVec2 BarMax(Max.x - Pad, BarY);
	pDraw->AddRectFilled(BarMin, BarMax, Col(t.m_Track, Alpha), BarHeight);
	const float Fill = std::clamp(Speed / 40.0f, 0.0f, 1.0f);
	if(Fill > 0.0f)
	{
		const ImVec2 FillMax(BarMin.x + std::max((BarMax.x - BarMin.x) * Fill, BarHeight), BarMax.y);
		pDraw->AddRectFilled(BarMin, FillMax, Col(t.m_Accent, Alpha), BarHeight);
	}
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
	const float Width = 14.0f * U + IconWidth + 12.0f * U + std::max(NameWidth, DotsWidth) + 16.0f * U;
	const float Height = 48.0f * U;
	const ImVec2 Min = PlaceRect(ELEMENT_WEAPON, ImVec2(Width, Height));
	const ImVec2 Max(Min.x + Width, Min.y + Height);
	RenderPlate(pDraw, Min, Max, U, Alpha);

	const IGraphics::CTextureHandle Texture = GameClient()->m_GameSkin.m_aSpriteWeapons[Weapon];
	const float IconX = Min.x + 14.0f * U;
	const float IconY = Min.y + (Height - IconWidth / Aspect) * 0.5f;
	if(Texture.IsValid())
		pDraw->AddImage((ImTextureID)(uintptr_t)m_Backend.TextureId(Texture), ImVec2(IconX, IconY), ImVec2(IconX + IconWidth, IconY + IconWidth / Aspect), ImVec2(0, 0), ImVec2(1, 1), Col(ImVec4(1, 1, 1, 1), Alpha));
	const float TextX = IconX + IconWidth + 12.0f * U;
	pDraw->AddText(pLabel, TextSize, ImVec2(TextX, Min.y + (Dots ? 9.0f : 17.0f) * U), Col(t.m_Text, Alpha), pName);
	for(int i = 0; i < Dots; i++)
		pDraw->AddCircleFilled(ImVec2(TextX + 3.0f * U + i * 8.0f * U, Max.y - 14.0f * U), 2.6f * U, Col(t.m_Accent, Alpha), 12);
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

void CXcHud::ShowBanner(const char *pTitle, const char *pSubtitle, int Kind)
{
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
	// soft colored glow behind, the plate, a colored rim
	for(int i = 4; i >= 1; i--)
	{
		const float Grow = i * 7.0f * U;
		pDraw->AddRectFilled(ImVec2(Min.x - Grow, Min.y - Grow), ImVec2(Max.x + Grow, Max.y + Grow), Col(Color, 0.05f * Alpha), Rounding + Grow);
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
		const ImVec2 IconDim = pLabel->CalcTextSizeA(IconSize, FLT_MAX, 0.0f, XC_ICON_MUSIC);
		pDraw->AddText(pLabel, IconSize, ImVec2((ArtMin.x + ArtMax.x - IconDim.x) * 0.5f, (ArtMin.y + ArtMax.y - IconDim.y) * 0.5f), Col(t.m_OnAccent, Alpha), XC_ICON_MUSIC);
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

void CXcHud::RenderEditor(ImDrawList *pDraw)
{
	const STheme &t = Theme();
	ImGuiIO &Io = ImGui::GetIO();
	const ImVec2 Screen = Io.DisplaySize;
	ImFont *pLabel = m_Backend.Fonts().m_pLabel;
	ImFont *pSmall = m_Backend.Fonts().m_pSmall;
	const float U = Graphics()->ScreenHeight() / 1080.0f;

	// the panel on the right is drawn first in the list, so clicks on it do not move elements
	const ImVec2 PanelSize(300.0f * U, 0.0f);
	ImGui::SetNextWindowPos(ImVec2(Screen.x - PanelSize.x - 24.0f * U, 90.0f * U), ImGuiCond_Always);
	ImGui::SetNextWindowSize(ImVec2(PanelSize.x, 0.0f));
	ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(t.m_Panel.x, t.m_Panel.y, t.m_Panel.z, 0.95f));
	ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 16.0f * U);
	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(18.0f * U, 16.0f * U));
	ImGui::Begin("##hud_editor_panel", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_AlwaysAutoResize);
	const bool OverPanel = ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows);
	ImGui::PushFont(m_Backend.Fonts().m_pTitle);
	ImGui::TextColored(t.m_Text, "%s", Tr("HUD editor"));
	ImGui::PopFont();
	ImGui::PushFont(pSmall);
	ImGui::PushTextWrapPos(0.0f);
	ImGui::TextColored(t.m_TextMuted, "%s", Tr("Drag the elements with the mouse, the wheel changes the size. Esc closes the editor."));
	ImGui::PopTextWrapPos();
	ImGui::PopFont();
	ImGui::Dummy(ImVec2(0.0f, 4.0f * U));
	ImGui::PushFont(pLabel);
	for(int i = 0; i < NUM_ELEMENTS; i++)
	{
		const SElementConfig Config = Element(i);
		bool Enabled = *Config.m_pEnabled != 0;
		if(ImGui::Checkbox(Tr(Config.m_pName), &Enabled))
		{
			*Config.m_pEnabled = Enabled ? 1 : 0;
			PlayUiSound(Enabled ? SOUND_TOGGLE_ON : SOUND_TOGGLE_OFF);
		}
		ImGui::SameLine(PanelSize.x - 90.0f * U);
		ImGui::TextColored(t.m_TextMuted, "%d%%", *Config.m_pScale);
	}
	bool Toasts = g_Config.m_XcHudToasts != 0;
	if(ImGui::Checkbox(Tr("Notifications"), &Toasts))
		g_Config.m_XcHudToasts = Toasts ? 1 : 0;
	ImGui::Dummy(ImVec2(0.0f, 6.0f * U));
	const float ButtonWidth = (ImGui::GetContentRegionAvail().x - 8.0f * U) * 0.5f;
	if(ImGui::Button(Tr("Reset"), ImVec2(ButtonWidth, 34.0f * U)))
	{
		for(int i = 0; i < NUM_ELEMENTS; i++)
		{
			const SElementConfig Config = Element(i);
			*Config.m_pX = Config.m_DefaultX;
			*Config.m_pY = Config.m_DefaultY;
			*Config.m_pScale = 100;
		}
		PlayUiSound(SOUND_CLICK);
	}
	ImGui::SameLine(0.0f, 8.0f * U);
	ImGui::PushStyleColor(ImGuiCol_Button, t.m_Accent);
	ImGui::PushStyleColor(ImGuiCol_ButtonHovered, Mix(t.m_Accent, ImVec4(1, 1, 1, 1), 0.12f));
	ImGui::PushStyleColor(ImGuiCol_Text, t.m_OnAccent);
	if(ImGui::Button(Tr("Done"), ImVec2(ButtonWidth, 34.0f * U)))
	{
		PlayUiSound(SOUND_CLICK);
		SetEditing(false);
	}
	ImGui::PopStyleColor(3);
	ImGui::PopFont();
	ImGui::End();
	ImGui::PopStyleVar(2);
	ImGui::PopStyleColor();

	// which element is under the mouse (the last drawn wins)
	int Hovered = -1;
	if(!OverPanel)
	{
		for(int i = 0; i < NUM_ELEMENTS; i++)
		{
			if(!*Element(i).m_pEnabled)
				continue;
			if(Io.MousePos.x >= m_aaRects[i][0] && Io.MousePos.x <= m_aaRects[i][2] && Io.MousePos.y >= m_aaRects[i][1] && Io.MousePos.y <= m_aaRects[i][3])
				Hovered = i;
		}
	}

	// drag: the center follows the mouse, snapping to the middle of the screen
	if(Hovered >= 0 && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
	{
		m_Dragging = Hovered;
		const ImVec2 C((m_aaRects[Hovered][0] + m_aaRects[Hovered][2]) * 0.5f, (m_aaRects[Hovered][1] + m_aaRects[Hovered][3]) * 0.5f);
		m_aDragOffset[0] = C.x - Io.MousePos.x;
		m_aDragOffset[1] = C.y - Io.MousePos.y;
	}
	if(!ImGui::IsMouseDown(ImGuiMouseButton_Left))
		m_Dragging = -1;
	m_aSnapped[0] = m_aSnapped[1] = false;
	if(m_Dragging >= 0)
	{
		const SElementConfig Config = Element(m_Dragging);
		ImVec2 C(Io.MousePos.x + m_aDragOffset[0], Io.MousePos.y + m_aDragOffset[1]);
		if(std::abs(C.x - Screen.x * 0.5f) < SNAP_DISTANCE * U)
		{
			C.x = Screen.x * 0.5f;
			m_aSnapped[0] = true;
		}
		if(std::abs(C.y - Screen.y * 0.5f) < SNAP_DISTANCE * U)
		{
			C.y = Screen.y * 0.5f;
			m_aSnapped[1] = true;
		}
		*Config.m_pX = std::clamp((int)std::round(C.x / Screen.x * 1000.0f), 0, 1000);
		*Config.m_pY = std::clamp((int)std::round(C.y / Screen.y * 1000.0f), 0, 1000);
	}
	// wheel: size in steps of 10 %
	const int Target = m_Dragging >= 0 ? m_Dragging : Hovered;
	if(Target >= 0 && Io.MouseWheel != 0.0f)
	{
		int *pScale = Element(Target).m_pScale;
		*pScale = std::clamp(*pScale + (Io.MouseWheel > 0.0f ? 10 : -10), 50, 200);
	}

	// guides and frames
	if(m_aSnapped[0])
		pDraw->AddLine(ImVec2(Screen.x * 0.5f, 0.0f), ImVec2(Screen.x * 0.5f, Screen.y), Col(t.m_Accent, 0.7f), 1.0f);
	if(m_aSnapped[1])
		pDraw->AddLine(ImVec2(0.0f, Screen.y * 0.5f), ImVec2(Screen.x, Screen.y * 0.5f), Col(t.m_Accent, 0.7f), 1.0f);
	for(int i = 0; i < NUM_ELEMENTS; i++)
	{
		const SElementConfig Config = Element(i);
		if(!*Config.m_pEnabled)
			continue;
		const float Active = (i == m_Dragging || i == Hovered) ? 1.0f : 0.45f;
		const ImVec2 Min(m_aaRects[i][0] - 5.0f * U, m_aaRects[i][1] - 5.0f * U);
		const ImVec2 Max(m_aaRects[i][2] + 5.0f * U, m_aaRects[i][3] + 5.0f * U);
		pDraw->AddRect(Min, Max, Col(t.m_Accent, Active), 15.0f * U, 0, i == m_Dragging ? 2.0f : 1.5f);
		const char *pName = Tr(Config.m_pName);
		const float NameSize = 12.0f * U;
		// the name plate goes above the frame, or below it at the top of the screen
		const float PlateHeight = NameSize + 7.0f * U;
		const bool Below = Min.y - PlateHeight - 3.0f * U < 0.0f;
		const ImVec2 NameMin(Min.x, Below ? Max.y + 3.0f * U : Min.y - PlateHeight - 3.0f * U);
		const float NameWidth = pSmall->CalcTextSizeA(NameSize, FLT_MAX, 0.0f, pName).x;
		pDraw->AddRectFilled(NameMin, ImVec2(NameMin.x + NameWidth + 12.0f * U, NameMin.y + PlateHeight), Col(t.m_Accent, Active), 6.0f * U);
		pDraw->AddText(pSmall, NameSize, ImVec2(NameMin.x + 6.0f * U, NameMin.y + 3.0f * U), Col(t.m_OnAccent, Active), pName);
	}
}

void CXcHud::OnRender()
{
	if(!m_Backend.IsReady())
		return;
	const bool Online = Client()->State() == IClient::STATE_ONLINE || Client()->State() == IClient::STATE_DEMOPLAYBACK;
	// the own scoreboard shows even with the HUD turned off, like the classic one
	const bool Scoreboard = Online && (m_ScoreboardOpen > 0.0f || (GameClient()->m_Scoreboard.IsActive() && DrawsScoreboard()));
	const bool HudVisible = Visible();
	if(!HudVisible && !Scoreboard)
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
	ImDrawList *pDraw = ImGui::GetBackgroundDrawList();
	// the HUD steps back while the menu is open
	const float FadeSpeed = AnimationSpeed() > 0.0f ? 8.0f * AnimationSpeed() : 1000.0f;
	const auto Approach = [&](float &Value, float Target) {
		Value = Value < Target ? std::min(Value + Dt * FadeSpeed, Target) : std::max(Value - Dt * FadeSpeed, Target);
	};
	Approach(m_MenuFade, GameClient()->m_XcMenu.IsActive() ? 0.0f : 1.0f);
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
	};
	for(int i = 0; i < NUM_ELEMENTS; i++)
		Approach(m_aShown[i], aWanted[i] ? 1.0f : 0.0f);
	if(HudVisible && (Alpha > 0.0f || m_Editing))
	{
		const float a = m_Editing ? 1.0f : Alpha;
		if(m_Editing)
			pDraw->AddRectFilled(ImVec2(0, 0), ImGui::GetIO().DisplaySize, Col(ImVec4(0, 0, 0, 1), 0.25f));
		const auto Shown = [&](int Element) { return m_Editing ? (aWanted[Element] ? 1.0f : 0.0f) : m_aShown[Element]; };
		if(Shown(ELEMENT_WATERMARK) > 0.0f)
			RenderWatermark(pDraw, a * Shown(ELEMENT_WATERMARK));
		if(Shown(ELEMENT_TIMER) > 0.0f)
			RenderTimer(pDraw, a * Shown(ELEMENT_TIMER));
		if(Shown(ELEMENT_SPEED) > 0.0f)
			RenderSpeed(pDraw, a * Shown(ELEMENT_SPEED));
		if(Shown(ELEMENT_WEAPON) > 0.0f)
			RenderWeapon(pDraw, a * Shown(ELEMENT_WEAPON));
		if(Shown(ELEMENT_MUSIC) > 0.0f)
			RenderMusic(pDraw, a * Shown(ELEMENT_MUSIC));
		if(Shown(ELEMENT_KEYS) > 0.0f)
			RenderKeys(pDraw, a * Shown(ELEMENT_KEYS));
		// the top list steps aside while the full scoreboard is open
		if(Shown(ELEMENT_MINITOP) > 0.0f)
			RenderMiniTop(pDraw, a * Shown(ELEMENT_MINITOP) * (m_Editing ? 1.0f : 1.0f - m_ScoreboardOpen));
		if(!m_Editing)
			RenderBanner(ImGui::GetForegroundDrawList());
		if(m_Editing)
			RenderEditor(ImGui::GetForegroundDrawList());
		else if(g_Config.m_XcHudToasts && HasToasts())
			RenderToasts(ImGui::GetForegroundDrawList(), ImGui::GetIO().DisplaySize, Client()->GlobalTime(), a, m_Backend.Fonts().m_pLabel, m_Backend.Fonts().m_pSmall);
	}
	if(Online)
		RenderScoreboard(ImGui::GetForegroundDrawList(), Dt);
	m_Backend.Render();

	if(m_Editing || ScoreboardMouse())
	{
		const float Width = (float)Graphics()->ScreenWidth();
		const float Height = (float)Graphics()->ScreenHeight();
		const vec2 Mouse = Ui()->UpdatedMousePos() * vec2(Width / std::max((float)Graphics()->WindowWidth(), 1.0f), Height / std::max((float)Graphics()->WindowHeight(), 1.0f));
		Graphics()->MapScreen(CScreenRect(0.0f, 0.0f, Width, Height));
		RenderTools()->RenderCursor(Mouse, 24.0f * Height / 1080.0f);
	}
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

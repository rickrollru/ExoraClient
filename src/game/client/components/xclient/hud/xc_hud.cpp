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
}

void CXcHud::ConEditHud(IConsole::IResult *pResult, void *pUserData)
{
	CXcHud *pSelf = static_cast<CXcHud *>(pUserData);
	pSelf->SetEditing(!pSelf->m_Editing);
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
	default: return {"Weapon", &g_Config.m_XcHudWeapon, &g_Config.m_XcHudWeaponX, &g_Config.m_XcHudWeaponY, &g_Config.m_XcHudWeaponScale, 565, 905};
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
	const float LogoRadius = 5.5f * U;
	const char *pName = "XClient";
	float Width = Pad + LogoRadius * 2.0f + 8.0f * U + pBold->CalcTextSizeA(TitleSize, FLT_MAX, 0.0f, pName).x;
	for(int i = 0; i < 4; i++)
		Width += Gap * 2.0f + 1.0f + pLabel->CalcTextSizeA(TextSize, FLT_MAX, 0.0f, apIcons[i]).x + IconGap + pLabel->CalcTextSizeA(TextSize, FLT_MAX, 0.0f, apTexts[i]).x;
	Width += Pad;

	const ImVec2 Min = PlaceRect(ELEMENT_WATERMARK, ImVec2(Width, Height));
	const ImVec2 Max(Min.x + Width, Min.y + Height);
	RenderPlate(pDraw, Min, Max, U, Alpha);

	// logo: accent dot with a soft glow, then the name
	float x = Min.x + Pad;
	const float MidY = Min.y + Height * 0.5f;
	pDraw->AddCircleFilled(ImVec2(x + LogoRadius, MidY), LogoRadius * 2.2f, Col(t.m_Accent, 0.16f * Alpha), 24);
	pDraw->AddCircleFilled(ImVec2(x + LogoRadius, MidY), LogoRadius, Col(t.m_Accent, Alpha), 24);
	x += LogoRadius * 2.0f + 8.0f * U;
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
	if(!m_Backend.IsReady() || !Visible())
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
	const float Alpha = GameClient()->m_XcMenu.IsActive() ? 0.0f : 1.0f;
	if(Alpha > 0.0f || m_Editing)
	{
		const float a = m_Editing ? 1.0f : Alpha;
		if(m_Editing)
			pDraw->AddRectFilled(ImVec2(0, 0), ImGui::GetIO().DisplaySize, Col(ImVec4(0, 0, 0, 1), 0.25f));
		if(g_Config.m_XcHudWatermark)
			RenderWatermark(pDraw, a);
		if(g_Config.m_XcHudTimer)
			RenderTimer(pDraw, a);
		if(g_Config.m_XcHudSpeed)
			RenderSpeed(pDraw, a);
		if(g_Config.m_XcHudWeapon)
			RenderWeapon(pDraw, a);
		if(m_Editing)
			RenderEditor(ImGui::GetForegroundDrawList());
		else if(g_Config.m_XcHudToasts && HasToasts())
			RenderToasts(ImGui::GetForegroundDrawList(), ImGui::GetIO().DisplaySize, Client()->GlobalTime(), a, m_Backend.Fonts().m_pLabel, m_Backend.Fonts().m_pSmall);
	}
	m_Backend.Render();

	if(m_Editing)
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
	if(!m_Editing)
		return false;
	Ui()->ConvertMouseMove(&x, &y, CursorType);
	Ui()->OnCursorMove(x, y);
	m_Backend.OnMouseMove(Ui()->UpdatedMousePos());
	return true;
}

bool CXcHud::OnInput(const IInput::CEvent &Event)
{
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

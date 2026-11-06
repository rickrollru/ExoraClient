// The first screen when the client starts (only a clock), the minimal home it opens into, and the animated menu
// backgrounds. Every movement eases in and out and runs on the frame time, so it looks the same at any FPS.
#include "menu.h"

#include "feedback.h"
#include "icons.h"
#include "lang.h"
#include "theme.h"
#include "widgets.h"

#include <base/color.h>
#include <base/str.h>
#include <base/time.h>

#include <engine/client.h>
#include <engine/external/imgui/imgui.h>
#include <engine/external/imgui/imgui_internal.h>
#include <engine/shared/config.h>

#include <game/client/gameclient.h>

#include <algorithm>
#include <cmath>
#include <ctime>

using namespace XcUi;

// shared with menu.cpp
void XcSoftCircle(ImDrawList *pDraw, ImVec2 Center, float Radius, ImVec4 Color);
ImVec4 XcShiftHue(const ImVec4 &Color, float Shift);
float XcHash(int i, int Salt);

namespace
{
	// fast at first, gently settling at the end
	float EaseOut(float x)
	{
		x = std::clamp(x, 0.0f, 1.0f);
		return 1.0f - std::pow(1.0f - x, 4.0f);
	}
	float Smooth(float x)
	{
		x = std::clamp(x, 0.0f, 1.0f);
		return x * x * (3.0f - 2.0f * x);
	}
	// a part of a page: it comes in after Delay, over Length
	float Stagger(float Time, float Delay, float Length = 0.75f) { return EaseOut((Time - Delay) / Length); }

	ImU32 Rgba(const ImVec4 &c, float a) { return ImGui::ColorConvertFloat4ToU32(ImVec4(c.x, c.y, c.z, c.w * std::clamp(a, 0.0f, 1.0f))); }
}

bool CXcMenu::MinimalHome() const
{
	return g_Config.m_XcHomeStyle == 0 && !IsOnline();
}

void CXcMenu::LeaveHome(int Page)
{
	if(m_HomeLeaveTo >= 0)
		return;
	PlayUiSound(SOUND_CLICK);
	m_HomeLeaveTo = Page;
	m_HomeLeave = 0.0f;
}

// ---------------------------------------------------------------------------------------------------------------
// the clock

void CXcMenu::RenderStartScreen(float Dt)
{
	const STheme &t = Theme();
	const ImVec2 Size = ImGui::GetIO().DisplaySize;
	ImDrawList *pDraw = ImGui::GetForegroundDrawList();
	m_StartTime += Dt;
	if(m_StartLeave >= 0.0f)
		m_StartLeave += Dt;

	// a click or any key sends the clock away
	if(m_StartLeave < 0.0f && m_StartTime > 0.35f)
	{
		bool Pressed = ImGui::IsMouseClicked(ImGuiMouseButton_Left) || ImGui::IsMouseClicked(ImGuiMouseButton_Right) || ImGui::GetIO().InputQueueCharacters.Size > 0;
		for(int Key = ImGuiKey_NamedKey_BEGIN; Key < ImGuiKey_NamedKey_END && !Pressed; Key++)
			if(Key < ImGuiKey_MouseLeft && ImGui::IsKeyPressed((ImGuiKey)Key, false))
				Pressed = true;
		if(Pressed)
		{
			m_StartLeave = 0.0f;
			PlayUiSound(SOUND_CLICK);
		}
	}
	const float Appear = EaseOut(m_StartTime / 1.1f);
	const float Leave = m_StartLeave < 0.0f ? 0.0f : Smooth(m_StartLeave / 0.9f);
	// the home starts to come in while the clock is still leaving
	if(m_StartLeave > 0.25f && m_StartLocked && m_HomeTime <= 0.0f)
		m_HomeTime = 0.0001f;
	if(m_StartLeave >= 0.95f)
	{
		m_StartLocked = false;
		return;
	}

	// the background stays a little darker while the clock is up
	pDraw->AddRectFilled(ImVec2(0, 0), Size, Rgba(t.m_Background, 0.38f * (1.0f - Leave)));

	const float Alpha = Appear * (1.0f - Leave);
	const float Rise = (1.0f - Appear) * S(18.0f) + Leave * S(70.0f);
	const float Grow = 1.0f + 0.14f * Leave;

	// the time: hours, a colon that breathes, minutes
	const time_t Now = std::time(nullptr);
	struct tm Local;
#if defined(CONF_FAMILY_WINDOWS)
	localtime_s(&Local, &Now);
#else
	localtime_r(&Now, &Local);
#endif
	int Hours = Local.tm_hour;
	const char *pSuffix = "";
	if(!g_Config.m_XcStartClock24h)
	{
		pSuffix = Hours < 12 ? "AM" : "PM";
		Hours = Hours % 12 == 0 ? 12 : Hours % 12;
	}
	char aHours[8], aMinutes[8];
	str_format(aHours, sizeof(aHours), g_Config.m_XcStartClock24h ? "%02d" : "%d", Hours);
	str_format(aMinutes, sizeof(aMinutes), "%02d", Local.tm_min);
	ImFont *pClock = Fonts().m_pClock ? Fonts().m_pClock : Fonts().m_pHeader;
	const float ClockSize = pClock->FontSize * Grow;
	const float HoursW = pClock->CalcTextSizeA(ClockSize, FLT_MAX, 0.0f, aHours).x;
	const float ColonW = pClock->CalcTextSizeA(ClockSize, FLT_MAX, 0.0f, ":").x;
	const float MinutesW = pClock->CalcTextSizeA(ClockSize, FLT_MAX, 0.0f, aMinutes).x;
	const float Gap = ClockSize * 0.04f;
	const float TotalW = HoursW + ColonW + MinutesW + Gap * 2.0f;
	const float Top = Size.y * 0.5f - ClockSize * 0.62f - Rise;
	float x = (Size.x - TotalW) * 0.5f;
	const ImVec4 White = t.m_Text;
	pDraw->AddText(pClock, ClockSize, ImVec2(x, Top), Rgba(White, Alpha), aHours);
	x += HoursW + Gap;
	const float Breath = 0.45f + 0.55f * (0.5f + 0.5f * std::cos(m_StartTime * 3.14159f));
	pDraw->AddText(pClock, ClockSize, ImVec2(x, Top - ClockSize * 0.02f), Rgba(White, Alpha * Breath), ":");
	x += ColonW + Gap;
	pDraw->AddText(pClock, ClockSize, ImVec2(x, Top), Rgba(White, Alpha), aMinutes);
	if(pSuffix[0])
	{
		ImFont *pSmall = Fonts().m_pTitle;
		pDraw->AddText(pSmall, pSmall->FontSize * 1.2f, ImVec2(x + MinutesW + S(10.0f), Top + ClockSize * 0.2f), Rgba(t.m_TextSecondary, Alpha), pSuffix);
	}

	// the date and a quiet hint
	float y = Top + ClockSize * 1.08f;
	if(g_Config.m_XcStartClockDate)
	{
		static const char *const s_apDays[] = {"Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday"};
		static const char *const s_apMonths[] = {"January", "February", "March", "April", "May", "June", "July", "August", "September", "October", "November", "December"};
		char aDate[96];
		// "Friday, 6 November" (the month in the genitive in Russian: the table has the whole phrase)
		char aMonthKey[32];
		str_format(aMonthKey, sizeof(aMonthKey), "of %s", s_apMonths[Local.tm_mon]);
		str_format(aDate, sizeof(aDate), "%s, %d %s", Tr(s_apDays[Local.tm_wday]), Local.tm_mday, Tr(aMonthKey));
		ImFont *pDateFont = Fonts().m_pLabel;
		const float DateSize = pDateFont->FontSize * 1.15f;
		const float DateW = pDateFont->CalcTextSizeA(DateSize, FLT_MAX, 0.0f, aDate).x;
		pDraw->AddText(pDateFont, DateSize, ImVec2((Size.x - DateW) * 0.5f, y), Rgba(t.m_TextSecondary, Alpha * Stagger(m_StartTime, 0.25f, 1.0f)), aDate);
		y += DateSize + S(36.0f);
	}
	else
		y += S(30.0f);
	const char *pHint = Tr("Click anywhere or press any key");
	ImFont *pHintFont = Fonts().m_pSmall;
	const float HintW = pHintFont->CalcTextSizeA(pHintFont->FontSize, FLT_MAX, 0.0f, pHint).x;
	const float Pulse = 0.45f + 0.55f * (0.5f + 0.5f * std::cos(m_StartTime * 2.4f));
	pDraw->AddText(pHintFont, pHintFont->FontSize, ImVec2((Size.x - HintW) * 0.5f, y), Rgba(t.m_TextMuted, Alpha * Pulse * Stagger(m_StartTime, 0.8f, 1.0f)), pHint);
}

// ---------------------------------------------------------------------------------------------------------------
// the minimal home: the tee, the name, Play, a row of icons, a quiet line in the corner

void CXcMenu::RenderMinimalHome(float Dt)
{
	const STheme &t = Theme();
	ImDrawList *pDraw = ImGui::GetWindowDrawList();
	const ImVec2 Avail = ImGui::GetContentRegionAvail();
	const ImVec2 Origin = ImGui::GetCursorScreenPos();
	if(m_HomeTime > 0.0f)
		m_HomeTime += Dt;
	// leaving: everything sinks and fades, then the page opens
	float Out = 0.0f;
	if(m_HomeLeaveTo >= 0)
	{
		m_HomeLeave += Dt;
		Out = Smooth(m_HomeLeave / 0.32f);
		if(m_HomeLeave >= 0.32f)
		{
			m_Page = m_HomeLeaveTo;
			m_HomeLeaveTo = -1;
			m_PageAppear = 0.0f;
			m_ScrollTarget = 0.0f;
			return;
		}
	}
	const float Time = m_HomeTime;
	const auto Part = [&](int Index) { return Stagger(Time, 0.07f * Index) * (1.0f - Out); };
	const auto Shift = [&](float a) { return (1.0f - a) * S(16.0f) + Out * S(10.0f); };

	const float TeeBox = std::min(S(210.0f), Avail.y * 0.32f);
	const float NickH = Fonts().m_pHeader->FontSize;
	const float PlayH = S(50.0f), IconSize = S(46.0f);
	const float BlockH = TeeBox + S(10.0f) + NickH + S(22.0f) + PlayH + S(18.0f) + IconSize;
	float y = Origin.y + std::max((Avail.y - BlockH) * 0.46f, 0.0f);
	const float Cx = Origin.x + Avail.x * 0.5f;

	// the tee floats slowly up and down
	{
		const float a = Part(0);
		const float Float = std::sin(Client()->GlobalTime() * 1.15f) * S(6.0f);
		const ImVec2 Min(Cx - TeeBox * 0.5f, y + Shift(a) + Float);
		STeeDraw Data = {this, {Min.x, Min.y, Min.x + TeeBox, Min.y + TeeBox}, false, 0, ""};
		Data.m_Alpha = a;
		pDraw->AddCallback(TeePreviewCallback, (void *)&Data, sizeof(Data));
		pDraw->AddCallback(ImDrawCallback_ResetRenderState, nullptr);
		y += TeeBox + S(10.0f);
	}
	// the name with the Exora mark
	{
		const float a = Part(1);
		ImFont *pFont = Fonts().m_pHeader;
		const char *pName = g_Config.m_PlayerName[0] ? g_Config.m_PlayerName : Client()->PlayerName();
		const float NameW = pFont->CalcTextSizeA(pFont->FontSize, FLT_MAX, 0.0f, pName).x;
		const float Mark = NickH * 0.95f, MarkGap = S(10.0f);
		const float Left = Cx - (NameW + Mark + MarkGap) * 0.5f;
		DrawLogo(pDraw, ImVec2(Left + Mark * 0.5f, y + NickH * 0.5f + Shift(a)), Mark, t.m_Accent, a, true);
		pDraw->AddText(pFont, pFont->FontSize, ImVec2(Left + Mark + MarkGap, y + Shift(a)), Rgba(t.m_Text, a), pName);
		y += NickH + S(22.0f);
	}
	// Play: lifts a little under the mouse, gives way when pressed
	{
		const float a = Part(2);
		const char *pLabel = Tr("Play");
		ImFont *pFont = Fonts().m_pTitle;
		const float W = S(230.0f);
		ImGui::SetCursorScreenPos(ImVec2(Cx - W * 0.5f, y));
		const bool Clicked = ImGui::InvisibleButton("##min_play", ImVec2(W, PlayH)) && a > 0.5f;
		const float Hover = Animate(ImGui::GetID("##min_play_h"), ImGui::IsItemHovered() ? 1.0f : 0.0f, 12.0f);
		const float Press = Animate(ImGui::GetID("##min_play_p"), ImGui::IsItemActive() ? 1.0f : 0.0f, 22.0f);
		const float Scale = 1.0f + 0.03f * Hover - 0.035f * Press;
		const float Lift = Hover * S(2.0f) - Shift(a);
		const ImVec2 C(Cx, y + PlayH * 0.5f - Lift);
		const ImVec2 Half(W * 0.5f * Scale, PlayH * 0.5f * Scale);
		if(Hover > 0.01f)
			XcSoftCircle(pDraw, ImVec2(C.x, C.y + S(8.0f)), W * 0.62f, ImVec4(t.m_Accent.x, t.m_Accent.y, t.m_Accent.z, 0.22f * Hover * a));
		pDraw->AddRectFilled(ImVec2(C.x - Half.x, C.y - Half.y), ImVec2(C.x + Half.x, C.y + Half.y), Rgba(Mix(t.m_Text, ImVec4(1, 1, 1, 1), Hover), a), S(14.0f) * Scale);
		const float FontSize = pFont->FontSize * 1.1f * Scale;
		const ImVec2 Dim = pFont->CalcTextSizeA(FontSize, FLT_MAX, 0.0f, pLabel);
		pDraw->AddText(pFont, FontSize, ImVec2(C.x - Dim.x * 0.5f, C.y - Dim.y * 0.5f), Rgba(ImVec4(0.06f, 0.07f, 0.1f, 1.0f), a), pLabel);
		if(Clicked)
		{
			const char *pLast = g_Config.m_UiServerAddress;
			if(pLast[0] && !str_startswith(pLast, "localhost") && !str_startswith(pLast, "127.0.0.1"))
			{
				m_SelectedServer = pLast;
				m_ScrollToSelected = true;
			}
			LeaveHome(PAGE_SERVERS);
		}
		y += PlayH + S(18.0f);
	}
	// the row of icons, each with its name floating in under it
	{
		struct SIcon
		{
			const char *m_pIcon;
			const char *m_pName;
			int m_Action;
		};
		enum
		{
			ACT_SERVERS = 0,
			ACT_EDITOR,
			ACT_DEMOS,
			ACT_VISUALS,
			ACT_SETTINGS,
			ACT_QUIT,
		};
		const SIcon aIcons[] = {
			{XC_ICON_SERVER, Tr("Servers"), ACT_SERVERS},
			{XC_ICON_MAP, Tr("Map editor"), ACT_EDITOR},
			{XC_ICON_FILM, Tr("Demos"), ACT_DEMOS},
			{XC_ICON_WAND, Tr("Visuals"), ACT_VISUALS},
			{XC_ICON_GEAR, Tr("Settings"), ACT_SETTINGS},
			{XC_ICON_POWER, Tr("Quit"), ACT_QUIT},
		};
		const int Num = (int)std::size(aIcons);
		const float Gap = S(10.0f);
		const float RowW = Num * IconSize + (Num - 1) * Gap;
		const float a = Part(3);
		for(int i = 0; i < Num; i++)
		{
			const float x = Cx - RowW * 0.5f + i * (IconSize + Gap);
			ImGui::SetCursorScreenPos(ImVec2(x, y));
			ImGui::PushID(i);
			const bool Clicked = ImGui::InvisibleButton("##min_icon", ImVec2(IconSize, IconSize)) && a > 0.5f;
			const float Hover = Animate(ImGui::GetID("##h"), ImGui::IsItemHovered() ? 1.0f : 0.0f, 12.0f);
			const float Press = Animate(ImGui::GetID("##p"), ImGui::IsItemActive() ? 1.0f : 0.0f, 22.0f);
			ImGui::PopID();
			const float Each = Stagger(Time, 0.21f + 0.05f * i) * (1.0f - Out);
			const float Scale = 1.0f - 0.06f * Press;
			const ImVec2 C(x + IconSize * 0.5f, y + IconSize * 0.5f - Hover * S(3.0f) + Shift(Each));
			const float Half = IconSize * 0.5f * Scale;
			pDraw->AddRectFilled(ImVec2(C.x - Half, C.y - Half), ImVec2(C.x + Half, C.y + Half), Rgba(ImVec4(1, 1, 1, 0.05f + 0.06f * Hover), Each), S(13.0f));
			pDraw->AddRect(ImVec2(C.x - Half, C.y - Half), ImVec2(C.x + Half, C.y + Half), Rgba(ImVec4(1, 1, 1, 0.07f + 0.07f * Hover), Each), S(13.0f), 0, 1.0f);
			DrawIcon(pDraw, Fonts().m_pIcons, Fonts().m_pIcons->FontSize, C, Rgba(Mix(t.m_TextSecondary, t.m_Text, Hover), Each), aIcons[i].m_pIcon);
			if(Hover > 0.01f)
			{
				ImFont *pSmall = Fonts().m_pSmall;
				const ImVec2 Dim = pSmall->CalcTextSizeA(pSmall->FontSize, FLT_MAX, 0.0f, aIcons[i].m_pName);
				pDraw->AddText(pSmall, pSmall->FontSize, ImVec2(C.x - Dim.x * 0.5f, y + IconSize + S(8.0f) + (1.0f - Hover) * S(-4.0f)), Rgba(t.m_TextSecondary, Hover * Each), aIcons[i].m_pName);
			}
			if(!Clicked)
				continue;
			switch(aIcons[i].m_Action)
			{
			case ACT_SERVERS: LeaveHome(PAGE_SERVERS); break;
			case ACT_DEMOS: LeaveHome(PAGE_DEMOS); break;
			case ACT_VISUALS: LeaveHome(PAGE_VISUALS); break;
			case ACT_SETTINGS: LeaveHome(PAGE_SETTINGS); break;
			case ACT_EDITOR:
				PlayUiSound(SOUND_CLICK);
				g_Config.m_ClEditor = 1;
				Input()->MouseModeRelative();
				break;
			default:
				Client()->Quit();
				break;
			}
		}
	}
	// a quiet line in the corner: how many play Exora now, what plays
	{
		const float a = Part(5) * 0.9f;
		ImFont *pSmall = Fonts().m_pSmall;
		const float Base = Origin.y + Avail.y - pSmall->FontSize;
		float x = Origin.x;
		const int Online = GameClient()->m_XcCosmetics.Online();
		if(Online > 0)
		{
			char aBuf[64];
			str_format(aBuf, sizeof(aBuf), Tr("%d in Exora now"), Online);
			pDraw->AddCircleFilled(ImVec2(x + S(4.0f), Base + pSmall->FontSize * 0.5f), S(3.5f), Rgba(ImVec4(0.24f, 0.84f, 0.64f, 1.0f), a * (0.65f + 0.35f * std::sin(Client()->GlobalTime() * 2.0f))));
			x += S(14.0f);
			pDraw->AddText(pSmall, pSmall->FontSize, ImVec2(x, Base), Rgba(t.m_TextMuted, a), aBuf);
			x += pSmall->CalcTextSizeA(pSmall->FontSize, FLT_MAX, 0.0f, aBuf).x + S(18.0f);
		}
		const CXcMusic &Music = GameClient()->m_XcMusic;
		if(g_Config.m_XcMusic && Music.HasTrack())
		{
			char aTrack[256];
			Music.TrackText(aTrack, sizeof(aTrack));
			DrawIcon(pDraw, pSmall, pSmall->FontSize, ImVec2(x + S(6.0f), Base + pSmall->FontSize * 0.5f), Rgba(t.m_TextMuted, a), XC_ICON_MUSIC);
			pDraw->AddText(pSmall, pSmall->FontSize, ImVec2(x + S(18.0f), Base), Rgba(t.m_TextMuted, a), aTrack);
		}
	}
	// Enter plays, like the big button
	if(Time > 0.5f && m_HomeLeaveTo < 0 && ImGui::IsKeyPressed(ImGuiKey_Enter, false) && !ImGui::GetIO().WantTextInput)
		LeaveHome(PAGE_SERVERS);
	ImGui::SetCursorScreenPos(Origin);
	ImGui::Dummy(Avail);
}

// ---------------------------------------------------------------------------------------------------------------
// backgrounds

// tees float in the air at different depths: the far ones small and dim, the near ones big; they drift, bob, turn
// their eyes to the cursor and lean with it a little (parallax)
void CXcMenu::RenderTeesBackground(ImDrawList *pDraw, float Alpha)
{
	const STheme &t = Theme();
	const ImVec2 Size = ImGui::GetIO().DisplaySize;
	const float Time = m_BackgroundTime;
	const bool Light = t.m_Background.x > 0.5f;
	// the base: the theme background, a little of the accent toward the bottom
	const ImVec4 Top(t.m_Background.x, t.m_Background.y, t.m_Background.z, Alpha);
	ImVec4 Bottom = Mix(t.m_Background, t.m_Accent, Light ? 0.07f : 0.06f);
	Bottom.w = Alpha;
	pDraw->AddRectFilledMultiColor(ImVec2(0, 0), Size, Col(Top), Col(Top), Col(Bottom), Col(Bottom));
	// two big soft lights that wander very slowly
	for(int i = 0; i < 2; i++)
	{
		const ImVec2 C(Size.x * (0.3f + 0.4f * i + 0.12f * std::sin(Time * (0.031f + 0.013f * i) * 6.283f + i * 2.0f)), Size.y * (0.35f + 0.3f * i + 0.1f * std::cos(Time * (0.027f + 0.01f * i) * 6.283f)));
		ImVec4 Color = XcShiftHue(t.m_Accent, i ? 0.09f : 0.0f);
		Color.w = (Light ? 0.22f : 0.30f) * Alpha;
		XcSoftCircle(pDraw, C, Size.y * (0.75f - 0.15f * i), Color);
	}
	// the mouse, eased: the tees lean toward it
	static ImVec2 s_Mouse(0.5f, 0.5f);
	const ImVec2 MouseNow(ImGui::GetIO().MousePos.x / std::max(Size.x, 1.0f), ImGui::GetIO().MousePos.y / std::max(Size.y, 1.0f));
	if(MouseNow.x >= 0.0f && MouseNow.x <= 1.0f && MouseNow.y >= 0.0f && MouseNow.y <= 1.0f)
	{
		const float k = 1.0f - std::exp(-std::min(Client()->RenderFrameTime(), 0.1f) * 2.0f);
		s_Mouse.x += (MouseNow.x - s_Mouse.x) * k;
		s_Mouse.y += (MouseNow.y - s_Mouse.y) * k;
	}
	static const char *const s_apSkins[] = {"default", "bluekitty", "limekitty", "pinky", "redbopp", "twinbop", "coala", "cammo", "toptri", "saddo", "bluestripe", "brownbear", "warpaint", "twintri"};
	const int NumTees = 13;
	// far ones first
	for(int Pass = 0; Pass < 3; Pass++)
	{
		for(int i = 0; i < NumTees; i++)
		{
			const int Layer = XcHash(i, 11) < 0.45f ? 0 : (XcHash(i, 11) < 0.82f ? 1 : 2);
			if(Layer != Pass)
				continue;
			const float Depth = (Layer + XcHash(i, 12)) / 3.0f; // 0 far .. 1 near
			const float TeeSize = S(34.0f + 92.0f * Depth);
			// a slow drift across the screen, wrapping around, with a soft bob and sway
			const float SpeedX = (XcHash(i, 13) - 0.5f) * (0.006f + 0.012f * Depth);
			const float SpeedY = -(0.004f + 0.010f * Depth) * (0.4f + XcHash(i, 14));
			const float Range = 1.0f + 2.0f * TeeSize / std::max(Size.x, 1.0f);
			float u = std::fmod(XcHash(i, 15) + Time * SpeedX + 10.0f, 1.0f) * Range - TeeSize / std::max(Size.x, 1.0f);
			float v = std::fmod(XcHash(i, 16) + Time * SpeedY + 10.0f, 1.0f) * (1.0f + 2.0f * TeeSize / std::max(Size.y, 1.0f)) - TeeSize / std::max(Size.y, 1.0f);
			const float Parallax = S(10.0f + 40.0f * Depth);
			const float Bob = std::sin(Time * (0.6f + 0.5f * XcHash(i, 17)) + i) * S(8.0f) * (0.5f + Depth);
			const ImVec2 C(u * Size.x - (s_Mouse.x - 0.5f) * Parallax + std::sin(Time * 0.23f + i * 1.7f) * S(12.0f), v * Size.y - (s_Mouse.y - 0.5f) * Parallax + Bob);
			// fade in and out at the edges of the wrap, so nothing pops
			const float EdgeFade = std::min({Smooth(v * 6.0f), Smooth((1.0f - v) * 6.0f), 1.0f});
			const float TeeAlpha = Alpha * (Light ? 0.35f + 0.45f * Depth : 0.22f + 0.5f * Depth) * EdgeFade;
			if(TeeAlpha < 0.01f)
				continue;
			// a soft light behind the near ones
			if(Layer == 2)
				XcSoftCircle(pDraw, C, TeeSize * 1.1f, ImVec4(t.m_Accent.x, t.m_Accent.y, t.m_Accent.z, 0.10f * TeeAlpha));
			STeeDraw Data = {this, {C.x - TeeSize * 0.8f, C.y - TeeSize * 0.8f, C.x + TeeSize * 0.8f, C.y + TeeSize * 0.8f}, true, 0, ""};
			str_copy(Data.m_aSkin, s_apSkins[i % (int)std::size(s_apSkins)]);
			// soft colors around the accent, the feet a little darker
			float AccentH, AccentS, AccentV;
			ImGui::ColorConvertRGBtoHSV(t.m_Accent.x, t.m_Accent.y, t.m_Accent.z, AccentH, AccentS, AccentV);
			const float Hue = std::fmod(AccentH + (XcHash(i, 19) - 0.5f) * 0.45f + 1.0f, 1.0f);
			Data.m_ColorMode = 1;
			Data.m_Body = ColorHSLA(Hue, 0.45f + 0.25f * XcHash(i, 20), 0.62f + 0.12f * XcHash(i, 21), 1.0f).Pack(false);
			Data.m_Feet = ColorHSLA(Hue, 0.4f, 0.48f, 1.0f).Pack(false);
			Data.m_Alpha = TeeAlpha;
			// they look around on their own, the near ones toward the cursor
			const float Look = Time * (0.15f + 0.2f * XcHash(i, 18)) + i * 2.3f;
			vec2 Dir(std::cos(Look), std::sin(Look) * 0.6f);
			if(Layer == 2)
			{
				const ImVec2 M = ImGui::GetIO().MousePos;
				const vec2 ToMouse(M.x - C.x, M.y - C.y);
				if(length(ToMouse) > 1.0f)
					Dir = normalize(ToMouse);
			}
			Data.m_Look = Dir;
			pDraw->AddCallback(TeePreviewCallback, (void *)&Data, sizeof(Data));
			pDraw->AddCallback(ImDrawCallback_ResetRenderState, nullptr);
		}
	}
	// the corners a little darker, the middle stands out
	if(!Light)
	{
		const ImU32 Clear = Col(t.m_Background, 0.0f);
		const ImU32 Edge = Col(t.m_Background, 0.5f * Alpha);
		const float Band = Size.y * 0.22f;
		pDraw->AddRectFilledMultiColor(ImVec2(0, 0), ImVec2(Size.x, Band), Edge, Edge, Clear, Clear);
		pDraw->AddRectFilledMultiColor(ImVec2(0, Size.y - Band), Size, Clear, Clear, Edge, Edge);
	}
}

// soft lines that flow like silk across the screen, in the accent and its neighbours
void CXcMenu::RenderWavesBackground(ImDrawList *pDraw, float Alpha)
{
	const STheme &t = Theme();
	const ImVec2 Size = ImGui::GetIO().DisplaySize;
	const float Time = m_BackgroundTime;
	const bool Light = t.m_Background.x > 0.5f;
	const ImVec4 Top(t.m_Background.x, t.m_Background.y, t.m_Background.z, Alpha);
	ImVec4 Bottom = Mix(t.m_Background, t.m_Accent, Light ? 0.07f : 0.07f);
	Bottom.w = Alpha;
	pDraw->AddRectFilledMultiColor(ImVec2(0, 0), Size, Col(Top), Col(Top), Col(Bottom), Col(Bottom));
	XcSoftCircle(pDraw, ImVec2(Size.x * 0.5f, Size.y * 0.62f), Size.y * 0.8f, ImVec4(t.m_Accent.x, t.m_Accent.y, t.m_Accent.z, (Light ? 0.16f : 0.22f) * Alpha));
	const int NumLines = 14;
	const int Steps = 90;
	static ImVec2 s_aPoints[91];
	for(int Line = 0; Line < NumLines; Line++)
	{
		const float k = Line / (float)(NumLines - 1);
		const float Base = Size.y * (0.42f + 0.32f * k);
		const float Amp = Size.y * (0.05f + 0.05f * std::sin(k * 3.0f + 1.0f));
		const float Phase = Time * (0.16f + 0.05f * k) + k * 2.2f;
		for(int i = 0; i <= Steps; i++)
		{
			const float x = Size.x * i / (float)Steps;
			const float u = i / (float)Steps;
			const float yv = Base + Amp * std::sin(u * 6.0f + Phase) + Amp * 0.6f * std::sin(u * 2.3f - Phase * 0.7f + k * 4.0f);
			s_aPoints[i] = ImVec2(x, yv);
		}
		ImVec4 Color = XcShiftHue(t.m_Accent, (k - 0.5f) * 0.18f);
		const float LineAlpha = Alpha * (Light ? 0.22f : 0.30f) * (0.4f + 0.6f * std::sin(k * 3.14159f));
		Color.w = LineAlpha * 0.35f;
		pDraw->AddPolyline(s_aPoints, Steps + 1, Col(Color), ImDrawFlags_None, S(5.0f));
		Color.w = LineAlpha;
		pDraw->AddPolyline(s_aPoints, Steps + 1, Col(Color), ImDrawFlags_None, std::max(S(1.3f), 1.0f));
	}
	if(!Light)
	{
		const ImU32 Clear = Col(t.m_Background, 0.0f);
		const ImU32 Edge = Col(t.m_Background, 0.5f * Alpha);
		const float Band = Size.y * 0.22f;
		pDraw->AddRectFilledMultiColor(ImVec2(0, 0), ImVec2(Size.x, Band), Edge, Edge, Clear, Clear);
		pDraw->AddRectFilledMultiColor(ImVec2(0, Size.y - Band), Size, Clear, Clear, Edge, Edge);
	}
}

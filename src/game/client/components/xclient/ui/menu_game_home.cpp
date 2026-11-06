// The minimal home in game (Esc on a server): the map name big, the server over it, the race time and the best time
// under it, "Back to the game" and a row of icons; the dummy and leaving the server in the top right corner, the
// track and Play / Spectate at the bottom. The map card slides in from the right, the vote opens under the icons.
// Every movement eases and runs on the frame time.
#include "menu.h"

#include "feedback.h"
#include "icons.h"
#include "lang.h"
#include "theme.h"
#include "widgets.h"

#include <base/str.h>

#include <engine/client.h>
#include <engine/external/imgui/imgui.h>
#include <engine/shared/config.h>
#include <engine/shared/protocol.h>

#include <game/client/gameclient.h>

#include <algorithm>
#include <cmath>

using namespace XcUi;

void XcSoftCircle(ImDrawList *pDraw, ImVec2 Center, float Radius, ImVec4 Color);

namespace
{
	float EaseOut(float x)
	{
		x = std::clamp(x, 0.0f, 1.0f);
		return 1.0f - std::pow(1.0f - x, 4.0f);
	}
	ImU32 Rgba(const ImVec4 &c, float a) { return ImGui::ColorConvertFloat4ToU32(ImVec4(c.x, c.y, c.z, c.w * std::clamp(a, 0.0f, 1.0f))); }
	void FormatRaceTime(char *pBuf, int Size, int Seconds, int Centi)
	{
		if(Seconds >= 3600)
			str_format(pBuf, Size, "%d:%02d:%02d", Seconds / 3600, (Seconds / 60) % 60, Seconds % 60);
		else if(Centi >= 0)
			str_format(pBuf, Size, "%02d:%02d.%02d", Seconds / 60, Seconds % 60, Centi);
		else
			str_format(pBuf, Size, "%02d:%02d", Seconds / 60, Seconds % 60);
	}
}

bool CXcMenu::GameHome() const
{
	return g_Config.m_XcHomeStyle == 0 && Client()->State() == IClient::STATE_ONLINE;
}

bool CXcMenu::GameHomeEscape()
{
	if(m_GameMapCard)
	{
		m_GameMapCard = false;
		return true;
	}
	if(m_GamePopup != GAME_POPUP_NONE)
	{
		m_GamePopup = GAME_POPUP_NONE;
		return true;
	}
	return false;
}

void CXcMenu::RenderGameHome(float Dt)
{
	const STheme &t = Theme();
	ImDrawList *pDraw = ImGui::GetWindowDrawList();
	const ImVec2 Avail = ImGui::GetContentRegionAvail();
	const ImVec2 Origin = ImGui::GetCursorScreenPos();
	if(m_HomeTime > 0.0f)
		m_HomeTime += Dt;
	float Out = 0.0f;
	if(m_HomeLeaveTo >= 0)
	{
		m_HomeLeave += Dt;
		Out = EaseOut(m_HomeLeave / 0.32f);
		if(m_HomeLeave >= 0.32f)
		{
			m_Page = m_HomeLeaveTo;
			m_HomeLeaveTo = -1;
			m_PageAppear = 0.0f;
			m_ScrollTarget = 0.0f;
			m_GameMapCard = false;
			m_GamePopup = GAME_POPUP_NONE;
			return;
		}
	}
	const float Time = m_HomeTime;
	const auto Part = [&](int Index) { return EaseOut((Time - 0.06f * Index) / 0.7f) * (1.0f - Out); };
	const auto Shift = [&](float a) { return (1.0f - a) * S(14.0f) + Out * S(10.0f); };
	const CServerInfo &Info = Client()->ServerInfo();
	const auto &Snap = GameClient()->m_Snap;
	const int LocalId = Snap.m_LocalClientId;
	const bool MouseClicked = ImGui::IsMouseClicked(ImGuiMouseButton_Left);
	ImFont *pSmall = Fonts().m_pSmall;
	ImFont *pLabel = Fonts().m_pLabel;

	// a soft button with an optional icon; Tone: 0 quiet, 1 the danger red on hover, 2 on (mint)
	const auto Pill = [&](const char *pId, ImVec2 Pos, const char *pIcon, const char *pText, int Tone, float a) {
		const float H = S(38.0f);
		const float TextW = pLabel->CalcTextSizeA(pLabel->FontSize, FLT_MAX, 0.0f, pText).x;
		const float W = S(14.0f) * 2.0f + TextW + (pIcon ? S(24.0f) : 0.0f);
		Pos.x -= W; // placed by the right edge
		ImGui::SetCursorScreenPos(Pos);
		const bool Clicked = ImGui::InvisibleButton(pId, ImVec2(W, H)) && a > 0.5f;
		const float Hover = Animate(ImGui::GetItemID(), ImGui::IsItemHovered() ? 1.0f : 0.0f, 14.0f);
		const float Lift = Hover * S(2.0f) - Shift(a);
		const ImVec2 Min(Pos.x, Pos.y - Lift), Max(Pos.x + W, Pos.y + H - Lift);
		const ImVec4 Red(1.0f, 0.30f, 0.30f, 1.0f), Mint(0.24f, 0.84f, 0.64f, 1.0f);
		const ImVec4 Edge = Tone == 2 ? ImVec4(Mint.x, Mint.y, Mint.z, 0.35f) : Tone == 1 ? Mix(ImVec4(1, 1, 1, 0.07f), ImVec4(Red.x, Red.y, Red.z, 0.35f), Hover) : ImVec4(1, 1, 1, 0.07f + 0.06f * Hover);
		pDraw->AddRectFilled(Min, Max, Rgba(ImVec4(1, 1, 1, 0.05f + 0.05f * Hover), a), S(12.0f));
		pDraw->AddRect(Min, Max, Rgba(Edge, a), S(12.0f), 0, 1.0f);
		const ImVec4 TextColor = Tone == 1 ? Mix(t.m_TextSecondary, Red, Hover) : Mix(t.m_TextSecondary, t.m_Text, Tone == 2 ? 1.0f : Hover);
		float x = Min.x + S(14.0f);
		if(pIcon)
		{
			if(Tone == 2)
			{
				XcSoftCircle(pDraw, ImVec2(x + S(7.0f), (Min.y + Max.y) * 0.5f), S(14.0f), ImVec4(Mint.x, Mint.y, Mint.z, 0.5f * a));
				pDraw->AddCircleFilled(ImVec2(x + S(7.0f), (Min.y + Max.y) * 0.5f), S(5.0f), Rgba(Mint, a));
			}
			else
				DrawIcon(pDraw, pLabel, pLabel->FontSize, ImVec2(x + S(7.0f), (Min.y + Max.y) * 0.5f), Rgba(TextColor, a), pIcon);
			x += S(24.0f);
		}
		pDraw->AddText(pLabel, pLabel->FontSize, ImVec2(x, (Min.y + Max.y - pLabel->FontSize) * 0.5f), Rgba(TextColor, a), pText);
		if(Clicked)
			PlayUiSound(SOUND_CLICK);
		return std::make_pair(Clicked, ImVec4(Min.x, Min.y, Max.x, Max.y));
	};
	// a little panel that opens downward: grows and fades in
	const auto Popup = [&](ImVec2 Min, float W, float H, float Open) {
		const float y = Min.y - (1.0f - Open) * S(8.0f);
		pDraw->AddRectFilled(ImVec2(Min.x, y), ImVec2(Min.x + W, y + H), Rgba(t.m_Panel, Open * 1.1f), S(14.0f));
		pDraw->AddRect(ImVec2(Min.x, y), ImVec2(Min.x + W, y + H), Rgba(t.m_Border, Open), S(14.0f), 0, 1.0f);
		return ImVec2(Min.x, y);
	};
	const auto SmallButton = [&](const char *pId, ImVec2 Pos, const char *pText, const ImVec4 &Accent, bool Solid, bool Selected, float a) {
		const float TextW = pLabel->CalcTextSizeA(pLabel->FontSize, FLT_MAX, 0.0f, pText).x;
		const ImVec2 Size(TextW + S(28.0f), S(34.0f));
		ImGui::SetCursorScreenPos(Pos);
		const bool Clicked = ImGui::InvisibleButton(pId, Size) && a > 0.6f;
		const float Hover = Animate(ImGui::GetItemID(), (ImGui::IsItemHovered() || Selected) ? 1.0f : 0.0f, 16.0f);
		const ImVec2 Max(Pos.x + Size.x, Pos.y + Size.y);
		if(Solid)
			pDraw->AddRectFilled(Pos, Max, Rgba(Mix(Accent, ImVec4(1, 1, 1, 1), 0.12f * Hover), a), S(10.0f));
		else
		{
			pDraw->AddRectFilled(Pos, Max, Rgba(ImVec4(1, 1, 1, 0.06f + 0.05f * Hover), a), S(10.0f));
			pDraw->AddRect(Pos, Max, Rgba(Mix(ImVec4(1, 1, 1, 0.07f), ImVec4(Accent.x, Accent.y, Accent.z, 0.5f), Hover), a), S(10.0f), 0, 1.0f);
		}
		const ImVec4 TextColor = Solid ? ImVec4(1, 1, 1, 1) : Mix(t.m_Text, Accent, Hover);
		pDraw->AddText(pLabel, pLabel->FontSize, ImVec2(Pos.x + S(14.0f), Pos.y + (Size.y - pLabel->FontSize) * 0.5f), Rgba(TextColor, a), pText);
		if(Clicked)
			PlayUiSound(SOUND_CLICK);
		return std::make_pair(Clicked, Size.x);
	};

	// ---- the race: the time now, the best time on this map
	int RaceSeconds = -1;
	if(Snap.m_pGameInfoObj && Snap.m_pLocalInfo && Snap.m_pLocalInfo->m_Team != TEAM_SPECTATORS && Snap.m_pGameInfoObj->m_RoundStartTick > 0)
	{
		const int Ticks = Client()->GameTick(g_Config.m_ClDummy) - Snap.m_pGameInfoObj->m_RoundStartTick;
		if(Ticks >= 0)
			RaceSeconds = Ticks / Client()->GameTickSpeed();
	}
	char aBest[32] = "";
	if(LocalId >= 0 && GameClient()->m_ReceivedDDNetPlayerFinishTimes)
	{
		const auto &Me = GameClient()->m_aClients[LocalId];
		if(Me.m_FinishTimeSeconds != FinishTime::UNSET && Me.m_FinishTimeSeconds != FinishTime::NOT_FINISHED_MILLIS && Me.m_FinishTimeSeconds >= 0)
			FormatRaceTime(aBest, sizeof(aBest), Me.m_FinishTimeSeconds, std::clamp(Me.m_FinishTimeMillis / 10, 0, 99));
	}

	// ---- the centre block
	ImFont *pDisplay = Fonts().m_pDisplay ? Fonts().m_pDisplay : Fonts().m_pHeader;
	const char *pMap = Info.m_aMap[0] ? Info.m_aMap : Tr("Current map");
	const float MaxNameW = Avail.x * 0.8f;
	float NameSize = pDisplay->FontSize * std::clamp(Avail.x / S(1400.0f), 0.7f, 1.25f);
	{
		const float W = pDisplay->CalcTextSizeA(NameSize, FLT_MAX, 0.0f, pMap).x;
		if(W > MaxNameW)
			NameSize *= MaxNameW / W;
	}
	const float EyebrowH = pSmall->FontSize, StatsH = pSmall->FontSize, ResumeH = S(50.0f), IconSize = S(46.0f);
	const float BlockH = EyebrowH + S(14.0f) + NameSize + S(14.0f) + StatsH + S(26.0f) + ResumeH + S(18.0f) + IconSize;
	float y = Origin.y + std::max((Avail.y - BlockH) * 0.44f, 0.0f);
	const float Cx = Origin.x + Avail.x * 0.5f;

	// the server: a green dot, its name, the type, the ping
	{
		const float a = Part(0);
		char aLine[256];
		const int Ping = Snap.m_pLocalInfo ? Snap.m_pLocalInfo->m_Latency : 0;
		if(Info.m_aGameType[0])
			str_format(aLine, sizeof(aLine), "%s  ·  %s  ·  %d %s", Info.m_aName, Info.m_aGameType, Ping, Tr("ms"));
		else
			str_format(aLine, sizeof(aLine), "%s  ·  %d %s", Info.m_aName, Ping, Tr("ms"));
		const float W = pSmall->CalcTextSizeA(pSmall->FontSize, FLT_MAX, 0.0f, aLine).x + S(14.0f);
		const float x = Cx - W * 0.5f;
		pDraw->AddCircleFilled(ImVec2(x + S(3.5f), y + EyebrowH * 0.5f + Shift(a)), S(3.5f), Rgba(ImVec4(0.24f, 0.84f, 0.64f, 1.0f), a));
		pDraw->AddText(pSmall, pSmall->FontSize, ImVec2(x + S(14.0f), y + Shift(a)), Rgba(t.m_TextMuted, a), aLine);
		y += EyebrowH + S(14.0f);
	}
	// the map name: a click opens the card about it
	ImVec4 NameRect(0, 0, 0, 0);
	{
		const float a = Part(1);
		const bool CanOpen = g_Config.m_XcMenuMapInfo != 0;
		const char *pMore = Tr("about the map");
		const float NameW = pDisplay->CalcTextSizeA(NameSize, FLT_MAX, 0.0f, pMap).x;
		const float MoreW = CanOpen ? pSmall->CalcTextSizeA(pSmall->FontSize, FLT_MAX, 0.0f, pMore).x + S(26.0f) : 0.0f;
		const float Left = Cx - NameW * 0.5f;
		ImGui::SetCursorScreenPos(ImVec2(Left, y));
		const bool Clicked = ImGui::InvisibleButton("##gh_map", ImVec2(NameW + MoreW, NameSize)) && a > 0.5f;
		const float Hover = Animate(ImGui::GetItemID(), (ImGui::IsItemHovered() && CanOpen) ? 1.0f : 0.0f, 14.0f);
		NameRect = ImVec4(Left, y, Left + NameW + MoreW, y + NameSize);
		pDraw->AddText(pDisplay, NameSize, ImVec2(Left, y + Shift(a)), Rgba(Mix(t.m_Text, ImVec4(1, 1, 1, 1), Hover), a), pMap);
		if(CanOpen)
		{
			const ImVec2 MorePos(Left + NameW + S(12.0f) + Hover * S(3.0f), y + NameSize * 0.5f - pSmall->FontSize * 0.5f + Shift(a));
			pDraw->AddText(pSmall, pSmall->FontSize, MorePos, Rgba(Mix(t.m_TextMuted, t.m_Accent, Hover), a), pMore);
			DrawIcon(pDraw, pSmall, pSmall->FontSize * 0.8f, ImVec2(MorePos.x + MoreW - S(26.0f) + S(9.0f), MorePos.y + pSmall->FontSize * 0.5f), Rgba(Mix(t.m_TextMuted, t.m_Accent, Hover), a), XC_ICON_ARROW_RIGHT);
			if(Clicked)
			{
				PlayUiSound(SOUND_CLICK);
				m_GameMapCard = !m_GameMapCard;
				m_GamePopup = GAME_POPUP_NONE;
			}
		}
		y += NameSize + S(14.0f);
	}
	// race time, players, the best time
	{
		const float a = Part(2);
		char aaParts[3][96];
		int Num = 0;
		char aTime[32];
		if(RaceSeconds >= 0)
		{
			FormatRaceTime(aTime, sizeof(aTime), RaceSeconds, -1);
			str_format(aaParts[Num++], sizeof(aaParts[0]), "%s|%s", Tr("time"), aTime);
		}
		char aPlayers[32];
		str_format(aPlayers, sizeof(aPlayers), "%d / %d", Info.m_NumClients, Info.m_MaxClients);
		str_format(aaParts[Num++], sizeof(aaParts[0]), "%s|%s", Tr("players"), aPlayers);
		if(aBest[0])
			str_format(aaParts[Num++], sizeof(aaParts[0]), "%s|%s", Tr("my best"), aBest);
		// "label value" pairs: the label muted, the value bright; measured first to center the row
		const float Gap = S(22.0f), Space = S(5.0f);
		float Total = 0.0f;
		float aW[3][2];
		for(int i = 0; i < Num; i++)
		{
			const char *pBar = str_find(aaParts[i], "|");
			aW[i][0] = pSmall->CalcTextSizeA(pSmall->FontSize, FLT_MAX, 0.0f, aaParts[i], pBar).x;
			aW[i][1] = pLabel->CalcTextSizeA(pLabel->FontSize, FLT_MAX, 0.0f, pBar + 1).x;
			Total += aW[i][0] + Space + aW[i][1] + (i ? Gap : 0.0f);
		}
		float x = Cx - Total * 0.5f;
		for(int i = 0; i < Num; i++)
		{
			const char *pBar = str_find(aaParts[i], "|");
			pDraw->AddText(pSmall, pSmall->FontSize, ImVec2(x, y + Shift(a)), Rgba(t.m_TextMuted, a), aaParts[i], pBar);
			x += aW[i][0] + Space;
			pDraw->AddText(pLabel, pLabel->FontSize, ImVec2(x, y + Shift(a) - (pLabel->FontSize - pSmall->FontSize) * 0.5f), Rgba(t.m_Text, a), pBar + 1);
			x += aW[i][1] + Gap;
		}
		y += StatsH + S(26.0f);
	}
	// back to the game
	{
		const float a = Part(3);
		const char *pText = Tr("Back to the game");
		ImFont *pFont = Fonts().m_pTitle;
		const float FontSize = pFont->FontSize * 1.1f;
		const float W = std::max(S(240.0f), pFont->CalcTextSizeA(FontSize, FLT_MAX, 0.0f, pText).x + S(80.0f));
		ImGui::SetCursorScreenPos(ImVec2(Cx - W * 0.5f, y));
		const bool Clicked = ImGui::InvisibleButton("##gh_resume", ImVec2(W, ResumeH)) && a > 0.5f;
		const float Hover = Animate(ImGui::GetID("##gh_resume_h"), ImGui::IsItemHovered() ? 1.0f : 0.0f, 12.0f);
		const float Press = Animate(ImGui::GetID("##gh_resume_p"), ImGui::IsItemActive() ? 1.0f : 0.0f, 22.0f);
		const float Scale = 1.0f + 0.03f * Hover - 0.035f * Press;
		const ImVec2 C(Cx, y + ResumeH * 0.5f - Hover * S(2.0f) + Shift(a));
		const ImVec2 Half(W * 0.5f * Scale, ResumeH * 0.5f * Scale);
		if(Hover > 0.01f)
			XcSoftCircle(pDraw, ImVec2(C.x, C.y + S(8.0f)), W * 0.62f, ImVec4(t.m_Accent.x, t.m_Accent.y, t.m_Accent.z, 0.22f * Hover * a));
		pDraw->AddRectFilled(ImVec2(C.x - Half.x, C.y - Half.y), ImVec2(C.x + Half.x, C.y + Half.y), Rgba(Mix(t.m_Text, ImVec4(1, 1, 1, 1), Hover), a), S(14.0f) * Scale);
		const ImVec2 Dim = pFont->CalcTextSizeA(FontSize * Scale, FLT_MAX, 0.0f, pText);
		pDraw->AddText(pFont, FontSize * Scale, ImVec2(C.x - Dim.x * 0.5f, C.y - Dim.y * 0.5f), Rgba(ImVec4(0.06f, 0.07f, 0.1f, 1.0f), a), pText);
		if(Clicked)
		{
			PlayUiSound(SOUND_CLICK);
			SetActive(false);
		}
		y += ResumeH + S(18.0f);
	}
	// the icons; the vote one shows that a vote is running
	CVoting &Voting = GameClient()->m_Voting;
	float IconsBottom = y + IconSize;
	ImVec4 VoteIconRect(0, 0, 0, 0);
	{
		enum
		{
			ACT_SERVERS = 0,
			ACT_VISUALS,
			ACT_SETTINGS,
			ACT_DEMOS,
			ACT_VOTE,
		};
		struct SIcon
		{
			const char *m_pIcon;
			const char *m_pName;
			int m_Action;
		};
		const SIcon aIcons[] = {
			{XC_ICON_SERVER, Tr("Servers"), ACT_SERVERS},
			{XC_ICON_WAND, Tr("Visuals"), ACT_VISUALS},
			{XC_ICON_GEAR, Tr("Settings"), ACT_SETTINGS},
			{XC_ICON_FILM, Tr("Demos"), ACT_DEMOS},
			{XC_ICON_CHECK, Tr("Vote"), ACT_VOTE},
		};
		const int Num = (int)std::size(aIcons);
		const float Gap = S(10.0f);
		const float RowW = Num * IconSize + (Num - 1) * Gap;
		const float a = Part(4);
		for(int i = 0; i < Num; i++)
		{
			const float x = Cx - RowW * 0.5f + i * (IconSize + Gap);
			ImGui::SetCursorScreenPos(ImVec2(x, y));
			ImGui::PushID(i);
			const bool Clicked = ImGui::InvisibleButton("##gh_icon", ImVec2(IconSize, IconSize)) && a > 0.5f;
			const bool Selected = aIcons[i].m_Action == ACT_VOTE && m_GamePopup == GAME_POPUP_VOTE;
			const float Hover = Animate(ImGui::GetID("##h"), (ImGui::IsItemHovered() || Selected) ? 1.0f : 0.0f, 12.0f);
			const float Press = Animate(ImGui::GetID("##p"), ImGui::IsItemActive() ? 1.0f : 0.0f, 22.0f);
			const bool ShowName = ImGui::IsItemHovered();
			const float NameIn = Animate(ImGui::GetID("##n"), ShowName ? 1.0f : 0.0f, 12.0f);
			ImGui::PopID();
			const float Each = EaseOut((Time - 0.24f - 0.05f * i) / 0.7f) * (1.0f - Out);
			const float Half = IconSize * 0.5f * (1.0f - 0.06f * Press);
			const ImVec2 C(x + IconSize * 0.5f, y + IconSize * 0.5f - Hover * S(3.0f) + Shift(Each));
			pDraw->AddRectFilled(ImVec2(C.x - Half, C.y - Half), ImVec2(C.x + Half, C.y + Half), Rgba(ImVec4(1, 1, 1, 0.05f + 0.06f * Hover), Each), S(13.0f));
			pDraw->AddRect(ImVec2(C.x - Half, C.y - Half), ImVec2(C.x + Half, C.y + Half), Rgba(ImVec4(1, 1, 1, 0.07f + 0.07f * Hover), Each), S(13.0f), 0, 1.0f);
			DrawIcon(pDraw, Fonts().m_pIcons, Fonts().m_pIcons->FontSize, C, Rgba(Mix(t.m_TextSecondary, t.m_Text, Hover), Each), aIcons[i].m_pIcon);
			if(aIcons[i].m_Action == ACT_VOTE)
			{
				VoteIconRect = ImVec4(C.x - Half, C.y - Half, C.x + Half, C.y + Half);
				// a running vote: a pulsing accent dot on the corner
				if(Voting.IsVoting())
				{
					const float Pulse = 0.75f + 0.25f * std::sin(Client()->GlobalTime() * 4.0f);
					const ImVec2 Dot(C.x + Half - S(3.0f), C.y - Half + S(3.0f));
					XcSoftCircle(pDraw, Dot, S(12.0f), ImVec4(t.m_Accent.x, t.m_Accent.y, t.m_Accent.z, 0.6f * Each * Pulse));
					pDraw->AddCircleFilled(Dot, S(5.0f), Rgba(t.m_Accent, Each));
				}
			}
			if(NameIn > 0.01f && m_GamePopup == GAME_POPUP_NONE)
			{
				const ImVec2 Dim = pSmall->CalcTextSizeA(pSmall->FontSize, FLT_MAX, 0.0f, aIcons[i].m_pName);
				pDraw->AddText(pSmall, pSmall->FontSize, ImVec2(C.x - Dim.x * 0.5f, y + IconSize + S(8.0f) - (1.0f - NameIn) * S(4.0f)), Rgba(t.m_TextSecondary, NameIn * Each), aIcons[i].m_pName);
			}
			if(!Clicked)
				continue;
			switch(aIcons[i].m_Action)
			{
			case ACT_SERVERS: LeaveHome(PAGE_SERVERS); break;
			case ACT_VISUALS: LeaveHome(PAGE_VISUALS); break;
			case ACT_SETTINGS: LeaveHome(PAGE_SETTINGS); break;
			case ACT_DEMOS: LeaveHome(PAGE_DEMOS); break;
			default:
				// a running vote opens under the icons, otherwise the page to start one
				if(Voting.IsVoting())
				{
					PlayUiSound(SOUND_CLICK);
					m_GamePopup = m_GamePopup == GAME_POPUP_VOTE ? GAME_POPUP_NONE : GAME_POPUP_VOTE;
					m_GameMapCard = false;
				}
				else
					LeaveHome(PAGE_VOTES);
				break;
			}
		}
	}
	if(m_GamePopup == GAME_POPUP_VOTE && !Voting.IsVoting())
		m_GamePopup = GAME_POPUP_NONE;

	// ---- the vote under the icons
	{
		const float Open = EaseOut(Animate(ImGui::GetID("##gh_vote_open"), m_GamePopup == GAME_POPUP_VOTE ? 1.0f : 0.0f, 10.0f)) * (1.0f - Out);
		if(Open > 0.01f && Voting.IsVoting())
		{
			const float W = std::min(S(440.0f), Avail.x - S(32.0f)), H = S(148.0f);
			const ImVec2 P = Popup(ImVec2(Cx - W * 0.5f, IconsBottom + S(18.0f)), W, H, Open);
			const float Pad = S(14.0f);
			ImFont *pTitle = Fonts().m_pTitle;
			char aTitle[256];
			str_format(aTitle, sizeof(aTitle), "%s: %s", Tr("Vote"), Voting.VoteDescription());
			pDraw->PushClipRect(ImVec2(P.x + Pad, P.y), ImVec2(P.x + W - Pad, P.y + H), true);
			pDraw->AddText(pTitle, pTitle->FontSize, ImVec2(P.x + Pad, P.y + Pad), Rgba(t.m_Text, Open), aTitle);
			pDraw->PopClipRect();
			// the yes / no bar
			const int Yes = Voting.XcYes(), No = Voting.XcNo(), Total = std::max(Yes + No, 1);
			const float BarY = P.y + Pad + pTitle->FontSize + S(14.0f);
			char aCount[96];
			str_format(aCount, sizeof(aCount), "%d %s · %d %s · %d %s", Yes, Tr("yes"), No, Tr("no"), Voting.SecondsLeft(), Tr("s"));
			const float CountW = pSmall->CalcTextSizeA(pSmall->FontSize, FLT_MAX, 0.0f, aCount).x;
			const float BarW = W - Pad * 3.0f - CountW;
			const float YesW = BarW * Yes / (float)Total * std::min(1.0f, (Yes + No) / (float)std::max(Total, 1));
			pDraw->AddRectFilled(ImVec2(P.x + Pad, BarY), ImVec2(P.x + Pad + BarW, BarY + S(6.0f)), Rgba(ImVec4(1, 1, 1, 0.08f), Open), S(3.0f));
			if(Yes > 0)
				pDraw->AddRectFilled(ImVec2(P.x + Pad, BarY), ImVec2(P.x + Pad + YesW, BarY + S(6.0f)), Rgba(ImVec4(0.24f, 0.84f, 0.64f, 1.0f), Open), S(3.0f));
			if(No > 0)
				pDraw->AddRectFilled(ImVec2(P.x + Pad + YesW, BarY), ImVec2(P.x + Pad + YesW + BarW * No / (float)Total, BarY + S(6.0f)), Rgba(ImVec4(1.0f, 0.30f, 0.30f, 1.0f), Open), S(3.0f));
			pDraw->AddText(pSmall, pSmall->FontSize, ImVec2(P.x + W - Pad - CountW, BarY + S(3.0f) - pSmall->FontSize * 0.5f), Rgba(t.m_TextMuted, Open), aCount);
			// yes, no, and the page with all the votes
			const float ButtonsY = BarY + S(20.0f);
			const int Taken = Voting.TakenChoice();
			auto [YesClicked, YesW2] = SmallButton("##gh_vote_yes", ImVec2(P.x + Pad, ButtonsY), Tr("Yes"), ImVec4(0.24f, 0.84f, 0.64f, 1.0f), false, Taken == 1, Open);
			auto [NoClicked, NoW] = SmallButton("##gh_vote_no", ImVec2(P.x + Pad + YesW2 + S(8.0f), ButtonsY), Tr("No"), ImVec4(1.0f, 0.30f, 0.30f, 1.0f), false, Taken == -1, Open);
			(void)NoW;
			if(YesClicked)
				Voting.Vote(1);
			if(NoClicked)
				Voting.Vote(-1);
			const char *pAll = Tr("Start a vote");
			const float AllW = pLabel->CalcTextSizeA(pLabel->FontSize, FLT_MAX, 0.0f, pAll).x + S(28.0f);
			auto [AllClicked, AllW2] = SmallButton("##gh_vote_all", ImVec2(P.x + W - Pad - AllW, ButtonsY), pAll, t.m_Accent, false, false, Open);
			(void)AllW2;
			if(AllClicked)
				LeaveHome(PAGE_VOTES);
			// a click outside closes it
			if(MouseClicked && !ImGui::IsMouseHoveringRect(P, ImVec2(P.x + W, P.y + H)) && !ImGui::IsMouseHoveringRect(ImVec2(VoteIconRect.x, VoteIconRect.y), ImVec2(VoteIconRect.z, VoteIconRect.w)))
				m_GamePopup = GAME_POPUP_NONE;
		}
	}

	// ---- the top right corner: the dummy, leaving the server
	{
		const float a = Part(5);
		float Right = Origin.x + Avail.x;
		const float Top = Origin.y;
		auto [DiscClicked, DiscRect] = Pill("##gh_disconnect", ImVec2(Right, Top), XC_ICON_EXIT, Tr("Disconnect"), 1, a);
		if(DiscClicked)
		{
			m_GamePopup = m_GamePopup == GAME_POPUP_DISCONNECT ? GAME_POPUP_NONE : GAME_POPUP_DISCONNECT;
			m_GameMapCard = false;
		}
		Right = DiscRect.x - S(8.0f);
		const bool DummyOn = Client()->DummyConnected();
		const bool DummyBusy = Client()->DummyConnecting() || Client()->DummyConnectingDelayed();
		const char *pDummy = DummyBusy ? Tr("Connecting...") : DummyOn ? Tr("Disconnect the dummy") : Tr("Connect the dummy");
		auto [DummyClicked, DummyRect] = Pill("##gh_dummy", ImVec2(Right, Top), DummyOn ? "" : XC_ICON_USER_PLUS, pDummy, DummyOn ? 2 : 0, a);
		(void)DummyRect;
		if(DummyClicked && !DummyBusy)
		{
			if(DummyOn)
				Client()->DummyDisconnect(nullptr);
			else
				Client()->DummyConnect();
		}

		// "disconnect?" under the button, so a slip does not throw you off the server
		const float Open = EaseOut(Animate(ImGui::GetID("##gh_disc_open"), m_GamePopup == GAME_POPUP_DISCONNECT ? 1.0f : 0.0f, 12.0f)) * (1.0f - Out);
		if(Open > 0.01f)
		{
			const float W = S(310.0f), Pad = S(14.0f);
			const bool Racing = RaceSeconds > 0;
			const float H = Pad * 2.0f + Fonts().m_pTitle->FontSize + S(10.0f) + (Racing ? pSmall->FontSize + S(10.0f) : 0.0f) + S(34.0f);
			const ImVec2 P = Popup(ImVec2(DiscRect.z - W, DiscRect.w + S(10.0f)), W, H, Open);
			float Py = P.y + Pad;
			pDraw->AddText(Fonts().m_pTitle, Fonts().m_pTitle->FontSize, ImVec2(P.x + Pad, Py), Rgba(t.m_Text, Open), Tr("Disconnect from the server?"));
			Py += Fonts().m_pTitle->FontSize + S(10.0f);
			if(Racing)
			{
				char aRun[128], aTime[32];
				FormatRaceTime(aTime, sizeof(aTime), RaceSeconds, -1);
				str_format(aRun, sizeof(aRun), Tr("The run is not finished: %s will be lost."), aTime);
				pDraw->AddText(pSmall, pSmall->FontSize, ImVec2(P.x + Pad, Py), Rgba(t.m_TextMuted, Open), aRun);
				Py += pSmall->FontSize + S(10.0f);
			}
			auto [YesClicked, YesW] = SmallButton("##gh_disc_yes", ImVec2(P.x + Pad, Py), Tr("Disconnect"), ImVec4(1.0f, 0.30f, 0.30f, 1.0f), true, false, Open);
			auto [StayClicked, StayW] = SmallButton("##gh_disc_no", ImVec2(P.x + Pad + YesW + S(8.0f), Py), Tr("Stay"), t.m_Accent, false, false, Open);
			(void)StayW;
			if(StayClicked)
				m_GamePopup = GAME_POPUP_NONE;
			if(YesClicked && m_GamePopup == GAME_POPUP_DISCONNECT)
			{
				m_GamePopup = GAME_POPUP_NONE;
				SetActive(false);
				Client()->Disconnect();
				return;
			}
			if(MouseClicked && !ImGui::IsMouseHoveringRect(P, ImVec2(P.x + W, P.y + H)) && !ImGui::IsMouseHoveringRect(ImVec2(DiscRect.x, DiscRect.y), ImVec2(DiscRect.z, DiscRect.w)))
				m_GamePopup = GAME_POPUP_NONE;
		}
	}

	// ---- the bottom line: the track on the left, play / spectate on the right
	{
		const float a = Part(6) * 0.95f;
		const float Base = Origin.y + Avail.y;
		const CXcMusic &Music = GameClient()->m_XcMusic;
		if(g_Config.m_XcMusic && Music.HasTrack())
		{
			char aTrack[256];
			Music.TrackText(aTrack, sizeof(aTrack));
			const float Cover = S(28.0f);
			const ImVec2 CoverMin(Origin.x, Base - Cover);
			pDraw->AddRectFilled(CoverMin, ImVec2(CoverMin.x + Cover, CoverMin.y + Cover), Rgba(ImVec4(1, 1, 1, 0.06f), a), S(7.0f));
			DrawIcon(pDraw, pSmall, pSmall->FontSize, ImVec2(CoverMin.x + Cover * 0.5f, CoverMin.y + Cover * 0.5f), Rgba(t.m_TextMuted, a), XC_ICON_MUSIC);
			const float TextX = CoverMin.x + Cover + S(10.0f);
			const float MaxW = Avail.x * 0.45f;
			pDraw->PushClipRect(ImVec2(TextX, CoverMin.y), ImVec2(TextX + MaxW, Base), true);
			pDraw->AddText(pLabel, pLabel->FontSize, ImVec2(TextX, CoverMin.y + (Cover - pLabel->FontSize) * 0.5f), Rgba(t.m_TextSecondary, a), aTrack);
			pDraw->PopClipRect();
			// how far the track is
			const double Duration = Music.State().m_Duration;
			const double Position = Music.Position();
			if(Duration > 0.0 && Position >= 0.0)
			{
				const float TrackW = std::min(pLabel->CalcTextSizeA(pLabel->FontSize, FLT_MAX, 0.0f, aTrack).x, MaxW);
				const float BarX = TextX + TrackW + S(14.0f), BarW = S(90.0f), BarY = CoverMin.y + Cover * 0.5f - S(1.5f);
				pDraw->AddRectFilled(ImVec2(BarX, BarY), ImVec2(BarX + BarW, BarY + S(3.0f)), Rgba(ImVec4(1, 1, 1, 0.1f), a), S(1.5f));
				pDraw->AddRectFilled(ImVec2(BarX, BarY), ImVec2(BarX + BarW * (float)std::clamp(Position / Duration, 0.0, 1.0), BarY + S(3.0f)), Rgba(t.m_TextSecondary, a), S(1.5f));
			}
		}

		// play / spectate, like a segmented control with a sliding knob (not on team servers: red / blue)
		if(Snap.m_pLocalInfo && Snap.m_pGameInfoObj && !GameClient()->IsTeamPlay())
		{
			bool Paused = false, Spec = false;
			if(LocalId >= 0)
			{
				Paused = GameClient()->m_aClients[LocalId].m_Paused;
				Spec = GameClient()->m_aClients[LocalId].m_Spec;
			}
			const bool Watching = Snap.m_pLocalInfo->m_Team == TEAM_SPECTATORS || Paused || Spec;
			const char *apText[] = {Tr("I play"), Tr("I spectate")};
			const float Pad = S(3.0f), H = S(36.0f);
			float aW[2];
			for(int i = 0; i < 2; i++)
				aW[i] = pLabel->CalcTextSizeA(pLabel->FontSize, FLT_MAX, 0.0f, apText[i]).x + S(26.0f);
			const float W = aW[0] + aW[1] + Pad * 2.0f;
			const ImVec2 Min(Origin.x + Avail.x - W, Base - H + Shift(a));
			pDraw->AddRectFilled(Min, ImVec2(Min.x + W, Min.y + H), Rgba(ImVec4(1, 1, 1, 0.05f), a), S(11.0f));
			pDraw->AddRect(Min, ImVec2(Min.x + W, Min.y + H), Rgba(ImVec4(1, 1, 1, 0.07f), a), S(11.0f), 0, 1.0f);
			const float Knob = Animate(ImGui::GetID("##gh_team_knob"), Watching ? 1.0f : 0.0f, 12.0f);
			const float KnobX = Min.x + Pad + aW[0] * Knob;
			const float KnobW = aW[0] + (aW[1] - aW[0]) * Knob;
			pDraw->AddRectFilled(ImVec2(KnobX, Min.y + Pad), ImVec2(KnobX + KnobW, Min.y + H - Pad), Rgba(t.m_Text, a), S(8.0f));
			float x = Min.x + Pad;
			const bool Busy = Client()->DummyConnecting();
			for(int i = 0; i < 2; i++)
			{
				ImGui::SetCursorScreenPos(ImVec2(x, Min.y + Pad));
				ImGui::PushID(i);
				const bool Clicked = ImGui::InvisibleButton("##gh_team", ImVec2(aW[i], H - Pad * 2.0f)) && a > 0.5f && !Busy;
				const float Hover = Animate(ImGui::GetItemID(), ImGui::IsItemHovered() ? 1.0f : 0.0f, 14.0f);
				ImGui::PopID();
				const float On = i == 1 ? Knob : 1.0f - Knob;
				const ImVec4 TextColor = Mix(Mix(t.m_TextMuted, t.m_Text, Hover), ImVec4(0.06f, 0.07f, 0.1f, 1.0f), On);
				pDraw->AddText(pLabel, pLabel->FontSize, ImVec2(x + S(13.0f), Min.y + (H - pLabel->FontSize) * 0.5f), Rgba(TextColor, a), apText[i]);
				if(Clicked && (i == 1) != Watching)
				{
					PlayUiSound(SOUND_CLICK);
					if(i == 1)
					{
						if(g_Config.m_ClDummy == 0 || Client()->DummyConnected())
							GameClient()->SendSwitchTeam(TEAM_SPECTATORS);
					}
					else if(Paused || Spec)
						Console()->ExecuteLine("say /pause", IConsole::CLIENT_ID_UNSPECIFIED);
					else
						GameClient()->SendSwitchTeam(TEAM_GAME);
					SetActive(false);
				}
				x += aW[i];
			}
		}
	}

	// ---- the map card slides in from the right
	{
		const float Open = EaseOut(Animate(ImGui::GetID("##gh_card_open"), m_GameMapCard ? 1.0f : 0.0f, 9.0f)) * (1.0f - Out);
		if(Open > 0.005f)
		{
			const float W = std::min(S(400.0f), Avail.x - S(32.0f));
			const float Top = Origin.y + S(56.0f), Bottom = Origin.y + Avail.y - S(54.0f);
			const ImVec2 Min(Origin.x + Avail.x - W + (1.0f - Open) * (W + S(40.0f)), Top);
			const ImVec2 Max(Min.x + W, Bottom);
			PanelRect(pDraw, Min, Max, S(18.0f), t.m_Panel);
			ImGui::SetCursorScreenPos(ImVec2(Min.x + S(12.0f), Min.y + S(12.0f)));
			ImGui::PushStyleVar(ImGuiStyleVar_Alpha, ImGui::GetStyle().Alpha * Open);
			ImGui::BeginChild("##gh_card", ImVec2(W - S(24.0f), Bottom - Top - S(24.0f)), ImGuiChildFlags_None, ImGuiWindowFlags_NoBackground);
			BeginCardGrid("##gh_card_grid", 1);
			RenderMapInfoCard();
			EndCardGrid();
			ImGui::EndChild();
			ImGui::PopStyleVar();
			if(MouseClicked && m_GameMapCard && Open > 0.9f && !ImGui::IsMouseHoveringRect(Min, Max))
			{
				// a click on the map name toggles it itself
				if(!ImGui::IsMouseHoveringRect(ImVec2(NameRect.x, NameRect.y), ImVec2(NameRect.z, NameRect.w)))
					m_GameMapCard = false;
			}
		}
	}

	ImGui::SetCursorScreenPos(Origin);
	ImGui::Dummy(Avail);
}

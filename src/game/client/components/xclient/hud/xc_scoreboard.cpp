// The XClient scoreboard (Tab) and the small top list in the HUD, drawn with ImGui in the HUD context.
// Only shows what the classic scoreboard shows: players, teams, times and pings.
#include "xc_hud.h"

#include <base/math.h>
#include <base/str.h>

#include <engine/client.h>
#include <engine/external/imgui/imgui_internal.h>
#include <engine/graphics.h>
#include <engine/shared/config.h>
#include <engine/shared/protocol.h>

#include <generated/protocol.h>
#include <generated/protocol7.h>

#include <game/client/animstate.h>
#include <game/client/components/xclient/ui/icons.h>
#include <game/client/components/xclient/ui/lang.h>
#include <game/client/components/xclient/ui/theme.h>
#include <game/client/gameclient.h>

#include <algorithm>
#include <vector>

using namespace XcUi;

namespace
{
struct STeeDraw
{
	CXcHud *m_pHud;
	int m_ClientId;
	float m_aPos[2];
	float m_Size;
	float m_Alpha;
};

void FormatMillis(int Millis, bool Centis, char *pBuf, int BufSize)
{
	const int Seconds = Millis / 1000;
	const int Hours = Seconds / 3600;
	if(Hours > 0)
		str_format(pBuf, BufSize, "%d:%02d:%02d", Hours, Seconds / 60 % 60, Seconds % 60);
	else if(Centis)
		str_format(pBuf, BufSize, "%02d:%02d.%02d", Seconds / 60, Seconds % 60, Millis % 1000 / 10);
	else
		str_format(pBuf, BufSize, "%02d:%02d", Seconds / 60, Seconds % 60);
}

ImVec4 PingColor(int Latency)
{
	// green under 60 ms, yellow around 120, red from 200
	const float t = std::clamp((Latency - 40) / 160.0f, 0.0f, 1.0f);
	const ImVec4 Good(0.30f, 0.85f, 0.55f, 1.0f);
	const ImVec4 Mid(0.98f, 0.80f, 0.25f, 1.0f);
	const ImVec4 Bad(1.00f, 0.35f, 0.35f, 1.0f);
	return t < 0.5f ? Mix(Good, Mid, t * 2.0f) : Mix(Mid, Bad, (t - 0.5f) * 2.0f);
}

// text cut with "…" so it ends before MaxX
void DrawFit(ImDrawList *pDraw, ImFont *pFont, float Size, ImVec2 Pos, float MaxX, ImU32 Color, const char *pText)
{
	char aBuf[256];
	XcFitText(pFont, Size, pText, std::max(MaxX - Pos.x, 0.0f), aBuf, sizeof(aBuf));
	pDraw->AddText(pFont, Size, Pos, Color, aBuf);
}

ImVec4 ToVec4(ColorRGBA Color)
{
	return ImVec4(Color.r, Color.g, Color.b, Color.a);
}
} // namespace

bool CXcHud::DrawsScoreboard() const
{
	// with the mouse unlocked the classic scoreboard is needed: it has the player popups
	return g_Config.m_XcScoreboard && !GameClient()->m_Scoreboard.XcMouseUnlocked();
}

// race time of a player in milliseconds, -1 when not finished, -2 when the game counts points
int CXcHud::PlayerTimeMillis(const CNetObj_PlayerInfo *pInfo, bool *pCentis) const
{
	*pCentis = true;
	const CNetObj_GameInfo *pGameInfoObj = GameClient()->m_Snap.m_pGameInfoObj;
	const bool Race7 = Client()->IsSixup() && pGameInfoObj && pGameInfoObj->m_GameFlags & protocol7::GAMEFLAG_RACE;
	if(Race7)
		return pInfo->m_Score == protocol7::FinishTime::NOT_FINISHED ? -1 : std::abs(pInfo->m_Score);
	if(GameClient()->m_ReceivedDDNetPlayerFinishTimes)
	{
		const auto &Client = GameClient()->m_aClients[pInfo->m_ClientId];
		if(Client.m_FinishTimeSeconds == FinishTime::NOT_FINISHED_MILLIS || Client.m_FinishTimeSeconds == FinishTime::UNSET)
			return -1;
		return std::abs(Client.m_FinishTimeSeconds) * 1000 + std::max(Client.m_FinishTimeMillis, 0);
	}
	if(GameClient()->m_GameInfo.m_TimeScore)
	{
		*pCentis = false;
		return pInfo->m_Score == FinishTime::NOT_FINISHED_TIMESCORE ? -1 : std::abs(pInfo->m_Score) * 1000;
	}
	return -2;
}

void CXcHud::TeeCallback(const ImDrawList *pList, const ImDrawCmd *pCmd)
{
	(void)pList;
	const STeeDraw *pData = static_cast<const STeeDraw *>(pCmd->UserCallbackData);
	CXcHud *pHud = pData->m_pHud;
	const auto &Client = pHud->GameClient()->m_aClients[pData->m_ClientId];
	CTeeRenderInfo Info = Client.m_RenderInfo;
	Info.m_Size = pData->m_Size;
	const CAnimState *pIdle = CAnimState::GetIdle();
	vec2 OffsetToMid;
	CRenderTools::GetRenderTeeOffsetToRenderedTee(pIdle, &Info, OffsetToMid);
	pHud->RenderTools()->RenderTee(pIdle, &Info, Client.m_Afk ? EMOTE_BLINK : EMOTE_NORMAL, vec2(1.0f, 0.0f), vec2(pData->m_aPos[0], pData->m_aPos[1]) + OffsetToMid, pData->m_Alpha);
}

void CXcHud::AddTee(ImDrawList *pDraw, int ClientId, ImVec2 Center, float Size, float Alpha)
{
	const STeeDraw Data = {this, ClientId, {Center.x, Center.y}, Size, Alpha};
	pDraw->AddCallback(TeeCallback, (void *)&Data, sizeof(Data));
	pDraw->AddCallback(ImDrawCallback_ResetRenderState, nullptr);
}

void CXcHud::RenderScoreboard(ImDrawList *pDraw, float Dt)
{
	const bool Want = GameClient()->m_Scoreboard.IsActive() && DrawsScoreboard() && !GameClient()->m_XcMenu.IsActive() && !m_Editing;
	const float Speed = AnimationSpeed() > 0.0f ? 9.0f * AnimationSpeed() : 1000.0f;
	if(Want && m_ScoreboardOpen <= 0.0f)
		m_ScoreboardOpenTime = Client()->GlobalTime();
	m_ScoreboardOpen = Want ? std::min(m_ScoreboardOpen + Dt * Speed, 1.0f) : std::max(m_ScoreboardOpen - Dt * Speed, 0.0f);
	if(m_ScoreboardOpen <= 0.0f)
		return;
	const float Ease = 1.0f - (1.0f - m_ScoreboardOpen) * (1.0f - m_ScoreboardOpen) * (1.0f - m_ScoreboardOpen);
	const float Alpha = Ease;
	const float Since = Client()->GlobalTime() - m_ScoreboardOpenTime;

	const STheme &t = Theme();
	const float U = Graphics()->ScreenHeight() / 1080.0f * g_Config.m_XcScoreboardScale / 100.0f;
	ImFont *pTitle = m_Backend.Fonts().m_pTitle;
	ImFont *pLabel = m_Backend.Fonts().m_pLabel;
	ImFont *pSmall = m_Backend.Fonts().m_pSmall;

	// players in game order (by team, then score), spectators apart
	std::vector<const CNetObj_PlayerInfo *> vPlayers;
	std::vector<const CNetObj_PlayerInfo *> vSpectators;
	for(const CNetObj_PlayerInfo *pInfo : GameClient()->m_Snap.m_apInfoByDDTeamScore)
	{
		if(!pInfo)
			continue;
		if(pInfo->m_Team == TEAM_SPECTATORS)
			vSpectators.push_back(pInfo);
		else
			vPlayers.push_back(pInfo);
	}
	// ranks by finish time
	std::vector<std::pair<int, int>> vFinished; // time, client id
	for(const CNetObj_PlayerInfo *pInfo : vPlayers)
	{
		bool Centis;
		const int Time = PlayerTimeMillis(pInfo, &Centis);
		if(Time >= 0)
			vFinished.emplace_back(Time, pInfo->m_ClientId);
	}
	std::sort(vFinished.begin(), vFinished.end());
	int aRank[MAX_CLIENTS] = {};
	for(size_t i = 0; i < vFinished.size(); i++)
		aRank[vFinished[i].second] = i > 0 && vFinished[i].first == vFinished[i - 1].first ? aRank[vFinished[i - 1].second] : (int)i + 1;

	const int Num = (int)vPlayers.size();
	const int Columns = Num > 16 ? 2 : 1;
	const int PerColumn = std::max((Num + Columns - 1) / Columns, 1);
	const float RowHeight = (PerColumn <= 8 ? 42.0f : (PerColumn <= 16 ? 34.0f : 26.0f)) * U;
	const float RowGap = 3.0f * U;
	const float ColumnWidth = 660.0f * U;
	const float Pad = 18.0f * U;
	const float HeaderHeight = 70.0f * U;
	const float LabelsHeight = 24.0f * U;
	const float SpecHeight = vSpectators.empty() ? 0.0f : 34.0f * U;
	const float Width = Pad * 2.0f + ColumnWidth * Columns + (Columns - 1) * 16.0f * U;
	const float Height = Pad + HeaderHeight + LabelsHeight + PerColumn * (RowHeight + RowGap) + SpecHeight + Pad;
	const ImVec2 Display = ImGui::GetIO().DisplaySize;
	// grows from 96 % and slides down a little while it fades in
	const float Scale = 0.96f + 0.04f * Ease;
	const ImVec2 Center(Display.x * 0.5f, std::max(Display.y * 0.42f, Height * 0.5f + 20.0f * U) - (1.0f - Ease) * 14.0f * U);
	const ImVec2 Min(Center.x - Width * 0.5f * Scale, Center.y - Height * 0.5f * Scale);
	const ImVec2 Max(Center.x + Width * 0.5f * Scale, Center.y + Height * 0.5f * Scale);
	const float S = Scale; // everything below is placed in unscaled units and multiplied by S
	const auto P = [&](float x, float y) { return ImVec2(Min.x + x * S, Min.y + y * S); };

	// panel with a soft shadow and an accent line on top
	for(int i = 4; i >= 1; i--)
	{
		const float Grow = i * 4.0f * U;
		pDraw->AddRectFilled(ImVec2(Min.x - Grow, Min.y - Grow + 6.0f * U), ImVec2(Max.x + Grow, Max.y + Grow + 6.0f * U), Col(ImVec4(0, 0, 0, 1), 0.06f * Alpha), 18.0f * U + Grow);
	}
	pDraw->AddRectFilled(Min, Max, Col(ImVec4(t.m_Panel.x, t.m_Panel.y, t.m_Panel.z, 0.97f), Alpha), 18.0f * U);
	pDraw->AddRect(Min, Max, Col(t.m_Border, Alpha), 18.0f * U, 0, 1.0f);
	pDraw->AddRectFilledMultiColor(ImVec2(Min.x + 40.0f * U, Min.y), ImVec2(Max.x - 40.0f * U, Min.y + 2.0f * U), Col(t.m_Accent, 0.0f), Col(t.m_Accent, Alpha), Col(t.m_Accent, Alpha), Col(t.m_Accent, 0.0f));

	// header: server name, then map, players and game type
	const CServerInfo &ServerInfo = Client()->ServerInfo();
	const char *pServerName = Client()->State() == IClient::STATE_DEMOPLAYBACK ? "Demo" : ServerInfo.m_aName;
	pDraw->AddText(pTitle, 22.0f * U * S, P(Pad, Pad + 2.0f * U), Col(t.m_Text, Alpha), pServerName);
	char aSub[256];
	str_format(aSub, sizeof(aSub), XC_ICON_MAP "  %s      " XC_ICON_USERS "  %d / %d      " XC_ICON_SERVER "  %s", ServerInfo.m_aMap, GameClient()->m_Snap.m_NumPlayers, std::max(ServerInfo.m_MaxClients, GameClient()->m_Snap.m_NumPlayers), ServerInfo.m_aGameType);
	pDraw->AddText(pLabel, 14.0f * U * S, P(Pad, Pad + 36.0f * U), Col(t.m_TextSecondary, Alpha), aSub);
	// your place on the right
	const int Local = GameClient()->m_Snap.m_LocalClientId;
	if(Local >= 0 && aRank[Local] > 0)
	{
		char aPlace[64];
		str_format(aPlace, sizeof(aPlace), "#%d", aRank[Local]);
		const float PlaceSize = 30.0f * U * S;
		const ImVec2 PlaceDim = pTitle->CalcTextSizeA(PlaceSize, FLT_MAX, 0.0f, aPlace);
		pDraw->AddText(pTitle, PlaceSize, ImVec2(Max.x - Pad * S - PlaceDim.x, Min.y + Pad * S), Col(t.m_Accent, Alpha), aPlace);
		const char *pHint = Tr("your place");
		const ImVec2 HintDim = pSmall->CalcTextSizeA(12.0f * U * S, FLT_MAX, 0.0f, pHint);
		pDraw->AddText(pSmall, 12.0f * U * S, ImVec2(Max.x - Pad * S - HintDim.x, Min.y + (Pad + 40.0f * U) * S), Col(t.m_TextMuted, Alpha), pHint);
	}

	bool Centis;
	const bool TimeGame = vPlayers.empty() || PlayerTimeMillis(vPlayers[0], &Centis) != -2;
	for(int c = 0; c < Columns; c++)
	{
		const float x0 = Pad + c * (ColumnWidth + 16.0f * U);
		float y = Pad + HeaderHeight;
		// column names
		const float LabelSize = 12.0f * U * S;
		pDraw->AddText(pSmall, LabelSize, P(x0 + 12.0f * U, y), Col(t.m_TextMuted, Alpha), "#");
		pDraw->AddText(pSmall, LabelSize, P(x0 + 50.0f * U + RowHeight, y), Col(t.m_TextMuted, Alpha), Tr("Player"));
		const char *pScoreLabel = TimeGame ? Tr("Time") : Tr("Score");
		const ImVec2 ScoreLabelDim = pSmall->CalcTextSizeA(LabelSize, FLT_MAX, 0.0f, pScoreLabel);
		pDraw->AddText(pSmall, LabelSize, P(x0 + ColumnWidth - 86.0f * U - ScoreLabelDim.x / S, y), Col(t.m_TextMuted, Alpha), pScoreLabel);
		pDraw->AddText(pSmall, LabelSize, P(x0 + ColumnWidth - 62.0f * U, y), Col(t.m_TextMuted, Alpha), Tr("Ping"));
		y += LabelsHeight;

		for(int r = 0; r < PerColumn; r++)
		{
			const int Index = c * PerColumn + r;
			if(Index >= Num)
				break;
			const CNetObj_PlayerInfo *pInfo = vPlayers[Index];
			const int Id = pInfo->m_ClientId;
			const auto &ClientData = GameClient()->m_aClients[Id];
			// rows slide in one after another
			const float RowAnim = std::clamp((Since - Index * 0.018f) * 9.0f, 0.0f, 1.0f);
			const float RowEase = AnimationSpeed() > 0.0f ? 1.0f - (1.0f - RowAnim) * (1.0f - RowAnim) : 1.0f;
			const float RowAlpha = Alpha * RowEase;
			const float Slide = (1.0f - RowEase) * 18.0f * U;
			const ImVec2 RowMin = P(x0 + Slide, y);
			const ImVec2 RowMax = P(x0 + ColumnWidth + Slide, y + RowHeight);
			const float MidY = (RowMin.y + RowMax.y) * 0.5f;

			const bool Own = pInfo->m_Local || (GameClient()->m_Snap.m_SpecInfo.m_Active && GameClient()->m_Snap.m_SpecInfo.m_SpectatorId == Id);
			const int DDTeam = GameClient()->m_Teams.Team(Id);
			ImU32 Fill = Col(ImVec4(1, 1, 1, 1), (r % 2 ? 0.025f : 0.045f) * RowAlpha);
			if(Own)
				Fill = Col(t.m_Accent, 0.18f * RowAlpha);
			pDraw->AddRectFilled(RowMin, RowMax, Fill, 10.0f * U * S);
			if(Own)
				pDraw->AddRect(RowMin, RowMax, Col(t.m_Accent, 0.55f * RowAlpha), 10.0f * U * S, 0, 1.0f);
			// team color on the left edge, the same as in the classic scoreboard
			if(DDTeam != TEAM_FLOCK && DDTeam != TEAM_SUPER)
			{
				const ImVec4 TeamColor = ToVec4(GameClient()->GetDDTeamColor(DDTeam));
				pDraw->AddRectFilled(ImVec2(RowMin.x, RowMin.y + 6.0f * U), ImVec2(RowMin.x + 4.0f * U * S, RowMax.y - 6.0f * U), Col(TeamColor, RowAlpha), 2.0f * U);
			}

			// rank: medals for the first three
			const int Rank = aRank[Id];
			char aRank[16];
			if(Rank > 0)
				str_format(aRank, sizeof(aRank), "%d", Rank);
			else
				str_copy(aRank, "-");
			const float Radius = RowHeight * 0.32f * S;
			const ImVec2 RankCenter(RowMin.x + (14.0f * U + RowHeight * 0.32f) * S, MidY);
			static const ImVec4 s_aMedals[3] = {ImVec4(1.00f, 0.80f, 0.25f, 1.0f), ImVec4(0.78f, 0.80f, 0.85f, 1.0f), ImVec4(0.85f, 0.55f, 0.32f, 1.0f)};
			const bool Medal = Rank >= 1 && Rank <= 3;
			if(Medal)
				pDraw->AddCircleFilled(RankCenter, Radius, Col(s_aMedals[Rank - 1], 0.9f * RowAlpha), 24);
			const float RankSize = std::min(13.0f * U, RowHeight * 0.42f) * S;
			const ImVec2 RankDim = pLabel->CalcTextSizeA(RankSize, FLT_MAX, 0.0f, aRank);
			pDraw->AddText(pLabel, RankSize, ImVec2(RankCenter.x - RankDim.x * 0.5f, MidY - RankDim.y * 0.5f), Medal ? IM_COL32(20, 20, 24, (int)(255 * RowAlpha)) : Col(t.m_TextMuted, RowAlpha), aRank);

			// tee
			const float TeeSize = RowHeight * 0.95f * S;
			AddTee(pDraw, Id, ImVec2(RowMin.x + (42.0f * U + RowHeight * 0.5f) * S, MidY), TeeSize, RowAlpha * (ClientData.m_Afk ? 0.55f : 1.0f));

			// name, friend star, clan
			float NameX = RowMin.x + (50.0f * U + RowHeight) * S;
			const float NameSize = std::min(15.0f * U, RowHeight * 0.45f) * S;
			const ImVec4 NameColor = Own ? t.m_Accent : (ClientData.m_Afk ? t.m_TextMuted : t.m_Text);
			const float NameEnd = RowMax.x - 190.0f * U * S;
			const ImVec2 NameDim = pLabel->CalcTextSizeA(NameSize, FLT_MAX, 0.0f, ClientData.m_aName);
			DrawFit(pDraw, pLabel, NameSize, ImVec2(NameX, MidY - NameDim.y * 0.5f), NameEnd, Col(NameColor, RowAlpha), ClientData.m_aName);
			NameX += std::min(NameDim.x, NameEnd - NameX) + 8.0f * U * S;
			if(ClientData.m_Friend)
			{
				pDraw->AddText(pSmall, NameSize * 0.85f, ImVec2(NameX, MidY - NameSize * 0.45f), Col(ImVec4(1.0f, 0.80f, 0.25f, 1.0f), RowAlpha), XC_ICON_STAR);
				NameX += NameSize + 4.0f * U * S;
			}
			if(ClientData.m_Afk)
			{
				const ImVec2 AfkDim = pSmall->CalcTextSizeA(11.0f * U * S, FLT_MAX, 0.0f, "AFK");
				pDraw->AddRectFilled(ImVec2(NameX, MidY - AfkDim.y * 0.5f - 2.0f * U), ImVec2(NameX + AfkDim.x + 10.0f * U, MidY + AfkDim.y * 0.5f + 2.0f * U), Col(t.m_Track, RowAlpha), 6.0f * U);
				pDraw->AddText(pSmall, 11.0f * U * S, ImVec2(NameX + 5.0f * U, MidY - AfkDim.y * 0.5f), Col(t.m_TextMuted, RowAlpha), "AFK");
				NameX += AfkDim.x + 16.0f * U;
			}
			if(g_Config.m_XcScoreboardClan && ClientData.m_aClan[0] && NameX < NameEnd - 30.0f * U)
			{
				const float ClanSize = NameSize * 0.82f;
				DrawFit(pDraw, pSmall, ClanSize, ImVec2(NameX, MidY - ClanSize * 0.55f), NameEnd, Col(t.m_TextMuted, RowAlpha), ClientData.m_aClan);
			}

			// time or score, right aligned
			char aScore[32];
			bool RowCentis;
			const int Millis = PlayerTimeMillis(pInfo, &RowCentis);
			if(Millis == -2)
				str_format(aScore, sizeof(aScore), "%d", std::clamp(pInfo->m_Score, -999, 99999));
			else if(Millis >= 0)
				FormatMillis(Millis, RowCentis, aScore, sizeof(aScore));
			else
				aScore[0] = '\0';
			const ImVec2 ScoreDim = pLabel->CalcTextSizeA(NameSize, FLT_MAX, 0.0f, aScore);
			pDraw->AddText(pLabel, NameSize, ImVec2(RowMax.x - 86.0f * U * S - ScoreDim.x, MidY - ScoreDim.y * 0.5f), Col(Medal ? s_aMedals[Rank - 1] : t.m_TextSecondary, RowAlpha), aScore);

			// ping: colored dot and the number
			char aPing[16];
			str_format(aPing, sizeof(aPing), "%d", std::clamp(pInfo->m_Latency, 0, 999));
			const ImVec2 DotCenter(RowMax.x - 60.0f * U * S, MidY);
			pDraw->AddCircleFilled(DotCenter, 3.5f * U * S, Col(PingColor(pInfo->m_Latency), RowAlpha), 12);
			const float PingSize = NameSize * 0.85f;
			const ImVec2 PingDim = pSmall->CalcTextSizeA(PingSize, FLT_MAX, 0.0f, aPing);
			pDraw->AddText(pSmall, PingSize, ImVec2(DotCenter.x + 10.0f * U * S, MidY - PingDim.y * 0.5f), Col(t.m_TextSecondary, RowAlpha), aPing);

			y += RowHeight + RowGap;
		}
	}

	// spectators in one line at the bottom
	if(!vSpectators.empty())
	{
		char aSpecs[512];
		str_format(aSpecs, sizeof(aSpecs), XC_ICON_USERS "  %s (%d): ", Tr("Spectators"), (int)vSpectators.size());
		for(size_t i = 0; i < vSpectators.size(); i++)
		{
			if(i > 0)
				str_append(aSpecs, ", ");
			str_append(aSpecs, GameClient()->m_aClients[vSpectators[i]->m_ClientId].m_aName);
		}
		const float SpecY = Height - Pad - 22.0f * U;
		const float SpecEnd = Max.x - Pad * S;
		DrawFit(pDraw, pSmall, 13.0f * U * S, P(Pad, SpecY), SpecEnd, Col(t.m_TextSecondary, Alpha), aSpecs);
	}
}

void CXcHud::RenderMiniTop(ImDrawList *pDraw, float Alpha)
{
	const STheme &t = Theme();
	const float U = Unit(ELEMENT_MINITOP);
	ImFont *pLabel = m_Backend.Fonts().m_pLabel;
	ImFont *pSmall = m_Backend.Fonts().m_pSmall;
	const float TextSize = 14.5f * U;

	// best finished players on the server; points games show the highest scores
	std::vector<std::pair<int, int>> vEntries; // sort key, client id
	bool TimeGame = true;
	for(const CNetObj_PlayerInfo *pInfo : GameClient()->m_Snap.m_apInfoByDDTeamScore)
	{
		if(!pInfo || pInfo->m_Team == TEAM_SPECTATORS)
			continue;
		bool Centis;
		const int Millis = PlayerTimeMillis(pInfo, &Centis);
		if(Millis == -2)
		{
			TimeGame = false;
			vEntries.emplace_back(-pInfo->m_Score, pInfo->m_ClientId);
		}
		else if(Millis >= 0)
			vEntries.emplace_back(Millis, pInfo->m_ClientId);
	}
	std::sort(vEntries.begin(), vEntries.end());
	const int Count = std::min((int)vEntries.size(), g_Config.m_XcHudMinitopCount);
	const int Local = GameClient()->m_Snap.m_LocalClientId;
	int LocalPlace = -1;
	for(size_t i = 0; i < vEntries.size(); i++)
		if(vEntries[i].second == Local)
			LocalPlace = (int)i;
	const bool ShowLocal = LocalPlace >= Count;

	const float Pad = 12.0f * U;
	const float RowHeight = 25.0f * U;
	const float TitleHeight = 26.0f * U;
	const int Rows = std::max(Count, 1) + (ShowLocal ? 1 : 0);
	const float Width = 250.0f * U;
	const float Height = Pad + TitleHeight + Rows * RowHeight + Pad * 0.6f;
	const ImVec2 Min = PlaceRect(ELEMENT_MINITOP, ImVec2(Width, Height));
	const ImVec2 Max(Min.x + Width, Min.y + Height);
	RenderPlate(pDraw, Min, Max, U, Alpha);

	pDraw->AddText(pLabel, TextSize, ImVec2(Min.x + Pad, Min.y + Pad - 2.0f * U), Col(t.m_Accent, Alpha), XC_ICON_STAR);
	pDraw->AddText(pLabel, TextSize, ImVec2(Min.x + Pad + 20.0f * U, Min.y + Pad - 2.0f * U), Col(t.m_Text, Alpha), Tr("Top of the server"));

	float y = Min.y + Pad + TitleHeight;
	if(Count == 0)
	{
		pDraw->AddText(pSmall, 13.0f * U, ImVec2(Min.x + Pad, y + 2.0f * U), Col(t.m_TextMuted, Alpha), Tr("Nobody has finished yet"));
		return;
	}
	static const ImVec4 s_aMedals[3] = {ImVec4(1.00f, 0.80f, 0.25f, 1.0f), ImVec4(0.78f, 0.80f, 0.85f, 1.0f), ImVec4(0.85f, 0.55f, 0.32f, 1.0f)};
	const auto Row = [&](int Place, int Id) {
		const bool Own = Id == Local;
		if(Own)
			pDraw->AddRectFilled(ImVec2(Min.x + 6.0f * U, y), ImVec2(Max.x - 6.0f * U, y + RowHeight - 2.0f * U), Col(t.m_Accent, 0.16f * Alpha), 6.0f * U);
		char aPlace[16];
		str_format(aPlace, sizeof(aPlace), "%d", Place + 1);
		const ImVec2 PlaceCenter(Min.x + Pad + 7.0f * U, y + RowHeight * 0.5f - 1.0f * U);
		if(Place < 3)
			pDraw->AddCircleFilled(PlaceCenter, 8.0f * U, Col(s_aMedals[Place], 0.9f * Alpha), 20);
		const ImVec2 PlaceDim = pSmall->CalcTextSizeA(11.0f * U, FLT_MAX, 0.0f, aPlace);
		pDraw->AddText(pSmall, 11.0f * U, ImVec2(PlaceCenter.x - PlaceDim.x * 0.5f, PlaceCenter.y - PlaceDim.y * 0.5f), Place < 3 ? IM_COL32(20, 20, 24, (int)(255 * Alpha)) : Col(t.m_TextMuted, Alpha), aPlace);

		char aValue[32];
		bool Centis = true;
		const CNetObj_PlayerInfo *pInfo = GameClient()->m_Snap.m_apPlayerInfos[Id];
		const int Millis = pInfo ? PlayerTimeMillis(pInfo, &Centis) : -1;
		if(!TimeGame)
			str_format(aValue, sizeof(aValue), "%d", pInfo ? pInfo->m_Score : 0);
		else
			FormatMillis(std::max(Millis, 0), Centis, aValue, sizeof(aValue));
		const ImVec2 ValueDim = pSmall->CalcTextSizeA(13.5f * U, FLT_MAX, 0.0f, aValue);
		pDraw->AddText(pSmall, 13.5f * U, ImVec2(Max.x - Pad - ValueDim.x, y + (RowHeight - ValueDim.y) * 0.5f - 1.0f * U), Col(Place < 3 ? s_aMedals[Place] : t.m_TextSecondary, Alpha), aValue);
		const float NameX = Min.x + Pad + 22.0f * U;
		const float NameEnd = Max.x - Pad - ValueDim.x - 8.0f * U;
		DrawFit(pDraw, pLabel, 13.5f * U, ImVec2(NameX, y + (RowHeight - 13.5f * U) * 0.5f - 1.0f * U), NameEnd, Col(Own ? t.m_Accent : t.m_Text, Alpha), GameClient()->m_aClients[Id].m_aName);
		y += RowHeight;
	};
	for(int i = 0; i < Count; i++)
		Row(i, vEntries[i].second);
	if(ShowLocal)
		Row(LocalPlace, Local);
}

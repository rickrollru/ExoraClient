// The map card on Home while in game: what ddnet.org knows about the current map
// (mapper, difficulty, points, release, finishes and the best players).
#include "feedback.h"
#include "icons.h"
#include "lang.h"
#include "map_info.h"
#include "menu.h"
#include "theme.h"
#include "widgets.h"

#include <base/str.h>
#include <base/time.h>

#include <engine/client.h>
#include <engine/external/imgui/imgui.h>
#include <engine/external/imgui/imgui_internal.h>
#include <engine/external/json-parser/json.h>
#include <engine/http.h>
#include <engine/shared/config.h>

#include <game/client/gameclient.h>

#include <ctime>

using namespace XcUi;

void CXcMenu::UpdateMapInfo()
{
	const char *pMap = Client()->ServerInfo().m_aMap;
	if(!pMap[0])
		return;
	// ask once per map
	if(str_comp(m_aMapInfoName, pMap) != 0)
	{
		if(m_pMapTask)
			m_pMapTask->Abort();
		str_copy(m_aMapInfoName, pMap);
		char aEscaped[256];
		EscapeUrl(aEscaped, pMap);
		char aUrl[512];
		// the address comes from the config, only its %s is filled
		const char *pPlaceholder = str_find(g_Config.m_XcMapInfoUrl, "%s");
		if(pPlaceholder)
			str_format(aUrl, sizeof(aUrl), "%.*s%s%s", (int)(pPlaceholder - g_Config.m_XcMapInfoUrl), g_Config.m_XcMapInfoUrl, aEscaped, pPlaceholder + 2);
		else
			str_format(aUrl, sizeof(aUrl), "%s%s", g_Config.m_XcMapInfoUrl, aEscaped);
		m_pMapTask = HttpGet(aUrl);
		m_pMapTask->Timeout(CTimeout{10000, 0, 500, 10});
		m_pMapTask->MaxResponseSize(8 * 1024 * 1024);
		Http()->Run(m_pMapTask);
		m_MapInfoState = STATS_LOADING;
	}

	if(!m_pMapTask || !m_pMapTask->Done())
		return;
	std::shared_ptr<IHttpRequest> pTask = m_pMapTask;
	m_pMapTask = nullptr;
	if(pTask->State() != EHttpState::DONE)
	{
		m_MapInfoState = STATS_FAILED;
		return;
	}
	json_value *pJson = pTask->ResultJson();
	if(!pJson)
	{
		m_MapInfoState = STATS_UNKNOWN;
		return;
	}
	m_MapInfoState = ParseMapInfo(*pJson, m_MapInfo) ? STATS_LOADED : STATS_UNKNOWN;
	json_value_free(pJson);
}

// colors of the DDNet server types, like on ddnet.org
static ImVec4 MapTypeColor(const char *pType)
{
	if(str_comp_nocase(pType, "Novice") == 0)
		return ImVec4(0.30f, 0.85f, 0.55f, 1.0f);
	if(str_comp_nocase(pType, "Moderate") == 0)
		return ImVec4(0.98f, 0.80f, 0.25f, 1.0f);
	if(str_comp_nocase(pType, "Brutal") == 0)
		return ImVec4(1.00f, 0.54f, 0.24f, 1.0f);
	if(str_comp_nocase(pType, "Insane") == 0)
		return ImVec4(1.00f, 0.30f, 0.30f, 1.0f);
	if(str_comp_nocase(pType, "Dummy") == 0)
		return ImVec4(0.55f, 0.65f, 1.00f, 1.0f);
	if(str_comp_nocase(pType, "Solo") == 0)
		return ImVec4(0.75f, 0.55f, 1.00f, 1.0f);
	return ImVec4(0.75f, 0.78f, 0.82f, 1.0f);
}

void CXcMenu::RenderMapInfoCard()
{
	UpdateMapInfo();
	const STheme &t = Theme();
	const bool Loaded = m_MapInfoState == STATS_LOADED;
	const char *pMap = Client()->ServerInfo().m_aMap;
	char aSubtitle[160];
	if(Loaded && !m_MapInfo.m_Mapper.empty())
		str_format(aSubtitle, sizeof(aSubtitle), "%s %s", Tr("by"), m_MapInfo.m_Mapper.c_str());
	else
		str_copy(aSubtitle, Tr("Current map"));
	if(!BeginCard("##mapinfo", pMap, aSubtitle))
		return;

	ImDrawList *pDraw = ImGui::GetWindowDrawList();
	if(!Loaded)
	{
		Description(m_MapInfoState == STATS_LOADING ? Tr("Loading the map from ddnet.org...") : (m_MapInfoState == STATS_UNKNOWN ? Tr("ddnet.org does not know this map: it is new, custom or from another community") : Tr("Could not load the map from ddnet.org")));
		ParamLabel(Tr("Server"));
		ImGui::SameLine();
		ImGui::TextColored(t.m_Text, "%s", Client()->ServerInfo().m_aName);
		EndCard();
		return;
	}

	// type chip, difficulty stars and points in one row
	{
		ImFont *pLabel = Fonts().m_pLabel;
		const char *pType = m_MapInfo.m_Type.empty() ? "?" : m_MapInfo.m_Type.c_str();
		const ImVec4 TypeColor = MapTypeColor(pType);
		const ImVec2 Pos = ImGui::GetCursorScreenPos();
		const ImVec2 TypeSize = pLabel->CalcTextSizeA(pLabel->FontSize, FLT_MAX, 0.0f, pType);
		const float ChipHeight = pLabel->FontSize + S(10.0f);
		const ImVec2 ChipMax(Pos.x + TypeSize.x + S(20.0f), Pos.y + ChipHeight);
		pDraw->AddRectFilled(Pos, ChipMax, Col(TypeColor, 0.16f), S(6.0f));
		pDraw->AddRect(Pos, ChipMax, Col(TypeColor, 0.5f), S(6.0f));
		pDraw->AddText(pLabel, pLabel->FontSize, ImVec2(Pos.x + S(10.0f), Pos.y + S(5.0f)), Col(TypeColor), pType);

		// five stars, the filled ones in the accent color
		ImFont *pIcons = Fonts().m_pSmall;
		const float StarSize = pIcons->FontSize * 1.15f;
		float x = ChipMax.x + S(14.0f);
		for(int i = 0; i < 5; i++)
		{
			const bool Filled = i < m_MapInfo.m_Stars;
			pDraw->AddText(pIcons, StarSize, ImVec2(x, Pos.y + (ChipHeight - StarSize) * 0.5f), Filled ? Col(t.m_Accent) : Col(t.m_TextMuted, 0.35f), XC_ICON_STAR);
			x += StarSize + S(4.0f);
		}
		char aPoints[64];
		str_format(aPoints, sizeof(aPoints), "%s: %d", Tr("Points"), m_MapInfo.m_Points);
		pDraw->AddText(pLabel, pLabel->FontSize, ImVec2(x + S(10.0f), Pos.y + S(5.0f)), Col(t.m_TextSecondary), aPoints);
		ImGui::Dummy(ImVec2(AvailWidth(), ChipHeight + S(8.0f)));
	}

	// release, finishes, median time
	{
		char aaValues[3][64];
		if(m_MapInfo.m_Release > 0)
		{
			const time_t Release = (time_t)m_MapInfo.m_Release;
			struct tm Tm;
#if defined(CONF_FAMILY_WINDOWS)
			localtime_s(&Tm, &Release);
#else
			localtime_r(&Release, &Tm);
#endif
			strftime(aaValues[0], sizeof(aaValues[0]), "%d.%m.%Y", &Tm);
		}
		else
			str_copy(aaValues[0], "-");
		str_format(aaValues[1], sizeof(aaValues[1]), "%d", m_MapInfo.m_Finishes);
		if(m_MapInfo.m_MedianTime > 0.0f)
			FormatRaceTime(m_MapInfo.m_MedianTime, aaValues[2], sizeof(aaValues[2]));
		else
			str_copy(aaValues[2], "-");
		const char *apLabels[3] = {Tr("Released"), Tr("Finishes"), Tr("Median time")};
		const float Gap = S(8.0f);
		const float Width = (AvailWidth() - Gap * 2.0f) / 3.0f;
		const float Height = S(58.0f);
		const ImVec2 Start = ImGui::GetCursorScreenPos();
		ImFont *pValue = Fonts().m_pTitle;
		ImFont *pSmall = Fonts().m_pSmall;
		for(int i = 0; i < 3; i++)
		{
			const ImVec2 Min(Start.x + i * (Width + Gap), Start.y);
			const ImVec2 Max(Min.x + Width, Min.y + Height);
			pDraw->AddRectFilled(Min, Max, Col(t.m_Track, 0.6f), S(10.0f));
			pDraw->AddText(pSmall, pSmall->FontSize, ImVec2(Min.x + S(12.0f), Min.y + S(9.0f)), Col(t.m_TextMuted), apLabels[i]);
			pDraw->AddText(pValue, pValue->FontSize, ImVec2(Min.x + S(12.0f), Min.y + S(27.0f)), Col(t.m_Text), aaValues[i]);
		}
		ImGui::Dummy(ImVec2(AvailWidth(), Height + S(6.0f)));
	}

	// the best players: medals for the first three, your own name in the accent color
	const auto RenderRanks = [&](const std::vector<SMapInfo::SRank> &vRanks, const char *pTitle) {
		if(vRanks.empty())
			return;
		ParamLabel(pTitle);
		const ImVec4 aMedals[3] = {ImVec4(1.00f, 0.80f, 0.25f, 1.0f), ImVec4(0.78f, 0.80f, 0.85f, 1.0f), ImVec4(0.85f, 0.55f, 0.32f, 1.0f)};
		ImFont *pLabel = Fonts().m_pLabel;
		ImFont *pSmall = Fonts().m_pSmall;
		const float RowHeight = pLabel->FontSize + S(12.0f);
		for(const SMapInfo::SRank &Rank : vRanks)
		{
			const ImVec2 Pos = ImGui::GetCursorScreenPos();
			const float Width = AvailWidth();
			const bool Own = str_find_nocase(Rank.m_Name.c_str(), Client()->PlayerName()) != nullptr;
			if(Own)
				pDraw->AddRectFilled(Pos, ImVec2(Pos.x + Width, Pos.y + RowHeight), Col(t.m_Accent, 0.12f), S(8.0f));
			// rank badge
			char aRank[16];
			str_format(aRank, sizeof(aRank), "%d", Rank.m_Rank);
			const float Radius = RowHeight * 0.36f;
			const ImVec2 Center(Pos.x + S(6.0f) + Radius, Pos.y + RowHeight * 0.5f);
			const bool Medal = Rank.m_Rank >= 1 && Rank.m_Rank <= 3;
			pDraw->AddCircleFilled(Center, Radius, Medal ? Col(aMedals[Rank.m_Rank - 1], 0.9f) : Col(t.m_Track), 24);
			const ImVec2 RankSize = pSmall->CalcTextSizeA(pSmall->FontSize, FLT_MAX, 0.0f, aRank);
			pDraw->AddText(pSmall, pSmall->FontSize, ImVec2(Center.x - RankSize.x * 0.5f, Center.y - RankSize.y * 0.5f), Medal ? IM_COL32(20, 20, 24, 255) : Col(t.m_TextSecondary), aRank);
			// time on the right, name cut to the room left
			char aTime[32];
			FormatRaceTime(Rank.m_Time, aTime, sizeof(aTime));
			const ImVec2 TimeSize = pLabel->CalcTextSizeA(pLabel->FontSize, FLT_MAX, 0.0f, aTime);
			pDraw->AddText(pLabel, pLabel->FontSize, ImVec2(Pos.x + Width - TimeSize.x - S(8.0f), Pos.y + S(6.0f)), Col(t.m_TextSecondary), aTime);
			ImGui::PushFont(pLabel);
			const ImVec2 NamePos(Center.x + Radius + S(10.0f), Pos.y + S(6.0f));
			const float NameEnd = Pos.x + Width - TimeSize.x - S(20.0f);
			ImGui::PushStyleColor(ImGuiCol_Text, Own ? t.m_Accent : t.m_Text);
			ImGui::RenderTextEllipsis(pDraw, NamePos, ImVec2(NameEnd, NamePos.y + RowHeight), NameEnd, NameEnd, Rank.m_Name.c_str(), nullptr, nullptr);
			ImGui::PopStyleColor();
			ImGui::PopFont();
			ImGui::Dummy(ImVec2(Width, RowHeight + S(2.0f)));
		}
		ImGui::Dummy(ImVec2(0.0f, S(4.0f)));
	};
	RenderRanks(m_MapInfo.m_vRanks, Tr("Top players"));
	RenderRanks(m_MapInfo.m_vTeamRanks, Tr("Top teams"));

	if(Button(Tr("Open on ddnet.org"), false, -1.0f))
	{
		char aEscaped[256];
		EscapeUrl(aEscaped, m_MapInfo.m_Name.c_str());
		char aUrl[512];
		str_format(aUrl, sizeof(aUrl), "https://ddnet.org/maps/%s", aEscaped);
		Client()->ViewLink(aUrl);
	}
	EndCard();
}

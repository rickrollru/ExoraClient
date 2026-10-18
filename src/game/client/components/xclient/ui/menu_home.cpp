// Player statistics from ddnet.org on the home page: points, rank, finished maps.
#include "lang.h"
#include "menu.h"
#include "player_stats.h"
#include "theme.h"
#include "widgets.h"

#include <base/str.h>

#include <engine/client.h>
#include <engine/external/imgui/imgui.h>
#include <engine/external/json-parser/json.h>
#include <engine/http.h>
#include <engine/shared/config.h>

using namespace XcUi;

void CXcMenu::UpdatePlayerStats()
{
	// ask again when the name changes, or every 10 minutes while the page is open
	const char *pName = Client()->PlayerName();
	const bool NameChanged = str_comp(m_aStatsName, pName) != 0;
	if(NameChanged || (m_StatsState != STATS_LOADING && Client()->GlobalTime() - m_StatsTime > 600.0f))
	{
		if(m_pStatsTask)
			m_pStatsTask->Abort();
		str_copy(m_aStatsName, pName);
		char aEscaped[256];
		EscapeUrl(aEscaped, pName);
		char aUrl[512];
		// the address comes from the config, only its %s is filled
		const char *pPlaceholder = str_find(g_Config.m_XcStatsUrl, "%s");
		if(pPlaceholder)
			str_format(aUrl, sizeof(aUrl), "%.*s%s%s", (int)(pPlaceholder - g_Config.m_XcStatsUrl), g_Config.m_XcStatsUrl, aEscaped, pPlaceholder + 2);
		else
			str_format(aUrl, sizeof(aUrl), "%s%s", g_Config.m_XcStatsUrl, aEscaped);
		m_pStatsTask = HttpGet(aUrl);
		m_pStatsTask->Timeout(CTimeout{10000, 0, 500, 10});
		m_pStatsTask->MaxResponseSize(4 * 1024 * 1024);
		Http()->Run(m_pStatsTask);
		m_StatsState = STATS_LOADING;
		m_StatsTime = Client()->GlobalTime();
	}

	if(!m_pStatsTask || !m_pStatsTask->Done())
		return;
	std::shared_ptr<IHttpRequest> pTask = m_pStatsTask;
	m_pStatsTask = nullptr;
	if(pTask->State() != EHttpState::DONE)
	{
		m_StatsState = STATS_FAILED;
		return;
	}
	json_value *pJson = pTask->ResultJson();
	if(!pJson)
	{
		m_StatsState = STATS_FAILED;
		return;
	}
	if(!ParsePlayerStats(*pJson, m_Stats))
	{
		// an empty answer: nobody with this name has finished a map
		m_StatsState = STATS_UNKNOWN;
		json_value_free(pJson);
		return;
	}
	m_StatsState = STATS_LOADED;
	json_value_free(pJson);
}

void CXcMenu::RenderPlayerStats()
{
	UpdatePlayerStats();
	const STheme &t = Theme();
	if(m_StatsState != STATS_LOADED)
	{
		Description(m_StatsState == STATS_LOADING ? Tr("Loading stats from ddnet.org...") : (m_StatsState == STATS_UNKNOWN ? Tr("No finished maps on ddnet.org yet") : Tr("Could not load stats from ddnet.org")));
		return;
	}

	// three small tiles: points, rank, finished maps
	char aaValues[3][64];
	char aaHints[3][64];
	str_format(aaValues[0], sizeof(aaValues[0]), "%d", m_Stats.m_Points);
	str_format(aaHints[0], sizeof(aaHints[0]), "%s %d", Tr("of"), m_Stats.m_MaxPoints);
	str_format(aaValues[1], sizeof(aaValues[1]), "#%d", m_Stats.m_Rank);
	str_format(aaHints[1], sizeof(aaHints[1]), "%d %s", m_Stats.m_Hours, Tr("h this year"));
	str_format(aaValues[2], sizeof(aaValues[2]), "%d", m_Stats.m_Finished);
	str_format(aaHints[2], sizeof(aaHints[2]), "%s %d", Tr("of"), m_Stats.m_Maps);
	const char *apLabels[3] = {Tr("Points"), Tr("Rank"), Tr("Maps finished")};

	ImDrawList *pDraw = ImGui::GetWindowDrawList();
	const float Gap = S(8.0f);
	const float Width = (AvailWidth() - Gap * 2.0f) / 3.0f;
	const float Height = S(62.0f);
	const ImVec2 Start = ImGui::GetCursorScreenPos();
	ImFont *pValue = Fonts().m_pTitle;
	ImFont *pSmall = Fonts().m_pSmall;
	for(int i = 0; i < 3; i++)
	{
		const ImVec2 Min(Start.x + i * (Width + Gap), Start.y);
		const ImVec2 Max(Min.x + Width, Min.y + Height);
		pDraw->AddRectFilled(Min, Max, Col(t.m_Track, 0.6f), S(10.0f));
		pDraw->AddText(pSmall, pSmall->FontSize, ImVec2(Min.x + S(12.0f), Min.y + S(9.0f)), Col(t.m_TextMuted), apLabels[i]);
		pDraw->AddText(pValue, pValue->FontSize, ImVec2(Min.x + S(12.0f), Min.y + S(28.0f)), Col(i == 1 ? t.m_Accent : t.m_Text), aaValues[i]);
		const float ValueWidth = pValue->CalcTextSizeA(pValue->FontSize, FLT_MAX, 0.0f, aaValues[i]).x;
		pDraw->AddText(pSmall, pSmall->FontSize, ImVec2(Min.x + S(18.0f) + ValueWidth, Min.y + S(31.0f)), Col(t.m_TextMuted), aaHints[i]);
	}
	ImGui::Dummy(ImVec2(AvailWidth(), Height));
}

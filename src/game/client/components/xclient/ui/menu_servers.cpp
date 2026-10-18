#include "icons.h"
#include "lang.h"
#include "menu.h"
#include "theme.h"
#include "widgets.h"

#include <base/str.h>

#include <engine/client.h>
#include <engine/external/imgui/imgui.h>
#include <engine/external/imgui/imgui_internal.h>
#include <engine/favorites.h>
#include <engine/friends.h>
#include <engine/serverbrowser.h>

#include <game/client/gameclient.h>

#include <algorithm>

using namespace XcUi;

enum
{
	FILTER_ALL = 0,
	FILTER_NOVICE,
	FILTER_MODERATE,
	FILTER_BRUTAL,
	FILTER_INSANE,
	FILTER_FAVORITES,
	FILTER_LAN,
	NUM_FILTERS,
};

static ImVec4 PingColor(int Ping)
{
	if(Ping < 80)
		return ImVec4(0.24f, 0.84f, 0.64f, 1.0f); // mint
	if(Ping < 150)
		return ImVec4(1.0f, 0.78f, 0.3f, 1.0f);
	return ImVec4(1.0f, 0.3f, 0.3f, 1.0f);
}

void CXcMenu::UpdateServerList()
{
	m_vServers.clear();
	static const char *const s_apDifficulties[NUM_FILTERS] = {nullptr, "novice", "moderate", "brutal", "insane", nullptr, nullptr};
	IServerBrowser *pBrowser = ServerBrowser();
	for(int i = 0; i < pBrowser->NumServers(); i++)
	{
		const CServerInfo *pInfo = pBrowser->Get(i);
		if(!pInfo)
			continue;
		if(s_apDifficulties[m_ServerFilter] && !str_find_nocase(pInfo->m_aName, s_apDifficulties[m_ServerFilter]))
			continue;
		if(m_aServerSearch[0] && !str_find_nocase(pInfo->m_aName, m_aServerSearch) && !str_find_nocase(pInfo->m_aMap, m_aServerSearch))
			continue;
		m_vServers.push_back(pInfo);
	}
	// most players first, then the lowest ping
	std::stable_sort(m_vServers.begin(), m_vServers.end(), [](const CServerInfo *pA, const CServerInfo *pB) {
		if(pA->m_NumClients != pB->m_NumClients)
			return pA->m_NumClients > pB->m_NumClients;
		return pA->m_Latency < pB->m_Latency;
	});
}

bool CXcMenu::RenderServerCard(const CServerInfo *pInfo, float Width, float Height)
{
	const STheme &t = Theme();
	ImDrawList *pDraw = ImGui::GetWindowDrawList();
	const ImVec2 Min = ImGui::GetCursorScreenPos();
	const ImVec2 Max(Min.x + Width, Min.y + Height);
	ImGui::PushID(pInfo->m_aAddress);
	const bool Clicked = ImGui::InvisibleButton("##card", ImVec2(Width, Height));
	const bool Hovered = ImGui::IsItemHovered();
	const bool DoubleClicked = Hovered && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left);
	const ImGuiID Id = ImGui::GetItemID();
	ImGui::PopID();

	const bool Selected = m_SelectedServer == pInfo->m_aAddress;
	const float Hover = Animate(Id, Hovered ? 1.0f : 0.0f, 16.0f);
	const float Select = Animate(Id + 1, Selected ? 1.0f : 0.0f, 16.0f);
	pDraw->AddRectFilled(Min, Max, Col(Mix(t.m_Card, t.m_CardHover, Hover)), S(12.0f));
	pDraw->AddRect(Min, Max, Col(Mix(t.m_Border, t.m_Accent, Select)), S(12.0f), 0, 1.0f + Select);

	const float Pad = S(14.0f);
	ImFont *pLabel = Fonts().m_pLabel;
	ImFont *pSmall = Fonts().m_pSmall;
	const ImVec4 ClipRect(Min.x + Pad, Min.y, Max.x - Pad, Max.y);
	// name, then the map below it
	pDraw->AddText(pLabel, pLabel->FontSize, ImVec2(Min.x + Pad, Min.y + Pad), Col(t.m_Text), pInfo->m_aName, nullptr, 0.0f, &ClipRect);
	char aMap[128];
	str_format(aMap, sizeof(aMap), "%s  %s", XC_ICON_MAP, pInfo->m_aMap);
	pDraw->AddText(pSmall, pSmall->FontSize, ImVec2(Min.x + Pad, Min.y + Pad + pLabel->FontSize + S(4.0f)), Col(t.m_TextMuted), aMap, nullptr, 0.0f, &ClipRect);

	// bottom row: players, ping, game type
	const float RowY = Max.y - Pad - pSmall->FontSize;
	char aPlayers[64];
	str_format(aPlayers, sizeof(aPlayers), "%s  %d/%d", XC_ICON_USERS, pInfo->m_NumClients, pInfo->m_MaxClients);
	pDraw->AddText(pSmall, pSmall->FontSize, ImVec2(Min.x + Pad, RowY), Col(pInfo->m_NumClients > 0 ? t.m_TextSecondary : t.m_TextMuted), aPlayers);
	const float PlayersWidth = pSmall->CalcTextSizeA(pSmall->FontSize, FLT_MAX, 0.0f, aPlayers).x;
	const float PingX = Min.x + Pad + PlayersWidth + S(16.0f);
	pDraw->AddCircleFilled(ImVec2(PingX + S(4.0f), RowY + pSmall->FontSize * 0.5f), S(3.5f), Col(PingColor(pInfo->m_Latency)), 12);
	char aPing[32];
	str_format(aPing, sizeof(aPing), "%d ms", pInfo->m_Latency);
	pDraw->AddText(pSmall, pSmall->FontSize, ImVec2(PingX + S(12.0f), RowY), Col(t.m_TextMuted), aPing);
	if(pInfo->m_aGameType[0])
	{
		const ImVec2 TypeSize = pSmall->CalcTextSizeA(pSmall->FontSize, FLT_MAX, 0.0f, pInfo->m_aGameType);
		const ImVec2 ChipMin(Max.x - Pad - TypeSize.x - S(12.0f), RowY - S(3.0f));
		pDraw->AddRectFilled(ChipMin, ImVec2(Max.x - Pad, RowY + pSmall->FontSize + S(3.0f)), Col(t.m_Track), S(6.0f));
		pDraw->AddText(pSmall, pSmall->FontSize, ImVec2(ChipMin.x + S(6.0f), RowY), Col(t.m_TextSecondary), pInfo->m_aGameType);
	}
	if(pInfo->m_Favorite == TRISTATE::ALL)
	{
		const ImVec2 StarSize = pSmall->CalcTextSizeA(pSmall->FontSize, FLT_MAX, 0.0f, XC_ICON_STAR);
		pDraw->AddText(pSmall, pSmall->FontSize, ImVec2(Max.x - Pad - StarSize.x, Min.y + Pad), Col(t.m_Accent), XC_ICON_STAR);
	}

	if(Clicked)
		m_SelectedServer = pInfo->m_aAddress;
	if(DoubleClicked)
		Client()->Connect(pInfo->m_aAddress);
	return Clicked;
}

void CXcMenu::RenderServerDetails(const CServerInfo *pInfo, float Height)
{
	const STheme &t = Theme();
	ImDrawList *pDraw = ImGui::GetWindowDrawList();
	const ImVec2 Min = ImGui::GetCursorScreenPos();
	const float Width = ImGui::GetContentRegionAvail().x;
	PanelRect(pDraw, Min, ImVec2(Min.x + Width, Min.y + Height), S(14.0f), t.m_Card);
	const float Pad = S(18.0f);
	ImGui::SetCursorScreenPos(ImVec2(Min.x + Pad, Min.y + Pad));
	ImGui::BeginChild("##server_details", ImVec2(Width - Pad * 2.0f, Height - Pad * 2.0f), ImGuiChildFlags_None, ImGuiWindowFlags_NoBackground);
	if(!pInfo)
	{
		Title(Tr("Server browser"));
		Description(Tr("Pick a server to see who is playing"));
		ImGui::EndChild();
		return;
	}

	// map as the big title, the server name below
	ImGui::PushFont(Fonts().m_pHeader);
	ImGui::PushTextWrapPos(0.0f);
	ImGui::TextColored(t.m_Text, "%s", pInfo->m_aMap);
	ImGui::PopTextWrapPos();
	ImGui::PopFont();
	Description(pInfo->m_aName);
	ImGui::Dummy(ImVec2(0.0f, S(4.0f)));

	const auto InfoRow = [&](const char *pLabel, const char *pValue) {
		RowLabel(pLabel, ImGui::CalcTextSize(pValue).x);
		ImGui::TextColored(t.m_Text, "%s", pValue);
	};
	char aBuf[128];
	str_format(aBuf, sizeof(aBuf), "%d/%d", pInfo->m_NumClients, pInfo->m_MaxClients);
	InfoRow(Tr("Players"), aBuf);
	str_format(aBuf, sizeof(aBuf), "%d ms", pInfo->m_Latency);
	InfoRow(Tr("Ping"), aBuf);
	InfoRow(Tr("Game type"), pInfo->m_aGameType);
	ImGui::Dummy(ImVec2(0.0f, S(4.0f)));

	if(Button(Tr("Join"), true, -1.0f))
		Client()->Connect(pInfo->m_aAddress);
	const bool Favorite = pInfo->m_Favorite == TRISTATE::ALL;
	if(Button(Favorite ? Tr("Remove from favorites") : Tr("Add to favorites"), false, -1.0f))
	{
		if(Favorite)
			Favorites()->Remove(pInfo->m_aAddresses, pInfo->m_NumAddresses);
		else
			Favorites()->Add(pInfo->m_aAddresses, pInfo->m_NumAddresses);
	}

	ImGui::Dummy(ImVec2(0.0f, S(6.0f)));
	Title(Tr("Players"));
	if(pInfo->m_vClients.empty())
		Description(Tr("Nobody is playing"));
	for(const CServerInfo::CClient &Client : pInfo->m_vClients)
	{
		ImGui::PushFont(Fonts().m_pLabel);
		ImGui::TextColored(Client.m_FriendState != IFriends::FRIEND_NO ? t.m_Accent : t.m_Text, "%s", Client.m_aName);
		ImGui::PopFont();
		if(Client.m_aClan[0])
		{
			ImGui::SameLine();
			ImGui::PushFont(Fonts().m_pSmall);
			ImGui::TextColored(t.m_TextMuted, "%s", Client.m_aClan);
			ImGui::PopFont();
		}
	}
	ImGui::EndChild();
}

void CXcMenu::RenderServers()
{
	const STheme &t = Theme();
	// favorites and LAN are separate lists in DDNet, the difficulty filters work on the internet list
	const int ListType = m_ServerFilter == FILTER_FAVORITES ? IServerBrowser::TYPE_FAVORITES : (m_ServerFilter == FILTER_LAN ? IServerBrowser::TYPE_LAN : IServerBrowser::TYPE_INTERNET);
	if(!m_ServersRequested || ServerBrowser()->GetCurrentType() != ListType)
	{
		m_ServersRequested = true;
		ServerBrowser()->Refresh(ListType);
	}

	// top row: search, difficulty filters, refresh
	const float Avail = ImGui::GetContentRegionAvail().x;
	const float SearchWidth = S(300.0f);
	const float RefreshWidth = S(34.0f);
	ImGui::BeginGroup();
	ImGui::PushItemWidth(SearchWidth);
	ImGui::BeginChild("##search", ImVec2(SearchWidth, S(36.0f)), ImGuiChildFlags_None, ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoScrollbar);
	InputText("##server_search", m_aServerSearch, sizeof(m_aServerSearch), Tr("Search by name or map"));
	ImGui::EndChild();
	ImGui::PopItemWidth();
	ImGui::SameLine(0.0f, S(12.0f));
	const char *apFilters[NUM_FILTERS] = {Tr("All"), "Novice", "Moderate", "Brutal", "Insane", Tr("Favorites"), "LAN"};
	ImGui::BeginChild("##filters", ImVec2(Avail - SearchWidth - RefreshWidth - S(24.0f), S(36.0f)), ImGuiChildFlags_None, ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoScrollbar);
	Segmented("##server_filter", &m_ServerFilter, apFilters, NUM_FILTERS);
	ImGui::EndChild();
	ImGui::SameLine(0.0f, S(12.0f));
	if(IconButton(XC_ICON_ROTATE, Tr("Refresh")))
		ServerBrowser()->Refresh(ListType, true);
	ImGui::EndGroup();
	ImGui::Dummy(ImVec2(0.0f, S(4.0f)));

	UpdateServerList();
	const CServerInfo *pSelected = nullptr;
	for(const CServerInfo *pInfo : m_vServers)
		if(m_SelectedServer == pInfo->m_aAddress)
			pSelected = pInfo;

	// cards on the left, details on the right
	const float DetailsWidth = S(340.0f);
	const ImVec2 Area = ImGui::GetContentRegionAvail();
	const float ListWidth = Area.x - DetailsWidth - S(16.0f);
	const ImVec2 ListPos = ImGui::GetCursorScreenPos();
	ImGui::BeginChild("##server_list", ImVec2(ListWidth, Area.y), ImGuiChildFlags_None, ImGuiWindowFlags_NoBackground);
	if(m_vServers.empty())
	{
		const bool Loading = ServerBrowser()->IsGettingServerlist() || ServerBrowser()->IsRefreshing();
		ImGui::Dummy(ImVec2(0.0f, S(40.0f)));
		Title(Loading ? Tr("Loading servers...") : Tr("No servers found"));
		Description(Loading ? Tr("Asking the master servers for the list") : Tr("Try another filter or refresh the list"));
	}
	else
	{
		const float Gap = S(12.0f);
		const int Columns = std::max(1, (int)((ListWidth + Gap) / (S(300.0f) + Gap)));
		const float CardWidth = (ImGui::GetContentRegionAvail().x - Gap * (Columns - 1)) / Columns;
		const float CardHeight = S(96.0f);
		const int Rows = ((int)m_vServers.size() + Columns - 1) / Columns;
		// only the visible rows are drawn, the list can hold thousands of servers
		ImGuiListClipper Clipper;
		Clipper.Begin(Rows, CardHeight + Gap);
		while(Clipper.Step())
		{
			for(int Row = Clipper.DisplayStart; Row < Clipper.DisplayEnd; Row++)
			{
				const ImVec2 RowPos = ImGui::GetCursorScreenPos();
				for(int Column = 0; Column < Columns; Column++)
				{
					const int Index = Row * Columns + Column;
					if(Index >= (int)m_vServers.size())
						break;
					ImGui::SetCursorScreenPos(ImVec2(RowPos.x + Column * (CardWidth + Gap), RowPos.y));
					RenderServerCard(m_vServers[Index], CardWidth, CardHeight);
				}
				ImGui::SetCursorScreenPos(ImVec2(RowPos.x, RowPos.y + CardHeight + Gap));
				ImGui::Dummy(ImVec2(0.0f, 0.0f));
			}
		}
	}
	ImGui::EndChild();
	(void)t;

	ImGui::SetCursorScreenPos(ImVec2(ListPos.x + ListWidth + S(16.0f), ListPos.y));
	ImGui::BeginChild("##details_area", ImVec2(DetailsWidth, Area.y), ImGuiChildFlags_None, ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoScrollbar);
	RenderServerDetails(pSelected, Area.y);
	ImGui::EndChild();
}

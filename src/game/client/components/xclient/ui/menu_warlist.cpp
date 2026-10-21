// Settings > War list: the TClient war list (players and clans in colored groups with a reason) in the style
// of the menu. The data and the file (tclient_warlist.cfg) stay TClient's, so its chat commands keep working.
#include "feedback.h"
#include "icons.h"
#include "lang.h"
#include "menu.h"
#include "theme.h"
#include "widgets.h"

#include <base/str.h>

#include <engine/external/imgui/imgui.h>
#include <engine/external/imgui/imgui_internal.h>
#include <engine/shared/config.h>

#include <game/client/components/tclient/warlist.h>
#include <game/client/gameclient.h>

#include <algorithm>

using namespace XcUi;

// the colors a group can take: the accents of the menu and a few more
static const ImVec4 s_aGroupColors[] = {
	ImVec4(1.0f, 0.30f, 0.30f, 1.0f), // red
	ImVec4(1.0f, 0.54f, 0.24f, 1.0f), // orange
	ImVec4(1.0f, 0.84f, 0.25f, 1.0f), // yellow
	ImVec4(0.71f, 0.91f, 0.24f, 1.0f), // lime
	ImVec4(0.24f, 0.84f, 0.64f, 1.0f), // mint
	ImVec4(0.31f, 0.55f, 1.0f, 1.0f), // blue
	ImVec4(0.62f, 0.45f, 1.0f, 1.0f), // violet
	ImVec4(1.0f, 0.36f, 0.54f, 1.0f), // pink
	ImVec4(0.95f, 0.95f, 0.96f, 1.0f), // white
};

static ImVec4 ToImVec4(ColorRGBA Color)
{
	return ImVec4(Color.r, Color.g, Color.b, 1.0f);
}

static ColorRGBA ToColor(const ImVec4 &Color)
{
	return ColorRGBA(Color.x, Color.y, Color.z, 1.0f);
}

static int ClosestGroupColor(ColorRGBA Color)
{
	int Best = 0;
	float BestDistance = 1e9f;
	for(int i = 0; i < (int)std::size(s_aGroupColors); i++)
	{
		const float dr = s_aGroupColors[i].x - Color.r, dg = s_aGroupColors[i].y - Color.g, db = s_aGroupColors[i].z - Color.b;
		const float Distance = dr * dr + dg * dg + db * db;
		if(Distance < BestDistance)
		{
			BestDistance = Distance;
			Best = i;
		}
	}
	return Best;
}

// the name of a group for people: the three built-in ones are translated
static const char *GroupLabel(const CWarType *pType)
{
	if(str_comp(pType->m_aWarName, "none") == 0)
		return Tr("No group");
	if(str_comp(pType->m_aWarName, "enemy") == 0)
		return Tr("Enemies");
	if(str_comp(pType->m_aWarName, "team") == 0)
		return Tr("Team");
	return pType->m_aWarName;
}

// a small round dot in the color of a group, as an item on the line
static void ColorDot(const ImVec4 &Color, float Radius)
{
	const ImVec2 Pos = ImGui::GetCursorScreenPos();
	const float LineHeight = ImGui::GetTextLineHeight();
	ImGui::GetWindowDrawList()->AddCircleFilled(ImVec2(Pos.x + Radius, Pos.y + LineHeight * 0.5f), Radius, Col(Color), 16);
	ImGui::Dummy(ImVec2(Radius * 2.0f, LineHeight));
}

void CXcMenu::RenderSettingsWarList()
{
	CWarList &WarList = GameClient()->m_WarList;
	const STheme &t = Theme();
	static char s_aSearch[64] = "";
	static int s_FilterGroup = -1; // -1 all
	static int s_EditIndex = -1; // the entry in the form, -1 a new one
	static int s_IsClan = 0;
	static char s_aName[MAX_NAME_LENGTH] = "";
	static char s_aReason[MAX_WARLIST_REASON_LENGTH] = "";
	static int s_FormGroup = 1; // enemy
	static char s_aNewGroup[MAX_WARLIST_TYPE_LENGTH] = "";
	static int s_NewGroupColor = 6;
	static int s_ColorGroup = -1; // the group whose color is being changed
	s_FormGroup = std::clamp(s_FormGroup, 0, (int)WarList.m_WarTypes.size() - 1);
	if(s_FilterGroup >= (int)WarList.m_WarTypes.size())
		s_FilterGroup = -1;
	if(s_EditIndex >= (int)WarList.m_vWarEntries.size())
		s_EditIndex = -1;

	const auto &&LoadForm = [&](int Index) {
		const CWarEntry &Entry = WarList.m_vWarEntries[Index];
		s_EditIndex = Index;
		s_IsClan = Entry.m_aName[0] == '\0';
		str_copy(s_aName, s_IsClan ? Entry.m_aClan : Entry.m_aName);
		str_copy(s_aReason, Entry.m_aReason);
		for(int g = 0; g < (int)WarList.m_WarTypes.size(); g++)
			if(WarList.m_WarTypes[g] == Entry.m_pWarType)
				s_FormGroup = g;
	};
	const auto &&ClearForm = [&]() {
		s_EditIndex = -1;
		s_aName[0] = '\0';
		s_aReason[0] = '\0';
	};

	BeginCardGrid("##warlist", 2);

	// ---- the list
	if(BeginCard("##war_list", Tr("War list"), Tr("Players and clans in colored groups: their names get the color of the group in the game, the chat and the scoreboard"), &g_Config.m_TcWarList))
	{
		InputText("##war_search", s_aSearch, sizeof(s_aSearch), Tr("Search by name, clan or reason"));
		ImGui::Dummy(ImVec2(0.0f, S(4.0f)));
		// group filter: all + every group
		std::vector<std::string> vLabels = {Tr("All")};
		for(const CWarType *pType : WarList.m_WarTypes)
			vLabels.emplace_back(GroupLabel(pType));
		std::vector<const char *> vpLabels;
		for(const std::string &Label : vLabels)
			vpLabels.push_back(Label.c_str());
		int Filter = s_FilterGroup + 1;
		if(ChoiceChips("##war_filter", &Filter, vpLabels.data(), (int)vpLabels.size()))
			s_FilterGroup = Filter - 1;
		ImGui::Dummy(ImVec2(0.0f, S(6.0f)));

		const float ListHeight = S(380.0f);
		BeginInnerScroll("##war_entries", ImVec2(AvailWidth(), ListHeight));
		ImDrawList *pDraw = ImGui::GetWindowDrawList();
		ImFont *pLabel = Fonts().m_pLabel;
		ImFont *pSmall = Fonts().m_pSmall;
		const float Width = ImGui::GetContentRegionAvail().x;
		const float RowHeight = S(46.0f);
		int Shown = 0;
		int RemoveIndex = -1;
		for(int i = 0; i < (int)WarList.m_vWarEntries.size(); i++)
		{
			const CWarEntry &Entry = WarList.m_vWarEntries[i];
			if(s_FilterGroup >= 0 && Entry.m_pWarType != WarList.m_WarTypes[s_FilterGroup])
				continue;
			if(s_aSearch[0] && !str_find_nocase(Entry.m_aName, s_aSearch) && !str_find_nocase(Entry.m_aClan, s_aSearch) && !str_find_nocase(Entry.m_aReason, s_aSearch))
				continue;
			Shown++;
			const bool IsClan = Entry.m_aName[0] == '\0';
			const ImVec2 Min = ImGui::GetCursorScreenPos();
			const ImVec2 Max(Min.x + Width, Min.y + RowHeight);
			ImGui::PushID(i);
			const bool Clicked = ImGui::InvisibleButton("##entry", ImVec2(Width, RowHeight));
			const bool Hovered = ImGui::IsItemHovered();
			ImGui::PopID();
			const ImVec4 GroupColor = ToImVec4(Entry.m_pWarType->m_Color);
			const bool Editing = s_EditIndex == i;
			if(Hovered || Editing)
				pDraw->AddRectFilled(Min, Max, Col(Editing ? ImVec4(t.m_Accent.x, t.m_Accent.y, t.m_Accent.z, 0.14f) : t.m_Track), S(8.0f));
			// a stripe in the group color on the left
			pDraw->AddRectFilled(ImVec2(Min.x + S(4.0f), Min.y + S(9.0f)), ImVec2(Min.x + S(8.0f), Max.y - S(9.0f)), Col(GroupColor), S(2.0f));
			const float x = Min.x + S(18.0f);
			const ImVec4 Clip(x, Min.y, Max.x - S(40.0f), Max.y);
			char aTitle[128];
			str_format(aTitle, sizeof(aTitle), "%s%s", IsClan ? Tr("Clan ") : "", IsClan ? Entry.m_aClan : Entry.m_aName);
			pDraw->AddText(pLabel, pLabel->FontSize, ImVec2(x, Min.y + S(6.0f)), Col(GroupColor), aTitle, nullptr, 0.0f, &Clip);
			char aSub[MAX_WARLIST_REASON_LENGTH + 64];
			if(Entry.m_aReason[0])
				str_format(aSub, sizeof(aSub), "%s · %s", GroupLabel(Entry.m_pWarType), Entry.m_aReason);
			else
				str_copy(aSub, GroupLabel(Entry.m_pWarType));
			pDraw->AddText(pSmall, pSmall->FontSize, ImVec2(x, Min.y + S(8.0f) + pLabel->FontSize), Col(t.m_TextMuted), aSub, nullptr, 0.0f, &Clip);
			bool OverCross = false;
			if(Hovered)
			{
				const ImVec2 CrossMin(Max.x - S(30.0f), Min.y + (RowHeight - S(22.0f)) * 0.5f);
				OverCross = ImGui::IsMouseHoveringRect(CrossMin, ImVec2(CrossMin.x + S(22.0f), CrossMin.y + S(22.0f)));
				pDraw->AddText(pSmall, pSmall->FontSize, ImVec2(CrossMin.x + S(5.0f), CrossMin.y + S(4.0f)), Col(OverCross ? ImVec4(1.0f, 0.36f, 0.36f, 1.0f) : t.m_TextMuted), XC_ICON_TRASH);
				ImGui::SetTooltip("%s", OverCross ? Tr("Remove") : Tr("Click to edit"));
			}
			if(Clicked && OverCross)
				RemoveIndex = i;
			else if(Clicked)
				LoadForm(i);
		}
		if(Shown == 0)
			Description(WarList.m_vWarEntries.empty() ? Tr("The list is empty. Add a player or a clan on the right, or with a click on a player of the server") : Tr("Nothing found"));
		EndInnerScroll();
		if(RemoveIndex >= 0)
		{
			WarList.RemoveWarEntry(&WarList.m_vWarEntries[RemoveIndex]);
			if(s_EditIndex == RemoveIndex)
				ClearForm();
			else if(s_EditIndex > RemoveIndex)
				s_EditIndex--;
			WarList.UpdateWarPlayers();
			PlayUiSound(SOUND_CLICK);
		}
		char aCount[64];
		str_format(aCount, sizeof(aCount), "%s: %d", Tr("Entries"), (int)WarList.m_vWarEntries.size());
		Description(aCount);
		EndCard();
	}

	// ---- the form
	if(BeginCard("##war_form", s_EditIndex >= 0 ? Tr("Edit the entry") : Tr("Add a player or clan"), Tr("A player matches by the name, a clan matches everyone with that clan")))
	{
		const char *apKinds[] = {Tr("Player"), Tr("Clan")};
		Segmented("##war_kind", &s_IsClan, apKinds, 2);
		ImGui::Dummy(ImVec2(0.0f, S(4.0f)));
		InputText("##war_name", s_aName, s_IsClan ? MAX_CLAN_LENGTH : MAX_NAME_LENGTH, s_IsClan ? Tr("Clan") : Tr("Name"));
		ImGui::Dummy(ImVec2(0.0f, S(4.0f)));
		InputText("##war_reason", s_aReason, sizeof(s_aReason), Tr("Reason (optional)"));
		ImGui::Dummy(ImVec2(0.0f, S(6.0f)));
		ParamLabel(Tr("Group"));
		std::vector<std::string> vGroups;
		for(const CWarType *pType : WarList.m_WarTypes)
			vGroups.emplace_back(GroupLabel(pType));
		std::vector<const char *> vpGroups;
		for(const std::string &Group : vGroups)
			vpGroups.push_back(Group.c_str());
		ChoiceChips("##war_group", &s_FormGroup, vpGroups.data(), (int)vpGroups.size());
		ImGui::Dummy(ImVec2(0.0f, S(8.0f)));
		const bool CanSave = s_aName[0] != '\0';
		const float Half = (AvailWidth() - S(8.0f)) * 0.5f;
		if(s_EditIndex >= 0)
		{
			if(Button(Tr("Cancel"), false, Half))
				ClearForm();
			ImGui::SameLine(0.0f, S(8.0f));
			if(Button(Tr("Save"), true, Half) && CanSave)
			{
				WarList.UpdateWarEntry(s_EditIndex, s_IsClan ? "" : s_aName, s_IsClan ? s_aName : "", s_aReason, WarList.m_WarTypes[s_FormGroup]);
				WarList.UpdateWarPlayers();
				Toast(Tr("Saved"));
				ClearForm();
			}
		}
		else if(Button(Tr("Add to the list"), true, -1.0f) && CanSave)
		{
			WarList.AddWarEntry(s_IsClan ? "" : s_aName, s_IsClan ? s_aName : "", s_aReason, WarList.m_WarTypes[s_FormGroup]->m_aWarName);
			WarList.UpdateWarPlayers();
			Toast(Tr("Added to the war list"));
			ClearForm();
		}
		Description(Tr("In the chat: .war name reason, .team name, .delwar name, .clanwar clan"));
		EndCard();
	}

	// ---- the players of the server
	if(BeginCard("##war_players", Tr("On this server"), Tr("Click a player to put the name or the clan into the form")))
	{
		int Shown = 0;
		if(IsOnline())
		{
			for(int ClientId = 0; ClientId < MAX_CLIENTS; ClientId++)
			{
				const auto &Client = GameClient()->m_aClients[ClientId];
				if(!Client.m_Active || ClientId == GameClient()->m_aLocalIds[0])
					continue;
				Shown++;
				ImGui::PushID(ClientId);
				const ImVec4 NameColor = WarList.GetAnyWar(ClientId) ? ToImVec4(WarList.GetPriorityColor(ClientId)) : t.m_Text;
				ColorDot(WarList.GetAnyWar(ClientId) ? NameColor : t.m_Track, S(4.0f));
				ImGui::SameLine(0.0f, S(8.0f));
				ImGui::PushStyleColor(ImGuiCol_Text, NameColor);
				if(ImGui::Selectable(Client.m_aName, false, 0, ImVec2(AvailWidth() * 0.55f, 0.0f)))
				{
					s_IsClan = 0;
					str_copy(s_aName, Client.m_aName);
					s_EditIndex = -1;
				}
				ImGui::PopStyleColor();
				if(Client.m_aClan[0])
				{
					ImGui::SameLine();
					ImGui::PushStyleColor(ImGuiCol_Text, t.m_TextMuted);
					if(ImGui::Selectable(Client.m_aClan, false, 0, ImVec2(AvailWidth(), 0.0f)))
					{
						s_IsClan = 1;
						str_copy(s_aName, Client.m_aClan);
						s_EditIndex = -1;
					}
					ImGui::PopStyleColor();
				}
				ImGui::PopID();
			}
		}
		if(Shown == 0)
			Description(IsOnline() ? Tr("Nobody else is here") : Tr("Join a server to see its players here"));
		EndCard();
	}

	// ---- the groups
	if(BeginCard("##war_groups", Tr("Groups"), Tr("Every group has its own color. Click a group to change its color")))
	{
		int RemoveGroup = -1;
		for(int g = 1; g < (int)WarList.m_WarTypes.size(); g++)
		{
			CWarType *pType = WarList.m_WarTypes[g];
			ImGui::PushID(g);
			ColorDot(ToImVec4(pType->m_Color), S(6.0f));
			ImGui::SameLine(0.0f, S(10.0f));
			int Count = 0;
			for(const CWarEntry &Entry : WarList.m_vWarEntries)
				Count += Entry.m_pWarType == pType;
			char aLabel[96];
			str_format(aLabel, sizeof(aLabel), "%s  (%d)", GroupLabel(pType), Count);
			if(ImGui::Selectable(aLabel, s_ColorGroup == g, 0, ImVec2(AvailWidth() - (pType->m_Removable ? S(30.0f) : 0.0f), 0.0f)))
				s_ColorGroup = s_ColorGroup == g ? -1 : g;
			if(pType->m_Removable)
			{
				ImGui::SameLine();
				ImGui::PushStyleColor(ImGuiCol_Text, t.m_TextMuted);
				if(ImGui::SmallButton(XC_ICON_TRASH))
					RemoveGroup = g;
				ImGui::PopStyleColor();
				if(ImGui::IsItemHovered())
					ImGui::SetTooltip("%s", Tr("Remove the group (its entries stay without a group)"));
			}
			if(s_ColorGroup == g)
			{
				int Color = ClosestGroupColor(pType->m_Color);
				if(ColorSwatches("##group_color", &Color, s_aGroupColors, (int)std::size(s_aGroupColors)))
				{
					pType->m_Color = ToColor(s_aGroupColors[Color]);
					WarList.UpdateWarPlayers();
				}
			}
			ImGui::PopID();
		}
		if(RemoveGroup >= 0)
		{
			WarList.RemoveWarType(WarList.m_WarTypes[RemoveGroup]->m_aWarName);
			WarList.UpdateWarPlayers();
			s_ColorGroup = -1;
		}
		ImGui::Dummy(ImVec2(0.0f, S(6.0f)));
		ParamLabel(Tr("New group"));
		InputText("##new_group", s_aNewGroup, sizeof(s_aNewGroup), Tr("Group name, for example: helpers"));
		ImGui::Dummy(ImVec2(0.0f, S(4.0f)));
		ColorSwatches("##new_group_color", &s_NewGroupColor, s_aGroupColors, (int)std::size(s_aGroupColors));
		ImGui::Dummy(ImVec2(0.0f, S(4.0f)));
		const bool Exists = s_aNewGroup[0] && WarList.FindWarType(s_aNewGroup) != WarList.m_pWarTypeNone;
		if(Button(Tr("Add group"), false, -1.0f) && s_aNewGroup[0] && !Exists && str_comp(s_aNewGroup, "none") != 0)
		{
			WarList.AddWarType(s_aNewGroup, ToColor(s_aGroupColors[s_NewGroupColor]));
			s_aNewGroup[0] = '\0';
		}
		if(Exists)
			Description(Tr("A group with this name already exists"));
		EndCard();
	}

	// ---- where it shows
	if(BeginCard("##war_show", Tr("Where to show"), Tr("The colors of the groups on names")))
	{
		ToggleRow(Tr("In the chat"), "tc_warlist_chat", &g_Config.m_TcWarListChat);
		ToggleRow(Tr("In the scoreboard"), "tc_warlist_scoreboard", &g_Config.m_TcWarListScoreboard);
		ToggleRow(Tr("In the spectator menu"), "tc_warlist_spectate", &g_Config.m_TcWarListSpectate);
		ToggleRow(Tr("Reason under the name"), "tc_warlist_reason", &g_Config.m_TcWarListReason);
		ToggleRow(Tr("Show the clan when the clan is in the list"), "tc_warlist_show_clan_if_war", &g_Config.m_TcWarListShowClan);
		ToggleRow(Tr("Allow the same player twice"), "tc_warlist_allow_duplicates", &g_Config.m_TcWarListAllowDuplicates);
		ToggleRow(Tr("Use the groups for the player indicator"), "tc_warlist_indicator", &g_Config.m_TcWarListIndicator);
		if(g_Config.m_TcWarListIndicator)
		{
			ToggleRow(Tr("Indicator in the colors of the groups"), "tc_warlist_indicator_colors", &g_Config.m_TcWarListIndicatorColors);
			ToggleRow(Tr("Indicator: all groups"), "tc_warlist_indicator_all", &g_Config.m_TcWarListIndicatorAll);
		}
		EndCard();
	}

	EndCardGrid();
}

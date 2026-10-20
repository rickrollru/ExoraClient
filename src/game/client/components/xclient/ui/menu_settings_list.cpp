// The full lists of settings (Settings > TClient, Settings > DDNet): a card per group, the right control for
// every variable (toggle, segmented, slider, color, text), search, "only changed" and a mark on changed ones.
#include "lang.h"
#include "menu.h"
#include "settings_list.h"
#include "theme.h"
#include "widgets.h"

#include <base/color.h>
#include <base/str.h>

#include <engine/external/imgui/imgui.h>
#include <engine/shared/config.h>

#include <algorithm>
#include <string>
#include <vector>

using namespace XcUi;
using namespace XcSettingsList;

namespace
{
	int GroupOf(const SList &List, const char *pName)
	{
		for(int i = 0; i < List.m_NumRules; i++)
		{
			if(str_startswith(pName, List.m_pRules[i].m_pPrefix))
				return List.m_pRules[i].m_Group;
		}
		return List.m_OtherGroup;
	}

	bool Hidden(const SList &List, const char *pName)
	{
		for(int i = 0; i < List.m_NumHidden; i++)
		{
			if(str_startswith(pName, List.m_ppHidden[i]))
				return true;
		}
		return false;
	}

	const SLabel *FindLabel(const SList &List, const char *pName)
	{
		for(int i = 0; i < List.m_NumLabels; i++)
		{
			if(str_comp(List.m_pLabels[i].m_pName, pName) == 0)
				return &List.m_pLabels[i];
		}
		return nullptr;
	}

	bool IsChanged(const SSetting &Setting)
	{
		switch(Setting.m_Type)
		{
		case TYPE_INT: return *(int *)Setting.m_pValue != (int)Setting.m_Default;
		case TYPE_COL: return *(unsigned *)Setting.m_pValue != Setting.m_Default;
		default: return str_comp((const char *)Setting.m_pValue, Setting.m_pDefaultStr) != 0;
		}
	}

	// splits "a|b|c" into the list, true when it has exactly Count items
	bool SplitOptions(const char *pOptions, std::vector<std::string> *pList, int Count)
	{
		pList->clear();
		if(!pOptions)
			return false;
		const char *pStart = pOptions;
		while(true)
		{
			const char *pBar = str_find(pStart, "|");
			pList->emplace_back(pStart, pBar ? pBar - pStart : str_length(pStart));
			if(!pBar)
				break;
			pStart = pBar + 1;
		}
		return (int)pList->size() == Count;
	}
} // namespace

// color with a swatch on the right, the picker opens in a popup
void CXcMenu::ConfigColorRow(const char *pLabel, unsigned *pValue, bool Alpha)
{
	ImGui::PushID(pValue);
	RowLabel(pLabel, S(44.0f));
	const ColorRGBA Rgba = color_cast<ColorRGBA>(ColorHSLA(*pValue, Alpha));
	float aColor[4] = {Rgba.r, Rgba.g, Rgba.b, Alpha ? Rgba.a : 1.0f};
	const ImVec2 Pos = ImGui::GetCursorScreenPos();
	const ImVec2 Size(S(44.0f), S(24.0f));
	if(ImGui::InvisibleButton("##swatch", Size))
		ImGui::OpenPopup("##picker");
	const bool Hovered = ImGui::IsItemHovered();
	ImDrawList *pDraw = ImGui::GetWindowDrawList();
	const STheme &t = Theme();
	pDraw->AddRectFilled(Pos, ImVec2(Pos.x + Size.x, Pos.y + Size.y), Col(ImVec4(aColor[0], aColor[1], aColor[2], 1.0f)), S(6.0f));
	pDraw->AddRect(Pos, ImVec2(Pos.x + Size.x, Pos.y + Size.y), Col(Hovered ? t.m_Accent : t.m_Border), S(6.0f), 0, Hovered ? 2.0f : 1.0f);
	if(ImGui::BeginPopup("##picker"))
	{
		ImGuiColorEditFlags Flags = ImGuiColorEditFlags_NoSidePreview | ImGuiColorEditFlags_PickerHueWheel | ImGuiColorEditFlags_DisplayHex;
		if(!Alpha)
			Flags |= ImGuiColorEditFlags_NoAlpha;
		ImGui::SetNextItemWidth(S(220.0f));
		if(Alpha ? ImGui::ColorPicker4("##pick", aColor, Flags | ImGuiColorEditFlags_AlphaBar) : ImGui::ColorPicker3("##pick", aColor, Flags))
		{
			const ColorHSLA Hsl = color_cast<ColorHSLA>(ColorRGBA(aColor[0], aColor[1], aColor[2], Alpha ? aColor[3] : 1.0f));
			*pValue = Hsl.Pack(Alpha);
		}
		ImGui::EndPopup();
	}
	ImGui::PopID();
}


void CXcMenu::RenderSettingsList(const SList &List, char *pSearch, int SearchSize, bool *pOnlyChanged)
{
	const STheme &t = Theme();
	const bool Russian = IsRussian();

	// search and filters on top
	const float ButtonWidth = S(190.0f);
	InputText("##list_search", pSearch, SearchSize, Tr(List.m_pSearchHint), std::max(AvailWidth() - ButtonWidth - S(10.0f), S(160.0f)));
	ImGui::SameLine(0.0f, S(10.0f));
	if(Button(Tr("Only changed"), *pOnlyChanged, ButtonWidth))
		*pOnlyChanged = !*pOnlyChanged;
	ImGui::Dummy(ImVec2(0.0f, S(6.0f)));

	// what fits the filters, per group
	std::vector<std::vector<int>> avGroups(List.m_NumGroups);
	for(int i = 0; i < List.m_NumSettings; i++)
	{
		const SSetting &Setting = List.m_pSettings[i];
		if(Hidden(List, Setting.m_pName) || !(Setting.m_Flags & CFGFLAG_CLIENT))
			continue;
		if(*pOnlyChanged && !IsChanged(Setting))
			continue;
		if(pSearch[0])
		{
			const SLabel *pLabel = FindLabel(List, Setting.m_pName);
			if(!str_find_nocase(Setting.m_pName, pSearch) && !str_find_nocase(Setting.m_pDesc, pSearch) &&
				!(pLabel && str_utf8_find_nocase(pLabel->m_pRu, pSearch)) && !str_utf8_find_nocase(Tr(List.m_pGroups[GroupOf(List, Setting.m_pName)].m_pTitle), pSearch))
				continue;
		}
		avGroups[GroupOf(List, Setting.m_pName)].push_back(i);
	}

	bool Any = false;
	BeginCardGrid(List.m_pId, 2);
	std::vector<std::string> vOptions;
	for(int Group = 0; Group < List.m_NumGroups; Group++)
	{
		if(avGroups[Group].empty())
			continue;
		Any = true;
		char aId[32];
		str_format(aId, sizeof(aId), "%s_group_%d", List.m_pId, Group);
		if(!BeginCard(aId, Tr(List.m_pGroups[Group].m_pTitle), Tr(List.m_pGroups[Group].m_pDesc)))
			continue;
		for(int Index : avGroups[Group])
		{
			const SSetting &Setting = List.m_pSettings[Index];
			const SLabel *pLabel = FindLabel(List, Setting.m_pName);
			const char *pText = Russian && pLabel ? pLabel->m_pRu : Setting.m_pDesc;
			if(!pText[0])
				pText = Setting.m_pName;
			ImGui::PushID(Setting.m_pName);
			const ImVec2 RowStart = ImGui::GetCursorScreenPos();
			switch(Setting.m_Type)
			{
			case TYPE_INT:
			{
				int *pValue = (int *)Setting.m_pValue;
				const int Count = Setting.m_Max - Setting.m_Min + 1;
				if(Setting.m_Min == 0 && Setting.m_Max == 1)
					ToggleRow(pText, Setting.m_pName, pValue);
				else if(pLabel && Count <= 5 && SplitOptions(Russian ? pLabel->m_pOptionsRu : pLabel->m_pOptionsEn, &vOptions, Count))
				{
					ParamLabel(pText);
					const char *apItems[5];
					int aValues[5];
					for(int i = 0; i < Count; i++)
					{
						apItems[i] = vOptions[i].c_str();
						aValues[i] = Setting.m_Min + i;
					}
					ConfigSegmented("##options", Setting.m_pName, pValue, apItems, Count, aValues);
				}
				else
					ConfigSlider(pText, Setting.m_pName, pValue, Setting.m_Min, Setting.m_Max);
				break;
			}
			case TYPE_COL:
				ConfigColorRow(pText, (unsigned *)Setting.m_pValue, (Setting.m_Flags & CFGFLAG_COLALPHA) != 0);
				break;
			default:
				ConfigText(pText, Setting.m_pName, (const char *)Setting.m_pValue, Setting.m_Length);
				break;
			}
			// changed settings get a small accent mark on the left, the console name shows on hover
			const ImVec2 RowEnd = ImGui::GetCursorScreenPos();
			if(IsChanged(Setting))
				ImGui::GetWindowDrawList()->AddCircleFilled(ImVec2(RowStart.x - S(8.0f), RowStart.y + S(9.0f)), S(2.5f), Col(t.m_Accent), 12);
			if(ImGui::IsMouseHoveringRect(RowStart, ImVec2(RowStart.x + AvailWidth(), RowEnd.y)) && ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows))
				ImGui::SetTooltip("%s\n%s", Setting.m_pName, Setting.m_pDesc);
			ImGui::PopID();
		}
		EndCard();
	}
	EndCardGrid();
	if(!Any)
		Description(*pOnlyChanged && !pSearch[0] ? Tr("Nothing is changed yet") : Tr("Nothing found"));
}

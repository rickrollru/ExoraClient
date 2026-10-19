// The docks under the map in the new style: the server settings of the map and the undo history. The actions
// are the classic ones (the same history records), only the panels are new.
#include "xeditor.h"

#include <base/str.h>

#include <engine/font_icons.h>
#include <engine/keys.h>

#include <game/client/components/xclient/ui/lang.h>
#include <game/client/components/xclient/ui/theme.h>
#include <game/client/components/xclient/ui/widgets.h>
#include <game/editor/editor.h>
#include <game/editor/editor_actions.h>

#include <engine/external/imgui/imgui.h>

#include <algorithm>

using namespace XcUi;

bool CXcEditorUi::BeginDock(const char *pId, int ExtraEditor, ImVec2 &Min, ImVec2 &Max)
{
	CEditor &E = *m_pEditor;
	const STheme &t = Theme();
	const float k = UiToPixels();
	Min = ImVec2(m_Dock.x * k, m_Dock.y * k);
	Max = ImVec2((m_Dock.x + m_Dock.w) * k, (m_Dock.y + m_Dock.h) * k);
	ImGui::SetNextWindowPos(Min);
	ImGui::SetNextWindowSize(ImVec2(Max.x - Min.x, Max.y - Min.y));
	ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(t.m_Panel.x, t.m_Panel.y, t.m_Panel.z, 1.0f));
	ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
	ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(S(14.0f), S(10.0f)));
	const bool Open = ImGui::Begin(pId, nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoScrollbar);
	ImGui::PopStyleVar(3);
	ImGui::PopStyleColor();

	// the top edge resizes the dock, like the classic drag bar
	ImDrawList *pDraw = ImGui::GetWindowDrawList();
	const ImVec2 Saved = ImGui::GetCursorScreenPos();
	ImGui::SetCursorScreenPos(ImVec2(Min.x, Min.y));
	ImGui::InvisibleButton("##resize", ImVec2(Max.x - Min.x, S(6.0f)));
	const bool Resize = ImGui::IsItemActive();
	if(ImGui::IsItemHovered() || Resize)
		ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeNS);
	if(Resize)
	{
		float &Split = E.m_aExtraEditorSplits[ExtraEditor];
		Split = std::clamp(Split - ImGui::GetIO().MouseDelta.y / k, 100.0f, E.Ui()->Screen()->h * 0.7f);
	}
	pDraw->AddLine(Min, ImVec2(Max.x, Min.y), Col(Resize ? t.m_Accent : t.m_Border), Resize ? 2.0f : 1.0f);
	ImGui::SetCursorScreenPos(Saved);
	return Open;
}

// a small square button with an icon
static bool DockIcon(const char *pId, const char *pIcon, const char *pTooltip, bool Enabled, bool Active = false)
{
	const STheme &t = Theme();
	const float Size = S(28.0f);
	const ImVec2 Min = ImGui::GetCursorScreenPos();
	ImGui::BeginDisabled(!Enabled);
	const bool Clicked = ImGui::InvisibleButton(pId, ImVec2(Size, Size));
	const bool Hovered = ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled);
	ImGui::EndDisabled();
	ImDrawList *pDraw = ImGui::GetWindowDrawList();
	if(Active)
		pDraw->AddRectFilled(Min, ImVec2(Min.x + Size, Min.y + Size), Col(t.m_Accent), S(7.0f));
	else if(Hovered && Enabled)
		pDraw->AddRectFilled(Min, ImVec2(Min.x + Size, Min.y + Size), Col(t.m_Track), S(7.0f));
	ImFont *pFont = Fonts().m_pSmall;
	const ImVec2 TextSize = pFont->CalcTextSizeA(pFont->FontSize, FLT_MAX, 0.0f, pIcon);
	pDraw->AddText(pFont, pFont->FontSize, ImVec2(Min.x + (Size - TextSize.x) * 0.5f, Min.y + (Size - TextSize.y) * 0.5f), Col(Active ? ImVec4(1, 1, 1, 1) : (Enabled ? t.m_TextSecondary : t.m_TextMuted), Enabled ? 1.0f : 0.5f), pIcon);
	if(Hovered && pTooltip)
		ImGui::SetTooltip("%s", pTooltip);
	return Clicked && Enabled;
}

void CXcEditorUi::RenderServerSettingsDock()
{
	CEditor &E = *m_pEditor;
	CEditorMap *pMap = E.Map();
	const STheme &t = Theme();
	auto &Context = pMap->m_MapSettingsCommandContext;
	std::vector<CEditorMapSetting> &vSettings = pMap->m_vSettings;
	int &Selected = Context.m_CommandSelectedIndex;
	CEditorHistory &History = pMap->m_ServerSettingsHistory;

	if(!m_SettingsDockOpen)
	{
		m_SettingsDockOpen = true;
		m_SettingFocus = true;
		str_copy(m_aSettingInput, Context.m_CommandInput.GetString());
	}
	ImVec2 Min, Max;
	if(!BeginDock("##xe_dock_settings", CEditor::EXTRAEDITOR_SERVER_SETTINGS, Min, Max))
	{
		ImGui::End();
		return;
	}
	ImDrawList *pDraw = ImGui::GetWindowDrawList();
	ImFont *pSmall = Fonts().m_pSmall;
	const float Inner = Max.x - Min.x - S(28.0f);

	// the input is the classic one underneath, so its checks, the collisions and the hints stay the same
	if(str_comp(Context.m_CommandInput.GetString(), m_aSettingInput) != 0)
	{
		Context.m_CommandInput.Set(m_aSettingInput);
		Context.Update();
	}
	const bool Valid = m_aSettingInput[0] != '\0' && Context.Valid();
	ECollisionCheckResult CheckResult = ECollisionCheckResult::ERROR;
	const int Colliding = Valid ? Context.CheckCollision(CheckResult) : -1;
	const bool GotSelection = Selected >= 0 && Selected < (int)vSettings.size();
	const bool CanAdd = Valid && CheckResult == ECollisionCheckResult::ADD;
	const bool CanReplace = Valid && CheckResult == ECollisionCheckResult::REPLACE;
	const bool CanUpdate = GotSelection && Valid && str_comp(vSettings[Selected].m_aCommand, m_aSettingInput) != 0;

	const auto AfterChange = [&]() {
		pMap->OnModify();
		m_aSettingInput[0] = '\0';
		Context.m_CommandInput.Clear();
		Context.Reset();
		m_SettingFocus = true;
	};

	// ---- title and tools
	Title(Tr("Server settings"));
	ImGui::SameLine();
	ImGui::SetCursorPosY(ImGui::GetCursorPosY() + S(4.0f));
	Description(Tr("Commands the server runs when this map loads"));

	const float ToolsWidth = S(28.0f) * 7 + S(4.0f) * 6;
	const float InputWidth = Inner - ToolsWidth - S(10.0f);
	if(m_SettingFocus)
	{
		ImGui::SetKeyboardFocusHere();
		m_SettingFocus = false;
	}
	if(InputText("##setting", m_aSettingInput, sizeof(m_aSettingInput), Tr("Command, for example sv_team 1"), InputWidth))
		Context.UpdateCursor(true);
	const bool InputActive = ImGui::IsItemActive();
	// Enter leaves the field in ImGui: that is the moment to add the command
	const bool Enter = ImGui::IsItemDeactivated() && (ImGui::IsKeyPressed(ImGuiKey_Enter) || ImGui::IsKeyPressed(ImGuiKey_KeypadEnter));
	if(InputActive || Enter)
		m_SettingTyping = true;
	else if(ImGui::IsMouseClicked(ImGuiMouseButton_Left) && !ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows))
		m_SettingTyping = false;
	ImGui::SameLine(0.0f, S(10.0f));
	const bool AltEnter = Enter && ImGui::GetIO().KeyAlt;

	// add or replace: Enter
	if(DockIcon("##add", CanReplace ? FontIcon::ARROWS_ROTATE : FontIcon::PLUS, CanReplace ? Tr("Replace the same command in the list (Enter)") : Tr("Add the command to the list (Enter)"), CanAdd || CanReplace) || (Enter && !AltEnter && (CanAdd || CanReplace)))
	{
		if(CanReplace && Colliding >= 0)
		{
			Selected = Colliding;
			History.RecordAction(std::make_shared<CEditorCommandAction>(pMap, CEditorCommandAction::EType::EDIT, &Selected, Selected, vSettings[Selected].m_aCommand, m_aSettingInput));
			str_copy(vSettings[Selected].m_aCommand, m_aSettingInput);
		}
		else
		{
			vSettings.emplace_back(m_aSettingInput);
			Selected = vSettings.size() - 1;
			History.RecordAction(std::make_shared<CEditorCommandAction>(pMap, CEditorCommandAction::EType::ADD, &Selected, Selected, vSettings[Selected].m_aCommand));
		}
		AfterChange();
	}
	ImGui::SameLine(0.0f, S(4.0f));
	// update the selected line: Alt+Enter (the same as the classic one, colliding lines are removed)
	if(DockIcon("##update", FontIcon::PENCIL, Tr("Change the selected command to this one (Alt+Enter)"), CanUpdate) || (AltEnter && CanUpdate))
	{
		if(Colliding == -1 || Colliding == Selected)
		{
			int Duplicate = -1;
			for(int i = 0; i < (int)vSettings.size(); i++)
				if(i != Selected && !str_comp(vSettings[i].m_aCommand, m_aSettingInput))
					Duplicate = i;
			if(Duplicate >= 0)
			{
				History.RecordAction(std::make_shared<CEditorCommandAction>(pMap, CEditorCommandAction::EType::DELETE, &Selected, Selected, vSettings[Selected].m_aCommand));
				vSettings.erase(vSettings.begin() + Selected);
				Selected = Duplicate > Selected ? Duplicate - 1 : Duplicate;
			}
			else
			{
				History.RecordAction(std::make_shared<CEditorCommandAction>(pMap, CEditorCommandAction::EType::EDIT, &Selected, Selected, vSettings[Selected].m_aCommand, m_aSettingInput));
				str_copy(vSettings[Selected].m_aCommand, m_aSettingInput);
			}
		}
		else
		{
			char aBuf[256];
			str_format(aBuf, sizeof(aBuf), "Delete command %d; Edit command %d", Colliding, Selected);
			History.BeginBulk();
			History.RecordAction(std::make_shared<CEditorCommandAction>(pMap, CEditorCommandAction::EType::DELETE, &Selected, Colliding, vSettings[Colliding].m_aCommand));
			vSettings.erase(vSettings.begin() + Colliding);
			Selected = Selected > Colliding ? Selected - 1 : Selected;
			History.RecordAction(std::make_shared<CEditorCommandAction>(pMap, CEditorCommandAction::EType::EDIT, &Selected, Selected, vSettings[Selected].m_aCommand, m_aSettingInput));
			str_copy(vSettings[Selected].m_aCommand, m_aSettingInput);
			History.EndBulk(aBuf);
		}
		AfterChange();
	}
	ImGui::SameLine(0.0f, S(4.0f));
	const bool CanUp = GotSelection && Selected > 0;
	if(DockIcon("##up", FontIcon::SORT_UP, Tr("Move up"), CanUp))
	{
		History.RecordAction(std::make_shared<CEditorCommandAction>(pMap, CEditorCommandAction::EType::MOVE_UP, &Selected, Selected));
		std::swap(vSettings[Selected], vSettings[Selected - 1]);
		Selected--;
		pMap->OnModify();
	}
	ImGui::SameLine(0.0f, S(4.0f));
	const bool CanDown = GotSelection && Selected < (int)vSettings.size() - 1;
	if(DockIcon("##down", FontIcon::SORT_DOWN, Tr("Move down"), CanDown))
	{
		History.RecordAction(std::make_shared<CEditorCommandAction>(pMap, CEditorCommandAction::EType::MOVE_DOWN, &Selected, Selected));
		std::swap(vSettings[Selected], vSettings[Selected + 1]);
		Selected++;
		pMap->OnModify();
	}
	ImGui::SameLine(0.0f, S(4.0f));
	const bool ListFocused = ImGui::IsWindowFocused(ImGuiFocusedFlags_ChildWindows) && !ImGui::GetIO().WantTextInput;
	if(DockIcon("##delete", FontIcon::TRASH, Tr("Delete the selected command (Delete)"), GotSelection) || (GotSelection && ListFocused && ImGui::IsKeyPressed(ImGuiKey_Delete)))
	{
		History.RecordAction(std::make_shared<CEditorCommandAction>(pMap, CEditorCommandAction::EType::DELETE, &Selected, Selected, vSettings[Selected].m_aCommand));
		vSettings.erase(vSettings.begin() + Selected);
		if(Selected >= (int)vSettings.size())
			Selected = vSettings.size() - 1;
		pMap->OnModify();
		Context.Update();
	}
	ImGui::SameLine(0.0f, S(4.0f));
	if(DockIcon("##undo", FontIcon::ARROW_ROTATE_LEFT, Tr("Undo the last change of the settings"), History.CanUndo()))
		History.Undo();
	ImGui::SameLine(0.0f, S(4.0f));
	if(DockIcon("##redo", FontIcon::ARROW_ROTATE_RIGHT, Tr("Redo"), History.CanRedo()))
		History.Redo();
	ImGui::SameLine(0.0f, S(4.0f));
	if(DockIcon("##unknown", FontIcon::QUESTION, Tr("Allow unknown commands"), true, Context.m_AllowUnknownCommands))
	{
		Context.m_AllowUnknownCommands = !Context.m_AllowUnknownCommands;
		Context.Update();
	}

	// ---- what the input means: an error, the help of the command, the possible values
	ImGui::PushFont(pSmall);
	if(m_aSettingInput[0] == '\0')
		ImGui::TextColored(t.m_TextMuted, "%s", Tr("Type a command: the editor checks it and suggests values"));
	else if(Context.HasError())
		ImGui::TextColored(ImVec4(1.0f, 0.42f, 0.42f, 1.0f), "%s %s", FontIcon::TRIANGLE_EXCLAMATION, Context.Error());
	else
	{
		char aHelp[256] = "";
		if(Context.CommandIsValid())
			Context.GetCommandHelpText(aHelp, sizeof(aHelp));
		const char *pState = CanReplace ? Tr("Enter replaces the same command") : CanAdd ? Tr("Enter adds it") : "";
		ImGui::TextColored(t.m_TextMuted, "%s%s%s", aHelp, aHelp[0] && pState[0] ? "  ·  " : "", pState);
	}
	ImGui::PopFont();
	// suggestions: a click puts the value in place of the word being typed
	const std::vector<SPossibleValueMatch> &vMatches = Context.PossibleMatches();
	if(!vMatches.empty() && m_SettingTyping)
	{
		const int Shown = std::min((int)vMatches.size(), 10);
		for(int i = 0; i < Shown; i++)
		{
			if(i > 0)
				ImGui::SameLine(0.0f, S(4.0f));
			ImGui::PushID(i);
			if(ImGui::SmallButton(vMatches[i].m_pValue))
			{
				char *pLast = (char *)str_rchr(m_aSettingInput, ' ');
				if(pLast)
					pLast[1] = '\0';
				else
					m_aSettingInput[0] = '\0';
				str_append(m_aSettingInput, vMatches[i].m_pValue);
				str_append(m_aSettingInput, " ");
				m_SettingFocus = true;
			}
			ImGui::PopID();
		}
	}
	ImGui::Dummy(ImVec2(0.0f, S(4.0f)));

	// ---- the list of commands
	const ImVec2 ListPos = ImGui::GetCursorScreenPos();
	const float ListHeight = Max.y - ListPos.y - S(10.0f);
	if(ListHeight > S(20.0f))
	{
		pDraw->AddRectFilled(ListPos, ImVec2(ListPos.x + Inner, ListPos.y + ListHeight), Col(t.m_Track, 0.4f), S(8.0f));
		ImGui::BeginChild("##settings", ImVec2(Inner, ListHeight), ImGuiChildFlags_None, ImGuiWindowFlags_NoBackground);
		ImDrawList *pList = ImGui::GetWindowDrawList();
		const float RowHeight = S(24.0f);
		const float RowWidth = ImGui::GetContentRegionAvail().x;
		if(vSettings.empty())
		{
			ImGui::SetCursorPos(ImVec2(S(10.0f), S(8.0f)));
			Description(Tr("No commands yet"));
		}
		for(int i = 0; i < (int)vSettings.size(); i++)
		{
			ImGui::PushID(i);
			const ImVec2 RowMin = ImGui::GetCursorScreenPos();
			if(ImGui::InvisibleButton("##row", ImVec2(RowWidth, RowHeight)))
			{
				Selected = i;
				if(!ImGui::GetIO().KeyCtrl) // Ctrl+click only selects, like the classic list
				{
					str_copy(m_aSettingInput, vSettings[i].m_aCommand);
					m_SettingFocus = true;
				}
			}
			const bool Hovered = ImGui::IsItemHovered();
			if(Selected == i)
				pList->AddRectFilled(RowMin, ImVec2(RowMin.x + RowWidth, RowMin.y + RowHeight), Col(t.m_Accent, 0.22f), S(6.0f));
			else if(Hovered)
				pList->AddRectFilled(RowMin, ImVec2(RowMin.x + RowWidth, RowMin.y + RowHeight), Col(t.m_Card), S(6.0f));
			char aNum[16];
			str_format(aNum, sizeof(aNum), "%d", i + 1);
			const float TextY = RowMin.y + (RowHeight - pSmall->FontSize) * 0.5f;
			pList->AddText(pSmall, pSmall->FontSize, ImVec2(RowMin.x + S(10.0f), TextY), Col(t.m_TextMuted), aNum);
			pList->AddText(pSmall, pSmall->FontSize, ImVec2(RowMin.x + S(36.0f), TextY), Col(Selected == i ? t.m_Text : t.m_TextSecondary), vSettings[i].m_aCommand);
			ImGui::PopID();
		}
		ImGui::EndChild();
	}
	ImGui::End();
}

void CXcEditorUi::RenderHistoryDock()
{
	CEditor &E = *m_pEditor;
	CEditorMap *pMap = E.Map();
	const STheme &t = Theme();
	CEditorHistoryUiState &State = pMap->m_EditorHistoryUiState;

	ImVec2 Min, Max;
	if(!BeginDock("##xe_dock_history", CEditor::EXTRAEDITOR_HISTORY, Min, Max))
	{
		ImGui::End();
		return;
	}
	ImDrawList *pDraw = ImGui::GetWindowDrawList();
	ImFont *pSmall = Fonts().m_pSmall;
	const float Inner = Max.x - Min.x - S(28.0f);

	Title(Tr("History"));
	ImGui::SameLine();
	ImGui::SetCursorPosY(ImGui::GetCursorPosY() + S(4.0f));
	Description(Tr("Click an action to go back to it, everything above it is undone"));

	// which history: the map, the envelopes or the server settings
	int Type = (int)State.m_HistoryType;
	const char *apTypes[] = {Tr("Map"), Tr("Envelopes"), Tr("Server settings")};
	ImGui::PushItemWidth(S(360.0f));
	if(Segmented("##histtype", &Type, apTypes, 3))
		State.m_HistoryType = (EHistoryType)Type;
	ImGui::PopItemWidth();
	CEditorHistory *pHistory = State.m_HistoryType == EHistoryType::EDITOR ? &pMap->m_EditorHistory : State.m_HistoryType == EHistoryType::ENVELOPE ? &pMap->m_EnvelopeEditorHistory : &pMap->m_ServerSettingsHistory;
	ImGui::SameLine(0.0f, S(10.0f));
	if(DockIcon("##clear", FontIcon::TRASH, Tr("Clear the history"), pHistory->CanUndo() || pHistory->CanRedo()))
		pHistory->Clear();
	ImGui::Dummy(ImVec2(0.0f, S(4.0f)));

	const int RedoSize = (int)pHistory->m_vpRedoActions.size();
	const int UndoSize = (int)pHistory->m_vpUndoActions.size();
	const int Current = RedoSize;
	int Clicked = -1;

	const ImVec2 ListPos = ImGui::GetCursorScreenPos();
	const float ListHeight = Max.y - ListPos.y - S(10.0f);
	if(ListHeight > S(20.0f))
	{
		pDraw->AddRectFilled(ListPos, ImVec2(ListPos.x + Inner, ListPos.y + ListHeight), Col(t.m_Track, 0.4f), S(8.0f));
		ImGui::BeginChild("##history", ImVec2(Inner, ListHeight), ImGuiChildFlags_None, ImGuiWindowFlags_NoBackground);
		ImDrawList *pList = ImGui::GetWindowDrawList();
		const float RowHeight = S(24.0f);
		const float RowWidth = ImGui::GetContentRegionAvail().x;
		// the redone part on top (grey), then what can be undone, newest first, then the loaded map
		for(int i = 0; i <= RedoSize + UndoSize; i++)
		{
			const char *pText;
			if(i < RedoSize)
				pText = pHistory->m_vpRedoActions[i]->DisplayText();
			else if(i < RedoSize + UndoSize)
				pText = pHistory->m_vpUndoActions[UndoSize - (i - RedoSize) - 1]->DisplayText();
			else
				pText = Tr("Loaded map");
			ImGui::PushID(i);
			const ImVec2 RowMin = ImGui::GetCursorScreenPos();
			if(ImGui::InvisibleButton("##row", ImVec2(RowWidth, RowHeight)))
				Clicked = i;
			const bool Hovered = ImGui::IsItemHovered();
			if(i == Current)
				pList->AddRectFilled(RowMin, ImVec2(RowMin.x + RowWidth, RowMin.y + RowHeight), Col(t.m_Accent, 0.22f), S(6.0f));
			else if(Hovered)
				pList->AddRectFilled(RowMin, ImVec2(RowMin.x + RowWidth, RowMin.y + RowHeight), Col(t.m_Card), S(6.0f));
			const float TextY = RowMin.y + (RowHeight - pSmall->FontSize) * 0.5f;
			const ImVec4 Color = i < RedoSize ? t.m_TextMuted : (i == Current ? t.m_Text : t.m_TextSecondary);
			const char *pIcon = i < RedoSize ? FontIcon::ARROW_ROTATE_RIGHT : (i == RedoSize + UndoSize ? FontIcon::FOLDER_OPEN : FontIcon::ARROW_ROTATE_LEFT);
			pList->AddText(pSmall, pSmall->FontSize * 0.9f, ImVec2(RowMin.x + S(10.0f), TextY), Col(t.m_TextMuted, i < RedoSize ? 0.6f : 1.0f), pIcon);
			pList->AddText(pSmall, pSmall->FontSize, ImVec2(RowMin.x + S(34.0f), TextY), Col(Color, i < RedoSize ? 0.7f : 1.0f), pText);
			ImGui::PopID();
		}
		ImGui::EndChild();
	}
	ImGui::End();

	// undo or redo up to the clicked action, like the classic list
	if(Clicked >= 0 && Clicked != Current)
	{
		if(Clicked > Current)
			for(int i = 0; i < Clicked - Current; i++)
				pHistory->Undo();
		else
			for(int i = 0; i < Current - Clicked; i++)
				pHistory->Redo();
	}
}

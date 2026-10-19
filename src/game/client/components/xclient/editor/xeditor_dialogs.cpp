// The questions, warnings and error messages of the editor in dialogs of the new interface. The event popup
// (unsaved changes, remove a used image, large layer...) still runs its classic code: it is called in the capture
// mode, so its buttons come back as a list and a press here runs the classic action.
#include "xeditor.h"

#include <base/str.h>

#include <game/client/components/xclient/ui/lang.h>
#include <game/client/components/xclient/ui/theme.h>
#include <game/client/components/xclient/ui/widgets.h>
#include <game/editor/editor.h>

#include <engine/external/imgui/imgui.h>

#include <algorithm>

using namespace XcUi;

void CXcEditorUi::ShowMessage(const char *pMessage)
{
	// the same message twice is shown once, like the classic popups
	if(std::find(m_vMessages.begin(), m_vMessages.end(), pMessage) == m_vMessages.end())
		m_vMessages.emplace_back(pMessage);
}

// the card of a dialog in the middle of the screen over a dark veil
static bool BeginDialog(const char *pId, float Width)
{
	const STheme &t = Theme();
	const ImVec2 Display = ImGui::GetIO().DisplaySize;
	ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f));
	ImGui::SetNextWindowSize(Display);
	ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.0f, 0.0f, 0.0f, 0.45f));
	ImGui::Begin("##xe_dialog_veil", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing);
	ImGui::End();
	ImGui::PopStyleColor();

	ImGui::SetNextWindowPos(ImVec2(Display.x * 0.5f, Display.y * 0.45f), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
	ImGui::SetNextWindowSize(ImVec2(Width, 0.0f));
	ImGui::SetNextWindowFocus();
	ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(t.m_Card.x, t.m_Card.y, t.m_Card.z, 1.0f));
	ImGui::PushStyleColor(ImGuiCol_Border, t.m_Border);
	ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, S(16.0f));
	ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 1.0f);
	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(S(20.0f), S(18.0f)));
	return ImGui::Begin(pId, nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_AlwaysAutoResize);
}

static void EndDialog()
{
	ImGui::End();
	ImGui::PopStyleVar(3);
	ImGui::PopStyleColor(2);
}

void CXcEditorUi::RenderEventDialog()
{
	CEditor &E = *m_pEditor;
	if(!m_EventOpen)
		return;

	// the classic popup decides which buttons there are and what they do
	const bool Pressed = m_aCaptures[CAPTURE_EVENT].m_pPendingButton != nullptr;
	if(RunCapture(CAPTURE_EVENT, CEditor::PopupEvent, &E, false) == CUi::POPUP_CLOSE_CURRENT || (Pressed && m_EventCloseAfterPress))
	{
		m_EventOpen = false;
		m_EventCloseAfterPress = false;
		return;
	}
	SCaptureState &Capture = m_aCaptures[CAPTURE_EVENT];
	const std::vector<SXcCapturedButton> &vButtons = Capture.m_Result.m_vButtons;

	const char *pTitle = "";
	const char *pMessage = "";
	char aBuf[128 + IO_MAX_PATH_LENGTH];
	E.XcPopupEventText(&pTitle, &pMessage, aBuf, sizeof(aBuf));

	const float Width = S(460.0f);
	const float Inner = Width - S(40.0f);
	if(BeginDialog("##xe_event", Width))
	{
		Header(Tr(pTitle));
		ImGui::Dummy(ImVec2(0.0f, S(2.0f)));
		Description(Tr(pMessage), Inner);
		ImGui::Dummy(ImVec2(0.0f, S(10.0f)));

		// the buttons in the classic order: Cancel, Confirm (or Save changes), Discard changes
		const void *pPress = nullptr;
		const void *pCancel = nullptr;
		const void *pConfirm = nullptr;
		if(!vButtons.empty())
		{
			const float Gap = S(8.0f);
			const float ButtonWidth = (Inner - Gap * (vButtons.size() - 1)) / vButtons.size();
			for(size_t i = 0; i < vButtons.size(); i++)
			{
				const SXcCapturedButton &Captured = vButtons[i];
				const bool IsCancel = Captured.m_Label == "Cancel";
				const bool IsDanger = Captured.m_Label == "Discard changes";
				if(IsCancel)
					pCancel = Captured.m_pId;
				else if(!IsDanger)
					pConfirm = Captured.m_pId;
				if(i > 0)
					ImGui::SameLine(0.0f, Gap);
				ImGui::PushID((int)i);
				if(IsDanger)
					ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.55f, 0.55f, 1.0f));
				if(Button(Tr(Captured.m_Label.c_str()), !IsCancel && !IsDanger, ButtonWidth))
				{
					pPress = Captured.m_pId;
					// "Discard changes" closes the map but the classic popup stays open by itself
					m_EventCloseAfterPress = IsDanger;
				}
				if(IsDanger)
					ImGui::PopStyleColor();
				ImGui::PopID();
			}
		}
		// Enter confirms, Esc cancels (a notice without Cancel closes with its only button)
		if(ImGui::IsKeyPressed(ImGuiKey_Enter) || ImGui::IsKeyPressed(ImGuiKey_KeypadEnter))
			pPress = pConfirm;
		if(ImGui::IsKeyPressed(ImGuiKey_Escape))
			pPress = pCancel != nullptr ? pCancel : pConfirm;
		if(pPress != nullptr)
			Capture.m_pPendingButton = pPress;
	}
	EndDialog();
	E.Ui()->ConsumeHotkey(CUi::HOTKEY_ESCAPE);
	E.Ui()->ConsumeHotkey(CUi::HOTKEY_ENTER);
}

void CXcEditorUi::RenderMessages()
{
	if(m_vMessages.empty() || m_EventOpen)
		return;
	CEditor &E = *m_pEditor;
	const float Width = S(460.0f);
	const float Inner = Width - S(40.0f);
	bool Close = false;
	if(BeginDialog("##xe_message", Width))
	{
		Header(Tr("Something went wrong"));
		ImGui::Dummy(ImVec2(0.0f, S(2.0f)));
		Description(Tr(m_vMessages.front().c_str()), Inner);
		ImGui::Dummy(ImVec2(0.0f, S(10.0f)));
		if(Button(Tr("OK"), true, Inner) || ImGui::IsKeyPressed(ImGuiKey_Enter) || ImGui::IsKeyPressed(ImGuiKey_KeypadEnter) || ImGui::IsKeyPressed(ImGuiKey_Escape))
			Close = true;
	}
	EndDialog();
	E.Ui()->ConsumeHotkey(CUi::HOTKEY_ESCAPE);
	E.Ui()->ConsumeHotkey(CUi::HOTKEY_ENTER);
	if(Close)
		m_vMessages.erase(m_vMessages.begin());
}

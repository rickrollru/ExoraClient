// The window of the shared editing (open a room, join a room, who is here) and the cursors of the others
// over the map.
#include "xeditor.h"

#include <base/str.h>

#include <engine/shared/config.h>

#include <game/client/components/xclient/ui/lang.h>
#include <game/client/components/xclient/ui/theme.h>
#include <game/client/components/xclient/ui/widgets.h>
#include <game/editor/editor.h>
#include <game/editor/mapitems/layer_group.h>

#include <engine/external/imgui/imgui.h>

#include <algorithm>

using namespace XcUi;

static ImVec4 MemberColor(int Id, float Alpha = 1.0f)
{
	const unsigned Rgb = CXcCoop::MemberColor(Id);
	return ImVec4(((Rgb >> 16) & 0xff) / 255.0f, ((Rgb >> 8) & 0xff) / 255.0f, (Rgb & 0xff) / 255.0f, Alpha);
}

void CXcEditorUi::UpdateCoop()
{
	m_Coop.Update();
	if(m_Coop.TakeMapReplaced())
	{
		// pointers into the old map
		m_pAdjustImage = nullptr;
		m_pLastQuads = nullptr;
		m_pLastBrushLayer = nullptr;
		m_pEnvLast = nullptr;
		m_EnvSelPoint = -1;
		m_EnvDragging = false;
		m_RenameGroup = -1;
		m_RenameLayer = -1;
		for(SCaptureState &Capture : m_aCaptures)
			Capture = SCaptureState();
	}
	const std::string Notice = m_Coop.TakeNotice();
	if(!Notice.empty())
		ShowMessage(Notice.c_str(), "Edit together");
}

void CXcEditorUi::RenderCoop()
{
	const STheme &t = Theme();
	if(m_CoopRequest)
	{
		ImGui::OpenPopup("##xe_coop");
		m_CoopRequest = false;
		m_aCoopError[0] = '\0';
		str_format(m_aCoopPort, sizeof(m_aCoopPort), "%d", g_Config.m_XcCoopPort);
		str_copy(m_aCoopAddress, g_Config.m_XcCoopAddress);
	}
	const ImVec2 Display = ImGui::GetIO().DisplaySize;
	const float Width = std::min(S(520.0f), Display.x - S(40.0f));
	const float Inner = Width - S(40.0f);
	ImGui::SetNextWindowPos(ImVec2(Display.x * 0.5f, Display.y * 0.45f), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
	ImGui::SetNextWindowSize(ImVec2(Width, 0.0f));
	ImGui::PushStyleColor(ImGuiCol_PopupBg, ImVec4(t.m_Card.x, t.m_Card.y, t.m_Card.z, 1.0f));
	ImGui::PushStyleColor(ImGuiCol_ModalWindowDimBg, ImVec4(0.0f, 0.0f, 0.0f, 0.45f));
	ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, S(16.0f));
	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(S(20.0f), S(18.0f)));
	if(ImGui::BeginPopupModal("##xe_coop", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_AlwaysAutoResize))
	{
		bool Close = false;
		Header(Tr("Edit together"));
		Description(Tr("One map with friends over LAN or Radmin VPN. Blocks show up right away, layers, quads and envelopes a moment after you stop."), Inner);
		ImGui::Dummy(ImVec2(0.0f, S(8.0f)));

		ImDrawList *pDraw = ImGui::GetWindowDrawList();
		const int Role = m_Coop.Role();
		if(Role == CXcCoop::ROLE_NONE)
		{
			// open a room
			ParamLabel(Tr("Open a room for the current map"));
			const float PortWidth = S(90.0f);
			InputText("##coop_port", m_aCoopPort, sizeof(m_aCoopPort), Tr("Port"), PortWidth);
			ImGui::SameLine(0.0f, S(8.0f));
			if(Button(Tr("Open a room"), true, Inner - PortWidth - S(8.0f)))
			{
				const int Port = std::clamp(str_toint(m_aCoopPort), 1024, 65535);
				char aError[128];
				if(m_Coop.Host(Port, aError, sizeof(aError)))
				{
					g_Config.m_XcCoopPort = Port;
					m_aCoopError[0] = '\0';
				}
				else
					str_copy(m_aCoopError, Tr(aError));
			}
			ImGui::Dummy(ImVec2(0.0f, S(2.0f)));
			Description(Tr("Friends connect to your IP. In Radmin VPN it is the 26.x.x.x address next to your name. If Windows asks about the network, allow it."), Inner);
			ImGui::Dummy(ImVec2(0.0f, S(10.0f)));

			// join a room
			ParamLabel(Tr("Join a friend's room"));
			const float JoinWidth = S(130.0f);
			const bool Enter = InputText("##coop_address", m_aCoopAddress, sizeof(m_aCoopAddress), "26.12.34.56:8314", Inner - JoinWidth - S(8.0f)) && ImGui::IsKeyPressed(ImGuiKey_Enter);
			ImGui::SameLine(0.0f, S(8.0f));
			if((Button(Tr("Join"), false, JoinWidth) || Enter) && m_aCoopAddress[0])
			{
				char aError[128];
				if(m_Coop.Join(m_aCoopAddress, aError, sizeof(aError)))
				{
					str_copy(g_Config.m_XcCoopAddress, m_aCoopAddress);
					m_aCoopError[0] = '\0';
				}
				else
					str_copy(m_aCoopError, Tr(aError));
			}
			ImGui::Dummy(ImVec2(0.0f, S(2.0f)));
			Description(Tr("The map of the room opens in a new tab. Save it to your computer whenever you want."), Inner);
			if(m_aCoopError[0])
			{
				ImGui::Dummy(ImVec2(0.0f, S(4.0f)));
				ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.42f, 0.42f, 1.0f));
				ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + Inner);
				ImGui::TextUnformatted(m_aCoopError);
				ImGui::PopTextWrapPos();
				ImGui::PopStyleColor();
			}
		}
		else
		{
			// the state of the session
			char aStatus[256];
			const int State = m_Coop.State();
			if(Role == CXcCoop::ROLE_HOST)
				str_format(aStatus, sizeof(aStatus), Tr("The room is open on port %d. Friends join with your IP and this port."), m_Coop.Port());
			else if(State == CXcCoop::STATE_CONNECTING)
				str_format(aStatus, sizeof(aStatus), Tr("Connecting to %s..."), m_Coop.Address());
			else if(State == CXcCoop::STATE_JOINING)
			{
				if(m_Coop.Progress() >= 0.0f)
					str_format(aStatus, sizeof(aStatus), Tr("Getting the map... %d%%"), (int)(m_Coop.Progress() * 100.0f));
				else
					str_copy(aStatus, Tr("Getting the map..."));
			}
			else
				str_format(aStatus, sizeof(aStatus), Tr("Connected to %s"), m_Coop.Address());
			{
				const ImVec2 Pos = ImGui::GetCursorScreenPos();
				const float Pulse = 0.6f + 0.4f * std::sin(ImGui::GetTime() * 4.0f);
				const ImVec4 Dot = State == CXcCoop::STATE_ONLINE ? ImVec4(0.24f, 0.84f, 0.64f, 1.0f) : ImVec4(1.0f, 0.75f, 0.25f, Pulse);
				ImFont *pLabel = Fonts().m_pLabel;
				pDraw->AddCircleFilled(ImVec2(Pos.x + S(5.0f), Pos.y + pLabel->FontSize * 0.5f), S(4.0f), Col(Dot));
				ImGui::SetCursorScreenPos(ImVec2(Pos.x + S(16.0f), Pos.y));
				ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + Inner - S(16.0f));
				ImGui::PushStyleColor(ImGuiCol_Text, t.m_Text);
				ImGui::TextUnformatted(aStatus);
				ImGui::PopStyleColor();
				ImGui::PopTextWrapPos();
			}
			ImGui::Dummy(ImVec2(0.0f, S(10.0f)));

			// who is here
			ParamLabel(Tr("In the room"));
			for(const CXcCoop::SMember &Member : m_Coop.Members())
			{
				const ImVec2 Min = ImGui::GetCursorScreenPos();
				const float Row = S(30.0f);
				PanelRect(pDraw, Min, ImVec2(Min.x + Inner, Min.y + Row), S(8.0f), t.m_Panel);
				pDraw->AddCircleFilled(ImVec2(Min.x + S(15.0f), Min.y + Row * 0.5f), S(6.0f), Col(MemberColor(Member.m_Id)));
				char aName[96];
				str_format(aName, sizeof(aName), "%s%s%s", Member.m_Name.c_str(), Member.m_Id == m_Coop.SelfId() ? Tr(" (you)") : "", Member.m_Id == 0 ? Tr(" · host") : "");
				ImFont *pLabel = Fonts().m_pLabel;
				pDraw->AddText(pLabel, pLabel->FontSize, ImVec2(Min.x + S(30.0f), Min.y + (Row - pLabel->FontSize) * 0.5f), Col(t.m_Text), aName);
				ImGui::Dummy(ImVec2(Inner, Row));
				ImGui::Dummy(ImVec2(0.0f, S(2.0f)));
			}
			if(m_Coop.Members().empty())
				Description(Tr("Nobody yet"), Inner);
			ImGui::Dummy(ImVec2(0.0f, S(8.0f)));
			if(Button(Role == CXcCoop::ROLE_HOST ? Tr("Close the room") : Tr("Leave the room"), false, Inner))
				m_Coop.Leave();
		}
		ImGui::Dummy(ImVec2(0.0f, S(6.0f)));
		if(Button(Tr("Close"), false, Inner) || (ImGui::IsKeyPressed(ImGuiKey_Escape) && !m_Typing))
			Close = true;
		if(Close)
			ImGui::CloseCurrentPopup();
		ImGui::EndPopup();
	}
	ImGui::PopStyleVar(2);
	ImGui::PopStyleColor(2);
}

void CXcEditorUi::RenderCoopCursors()
{
	CEditor &E = *m_pEditor;
	const CEditorMap *pMap = m_Coop.SharedMap();
	if(m_Coop.State() != CXcCoop::STATE_ONLINE || !pMap || E.Map() != pMap)
		return;
	// world (without parallax) -> pixels through the game group, like the editor does for the mouse
	const CLayerGroup *pGame = nullptr;
	for(const auto &pGroup : pMap->m_vpGroups)
		if(pGroup->m_GameGroup)
			pGame = pGroup.get();
	if(!pGame)
		return;
	const CScreenRect Rect = pGame->Mapping();
	if(Rect.Width() <= 0.0f || Rect.Height() <= 0.0f)
		return;
	const ImVec2 Display = ImGui::GetIO().DisplaySize;
	const float k = UiToPixels();
	const ImVec2 ClipMin(m_View.x * k, m_View.y * k);
	const ImVec2 ClipMax((m_View.x + m_View.w) * k, (m_View.y + m_View.h) * k);
	ImDrawList *pDraw = ImGui::GetBackgroundDrawList();
	pDraw->PushClipRect(ClipMin, ClipMax, true);
	ImFont *pSmall = Fonts().m_pSmall;
	const float Time = E.Client()->GlobalTime();
	for(const CXcCoop::SMember &Member : m_Coop.Members())
	{
		if(Member.m_Id == m_Coop.SelfId() || !Member.m_HasCursor)
			continue;
		const ImVec2 Pos((Member.m_Cursor.x - Rect.m_TopLeft.x) / Rect.Width() * Display.x, (Member.m_Cursor.y - Rect.m_TopLeft.y) / Rect.Height() * Display.y);
		const float Alpha = Time - Member.m_LastSeen > 20.0f ? 0.45f : 1.0f;
		const ImVec4 Color = MemberColor(Member.m_Id, Alpha);
		// an arrow like the system cursor
		const float s = S(1.0f);
		const ImVec2 aArrow[] = {
			Pos,
			ImVec2(Pos.x, Pos.y + 17.0f * s),
			ImVec2(Pos.x + 4.5f * s, Pos.y + 13.0f * s),
			ImVec2(Pos.x + 8.0f * s, Pos.y + 20.0f * s),
			ImVec2(Pos.x + 11.0f * s, Pos.y + 18.5f * s),
			ImVec2(Pos.x + 7.5f * s, Pos.y + 12.0f * s),
			ImVec2(Pos.x + 13.0f * s, Pos.y + 12.0f * s),
		};
		pDraw->AddConcavePolyFilled(aArrow, (int)std::size(aArrow), Col(Color));
		pDraw->AddPolyline(aArrow, (int)std::size(aArrow), Col(ImVec4(0.04f, 0.04f, 0.06f, 0.9f * Alpha)), ImDrawFlags_Closed, 1.2f * s);

		// the name and the layer in a pill
		char aLabel[128];
		str_copy(aLabel, Member.m_Name.c_str());
		if(Member.m_Group >= 0 && Member.m_Group < (int)pMap->m_vpGroups.size())
		{
			const auto &vpLayers = pMap->m_vpGroups[Member.m_Group]->m_vpLayers;
			if(Member.m_Layer >= 0 && Member.m_Layer < (int)vpLayers.size())
			{
				const char *pLayerName = vpLayers[Member.m_Layer]->m_aName;
				char aLayer[32];
				if(!pLayerName[0])
				{
					str_format(aLayer, sizeof(aLayer), "#%d", Member.m_Layer);
					pLayerName = aLayer;
				}
				str_append(aLabel, " · ");
				str_append(aLabel, pLayerName);
			}
		}
		const ImVec2 TextSize = pSmall->CalcTextSizeA(pSmall->FontSize, FLT_MAX, 0.0f, aLabel);
		const ImVec2 PillMin(Pos.x + 12.0f * s, Pos.y + 18.0f * s);
		const ImVec2 PillMax(PillMin.x + TextSize.x + S(14.0f), PillMin.y + TextSize.y + S(6.0f));
		pDraw->AddRectFilled(PillMin, PillMax, Col(Color), S(6.0f));
		const float Light = Color.x * 0.3f + Color.y * 0.59f + Color.z * 0.11f;
		pDraw->AddText(pSmall, pSmall->FontSize, ImVec2(PillMin.x + S(7.0f), PillMin.y + S(3.0f)), Col(Light > 0.6f ? ImVec4(0.05f, 0.06f, 0.08f, Alpha) : ImVec4(1.0f, 1.0f, 1.0f, Alpha)), aLabel);
	}
	pDraw->PopClipRect();
}


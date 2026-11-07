// The visuals as a compact window over the game: an icon rail with the sections on the left, the cards of the section
// in a column next to it (a click scrolls to the card), a search over all sections at the top, the cards themselves on
// the right. It opens softly, the marks slide to what is picked, and it is dragged by its top on a spring.
#include "menu.h"

#include "feedback.h"
#include "icons.h"
#include "lang.h"
#include "theme.h"
#include "widgets.h"

#include <base/str.h>

#include <engine/client.h>
#include <engine/external/imgui/imgui.h>
#include <engine/external/imgui/imgui_internal.h>
#include <engine/shared/config.h>

#include <game/client/components/xclient/hud/xc_hud.h>
#include <game/client/components/xclient/streamer.h>
#include <game/client/gameclient.h>

#include <algorithm>
#include <cmath>

using namespace XcUi;

namespace
{
	float EaseOut(float x)
	{
		x = std::clamp(x, 0.0f, 1.0f);
		return 1.0f - std::pow(1.0f - x, 3.0f);
	}
}

void CXcMenu::RenderVisualsWindow(float Dt)
{
	const STheme &t = Theme();
	ImGuiIO &Io = ImGui::GetIO();
	const ImVec2 Display = Io.DisplaySize;
	const float W = std::min(S(1120.0f), Display.x - S(32.0f));
	const float H = std::min(S(700.0f), Display.y - S(40.0f));
	const float RailW = S(64.0f), ColW = S(220.0f), TopH = S(60.0f);
	const char *apIcons[NUM_VISUALS] = {XC_ICON_SLIDERS, XC_ICON_USER, XC_ICON_BOLT, XC_ICON_HAT, XC_ICON_HAMMER, XC_ICON_CROSSHAIR, XC_ICON_GLOBE, XC_ICON_STAR, XC_ICON_USERS};
	const char *apLabels[NUM_VISUALS] = {Tr("General"), Tr("Tee"), Tr("Movement"), Tr("Cosmetics"), Tr("Weapons"), Tr("Crosshair"), Tr("World"), Tr("Events"), Tr("Names")};

	// the place: the middle at first, then where it was dragged; it follows on a spring
	if(m_aVisWinTarget[0] < 0.0f || m_VisWinOpen <= 0.0f)
	{
		m_aVisWinTarget[0] = (Display.x - W) * 0.5f;
		m_aVisWinTarget[1] = (Display.y - H) * 0.5f;
	}
	m_aVisWinTarget[0] = std::clamp(m_aVisWinTarget[0], S(60.0f) - W, Display.x - S(60.0f));
	m_aVisWinTarget[1] = std::clamp(m_aVisWinTarget[1], 0.0f, Display.y - S(60.0f));
	if(m_aVisWinPos[0] < -9999.0f || m_VisWinOpen <= 0.0f)
	{
		m_aVisWinPos[0] = m_aVisWinTarget[0];
		m_aVisWinPos[1] = m_aVisWinTarget[1];
		m_aVisWinVel[0] = m_aVisWinVel[1] = 0.0f;
	}
	const float Stiff = AnimationSpeed() > 0.0f ? 260.0f : 0.0f;
	for(int a = 0; a < 2; a++)
	{
		if(Stiff <= 0.0f)
		{
			m_aVisWinPos[a] = m_aVisWinTarget[a];
			continue;
		}
		const float Damp = 2.0f * std::sqrt(Stiff) * 0.85f;
		const int Steps = std::max(1, (int)std::ceil(Dt / 0.008f));
		for(int s = 0; s < Steps; s++)
		{
			const float h = Dt / Steps;
			m_aVisWinVel[a] += ((m_aVisWinTarget[a] - m_aVisWinPos[a]) * Stiff - m_aVisWinVel[a] * Damp) * h;
			m_aVisWinPos[a] += m_aVisWinVel[a] * h;
		}
	}
	// opens: fades in, rises a little
	m_VisWinOpen = std::min(m_VisWinOpen + Dt / 0.4f, 1.0f);
	const float Open = EaseOut(m_VisWinOpen);
	const ImVec2 Min(m_aVisWinPos[0], m_aVisWinPos[1] + (1.0f - Open) * S(14.0f));
	const ImVec2 Max(Min.x + W, Min.y + H);

	ImGui::GetStyle().Alpha *= Open;
	ImGui::SetNextWindowPos(Min);
	ImGui::SetNextWindowSize(ImVec2(W, H));
	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
	ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
	ImGui::Begin("##xc_visuals_window", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoNav);
	ImGui::PopStyleVar(2);
	ImGui::PushFont(Fonts().m_pBody);
	ImDrawList *pDraw = ImGui::GetWindowDrawList();
	const float Round = S(18.0f);
	// a soft shadow, the panel, the lines between the parts
	for(int i = 1; i <= 4; i++)
	{
		const float g = i * S(6.0f);
		pDraw->AddRectFilled(ImVec2(Min.x - g, Min.y - g + S(10.0f)), ImVec2(Max.x + g, Max.y + g + S(10.0f)), Col(ImVec4(0, 0, 0, 1), 0.06f * Open), Round + g);
	}
	pDraw->AddRectFilled(Min, Max, Col(ImVec4(t.m_Panel.x, t.m_Panel.y, t.m_Panel.z, 0.95f)), Round);
	pDraw->AddRect(Min, Max, Col(t.m_Border), Round, 0, 1.0f);
	pDraw->AddLine(ImVec2(Min.x + RailW, Min.y), ImVec2(Min.x + RailW, Max.y), Col(t.m_Border), 1.0f);
	pDraw->AddLine(ImVec2(Min.x + RailW + ColW, Min.y), ImVec2(Min.x + RailW + ColW, Max.y), Col(t.m_Border), 1.0f);
	pDraw->AddLine(ImVec2(Min.x + RailW + ColW, Min.y + TopH), ImVec2(Max.x, Min.y + TopH), Col(t.m_Border), 1.0f);

	const bool Searching = m_aVisSearch[0] != '\0';

	// ---- the rail: the logo, the sections, the settings at the bottom
	{
		DrawLogo(pDraw, ImVec2(Min.x + RailW * 0.5f, Min.y + S(32.0f)), S(30.0f), t.m_Accent, 1.0f, false);
		const float B = S(42.0f);
		const float Top = Min.y + S(70.0f);
		const float Step = std::min(S(48.0f), (H - S(140.0f)) / NUM_VISUALS);
		const float KnobY = Animate(ImGui::GetID("##vis_rail_knob"), Top + m_VisualsSection * Step, 16.0f);
		if(!Searching)
		{
			const ImVec2 KMin(Min.x + (RailW - B) * 0.5f, KnobY), KMax(KMin.x + B, KMin.y + B);
			pDraw->AddRectFilled(KMin, KMax, Col(ImVec4(1, 1, 1, 0.08f)), S(11.0f));
			pDraw->AddRectFilled(ImVec2(Min.x + S(1.0f), KMin.y + S(12.0f)), ImVec2(Min.x + S(4.0f), KMax.y - S(12.0f)), Col(t.m_Accent), S(2.0f));
		}
		for(int i = 0; i < NUM_VISUALS; i++)
		{
			const ImVec2 P(Min.x + (RailW - B) * 0.5f, Top + i * Step);
			ImGui::SetCursorScreenPos(P);
			ImGui::PushID(i);
			const bool Clicked = ImGui::InvisibleButton("##vis_rail", ImVec2(B, B));
			const bool Hovered = ImGui::IsItemHovered();
			const float Hover = Animate(ImGui::GetID("##h"), (Hovered || (i == m_VisualsSection && !Searching)) ? 1.0f : 0.0f, 16.0f);
			const float Press = Animate(ImGui::GetID("##p"), ImGui::IsItemActive() ? 1.0f : 0.0f, 22.0f);
			ImGui::PopID();
			DrawIcon(pDraw, Fonts().m_pIcons, Fonts().m_pIcons->FontSize * (1.0f - 0.1f * Press), ImVec2(P.x + B * 0.5f, P.y + B * 0.5f), Col(Mix(t.m_TextMuted, t.m_Text, Hover)), apIcons[i]);
			if(Hovered)
				ImGui::SetTooltip("%s", apLabels[i]);
			if(Clicked && (i != m_VisualsSection || Searching))
			{
				m_VisualsSection = i;
				m_aVisSearch[0] = '\0';
				m_ScrollTarget = 0.0f;
				m_VisPickedCard.clear();
				PlayUiSound(SOUND_TAB);
			}
		}
		// the settings
		const ImVec2 P(Min.x + (RailW - B) * 0.5f, Max.y - B - S(14.0f));
		ImGui::SetCursorScreenPos(P);
		const bool Clicked = ImGui::InvisibleButton("##vis_settings", ImVec2(B, B));
		const float Hover = Animate(ImGui::GetID("##vis_settings_h"), ImGui::IsItemHovered() ? 1.0f : 0.0f, 16.0f);
		DrawIcon(pDraw, Fonts().m_pIcons, Fonts().m_pIcons->FontSize, ImVec2(P.x + B * 0.5f, P.y + B * 0.5f), Col(Mix(t.m_TextMuted, t.m_Text, Hover)), XC_ICON_GEAR);
		if(ImGui::IsItemHovered())
			ImGui::SetTooltip("%s", Tr("Settings"));
		if(Clicked)
		{
			PlayUiSound(SOUND_TAB);
			m_Page = PAGE_SETTINGS;
			m_PageAppear = 0.0f;
		}
	}

	// ---- the column: the section name and its cards; a click scrolls to the card
	{
		const float X = Min.x + RailW;
		ImFont *pHead = Fonts().m_pTitle;
		const char *pHeadText = Searching ? Tr("Search") : apLabels[m_VisualsSection];
		pDraw->AddText(pHead, pHead->FontSize * 1.1f, ImVec2(X + S(18.0f), Min.y + S(20.0f)), Col(t.m_Text), pHeadText);
		const float Top = Min.y + S(60.0f);
		const float RowH = S(34.0f);
		ImGui::SetCursorScreenPos(ImVec2(X + S(10.0f), Top));
		ImGui::BeginChild("##vis_cards_list", ImVec2(ColW - S(20.0f), Max.y - Top - S(12.0f)), ImGuiChildFlags_None, ImGuiWindowFlags_NoBackground);
		ImDrawList *pList = ImGui::GetWindowDrawList();
		const ImVec2 ListMin = ImGui::GetCursorScreenPos();
		int Picked = -1;
		for(int i = 0; i < (int)m_vVisCards.size(); i++)
			if(m_vVisCards[i].first == m_VisPickedCard)
				Picked = i;
		const float KnobY = Animate(ImGui::GetID("##vis_col_knob"), ListMin.y + std::max(Picked, 0) * RowH, 16.0f);
		const float KnobA = Animate(ImGui::GetID("##vis_col_knob_a"), Picked >= 0 ? 1.0f : 0.0f, 12.0f);
		if(KnobA > 0.01f)
			pList->AddRectFilled(ImVec2(ListMin.x, KnobY), ImVec2(ListMin.x + ColW - S(20.0f), KnobY + RowH), Col(ImVec4(1, 1, 1, 0.06f), KnobA), S(9.0f));
		for(int i = 0; i < (int)m_vVisCards.size(); i++)
		{
			ImGui::PushID(i);
			const ImVec2 P = ImGui::GetCursorScreenPos();
			const bool Clicked = ImGui::InvisibleButton("##vis_card", ImVec2(ColW - S(20.0f), RowH));
			const float Hover = Animate(ImGui::GetID("##h"), (ImGui::IsItemHovered() || i == Picked) ? 1.0f : 0.0f, 16.0f);
			// the rows come in one after another when the list changes
			const float In = EaseOut((float)(ImGui::GetTime() - m_VisListTime) * 4.0f - i * 0.12f);
			ImGui::PopID();
			char aTitle[128];
			XcFitText(Fonts().m_pLabel, Fonts().m_pLabel->FontSize, m_vVisCards[i].second.c_str(), ColW - S(44.0f), aTitle, sizeof(aTitle));
			pList->AddText(Fonts().m_pLabel, Fonts().m_pLabel->FontSize, ImVec2(P.x + S(10.0f) - (1.0f - In) * S(8.0f), P.y + (RowH - Fonts().m_pLabel->FontSize) * 0.5f), Col(Mix(t.m_TextMuted, t.m_Text, Hover), In), aTitle);
			if(Clicked)
			{
				PlayUiSound(SOUND_CLICK);
				m_VisPickedCard = m_vVisCards[i].first;
				HighlightCard(m_VisPickedCard.c_str());
				m_ScrollToHighlight = true;
			}
		}
		if(m_vVisCards.empty() && Searching)
		{
			ImGui::PushFont(Fonts().m_pSmall);
			ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + ColW - S(40.0f));
			ImGui::TextColored(t.m_TextMuted, "%s", Tr("Nothing found"));
			ImGui::PopTextWrapPos();
			ImGui::PopFont();
		}
		ImGui::EndChild();
	}

	// ---- the top: search, the profile, close; the free part drags the window
	{
		const float X = Min.x + RailW + ColW;
		const float SearchW = std::min(S(420.0f), (Max.x - X) * 0.55f);
		const float FieldH = S(36.0f);
		const ImVec2 SMin(X + S(16.0f), Min.y + (TopH - FieldH) * 0.5f);
		const bool Focused = ImGui::GetID("##vis_search") == ImGui::GetActiveID();
		const float Focus = Animate(ImGui::GetID("##vis_search_f"), Focused ? 1.0f : 0.0f, 14.0f);
		const float FieldW = SearchW + Focus * S(60.0f);
		pDraw->AddRectFilled(SMin, ImVec2(SMin.x + FieldW, SMin.y + FieldH), Col(ImVec4(1, 1, 1, 0.04f + 0.03f * Focus)), S(10.0f));
		pDraw->AddRect(SMin, ImVec2(SMin.x + FieldW, SMin.y + FieldH), Col(Mix(t.m_Border, ImVec4(t.m_Accent.x, t.m_Accent.y, t.m_Accent.z, 0.5f), Focus)), S(10.0f), 0, 1.0f);
		DrawIcon(pDraw, Fonts().m_pSmall, Fonts().m_pSmall->FontSize, ImVec2(SMin.x + S(18.0f), SMin.y + FieldH * 0.5f), Col(t.m_TextMuted), XC_ICON_SEARCH);
		ImGui::SetCursorScreenPos(ImVec2(SMin.x + S(32.0f), SMin.y + (FieldH - Fonts().m_pLabel->FontSize) * 0.5f - S(3.0f)));
		ImGui::PushFont(Fonts().m_pLabel);
		ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0, 0, 0, 0));
		ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, ImVec4(0, 0, 0, 0));
		ImGui::PushStyleColor(ImGuiCol_FrameBgActive, ImVec4(0, 0, 0, 0));
		ImGui::PushStyleColor(ImGuiCol_TextDisabled, t.m_TextMuted);
		ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(0.0f, S(3.0f)));
		ImGui::SetNextItemWidth(FieldW - S(44.0f));
		ImGui::InputTextWithHint("##vis_search", Tr("Search the visuals"), m_aVisSearch, sizeof(m_aVisSearch));
		ImGui::PopStyleVar();
		ImGui::PopStyleColor(4);
		ImGui::PopFont();

		// close: back to the home
		const float B = S(32.0f);
		const ImVec2 CMin(Max.x - S(16.0f) - B, Min.y + (TopH - B) * 0.5f);
		ImGui::SetCursorScreenPos(CMin);
		const bool Close = ImGui::InvisibleButton("##vis_close", ImVec2(B, B));
		const float CloseHover = Animate(ImGui::GetID("##vis_close_h"), ImGui::IsItemHovered() ? 1.0f : 0.0f, 16.0f);
		pDraw->AddRectFilled(CMin, ImVec2(CMin.x + B, CMin.y + B), Col(ImVec4(1, 1, 1, 0.05f * CloseHover)), S(9.0f));
		pDraw->AddRect(CMin, ImVec2(CMin.x + B, CMin.y + B), Col(t.m_Border), S(9.0f), 0, 1.0f);
		DrawIcon(pDraw, Fonts().m_pSmall, Fonts().m_pSmall->FontSize, ImVec2(CMin.x + B * 0.5f, CMin.y + B * 0.5f), Col(Mix(t.m_TextMuted, t.m_Text, CloseHover)), XC_ICON_XMARK);
		// the profile: the name, "Exora", the logo
		const char *pName = XcStreamer::OwnName(g_Config.m_PlayerName[0] ? g_Config.m_PlayerName : Client()->PlayerName());
		ImFont *pBold = Fonts().m_pLabel;
		const float NameW = pBold->CalcTextSizeA(pBold->FontSize, FLT_MAX, 0.0f, pName).x;
		const float ProfileR = CMin.x - S(14.0f);
		const float LogoS = S(30.0f);
		DrawLogo(pDraw, ImVec2(ProfileR - LogoS * 0.5f, Min.y + TopH * 0.5f), LogoS, t.m_Accent, 1.0f, true);
		pDraw->AddText(pBold, pBold->FontSize, ImVec2(ProfileR - LogoS - S(10.0f) - NameW, Min.y + TopH * 0.5f - pBold->FontSize), Col(t.m_Text), pName);
		const char *pClient = "Exora";
		const float ClientW = Fonts().m_pSmall->CalcTextSizeA(Fonts().m_pSmall->FontSize, FLT_MAX, 0.0f, pClient).x;
		pDraw->AddText(Fonts().m_pSmall, Fonts().m_pSmall->FontSize, ImVec2(ProfileR - LogoS - S(10.0f) - ClientW, Min.y + TopH * 0.5f + S(1.0f)), Col(t.m_Accent), pClient);
		// the free part of the top drags the window
		const float DragL = SMin.x + FieldW + S(10.0f);
		const float DragR = ProfileR - LogoS - S(16.0f) - std::max(NameW, ClientW);
		if(DragR > DragL)
		{
			ImGui::SetCursorScreenPos(ImVec2(DragL, Min.y));
			ImGui::InvisibleButton("##vis_drag", ImVec2(DragR - DragL, TopH));
			if(ImGui::IsItemActive() && ImGui::IsMouseDragging(ImGuiMouseButton_Left, 0.0f))
			{
				m_aVisWinTarget[0] += Io.MouseDelta.x;
				m_aVisWinTarget[1] += Io.MouseDelta.y;
			}
			if(ImGui::IsItemHovered() || ImGui::IsItemActive())
				ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeAll);
		}
		if(Close)
		{
			PlayUiSound(SOUND_CLICK);
			m_Page = PAGE_HOME;
		}
	}

	// ---- the cards: the section, or all sections filtered by the search
	{
		const float X = Min.x + RailW + ColW;
		const std::string Query = m_aVisSearch;
		if(m_VisualsSection != m_VisLastSection || Query != m_VisLastQuery)
		{
			// a new page: its cards come in one after another, the list too
			m_VisLastSection = m_VisualsSection;
			m_VisLastQuery = Query;
			PageEntered();
			m_VisListTime = ImGui::GetTime();
			m_ScrollTarget = 0.0f;
			g_Config.m_XcMenuVisualsSection = m_VisualsSection;
		}
		ImGui::SetCursorScreenPos(ImVec2(X + S(18.0f), Min.y + TopH + S(16.0f)));
		ClearDrawnCards();
		BeginSmoothScroll("##vis_window_scroll", ImVec2(Max.x - X - S(30.0f), Max.y - Min.y - TopH - S(28.0f)));
		if(Searching)
		{
			SetCardFilter(m_aVisSearch);
			BeginCardGrid("##vis_found", 2);
			MergeCardGrids(true);
			for(int i = 0; i < NUM_VISUALS; i++)
				RenderVisualsSection(i);
			MergeCardGrids(false);
			EndCardGrid();
			SetCardFilter("");
			if(DrawnCards().empty())
			{
				Title(Tr("Nothing found"));
				Description(Tr("Try another word: the search looks in the names and descriptions of the cards"));
			}
		}
		else
			RenderVisualsSection(m_VisualsSection);
		if(m_VisualsSection != VISUALS_COSMETICS)
			m_WheelCaptured = false;
		if(m_ScrollToHighlight)
		{
			const float CardY = HighlightedCardY();
			if(CardY >= 0.0f)
			{
				m_ScrollTarget = std::max(CardY - S(12.0f), 0.0f);
				m_ScrollToHighlight = false;
			}
		}
		ImGui::Dummy(ImVec2(0.0f, S(12.0f)));
		EndSmoothScroll();
		// the list of the column for the next frame
		std::vector<std::pair<std::string, std::string>> vCards;
		for(const SDrawnCard &Card : DrawnCards())
			vCards.emplace_back(Card.m_Id, Card.m_Title);
		if(vCards != m_vVisCards)
		{
			if(vCards.size() != m_vVisCards.size())
				m_VisListTime = ImGui::GetTime();
			m_vVisCards = std::move(vCards);
		}
	}
	ImGui::PopFont();
	ImGui::End();
}

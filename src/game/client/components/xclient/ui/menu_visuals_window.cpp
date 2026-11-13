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

void CXcMenu::RenderVisualsWindow(float Dt) { RenderPanelWindow(Dt, false); }
void CXcMenu::RenderSettingsWindow(float Dt) { RenderPanelWindow(Dt, true); }

// the visuals or the settings in one compact window: the same look, the same moves
void CXcMenu::RenderPanelWindow(float Dt, bool SettingsKind)
{
	const STheme &t = Theme();
	const int Kind = SettingsKind ? 1 : 0;
	ImGuiIO &Io = ImGui::GetIO();
	const ImVec2 Display = Io.DisplaySize;
	const float W = std::min(S((float)g_Config.m_XcUiWindowW), Display.x - S(32.0f));
	const float H = std::min(S((float)g_Config.m_XcUiWindowH), Display.y - S(40.0f));
	const float RailW = S(64.0f), ColW = S(220.0f), TopH = S(60.0f);
	const char *apVisIcons[NUM_VISUALS] = {XC_ICON_SLIDERS, XC_ICON_USER, XC_ICON_BOLT, XC_ICON_HAT, XC_ICON_HAMMER, XC_ICON_CROSSHAIR, XC_ICON_GLOBE, XC_ICON_STAR, XC_ICON_USERS};
	const char *apVisLabels[NUM_VISUALS] = {Tr("General"), Tr("Tee"), Tr("Movement"), Tr("Cosmetics"), Tr("Weapons"), Tr("Crosshair"), Tr("World"), Tr("Events"), Tr("Names")};
	const char *apSetIcons[NUM_SETTINGS] = {XC_ICON_PALETTE, XC_ICON_IMAGE, XC_ICON_USER, XC_ICON_GLOBE, XC_ICON_KEYBOARD, XC_ICON_VOLUME, XC_ICON_COMMENTS, XC_ICON_DESKTOP, XC_ICON_LAYERS, XC_ICON_BRUSH, XC_ICON_CROSSHAIR, XC_ICON_SLIDERS, XC_ICON_GEAR};
	const char *apSetLabels[NUM_SETTINGS] = {Tr("Interface"), Tr("Background"), Tr("Player"), Tr("Online"), Tr("Controls"), Tr("Sound"), Tr("Chat"), Tr("Graphics"), Tr("HUD"), Tr("Textures"), Tr("War list"), "TClient", "DDNet"};
	const char *const *apIcons = SettingsKind ? apSetIcons : apVisIcons;
	const char *const *apLabels = SettingsKind ? apSetLabels : apVisLabels;
	const int NumSections = SettingsKind ? (int)NUM_SETTINGS : (int)NUM_VISUALS;
	int &Section = SettingsKind ? m_SettingsSection : m_VisualsSection;
	std::string &Picked = m_aVisPickedCard[Kind];
	const auto RenderSection = [&](int i) {
		if(SettingsKind)
			RenderSettingsSection(i);
		else
			RenderVisualsSection(i);
	};
	// the war list is one page of its own (a form and the rows): shown whole, the column scrolls to its parts
	const auto Whole = [&](int i) { return SettingsKind && i == SETTINGS_WARLIST; };

	// the place: the middle at first, then where it was dragged; it follows on a spring
	if(m_aVisWinTarget[0] < 0.0f || m_VisWinOpen <= 0.0f)
	{
		m_aVisWinTarget[0] = (Display.x - W) * 0.5f;
		m_aVisWinTarget[1] = (Display.y - H) * 0.5f;
	}
	// it stays whole on the screen, so its round corners are always seen
	const float Edge = S(8.0f);
	m_aVisWinTarget[0] = std::clamp(m_aVisWinTarget[0], Edge, std::max(Edge, Display.x - W - Edge));
	m_aVisWinTarget[1] = std::clamp(m_aVisWinTarget[1], Edge, std::max(Edge, Display.y - H - Edge));
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
	// the panel, the lines between the parts
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
		const float Step = std::min(S(48.0f), (H - S(140.0f)) / NumSections);
		const float KnobY = Animate(ImGui::GetID("##vis_rail_knob"), Top + Section * Step, 16.0f);
		if(!Searching)
		{
			const ImVec2 KMin(Min.x + (RailW - B) * 0.5f, KnobY), KMax(KMin.x + B, KMin.y + B);
			pDraw->AddRectFilled(KMin, KMax, Col(ImVec4(1, 1, 1, 0.08f)), S(11.0f));
			pDraw->AddRectFilled(ImVec2(Min.x + S(1.0f), KMin.y + S(12.0f)), ImVec2(Min.x + S(4.0f), KMax.y - S(12.0f)), Col(t.m_Accent), S(2.0f));
		}
		for(int i = 0; i < NumSections; i++)
		{
			const ImVec2 P(Min.x + (RailW - B) * 0.5f, Top + i * Step);
			ImGui::SetCursorScreenPos(P);
			ImGui::PushID(i);
			const bool Clicked = ImGui::InvisibleButton("##vis_rail", ImVec2(B, B));
			const bool Hovered = ImGui::IsItemHovered();
			const float Hover = Animate(ImGui::GetID("##h"), (Hovered || (i == Section && !Searching)) ? 1.0f : 0.0f, 16.0f);
			const float Press = Animate(ImGui::GetID("##p"), ImGui::IsItemActive() ? 1.0f : 0.0f, 22.0f);
			ImGui::PopID();
			DrawIcon(pDraw, Fonts().m_pIcons, Fonts().m_pIcons->FontSize * (1.0f - 0.1f * Press), ImVec2(P.x + B * 0.5f, P.y + B * 0.5f), Col(Mix(t.m_TextMuted, t.m_Text, Hover)), apIcons[i]);
			if(Hovered)
				ImGui::SetTooltip("%s", apLabels[i]);
			if(Clicked && (i != Section || Searching))
			{
				Section = i;
				m_aVisSearch[0] = '\0';
				m_ScrollTarget = 0.0f;
				Picked.clear();
				PlayUiSound(SOUND_TAB);
			}
		}
		// the settings
		const ImVec2 P(Min.x + (RailW - B) * 0.5f, Max.y - B - S(14.0f));
		ImGui::SetCursorScreenPos(P);
		const bool Clicked = ImGui::InvisibleButton("##vis_settings", ImVec2(B, B));
		const float Hover = Animate(ImGui::GetID("##vis_settings_h"), ImGui::IsItemHovered() ? 1.0f : 0.0f, 16.0f);
		// the other window: the visuals from the settings, the settings from the visuals
		DrawIcon(pDraw, Fonts().m_pIcons, Fonts().m_pIcons->FontSize, ImVec2(P.x + B * 0.5f, P.y + B * 0.5f), Col(Mix(t.m_TextMuted, t.m_Text, Hover)), SettingsKind ? XC_ICON_WAND : XC_ICON_GEAR);
		if(ImGui::IsItemHovered())
			ImGui::SetTooltip("%s", SettingsKind ? Tr("Visuals") : Tr("Settings"));
		if(Clicked)
		{
			PlayUiSound(SOUND_TAB);
			m_Page = SettingsKind ? PAGE_VISUALS : PAGE_SETTINGS;
			m_aVisSearch[0] = '\0';
			m_PageAppear = 0.0f;
		}
	}

	// ---- the column: the section name and its cards; a click scrolls to the card
	{
		const float X = Min.x + RailW;
		ImFont *pHead = Fonts().m_pTitle;
		const char *pHeadText = Searching ? Tr("Search") : apLabels[Section];
		pDraw->AddText(pHead, pHead->FontSize * 1.1f, ImVec2(X + S(18.0f), Min.y + S(20.0f)), Col(t.m_Text), pHeadText);
		const float Top = Min.y + S(60.0f);
		const float RowH = S(34.0f);
		ImGui::SetCursorScreenPos(ImVec2(X + S(10.0f), Top));
		BeginInnerScroll("##vis_cards_list", ImVec2(ColW - S(20.0f), Max.y - Top - S(12.0f)), ImGuiWindowFlags_NoScrollbar);
		ImDrawList *pList = ImGui::GetWindowDrawList();
		const ImVec2 ListMin = ImGui::GetCursorScreenPos();
		int PickedRow = -1;
		for(int i = 0; i < (int)m_vVisCards.size(); i++)
			if(m_vVisCards[i].first == Picked)
				PickedRow = i;
		// rows sit RowH + the item spacing apart
		const float Step = RowH + ImGui::GetStyle().ItemSpacing.y;
		const float KnobY = Animate(ImGui::GetID("##vis_col_knob"), ListMin.y + std::max(PickedRow, 0) * Step, 16.0f);
		const float KnobA = Animate(ImGui::GetID("##vis_col_knob_a"), PickedRow >= 0 ? 1.0f : 0.0f, 12.0f);
		if(KnobA > 0.01f)
			pList->AddRectFilled(ImVec2(ListMin.x, KnobY), ImVec2(ListMin.x + ColW - S(20.0f), KnobY + RowH), Col(ImVec4(1, 1, 1, 0.06f), KnobA), S(9.0f));
		for(int i = 0; i < (int)m_vVisCards.size(); i++)
		{
			ImGui::PushID(i);
			const ImVec2 P = ImGui::GetCursorScreenPos();
			const bool Clicked = ImGui::InvisibleButton("##vis_card", ImVec2(ColW - S(20.0f), RowH));
			const float Hover = Animate(ImGui::GetID("##h"), (ImGui::IsItemHovered() || i == PickedRow) ? 1.0f : 0.0f, 16.0f);
			// the rows come in one after another when the list changes
			const float In = EaseOut((float)(ImGui::GetTime() - m_VisListTime) * 4.0f - i * 0.12f);
			ImGui::PopID();
			char aTitle[128];
			XcFitText(Fonts().m_pLabel, Fonts().m_pLabel->FontSize, m_vVisCards[i].second.c_str(), ColW - S(44.0f), aTitle, sizeof(aTitle));
			pList->AddText(Fonts().m_pLabel, Fonts().m_pLabel->FontSize, ImVec2(P.x + S(10.0f) - (1.0f - In) * S(8.0f), std::floor(P.y + RowH * 0.5f - TextMiddle(Fonts().m_pLabel, Fonts().m_pLabel->FontSize) + 0.5f)), Col(Mix(t.m_TextMuted, t.m_Text, Hover), In), aTitle);
			if(Clicked)
			{
				PlayUiSound(SOUND_CLICK);
				if(Searching)
				{
					// from the search: the card's own page
					Picked = m_vVisCards[i].first;
					if(m_aVisCardSection.count(Picked))
						Section = m_aVisCardSection[Picked];
					m_aVisSearch[0] = '\0';
				}
				else
				{
					Picked = m_vVisCards[i].first;
					if(Whole(Section))
					{
						HighlightCard(Picked.c_str());
						m_ScrollToHighlight = true;
					}
				}
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
		EndInnerScroll();
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
		ImGui::InputTextWithHint("##vis_search", SettingsKind ? Tr("Search the settings") : Tr("Search the visuals"), m_aVisSearch, sizeof(m_aVisSearch));
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
			// in game back to the game, in the main menu to the home
			if(IsOnline())
				SetActive(false);
			else
				m_Page = PAGE_HOME;
		}
	}

	// ---- the cards: the section, or all sections filtered by the search
	{
		const float X = Min.x + RailW + ColW;
		const std::string Query = m_aVisSearch;
		if(Section != m_VisLastSection || Kind != m_VisLastKind || Query != m_VisLastQuery || Picked != m_VisLastPicked)
		{
			// the list on the left comes in again only for a new section or search; picking a card in it only
			// changes the card on the right, the list stays as it is
			if(Section != m_VisLastSection || Kind != m_VisLastKind || Query != m_VisLastQuery)
				m_VisListTime = ImGui::GetTime();
			m_VisLastKind = Kind;
			m_VisLastPicked = Picked;
			m_VisLastSection = Section;
			m_VisLastQuery = Query;
			PageEntered();
			m_ScrollTarget = 0.0f;
			if(SettingsKind)
			{
				g_Config.m_XcMenuSettingsSection = Section;
				m_BindWaiting = -1;
			}
			else
				g_Config.m_XcMenuVisualsSection = Section;
		}
		ImGui::SetCursorScreenPos(ImVec2(X + S(18.0f), Min.y + TopH + S(16.0f)));
		ClearDrawnCards();
		BeginSmoothScroll("##vis_window_scroll", ImVec2(Max.x - X - S(30.0f), Max.y - Min.y - TopH - S(28.0f)), ImGuiWindowFlags_NoScrollbar);
		if(Searching)
		{
			SetCardFilter(m_aVisSearch);
			BeginCardGrid("##vis_found", 2);
			MergeCardGrids(true);
			for(int i = 0; i < NumSections; i++)
			{
				if(Whole(i))
					continue;
				const size_t Before = DrawnCards().size();
				RenderSection(i);
				for(size_t k = Before; k < DrawnCards().size(); k++)
					m_aVisCardSection[DrawnCards()[k].m_Id] = i;
			}
			MergeCardGrids(false);
			EndCardGrid();
			SetCardFilter("");
			if(DrawnCards().empty())
			{
				Title(Tr("Nothing found"));
				Description(Tr("Try another word: the search looks in the names and descriptions of the cards"));
			}
		}
		else if(Whole(Section))
			RenderSection(Section);
		else
		{
			// one card at a time, the whole width: the column on the left picks it, like the modules of a click menu
			// a page whose cards are not known yet shows its first card at once (no empty frame before it)
			const bool Known = !Picked.empty() && std::any_of(m_vVisCards.begin(), m_vVisCards.end(), [&](const auto &Card) { return Card.first == Picked; });
			SetCardOnly(Known ? Picked.c_str() : "\x02");
			BeginCardGrid("##vis_one", 1);
			MergeCardGrids(true);
			RenderSection(Section);
			MergeCardGrids(false);
			EndCardGrid();
			SetCardOnly("");
			if(!Known && !DrawnCards().empty())
				Picked = DrawnCards().front().m_Id;
			for(const SDrawnCard &Card : DrawnCards())
				m_aVisCardSection[Card.m_Id] = Section;
		}
		if(SettingsKind || Section != VISUALS_COSMETICS)
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
			m_vVisCards = std::move(vCards);
		}
	}
	const bool OverRail = !SettingsKind && ImGui::IsMouseHoveringRect(Min, ImVec2(Min.x + RailW, Max.y));
	ResizeGrip(pDraw, Max);
	ImGui::PopFont();
	ImGui::End();

	// the names of the sections slide out of the rail under the mouse: to the left of the window (to its right
	// side over the column when there is no room on the left)
	if(SettingsKind)
		m_RailFly = 0.0f;
	else
	{
		const float FlyW = S(190.0f);
		const bool Left = Min.x - FlyW >= S(4.0f);
		const float Top = Min.y + S(70.0f);
		const float Step = std::min(S(48.0f), (H - S(140.0f)) / NumSections);
		const float B = S(42.0f);
		const ImVec2 FMin(Left ? Min.x - FlyW + S(6.0f) : Min.x + RailW, Min.y + S(56.0f));
		const ImVec2 FMax(FMin.x + FlyW - S(6.0f), Top + NumSections * Step + S(8.0f));
		const bool OverFly = m_RailFly > 0.5f && ImGui::IsMouseHoveringRect(FMin, FMax);
		m_RailFly = std::clamp(m_RailFly + Dt / (OverRail || OverFly ? 0.18f : -0.22f), 0.0f, 1.0f);
		if(m_RailFly > 0.0f)
		{
			const float k = EaseOut(m_RailFly);
			const float Shift = (1.0f - k) * S(16.0f) * (Left ? 1.0f : -1.0f);
			ImGui::SetNextWindowPos(ImVec2(FMin.x + Shift, FMin.y));
			ImGui::SetNextWindowSize(ImVec2(FMax.x - FMin.x, FMax.y - FMin.y));
			ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
			ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
			ImGui::PushStyleVar(ImGuiStyleVar_Alpha, ImGui::GetStyle().Alpha * k);
			ImGui::Begin("##rail_fly", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoFocusOnAppearing);
			ImDrawList *pFly = ImGui::GetWindowDrawList();
			const ImVec2 PMin(FMin.x + Shift, FMin.y), PMax(FMax.x + Shift, FMax.y);
			pFly->AddRectFilled(PMin, PMax, Col(ImVec4(t.m_Panel.x, t.m_Panel.y, t.m_Panel.z, 0.97f)), S(14.0f));
			pFly->AddRect(PMin, PMax, Col(t.m_Border), S(14.0f), 0, 1.0f);
			ImFont *pLabel = Fonts().m_pLabel;
			for(int i = 0; i < NumSections; i++)
			{
				const float RowY = Top + i * Step;
				ImGui::SetCursorScreenPos(ImVec2(PMin.x + S(6.0f), RowY));
				ImGui::PushID(i);
				const bool Clicked = ImGui::InvisibleButton("##fly_row", ImVec2(PMax.x - PMin.x - S(12.0f), B));
				const float Hover = Animate(ImGui::GetID("##fly_h"), (ImGui::IsItemHovered() || (i == Section && !Searching)) ? 1.0f : 0.0f, 16.0f);
				ImGui::PopID();
				if(Hover > 0.01f)
					pFly->AddRectFilled(ImVec2(PMin.x + S(6.0f), RowY), ImVec2(PMax.x - S(6.0f), RowY + B), Col(ImVec4(1, 1, 1, 0.07f * Hover)), S(10.0f));
				// one after another, the names come in from the rail side
				const float Each = EaseOut(m_RailFly * 1.6f - i * 0.04f);
				const float TextX = PMin.x + S(18.0f) + (1.0f - Each) * S(10.0f) * (Left ? 1.0f : -1.0f);
				pFly->AddText(pLabel, pLabel->FontSize, ImVec2(TextX, std::floor(RowY + B * 0.5f - TextMiddle(pLabel, pLabel->FontSize) + 0.5f)), Col(Mix(t.m_TextSecondary, t.m_Text, Hover), Each), apLabels[i]);
				if(Clicked && (i != Section || Searching))
				{
					Section = i;
					m_aVisSearch[0] = '\0';
					m_ScrollTarget = 0.0f;
					Picked.clear();
					PlayUiSound(SOUND_TAB);
				}
			}
			ImGui::End();
			ImGui::PopStyleVar(3);
		}
	}
}

// a page that is not made of cards (the demos, the votes) in the same window: a title, a close button, the page
void CXcMenu::RenderPageWindow(float Dt, const char *pIcon, const char *pTitle, void (CXcMenu::*pfnPage)())
{
	const STheme &t = Theme();
	ImGuiIO &Io = ImGui::GetIO();
	const ImVec2 Display = Io.DisplaySize;
	const float W = std::min(S((float)g_Config.m_XcUiWindowW), Display.x - S(32.0f));
	const float H = std::min(S((float)g_Config.m_XcUiWindowH), Display.y - S(40.0f));
	const float TopH = S(60.0f);
	if(m_aVisWinTarget[0] < 0.0f || m_VisWinOpen <= 0.0f)
	{
		m_aVisWinTarget[0] = (Display.x - W) * 0.5f;
		m_aVisWinTarget[1] = (Display.y - H) * 0.5f;
	}
	const float Edge = S(8.0f);
	m_aVisWinTarget[0] = std::clamp(m_aVisWinTarget[0], Edge, std::max(Edge, Display.x - W - Edge));
	m_aVisWinTarget[1] = std::clamp(m_aVisWinTarget[1], Edge, std::max(Edge, Display.y - H - Edge));
	if(m_VisWinOpen <= 0.0f)
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
	m_VisWinOpen = std::min(m_VisWinOpen + Dt / 0.4f, 1.0f);
	const float Open = EaseOut(m_VisWinOpen);
	const ImVec2 Min(m_aVisWinPos[0], m_aVisWinPos[1] + (1.0f - Open) * S(14.0f));
	const ImVec2 Max(Min.x + W, Min.y + H);

	ImGui::GetStyle().Alpha *= Open;
	ImGui::SetNextWindowPos(Min);
	ImGui::SetNextWindowSize(ImVec2(W, H));
	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
	ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
	ImGui::Begin("##xc_page_window", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoNav);
	ImGui::PopStyleVar(2);
	ImGui::PushFont(Fonts().m_pBody);
	ImDrawList *pDraw = ImGui::GetWindowDrawList();
	const float Round = S(18.0f);
	pDraw->AddRectFilled(Min, Max, Col(ImVec4(t.m_Panel.x, t.m_Panel.y, t.m_Panel.z, 0.95f)), Round);
	pDraw->AddRect(Min, Max, Col(t.m_Border), Round, 0, 1.0f);
	pDraw->AddLine(ImVec2(Min.x, Min.y + TopH), ImVec2(Max.x, Min.y + TopH), Col(t.m_Border), 1.0f);

	// the top: the logo, the icon and the name of the page, the profile, close; the free part drags
	{
		DrawLogo(pDraw, ImVec2(Min.x + S(32.0f), Min.y + TopH * 0.5f), S(28.0f), t.m_Accent, 1.0f, false);
		ImFont *pHead = Fonts().m_pTitle;
		const float HeadSize = pHead->FontSize * 1.15f;
		DrawIcon(pDraw, Fonts().m_pIcons, Fonts().m_pIcons->FontSize, ImVec2(Min.x + S(78.0f), Min.y + TopH * 0.5f), Col(t.m_Accent), pIcon);
		pDraw->AddText(pHead, HeadSize, ImVec2(Min.x + S(96.0f), Min.y + (TopH - HeadSize) * 0.5f), Col(t.m_Text), pTitle);
		const float B = S(32.0f);
		const ImVec2 CMin(Max.x - S(16.0f) - B, Min.y + (TopH - B) * 0.5f);
		ImGui::SetCursorScreenPos(CMin);
		const bool Close = ImGui::InvisibleButton("##page_close", ImVec2(B, B));
		const float CloseHover = Animate(ImGui::GetID("##page_close_h"), ImGui::IsItemHovered() ? 1.0f : 0.0f, 16.0f);
		pDraw->AddRectFilled(CMin, ImVec2(CMin.x + B, CMin.y + B), Col(ImVec4(1, 1, 1, 0.05f * CloseHover)), S(9.0f));
		pDraw->AddRect(CMin, ImVec2(CMin.x + B, CMin.y + B), Col(t.m_Border), S(9.0f), 0, 1.0f);
		DrawIcon(pDraw, Fonts().m_pSmall, Fonts().m_pSmall->FontSize, ImVec2(CMin.x + B * 0.5f, CMin.y + B * 0.5f), Col(Mix(t.m_TextMuted, t.m_Text, CloseHover)), XC_ICON_XMARK);
		const float DragL = Min.x + S(110.0f) + pHead->CalcTextSizeA(HeadSize, FLT_MAX, 0.0f, pTitle).x;
		const float DragR = CMin.x - S(12.0f);
		if(DragR > DragL)
		{
			ImGui::SetCursorScreenPos(ImVec2(DragL, Min.y));
			ImGui::InvisibleButton("##page_drag", ImVec2(DragR - DragL, TopH));
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

	// the page itself, with room around it; it comes in like a new page
	if(m_LastPageWindow != m_Page)
	{
		m_LastPageWindow = m_Page;
		PageEntered();
	}
	const float Pad = S(18.0f);
	ImGui::SetCursorScreenPos(ImVec2(Min.x + Pad, Min.y + TopH + Pad));
	ImGui::BeginChild("##page_window_body", ImVec2(W - Pad * 2.0f, H - TopH - Pad * 2.0f), ImGuiChildFlags_None, ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
	(this->*pfnPage)();
	ImGui::EndChild();
	ResizeGrip(pDraw, Max);
	ImGui::PopFont();
	ImGui::End();
}

// the bottom right corner of the windows: dragged, it changes their size; three short lines along the round corner
// show it, a little brighter under the mouse
void CXcMenu::ResizeGrip(ImDrawList *pDraw, ImVec2 Max)
{
	const STheme &t = Theme();
	const float G = S(22.0f);
	ImGui::SetCursorScreenPos(ImVec2(Max.x - G, Max.y - G));
	ImGui::InvisibleButton("##window_grip", ImVec2(G, G));
	const bool Active = ImGui::IsItemActive();
	const float Hover = Animate(ImGui::GetID("##window_grip_h"), (ImGui::IsItemHovered() || Active) ? 1.0f : 0.0f, 14.0f);
	if(Active && ImGui::IsMouseDragging(ImGuiMouseButton_Left, 0.0f))
	{
		const float Scale = std::max(S(1.0f), 0.01f);
		m_aGripSize[0] += ImGui::GetIO().MouseDelta.x / Scale;
		m_aGripSize[1] += ImGui::GetIO().MouseDelta.y / Scale;
		g_Config.m_XcUiWindowW = std::clamp((int)std::round(m_aGripSize[0]), 760, 2400);
		g_Config.m_XcUiWindowH = std::clamp((int)std::round(m_aGripSize[1]), 480, 1600);
	}
	else
	{
		m_aGripSize[0] = (float)g_Config.m_XcUiWindowW;
		m_aGripSize[1] = (float)g_Config.m_XcUiWindowH;
	}
	if(ImGui::IsItemHovered() || Active)
		ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeNWSE);
	const ImU32 Color = Col(Mix(t.m_TextMuted, t.m_Text, Hover), 0.35f + 0.5f * Hover);
	for(int i = 0; i < 3; i++)
	{
		const float d = S(6.0f) + i * S(4.0f);
		pDraw->AddLine(ImVec2(Max.x - S(5.0f) - d, Max.y - S(5.0f)), ImVec2(Max.x - S(5.0f), Max.y - S(5.0f) - d), Color, S(1.4f));
	}
}

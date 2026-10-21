// Top bar of the menu: the logo, the page tabs and quick buttons. Which tabs and buttons it shows and their
// order are set by the player (Settings > Interface > Top bar); the ones taken off go into the "..." menu.
#include "feedback.h"
#include "icons.h"
#include "lang.h"
#include "menu.h"
#include "theme.h"
#include "widgets.h"

#include <base/str.h>

#include <engine/client.h>
#include <engine/external/imgui/imgui.h>
#include <engine/shared/config.h>

#include <game/client/gameclient.h>

#include <algorithm>
#include <vector>

using namespace XcUi;

namespace
{
const char *const CLIENT_NAME = "Exora Client";

struct STopTab
{
	const char *m_pId;
	int m_Page;
	const char *m_pIcon;
	const char *m_pLabel;
};
const STopTab s_aTabs[] = {
	{"home", CXcMenu::PAGE_HOME, XC_ICON_HOME, "Home"},
	{"servers", CXcMenu::PAGE_SERVERS, XC_ICON_SERVER, "Servers"},
	{"visuals", CXcMenu::PAGE_VISUALS, XC_ICON_WAND, "Visuals"},
	{"configs", CXcMenu::PAGE_CONFIGS, XC_ICON_FOLDER, "Configs"},
	{"demos", CXcMenu::PAGE_DEMOS, XC_ICON_FILM, "Demos"},
	{"votes", CXcMenu::PAGE_VOTES, XC_ICON_CHECK, "Votes"},
	{"settings", CXcMenu::PAGE_SETTINGS, XC_ICON_GEAR, "Settings"},
};
static_assert(std::size(s_aTabs) == CXcMenu::NUM_PAGES, "every page needs a tab");

enum
{
	BUTTON_SEARCH = 0,
	BUTTON_LANGUAGE,
	BUTTON_THEME,
	BUTTON_CLASSIC,
	BUTTON_PROFILE,
	NUM_BUTTONS,
};
struct STopButton
{
	const char *m_pId;
	const char *m_pIcon;
	const char *m_pLabel;
};
const STopButton s_aButtons[NUM_BUTTONS] = {
	{"search", XC_ICON_SEARCH, "Search"},
	{"language", XC_ICON_GLOBE, "Language"},
	{"theme", XC_ICON_MOON, "Theme"},
	{"classic", XC_ICON_WINDOW, "Classic menu"},
	{"profile", XC_ICON_USER, "Profile"},
};

// ids from a config string, in order, each once; unknown words are skipped
template<typename T, size_t N>
std::vector<int> ParseOrder(const char *pList, const T (&aItems)[N])
{
	std::vector<int> vOrder;
	char aWord[32];
	const char *p = pList;
	while((p = str_next_token(p, " ,", aWord, sizeof(aWord))))
	{
		for(int i = 0; i < (int)N; i++)
			if(str_comp(aItems[i].m_pId, aWord) == 0 && std::find(vOrder.begin(), vOrder.end(), i) == vOrder.end())
				vOrder.push_back(i);
	}
	return vOrder;
}

template<typename T, size_t N>
void WriteOrder(char *pBuf, int BufSize, const std::vector<int> &vOrder, const T (&aItems)[N])
{
	pBuf[0] = '\0';
	for(int Index : vOrder)
	{
		if(pBuf[0])
			str_append(pBuf, " ", BufSize);
		str_append(pBuf, aItems[Index].m_pId, BufSize);
	}
}

void DrawChevron(ImDrawList *pDraw, ImVec2 Center, float Size, bool Up, ImU32 Color)
{
	const float h = Size * 0.35f * (Up ? -1.0f : 1.0f);
	pDraw->AddLine(ImVec2(Center.x - Size * 0.5f, Center.y - h), ImVec2(Center.x, Center.y + h), Color, 2.0f);
	pDraw->AddLine(ImVec2(Center.x, Center.y + h), ImVec2(Center.x + Size * 0.5f, Center.y - h), Color, 2.0f);
}

void DrawDots(ImDrawList *pDraw, ImVec2 Center, float Size, ImU32 Color)
{
	for(int i = -1; i <= 1; i++)
		pDraw->AddCircleFilled(ImVec2(Center.x + i * Size * 0.32f, Center.y), Size * 0.09f, Color, 12);
}
} // namespace

bool CXcMenu::TopbarButtonAction(int Button)
{
	switch(Button)
	{
	case BUTTON_SEARCH: OpenSearch(); return true;
	case BUTTON_LANGUAGE: SetConfigString("cl_languagefile", IsRussian() ? "" : "languages/russian.txt"); return true;
	case BUTTON_THEME: SetConfigInt("xc_ui_theme", g_Config.m_XcUiTheme ? 0 : 1); return true;
	case BUTTON_CLASSIC: m_WantClassic = true; return true;
	case BUTTON_PROFILE:
		m_Page = PAGE_SETTINGS;
		m_SettingsSection = SETTINGS_PLAYER;
		m_ScrollTarget = 0.0f;
		return true;
	}
	return false;
}

void CXcMenu::RenderTopBar(float Width)
{
	const STheme &t = Theme();
	const bool Panel = g_Config.m_XcTopbarStyle == 1;
	const float BarHeight = Panel ? S(56.0f) : S(40.0f);
	const float Pad = Panel ? S(10.0f) : 0.0f;
	const ImVec2 Start = ImGui::GetCursorScreenPos();
	ImDrawList *pDraw = ImGui::GetWindowDrawList();
	const float MidY = Start.y + BarHeight * 0.5f;
	ImFont *pTitle = Fonts().m_pTitle;
	ImFont *pSmall = Fonts().m_pSmall;
	ImFont *pLabel = Fonts().m_pLabel;
	const auto TextWidth = [](ImFont *pFont, const char *pText) { return pFont->CalcTextSizeA(pFont->FontSize, FLT_MAX, 0.0f, pText).x; };
	if(Panel)
		PanelRect(pDraw, Start, ImVec2(Start.x + Width, Start.y + BarHeight), S(16.0f), t.m_Panel);

	// left: logo, the client name and, on the panel, where you are
	const float LogoSize = Panel ? S(32.0f) : S(30.0f);
	DrawLogo(pDraw, ImVec2(Start.x + Pad + LogoSize * 0.5f + (Panel ? 0.0f : S(2.0f)), MidY), LogoSize, t.m_Accent);
	const float TextX = Start.x + Pad + LogoSize + (Panel ? S(10.0f) : S(12.0f));
	char aStatus[128] = "";
	if(Panel)
	{
		if(Client()->State() == IClient::STATE_DEMOPLAYBACK)
			str_copy(aStatus, Tr("Watching a demo"));
		else if(IsOnline() && Client()->ServerInfo().m_aMap[0])
			str_format(aStatus, sizeof(aStatus), "%s · %s", Tr("In game"), Client()->ServerInfo().m_aMap);
		else
			str_copy(aStatus, Tr("Main menu"));
		const float TextTop = MidY - (pTitle->FontSize + pSmall->FontSize + S(1.0f)) * 0.5f;
		pDraw->AddText(pTitle, pTitle->FontSize, ImVec2(TextX, TextTop), Col(t.m_Text), CLIENT_NAME);
		pDraw->AddText(pSmall, pSmall->FontSize, ImVec2(TextX, TextTop + pTitle->FontSize + S(1.0f)), Col(t.m_TextMuted), aStatus);
	}
	else
		pDraw->AddText(pTitle, pTitle->FontSize, ImVec2(TextX, MidY - pTitle->FontSize * 0.5f), Col(t.m_Text), CLIENT_NAME);
	const float LeftEnd = TextX + std::max(TextWidth(pTitle, CLIENT_NAME), TextWidth(pSmall, aStatus)) + S(20.0f);

	// what the player put on the bar; voting only exists on a server
	const bool CanVote = IsOnline() && Client()->State() != IClient::STATE_DEMOPLAYBACK;
	if(m_Page == PAGE_VOTES && !CanVote)
		m_Page = PAGE_HOME;
	std::vector<int> vTabs = ParseOrder(g_Config.m_XcTopbarTabs, s_aTabs);
	std::vector<int> vHiddenTabs;
	for(int i = 0; i < NUM_PAGES; i++)
		if(std::find(vTabs.begin(), vTabs.end(), i) == vTabs.end())
			vHiddenTabs.push_back(i);
	const auto Usable = [&](int Tab) { return s_aTabs[Tab].m_Page != PAGE_VOTES || CanVote; };
	vTabs.erase(std::remove_if(vTabs.begin(), vTabs.end(), [&](int Tab) { return !Usable(Tab); }), vTabs.end());
	vHiddenTabs.erase(std::remove_if(vHiddenTabs.begin(), vHiddenTabs.end(), [&](int Tab) { return !Usable(Tab); }), vHiddenTabs.end());
	const std::vector<int> vButtons = ParseOrder(g_Config.m_XcTopbarButtons, s_aButtons);
	std::vector<int> vHiddenButtons;
	for(int i = 0; i < NUM_BUTTONS; i++)
		if(std::find(vButtons.begin(), vButtons.end(), i) == vButtons.end())
			vHiddenButtons.push_back(i);
	const bool HasMore = !vHiddenTabs.empty() || !vHiddenButtons.empty();

	const char *apIcons[NUM_PAGES];
	const char *apLabels[NUM_PAGES];
	const char *apNoLabels[NUM_PAGES];
	int Current = -1;
	for(int i = 0; i < (int)vTabs.size(); i++)
	{
		apIcons[i] = s_aTabs[vTabs[i]].m_pIcon;
		apLabels[i] = Tr(s_aTabs[vTabs[i]].m_pLabel);
		apNoLabels[i] = "";
		if(s_aTabs[vTabs[i]].m_Page == m_Page)
			Current = i;
	}
	const int NumTabs = vTabs.size();
	const auto CapsuleWidthOf = [&](bool Labels) {
		float Total = S(8.0f);
		for(int i = 0; i < NumTabs; i++)
			Total += S(32.0f) + S(8.0f) + TextWidth(pLabel, apIcons[i]) + (Labels ? TextWidth(pLabel, apLabels[i]) : 0.0f);
		return Total;
	};

	// widths of the right side; the panel has wide search and profile, which fold into icons when space runs out
	const float ButtonSize = S(34.0f);
	const float Gap = S(8.0f);
	const char *pName = Client()->PlayerName();
	const float NameWidth = std::min(TextWidth(pLabel, pName), S(130.0f));
	const float LangWidth = Panel ? std::max(TextWidth(pLabel, "RU") + S(24.0f), ButtonSize) : ButtonSize;
	const auto ButtonWidth = [&](int Button, bool Compact) {
		if(Panel && !Compact && Button == BUTTON_SEARCH)
			return S(180.0f);
		if(Panel && !Compact && Button == BUTTON_PROFILE)
			return ButtonSize + S(20.0f) + NameWidth;
		if(Button == BUTTON_LANGUAGE)
			return LangWidth;
		return ButtonSize;
	};
	const auto RightWidth = [&](bool Compact) {
		float Total = Pad + ButtonSize + (Panel ? Gap + S(6.0f) : Gap);
		if(HasMore)
			Total += ButtonSize + Gap;
		for(int Button : vButtons)
			Total += ButtonWidth(Button, Compact) + Gap;
		return Total;
	};
	const float Room = Width - (LeftEnd - Start.x) - S(20.0f);
	int Level = 0;
	if(CapsuleWidthOf(true) + RightWidth(false) > Room)
		Level = CapsuleWidthOf(true) + RightWidth(true) > Room ? 2 : 1;
	const bool Compact = !Panel || Level >= 1;

	// middle: the tabs, centered, but never under the logo or the buttons
	if(NumTabs > 0)
	{
		const float CapsuleWidth = CapsuleWidthOf(Level < 2);
		const float RightStart = Start.x + Width - RightWidth(Compact);
		const float CapsuleX = std::clamp(Start.x + (Width - CapsuleWidth) * 0.5f, LeftEnd, std::max(RightStart - S(12.0f) - CapsuleWidth, LeftEnd));
		ImGui::SetCursorScreenPos(ImVec2(CapsuleX, MidY - S(20.0f)));
		int Selected = Current;
		if(TabCapsule("##pages", &Selected, apIcons, Level < 2 ? apLabels : apNoLabels, NumTabs) && Selected >= 0)
		{
			m_Page = s_aTabs[vTabs[Selected]].m_Page;
			m_ScrollTarget = 0.0f;
			m_DemosDirty = true; // new recordings show up when the page is opened again
		}
	}

	// right, from the edge: close, "...", then the buttons in the player's order
	float Right = Start.x + Width - Pad;
	const float ButtonY = MidY - ButtonSize * 0.5f;
	const auto Place = [&](float ItemWidth) {
		Right -= ItemWidth;
		ImGui::SetCursorScreenPos(ImVec2(Right, ButtonY));
		const ImVec2 Pos(Right, ButtonY);
		Right -= Gap;
		return Pos;
	};
	// a button with a soft background, like IconButton; the panel style keeps the background always on
	const auto Pill = [&](const char *pId, float PillWidth, bool Selected, const char *pTooltip) {
		const ImVec2 Pos = Place(PillWidth);
		const bool Pressed = ImGui::InvisibleButton(pId, ImVec2(PillWidth, ButtonSize));
		const bool Hovered = ImGui::IsItemHovered();
		const float Hover = Animate(ImGui::GetItemID(), (Hovered || Selected) ? 1.0f : 0.0f, 18.0f);
		const float Base = Panel ? 0.55f : 0.0f;
		pDraw->AddRectFilled(Pos, ImVec2(Pos.x + PillWidth, Pos.y + ButtonSize), Col(Selected ? ImVec4(t.m_Accent.x, t.m_Accent.y, t.m_Accent.z, 0.18f) : t.m_Track, Selected ? 1.0f : Base + Hover * (1.4f - Base)), S(10.0f));
		if(Pressed)
			PlayUiSound(SOUND_CLICK);
		if(pTooltip && Hovered)
			ImGui::SetTooltip("%s", pTooltip);
		return std::make_pair(Pressed, Pos);
	};
	const auto CenteredText = [&](ImVec2 Pos, float ItemWidth, const char *pText, const ImVec4 &Color) {
		pDraw->AddText(pLabel, pLabel->FontSize, ImVec2(Pos.x + (ItemWidth - TextWidth(pLabel, pText)) * 0.5f, MidY - pLabel->FontSize * 0.5f), Col(Color), pText);
	};

	Place(ButtonSize);
	if(IsOnline())
	{
		// Esc goes back to the game, the cross leaves the server
		if(IconButton(XC_ICON_XMARK, Tr("Disconnect")))
		{
			SetActive(false);
			Client()->Disconnect();
		}
	}
	else if(IconButton(XC_ICON_POWER, Tr("Quit")))
		Client()->Quit();
	if(Panel)
	{
		pDraw->AddLine(ImVec2(Right + Gap * 0.5f - S(2.0f), MidY - S(12.0f)), ImVec2(Right + Gap * 0.5f - S(2.0f), MidY + S(12.0f)), Col(t.m_Border), 1.0f);
		Right -= S(6.0f);
	}

	if(HasMore)
	{
		// everything taken off the bar is one click away
		const bool HiddenPageOpen = std::any_of(vHiddenTabs.begin(), vHiddenTabs.end(), [&](int Tab) { return s_aTabs[Tab].m_Page == m_Page; });
		const auto [Pressed, Pos] = Pill("##topbar_more", ButtonSize, HiddenPageOpen || ImGui::IsPopupOpen("##topbar_more_popup"), Tr("More"));
		DrawDots(pDraw, ImVec2(Pos.x + ButtonSize * 0.5f, MidY), ButtonSize * 0.6f, Col(HiddenPageOpen ? t.m_Accent : t.m_TextSecondary));
		if(Pressed)
			ImGui::OpenPopup("##topbar_more_popup");
		ImGui::SetNextWindowPos(ImVec2(Pos.x + ButtonSize, Pos.y + ButtonSize + S(6.0f)), ImGuiCond_Appearing, ImVec2(1.0f, 0.0f));
		ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(S(10.0f), S(10.0f)));
		if(ImGui::BeginPopup("##topbar_more_popup"))
		{
			const float RowWidth = S(220.0f);
			const auto Row = [&](const char *pIcon, const char *pText, bool Selected) {
				char aLabel[128];
				str_format(aLabel, sizeof(aLabel), "%s   %s", pIcon, pText);
				return Button(aLabel, Selected, RowWidth);
			};
			for(int Tab : vHiddenTabs)
			{
				ImGui::PushID(Tab);
				if(Row(s_aTabs[Tab].m_pIcon, Tr(s_aTabs[Tab].m_pLabel), s_aTabs[Tab].m_Page == m_Page))
				{
					m_Page = s_aTabs[Tab].m_Page;
					m_ScrollTarget = 0.0f;
					m_DemosDirty = true;
					ImGui::CloseCurrentPopup();
				}
				ImGui::PopID();
			}
			for(int Button : vHiddenButtons)
			{
				ImGui::PushID(100 + Button);
				if(Row(s_aButtons[Button].m_pIcon, Tr(s_aButtons[Button].m_pLabel), false))
				{
					TopbarButtonAction(Button);
					ImGui::CloseCurrentPopup();
				}
				ImGui::PopID();
			}
			ImGui::Dummy(ImVec2(0.0f, S(2.0f)));
			if(Row(XC_ICON_SLIDERS, Tr("Set up the bar"), false))
			{
				m_Page = PAGE_SETTINGS;
				m_SettingsSection = SETTINGS_INTERFACE;
				HighlightCard("##topbar");
				m_ScrollToHighlight = true;
				ImGui::CloseCurrentPopup();
			}
			ImGui::EndPopup();
		}
		ImGui::PopStyleVar();
	}

	for(auto It = vButtons.rbegin(); It != vButtons.rend(); ++It)
	{
		const int Button = *It;
		const float ItemWidth = ButtonWidth(Button, Compact);
		ImGui::PushID(Button);
		switch(Button)
		{
		case BUTTON_SEARCH:
		{
			const auto [Pressed, Pos] = Pill("##search", ItemWidth, m_SearchOpen, Compact ? Tr("Search (Ctrl+F)") : nullptr);
			if(Pressed)
				TopbarButtonAction(Button);
			if(Compact)
				CenteredText(Pos, ItemWidth, XC_ICON_SEARCH, m_SearchOpen ? t.m_Accent : t.m_TextSecondary);
			else
			{
				// looks like a field, with the shortcut on the right
				pDraw->AddText(pLabel, pLabel->FontSize, ImVec2(Pos.x + S(12.0f), MidY - pLabel->FontSize * 0.5f), Col(t.m_TextMuted), XC_ICON_SEARCH);
				pDraw->AddText(pLabel, pLabel->FontSize, ImVec2(Pos.x + S(20.0f) + TextWidth(pLabel, XC_ICON_SEARCH), MidY - pLabel->FontSize * 0.5f), Col(t.m_TextMuted), Tr("Search"));
				const char *pKeys = "Ctrl F";
				const ImVec2 KeySize = pSmall->CalcTextSizeA(pSmall->FontSize, FLT_MAX, 0.0f, pKeys);
				const ImVec2 KeyMin(Pos.x + ItemWidth - KeySize.x - S(20.0f), MidY - KeySize.y * 0.5f - S(3.0f));
				pDraw->AddRect(KeyMin, ImVec2(KeyMin.x + KeySize.x + S(10.0f), KeyMin.y + KeySize.y + S(6.0f)), Col(t.m_Border), S(5.0f));
				pDraw->AddText(pSmall, pSmall->FontSize, ImVec2(KeyMin.x + S(5.0f), KeyMin.y + S(3.0f)), Col(t.m_TextMuted), pKeys);
			}
			break;
		}
		case BUTTON_LANGUAGE:
		{
			const auto [Pressed, Pos] = Pill("##language", ItemWidth, false, IsRussian() ? "Switch to English" : "Переключить на русский");
			if(Pressed)
				TopbarButtonAction(Button);
			// the panel shows the current language, the classic bar a globe
			CenteredText(Pos, ItemWidth, Panel ? (IsRussian() ? "RU" : "EN") : XC_ICON_GLOBE, t.m_TextSecondary);
			break;
		}
		case BUTTON_THEME:
		{
			const auto [Pressed, Pos] = Pill("##theme", ItemWidth, false, t.m_Dark ? Tr("Light theme") : Tr("Dark theme"));
			if(Pressed)
				TopbarButtonAction(Button);
			CenteredText(Pos, ItemWidth, t.m_Dark ? XC_ICON_MOON : XC_ICON_SUN, t.m_TextSecondary);
			break;
		}
		case BUTTON_CLASSIC:
		{
			const auto [Pressed, Pos] = Pill("##classic", ItemWidth, false, Tr("Classic menu"));
			if(Pressed)
				TopbarButtonAction(Button);
			CenteredText(Pos, ItemWidth, XC_ICON_WINDOW, t.m_TextSecondary);
			break;
		}
		case BUTTON_PROFILE:
		{
			const bool Selected = m_Page == PAGE_SETTINGS && m_SettingsSection == SETTINGS_PLAYER;
			const auto [Pressed, Pos] = Pill("##profile", ItemWidth, Selected, Compact ? pName : Tr("Profile"));
			if(Pressed)
				TopbarButtonAction(Button);
			// the own tee; on the panel with the name next to it
			const float TeeSize = ButtonSize * (Panel ? 0.86f : 1.0f);
			const ImVec2 TeeCenter(Compact ? Pos.x + ButtonSize * 0.5f : Pos.x + S(4.0f) + TeeSize * 0.5f, MidY);
			const float Reach = Panel ? 0.65f : 0.7f;
			AddTeePreview(pDraw, ImVec2(TeeCenter.x - TeeSize * Reach, TeeCenter.y - TeeSize * Reach), ImVec2(TeeCenter.x + TeeSize * Reach, TeeCenter.y + TeeSize * Reach));
			if(!Compact)
			{
				const ImVec4 Clip(Pos.x, Pos.y, Pos.x + ButtonSize + S(6.0f) + NameWidth, Pos.y + ButtonSize);
				pDraw->AddText(pLabel, pLabel->FontSize, ImVec2(Pos.x + ButtonSize + S(6.0f), MidY - pLabel->FontSize * 0.5f), Col(Selected ? t.m_Accent : t.m_Text), pName, nullptr, 0.0f, &Clip);
			}
			break;
		}
		}
		ImGui::PopID();
	}

	ImGui::SetCursorScreenPos(ImVec2(Start.x, Start.y + BarHeight + S(12.0f)));
	if(m_WantClassic)
	{
		m_WantClassic = false;
		OpenClassicMenu();
	}
}

// Settings > Interface: the look of the bar, which tabs and buttons it has and their order
void CXcMenu::RenderTopbarCard()
{
	if(!BeginCard("##topbar", Tr("Top bar"), Tr("Turn tabs and buttons on or off and move them. The ones you turn off are in the ... menu of the bar")))
		return;
	ParamLabel(Tr("Look"));
	const char *apStyles[] = {Tr("Classic"), Tr("Panel")};
	ConfigSegmented("##topbar_style", "xc_topbar_style", &g_Config.m_XcTopbarStyle, apStyles, 2);

	const STheme &t = Theme();
	// one list: shown items first in their order with arrows, then the hidden ones
	const auto List = [&](const char *pTitle, const char *pConfig, char *pValue, int ValueSize, auto &aItems) {
		ParamLabel(pTitle);
		std::vector<int> vShown = ParseOrder(pValue, aItems);
		const int NumItems = (int)std::size(aItems);
		std::vector<int> vRows = vShown;
		for(int i = 0; i < NumItems; i++)
			if(std::find(vShown.begin(), vShown.end(), i) == vShown.end())
				vRows.push_back(i);
		bool Changed = false;
		ImDrawList *pDraw = ImGui::GetWindowDrawList();
		const float Small = S(26.0f);
		for(int Row = 0; Row < (int)vRows.size(); Row++)
		{
			const int Item = vRows[Row];
			const int ShownIndex = Row < (int)vShown.size() ? Row : -1;
			ImGui::PushID(pConfig);
			ImGui::PushID(Item);
			char aLabel[96];
			str_format(aLabel, sizeof(aLabel), "%s   %s", aItems[Item].m_pIcon, Tr(aItems[Item].m_pLabel));
			RowLabel(aLabel, Small * 2.0f + S(6.0f) + S(10.0f) + S(38.0f));
			// up and down only for the shown ones
			for(int Dir = -1; Dir <= 1; Dir += 2)
			{
				const ImVec2 Pos = ImGui::GetCursorScreenPos();
				ImGui::PushID(Dir);
				const bool Enabled = ShownIndex >= 0 && ShownIndex + Dir >= 0 && ShownIndex + Dir < (int)vShown.size();
				const bool Pressed = ImGui::InvisibleButton("##move", ImVec2(Small, S(22.0f))) && Enabled;
				const bool Hovered = ImGui::IsItemHovered() && Enabled;
				ImGui::PopID();
				pDraw->AddRectFilled(Pos, ImVec2(Pos.x + Small, Pos.y + S(22.0f)), Col(t.m_Track, Enabled ? (Hovered ? 1.4f : 0.7f) : 0.25f), S(6.0f));
				DrawChevron(pDraw, ImVec2(Pos.x + Small * 0.5f, Pos.y + S(11.0f)), S(9.0f), Dir < 0, Col(Enabled ? t.m_TextSecondary : t.m_TextMuted, Enabled ? 1.0f : 0.4f));
				if(Pressed)
				{
					std::swap(vShown[ShownIndex], vShown[ShownIndex + Dir]);
					PlayUiSound(SOUND_CLICK);
					Changed = true;
				}
				ImGui::SameLine(0.0f, S(6.0f));
			}
			ImGui::SameLine(0.0f, S(4.0f));
			bool Shown = ShownIndex >= 0;
			if(Toggle("##shown", &Shown))
			{
				if(Shown)
					vShown.push_back(Item);
				else
					vShown.erase(std::find(vShown.begin(), vShown.end(), Item));
				Changed = true;
			}
			ImGui::PopID();
			ImGui::PopID();
			if(Changed)
				break; // the rows changed, draw them again next frame
		}
		if(Changed)
		{
			char aValue[128];
			WriteOrder(aValue, sizeof(aValue), vShown, aItems);
			SetConfigString(pConfig, aValue);
			(void)ValueSize;
		}
	};
	List(Tr("Tabs"), "xc_topbar_tabs", g_Config.m_XcTopbarTabs, sizeof(g_Config.m_XcTopbarTabs), s_aTabs);
	List(Tr("Buttons"), "xc_topbar_buttons", g_Config.m_XcTopbarButtons, sizeof(g_Config.m_XcTopbarButtons), s_aButtons);
	Description(Tr("Voting shows up only on a server. The close button always stays"));
	if(Button(Tr("Reset the bar"), false, -1.0f))
	{
		SetConfigString("xc_topbar_tabs", "home servers visuals configs demos votes settings");
		SetConfigString("xc_topbar_buttons", "search language theme classic profile");
		SetConfigInt("xc_topbar_style", 0);
	}
	EndCard();
}

// Settings > Exora in the classic menu: every Exora setting with the classic DDNet widgets, for players
// who keep the old menu. The list is built from config_variables_xclient.h, so new settings show up on
// their own; things that need pictures (galleries, file pickers) open the new menu on the right page.

#include "menu.h"
#include "settings_list.h"

#include <base/color.h>
#include <base/str.h>

#include <engine/graphics.h>
#include <engine/shared/config.h>
#include <engine/textrender.h>

#include <game/client/components/menus.h>
#include <game/client/gameclient.h>
#include <game/client/lineinput.h>
#include <game/client/ui.h>
#include <game/client/ui_scrollregion.h>

#include <algorithm>
#include <iterator>
#include <string>

using namespace XcSettingsList;

namespace
{
	// clang-format off
const SSetting gs_aXcSettings[] = {
#define MACRO_CONFIG_INT(Name, ScriptName, Def, Min, Max, Flags, Desc) {#ScriptName, TYPE_INT, &g_Config.m_##Name, (int)(Min), (int)(Max), (unsigned)(Def), nullptr, Flags, 0, Desc},
#define MACRO_CONFIG_COL(Name, ScriptName, Def, Flags, Desc) {#ScriptName, TYPE_COL, &g_Config.m_##Name, 0, 0, (unsigned)(Def), nullptr, Flags, 0, Desc},
#define MACRO_CONFIG_STR(Name, ScriptName, Len, Def, Flags, Desc) {#ScriptName, TYPE_STR, g_Config.m_##Name, 0, 0, 0, Def, Flags, Len, Desc},
#include <engine/shared/config_variables_xclient.h>
#undef MACRO_CONFIG_INT
#undef MACRO_CONFIG_COL
#undef MACRO_CONFIG_STR
};
	// clang-format on
	constexpr int NUM_XC_SETTINGS = (int)std::size(gs_aXcSettings);

	enum ESection
	{
		SECTION_EFFECTS = 0,
		SECTION_TEE,
		SECTION_MOVEMENT,
		SECTION_COSMETICS,
		SECTION_WEAPONS,
		SECTION_CROSSHAIR,
		SECTION_WORLD,
		SECTION_EVENTS,
		SECTION_NAMES,
		SECTION_INTERFACE,
		SECTION_HUD,
		SECTION_CHAT,
		SECTION_GRAPHICS,
		SECTION_EDITOR,
		SECTION_OTHER,
		NUM_SECTIONS,
	};

	struct SSection
	{
		const char *m_pTitle;
		int m_Page; // the page of the new menu with the same settings, -1 = none
		int m_SubSection;
	};

	const SSection gs_aSections[NUM_SECTIONS] = {
		{"Effects", CXcMenu::PAGE_VISUALS, CXcMenu::VISUALS_GENERAL},
		{"Tee", CXcMenu::PAGE_VISUALS, CXcMenu::VISUALS_TEE},
		{"Movement", CXcMenu::PAGE_VISUALS, CXcMenu::VISUALS_MOVEMENT},
		{"Cosmetics", CXcMenu::PAGE_VISUALS, CXcMenu::VISUALS_COSMETICS},
		{"Weapons", CXcMenu::PAGE_VISUALS, CXcMenu::VISUALS_WEAPONS},
		{"Crosshair", CXcMenu::PAGE_VISUALS, CXcMenu::VISUALS_CROSSHAIR},
		{"World", CXcMenu::PAGE_VISUALS, CXcMenu::VISUALS_WORLD},
		{"Events", CXcMenu::PAGE_VISUALS, CXcMenu::VISUALS_EVENTS},
		{"Names", CXcMenu::PAGE_VISUALS, CXcMenu::VISUALS_NAMES},
		{"Interface", CXcMenu::PAGE_SETTINGS, CXcMenu::SETTINGS_INTERFACE},
		{"HUD", CXcMenu::PAGE_SETTINGS, CXcMenu::SETTINGS_HUD},
		{"Chat", CXcMenu::PAGE_SETTINGS, CXcMenu::SETTINGS_CHAT},
		{"Graphics", CXcMenu::PAGE_SETTINGS, CXcMenu::SETTINGS_GRAPHICS},
		{"Map editor", -1, 0},
		{"Other", -1, 0},
	};

	// the first prefix that fits decides the section (names without "xc_")
	const SRule gs_aRules[] = {
		{"hats_over", SECTION_EFFECTS},
		{"fx", SECTION_EFFECTS},
		{"particles_", SECTION_EFFECTS},
		{"replace_", SECTION_EFFECTS},
		{"glow", SECTION_TEE},
		{"tee_shader", SECTION_TEE},
		{"jelly", SECTION_TEE},
		{"freeze_", SECTION_TEE},
		{"trail", SECTION_MOVEMENT},
		{"jump_fx", SECTION_MOVEMENT},
		{"land_fx", SECTION_MOVEMENT},
		{"run_dust", SECTION_MOVEMENT},
		{"motion_blur", SECTION_MOVEMENT},
		{"speed_lines", SECTION_MOVEMENT},
		{"cosmetic", SECTION_COSMETICS},
		{"hat", SECTION_COSMETICS},
		{"accessory", SECTION_COSMETICS},
		{"cape", SECTION_COSMETICS},
		{"hook_", SECTION_WEAPONS},
		{"hammer_", SECTION_WEAPONS},
		{"laser_", SECTION_WEAPONS},
		{"items_", SECTION_WEAPONS},
		{"projectile_", SECTION_WEAPONS},
		{"impact_", SECTION_WEAPONS},
		{"explosion_", SECTION_WEAPONS},
		{"weapon_", SECTION_WEAPONS},
		{"swing_", SECTION_WEAPONS},
		{"crosshair", SECTION_CROSSHAIR},
		{"block_outline", SECTION_CROSSHAIR},
		{"weather", SECTION_WORLD},
		{"floaters", SECTION_WORLD},
		{"world_tint", SECTION_WORLD},
		{"vignette", SECTION_WORLD},
		{"ambience", SECTION_WORLD},
		{"camera_shake", SECTION_WORLD},
		{"screen_flash", SECTION_WORLD},
		{"finish_", SECTION_EVENTS},
		{"death_", SECTION_EVENTS},
		{"spawn_", SECTION_EVENTS},
		{"teleport_", SECTION_EVENTS},
		{"checkpoint_", SECTION_EVENTS},
		{"event_sound", SECTION_EVENTS},
		{"dummy_arrow", SECTION_EVENTS},
		{"team_arrows", SECTION_EVENTS},
		{"friend_arrows", SECTION_EVENTS},
		{"name_gradient", SECTION_NAMES},
		{"flying_names", SECTION_NAMES},
		{"ui_", SECTION_INTERFACE},
		{"topbar_", SECTION_INTERFACE},
		{"new_menu", SECTION_INTERFACE},
		{"menu_", SECTION_INTERFACE},
		{"map_info", SECTION_INTERFACE},
		{"stats_url", SECTION_INTERFACE},
		{"link_", SECTION_INTERFACE},
		{"streamer_", SECTION_INTERFACE},
		{"hud", SECTION_HUD},
		{"focus_mode", SECTION_HUD},
		{"scoreboard", SECTION_HUD},
		{"music", SECTION_HUD},
		{"chat_", SECTION_CHAT},
		{"aspect_ratio", SECTION_GRAPHICS},
		{"low_latency", SECTION_GRAPHICS},
		{"render", SECTION_GRAPHICS},
		{"bloom", SECTION_GRAPHICS},
		{"editor_", SECTION_EDITOR},
		{"coop_", SECTION_EDITOR},
	};

	// the state of the new menu, not settings
	const char *const gs_apHidden[] = {"menu_page", "menu_visuals_section", "menu_settings_section"};

	const char *ShortName(const SSetting &Setting)
	{
		return str_startswith(Setting.m_pName, "xc_") ? Setting.m_pName + 3 : Setting.m_pName;
	}

	int SectionOf(const SSetting &Setting)
	{
		const char *pName = ShortName(Setting);
		for(const char *pHidden : gs_apHidden)
			if(str_startswith(pName, pHidden))
				return -1;
		for(const SRule &Rule : gs_aRules)
			if(str_startswith(pName, Rule.m_pPrefix))
				return Rule.m_Group;
		return SECTION_OTHER;
	}

	// the first word of the name groups the settings inside a section ("trail", "jump", "hud"...)
	void StemOf(const SSetting &Setting, char *pBuf, int BufSize)
	{
		const char *pName = ShortName(Setting);
		const char *pEnd = str_find(pName, "_");
		str_truncate(pBuf, BufSize, pName, pEnd ? (int)(pEnd - pName) : str_length(pName));
		if(pBuf[0] >= 'a' && pBuf[0] <= 'z')
			pBuf[0] = pBuf[0] - 'a' + 'A';
	}
} // namespace

void CMenus::RenderSettingsExora(CUIRect MainView)
{
	static int s_Section = SECTION_EFFECTS;
	static CButtonContainer s_aSectionButtons[NUM_SECTIONS];
	static CButtonContainer s_NewMenuButton, s_PageButton, s_HudEditorButton;
	static CButtonContainer s_aResetIds[NUM_XC_SETTINGS];
	static CLineInput s_aInputs[NUM_XC_SETTINGS];
	static bool s_aInputReady[NUM_XC_SETTINGS] = {};
	static CScrollRegion s_ScrollRegion;

	constexpr float LineSize = 20.0f;
	constexpr float Margin = 10.0f;
	constexpr float FontSize = 14.0f;
	constexpr float HeadlineSize = 18.0f;

	// the sections on the left, like the tabs of the settings
	CUIRect Sections, Content, Button;
	MainView.VSplitLeft(150.0f, &Sections, &Content);
	Content.VSplitLeft(Margin, nullptr, &Content);
	int aCounts[NUM_SECTIONS] = {};
	for(const SSetting &Setting : gs_aXcSettings)
	{
		const int Section = SectionOf(Setting);
		if(Section >= 0)
			aCounts[Section]++;
	}
	for(int i = 0; i < NUM_SECTIONS; i++)
	{
		if(aCounts[i] == 0)
			continue;
		Sections.HSplitTop(LineSize + 4.0f, &Button, &Sections);
		Sections.HSplitTop(3.0f, nullptr, &Sections);
		if(DoButton_MenuTab(&s_aSectionButtons[i], gs_aSections[i].m_pTitle, s_Section == i, &Button, IGraphics::CORNER_ALL, nullptr, nullptr, nullptr, nullptr, 5.0f))
			s_Section = i;
	}

	// the buttons above the settings: back to the new menu, the same page there, the HUD editor
	CUIRect TopBar;
	Content.HSplitTop(LineSize + 4.0f, &TopBar, &Content);
	Content.HSplitTop(Margin, nullptr, &Content);
	TopBar.VSplitLeft(170.0f, &Button, &TopBar);
	if(DoButton_Menu(&s_NewMenuButton, "Open the new menu", 0, &Button))
		GameClient()->m_XcMenu.OpenAt(CXcMenu::PAGE_HOME, 0);
	const SSection &Current = gs_aSections[s_Section];
	if(Current.m_Page >= 0)
	{
		TopBar.VSplitLeft(Margin, nullptr, &TopBar);
		TopBar.VSplitLeft(230.0f, &Button, &TopBar);
		char aLabel[128];
		str_format(aLabel, sizeof(aLabel), "%s in the new menu", Current.m_pTitle);
		if(DoButton_Menu(&s_PageButton, aLabel, 0, &Button))
			GameClient()->m_XcMenu.OpenAt(Current.m_Page, Current.m_SubSection);
	}
	if(s_Section == SECTION_HUD)
	{
		TopBar.VSplitLeft(Margin, nullptr, &TopBar);
		TopBar.VSplitLeft(150.0f, &Button, &TopBar);
		if(DoButton_Menu(&s_HudEditorButton, "Open the HUD editor", 0, &Button))
		{
			SetActive(false);
			GameClient()->m_XcHud.SetEditing(true);
		}
	}

	// the settings of the section, with a headline for every group
	CScrollRegionParams ScrollParams;
	ScrollParams.m_ScrollUnit = 60.0f;
	ScrollParams.m_ForceShowScrollbar = true;
	ScrollParams.m_ScrollbarMargin = 5.0f;
	s_ScrollRegion.Begin(&Content, &ScrollParams);
	CUIRect Column = Content;
	Column.VSplitRight(15.0f, &Column, nullptr);

	char aStem[64] = "";
	for(int i = 0; i < NUM_XC_SETTINGS; i++)
	{
		const SSetting &Setting = gs_aXcSettings[i];
		if(SectionOf(Setting) != s_Section)
			continue;
		char aNewStem[64];
		StemOf(Setting, aNewStem, sizeof(aNewStem));
		if(str_comp(aNewStem, aStem) != 0)
		{
			str_copy(aStem, aNewStem);
			CUIRect Headline;
			Column.HSplitTop(Margin, nullptr, &Column);
			Column.HSplitTop(HeadlineSize + 6.0f, &Headline, &Column);
			if(s_ScrollRegion.AddRect(Headline))
			{
				Headline.Draw(ColorRGBA(0.0f, 0.0f, 0.0f, 0.25f), IGraphics::CORNER_ALL, 5.0f);
				Headline.VSplitLeft(8.0f, nullptr, &Headline);
				Ui()->DoLabel(&Headline, aStem, HeadlineSize, TEXTALIGN_ML);
			}
			Column.HSplitTop(4.0f, nullptr, &Column);
		}

		const char *pLabel = Setting.m_pDesc && Setting.m_pDesc[0] ? Setting.m_pDesc : Setting.m_pName;
		CUIRect Row;
		if(Setting.m_Type == TYPE_INT)
		{
			int *pValue = static_cast<int *>(Setting.m_pValue);
			if(Setting.m_Min == 0 && Setting.m_Max == 1)
			{
				Column.HSplitTop(LineSize, &Row, &Column);
				if(s_ScrollRegion.AddRect(Row) && DoButton_CheckBox(pValue, pLabel, *pValue, &Row))
					*pValue ^= 1;
			}
			else
			{
				// a long name goes above its slider
				const bool Long = TextRender()->TextWidth(FontSize, pLabel) > Column.w * 0.45f;
				Column.HSplitTop(Long ? LineSize * 2.0f : LineSize, &Row, &Column);
				if(s_ScrollRegion.AddRect(Row))
					Ui()->DoScrollbarOption(pValue, pValue, &Row, pLabel, Setting.m_Min, Setting.m_Max, &CUi::ms_LinearScrollbarScale, Long ? CUi::SCROLLBAR_OPTION_MULTILINE : 0u);
			}
		}
		else if(Setting.m_Type == TYPE_COL)
		{
			const bool Alpha = Setting.m_Flags & CFGFLAG_COLALPHA;
			CUIRect Probe = Column;
			Probe.h = 25.0f;
			if(s_ScrollRegion.AddRect(Probe))
				DoLine_ColorPicker(&s_aResetIds[i], 25.0f, 13.0f, 5.0f, &Column, pLabel, static_cast<unsigned *>(Setting.m_pValue), color_cast<ColorRGBA>(ColorHSLA(Setting.m_Default, Alpha)), false, nullptr, Alpha);
			else
				Column.HSplitTop(30.0f, nullptr, &Column);
		}
		else
		{
			// a text: the name above, the field under it
			if(!s_aInputReady[i])
			{
				s_aInputs[i].SetBuffer(static_cast<char *>(Setting.m_pValue), Setting.m_Length);
				s_aInputReady[i] = true;
			}
			CUIRect Label, Field;
			Column.HSplitTop(LineSize, &Label, &Column);
			Column.HSplitTop(LineSize + 2.0f, &Field, &Column);
			Row = Label;
			Row.h += Field.h;
			if(s_ScrollRegion.AddRect(Row))
			{
				Ui()->DoLabel(&Label, pLabel, FontSize, TEXTALIGN_ML);
				Ui()->DoEditBox(&s_aInputs[i], &Field, 12.0f);
			}
		}
		Column.HSplitTop(4.0f, nullptr, &Column);
	}
	CUIRect End;
	Column.HSplitTop(Margin, &End, &Column);
	s_ScrollRegion.AddRect(End);
	s_ScrollRegion.End();
}

#include "icons.h"
#include "lang.h"
#include "menu.h"
#include "theme.h"
#include "widgets.h"

#include <base/str.h>

#include <engine/external/imgui/imgui.h>
#include <engine/external/imgui/imgui_internal.h>
#include <engine/graphics.h>
#include <engine/keys.h>
#include <engine/shared/config.h>

#include <game/client/gameclient.h>

#include <map>
#include <string>

using namespace XcUi;

struct SBindEntry
{
	const char *m_pLabel;
	const char *m_pCommand;
};

static const SBindEntry s_aBinds[] = {
	{"Move left", "+left"},
	{"Move right", "+right"},
	{"Jump", "+jump"},
	{"Hook", "+hook"},
	{"Fire", "+fire"},
	{"Kill", "kill"},
	{"Next weapon", "+nextweapon"},
	{"Previous weapon", "+prevweapon"},
	{"Chat all", "+show_chat; chat all"},
	{"Team chat", "+show_chat; chat team"},
	{"Scoreboard", "+scoreboard"},
	{"Emoticons", "+emote"},
	{"Spectator mode", "+spectate"},
	{"Toggle dummy", "toggle cl_dummy 0 1"},
};

// --------------------------------------------------------------------------------------------
// config helpers

void CXcMenu::SetConfigInt(const char *pName, int Value)
{
	char aCommand[256];
	str_format(aCommand, sizeof(aCommand), "%s %d", pName, Value);
	Console()->ExecuteLine(aCommand, IConsole::CLIENT_ID_UNSPECIFIED);
}

void CXcMenu::SetConfigString(const char *pName, const char *pValue)
{
	char aEscaped[512];
	char *pDst = aEscaped;
	str_escape(&pDst, pValue, aEscaped + sizeof(aEscaped));
	char aCommand[640];
	str_format(aCommand, sizeof(aCommand), "%s \"%s\"", pName, aEscaped);
	Console()->ExecuteLine(aCommand, IConsole::CLIENT_ID_UNSPECIFIED);
}

bool CXcMenu::ConfigToggle(const char *pId, const char *pName, int *pValue)
{
	int Value = *pValue;
	if(!ToggleInt(pId, &Value))
		return false;
	SetConfigInt(pName, Value);
	return true;
}

bool CXcMenu::ConfigSlider(const char *pLabel, const char *pName, int *pValue, int Min, int Max, const char *pFormat)
{
	int Value = *pValue;
	if(!SliderInt(pLabel, &Value, Min, Max, pFormat))
		return false;
	SetConfigInt(pName, Value);
	return true;
}

bool CXcMenu::ConfigSegmented(const char *pId, const char *pName, int *pValue, const char *const *ppItems, int NumItems, const int *pValues)
{
	int Current = 0;
	for(int i = 0; i < NumItems; i++)
		if((pValues ? pValues[i] : i) == *pValue)
			Current = i;
	if(!Segmented(pId, &Current, ppItems, NumItems))
		return false;
	SetConfigInt(pName, pValues ? pValues[Current] : Current);
	return true;
}

void CXcMenu::ConfigText(const char *pLabel, const char *pName, const char *pValue, int MaxLength)
{
	// edit a copy, write it to the config when the field is left (sends the player info only once)
	static std::map<std::string, std::string> s_Buffers;
	std::string &Buffer = s_Buffers[pName];
	char aBuffer[256];
	ParamLabel(pLabel);
	ImGui::PushID(pName);
	const bool Editing = ImGui::GetActiveID() == ImGui::GetID("##text");
	str_copy(aBuffer, Editing ? Buffer.c_str() : pValue, std::min((int)sizeof(aBuffer), MaxLength));
	InputText("##text", aBuffer, std::min((int)sizeof(aBuffer), MaxLength));
	const bool Done = ImGui::IsItemDeactivatedAfterEdit();
	ImGui::PopID();
	Buffer = aBuffer;
	if(Done)
		SetConfigString(pName, aBuffer);
}

void CXcMenu::ToggleRow(const char *pLabel, const char *pName, int *pValue)
{
	RowLabel(pLabel, S(38.0f));
	ConfigToggle(pName, pName, pValue);
}

// --------------------------------------------------------------------------------------------
// layout

void CXcMenu::RenderSettings()
{
	const char *apIcons[NUM_SETTINGS] = {XC_ICON_PALETTE, XC_ICON_IMAGE, XC_ICON_USER, XC_ICON_KEYBOARD, XC_ICON_VOLUME, XC_ICON_COMMENTS, XC_ICON_DESKTOP, XC_ICON_LAYERS};
	const char *apLabels[NUM_SETTINGS] = {Tr("Interface"), Tr("Background"), Tr("Player"), Tr("Controls"), Tr("Sound"), Tr("Chat"), Tr("Graphics"), Tr("HUD editor")};

	const float NavWidth = S(210.0f);
	const ImVec2 Start = ImGui::GetCursorScreenPos();
	if(SideNav("##sections", &m_SettingsSection, apIcons, apLabels, NUM_SETTINGS, NavWidth))
	{
		m_ScrollTarget = 0.0f;
		m_BindWaiting = -1;
	}

	ImGui::SetCursorScreenPos(ImVec2(Start.x + NavWidth + S(20.0f), Start.y));
	const ImVec2 Avail = ImGui::GetContentRegionAvail();
	BeginSmoothScroll("##settings_scroll", ImVec2(Avail.x, Avail.y));
	Header(apLabels[m_SettingsSection]);
	ImGui::Dummy(ImVec2(0.0f, S(4.0f)));
	switch(m_SettingsSection)
	{
	case SETTINGS_INTERFACE: RenderSettingsInterface(); break;
	case SETTINGS_BACKGROUND: RenderSettingsBackground(); break;
	case SETTINGS_PLAYER: RenderSettingsPlayer(); break;
	case SETTINGS_CONTROLS: RenderSettingsControls(); break;
	case SETTINGS_SOUND: RenderSettingsSound(); break;
	case SETTINGS_CHAT: RenderSettingsChat(); break;
	case SETTINGS_GRAPHICS: RenderSettingsGraphics(); break;
	default: RenderSettingsHud(); break;
	}
	ImGui::Dummy(ImVec2(0.0f, S(12.0f)));
	EndSmoothScroll();
}

// --------------------------------------------------------------------------------------------
// sections

void CXcMenu::RenderSettingsInterface()
{
	BeginCardGrid("##interface", 2);

	if(BeginCard("##theme", Tr("Theme"), Tr("Look of the menu: dark or light, and the accent color")))
	{
		const char *apThemes[] = {Tr("Dark"), Tr("Light")};
		ConfigSegmented("##theme_mode", "xc_ui_theme", &g_Config.m_XcUiTheme, apThemes, 2);
		ParamLabel(Tr("Accent"));
		ImVec4 aColors[NUM_ACCENTS];
		for(int i = 0; i < NUM_ACCENTS; i++)
			aColors[i] = AccentColor(i, Theme().m_Dark);
		int Accent = g_Config.m_XcUiAccent;
		if(ColorSwatches("##accent", &Accent, aColors, NUM_ACCENTS))
			SetConfigInt("xc_ui_accent", Accent);
		EndCard();
	}

	if(BeginCard("##font", Tr("Font"), Tr("Main font of the menu")))
	{
		const char *apFonts[] = {"Manrope", "Onest", "Unbounded"};
		ConfigSegmented("##font_family", "xc_ui_font", &g_Config.m_XcUiFont, apFonts, 3);
		EndCard();
	}

	if(BeginCard("##scale", Tr("Scale"), Tr("Size of the menu for your screen")))
	{
		const char *apScales[] = {Tr("Auto"), "1080p", "2K", "4K"};
		static const int s_aScales[] = {0, 100, 133, 200};
		ConfigSegmented("##scale_mode", "xc_ui_scale", &g_Config.m_XcUiScale, apScales, 4, s_aScales);
		EndCard();
	}

	if(BeginCard("##animations", Tr("Animations"), Tr("How smooth the menu moves")))
	{
		const char *apModes[] = {Tr("Off"), Tr("Fast"), Tr("Normal"), Tr("Smooth")};
		ConfigSegmented("##anim_mode", "xc_ui_animations", &g_Config.m_XcUiAnimations, apModes, 4);
		EndCard();
	}

	if(BeginCard("##language", Tr("Game language"), Tr("Language of the menu and the game")))
	{
		const char *apLanguages[] = {"Русский", "English"};
		int Current = IsRussian() ? 0 : 1;
		if(Segmented("##language_mode", &Current, apLanguages, 2))
			SetConfigString("cl_languagefile", Current == 0 ? "languages/russian.txt" : "");
		EndCard();
	}

	if(BeginCard("##ui_sounds", Tr("Interface sounds"), Tr("Clicks, switches and notifications"), &g_Config.m_XcUiSounds))
	{
		ConfigSlider(Tr("Volume"), "xc_ui_sound_volume", &g_Config.m_XcUiSoundVolume, 0, 100, "%d%%");
		EndCard();
	}

	if(BeginCard("##classic", Tr("Classic menu"), Tr("The old DDNet menu, for settings that are not here yet")))
	{
		if(Button(Tr("Open"), false, -1.0f))
			m_WantClassic = true;
		ToggleRow(Tr("Use the new menu"), "xc_new_menu", &g_Config.m_XcNewMenu);
		EndCard();
	}

	EndCardGrid();
	if(m_WantClassic)
	{
		m_WantClassic = false;
		OpenClassicMenu();
	}
}

void CXcMenu::RenderSettingsPlayer()
{
	BeginCardGrid("##player", 2);
	if(BeginCard("##profile", Tr("Profile"), Tr("Name and clan of you and your dummy")))
	{
		ConfigText(Tr("Name"), "player_name", g_Config.m_PlayerName, sizeof(g_Config.m_PlayerName));
		ConfigText(Tr("Clan"), "player_clan", g_Config.m_PlayerClan, sizeof(g_Config.m_PlayerClan));
		ConfigText(Tr("Dummy name"), "dummy_name", g_Config.m_ClDummyName, sizeof(g_Config.m_ClDummyName));
		ConfigText(Tr("Dummy clan"), "dummy_clan", g_Config.m_ClDummyClan, sizeof(g_Config.m_ClDummyClan));
		EndCard();
	}
	if(BeginCard("##skin", Tr("Skin"), Tr("Skin name and custom colors")))
	{
		ConfigText(Tr("Skin name"), "player_skin", g_Config.m_ClPlayerSkin, sizeof(g_Config.m_ClPlayerSkin));
		ToggleRow(Tr("Custom colors"), "player_use_custom_color", &g_Config.m_ClPlayerUseCustomColor);
		EndCard();
	}
	EndCardGrid();
}

void CXcMenu::RenderSettingsControls()
{
	BeginCardGrid("##controls", 2);
	if(BeginCard("##mouse", Tr("Mouse"), Tr("Sensitivity in game and in menus")))
	{
		ConfigSlider(Tr("In game"), "inp_mousesens", &g_Config.m_InpMousesens, 1, 500);
		ConfigSlider(Tr("In menus"), "ui_mousesens", &g_Config.m_UiMousesens, 1, 500);
		ConfigSlider(Tr("Max mouse distance"), "cl_mouse_max_distance", &g_Config.m_ClMouseMaxDistance, 0, 5000);
		EndCard();
	}
	if(BeginCard("##camera", Tr("Camera"), Tr("Dynamic camera follows the mouse"), &g_Config.m_ClDyncam))
	{
		ConfigSlider(Tr("Max distance"), "cl_dyncam_max_distance", &g_Config.m_ClDyncamMaxDistance, 0, 2000);
		ConfigSlider(Tr("Follow factor"), "cl_dyncam_follow_factor", &g_Config.m_ClDyncamFollowFactor, 0, 200);
		ConfigSlider(Tr("Deadzone"), "cl_dyncam_deadzone", &g_Config.m_ClDyncamDeadzone, 1, 1300);
		EndCard();
	}
	if(BeginCard("##binds", Tr("Key binds"), Tr("Click a key and press a new one, Esc cancels")))
	{
		for(int i = 0; i < (int)std::size(s_aBinds); i++)
		{
			char aKey[64];
			GameClient()->m_Binds.GetKey(s_aBinds[i].m_pCommand, aKey, sizeof(aKey));
			RowLabel(Tr(s_aBinds[i].m_pLabel), S(90.0f));
			ImGui::PushID(i);
			if(BindChip("##bind", aKey, m_BindWaiting == i))
				m_BindWaiting = i;
			ImGui::PopID();
		}
		EndCard();
	}
	EndCardGrid();
}

bool CXcMenu::OnBindInput(const IInput::CEvent &Event)
{
	if(!(Event.m_Flags & IInput::FLAG_PRESS) || Event.m_Key == KEY_UNKNOWN)
		return Event.m_Flags & IInput::FLAG_TEXT; // swallow typed text while waiting
	const int Index = m_BindWaiting;
	m_BindWaiting = -1;
	if(Event.m_Key == KEY_ESCAPE)
		return true;

	// one key per action: free the old keys of this command first
	CBinds &Binds = GameClient()->m_Binds;
	const char *pCommand = s_aBinds[Index].m_pCommand;
	for(int Key = KEY_FIRST; Key < KEY_LAST; Key++)
	{
		const char *pBind = Binds.Get(Key, KeyModifier::NONE);
		if(pBind[0] && str_comp(pBind, pCommand) == 0)
			Binds.Bind(Key, "");
	}
	Binds.Bind(Event.m_Key, pCommand);
	return true;
}

void CXcMenu::RenderSettingsSound()
{
	BeginCardGrid("##sound", 2);
	if(BeginCard("##volume", Tr("Sound"), Tr("Sound and volume"), &g_Config.m_SndEnable))
	{
		ConfigSlider(Tr("Master volume"), "snd_volume", &g_Config.m_SndVolume, 0, 100, "%d%%");
		ConfigSlider(Tr("Game"), "snd_game_volume", &g_Config.m_SndGameVolume, 0, 100, "%d%%");
		ConfigSlider(Tr("Chat"), "snd_chat_volume", &g_Config.m_SndChatVolume, 0, 100, "%d%%");
		ConfigSlider(Tr("Ambient"), "snd_ambient_volume", &g_Config.m_SndMapVolume, 0, 100, "%d%%");
		ConfigSlider(Tr("Music"), "snd_background_music_volume", &g_Config.m_SndBackgroundMusicVolume, 0, 100, "%d%%");
		ToggleRow(Tr("Mute when not active"), "snd_nonactive_mute", &g_Config.m_SndNonactiveMute);
		EndCard();
	}
	EndCardGrid();
}

void CXcMenu::RenderSettingsChat()
{
	BeginCardGrid("##chat", 2);
	if(BeginCard("##chat_look", Tr("Chat"), Tr("How the chat looks"), &g_Config.m_ClShowChat))
	{
		ConfigSlider(Tr("Font size"), "cl_chat_size", &g_Config.m_ClChatFontSize, 10, 100);
		ConfigSlider(Tr("Width"), "cl_chat_width", &g_Config.m_ClChatWidth, 140, 400);
		ToggleRow(Tr("Old chat style"), "cl_chat_old", &g_Config.m_ClChatOld);
		ToggleRow(Tr("Only friends"), "cl_show_chat_friends", &g_Config.m_ClShowChatFriends);
		ToggleRow(Tr("Team colors"), "cl_chat_teamcolors", &g_Config.m_ClChatTeamColors);
		ToggleRow(Tr("System messages"), "cl_show_chat_system", &g_Config.m_ClShowChatSystem);
		EndCard();
	}
	EndCardGrid();
}

void CXcMenu::RenderSettingsGraphics()
{
	BeginCardGrid("##graphics", 2);
	if(BeginCard("##screen", Tr("Screen"), Tr("Window mode and frame rate")))
	{
		ParamLabel(Tr("Window mode"));
		const char *apModes[] = {Tr("Window"), Tr("Fullscreen"), Tr("Borderless")};
		ConfigSegmented("##fullscreen", "gfx_fullscreen", &g_Config.m_GfxFullscreen, apModes, 3);
		ToggleRow(Tr("V-Sync"), "gfx_vsync", &g_Config.m_GfxVsync);
		ConfigSlider(Tr("FPS limit"), "gfx_refresh_rate", &g_Config.m_GfxRefreshRate, 0, 1000, g_Config.m_GfxRefreshRate == 0 ? Tr("unlimited") : "%d");
		ToggleRow(Tr("Show FPS"), "cl_showfps", &g_Config.m_ClShowfps);
		EndCard();
	}
	if(BeginCard("##effects", Tr("Effects"), Tr("Quality of the render module (OpenGL 3.3)"), &g_Config.m_XcRender))
	{
		if(!Graphics()->XcRenderAvailable())
			Description(Tr("Not available: needs OpenGL 3.3"));
		ParamLabel(Tr("Quality"));
		const char *apQuality[] = {Tr("Low"), Tr("Medium"), Tr("High")};
		ConfigSegmented("##quality", "xc_fx_quality", &g_Config.m_XcFxQuality, apQuality, 3);
		ConfigSlider(Tr("Bloom"), "xc_bloom", &g_Config.m_XcBloom, 0, 200, "%d%%");
		EndCard();
	}
	EndCardGrid();
	Description(Tr("Some changes apply after a restart"));
}

void CXcMenu::RenderSettingsHud()
{
	RenderPlaceholder(XC_ICON_LAYERS, Tr("HUD editor"), Tr("Drag and resize HUD elements, coming soon."), false);
}

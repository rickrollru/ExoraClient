#include "feedback.h"
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

#include <game/client/components/xclient/hud/xc_hud.h>
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
	{"Music: play / pause", "xc_music_play_pause"},
	{"Music: next track", "xc_music_next"},
	{"Music: previous track", "xc_music_prev"},
	{"Focus mode", "xc_focus"},
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
	const char *apIcons[NUM_SETTINGS] = {XC_ICON_PALETTE, XC_ICON_IMAGE, XC_ICON_USER, XC_ICON_KEYBOARD, XC_ICON_VOLUME, XC_ICON_COMMENTS, XC_ICON_DESKTOP, XC_ICON_LAYERS, XC_ICON_BRUSH, XC_ICON_SLIDERS};
	const char *apLabels[NUM_SETTINGS] = {Tr("Interface"), Tr("Background"), Tr("Player"), Tr("Controls"), Tr("Sound"), Tr("Chat"), Tr("Graphics"), Tr("HUD"), Tr("Textures"), "TClient"};

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
	case SETTINGS_TEXTURES: RenderSettingsTextures(); break;
	case SETTINGS_TCLIENT: RenderSettingsTClient(); break;
	default: RenderSettingsHud(); break;
	}
	// opened from the search: glide to the found card
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

	if(BeginCard("##presets", Tr("Theme presets"), Tr("Save theme, accent, font and background together")))
	{
		RenderThemePresets();
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
		ConfigSlider(Tr("Size"), "xc_ui_size", &g_Config.m_XcUiSize, 70, 200, "%d%%");
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
	const STheme &t = Theme();
	const bool Dummy = m_PlayerTarget == 1;
	BeginCardGrid("##player", 2);
	if(BeginCard("##profile", Tr("Tee"), Tr("Name, clan, skin and colors of you or your dummy")))
	{
		const char *apWho[] = {Tr("Me"), Tr("Dummy")};
		Segmented("##who", &m_PlayerTarget, apWho, 2);
		// a big living preview of the tee that is edited
		const float PreviewHeight = S(150.0f);
		const ImVec2 Pos = ImGui::GetCursorScreenPos();
		const float Width = AvailWidth();
		ImDrawList *pDraw = ImGui::GetWindowDrawList();
		pDraw->AddRectFilled(Pos, ImVec2(Pos.x + Width, Pos.y + PreviewHeight), Col(t.m_Track), S(10.0f));
		AddTeePreview(pDraw, ImVec2(Pos.x + Width * 0.5f - PreviewHeight * 0.5f, Pos.y), ImVec2(Pos.x + Width * 0.5f + PreviewHeight * 0.5f, Pos.y + PreviewHeight), false, m_PlayerTarget);
		ImGui::Dummy(ImVec2(Width, PreviewHeight));
		if(Dummy)
		{
			ConfigText(Tr("Name"), "dummy_name", g_Config.m_ClDummyName, sizeof(g_Config.m_ClDummyName));
			ConfigText(Tr("Clan"), "dummy_clan", g_Config.m_ClDummyClan, sizeof(g_Config.m_ClDummyClan));
			ConfigText(Tr("Skin name"), "dummy_skin", g_Config.m_ClDummySkin, sizeof(g_Config.m_ClDummySkin));
			ToggleRow(Tr("Custom colors"), "dummy_use_custom_color", &g_Config.m_ClDummyUseCustomColor);
			if(g_Config.m_ClDummyUseCustomColor)
			{
				ConfigColorRow(Tr("Body"), &g_Config.m_ClDummyColorBody, false);
				ConfigColorRow(Tr("Feet"), &g_Config.m_ClDummyColorFeet, false);
			}
		}
		else
		{
			ConfigText(Tr("Name"), "player_name", g_Config.m_PlayerName, sizeof(g_Config.m_PlayerName));
			ConfigText(Tr("Clan"), "player_clan", g_Config.m_PlayerClan, sizeof(g_Config.m_PlayerClan));
			ConfigText(Tr("Skin name"), "player_skin", g_Config.m_ClPlayerSkin, sizeof(g_Config.m_ClPlayerSkin));
			ToggleRow(Tr("Custom colors"), "player_use_custom_color", &g_Config.m_ClPlayerUseCustomColor);
			if(g_Config.m_ClPlayerUseCustomColor)
			{
				ConfigColorRow(Tr("Body"), &g_Config.m_ClPlayerColorBody, false);
				ConfigColorRow(Tr("Feet"), &g_Config.m_ClPlayerColorFeet, false);
			}
		}
		EndCard();
	}

	if(BeginCard("##dummy", Tr("Dummy"), Tr("A second tee on the same server that you can switch to")))
	{
		const bool Online = Client()->State() == IClient::STATE_ONLINE;
		const bool Connected = Client()->DummyConnected();
		const bool Connecting = Client()->DummyConnecting() || Client()->DummyConnectingDelayed();
		Description(Connected ? Tr("The dummy is on the server") : (Connecting ? Tr("The dummy is connecting...") : (Online ? Tr("The dummy is not connected") : Tr("Join a server to spawn the dummy"))));
		ImGui::BeginDisabled(!Online || Connecting);
		if(Button(Connected ? Tr("Disconnect the dummy") : Tr("Connect the dummy"), !Connected, AvailWidth()))
		{
			if(Connected)
				Client()->DummyDisconnect(nullptr);
			else if(Client()->DummyAllowed())
				Client()->DummyConnect();
			else
				Toast(Tr("This server does not allow a dummy"), TOAST_ERROR);
		}
		ImGui::EndDisabled();
		ImGui::BeginDisabled(!Connected);
		if(Button(g_Config.m_ClDummy ? Tr("Play as yourself") : Tr("Play as the dummy"), false, AvailWidth()))
			SetConfigInt("cl_dummy", g_Config.m_ClDummy ? 0 : 1);
		ImGui::EndDisabled();
		if(Button(Tr("Copy my skin to the dummy"), false, AvailWidth()))
		{
			SetConfigString("dummy_skin", g_Config.m_ClPlayerSkin);
			SetConfigInt("dummy_use_custom_color", g_Config.m_ClPlayerUseCustomColor);
			SetConfigInt("dummy_color_body", (int)g_Config.m_ClPlayerColorBody);
			SetConfigInt("dummy_color_feet", (int)g_Config.m_ClPlayerColorFeet);
			Toast(Tr("The dummy wears your skin now"), TOAST_SUCCESS);
		}
		EndCard();
	}

	if(BeginCard("##skins", Tr("Skins"), Dummy ? Tr("Click a skin to put it on the dummy") : Tr("Click a skin to put it on")))
	{
		// search in the skin names, the list filters itself
		static char s_aFilter[sizeof(g_Config.m_ClSkinFilterString)] = {};
		if(InputText("##skin_filter", s_aFilter, sizeof(s_aFilter), Tr("Search skins")))
		{
			str_copy(g_Config.m_ClSkinFilterString, s_aFilter);
			GameClient()->m_Skins.SkinList().ForceRefresh();
		}
		std::vector<CSkins::CSkinListEntry> &vSkins = GameClient()->m_Skins.SkinList().Skins();
		const char *pCurrent = Dummy ? g_Config.m_ClDummySkin : g_Config.m_ClPlayerSkin;
		const float Tile = S(72.0f);
		const float Gap = S(6.0f);
		const float Width = AvailWidth();
		const int Columns = std::max(1, (int)((Width + Gap) / (Tile + Gap)));
		const int Rows = ((int)vSkins.size() + Columns - 1) / Columns;
		const float ListHeight = std::min(Rows * (Tile + Gap), S(340.0f));
		ImGui::BeginChild("##skin_grid", ImVec2(Width, std::max(ListHeight, Tile)), ImGuiChildFlags_None, ImGuiWindowFlags_NoBackground);
		ImDrawList *pGridDraw = ImGui::GetWindowDrawList();
		const ImVec2 Start = ImGui::GetCursorScreenPos();
		for(int i = 0; i < (int)vSkins.size(); i++)
		{
			const ImVec2 Min(Start.x + (i % Columns) * (Tile + Gap), Start.y + (i / Columns) * (Tile + Gap));
			const ImVec2 Max(Min.x + Tile, Min.y + Tile);
			ImGui::SetCursorScreenPos(Min);
			ImGui::PushID(i);
			const bool Clicked = ImGui::InvisibleButton("##skin", ImVec2(Tile, Tile));
			const bool Hovered = ImGui::IsItemHovered();
			ImGui::PopID();
			// only the visible tiles load their skins and draw
			if(!ImGui::IsRectVisible(Min, Max))
				continue;
			CSkins::CSkinListEntry &Entry = vSkins[i];
			Entry.RequestLoad();
			const char *pName = Entry.SkinContainer()->Name();
			const bool Selected = str_comp(pName, pCurrent) == 0;
			pGridDraw->AddRectFilled(Min, Max, Col(Selected ? ImVec4(t.m_Accent.x, t.m_Accent.y, t.m_Accent.z, 0.22f) : t.m_Track, Hovered ? 1.3f : 1.0f), S(8.0f));
			if(Selected)
				pGridDraw->AddRect(Min, Max, Col(t.m_Accent), S(8.0f), 0, S(1.5f));
			AddTeePreview(pGridDraw, ImVec2(Min.x + Tile * 0.1f, Min.y + Tile * 0.02f), ImVec2(Max.x - Tile * 0.1f, Max.y - Tile * 0.18f), true, m_PlayerTarget, pName);
			char aShort[32];
			XcFitText(Fonts().m_pSmall, Fonts().m_pSmall->FontSize, pName, Tile - S(6.0f), aShort, sizeof(aShort));
			const ImVec2 TextSize = Fonts().m_pSmall->CalcTextSizeA(Fonts().m_pSmall->FontSize, FLT_MAX, 0.0f, aShort);
			pGridDraw->AddText(Fonts().m_pSmall, Fonts().m_pSmall->FontSize, ImVec2((Min.x + Max.x - TextSize.x) * 0.5f, Max.y - TextSize.y - S(3.0f)), Col(Selected ? t.m_Text : t.m_TextMuted), aShort);
			if(Hovered)
				ImGui::SetTooltip("%s", pName);
			if(Clicked)
			{
				PlayUiSound(SOUND_CLICK);
				SetConfigString(Dummy ? "dummy_skin" : "player_skin", pName);
			}
		}
		ImGui::SetCursorScreenPos(ImVec2(Start.x, Start.y + Rows * (Tile + Gap)));
		ImGui::Dummy(ImVec2(1.0f, 1.0f));
		ImGui::EndChild();
		if(vSkins.empty())
			Description(Tr("No skins found"));
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
		ToggleRow(Tr("New messages slide in"), "xc_chat_anim", &g_Config.m_XcChatAnim);
		EndCard();
	}
	if(BeginCard("##chat_style", Tr("Chat in the HUD style"), Tr("Plates in the theme colors, marks for mentions, team chat and whispers, a framed input"), &g_Config.m_XcChatStyle))
	{
		ConfigSlider(Tr("Plate opacity"), "xc_chat_opacity", &g_Config.m_XcChatOpacity, 0, 100, "%d%%");
		Description(Tr("Accent: you were mentioned. Mint: team chat. Pink: whisper. Gray: server"));
		EndCard();
	}
	EndCardGrid();
}

void CXcMenu::RenderSettingsGraphics()
{
	BeginCardGrid("##graphics", 2);
	if(BeginCard("##performance", Tr("Performance"), Tr("Low, medium or high in one click")))
	{
		RenderPerformance();
		EndCard();
	}
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
	BeginCardGrid("##hud", 2);
	if(BeginCard("##hud_main", Tr("New HUD"), Tr("Watermark, timer, speed and weapon in the style of the menu"), &g_Config.m_XcHud))
	{
		if(Button(Tr("Open the HUD editor"), true, -1.0f))
			GameClient()->m_XcHud.SetEditing(true);
		Description(Tr("Drag the elements in the game, the mouse wheel changes their size"));
		ToggleRow(Tr("Watermark"), "xc_hud_watermark", &g_Config.m_XcHudWatermark);
		ToggleRow(Tr("Timer"), "xc_hud_timer", &g_Config.m_XcHudTimer);
		ToggleRow(Tr("Speed"), "xc_hud_speed", &g_Config.m_XcHudSpeed);
		ToggleRow(Tr("Weapon"), "xc_hud_weapon", &g_Config.m_XcHudWeapon);
		ToggleRow(Tr("Music"), "xc_hud_music", &g_Config.m_XcHudMusic);
		ToggleRow(Tr("Keystrokes"), "xc_hud_keys", &g_Config.m_XcHudKeys);
		ToggleRow(Tr("Focus mode (timer and speed only, no chat)"), "xc_focus_mode", &g_Config.m_XcFocusMode);
		ToggleRow(Tr("Notifications"), "xc_hud_toasts", &g_Config.m_XcHudToasts);
		EndCard();
	}
	if(BeginCard("##hud_sizes", Tr("Sizes"), Tr("Size of every element in percent")))
	{
		ConfigSlider(Tr("Watermark"), "xc_hud_watermark_scale", &g_Config.m_XcHudWatermarkScale, 50, 200, "%d%%");
		ConfigSlider(Tr("Timer"), "xc_hud_timer_scale", &g_Config.m_XcHudTimerScale, 50, 200, "%d%%");
		ConfigSlider(Tr("Speed"), "xc_hud_speed_scale", &g_Config.m_XcHudSpeedScale, 50, 200, "%d%%");
		ConfigSlider(Tr("Weapon"), "xc_hud_weapon_scale", &g_Config.m_XcHudWeaponScale, 50, 200, "%d%%");
		ConfigSlider(Tr("Music"), "xc_hud_music_scale", &g_Config.m_XcHudMusicScale, 50, 200, "%d%%");
		ConfigSlider(Tr("Keystrokes"), "xc_hud_keys_scale", &g_Config.m_XcHudKeysScale, 50, 200, "%d%%");
		EndCard();
	}
	if(BeginCard("##music_player", Tr("Music player"), Tr("What plays in the browser or a music app: YouTube, SoundCloud, Yandex Music, Spotify..."), &g_Config.m_XcMusic))
	{
		if(!GameClient()->m_XcMusic.Supported())
			Description(Tr("Not supported on this system"));
		ToggleRow(Tr("Hide in the HUD when paused"), "xc_hud_music_autohide", &g_Config.m_XcHudMusicAutohide);
		ToggleRow(Tr("Show the next track"), "xc_music_toasts", &g_Config.m_XcMusicToasts);
		Description(Tr("Keys for play, next and previous are in Controls > Key binds"));
		EndCard();
	}
	if(BeginCard("##hud_plates", Tr("Name plates"), Tr("Names above the tees on dark rounded plates"), &g_Config.m_XcHudPlates))
	{
		Description(Tr("The text stays readable on bright maps"));
		EndCard();
	}
	if(BeginCard("##hud_own_scoreboard", Tr("Own scoreboard"), Tr("Tab shows players by teams with medals, times and pings in the style of the menu"), &g_Config.m_XcScoreboard))
	{
		ToggleRow(Tr("Show clans"), "xc_scoreboard_clan", &g_Config.m_XcScoreboardClan);
		ConfigSlider(Tr("Size"), "xc_scoreboard_scale", &g_Config.m_XcScoreboardScale, 60, 150, "%d%%");
		ToggleRow(Tr("Top of the server in the HUD"), "xc_hud_minitop", &g_Config.m_XcHudMinitop);
		ConfigSlider(Tr("Players in the list"), "xc_hud_minitop_count", &g_Config.m_XcHudMinitopCount, 1, 5);
		EndCard();
	}
	if(BeginCard("##hud_scoreboard", Tr("Scoreboard"), Tr("The player list in the colors of the menu, your row in the accent color"), &g_Config.m_XcHudScoreboard))
	{
		Description(Tr("Press Tab in game to see it"));
		ToggleRow(Tr("Vote panel in the same style"), "xc_hud_votes", &g_Config.m_XcHudVotes);
		EndCard();
	}
	if(BeginCard("##hud_wheels", Tr("Wheels"), Tr("Emote wheel and spectator menu in the colors of the menu"), &g_Config.m_XcHudWheels))
	{
		Description(Tr("The chosen emote lights up in the accent color"));
		EndCard();
	}
	EndCardGrid();
}

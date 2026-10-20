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

#include <algorithm>
#include <cmath>
#include <map>
#include <numeric>
#include <vector>
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
	const char *apIcons[NUM_SETTINGS] = {XC_ICON_PALETTE, XC_ICON_IMAGE, XC_ICON_USER, XC_ICON_KEYBOARD, XC_ICON_VOLUME, XC_ICON_COMMENTS, XC_ICON_DESKTOP, XC_ICON_LAYERS, XC_ICON_BRUSH, XC_ICON_SLIDERS, XC_ICON_GEAR};
	const char *apLabels[NUM_SETTINGS] = {Tr("Interface"), Tr("Background"), Tr("Player"), Tr("Controls"), Tr("Sound"), Tr("Chat"), Tr("Graphics"), Tr("HUD"), Tr("Textures"), "TClient", "DDNet"};

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
	case SETTINGS_DDNET: RenderSettingsDDNet(); break;
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
	if(BeginCard("##zoom", Tr("Camera zoom"), Tr("How far the camera sees and how smooth it moves")))
	{
		ConfigSlider(Tr("Default zoom"), "cl_default_zoom", &g_Config.m_ClDefaultZoom, 0, 20);
		ConfigSlider(Tr("Smooth zoom, ms"), "cl_smooth_zoom_time", &g_Config.m_ClSmoothZoomTime, 0, 5000);
		ConfigSlider(Tr("Camera smoothness"), "cl_dyncam_smoothness", &g_Config.m_ClDyncamSmoothness, 0, 100);
		ConfigSlider(Tr("Calm on fast mouse moves"), "cl_dyncam_stabilizing", &g_Config.m_ClDyncamStabilizing, 0, 100);
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
	if(BeginCard("##ui_sounds", Tr("Interface sounds"), Tr("Clicks, switches and notifications"), &g_Config.m_XcUiSounds))
	{
		ConfigSlider(Tr("Volume"), "xc_ui_sound_volume", &g_Config.m_XcUiSoundVolume, 0, 100, "%d%%");
		EndCard();
	}
	RenderEventSoundsCard();
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
	const STheme &t = Theme();
	char aBuf[128];
	// the screen modes are asked once and again when the window moves to another screen
	static CVideoMode s_aModes[256];
	static int s_NumModes = -1;
	static int s_ModesScreen = -1;
	static bool s_ListenerAdded = false;
	static bool s_ModesReload = false;
	if(!s_ListenerAdded)
	{
		s_ListenerAdded = true;
		Graphics()->AddWindowPropChangeListener([]() { s_ModesReload = true; });
	}
	if(s_NumModes < 0 || s_ModesReload || s_ModesScreen != g_Config.m_GfxScreen)
	{
		s_NumModes = Graphics()->GetVideoModes(s_aModes, (int)std::size(s_aModes), g_Config.m_GfxScreen);
		s_ModesScreen = g_Config.m_GfxScreen;
		s_ModesReload = false;
	}
	static bool s_FsaaNeedsRestart = false;
	static char s_aStartBackend[64] = "";
	static char s_aStartGpu[64] = "";
	if(!s_aStartBackend[0])
	{
		str_format(s_aStartBackend, sizeof(s_aStartBackend), "%s %d.%d.%d", g_Config.m_GfxBackend, g_Config.m_GfxGLMajor, g_Config.m_GfxGLMinor, g_Config.m_GfxGLPatch);
		str_copy(s_aStartGpu, g_Config.m_GfxGpuName);
	}

	BeginCardGrid("##graphics", 2);

	// ---- resolution
	if(BeginCard("##resolution", Tr("Resolution"), Tr("The size of the picture and the refresh rate of the monitor")))
	{
		const int Gcd = std::gcd(std::max(g_Config.m_GfxScreenWidth, 1), std::max(g_Config.m_GfxScreenHeight, 1));
		str_format(aBuf, sizeof(aBuf), "%s: %d×%d · %d %s · %d:%d", Tr("Now"),
			(int)(g_Config.m_GfxScreenWidth * Graphics()->ScreenHiDPIScale()), (int)(g_Config.m_GfxScreenHeight * Graphics()->ScreenHiDPIScale()),
			g_Config.m_GfxScreenRefreshRate, Tr("Hz"), g_Config.m_GfxScreenWidth / Gcd, g_Config.m_GfxScreenHeight / Gcd);
		ImGui::PushFont(Fonts().m_pLabel);
		ImGui::TextColored(t.m_Text, "%s", aBuf);
		ImGui::PopFont();
		ImGui::Dummy(ImVec2(0.0f, S(4.0f)));
		ImGui::BeginChild("##modes", ImVec2(AvailWidth(), S(220.0f)), ImGuiChildFlags_None, ImGuiWindowFlags_NoBackground);
		ImDrawList *pDraw = ImGui::GetWindowDrawList();
		const float RowWidth = ImGui::GetContentRegionAvail().x;
		const float RowHeight = S(30.0f);
		for(int i = s_NumModes - 1; i >= 0; i--) // the biggest first
		{
			const CVideoMode &Mode = s_aModes[i];
			const bool Selected = Mode.m_WindowWidth == g_Config.m_GfxScreenWidth && Mode.m_WindowHeight == g_Config.m_GfxScreenHeight && Mode.m_RefreshRate == g_Config.m_GfxScreenRefreshRate;
			ImGui::PushID(i);
			const ImVec2 Min = ImGui::GetCursorScreenPos();
			if(ImGui::InvisibleButton("##mode", ImVec2(RowWidth, RowHeight)) && !Selected)
			{
				g_Config.m_GfxScreenWidth = Mode.m_WindowWidth;
				g_Config.m_GfxScreenHeight = Mode.m_WindowHeight;
				g_Config.m_GfxScreenRefreshRate = Mode.m_RefreshRate;
				Graphics()->ResizeToScreen();
			}
			const bool Hovered = ImGui::IsItemHovered();
			if(Selected)
				pDraw->AddRectFilled(Min, ImVec2(Min.x + RowWidth, Min.y + RowHeight), Col(t.m_Accent, 0.22f), S(7.0f));
			else if(Hovered)
				pDraw->AddRectFilled(Min, ImVec2(Min.x + RowWidth, Min.y + RowHeight), Col(t.m_Track, 0.6f), S(7.0f));
			const int ModeGcd = std::gcd(std::max(Mode.m_WindowWidth, 1), std::max(Mode.m_WindowHeight, 1));
			char aSize[32], aRest[48];
			str_format(aSize, sizeof(aSize), "%d×%d", Mode.m_CanvasWidth, Mode.m_CanvasHeight);
			str_format(aRest, sizeof(aRest), "%d %s · %d:%d", Mode.m_RefreshRate, Tr("Hz"), Mode.m_WindowWidth / ModeGcd, Mode.m_WindowHeight / ModeGcd);
			ImFont *pLabel = Fonts().m_pLabel;
			ImFont *pSmall = Fonts().m_pSmall;
			pDraw->AddText(pLabel, pLabel->FontSize, ImVec2(Min.x + S(10.0f), Min.y + (RowHeight - pLabel->FontSize) * 0.5f), Col(Selected ? t.m_Text : t.m_TextSecondary), aSize);
			const float RestWidth = pSmall->CalcTextSizeA(pSmall->FontSize, FLT_MAX, 0.0f, aRest).x;
			pDraw->AddText(pSmall, pSmall->FontSize, ImVec2(Min.x + RowWidth - RestWidth - S(10.0f), Min.y + (RowHeight - pSmall->FontSize) * 0.5f), Col(t.m_TextMuted), aRest);
			ImGui::PopID();
		}
		if(s_NumModes == 0)
			Description(Tr("The system did not report any screen modes"));
		ImGui::EndChild();
		ToggleRow(Tr("Show all modes (also unusual ones)"), "gfx_display_all_video_modes", &g_Config.m_GfxDisplayAllVideoModes);
		EndCard();
	}

	// ---- window
	if(BeginCard("##screen", Tr("Window"), Tr("Window mode, monitor and frame rate")))
	{
		ParamLabel(Tr("Window mode"));
		const char *apModes[] = {Tr("Window"), Tr("Borderless window"), Tr("Fullscreen window"), Tr("Desktop fullscreen"), Tr("Fullscreen")};
		int Mode = g_Config.m_GfxFullscreen ? (g_Config.m_GfxFullscreen == 1 ? 4 : (g_Config.m_GfxFullscreen == 2 ? 3 : 2)) : (g_Config.m_GfxBorderless ? 1 : 0);
		if(ChoiceChips("##window_mode", &Mode, apModes, 5))
		{
			static const int s_aFullscreen[] = {0, 0, 3, 2, 1};
			Graphics()->SetWindowParams(s_aFullscreen[Mode], Mode == 1);
		}
		Description(Tr("«Desktop fullscreen» switches windows fastest; «Fullscreen» may give a bit more FPS"));
		if(Graphics()->GetNumScreens() > 1)
		{
			ParamLabel(Tr("Monitor"));
			const int NumScreens = std::min(Graphics()->GetNumScreens(), 8);
			static char s_aaScreenNames[8][96];
			const char *apScreens[8];
			for(int i = 0; i < NumScreens; i++)
			{
				str_format(s_aaScreenNames[i], sizeof(s_aaScreenNames[i]), "%d: %s", i + 1, Graphics()->GetScreenName(i));
				apScreens[i] = s_aaScreenNames[i];
			}
			int Screen = g_Config.m_GfxScreen;
			if(ChoiceChips("##monitor", &Screen, apScreens, NumScreens) && Screen != g_Config.m_GfxScreen)
				Graphics()->SwitchWindowScreen(Screen, true);
		}
		bool Vsync = g_Config.m_GfxVsync != 0;
		RowLabel(Tr("V-Sync (smooth, but adds delay)"), S(38.0f));
		if(Toggle("##vsync", &Vsync))
			Graphics()->SetVSync(Vsync);
		ConfigSlider(Tr("FPS limit"), "gfx_refresh_rate", &g_Config.m_GfxRefreshRate, 0, 1000, g_Config.m_GfxRefreshRate == 0 ? Tr("unlimited") : "%d");
		ConfigSlider(Tr("FPS when the game is in the background"), "cl_refresh_rate_inactive", &g_Config.m_ClRefreshRateInactive, 0, 500, g_Config.m_ClRefreshRateInactive == 0 ? Tr("unlimited") : "%d");
		ToggleRow(Tr("Show FPS"), "cl_showfps", &g_Config.m_ClShowfps);
		EndCard();
	}

	// ---- aspect ratio
	if(BeginCard("##aspect", Tr("Aspect ratio"), Tr("Draw the game in another aspect ratio and stretch it over the whole screen, like 4:3 stretched in CS")))
	{
		static const int s_aRatios[] = {0, 133, 125, 160, 178, 233};
		const char *apRatios[] = {Tr("Like the screen"), "4:3", "5:4", "16:10", "16:9", "21:9"};
		int Current = -1;
		for(int i = 0; i < (int)std::size(s_aRatios); i++)
			if(g_Config.m_XcAspectRatio == s_aRatios[i])
				Current = i;
		const int Before = Current;
		ChoiceChips("##ratio", &Current, apRatios, (int)std::size(s_aRatios));
		if(Current != Before && Current >= 0)
			g_Config.m_XcAspectRatio = s_aRatios[Current];
		int Custom = g_Config.m_XcAspectRatio == 0 ? (int)std::round(Graphics()->ScreenAspect() * 100.0f) : g_Config.m_XcAspectRatio;
		char aFormat[32];
		str_format(aFormat, sizeof(aFormat), "%.2f : 1", Custom / 100.0f);
		if(SliderInt(Tr("Own ratio"), &Custom, 100, 300, aFormat))
			g_Config.m_XcAspectRatio = Custom;
		const float Screen = Graphics()->ScreenAspect();
		const float Game = g_Config.m_XcAspectRatio > 0 ? g_Config.m_XcAspectRatio / 100.0f : Screen;
		if(std::abs(Game - Screen) < 0.02f)
			Description(Tr("The game looks as usual"));
		else if(Game < Screen)
			Description(Tr("Narrower than the screen: everything looks wider and bigger, you see less to the sides"));
		else
			Description(Tr("Wider than the screen: everything looks thinner, you see more to the sides (up to the limit of the game)"));
		Description(Tr("Only the game is stretched: the HUD, the chat and the menus stay sharp. The editor always shows the map as it is."));
		EndCard();
	}

	// ---- picture quality and the renderer
	if(BeginCard("##quality", Tr("Picture"), Tr("Smoothing, details and the renderer")))
	{
		ParamLabel(Tr("Smooth edges (FSAA)"));
		static const int s_aFsaa[] = {0, 2, 4, 8, 16};
		const char *apFsaa[] = {Tr("Off"), "2×", "4×", "8×", "16×"};
		int Fsaa = 0;
		for(int i = 0; i < (int)std::size(s_aFsaa); i++)
			if(g_Config.m_GfxFsaaSamples == s_aFsaa[i])
				Fsaa = i;
		if(ChoiceChips("##fsaa", &Fsaa, apFsaa, (int)std::size(s_aFsaa)))
		{
			uint32_t Backend = 0;
			if(Graphics()->SetMultiSampling(s_aFsaa[Fsaa], Backend))
				g_Config.m_GfxFsaaSamples = (int)Backend; // applied right away (the card may give less than asked)
			else
			{
				g_Config.m_GfxFsaaSamples = s_aFsaa[Fsaa];
				s_FsaaNeedsRestart = true;
			}
		}
		ToggleRow(Tr("High detail (map decorations)"), "gfx_high_detail", &g_Config.m_GfxHighDetail);

		// the renderers this computer has
		struct SRenderer
		{
			char m_aLabel[64];
			const char *m_pName;
			int m_Major, m_Minor, m_Patch;
		};
		static std::vector<SRenderer> s_vRenderers;
		if(s_vRenderers.empty())
		{
			for(int Type = 0; Type < BACKEND_TYPE_COUNT; Type++)
			{
				if(EBackendType(Type) == BACKEND_TYPE_AUTO)
					continue;
				for(int Age = 0; Age < GRAPHICS_DRIVER_AGE_TYPE_COUNT; Age++)
				{
					SRenderer Renderer;
					if(!Graphics()->GetDriverVersion(EGraphicsDriverAgeType(Age), Renderer.m_Major, Renderer.m_Minor, Renderer.m_Patch, Renderer.m_pName, EBackendType(Type)))
						continue;
					if(EBackendType(Type) == BACKEND_TYPE_OPENGL && EGraphicsDriverAgeType(Age) != GRAPHICS_DRIVER_AGE_TYPE_LEGACY && g_Config.m_GfxDriverIsBlocked)
						continue;
					str_format(Renderer.m_aLabel, sizeof(Renderer.m_aLabel), "%s %d.%d", Renderer.m_pName, Renderer.m_Major, Renderer.m_Minor);
					s_vRenderers.push_back(Renderer);
				}
			}
		}
		if(s_vRenderers.size() > 1)
		{
			ParamLabel(Tr("Renderer"));
			std::vector<const char *> vpLabels;
			int Current = -1;
			for(size_t i = 0; i < s_vRenderers.size(); i++)
			{
				const SRenderer &Renderer = s_vRenderers[i];
				vpLabels.push_back(Renderer.m_aLabel);
				if(str_comp_nocase(Renderer.m_pName, g_Config.m_GfxBackend) == 0 && Renderer.m_Major == g_Config.m_GfxGLMajor && Renderer.m_Minor == g_Config.m_GfxGLMinor && Renderer.m_Patch == g_Config.m_GfxGLPatch)
					Current = (int)i;
			}
			if(ChoiceChips("##renderer", &Current, vpLabels.data(), (int)vpLabels.size()) && Current >= 0)
			{
				const SRenderer &Renderer = s_vRenderers[Current];
				str_copy(g_Config.m_GfxBackend, Renderer.m_pName);
				g_Config.m_GfxGLMajor = Renderer.m_Major;
				g_Config.m_GfxGLMinor = Renderer.m_Minor;
				g_Config.m_GfxGLPatch = Renderer.m_Patch;
			}
			Description(Tr("The glow, the blur and the neon outline of Exora need OpenGL 3.3"));
		}
		const auto &GpuList = Graphics()->GetGpus();
		if(GpuList.m_vGpus.size() > 1)
		{
			ParamLabel(Tr("Graphics card"));
			std::vector<const char *> vpGpus;
			vpGpus.push_back(Tr("Automatic"));
			int Current = str_comp(g_Config.m_GfxGpuName, "auto") == 0 ? 0 : -1;
			for(size_t i = 0; i < GpuList.m_vGpus.size(); i++)
			{
				vpGpus.push_back(GpuList.m_vGpus[i].m_aName);
				if(str_comp(GpuList.m_vGpus[i].m_aName, g_Config.m_GfxGpuName) == 0)
					Current = (int)i + 1;
			}
			if(ChoiceChips("##gpu", &Current, vpGpus.data(), (int)vpGpus.size()) && Current >= 0)
				str_copy(g_Config.m_GfxGpuName, Current == 0 ? "auto" : GpuList.m_vGpus[Current - 1].m_aName);
		}
		// some of these need a restart of the game
		char aBackend[64];
		str_format(aBackend, sizeof(aBackend), "%s %d.%d.%d", g_Config.m_GfxBackend, g_Config.m_GfxGLMajor, g_Config.m_GfxGLMinor, g_Config.m_GfxGLPatch);
		const bool NeedRestart = str_comp(aBackend, s_aStartBackend) != 0 || str_comp(g_Config.m_GfxGpuName, s_aStartGpu) != 0 || s_FsaaNeedsRestart;
		if(NeedRestart)
		{
			ImGui::Dummy(ImVec2(0.0f, S(4.0f)));
			Description(Tr("The game has to restart for this"));
			if(Button(Tr("Restart now"), true, -1.0f))
				Client()->Restart();
		}
		EndCard();
	}

	if(BeginCard("##prediction", Tr("Prediction and ping"), Tr("How the client shows the game ahead of the server, so it feels without lag")))
	{
		ToggleRow(Tr("Predict my movement"), "cl_predict", &g_Config.m_ClPredict);
		ToggleRow(Tr("Predict the dummy"), "cl_predict_dummy", &g_Config.m_ClPredictDummy);
		ConfigSlider(Tr("Prediction margin, ms"), "cl_prediction_margin", &g_Config.m_ClPredictionMargin, 1, 300);
		ToggleRow(Tr("Antiping: other players and shots without delay"), "cl_antiping", &g_Config.m_ClAntiPing);
		if(g_Config.m_ClAntiPing)
		{
			ToggleRow(Tr("Smooth other players"), "cl_antiping_smooth", &g_Config.m_ClAntiPingSmooth);
			ToggleRow(Tr("Grenades"), "cl_antiping_grenade", &g_Config.m_ClAntiPingGrenade);
			ToggleRow(Tr("Bullets and lasers"), "cl_antiping_weapons", &g_Config.m_ClAntiPingWeapons);
		}
		Description(Tr("Everything else is in Settings > DDNet"));
		EndCard();
	}
	if(BeginCard("##performance", Tr("Performance"), Tr("Low, medium or high in one click, then each effect by hand")))
	{
		RenderPerformance();
		ImGui::Dummy(ImVec2(0.0f, S(6.0f)));
		// the render module draws the glow, the blur and the neon outline
		ToggleRow(Tr("Effects module (OpenGL 3.3)"), "xc_render", &g_Config.m_XcRender);
		if(!Graphics()->XcRenderAvailable())
			Description(Tr("Not available: needs OpenGL 3.3"));
		ParamLabel(Tr("Effect quality"));
		const char *apQuality[] = {Tr("Low"), Tr("Medium"), Tr("High")};
		ConfigSegmented("##quality", "xc_fx_quality", &g_Config.m_XcFxQuality, apQuality, 3);
		EndCard();
	}
	EndCardGrid();
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
	if(BeginCard("##hud_scoreboard", Tr("Scoreboard"), Tr("The player list on Tab and the top of the server in the HUD")))
	{
		ToggleRow(Tr("Own scoreboard: teams, medals, times and pings"), "xc_scoreboard", &g_Config.m_XcScoreboard);
		if(g_Config.m_XcScoreboard)
		{
			ToggleRow(Tr("Show clans"), "xc_scoreboard_clan", &g_Config.m_XcScoreboardClan);
			ConfigSlider(Tr("Size"), "xc_scoreboard_scale", &g_Config.m_XcScoreboardScale, 60, 150, "%d%%");
		}
		else
			ToggleRow(Tr("Standard scoreboard in the colors of the menu"), "xc_hud_scoreboard", &g_Config.m_XcHudScoreboard);
		ToggleRow(Tr("Vote panel in the same style"), "xc_hud_votes", &g_Config.m_XcHudVotes);
		ToggleRow(Tr("Top of the server in the HUD"), "xc_hud_minitop", &g_Config.m_XcHudMinitop);
		if(g_Config.m_XcHudMinitop)
			ConfigSlider(Tr("Players in the list"), "xc_hud_minitop_count", &g_Config.m_XcHudMinitopCount, 1, 5);
		EndCard();
	}
	if(BeginCard("##hud_wheels", Tr("Wheels"), Tr("Emote wheel and spectator menu in the colors of the menu"), &g_Config.m_XcHudWheels))
	{
		Description(Tr("The chosen emote lights up in the accent color"));
		EndCard();
	}
	EndCardGrid();
}

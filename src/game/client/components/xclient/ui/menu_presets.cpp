// Theme presets (theme + accent + font + background in one click) and performance presets.
#include "feedback.h"
#include "icons.h"
#include "lang.h"
#include "menu.h"
#include "theme.h"
#include "widgets.h"

#include <base/io.h>
#include <base/str.h>

#include <engine/external/imgui/imgui.h>
#include <engine/external/imgui/imgui_internal.h>
#include <engine/shared/config.h>
#include <engine/storage.h>

#include <game/client/gameclient.h>

#include <algorithm>

using namespace XcUi;

static const char *const THEME_PRESETS_FILE = "xclient/theme_presets.cfg";

// ---------------------------------------------------------------------------------------------
// theme presets

void CXcMenu::LoadThemePresets()
{
	m_ThemePresetsLoaded = true;
	m_vThemePresets.clear();

	// shipped with the client, they only change colors and fonts, the background stays animated
	const auto Builtin = [&](const char *pName, int Theme, int Accent, int Font) {
		SThemePreset Preset;
		Preset.m_Name = pName;
		Preset.m_Theme = Theme;
		Preset.m_Accent = Accent;
		Preset.m_Font = Font;
		Preset.m_Background = 1;
		Preset.m_Builtin = true;
		m_vThemePresets.push_back(Preset);
	};
	Builtin("Midnight", THEME_DARK, ACCENT_BLUE, 0);
	Builtin("Mint", THEME_DARK, ACCENT_MINT, 1);
	Builtin("Sunset", THEME_DARK, ACCENT_ORANGE, 2);
	Builtin("Sakura", THEME_DARK, ACCENT_PINK, 0);
	Builtin("Paper", THEME_LIGHT, ACCENT_WHITE, 0);
	Builtin("Lime", THEME_LIGHT, ACCENT_LIME, 1);

	// own presets: one per line, fields separated by tabs
	char *pFile = Storage()->ReadFileStr(THEME_PRESETS_FILE, IStorage::TYPE_SAVE);
	if(!pFile)
		return;
	char aLine[512];
	const char *pRest = pFile;
	while((pRest = str_next_token(pRest, "\n", aLine, sizeof(aLine))))
	{
		char aaFields[8][160] = {};
		int NumFields = 0;
		const char *pField = aLine;
		while(NumFields < 8 && (pField = str_next_token(pField, "\t", aaFields[NumFields], sizeof(aaFields[NumFields]))))
			NumFields++;
		if(NumFields < 6 || !aaFields[0][0])
			continue;
		SThemePreset Preset;
		Preset.m_Name = aaFields[0];
		Preset.m_Theme = std::clamp(str_toint(aaFields[1]), 0, 1);
		Preset.m_Accent = std::clamp(str_toint(aaFields[2]), 0, NUM_ACCENTS - 1);
		Preset.m_Font = std::clamp(str_toint(aaFields[3]), 0, 2);
		Preset.m_Background = std::clamp(str_toint(aaFields[4]), 0, 3);
		Preset.m_Dim = std::clamp(str_toint(aaFields[5]), 0, 90);
		Preset.m_BgFile = NumFields > 6 && str_comp(aaFields[6], "-") != 0 ? aaFields[6] : "";
		Preset.m_MenuMap = NumFields > 7 && str_comp(aaFields[7], "-") != 0 ? aaFields[7] : "";
		m_vThemePresets.push_back(Preset);
	}
	free(pFile);
}

void CXcMenu::SaveThemePresets()
{
	IOHANDLE File = Storage()->OpenFile(THEME_PRESETS_FILE, IOFLAG_WRITE, IStorage::TYPE_SAVE);
	if(!File)
	{
		Toast(Tr("Could not save"), TOAST_ERROR);
		return;
	}
	for(const SThemePreset &Preset : m_vThemePresets)
	{
		if(Preset.m_Builtin)
			continue;
		char aLine[512];
		str_format(aLine, sizeof(aLine), "%s\t%d\t%d\t%d\t%d\t%d\t%s\t%s\n", Preset.m_Name.c_str(), Preset.m_Theme, Preset.m_Accent, Preset.m_Font, Preset.m_Background, Preset.m_Dim,
			Preset.m_BgFile.empty() ? "-" : Preset.m_BgFile.c_str(), Preset.m_MenuMap.empty() ? "-" : Preset.m_MenuMap.c_str());
		io_write(File, aLine, str_length(aLine));
	}
	io_close(File);
}

bool CXcMenu::ThemePresetActive(const SThemePreset &Preset) const
{
	if(Preset.m_Theme != g_Config.m_XcUiTheme || Preset.m_Accent != g_Config.m_XcUiAccent || Preset.m_Font != g_Config.m_XcUiFont || Preset.m_Background != g_Config.m_XcMenuBackground)
		return false;
	if(Preset.m_Builtin)
		return true;
	return (Preset.m_Background != 3 || Preset.m_BgFile == g_Config.m_XcMenuBgFile) && (Preset.m_Background != 2 || Preset.m_MenuMap == g_Config.m_ClMenuMap);
}

void CXcMenu::ApplyThemePreset(const SThemePreset &Preset)
{
	SetConfigInt("xc_ui_theme", Preset.m_Theme);
	SetConfigInt("xc_ui_accent", Preset.m_Accent);
	SetConfigInt("xc_ui_font", Preset.m_Font);
	if(Preset.m_Builtin)
		return; // keep the own background
	SetConfigInt("xc_menu_background", Preset.m_Background);
	SetConfigInt("xc_menu_bg_dim", Preset.m_Dim);
	if(Preset.m_Background == 3)
		SetConfigString("xc_menu_bg_file", Preset.m_BgFile.c_str());
	if(Preset.m_Background == 2 && str_comp(g_Config.m_ClMenuMap, Preset.m_MenuMap.c_str()) != 0)
	{
		SetConfigString("cl_menu_map", Preset.m_MenuMap.c_str());
		GameClient()->m_MenuBackground.LoadMenuBackground();
	}
}

void CXcMenu::RenderThemePresets()
{
	if(!m_ThemePresetsLoaded)
		LoadThemePresets();
	if(!m_PresetDeleteArmed.empty() && ImGui::GetTime() > m_PresetDeleteArmedUntil)
		m_PresetDeleteArmed.clear();

	ImDrawList *pDraw = ImGui::GetWindowDrawList();
	const float Gap = S(10.0f);
	const float MinWidth = S(150.0f);
	const float Avail = AvailWidth();
	const int Columns = std::max(1, (int)((Avail + Gap) / (MinWidth + Gap)));
	const float Width = (Avail - Gap * (Columns - 1)) / Columns;
	const float PreviewHeight = S(64.0f);
	const float Height = PreviewHeight + S(28.0f);
	static const char *const s_apFonts[] = {"Manrope", "Onest", "Unbounded"};

	int Remove = -1;
	int Apply = -1;
	for(int i = 0; i < (int)m_vThemePresets.size(); i++)
	{
		const SThemePreset &Preset = m_vThemePresets[i];
		if(i % Columns != 0)
			ImGui::SameLine(0.0f, Gap);
		else if(i > 0)
			ImGui::Dummy(ImVec2(0.0f, Gap * 0.3f));
		ImGui::PushID(i);
		const ImVec2 Pos = ImGui::GetCursorScreenPos();
		const bool Clicked = ImGui::InvisibleButton("##preset", ImVec2(Width, Height));
		const bool Hovered = ImGui::IsItemHovered();
		const bool Active = ThemePresetActive(Preset);
		const float Hover = Animate(ImGui::GetID("##hover"), Hovered ? 1.0f : 0.0f);
		const float Select = Animate(ImGui::GetID("##select"), Active ? 1.0f : 0.0f);

		// small picture of the menu in the preset's colors: background, panel, a card with an accent button
		const STheme Look = MakeTheme(Preset.m_Theme, Preset.m_Accent);
		const ImVec2 Max(Pos.x + Width, Pos.y + PreviewHeight);
		const float Rounding = S(10.0f);
		pDraw->AddRectFilled(Pos, Max, Col(ImVec4(Look.m_Background.x, Look.m_Background.y, Look.m_Background.z, 1.0f)), Rounding);
		pDraw->PushClipRect(ImVec2(Pos.x + S(2.0f), Pos.y + S(2.0f)), ImVec2(Max.x - S(2.0f), Max.y - S(2.0f)), true);
		pDraw->AddCircleFilled(ImVec2(Pos.x + Width * 0.8f, Pos.y + PreviewHeight * 0.3f), PreviewHeight * 0.45f, Col(Look.m_Accent, 0.16f), 32);
		const ImVec2 CardMin(Pos.x + S(10.0f), Pos.y + S(12.0f));
		const ImVec2 CardMax(Pos.x + Width * 0.62f, Max.y - S(10.0f));
		pDraw->AddRectFilled(CardMin, CardMax, Col(ImVec4(Look.m_Card.x, Look.m_Card.y, Look.m_Card.z, 1.0f)), S(6.0f));
		pDraw->AddRectFilled(ImVec2(CardMin.x + S(8.0f), CardMin.y + S(8.0f)), ImVec2(CardMin.x + (CardMax.x - CardMin.x) * 0.6f, CardMin.y + S(12.0f)), Col(Look.m_Text, 0.8f), S(2.0f));
		pDraw->AddRectFilled(ImVec2(CardMin.x + S(8.0f), CardMin.y + S(17.0f)), ImVec2(CardMin.x + (CardMax.x - CardMin.x) * 0.8f, CardMin.y + S(20.0f)), Col(Look.m_TextMuted, 0.7f), S(2.0f));
		pDraw->AddRectFilled(ImVec2(CardMin.x + S(8.0f), CardMax.y - S(14.0f)), ImVec2(CardMax.x - S(8.0f), CardMax.y - S(6.0f)), Col(Look.m_Accent), S(4.0f));
		// toggle in the accent color on the right
		const ImVec2 ToggleMin(Pos.x + Width * 0.7f, Max.y - S(22.0f));
		pDraw->AddRectFilled(ToggleMin, ImVec2(ToggleMin.x + S(22.0f), ToggleMin.y + S(12.0f)), Col(Look.m_Accent), S(6.0f));
		pDraw->AddCircleFilled(ImVec2(ToggleMin.x + S(16.0f), ToggleMin.y + S(6.0f)), S(4.0f), Col(Look.m_OnAccent), 12);
		pDraw->PopClipRect();
		if(Hover > 0.0f)
			pDraw->AddRectFilled(Pos, Max, Col(ImVec4(1, 1, 1, 1), 0.06f * Hover), Rounding);
		pDraw->AddRect(Pos, Max, Col(Mix(Theme().m_Border, Theme().m_Accent, Select)), Rounding, 0, 1.0f + S(1.5f) * Select);

		// name and font under the picture
		ImFont *pSmall = Fonts().m_pSmall;
		const char *pName = Preset.m_Builtin ? Tr(Preset.m_Name.c_str()) : Preset.m_Name.c_str();
		ImGui::RenderTextEllipsis(pDraw, ImVec2(Pos.x + S(2.0f), Max.y + S(6.0f)), ImVec2(Pos.x + Width * 0.6f, Max.y + S(24.0f)), Pos.x + Width * 0.6f, Pos.x + Width * 0.6f, pName, nullptr, nullptr);
		const char *pFont = s_apFonts[Preset.m_Font];
		const float FontWidth = pSmall->CalcTextSizeA(pSmall->FontSize, FLT_MAX, 0.0f, pFont).x;
		pDraw->AddText(pSmall, pSmall->FontSize, ImVec2(Pos.x + Width - FontWidth - S(2.0f), Max.y + S(8.0f)), Col(Theme().m_TextMuted), pFont);

		// own presets have a delete button
		bool OverTrash = false;
		if(!Preset.m_Builtin)
		{
			const bool Armed = m_PresetDeleteArmed == Preset.m_Name;
			if(Hover > 0.01f || Armed)
			{
				const float Radius = S(11.0f);
				const ImVec2 Center(Max.x - S(6.0f) - Radius, Pos.y + S(6.0f) + Radius);
				OverTrash = ImGui::IsMouseHoveringRect(ImVec2(Center.x - Radius, Center.y - Radius), ImVec2(Center.x + Radius, Center.y + Radius));
				pDraw->AddCircleFilled(Center, Radius, Armed ? Col(ImVec4(1.0f, 0.3f, 0.3f, 1.0f)) : Col(ImVec4(0, 0, 0, 1), OverTrash ? 0.8f : 0.55f), 24);
				const ImVec2 IconSize = pSmall->CalcTextSizeA(pSmall->FontSize, FLT_MAX, 0.0f, XC_ICON_TRASH);
				pDraw->AddText(pSmall, pSmall->FontSize, ImVec2(Center.x - IconSize.x * 0.5f, Center.y - IconSize.y * 0.5f), IM_COL32_WHITE, XC_ICON_TRASH);
				if(OverTrash)
					ImGui::SetTooltip("%s", Armed ? Tr("Click again to delete") : Tr("Delete"));
			}
		}
		if(Clicked && OverTrash)
		{
			if(m_PresetDeleteArmed == Preset.m_Name)
				Remove = i;
			else
			{
				m_PresetDeleteArmed = Preset.m_Name;
				m_PresetDeleteArmedUntil = (float)ImGui::GetTime() + 3.0f;
			}
		}
		else if(Clicked)
			Apply = i;
		ImGui::PopID();
	}

	if(Apply >= 0)
	{
		ApplyThemePreset(m_vThemePresets[Apply]);
		PlayUiSound(SOUND_TAB);
	}
	if(Remove >= 0)
	{
		m_vThemePresets.erase(m_vThemePresets.begin() + Remove);
		m_PresetDeleteArmed.clear();
		SaveThemePresets();
		Toast(Tr("Deleted"));
	}

	// save the current look as a new preset
	ImGui::Dummy(ImVec2(0.0f, S(6.0f)));
	const float ButtonWidth = S(150.0f);
	InputText("##preset_name", m_aPresetName, sizeof(m_aPresetName), Tr("Preset name"), std::max(AvailWidth() - ButtonWidth - S(8.0f), S(80.0f)));
	const bool Enter = ImGui::IsItemDeactivated() && (ImGui::IsKeyPressed(ImGuiKey_Enter) || ImGui::IsKeyPressed(ImGuiKey_KeypadEnter));
	ImGui::SameLine(0.0f, S(8.0f));
	if(Button(Tr("Save current"), true, ButtonWidth) || Enter)
	{
		char aName[32];
		str_copy(aName, m_aPresetName[0] ? m_aPresetName : Tr("My preset"));
		// tabs and new lines would break the file
		for(char *p = aName; *p; p++)
			if(*p == '\t' || *p == '\n' || *p == '\r')
				*p = ' ';
		SThemePreset Preset;
		Preset.m_Name = aName;
		Preset.m_Theme = g_Config.m_XcUiTheme;
		Preset.m_Accent = g_Config.m_XcUiAccent;
		Preset.m_Font = g_Config.m_XcUiFont;
		Preset.m_Background = g_Config.m_XcMenuBackground;
		Preset.m_Dim = g_Config.m_XcMenuBgDim;
		Preset.m_BgFile = g_Config.m_XcMenuBgFile;
		Preset.m_MenuMap = g_Config.m_ClMenuMap;
		// the same name replaces the old preset
		auto It = std::find_if(m_vThemePresets.begin(), m_vThemePresets.end(), [&](const SThemePreset &p) { return !p.m_Builtin && p.m_Name == Preset.m_Name; });
		if(It != m_vThemePresets.end())
			*It = Preset;
		else
			m_vThemePresets.push_back(Preset);
		SaveThemePresets();
		m_aPresetName[0] = '\0';
		Toast(Tr("Preset saved"));
	}
}

// ---------------------------------------------------------------------------------------------
// performance presets

namespace
{
	struct SPerformance
	{
		int m_HighDetail;
		int m_FxQuality;
		int m_MenuBlur;
		int m_BgBlur;
		int m_Bloom;
	};
	// low turns off everything heavy, medium keeps the look with cheaper bloom, high is everything
	const SPerformance gs_aPerformance[] = {
		{0, 0, 0, 0, 0},
		{1, 1, 1, 0, 80},
		{1, 2, 1, 0, 100},
	};
} // namespace

int CXcMenu::CurrentPerformancePreset() const
{
	for(int i = 0; i < 3; i++)
	{
		const SPerformance &p = gs_aPerformance[i];
		if(g_Config.m_GfxHighDetail == p.m_HighDetail && g_Config.m_XcFxQuality == p.m_FxQuality && g_Config.m_XcMenuBlur == p.m_MenuBlur && g_Config.m_XcMenuBgBlur == p.m_BgBlur && g_Config.m_XcBloom == p.m_Bloom)
			return i;
	}
	return -1;
}

void CXcMenu::RenderPerformance()
{
	const int Current = CurrentPerformancePreset();
	int Selected = Current < 0 ? 3 : Current;
	const char *apModes[] = {Tr("Low"), Tr("Medium"), Tr("High"), Tr("Custom")};
	if(Segmented("##performance_mode", &Selected, apModes, 4) && Selected < 3)
	{
		const SPerformance &p = gs_aPerformance[Selected];
		SetConfigInt("gfx_high_detail", p.m_HighDetail);
		SetConfigInt("xc_fx_quality", p.m_FxQuality);
		SetConfigInt("xc_menu_blur", p.m_MenuBlur);
		SetConfigInt("xc_menu_bg_blur", p.m_BgBlur);
		SetConfigInt("xc_bloom", p.m_Bloom);
	}
	Description(Selected == 3 ? Tr("Your own mix, change anything below") : (Selected == 0 ? Tr("For weak computers: no blur, no bloom, simple map") : (Selected == 1 ? Tr("Balanced: blur and light bloom") : Tr("Everything on"))));
	// each effect by hand
	ToggleRow(Tr("Detailed map"), "gfx_high_detail", &g_Config.m_GfxHighDetail);
	ToggleRow(Tr("Blur the game behind the menu"), "xc_menu_blur", &g_Config.m_XcMenuBlur);
	ToggleRow(Tr("Blur own background"), "xc_menu_bg_blur", &g_Config.m_XcMenuBgBlur);
}

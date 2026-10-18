// Menu background settings: animated / blurred game / map / own pictures and gifs.
#include "feedback.h"
#include "icons.h"
#include "lang.h"
#include "menu.h"
#include "theme.h"
#include "widgets.h"

#include <base/fs.h>
#include <base/mem.h>
#include <base/str.h>

#include <engine/client.h>
#include <engine/external/imgui/imgui.h>
#include <engine/external/imgui/imgui_internal.h>
#include <engine/shared/config.h>
#include <engine/storage.h>

#include <game/client/gameclient.h>

using namespace XcUi;

int CXcMenu::EffectiveBackground() const
{
	switch(g_Config.m_XcMenuBackground)
	{
	case 0: return IsOnline() ? BG_BLUR_GAME : BG_ANIMATED; // nothing to blur in the main menu
	case 2: return BG_MAP;
	case 3: return m_Backgrounds.HasFrames() ? BG_CUSTOM : BG_ANIMATED; // while loading or when the file is gone
	default: return BG_ANIMATED;
	}
}

void CXcMenu::HandleBackgroundFile(const char *pPath)
{
	// "Copy as path" in the Windows explorer puts quotes around the path
	char aPath[IO_MAX_PATH_LENGTH];
	str_copy(aPath, str_skip_whitespaces_const(pPath));
	str_utf8_trim_right(aPath);
	int Length = str_length(aPath);
	if(Length >= 2 && aPath[0] == '"' && aPath[Length - 1] == '"')
	{
		aPath[Length - 1] = '\0';
		mem_move(aPath, aPath + 1, Length - 1);
	}
	if(aPath[0] == '\0')
		return;

	char aAdded[128];
	switch(m_Backgrounds.AddFile(aPath, aAdded, sizeof(aAdded)))
	{
	case CXcBackgrounds::ADD_IMAGE:
		SetConfigString("xc_menu_bg_file", aAdded);
		SetConfigInt("xc_menu_background", 3);
		Toast(Tr("Background added"));
		m_aBgPath[0] = '\0';
		break;
	case CXcBackgrounds::ADD_MAP:
		GameClient()->m_MenuBackground.XcRescanThemes();
		SetConfigString("cl_menu_map", aAdded);
		SetConfigInt("xc_menu_background", 2);
		GameClient()->m_MenuBackground.LoadMenuBackground();
		Toast(Tr("Map added"));
		m_aBgPath[0] = '\0';
		break;
	case CXcBackgrounds::ADD_UNSUPPORTED:
		Toast(Tr("Only png, jpg, gif, bmp and map files"), TOAST_ERROR);
		break;
	default:
		Toast(Tr("Could not read this file"), TOAST_ERROR);
		break;
	}
}

void CXcMenu::RemoveBackground(const std::string &Key)
{
	const std::string Name = Key.substr(2);
	if(Key[0] == 'f')
	{
		if(!m_Backgrounds.Remove(Name.c_str()))
		{
			Toast(Tr("Could not delete the file"), TOAST_ERROR);
			return;
		}
		if(str_comp(g_Config.m_XcMenuBgFile, Name.c_str()) == 0)
			SetConfigString("xc_menu_bg_file", "");
	}
	else
	{
		char aPath[IO_MAX_PATH_LENGTH];
		str_format(aPath, sizeof(aPath), "themes/%s.map", Name.c_str());
		if(!Storage()->RemoveFile(aPath, IStorage::TYPE_SAVE))
		{
			Toast(Tr("Could not delete the file"), TOAST_ERROR);
			return;
		}
		if(str_comp(g_Config.m_ClMenuMap, Name.c_str()) == 0)
		{
			SetConfigString("cl_menu_map", "");
			GameClient()->m_MenuBackground.LoadMenuBackground();
		}
		GameClient()->m_MenuBackground.XcRescanThemes();
	}
	Toast(Tr("Deleted"));
}

// One picture in a gallery: image cropped to the tile (16:9 by default) with rounded corners, name below.
// Contain shows the whole picture instead (hats). Returns true when clicked; *pDelete is set when the
// trash button was clicked.
bool CXcMenu::BackgroundTile(const char *pId, IGraphics::CTextureHandle Texture, float TextureAspect, const char *pName, const char *pBadge, bool Selected, bool Deletable, bool *pDelete, float Width, float UMin, float UMax, float TileAspect, bool Contain)
{
	const STheme &t = Theme();
	ImDrawList *pDraw = ImGui::GetWindowDrawList();
	const float ImageHeight = Width / TileAspect;
	const float NameHeight = ImGui::GetFontSize() + S(10.0f);
	const ImVec2 Pos = ImGui::GetCursorScreenPos();
	const ImVec2 ImageMax(Pos.x + Width, Pos.y + ImageHeight);
	const float Rounding = S(10.0f);

	ImGui::PushID(pId);
	const bool Clicked = ImGui::InvisibleButton("##tile", ImVec2(Width, ImageHeight + NameHeight));
	const bool Hovered = ImGui::IsItemHovered();
	const float Hover = Animate(ImGui::GetID("##hover"), Hovered ? 1.0f : 0.0f);
	const float Select = Animate(ImGui::GetID("##select"), Selected ? 1.0f : 0.0f);

	// picture, or a placeholder while the thumbnail loads
	pDraw->AddRectFilled(Pos, ImageMax, Col(t.m_Track), Rounding);
	if(Texture.IsValid() && !Texture.IsNullTexture() && Contain)
	{
		// contain: the whole picture with some air around it
		const float Padding = Width * 0.12f;
		const ImVec2 Room(Width - Padding * 2.0f, ImageHeight - Padding * 2.0f);
		const ImVec2 Fit = TextureAspect > Room.x / Room.y ? ImVec2(Room.x, Room.x / TextureAspect) : ImVec2(Room.y * TextureAspect, Room.y);
		const ImVec2 ImgMin(Pos.x + (Width - Fit.x) * 0.5f, Pos.y + (ImageHeight - Fit.y) * 0.5f);
		const float Lift = Hover * S(3.0f);
		pDraw->AddImage((ImTextureID)(uintptr_t)m_Backend.TextureId(Texture), ImVec2(ImgMin.x, ImgMin.y - Lift), ImVec2(ImgMin.x + Fit.x, ImgMin.y + Fit.y - Lift));
	}
	else if(Texture.IsValid() && !Texture.IsNullTexture())
	{
		// cover: crop the side that does not fit the tile, inside the used part [UMin, UMax] of the texture
		ImVec2 Uv0(UMin, 0), Uv1(UMax, 1);
		if(TextureAspect > TileAspect)
		{
			const float u = TileAspect / TextureAspect * (UMax - UMin);
			const float Middle = (UMin + UMax) * 0.5f;
			Uv0.x = Middle - u * 0.5f;
			Uv1.x = Middle + u * 0.5f;
		}
		else
		{
			const float v = TextureAspect / TileAspect;
			Uv0.y = (1.0f - v) * 0.5f;
			Uv1.y = (1.0f + v) * 0.5f;
		}
		pDraw->AddImageRounded((ImTextureID)(uintptr_t)m_Backend.TextureId(Texture), Pos, ImageMax, Uv0, Uv1, IM_COL32_WHITE, Rounding);
	}
	else
	{
		ImGui::PushFont(m_Backend.Fonts().m_pIcons);
		const ImVec2 IconSize = ImGui::CalcTextSize(XC_ICON_IMAGE);
		pDraw->AddText(ImVec2(Pos.x + (Width - IconSize.x) * 0.5f, Pos.y + (ImageHeight - IconSize.y) * 0.5f), Col(t.m_TextMuted), XC_ICON_IMAGE);
		ImGui::PopFont();
	}
	// hover lightens, selection draws an accent frame with a check mark
	if(Hover > 0.0f)
		pDraw->AddRectFilled(Pos, ImageMax, Col(ImVec4(1, 1, 1, 1), 0.08f * Hover), Rounding);
	pDraw->AddRect(Pos, ImageMax, Col(Mix(t.m_Border, t.m_Accent, Select)), Rounding, 0, 1.0f + S(1.5f) * Select);
	if(Select > 0.01f)
	{
		const float Radius = S(11.0f);
		const ImVec2 Center(Pos.x + S(8.0f) + Radius, Pos.y + S(8.0f) + Radius);
		pDraw->AddCircleFilled(Center, Radius, Col(t.m_Accent, Select), 24);
		ImGui::PushFont(m_Backend.Fonts().m_pSmall);
		const ImVec2 CheckSize = ImGui::CalcTextSize(XC_ICON_CHECK);
		pDraw->AddText(ImVec2(Center.x - CheckSize.x * 0.5f, Center.y - CheckSize.y * 0.5f), Col(t.m_OnAccent, Select), XC_ICON_CHECK);
		ImGui::PopFont();
	}
	if(pBadge)
	{
		ImGui::PushFont(m_Backend.Fonts().m_pSmall);
		const ImVec2 BadgeSize = ImGui::CalcTextSize(pBadge);
		const ImVec2 BadgeMin(ImageMax.x - BadgeSize.x - S(16.0f), ImageMax.y - BadgeSize.y - S(14.0f));
		pDraw->AddRectFilled(BadgeMin, ImVec2(ImageMax.x - S(8.0f), ImageMax.y - S(8.0f)), Col(ImVec4(0, 0, 0, 0.6f)), S(6.0f));
		pDraw->AddText(ImVec2(BadgeMin.x + S(4.0f), BadgeMin.y + S(3.0f)), IM_COL32_WHITE, pBadge);
		ImGui::PopFont();
	}

	// name, cut with "..." when too long
	ImGui::PushFont(m_Backend.Fonts().m_pLabel);
	const ImVec2 NamePos(Pos.x + S(2.0f), ImageMax.y + S(6.0f));
	ImGui::RenderTextEllipsis(pDraw, NamePos, ImVec2(Pos.x + Width, NamePos.y + ImGui::GetFontSize() + S(2.0f)), Pos.x + Width, Pos.x + Width, pName, nullptr, nullptr);
	ImGui::PopFont();

	// trash button in the top right corner, shown on hover
	bool DeleteClicked = false;
	if(Deletable && (Hover > 0.01f || m_BgDeleteArmed == pId))
	{
		const float Radius = S(13.0f);
		const ImVec2 Center(ImageMax.x - S(8.0f) - Radius, Pos.y + S(8.0f) + Radius);
		const bool Armed = m_BgDeleteArmed == pId && ImGui::GetTime() < m_BgDeleteArmedUntil;
		const bool OverTrash = ImGui::IsMouseHoveringRect(ImVec2(Center.x - Radius, Center.y - Radius), ImVec2(Center.x + Radius, Center.y + Radius));
		const ImVec4 Red(1.0f, 0.30f, 0.30f, 1.0f);
		pDraw->AddCircleFilled(Center, Radius, Armed ? Col(Red) : Col(ImVec4(0, 0, 0, OverTrash ? 0.8f : 0.55f), std::max(Hover, Armed ? 1.0f : 0.0f)), 24);
		ImGui::PushFont(m_Backend.Fonts().m_pSmall);
		const ImVec2 IconSize = ImGui::CalcTextSize(XC_ICON_TRASH);
		pDraw->AddText(ImVec2(Center.x - IconSize.x * 0.5f, Center.y - IconSize.y * 0.5f), Col(ImVec4(1, 1, 1, 1), std::max(Hover, Armed ? 1.0f : 0.0f)), XC_ICON_TRASH);
		ImGui::PopFont();
		if(OverTrash)
			ImGui::SetTooltip("%s", Armed ? Tr("Click again to delete") : Tr("Delete"));
		DeleteClicked = Clicked && OverTrash;
	}
	if(pDelete)
		*pDelete = DeleteClicked;
	ImGui::PopID();
	return Clicked && !DeleteClicked;
}

void CXcMenu::RenderSettingsBackground()
{
	if(!m_BackgroundsScanned)
	{
		m_Backgrounds.Refresh();
		m_BackgroundsScanned = true;
	}
	if(!m_BgPendingAdd.empty())
	{
		HandleBackgroundFile(m_BgPendingAdd.c_str());
		m_BgPendingAdd.clear();
	}
	if(!m_BgPendingRemove.empty())
	{
		RemoveBackground(m_BgPendingRemove);
		m_BgPendingRemove.clear();
	}
	m_Backgrounds.UpdateThumbnails();
	if(!m_BgDeleteArmed.empty() && ImGui::GetTime() > m_BgDeleteArmedUntil)
		m_BgDeleteArmed.clear();
	// a second click arms -> deletes, so a misclick never removes a file
	const auto ArmOrConfirm = [&](const std::string &Key) {
		if(m_BgDeleteArmed == Key && ImGui::GetTime() < m_BgDeleteArmedUntil)
		{
			m_BgDeleteArmed.clear();
			return true;
		}
		m_BgDeleteArmed = Key;
		m_BgDeleteArmedUntil = (float)ImGui::GetTime() + 3.0f;
		return false;
	};

	BeginCardGrid("##background_top", 2);
	if(BeginCard("##bg_kind", Tr("Menu background"), Tr("What is behind the menu")))
	{
		const char *apModes[] = {Tr("Animated"), Tr("Blur"), Tr("Map"), Tr("Own")};
		static const int s_aValues[] = {1, 0, 2, 3};
		ConfigSegmented("##bg_mode", "xc_menu_background", &g_Config.m_XcMenuBackground, apModes, 4, s_aValues);
		const char *pHint = "";
		switch(g_Config.m_XcMenuBackground)
		{
		case 0: pHint = Tr("In game the game is blurred, in the main menu the animated background is shown"); break;
		case 1: pHint = Tr("Soft glows in the accent color and floating sparks"); break;
		case 2: pHint = Tr("An animated DDNet map, pick one below"); break;
		default: pHint = Tr("Your picture or gif, pick one below"); break;
		}
		Description(pHint);
		if(g_Config.m_XcMenuBackground == 3)
		{
			if(m_Backgrounds.IsLoading())
				Description(Tr("Loading..."));
			else if(m_Backgrounds.IsFailed() || g_Config.m_XcMenuBgFile[0] == '\0')
				Description(Tr("No picture chosen yet"));
		}
		EndCard();
	}
	if(BeginCard("##bg_look", Tr("Look"), Tr("Darkening, blur and gif speed")))
	{
		ConfigSlider(Tr("Darken own background"), "xc_menu_bg_dim", &g_Config.m_XcMenuBgDim, 0, 90, "%d%%");
		ConfigSlider(Tr("Gif speed"), "xc_menu_bg_gif_speed", &g_Config.m_XcMenuBgGifSpeed, 25, 300, "%d%%");
		ToggleRow(Tr("Blur own background"), "xc_menu_bg_blur", &g_Config.m_XcMenuBgBlur);
		ToggleRow(Tr("Blur the game behind the menu"), "xc_menu_blur", &g_Config.m_XcMenuBlur);
		EndCard();
	}
	EndCardGrid();

	ImGui::Dummy(ImVec2(0.0f, S(12.0f)));
	BeginCardGrid("##background_galleries", 1);
	if(BeginCard("##bg_own", Tr("My backgrounds"), Tr("Pictures and gifs. Drag a file onto the game window, paste its path or open the folder")))
	{
		std::vector<CXcBackgrounds::SEntry> &vEntries = m_Backgrounds.Entries();
		CTileGrid Grid(AvailWidth(), S(220.0f), S(14.0f));
		for(const CXcBackgrounds::SEntry &Entry : vEntries)
		{
			Grid.Next();
			const std::string Key = "f:" + Entry.m_Name;
			const bool Selected = g_Config.m_XcMenuBackground == 3 && str_comp(g_Config.m_XcMenuBgFile, Entry.m_Name.c_str()) == 0;
			char aName[128];
			fs_split_file_extension(Entry.m_Name.c_str(), aName, sizeof(aName));
			bool Delete;
			const char *pBadge = Entry.m_Broken ? Tr("broken") : (Entry.m_Animated ? "GIF" : nullptr);
			if(BackgroundTile(Key.c_str(), Entry.m_Thumbnail, Entry.m_Aspect, aName, pBadge, Selected, true, &Delete, Grid.Width()))
			{
				SetConfigString("xc_menu_bg_file", Entry.m_Name.c_str());
				SetConfigInt("xc_menu_background", 3);
			}
			if(Delete && ArmOrConfirm(Key))
				m_BgPendingRemove = Key;
		}
		if(vEntries.empty())
			Description(Tr("No backgrounds yet: add a png, jpg or gif"));

		ImGui::Dummy(ImVec2(0.0f, S(8.0f)));
		// add by path, or open the folder in the file manager
		const float ButtonWidth = S(130.0f);
		const float FolderWidth = S(150.0f);
		const float Gap = S(8.0f);
		InputText("##bg_path", m_aBgPath, sizeof(m_aBgPath), Tr("Path to a picture, gif or map"), std::max(AvailWidth() - ButtonWidth - FolderWidth - Gap * 2.0f, S(100.0f)));
		const bool Enter = ImGui::IsItemDeactivated() && (ImGui::IsKeyPressed(ImGuiKey_Enter) || ImGui::IsKeyPressed(ImGuiKey_KeypadEnter));
		ImGui::SameLine(0.0f, Gap);
		if((Button(Tr("Add"), true, ButtonWidth) || Enter) && m_aBgPath[0])
			m_BgPendingAdd = m_aBgPath;
		ImGui::SameLine(0.0f, Gap);
		if(Button(Tr("Open folder"), false, FolderWidth))
		{
			char aFolder[IO_MAX_PATH_LENGTH];
			Storage()->GetCompletePath(IStorage::TYPE_SAVE, CXcBackgrounds::FOLDER, aFolder, sizeof(aFolder));
			Client()->ViewFile(aFolder);
			// files copied there by hand show up when the page is opened again
			m_BackgroundsScanned = false;
		}
		EndCard();
	}

	if(BeginCard("##bg_maps", Tr("Maps"), Tr("Animated DDNet maps behind the main menu. Add your own .map files the same way")))
	{
		std::vector<CTheme> &vThemes = GameClient()->m_MenuBackground.GetThemes();
		CTileGrid Grid(AvailWidth(), S(220.0f), S(14.0f));
		for(const CTheme &Theme : vThemes)
		{
			if(Theme.m_Name.empty())
				continue; // "none" is the same as the animated background here
			Grid.Next();
			const std::string Key = "m:" + Theme.m_Name;
			const bool Selected = g_Config.m_XcMenuBackground == 2 && str_comp(g_Config.m_ClMenuMap, Theme.m_Name.c_str()) == 0;
			const char *pName = Theme.m_Name == "auto" ? Tr("By season") : (Theme.m_Name == "rand" ? Tr("Random") : Theme.m_Name.c_str());
			// only maps the player added can be deleted, the ones shipped with the game stay
			char aPath[IO_MAX_PATH_LENGTH];
			str_format(aPath, sizeof(aPath), "themes/%s.map", Theme.m_Name.c_str());
			const bool Deletable = Storage()->FileExists(aPath, IStorage::TYPE_SAVE);
			bool Delete;
			// theme icons are 128x64 with a transparent 16 px margin left and right
			if(BackgroundTile(Key.c_str(), Theme.m_IconTexture, 96.0f / 64.0f, pName, nullptr, Selected, Deletable, &Delete, Grid.Width(), 0.125f, 0.875f))
			{
				SetConfigString("cl_menu_map", Theme.m_Name.c_str());
				SetConfigInt("xc_menu_background", 2);
				GameClient()->m_MenuBackground.LoadMenuBackground(Theme.m_HasDay, Theme.m_HasNight);
			}
			if(Delete && ArmOrConfirm(Key))
				m_BgPendingRemove = Key;
		}
		EndCard();
	}
	EndCardGrid();
}

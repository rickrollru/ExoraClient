// Cosmetics editor: own hats from pictures and gifs, moved, sized and turned right on a big tee.
#include "feedback.h"
#include "icons.h"
#include "lang.h"
#include "menu.h"
#include "theme.h"
#include "widgets.h"

#include <base/fs.h>
#include <base/math.h>
#include <base/mem.h>
#include <base/str.h>

#include <engine/client.h>
#include <engine/external/imgui/imgui.h>
#include <engine/external/imgui/imgui_internal.h>
#include <engine/shared/config.h>
#include <engine/storage.h>

#include <game/client/gameclient.h>

#include <cmath>

using namespace XcUi;

void CXcMenu::HandleHatFile(const char *pPath)
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
	switch(GameClient()->m_XcHats.Images().AddFile(aPath, aAdded, sizeof(aAdded)))
	{
	case CXcBackgrounds::ADD_IMAGE:
		GameClient()->m_XcHats.SelectHat(aAdded);
		SetConfigInt("xc_hat", 1);
		Toast(Tr("Hat added, now put it on the tee"));
		m_aHatPath[0] = '\0';
		break;
	case CXcBackgrounds::ADD_UNSUPPORTED:
		Toast(Tr("Only png, jpg, gif and bmp files"), TOAST_ERROR);
		break;
	default:
		Toast(Tr("Could not read this file"), TOAST_ERROR);
		break;
	}
}

static ImVec2 RotateVec(ImVec2 v, float Angle)
{
	const float c = std::cos(Angle), s = std::sin(Angle);
	return ImVec2(v.x * c - v.y * s, v.x * s + v.y * c);
}

// Big tee with the hat: drag the hat to move it, the corner to resize, the circle above to turn it,
// the mouse wheel scales. Double click resets the rotation.
void CXcMenu::RenderHatEditor(float Size)
{
	const STheme &t = Theme();
	ImDrawList *pDraw = ImGui::GetWindowDrawList();
	const ImVec2 Min = ImGui::GetCursorScreenPos();
	const ImVec2 Max(Min.x + Size, Min.y + Size);
	const float Rounding = S(14.0f);
	ImGui::InvisibleButton("##hat_stage", ImVec2(Size, Size), ImGuiButtonFlags_MouseButtonLeft);
	const bool StageHovered = ImGui::IsItemHovered();
	const bool StageActive = ImGui::IsItemActive();

	// stage: soft accent light behind the tee and a quiet grid to line things up
	pDraw->AddRectFilled(Min, Max, Col(t.m_Track), Rounding);
	const ImVec2 Mid((Min.x + Max.x) * 0.5f, (Min.y + Max.y) * 0.5f);
	pDraw->AddCircleFilled(Mid, Size * 0.36f, Col(t.m_Accent, 0.08f), 64);
	const int Lines = 8;
	for(int i = 1; i < Lines; i++)
	{
		const float a = (float)i / Lines;
		const float Alpha = i == Lines / 2 ? 0.10f : 0.04f;
		pDraw->AddLine(ImVec2(Min.x + Size * a, Min.y + S(6.0f)), ImVec2(Min.x + Size * a, Max.y - S(6.0f)), Col(t.m_Text, Alpha));
		pDraw->AddLine(ImVec2(Min.x + S(6.0f), Min.y + Size * a), ImVec2(Max.x - S(6.0f), Min.y + Size * a), Col(t.m_Text, Alpha));
	}

	// the tee is as tall as in DrawTeePreview: 62% of the rect, here the rect is a bit wider than the stage
	// so the hat has room above the head
	const float TeeRect = Size * 0.62f;
	const ImVec2 TeeMin(Mid.x - TeeRect * 0.5f, Mid.y - TeeRect * 0.5f + Size * 0.08f);
	AddTeePreview(pDraw, TeeMin, ImVec2(TeeMin.x + TeeRect, TeeMin.y + TeeRect), true);
	const float TeeSize = TeeRect * 0.62f;
	const float Scale = TeeSize / 64.0f;
	const ImVec2 TeeCenter(TeeMin.x + TeeRect * 0.5f, TeeMin.y + TeeRect * 0.5f);

	CXcHats &Hats = GameClient()->m_XcHats;
	const bool HasHat = g_Config.m_XcHat && Hats.HasHat();
	if(!HasHat)
	{
		const char *pText = !g_Config.m_XcHat ? Tr("Turn the hat on above") : (g_Config.m_XcHatFile[0] ? Tr("Loading...") : Tr("Pick a hat below or add your own"));
		ImFont *pFont = Fonts().m_pLabel;
		const ImVec2 TextSize = pFont->CalcTextSizeA(pFont->FontSize, FLT_MAX, 0.0f, pText);
		pDraw->AddText(pFont, pFont->FontSize, ImVec2(Mid.x - TextSize.x * 0.5f, Min.y + S(18.0f)), Col(t.m_TextMuted), pText);
		m_HatDrag = 0;
		m_WheelCaptured = false;
		ImGui::Dummy(ImVec2(0.0f, S(2.0f)));
		return;
	}

	// hat frame on the stage
	vec2 HatCenter, HatSize;
	Hats.HatRect(vec2(TeeCenter.x, TeeCenter.y), TeeSize, false, &HatCenter, &HatSize);
	const ImVec2 Center(HatCenter.x, HatCenter.y);
	const ImVec2 Half(HatSize.x * 0.5f, HatSize.y * 0.5f);
	const float Angle = g_Config.m_XcHatRotation * pi / 180.0f;
	ImVec2 aCorners[4];
	const ImVec2 aLocal[4] = {ImVec2(-Half.x, -Half.y), ImVec2(Half.x, -Half.y), ImVec2(Half.x, Half.y), ImVec2(-Half.x, Half.y)};
	for(int i = 0; i < 4; i++)
	{
		const ImVec2 r = RotateVec(aLocal[i], Angle);
		aCorners[i] = ImVec2(Center.x + r.x, Center.y + r.y);
	}
	const ImVec2 Up = RotateVec(ImVec2(0.0f, -1.0f), Angle);
	const float HandleRadius = S(7.0f);
	const ImVec2 RotateHandle(Center.x + Up.x * (Half.y + S(24.0f)), Center.y + Up.y * (Half.y + S(24.0f)));
	const ImVec2 ResizeHandle = aCorners[2];

	const ImVec2 Mouse = ImGui::GetIO().MousePos;
	const auto Near = [&](ImVec2 p, float r) { return (Mouse.x - p.x) * (Mouse.x - p.x) + (Mouse.y - p.y) * (Mouse.y - p.y) <= r * r; };
	const ImVec2 LocalMouse = RotateVec(ImVec2(Mouse.x - Center.x, Mouse.y - Center.y), -Angle);
	const bool OverHat = std::abs(LocalMouse.x) <= Half.x + S(4.0f) && std::abs(LocalMouse.y) <= Half.y + S(4.0f);
	const bool OverRotate = Near(RotateHandle, HandleRadius * 1.8f);
	const bool OverResize = Near(ResizeHandle, HandleRadius * 1.8f);

	// start a drag
	if(StageHovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
	{
		m_HatDrag = OverRotate ? 3 : (OverResize ? 2 : (OverHat ? 1 : 0));
		m_aHatDragStart[0] = (float)g_Config.m_XcHatX;
		m_aHatDragStart[1] = (float)g_Config.m_XcHatY;
		m_aHatDragStart[2] = (float)g_Config.m_XcHatSize;
		m_aHatDragStart[3] = (float)g_Config.m_XcHatRotation;
		m_aHatDragMouse[0] = Mouse.x;
		m_aHatDragMouse[1] = Mouse.y;
		if(m_HatDrag)
			PlayUiSound(SOUND_CLICK);
	}
	if(!StageActive)
		m_HatDrag = 0;
	if(StageHovered && OverHat && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
		g_Config.m_XcHatRotation = 0;

	// the values are written straight into the config while dragging, they are saved on exit as usual
	if(m_HatDrag == 1)
	{
		// Shift keeps it on the middle line
		g_Config.m_XcHatX = std::clamp((int)std::round(m_aHatDragStart[0] + (Mouse.x - m_aHatDragMouse[0]) / Scale), -96, 96);
		g_Config.m_XcHatY = std::clamp((int)std::round(m_aHatDragStart[1] + (Mouse.y - m_aHatDragMouse[1]) / Scale), -128, 96);
		if(ImGui::GetIO().KeyShift)
			g_Config.m_XcHatX = 0;
	}
	else if(m_HatDrag == 2)
	{
		const float StartDist = std::max(length(vec2(m_aHatDragMouse[0] - Center.x, m_aHatDragMouse[1] - Center.y)), 1.0f);
		const float Dist = length(vec2(Mouse.x - Center.x, Mouse.y - Center.y));
		g_Config.m_XcHatSize = std::clamp((int)std::round(m_aHatDragStart[2] * Dist / StartDist), 8, 256);
	}
	else if(m_HatDrag == 3)
	{
		float Degrees = std::atan2(Mouse.x - Center.x, -(Mouse.y - Center.y)) * 180.0f / pi;
		// Shift snaps to 15 degrees, without it the straight angle still pulls a little
		if(ImGui::GetIO().KeyShift)
			Degrees = std::round(Degrees / 15.0f) * 15.0f;
		else if(std::abs(Degrees) < 4.0f)
			Degrees = 0.0f;
		g_Config.m_XcHatRotation = std::clamp((int)std::round(Degrees), -180, 180);
	}
	// the wheel scales while the mouse is over the stage
	m_WheelCaptured = StageHovered;
	if(StageHovered && ImGui::GetIO().MouseWheel != 0.0f)
		g_Config.m_XcHatSize = std::clamp((int)std::round(g_Config.m_XcHatSize * std::pow(1.08f, ImGui::GetIO().MouseWheel)), 8, 256);

	// frame and handles show on hover and while editing
	const float Show = Animate(ImGui::GetID("##hat_frame"), StageHovered || m_HatDrag ? 1.0f : 0.35f, 12.0f);
	const ImU32 Frame = Col(t.m_Accent, 0.9f * Show);
	pDraw->AddPolyline(aCorners, 4, Frame, ImDrawFlags_Closed, S(1.5f));
	const ImVec2 TopMid((aCorners[0].x + aCorners[1].x) * 0.5f, (aCorners[0].y + aCorners[1].y) * 0.5f);
	pDraw->AddLine(TopMid, RotateHandle, Frame, S(1.5f));
	const auto Handle = [&](const char *pId, ImVec2 p, bool Hot, bool Active) {
		const float Grow = Animate(ImGui::GetID(pId), Hot || Active ? 1.0f : 0.0f, 18.0f);
		pDraw->AddCircleFilled(p, HandleRadius + Grow * S(2.0f), Col(ImVec4(1, 1, 1, 1), Show), 24);
		pDraw->AddCircleFilled(p, HandleRadius - S(2.0f) + Grow * S(1.5f), Col(t.m_Accent, Show), 24);
	};
	Handle("##h_resize", ResizeHandle, OverResize, m_HatDrag == 2);
	Handle("##h_rotate", RotateHandle, OverRotate, m_HatDrag == 3);
	if(StageHovered && !m_HatDrag)
	{
		if(OverRotate || OverResize)
			ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
		else if(OverHat)
			ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeAll);
	}
	// values in the corner while editing
	if(m_HatDrag)
	{
		char aInfo[64];
		if(m_HatDrag == 3)
			str_format(aInfo, sizeof(aInfo), "%d°", g_Config.m_XcHatRotation);
		else if(m_HatDrag == 2)
			str_format(aInfo, sizeof(aInfo), "%d", g_Config.m_XcHatSize);
		else
			str_format(aInfo, sizeof(aInfo), "%d, %d", g_Config.m_XcHatX, g_Config.m_XcHatY);
		ImFont *pFont = Fonts().m_pSmall;
		const ImVec2 TextSize = pFont->CalcTextSizeA(pFont->FontSize, FLT_MAX, 0.0f, aInfo);
		const ImVec2 BoxMin(Min.x + S(10.0f), Max.y - TextSize.y - S(20.0f));
		pDraw->AddRectFilled(BoxMin, ImVec2(BoxMin.x + TextSize.x + S(16.0f), Max.y - S(10.0f)), Col(ImVec4(0, 0, 0, 0.55f)), S(6.0f));
		pDraw->AddText(pFont, pFont->FontSize, ImVec2(BoxMin.x + S(8.0f), BoxMin.y + S(5.0f)), IM_COL32_WHITE, aInfo);
	}
}

void CXcMenu::RenderVisualsCosmetics()
{
	CXcBackgrounds &Images = GameClient()->m_XcHats.Images();
	if(!m_HatsScanned)
	{
		Images.Refresh();
		m_HatsScanned = true;
	}
	if(!m_HatPendingAdd.empty())
	{
		HandleHatFile(m_HatPendingAdd.c_str());
		m_HatPendingAdd.clear();
	}
	if(!m_HatPendingRemove.empty())
	{
		if(Images.Remove(m_HatPendingRemove.c_str()))
		{
			if(str_comp(g_Config.m_XcHatFile, m_HatPendingRemove.c_str()) == 0)
				SetConfigString("xc_hat_file", "");
			Toast(Tr("Deleted"));
		}
		else
			Toast(Tr("Could not delete the file"), TOAST_ERROR);
		m_HatPendingRemove.clear();
	}
	Images.UpdateThumbnails();
	if(!m_BgDeleteArmed.empty() && ImGui::GetTime() > m_BgDeleteArmedUntil)
		m_BgDeleteArmed.clear();

	BeginCardGrid("##cosmetics_editor", 1);
	if(BeginCard("##hat_editor", Tr("Own hat"), Tr("Any picture or gif on your tee. Drag it on the tee, pull the corner to resize, the circle to turn it"), &g_Config.m_XcHat))
	{
		const float StageSize = std::min(S(380.0f), AvailWidth() * 0.5f);
		ImGui::BeginGroup();
		RenderHatEditor(StageSize);
		Description(Tr("Mouse wheel: size. Shift: snap. Double click: straight"), StageSize);
		ImGui::EndGroup();
		ImGui::SameLine(0.0f, S(20.0f));
		ImGui::BeginGroup();
		ConfigSlider(Tr("Left / right"), "xc_hat_x", &g_Config.m_XcHatX, -96, 96);
		ConfigSlider(Tr("Up / down"), "xc_hat_y", &g_Config.m_XcHatY, -128, 96);
		ConfigSlider(Tr("Size"), "xc_hat_size", &g_Config.m_XcHatSize, 8, 256);
		ConfigSlider(Tr("Rotation"), "xc_hat_rotation", &g_Config.m_XcHatRotation, -180, 180, "%d°");
		ConfigSlider(Tr("Opacity"), "xc_hat_alpha", &g_Config.m_XcHatAlpha, 10, 100, "%d%%");
		ConfigSlider(Tr("Sway"), "xc_hat_sway", &g_Config.m_XcHatSway, 0, 200, "%d%%");
		if(Images.Entries().size() && str_endswith_nocase(g_Config.m_XcHatFile, ".gif"))
			ConfigSlider(Tr("Gif speed"), "xc_hat_gif_speed", &g_Config.m_XcHatGifSpeed, 25, 300, "%d%%");
		ToggleRow(Tr("Mirror when looking left"), "xc_hat_flip", &g_Config.m_XcHatFlip);
		TargetChips("##hat_targets", &g_Config.m_XcHatSelf, &g_Config.m_XcHatDummy, &g_Config.m_XcHatOthers);
		ImGui::Dummy(ImVec2(0.0f, S(4.0f)));
		if(Button(Tr("Reset position"), false, AvailWidth()))
			GameClient()->m_XcHats.ResetPlacement();
		Description(Tr("Only you see the hat, other players see your normal skin"));
		ImGui::EndGroup();
		EndCard();
	}

	if(BeginCard("##hat_gallery", Tr("My hats"), Tr("Pictures and gifs with a transparent background work best. Drag a file onto the game window, paste its path or open the folder")))
	{
		std::vector<CXcBackgrounds::SEntry> &vEntries = Images.Entries();
		CTileGrid Grid(AvailWidth(), S(130.0f), S(12.0f));
		for(const CXcBackgrounds::SEntry &Entry : vEntries)
		{
			Grid.Next();
			const std::string Key = "h:" + Entry.m_Name;
			const bool Selected = g_Config.m_XcHat && str_comp(g_Config.m_XcHatFile, Entry.m_Name.c_str()) == 0;
			char aName[128];
			fs_split_file_extension(Entry.m_Name.c_str(), aName, sizeof(aName));
			bool Delete;
			const char *pBadge = Entry.m_Broken ? Tr("broken") : (Entry.m_Animated ? "GIF" : nullptr);
			if(BackgroundTile(Key.c_str(), Entry.m_Thumbnail, Entry.m_Aspect, aName, pBadge, Selected, !Entry.m_Builtin, &Delete, Grid.Width(), 0.0f, 1.0f, 1.0f, true))
			{
				GameClient()->m_XcHats.SelectHat(Entry.m_Name.c_str());
				SetConfigInt("xc_hat", 1);
			}
			if(Delete)
			{
				if(m_BgDeleteArmed == Key && ImGui::GetTime() < m_BgDeleteArmedUntil)
				{
					m_BgDeleteArmed.clear();
					m_HatPendingRemove = Entry.m_Name;
				}
				else
				{
					m_BgDeleteArmed = Key;
					m_BgDeleteArmedUntil = (float)ImGui::GetTime() + 3.0f;
				}
			}
		}
		if(vEntries.empty())
			Description(Tr("No hats yet: add a png or gif"));

		ImGui::Dummy(ImVec2(0.0f, S(8.0f)));
		const float ButtonWidth = S(130.0f);
		const float FolderWidth = S(150.0f);
		const float Gap = S(8.0f);
		InputText("##hat_path", m_aHatPath, sizeof(m_aHatPath), Tr("Path to a picture or gif"), std::max(AvailWidth() - ButtonWidth - FolderWidth - Gap * 2.0f, S(100.0f)));
		const bool Enter = ImGui::IsItemDeactivated() && (ImGui::IsKeyPressed(ImGuiKey_Enter) || ImGui::IsKeyPressed(ImGuiKey_KeypadEnter));
		ImGui::SameLine(0.0f, Gap);
		if((Button(Tr("Add"), true, ButtonWidth) || Enter) && m_aHatPath[0])
			m_HatPendingAdd = m_aHatPath;
		ImGui::SameLine(0.0f, Gap);
		if(Button(Tr("Open folder"), false, FolderWidth))
		{
			char aFolder[IO_MAX_PATH_LENGTH];
			Storage()->GetCompletePath(IStorage::TYPE_SAVE, Images.Folder(), aFolder, sizeof(aFolder));
			Client()->ViewFile(aFolder);
			m_HatsScanned = false;
		}
		EndCard();
	}
	EndCardGrid();
}

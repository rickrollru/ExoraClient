// The picture card of the new editor: a big preview and color correction like in Photoshop (brightness,
// contrast, saturation, hue). The sliders change a small preview; "Apply" changes the pixels of the embedded
// picture and loads it again, "Undo" brings back the pixels from before the last apply.
#include "xeditor.h"

#include <base/math.h>
#include <base/str.h>

#include <engine/graphics.h>

#include <game/client/components/xclient/ui/feedback.h>
#include <game/client/components/xclient/ui/lang.h>
#include <game/client/components/xclient/ui/theme.h>
#include <game/client/components/xclient/ui/widgets.h>
#include <game/editor/editor.h>
#include <game/editor/editor_actions.h>
#include <game/editor/mapitems/image.h>
#include <game/editor/mapitems/layer_group.h>
#include <game/editor/mapitems/layer_quads.h>

#include <engine/external/imgui/imgui.h>

#include <algorithm>
#include <cmath>

using namespace XcUi;

// one RGBA pixel through hue, saturation, contrast and brightness (the values in -1..1, the hue in degrees)
static void AdjustPixel(uint8_t *pPixel, float Brightness, float Contrast, float Saturation, float Hue)
{
	float r = pPixel[0] / 255.0f, g = pPixel[1] / 255.0f, b = pPixel[2] / 255.0f;
	if(Hue != 0.0f)
	{
		float h, s, v;
		ImGui::ColorConvertRGBtoHSV(r, g, b, h, s, v);
		h = std::fmod(h + Hue / 360.0f + 1.0f, 1.0f);
		ImGui::ColorConvertHSVtoRGB(h, s, v, r, g, b);
	}
	if(Saturation != 0.0f)
	{
		const float Luma = 0.299f * r + 0.587f * g + 0.114f * b;
		const float k = 1.0f + Saturation;
		r = Luma + (r - Luma) * k;
		g = Luma + (g - Luma) * k;
		b = Luma + (b - Luma) * k;
	}
	if(Contrast != 0.0f)
	{
		const float k = Contrast >= 0.0f ? 1.0f + Contrast * 2.0f : 1.0f + Contrast;
		r = (r - 0.5f) * k + 0.5f;
		g = (g - 0.5f) * k + 0.5f;
		b = (b - 0.5f) * k + 0.5f;
	}
	r += Brightness * 0.5f;
	g += Brightness * 0.5f;
	b += Brightness * 0.5f;
	pPixel[0] = (uint8_t)std::clamp(round_to_int(r * 255.0f), 0, 255);
	pPixel[1] = (uint8_t)std::clamp(round_to_int(g * 255.0f), 0, 255);
	pPixel[2] = (uint8_t)std::clamp(round_to_int(b * 255.0f), 0, 255);
}

void CXcEditorUi::AdjustPixels(uint8_t *pData, size_t NumPixels) const
{
	const float Brightness = m_aAdjust[0] / 100.0f, Contrast = m_aAdjust[1] / 100.0f, Saturation = m_aAdjust[2] / 100.0f, Hue = (float)m_aAdjust[3];
	for(size_t i = 0; i < NumPixels; i++)
		AdjustPixel(pData + i * 4, Brightness, Contrast, Saturation, Hue);
}

void CXcEditorUi::UpdateImagePreview(const CEditorImage *pImage)
{
	IGraphics *pGraphics = m_pEditor->Graphics();
	if(m_ImagePreview.IsValid())
		pGraphics->UnloadTexture(&m_ImagePreview);
	if(!pImage || pImage->m_Format != CImageInfo::FORMAT_RGBA || !pImage->m_pData)
		return;
	// a small copy (at most 192 pixels) with the correction on it
	const int Step = std::max(1, (int)std::ceil(std::max(pImage->m_Width, pImage->m_Height) / 192.0f));
	CImageInfo Small;
	Small.m_Width = std::max<size_t>(1, pImage->m_Width / Step);
	Small.m_Height = std::max<size_t>(1, pImage->m_Height / Step);
	Small.m_Format = CImageInfo::FORMAT_RGBA;
	Small.Allocate();
	for(size_t y = 0; y < Small.m_Height; y++)
		for(size_t x = 0; x < Small.m_Width; x++)
			mem_copy(Small.m_pData + (y * Small.m_Width + x) * 4, pImage->m_pData + ((y * Step) * pImage->m_Width + x * Step) * 4, 4);
	AdjustPixels(Small.m_pData, Small.m_Width * Small.m_Height);
	m_ImagePreview = pGraphics->LoadTextureRawMove(Small, 0, "xc_image_preview");
}

void CXcEditorUi::RenderImageCard()
{
	CEditor &E = *m_pEditor;
	CEditorMap *pMap = E.Map();
	const STheme &t = Theme();
	if(pMap->m_SelectedImage < 0 || pMap->m_SelectedImage >= (int)pMap->m_vpImages.size())
	{
		Description(Tr("Click a picture on the left to see it here. Right click it to replace, embed, export or delete it."));
		return;
	}
	CEditorImage *pImage = pMap->m_vpImages[pMap->m_SelectedImage].get();
	if(pImage != m_pAdjustImage)
	{
		// another picture: the sliders start at zero and the undo belongs to the old picture
		m_pAdjustImage = pImage;
		std::fill(std::begin(m_aAdjust), std::end(m_aAdjust), 0);
		m_vAdjustBackup.clear();
		UpdateImagePreview(pImage);
	}

	const float Width = ImGui::GetContentRegionAvail().x;
	ImDrawList *pDraw = ImGui::GetWindowDrawList();
	Title(pImage->m_aName[0] ? pImage->m_aName : Tr("Picture"));
	char aInfo[128];
	str_format(aInfo, sizeof(aInfo), "%d × %d · %s", (int)pImage->m_Width, (int)pImage->m_Height, pImage->m_External ? Tr("from the game files") : Tr("embedded in the map"));
	Description(aInfo);
	ImGui::Dummy(ImVec2(0.0f, S(4.0f)));

	// the preview on a checkerboard so transparency shows
	const float Box = Width;
	const ImVec2 Min = ImGui::GetCursorScreenPos();
	const ImVec2 Max(Min.x + Box, Min.y + Box * 0.75f);
	pDraw->AddRectFilled(Min, Max, Col(t.m_Track), S(10.0f));
	const float Check = S(10.0f);
	for(float y = Min.y; y < Max.y; y += Check)
		for(float x = Min.x + (((int)((y - Min.y) / Check)) % 2) * Check; x < Max.x; x += Check * 2.0f)
			pDraw->AddRectFilled(ImVec2(x, y), ImVec2(std::min(x + Check, Max.x), std::min(y + Check, Max.y)), Col(t.m_Card, 0.6f));
	const IGraphics::CTextureHandle Texture = m_ImagePreview.IsValid() ? m_ImagePreview : pImage->m_Texture;
	if(Texture.IsValid() && pImage->m_Width > 0 && pImage->m_Height > 0)
	{
		const float Aspect = (float)pImage->m_Width / pImage->m_Height;
		const float AreaW = Box - S(16.0f), AreaH = Box * 0.75f - S(16.0f);
		const float w = std::min(AreaW, AreaH * Aspect), h = w / Aspect;
		const ImVec2 PicMin(Min.x + (Box - w) * 0.5f, Min.y + (Box * 0.75f - h) * 0.5f);
		pDraw->AddImage((ImTextureID)(uintptr_t)m_Gui.TextureId(Texture), PicMin, ImVec2(PicMin.x + w, PicMin.y + h));
	}
	ImGui::Dummy(ImVec2(Box, Box * 0.75f));
	ImGui::Dummy(ImVec2(0.0f, S(6.0f)));

	// the color correction
	if(pImage->m_External)
	{
		Description(Tr("This picture comes from the game files. Embed it (right click on the left) to change its colors."));
		return;
	}
	if(pImage->m_Format != CImageInfo::FORMAT_RGBA || !pImage->m_pData)
		return;
	ParamLabel(Tr("Color correction"));
	bool Changed = false;
	Changed |= SliderInt(Tr("Brightness"), &m_aAdjust[0], -100, 100);
	Changed |= SliderInt(Tr("Contrast"), &m_aAdjust[1], -100, 100);
	Changed |= SliderInt(Tr("Saturation"), &m_aAdjust[2], -100, 100);
	Changed |= SliderInt(Tr("Hue"), &m_aAdjust[3], -180, 180, "%d°");
	if(Changed)
		UpdateImagePreview(pImage);
	ImGui::Dummy(ImVec2(0.0f, S(4.0f)));
	const bool Any = m_aAdjust[0] || m_aAdjust[1] || m_aAdjust[2] || m_aAdjust[3];
	const float ButtonWidth = (Width - S(8.0f)) * 0.5f;
	ImGui::BeginDisabled(!Any);
	if(Button(Tr("Apply"), true, ButtonWidth))
	{
		// keep the pixels for one undo, change the picture and load it again
		const size_t Size = pImage->m_Width * pImage->m_Height * 4;
		m_vAdjustBackup.assign(pImage->m_pData, pImage->m_pData + Size);
		AdjustPixels(pImage->m_pData, pImage->m_Width * pImage->m_Height);
		ReloadImageTexture(pImage);
		std::fill(std::begin(m_aAdjust), std::end(m_aAdjust), 0);
		UpdateImagePreview(pImage);
		pMap->OnModify();
		Toast(Tr("The colors of the picture are changed"), TOAST_SUCCESS);
	}
	ImGui::EndDisabled();
	ImGui::SameLine(0.0f, S(8.0f));
	ImGui::BeginDisabled(m_vAdjustBackup.size() != pImage->m_Width * pImage->m_Height * 4);
	if(Button(Tr("Undo"), false, ButtonWidth))
	{
		mem_copy(pImage->m_pData, m_vAdjustBackup.data(), m_vAdjustBackup.size());
		m_vAdjustBackup.clear();
		ReloadImageTexture(pImage);
		UpdateImagePreview(pImage);
		pMap->OnModify();
	}
	ImGui::EndDisabled();
}

void CXcEditorUi::ReloadImageTexture(CEditorImage *pImage)
{
	IGraphics *pGraphics = m_pEditor->Graphics();
	pGraphics->UnloadTexture(&pImage->m_Texture);
	int Flags = pGraphics->TextureLoadFlags();
	if(pImage->m_Width % 16 != 0 || pImage->m_Height % 16 != 0)
		Flags = 0;
	pImage->m_Texture = pGraphics->LoadTextureRaw(*pImage, Flags, pImage->m_aName);
}

namespace
{
struct SMapTheme
{
	const char *m_pName;
	unsigned m_Top;
	unsigned m_Bottom;
};
const SMapTheme s_aThemes[] = {
	{"Sunset", 0x2B1055, 0xFF7E5F},
	{"Day sky", 0x4FACFE, 0xC9ECFF},
	{"Night", 0x0F2027, 0x2C5364},
	{"Neon", 0x12002B, 0x5B2BFF},
	{"Forest", 0x134E5E, 0x71B280},
	{"Desert", 0xF7B267, 0xF4845F},
	{"Ice", 0xE0F7FF, 0x8EC5FC},
	{"Lava", 0x1A0000, 0xD3321D},
};
ImVec4 ThemeColor(unsigned Rgb) { return ImVec4(((Rgb >> 16) & 255) / 255.0f, ((Rgb >> 8) & 255) / 255.0f, (Rgb & 255) / 255.0f, 1.0f); }
} // namespace

void CXcEditorUi::AddThemeBackground(int Theme)
{
	CEditor &E = *m_pEditor;
	CEditorMap *pMap = E.Map();
	// a group that does not move with the camera, behind everything, with one gradient quad over the screen
	std::shared_ptr<CLayerGroup> pGroup = pMap->NewGroup();
	str_copy(pGroup->m_aName, "Background", sizeof(pGroup->m_aName));
	pGroup->m_ParallaxX = 0;
	pGroup->m_ParallaxY = 0;
	std::shared_ptr<CLayerQuads> pLayer = std::make_shared<CLayerQuads>(pMap);
	str_copy(pLayer->m_aName, Tr(s_aThemes[Theme].m_pName), sizeof(pLayer->m_aName));
	pGroup->AddLayer(pLayer);
	CQuad *pQuad = pLayer->NewQuad(0, 0, 1600, 1200);
	const auto Color = [](unsigned Rgb) { return CColor((Rgb >> 16) & 255, (Rgb >> 8) & 255, Rgb & 255, 255); };
	pQuad->m_aColors[0] = pQuad->m_aColors[1] = Color(s_aThemes[Theme].m_Top);
	pQuad->m_aColors[2] = pQuad->m_aColors[3] = Color(s_aThemes[Theme].m_Bottom);
	const int Index = pMap->MoveGroup((int)pMap->m_vpGroups.size() - 1, 0);
	pMap->m_EditorHistory.RecordAction(std::make_shared<CEditorActionGroup>(pMap, Index, false));
	pMap->SelectLayer(0, Index);
	pMap->OnModify();
	Toast(Tr("The background is added as the first group"), TOAST_SUCCESS);
}

void CXcEditorUi::RenderThemes()
{
	const STheme &t = Theme();
	if(m_ThemesRequest)
	{
		ImGui::OpenPopup("##xe_themes");
		m_ThemesRequest = false;
	}
	const ImVec2 Display = ImGui::GetIO().DisplaySize;
	const float Width = std::min(S(620.0f), Display.x - S(40.0f));
	ImGui::SetNextWindowPos(ImVec2(Display.x * 0.5f, Display.y * 0.45f), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
	ImGui::SetNextWindowSize(ImVec2(Width, 0.0f));
	ImGui::PushStyleColor(ImGuiCol_PopupBg, ImVec4(t.m_Card.x, t.m_Card.y, t.m_Card.z, 1.0f));
	ImGui::PushStyleColor(ImGuiCol_ModalWindowDimBg, ImVec4(0.0f, 0.0f, 0.0f, 0.45f));
	ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, S(16.0f));
	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(S(20.0f), S(18.0f)));
	if(ImGui::BeginPopupModal("##xe_themes", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_AlwaysAutoResize))
	{
		Header(Tr("Background theme"));
		Description(Tr("Adds a sky gradient behind the whole map. It is a normal group: change its colors in the quad properties."));
		ImGui::Dummy(ImVec2(0.0f, S(8.0f)));
		const int Columns = 4;
		const float Gap = S(10.0f);
		const float Tile = (Width - S(40.0f) - Gap * (Columns - 1)) / Columns;
		ImDrawList *pDraw = ImGui::GetWindowDrawList();
		bool Close = false;
		for(int i = 0; i < (int)std::size(s_aThemes); i++)
		{
			if(i % Columns)
				ImGui::SameLine(0.0f, Gap);
			ImGui::PushID(i);
			const ImVec2 Min = ImGui::GetCursorScreenPos();
			const ImVec2 Max(Min.x + Tile, Min.y + Tile * 0.7f);
			if(ImGui::InvisibleButton("##theme", ImVec2(Tile, Tile * 0.7f + S(22.0f))))
			{
				AddThemeBackground(i);
				Close = true;
			}
			const bool Hovered = ImGui::IsItemHovered();
			const ImU32 Top = Col(ThemeColor(s_aThemes[i].m_Top)), Bottom = Col(ThemeColor(s_aThemes[i].m_Bottom));
			pDraw->AddRectFilledMultiColor(Min, Max, Top, Top, Bottom, Bottom);
			pDraw->AddRect(Min, Max, Col(Hovered ? t.m_Accent : t.m_Border), S(8.0f), 0, Hovered ? 2.0f : 1.0f);
			ImFont *pSmall = Fonts().m_pSmall;
			pDraw->AddText(pSmall, pSmall->FontSize, ImVec2(Min.x + S(2.0f), Max.y + S(4.0f)), Col(Hovered ? t.m_Text : t.m_TextSecondary), Tr(s_aThemes[i].m_pName));
			ImGui::PopID();
		}
		ImGui::Dummy(ImVec2(0.0f, S(8.0f)));
		if(Button(Tr("Cancel"), false, Width - S(40.0f)) || ImGui::IsKeyPressed(ImGuiKey_Escape))
			Close = true;
		if(Close)
			ImGui::CloseCurrentPopup();
		ImGui::EndPopup();
	}
	ImGui::PopStyleVar(2);
	ImGui::PopStyleColor(2);
}

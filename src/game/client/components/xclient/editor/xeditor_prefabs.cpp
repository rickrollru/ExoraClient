// Prefabs: pieces of a map saved from the brush and taken back as the brush with one click, like the
// components of Figma. A prefab is a small text file in xclient/prefabs (in the save folder): the tile
// layers of the brush with their tiles and the numbers of the special layers. Loading builds the layers
// again and grabs them as the brush, so the classic brush code does the rest (painting, flipping, undo).
#include "xeditor.h"

#include <base/io.h>
#include <base/str.h>

#include <engine/font_icons.h>
#include <engine/storage.h>

#include <game/client/components/xclient/hud/xc_hud.h>
#include <game/client/components/xclient/ui/feedback.h>
#include <game/client/components/xclient/ui/lang.h>
#include <game/client/components/xclient/ui/theme.h>
#include <game/client/components/xclient/ui/widgets.h>
#include <game/editor/editor.h>
#include <game/editor/mapitems/image.h>
#include <game/editor/mapitems/layer_front.h>
#include <game/editor/mapitems/layer_game.h>
#include <game/editor/mapitems/layer_group.h>
#include <game/editor/mapitems/layer_speedup.h>
#include <game/editor/mapitems/layer_switch.h>
#include <game/editor/mapitems/layer_tele.h>
#include <game/editor/mapitems/layer_tiles.h>
#include <game/editor/mapitems/layer_tune.h>

#include <engine/external/imgui/imgui.h>

#include <algorithm>
#include <cstdlib>
#include <sstream>

using namespace XcUi;

static const char *const PREFAB_FOLDER = "xclient/prefabs";

static const char *LayerKind(const CLayerTiles *pLayer)
{
	if(pLayer->m_HasGame)
		return "game";
	if(pLayer->m_HasFront)
		return "front";
	if(pLayer->m_HasTele)
		return "tele";
	if(pLayer->m_HasSwitch)
		return "switch";
	if(pLayer->m_HasSpeedup)
		return "speedup";
	if(pLayer->m_HasTune)
		return "tune";
	return "tiles";
}

void CXcEditorUi::ScanPrefabs()
{
	m_vPrefabs.clear();
	m_pEditor->Storage()->ListDirectory(
		IStorage::TYPE_SAVE, PREFAB_FOLDER, [](const char *pName, int IsDir, int StorageType, void *pUser) {
			if(IsDir || !str_endswith(pName, ".prefab"))
				return 0;
			CXcEditorUi *pThis = static_cast<CXcEditorUi *>(pUser);
			SPrefab Prefab;
			Prefab.m_Name.assign(pName, str_length(pName) - str_length(".prefab"));
			pThis->m_vPrefabs.push_back(Prefab);
			return 0;
		},
		this);
	std::sort(m_vPrefabs.begin(), m_vPrefabs.end(), [](const SPrefab &a, const SPrefab &b) { return str_comp_nocase(a.m_Name.c_str(), b.m_Name.c_str()) < 0; });
	for(SPrefab &Prefab : m_vPrefabs)
		ReadPrefab(Prefab);
	m_PrefabsScanned = true;
}

bool CXcEditorUi::ReadPrefab(SPrefab &Prefab)
{
	char aPath[IO_MAX_PATH_LENGTH];
	str_format(aPath, sizeof(aPath), "%s/%s.prefab", PREFAB_FOLDER, Prefab.m_Name.c_str());
	char *pText = m_pEditor->Storage()->ReadFileStr(aPath, IStorage::TYPE_SAVE);
	if(!pText)
		return false;
	std::istringstream Stream(pText);
	free(pText);
	std::string Word;
	Stream >> Word;
	if(Word != "XCPREFAB")
		return false;
	int Version = 0;
	Stream >> Version;
	Prefab.m_vLayers.clear();
	while(Stream >> Word)
	{
		if(Word != "layer")
			break;
		SPrefabLayer Layer;
		int r, g, b, a;
		Stream >> Layer.m_Kind >> Layer.m_Width >> Layer.m_Height >> r >> g >> b >> a >> Layer.m_Image;
		if(Layer.m_Width <= 0 || Layer.m_Height <= 0 || Layer.m_Width > 1000 || Layer.m_Height > 1000)
			return false;
		Layer.m_Color = CColor(r, g, b, a);
		const int Count = Layer.m_Width * Layer.m_Height;
		Layer.m_vTiles.resize(Count);
		for(int i = 0; i < Count; i++)
		{
			// index,flags[,a,b,c,d] where the extra numbers belong to the special layers
			std::string Cell;
			Stream >> Cell;
			int aValues[6] = {};
			int n = 0;
			for(const char *p = Cell.c_str(); *p && n < 6;)
			{
				aValues[n++] = std::atoi(p);
				const char *pComma = str_find(p, ",");
				if(!pComma)
					break;
				p = pComma + 1;
			}
			SPrefabTile &Tile = Layer.m_vTiles[i];
			Tile.m_Index = aValues[0];
			Tile.m_Flags = aValues[1];
			for(int k = 0; k < 4; k++)
				Tile.m_aExtra[k] = aValues[2 + k];
		}
		Prefab.m_vLayers.push_back(Layer);
	}
	if(Prefab.m_vLayers.empty())
		return false;

	// the preview: at most 48 blocks on the long side, a block shows its first tile that is not empty
	int w = 1, h = 1;
	for(const auto &L : Prefab.m_vLayers)
	{
		w = std::max(w, L.m_Width);
		h = std::max(h, L.m_Height);
	}
	const int Step = std::max(1, (std::max(w, h) + 47) / 48);
	Prefab.m_PreviewWidth = (w + Step - 1) / Step;
	Prefab.m_PreviewHeight = (h + Step - 1) / Step;
	Prefab.m_vPreview.assign((size_t)Prefab.m_PreviewWidth * Prefab.m_PreviewHeight, 0);
	Prefab.m_vPreviewGame.assign(Prefab.m_vPreview.size(), false);
	for(const auto &L : Prefab.m_vLayers)
	{
		const bool Game = L.m_Kind == "game" || L.m_Kind == "front";
		for(int y = 0; y < L.m_Height; y++)
			for(int x = 0; x < L.m_Width; x++)
			{
				const int Index = L.m_vTiles[(size_t)y * L.m_Width + x].m_Index;
				const size_t Cell = (size_t)(y / Step) * Prefab.m_PreviewWidth + x / Step;
				if(Index != 0 && Prefab.m_vPreview[Cell] == 0)
				{
					Prefab.m_vPreview[Cell] = Index;
					Prefab.m_vPreviewGame[Cell] = Game;
				}
			}
	}
	return true;
}

bool CXcEditorUi::SavePrefab(const char *pName)
{
	CEditor &E = *m_pEditor;
	CEditorMap *pMap = E.Map();
	if(!str_valid_filename(pName))
	{
		Toast(Tr("This name cannot be used for files and folders"), TOAST_ERROR);
		return false;
	}
	std::string Text = "XCPREFAB 1\n";
	int Layers = 0;
	for(const auto &pLayer : E.m_pBrush->m_vpLayers)
	{
		if(pLayer->m_Type != LAYERTYPE_TILES)
			continue;
		const CLayerTiles *pTiles = static_cast<const CLayerTiles *>(pLayer.get());
		const char *pImage = "-";
		if(pTiles->m_Image >= 0 && pTiles->m_Image < (int)pMap->m_vpImages.size() && pMap->m_vpImages[pTiles->m_Image]->m_aName[0])
			pImage = pMap->m_vpImages[pTiles->m_Image]->m_aName;
		char aLine[256];
		str_format(aLine, sizeof(aLine), "layer %s %d %d %d %d %d %d %s\n", LayerKind(pTiles), pTiles->m_Width, pTiles->m_Height,
			pTiles->m_Color.r, pTiles->m_Color.g, pTiles->m_Color.b, pTiles->m_Color.a, pImage);
		Text += aLine;
		for(int y = 0; y < pTiles->m_Height; y++)
		{
			for(int x = 0; x < pTiles->m_Width; x++)
			{
				const int i = y * pTiles->m_Width + x;
				const CTile &Tile = pTiles->m_pTiles[i];
				char aCell[64];
				if(pTiles->m_HasTele)
				{
					const CTeleTile &Tele = static_cast<const CLayerTele *>(pTiles)->m_pTeleTile[i];
					str_format(aCell, sizeof(aCell), "%d,%d,%d,%d ", Tile.m_Index, Tile.m_Flags, Tele.m_Number, Tele.m_Type);
				}
				else if(pTiles->m_HasSwitch)
				{
					const CSwitchTile &Switch = static_cast<const CLayerSwitch *>(pTiles)->m_pSwitchTile[i];
					str_format(aCell, sizeof(aCell), "%d,%d,%d,%d,%d,%d ", Tile.m_Index, Tile.m_Flags, Switch.m_Number, Switch.m_Type, Switch.m_Flags, Switch.m_Delay);
				}
				else if(pTiles->m_HasSpeedup)
				{
					const CSpeedupTile &Speedup = static_cast<const CLayerSpeedup *>(pTiles)->m_pSpeedupTile[i];
					str_format(aCell, sizeof(aCell), "%d,%d,%d,%d,%d,%d ", Tile.m_Index, Tile.m_Flags, Speedup.m_Force, Speedup.m_MaxSpeed, Speedup.m_Type, Speedup.m_Angle);
				}
				else if(pTiles->m_HasTune)
				{
					const CTuneTile &Tune = static_cast<const CLayerTune *>(pTiles)->m_pTuneTile[i];
					str_format(aCell, sizeof(aCell), "%d,%d,%d,%d ", Tile.m_Index, Tile.m_Flags, Tune.m_Number, Tune.m_Type);
				}
				else
					str_format(aCell, sizeof(aCell), "%d,%d ", Tile.m_Index, Tile.m_Flags);
				Text += aCell;
			}
			Text += "\n";
		}
		Layers++;
	}
	if(Layers == 0)
	{
		Toast(Tr("The brush has no tiles: grab some tiles first"), TOAST_ERROR);
		return false;
	}
	E.Storage()->CreateFolder("xclient", IStorage::TYPE_SAVE);
	E.Storage()->CreateFolder(PREFAB_FOLDER, IStorage::TYPE_SAVE);
	char aPath[IO_MAX_PATH_LENGTH];
	str_format(aPath, sizeof(aPath), "%s/%s.prefab", PREFAB_FOLDER, pName);
	IOHANDLE File = E.Storage()->OpenFile(aPath, IOFLAG_WRITE, IStorage::TYPE_SAVE);
	if(!File)
	{
		Toast(Tr("Could not save the prefab"), TOAST_ERROR);
		return false;
	}
	io_write(File, Text.c_str(), Text.size());
	io_close(File);
	Toast(Tr("Prefab saved"), TOAST_SUCCESS);
	ScanPrefabs();
	return true;
}

void CXcEditorUi::UsePrefab(const SPrefab &Prefab)
{
	CEditor &E = *m_pEditor;
	CEditorMap *pMap = E.Map();
	E.m_pBrush->Clear();
	bool MissingImage = false;
	for(const SPrefabLayer &Layer : Prefab.m_vLayers)
	{
		// build the layer again, then grab it all as the brush
		std::shared_ptr<CLayerTiles> pLayer;
		const int w = Layer.m_Width, h = Layer.m_Height;
		if(Layer.m_Kind == "game")
			pLayer = std::make_shared<CLayerGame>(pMap, w, h);
		else if(Layer.m_Kind == "front")
			pLayer = std::make_shared<CLayerFront>(pMap, w, h);
		else if(Layer.m_Kind == "tele")
		{
			auto pTele = std::make_shared<CLayerTele>(pMap, w, h);
			for(int i = 0; i < w * h; i++)
				pTele->m_pTeleTile[i] = {(unsigned char)Layer.m_vTiles[i].m_aExtra[0], (unsigned char)Layer.m_vTiles[i].m_aExtra[1]};
			pLayer = pTele;
		}
		else if(Layer.m_Kind == "switch")
		{
			auto pSwitch = std::make_shared<CLayerSwitch>(pMap, w, h);
			for(int i = 0; i < w * h; i++)
			{
				const int *pE = Layer.m_vTiles[i].m_aExtra;
				pSwitch->m_pSwitchTile[i] = {(unsigned char)pE[0], (unsigned char)pE[1], (unsigned char)pE[2], (unsigned char)pE[3]};
			}
			pLayer = pSwitch;
		}
		else if(Layer.m_Kind == "speedup")
		{
			auto pSpeedup = std::make_shared<CLayerSpeedup>(pMap, w, h);
			for(int i = 0; i < w * h; i++)
			{
				const int *pE = Layer.m_vTiles[i].m_aExtra;
				pSpeedup->m_pSpeedupTile[i] = {(unsigned char)pE[0], (unsigned char)pE[1], (unsigned char)pE[2], 0, (short)pE[3]};
			}
			pLayer = pSpeedup;
		}
		else if(Layer.m_Kind == "tune")
		{
			auto pTune = std::make_shared<CLayerTune>(pMap, w, h);
			for(int i = 0; i < w * h; i++)
				pTune->m_pTuneTile[i] = {(unsigned char)Layer.m_vTiles[i].m_aExtra[0], (unsigned char)Layer.m_vTiles[i].m_aExtra[1]};
			pLayer = pTune;
		}
		else
		{
			pLayer = std::make_shared<CLayerTiles>(pMap, w, h);
			// the picture is found by its name in this map
			if(Layer.m_Image != "-")
			{
				for(size_t i = 0; i < pMap->m_vpImages.size(); i++)
					if(Layer.m_Image == pMap->m_vpImages[i]->m_aName)
						pLayer->m_Image = i;
				MissingImage |= pLayer->m_Image < 0;
			}
		}
		pLayer->m_Color = Layer.m_Color;
		for(int i = 0; i < w * h; i++)
		{
			pLayer->m_pTiles[i].m_Index = Layer.m_vTiles[i].m_Index;
			pLayer->m_pTiles[i].m_Flags = Layer.m_vTiles[i].m_Flags;
		}
		const CUIRect All = {1.0f, 1.0f, w * 32.0f - 2.0f, h * 32.0f - 2.0f};
		pLayer->BrushGrab(E.m_pBrush.get(), All);
	}
	E.m_XcTool = CEditor::XC_TOOL_BRUSH;
	if(MissingImage)
		Toast(Tr("The picture of the prefab is not in this map: add it in Images with the same name"), TOAST_INFO);
}

// the prefab drawn as small squares: game tiles in their colors, other tiles in a color of their index
static void DrawPrefabPreview(ImDrawList *pDraw, const CXcEditorUi::SPrefab &Prefab, ImVec2 Min, ImVec2 Max)
{
	const int w = std::max(Prefab.m_PreviewWidth, 1), h = std::max(Prefab.m_PreviewHeight, 1);
	const float Cell = std::min((Max.x - Min.x) / w, (Max.y - Min.y) / h);
	const ImVec2 Origin(Min.x + ((Max.x - Min.x) - Cell * w) * 0.5f, Min.y + ((Max.y - Min.y) - Cell * h) * 0.5f);
	const float Gap = Cell > 4.0f ? 1.0f : 0.0f;
	for(int y = 0; y < Prefab.m_PreviewHeight; y++)
	{
		for(int x = 0; x < Prefab.m_PreviewWidth; x++)
		{
			const size_t i = (size_t)y * Prefab.m_PreviewWidth + x;
			const int Index = Prefab.m_vPreview[i];
			if(Index == 0)
				continue;
			ImVec4 Color;
			if(Prefab.m_vPreviewGame[i])
			{
				// the usual entity colors: solid, unhookable, freeze, death, start and finish
				if(Index == 1)
					Color = ImVec4(0.72f, 0.66f, 0.55f, 1.0f);
				else if(Index == 3)
					Color = ImVec4(0.35f, 0.35f, 0.4f, 1.0f);
				else if(Index == 9 || Index == 12)
					Color = ImVec4(0.45f, 0.65f, 1.0f, 0.9f);
				else if(Index == 2)
					Color = ImVec4(0.95f, 0.35f, 0.35f, 1.0f);
				else if(Index == 33 || Index == 34)
					Color = ImVec4(0.4f, 0.9f, 0.5f, 1.0f);
				else
					Color = ImVec4(0.85f, 0.8f, 0.4f, 1.0f);
			}
			else
			{
				const float Hue = (Index * 37 % 256) / 256.0f;
				ImGui::ColorConvertHSVtoRGB(Hue, 0.35f, 0.85f, Color.x, Color.y, Color.z);
				Color.w = 1.0f;
			}
			const ImVec2 P(Origin.x + x * Cell, Origin.y + y * Cell);
			pDraw->AddRectFilled(P, ImVec2(P.x + Cell - Gap, P.y + Cell - Gap), Col(Color));
		}
	}
}

void CXcEditorUi::RenderPrefabsPanel()
{
	CEditor &E = *m_pEditor;
	const STheme &t = Theme();
	if(!m_PrefabsScanned)
		ScanPrefabs();

	// save the brush
	const bool CanSave = !E.m_pBrush->IsEmpty();
	Description(CanSave ? Tr("Save the brush as a prefab to use it again in any map") : Tr("Grab tiles with the brush (or the magic wand), then save them here"));
	const float Width = ImGui::GetContentRegionAvail().x;
	ImGui::BeginDisabled(!CanSave);
	InputText("##prefabname", m_aPrefabName, sizeof(m_aPrefabName), Tr("Prefab name"), Width);
	ImGui::Dummy(ImVec2(0.0f, S(2.0f)));
	if(Button(Tr("Save the brush"), true, Width) && m_aPrefabName[0])
	{
		if(SavePrefab(m_aPrefabName))
			m_aPrefabName[0] = '\0';
	}
	ImGui::EndDisabled();
	ImGui::Dummy(ImVec2(0.0f, S(6.0f)));

	// the library
	ImGui::BeginChild("##prefabs", ImVec2(Width, ImGui::GetContentRegionAvail().y), ImGuiChildFlags_None, ImGuiWindowFlags_NoBackground);
	if(m_vPrefabs.empty())
		Description(Tr("No prefabs yet"));
	ImDrawList *pDraw = ImGui::GetWindowDrawList();
	const float Gap = S(8.0f);
	const float Tile = (ImGui::GetContentRegionAvail().x - Gap) * 0.5f;
	int Delete = -1;
	for(int i = 0; i < (int)m_vPrefabs.size(); i++)
	{
		const SPrefab &Prefab = m_vPrefabs[i];
		if(i % 2 == 1)
			ImGui::SameLine(0.0f, Gap);
		ImGui::PushID(i);
		const ImVec2 Min = ImGui::GetCursorScreenPos();
		const ImVec2 Max(Min.x + Tile, Min.y + Tile + S(22.0f));
		ImGui::InvisibleButton("##prefab", ImVec2(Tile, Tile + S(22.0f)));
		const bool Hovered = ImGui::IsItemHovered();
		if(ImGui::IsItemClicked(ImGuiMouseButton_Left))
			UsePrefab(Prefab);
		if(ImGui::IsItemClicked(ImGuiMouseButton_Right))
			ImGui::OpenPopup("##prefabmenu");
		pDraw->AddRectFilled(Min, Max, Col(Hovered ? t.m_Card : t.m_Track, Hovered ? 1.0f : 0.6f), S(10.0f));
		DrawPrefabPreview(pDraw, Prefab, ImVec2(Min.x + S(10.0f), Min.y + S(10.0f)), ImVec2(Max.x - S(10.0f), Min.y + Tile - S(4.0f)));
		ImFont *pSmall = Fonts().m_pSmall;
		char aName[64];
		XcFitText(pSmall, pSmall->FontSize, Prefab.m_Name.c_str(), Tile - S(16.0f), aName, sizeof(aName));
		pDraw->AddText(pSmall, pSmall->FontSize, ImVec2(Min.x + S(8.0f), Max.y - pSmall->FontSize - S(6.0f)), Col(t.m_TextSecondary), aName);
		if(Hovered)
			ImGui::SetTooltip("%s", Tr("Click: take it as the brush. Right click: delete"));
		if(ImGui::BeginPopup("##prefabmenu"))
		{
			if(ImGui::MenuItem(Tr("Take as the brush")))
				UsePrefab(Prefab);
			if(ImGui::MenuItem(Tr("Delete")))
				Delete = i;
			ImGui::EndPopup();
		}
		ImGui::PopID();
	}
	ImGui::EndChild();
	if(Delete >= 0)
	{
		char aPath[IO_MAX_PATH_LENGTH];
		str_format(aPath, sizeof(aPath), "%s/%s.prefab", PREFAB_FOLDER, m_vPrefabs[Delete].m_Name.c_str());
		E.Storage()->RemoveFile(aPath, IStorage::TYPE_SAVE);
		Toast(Tr("Deleted"), TOAST_SUCCESS);
		ScanPrefabs();
	}
}

// "Make 3D": real depth for every player, in a normal map. Copies of a tile layer go into groups with a parallax a
// bit below 100% and an offset made for their piece of the map: when the camera looks at the piece the copies lie
// exactly under the layer, when it moves they slide like things standing deeper. Darker copies one behind the other
// look like the sides of the blocks. Only standard map items: works in every client, on every server.
#include "editor_style.h"
#include "xeditor.h"

#include <base/math.h>
#include <base/str.h>

#include <game/client/components/xclient/ui/feedback.h>
#include <game/client/components/xclient/ui/lang.h>
#include <game/client/components/xclient/ui/theme.h>
#include <game/client/components/xclient/ui/widgets.h>
#include <game/editor/editor.h>
#include <game/editor/mapitems/layer_group.h>
#include <game/editor/mapitems/layer_tiles.h>

#include <engine/external/imgui/imgui.h>

#include <algorithm>
#include <cmath>
#include <vector>

using namespace XcUi;

namespace
{
	// the groups made by the tool are called this, their layer keeps the name of the layer it copies
	const char *const DEPTH_GROUP_NAME = "3D";
	const int s_aPieceSizes[] = {4, 8, 16};

	bool IsDepthGroup(const CLayerGroup &Group)
	{
		return str_comp(Group.m_aName, DEPTH_GROUP_NAME) == 0 && Group.m_ParallaxX < 100 && Group.m_vpLayers.size() == 1 && Group.m_vpLayers[0]->m_Type == LAYERTYPE_TILES;
	}

	bool IsCopyOf(const CLayerGroup &Group, const CLayerTiles &Source)
	{
		if(!IsDepthGroup(Group))
			return false;
		const CLayerTiles *pCopy = static_cast<const CLayerTiles *>(Group.m_vpLayers[0].get());
		return pCopy->m_Image == Source.m_Image && str_comp(pCopy->m_aName, Source.m_aName) == 0;
	}

	// the tiles worth copying: only near an empty cell. Deep inside the rock the copies would stay hidden behind
	// the layer, the camera never moves them farther than Reach tiles
	std::vector<bool> VisibleTiles(const CLayerTiles &Layer, int Reach)
	{
		const int W = Layer.m_Width, H = Layer.m_Height;
		std::vector<bool> vKeep((size_t)W * H, false);
		for(int y = 0; y < H; y++)
		{
			for(int x = 0; x < W; x++)
			{
				if(Layer.m_pTiles[y * W + x].m_Index == 0)
					continue;
				bool NearAir = false;
				for(int dy = -Reach; dy <= Reach && !NearAir; dy++)
					for(int dx = -Reach; dx <= Reach && !NearAir; dx++)
					{
						const int nx = x + dx, ny = y + dy;
						NearAir = nx >= 0 && ny >= 0 && nx < W && ny < H && Layer.m_pTiles[ny * W + nx].m_Index == 0;
					}
				vKeep[(size_t)y * W + x] = NearAir;
			}
		}
		return vKeep;
	}

	// how far (tiles) the deepest copy can slide on a big screen
	int ReachOf(int Levels, int Step)
	{
		return std::clamp((int)std::ceil(Levels * Step / 100.0f * 1100.0f / 32.0f) + 1, 1, 8);
	}

	// the pieces of the layer that have tiles worth copying
	int CountPieces(const CLayerTiles &Layer, int Piece, const std::vector<bool> &vKeep)
	{
		int Count = 0;
		for(int py = 0; py < Layer.m_Height; py += Piece)
			for(int px = 0; px < Layer.m_Width; px += Piece)
			{
				bool Any = false;
				for(int y = py; y < std::min(py + Piece, Layer.m_Height) && !Any; y++)
					for(int x = px; x < std::min(px + Piece, Layer.m_Width) && !Any; x++)
						Any = vKeep[(size_t)y * Layer.m_Width + x];
				Count += Any;
			}
		return Count;
	}
} // namespace

void CXcEditorUi::OpenDepthDialog()
{
	m_DepthRequest = true;
}

// takes away the copies of this layer; returns how many groups
static int RemoveDepth(CEditorMap *pMap, const CLayerTiles &Source)
{
	int Removed = 0;
	for(int g = (int)pMap->m_vpGroups.size() - 1; g >= 0; g--)
	{
		if(IsCopyOf(*pMap->m_vpGroups[g], Source))
		{
			pMap->m_vpGroups.erase(pMap->m_vpGroups.begin() + g);
			if(pMap->m_SelectedGroup > g)
				pMap->m_SelectedGroup--;
			Removed++;
		}
	}
	return Removed;
}

void CXcEditorUi::MakeDepth(int Levels, int Step, int Dark, int Piece)
{
	CEditor &E = *m_pEditor;
	CEditorMap *pMap = E.Map();
	std::shared_ptr<CLayerTiles> pSource = std::static_pointer_cast<CLayerTiles>(pMap->SelectedLayerType(0, LAYERTYPE_TILES));
	if(!pSource || pSource->IsEntitiesLayer() || pSource->m_Image < 0)
	{
		ShowMessage(Tr("Select a tile layer with a picture (not a game layer)."), Tr("Make 3D"));
		return;
	}
	int SourceGroup = pMap->m_SelectedGroup;
	std::shared_ptr<CLayerGroup> pGroup = pMap->m_vpGroups[SourceGroup];
	if(pGroup->m_ParallaxX != 100 || pGroup->m_ParallaxY != 100)
	{
		ShowMessage(Tr("The layer must move with the game: its group needs parallax 100."), Tr("Make 3D"));
		return;
	}
	// made again: the old copies go first
	RemoveDepth(pMap, *pSource);
	SourceGroup = (int)(std::find(pMap->m_vpGroups.begin(), pMap->m_vpGroups.end(), pGroup) - pMap->m_vpGroups.begin());

	// the layers drawn before this one in its group go into their own group in front of the copies, so the copies
	// are behind the layer but not behind its background. A group with a physics layer there stays as it is.
	const int LayerIndex = (int)(std::find(pGroup->m_vpLayers.begin(), pGroup->m_vpLayers.end(), pSource) - pGroup->m_vpLayers.begin());
	bool CanSplit = LayerIndex > 0 && LayerIndex < (int)pGroup->m_vpLayers.size();
	for(int i = 0; i < LayerIndex && CanSplit; i++)
		if(pGroup->m_vpLayers[i]->IsEntitiesLayer())
			CanSplit = false;
	int InsertAt = SourceGroup;
	if(CanSplit)
	{
		std::shared_ptr<CLayerGroup> pBefore = std::make_shared<CLayerGroup>(pMap);
		str_copy(pBefore->m_aName, pGroup->m_aName);
		pBefore->m_OffsetX = pGroup->m_OffsetX;
		pBefore->m_OffsetY = pGroup->m_OffsetY;
		pBefore->m_ParallaxX = pGroup->m_ParallaxX;
		pBefore->m_ParallaxY = pGroup->m_ParallaxY;
		pBefore->m_UseClipping = pGroup->m_UseClipping;
		pBefore->m_ClipX = pGroup->m_ClipX;
		pBefore->m_ClipY = pGroup->m_ClipY;
		pBefore->m_ClipW = pGroup->m_ClipW;
		pBefore->m_ClipH = pGroup->m_ClipH;
		pBefore->m_vpLayers.assign(pGroup->m_vpLayers.begin(), pGroup->m_vpLayers.begin() + LayerIndex);
		pGroup->m_vpLayers.erase(pGroup->m_vpLayers.begin(), pGroup->m_vpLayers.begin() + LayerIndex);
		pMap->m_vpGroups.insert(pMap->m_vpGroups.begin() + SourceGroup, pBefore);
		InsertAt = SourceGroup + 1;
	}

	// deepest copies first: they are drawn first, so the nearer ones cover them
	std::vector<std::shared_ptr<CLayerGroup>> vpNew;
	const CLayerTiles &Src = *pSource;
	const std::vector<bool> vKeep = VisibleTiles(Src, ReachOf(Levels, Step));
	for(int Level = Levels; Level >= 1; Level--)
	{
		const int Parallax = 100 - Level * Step;
		const float Keep = 1.0f - Parallax / 100.0f;
		const float Shade = std::clamp(1.0f - Level * Dark / 100.0f, 0.05f, 1.0f);
		for(int py = 0; py < Src.m_Height; py += Piece)
		{
			for(int px = 0; px < Src.m_Width; px += Piece)
			{
				const int w = std::min(Piece, Src.m_Width - px), h = std::min(Piece, Src.m_Height - py);
				bool Any = false;
				for(int y = 0; y < h && !Any; y++)
					for(int x = 0; x < w && !Any; x++)
						Any = vKeep[(size_t)(py + y) * Src.m_Width + px + x];
				if(!Any)
					continue;
				// an empty tile around the piece: the game stretches the edge tiles of a layer to the screen edges
				std::shared_ptr<CLayerTiles> pCopy = std::make_shared<CLayerTiles>(pMap, w + 2, h + 2);
				str_copy(pCopy->m_aName, Src.m_aName);
				pCopy->m_Image = Src.m_Image;
				pCopy->m_Color = CColor((int)(Src.m_Color.r * Shade), (int)(Src.m_Color.g * Shade), (int)(Src.m_Color.b * Shade), Src.m_Color.a);
				pCopy->m_ColorEnv = Src.m_ColorEnv;
				pCopy->m_ColorEnvOffset = Src.m_ColorEnvOffset;
				for(int y = 0; y < h; y++)
					for(int x = 0; x < w; x++)
						if(vKeep[(size_t)(py + y) * Src.m_Width + px + x])
							pCopy->m_pTiles[(y + 1) * (w + 2) + x + 1] = Src.m_pTiles[(py + y) * Src.m_Width + px + x];
				// the copy lies under the layer when the camera looks at the middle of the piece
				std::shared_ptr<CLayerGroup> pCopyGroup = std::make_shared<CLayerGroup>(pMap);
				str_copy(pCopyGroup->m_aName, DEPTH_GROUP_NAME);
				pCopyGroup->m_ParallaxX = Parallax;
				pCopyGroup->m_ParallaxY = Parallax;
				const float CenterX = (px + w * 0.5f) * 32.0f - pGroup->m_OffsetX;
				const float CenterY = (py + h * 0.5f) * 32.0f - pGroup->m_OffsetY;
				pCopyGroup->m_OffsetX = (int)std::round(pGroup->m_OffsetX - (px - 1) * 32.0f + CenterX * Keep);
				pCopyGroup->m_OffsetY = (int)std::round(pGroup->m_OffsetY - (py - 1) * 32.0f + CenterY * Keep);
				pCopyGroup->AddLayer(pCopy);
				vpNew.push_back(pCopyGroup);
			}
		}
	}
	pMap->m_vpGroups.insert(pMap->m_vpGroups.begin() + InsertAt, vpNew.begin(), vpNew.end());
	pMap->m_SelectedGroup = (int)(std::find(pMap->m_vpGroups.begin(), pMap->m_vpGroups.end(), pGroup) - pMap->m_vpGroups.begin());
	pMap->m_vSelectedLayers = {(int)(std::find(pGroup->m_vpLayers.begin(), pGroup->m_vpLayers.end(), pSource) - pGroup->m_vpLayers.begin())};
	// the undo steps point at group numbers that just moved
	pMap->m_EditorHistory.Clear();
	pMap->OnModify();
	char aMsg[128];
	str_format(aMsg, sizeof(aMsg), Tr("The layer is 3D: %d groups added"), (int)vpNew.size());
	Toast(aMsg, TOAST_SUCCESS);
}

void CXcEditorUi::RenderDepthDialog()
{
	CEditor &E = *m_pEditor;
	CEditorMap *pMap = E.Map();
	const STheme &t = Theme();
	if(m_DepthRequest)
	{
		ImGui::OpenPopup("##xe_depth");
		m_DepthRequest = false;
	}
	const ImVec2 Display = ImGui::GetIO().DisplaySize;
	const float Width = std::min(S(560.0f), Display.x - S(40.0f));
	ImGui::SetNextWindowPos(ImVec2(Display.x * 0.5f, Display.y * 0.45f), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
	ImGui::SetNextWindowSize(ImVec2(Width, 0.0f));
	ImGui::PushStyleColor(ImGuiCol_PopupBg, ImVec4(t.m_Card.x, t.m_Card.y, t.m_Card.z, 1.0f));
	ImGui::PushStyleColor(ImGuiCol_ModalWindowDimBg, ImVec4(0.0f, 0.0f, 0.0f, 0.45f));
	ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, S(16.0f));
	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(S(20.0f), S(18.0f)));
	const bool Open = ImGui::BeginPopupModal("##xe_depth", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_AlwaysAutoResize);
	ImGui::PopStyleVar(2);
	ImGui::PopStyleColor(2);
	if(!Open)
		return;

	bool Close = false;
	Header(Tr("Make 3D"));
	Description(Tr("The blocks of the layer get depth that every player sees, in any client and on any server: darker copies of the layer slide behind it like the sides of the blocks. Only the picture, the physics stay the same."));
	ImGui::Dummy(ImVec2(0.0f, S(8.0f)));

	std::shared_ptr<CLayerTiles> pSource = std::static_pointer_cast<CLayerTiles>(pMap->SelectedLayerType(0, LAYERTYPE_TILES));
	const bool Valid = pSource && !pSource->IsEntitiesLayer() && pSource->m_Image >= 0;
	if(!Valid)
		Description(Tr("Select a tile layer with a picture (not a game layer) first."));
	else
	{
		char aLayer[128];
		str_format(aLayer, sizeof(aLayer), "%s: %s", Tr("Layer"), pSource->m_aName[0] ? pSource->m_aName : Tr("Tiles"));
		Title(aLayer);
		SliderInt(Tr("Depth (copies)"), &m_DepthLevels, 2, 12);
		SliderInt(Tr("How far each copy goes back"), &m_DepthStep, 1, 4, "%d%%");
		SliderInt(Tr("Darker with depth"), &m_DepthDark, 0, 30, "%d%%");
		ParamLabel(Tr("Piece size"));
		const char *apPieces[] = {"4×4", "8×8", "16×16"};
		Segmented("##depth_piece", &m_DepthPiece, apPieces, 3);
		Description(Tr("Smaller pieces: cleaner depth, more groups. Bigger pieces: fewer groups, small seams far from the camera."));
		const int Piece = s_aPieceSizes[std::clamp(m_DepthPiece, 0, 2)];
		const int Groups = CountPieces(*pSource, Piece, VisibleTiles(*pSource, ReachOf(m_DepthLevels, m_DepthStep))) * m_DepthLevels;
		char aCount[128];
		str_format(aCount, sizeof(aCount), Tr("Adds %d groups to the map"), Groups);
		ImGui::Dummy(ImVec2(0.0f, S(4.0f)));
		ImGui::PushStyleColor(ImGuiCol_Text, Groups > 800 ? ImVec4(1.0f, 0.54f, 0.24f, 1.0f) : t.m_TextSecondary);
		ImGui::TextUnformatted(aCount);
		ImGui::PopStyleColor();
		if(Groups > 800)
			Description(Tr("Many groups can cost FPS for every player. Use it on a smaller layer (the start, the finish, a nice spot) or bigger pieces."));
		Description(Tr("The undo history is cleared. \"Remove 3D\" takes the copies away again."));
	}
	ImGui::Dummy(ImVec2(0.0f, S(8.0f)));
	const float ButtonW = (Width - S(40.0f) - S(16.0f)) / 3.0f;
	if(Button(Tr("Cancel"), false, ButtonW) || ImGui::IsKeyPressed(ImGuiKey_Escape))
		Close = true;
	ImGui::SameLine(0.0f, S(8.0f));
	ImGui::BeginDisabled(!Valid);
	if(Button(Tr("Remove 3D"), false, ButtonW) && Valid)
	{
		const int Removed = RemoveDepth(pMap, *pSource);
		if(Removed)
		{
			pMap->m_EditorHistory.Clear();
			pMap->OnModify();
		}
		char aMsg[128];
		str_format(aMsg, sizeof(aMsg), Tr("%d groups removed"), Removed);
		Toast(aMsg, TOAST_SUCCESS);
		Close = true;
	}
	ImGui::SameLine(0.0f, S(8.0f));
	if(Button(Tr("Make 3D"), true, ButtonW) && Valid)
	{
		MakeDepth(m_DepthLevels, m_DepthStep, m_DepthDark, s_aPieceSizes[std::clamp(m_DepthPiece, 0, 2)]);
		Close = true;
	}
	ImGui::EndDisabled();
	if(Close)
		ImGui::CloseCurrentPopup();
	ImGui::EndPopup();
}

#include "xeditor.h"

#include "automap_rules.h"

#include <base/math.h>
#include <base/str.h>
#include <engine/keys.h>
#include <engine/storage.h>
#include <game/client/components/xclient/ui/lang.h>
#include <game/client/components/xclient/ui/theme.h>
#include <game/client/components/xclient/ui/widgets.h>
#include <game/editor/editor.h>
#include <game/editor/editor_actions.h>
#include <game/editor/mapitems/image.h>

#include <algorithm>
#include <array>
#include <cstdlib>
#include <numeric>

using namespace XcUi;

namespace
{
float Turn(vec2 A, vec2 B, vec2 C)
{
	return (B.x - A.x) * (C.y - A.y) - (B.y - A.y) * (C.x - A.x);
}

bool SegmentsIntersect(vec2 A, vec2 B, vec2 C, vec2 D)
{
	if(std::max(A.x, B.x) < std::min(C.x, D.x) || std::max(C.x, D.x) < std::min(A.x, B.x) ||
		std::max(A.y, B.y) < std::min(C.y, D.y) || std::max(C.y, D.y) < std::min(A.y, B.y))
		return false;

	const float ABC = Turn(A, B, C), ABD = Turn(A, B, D);
	const float CDA = Turn(C, D, A), CDB = Turn(C, D, B);
	return ABC * ABD <= 0.0f && CDA * CDB <= 0.0f;
}

std::vector<std::array<int, 3>> Triangulate(const std::vector<vec2> &vPoints)
{
	std::vector<int> vRing(vPoints.size());
	std::iota(vRing.begin(), vRing.end(), 0);
	// Extra clicks on a straight edge do not change the shape.
	for(size_t i = 0; vRing.size() > 3 && i < vRing.size();)
	{
		const vec2 A = vPoints[vRing[(i + vRing.size() - 1) % vRing.size()]];
		const vec2 B = vPoints[vRing[i]], C = vPoints[vRing[(i + 1) % vRing.size()]];
		if(std::abs(Turn(A, B, C)) <= 0.001f && dot(B - A, B - C) <= 0.0f)
		{
			vRing.erase(vRing.begin() + i);
			i = 0;
		}
		else
			++i;
	}
	const int Count = vRing.size();
	if(Count < 3)
		return {};
	float Area = 0.0f;
	for(int i = 0; i < Count; ++i)
	{
		const int Next = (i + 1) % Count;
		Area += Turn(vPoints[vRing[0]], vPoints[vRing[i]], vPoints[vRing[Next]]);
		for(int j = i + 2; j < Count; ++j)
		{
			if(i == 0 && j == Count - 1)
				continue;
			if(SegmentsIntersect(vPoints[vRing[i]], vPoints[vRing[Next]], vPoints[vRing[j]], vPoints[vRing[(j + 1) % Count]]))
				return {};
		}
	}
	if(std::abs(Area) < 0.001f)
		return {};

	if(Area < 0.0f)
		std::reverse(vRing.begin(), vRing.end());
	std::vector<std::array<int, 3>> vTriangles;
	while(vRing.size() > 2)
	{
		bool Found = false;
		for(size_t i = 0; i < vRing.size(); ++i)
		{
			const int A = vRing[(i + vRing.size() - 1) % vRing.size()];
			const int B = vRing[i], C = vRing[(i + 1) % vRing.size()];
			if(Turn(vPoints[A], vPoints[B], vPoints[C]) <= 0.001f)
				continue;
			const bool Occupied = std::any_of(vRing.begin(), vRing.end(), [&](int P) {
				return P != A && P != B && P != C &&
					Turn(vPoints[A], vPoints[B], vPoints[P]) >= 0.0f &&
					Turn(vPoints[B], vPoints[C], vPoints[P]) >= 0.0f &&
					Turn(vPoints[C], vPoints[A], vPoints[P]) >= 0.0f;
			});
			if(Occupied)
				continue;
			vTriangles.push_back({A, B, C});
			vRing.erase(vRing.begin() + i);
			Found = true;
			break;
		}
		if(!Found)
			return {};
	}
	return vTriangles;
}
}

void CXcEditorUi::OpenArtImport(bool Quad)
{
	CEditor &E = *m_pEditor;
	E.m_FileBrowser.ShowFileDialog(IStorage::TYPE_ALL, CFileBrowser::EFileType::IMAGE,
		Quad ? "Add quad art" : "Add tile art", "Open", "mapres", "",
		Quad ? CEditor::CallbackAddQuadArt : CEditor::CallbackAddTileArt, &E);
}

void CXcEditorUi::ToggleGoresBrush()
{
	CEditor &E = *m_pEditor;
	E.m_XcGoresBrush = !E.m_XcGoresBrush;
	if(E.m_XcGoresBrush)
	{
		if(m_QuadPen)
			ToggleQuadPen();
		E.m_XcTool = CEditor::XC_TOOL_BRUSH;
	}
}

void CXcEditorUi::ToggleQuadPen()
{
	m_QuadPen = !m_QuadPen;
	m_vPenPoints.clear();
	m_pPenLayer.reset();
}

bool CXcEditorUi::DoQuadPen(vec2 WorldPos)
{
	if(!m_QuadPen)
		return false;
	CEditor &E = *m_pEditor;
	CEditorMap *pMap = E.Map();
	if(E.Input()->ModifierIsPressed() || E.m_Dialog != DIALOG_NONE || m_WantsMouse || m_PaletteOpen || DialogOpen())
		return true;
	const auto pLayer = std::static_pointer_cast<CLayerQuads>(pMap->SelectedLayerType(0, LAYERTYPE_QUADS));
	if(m_pPenLayer.lock() != pLayer)
	{
		m_vPenPoints.clear();
		m_pPenLayer = pLayer;
	}
	if(!pLayer || pLayer->m_Readonly)
	{
		str_copy(E.m_aTooltip, Tr("Select an editable quad layer"));
		return true;
	}
	str_copy(E.m_aTooltip, Tr("Left click adds points, right click fills, Escape cancels"));

	const bool AddPoint = E.Input()->KeyPress(KEY_MOUSE_1) || E.Ui()->MouseButtonClicked(0);
	const float PointRadius = 6.0f * E.MapView()->MouseWorldScale();
	const bool CloseContour = AddPoint && m_vPenPoints.size() >= 3 && distance(m_vPenPoints.front(), WorldPos) <= PointRadius;
	if(AddPoint && !CloseContour && m_vPenPoints.size() < 128 &&
		std::none_of(m_vPenPoints.begin(), m_vPenPoints.end(), [&](vec2 P) { return distance(P, WorldPos) < 0.01f; }))
		m_vPenPoints.push_back(WorldPos);

	const bool Fill = CloseContour || E.Input()->KeyPress(KEY_MOUSE_2) || E.Ui()->MouseButtonClicked(1);
	if(Fill && !m_vPenPoints.empty())
	{
		if(m_vPenPoints.size() < 3)
		{
			ShowMessage(Tr("Add at least three points with the left mouse button."), Tr("Quad Pen"));
			return true;
		}
		const auto vTriangles = Triangulate(m_vPenPoints);
		if(vTriangles.empty())
		{
			ShowMessage(Tr("The outline crosses itself or has no area."), Tr("Quad Pen"));
			return true;
		}
		const auto pGroup = pMap->SelectedGroup();
		const int Layer = std::find(pGroup->m_vpLayers.begin(), pGroup->m_vpLayers.end(), pLayer) - pGroup->m_vpLayers.begin();
		pMap->m_EditorHistory.BeginBulk();
		for(const auto &Triangle : vTriangles)
		{
			const vec2 A = m_vPenPoints[Triangle[0]], B = m_vPenPoints[Triangle[1]], C = m_vPenPoints[Triangle[2]];
			const vec2 Center = (A + B + C) / 3.0f;
			CQuad *pQuad = pLayer->NewQuad(0, 0, 0, 0);
			const vec2 aPoints[] = {A, B, C, C, Center};
			for(int i = 0; i < 5; ++i)
				pQuad->m_aPoints[i] = {f2fx(aPoints[i].x), f2fx(aPoints[i].y)};
			pMap->m_EditorHistory.RecordAction(std::make_shared<CEditorActionNewQuad>(pMap, pMap->m_SelectedGroup, Layer));
		}
		pMap->m_EditorHistory.EndBulk("Quad Pen");
		m_vPenPoints.clear();
	}

	if(!m_vPenPoints.empty())
	{
		const CScreenRect Saved = E.Graphics()->GetScreen();
		pMap->SelectedGroup()->MapScreen();
		E.Graphics()->TextureClear();
		E.Graphics()->LinesBegin();
		E.Graphics()->SetColor(0.4f, 0.8f, 1.0f, 1.0f);
		for(size_t i = 0; i < m_vPenPoints.size(); ++i)
		{
			const vec2 End = i + 1 < m_vPenPoints.size() ? m_vPenPoints[i + 1] : WorldPos;
			const IGraphics::CLineItem Line(m_vPenPoints[i], End);
			E.Graphics()->LinesDraw(&Line, 1);
		}
		E.Graphics()->LinesEnd();
		E.Graphics()->QuadsBegin();
		E.Graphics()->SetColor(0.4f, 0.8f, 1.0f, 1.0f);
		for(const vec2 Point : m_vPenPoints)
		{
			IGraphics::CQuadItem Marker(Point.x, Point.y, PointRadius, PointRadius);
			E.Graphics()->QuadsDraw(&Marker, 1);
		}
		E.Graphics()->QuadsEnd();
		E.Graphics()->MapScreen(Saved);
	}
	return true;
}

namespace
{
constexpr int s_aDesignTiles[] = {TILE_SOLID, TILE_NOHOOK, TILE_FREEZE};
const char *const s_apDesignMaterials[] = {"Hookable", "Unhookable", "Freeze"};
const char *const s_apDesignPresets[] = {"Meadow", "Stone", "Night"};
const ImVec4 s_aaDesignColors[][3] = {
	{{0.66f, 0.91f, 0.56f, 1}, {0.72f, 0.79f, 0.88f, 1}, {0.51f, 0.81f, 1, 0.65f}},
	{{0.89f, 0.87f, 0.82f, 1}, {0.64f, 0.71f, 0.82f, 1}, {0.69f, 0.85f, 0.97f, 0.65f}},
	{{0.63f, 0.58f, 0.89f, 1}, {0.54f, 0.62f, 0.79f, 1}, {0.47f, 0.63f, 1, 0.55f}},
};

bool HasDesignRules(const std::shared_ptr<CEditorImage> &pImage)
{
	return pImage->m_Width % 16 == 0 && pImage->m_Height % 16 == 0 && pImage->m_Automapper.ConfigNamesNum() > 0;
}
}

void CXcEditorUi::BuildDesignPreview(int Material)
{
	auto &Layer = m_aDesignLayers[Material];
	Layer.m_pPreview.reset();
	CEditor &E = *m_pEditor;
	CEditorMap *pMap = E.Map();
	const auto &pGame = pMap->m_pGameLayer;
	if(!Layer.m_Enabled || !pGame || Layer.m_Image < 0 || Layer.m_Image >= (int)pMap->m_vpImages.size())
		return;

	char aPath[IO_MAX_PATH_LENGTH];
	str_format(aPath, sizeof(aPath), "editor/automap/%s.rules", pMap->m_vpImages[Layer.m_Image]->m_aName);
	char *pText = E.Storage()->ReadFileStr(aPath, IStorage::TYPE_ALL);
	if(!pText)
		return;
	std::vector<XcAutomap::SConfig> vRules;
	XcAutomap::Parse(pText, vRules);
	free(pText);
	if(Layer.m_Rule < 0 || Layer.m_Rule >= (int)vRules.size())
		return;

	const int Width = pGame->m_Width, Height = pGame->m_Height;
	std::vector<int> vIndices((size_t)Width * Height), vFlags, vWriters;
	bool HasMaterial = false;
	for(int y = 0; y < Height; ++y)
		for(int x = 0; x < Width; ++x)
		{
			const bool Match = pGame->GetTile(x, y).m_Index == s_aDesignTiles[Material];
			HasMaterial |= Match;
			vIndices[(size_t)y * Width + x] = Match ? 1 : 0;
		}
	if(!HasMaterial)
		return;
	XcAutomap::Apply(vRules[Layer.m_Rule], Width, Height, vIndices, &vFlags, 1, &vWriters);
	auto pLayer = std::make_shared<CLayerTiles>(pMap, Width, Height);
	pLayer->m_Image = Layer.m_Image;
	str_copy(pLayer->m_aName, s_apDesignMaterials[Material]);
	bool HasTiles = false;
	for(int y = 0; y < Height; ++y)
		for(int x = 0; x < Width; ++x)
		{
			const size_t Cell = (size_t)y * Width + x;
			if(vWriters[Cell] >= 0 && vIndices[Cell] > 0 && vIndices[Cell] <= 255)
			{
				pLayer->SetTileIgnoreHistory(x, y, CTile{(unsigned char)vIndices[Cell], (unsigned char)vFlags[Cell]});
				HasTiles = true;
			}
		}
	if(HasTiles)
		Layer.m_pPreview = std::move(pLayer);
}

void CXcEditorUi::RenderDesignCanvas(ImVec2 Size)
{
	const CEditorMap *pMap = m_pEditor->Map();
	const ImVec2 Min = ImGui::GetCursorScreenPos();
	const ImVec2 Max(Min.x + Size.x, Min.y + Size.y);
	ImGui::InvisibleButton("##design_canvas", Size, ImGuiButtonFlags_MouseButtonMiddle);
	auto *pDraw = ImGui::GetWindowDrawList();
	pDraw->AddRectFilled(Min, Max, Col(Theme().m_Background), S(10.0f));
	if(!pMap->m_pGameLayer)
		return;
	const auto &Game = *pMap->m_pGameLayer;
	const ImGuiIO &Io = ImGui::GetIO();
	if(ImGui::IsItemHovered() && Io.MouseWheel != 0)
		m_DesignZoom = std::clamp(m_DesignZoom * std::pow(1.25f, Io.MouseWheel), 1.0f, 64.0f);
	if(ImGui::IsItemActive() && ImGui::IsMouseDragging(ImGuiMouseButton_Middle))
	{
		m_DesignPan.x += Io.MouseDelta.x;
		m_DesignPan.y += Io.MouseDelta.y;
	}
	const float Tile = std::max(0.05f, std::min(Size.x / Game.m_Width, Size.y / Game.m_Height) * 0.92f * m_DesignZoom);
	const ImVec2 Origin(Min.x + (Size.x - Game.m_Width * Tile) * 0.5f + m_DesignPan.x,
		Min.y + (Size.y - Game.m_Height * Tile) * 0.5f + m_DesignPan.y);
	const int X0 = std::clamp((int)std::floor((Min.x - Origin.x) / Tile), 0, Game.m_Width);
	const int Y0 = std::clamp((int)std::floor((Min.y - Origin.y) / Tile), 0, Game.m_Height);
	const int X1 = std::clamp((int)std::ceil((Max.x - Origin.x) / Tile), 0, Game.m_Width);
	const int Y1 = std::clamp((int)std::ceil((Max.y - Origin.y) / Tile), 0, Game.m_Height);
	// Sample very large maps at overview scale to bound the number of preview vertices.
	const int Step = std::max(1, (int)std::ceil(std::sqrt((double)(X1 - X0) * (Y1 - Y0) / 12000.0)));
	pDraw->PushClipRect(Min, Max, true);
	for(int y = Y0; y < Y1; y += Step)
		for(int x = X0; x < X1; x += Step)
		{
			const int Index = Game.GetTile(x, y).m_Index;
			if(Index != TILE_SOLID && Index != TILE_NOHOOK && Index != TILE_FREEZE)
				continue;
			const ImVec2 P(Origin.x + x * Tile, Origin.y + y * Tile);
			pDraw->AddRectFilled(P, ImVec2(P.x + Tile * Step, P.y + Tile * Step), Col(Theme().m_TextMuted, 0.23f));
		}
	for(const auto &Layer : m_aDesignLayers)
	{
		if(!Layer.m_pPreview || Layer.m_Image < 0 || Layer.m_Image >= (int)pMap->m_vpImages.size())
			continue;
		const auto Texture = (ImTextureID)(uintptr_t)m_Gui.TextureId(pMap->m_vpImages[Layer.m_Image]->m_Texture);
		const ImU32 Tint = ImGui::ColorConvertFloat4ToU32(Layer.m_Tint);
		for(int y = Y0; y < Y1; y += Step)
			for(int x = X0; x < X1; x += Step)
			{
				const CTile Data = Layer.m_pPreview->GetTile(x, y);
				if(Data.m_Index == 0)
					continue;
				float aU[4], aV[4];
				XcAutomap::TileUvs(Data.m_Flags, aU, aV);
				ImVec2 aUv[4];
				for(int i = 0; i < 4; ++i)
					aUv[i] = ImVec2((Data.m_Index % 16 + aU[i]) / 16.0f, (Data.m_Index / 16 + aV[i]) / 16.0f);
				const ImVec2 P(Origin.x + x * Tile, Origin.y + y * Tile);
				const float Span = Tile * Step;
				pDraw->AddImageQuad(Texture, P, ImVec2(P.x + Span, P.y), ImVec2(P.x + Span, P.y + Span), ImVec2(P.x, P.y + Span),
					aUv[0], aUv[1], aUv[2], aUv[3], Tint);
			}
	}
	pDraw->PopClipRect();
}

void CXcEditorUi::RenderDesignDialog()
{
	CEditorMap *pMap = m_pEditor->Map();
	const auto LoadPreset = [&](int Preset) {
		m_DesignPreset = Preset;
		std::vector<int> vImages;
		for(int i = 0; i < (int)pMap->m_vpImages.size(); ++i)
			if(HasDesignRules(pMap->m_vpImages[i]))
				vImages.push_back(i);
		for(int i = 0; i < 3; ++i)
		{
			auto &Layer = m_aDesignLayers[i];
			Layer.m_Enabled = true;
			Layer.m_Image = vImages.empty() ? -1 : vImages[std::min(Preset, (int)vImages.size() - 1)];
			Layer.m_Rule = 0;
			Layer.m_Tint = s_aaDesignColors[Preset][i];
			BuildDesignPreview(i);
		}
	};

	const ImVec2 Display = ImGui::GetIO().DisplaySize;
	const ImVec2 WindowSize(std::min(S(1160.0f), Display.x * 0.97f), std::min(S(700.0f), Display.y * 0.94f));
	if(m_DesignRequest)
	{
		m_DesignRequest = false;
		m_DesignZoom = 1.0f;
		m_DesignPan = {};
		LoadPreset(0);
		ImGui::OpenPopup("##xe_design");
	}
	ImGui::SetNextWindowPos(ImVec2(Display.x * 0.5f, Display.y * 0.5f), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
	ImGui::SetNextWindowSize(WindowSize);
	if(!ImGui::BeginPopupModal("##xe_design", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoScrollbar))
		return;

	Header(Tr("Design map..."));
	const float Width = ImGui::GetContentRegionAvail().x;
	const float Height = std::max(S(100.0f), ImGui::GetContentRegionAvail().y - S(48.0f));
	const float Gap = ImGui::GetStyle().ItemSpacing.x;
	const float Left = Width * 0.18f, Right = Width * 0.27f;
	ImGui::BeginChild("##design_presets", ImVec2(Left, Height));
	Title(Tr("Presets"));
	for(int i = 0; i < 3; ++i)
	{
		ImGui::PushID(i);
		const ImVec2 P = ImGui::GetCursorScreenPos();
		auto *pDraw = ImGui::GetWindowDrawList();
		const float CardWidth = ImGui::GetContentRegionAvail().x;
		const float CardHeight = S(55.0f);
		if(ImGui::InvisibleButton("##preset", ImVec2(CardWidth, CardHeight)))
			LoadPreset(i);
		pDraw->AddRectFilled(P, ImVec2(P.x + CardWidth, P.y + CardHeight), Col(Theme().m_Card), S(8.0f));
		if(i == m_DesignPreset || ImGui::IsItemHovered())
			pDraw->AddRect(P, ImVec2(P.x + CardWidth, P.y + CardHeight), Col(Theme().m_Accent), S(8.0f));
		for(int j = 0; j < 3; ++j)
		{
			const float X = P.x + S(8.0f) + j * (CardWidth - S(16.0f)) / 3.0f;
			pDraw->AddRectFilled(ImVec2(X, P.y + S(8.0f)), ImVec2(X + (CardWidth - S(20.0f)) / 3.0f, P.y + S(25.0f)), Col(s_aaDesignColors[i][j]), S(3.0f));
		}
		pDraw->AddText(ImVec2(P.x + S(8.0f), P.y + S(32.0f)), Col(Theme().m_Text), Tr(s_apDesignPresets[i]));
		ImGui::PopID();
	}
	ImGui::EndChild();
	ImGui::SameLine();
	RenderDesignCanvas(ImVec2(Width - Left - Right - Gap * 2.0f, Height));
	ImGui::SameLine();
	ImGui::BeginChild("##design_customize", ImVec2(Right, Height));
	Title(Tr("Customize"));
	if(std::none_of(pMap->m_vpImages.begin(), pMap->m_vpImages.end(), HasDesignRules))
		Description(Tr("Add a tileset with automapper rules first."));
	for(int i = 0; i < 3; ++i)
	{
		ImGui::PushID(i);
		auto &Layer = m_aDesignLayers[i];
		bool Rebuild = ImGui::Checkbox(Tr(s_apDesignMaterials[i]), &Layer.m_Enabled);
		if(Layer.m_Enabled)
		{
			const bool ValidImage = Layer.m_Image >= 0 && Layer.m_Image < (int)pMap->m_vpImages.size();
			ImGui::TextUnformatted(Tr("Tileset"));
			ImGui::SetNextItemWidth(-1.0f);
			if(ImGui::BeginCombo("##tileset", ValidImage ? pMap->m_vpImages[Layer.m_Image]->m_aName : "-"))
			{
				for(int j = 0; j < (int)pMap->m_vpImages.size(); ++j)
					if(HasDesignRules(pMap->m_vpImages[j]) && ImGui::Selectable(pMap->m_vpImages[j]->m_aName, j == Layer.m_Image))
					{
						Layer.m_Image = j;
						Layer.m_Rule = 0;
						Rebuild = true;
					}
				ImGui::EndCombo();
			}
			if(Layer.m_Image >= 0 && Layer.m_Image < (int)pMap->m_vpImages.size())
			{
				const auto &Mapper = pMap->m_vpImages[Layer.m_Image]->m_Automapper;
				ImGui::TextUnformatted(Tr("Style"));
				ImGui::SetNextItemWidth(-1.0f);
				if(Mapper.ConfigNamesNum() > 0 && ImGui::BeginCombo("##style", Mapper.GetConfigName(Layer.m_Rule)))
				{
					for(int j = 0; j < Mapper.ConfigNamesNum(); ++j)
						if(ImGui::Selectable(Mapper.GetConfigName(j), j == Layer.m_Rule))
						{
							Layer.m_Rule = j;
							Rebuild = true;
						}
					ImGui::EndCombo();
				}
			}
			ImGui::ColorEdit4(Tr("Color"), &Layer.m_Tint.x, ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_AlphaBar);
		}
		if(Rebuild)
			BuildDesignPreview(i);
		ImGui::Separator();
		ImGui::PopID();
	}
	ImGui::EndChild();

	const bool Cancel = Button(Tr("Cancel"), false, S(120.0f)) || ImGui::IsKeyPressed(ImGuiKey_Escape);
	ImGui::SameLine();
	const bool HasPreview = std::any_of(std::begin(m_aDesignLayers), std::end(m_aDesignLayers), [](const auto &Layer) { return Layer.m_pPreview != nullptr; });
	ImGui::BeginDisabled(!HasPreview || !pMap->m_pGameGroup);
	const bool Apply = Button(Tr("Apply"), true, S(120.0f));
	ImGui::EndDisabled();
	if(Apply && !Cancel)
	{
		const auto &pGroup = pMap->m_pGameGroup;
		const int Group = std::find(pMap->m_vpGroups.begin(), pMap->m_vpGroups.end(), pGroup) - pMap->m_vpGroups.begin();
		pMap->m_EditorHistory.BeginBulk();
		for(auto &Layer : m_aDesignLayers)
		{
			if(!Layer.m_pPreview)
				continue;
			const ImU32 Color = ImGui::ColorConvertFloat4ToU32(Layer.m_Tint);
			Layer.m_pPreview->m_Color = {(int)(Color & 255), (int)((Color >> 8) & 255), (int)((Color >> 16) & 255), (int)((Color >> 24) & 255)};
			pGroup->AddLayer(Layer.m_pPreview);
			pMap->m_EditorHistory.RecordAction(std::make_shared<CEditorActionAddLayer>(pMap, Group, pGroup->m_vpLayers.size() - 1));
		}
		pMap->m_EditorHistory.EndBulk("Design map");
	}
	if(Cancel || Apply)
	{
		for(auto &Layer : m_aDesignLayers)
			Layer.m_pPreview.reset();
		ImGui::CloseCurrentPopup();
	}
	ImGui::EndPopup();
}

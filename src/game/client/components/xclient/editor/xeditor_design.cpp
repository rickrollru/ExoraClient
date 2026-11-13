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

void CXcEditorUi::BuildDesignPreview()
{
	m_pDesignPreview.reset();
	CEditor &E = *m_pEditor;
	CEditorMap *pMap = E.Map();
	const auto &pGame = pMap->m_pGameLayer;
	if(!pGame || m_DesignImage < 0 || m_DesignImage >= (int)pMap->m_vpImages.size())
		return;

	char aPath[IO_MAX_PATH_LENGTH];
	str_format(aPath, sizeof(aPath), "editor/automap/%s.rules", pMap->m_vpImages[m_DesignImage]->m_aName);
	char *pText = E.Storage()->ReadFileStr(aPath, IStorage::TYPE_ALL);
	if(!pText)
		return;
	std::vector<XcAutomap::SConfig> vRules;
	XcAutomap::Parse(pText, vRules);
	free(pText);
	if(m_DesignRule < 0 || m_DesignRule >= (int)vRules.size())
		return;

	const int Width = pGame->m_Width, Height = pGame->m_Height;
	std::vector<int> vIndices((size_t)Width * Height), vFlags, vWriters;
	const int aMaterials[] = {TILE_SOLID, TILE_NOHOOK, TILE_FREEZE};
	for(int y = 0; y < Height; ++y)
		for(int x = 0; x < Width; ++x)
			vIndices[(size_t)y * Width + x] = pGame->GetTile(x, y).m_Index == aMaterials[m_DesignMaterial] ? 1 : 0;
	XcAutomap::Apply(vRules[m_DesignRule], Width, Height, vIndices, &vFlags, m_DesignSeed, &vWriters);
	for(size_t i = 0; i < vIndices.size(); ++i)
		if(vWriters[i] < 0 || vIndices[i] < 0 || vIndices[i] > 255)
			vIndices[i] = 0;
	if(std::none_of(vIndices.begin(), vIndices.end(), [](int Index) { return Index > 0 && Index <= 255; }))
		return;
	m_pDesignPreview = std::make_shared<CLayerTiles>(pMap, Width, Height);
	str_copy(m_pDesignPreview->m_aName, "Design");
	m_pDesignPreview->m_Image = m_DesignImage;
	for(int y = 0; y < Height; ++y)
		for(int x = 0; x < Width; ++x)
		{
			const size_t Cell = (size_t)y * Width + x;
			m_pDesignPreview->SetTileIgnoreHistory(x, y, CTile{(unsigned char)std::clamp(vIndices[Cell], 0, 255), (unsigned char)vFlags[Cell]});
		}
}

void CXcEditorUi::RenderDesignPreview()
{
	if(!m_pDesignPreview || m_pDesignPreview->Map() != m_pEditor->Map())
		return;
	CEditor &E = *m_pEditor;
	const CScreenRect Saved = E.Graphics()->GetScreen();
	E.Map()->m_pGameGroup->MapScreen();
	m_pDesignPreview->Render(E.Map());
	E.Graphics()->MapScreen(Saved);
}

void CXcEditorUi::RenderDesignDialog()
{
	if(m_DesignRequest)
	{
		m_DesignRequest = false;
		m_pDesignPreview.reset();
		ImGui::OpenPopup("##xe_design");
		ImGui::SetNextWindowPos(ImVec2(ImGui::GetIO().DisplaySize.x - S(20.0f), S(70.0f)), ImGuiCond_Appearing, ImVec2(1.0f, 0.0f));
	}
	if(!ImGui::BeginPopupModal("##xe_design", nullptr, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoTitleBar))
		return;

	CEditorMap *pMap = m_pEditor->Map();
	Header(Tr("Design map..."));
	Description(Tr("Choose a tileset and preview a decorative layer. The Game layer stays unchanged."), S(380.0f));
	const auto HasRules = [](const auto &pImage) { return pImage->m_Width % 16 == 0 && pImage->m_Height % 16 == 0 && pImage->m_Automapper.ConfigNamesNum() > 0; };
	if(m_DesignImage >= (int)pMap->m_vpImages.size() || (m_DesignImage >= 0 && !HasRules(pMap->m_vpImages[m_DesignImage])))
		m_DesignImage = -1;
	if(m_DesignImage < 0)
	{
		const auto It = std::find_if(pMap->m_vpImages.begin(), pMap->m_vpImages.end(), HasRules);
		if(It != pMap->m_vpImages.end())
			m_DesignImage = It - pMap->m_vpImages.begin();
	}
	if(m_DesignImage >= 0 && pMap->m_pGameLayer && pMap->m_pGameGroup)
	{
		ImGui::SetNextItemWidth(S(320.0f));
		if(ImGui::BeginCombo(Tr("Tileset"), pMap->m_vpImages[m_DesignImage]->m_aName))
		{
			for(int i = 0; i < (int)pMap->m_vpImages.size(); ++i)
				if(HasRules(pMap->m_vpImages[i]) && ImGui::Selectable(pMap->m_vpImages[i]->m_aName, i == m_DesignImage))
				{
					m_DesignImage = i;
					m_DesignRule = 0;
					m_pDesignPreview.reset();
				}
			ImGui::EndCombo();
		}
		const auto &Mapper = pMap->m_vpImages[m_DesignImage]->m_Automapper;
		m_DesignRule = std::clamp(m_DesignRule, 0, Mapper.ConfigNamesNum() - 1);
		ImGui::SetNextItemWidth(S(320.0f));
		if(ImGui::BeginCombo(Tr("Style"), Mapper.GetConfigName(m_DesignRule)))
		{
			for(int i = 0; i < Mapper.ConfigNamesNum(); ++i)
				if(ImGui::Selectable(Mapper.GetConfigName(i), i == m_DesignRule))
				{
					m_DesignRule = i;
					m_pDesignPreview.reset();
				}
			ImGui::EndCombo();
		}
		const char *apMaterials[] = {Tr("Hookable"), Tr("Unhookable"), Tr("Freeze")};
		if(ImGui::Combo(Tr("Game tiles"), &m_DesignMaterial, apMaterials, 3))
			m_pDesignPreview.reset();
		if(ImGui::InputInt(Tr("Seed"), &m_DesignSeed))
			m_pDesignPreview.reset();
		ImGui::ColorEdit4(Tr("Color"), &m_DesignTint.x);
		if(Button(Tr("Preview"), false, S(130.0f)))
		{
			BuildDesignPreview();
			if(!m_pDesignPreview)
				ShowMessage(Tr("This style did not produce any tiles."), Tr("Design map..."));
		}
		if(m_pDesignPreview)
		{
			const ImU32 Color = ImGui::ColorConvertFloat4ToU32(m_DesignTint);
			m_pDesignPreview->m_Color = {(int)(Color & 255), (int)((Color >> 8) & 255), (int)((Color >> 16) & 255), (int)((Color >> 24) & 255)};
			ImGui::SameLine();
			if(Button(Tr("Apply"), true, S(130.0f)))
			{
				const auto &pGroup = pMap->m_pGameGroup;
				const int Group = std::find(pMap->m_vpGroups.begin(), pMap->m_vpGroups.end(), pGroup) - pMap->m_vpGroups.begin();
				pGroup->AddLayer(m_pDesignPreview);
				pMap->m_EditorHistory.RecordAction(std::make_shared<CEditorActionAddLayer>(pMap, Group, pGroup->m_vpLayers.size() - 1), "Design map");
				m_pDesignPreview.reset();
				ImGui::CloseCurrentPopup();
			}
		}
	}
	else
		Description(Tr("Add a tileset with automapper rules first."), S(380.0f));

	if(Button(Tr("Cancel"), false, S(130.0f)) || ImGui::IsKeyPressed(ImGuiKey_Escape))
	{
		m_pDesignPreview.reset();
		ImGui::CloseCurrentPopup();
	}
	ImGui::EndPopup();
}

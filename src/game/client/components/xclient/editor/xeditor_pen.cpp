#include "xeditor.h"


#include <base/math.h>
#include <base/str.h>
#include <engine/keys.h>
#include <engine/storage.h>
#include <game/client/components/xclient/ui/lang.h>
#include <game/editor/editor.h>
#include <game/editor/editor_actions.h>

#include <algorithm>
#include <array>
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

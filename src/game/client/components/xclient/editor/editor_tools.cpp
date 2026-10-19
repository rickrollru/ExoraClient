// Photoshop like tools for the map editor: a vertical tool strip on the left and tools that work on the
// map with the left mouse button. Brush is the classic editor behavior; eraser, bucket fill and the
// eyedropper change tiles of the selected tile layers and go into the undo history; hand and zoom
// only move the view.
#include "editor_style.h"

#include <base/math.h>
#include <base/str.h>

#include <engine/input.h>
#include <engine/keys.h>
#include <engine/shared/config.h>

#include <game/client/components/xclient/ui/lang.h>
#include <game/client/lineinput.h>
#include <game/client/ui.h>
#include <game/editor/editor.h>
#include <game/editor/editor_actions.h>
#include <game/editor/mapitems/layer_group.h>
#include <game/editor/mapitems/layer_tiles.h>

#include <algorithm>
#include <vector>

namespace
{
struct STool
{
	const char *m_pIcon; // Font Awesome 6 solid
	const char *m_pName;
	int m_Key;
	const char *m_pHint;
};

const STool s_aTools[CEditor::XC_NUM_TOOLS] = {
	{"\xef\x87\xbc", "Brush", KEY_B, "[B] Brush: drag to grab tiles as a brush, then paint with it. The classic editor tool."},
	{"\xef\x84\xad", "Eraser", KEY_E, "[E] Eraser: removes tiles under the mouse on the selected tile layers. Wheel with Shift changes the size."},
	{"\xef\x95\xb6", "Fill", KEY_G, "[G] Bucket fill: fills the connected area of the same tile with the first tile of the brush (an empty brush clears it)."},
	{"\xef\x8d\xa2", "Replace", KEY_J, "[J] Replace: every tile like the clicked one on the whole layer becomes the first tile of the brush (an empty brush removes them)."},
	{"\xee\x8b\x8a", "Magic wand", KEY_X, "[X] Magic wand: the connected area of the same tile becomes the brush. Shift takes every such tile of the layer, Alt erases the area."},
	{"\xef\x87\xbb", "Eyedropper", KEY_I, "[I] Eyedropper: takes the tile under the mouse as the brush and goes back to the brush tool."},
	{"\xef\x89\x96", "Hand", KEY_H, "[H] Hand: drag to move the view. Space or the middle mouse button work with every tool."},
	{"\xef\x80\x82", "Zoom", KEY_Z, "[Z] Zoom: left click zooms in, right click zooms out."},
};

// the tile layers the tools work on: the selected ones
std::vector<std::shared_ptr<CLayerTiles>> SelectedTileLayers(CEditorMap *pMap)
{
	std::vector<std::shared_ptr<CLayerTiles>> vLayers;
	for(size_t i = 0; i < pMap->m_vSelectedLayers.size(); i++)
	{
		std::shared_ptr<CLayer> pLayer = pMap->SelectedLayerType(i, LAYERTYPE_TILES);
		if(pLayer)
			vLayers.push_back(std::static_pointer_cast<CLayerTiles>(pLayer));
	}
	return vLayers;
}

// the tiles like the one at (x, y): connected (4 neighbors) or, with Global, the whole layer. Returns how many,
// the mask is width * height and the box is set to the bounding box
int SameTiles(const CLayerTiles *pLayer, int TileX, int TileY, bool Global, std::vector<bool> &vMask, ivec2 &BoxMin, ivec2 &BoxMax)
{
	vMask.assign((size_t)pLayer->m_Width * pLayer->m_Height, false);
	BoxMin = ivec2(TileX, TileY);
	BoxMax = ivec2(TileX, TileY);
	if(TileX < 0 || TileY < 0 || TileX >= pLayer->m_Width || TileY >= pLayer->m_Height)
		return 0;
	const CTile Target = pLayer->GetTile(TileX, TileY);
	const auto Same = [&](int x, int y) {
		const CTile Tile = pLayer->GetTile(x, y);
		return Tile.m_Index == Target.m_Index && Tile.m_Flags == Target.m_Flags;
	};
	int Count = 0;
	const auto Take = [&](int x, int y) {
		vMask[(size_t)y * pLayer->m_Width + x] = true;
		BoxMin = ivec2(std::min(BoxMin.x, x), std::min(BoxMin.y, y));
		BoxMax = ivec2(std::max(BoxMax.x, x), std::max(BoxMax.y, y));
		Count++;
	};
	if(Global)
	{
		for(int y = 0; y < pLayer->m_Height; y++)
			for(int x = 0; x < pLayer->m_Width; x++)
				if(Same(x, y))
					Take(x, y);
		return Count;
	}
	std::vector<bool> vVisited((size_t)pLayer->m_Width * pLayer->m_Height, false);
	std::vector<ivec2> vStack = {ivec2(TileX, TileY)};
	while(!vStack.empty() && Count < 250000)
	{
		const ivec2 P = vStack.back();
		vStack.pop_back();
		if(P.x < 0 || P.y < 0 || P.x >= pLayer->m_Width || P.y >= pLayer->m_Height)
			continue;
		const size_t Index = (size_t)P.y * pLayer->m_Width + P.x;
		if(vVisited[Index])
			continue;
		vVisited[Index] = true;
		if(!Same(P.x, P.y))
			continue;
		Take(P.x, P.y);
		vStack.push_back(ivec2(P.x + 1, P.y));
		vStack.push_back(ivec2(P.x - 1, P.y));
		vStack.push_back(ivec2(P.x, P.y + 1));
		vStack.push_back(ivec2(P.x, P.y - 1));
	}
	return Count;
}
} // namespace

bool CEditor::XcPhotoshopLayout() const
{
	// the new interface always has the tools
	return XcNewUi() || (XcEditorStyle::Enabled() && g_Config.m_XcEditorLayout);
}

bool CEditor::XcNewUi() const
{
	return g_Config.m_XcEditorUi && m_XcUi.IsReady();
}

void CEditor::XcToolInfo(int Tool, const char **ppIcon, const char **ppName, const char **ppHint)
{
	*ppIcon = s_aTools[Tool].m_pIcon;
	*ppName = s_aTools[Tool].m_pName;
	*ppHint = s_aTools[Tool].m_pHint;
}

void CEditor::XcHandleToolHotkeys()
{
	if(!XcPhotoshopLayout() || m_Dialog != DIALOG_NONE || CLineInput::GetActiveInput() != nullptr || Ui()->IsPopupOpen() ||
		Input()->ModifierIsPressed() || Input()->ShiftIsPressed() || Input()->AltIsPressed())
		return;
	for(int Tool = 0; Tool < XC_NUM_TOOLS; Tool++)
		if(Input()->KeyPress(s_aTools[Tool].m_Key))
			m_XcTool = Tool;
}

void CEditor::XcRenderToolStrip(CUIRect Strip)
{
	XcHandleToolHotkeys();
	if(m_XcFloatingTools.w > 0.0f)
	{
		// floating over the map: its own panel with a shadow
		CUIRect Shadow = Strip;
		Shadow.Margin(-2.0f, &Shadow);
		Shadow.y += 2.0f;
		Shadow.Draw(ColorRGBA(0.0f, 0.0f, 0.0f, 0.35f), IGraphics::CORNER_ALL, 7.0f);
		Strip.Draw(XcEditorStyle::Panel(), IGraphics::CORNER_ALL, 6.0f);
		Strip.DrawOutline(XcEditorStyle::PanelLine());
	}
	Strip.Margin(3.0f, &Strip);
	CUIRect Grip;
	Strip.HSplitTop(10.0f, &Grip, &Strip);
	XcDoPanelGrip(Grip, XC_PANEL_TOOLS);
	static int s_aButtons[XC_NUM_TOOLS] = {};
	for(int Tool = 0; Tool < XC_NUM_TOOLS; Tool++)
	{
		CUIRect Button;
		Strip.HSplitTop(24.0f, &Button, &Strip);
		Strip.HSplitTop(Tool == XC_TOOL_PICKER ? 10.0f : 3.0f, nullptr, &Strip); // a gap before the view tools
		if(DoButton_FontIcon(&s_aButtons[Tool], s_aTools[Tool].m_pIcon, m_XcTool == Tool ? 1 : 0, &Button, BUTTONFLAG_LEFT, s_aTools[Tool].m_pHint, IGraphics::CORNER_ALL, 10.0f))
			m_XcTool = Tool;
	}
	// After Effects like animation presets for the selection
	{
		Strip.HSplitTop(10.0f, nullptr, &Strip);
		CUIRect Button;
		Strip.HSplitTop(24.0f, &Button, &Strip);
		Strip.HSplitTop(3.0f, nullptr, &Strip);
		static int s_AnimateButton = 0;
		if(DoButton_FontIcon(&s_AnimateButton, "\xef\x80\x88", 0, &Button, BUTTONFLAG_LEFT, XcUi::Tr("Animation presets (After Effects): sway, spin, pulse and more for the selected quads"), IGraphics::CORNER_ALL, 10.0f))
			XcOpenAnimate();
	}
	if(m_XcTool == XC_TOOL_ERASER)
	{
		// the eraser size under the tools
		char aSize[16];
		str_format(aSize, sizeof(aSize), "%d", g_Config.m_XcEditorEraserSize);
		CUIRect Label;
		Strip.HSplitTop(14.0f, &Label, &Strip);
		Ui()->DoLabel(&Label, aSize, 9.0f, TEXTALIGN_MC);
	}
}

bool CEditor::XcDoMapTool(vec2 WorldPos, bool Inside)
{
	if(!XcPhotoshopLayout() || m_XcTool == XC_TOOL_BRUSH || m_ShowPicker)
		return false;
	// Ctrl + mouse keeps panning like in the classic editor
	if(Input()->ModifierIsPressed())
		return true;

	static bool s_Painting = false;
	const auto FinishStroke = [&]() {
		if(s_Painting)
		{
			Map()->m_EditorHistory.RecordAction(std::make_shared<CEditorBrushDrawAction>(Map(), Map()->m_SelectedGroup));
			s_Painting = false;
		}
	};

	if(m_XcTool == XC_TOOL_HAND)
	{
		m_CursorType = CURSOR_NORMAL;
		str_copy(m_aTooltip, XcUi::Tr("Hand: drag to move the view"));
		return true;
	}
	if(m_XcTool == XC_TOOL_ZOOM)
	{
		str_copy(m_aTooltip, XcUi::Tr("Zoom: left click zooms in, right click zooms out"));
		if(Inside && Ui()->MouseButtonClicked(0))
			MapView()->Zoom()->ScaleValue(1.0f / 1.4f);
		else if(Inside && Ui()->MouseButtonClicked(1))
			MapView()->Zoom()->ScaleValue(1.4f);
		return true;
	}

	std::vector<std::shared_ptr<CLayerTiles>> vLayers = SelectedTileLayers(Map());
	if(vLayers.empty())
	{
		str_copy(m_aTooltip, XcUi::Tr("Select a tile layer on the right to use this tool"));
		FinishStroke();
		return true;
	}
	const int TileX = vLayers[0]->ConvertX(WorldPos.x);
	const int TileY = vLayers[0]->ConvertY(WorldPos.y);

	if(m_XcTool == XC_TOOL_ERASER)
	{
		// Shift + wheel changes the size
		if(Input()->ShiftIsPressed())
		{
			if(Input()->KeyPress(KEY_MOUSE_WHEEL_UP))
				g_Config.m_XcEditorEraserSize = std::min(g_Config.m_XcEditorEraserSize + 1, 16);
			if(Input()->KeyPress(KEY_MOUSE_WHEEL_DOWN))
				g_Config.m_XcEditorEraserSize = std::max(g_Config.m_XcEditorEraserSize - 1, 1);
		}
		const int Size = g_Config.m_XcEditorEraserSize;
		const int Start = -(Size - 1) / 2;
		// outline of what gets erased
		CUIRect Outline = {(TileX + Start) * 32.0f, (TileY + Start) * 32.0f, Size * 32.0f, Size * 32.0f};
		Outline.DrawOutline(XcEditorStyle::Accent(0.9f));
		str_copy(m_aTooltip, XcUi::Tr("Eraser: hold the left mouse button. Shift + wheel changes the size"));
		if(Inside && Ui()->MouseButton(0))
		{
			for(const auto &pLayer : vLayers)
			{
				for(int y = TileY + Start; y < TileY + Start + Size; y++)
					for(int x = TileX + Start; x < TileX + Start + Size; x++)
						if(x >= 0 && y >= 0 && x < pLayer->m_Width && y < pLayer->m_Height && pLayer->GetTile(x, y).m_Index != 0)
						{
							pLayer->SetTile(x, y, CTile{0});
							s_Painting = true;
						}
			}
			if(s_Painting)
				Map()->OnModify();
		}
		else
			FinishStroke();
		return true;
	}

	if(m_XcTool == XC_TOOL_PICKER)
	{
		CUIRect Outline = {TileX * 32.0f, TileY * 32.0f, 32.0f, 32.0f};
		Outline.DrawOutline(XcEditorStyle::Accent(0.9f));
		str_copy(m_aTooltip, XcUi::Tr("Eyedropper: click a tile to paint with it"));
		if(Inside && Ui()->MouseButtonClicked(0))
		{
			m_pBrush->Clear();
			CUIRect Grab = {TileX * 32.0f + 1.0f, TileY * 32.0f + 1.0f, 30.0f, 30.0f};
			vLayers[0]->BrushGrab(m_pBrush.get(), Grab);
			m_XcTool = XC_TOOL_BRUSH;
		}
		return true;
	}

	if(m_XcTool == XC_TOOL_FILL)
	{
		CUIRect Outline = {TileX * 32.0f, TileY * 32.0f, 32.0f, 32.0f};
		Outline.DrawOutline(XcEditorStyle::Accent(0.9f));
		str_copy(m_aTooltip, XcUi::Tr("Bucket fill: click to fill the area of the same tile with the brush"));
		if(!Inside || !Ui()->MouseButtonClicked(0))
			return true;
		// the tile to fill with: the first tile of a tile brush, nothing clears the area
		CTile FillTile = CTile{0};
		if(!m_pBrush->IsEmpty() && m_pBrush->m_vpLayers[0]->m_Type == LAYERTYPE_TILES)
			FillTile = std::static_pointer_cast<CLayerTiles>(m_pBrush->m_vpLayers[0])->GetTile(0, 0);
		for(const auto &pLayer : vLayers)
		{
			if(TileX < 0 || TileY < 0 || TileX >= pLayer->m_Width || TileY >= pLayer->m_Height)
				continue;
			const CTile Target = pLayer->GetTile(TileX, TileY);
			if(Target.m_Index == FillTile.m_Index && Target.m_Flags == FillTile.m_Flags)
				continue;
			// flood fill over the same tile, 4 neighbors, at most a big area
			std::vector<bool> vVisited((size_t)pLayer->m_Width * pLayer->m_Height, false);
			std::vector<ivec2> vStack = {ivec2(TileX, TileY)};
			int Filled = 0;
			while(!vStack.empty() && Filled < 250000)
			{
				const ivec2 P = vStack.back();
				vStack.pop_back();
				if(P.x < 0 || P.y < 0 || P.x >= pLayer->m_Width || P.y >= pLayer->m_Height)
					continue;
				const size_t Index = (size_t)P.y * pLayer->m_Width + P.x;
				if(vVisited[Index])
					continue;
				vVisited[Index] = true;
				const CTile Tile = pLayer->GetTile(P.x, P.y);
				if(Tile.m_Index != Target.m_Index || Tile.m_Flags != Target.m_Flags)
					continue;
				pLayer->SetTile(P.x, P.y, FillTile);
				Filled++;
				vStack.push_back(ivec2(P.x + 1, P.y));
				vStack.push_back(ivec2(P.x - 1, P.y));
				vStack.push_back(ivec2(P.x, P.y + 1));
				vStack.push_back(ivec2(P.x, P.y - 1));
			}
			if(Filled > 0)
				s_Painting = true;
		}
		if(s_Painting)
			Map()->OnModify();
		FinishStroke();
		return true;
	}
	if(m_XcTool == XC_TOOL_REPLACE)
	{
		CUIRect Outline = {TileX * 32.0f, TileY * 32.0f, 32.0f, 32.0f};
		Outline.DrawOutline(XcEditorStyle::Accent(0.9f));
		str_copy(m_aTooltip, XcUi::Tr("Replace: click a tile, every tile like it on the layer becomes the brush tile"));
		if(!Inside || !Ui()->MouseButtonClicked(0))
			return true;
		CTile NewTile = CTile{0};
		if(!m_pBrush->IsEmpty() && m_pBrush->m_vpLayers[0]->m_Type == LAYERTYPE_TILES)
			NewTile = std::static_pointer_cast<CLayerTiles>(m_pBrush->m_vpLayers[0])->GetTile(0, 0);
		for(const auto &pLayer : vLayers)
		{
			std::vector<bool> vMask;
			ivec2 BoxMin, BoxMax;
			if(SameTiles(pLayer.get(), TileX, TileY, true, vMask, BoxMin, BoxMax) == 0)
				continue;
			const CTile Old = pLayer->GetTile(TileX, TileY);
			if(Old.m_Index == NewTile.m_Index && Old.m_Flags == NewTile.m_Flags)
				continue;
			for(int y = BoxMin.y; y <= BoxMax.y; y++)
				for(int x = BoxMin.x; x <= BoxMax.x; x++)
					if(vMask[(size_t)y * pLayer->m_Width + x])
						pLayer->SetTile(x, y, NewTile);
			s_Painting = true;
		}
		if(s_Painting)
			Map()->OnModify();
		FinishStroke();
		return true;
	}

	if(m_XcTool == XC_TOOL_WAND)
	{
		CUIRect Outline = {TileX * 32.0f, TileY * 32.0f, 32.0f, 32.0f};
		Outline.DrawOutline(XcEditorStyle::Accent(0.9f));
		str_copy(m_aTooltip, XcUi::Tr("Magic wand: click to take the area of the same tile as the brush. Shift: the whole layer, Alt: erase the area"));
		if(!Inside || !Ui()->MouseButtonClicked(0))
			return true;
		const std::shared_ptr<CLayerTiles> &pLayer = vLayers[0];
		std::vector<bool> vMask;
		ivec2 BoxMin, BoxMax;
		if(SameTiles(pLayer.get(), TileX, TileY, Input()->ShiftIsPressed(), vMask, BoxMin, BoxMax) == 0)
			return true;
		if(Input()->AltIsPressed())
		{
			// erase the area
			for(int y = BoxMin.y; y <= BoxMax.y; y++)
				for(int x = BoxMin.x; x <= BoxMax.x; x++)
					if(vMask[(size_t)y * pLayer->m_Width + x])
						pLayer->SetTile(x, y, CTile{0});
			s_Painting = true;
			Map()->OnModify();
			FinishStroke();
			return true;
		}
		// the bounding box becomes the brush, the tiles outside the area are emptied
		m_pBrush->Clear();
		const CUIRect Grab = {BoxMin.x * 32.0f + 1.0f, BoxMin.y * 32.0f + 1.0f, (BoxMax.x - BoxMin.x + 1) * 32.0f - 2.0f, (BoxMax.y - BoxMin.y + 1) * 32.0f - 2.0f};
		pLayer->BrushGrab(m_pBrush.get(), Grab);
		if(!m_pBrush->IsEmpty() && m_pBrush->m_vpLayers[0]->m_Type == LAYERTYPE_TILES)
		{
			std::shared_ptr<CLayerTiles> pBrushLayer = std::static_pointer_cast<CLayerTiles>(m_pBrush->m_vpLayers[0]);
			for(int y = 0; y < pBrushLayer->m_Height; y++)
				for(int x = 0; x < pBrushLayer->m_Width; x++)
					if(!vMask[(size_t)(BoxMin.y + y) * pLayer->m_Width + BoxMin.x + x])
						pBrushLayer->m_pTiles[(size_t)y * pBrushLayer->m_Width + x] = CTile{0};
		}
		m_XcTool = XC_TOOL_BRUSH;
		return true;
	}
	return false;
}

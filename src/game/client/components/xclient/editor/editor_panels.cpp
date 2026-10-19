// Panels of the map editor that are moved with the mouse, like in Photoshop: the tool strip is dragged by
// the dotted grip on its top and docks to the left or right edge or floats anywhere over the map; the
// layers panel is dragged by the grip above its tabs and docks left or right. The places are saved.
#include "editor_style.h"

#include <base/math.h>

#include <engine/shared/config.h>

#include <game/client/components/xclient/ui/lang.h>
#include <game/client/ui.h>
#include <game/editor/editor.h>

#include <algorithm>

namespace
{
constexpr float TOOLS_WIDTH = 30.0f;
constexpr float TOOLS_HEIGHT = 262.0f;
// closer than this to a side of the work area docks the tool strip there
constexpr float DOCK_DISTANCE = 70.0f;

int s_aGripIds[2];
} // namespace

CUIRect CEditor::XcFloatingToolsRect(CUIRect Area)
{
	const CUIRect Screen = *Ui()->Screen();
	CUIRect Rect;
	Rect.w = TOOLS_WIDTH;
	Rect.h = TOOLS_HEIGHT;
	Rect.x = g_Config.m_XcEditorToolsX / 1000.0f * Screen.w;
	Rect.y = g_Config.m_XcEditorToolsY / 1000.0f * Screen.h;
	// always fully inside the work area, also after the window got smaller
	Rect.x = std::clamp(Rect.x, Area.x + 4.0f, std::max(Area.x + 4.0f, Area.x + Area.w - Rect.w - 4.0f));
	Rect.y = std::clamp(Rect.y, Area.y + 4.0f, std::max(Area.y + 4.0f, Area.y + Area.h - Rect.h - 24.0f));
	return Rect;
}

bool CEditor::XcMouseOverFloatingTools()
{
	if(XcNewUi()) // the panels, menus and the tool capsule of the new interface
		return m_XcUi.WantsMouse();
	return m_XcFloatingTools.w > 0.0f && Ui()->MouseInside(&m_XcFloatingTools);
}

void CEditor::XcDoPanelGrip(CUIRect Grip, int Panel)
{
	const void *pId = &s_aGripIds[Panel == XC_PANEL_TOOLS ? 0 : 1];
	const bool Hot = Ui()->HotItem() == pId || m_XcDragPanel == Panel;
	// a row of dots, like the handle of a Photoshop panel
	const ColorRGBA Dots = Hot ? XcEditorStyle::Accent(0.95f) : ColorRGBA(1.0f, 1.0f, 1.0f, 0.3f);
	const int NumDots = Panel == XC_PANEL_TOOLS ? 3 : 5;
	for(int Row = 0; Row < 2; Row++)
		for(int i = 0; i < NumDots; i++)
		{
			const CUIRect Dot = {Grip.x + Grip.w * 0.5f + (i - (NumDots - 1) * 0.5f) * 4.0f - 1.0f, Grip.y + Grip.h * 0.5f - 2.5f + Row * 4.0f, 2.0f, 2.0f};
			Dot.Draw(Dots, IGraphics::CORNER_ALL, 1.0f);
		}

	if(m_XcDragPanel != XC_PANEL_NONE || Ui()->IsPopupOpen() || m_Dialog != DIALOG_NONE)
		return;
	if(Ui()->MouseInside(&Grip))
	{
		Ui()->SetHotItem(pId);
		str_copy(m_aTooltip, Panel == XC_PANEL_TOOLS ? XcUi::Tr("Drag to move the tools: to the left, to the right or anywhere over the map") : XcUi::Tr("Drag to move the layers panel to the other side"));
	}
	if(Ui()->HotItem() == pId && Ui()->CheckActiveItem(nullptr) && Ui()->MouseButtonClicked(0))
	{
		m_XcDragPanel = Panel;
		m_XcDragOffset = Ui()->MousePos() - vec2(Grip.x, Grip.y);
		Ui()->SetActiveItem(pId);
	}
}

void CEditor::XcRenderPanelDrag()
{
	if(m_XcDragPanel == XC_PANEL_NONE)
		return;
	const CUIRect Area = m_XcWorkArea;
	const vec2 Mouse = Ui()->MousePos();
	const bool Drop = !Ui()->MouseButton(0);

	if(m_XcDragPanel == XC_PANEL_TOOLS)
	{
		// docked at a side when the mouse is close to it, otherwise floating where it is dropped
		int Dock = 2;
		if(Mouse.x < Area.x + DOCK_DISTANCE)
			Dock = 0;
		else if(Mouse.x > Area.x + Area.w - DOCK_DISTANCE)
			Dock = 1;
		CUIRect Ghost;
		if(Dock == 0)
			Ghost = {Area.x, Area.y, TOOLS_WIDTH, Area.h};
		else if(Dock == 1)
			Ghost = {Area.x + Area.w - TOOLS_WIDTH, Area.y, TOOLS_WIDTH, Area.h};
		else
		{
			Ghost = {Mouse.x - m_XcDragOffset.x, Mouse.y - m_XcDragOffset.y, TOOLS_WIDTH, TOOLS_HEIGHT};
			Ghost.x = std::clamp(Ghost.x, Area.x + 4.0f, Area.x + Area.w - Ghost.w - 4.0f);
			Ghost.y = std::clamp(Ghost.y, Area.y + 4.0f, Area.y + Area.h - Ghost.h - 24.0f);
		}
		Ghost.Draw(XcEditorStyle::Accent(0.22f), IGraphics::CORNER_ALL, Dock == 2 ? 6.0f : 0.0f);
		Ghost.DrawOutline(XcEditorStyle::Accent(0.9f));
		if(Drop)
		{
			g_Config.m_XcEditorToolsDock = Dock;
			if(Dock == 2)
			{
				const CUIRect Screen = *Ui()->Screen();
				g_Config.m_XcEditorToolsX = round_to_int(Ghost.x / Screen.w * 1000.0f);
				g_Config.m_XcEditorToolsY = round_to_int(Ghost.y / Screen.h * 1000.0f);
			}
		}
	}
	else
	{
		// the side the mouse is on
		const bool Left = Mouse.x < Area.x + Area.w * 0.5f;
		const CUIRect Ghost = Left ? CUIRect{Area.x, Area.y, m_ToolBoxWidth, Area.h} : CUIRect{Area.x + Area.w - m_ToolBoxWidth, Area.y, m_ToolBoxWidth, Area.h};
		Ghost.Draw(XcEditorStyle::Accent(0.18f), IGraphics::CORNER_NONE, 0.0f);
		Ghost.DrawOutline(XcEditorStyle::Accent(0.9f));
		if(Drop)
			g_Config.m_XcEditorLayersSide = Left ? 1 : 0;
	}
	m_CursorType = CURSOR_NORMAL;
	if(Drop)
	{
		m_XcDragPanel = XC_PANEL_NONE;
		Ui()->SetActiveItem(nullptr);
	}
}

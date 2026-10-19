// Photoshop like preferences of the map editor (Ctrl+K), rulers with guides that are dragged out of
// them, and the precise crosshair cursor. Everything here only changes how the editor looks, the map
// stays the same.
#include "editor_style.h"

#include <base/math.h>
#include <base/str.h>

#include <engine/input.h>
#include <engine/keys.h>
#include <engine/shared/config.h>
#include <engine/textrender.h>

#include <game/client/components/xclient/ui/lang.h>
#include <game/client/lineinput.h>
#include <game/client/ui.h>
#include <game/editor/editor.h>
#include <game/editor/mapitems/layer_group.h>

#include <algorithm>
#include <cmath>

using XcUi::Tr;

// a row of buttons, one of them active; returns true when the value changed
static bool SegmentRow(CEditor *pEditor, CUIRect *pView, const char *pLabel, int *pValue, const char *const *ppItems, int NumItems, const void *pIdBase)
{
	CUIRect Slot, Label, Buttons;
	pView->HSplitTop(2.0f, nullptr, pView);
	pView->HSplitTop(12.0f, &Slot, pView);
	Slot.VMargin(5.0f, &Slot);
	Slot.VSplitLeft(110.0f, &Label, &Buttons);
	pEditor->Ui()->DoLabel(&Label, pLabel, 10.0f, TEXTALIGN_ML);
	bool Changed = false;
	const float Width = Buttons.w / NumItems;
	for(int i = 0; i < NumItems; i++)
	{
		CUIRect Button = {Buttons.x + i * Width, Buttons.y, Width, Buttons.h};
		const int Corners = NumItems == 1 ? IGraphics::CORNER_ALL : (i == 0 ? IGraphics::CORNER_L : (i == NumItems - 1 ? IGraphics::CORNER_R : IGraphics::CORNER_NONE));
		if(pEditor->DoButton_Ex((const char *)pIdBase + i, Tr(ppItems[i]), *pValue == i, &Button, BUTTONFLAG_LEFT, nullptr, Corners))
		{
			*pValue = i;
			Changed = true;
		}
	}
	return Changed;
}

CUi::EPopupMenuFunctionResult CEditor::XcPopupPreferences(void *pContext, CUIRect View, bool Active)
{
	CEditor *pEditor = static_cast<CEditor *>(pContext);
	CUIRect Title;
	View.HSplitTop(14.0f, &Title, &View);
	pEditor->Ui()->DoLabel(&Title, Tr("Preferences"), 12.0f, TEXTALIGN_MC);
	View.HSplitTop(4.0f, nullptr, &View);

	static char s_aIds[8][8];
	const char *apStyle[] = {"Classic", "XClient"};
	SegmentRow(pEditor, &View, Tr("Look"), &g_Config.m_XcEditorStyle, apStyle, 2, s_aIds[0]);
	const char *apLayout[] = {"Classic", "Photoshop"};
	SegmentRow(pEditor, &View, Tr("Layout"), &g_Config.m_XcEditorLayout, apLayout, 2, s_aIds[1]);
	const char *apBrightness[] = {"Darkest", "Dark gray", "Medium gray"};
	SegmentRow(pEditor, &View, Tr("Interface"), &g_Config.m_XcEditorBrightness, apBrightness, 3, s_aIds[2]);
	const char *apSizes[] = {"Small", "Medium size", "Large"};
	SegmentRow(pEditor, &View, Tr("Transparency grid"), &g_Config.m_XcEditorCheckerSize, apSizes, 3, s_aIds[3]);
	const char *apShades[] = {"Dark tone", "Medium tone", "Light tone"};
	SegmentRow(pEditor, &View, Tr("Grid shade"), &g_Config.m_XcEditorCheckerShade, apShades, 3, s_aIds[4]);
	const char *apNoYes[] = {"No", "Yes"};
	SegmentRow(pEditor, &View, Tr("Rulers"), &g_Config.m_XcEditorRulers, apNoYes, 2, s_aIds[5]);
	SegmentRow(pEditor, &View, Tr("Precise cursor"), &g_Config.m_XcEditorPreciseCursor, apNoYes, 2, s_aIds[6]);

	View.HSplitTop(6.0f, nullptr, &View);
	CUIRect Row, Left, Right;
	View.HSplitTop(14.0f, &Row, &View);
	Row.VMargin(5.0f, &Row);
	Row.VSplitMid(&Left, &Right, 4.0f);
	static int s_GuideButton = 0, s_ClearButton = 0;
	if(pEditor->DoButton_Editor(&s_GuideButton, Tr("Show the guide"), 0, &Left, BUTTONFLAG_LEFT, nullptr))
	{
		pEditor->m_XcGuideOpen = true;
		pEditor->m_XcGuideStep = 0;
		return CUi::POPUP_CLOSE_CURRENT;
	}
	if(pEditor->DoButton_Editor(&s_ClearButton, Tr("Clear guides"), 0, &Right, BUTTONFLAG_LEFT, nullptr))
	{
		pEditor->m_vXcGuidesX.clear();
		pEditor->m_vXcGuidesY.clear();
	}
	return CUi::POPUP_KEEP_OPEN;
}

void CEditor::XcOpenPreferences()
{
	static SPopupMenuId s_PopupPreferencesId;
	const CUIRect Screen = *Ui()->Screen();
	Ui()->DoPopupMenu(&s_PopupPreferencesId, Screen.w / 2.0f - 160.0f, Screen.h / 2.0f - 90.0f, 320.0f, 150.0f, this, XcPopupPreferences);
}

// world <-> ui units of the selected group, the rulers follow the layer you work on
static bool GroupMapping(CEditor *pEditor, CScreenRect *pRect)
{
	std::shared_ptr<CLayerGroup> pGroup = pEditor->Map()->SelectedGroup();
	if(!pGroup)
		return false;
	*pRect = pGroup->Mapping();
	return pRect->Width() > 0.0f && pRect->Height() > 0.0f;
}

void CEditor::XcRenderRulers(CUIRect View)
{
	// Ctrl+K opens the preferences everywhere in the editor
	if(XcEditorStyle::Enabled() && m_Dialog == DIALOG_NONE && CLineInput::GetActiveInput() == nullptr && Input()->ModifierIsPressed() && Input()->KeyPress(KEY_K))
		XcOpenPreferences();
	if(!XcEditorStyle::Enabled() || m_ShowPicker || m_Mode != MODE_LAYERS)
		return;
	CScreenRect Mapping(0.0f, 0.0f, 0.0f, 0.0f);
	if(!GroupMapping(this, &Mapping))
		return;
	const CUIRect Screen = *Ui()->Screen();
	const auto ToUiX = [&](float WorldX) { return (WorldX - Mapping.m_TopLeft.x) / Mapping.Width() * Screen.w; };
	const auto ToUiY = [&](float WorldY) { return (WorldY - Mapping.m_TopLeft.y) / Mapping.Height() * Screen.h; };
	const auto ToWorldX = [&](float UiX) { return Mapping.m_TopLeft.x + UiX / Screen.w * Mapping.Width(); };
	const auto ToWorldY = [&](float UiY) { return Mapping.m_TopLeft.y + UiY / Screen.h * Mapping.Height(); };
	const ColorRGBA GuideColor(0.24f, 0.84f, 0.95f, 0.85f); // cyan like Photoshop guides

	// guides across the map view
	Ui()->ClipEnable(&View);
	for(float x : m_vXcGuidesX)
		CUIRect{ToUiX(x), View.y, 1.0f, View.h}.Draw(GuideColor, IGraphics::CORNER_NONE, 0.0f);
	for(float y : m_vXcGuidesY)
		CUIRect{View.x, ToUiY(y), View.w, 1.0f}.Draw(GuideColor, IGraphics::CORNER_NONE, 0.0f);
	Ui()->ClipDisable();

	// precise cursor: a thin cross on the mouse inside the map view
	if(g_Config.m_XcEditorPreciseCursor && Ui()->MouseInside(&View))
	{
		const float mx = Ui()->MouseX(), my = Ui()->MouseY();
		const ColorRGBA Cross(1.0f, 1.0f, 1.0f, 0.8f);
		CUIRect{mx - 8.0f, my - 0.5f, 6.0f, 1.0f}.Draw(Cross, IGraphics::CORNER_NONE, 0.0f);
		CUIRect{mx + 2.0f, my - 0.5f, 6.0f, 1.0f}.Draw(Cross, IGraphics::CORNER_NONE, 0.0f);
		CUIRect{mx - 0.5f, my - 8.0f, 1.0f, 6.0f}.Draw(Cross, IGraphics::CORNER_NONE, 0.0f);
		CUIRect{mx - 0.5f, my + 2.0f, 1.0f, 6.0f}.Draw(Cross, IGraphics::CORNER_NONE, 0.0f);
	}

	if(!g_Config.m_XcEditorRulers)
		return;
	const float Thick = 12.0f;
	CUIRect Top = {View.x + Thick, View.y, View.w - Thick, Thick};
	CUIRect Left = {View.x, View.y + Thick, Thick, View.h - Thick};
	const ColorRGBA Back = XcEditorStyle::Panel().WithAlpha(0.92f);
	Top.Draw(Back, IGraphics::CORNER_NONE, 0.0f);
	Left.Draw(Back, IGraphics::CORNER_NONE, 0.0f);
	CUIRect{View.x, View.y, Thick, Thick}.Draw(XcEditorStyle::Window(), IGraphics::CORNER_NONE, 0.0f);

	// ticks in tiles, the step grows when zoomed out so the numbers do not overlap
	const float UiPerTile = 32.0f / Mapping.Width() * Screen.w;
	static const int s_aSteps[] = {1, 2, 5, 10, 20, 50, 100, 200, 500};
	int Step = s_aSteps[std::size(s_aSteps) - 1];
	for(int Candidate : s_aSteps)
		if(Candidate * UiPerTile >= 28.0f)
		{
			Step = Candidate;
			break;
		}
	const ColorRGBA Tick(1.0f, 1.0f, 1.0f, 0.35f);
	const ColorRGBA Text(0.75f, 0.77f, 0.8f, 1.0f);
	TextRender()->TextColor(Text);
	Ui()->ClipEnable(&Top);
	for(int Tile = (int)std::floor(ToWorldX(Top.x) / 32.0f / Step) * Step; Tile * 32.0f <= ToWorldX(Top.x + Top.w); Tile += Step)
	{
		const float x = ToUiX(Tile * 32.0f);
		CUIRect{x, Top.y + Thick * 0.4f, 1.0f, Thick * 0.6f}.Draw(Tick, IGraphics::CORNER_NONE, 0.0f);
		char aNum[16];
		str_format(aNum, sizeof(aNum), "%d", Tile);
		TextRender()->Text(x + 2.0f, Top.y, 6.0f, aNum);
	}
	Ui()->ClipDisable();
	Ui()->ClipEnable(&Left);
	for(int Tile = (int)std::floor(ToWorldY(Left.y) / 32.0f / Step) * Step; Tile * 32.0f <= ToWorldY(Left.y + Left.h); Tile += Step)
	{
		const float y = ToUiY(Tile * 32.0f);
		CUIRect{Left.x + Thick * 0.4f, y, Thick * 0.6f, 1.0f}.Draw(Tick, IGraphics::CORNER_NONE, 0.0f);
		char aNum[16];
		str_format(aNum, sizeof(aNum), "%d", Tile);
		TextRender()->Text(Left.x + 1.0f, y + 1.0f, 5.5f, aNum);
	}
	Ui()->ClipDisable();
	TextRender()->TextColor(TextRender()->DefaultTextColor());

	// guides: drag one out of a ruler, drag a guide back into the ruler to remove it
	static int s_TopRuler = 0, s_LeftRuler = 0;
	static int s_DragAxis = -1; // 0 = vertical guide (x), 1 = horizontal guide (y)
	static int s_DragIndex = -1;
	const float mx = Ui()->MouseX(), my = Ui()->MouseY();
	if(s_DragAxis < 0)
	{
		if(Ui()->DoButtonLogic(&s_TopRuler, 0, &Top, BUTTONFLAG_LEFT) || (Ui()->CheckActiveItem(&s_TopRuler)))
		{
			s_DragAxis = 1;
			m_vXcGuidesY.push_back(ToWorldY(my));
			s_DragIndex = (int)m_vXcGuidesY.size() - 1;
		}
		else if(Ui()->DoButtonLogic(&s_LeftRuler, 0, &Left, BUTTONFLAG_LEFT) || (Ui()->CheckActiveItem(&s_LeftRuler)))
		{
			s_DragAxis = 0;
			m_vXcGuidesX.push_back(ToWorldX(mx));
			s_DragIndex = (int)m_vXcGuidesX.size() - 1;
		}
		else if(Ui()->MouseInside(&View) && Ui()->MouseButtonClicked(0) && m_XcTool == XC_TOOL_HAND)
		{
			// with the hand tool existing guides can be grabbed and moved
			for(size_t i = 0; i < m_vXcGuidesX.size() && s_DragAxis < 0; i++)
				if(absolute(ToUiX(m_vXcGuidesX[i]) - mx) < 3.0f)
				{
					s_DragAxis = 0;
					s_DragIndex = (int)i;
				}
			for(size_t i = 0; i < m_vXcGuidesY.size() && s_DragAxis < 0; i++)
				if(absolute(ToUiY(m_vXcGuidesY[i]) - my) < 3.0f)
				{
					s_DragAxis = 1;
					s_DragIndex = (int)i;
				}
		}
	}
	if(s_DragAxis >= 0)
	{
		std::vector<float> &vGuides = s_DragAxis == 0 ? m_vXcGuidesX : m_vXcGuidesY;
		if(s_DragIndex < 0 || s_DragIndex >= (int)vGuides.size())
			s_DragAxis = -1;
		else if(Ui()->MouseButton(0))
		{
			// snap to whole tiles like Photoshop snaps to the pixel grid
			const float World = s_DragAxis == 0 ? ToWorldX(mx) : ToWorldY(my);
			vGuides[s_DragIndex] = std::round(World / 32.0f) * 32.0f;
			m_CursorType = s_DragAxis == 0 ? CURSOR_RESIZE_H : CURSOR_RESIZE_V;
			char aTip[64];
			str_format(aTip, sizeof(aTip), "%s: %d", Tr("Guide"), (int)std::round(vGuides[s_DragIndex] / 32.0f));
			str_copy(m_aTooltip, aTip);
		}
		else
		{
			// dropped on a ruler or outside the view: the guide goes away
			const bool Remove = !Ui()->MouseInside(&View) || Ui()->MouseInside(&Top) || Ui()->MouseInside(&Left);
			if(Remove)
				vGuides.erase(vGuides.begin() + s_DragIndex);
			if(Ui()->CheckActiveItem(&s_TopRuler) || Ui()->CheckActiveItem(&s_LeftRuler))
				Ui()->SetActiveItem(nullptr);
			s_DragAxis = -1;
			s_DragIndex = -1;
		}
	}
}

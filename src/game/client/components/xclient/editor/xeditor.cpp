// The new map editor interface (see xeditor.h): the frame of the editor and the panels around the map.
#include "xeditor.h"

#include "editor_style.h"

#include <base/math.h>
#include <base/str.h>

#include <engine/font_icons.h>
#include <engine/graphics.h>
#include <engine/keys.h>
#include <engine/shared/config.h>

#include <game/client/components/xclient/hud/xc_hud.h>
#include <game/client/components/xclient/ui/lang.h>
#include <game/client/components/xclient/ui/theme.h>
#include <game/client/components/xclient/ui/widgets.h>
#include <game/client/ui.h>
#include <game/editor/editor.h>
#include <game/editor/editor_actions.h>
#include <game/editor/mapitems/image.h>
#include <game/editor/mapitems/layer_group.h>
#include <game/editor/mapitems/sound.h>

#include <engine/external/imgui/imgui.h>
#include <engine/external/imgui/imgui_internal.h>

#include <algorithm>
#include <cfloat>
#include <functional>
#include <optional>

using namespace XcUi;

namespace
{
// Font Awesome 6 icons that DDNet has no constants for
const char *const ICON_TILES = ""; // table cells
const char *const ICON_QUADS = ""; // shapes
const char *const ICON_SOUNDS = ""; // volume high
const char *const ICON_GAME = ""; // gamepad
const char *const ICON_FRONT = ""; // layer group
const char *const ICON_TELE = ""; // bolt
const char *const ICON_SPEEDUP = ""; // forward
const char *const ICON_SWITCH = ""; // toggle on
const char *const ICON_TUNE = ""; // sliders
const char *const ICON_SAVE = ""; // floppy disk
const char *const ICON_ZOOM_IN = "";
const char *const ICON_ZOOM_OUT = "";
const char *const ICON_EXPAND = "";

const char *LayerIcon(const CEditorMap *pMap, const CLayer *pLayer)
{
	if(pLayer->m_Type == LAYERTYPE_QUADS)
		return ICON_QUADS;
	if(pLayer->m_Type == LAYERTYPE_SOUNDS)
		return ICON_SOUNDS;
	if(pLayer == pMap->m_pGameLayer.get())
		return ICON_GAME;
	if(pLayer == pMap->m_pFrontLayer.get())
		return ICON_FRONT;
	if(pLayer == pMap->m_pTeleLayer.get())
		return ICON_TELE;
	if(pLayer == pMap->m_pSpeedupLayer.get())
		return ICON_SPEEDUP;
	if(pLayer == pMap->m_pSwitchLayer.get())
		return ICON_SWITCH;
	if(pLayer == pMap->m_pTuneLayer.get())
		return ICON_TUNE;
	return ICON_TILES;
}

ImVec4 LayerColor(const CEditorMap *pMap, const CLayer *pLayer)
{
	// the special layers of the game group stand out like in the tile picker
	if(pLayer->IsEntitiesLayer())
		return ImVec4(1.0f, 0.72f, 0.3f, 1.0f);
	if(pLayer->m_Type == LAYERTYPE_QUADS)
		return ImVec4(0.55f, 0.75f, 1.0f, 1.0f);
	if(pLayer->m_Type == LAYERTYPE_SOUNDS)
		return ImVec4(0.75f, 0.6f, 1.0f, 1.0f);
	return Theme().m_TextSecondary;
}

bool MouseIn(ImVec2 Min, ImVec2 Max)
{
	const ImVec2 Mouse = ImGui::GetIO().MousePos;
	return Mouse.x >= Min.x && Mouse.x < Max.x && Mouse.y >= Min.y && Mouse.y < Max.y;
}

// a panel window at a fixed place
bool BeginPanel(const char *pId, ImVec2 Pos, ImVec2 Size, ImVec4 Background, ImGuiWindowFlags ExtraFlags = 0)
{
	ImGui::SetNextWindowPos(Pos);
	ImGui::SetNextWindowSize(Size);
	ImGui::PushStyleColor(ImGuiCol_WindowBg, Background);
	ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
	ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
	const bool Open = ImGui::Begin(pId, nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBringToFrontOnFocus | ExtraFlags);
	ImGui::PopStyleVar(2);
	ImGui::PopStyleColor();
	return Open;
}
} // namespace

void CXcEditorUi::Init(CEditor *pEditor)
{
	m_pEditor = pEditor;
	m_Gui.Init(pEditor->Graphics(), pEditor->Input(), pEditor->Storage());
}

float CXcEditorUi::Scale() const
{
	// like the menu: designed for 1080p, a bit more compact than the menu since the editor is dense
	const float Size = g_Config.m_XcUiSize / 100.0f;
	return std::clamp(m_pEditor->Graphics()->ScreenHeight() / 1080.0f, 0.75f, 3.0f) * Size * 0.9f;
}

float CXcEditorUi::UiToPixels() const
{
	return m_pEditor->Graphics()->ScreenHeight() / std::max(m_pEditor->Ui()->Screen()->h, 1.0f);
}

void CXcEditorUi::OnMouseMove(vec2 MousePos)
{
	if(m_Gui.IsReady())
		m_Gui.OnMouseMove(MousePos);
}

void CXcEditorUi::OnInput(const IInput::CEvent &Event)
{
	if(m_Gui.IsReady())
		m_Gui.OnInput(Event);
}

void CXcEditorUi::UpdateFonts()
{
	const float FontScale = std::round(Scale() * 20.0f) / 20.0f;
	if(m_Gui.FontFamily() != g_Config.m_XcUiFont || m_Gui.FontScale() != FontScale)
		m_Gui.LoadFonts(g_Config.m_XcUiFont, FontScale);
	const CImGuiBackend::SFonts &BackendFonts = m_Gui.Fonts();
	SFonts Fonts;
	Fonts.m_pBody = BackendFonts.m_pBody;
	Fonts.m_pLabel = BackendFonts.m_pLabel;
	Fonts.m_pTitle = BackendFonts.m_pTitle;
	Fonts.m_pHeader = BackendFonts.m_pHeader;
	Fonts.m_pSmall = BackendFonts.m_pSmall;
	Fonts.m_pIcons = BackendFonts.m_pIcons;
	SetFonts(Fonts);
	SetScale(FontScale);
	SetTheme(g_Config.m_XcUiTheme, g_Config.m_XcUiAccent);
	ApplyStyle();
	// menus and context menus are solid cards, like in Figma
	const STheme &t = Theme();
	ImGuiStyle &Style = ImGui::GetStyle();
	Style.Colors[ImGuiCol_PopupBg] = ImVec4(t.m_Card.x, t.m_Card.y, t.m_Card.z, 1.0f);
	Style.Colors[ImGuiCol_Border] = t.m_Border;
	Style.Colors[ImGuiCol_Header] = ImVec4(t.m_Accent.x, t.m_Accent.y, t.m_Accent.z, 0.25f);
	Style.Colors[ImGuiCol_HeaderHovered] = ImVec4(t.m_Accent.x, t.m_Accent.y, t.m_Accent.z, 0.3f);
	Style.Colors[ImGuiCol_HeaderActive] = ImVec4(t.m_Accent.x, t.m_Accent.y, t.m_Accent.z, 0.45f);
	Style.Colors[ImGuiCol_Separator] = t.m_Border;
	Style.PopupRounding = S(10.0f);
	Style.PopupBorderSize = 1.0f;
	Style.WindowPadding = ImVec2(S(8.0f), S(8.0f));
	Style.ItemSpacing = ImVec2(S(8.0f), S(6.0f));
	Style.SelectableTextAlign = ImVec2(0.0f, 0.5f);
}

void CXcEditorUi::HandleTyping()
{
	// typing into an ImGui field: the editor sees an active line input and leaves the keys alone
	// a dialog keeps the editor hotkeys quiet too
	const bool Typing = ImGui::GetIO().WantTextInput || DialogOpen();
	if(Typing && !m_TypingInput.IsActive())
		m_TypingInput.Activate(EInputPriority::UI);
	else if(!Typing && m_TypingInput.IsActive())
		m_TypingInput.Deactivate();
	if(Typing)
		m_TypingInput.XcMarkRendered(); // DDNet deactivates inputs that were not drawn this frame
	m_Typing = Typing;
}

bool CXcEditorUi::IconButton(const char *pId, const char *pIcon, const char *pTooltip, bool Active, bool Enabled, float Size)
{
	const STheme &t = Theme();
	if(Size <= 0.0f)
		Size = S(30.0f);
	const ImVec2 Min = ImGui::GetCursorScreenPos();
	const ImVec2 Max(Min.x + Size, Min.y + Size);
	ImGui::BeginDisabled(!Enabled);
	const bool Clicked = ImGui::InvisibleButton(pId, ImVec2(Size, Size));
	const bool Hovered = ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled);
	ImGui::EndDisabled();
	ImDrawList *pDraw = ImGui::GetWindowDrawList();
	const float Hover = Animate(ImGui::GetID(pId), Hovered && Enabled ? 1.0f : 0.0f, 18.0f);
	if(Active)
		pDraw->AddRectFilled(Min, Max, Col(t.m_Accent), S(8.0f));
	else if(Hover > 0.01f)
		pDraw->AddRectFilled(Min, Max, Col(t.m_Track, Hover), S(8.0f));
	ImFont *pFont = Fonts().m_pLabel;
	const float FontSize = pFont->FontSize * 1.05f;
	const ImVec2 TextSize = pFont->CalcTextSizeA(FontSize, FLT_MAX, 0.0f, pIcon);
	const ImVec4 IconColor = Active ? t.m_OnAccent : (Enabled ? (Hovered ? t.m_Text : t.m_TextSecondary) : t.m_TextMuted);
	pDraw->AddText(pFont, FontSize, ImVec2(Min.x + (Size - TextSize.x) * 0.5f, Min.y + (Size - TextSize.y) * 0.5f), Col(IconColor, Enabled ? 1.0f : 0.5f), pIcon);
	if(Hovered && pTooltip && pTooltip[0])
		ImGui::SetTooltip("%s", pTooltip);
	return Clicked && Enabled;
}

bool CXcEditorUi::QuickActionItem(CQuickAction &Action, const char *pLabel, const char *pShortcut)
{
	if(ImGui::MenuItem(pLabel ? pLabel : Tr(Action.Label()), pShortcut, Action.Active() || Action.Color() == 1, !Action.Disabled()))
	{
		Action.Call();
		return true;
	}
	if(ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled | ImGuiHoveredFlags_DelayNormal))
		ImGui::SetTooltip("%s", Action.Description());
	return false;
}

void CXcEditorUi::Render()
{
	CEditor &E = *m_pEditor;
	const float k = UiToPixels(); // pixels per editor UI unit

	// ---- the start of the classic frame
	E.Graphics()->Clear(0.0f, 0.0f, 0.0f);
	E.Ui()->MapScreen();
	E.m_CursorType = CEditor::CURSOR_NORMAL;
	str_copy(E.m_aTooltip, "");
	E.UpdateBrushPicker();
	const CUIRect Screen = *E.Ui()->Screen();

	// ---- layout in pixels, then the map view in UI units
	const float s = Scale();
	const bool Gui = E.m_GuiActive;
	m_TopHeight = Gui ? std::round(48.0f * s) : 0.0f;
	m_StatusHeight = Gui ? std::round(26.0f * s) : 0.0f;
	m_LeftWidth = Gui ? std::round(270.0f * s) : 0.0f;
	m_RightWidth = Gui && m_ShowProperties ? std::round(290.0f * s) : 0.0f;
	const bool Dock = Gui && !E.m_ShowPicker && E.m_ActiveExtraEditor != CEditor::EXTRAEDITOR_NONE;
	const float DockHeaderUi = 0.0f;
	float DockUi = Dock ? std::clamp(E.m_aExtraEditorSplits[(int)E.m_ActiveExtraEditor], 100.0f, Screen.h * 0.7f) : 0.0f;
	m_DockHeight = DockUi * k;
	m_View = {m_LeftWidth / k, m_TopHeight / k, Screen.w - (m_LeftWidth + m_RightWidth) / k, Screen.h - (m_TopHeight + m_StatusHeight) / k - DockUi};
	m_Dock = {m_View.x, m_View.y + m_View.h + DockHeaderUi, m_View.w, DockUi - DockHeaderUi};
	E.m_XcWorkArea = m_View;
	E.m_XcFloatingTools = {0.0f, 0.0f, 0.0f, 0.0f};

	// the guide points at the new panels
	E.m_aXcZones[CEditor::XC_ZONE_MENU] = {0.0f, 0.0f, Screen.w * 0.45f, m_TopHeight / k};
	E.m_aXcZones[CEditor::XC_ZONE_TABS] = {Screen.w * 0.3f, 0.0f, Screen.w * 0.4f, m_TopHeight / k};
	E.m_aXcZones[CEditor::XC_ZONE_TOOLBAR] = {m_View.x + m_View.w * 0.5f - 160.0f, m_View.y + m_View.h - 40.0f, 320.0f, 34.0f};
	E.m_aXcZones[CEditor::XC_ZONE_TOOLBOX] = {0.0f, m_TopHeight / k, m_LeftWidth / k, m_View.h};
	E.m_aXcZones[CEditor::XC_ZONE_VIEW] = m_View;
	E.m_aXcZones[CEditor::XC_ZONE_STATUS] = {0.0f, Screen.h - m_StatusHeight / k, Screen.w, m_StatusHeight / k};
	E.m_aXcZones[CEditor::XC_ZONE_TOOLS] = E.m_aXcZones[CEditor::XC_ZONE_TOOLBAR];

	// ---- the map
	E.RenderBackground(m_View, E.m_CheckerTexture, XcEditorStyle::CheckerSize(), XcEditorStyle::CheckerBrightness());
	if(E.m_Mode == MODE_LAYERS)
		E.MapView()->Render(m_View);
	else if(E.m_Mode == MODE_IMAGES)
		E.RenderSelectedImage(m_View);

	if(E.m_Dialog == DIALOG_NONE && CLineInput::GetActiveInput() == nullptr)
	{
		// undo and redo
		if(E.Ui()->CheckActiveItem(nullptr))
		{
			if(E.Input()->KeyPress(KEY_Z) && E.Input()->ModifierIsPressed() && !E.Input()->ShiftIsPressed())
				E.ActiveHistory().Undo();
			if((E.Input()->KeyPress(KEY_Y) && E.Input()->ModifierIsPressed()) || (E.Input()->KeyPress(KEY_Z) && E.Input()->ModifierIsPressed() && E.Input()->ShiftIsPressed()))
				E.ActiveHistory().Redo();
		}
		// saved brushes on the number keys
		for(int i = KEY_1; i <= KEY_0; i++)
		{
			if(!E.Input()->KeyPress(i))
				continue;
			const int Slot = i - KEY_1;
			if(E.Input()->ModifierIsPressed() && !E.m_pBrush->IsEmpty())
				E.m_apSavedBrushes[Slot] = std::make_shared<CLayerGroup>(*E.m_pBrush);
			else if(E.m_apSavedBrushes[Slot])
				E.m_pBrush = std::make_shared<CLayerGroup>(*E.m_apSavedBrushes[Slot]);
		}
	}

	// the classic tool bars stay out of sight but keep all their hotkeys
	{
		CUIRect Hidden = {-1000.0f, -1000.0f, 60.0f, 40.0f};
		E.DoMapTabs(Hidden);
		if(E.m_Mode == MODE_LAYERS)
			E.DoToolbarLayers(Hidden);
		else if(E.m_Mode == MODE_IMAGES)
			E.DoToolbarImages(Hidden);
		else if(E.m_Mode == MODE_SOUNDS)
			E.DoToolbarSounds(Hidden);
		E.XcHandleToolHotkeys();
	}

	if(E.m_Dialog == DIALOG_NONE)
	{
		const bool ModPressed = E.Input()->ModifierIsPressed();
		const bool ShiftPressed = E.Input()->ShiftIsPressed();
		const bool AltPressed = E.Input()->AltIsPressed();
		if(CLineInput::GetActiveInput() == nullptr && E.Input()->KeyPress(KEY_A) && ModPressed)
			E.m_QuickActionAppendMap.Call();
		if(E.Input()->KeyPress(KEY_N) && ModPressed)
			E.m_QuickActionNewMap.Call();
		if((E.Input()->KeyPress(KEY_O) || E.Input()->KeyPress(KEY_L)) && ModPressed)
		{
			if(ShiftPressed)
			{
				if(!E.m_QuickActionLoadIngameMap.Disabled())
					E.m_QuickActionLoadIngameMap.Call();
			}
			else
				E.m_QuickActionLoadMap.Call();
		}
		if(E.Input()->KeyPress(KEY_S) && ModPressed && ShiftPressed && AltPressed)
			E.m_QuickActionSaveCopy.Call();
		else if(E.Input()->KeyPress(KEY_S) && ModPressed && ShiftPressed)
			E.m_QuickActionSaveAs.Call();
		else if(E.Input()->KeyPress(KEY_S) && ModPressed)
			E.m_QuickActionSave.Call();
	}

	if(Gui && E.m_Mode == MODE_LAYERS)
		E.XcRenderRulers(m_View);
	// Ctrl+K: the command palette
	if(E.m_Dialog == DIALOG_NONE && CLineInput::GetActiveInput() == nullptr && E.Input()->ModifierIsPressed() && E.Input()->KeyPress(KEY_K))
	{
		m_PaletteOpen = true;
		m_PaletteFocus = true;
		m_aPaletteSearch[0] = '\0';
		m_PaletteSelected = 0;
	}

	// ---- the new panels
	if(m_Gui.IsReady())
	{
		m_Gui.MakeCurrent();
		const SFonts OldFonts = Fonts();
		const float OldScale = XcUi::Scale();
		UpdateFonts();
		// the classic editor owns the mouse while it drags something, a popup of it is hovered or a dialog is open
		const bool ClassicBusy = (E.m_Dialog != DIALOG_NONE && E.m_Dialog != DIALOG_FILE) || E.Ui()->IsPopupHovered() || (!m_WantsMouse && !E.Ui()->CheckActiveItem(nullptr));
		const vec2 Mouse = ClassicBusy ? vec2(-100000.0f, -100000.0f) : E.Ui()->UpdatedMousePos();
		m_Gui.NewFrame(std::min(E.Client()->RenderFrameTime(), 0.1f), Mouse);

		if(Gui)
		{
			RenderTopBar();
			RenderLeftPanel();
			if(m_ShowProperties)
				RenderRightPanel();
			if(Dock)
				RenderDockFrame();
			RenderStatusBar();
		}
		if(Gui && E.m_Mode == MODE_LAYERS && !E.m_ShowPicker)
			RenderCapsule();
		if(m_PaletteOpen)
			RenderPalette();
		RenderMapDetails();
		RenderFileDialog();
		RenderEventDialog();
		RenderMessages();

		const ImGuiIO &Io = ImGui::GetIO();
		m_WantsMouse = Io.WantCaptureMouse && !ClassicBusy;
		HandleTyping();
		// Esc closes the menus and stops typing before it can close the editor
		if(ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopupId) || m_Typing)
			E.Ui()->ConsumeHotkey(CUi::HOTKEY_ESCAPE);
		m_Gui.Render();
		SetFonts(OldFonts);
		SetScale(OldScale);
	}
	E.Ui()->MapScreen();

	// ---- the classic extra editors in the dock under the map
	if(Dock)
	{
		if(E.m_ActiveExtraEditor == CEditor::EXTRAEDITOR_ENVELOPES)
			E.m_EnvelopeEditor.Render(m_Dock);
		else if(E.m_ActiveExtraEditor == CEditor::EXTRAEDITOR_SERVER_SETTINGS)
		{
			static bool s_ShowServerSettingsLast = false;
			E.RenderServerSettingsEditor(m_Dock, s_ShowServerSettingsLast);
			s_ShowServerSettingsLast = true;
		}
		else if(E.m_ActiveExtraEditor == CEditor::EXTRAEDITOR_HISTORY)
			E.RenderEditorHistory(m_Dock);
	}

	E.RenderPressedKeys(m_View);
	E.RenderSavingIndicator(m_View);

	if(E.m_Dialog == DIALOG_MAPSETTINGS_ERROR)
	{
		static int s_NullUiTarget = 0;
		E.Ui()->SetHotItem(&s_NullUiTarget);
		E.RenderMapSettingsErrorDialog();
	}
	if(E.m_PopupEventActivated)
	{
		// the question opens in a dialog of the new interface
		m_EventOpen = true;
		E.m_PopupEventActivated = false;
		E.m_PopupEventWasActivated = true;
	}

	// zoom with the wheel and the keypad over the map
	if(E.m_Dialog == DIALOG_NONE && !E.Ui()->IsPopupHovered() && E.Ui()->MouseInside(&m_View) && !m_WantsMouse)
	{
		if(CLineInput::GetActiveInput() == nullptr)
		{
			if(E.Input()->KeyPress(KEY_KP_MINUS))
				E.MapView()->Zoom()->ScaleValue(1.331f);
			if(E.Input()->KeyPress(KEY_KP_PLUS))
				E.MapView()->Zoom()->ScaleValue(1.0f / 1.331f);
			if(E.Input()->KeyPress(KEY_KP_MULTIPLY))
				E.MapView()->ResetZoom();
		}
		if(E.m_pBrush->IsEmpty() || !E.Input()->ShiftIsPressed())
		{
			if(E.Input()->KeyPress(KEY_MOUSE_WHEEL_DOWN))
				E.MapView()->Zoom()->ScaleValue(1.1f);
			if(E.Input()->KeyPress(KEY_MOUSE_WHEEL_UP))
				E.MapView()->Zoom()->ScaleValue(1.0f / 1.1f);
		}
		if(!E.m_pBrush->IsEmpty())
		{
			bool HasTileAdjustLayer = false;
			bool HasSpeedupLayer = false;
			for(const auto &pLayer : E.m_pBrush->m_vpLayers)
			{
				if(pLayer->m_Type != LAYERTYPE_TILES)
					continue;
				std::shared_ptr<CLayerTiles> pTiles = std::static_pointer_cast<CLayerTiles>(pLayer);
				HasTileAdjustLayer |= pTiles->m_HasTele || pTiles->m_HasSwitch || pTiles->m_HasTune;
				HasSpeedupLayer |= pTiles->m_HasSpeedup;
			}
			if(HasTileAdjustLayer)
				str_copy(E.m_aTooltip, Tr("Shift + wheel changes the tile numbers, Ctrl+F picks the first unused number"));
			else if(HasSpeedupLayer)
				str_copy(E.m_aTooltip, Tr("Shift + wheel changes the angle"));
			if(E.Input()->ShiftIsPressed())
			{
				const int AdjustModifiers = E.Input()->ModifierIsPressed() ? (E.Input()->AltIsPressed() ? 2 : 1) : 0;
				if(E.Input()->KeyPress(KEY_MOUSE_WHEEL_DOWN))
					E.AdjustBrushSpecialTiles(false, AdjustModifiers, -1);
				if(E.Input()->KeyPress(KEY_MOUSE_WHEEL_UP))
					E.AdjustBrushSpecialTiles(false, AdjustModifiers, 1);
			}
			if(E.Input()->ModifierIsPressed() && E.Input()->KeyPress(KEY_F))
				E.AdjustBrushSpecialTiles(true, 0, 0);
		}
	}

	E.m_Prompt.Render();
	E.m_FontTyper.Render();
	E.XcRenderGuide();
	E.MapView()->UpdateZoom();

	if(E.m_XcGuideOpen && E.Ui()->ConsumeHotkey(CUi::HOTKEY_ESCAPE))
		E.m_XcGuideOpen = false;
	if(E.m_ColorPipetteActive && E.Ui()->ConsumeHotkey(CUi::HOTKEY_ESCAPE))
		E.m_ColorPipetteActive = false;

	E.Ui()->RenderPopupMenus();
	E.FreeDynamicPopupMenus();
	E.UpdateColorPipette();

	if(E.m_Dialog == DIALOG_NONE && !E.m_PopupEventActivated && !DialogOpen() && E.Ui()->ConsumeHotkey(CUi::HOTKEY_ESCAPE))
	{
		E.OnClose();
		g_Config.m_ClEditor = 0;
	}

	E.Ui()->RenderBackButton();
	E.RenderMousePointer();
}

// ------------------------------------------------------------------------------------------------
// top bar: logo, menus, map tabs, undo / redo, test and save

void CXcEditorUi::RenderTopBar()
{
	CEditor &E = *m_pEditor;
	const STheme &t = Theme();
	const ImVec2 Display = ImGui::GetIO().DisplaySize;
	if(!BeginPanel("##xe_top", ImVec2(0.0f, 0.0f), ImVec2(Display.x, m_TopHeight), ImVec4(t.m_Panel.x, t.m_Panel.y, t.m_Panel.z, 1.0f)))
	{
		ImGui::End();
		return;
	}
	ImDrawList *pDraw = ImGui::GetWindowDrawList();
	pDraw->AddLine(ImVec2(0.0f, m_TopHeight - 1.0f), ImVec2(Display.x, m_TopHeight - 1.0f), Col(t.m_Border));

	// logo
	const float Mid = m_TopHeight * 0.5f;
	const float x0 = S(16.0f);
	DrawLogo(pDraw, ImVec2(x0 + S(11.0f), Mid), S(24.0f), t.m_Accent);
	ImFont *pTitle = Fonts().m_pTitle;
	pDraw->AddText(pTitle, pTitle->FontSize, ImVec2(x0 + S(30.0f), Mid - pTitle->FontSize * 0.5f), Col(t.m_Text), Tr("Editor"));
	const float MenusX = x0 + S(34.0f) + pTitle->CalcTextSizeA(pTitle->FontSize, FLT_MAX, 0.0f, Tr("Editor")).x + S(14.0f);
	const float MenusEnd = RenderMenus(MenusX, Mid);

	// right side: undo, redo, test, save
	const float ButtonSize = S(32.0f);
	float Right = Display.x - S(12.0f);
	const char *pSave = Tr("Save");
	ImFont *pLabel = Fonts().m_pLabel;
	const float SaveWidth = pLabel->CalcTextSizeA(pLabel->FontSize, FLT_MAX, 0.0f, pSave).x + S(40.0f);
	Right -= SaveWidth;
	{
		ImGui::SetCursorScreenPos(ImVec2(Right, Mid - S(16.0f)));
		const ImVec2 Min = ImGui::GetCursorScreenPos();
		const ImVec2 Max(Min.x + SaveWidth, Min.y + S(32.0f));
		const bool Clicked = ImGui::InvisibleButton("##save", ImVec2(SaveWidth, S(32.0f)));
		const bool Hovered = ImGui::IsItemHovered();
		pDraw->AddRectFilled(Min, Max, Col(t.m_Accent, Hovered ? 1.0f : 0.9f), S(8.0f));
		const char *pIcon = ICON_SAVE;
		pDraw->AddText(pLabel, pLabel->FontSize, ImVec2(Min.x + S(12.0f), Mid - pLabel->FontSize * 0.5f), Col(t.m_OnAccent), pIcon);
		pDraw->AddText(pLabel, pLabel->FontSize, ImVec2(Min.x + S(30.0f), Mid - pLabel->FontSize * 0.5f), Col(t.m_OnAccent), pSave);
		if(Hovered)
			ImGui::SetTooltip("%s", Tr("Save the map (Ctrl+S)"));
		if(Clicked)
			E.m_QuickActionSave.Call();
	}
	Right -= S(8.0f) + ButtonSize;
	ImGui::SetCursorScreenPos(ImVec2(Right, Mid - ButtonSize * 0.5f));
	if(IconButton("##test", FontIcon::PLAY, Tr("Play the map on a local server"), false, !E.m_QuickActionTestMapLocally.Disabled(), ButtonSize))
		E.m_QuickActionTestMapLocally.Call();
	Right -= S(14.0f) + ButtonSize;
	ImGui::SetCursorScreenPos(ImVec2(Right, Mid - ButtonSize * 0.5f));
	if(IconButton("##redo", FontIcon::REDO, Tr("Redo (Ctrl+Y)"), false, E.ActiveHistory().CanRedo(), ButtonSize))
		E.ActiveHistory().Redo();
	Right -= S(4.0f) + ButtonSize;
	ImGui::SetCursorScreenPos(ImVec2(Right, Mid - ButtonSize * 0.5f));
	if(IconButton("##undo", FontIcon::UNDO, Tr("Undo (Ctrl+Z)"), false, E.ActiveHistory().CanUndo(), ButtonSize))
		E.ActiveHistory().Undo();

	// map tabs in the middle
	RenderMapTabs(MenusEnd + S(24.0f), Right - S(16.0f), Mid);
	ImGui::End();
}

float CXcEditorUi::RenderMenus(float x, float Mid)
{
	CEditor &E = *m_pEditor;
	const STheme &t = Theme();
	ImFont *pLabel = Fonts().m_pLabel;
	const char *apMenus[] = {Tr("File"), Tr("Edit"), Tr("View"), Tr("Map"), Tr("Layer")};
	static const char *const s_apIds[] = {"##m_file", "##m_edit", "##m_view", "##m_map", "##m_layer"};
	ImDrawList *pDraw = ImGui::GetWindowDrawList();
	for(int i = 0; i < (int)std::size(apMenus); i++)
	{
		const float Width = pLabel->CalcTextSizeA(pLabel->FontSize, FLT_MAX, 0.0f, apMenus[i]).x + S(18.0f);
		const ImVec2 Min(x, Mid - S(14.0f));
		const ImVec2 Max(x + Width, Mid + S(14.0f));
		ImGui::SetCursorScreenPos(Min);
		const bool Clicked = ImGui::InvisibleButton(s_apIds[i], ImVec2(Width, S(28.0f)));
		const bool Open = ImGui::IsPopupOpen(s_apIds[i]);
		const bool Hovered = ImGui::IsItemHovered();
		if(Hovered || Open)
			pDraw->AddRectFilled(Min, Max, Col(t.m_Track, Open ? 1.0f : 0.7f), S(7.0f));
		pDraw->AddText(pLabel, pLabel->FontSize, ImVec2(x + S(9.0f), Mid - pLabel->FontSize * 0.5f), Col(Hovered || Open ? t.m_Text : t.m_TextSecondary), apMenus[i]);
		// another menu opens by hovering once one is open, like in desktop apps
		const bool AnyOpen = ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopupId);
		if(Clicked || (Hovered && AnyOpen && !Open))
		{
			ImGui::CloseCurrentPopup();
			ImGui::OpenPopup(s_apIds[i]);
		}
		ImGui::SetNextWindowPos(ImVec2(Min.x, m_TopHeight - S(4.0f)));
		if(ImGui::BeginPopup(s_apIds[i]))
		{
			ImGui::PushFont(Fonts().m_pBody);
			switch(i)
			{
			case 0: // file
				QuickActionItem(E.m_QuickActionNewMap, Tr("New map"), "Ctrl+N");
				QuickActionItem(E.m_QuickActionLoadMap, Tr("Open..."), "Ctrl+O");
				QuickActionItem(E.m_QuickActionLoadIngameMap, Tr("Open the map of the server"), "Ctrl+Shift+O");
				QuickActionItem(E.m_QuickActionAppendMap, Tr("Append a map..."), "Ctrl+A");
				ImGui::Separator();
				QuickActionItem(E.m_QuickActionSave, Tr("Save"), "Ctrl+S");
				QuickActionItem(E.m_QuickActionSaveAs, Tr("Save as..."), "Ctrl+Shift+S");
				QuickActionItem(E.m_QuickActionSaveCopy, Tr("Save a copy..."), "Ctrl+Shift+Alt+S");
				ImGui::Separator();
				QuickActionItem(E.m_QuickActionTestMapLocally, Tr("Play on a local server"));
				QuickActionItem(E.m_QuickActionCloseMap, Tr("Close the map"), "Ctrl+F4");
				ImGui::Separator();
				QuickActionItem(E.m_QuickActionShowHelp, Tr("Help (DDNet wiki)"), "F1");
				QuickActionItem(E.m_QuickActionExit, Tr("Exit the editor"), "Esc");
				break;
			case 1: // edit
				if(ImGui::MenuItem(Tr("Undo"), "Ctrl+Z", false, E.ActiveHistory().CanUndo()))
					E.ActiveHistory().Undo();
				if(ImGui::MenuItem(Tr("Redo"), "Ctrl+Y", false, E.ActiveHistory().CanRedo()))
					E.ActiveHistory().Redo();
				QuickActionItem(E.m_QuickActionHistory, Tr("History"));
				ImGui::Separator();
				QuickActionItem(E.m_QuickActionPipette, Tr("Color pipette"), "Ctrl+Shift+C");
				if(ImGui::MenuItem(Tr("Search for a command..."), "Ctrl+K"))
				{
					m_PaletteOpen = true;
					m_PaletteFocus = true;
					m_aPaletteSearch[0] = '\0';
					m_PaletteSelected = 0;
				}
				ImGui::Separator();
				if(ImGui::MenuItem(Tr("Preferences..."), "Ctrl+,"))
					E.XcOpenPreferences();
				break;
			case 2: // view
				QuickActionItem(E.m_QuickActionToggleGrid, Tr("Grid"), "Ctrl+G");
				if(ImGui::MenuItem(Tr("High detail"), "Ctrl+H", E.Map()->m_ShowDetail))
					E.Map()->m_ShowDetail = !E.Map()->m_ShowDetail;
				if(ImGui::MenuItem(Tr("Preview the in game zoom"), nullptr, E.Map()->m_PreviewZoom))
					E.Map()->m_PreviewZoom = !E.Map()->m_PreviewZoom;
				QuickActionItem(E.m_QuickActionProof, Tr("Proof borders"));
				QuickActionItem(E.m_QuickActionPreviewQuadEnvelopes, Tr("Show quad animation paths"));
				if(ImGui::BeginMenu(Tr("Tile numbers")))
				{
					QuickActionItem(E.m_QuickActionShowInfoOff, Tr("Off"));
					QuickActionItem(E.m_QuickActionShowInfoDec, Tr("Decimal"), "Ctrl+I");
					QuickActionItem(E.m_QuickActionShowInfoHex, Tr("Hexadecimal"), "Ctrl+Shift+I");
					ImGui::EndMenu();
				}
				ImGui::Separator();
				QuickActionItem(E.m_QuickActionZoomIn, Tr("Zoom in"), "Num +");
				QuickActionItem(E.m_QuickActionZoomOut, Tr("Zoom out"), "Num -");
				QuickActionItem(E.m_QuickActionResetZoom, Tr("Reset the zoom"), "Num *");
				QuickActionItem(E.m_QuickActionRefocus, Tr("Back to the map"), "Home");
				QuickActionItem(E.m_QuickActionGotoPosition, Tr("Go to a position..."));
				ImGui::Separator();
				if(ImGui::MenuItem(Tr("Properties panel"), nullptr, m_ShowProperties))
					m_ShowProperties = !m_ShowProperties;
				QuickActionItem(E.m_QuickActionEnvelopes, Tr("Envelopes"));
				QuickActionItem(E.m_QuickActionServerSettings, Tr("Server settings"));
				if(ImGui::MenuItem(Tr("Hide the interface"), "Tab"))
					E.m_GuiActive = false;
				break;
			case 3: // map
				QuickActionItem(E.m_QuickActionMapDetails, Tr("Map details..."));
				QuickActionItem(E.m_QuickActionServerSettings, Tr("Server settings"));
				QuickActionItem(E.m_QuickActionEnvelopes, Tr("Envelopes"));
				ImGui::Separator();
				QuickActionItem(E.m_QuickActionAddImage, Tr("Add an image..."));
				if(ImGui::MenuItem(Tr("Add a sound...")))
					E.m_FileBrowser.ShowFileDialog(IStorage::TYPE_ALL, CFileBrowser::EFileType::SOUND, "Add sound", "Add", "mapres", "", CEditor::AddSound, &E);
				if(ImGui::MenuItem(Tr("Add a GIF animation...")))
					E.m_FileBrowser.ShowFileDialog(IStorage::TYPE_ALL, CFileBrowser::EFileType::IMAGE, "Add GIF animation", "Add", "mapres", "", CEditor::XcCallbackAddGif, &E);
				if(ImGui::MenuItem(Tr("Animation presets...")))
					E.XcOpenAnimate();
				ImGui::Separator();
				if(ImGui::MenuItem(Tr("More tools...")))
				{
					static SPopupMenuId s_PopupMenuToolsId;
					E.Ui()->DoPopupMenu(&s_PopupMenuToolsId, Min.x / UiToPixels(), m_TopHeight / UiToPixels(), 200.0f, 200.0f, &E, CEditor::PopupMenuTools);
				}
				if(ImGui::MenuItem(Tr("Editor settings...")))
				{
					static SPopupMenuId s_PopupMenuSettingsId;
					E.Ui()->DoPopupMenu(&s_PopupMenuSettingsId, Min.x / UiToPixels(), m_TopHeight / UiToPixels(), 280.0f, 148.0f, &E, CEditor::PopupMenuSettings);
				}
				break;
			case 4: // layer
				QuickActionItem(E.m_QuickActionAddGroup, Tr("New group"));
				QuickActionItem(E.m_QuickActionAddTileLayer, Tr("New tile layer"));
				QuickActionItem(E.m_QuickActionAddQuadsLayer, Tr("New quads layer"));
				QuickActionItem(E.m_QuickActionAddSoundLayer, Tr("New sound layer"));
				if(ImGui::BeginMenu(Tr("Game layers")))
				{
					QuickActionItem(E.m_QuickActionAddFrontLayer, Tr("Front"));
					QuickActionItem(E.m_QuickActionAddTeleLayer, Tr("Teleporters"));
					QuickActionItem(E.m_QuickActionAddSpeedupLayer, Tr("Speedup"));
					QuickActionItem(E.m_QuickActionAddSwitchLayer, Tr("Switches"));
					QuickActionItem(E.m_QuickActionAddTuneLayer, Tr("Tune zones"));
					ImGui::EndMenu();
				}
				QuickActionItem(E.m_QuickActionDeleteLayer, Tr("Delete the layer"));
				ImGui::Separator();
				QuickActionItem(E.m_QuickActionAddQuad, Tr("Add a quad"), "Ctrl+Q");
				QuickActionItem(E.m_QuickActionAddSoundSource, Tr("Add a sound source"));
				QuickActionItem(E.m_QuickActionBrushPicker, Tr("Tile picker"), "Space");
				if(ImGui::BeginMenu(Tr("Make game tiles from this layer")))
				{
					QuickActionItem(E.m_QuickActionGameTilesAir, Tr("Air"));
					QuickActionItem(E.m_QuickActionGameTilesHookable, Tr("Hookable"));
					QuickActionItem(E.m_QuickActionGameTilesUnhookable, Tr("Unhookable"));
					QuickActionItem(E.m_QuickActionGameTilesHookthrough, Tr("Hookthrough"));
					QuickActionItem(E.m_QuickActionGameTilesDeath, Tr("Death"));
					QuickActionItem(E.m_QuickActionGameTilesFreeze, Tr("Freeze"));
					QuickActionItem(E.m_QuickActionGameTilesUnfreeze, Tr("Unfreeze"));
					QuickActionItem(E.m_QuickActionGameTilesDeepFreeze, Tr("Deep freeze"));
					QuickActionItem(E.m_QuickActionGameTilesDeepUnfreeze, Tr("Deep unfreeze"));
					QuickActionItem(E.m_QuickActionGameTilesLiveFreeze, Tr("Live freeze"));
					QuickActionItem(E.m_QuickActionGameTilesLiveUnfreeze, Tr("Live unfreeze"));
					QuickActionItem(E.m_QuickActionGameTilesBlueCheckTele, Tr("Blue check teleporter"));
					QuickActionItem(E.m_QuickActionGameTilesRedCheckTele, Tr("Red check teleporter"));
					ImGui::EndMenu();
				}
				break;
			}
			ImGui::PopFont();
			ImGui::EndPopup();
		}
		x += Width + S(2.0f);
	}
	return x;
}

void CXcEditorUi::RenderMapTabs(float x0, float x1, float Mid)
{
	CEditor &E = *m_pEditor;
	const STheme &t = Theme();
	ImFont *pFont = Fonts().m_pSmall;
	ImDrawList *pDraw = ImGui::GetWindowDrawList();
	if(x1 - x0 < S(80.0f))
		return;
	const float Height = S(30.0f);
	const int Num = (int)E.m_vpMaps.size();
	const float TabWidth = std::clamp((x1 - x0 - S(36.0f)) / std::max(Num, 1) - S(4.0f), S(90.0f), S(200.0f));
	float x = x0;
	std::optional<size_t> Close;
	for(int i = 0; i < Num; i++)
	{
		if(x + TabWidth > x1 - S(36.0f))
			break;
		const CEditorMap &MapEntry = *E.m_vpMaps[i];
		const bool Selected = (size_t)i == E.m_SelectedMap;
		const ImVec2 Min(x, Mid - Height * 0.5f);
		const ImVec2 Max(x + TabWidth, Mid + Height * 0.5f);
		ImGui::PushID(i);
		ImGui::SetCursorScreenPos(Min);
		ImGui::InvisibleButton("##tab", ImVec2(TabWidth - Height, Height), ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight | ImGuiButtonFlags_MouseButtonMiddle);
		const bool Hovered = ImGui::IsItemHovered();
		if(ImGui::IsItemClicked(ImGuiMouseButton_Left) && !Selected)
		{
			E.m_SelectedMap = i;
			E.Reset();
		}
		if(ImGui::IsItemClicked(ImGuiMouseButton_Middle))
			Close = i;
		if(ImGui::IsItemClicked(ImGuiMouseButton_Right))
		{
			E.m_PopupMapTab.m_pEditor = &E;
			E.m_PopupMapTab.m_SelectedMap = i;
			E.Ui()->DoPopupMenu(&E.m_PopupMapTab, E.Ui()->MouseX(), E.Ui()->MouseY(), 120.0f, 80.0f, &E.m_PopupMapTab, CEditor::CPopupMapTab::Render);
		}
		if(Hovered)
			ImGui::SetTooltip("%s", MapEntry.m_aFilename[0] ? MapEntry.m_aFilename : Tr("A new map, not saved yet"));
		ImGui::SetCursorScreenPos(ImVec2(Max.x - Height, Min.y));
		const bool CloseClicked = ImGui::InvisibleButton("##close", ImVec2(Height, Height));
		const bool CloseHovered = ImGui::IsItemHovered();
		if(CloseClicked)
			Close = i;
		ImGui::PopID();

		pDraw->AddRectFilled(Min, Max, Col(Selected ? t.m_Card : t.m_Track, Selected ? 1.0f : (Hovered ? 0.8f : 0.45f)), S(8.0f));
		if(Selected)
			pDraw->AddRect(Min, Max, Col(t.m_Border), S(8.0f));
		char aName[64];
		XcFitText(pFont, pFont->FontSize, MapEntry.m_aDisplayName, TabWidth - Height - S(14.0f), aName, sizeof(aName));
		pDraw->AddText(pFont, pFont->FontSize, ImVec2(Min.x + S(12.0f), Mid - pFont->FontSize * 0.5f), Col(Selected ? t.m_Text : t.m_TextSecondary), aName);
		// a dot for unsaved changes, it turns into the cross on hover
		const ImVec2 CloseCenter(Max.x - Height * 0.5f, Mid);
		if(MapEntry.m_Modified && !CloseHovered)
			pDraw->AddCircleFilled(CloseCenter, S(3.5f), Col(t.m_Accent), 12);
		else
		{
			if(CloseHovered)
				pDraw->AddCircleFilled(CloseCenter, S(10.0f), Col(t.m_Track), 16);
			const ImVec2 CrossSize = pFont->CalcTextSizeA(pFont->FontSize, FLT_MAX, 0.0f, FontIcon::XMARK);
			pDraw->AddText(pFont, pFont->FontSize, ImVec2(CloseCenter.x - CrossSize.x * 0.5f, Mid - CrossSize.y * 0.5f), Col(CloseHovered ? t.m_Text : t.m_TextMuted), FontIcon::XMARK);
		}
		x += TabWidth + S(4.0f);
	}
	// a new map in a new tab
	ImGui::SetCursorScreenPos(ImVec2(x, Mid - S(14.0f)));
	if(IconButton("##newtab", FontIcon::PLUS, Tr("New map (Ctrl+N)"), false, true, S(28.0f)))
		E.m_QuickActionNewMap.Call();
	if(Close.has_value())
		E.CloseMap(Close.value(), true);
}

// ------------------------------------------------------------------------------------------------
// left panel: layers, images, sounds

void CXcEditorUi::RenderLeftPanel()
{
	CEditor &E = *m_pEditor;
	const STheme &t = Theme();
	const ImVec2 Display = ImGui::GetIO().DisplaySize;
	const float Height = Display.y - m_TopHeight - m_StatusHeight;
	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(S(10.0f), S(10.0f)));
	const bool Open = BeginPanel("##xe_left", ImVec2(0.0f, m_TopHeight), ImVec2(m_LeftWidth, Height), ImVec4(t.m_Panel.x, t.m_Panel.y, t.m_Panel.z, 1.0f), ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
	ImGui::PopStyleVar();
	if(Open)
	{
		ImDrawList *pDraw = ImGui::GetWindowDrawList();
		pDraw->AddLine(ImVec2(m_LeftWidth - 1.0f, m_TopHeight), ImVec2(m_LeftWidth - 1.0f, m_TopHeight + Height), Col(t.m_Border));
		// layers, images and sounds like the pages of a Figma panel
		const char *apModes[] = {Tr("Layers"), Tr("Images"), Tr("Sounds")};
		int Mode = E.m_Mode;
		if(Segmented("##xe_mode", &Mode, apModes, 3))
			E.m_Mode = Mode;
		ImGui::Dummy(ImVec2(0.0f, S(2.0f)));
		if(E.m_Mode == MODE_LAYERS)
			RenderLayersTree();
		else if(E.m_Mode == MODE_IMAGES)
			RenderImagesPanel();
		else
			RenderSoundsPanel();
	}
	ImGui::End();
}

void CXcEditorUi::RenderLayersTree()
{
	CEditor &E = *m_pEditor;
	CEditorMap *pMap = E.Map();
	const STheme &t = Theme();
	ImFont *pFont = Fonts().m_pSmall;
	ImFont *pLabel = Fonts().m_pLabel;

	InputText("##xe_layer_filter", m_aLayerFilter, sizeof(m_aLayerFilter), Tr("Search layers"));
	ImGui::Dummy(ImVec2(0.0f, S(2.0f)));

	// the tree scrolls, the buttons stay at the bottom
	const float ButtonsHeight = S(36.0f) + S(14.0f); // a button of the menu style and the gap above it
	const ImVec2 TreeSize(ImGui::GetContentRegionAvail().x, ImGui::GetContentRegionAvail().y - ButtonsHeight);
	ImGui::BeginChild("##xe_tree", TreeSize, ImGuiChildFlags_None, ImGuiWindowFlags_NoBackground);
	ImDrawList *pDraw = ImGui::GetWindowDrawList();
	const float RowHeight = S(28.0f);
	const float Width = ImGui::GetContentRegionAvail().x;
	ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0.0f, S(2.0f)));

	// a drop moves layers or groups after the frame, the lists must not change while they are drawn
	struct SMove
	{
		bool m_Group = false;
		int m_FromGroup = -1, m_FromLayer = -1;
		int m_ToGroup = -1, m_ToLayer = -1;
	} Move;
	int ToggleSoloGroup = -1;

	for(int g = 0; g < (int)pMap->m_vpGroups.size(); g++)
	{
		const std::shared_ptr<CLayerGroup> &pGroup = pMap->m_vpGroups[g];
		// the search shows groups with matching layers
		bool GroupMatches = !m_aLayerFilter[0] || str_utf8_find_nocase(pGroup->m_aName, m_aLayerFilter);
		bool AnyLayerMatches = false;
		for(const auto &pLayer : pGroup->m_vpLayers)
			AnyLayerMatches |= !m_aLayerFilter[0] || str_utf8_find_nocase(pLayer->m_aName, m_aLayerFilter) != nullptr;
		if(!GroupMatches && !AnyLayerMatches)
			continue;

		ImGui::PushID(g);
		// ---- group row
		const ImVec2 Min = ImGui::GetCursorScreenPos();
		const ImVec2 Max(Min.x + Width, Min.y + RowHeight);
		const bool GroupSelected = pMap->m_SelectedGroup == g;
		ImGui::InvisibleButton("##group", ImVec2(Width - RowHeight, RowHeight + S(2.0f)), ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight);
		const bool Hovered = ImGui::IsItemHovered();
		if(ImGui::IsItemClicked(ImGuiMouseButton_Left))
		{
			if(E.Input()->ShiftIsPressed())
			{
				// all layers of the group
				if(g != pMap->m_SelectedGroup)
					pMap->SelectLayer(0, g);
				pMap->m_vSelectedLayers.clear();
				for(size_t i = 0; i < pGroup->m_vpLayers.size(); i++)
					pMap->AddSelectedLayer(i);
			}
			else if(g != pMap->m_SelectedGroup)
				pMap->SelectLayer(0, g);
		}
		if(Hovered && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
		{
			if(!pGroup->m_GameGroup && MouseIn(ImVec2(Min.x + S(30.0f), Min.y), ImVec2(Max.x - RowHeight * 2.0f, Max.y)))
			{
				m_RenameGroup = g;
				m_RenameLayer = -1;
				str_copy(m_aRename, pGroup->m_aName);
				m_RenameFocus = true;
			}
			else
				pGroup->m_Collapse = !pGroup->m_Collapse;
		}
		if(ImGui::IsItemClicked(ImGuiMouseButton_Right))
		{
			if(g != pMap->m_SelectedGroup)
				pMap->SelectLayer(0, g);
			ImGui::OpenPopup("##group_ctx");
		}
		// drag a group to move it, drop layers on it to put them at its end
		if(ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceNoPreviewTooltip))
		{
			const int Payload[2] = {g, -1};
			ImGui::SetDragDropPayload("XE_GROUP", Payload, sizeof(Payload));
			ImGui::EndDragDropSource();
		}
		if(ImGui::BeginDragDropTarget())
		{
			pDraw->AddLine(ImVec2(Min.x, Min.y), ImVec2(Max.x, Min.y), Col(t.m_Accent), S(2.0f));
			if(const ImGuiPayload *pPayload = ImGui::AcceptDragDropPayload("XE_GROUP"))
			{
				const int *pData = (const int *)pPayload->Data;
				Move = {true, pData[0], -1, g, -1};
			}
			if(const ImGuiPayload *pPayload = ImGui::AcceptDragDropPayload("XE_LAYER"))
			{
				const int *pData = (const int *)pPayload->Data;
				Move = {false, pData[0], pData[1], g, (int)pGroup->m_vpLayers.size()};
			}
			ImGui::EndDragDropTarget();
		}

		if(GroupSelected)
			pDraw->AddRectFilled(Min, Max, Col(t.m_Track, 0.8f), S(7.0f));
		else if(Hovered)
			pDraw->AddRectFilled(Min, Max, Col(t.m_Track, 0.45f), S(7.0f));
		// collapse arrow
		const char *pArrow = pGroup->m_Collapse ? FontIcon::CHEVRON_RIGHT : FontIcon::CHEVRON_DOWN;
		pDraw->AddText(pFont, pFont->FontSize * 0.8f, ImVec2(Min.x + S(8.0f), Min.y + (RowHeight - pFont->FontSize * 0.8f) * 0.5f), Col(t.m_TextMuted), pArrow);
		if(Hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left) && MouseIn(Min, ImVec2(Min.x + S(24.0f), Max.y)))
			pGroup->m_Collapse = !pGroup->m_Collapse;
		// name
		const float NameX = Min.x + S(28.0f);
		if(m_RenameGroup == g)
		{
			ImGui::SetCursorScreenPos(ImVec2(NameX, Min.y + S(2.0f)));
			ImGui::SetNextItemWidth(Width - S(28.0f) - RowHeight * 1.2f);
			if(m_RenameFocus)
			{
				ImGui::SetKeyboardFocusHere();
				m_RenameFocus = false;
			}
			const bool Done = ImGui::InputText("##rename", m_aRename, sizeof(pGroup->m_aName), ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll);
			if(Done || (!ImGui::IsItemActive() && !ImGui::IsItemFocused()))
			{
				if(str_comp(pGroup->m_aName, m_aRename) != 0)
				{
					str_copy(pGroup->m_aName, m_aRename);
					pMap->OnModify();
				}
				m_RenameGroup = -1;
			}
		}
		else
		{
			char aName[64];
			if(pGroup->m_GameGroup)
				str_copy(aName, Tr("Game"));
			else
				str_copy(aName, pGroup->m_aName[0] ? pGroup->m_aName : Tr("Group"));
			pDraw->AddText(pLabel, pFont->FontSize, ImVec2(NameX, Min.y + (RowHeight - pFont->FontSize) * 0.5f), Col(GroupSelected ? t.m_Text : t.m_TextSecondary, pGroup->m_Visible ? 1.0f : 0.5f), aName);
			char aCount[16];
			str_format(aCount, sizeof(aCount), "%d", (int)pGroup->m_vpLayers.size());
			const float CountWidth = pFont->CalcTextSizeA(pFont->FontSize * 0.85f, FLT_MAX, 0.0f, aCount).x;
			pDraw->AddText(pFont, pFont->FontSize * 0.85f, ImVec2(Max.x - RowHeight - CountWidth - S(6.0f), Min.y + (RowHeight - pFont->FontSize * 0.85f) * 0.5f), Col(t.m_TextMuted), aCount);
		}
		// visibility, right click shows only this group
		ImGui::SetCursorScreenPos(ImVec2(Max.x - RowHeight, Min.y));
		ImGui::InvisibleButton("##eye", ImVec2(RowHeight, RowHeight), ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight);
		const bool EyeHovered = ImGui::IsItemHovered();
		if(ImGui::IsItemClicked(ImGuiMouseButton_Left))
			pGroup->m_Visible = !pGroup->m_Visible;
		if(ImGui::IsItemClicked(ImGuiMouseButton_Right))
			ToggleSoloGroup = g;
		if(EyeHovered)
			ImGui::SetTooltip("%s", Tr("Show or hide. Right click: only this group"));
		{
			const char *pEye = pGroup->m_Visible ? FontIcon::EYE : FontIcon::EYE_SLASH;
			const ImVec2 EyeSize = pFont->CalcTextSizeA(pFont->FontSize, FLT_MAX, 0.0f, pEye);
			if(EyeHovered || !pGroup->m_Visible || Hovered)
				pDraw->AddText(pFont, pFont->FontSize, ImVec2(Max.x - RowHeight * 0.5f - EyeSize.x * 0.5f, Min.y + (RowHeight - EyeSize.y) * 0.5f), Col(EyeHovered ? t.m_Text : t.m_TextMuted), pEye);
		}
		ImGui::SetCursorScreenPos(ImVec2(Min.x, Max.y + S(2.0f)));

		if(ImGui::BeginPopup("##group_ctx"))
		{
			ImGui::PushFont(Fonts().m_pBody);
			QuickActionItem(E.m_QuickActionAddTileLayer, Tr("New tile layer"));
			QuickActionItem(E.m_QuickActionAddQuadsLayer, Tr("New quads layer"));
			QuickActionItem(E.m_QuickActionAddSoundLayer, Tr("New sound layer"));
			if(pGroup->m_GameGroup)
			{
				if(!pMap->m_pFrontLayer)
					QuickActionItem(E.m_QuickActionAddFrontLayer, Tr("New front layer"));
				if(!pMap->m_pTeleLayer)
					QuickActionItem(E.m_QuickActionAddTeleLayer, Tr("New teleporter layer"));
				if(!pMap->m_pSpeedupLayer)
					QuickActionItem(E.m_QuickActionAddSpeedupLayer, Tr("New speedup layer"));
				if(!pMap->m_pSwitchLayer)
					QuickActionItem(E.m_QuickActionAddSwitchLayer, Tr("New switch layer"));
				if(!pMap->m_pTuneLayer)
					QuickActionItem(E.m_QuickActionAddTuneLayer, Tr("New tune layer"));
			}
			ImGui::Separator();
			if(!pGroup->m_GameGroup && ImGui::MenuItem(Tr("Rename")))
			{
				m_RenameGroup = g;
				m_RenameLayer = -1;
				str_copy(m_aRename, pGroup->m_aName);
				m_RenameFocus = true;
			}
			if(ImGui::MenuItem(pGroup->m_Collapse ? Tr("Expand") : Tr("Collapse")))
				pGroup->m_Collapse = !pGroup->m_Collapse;
			if(ImGui::MenuItem(Tr("Properties")))
				m_ShowProperties = true;
			if(!pGroup->m_GameGroup)
			{
				ImGui::Separator();
				if(ImGui::MenuItem(Tr("Delete the group")))
				{
					pMap->m_EditorHistory.RecordAction(std::make_shared<CEditorActionGroup>(pMap, g, true));
					pMap->DeleteGroup(g);
					pMap->m_SelectedGroup = std::max(0, g - 1);
					ImGui::PopFont();
					ImGui::EndPopup();
					ImGui::PopID();
					break;
				}
			}
			ImGui::PopFont();
			ImGui::EndPopup();
		}

		// ---- layers of the group
		if(!pGroup->m_Collapse)
		{
			for(int i = 0; i < (int)pGroup->m_vpLayers.size(); i++)
			{
				const std::shared_ptr<CLayer> &pLayer = pGroup->m_vpLayers[i];
				if(m_aLayerFilter[0] && !GroupMatches && !str_utf8_find_nocase(pLayer->m_aName, m_aLayerFilter))
					continue;
				const bool Selected = g == pMap->m_SelectedGroup && std::find(pMap->m_vSelectedLayers.begin(), pMap->m_vSelectedLayers.end(), i) != pMap->m_vSelectedLayers.end();
				ImGui::PushID(i);
				const ImVec2 LMin = ImGui::GetCursorScreenPos();
				const ImVec2 LMax(LMin.x + Width, LMin.y + RowHeight);
				ImGui::InvisibleButton("##layer", ImVec2(Width - RowHeight, RowHeight + S(2.0f)), ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight);
				const bool LHovered = ImGui::IsItemHovered();
				if(ImGui::IsItemClicked(ImGuiMouseButton_Left))
				{
					if((E.Input()->ShiftIsPressed() || E.Input()->ModifierIsPressed()) && pMap->m_SelectedGroup == g)
					{
						// add or remove from the selection
						auto Position = std::find(pMap->m_vSelectedLayers.begin(), pMap->m_vSelectedLayers.end(), i);
						if(Position != pMap->m_vSelectedLayers.end())
						{
							if(pMap->m_vSelectedLayers.size() > 1)
								pMap->m_vSelectedLayers.erase(Position);
						}
						else
							pMap->AddSelectedLayer(i);
					}
					else
						pMap->SelectLayer(i, g);
				}
				if(LHovered && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left) && !pLayer->IsEntitiesLayer())
				{
					m_RenameGroup = g;
					m_RenameLayer = i;
					str_copy(m_aRename, pLayer->m_aName);
					m_RenameFocus = true;
				}
				if(ImGui::IsItemClicked(ImGuiMouseButton_Right))
				{
					if(!Selected)
						pMap->SelectLayer(i, g);
					ImGui::OpenPopup("##layer_ctx");
				}
				if(ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceNoPreviewTooltip))
				{
					const int Payload[2] = {g, i};
					ImGui::SetDragDropPayload("XE_LAYER", Payload, sizeof(Payload));
					ImGui::EndDragDropSource();
				}
				if(ImGui::BeginDragDropTarget())
				{
					pDraw->AddLine(ImVec2(LMin.x + S(20.0f), LMin.y), ImVec2(LMax.x, LMin.y), Col(t.m_Accent), S(2.0f));
					if(const ImGuiPayload *pPayload = ImGui::AcceptDragDropPayload("XE_LAYER"))
					{
						const int *pData = (const int *)pPayload->Data;
						Move = {false, pData[0], pData[1], g, i};
					}
					ImGui::EndDragDropTarget();
				}

				if(Selected)
					pDraw->AddRectFilled(LMin, LMax, Col(t.m_Accent, 0.22f), S(7.0f));
				else if(LHovered)
					pDraw->AddRectFilled(LMin, LMax, Col(t.m_Track, 0.45f), S(7.0f));
				if(Selected)
					pDraw->AddRectFilled(ImVec2(LMin.x, LMin.y + S(6.0f)), ImVec2(LMin.x + S(3.0f), LMax.y - S(6.0f)), Col(t.m_Accent), S(2.0f));
				// type icon
				const char *pIcon = LayerIcon(pMap, pLayer.get());
				const float IconSize = pFont->FontSize * 0.9f;
				const ImVec2 IconDim = pFont->CalcTextSizeA(IconSize, FLT_MAX, 0.0f, pIcon);
				pDraw->AddText(pFont, IconSize, ImVec2(LMin.x + S(30.0f) - IconDim.x * 0.5f, LMin.y + (RowHeight - IconDim.y) * 0.5f), Col(LayerColor(pMap, pLayer.get()), pLayer->m_Visible ? 1.0f : 0.45f), pIcon);
				const float LNameX = LMin.x + S(44.0f);
				if(m_RenameGroup == g && m_RenameLayer == i)
				{
					ImGui::SetCursorScreenPos(ImVec2(LNameX, LMin.y + S(2.0f)));
					ImGui::SetNextItemWidth(Width - S(44.0f) - RowHeight * 1.2f);
					if(m_RenameFocus)
					{
						ImGui::SetKeyboardFocusHere();
						m_RenameFocus = false;
					}
					const bool Done = ImGui::InputText("##rename", m_aRename, sizeof(pLayer->m_aName), ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll);
					if(Done || (!ImGui::IsItemActive() && !ImGui::IsItemFocused()))
					{
						if(str_comp(pLayer->m_aName, m_aRename) != 0)
						{
							str_copy(pLayer->m_aName, m_aRename);
							pMap->OnModify();
						}
						m_RenameLayer = -1;
						m_RenameGroup = -1;
					}
				}
				else
				{
					// like the classic list: a layer without a name is called by its image or sound
					const char *pName = pLayer->m_aName[0] ? pLayer->m_aName : Tr(pLayer->TypeName());
					if(!pLayer->m_aName[0] && pLayer->m_Type == LAYERTYPE_TILES)
					{
						const int Image = std::static_pointer_cast<CLayerTiles>(pLayer)->m_Image;
						if(Image >= 0 && Image < (int)pMap->m_vpImages.size())
							pName = pMap->m_vpImages[Image]->m_aName;
					}
					else if(!pLayer->m_aName[0] && pLayer->m_Type == LAYERTYPE_QUADS)
					{
						const int Image = std::static_pointer_cast<CLayerQuads>(pLayer)->m_Image;
						if(Image >= 0 && Image < (int)pMap->m_vpImages.size())
							pName = pMap->m_vpImages[Image]->m_aName;
					}
					else if(!pLayer->m_aName[0] && pLayer->m_Type == LAYERTYPE_SOUNDS)
					{
						const int Sound = std::static_pointer_cast<CLayerSounds>(pLayer)->m_Sound;
						if(Sound >= 0 && Sound < (int)pMap->m_vpSounds.size())
							pName = pMap->m_vpSounds[Sound]->m_aName;
					}
					pDraw->AddText(pFont, pFont->FontSize, ImVec2(LNameX, LMin.y + (RowHeight - pFont->FontSize) * 0.5f), Col(Selected ? t.m_Text : t.m_TextSecondary, pLayer->m_Visible ? 1.0f : 0.5f), pName);
					if(pLayer->m_Flags & LAYERFLAG_DETAIL)
					{
						const float NameWidth = pFont->CalcTextSizeA(pFont->FontSize, FLT_MAX, 0.0f, pName).x;
						pDraw->AddText(pFont, pFont->FontSize * 0.8f, ImVec2(LNameX + NameWidth + S(6.0f), LMin.y + (RowHeight - pFont->FontSize * 0.8f) * 0.5f), Col(t.m_TextMuted), "HD");
					}
				}
				// visibility
				ImGui::SetCursorScreenPos(ImVec2(LMax.x - RowHeight, LMin.y));
				const bool EyeClicked = ImGui::InvisibleButton("##eye", ImVec2(RowHeight, RowHeight));
				const bool LEyeHovered = ImGui::IsItemHovered();
				if(EyeClicked)
					pLayer->m_Visible = !pLayer->m_Visible;
				if(LEyeHovered || LHovered || !pLayer->m_Visible)
				{
					const char *pEye = pLayer->m_Visible ? FontIcon::EYE : FontIcon::EYE_SLASH;
					const ImVec2 EyeSize = pFont->CalcTextSizeA(pFont->FontSize, FLT_MAX, 0.0f, pEye);
					pDraw->AddText(pFont, pFont->FontSize, ImVec2(LMax.x - RowHeight * 0.5f - EyeSize.x * 0.5f, LMin.y + (RowHeight - EyeSize.y) * 0.5f), Col(LEyeHovered ? t.m_Text : t.m_TextMuted), pEye);
				}
				ImGui::SetCursorScreenPos(ImVec2(LMin.x, LMax.y + S(2.0f)));

				if(ImGui::BeginPopup("##layer_ctx"))
				{
					ImGui::PushFont(Fonts().m_pBody);
					if(!pLayer->IsEntitiesLayer())
					{
						if(ImGui::MenuItem(Tr("Rename")))
						{
							m_RenameGroup = g;
							m_RenameLayer = i;
							str_copy(m_aRename, pLayer->m_aName);
							m_RenameFocus = true;
						}
						if(ImGui::MenuItem(Tr("Duplicate")))
						{
							pGroup->DuplicateLayer(i);
							pMap->m_EditorHistory.RecordAction(std::make_shared<CEditorActionAddLayer>(pMap, g, i + 1, true));
						}
					}
					if(ImGui::MenuItem(Tr("Properties")))
						m_ShowProperties = true;
					if(pLayer->m_Type == LAYERTYPE_TILES)
						QuickActionItem(E.m_QuickActionBrushPicker, Tr("Tile picker"), "Space");
					if(pLayer->m_Type == LAYERTYPE_QUADS)
						QuickActionItem(E.m_QuickActionAddQuad, Tr("Add a quad"), "Ctrl+Q");
					if(pLayer->m_Type == LAYERTYPE_SOUNDS)
						QuickActionItem(E.m_QuickActionAddSoundSource, Tr("Add a sound source"));
					if(pMap->m_pGameLayer != pLayer)
					{
						ImGui::Separator();
						QuickActionItem(E.m_QuickActionDeleteLayer, Tr("Delete the layer"));
					}
					ImGui::PopFont();
					ImGui::EndPopup();
				}
				ImGui::PopID();
			}
		}
		ImGui::Dummy(ImVec2(0.0f, S(4.0f)));
		ImGui::PopID();
	}
	ImGui::PopStyleVar();
	ImGui::EndChild();

	// ---- apply a drop
	if(Move.m_FromGroup >= 0 && Move.m_ToGroup >= 0 && Move.m_FromGroup < (int)pMap->m_vpGroups.size() && Move.m_ToGroup < (int)pMap->m_vpGroups.size())
	{
		if(Move.m_Group)
		{
			if(Move.m_FromGroup != Move.m_ToGroup)
			{
				pMap->m_SelectedGroup = pMap->MoveGroup(Move.m_FromGroup, Move.m_ToGroup);
				pMap->OnModify();
			}
		}
		else
		{
			std::shared_ptr<CLayerGroup> pFrom = pMap->m_vpGroups[Move.m_FromGroup];
			std::shared_ptr<CLayerGroup> pTo = pMap->m_vpGroups[Move.m_ToGroup];
			if(Move.m_FromLayer >= 0 && Move.m_FromLayer < (int)pFrom->m_vpLayers.size())
			{
				std::shared_ptr<CLayer> pLayer = pFrom->m_vpLayers[Move.m_FromLayer];
				// the game layers stay in the game group
				if(Move.m_FromGroup == Move.m_ToGroup || !pLayer->IsEntitiesLayer())
				{
					if(Move.m_FromGroup == Move.m_ToGroup)
					{
						const int To = std::clamp(Move.m_ToLayer > Move.m_FromLayer ? Move.m_ToLayer - 1 : Move.m_ToLayer, 0, (int)pFrom->m_vpLayers.size() - 1);
						if(To != Move.m_FromLayer)
							pMap->SelectLayer(pFrom->MoveLayer(Move.m_FromLayer, To), Move.m_FromGroup);
					}
					else
					{
						pFrom->m_vpLayers.erase(pFrom->m_vpLayers.begin() + Move.m_FromLayer);
						const int To = std::clamp(Move.m_ToLayer, 0, (int)pTo->m_vpLayers.size());
						pTo->m_vpLayers.insert(pTo->m_vpLayers.begin() + To, pLayer);
						pMap->SelectLayer(To, Move.m_ToGroup);
					}
					pMap->OnModify();
				}
			}
		}
	}
	if(ToggleSoloGroup >= 0)
	{
		// show only this group, again shows all
		int NumOthersVisible = 0;
		for(int g = 0; g < (int)pMap->m_vpGroups.size(); g++)
			if(g != ToggleSoloGroup && pMap->m_vpGroups[g]->m_Visible)
				NumOthersVisible++;
		for(int g = 0; g < (int)pMap->m_vpGroups.size(); g++)
			pMap->m_vpGroups[g]->m_Visible = g == ToggleSoloGroup || NumOthersVisible == 0;
	}

	// ---- buttons under the tree
	ImGui::Dummy(ImVec2(0.0f, S(4.0f)));
	const float ButtonWidth = (ImGui::GetContentRegionAvail().x - S(6.0f)) * 0.5f;
	if(Button(Tr("+ Group"), false, ButtonWidth))
		E.m_QuickActionAddGroup.Call();
	ImGui::SameLine(0.0f, S(6.0f));
	if(Button(Tr("+ Layer"), true, ButtonWidth))
		ImGui::OpenPopup("##add_layer");
	if(ImGui::BeginPopup("##add_layer"))
	{
		ImGui::PushFont(Fonts().m_pBody);
		QuickActionItem(E.m_QuickActionAddTileLayer, Tr("Tile layer"));
		QuickActionItem(E.m_QuickActionAddQuadsLayer, Tr("Quads layer"));
		QuickActionItem(E.m_QuickActionAddSoundLayer, Tr("Sound layer"));
		if(pMap->SelectedGroup() && pMap->SelectedGroup()->m_GameGroup)
		{
			ImGui::Separator();
			if(!pMap->m_pFrontLayer)
				QuickActionItem(E.m_QuickActionAddFrontLayer, Tr("Front layer"));
			if(!pMap->m_pTeleLayer)
				QuickActionItem(E.m_QuickActionAddTeleLayer, Tr("Teleporter layer"));
			if(!pMap->m_pSpeedupLayer)
				QuickActionItem(E.m_QuickActionAddSpeedupLayer, Tr("Speedup layer"));
			if(!pMap->m_pSwitchLayer)
				QuickActionItem(E.m_QuickActionAddSwitchLayer, Tr("Switch layer"));
			if(!pMap->m_pTuneLayer)
				QuickActionItem(E.m_QuickActionAddTuneLayer, Tr("Tune layer"));
		}
		ImGui::PopFont();
		ImGui::EndPopup();
	}
}

void CXcEditorUi::RenderImagesPanel()
{
	CEditor &E = *m_pEditor;
	CEditorMap *pMap = E.Map();
	const STheme &t = Theme();
	ImFont *pFont = Fonts().m_pSmall;

	const float ButtonsHeight = S(36.0f) + S(14.0f); // a button of the menu style and the gap above it
	ImGui::BeginChild("##xe_images", ImVec2(ImGui::GetContentRegionAvail().x, ImGui::GetContentRegionAvail().y - ButtonsHeight), ImGuiChildFlags_None, ImGuiWindowFlags_NoBackground);
	ImDrawList *pDraw = ImGui::GetWindowDrawList();
	const float Width = ImGui::GetContentRegionAvail().x;
	const float Gap = S(6.0f);
	const int Columns = 2;
	const float Tile = (Width - Gap * (Columns - 1)) / Columns;
	for(int Pass = 0; Pass < 2; Pass++)
	{
		// embedded first, then the images that come with the game
		bool Header = false;
		int Column = 0;
		for(int i = 0; i < (int)pMap->m_vpImages.size(); i++)
		{
			const std::shared_ptr<CEditorImage> &pImage = pMap->m_vpImages[i];
			if((Pass == 0) == pImage->m_External)
				continue;
			if(!Header)
			{
				Description(Pass == 0 ? Tr("In the map") : Tr("From the game (not saved in the map)"));
				Header = true;
			}
			if(Column > 0)
				ImGui::SameLine(0.0f, Gap);
			ImGui::PushID(i);
			const ImVec2 Min = ImGui::GetCursorScreenPos();
			const ImVec2 Max(Min.x + Tile, Min.y + Tile + S(22.0f));
			ImGui::InvisibleButton("##image", ImVec2(Tile, Tile + S(22.0f)), ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight);
			const bool Hovered = ImGui::IsItemHovered();
			const bool Selected = pMap->m_SelectedImage == i;
			if(ImGui::IsItemClicked(ImGuiMouseButton_Left) || ImGui::IsItemClicked(ImGuiMouseButton_Right))
				pMap->m_SelectedImage = i;
			if(ImGui::IsItemClicked(ImGuiMouseButton_Right))
			{
				const int Height = pImage->m_External ? 73 : (XcEditorStyle::Enabled() ? 158 : 107);
				static SPopupMenuId s_PopupImageId;
				E.Ui()->DoPopupMenu(&s_PopupImageId, E.Ui()->MouseX(), E.Ui()->MouseY(), 140, Height, &E, CEditor::PopupImage);
			}
			const bool Used = std::any_of(pMap->m_vpGroups.cbegin(), pMap->m_vpGroups.cend(), [i](const auto &pGroup) {
				return std::any_of(pGroup->m_vpLayers.cbegin(), pGroup->m_vpLayers.cend(), [i](const auto &pLayer) {
					return pLayer->IsImageUsed(i);
				});
			});
			pDraw->AddRectFilled(Min, Max, Col(Selected ? t.m_Card : t.m_Track, Selected ? 1.0f : (Hovered ? 0.8f : 0.5f)), S(8.0f));
			if(Selected)
				pDraw->AddRect(Min, Max, Col(t.m_Accent), S(8.0f), 0, S(1.5f));
			// the picture, kept in its aspect
			const float Pad = S(8.0f);
			const float Box = Tile - Pad * 2.0f;
			const float Aspect = pImage->m_Height > 0 ? (float)pImage->m_Width / pImage->m_Height : 1.0f;
			const float w = Aspect >= 1.0f ? Box : Box * Aspect;
			const float h = Aspect >= 1.0f ? Box / Aspect : Box;
			const ImVec2 PicMin(Min.x + Pad + (Box - w) * 0.5f, Min.y + Pad + (Box - h) * 0.5f);
			if(pImage->m_Texture.IsValid())
				pDraw->AddImage((ImTextureID)(uintptr_t)m_Gui.TextureId(pImage->m_Texture), PicMin, ImVec2(PicMin.x + w, PicMin.y + h));
			char aName[64];
			XcFitText(pFont, pFont->FontSize * 0.9f, pImage->m_aName, Tile - S(12.0f), aName, sizeof(aName));
			pDraw->AddText(pFont, pFont->FontSize * 0.9f, ImVec2(Min.x + S(6.0f), Max.y - S(19.0f)), Col(Used ? t.m_TextSecondary : t.m_TextMuted), aName);
			if(!Used)
				pDraw->AddText(pFont, pFont->FontSize * 0.75f, ImVec2(Min.x + S(6.0f), Min.y + S(4.0f)), Col(ImVec4(1.0f, 0.6f, 0.3f, 1.0f)), Tr("not used"));
			if(Hovered)
				ImGui::SetTooltip("%s\n%dx%d\n%s", pImage->m_aName, pImage->m_Width, pImage->m_Height, Tr("Right click: replace, embed, export, delete"));
			ImGui::PopID();
			Column = (Column + 1) % Columns;
		}
		if(Header)
			ImGui::Dummy(ImVec2(0.0f, S(6.0f)));
	}
	if(pMap->m_vpImages.empty())
		Description(Tr("No images yet. Tile layers and quads need an image (a tileset)."));
	ImGui::EndChild();
	ImGui::Dummy(ImVec2(0.0f, S(4.0f)));
	if(Button(Tr("+ Image"), true, ImGui::GetContentRegionAvail().x))
		E.m_QuickActionAddImage.Call();
}

void CXcEditorUi::RenderSoundsPanel()
{
	CEditor &E = *m_pEditor;
	CEditorMap *pMap = E.Map();
	const STheme &t = Theme();
	ImFont *pFont = Fonts().m_pSmall;

	const float ButtonsHeight = S(36.0f) + S(14.0f); // a button of the menu style and the gap above it
	ImGui::BeginChild("##xe_sounds", ImVec2(ImGui::GetContentRegionAvail().x, ImGui::GetContentRegionAvail().y - ButtonsHeight), ImGuiChildFlags_None, ImGuiWindowFlags_NoBackground);
	ImDrawList *pDraw = ImGui::GetWindowDrawList();
	const float Width = ImGui::GetContentRegionAvail().x;
	const float RowHeight = S(34.0f);
	for(int i = 0; i < (int)pMap->m_vpSounds.size(); i++)
	{
		const auto &pSound = pMap->m_vpSounds[i];
		ImGui::PushID(i);
		const ImVec2 Min = ImGui::GetCursorScreenPos();
		const ImVec2 Max(Min.x + Width, Min.y + RowHeight);
		ImGui::InvisibleButton("##sound", ImVec2(Width, RowHeight), ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight);
		const bool Hovered = ImGui::IsItemHovered();
		const bool Selected = pMap->m_SelectedSound == i;
		if(ImGui::IsItemClicked(ImGuiMouseButton_Left) || ImGui::IsItemClicked(ImGuiMouseButton_Right))
			pMap->m_SelectedSound = i;
		if(ImGui::IsItemClicked(ImGuiMouseButton_Right))
		{
			static SPopupMenuId s_PopupSoundId;
			E.Ui()->DoPopupMenu(&s_PopupSoundId, E.Ui()->MouseX(), E.Ui()->MouseY(), 140, 90, &E, CEditor::PopupSound);
		}
		const bool Used = std::any_of(pMap->m_vpGroups.cbegin(), pMap->m_vpGroups.cend(), [i](const auto &pGroup) {
			return std::any_of(pGroup->m_vpLayers.cbegin(), pGroup->m_vpLayers.cend(), [i](const auto &pLayer) {
				return pLayer->IsSoundUsed(i);
			});
		});
		pDraw->AddRectFilled(Min, Max, Col(Selected ? t.m_Accent : t.m_Track, Selected ? 0.22f : (Hovered ? 0.8f : 0.45f)), S(8.0f));
		pDraw->AddText(pFont, pFont->FontSize, ImVec2(Min.x + S(10.0f), Min.y + (RowHeight - pFont->FontSize) * 0.5f), Col(ImVec4(0.75f, 0.6f, 1.0f, 1.0f)), ICON_SOUNDS);
		char aName[64];
		XcFitText(pFont, pFont->FontSize, pSound->m_aName, Width - S(46.0f), aName, sizeof(aName));
		pDraw->AddText(pFont, pFont->FontSize, ImVec2(Min.x + S(34.0f), Min.y + (RowHeight - pFont->FontSize) * 0.5f), Col(Used ? t.m_Text : t.m_TextMuted), aName);
		if(Hovered)
			ImGui::SetTooltip("%s\n%s", pSound->m_aName, Tr("Right click: replace, export, delete"));
		ImGui::PopID();
		ImGui::Dummy(ImVec2(0.0f, S(2.0f)));
	}
	if(pMap->m_vpSounds.empty())
		Description(Tr("No sounds yet. Sound layers play them around their sources."));
	ImGui::EndChild();
	ImGui::Dummy(ImVec2(0.0f, S(4.0f)));
	if(Button(Tr("+ Sound"), true, ImGui::GetContentRegionAvail().x))
		E.m_FileBrowser.ShowFileDialog(IStorage::TYPE_ALL, CFileBrowser::EFileType::SOUND, "Add sound", "Add", "mapres", "", CEditor::AddSound, &E);
	(void)t;
}

// ------------------------------------------------------------------------------------------------
// floating tool capsule at the bottom of the map

void CXcEditorUi::RenderCapsule()
{
	CEditor &E = *m_pEditor;
	const STheme &t = Theme();
	const float k = UiToPixels();
	const float Button = S(34.0f);
	const float Gap = S(3.0f);
	const float Divider = S(12.0f);
	const int NumButtons = CEditor::XC_NUM_TOOLS + 4 + 4 + 2;
	const float Width = NumButtons * (Button + Gap) + Divider * 3.0f + S(16.0f);
	const float Height = Button + S(12.0f);
	const ImVec2 Pos(m_View.x * k + m_View.w * k * 0.5f - Width * 0.5f, (m_View.y + m_View.h) * k - Height - S(14.0f));
	ImGui::SetNextWindowPos(Pos);
	ImGui::SetNextWindowSize(ImVec2(Width, Height));
	ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(t.m_Card.x, t.m_Card.y, t.m_Card.z, 0.97f));
	ImGui::PushStyleColor(ImGuiCol_Border, t.m_Border);
	ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, Height * 0.5f);
	ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 1.0f);
	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(S(8.0f), S(6.0f)));
	ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(Gap, 0.0f));
	// a soft shadow under the capsule
	ImGui::GetBackgroundDrawList()->AddRectFilled(ImVec2(Pos.x, Pos.y + S(6.0f)), ImVec2(Pos.x + Width, Pos.y + Height + S(6.0f)), Col(ImVec4(0, 0, 0, 1), 0.3f), Height * 0.5f);
	if(ImGui::Begin("##xe_capsule", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoScrollbar))
	{
		ImDrawList *pDraw = ImGui::GetWindowDrawList();
		const auto Separator = [&]() {
			ImGui::SameLine(0.0f, 0.0f);
			const ImVec2 P = ImGui::GetCursorScreenPos();
			pDraw->AddLine(ImVec2(P.x + Divider * 0.5f, P.y + S(7.0f)), ImVec2(P.x + Divider * 0.5f, P.y + Button - S(7.0f)), Col(t.m_Border));
			ImGui::Dummy(ImVec2(Divider, Button));
			ImGui::SameLine(0.0f, 0.0f);
		};
		// tools
		for(int Tool = 0; Tool < CEditor::XC_NUM_TOOLS; Tool++)
		{
			const char *pIcon, *pName, *pHint;
			CEditor::XcToolInfo(Tool, &pIcon, &pName, &pHint);
			char aId[16];
			str_format(aId, sizeof(aId), "##tool%d", Tool);
			if(IconButton(aId, pIcon, Tr(pHint), E.m_XcTool == Tool, true, Button))
				E.m_XcTool = Tool;
			ImGui::SameLine();
		}
		Separator();
		// the brush
		const bool Brush = !E.m_pBrush->IsEmpty();
		if(IconButton("##flipx", FontIcon::ARROWS_LEFT_RIGHT, Tr("[N] Flip the brush horizontally"), false, Brush, Button))
			for(auto &pLayer : E.m_pBrush->m_vpLayers)
				pLayer->BrushFlipX();
		ImGui::SameLine();
		if(IconButton("##flipy", FontIcon::ARROWS_UP_DOWN, Tr("[M] Flip the brush vertically"), false, Brush, Button))
			for(auto &pLayer : E.m_pBrush->m_vpLayers)
				pLayer->BrushFlipY();
		ImGui::SameLine();
		if(IconButton("##rotl", FontIcon::ARROW_ROTATE_LEFT, Tr("[R] Rotate the brush to the left"), false, Brush, Button))
			for(auto &pLayer : E.m_pBrush->m_vpLayers)
				pLayer->BrushRotate(-pi / 2.0f);
		ImGui::SameLine();
		if(IconButton("##rotr", FontIcon::ARROW_ROTATE_RIGHT, Tr("[T] Rotate the brush to the right"), false, Brush, Button))
			for(auto &pLayer : E.m_pBrush->m_vpLayers)
				pLayer->BrushRotate(pi / 2.0f);
		ImGui::SameLine();
		Separator();
		// the view
		if(IconButton("##grid", FontIcon::BORDER_ALL, Tr("Grid (Ctrl+G)"), E.MapView()->MapGrid()->IsEnabled(), true, Button))
			E.m_QuickActionToggleGrid.Call();
		ImGui::SameLine();
		if(IconButton("##zoomout", ICON_ZOOM_OUT, Tr("Zoom out"), false, true, Button))
			E.m_QuickActionZoomOut.Call();
		ImGui::SameLine();
		if(IconButton("##zoomin", ICON_ZOOM_IN, Tr("Zoom in"), false, true, Button))
			E.m_QuickActionZoomIn.Call();
		ImGui::SameLine();
		if(IconButton("##refocus", ICON_EXPAND, Tr("Back to the map (Home)"), false, true, Button))
			E.m_QuickActionRefocus.Call();
		ImGui::SameLine();
		Separator();
		// animation
		const bool Animating = E.Map()->m_EnvelopeEvaluator.m_Animate;
		if(IconButton("##animate", Animating ? FontIcon::PAUSE : FontIcon::PLAY, Tr("Play the animations (Ctrl+M)"), Animating, true, Button))
		{
			E.Map()->m_EnvelopeEvaluator.m_Animate = !Animating;
			E.Map()->m_EnvelopeEvaluator.m_AnimateStart = E.Client()->GlobalTime() - E.Map()->m_EnvelopeEvaluator.m_AnimateTime;
		}
		ImGui::SameLine();
		if(IconButton("##presets", FontIcon::FILM, Tr("Animation presets for the selected quads"), false, true, Button))
			E.XcOpenAnimate();
	}
	ImGui::End();
	ImGui::PopStyleVar(4);
	ImGui::PopStyleColor(2);
}

// ------------------------------------------------------------------------------------------------
// right panel and the status bar

void CXcEditorUi::RenderDockFrame()
{
	CEditor &E = *m_pEditor;
	const STheme &t = Theme();
	const float k = UiToPixels();
	// a thin bar over the dock with its pages and the close button
	const ImVec2 Min(m_Dock.x * k, m_Dock.y * k - S(30.0f));
	(void)Min;
	ImDrawList *pDraw = ImGui::GetBackgroundDrawList();
	pDraw->AddRectFilled(ImVec2(m_Dock.x * k, m_Dock.y * k), ImVec2((m_Dock.x + m_Dock.w) * k, (m_Dock.y + m_Dock.h) * k), Col(ImVec4(t.m_Panel.x, t.m_Panel.y, t.m_Panel.z, 1.0f)));
	pDraw->AddLine(ImVec2(m_Dock.x * k, m_Dock.y * k), ImVec2((m_Dock.x + m_Dock.w) * k, m_Dock.y * k), Col(t.m_Border));
	(void)E;
}

void CXcEditorUi::RenderStatusBar()
{
	CEditor &E = *m_pEditor;
	const STheme &t = Theme();
	const ImVec2 Display = ImGui::GetIO().DisplaySize;
	const float y = Display.y - m_StatusHeight;
	if(!BeginPanel("##xe_status", ImVec2(0.0f, y), ImVec2(Display.x, m_StatusHeight), ImVec4(t.m_Panel.x, t.m_Panel.y, t.m_Panel.z, 1.0f), ImGuiWindowFlags_NoScrollbar))
	{
		ImGui::End();
		return;
	}
	ImDrawList *pDraw = ImGui::GetWindowDrawList();
	pDraw->AddLine(ImVec2(0.0f, y), ImVec2(Display.x, y), Col(t.m_Border));
	ImFont *pFont = Fonts().m_pSmall;
	const float FontSize = pFont->FontSize * 0.92f;
	const float TextY = y + (m_StatusHeight - FontSize) * 0.5f;

	// right: position, zoom and the docks
	char aRight[128];
	const vec2 World = E.MapView()->MouseWorldPos();
	str_format(aRight, sizeof(aRight), "x %d  y %d   ·   %d%%", (int)std::floor(World.x / 32.0f), (int)std::floor(World.y / 32.0f), round_to_int(100.0f / std::max(E.MapView()->GetWorldZoom(), 0.01f)));
	float Right = Display.x - S(12.0f);
	// toggles for the docks under the map
	struct SDockButton
	{
		CQuickAction *m_pAction;
		const char *m_pLabel;
	};
	const SDockButton aDocks[] = {
		{&E.m_QuickActionHistory, Tr("History")},
		{&E.m_QuickActionServerSettings, Tr("Server settings")},
		{&E.m_QuickActionEnvelopes, Tr("Envelopes")},
	};
	for(const SDockButton &Dock : aDocks)
	{
		const float Width = pFont->CalcTextSizeA(FontSize, FLT_MAX, 0.0f, Dock.m_pLabel).x + S(16.0f);
		Right -= Width;
		const ImVec2 Min(Right, y + S(3.0f));
		const ImVec2 Max(Right + Width, y + m_StatusHeight - S(3.0f));
		ImGui::SetCursorScreenPos(Min);
		ImGui::PushID(Dock.m_pLabel);
		const bool Clicked = ImGui::InvisibleButton("##dock", ImVec2(Width, Max.y - Min.y));
		const bool Hovered = ImGui::IsItemHovered();
		ImGui::PopID();
		const bool Active = Dock.m_pAction->Active() || Dock.m_pAction->Color() == 1;
		if(Active || Hovered)
			pDraw->AddRectFilled(Min, Max, Col(Active ? t.m_Accent : t.m_Track, Active ? 0.25f : 0.7f), S(6.0f));
		pDraw->AddText(pFont, FontSize, ImVec2(Min.x + S(8.0f), TextY), Col(Active ? t.m_Text : t.m_TextSecondary), Dock.m_pLabel);
		if(Clicked)
			Dock.m_pAction->Call();
		Right -= S(4.0f);
	}
	const float RightWidth = pFont->CalcTextSizeA(FontSize, FLT_MAX, 0.0f, aRight).x;
	Right -= RightWidth + S(16.0f);
	pDraw->AddText(pFont, FontSize, ImVec2(Right, TextY), Col(t.m_TextMuted), aRight);

	// left: the hint of what is under the mouse
	if(E.m_aTooltip[0])
	{
		char aHint[256];
		XcFitText(pFont, FontSize, E.m_aTooltip, Right - S(24.0f), aHint, sizeof(aHint));
		pDraw->AddText(pFont, FontSize, ImVec2(S(12.0f), TextY), Col(t.m_TextSecondary), aHint);
	}
	ImGui::End();
}

// ------------------------------------------------------------------------------------------------
// Ctrl+K command palette

void CXcEditorUi::RenderPalette()
{
	CEditor &E = *m_pEditor;
	const STheme &t = Theme();
	struct SEntry
	{
		const char *m_pLabel;
		const char *m_pEnglish; // the search also finds the english name
		const char *m_pHint;
		bool m_Disabled;
		bool m_Active;
		std::function<void()> m_Action;
	};
	std::vector<SEntry> vEntries;
	// the own commands first, then every quick action of the editor
	vEntries.push_back({Tr("Preferences..."), "Preferences", Tr("Look of the editor: interface brightness, transparency grid, rulers, cursor."), false, false, [&]() { E.XcOpenPreferences(); }});
	vEntries.push_back({Tr("Animation presets..."), "Animation presets", Tr("Animation presets for the selected quads"), false, false, [&]() { E.XcOpenAnimate(); }});
	vEntries.push_back({Tr("Add a GIF animation..."), "Add gif", Tr("Put an animated gif on the map. It plays for every player, also in other clients."), false, false, [&]() { E.m_FileBrowser.ShowFileDialog(IStorage::TYPE_ALL, CFileBrowser::EFileType::IMAGE, "Add GIF animation", "Add", "mapres", "", CEditor::XcCallbackAddGif, &E); }});
	vEntries.push_back({Tr("Add a sound..."), "Add sound", nullptr, false, false, [&]() { E.m_FileBrowser.ShowFileDialog(IStorage::TYPE_ALL, CFileBrowser::EFileType::SOUND, "Add sound", "Add", "mapres", "", CEditor::AddSound, &E); }});
	vEntries.push_back({Tr("Properties panel"), "Properties panel", nullptr, false, m_ShowProperties, [&]() { m_ShowProperties = !m_ShowProperties; }});
	vEntries.push_back({Tr("Hide the interface"), "Hide interface", nullptr, false, false, [&]() { E.m_GuiActive = false; }});
	vEntries.push_back({Tr("Editor guide"), "Guide", nullptr, false, false, [&]() {
				    E.m_XcGuideOpen = true;
				    E.m_XcGuideStep = 0;
			    }});
	for(int Tool = 0; Tool < CEditor::XC_NUM_TOOLS; Tool++)
	{
		const char *pIcon, *pName, *pHint;
		CEditor::XcToolInfo(Tool, &pIcon, &pName, &pHint);
		vEntries.push_back({Tr(pName), pName, Tr(pHint), false, E.m_XcTool == Tool, [&E, Tool]() { E.m_XcTool = Tool; }});
	}
#define REGISTER_QUICK_ACTION(name, text, callback, disabled, active, button_color, description) \
	vEntries.push_back({Tr(E.m_QuickAction##name.Label()), E.m_QuickAction##name.Label(), Tr(E.m_QuickAction##name.Description()), E.m_QuickAction##name.Disabled(), E.m_QuickAction##name.Active(), [&E]() { E.m_QuickAction##name.Call(); }});
#include <game/editor/quick_actions.h>
#undef REGISTER_QUICK_ACTION

	std::vector<const SEntry *> vShown;
	for(const SEntry &Entry : vEntries)
	{
		if(Entry.m_Disabled)
			continue;
		if(m_aPaletteSearch[0] && !str_utf8_find_nocase(Entry.m_pLabel, m_aPaletteSearch) && !str_utf8_find_nocase(Entry.m_pEnglish, m_aPaletteSearch))
			continue;
		vShown.push_back(&Entry);
	}
	m_PaletteSelected = std::clamp(m_PaletteSelected, 0, std::max((int)vShown.size() - 1, 0));

	// at the top of the screen, like Spotlight
	const ImVec2 Display = ImGui::GetIO().DisplaySize;
	const float Width = std::min(S(620.0f), Display.x - S(40.0f));
	ImGui::SetNextWindowPos(ImVec2((Display.x - Width) * 0.5f, m_TopHeight + S(60.0f)));
	ImGui::SetNextWindowSize(ImVec2(Width, 0.0f));
	ImGui::SetNextWindowFocus();
	ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(t.m_Card.x, t.m_Card.y, t.m_Card.z, 1.0f));
	ImGui::PushStyleColor(ImGuiCol_Border, t.m_Border);
	ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, S(14.0f));
	ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 1.0f);
	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(S(12.0f), S(12.0f)));
	if(ImGui::Begin("##xe_palette", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_AlwaysAutoResize))
	{
		if(m_PaletteFocus)
		{
			ImGui::SetKeyboardFocusHere();
			m_PaletteFocus = false;
		}
		ImGui::SetNextItemWidth(Width - S(24.0f));
		ImGui::PushFont(Fonts().m_pLabel);
		if(ImGui::InputTextWithHint("##search", Tr("Type a command: save, layer, grid, zoom..."), m_aPaletteSearch, sizeof(m_aPaletteSearch)))
			m_PaletteSelected = 0;
		ImGui::PopFont();
		if(ImGui::IsKeyPressed(ImGuiKey_DownArrow))
			m_PaletteSelected = std::min(m_PaletteSelected + 1, (int)vShown.size() - 1);
		if(ImGui::IsKeyPressed(ImGuiKey_UpArrow))
			m_PaletteSelected = std::max(m_PaletteSelected - 1, 0);
		const bool Enter = ImGui::IsKeyPressed(ImGuiKey_Enter) || ImGui::IsKeyPressed(ImGuiKey_KeypadEnter);
		const bool Escape = ImGui::IsKeyPressed(ImGuiKey_Escape);
		ImGui::Dummy(ImVec2(0.0f, S(4.0f)));

		std::function<void()> Run;
		ImGui::BeginChild("##list", ImVec2(Width - S(24.0f), std::min(S(380.0f), vShown.size() * S(40.0f) + S(4.0f))), ImGuiChildFlags_None, ImGuiWindowFlags_NoBackground);
		ImDrawList *pDraw = ImGui::GetWindowDrawList();
		ImFont *pLabel = Fonts().m_pLabel;
		ImFont *pSmall = Fonts().m_pSmall;
		for(int i = 0; i < (int)vShown.size(); i++)
		{
			const SEntry &Entry = *vShown[i];
			ImGui::PushID(i);
			const ImVec2 Min = ImGui::GetCursorScreenPos();
			const float RowWidth = ImGui::GetContentRegionAvail().x;
			const ImVec2 Max(Min.x + RowWidth, Min.y + S(38.0f));
			if(ImGui::InvisibleButton("##entry", ImVec2(RowWidth, S(38.0f))))
				Run = Entry.m_Action;
			if(ImGui::IsItemHovered() && ImGui::GetIO().MouseDelta.x * ImGui::GetIO().MouseDelta.x + ImGui::GetIO().MouseDelta.y * ImGui::GetIO().MouseDelta.y > 0.0f)
				m_PaletteSelected = i;
			const bool Selected = i == m_PaletteSelected;
			if(Selected)
			{
				pDraw->AddRectFilled(Min, Max, Col(t.m_Accent, 0.2f), S(8.0f));
				ImGui::SetScrollHereY(0.5f);
			}
			pDraw->AddText(pLabel, pLabel->FontSize, ImVec2(Min.x + S(12.0f), Min.y + S(4.0f)), Col(Selected ? t.m_Text : t.m_TextSecondary), Entry.m_pLabel);
			if(Entry.m_pHint && Entry.m_pHint[0])
			{
				char aHint[256];
				XcFitText(pSmall, pSmall->FontSize * 0.9f, Entry.m_pHint, RowWidth - S(24.0f), aHint, sizeof(aHint));
				pDraw->AddText(pSmall, pSmall->FontSize * 0.9f, ImVec2(Min.x + S(12.0f), Min.y + S(4.0f) + pLabel->FontSize), Col(t.m_TextMuted), aHint);
			}
			if(Entry.m_Active)
				pDraw->AddCircleFilled(ImVec2(Max.x - S(14.0f), (Min.y + Max.y) * 0.5f), S(3.5f), Col(t.m_Accent), 12);
			if(Selected && Enter)
				Run = Entry.m_Action;
			ImGui::PopID();
		}
		if(vShown.empty())
			Description(Tr("Nothing found"));
		ImGui::EndChild();

		const bool ClickedOutside = ImGui::IsMouseClicked(ImGuiMouseButton_Left) && !ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows);
		if(Run || Escape || ClickedOutside)
			m_PaletteOpen = false;
		ImGui::End();
		if(Run)
			Run();
	}
	else
		ImGui::End();
	ImGui::PopStyleVar(3);
	ImGui::PopStyleColor(2);
}

// ------------------------------------------------------------------------------------------------
// map details

void CXcEditorUi::RenderMapDetails()
{
	CEditor &E = *m_pEditor;
	CEditorMap *pMap = E.Map();
	const STheme &t = Theme();
	if(m_MapDetailsRequest)
	{
		ImGui::OpenPopup("##xe_mapinfo");
		m_MapDetailsRequest = false;
	}
	const ImVec2 Display = ImGui::GetIO().DisplaySize;
	const float Width = std::min(S(520.0f), Display.x - S(40.0f));
	ImGui::SetNextWindowPos(ImVec2(Display.x * 0.5f, Display.y * 0.42f), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
	ImGui::SetNextWindowSize(ImVec2(Width, 0.0f));
	ImGui::PushStyleColor(ImGuiCol_PopupBg, ImVec4(t.m_Card.x, t.m_Card.y, t.m_Card.z, 1.0f));
	ImGui::PushStyleColor(ImGuiCol_ModalWindowDimBg, ImVec4(0.0f, 0.0f, 0.0f, 0.45f));
	ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, S(16.0f));
	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(S(20.0f), S(18.0f)));
	if(ImGui::BeginPopupModal("##xe_mapinfo", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_AlwaysAutoResize))
	{
		Header(Tr("Map details"));
		Description(Tr("Saved in the map file: everybody who plays the map can read it"));
		ImGui::Dummy(ImVec2(0.0f, S(6.0f)));
		CEditorMap::CMapInfo &Info = pMap->m_MapInfoTmp;
		const auto Field = [&](const char *pLabel, const char *pId, char *pBuf, int BufSize, const char *pHint) {
			ParamLabel(pLabel);
			InputText(pId, pBuf, BufSize, pHint, Width - S(40.0f));
			ImGui::Dummy(ImVec2(0.0f, S(4.0f)));
		};
		Field(Tr("Author"), "##author", Info.m_aAuthor, sizeof(Info.m_aAuthor), Tr("Your name or nickname"));
		Field(Tr("Version"), "##version", Info.m_aVersion, sizeof(Info.m_aVersion), "1.0");
		Field(Tr("Credits"), "##credits", Info.m_aCredits, sizeof(Info.m_aCredits), Tr("Who helped: testers, tileset authors"));
		Field(Tr("License"), "##license", Info.m_aLicense, sizeof(Info.m_aLicense), "CC BY-SA 4.0");
		ImGui::Dummy(ImVec2(0.0f, S(8.0f)));
		const float ButtonWidth = (Width - S(40.0f) - S(8.0f)) * 0.5f;
		bool Close = false;
		if(Button(Tr("Cancel"), false, ButtonWidth) || ImGui::IsKeyPressed(ImGuiKey_Escape))
			Close = true;
		ImGui::SameLine(0.0f, S(8.0f));
		if(Button(Tr("Save"), true, ButtonWidth) || ImGui::IsKeyPressed(ImGuiKey_Enter))
		{
			// the same as the classic popup: only a real change marks the map as modified
			const bool Different = str_comp(Info.m_aAuthor, pMap->m_MapInfo.m_aAuthor) != 0 || str_comp(Info.m_aVersion, pMap->m_MapInfo.m_aVersion) != 0 ||
					       str_comp(Info.m_aCredits, pMap->m_MapInfo.m_aCredits) != 0 || str_comp(Info.m_aLicense, pMap->m_MapInfo.m_aLicense) != 0;
			if(Different)
				pMap->OnModify();
			pMap->m_MapInfo.Copy(Info);
			Close = true;
		}
		if(Close)
			ImGui::CloseCurrentPopup();
		ImGui::EndPopup();
	}
	ImGui::PopStyleVar(2);
	ImGui::PopStyleColor(2);
	(void)E;
}

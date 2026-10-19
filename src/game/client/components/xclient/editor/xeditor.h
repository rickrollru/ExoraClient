// The new map editor interface in the XClient style, drawn with Dear ImGui like the menu and the HUD.
// The editor itself (the map, the tools, the undo history, the hotkeys) stays the DDNet one; this class
// only replaces the panels around the map: the top bar with the menus and map tabs, the layers tree on
// the left, the properties on the right, the floating tool capsule and the status bar.
#ifndef GAME_CLIENT_COMPONENTS_XCLIENT_EDITOR_XEDITOR_H
#define GAME_CLIENT_COMPONENTS_XCLIENT_EDITOR_XEDITOR_H

#include <base/vmath.h>

#include <engine/input.h>

#include <game/client/components/xclient/ui/imgui_backend.h>
#include <game/client/lineinput.h>
#include <game/client/ui.h>
#include <game/client/ui_rect.h>

#include <string>
#include <vector>

class CEditor;
struct ImDrawList;
struct ImVec2;

// While a capture is active, CEditor::DoPropertiesWithState and CEditor::DoButton_Editor draw nothing: they
// record the properties and buttons of a property popup for the new properties panel and hand back the
// change the panel made in the last frame, so the classic popup code keeps doing the work (and the undo).
struct SXcCapturedProp
{
	std::string m_Name;
	int m_Value;
	int m_Type;
	int m_Min;
	int m_Max;
};
struct SXcCapturedButton
{
	const void *m_pId;
	std::string m_Label;
	std::string m_Tooltip;
	int m_Checked;
};
struct SXcCapture
{
	bool m_Active = false;
	int m_Call = 0;
	std::vector<std::vector<SXcCapturedProp>> m_vvProps;
	std::vector<SXcCapturedButton> m_vButtons;
	int m_PendingCall = -1;
	int m_PendingProp = -1;
	int m_PendingValue = 0;
	EEditState m_PendingState = EEditState::NONE;
	const void *m_pPendingButton = nullptr;
};

class CXcEditorUi
{
public:
	void Init(CEditor *pEditor);
	bool IsReady() const { return m_Gui.IsReady(); }

	// input from the editor, before the editor UI gets it
	void OnMouseMove(vec2 MousePos);
	void OnInput(const IInput::CEvent &Event);
	void OpenMapDetails() { m_MapDetailsRequest = true; }
	// true while the mouse is over a panel, a menu or the capsule: the map must not react then
	bool WantsMouse() const { return m_WantsMouse; }

	// the whole frame of the editor with the new interface (replaces CEditor::Render)
	void Render();

private:
	CEditor *m_pEditor = nullptr;
	CImGuiBackend m_Gui;
	bool m_WantsMouse = false;
	bool m_Typing = false;
	// while an ImGui text field is active, a dummy line input is active too: the editor checks
	// CLineInput::GetActiveInput() before every hotkey, so typing a name does not paint or switch tools
	CLineInputBuffered<64> m_TypingInput;
	float m_FontScale = 0.0f;
	int m_FontFamily = -1;

	// the panels in ImGui pixels, measured every frame
	float m_TopHeight = 0.0f;
	float m_LeftWidth = 0.0f;
	float m_RightWidth = 0.0f;
	float m_StatusHeight = 0.0f;
	float m_DockHeight = 0.0f;
	// the map view between the panels in editor UI units
	CUIRect m_View = {0.0f, 0.0f, 0.0f, 0.0f};
	CUIRect m_Dock = {0.0f, 0.0f, 0.0f, 0.0f};

	// the properties panel: one capture of a classic property popup per kind
	enum
	{
		CAPTURE_QUAD = 0,
		CAPTURE_POINT,
		CAPTURE_SOURCE,
		CAPTURE_BRUSH, // the numbers of the teleporter, switch, speedup and tune brushes
		CAPTURE_LAYER,
		CAPTURE_GROUP,
		NUM_CAPTURES,
	};
	struct SCaptureState
	{
		SXcCapture m_Result; // what the popup recorded this frame
		int m_PendingCall = -1;
		int m_PendingProp = -1;
		int m_PendingValue = 0;
		EEditState m_PendingState = EEditState::NONE;
		const void *m_pPendingButton = nullptr;
		// a drag in progress: which property, so it ends with EEditState::END
		int m_EditingCall = -1;
		int m_EditingProp = -1;
		int m_EditingValue = 0;
	};
	SCaptureState m_aCaptures[NUM_CAPTURES];
	float m_CardInnerWidth = 0.0f; // the width inside the card that is drawn
	const void *m_pLastQuads = nullptr;
	const void *m_pLastBrushLayer = nullptr; // the quad popup color is taken from the quad when the selection changes
	void RunCapture(int Kind, CUi::FPopupMenuFunction pfnPopup, void *pContext);
	void SetPending(int Kind, int Call, int Prop, int Value, EEditState State);
	void RenderCaptureCard(int Kind, const char *pId, const char *pTitle, const char *pSubtitle, int SkipButtons = 0);
	void RenderProp(int Kind, int Call, int Index, const SXcCapturedProp &Prop, float LabelWidth);

	// layers panel state
	int m_RenameGroup = -1;
	int m_RenameLayer = -1;
	char m_aRename[16] = {};
	bool m_RenameFocus = false;
	char m_aLayerFilter[32] = {};
	bool m_ShowProperties = true;

	float Scale() const;
	float UiToPixels() const; // editor UI units -> ImGui pixels
	void UpdateFonts();
	void HandleTyping();

	void RenderTopBar();
	float RenderMenus(float x, float y); // returns where the menus end
	void RenderMapTabs(float x0, float x1, float y);
	void RenderLeftPanel();
	void RenderLayersTree();
	void RenderImagesPanel();
	void RenderSoundsPanel();
	void RenderRightPanel();
	void RenderCapsule();
	void RenderStatusBar();
	void RenderDockFrame();
	// Ctrl+K: every command of the editor by its name, like in Figma
	void RenderPalette();
	// map details: author, version, credits, license
	void RenderMapDetails();
	bool m_MapDetailsRequest = false;
	bool m_PaletteOpen = false;
	bool m_PaletteFocus = false;
	char m_aPaletteSearch[64] = {};
	int m_PaletteSelected = 0;

	// small widgets in the editor style
	bool IconButton(const char *pId, const char *pIcon, const char *pTooltip, bool Active = false, bool Enabled = true, float Size = 0.0f);
	bool QuickActionItem(class CQuickAction &Action, const char *pLabel = nullptr, const char *pShortcut = nullptr);
};

#endif

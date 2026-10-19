// The new map editor interface in the XClient style, drawn with Dear ImGui like the menu and the HUD.
// The editor itself (the map, the tools, the undo history, the hotkeys) stays the DDNet one; this class
// only replaces the panels around the map: the top bar with the menus and map tabs, the layers tree on
// the left, the properties on the right, the floating tool capsule and the status bar.
#ifndef GAME_CLIENT_COMPONENTS_XCLIENT_EDITOR_XEDITOR_H
#define GAME_CLIENT_COMPONENTS_XCLIENT_EDITOR_XEDITOR_H

#include <base/types.h>
#include <game/mapitems.h>
#include <base/vmath.h>

#include <engine/graphics.h>
#include <engine/input.h>

#include <game/client/components/xclient/ui/imgui_backend.h>
#include <game/client/lineinput.h>
#include <game/client/ui.h>
#include <game/client/ui_rect.h>

#include <memory>
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
	// an error or a notice of the editor, shown in a dialog of the new interface
	void ShowMessage(const char *pMessage);
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
		CAPTURE_EVENT, // the buttons of the event popup (unsaved changes, warnings)
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
	CUi::EPopupMenuFunctionResult RunCapture(int Kind, CUi::FPopupMenuFunction pfnPopup, void *pContext, bool Active = true);
	void SetPending(int Kind, int Call, int Prop, int Value, EEditState State);
	void RenderCaptureCard(int Kind, const char *pId, const char *pTitle, const char *pSubtitle, int SkipButtons = 0);
	void RenderProp(int Kind, int Call, int Index, const SXcCapturedProp &Prop, float LabelWidth);
	// the tile under the mouse (stays on the last tile while the mouse is over the panels)
	int m_InspectX = -1;
	int m_InspectY = -1;
	void RenderInspector(const std::shared_ptr<class CLayerTiles> &pLayer);

public:
	// prefabs: tile layers of the brush saved in xclient/prefabs (xeditor_prefabs.cpp)
	struct SPrefabTile
	{
		int m_Index = 0;
		int m_Flags = 0;
		int m_aExtra[4] = {}; // the data of the tele, switch, speedup or tune tile
	};
	struct SPrefabLayer
	{
		std::string m_Kind; // tiles, game, front, tele, switch, speedup, tune
		int m_Width = 0;
		int m_Height = 0;
		CColor m_Color;
		std::string m_Image; // the name of the picture, found again by name in the map
		std::vector<SPrefabTile> m_vTiles;
	};
	struct SPrefab
	{
		std::string m_Name;
		std::vector<SPrefabLayer> m_vLayers;
		// a small picture made once when the prefab is read: the tile index of each block, 0 is empty
		int m_PreviewWidth = 0;
		int m_PreviewHeight = 0;
		std::vector<int> m_vPreview;
		std::vector<bool> m_vPreviewGame;
	};

private:
	std::vector<SPrefab> m_vPrefabs;
	bool m_PrefabsScanned = false;
	bool m_ShowPrefabs = false;
	char m_aPrefabName[64] = {};
	void ScanPrefabs();
	bool ReadPrefab(SPrefab &Prefab);
	bool SavePrefab(const char *pName);
	void UsePrefab(const SPrefab &Prefab);

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
	void RenderPrefabsPanel();
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
	// open / save / add an image or a sound: the classic file browser in the new style
	void RenderFileDialog();
	bool m_FileDialogShown = false;
	bool m_FileFocus = false;
	char m_aFileFilter[IO_MAX_PATH_LENGTH] = {};
	char m_aFileName[IO_MAX_PATH_LENGTH] = {};
	// the new folder and delete cards over the file dialog
	enum
	{
		FILESUB_NONE = 0,
		FILESUB_NEW_FOLDER,
		FILESUB_DELETE,
		FILESUB_OVERWRITE,
	};
	int m_FileSub = FILESUB_NONE;
	bool m_FileSubFocus = false;
	char m_aNewFolder[128] = {};
	char m_aFileDelete[IO_MAX_PATH_LENGTH] = {};
	bool m_FileDeleteIsDir = false;
	void RenderFileSubDialog();

	// questions and warnings of the editor (unsaved changes, remove a used image...) and error messages
	bool m_EventOpen = false;
	bool m_EventCloseAfterPress = false;
	std::vector<std::string> m_vMessages;
	void RenderEventDialog();
	void RenderMessages();
	bool DialogOpen() const { return m_EventOpen || !m_vMessages.empty(); }

	// the docks under the map: server settings and history (the envelopes stay classic until the timeline)
	char m_aSettingInput[256] = {};
	bool m_SettingFocus = false;
	bool m_SettingTyping = false;
	bool m_SettingsDockOpen = false; // the field gets the keyboard when the dock opens
	bool BeginDock(const char *pId, int ExtraEditor, ImVec2 &Min, ImVec2 &Max);
	void RenderServerSettingsDock();
	void RenderHistoryDock();
	// the envelopes as a timeline (xeditor_envelopes.cpp)
	std::shared_ptr<class CEnvelope> m_pEnvLast;
	int m_EnvSelPoint = -1;
	float m_EnvScroll = 0.0f; // the first second shown
	float m_EnvView = 5.0f; // how many seconds are shown
	bool m_EnvDragging = false;
	CFixedTime m_EnvDragOld;
	int m_EnvValueOld = 0;
	int m_EnvRename = -1;
	bool m_EnvRenameFocus = false;
	char m_aEnvRename[32] = {};
	void RenderEnvelopesDock();
	bool m_PaletteOpen = false;
	bool m_PaletteFocus = false;
	char m_aPaletteSearch[64] = {};
	int m_PaletteSelected = 0;

	// the picture card with the color correction (xeditor_images.cpp)
	const class CEditorImage *m_pAdjustImage = nullptr;
	int m_aAdjust[4] = {}; // brightness, contrast, saturation in -100..100, hue in degrees
	std::vector<uint8_t> m_vAdjustBackup; // the pixels before the last apply, for one undo
	IGraphics::CTextureHandle m_ImagePreview;
	void RenderImageCard();
	void AdjustPixels(uint8_t *pData, size_t NumPixels) const;
	void UpdateImagePreview(const class CEditorImage *pImage);
	void ReloadImageTexture(class CEditorImage *pImage);

	// map themes: a background group with a gradient (xeditor_images.cpp)
	bool m_ThemesRequest = false;
	void RenderThemes();
	void AddThemeBackground(int Theme);

	// small widgets in the editor style
	bool IconButton(const char *pId, const char *pIcon, const char *pTooltip, bool Active = false, bool Enabled = true, float Size = 0.0f);
	bool QuickActionItem(class CQuickAction &Action, const char *pLabel = nullptr, const char *pShortcut = nullptr);
};

#endif

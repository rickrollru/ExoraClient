#ifndef GAME_CLIENT_COMPONENTS_XCLIENT_UI_MENU_H
#define GAME_CLIENT_COMPONENTS_XCLIENT_UI_MENU_H

#include "backgrounds.h"
#include "imgui_backend.h"
#include "player_stats.h"

#include <engine/console.h>

#include <game/client/component.h>

#include <memory>
#include <string>
#include <vector>

class CServerInfo;
class IHttpRequest;
class CFsFileInfo;

struct ImVec2;

// The new XClient menu (Dear ImGui). Replaces the classic menu on Esc and in the main menu,
// the classic one stays reachable with a button.
class CXcMenu : public CComponent
{
public:
	enum
	{
		PAGE_HOME = 0,
		PAGE_SERVERS,
		PAGE_VISUALS,
		PAGE_CONFIGS,
		PAGE_DEMOS,
		PAGE_SETTINGS,
		NUM_PAGES,
	};

	enum
	{
		SETTINGS_INTERFACE = 0,
		SETTINGS_BACKGROUND,
		SETTINGS_PLAYER,
		SETTINGS_CONTROLS,
		SETTINGS_SOUND,
		SETTINGS_CHAT,
		SETTINGS_GRAPHICS,
		SETTINGS_HUD,
		NUM_SETTINGS,
	};

	enum
	{
		VISUALS_TEE = 0,
		VISUALS_MOVEMENT,
		VISUALS_WEAPONS,
		VISUALS_WORLD,
		VISUALS_COSMETICS,
		NUM_VISUALS,
	};

	int Sizeof() const override { return sizeof(*this); }
	void OnConsoleInit() override;
	void OnInit() override;
	void OnShutdown() override;
	bool OnInput(const IInput::CEvent &Event) override;
	bool OnCursorMove(float x, float y, IInput::ECursorType CursorType) override;
	void OnRender() override;
	void OnStateChange(int NewState, int OldState) override;

	bool IsActive() const { return m_Active; }
	void SetActive(bool Active);

private:
	CImGuiBackend m_Backend;
	int m_aUiSounds[6] = {-1, -1, -1, -1, -1, -1}; // XcUi::NUM_SOUNDS
	float m_aUiSoundTime[6] = {};
	static void PlayUiSoundCallback(void *pUser, int Sound);
	bool m_Active = false;
	float m_OpenAnimation = 0.0f;
	int m_Page = PAGE_HOME;
	int m_SettingsSection = SETTINGS_INTERFACE;
	int m_BindWaiting = -1; // index of the bind that waits for a key
	float m_ScrollTarget = 0.0f;
	bool m_WantClassic = false;

	// servers page
	char m_aServerSearch[64] = {};
	int m_ServerFilter = 0;
	std::string m_SelectedServer; // address
	std::vector<const CServerInfo *> m_vServers;
	bool m_ServersRequested = false;

	// configs page
	struct SConfigEntry
	{
		std::string m_Name;
		long long m_Modified;
	};
	std::vector<SConfigEntry> m_vConfigs;
	bool m_ConfigsDirty = true;
	std::string m_SelectedConfig;
	std::string m_SelectedCode; // export code of the selected config
	char m_aNewConfigName[32] = {};
	char m_aImportCode[4096] = {};
	char m_aRenameBuffer[32] = {};
	std::string m_RenameFor; // config the rename field was filled for
	float m_DeleteArmedUntil = 0.0f;

	// menu background (menu_bg.cpp)
	enum
	{
		BG_ANIMATED = 0,
		BG_BLUR_GAME,
		BG_MAP, // the menu map offline, the game without blur online
		BG_CUSTOM,
	};
	CXcBackgrounds m_Backgrounds;
	bool m_BackgroundsScanned = false;
	char m_aBgPath[512] = {};
	// textures must not be unloaded while the ImGui frame that draws them is being built,
	// so adding and deleting happen at the start of the next frame
	std::string m_BgPendingAdd;
	std::string m_BgPendingRemove; // "f:<file>" or "m:<map>"
	std::string m_BgDeleteArmed; // "f:<file>" or "m:<map>" waiting for the second click
	float m_BgDeleteArmedUntil = 0.0f;
	int EffectiveBackground() const;
	void HandleBackgroundFile(const char *pPath);
	void RemoveBackground(const std::string &Key);
	bool BackgroundTile(const char *pId, IGraphics::CTextureHandle Texture, float TextureAspect, const char *pName, const char *pBadge, bool Selected, bool Deletable, bool *pDelete, float Width, float UMin = 0.0f, float UMax = 1.0f, float TileAspect = 16.0f / 9.0f, bool Contain = false);
	void RenderSettingsBackground();

	// theme and performance presets (menu_presets.cpp)
	struct SThemePreset
	{
		std::string m_Name;
		int m_Theme = 0;
		int m_Accent = 0;
		int m_Font = 0;
		int m_Background = 1;
		int m_Dim = 25;
		std::string m_BgFile;
		std::string m_MenuMap;
		bool m_Builtin = false;
	};
	std::vector<SThemePreset> m_vThemePresets;
	bool m_ThemePresetsLoaded = false;
	char m_aPresetName[32] = {};
	std::string m_PresetDeleteArmed;
	float m_PresetDeleteArmedUntil = 0.0f;
	void LoadThemePresets();
	void SaveThemePresets();
	bool ThemePresetActive(const SThemePreset &Preset) const;
	void ApplyThemePreset(const SThemePreset &Preset);
	void RenderThemePresets();
	int CurrentPerformancePreset() const;
	void RenderPerformance();

	// last applied style values, the style and fonts are rebuilt when they change
	int m_AppliedTheme = -1;
	int m_AppliedAccent = -1;
	float m_AppliedScale = -1.0f;

	static void ConToggleMenu(IConsole::IResult *pResult, void *pUserData);

	bool IsOnline() const;
	float UiScale() const;
	void UpdateStyle();
	void RenderBackground();
	bool AnimatedBackground() const;
	void RenderAnimatedBackground(struct ImDrawList *pDraw, float Alpha);
	float m_BackgroundTime = 0.0f;
	void RenderFrame();
	void RenderTopBar(float Width);
	void RenderHome();
	void RenderMusicCard(); // menu_music.cpp
	// the tee is drawn by DDNet in the middle of the ImGui frame through a draw callback
	struct STeeDraw
	{
		CXcMenu *m_pMenu;
		float m_aRect[4]; // x0, y0, x1, y1 in pixels
		bool m_Still; // looks to the right without bobbing (hat editor)
	};
	static void TeePreviewCallback(const struct ImDrawList *pList, const struct ImDrawCmd *pCmd);
	void DrawTeePreview(const float *pRect, bool Still);
	void AddTeePreview(struct ImDrawList *pDraw, const ImVec2 &Min, const ImVec2 &Max, bool Still = false);

	// player stats from ddnet.org (menu_home.cpp)
	enum
	{
		STATS_LOADING = 0,
		STATS_LOADED,
		STATS_UNKNOWN, // no such player on ddnet.org
		STATS_FAILED,
	};
	std::shared_ptr<IHttpRequest> m_pStatsTask;
	char m_aStatsName[64] = {};
	int m_StatsState = STATS_LOADING;
	float m_StatsTime = 0.0f;
	SPlayerStats m_Stats;
	void UpdatePlayerStats();
	void RenderPlayerStats();

	// search (menu_search.cpp)
	bool m_SearchOpen = false;
	bool m_SearchFocus = false;
	char m_aSearch[64] = {};
	int m_SearchSelected = 0;
	bool m_ScrollToHighlight = false;
	void OpenSearch();
	void RenderSearch();
	void RenderServers();
	void UpdateServerList();
	bool RenderServerCard(const CServerInfo *pInfo, float Width, float Height);
	void RenderServerDetails(const CServerInfo *pInfo, float Height);
	// visuals page (menu_visuals.cpp)
	int m_VisualsSection = VISUALS_TEE;
	int m_aLastFxStyle[32] = {}; // last chosen style of each style card, to turn it back on
	bool StyleCard(const char *pId, const char *pTitle, const char *pDescription, const char *pConfig, int *pValue, const char *const *ppStyles, int NumStyles);
	void RenderVisualsTee();
	void RenderVisualsMovement();
	void RenderVisualsWeapons();
	void RenderVisualsWorld();
	// cosmetics editor (menu_cosmetics.cpp): own hats from pictures and gifs
	void RenderVisualsCosmetics();
	void RenderHatEditor(float Size);
	void HandleHatFile(const char *pPath);
	bool m_HatsScanned = false;
	char m_aHatPath[512] = {};
	std::string m_HatPendingAdd;
	std::string m_HatPendingRemove;
	int m_HatDrag = 0; // 0 none, 1 move, 2 resize, 3 rotate
	float m_aHatDragStart[4] = {}; // x, y, size, rotation when the drag started
	float m_aHatDragMouse[2] = {};
	bool m_WheelCaptured = false; // the hat preview uses the mouse wheel, the page must not scroll
	void RenderVisuals();
	void RenderConfigs();

	// demos page (menu_demos.cpp)
	struct SDemoEntry
	{
		std::string m_File; // file or folder name
		std::string m_Name; // shown, without .demo
		bool m_IsDir = false;
		int m_StorageType = 0;
		int64_t m_Date = 0;
		bool m_InfoLoaded = false;
		bool m_Valid = false;
		char m_aMap[64] = {};
		int m_Length = 0; // seconds
		int m_Markers = 0;
		int64_t m_Size = 0;
		char m_aTimestamp[20] = {};
	};
	std::vector<SDemoEntry> m_vDemos;
	std::vector<int> m_vDemoView; // indices into m_vDemos after search and sort
	bool m_DemosDirty = true;
	bool m_DemosViewDirty = true;
	std::string m_DemoFolder;
	std::string m_SelectedDemo;
	char m_aDemoSearch[64] = {};
	int m_DemoSort = 0;
	char m_aDemoRename[128] = {};
	std::string m_DemoRenameFor;
	std::string m_DemoDeleteArmed;
	float m_DemoDeleteArmedUntil = 0.0f;
	static int DemoListCallback(const CFsFileInfo *pInfo, int IsDir, int StorageType, void *pUser);
	void RefreshDemos();
	void DemoPath(const SDemoEntry &Entry, char *pBuf, int BufSize) const;
	void LoadDemoInfo(SDemoEntry &Entry);
	void UpdateDemoView();
	bool RenderDemoCard(int Index, float Width, float Height);
	void RenderDemoDetails(SDemoEntry *pEntry, float Height);
	void PlayDemo(const SDemoEntry &Entry);
	void RenderDemos();
	void RefreshConfigList();
	void ConfigPath(const char *pName, char *pBuf, int BufSize) const;
	std::string CurrentConfigText() const;
	bool WriteConfig(const char *pName, const std::string &Text);
	bool ReadConfig(const char *pName, std::string &Text) const;
	static std::string EncodeConfig(const std::string &Text);
	static bool DecodeConfig(const char *pCode, std::string &Text);
	static std::string FilterConfig(const std::string &Text);
	void RenderPlaceholder(const char *pIcon, const char *pTitle, const char *pText, bool ClassicButton);
	void BeginSmoothScroll(const char *pId, ImVec2 Size);
	void EndSmoothScroll();
	void OpenClassicMenu();

	// settings (menu_settings.cpp)
	void RenderSettings();
	void RenderSettingsInterface();
	void RenderSettingsPlayer();
	void RenderSettingsControls();
	void RenderSettingsSound();
	void RenderSettingsChat();
	void RenderSettingsGraphics();
	void RenderSettingsHud();
	bool OnBindInput(const IInput::CEvent &Event);

	// config helpers: go through the console so change callbacks (sending player info...) run
	void SetConfigInt(const char *pName, int Value);
	void SetConfigString(const char *pName, const char *pValue);
	bool ConfigToggle(const char *pId, const char *pName, int *pValue);
	bool ConfigSlider(const char *pLabel, const char *pName, int *pValue, int Min, int Max, const char *pFormat = "%d");
	bool ConfigSegmented(const char *pId, const char *pName, int *pValue, const char *const *ppItems, int NumItems, const int *pValues = nullptr);
	void ConfigText(const char *pLabel, const char *pName, const char *pValue, int MaxLength);
	void ToggleRow(const char *pLabel, const char *pName, int *pValue);
};

#endif

#ifndef GAME_CLIENT_COMPONENTS_XCLIENT_UI_MENU_H
#define GAME_CLIENT_COMPONENTS_XCLIENT_UI_MENU_H

#include "backgrounds.h"
#include "imgui_backend.h"

#include <engine/console.h>

#include <game/client/component.h>

#include <string>
#include <vector>

class CServerInfo;

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
	bool BackgroundTile(const char *pId, IGraphics::CTextureHandle Texture, float TextureAspect, const char *pName, const char *pBadge, bool Selected, bool Deletable, bool *pDelete, float Width, float UMin = 0.0f, float UMax = 1.0f);
	void RenderSettingsBackground();

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
	// the tee is drawn by DDNet in the middle of the ImGui frame through a draw callback
	struct STeeDraw
	{
		CXcMenu *m_pMenu;
		float m_aRect[4]; // x0, y0, x1, y1 in pixels
	};
	static void TeePreviewCallback(const struct ImDrawList *pList, const struct ImDrawCmd *pCmd);
	void DrawTeePreview(const float *pRect);
	void AddTeePreview(struct ImDrawList *pDraw, const ImVec2 &Min, const ImVec2 &Max);

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
	void RenderVisuals();
	void RenderConfigs();
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

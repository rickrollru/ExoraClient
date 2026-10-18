#ifndef GAME_CLIENT_COMPONENTS_XCLIENT_UI_MENU_H
#define GAME_CLIENT_COMPONENTS_XCLIENT_UI_MENU_H

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
		PAGE_SETTINGS,
		NUM_PAGES,
	};

	enum
	{
		SETTINGS_INTERFACE = 0,
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

	// last applied style values, the style and fonts are rebuilt when they change
	int m_AppliedTheme = -1;
	int m_AppliedAccent = -1;
	float m_AppliedScale = -1.0f;

	static void ConToggleMenu(IConsole::IResult *pResult, void *pUserData);

	bool IsOnline() const;
	float UiScale() const;
	void UpdateStyle();
	void RenderBackground();
	void RenderFrame();
	void RenderTopBar(float Width);
	void RenderHome();
	void RenderServers();
	void UpdateServerList();
	bool RenderServerCard(const CServerInfo *pInfo, float Width, float Height);
	void RenderServerDetails(const CServerInfo *pInfo, float Height);
	void RenderVisuals();
	void RenderConfigs();
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

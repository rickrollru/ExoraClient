#ifndef GAME_CLIENT_COMPONENTS_XCLIENT_HUD_XC_HUD_H
#define GAME_CLIENT_COMPONENTS_XCLIENT_HUD_XC_HUD_H

#include <engine/console.h>
#include <engine/external/imgui/imgui.h>

#include <game/client/component.h>
#include <game/client/components/xclient/ui/imgui_backend.h>

// The XClient HUD: watermark, race timer, speed, weapon and in-game notifications, drawn with
// Dear ImGui in its own context. Every element can be moved and resized in the HUD editor.
class CXcHud : public CComponent
{
	// the frame rate in the watermark, averaged
	int m_FpsShown = 0;
	int m_FpsFrames = 0;
	float m_FpsTime = 0.0f;

public:
	enum
	{
		ELEMENT_WATERMARK = 0,
		ELEMENT_TIMER,
		ELEMENT_SPEED,
		ELEMENT_WEAPON,
		ELEMENT_MUSIC,
		ELEMENT_KEYS,
		ELEMENT_MINITOP,
		ELEMENT_EFFECTS,
		NUM_ELEMENTS,
	};

	int Sizeof() const override { return sizeof(*this); }
	void OnConsoleInit() override;
	void OnInit() override;
	void OnShutdown() override;
	void OnRender() override;
	bool OnInput(const IInput::CEvent &Event) override;
	bool OnCursorMove(float x, float y, IInput::ECursorType CursorType) override;
	void OnStateChange(int NewState, int OldState) override;

	bool IsEditing() const { return m_Editing; }
	void SetEditing(bool Editing);
	// the classic HUD hides what the new one shows
	bool HidesClassicTimer() const;
	// the classic weapons, jumps and player state in the corner are hidden (xc_hud_hide_classic)
	bool HidesClassicState() const;
	// interface units the classic top left HUD (ammo, player state) moves down to make room for the watermark
	float ClassicTopLeftShift(float InterfaceHeight) const;

	// shared look for the emote wheel and the spectator menu (xc_hud_wheels)
	void RenderWheelBackground(vec2 Center, float InnerRadius, float OuterRadius, int NumItems, int Selected, float Alpha) const;
	ColorRGBA WheelPanelColor() const;
	ColorRGBA WheelSelectColor() const;
	// the scoreboard in the colors of the menu (xc_hud_scoreboard)
	ColorRGBA ScoreboardPanelColor() const;
	ColorRGBA ScoreboardHighlightColor() const;
	ColorRGBA ScoreboardHoverColor() const;
	// the chat in the colors of the HUD (xc_chat_style); Kind 1 = mention or input, 2 = team, 3 = whisper, 4 = server
	ColorRGBA ChatLineColor() const;
	ColorRGBA ChatMarkColor(int Kind) const;

	// the own scoreboard replaces the classic one (xc_scoreboard)
	bool DrawsScoreboard() const;

	// big banner at the top: finish time, new records (Kind 0 = finish, 1 = personal record, 2 = server record)
	void ShowBanner(const char *pTitle, const char *pSubtitle, int Kind);

private:
	struct SElementConfig
	{
		const char *m_pName;
		int *m_pEnabled;
		int *m_pX;
		int *m_pY;
		int *m_pScale;
		int m_DefaultX;
		int m_DefaultY;
	};

	CImGuiBackend m_Backend;
	bool m_Editing = false;
	int m_Dragging = -1;
	float m_aDragOffset[2] = {};
	bool m_aSnapped[2] = {};
	float m_aaRects[NUM_ELEMENTS][4] = {}; // last drawn rect of every element (x0, y0, x1, y1)

	static void ConEditHud(IConsole::IResult *pResult, void *pUserData);
	static void ConFocus(IConsole::IResult *pResult, void *pUserData);
	static void ConFriendAim(IConsole::IResult *pResult, void *pUserData);

	SElementConfig Element(int Index) const;
	bool Visible() const;
	float Unit(int Element) const;
	ImVec2 Center(int Element) const;

	void RenderPlate(ImDrawList *pDraw, ImVec2 Min, ImVec2 Max, float Unit, float Alpha) const;
	void RenderWatermark(ImDrawList *pDraw, float Alpha);
	void RenderTimer(ImDrawList *pDraw, float Alpha);
	void RenderSpeed(ImDrawList *pDraw, float Alpha);
	void RenderWeapon(ImDrawList *pDraw, float Alpha);
	void RenderMusic(ImDrawList *pDraw, float Alpha);
	void RenderBanner(ImDrawList *pDraw);
	void RenderKeys(ImDrawList *pDraw, float Alpha);
	// xc_scoreboard.cpp
	void RenderScoreboard(ImDrawList *pDraw, float Dt);
	void RenderMiniTop(ImDrawList *pDraw, float Alpha);
	int PlayerTimeMillis(const struct CNetObj_PlayerInfo *pInfo, bool *pCentis) const;
	static void TeeCallback(const ImDrawList *pList, const ImDrawCmd *pCmd);
	void AddTee(ImDrawList *pDraw, int ClientId, ImVec2 Center, float Size, float Alpha);
	float m_ScoreboardOpen = 0.0f;
	// with the scoreboard mouse unlocked (like the classic one): a click on a player opens a small popup
	bool ScoreboardMouse() const;
	void RenderScoreboardPopup(ImDrawList *pDraw, float U, float Alpha);
	int m_ScoreboardPopupId = -1;
	float m_aScoreboardPopupPos[2] = {};
	float m_aScoreboardPopupRect[4] = {}; // x0, y0, x1, y1 of the popup drawn last frame
	// elements fade in and out when they are turned on or off, the whole HUD while the menu opens
	float m_aShown[NUM_ELEMENTS] = {};
	float m_MenuFade = 1.0f;
	float m_ScoreboardOpenTime = 0.0f;
	float m_aKeyPress[5] = {};
	char m_aBannerTitle[128] = {};
	char m_aBannerSubtitle[128] = {};
	int m_BannerKind = 0;
	float m_BannerStart = -100.0f;
	float m_MusicAlpha = 0.0f;
	void RenderEditor(ImDrawList *pDraw);
	// xc_effects.cpp: active states of the tee (freeze, jetpack, endless hook...) like potion effects
	enum
	{
		EFFECT_FREEZE = 0,
		EFFECT_DEEP_FREEZE,
		EFFECT_LIVE_FREEZE,
		EFFECT_NINJA,
		EFFECT_JETPACK,
		EFFECT_ENDLESS_HOOK,
		EFFECT_ENDLESS_JUMP,
		EFFECT_SUPER,
		EFFECT_INVINCIBLE,
		EFFECT_SOLO,
		EFFECT_NO_COLLISION,
		EFFECT_NO_HOOK,
		EFFECT_NO_HAMMER,
		EFFECT_NO_SHOOTING,
		EFFECT_TELEGUN,
		EFFECT_PRACTICE,
		EFFECT_LOCK,
		EFFECT_TEAM0,
		NUM_EFFECTS,
	};
	float m_aEffectShown[NUM_EFFECTS] = {};
	bool m_aEffectWasActive[NUM_EFFECTS] = {};
	int m_EffectsClientId = -1;
	void RenderEffects(ImDrawList *pDraw, float Alpha, float Dt);
	void DrawEffectIcon(ImDrawList *pDraw, int Effect, ImVec2 Min, float Size, float Alpha);

	// helpers
	int LocalClientId() const;
	int RaceTimeCentis(bool *pRacing) const;
	float SpeedBps() const;
	ImVec2 PlaceRect(int Element, ImVec2 Size);
};

// text cut with "…" to fit MaxWidth
void XcFitText(ImFont *pFont, float Size, const char *pText, float MaxWidth, char *pBuf, int BufSize);

#endif

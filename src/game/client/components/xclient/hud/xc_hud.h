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
public:
	enum
	{
		ELEMENT_WATERMARK = 0,
		ELEMENT_TIMER,
		ELEMENT_SPEED,
		ELEMENT_WEAPON,
		ELEMENT_MUSIC,
		ELEMENT_KEYS,
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
	// interface units the classic top left HUD (ammo, player state) moves down to make room for the watermark
	float ClassicTopLeftShift(float InterfaceHeight) const;

	// shared look for the emote wheel and the spectator menu (xc_hud_wheels)
	void RenderWheelBackground(vec2 Center, float InnerRadius, float OuterRadius, int NumItems, int Selected, float Alpha) const;
	ColorRGBA WheelPanelColor() const;
	ColorRGBA WheelSelectColor() const;

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
	float m_aKeyPress[5] = {};
	char m_aBannerTitle[128] = {};
	char m_aBannerSubtitle[128] = {};
	int m_BannerKind = 0;
	float m_BannerStart = -100.0f;
	float m_MusicAlpha = 0.0f;
	void RenderEditor(ImDrawList *pDraw);

	// helpers
	int LocalClientId() const;
	int RaceTimeCentis(bool *pRacing) const;
	float SpeedBps() const;
	ImVec2 PlaceRect(int Element, ImVec2 Size);
};

#endif

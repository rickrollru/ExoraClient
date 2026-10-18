#ifndef GAME_CLIENT_COMPONENTS_XCLIENT_UI_FEEDBACK_H
#define GAME_CLIENT_COMPONENTS_XCLIENT_UI_FEEDBACK_H

struct ImDrawList;
struct ImFont;
struct ImVec2;

// Interface sounds and toast notifications. Widgets and pages only call PlayUiSound / Toast,
// the menu component plays the samples and draws the toasts.
namespace XcUi
{
	enum
	{
		SOUND_CLICK = 0,
		SOUND_TAB,
		SOUND_TOGGLE_ON,
		SOUND_TOGGLE_OFF,
		SOUND_NOTIFY,
		SOUND_ERROR,
		NUM_SOUNDS,
	};
	typedef void (*FSoundPlayer)(void *pUser, int Sound);
	void SetSoundPlayer(FSoundPlayer pfnPlayer, void *pUser);
	void PlayUiSound(int Sound);

	enum
	{
		TOAST_SUCCESS = 0,
		TOAST_INFO,
		TOAST_ERROR,
	};
	// shows a short message at the bottom of the screen for a few seconds
	void Toast(const char *pText, int Kind = TOAST_SUCCESS);
	// Time: seconds from a clock shared by everyone who draws toasts (the client's global time).
	// The fonts default to the menu fonts; the HUD passes its own (it has another ImGui context).
	void RenderToasts(ImDrawList *pDraw, const ImVec2 &DisplaySize, float Time, float Alpha, ImFont *pTextFont = nullptr, ImFont *pIconFont = nullptr);
	bool HasToasts();
} // namespace XcUi

#endif

#ifndef GAME_CLIENT_COMPONENTS_XCLIENT_UI_THEME_H
#define GAME_CLIENT_COMPONENTS_XCLIENT_UI_THEME_H

#include <engine/external/imgui/imgui.h>

// Colors and sizes of the XClient menu, taken from the design spec (CLAUDE.md).
namespace XcUi
{
	enum
	{
		THEME_DARK = 0,
		THEME_LIGHT,
	};

	enum
	{
		ACCENT_BLUE = 0,
		ACCENT_MINT,
		ACCENT_ORANGE,
		ACCENT_PINK,
		ACCENT_LIME,
		ACCENT_RED,
		ACCENT_WHITE,
		ACCENT_CUSTOM, // xc_ui_accent_color
		NUM_ACCENTS,
	};

	struct STheme
	{
		bool m_Dark = true;
		ImVec4 m_Background; // behind everything
		ImVec4 m_Panel; // big panels (top bar, side panels)
		ImVec4 m_Card; // setting cards
		ImVec4 m_CardHover;
		ImVec4 m_Border; // 1px outlines
		ImVec4 m_Text; // titles and values
		ImVec4 m_TextSecondary; // parameter names
		ImVec4 m_TextMuted; // descriptions
		ImVec4 m_Accent;
		ImVec4 m_OnAccent; // text and icons on top of the accent color
		ImVec4 m_Track; // toggle and slider background
		ImVec4 m_Shadow;
	};

	const STheme &Theme();
	// colors of a theme without making it the current one (previews)
	STheme MakeTheme(int Theme, int Accent);
	void SetTheme(int Theme, int Accent);
	ImVec4 AccentColor(int Accent, bool Dark);

	// UI scale: 1 at 1080p, everything (fonts, paddings, rounding) is multiplied by it
	float Scale();
	void SetScale(float Scale);
	inline float S(float Value) { return Value * Scale(); }

	// Animation speed from the user setting, 0 = no animations
	float AnimationSpeed();
	void SetAnimationSpeed(float Speed);

	// Applies colors and sizes to ImGuiStyle
	void ApplyStyle();

	ImU32 Col(const ImVec4 &Color, float AlphaMul = 1.0f);
	ImVec4 Mix(const ImVec4 &a, const ImVec4 &b, float t);
} // namespace XcUi

#endif

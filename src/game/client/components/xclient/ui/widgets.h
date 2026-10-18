#ifndef GAME_CLIENT_COMPONENTS_XCLIENT_UI_WIDGETS_H
#define GAME_CLIENT_COMPONENTS_XCLIENT_UI_WIDGETS_H

#include <engine/external/imgui/imgui.h>

// XClient widgets in the style of the design spec. Every state change is animated,
// the speed comes from the animation setting (instant when animations are off).
namespace XcUi
{
	struct SFonts
	{
		ImFont *m_pBody = nullptr;
		ImFont *m_pLabel = nullptr;
		ImFont *m_pTitle = nullptr;
		ImFont *m_pHeader = nullptr;
		ImFont *m_pSmall = nullptr;
		ImFont *m_pIcons = nullptr;
	};
	void SetFonts(const SFonts &Fonts);
	const SFonts &Fonts();

	// Smoothly moves a stored value towards Target. Returns the current value.
	float Animate(ImGuiID Id, float Target, float Speed = 14.0f);
	void ResetAnimations();

	// --- text
	void Header(const char *pText);
	void Title(const char *pText);
	// width left for a widget: inside a card up to the card's inner edge, else the window region
	float AvailWidth();
	void Description(const char *pText, float WrapWidth = 0.0f);
	void ParamLabel(const char *pText);

	// --- controls
	bool Toggle(const char *pId, bool *pValue);
	bool ToggleInt(const char *pId, int *pValue);
	// full width slider: name on the left, value on the right, track below
	bool SliderInt(const char *pLabel, int *pValue, int Min, int Max, const char *pFormat = "%d");
	bool SliderFloat(const char *pLabel, float *pValue, float Min, float Max, const char *pFormat = "%.2f");
	// 2-4 options side by side in a capsule, the highlight slides to the selected one
	bool Segmented(const char *pId, int *pCurrent, const char *const *ppItems, int NumItems);
	// small key plate, shows "..." while waiting for a key
	bool BindChip(const char *pId, const char *pKeyName, bool Waiting);
	bool Button(const char *pLabel, bool Primary = false, float Width = 0.0f);
	bool IconButton(const char *pIcon, const char *pTooltip, bool Selected = false);
	// colored circles to pick one of the given colors
	bool ColorSwatches(const char *pId, int *pCurrent, const ImVec4 *pColors, int NumColors);
	bool InputText(const char *pLabel, char *pBuffer, int BufferSize, const char *pHint = nullptr, float Width = 0.0f); // Width 0: all the space left
	// effect color: accent / own / rainbow / skin modes, swatches and a hue bar for the own color (packed HSL)
	bool EffectColorPicker(const char *pId, int *pMode, unsigned *pColor);
	// three chips: show an effect for me, my dummy, other players
	bool TargetChips(const char *pId, int *pSelf, int *pDummy, int *pOthers);
	// a labeled row: name on the left, the next widget on the right
	void RowLabel(const char *pLabel, float ControlWidth);

	// --- layout
	// capsule with icon tabs, the highlight slides between them
	bool TabCapsule(const char *pId, int *pCurrent, const char *const *ppIcons, const char *const *ppLabels, int NumTabs);
	// vertical list of sections
	bool SideNav(const char *pId, int *pCurrent, const char *const *ppIcons, const char *const *ppLabels, int NumItems, float Width);

	// Two column masonry grid of cards (like Space Visuals). Each card goes into the shorter column.
	void BeginCardGrid(const char *pId, int Columns = 2);
	void EndCardGrid();
	// Card header: title, gray description, optional bind plate and toggle. Returns true when the body
	// should be drawn (always, the body is dimmed when the toggle is off).
	bool BeginCard(const char *pId, const char *pTitle, const char *pDescription, int *pEnabled = nullptr);
	void EndCard();
	// search: the card with this id gets a pulsing frame for a moment
	void HighlightCard(const char *pId);
	// y of the highlighted card inside the scrolled content if it was drawn since the last call, else -1
	float HighlightedCardY();

	// panel background with border and rounding
	void PanelRect(ImDrawList *pDrawList, ImVec2 Min, ImVec2 Max, float Rounding, const ImVec4 &Fill);
} // namespace XcUi

#endif

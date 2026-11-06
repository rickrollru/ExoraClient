#ifndef GAME_CLIENT_COMPONENTS_XCLIENT_UI_WIDGETS_H
#define GAME_CLIENT_COMPONENTS_XCLIENT_UI_WIDGETS_H

#include <algorithm>
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
		ImFont *m_pClock = nullptr;
		ImFont *m_pDisplay = nullptr;
	};
	void SetFonts(const SFonts &Fonts);
	const SFonts &Fonts();

	// Smoothly moves a stored value towards Target. Returns the current value.
	float Animate(ImGuiID Id, float Target, float Speed = 14.0f);
	void ResetAnimations();
	// call when a page or section opens: its cards appear one after another
	void PageEntered();

	// --- icons
	// Icons are placed by their visible shape, not by the text line: the drawing is centered exactly on Center and
	// snapped to whole pixels. The nearest size the atlas has baked is used, so icons stay sharp; pFont only says
	// which atlas (menu, HUD or editor) to take it from. Works for any short text too.
	void DrawIcon(ImDrawList *pDraw, ImFont *pFont, float Size, ImVec2 Center, ImU32 Color, const char *pIcon);
	// the height of the middle of the capitals above the line top: an icon next to text is centered on it
	float TextMiddle(ImFont *pFont, float Size);
	// an icon in a slot of SlotWidth left at x, next to text whose line starts at TextY
	void DrawIconBesideText(ImDrawList *pDraw, ImFont *pFont, float Size, float x, float SlotWidth, float TextY, ImU32 Color, const char *pIcon);

	// --- dragging
	// The place 0..1 on a track under the mouse while it is dragged. With Shift held it moves 10 times slower from
	// where it was, for exact values; after Shift the drag stays relative until the mouse is let go, so nothing
	// jumps. Current is the value now (the start for Shift). Call it every frame while the item is held, and in the
	// frame it is let go.
	float DragFraction(ImGuiID Id, float Current, float TrackLeft, float TrackWidth);
	// true while the drag of Id goes slow with Shift
	bool DragIsFine(ImGuiID Id);
	// true while any drag goes slow with Shift (the menu then slows the mouse itself down)
	bool AnyDragIsFine();
	// the menu slows the cursor down during a Shift drag, then the value follows the cursor 1:1
	void SetCursorSlowsDown(bool SlowsDown);

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
	bool InputText(const char *pLabel, char *pBuffer, int BufferSize, const char *pHint = nullptr, float Width = 0.0f, int Flags = 0); // Width 0: all the space left
	// effect color: accent / own / rainbow / skin modes, swatches and a hue bar for the own color (packed HSL)
	bool EffectColorPicker(const char *pId, int *pMode, unsigned *pColor);
	// three chips: show an effect for me, my dummy, other players
	bool TargetChips(const char *pId, int *pSelf, int *pDummy, int *pOthers);
	// chips that are each on or off, as bits of one value (chip i = bit i)
	bool FlagChips(const char *pId, int *pMask, const char *const *ppLabels, int Num);
	// one choice of many as rounded chips that wrap to new lines; pMarked puts a dot on chosen chips
	bool ChoiceChips(const char *pId, int *pCurrent, const char *const *ppItems, int NumItems, const bool *pMarked = nullptr);
	// a labeled row: name on the left, the next widget on the right
	void RowLabel(const char *pLabel, float ControlWidth);

	// --- layout
	// capsule with icon tabs, the highlight slides between them
	bool TabCapsule(const char *pId, int *pCurrent, const char *const *ppIcons, const char *const *ppLabels, int NumTabs);
	// vertical list of sections
	bool SideNav(const char *pId, int *pCurrent, const char *const *ppIcons, const char *const *ppLabels, int NumItems, float Width);

	// Two column masonry grid of cards. Each card goes into the shorter column.
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

	// lays out tiles in rows; call Next() before each tile
	class CTileGrid
	{
	public:
		CTileGrid(float AvailWidth, float MinWidth, float Gap) :
			m_Gap(Gap)
		{
			m_Columns = std::max(1, (int)((AvailWidth + Gap) / (MinWidth + Gap)));
			m_Width = (AvailWidth - Gap * (m_Columns - 1)) / m_Columns;
		}
		float Width() const { return m_Width; }
		void Next()
		{
			if(m_Index % m_Columns != 0)
				ImGui::SameLine(0.0f, m_Gap);
			else if(m_Index > 0)
				ImGui::Dummy(ImVec2(0.0f, m_Gap * 0.5f));
			m_Index++;
		}

	private:
		float m_Gap;
		int m_Columns;
		float m_Width;
		int m_Index = 0;
	};
	// the Exora logo: a white line with a hook at its end crossed by a line in the accent color, on a dark tile
	void DrawLogo(ImDrawList *pDraw, ImVec2 Center, float Size, const ImVec4 &Accent, float Alpha = 1.0f, bool Tile = true);
} // namespace XcUi

#endif

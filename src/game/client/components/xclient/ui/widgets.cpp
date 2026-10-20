#include "widgets.h"

#include "feedback.h"
#include "theme.h"

#include <engine/external/imgui/imgui_internal.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <unordered_map>
#include <vector>

namespace XcUi
{
	static SFonts s_Fonts;
	static std::unordered_map<ImGuiID, float> s_Animations;

	void SetFonts(const SFonts &Fonts) { s_Fonts = Fonts; }
	const SFonts &Fonts() { return s_Fonts; }

	float Animate(ImGuiID Id, float Target, float Speed)
	{
		auto It = s_Animations.find(Id);
		if(It == s_Animations.end() || AnimationSpeed() <= 0.0f)
		{
			s_Animations[Id] = Target;
			return Target;
		}
		const float Dt = ImGui::GetIO().DeltaTime;
		float &Value = It->second;
		Value += (Target - Value) * (1.0f - std::exp(-Speed * AnimationSpeed() * Dt));
		if(std::abs(Target - Value) < 0.0005f)
			Value = Target;
		return Value;
	}

	void ResetAnimations()
	{
		s_Animations.clear();
	}

	// ---------------------------------------------------------------------------------------------
	// card grid state

	struct SCardGrid
	{
		ImVec2 m_Start;
		float m_Width;
		int m_Columns;
		float m_aColumnY[4];
		// current card
		int m_Column;
		ImVec2 m_CardPos;
		float m_CardWidth;
		ImGuiID m_CardId;
		bool m_InCard;
		bool m_Dimmed;
		bool m_Highlighted;
		float m_Rise; // how far the current card is still below its place while it appears
		int m_NumCards;
		ImDrawListSplitter m_Splitter;
	};
	static std::vector<SCardGrid> s_vGrids;

	// cards appear one after another when a page opens
	static double s_PageEnterTime = -10.0;
	static constexpr double CARD_APPEAR_TIME = 0.32;
	static constexpr double CARD_APPEAR_STAGGER = 0.045;

	void PageEntered() { s_PageEnterTime = ImGui::GetTime(); }

	static std::string s_HighlightCard;
	static double s_HighlightStart = -10.0;
	static float s_HighlightY = -1.0f;
	static constexpr double HIGHLIGHT_TIME = 2.2;

	void HighlightCard(const char *pId)
	{
		s_HighlightCard = pId;
		s_HighlightStart = ImGui::GetTime();
		s_HighlightY = -1.0f;
	}

	float HighlightedCardY()
	{
		const float y = s_HighlightY;
		s_HighlightY = -1.0f;
		return y;
	}

	static float Gap() { return S(12.0f); }
	static float CardPadding() { return S(16.0f); }

	float AvailWidth()
	{
		if(!s_vGrids.empty() && s_vGrids.back().m_InCard)
		{
			const SCardGrid &Grid = s_vGrids.back();
			return Grid.m_CardPos.x + Grid.m_CardWidth - CardPadding() - ImGui::GetCursorScreenPos().x;
		}
		return ImGui::GetContentRegionAvail().x;
	}

	static ImFont *FontOr(ImFont *pFont)
	{
		return pFont ? pFont : ImGui::GetFont();
	}

	// ---------------------------------------------------------------------------------------------
	// text

	static void TextWith(ImFont *pFont, const ImVec4 &Color, const char *pText, float WrapWidth = 0.0f)
	{
		ImGui::PushFont(FontOr(pFont));
		ImGui::PushStyleColor(ImGuiCol_Text, Color);
		if(WrapWidth > 0.0f)
		{
			ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + WrapWidth);
			ImGui::TextUnformatted(pText);
			ImGui::PopTextWrapPos();
		}
		else
		{
			ImGui::TextUnformatted(pText);
		}
		ImGui::PopStyleColor();
		ImGui::PopFont();
	}

	void Header(const char *pText) { TextWith(s_Fonts.m_pHeader, Theme().m_Text, pText); }
	void Title(const char *pText) { TextWith(s_Fonts.m_pTitle, Theme().m_Text, pText); }
	void Description(const char *pText, float WrapWidth) { TextWith(s_Fonts.m_pSmall, Theme().m_TextMuted, pText, WrapWidth > 0.0f ? WrapWidth : AvailWidth()); }
	void ParamLabel(const char *pText) { TextWith(s_Fonts.m_pLabel, Theme().m_TextSecondary, pText); }

	void PanelRect(ImDrawList *pDrawList, ImVec2 Min, ImVec2 Max, float Rounding, const ImVec4 &Fill)
	{
		pDrawList->AddRectFilled(Min, Max, Col(Fill), Rounding);
		pDrawList->AddRect(Min, Max, Col(Theme().m_Border), Rounding, 0, 1.0f);
	}

	// ---------------------------------------------------------------------------------------------
	// controls

	bool Toggle(const char *pId, bool *pValue)
	{
		ImGuiWindow *pWindow = ImGui::GetCurrentWindow();
		if(pWindow->SkipItems)
			return false;
		const ImGuiID Id = pWindow->GetID(pId);
		const ImVec2 Size(S(38.0f), S(22.0f));
		const ImVec2 Pos = ImGui::GetCursorScreenPos();
		const ImRect Rect(Pos, ImVec2(Pos.x + Size.x, Pos.y + Size.y));
		ImGui::ItemSize(Rect);
		if(!ImGui::ItemAdd(Rect, Id))
			return false;

		bool Hovered, Held;
		const bool Pressed = ImGui::ButtonBehavior(Rect, Id, &Hovered, &Held);
		if(Pressed)
		{
			*pValue = !*pValue;
			PlayUiSound(*pValue ? SOUND_TOGGLE_ON : SOUND_TOGGLE_OFF);
			ImGui::MarkItemEdited(Id);
		}

		const STheme &t = Theme();
		const float On = Animate(Id, *pValue ? 1.0f : 0.0f, 16.0f);
		const float Hover = Animate(Id + 1, Hovered ? 1.0f : 0.0f, 18.0f);
		ImVec4 Track = Mix(t.m_Track, t.m_Accent, On);
		Track.w = std::min(Track.w + Hover * 0.06f, 1.0f);
		ImDrawList *pDraw = pWindow->DrawList;
		pDraw->AddRectFilled(Rect.Min, Rect.Max, Col(Track), Size.y * 0.5f);

		const float Radius = Size.y * 0.5f - S(3.0f);
		// the knob stretches a little while pressed
		const float Stretch = Animate(Id + 2, Held ? S(4.0f) : 0.0f, 20.0f);
		const float Left = Rect.Min.x + S(3.0f) + Radius;
		const float Right = Rect.Max.x - S(3.0f) - Radius;
		const float x = Left + (Right - Left) * On;
		const float y = Rect.Min.y + Size.y * 0.5f;
		const ImVec4 Knob = Mix(t.m_Dark ? t.m_TextSecondary : ImVec4(1, 1, 1, 1), t.m_OnAccent, On);
		pDraw->AddRectFilled(ImVec2(x - Radius - Stretch * (1.0f - On), y - Radius), ImVec2(x + Radius + Stretch * On, y + Radius), Col(Knob), Radius);
		return Pressed;
	}

	bool ToggleInt(const char *pId, int *pValue)
	{
		bool Value = *pValue != 0;
		if(!Toggle(pId, &Value))
			return false;
		*pValue = Value ? 1 : 0;
		return true;
	}

	static bool SliderBehavior(const char *pLabel, float *pFraction, const char *pValueText, ImGuiID *pId)
	{
		ImGuiWindow *pWindow = ImGui::GetCurrentWindow();
		if(pWindow->SkipItems)
			return false;
		const ImGuiID Id = pWindow->GetID(pLabel);
		*pId = Id;
		const STheme &t = Theme();
		ImFont *pLabelFont = FontOr(s_Fonts.m_pLabel);
		const float Width = AvailWidth();
		const float TextHeight = pLabelFont->FontSize;
		const float TrackHeight = S(4.0f);
		const float GrabRadius = S(7.0f);
		const ImVec2 Pos = ImGui::GetCursorScreenPos();
		const float TrackY = Pos.y + TextHeight + S(10.0f) + GrabRadius;
		const ImRect Rect(Pos, ImVec2(Pos.x + Width, TrackY + GrabRadius));
		ImGui::ItemSize(Rect);
		if(!ImGui::ItemAdd(Rect, Id))
			return false;

		bool Hovered, Held;
		ImGui::ButtonBehavior(Rect, Id, &Hovered, &Held, ImGuiButtonFlags_PressedOnClick);
		bool Changed = false;
		const float TrackLeft = Rect.Min.x + GrabRadius;
		const float TrackRight = Rect.Max.x - GrabRadius;
		if(Held)
		{
			const float NewFraction = std::clamp((ImGui::GetIO().MousePos.x - TrackLeft) / std::max(TrackRight - TrackLeft, 1.0f), 0.0f, 1.0f);
			if(NewFraction != *pFraction)
			{
				*pFraction = NewFraction;
				Changed = true;
			}
		}

		ImDrawList *pDraw = pWindow->DrawList;
		pDraw->AddText(pLabelFont, pLabelFont->FontSize, Pos, Col(t.m_TextSecondary), pLabel);
		const ImVec2 ValueSize = pLabelFont->CalcTextSizeA(pLabelFont->FontSize, FLT_MAX, 0.0f, pValueText);
		pDraw->AddText(pLabelFont, pLabelFont->FontSize, ImVec2(Rect.Max.x - ValueSize.x, Pos.y), Col(t.m_Text), pValueText);

		const float Shown = Animate(Id, *pFraction, Held ? 40.0f : 16.0f);
		const float Focus = Animate(Id + 1, (Hovered || Held) ? 1.0f : 0.0f, 18.0f);
		pDraw->AddRectFilled(ImVec2(TrackLeft, TrackY - TrackHeight * 0.5f), ImVec2(TrackRight, TrackY + TrackHeight * 0.5f), Col(t.m_Track), TrackHeight);
		const float FillX = TrackLeft + (TrackRight - TrackLeft) * Shown;
		pDraw->AddRectFilled(ImVec2(TrackLeft, TrackY - TrackHeight * 0.5f), ImVec2(FillX, TrackY + TrackHeight * 0.5f), Col(t.m_Accent), TrackHeight);
		// soft halo around the grab while hovered
		pDraw->AddCircleFilled(ImVec2(FillX, TrackY), GrabRadius * (1.0f + 0.7f * Focus), Col(t.m_Accent, 0.18f * Focus), 24);
		pDraw->AddCircleFilled(ImVec2(FillX, TrackY), GrabRadius, Col(t.m_Accent), 24);
		pDraw->AddCircleFilled(ImVec2(FillX, TrackY), GrabRadius * 0.42f, Col(t.m_OnAccent), 16);
		if(Changed)
			ImGui::MarkItemEdited(Id);
		return Changed;
	}

	bool SliderInt(const char *pLabel, int *pValue, int Min, int Max, const char *pFormat)
	{
		char aValue[64];
		std::snprintf(aValue, sizeof(aValue), pFormat, *pValue);
		float Fraction = Max > Min ? (float)(*pValue - Min) / (Max - Min) : 0.0f;
		ImGuiID Id;
		if(!SliderBehavior(pLabel, &Fraction, aValue, &Id))
			return false;
		const int NewValue = Min + (int)std::lround(Fraction * (Max - Min));
		if(NewValue == *pValue)
			return false;
		*pValue = NewValue;
		return true;
	}

	bool SliderFloat(const char *pLabel, float *pValue, float Min, float Max, const char *pFormat)
	{
		char aValue[64];
		std::snprintf(aValue, sizeof(aValue), pFormat, *pValue);
		float Fraction = Max > Min ? (*pValue - Min) / (Max - Min) : 0.0f;
		ImGuiID Id;
		if(!SliderBehavior(pLabel, &Fraction, aValue, &Id))
			return false;
		*pValue = Min + Fraction * (Max - Min);
		return true;
	}

	bool Segmented(const char *pId, int *pCurrent, const char *const *ppItems, int NumItems)
	{
		ImGuiWindow *pWindow = ImGui::GetCurrentWindow();
		if(pWindow->SkipItems || NumItems <= 0)
			return false;
		const ImGuiID Id = pWindow->GetID(pId);
		const STheme &t = Theme();
		ImFont *pFont = FontOr(s_Fonts.m_pLabel);
		const float Width = AvailWidth();
		const float Height = S(32.0f);
		const ImVec2 Pos = ImGui::GetCursorScreenPos();
		const ImRect Rect(Pos, ImVec2(Pos.x + Width, Pos.y + Height));
		ImGui::ItemSize(Rect);
		if(!ImGui::ItemAdd(Rect, Id))
			return false;

		ImDrawList *pDraw = pWindow->DrawList;
		pDraw->AddRectFilled(Rect.Min, Rect.Max, Col(t.m_Track), S(9.0f));
		const float Inset = S(3.0f);
		const float SegmentWidth = (Width - Inset * 2.0f) / NumItems;
		const float Shown = Animate(Id, (float)*pCurrent, 16.0f);
		const ImVec2 HighlightMin(Rect.Min.x + Inset + SegmentWidth * Shown, Rect.Min.y + Inset);
		pDraw->AddRectFilled(HighlightMin, ImVec2(HighlightMin.x + SegmentWidth, Rect.Max.y - Inset), Col(t.m_Accent), S(7.0f));

		bool Changed = false;
		for(int i = 0; i < NumItems; i++)
		{
			const ImRect Segment(ImVec2(Rect.Min.x + Inset + SegmentWidth * i, Rect.Min.y), ImVec2(Rect.Min.x + Inset + SegmentWidth * (i + 1), Rect.Max.y));
			const ImGuiID SegmentId = Id + 10 + i;
			bool Hovered, Held;
			// every part is its own item: a button that is not added never gets the click
			ImGui::ItemAdd(Segment, SegmentId);
			if(ImGui::ButtonBehavior(Segment, SegmentId, &Hovered, &Held) && *pCurrent != i)
			{
				*pCurrent = i;
				PlayUiSound(SOUND_TAB);
				Changed = true;
			}
			// text color fades with the highlight passing under it
			const float Under = std::clamp(1.0f - std::abs(Shown - i), 0.0f, 1.0f);
			const float Hover = Animate(SegmentId, Hovered ? 1.0f : 0.0f, 18.0f);
			const ImVec4 TextColor = Mix(Mix(t.m_TextSecondary, t.m_Text, Hover), t.m_OnAccent, Under);
			const ImVec2 TextSize = pFont->CalcTextSizeA(pFont->FontSize, FLT_MAX, 0.0f, ppItems[i]);
			pDraw->AddText(pFont, pFont->FontSize, ImVec2(Segment.GetCenter().x - TextSize.x * 0.5f, Segment.GetCenter().y - TextSize.y * 0.5f), Col(TextColor), ppItems[i]);
		}
		if(Changed)
			ImGui::MarkItemEdited(Id);
		return Changed;
	}

	bool BindChip(const char *pId, const char *pKeyName, bool Waiting)
	{
		ImGuiWindow *pWindow = ImGui::GetCurrentWindow();
		if(pWindow->SkipItems)
			return false;
		const ImGuiID Id = pWindow->GetID(pId);
		const STheme &t = Theme();
		ImFont *pFont = FontOr(s_Fonts.m_pSmall);
		const char *pText = Waiting ? "..." : (pKeyName && pKeyName[0] ? pKeyName : "-");
		const ImVec2 TextSize = pFont->CalcTextSizeA(pFont->FontSize, FLT_MAX, 0.0f, pText);
		const ImVec2 Size(std::max(TextSize.x + S(16.0f), S(34.0f)), TextSize.y + S(8.0f));
		const ImVec2 Pos = ImGui::GetCursorScreenPos();
		const ImRect Rect(Pos, ImVec2(Pos.x + Size.x, Pos.y + Size.y));
		ImGui::ItemSize(Rect);
		if(!ImGui::ItemAdd(Rect, Id))
			return false;
		bool Hovered, Held;
		const bool Pressed = ImGui::ButtonBehavior(Rect, Id, &Hovered, &Held);
		if(Pressed)
			PlayUiSound(SOUND_CLICK);
		const float Hover = Animate(Id, Hovered ? 1.0f : 0.0f, 18.0f);
		ImDrawList *pDraw = pWindow->DrawList;
		pDraw->AddRectFilled(Rect.Min, Rect.Max, Col(t.m_Track, 1.0f + Hover * 0.6f), S(6.0f));
		if(Waiting)
		{
			// pulsing accent outline while waiting for a key
			const float Pulse = 0.55f + 0.45f * std::sin((float)ImGui::GetTime() * 6.0f);
			pDraw->AddRect(Rect.Min, Rect.Max, Col(t.m_Accent, Pulse), S(6.0f), 0, S(1.5f));
		}
		else
		{
			pDraw->AddRect(Rect.Min, Rect.Max, Col(t.m_Border), S(6.0f));
		}
		pDraw->AddText(pFont, pFont->FontSize, ImVec2(Rect.GetCenter().x - TextSize.x * 0.5f, Rect.GetCenter().y - TextSize.y * 0.5f), Col(Waiting ? t.m_Accent : t.m_TextSecondary), pText);
		return Pressed;
	}

	bool Button(const char *pLabel, bool Primary, float Width)
	{
		ImGuiWindow *pWindow = ImGui::GetCurrentWindow();
		if(pWindow->SkipItems)
			return false;
		const ImGuiID Id = pWindow->GetID(pLabel);
		const STheme &t = Theme();
		ImFont *pFont = FontOr(s_Fonts.m_pLabel);
		const char *pEnd = ImGui::FindRenderedTextEnd(pLabel);
		const ImVec2 TextSize = pFont->CalcTextSizeA(pFont->FontSize, FLT_MAX, 0.0f, pLabel, pEnd);
		const float ButtonWidth = Width > 0.0f ? Width : (Width < 0.0f ? AvailWidth() : TextSize.x + S(28.0f));
		const ImVec2 Pos = ImGui::GetCursorScreenPos();
		const ImRect Rect(Pos, ImVec2(Pos.x + ButtonWidth, Pos.y + S(36.0f)));
		ImGui::ItemSize(Rect);
		if(!ImGui::ItemAdd(Rect, Id))
			return false;
		bool Hovered, Held;
		const bool Pressed = ImGui::ButtonBehavior(Rect, Id, &Hovered, &Held);
		if(Pressed)
			PlayUiSound(SOUND_CLICK);
		const float Hover = Animate(Id, Hovered ? 1.0f : 0.0f, 18.0f);
		const float Press = Animate(Id + 1, Held ? 1.0f : 0.0f, 25.0f);
		// pressing shrinks the button slightly
		const float Shrink = S(1.5f) * Press;
		const ImVec2 Min(Rect.Min.x + Shrink, Rect.Min.y + Shrink);
		const ImVec2 Max(Rect.Max.x - Shrink, Rect.Max.y - Shrink);
		ImDrawList *pDraw = pWindow->DrawList;
		if(Primary)
		{
			ImVec4 Fill = Mix(t.m_Accent, ImVec4(1, 1, 1, 1), 0.12f * Hover);
			pDraw->AddRectFilled(Min, Max, Col(Fill), S(10.0f));
		}
		else
		{
			pDraw->AddRectFilled(Min, Max, Col(t.m_Track, 1.0f + Hover * 0.8f), S(10.0f));
			pDraw->AddRect(Min, Max, Col(t.m_Border), S(10.0f));
		}
		pDraw->AddText(pFont, pFont->FontSize, ImVec2(Rect.GetCenter().x - TextSize.x * 0.5f, Rect.GetCenter().y - TextSize.y * 0.5f), Col(Primary ? t.m_OnAccent : t.m_Text), pLabel, pEnd);
		return Pressed;
	}

	bool IconButton(const char *pIcon, const char *pTooltip, bool Selected)
	{
		ImGuiWindow *pWindow = ImGui::GetCurrentWindow();
		if(pWindow->SkipItems)
			return false;
		const ImGuiID Id = pWindow->GetID(pTooltip ? pTooltip : pIcon);
		const STheme &t = Theme();
		ImFont *pFont = FontOr(s_Fonts.m_pLabel);
		const float Size = S(34.0f);
		const ImVec2 Pos = ImGui::GetCursorScreenPos();
		const ImRect Rect(Pos, ImVec2(Pos.x + Size, Pos.y + Size));
		ImGui::ItemSize(Rect);
		if(!ImGui::ItemAdd(Rect, Id))
			return false;
		bool Hovered, Held;
		const bool Pressed = ImGui::ButtonBehavior(Rect, Id, &Hovered, &Held);
		if(Pressed)
			PlayUiSound(SOUND_CLICK);
		const float Hover = Animate(Id, (Hovered || Selected) ? 1.0f : 0.0f, 18.0f);
		ImDrawList *pDraw = pWindow->DrawList;
		pDraw->AddRectFilled(Rect.Min, Rect.Max, Col(Selected ? ImVec4(t.m_Accent.x, t.m_Accent.y, t.m_Accent.z, 0.18f) : t.m_Track, Selected ? 1.0f : Hover * 1.4f), S(10.0f));
		const ImVec2 IconSize = pFont->CalcTextSizeA(pFont->FontSize, FLT_MAX, 0.0f, pIcon);
		pDraw->AddText(pFont, pFont->FontSize, ImVec2(Rect.GetCenter().x - IconSize.x * 0.5f, Rect.GetCenter().y - IconSize.y * 0.5f), Col(Selected ? t.m_Accent : Mix(t.m_TextSecondary, t.m_Text, Hover)), pIcon);
		if(pTooltip && Hovered)
			ImGui::SetTooltip("%s", pTooltip);
		return Pressed;
	}

	bool ColorSwatches(const char *pId, int *pCurrent, const ImVec4 *pColors, int NumColors)
	{
		ImGuiWindow *pWindow = ImGui::GetCurrentWindow();
		if(pWindow->SkipItems)
			return false;
		const ImGuiID Id = pWindow->GetID(pId);
		const STheme &t = Theme();
		const float Radius = S(11.0f);
		const float Spacing = S(10.0f);
		const ImVec2 Pos = ImGui::GetCursorScreenPos();
		const ImRect Rect(Pos, ImVec2(Pos.x + NumColors * (Radius * 2.0f + Spacing) - Spacing + S(4.0f), Pos.y + Radius * 2.0f + S(4.0f)));
		ImGui::ItemSize(Rect);
		if(!ImGui::ItemAdd(Rect, Id))
			return false;
		bool Changed = false;
		ImDrawList *pDraw = pWindow->DrawList;
		for(int i = 0; i < NumColors; i++)
		{
			const ImVec2 Center(Pos.x + S(2.0f) + Radius + i * (Radius * 2.0f + Spacing), Pos.y + S(2.0f) + Radius);
			const ImRect Hit(ImVec2(Center.x - Radius, Center.y - Radius), ImVec2(Center.x + Radius, Center.y + Radius));
			bool Hovered, Held;
			ImGui::ItemAdd(Hit, Id + 10 + i);
			if(ImGui::ButtonBehavior(Hit, Id + 10 + i, &Hovered, &Held) && *pCurrent != i)
			{
				*pCurrent = i;
				PlayUiSound(SOUND_CLICK);
				Changed = true;
			}
			const float Selected = Animate(Id + 10 + i, *pCurrent == i ? 1.0f : 0.0f, 16.0f);
			const float Hover = Animate(Id + 100 + i, Hovered ? 1.0f : 0.0f, 18.0f);
			const float r = Radius * (0.78f + 0.12f * Hover - 0.18f * Selected);
			pDraw->AddCircleFilled(Center, r, Col(pColors[i]), 28);
			if(Selected > 0.01f)
				pDraw->AddCircle(Center, Radius, Col(t.m_Text, Selected), 28, S(1.6f));
		}
		if(Changed)
			ImGui::MarkItemEdited(Id);
		return Changed;
	}

	bool InputText(const char *pLabel, char *pBuffer, int BufferSize, const char *pHint, float Width)
	{
		const STheme &t = Theme();
		ImGui::PushFont(FontOr(s_Fonts.m_pBody));
		ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, S(8.0f));
		ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(S(10.0f), S(8.0f)));
		ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 1.0f);
		ImGui::PushStyleColor(ImGuiCol_FrameBg, t.m_Track);
		ImGui::PushStyleColor(ImGuiCol_Border, t.m_Border);
		ImGui::SetNextItemWidth(Width > 0.0f ? Width : AvailWidth());
		const bool Changed = ImGui::InputTextWithHint(pLabel, pHint ? pHint : "", pBuffer, BufferSize);
		ImGui::PopStyleColor(2);
		ImGui::PopStyleVar(3);
		ImGui::PopFont();
		return Changed;
	}

	void RowLabel(const char *pLabel, float ControlWidth)
	{
		const float Width = AvailWidth();
		const float StartX = ImGui::GetCursorPosX();
		ImFont *pFont = FontOr(s_Fonts.m_pLabel);
		// center the label on the control line
		const float Offset = std::max((S(22.0f) - pFont->FontSize) * 0.5f, 0.0f);
		ImGui::SetCursorPosY(ImGui::GetCursorPosY() + Offset);
		ParamLabel(pLabel);
		ImGui::SameLine();
		ImGui::SetCursorPosY(ImGui::GetCursorPosY() - Offset);
		ImGui::SetCursorPosX(StartX + std::max(Width - ControlWidth, 0.0f));
	}

	// ---------------------------------------------------------------------------------------------
	// navigation

	bool TabCapsule(const char *pId, int *pCurrent, const char *const *ppIcons, const char *const *ppLabels, int NumTabs)
	{
		ImGuiWindow *pWindow = ImGui::GetCurrentWindow();
		if(pWindow->SkipItems)
			return false;
		const ImGuiID Id = pWindow->GetID(pId);
		const STheme &t = Theme();
		ImFont *pFont = FontOr(s_Fonts.m_pLabel);
		const float Height = S(40.0f);
		const float Inset = S(4.0f);
		const float IconGap = S(8.0f);
		const float TabPadding = S(16.0f);

		float aWidths[16];
		float Total = Inset * 2.0f;
		for(int i = 0; i < NumTabs && i < 16; i++)
		{
			const float IconWidth = pFont->CalcTextSizeA(pFont->FontSize, FLT_MAX, 0.0f, ppIcons[i]).x;
			const float LabelWidth = pFont->CalcTextSizeA(pFont->FontSize, FLT_MAX, 0.0f, ppLabels[i]).x;
			aWidths[i] = TabPadding * 2.0f + IconWidth + IconGap + LabelWidth;
			Total += aWidths[i];
		}
		const ImVec2 Pos = ImGui::GetCursorScreenPos();
		const ImRect Rect(Pos, ImVec2(Pos.x + Total, Pos.y + Height));
		ImGui::ItemSize(Rect);
		if(!ImGui::ItemAdd(Rect, Id))
			return false;

		ImDrawList *pDraw = pWindow->DrawList;
		PanelRect(pDraw, Rect.Min, Rect.Max, Height * 0.5f, t.m_Panel);

		// highlight slides and resizes between tabs
		// *pCurrent -1: the open page is not a tab here, nothing is highlighted
		const bool HasCurrent = *pCurrent >= 0 && *pCurrent < NumTabs;
		float SelectedX = Rect.Min.x + Inset;
		for(int i = 0; HasCurrent && i < *pCurrent; i++)
			SelectedX += aWidths[i];
		const float HighlightX = Animate(Id, SelectedX, 16.0f);
		const float HighlightW = Animate(Id + 1, HasCurrent ? aWidths[*pCurrent] : 0.0f, 16.0f);
		if(HighlightW > S(2.0f))
			pDraw->AddRectFilled(ImVec2(HighlightX, Rect.Min.y + Inset), ImVec2(HighlightX + HighlightW, Rect.Max.y - Inset), Col(t.m_Accent), (Height - Inset * 2.0f) * 0.5f);

		bool Changed = false;
		float x = Rect.Min.x + Inset;
		for(int i = 0; i < NumTabs; i++)
		{
			const ImRect Tab(ImVec2(x, Rect.Min.y), ImVec2(x + aWidths[i], Rect.Max.y));
			bool Hovered, Held;
			ImGui::ItemAdd(Tab, Id + 10 + i);
			if(ImGui::ButtonBehavior(Tab, Id + 10 + i, &Hovered, &Held) && *pCurrent != i)
			{
				*pCurrent = i;
				PlayUiSound(SOUND_TAB);
				Changed = true;
			}
			const float Under = HighlightW > S(2.0f) ? std::clamp(1.0f - std::abs(HighlightX - Tab.Min.x) / std::max(aWidths[i], 1.0f), 0.0f, 1.0f) : 0.0f;
			const float Hover = Animate(Id + 10 + i, Hovered ? 1.0f : 0.0f, 18.0f);
			const ImVec4 TextColor = Mix(Mix(t.m_TextMuted, t.m_Text, Hover), t.m_OnAccent, Under);
			const float TextY = Rect.GetCenter().y - pFont->FontSize * 0.5f;
			pDraw->AddText(pFont, pFont->FontSize, ImVec2(x + TabPadding, TextY), Col(TextColor), ppIcons[i]);
			const float IconWidth = pFont->CalcTextSizeA(pFont->FontSize, FLT_MAX, 0.0f, ppIcons[i]).x;
			pDraw->AddText(pFont, pFont->FontSize, ImVec2(x + TabPadding + IconWidth + IconGap, TextY), Col(TextColor), ppLabels[i]);
			x += aWidths[i];
		}
		return Changed;
	}

	bool SideNav(const char *pId, int *pCurrent, const char *const *ppIcons, const char *const *ppLabels, int NumItems, float Width)
	{
		ImGuiWindow *pWindow = ImGui::GetCurrentWindow();
		if(pWindow->SkipItems)
			return false;
		const ImGuiID Id = pWindow->GetID(pId);
		const STheme &t = Theme();
		ImFont *pFont = FontOr(s_Fonts.m_pLabel);
		const float ItemHeight = S(40.0f);
		const ImVec2 Pos = ImGui::GetCursorScreenPos();
		const ImRect Rect(Pos, ImVec2(Pos.x + Width, Pos.y + ItemHeight * NumItems));
		ImGui::ItemSize(Rect);
		if(!ImGui::ItemAdd(Rect, Id))
			return false;

		ImDrawList *pDraw = pWindow->DrawList;
		const float HighlightY = Animate(Id, Pos.y + ItemHeight * *pCurrent, 16.0f);
		pDraw->AddRectFilled(ImVec2(Pos.x, HighlightY), ImVec2(Pos.x + Width, HighlightY + ItemHeight), Col(t.m_Card), S(10.0f));
		pDraw->AddRect(ImVec2(Pos.x, HighlightY), ImVec2(Pos.x + Width, HighlightY + ItemHeight), Col(t.m_Border), S(10.0f));
		// small accent bar on the left of the selected item
		pDraw->AddRectFilled(ImVec2(Pos.x + S(6.0f), HighlightY + S(12.0f)), ImVec2(Pos.x + S(9.0f), HighlightY + ItemHeight - S(12.0f)), Col(t.m_Accent), S(2.0f));

		bool Changed = false;
		for(int i = 0; i < NumItems; i++)
		{
			const ImRect Item(ImVec2(Pos.x, Pos.y + ItemHeight * i), ImVec2(Pos.x + Width, Pos.y + ItemHeight * (i + 1)));
			bool Hovered, Held;
			ImGui::ItemAdd(Item, Id + 10 + i);
			if(ImGui::ButtonBehavior(Item, Id + 10 + i, &Hovered, &Held) && *pCurrent != i)
			{
				*pCurrent = i;
				PlayUiSound(SOUND_TAB);
				Changed = true;
			}
			const float Hover = Animate(Id + 10 + i, (Hovered || *pCurrent == i) ? 1.0f : 0.0f, 18.0f);
			const ImVec4 TextColor = Mix(t.m_TextMuted, t.m_Text, Hover);
			const float TextY = Item.GetCenter().y - pFont->FontSize * 0.5f;
			pDraw->AddText(pFont, pFont->FontSize, ImVec2(Item.Min.x + S(18.0f), TextY), Col(*pCurrent == i ? t.m_Accent : TextColor), ppIcons[i]);
			pDraw->AddText(pFont, pFont->FontSize, ImVec2(Item.Min.x + S(44.0f), TextY), Col(TextColor), ppLabels[i]);
		}
		return Changed;
	}

	// ---------------------------------------------------------------------------------------------
	// cards

	void BeginCardGrid(const char *pId, int Columns)
	{
		SCardGrid &Grid = s_vGrids.emplace_back();
		Grid.m_Start = ImGui::GetCursorScreenPos();
		Grid.m_Width = ImGui::GetContentRegionAvail().x;
		Grid.m_Columns = std::clamp(Columns, 1, 4);
		for(float &y : Grid.m_aColumnY)
			y = Grid.m_Start.y;
		Grid.m_InCard = false;
		Grid.m_NumCards = 0;
		Grid.m_Rise = 0.0f;
		ImGui::PushID(pId);
	}

	void EndCardGrid()
	{
		SCardGrid &Grid = s_vGrids.back();
		float Bottom = Grid.m_Start.y;
		for(int i = 0; i < Grid.m_Columns; i++)
			Bottom = std::max(Bottom, Grid.m_aColumnY[i]);
		ImGui::PopID();
		ImGui::SetCursorScreenPos(Grid.m_Start);
		ImGui::Dummy(ImVec2(Grid.m_Width, std::max(Bottom - Grid.m_Start.y - Gap(), 0.0f)));
		s_vGrids.pop_back();
	}

	bool BeginCard(const char *pId, const char *pTitle, const char *pDescription, int *pEnabled)
	{
		SCardGrid &Grid = s_vGrids.back();
		const STheme &t = Theme();
		// the shorter column gets the next card
		int Column = 0;
		for(int i = 1; i < Grid.m_Columns; i++)
			if(Grid.m_aColumnY[i] < Grid.m_aColumnY[Column] - 1.0f)
				Column = i;
		Grid.m_Column = Column;
		Grid.m_CardWidth = (Grid.m_Width - Gap() * (Grid.m_Columns - 1)) / Grid.m_Columns;
		Grid.m_CardPos = ImVec2(Grid.m_Start.x + Column * (Grid.m_CardWidth + Gap()), Grid.m_aColumnY[Column]);
		// fades in and rises into its place, a little later than the card before it
		float Appear = 1.0f;
		if(AnimationSpeed() > 0.0f)
		{
			const double Age = (ImGui::GetTime() - s_PageEnterTime) * AnimationSpeed() - std::min(Grid.m_NumCards, 12) * CARD_APPEAR_STAGGER;
			const float x = std::clamp((float)(Age / CARD_APPEAR_TIME), 0.0f, 1.0f);
			Appear = 1.0f - (1.0f - x) * (1.0f - x) * (1.0f - x); // ease out
		}
		Grid.m_NumCards++;
		Grid.m_Rise = (1.0f - Appear) * S(16.0f);
		Grid.m_CardPos.y += Grid.m_Rise;
		ImGui::PushStyleVar(ImGuiStyleVar_Alpha, ImGui::GetStyle().Alpha * Appear);
		Grid.m_CardId = ImGui::GetID(pId);
		Grid.m_InCard = true;
		Grid.m_Highlighted = ImGui::GetTime() - s_HighlightStart < HIGHLIGHT_TIME && s_HighlightCard == pId;
		if(Grid.m_Highlighted)
		{
			// position inside the scrolled content, the page scrolls there
			const ImGuiWindow *pWindow = ImGui::GetCurrentWindow();
			s_HighlightY = Grid.m_CardPos.y - pWindow->Pos.y + pWindow->Scroll.y;
		}

		ImDrawList *pDraw = ImGui::GetWindowDrawList();
		Grid.m_Splitter.Split(pDraw, 2);
		Grid.m_Splitter.SetCurrentChannel(pDraw, 1);
		ImGui::PushID(pId);
		ImGui::SetCursorScreenPos(ImVec2(Grid.m_CardPos.x + CardPadding(), Grid.m_CardPos.y + CardPadding()));
		ImGui::BeginGroup();

		// header: title and description on the left, toggle on the right
		const float ToggleWidth = pEnabled ? S(38.0f) + S(12.0f) : 0.0f;
		const ImVec2 HeaderPos = ImGui::GetCursorScreenPos();
		Title(pTitle);
		if(pDescription && pDescription[0])
		{
			ImGui::SetCursorScreenPos(ImVec2(HeaderPos.x, ImGui::GetCursorScreenPos().y - S(6.0f)));
			Description(pDescription, Grid.m_CardWidth - CardPadding() * 2.0f - ToggleWidth);
		}
		const float HeaderBottom = ImGui::GetCursorScreenPos().y;
		if(pEnabled)
		{
			ImGui::SetCursorScreenPos(ImVec2(Grid.m_CardPos.x + Grid.m_CardWidth - CardPadding() - S(38.0f), HeaderPos.y));
			ToggleInt("##enabled", pEnabled);
		}
		ImGui::SetCursorScreenPos(ImVec2(HeaderPos.x, std::max(HeaderBottom, ImGui::GetCursorScreenPos().y)));

		Grid.m_Dimmed = pEnabled && !*pEnabled;
		const float Dim = Animate(Grid.m_CardId + 5, Grid.m_Dimmed ? 1.0f : 0.0f, 14.0f);
		// separator between header and parameters
		const float LineY = ImGui::GetCursorScreenPos().y + S(2.0f);
		pDraw->AddLine(ImVec2(Grid.m_CardPos.x + CardPadding(), LineY), ImVec2(Grid.m_CardPos.x + Grid.m_CardWidth - CardPadding(), LineY), Col(t.m_Border), 1.0f);
		ImGui::Dummy(ImVec2(0.0f, S(6.0f)));
		ImGui::PushStyleVar(ImGuiStyleVar_Alpha, ImGui::GetStyle().Alpha * (1.0f - 0.55f * Dim));
		ImGui::BeginDisabled(Grid.m_Dimmed);
		return true;
	}

	void EndCard()
	{
		SCardGrid &Grid = s_vGrids.back();
		ImGui::EndDisabled();
		ImGui::PopStyleVar();
		ImGui::EndGroup();
		const float Bottom = ImGui::GetItemRectMax().y + CardPadding();
		const ImVec2 Min = Grid.m_CardPos;
		const ImVec2 Max(Grid.m_CardPos.x + Grid.m_CardWidth, Bottom);

		ImDrawList *pDraw = ImGui::GetWindowDrawList();
		Grid.m_Splitter.SetCurrentChannel(pDraw, 0);
		const float Hover = Animate(Grid.m_CardId + 6, ImGui::IsMouseHoveringRect(Min, Max) && ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows) ? 1.0f : 0.0f, 12.0f);
		const STheme &t = Theme();
		PanelRect(pDraw, Min, Max, S(12.0f), Mix(t.m_Card, t.m_CardHover, Hover));
		Grid.m_Splitter.Merge(pDraw);
		if(Grid.m_Highlighted)
		{
			// found by the search: the accent frame pulses a few times and fades out
			const float Age = (float)(ImGui::GetTime() - s_HighlightStart);
			const float Fade = 1.0f - Age / (float)HIGHLIGHT_TIME;
			const float Pulse = 0.55f + 0.45f * std::cos(Age * 7.0f);
			pDraw->AddRect(Min, Max, Col(t.m_Accent, Fade * Pulse), S(12.0f), 0, S(2.0f));
		}

		ImGui::PopID();
		ImGui::PopStyleVar(); // the appear alpha
		Grid.m_aColumnY[Grid.m_Column] = Bottom - Grid.m_Rise + Gap();
		Grid.m_InCard = false;
	}

	void DrawLogo(ImDrawList *pDraw, ImVec2 Center, float Size, const ImVec4 &Accent, float Alpha, bool Tile)
	{
		// the logo is drawn in a 100 x 100 box (docs/xclient/logo/files/exora_hook_x.svg)
		const float k = Size / 100.0f;
		auto P = [&](float x, float y) { return ImVec2(Center.x + (x - 50.0f) * k, Center.y + (y - 50.0f) * k); };
		const float Thick = 9.0f * k;
		if(Tile)
			pDraw->AddRectFilled(P(0.0f, 0.0f), P(100.0f, 100.0f), Col(ImVec4(0.067f, 0.075f, 0.094f, 1.0f), Alpha), 28.0f * k);
		const ImU32 White = Col(ImVec4(0.93f, 0.933f, 0.95f, 1.0f), Alpha);
		const ImU32 Blue = Col(Accent, Alpha);
		// the hook: a straight line that curls at the bottom left
		pDraw->PathLineTo(P(74.0f, 26.0f));
		pDraw->PathLineTo(P(40.0f, 62.0f));
		pDraw->PathBezierCubicCurveTo(P(34.0f, 68.0f), P(30.0f, 72.0f), P(26.0f, 72.0f), 10);
		pDraw->PathBezierCubicCurveTo(P(21.0f, 72.0f), P(20.0f, 66.0f), P(24.0f, 63.0f), 10);
		pDraw->PathStroke(White, ImDrawFlags_None, Thick);
		// round caps
		pDraw->AddCircleFilled(P(74.0f, 26.0f), Thick * 0.5f, White, 16);
		pDraw->AddCircleFilled(P(24.0f, 63.0f), Thick * 0.5f, White, 16);
		// the accent line over it
		pDraw->AddLine(P(28.0f, 26.0f), P(74.0f, 76.0f), Blue, Thick);
		pDraw->AddCircleFilled(P(28.0f, 26.0f), Thick * 0.5f, Blue, 16);
		pDraw->AddCircleFilled(P(74.0f, 76.0f), Thick * 0.5f, Blue, 16);
	}
} // namespace XcUi

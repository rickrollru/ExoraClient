// Widgets for the visuals page: effect color picker and "mine / dummy / others" chips.
#include "feedback.h"
#include "lang.h"
#include "theme.h"
#include "widgets.h"

#include <base/color.h>

#include <engine/external/imgui/imgui_internal.h>

#include <algorithm>
#include <cmath>

namespace XcUi
{
	static ImVec4 HslToVec(float h, float s, float l)
	{
		const ColorRGBA Rgb = color_cast<ColorRGBA>(ColorHSLA(h, s, l, 1.0f));
		return ImVec4(Rgb.r, Rgb.g, Rgb.b, 1.0f);
	}

	// horizontal bar with the whole hue circle, the knob shows the current hue
	static bool HueBar(const char *pId, float *pHue, float Saturation, float Lightness)
	{
		ImGuiWindow *pWindow = ImGui::GetCurrentWindow();
		if(pWindow->SkipItems)
			return false;
		const ImGuiID Id = pWindow->GetID(pId);
		const float Width = AvailWidth();
		const float Height = S(14.0f);
		const ImVec2 Pos = ImGui::GetCursorScreenPos();
		// a bit taller hit area than the bar
		const ImRect Rect(Pos, ImVec2(Pos.x + Width, Pos.y + Height + S(8.0f)));
		ImGui::ItemSize(Rect);
		if(!ImGui::ItemAdd(Rect, Id))
			return false;
		bool Hovered, Held;
		ImGui::ButtonBehavior(Rect, Id, &Hovered, &Held, ImGuiButtonFlags_PressedOnClick);
		bool Changed = false;
		if(Held)
		{
			const float Hue = std::clamp((ImGui::GetIO().MousePos.x - Rect.Min.x) / Width, 0.0f, 0.999f);
			if(Hue != *pHue)
			{
				*pHue = Hue;
				Changed = true;
			}
		}

		ImDrawList *pDraw = pWindow->DrawList;
		const ImVec2 BarMin(Rect.Min.x, Rect.Min.y + S(4.0f));
		const ImVec2 BarMax(Rect.Max.x, BarMin.y + Height);
		// six gradient pieces make the hue circle, rounded ends are separate caps
		const int Pieces = 12;
		const float Rounding = Height * 0.5f;
		const float InnerMin = BarMin.x + Rounding;
		const float InnerWidth = (BarMax.x - BarMin.x) - Rounding * 2.0f;
		pDraw->AddCircleFilled(ImVec2(BarMin.x + Rounding, BarMin.y + Rounding), Rounding, Col(HslToVec(0.0f, Saturation, Lightness)), 16);
		pDraw->AddCircleFilled(ImVec2(BarMax.x - Rounding, BarMin.y + Rounding), Rounding, Col(HslToVec(0.999f, Saturation, Lightness)), 16);
		for(int i = 0; i < Pieces; i++)
		{
			const float a = (float)i / Pieces;
			const float b = (float)(i + 1) / Pieces;
			const ImU32 Left = Col(HslToVec(a, Saturation, Lightness));
			const ImU32 Right = Col(HslToVec(std::min(b, 0.999f), Saturation, Lightness));
			pDraw->AddRectFilledMultiColor(ImVec2(InnerMin + InnerWidth * a, BarMin.y), ImVec2(InnerMin + InnerWidth * b + 0.5f, BarMax.y), Left, Right, Right, Left);
		}
		const float KnobX = InnerMin + InnerWidth * *pHue;
		const float Knob = Animate(Id, Hovered || Held ? 1.0f : 0.0f, 18.0f);
		const ImVec2 KnobCenter(KnobX, BarMin.y + Rounding);
		pDraw->AddCircleFilled(KnobCenter, Rounding + S(3.0f) + Knob * S(1.5f), Col(ImVec4(1, 1, 1, 1)), 24);
		pDraw->AddCircleFilled(KnobCenter, Rounding + Knob * S(1.0f), Col(HslToVec(*pHue, Saturation, Lightness)), 24);
		return Changed;
	}

	bool EffectColorPicker(const char *pId, int *pMode, unsigned *pColor)
	{
		ImGui::PushID(pId);
		bool Changed = false;
		const char *apModes[] = {Tr("Accent"), Tr("Own"), Tr("Rainbow"), Tr("Skin")};
		int Mode = std::clamp(*pMode, 0, 3);
		if(Segmented("##mode", &Mode, apModes, 4))
		{
			*pMode = Mode;
			Changed = true;
		}
		const float Open = Animate(ImGui::GetID("##open"), Mode == 1 ? 1.0f : 0.0f, 14.0f);
		if(Open > 0.01f)
		{
			// the custom part slides open under the modes
			ImGui::PushStyleVar(ImGuiStyleVar_Alpha, ImGui::GetStyle().Alpha * Open);

			ColorHSLA Hsl(*pColor);
			// swatches: a few ready hues plus white
			static const float s_aHues[] = {0.61f, 0.53f, 0.44f, 0.33f, 0.22f, 0.13f, 0.07f, 0.0f, 0.93f, 0.8f, 0.72f};
			ImVec4 aColors[std::size(s_aHues) + 1];
			for(size_t i = 0; i < std::size(s_aHues); i++)
				aColors[i] = HslToVec(s_aHues[i], 0.9f, 0.62f);
			aColors[std::size(s_aHues)] = ImVec4(0.95f, 0.95f, 0.97f, 1.0f);
			int Picked = -1;
			if(ColorSwatches("##swatches", &Picked, aColors, (int)std::size(aColors)) && Picked >= 0)
			{
				if(Picked == (int)std::size(s_aHues))
					Hsl = ColorHSLA(Hsl.h, 0.0f, 1.0f, 1.0f);
				else
					Hsl = ColorHSLA(s_aHues[Picked], 0.9f, 0.62f, 1.0f);
				*pColor = Hsl.Pack(false);
				Changed = true;
			}
			float Hue = Hsl.h;
			if(HueBar("##hue", &Hue, std::max(Hsl.s, 0.6f), std::clamp(Hsl.l, 0.35f, 0.75f)))
			{
				Hsl.h = Hue;
				Hsl.s = std::max(Hsl.s, 0.6f);
				Hsl.l = std::clamp(Hsl.l, 0.35f, 0.75f);
				*pColor = Hsl.Pack(false);
				Changed = true;
			}
			int Lightness = (int)std::round(Hsl.l * 100.0f);
			if(SliderInt(Tr("Brightness"), &Lightness, 20, 100, "%d%%"))
			{
				Hsl.l = Lightness / 100.0f;
				*pColor = Hsl.Pack(false);
				Changed = true;
			}
			ImGui::PopStyleVar();
		}
		ImGui::PopID();
		return Changed;
	}

	bool TargetChips(const char *pId, int *pSelf, int *pDummy, int *pOthers)
	{
		ImGuiWindow *pWindow = ImGui::GetCurrentWindow();
		if(pWindow->SkipItems)
			return false;
		ImGui::PushID(pId);
		const STheme &t = Theme();
		ImFont *pFont = Fonts().m_pLabel ? Fonts().m_pLabel : ImGui::GetFont();
		const char *apLabels[] = {Tr("Mine"), Tr("Dummy"), Tr("Others")};
		int *apValues[] = {pSelf, pDummy, pOthers};
		const float Gap = S(6.0f);
		const float Width = (AvailWidth() - Gap * 2.0f) / 3.0f;
		const float Height = S(30.0f);
		const ImVec2 Start = ImGui::GetCursorScreenPos();
		bool Changed = false;
		for(int i = 0; i < 3; i++)
		{
			const ImRect Rect(ImVec2(Start.x + i * (Width + Gap), Start.y), ImVec2(Start.x + i * (Width + Gap) + Width, Start.y + Height));
			const ImGuiID Id = pWindow->GetID(apLabels[i]);
			ImGui::ItemSize(ImRect(Rect.Min, ImVec2(Rect.Max.x, Rect.Max.y)));
			if(i < 2)
				ImGui::SameLine(0.0f, Gap);
			if(!ImGui::ItemAdd(Rect, Id))
				continue;
			bool Hovered, Held;
			if(ImGui::ButtonBehavior(Rect, Id, &Hovered, &Held))
			{
				*apValues[i] = !*apValues[i];
				PlayUiSound(*apValues[i] ? SOUND_TOGGLE_ON : SOUND_TOGGLE_OFF);
				Changed = true;
			}
			const float On = Animate(Id, *apValues[i] ? 1.0f : 0.0f, 16.0f);
			const float Hover = Animate(Id + 1, Hovered ? 1.0f : 0.0f, 18.0f);
			ImDrawList *pDraw = pWindow->DrawList;
			pDraw->AddRectFilled(Rect.Min, Rect.Max, Col(Mix(t.m_Track, ImVec4(t.m_Accent.x, t.m_Accent.y, t.m_Accent.z, 0.22f), On), 1.0f + Hover * 0.3f), S(8.0f));
			pDraw->AddRect(Rect.Min, Rect.Max, Col(t.m_Accent, On * 0.8f), S(8.0f), 0, 1.0f);
			const ImVec2 TextSize = pFont->CalcTextSizeA(pFont->FontSize, FLT_MAX, 0.0f, apLabels[i]);
			pDraw->AddText(pFont, pFont->FontSize, ImVec2(Rect.GetCenter().x - TextSize.x * 0.5f, Rect.GetCenter().y - TextSize.y * 0.5f), Col(Mix(t.m_TextMuted, t.m_Text, std::max(On, Hover))), apLabels[i]);
		}
		ImGui::PopID();
		return Changed;
	}
} // namespace XcUi

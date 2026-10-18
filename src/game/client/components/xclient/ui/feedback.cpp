#include "feedback.h"

#include "icons.h"
#include "theme.h"
#include "widgets.h"

#include <engine/external/imgui/imgui.h>

#include <algorithm>
#include <cmath>
#include <deque>
#include <string>

namespace XcUi
{
	static FSoundPlayer s_pfnSoundPlayer = nullptr;
	static void *s_pSoundUser = nullptr;

	void SetSoundPlayer(FSoundPlayer pfnPlayer, void *pUser)
	{
		s_pfnSoundPlayer = pfnPlayer;
		s_pSoundUser = pUser;
	}

	void PlayUiSound(int Sound)
	{
		if(s_pfnSoundPlayer)
			s_pfnSoundPlayer(s_pSoundUser, Sound);
	}

	struct SToast
	{
		std::string m_Text;
		int m_Kind;
		float m_Start = -1.0f; // set on the first render, so toasts made while hidden still show fully
		float m_Slot = 0.0f; // animated stack position
	};
	static std::deque<SToast> s_Toasts;
	static constexpr float TOAST_LIFE = 2.8f;
	static constexpr float TOAST_FADE = 0.3f;
	static constexpr size_t MAX_TOASTS = 4;

	void Toast(const char *pText, int Kind)
	{
		// the same message twice in a row just restarts the timer
		if(!s_Toasts.empty() && s_Toasts.back().m_Text == pText && s_Toasts.back().m_Kind == Kind)
			s_Toasts.back().m_Start = -1.0f;
		else
		{
			SToast New;
			New.m_Text = pText;
			New.m_Kind = Kind;
			New.m_Slot = -1.0f; // comes in from below
			s_Toasts.push_back(New);
			while(s_Toasts.size() > MAX_TOASTS)
				s_Toasts.pop_front();
		}
		// a new song needs no sound of its own
		if(Kind != TOAST_MUSIC)
			PlayUiSound(Kind == TOAST_ERROR ? SOUND_ERROR : SOUND_NOTIFY);
	}

	bool HasToasts()
	{
		return !s_Toasts.empty();
	}

	void RenderToasts(ImDrawList *pDraw, const ImVec2 &DisplaySize, float Time, float Alpha, ImFont *pTextFont, ImFont *pIconFont)
	{
		const STheme &t = Theme();
		ImFont *pFont = pTextFont ? pTextFont : (Fonts().m_pLabel ? Fonts().m_pLabel : ImGui::GetFont());
		if(!pIconFont)
			pIconFont = Fonts().m_pSmall ? Fonts().m_pSmall : ImGui::GetFont();
		const float Height = S(42.0f);
		const float Gap = S(8.0f);
		const float Bottom = DisplaySize.y - S(36.0f);
		const float Dt = ImGui::GetIO().DeltaTime;

		for(auto It = s_Toasts.begin(); It != s_Toasts.end();)
		{
			if(It->m_Start < 0.0f)
				It->m_Start = Time;
			if(Time - It->m_Start > TOAST_LIFE + TOAST_FADE)
				It = s_Toasts.erase(It);
			else
				++It;
		}

		const int Count = (int)s_Toasts.size();
		for(int i = 0; i < Count; i++)
		{
			SToast &Toast = s_Toasts[i];
			// newest at the bottom, older ones slide up
			const float Target = (float)(Count - 1 - i);
			Toast.m_Slot += (Target - Toast.m_Slot) * (1.0f - std::exp(-14.0f * Dt));
			const float Age = Time - Toast.m_Start;
			const float In = std::clamp(Age / 0.25f, 0.0f, 1.0f);
			const float Out = std::clamp((TOAST_LIFE + TOAST_FADE - Age) / TOAST_FADE, 0.0f, 1.0f);
			const float Ease = 1.0f - (1.0f - In) * (1.0f - In) * (1.0f - In);
			const float a = Alpha * std::min(Ease, Out);
			if(a <= 0.0f)
				continue;

			const char *pIcon = Toast.m_Kind == TOAST_ERROR ? XC_ICON_XMARK : (Toast.m_Kind == TOAST_INFO ? XC_ICON_BOLT : (Toast.m_Kind == TOAST_MUSIC ? XC_ICON_MUSIC : XC_ICON_CHECK));
			const ImVec4 IconColor = Toast.m_Kind == TOAST_ERROR ? ImVec4(1.0f, 0.36f, 0.36f, 1.0f) : t.m_Accent;
			const ImVec2 TextSize = pFont->CalcTextSizeA(pFont->FontSize, FLT_MAX, 0.0f, Toast.m_Text.c_str());
			const float Width = TextSize.x + Height + S(22.0f);
			const float y = Bottom - Height - Toast.m_Slot * (Height + Gap) + (1.0f - Ease) * S(16.0f);
			const ImVec2 Min((DisplaySize.x - Width) * 0.5f, y);
			const ImVec2 Max(Min.x + Width, y + Height);

			// soft shadow, pill, icon dot, text
			for(int s = 3; s >= 1; s--)
				pDraw->AddRectFilled(ImVec2(Min.x - s * 2.0f, Min.y - s + S(4.0f)), ImVec2(Max.x + s * 2.0f, Max.y + s * 2.0f + S(4.0f)), Col(ImVec4(0, 0, 0, 1), 0.06f * a), Height * 0.5f + s * 2.0f);
			pDraw->AddRectFilled(Min, Max, Col(t.m_Card, a / std::max(t.m_Card.w, 0.01f) * 0.98f), Height * 0.5f);
			pDraw->AddRect(Min, Max, Col(t.m_Border, a), Height * 0.5f, 0, 1.0f);
			const ImVec2 Dot(Min.x + Height * 0.5f, Min.y + Height * 0.5f);
			pDraw->AddCircleFilled(Dot, Height * 0.30f, Col(IconColor, 0.18f * a), 24);
			const ImVec2 IconSize = pIconFont->CalcTextSizeA(pIconFont->FontSize, FLT_MAX, 0.0f, pIcon);
			pDraw->AddText(pIconFont, pIconFont->FontSize, ImVec2(Dot.x - IconSize.x * 0.5f, Dot.y - IconSize.y * 0.5f), Col(IconColor, a), pIcon);
			pDraw->AddText(pFont, pFont->FontSize, ImVec2(Min.x + Height + S(4.0f), Min.y + (Height - TextSize.y) * 0.5f), Col(t.m_Text, a), Toast.m_Text.c_str());
		}
	}
} // namespace XcUi

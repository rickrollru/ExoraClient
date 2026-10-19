#include "theme.h"

#include <algorithm>

namespace XcUi
{
	static STheme s_Theme;
	static float s_Scale = 1.0f;
	static float s_AnimationSpeed = 1.0f;

	static ImVec4 Hex(unsigned Rgb, float Alpha = 1.0f)
	{
		return ImVec4(((Rgb >> 16) & 0xFF) / 255.0f, ((Rgb >> 8) & 0xFF) / 255.0f, (Rgb & 0xFF) / 255.0f, Alpha);
	}

	ImVec4 AccentColor(int Accent, bool Dark)
	{
		switch(Accent)
		{
		case ACCENT_MINT: return Hex(0x3DD6A4);
		case ACCENT_ORANGE: return Hex(0xFF8A3D);
		case ACCENT_PINK: return Hex(0xFF5C8A);
		case ACCENT_LIME: return Hex(0xB6E83C);
		case ACCENT_RED: return Hex(0xFF4D4D);
		// monochrome: white on the dark theme, near black on the light one
		case ACCENT_WHITE: return Dark ? Hex(0xF2F3F5) : Hex(0x15171C);
		default: return Hex(0x4F8CFF);
		}
	}

	const STheme &Theme()
	{
		return s_Theme;
	}

	STheme MakeTheme(int Theme, int Accent)
	{
		STheme t;
		t.m_Dark = Theme != THEME_LIGHT;
		if(t.m_Dark)
		{
			t.m_Background = Hex(0x0A0B0E);
			t.m_Panel = ImVec4(17 / 255.0f, 19 / 255.0f, 24 / 255.0f, 0.82f);
			t.m_Card = ImVec4(28 / 255.0f, 31 / 255.0f, 38 / 255.0f, 0.9f);
			t.m_CardHover = ImVec4(34 / 255.0f, 38 / 255.0f, 46 / 255.0f, 0.92f);
			t.m_Border = ImVec4(1.0f, 1.0f, 1.0f, 0.07f);
			t.m_Text = Hex(0xF2F3F5);
			t.m_TextSecondary = Hex(0xC9CCD2);
			t.m_TextMuted = Hex(0x8C919B);
			t.m_Track = ImVec4(1.0f, 1.0f, 1.0f, 0.1f);
			t.m_Shadow = ImVec4(0.0f, 0.0f, 0.0f, 0.35f);
		}
		else
		{
			t.m_Background = Hex(0xF3F4F7);
			t.m_Panel = ImVec4(1.0f, 1.0f, 1.0f, 0.86f);
			t.m_Card = ImVec4(1.0f, 1.0f, 1.0f, 0.95f);
			t.m_CardHover = Hex(0xF7F8FA, 0.97f);
			t.m_Border = ImVec4(0.0f, 0.0f, 0.0f, 0.08f);
			t.m_Text = Hex(0x15171C);
			t.m_TextSecondary = Hex(0x3A3E47);
			t.m_TextMuted = Hex(0x7A808C);
			t.m_Track = ImVec4(0.0f, 0.0f, 0.0f, 0.1f);
			t.m_Shadow = ImVec4(0.0f, 0.0f, 0.0f, 0.12f);
		}
		t.m_Accent = AccentColor(Accent, t.m_Dark);
		// dark text on light accents (white, lime, mint), white text on the others
		const float Luma = 0.299f * t.m_Accent.x + 0.587f * t.m_Accent.y + 0.114f * t.m_Accent.z;
		t.m_OnAccent = Luma > 0.6f ? Hex(0x0A0B0E) : Hex(0xFFFFFF);
		return t;
	}

	void SetTheme(int Theme, int Accent)
	{
		s_Theme = MakeTheme(Theme, Accent);
	}

	float Scale()
	{
		return s_Scale;
	}

	void SetScale(float Scale)
	{
		s_Scale = std::clamp(Scale, 0.5f, 3.0f);
	}

	float AnimationSpeed()
	{
		return s_AnimationSpeed;
	}

	void SetAnimationSpeed(float Speed)
	{
		s_AnimationSpeed = Speed;
	}

	ImU32 Col(const ImVec4 &Color, float AlphaMul)
	{
		// the style alpha fades everything drawn by hand together with the widgets (opening, dimmed cards)
		const float StyleAlpha = ImGui::GetCurrentContext() ? ImGui::GetStyle().Alpha : 1.0f;
		return ImGui::ColorConvertFloat4ToU32(ImVec4(Color.x, Color.y, Color.z, Color.w * AlphaMul * StyleAlpha));
	}

	ImVec4 Mix(const ImVec4 &a, const ImVec4 &b, float t)
	{
		return ImVec4(a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, a.z + (b.z - a.z) * t, a.w + (b.w - a.w) * t);
	}

	void ApplyStyle()
	{
		const STheme &t = s_Theme;
		ImGuiStyle &Style = ImGui::GetStyle();
		Style = ImGuiStyle();
		Style.WindowPadding = ImVec2(S(16), S(16));
		Style.FramePadding = ImVec2(S(10), S(7));
		Style.ItemSpacing = ImVec2(S(10), S(10));
		Style.ItemInnerSpacing = ImVec2(S(8), S(6));
		Style.ScrollbarSize = S(6);
		Style.WindowRounding = S(16);
		Style.ChildRounding = S(14);
		Style.FrameRounding = S(8);
		Style.PopupRounding = S(10);
		Style.ScrollbarRounding = S(6);
		Style.GrabRounding = S(6);
		Style.WindowBorderSize = 1.0f;
		Style.ChildBorderSize = 1.0f;
		Style.FrameBorderSize = 0.0f;
		Style.PopupBorderSize = 1.0f;
		Style.WindowMinSize = ImVec2(S(32), S(32));
		Style.AntiAliasedFill = true;
		Style.AntiAliasedLines = true;

		ImVec4 *c = Style.Colors;
		c[ImGuiCol_Text] = t.m_Text;
		c[ImGuiCol_TextDisabled] = t.m_TextMuted;
		c[ImGuiCol_WindowBg] = t.m_Panel;
		c[ImGuiCol_ChildBg] = ImVec4(0, 0, 0, 0);
		c[ImGuiCol_PopupBg] = Mix(t.m_Card, t.m_Background, 0.2f);
		c[ImGuiCol_PopupBg].w = 0.98f;
		c[ImGuiCol_Border] = t.m_Border;
		c[ImGuiCol_BorderShadow] = ImVec4(0, 0, 0, 0);
		c[ImGuiCol_FrameBg] = t.m_Track;
		c[ImGuiCol_FrameBgHovered] = ImVec4(t.m_Track.x, t.m_Track.y, t.m_Track.z, t.m_Track.w * 1.6f);
		c[ImGuiCol_FrameBgActive] = ImVec4(t.m_Track.x, t.m_Track.y, t.m_Track.z, t.m_Track.w * 2.0f);
		c[ImGuiCol_ScrollbarBg] = ImVec4(0, 0, 0, 0);
		c[ImGuiCol_ScrollbarGrab] = t.m_Track;
		c[ImGuiCol_ScrollbarGrabHovered] = ImVec4(t.m_Track.x, t.m_Track.y, t.m_Track.z, t.m_Track.w * 2.0f);
		c[ImGuiCol_ScrollbarGrabActive] = t.m_Accent;
		c[ImGuiCol_CheckMark] = t.m_Accent;
		c[ImGuiCol_SliderGrab] = t.m_Accent;
		c[ImGuiCol_SliderGrabActive] = t.m_Accent;
		c[ImGuiCol_Button] = t.m_Track;
		c[ImGuiCol_ButtonHovered] = ImVec4(t.m_Track.x, t.m_Track.y, t.m_Track.z, t.m_Track.w * 1.8f);
		c[ImGuiCol_ButtonActive] = ImVec4(t.m_Track.x, t.m_Track.y, t.m_Track.z, t.m_Track.w * 2.4f);
		c[ImGuiCol_Header] = ImVec4(t.m_Accent.x, t.m_Accent.y, t.m_Accent.z, 0.18f);
		c[ImGuiCol_HeaderHovered] = ImVec4(t.m_Accent.x, t.m_Accent.y, t.m_Accent.z, 0.25f);
		c[ImGuiCol_HeaderActive] = ImVec4(t.m_Accent.x, t.m_Accent.y, t.m_Accent.z, 0.35f);
		c[ImGuiCol_Separator] = t.m_Border;
		c[ImGuiCol_TextSelectedBg] = ImVec4(t.m_Accent.x, t.m_Accent.y, t.m_Accent.z, 0.35f);
		c[ImGuiCol_NavCursor] = t.m_Accent;
		c[ImGuiCol_ModalWindowDimBg] = ImVec4(0, 0, 0, 0.45f);
	}
} // namespace XcUi

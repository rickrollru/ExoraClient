// "Now playing" card on the home page: cover, title, progress and the buttons of the music player.
#include "feedback.h"
#include "icons.h"
#include "lang.h"
#include "menu.h"
#include "theme.h"
#include "widgets.h"

#include <base/str.h>

#include <engine/external/imgui/imgui.h>
#include <engine/external/imgui/imgui_internal.h>
#include <engine/shared/config.h>

#include <game/client/gameclient.h>

#include <cmath>

using namespace XcUi;

static void FormatTime(double Seconds, char *pBuf, int BufSize)
{
	const int Total = (int)Seconds;
	if(Total >= 3600)
		str_format(pBuf, BufSize, "%d:%02d:%02d", Total / 3600, (Total / 60) % 60, Total % 60);
	else
		str_format(pBuf, BufSize, "%d:%02d", Total / 60, Total % 60);
}

// round button with an icon; the main one is filled with the accent color
static bool RoundButton(const char *pId, const char *pIcon, float Radius, bool Main, ImFont *pIconFont)
{
	const STheme &t = Theme();
	const ImVec2 Pos = ImGui::GetCursorScreenPos();
	const bool Clicked = ImGui::InvisibleButton(pId, ImVec2(Radius * 2.0f, Radius * 2.0f));
	const bool Hovered = ImGui::IsItemHovered();
	const bool Held = ImGui::IsItemActive();
	const float Hover = Animate(ImGui::GetID(pId), Hovered ? 1.0f : 0.0f, 18.0f);
	const ImVec2 Center(Pos.x + Radius, Pos.y + Radius);
	const float Size = Radius * (Held ? 0.92f : 1.0f + 0.04f * Hover);
	ImDrawList *pDraw = ImGui::GetWindowDrawList();
	if(Main)
	{
		pDraw->AddCircleFilled(Center, Size + S(5.0f) * Hover, Col(t.m_Accent, 0.18f * Hover), 40);
		pDraw->AddCircleFilled(Center, Size, Col(Mix(t.m_Accent, ImVec4(1, 1, 1, 1), 0.1f * Hover)), 40);
	}
	else
		pDraw->AddCircleFilled(Center, Size, Col(Mix(t.m_Track, t.m_Accent, 0.25f * Hover)), 40);
	const float IconSize = Radius * (Main ? 0.78f : 0.7f);
	const ImVec2 IconDim = pIconFont->CalcTextSizeA(IconSize, FLT_MAX, 0.0f, pIcon);
	pDraw->AddText(pIconFont, IconSize, ImVec2(Center.x - IconDim.x * 0.5f, Center.y - IconDim.y * 0.5f), Col(Main ? t.m_OnAccent : t.m_Text), pIcon);
	if(Clicked)
		PlayUiSound(SOUND_CLICK);
	return Clicked;
}

void CXcMenu::RenderMusicCard()
{
	CXcMusic &Music = GameClient()->m_XcMusic;
	if(!g_Config.m_XcMusic || !Music.Supported())
		return;
	const STheme &t = Theme();
	if(!BeginCard("##music", Tr("Music"), Music.HasTrack() ? Music.AppName() : Tr("What plays in the browser or a music app")))
		return;

	const SXcMediaState &State = Music.State();
	const bool HasTrack = Music.HasTrack();
	ImDrawList *pDraw = ImGui::GetWindowDrawList();
	const float ArtSize = S(112.0f);
	const ImVec2 ArtMin = ImGui::GetCursorScreenPos();
	const ImVec2 ArtMax(ArtMin.x + ArtSize, ArtMin.y + ArtSize);
	const float Rounding = S(12.0f);
	const IGraphics::CTextureHandle Art = Music.Art();
	if(HasTrack && Art.IsValid() && !Art.IsNullTexture())
	{
		// a soft shadow of the cover's own colors under it
		pDraw->AddImageRounded((ImTextureID)(uintptr_t)m_Backend.TextureId(Art), ImVec2(ArtMin.x + S(4.0f), ArtMin.y + S(8.0f)), ImVec2(ArtMax.x - S(4.0f), ArtMax.y + S(6.0f)), ImVec2(0, 0), ImVec2(1, 1), Col(ImVec4(1, 1, 1, 1), 0.25f), Rounding);
		pDraw->AddImageRounded((ImTextureID)(uintptr_t)m_Backend.TextureId(Art), ArtMin, ArtMax, ImVec2(0, 0), ImVec2(1, 1), IM_COL32_WHITE, Rounding);
	}
	else
	{
		pDraw->AddRectFilled(ArtMin, ArtMax, Col(t.m_Accent, 0.16f), Rounding);
		ImFont *pIcons = Fonts().m_pHeader;
		const float IconSize = ArtSize * 0.34f;
		const ImVec2 IconDim = pIcons->CalcTextSizeA(IconSize, FLT_MAX, 0.0f, XC_ICON_MUSIC);
		pDraw->AddText(pIcons, IconSize, ImVec2((ArtMin.x + ArtMax.x - IconDim.x) * 0.5f, (ArtMin.y + ArtMax.y - IconDim.y) * 0.5f), Col(t.m_Accent), XC_ICON_MUSIC);
	}
	ImGui::Dummy(ImVec2(ArtSize, ArtSize));
	ImGui::SameLine(0.0f, S(16.0f));

	ImGui::BeginGroup();
	const float Width = AvailWidth();
	// one line cut with "..." in the pushed text color
	const auto Clipped = [&](ImFont *pFont, const char *pText) {
		const ImVec2 Pos = ImGui::GetCursorScreenPos();
		ImGui::PushFont(pFont);
		ImGui::RenderTextEllipsis(pDraw, Pos, ImVec2(Pos.x + Width, Pos.y + ImGui::GetFontSize() + S(2.0f)), Pos.x + Width, Pos.x + Width, pText, nullptr, nullptr);
		ImGui::PopFont();
		ImGui::Dummy(ImVec2(Width, pFont->FontSize + S(4.0f)));
	};
	if(HasTrack)
	{
		ImGui::PushStyleColor(ImGuiCol_Text, t.m_Text);
		Clipped(Fonts().m_pTitle, State.m_Title.c_str());
		ImGui::PopStyleColor();
		ImGui::PushStyleColor(ImGuiCol_Text, t.m_TextSecondary);
		Clipped(Fonts().m_pBody, State.m_Artist.empty() ? Music.AppName() : State.m_Artist.c_str());
		ImGui::PopStyleColor();
	}
	else
	{
		ImGui::PushStyleColor(ImGuiCol_Text, t.m_Text);
		Clipped(Fonts().m_pTitle, Tr("Nothing playing"));
		ImGui::PopStyleColor();
		Description(Tr("Turn on music in the browser or a player, it shows up here"), Width);
	}

	// progress with times
	const double Position = Music.Position();
	ImGui::Dummy(ImVec2(0.0f, S(4.0f)));
	if(HasTrack && Position >= 0.0)
	{
		const ImVec2 BarPos = ImGui::GetCursorScreenPos();
		const float BarHeight = S(4.0f);
		const float Fill = (float)(Position / State.m_Duration);
		pDraw->AddRectFilled(BarPos, ImVec2(BarPos.x + Width, BarPos.y + BarHeight), Col(t.m_Track), BarHeight);
		pDraw->AddRectFilled(BarPos, ImVec2(BarPos.x + Width * Fill, BarPos.y + BarHeight), Col(t.m_Accent), BarHeight);
		pDraw->AddCircleFilled(ImVec2(BarPos.x + Width * Fill, BarPos.y + BarHeight * 0.5f), S(5.0f), Col(t.m_Text), 16);
		ImGui::Dummy(ImVec2(Width, BarHeight + S(4.0f)));
		char aNow[16], aTotal[16];
		FormatTime(Position, aNow, sizeof(aNow));
		FormatTime(State.m_Duration, aTotal, sizeof(aTotal));
		ImFont *pSmall = Fonts().m_pSmall;
		const ImVec2 TimePos = ImGui::GetCursorScreenPos();
		const float TotalWidth = pSmall->CalcTextSizeA(pSmall->FontSize, FLT_MAX, 0.0f, aTotal).x;
		pDraw->AddText(pSmall, pSmall->FontSize, TimePos, Col(t.m_TextMuted), aNow);
		pDraw->AddText(pSmall, pSmall->FontSize, ImVec2(TimePos.x + Width - TotalWidth, TimePos.y), Col(t.m_TextMuted), aTotal);
		ImGui::Dummy(ImVec2(Width, pSmall->FontSize));
	}
	ImGui::EndGroup();

	// buttons in the middle under everything
	ImGui::Dummy(ImVec2(0.0f, S(6.0f)));
	const float Small = S(19.0f);
	const float Big = S(25.0f);
	const float Gap = S(14.0f);
	const float Total = Small * 4.0f + Big * 2.0f + Gap * 2.0f;
	ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (AvailWidth() - Total) * 0.5f);
	ImFont *pIcons = Fonts().m_pLabel;
	const float Row = ImGui::GetCursorPosY();
	ImGui::SetCursorPosY(Row + Big - Small);
	if(RoundButton("##prev", XC_ICON_BACKWARD, Small, false, pIcons))
		Music.Previous();
	ImGui::SameLine(0.0f, Gap);
	ImGui::SetCursorPosY(Row);
	if(RoundButton("##play", State.m_Playing && HasTrack ? XC_ICON_PAUSE : XC_ICON_PLAY, Big, true, pIcons))
		Music.PlayPause();
	ImGui::SameLine(0.0f, Gap);
	ImGui::SetCursorPosY(Row + Big - Small);
	if(RoundButton("##next", XC_ICON_FORWARD, Small, false, pIcons))
		Music.Next();
	EndCard();
}

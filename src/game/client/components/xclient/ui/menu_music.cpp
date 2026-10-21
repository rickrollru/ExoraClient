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

#include <algorithm>
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
	const STheme &t = Theme();
	// the card is always there: when the player is off or missing, it says why and how to fix it
	if(!g_Config.m_XcMusic || !Music.Supported())
	{
		if(BeginCard("##music", Tr("Music"), Tr("What plays in the browser or a music app")))
		{
			if(!Music.Supported())
			{
#if defined(CONF_FAMILY_WINDOWS)
				Description(Tr("The music player is not in this build: the Windows SDK with C++/WinRT was not found when it was built."));
#else
				Description(Tr("The music player needs the playerctl program. Install it (for example: sudo apt install playerctl) and restart the game."));
#endif
			}
			else
			{
				Description(Tr("The music player is off. It shows the track from YouTube, Spotify, Yandex Music and others, with the buttons."));
				ImGui::Dummy(ImVec2(0.0f, S(4.0f)));
				if(Button(Tr("Turn on"), true, -1.0f))
					g_Config.m_XcMusic = 1;
			}
			EndCard();
		}
		return;
	}
	if(!BeginCard("##music", Tr("Music"), Music.HasTrack() ? Music.AppName() : Tr("What plays in the browser or a music app")))
		return;

	const SXcMediaState &State = Music.State();
	const bool HasTrack = Music.HasTrack();
	ImDrawList *pDraw = ImGui::GetWindowDrawList();
	const float ArtSize = S(96.0f);
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
		// the bar can be dragged to jump inside the track (when the player allows it)
		const ImVec2 BarPos = ImGui::GetCursorScreenPos();
		const float BarHeight = S(4.0f);
		const float HitHeight = S(16.0f);
		const bool CanSeek = State.m_CanSeek;
		ImGui::SetCursorScreenPos(ImVec2(BarPos.x, BarPos.y - (HitHeight - BarHeight) * 0.5f));
		ImGui::InvisibleButton("##seek", ImVec2(Width, HitHeight));
		const bool Hovered = ImGui::IsItemHovered() && CanSeek;
		const bool Dragging = ImGui::IsItemActive() && CanSeek;
		const float MouseFill = std::clamp((ImGui::GetIO().MousePos.x - BarPos.x) / Width, 0.0f, 1.0f);
		if(Hovered || Dragging)
			ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
		if(ImGui::IsItemDeactivated() && CanSeek)
			Music.Seek(MouseFill * State.m_Duration);
		const float Grow = Animate(ImGui::GetID("##seek_grow"), Hovered || Dragging ? 1.0f : 0.0f, 16.0f);
		const float Fill = Dragging ? MouseFill : (float)(Position / State.m_Duration);
		const float Thick = BarHeight * (1.0f + 0.5f * Grow);
		const float Mid = BarPos.y + BarHeight * 0.5f;
		pDraw->AddRectFilled(ImVec2(BarPos.x, Mid - Thick * 0.5f), ImVec2(BarPos.x + Width, Mid + Thick * 0.5f), Col(t.m_Track), Thick);
		if(Hovered && !Dragging)
			pDraw->AddRectFilled(ImVec2(BarPos.x, Mid - Thick * 0.5f), ImVec2(BarPos.x + Width * MouseFill, Mid + Thick * 0.5f), Col(t.m_Accent, 0.25f), Thick);
		pDraw->AddRectFilled(ImVec2(BarPos.x, Mid - Thick * 0.5f), ImVec2(BarPos.x + Width * Fill, Mid + Thick * 0.5f), Col(t.m_Accent), Thick);
		pDraw->AddCircleFilled(ImVec2(BarPos.x + Width * Fill, Mid), S(5.0f) + S(2.0f) * Grow, Col(t.m_Text), 16);
		char aNow[16], aTotal[16];
		FormatTime(Dragging ? MouseFill * State.m_Duration : Position, aNow, sizeof(aNow));
		FormatTime(State.m_Duration, aTotal, sizeof(aTotal));
		if(Hovered && !Dragging)
		{
			char aAt[16];
			FormatTime(MouseFill * State.m_Duration, aAt, sizeof(aAt));
			ImGui::SetTooltip("%s", aAt);
		}
		ImGui::SetCursorScreenPos(ImVec2(BarPos.x, BarPos.y + BarHeight + S(4.0f)));
		ImFont *pSmall = Fonts().m_pSmall;
		const ImVec2 TimePos = ImGui::GetCursorScreenPos();
		const float TotalWidth = pSmall->CalcTextSizeA(pSmall->FontSize, FLT_MAX, 0.0f, aTotal).x;
		pDraw->AddText(pSmall, pSmall->FontSize, TimePos, Col(Dragging ? t.m_Text : t.m_TextMuted), aNow);
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

	// the volume of the player app: the speaker mutes, the bar drags, the wheel steps by 5%
	// when the player's sound is not found (it is paused, or the app is unknown) the row stays, dimmed
	const float KnownVolume = Music.Volume();
	const bool VolumeKnown = KnownVolume >= 0.0f;
	const float Volume = VolumeKnown ? KnownVolume : 0.0f;
	if(HasTrack)
	{
		ImGui::BeginDisabled(!VolumeKnown);
		ImGui::Dummy(ImVec2(0.0f, S(8.0f)));
		static float s_VolumeBeforeMute = 0.5f;
		ImFont *pSmall = Fonts().m_pSmall;
		const float RowWidth = AvailWidth();
		const float RowHeight = S(20.0f);
		const ImVec2 RowPos = ImGui::GetCursorScreenPos();
		const char *pSpeaker = Volume <= 0.0f ? XC_ICON_VOLUME_MUTE : XC_ICON_VOLUME;
		const float IconWidth = S(22.0f);
		ImGui::InvisibleButton("##mute", ImVec2(IconWidth, RowHeight));
		const bool IconHovered = ImGui::IsItemHovered();
		if(ImGui::IsItemClicked())
		{
			if(Volume > 0.0f)
			{
				s_VolumeBeforeMute = Volume;
				Music.SetVolume(0.0f);
			}
			else
				Music.SetVolume(s_VolumeBeforeMute > 0.0f ? s_VolumeBeforeMute : 0.5f);
			PlayUiSound(SOUND_CLICK);
		}
		if(IconHovered)
			ImGui::SetTooltip("%s", Volume > 0.0f ? Tr("Mute") : Tr("Unmute"));
		const ImVec2 IconDim = pIcons->CalcTextSizeA(pIcons->FontSize, FLT_MAX, 0.0f, pSpeaker);
		pDraw->AddText(pIcons, pIcons->FontSize, ImVec2(RowPos.x + (IconWidth - IconDim.x) * 0.5f, RowPos.y + (RowHeight - IconDim.y) * 0.5f), Col(IconHovered ? t.m_Text : t.m_TextSecondary), pSpeaker);

		char aPercent[16];
		if(VolumeKnown)
			str_format(aPercent, sizeof(aPercent), "%d%%", (int)std::round(Volume * 100.0f));
		else
			str_copy(aPercent, "—");
		const float PercentWidth = pSmall->CalcTextSizeA(pSmall->FontSize, FLT_MAX, 0.0f, "100%").x;
		const float BarX = RowPos.x + IconWidth + S(10.0f);
		const float BarWidth = RowWidth - IconWidth - S(20.0f) - PercentWidth;
		ImGui::SetCursorScreenPos(ImVec2(BarX, RowPos.y));
		ImGui::InvisibleButton("##volume", ImVec2(BarWidth, RowHeight));
		const bool Hovered = ImGui::IsItemHovered();
		const bool Dragging = ImGui::IsItemActive();
		const float MouseLevel = std::clamp((ImGui::GetIO().MousePos.x - BarX) / BarWidth, 0.0f, 1.0f);
		if(Dragging && std::abs(MouseLevel - Volume) > 0.005f)
			Music.SetVolume(MouseLevel);
		if(Hovered)
			m_InnerWheel = true; // the wheel turns the volume, not the page
		if(Hovered && ImGui::GetIO().MouseWheel != 0.0f)
			Music.SetVolume(Volume + (ImGui::GetIO().MouseWheel > 0.0f ? 0.05f : -0.05f));
		if(Hovered || Dragging)
			ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
		const float Grow = Animate(ImGui::GetID("##volume_grow"), Hovered || Dragging ? 1.0f : 0.0f, 16.0f);
		const float Thick = S(4.0f) * (1.0f + 0.5f * Grow);
		const float Mid = RowPos.y + RowHeight * 0.5f;
		const float Shown = Dragging ? MouseLevel : Volume;
		pDraw->AddRectFilled(ImVec2(BarX, Mid - Thick * 0.5f), ImVec2(BarX + BarWidth, Mid + Thick * 0.5f), Col(t.m_Track), Thick);
		if(VolumeKnown)
		{
			pDraw->AddRectFilled(ImVec2(BarX, Mid - Thick * 0.5f), ImVec2(BarX + BarWidth * Shown, Mid + Thick * 0.5f), Col(t.m_Accent), Thick);
			pDraw->AddCircleFilled(ImVec2(BarX + BarWidth * Shown, Mid), S(5.0f) + S(2.0f) * Grow, Col(t.m_Text), 16);
		}
		const float PercentTextWidth = pSmall->CalcTextSizeA(pSmall->FontSize, FLT_MAX, 0.0f, aPercent).x;
		pDraw->AddText(pSmall, pSmall->FontSize, ImVec2(RowPos.x + RowWidth - PercentTextWidth, Mid - pSmall->FontSize * 0.5f), Col(Dragging ? t.m_Text : t.m_TextMuted), aPercent);
		ImGui::SetCursorScreenPos(ImVec2(RowPos.x, RowPos.y + RowHeight));
		ImGui::Dummy(ImVec2(RowWidth, 0.0f));
		ImGui::EndDisabled();
		if(!VolumeKnown && ImGui::IsMouseHoveringRect(RowPos, ImVec2(RowPos.x + RowWidth, RowPos.y + RowHeight)))
			ImGui::SetTooltip("%s", Tr("The volume of this player is not found yet: start the music"));
	}
	EndCard();
}

// The island at the top of the HUD: a small glass capsule with the race time, the track that plays (or the line of the
// song sung now), the logo and an own text. A notification (a finish, a record, the dummy) takes it for a moment;
// a new track, or the xc_island_player bind, grows it into a player with the cover and the lyrics. It always moves to
// the size of what it shows on a spring, so every change is one soft movement.
#include "xc_hud.h"

#include <base/str.h>

#include <engine/client.h>
#include <engine/graphics.h>
#include <engine/shared/config.h>

#include <game/client/components/xclient/fx_renderer.h>
#include <game/client/components/xclient/music/lyrics.h>
#include <game/client/components/xclient/music/music.h>
#include <game/client/components/xclient/ui/feedback.h>
#include <game/client/components/xclient/ui/icons.h>
#include <game/client/components/xclient/ui/lang.h>
#include <game/client/components/xclient/ui/theme.h>
#include <game/client/components/xclient/ui/widgets.h>
#include <game/client/gameclient.h>

#include <algorithm>
#include <cmath>
#include <ctime>

using namespace XcUi;

namespace
{
	// a critically damped spring, the same at any frame rate
	void Spring(float &Value, float &Velocity, float Target, float Dt, float Time)
	{
		const float Omega = 2.0f / std::max(Time, 0.01f);
		const float x = Omega * Dt;
		const float Exp = 1.0f / (1.0f + x + 0.48f * x * x + 0.235f * x * x * x);
		const float Change = Value - Target;
		const float Temp = (Velocity + Omega * Change) * Dt;
		Velocity = (Velocity - Omega * Temp) * Exp;
		Value = Target + (Change + Temp) * Exp;
	}
	float Approach(float Value, float Target, float Speed, float Dt)
	{
		return Value < Target ? std::min(Value + Dt * Speed, Target) : std::max(Value - Dt * Speed, Target);
	}
	float Smooth(float x)
	{
		x = std::clamp(x, 0.0f, 1.0f);
		return x * x * (3.0f - 2.0f * x);
	}
}

bool CXcHud::IslandHas(char Part) const
{
	const char aPart[2] = {Part, '\0'};
	return str_find(g_Config.m_XcHudIslandParts, aPart) != nullptr;
}

bool CXcHud::IslandTakesNotes() const
{
	return g_Config.m_XcHud && g_Config.m_XcHudIsland && IslandHas('n') && !g_Config.m_XcFocusMode;
}

void CXcHud::IslandNotify(const char *pTitle, const char *pSubtitle, int Kind)
{
	str_copy(m_aIslandNoteTitle, pTitle);
	str_copy(m_aIslandNoteSub, pSubtitle);
	m_IslandNoteKind = Kind;
	m_IslandNoteStart = Client()->GlobalTime();
}

void CXcHud::ConIslandPlayer(IConsole::IResult *pResult, void *pUserData)
{
	CXcHud *pSelf = (CXcHud *)pUserData;
	pSelf->m_IslandPlayerPinned = !pSelf->m_IslandPlayerPinned;
	pSelf->m_IslandPlayerUntil = -1.0f;
}

void CXcHud::RenderIsland(ImDrawList *pDraw, float Alpha, float Dt)
{
	const STheme &t = Theme();
	const float U = Unit(ELEMENT_ISLAND);
	const float Now = Client()->GlobalTime();
	ImFont *pLabel = m_Backend.Fonts().m_pLabel;
	ImFont *pTitle = m_Backend.Fonts().m_pTitle;
	ImFont *pSmall = m_Backend.Fonts().m_pSmall;
	const CXcMusic &Music = GameClient()->m_XcMusic;
	const bool HasTrack = Music.HasTrack() || m_Editing;
	const SXcMediaState &State = Music.State();

	// a new track opens the player for a few seconds
	// browsers change the title of a tab for many reasons (ads, a page title, the artist coming late): a track counts
	// as new when it stays two seconds, while it plays, and with another title
	const std::string TrackKey = State.m_Title;
	if(Music.HasTrack() && TrackKey != m_IslandSeenTrack)
	{
		m_IslandSeenTrack = TrackKey;
		m_IslandSeenTrackTime = Now;
	}
	if(Music.HasTrack() && State.m_Playing && m_IslandSeenTrack != m_IslandTrack && Now - m_IslandSeenTrackTime > 2.0f)
	{
		if(!m_IslandTrack.empty() && IslandHas('m') && g_Config.m_XcHudIslandAutoPlayer)
			m_IslandPlayerUntil = Now + 5.0f;
		m_IslandTrack = m_IslandSeenTrack;
	}
	const bool Note = IslandHas('n') && !m_Editing && Now - m_IslandNoteStart < 2.8f && m_IslandNoteStart > 0.0f;
	// the player: a few seconds for a new track, or kept open (the bind, a click on the island in the HUD editor)
	const bool Player = !Note && IslandHas('m') && (Music.HasTrack() || m_Editing) && (m_IslandPlayerPinned || Now < m_IslandPlayerUntil);
	m_IslandOverControl = false;
	const int Mode = Note ? 1 : (Player ? 2 : 0);

	// the line sung now (timed lyrics only)
	bool Approx = false;
	const std::vector<CXcLyrics::SLine> *pLines = Music.HasTrack() ? GameClient()->m_XcLyrics.CurrentLines(&Approx) : nullptr;
	const double Position = Music.Position();
	int LineNow = -1;
	if(pLines && Position >= 0.0)
		for(int i = 0; i < (int)pLines->size(); i++)
			if((*pLines)[i].m_Time <= Position)
				LineNow = i;
	// how much of the line is sung: from its start to the next line (or the end of the track)
	float LineFrac = 0.0f;
	if(LineNow >= 0)
	{
		const double Start = (*pLines)[LineNow].m_Time;
		const double End = LineNow + 1 < (int)pLines->size() ? (*pLines)[LineNow + 1].m_Time : std::max(State.m_Duration, Start + 4.0);
		LineFrac = std::clamp((float)((Position - Start) / std::max(std::min(End - Start, 8.0), 0.5)), 0.0f, 1.0f);
	}

	// ---- what the small island shows, measured first
	const float CompactH = 40.0f * U;
	const float TextSize = 14.0f * U;
	bool Racing = false;
	const int Centis = RaceTimeCentis(&Racing);
	char aTime[32];
	if(Racing || Centis > 0)
		str_format(aTime, sizeof(aTime), "%02d:%02d", Centis / 6000, (Centis / 100) % 60);
	else
	{
		const time_t Clock = std::time(nullptr);
		const struct tm *pTm = localtime(&Clock);
		str_format(aTime, sizeof(aTime), "%02d:%02d", pTm ? pTm->tm_hour : 0, pTm ? pTm->tm_min : 0);
	}
	// the pill text: the title, or the line sung now; a change slides the old text out and the new in
	char aPill[256] = "";
	if(HasTrack)
	{
		const char *pText = !Music.HasTrack() ? Tr("Nothing playing") : State.m_Title.c_str();
		// the line sung now: always (the setting) or while the mouse is over the pill (the HUD editor)
		const bool Hover = m_Editing && ImGui::IsMouseHoveringRect(ImVec2(m_aIslandPillRect[0], m_aIslandPillRect[1]), ImVec2(m_aIslandPillRect[2], m_aIslandPillRect[3]));
		m_IslandPillLyric = false;
		if((g_Config.m_XcHudIslandLyrics || Hover) && pLines && !Approx && LineNow >= 0 && !(*pLines)[LineNow].m_Text.empty() && CXcLyrics::OwnFontCovers((*pLines)[LineNow].m_Text.c_str()))
		{
			pText = (*pLines)[LineNow].m_Text.c_str();
			m_IslandPillLyric = true;
		}
		XcFitText(pLabel, TextSize, pText, 260.0f * U, aPill, sizeof(aPill));
	}
	if(str_comp(aPill, m_aIslandPill) != 0)
	{
		str_copy(m_aIslandPillOld, m_aIslandPill);
		str_copy(m_aIslandPill, aPill);
		m_IslandPillSwap = 0.0f;
	}
	m_IslandPillSwap = std::min(m_IslandPillSwap + Dt / 0.45f, 1.0f);
	// the room for the text: while the song is sung it stays one width (the lines differ, the parts next to it must
	// not jump); otherwise the title, and every change of the width eases
	const float LyricRoom = 260.0f * U;
	const float TargetTextW = m_IslandPillLyric ? LyricRoom : std::min(pLabel->CalcTextSizeA(TextSize, FLT_MAX, 0.0f, m_aIslandPill).x, LyricRoom);
	if(m_IslandPillTextW <= 0.0f)
		m_IslandPillTextW = TargetTextW;
	m_IslandPillTextW += (TargetTextW - m_IslandPillTextW) * (1.0f - std::exp(-Dt * 10.0f));
	const float PillTextW = m_IslandPillTextW;

	// the parts: the time as plain text, then the statuses in their order; like a phone island only the first of the
	// watermark and the music is a whole pill, the others shrink to a circle; notifications are a small plane
	struct SPart
	{
		char m_Kind; // 't' time, 'w' watermark (logo and own text), 'm' music, 'c' connection
		float m_Width;
		bool m_Big;
	};
	SPart aParts[8];
	int NumParts = 0;
	const float TimeW = pLabel->CalcTextSizeA(TextSize, FLT_MAX, 0.0f, "00:00").x;
	aParts[NumParts++] = {'t', TimeW, false};
	const float PillH = 28.0f * U, CoverS = 20.0f * U, EqW = 16.0f * U;
	const char *pMark = g_Config.m_XcHudIslandText[0] ? g_Config.m_XcHudIslandText : "Exora";
	// the connection: the ping and how good it is (the notifications have no part of their own: they take the island)
	const int Ping = GameClient()->m_Snap.m_pLocalInfo ? GameClient()->m_Snap.m_pLocalInfo->m_Latency : 0;
	char aPing[16];
	if(Client()->State() == IClient::STATE_DEMOPLAYBACK)
		str_copy(aPing, "demo");
	else
		str_format(aPing, sizeof(aPing), "%d", Ping);
	bool BigTaken = false;
	for(const char *p = g_Config.m_XcHudIslandParts; *p && NumParts < 6; p++)
	{
		if(*p == 'w')
		{
			const bool Big = !BigTaken;
			BigTaken = true;
			aParts[NumParts++] = {'w', Big ? 10.0f * U + 14.0f * U + 6.0f * U + pLabel->CalcTextSizeA(TextSize, FLT_MAX, 0.0f, pMark).x + 12.0f * U : PillH, Big};
		}
		else if(*p == 'm' && HasTrack)
		{
			const bool Big = !BigTaken;
			BigTaken = true;
			aParts[NumParts++] = {'m', Big ? 4.0f * U + CoverS + 8.0f * U + PillTextW + 8.0f * U + EqW + 10.0f * U : PillH, Big};
		}
		else if(*p == 'c')
			aParts[NumParts++] = {'c', 16.0f * U, false};
	}
	const float PartGap = 8.0f * U, Edge = 8.0f * U;
	float ContentW = 0.0f;
	for(int i = 0; i < NumParts; i++)
		ContentW += aParts[i].m_Width + (i ? PartGap : 0.0f);
	const float CompactW = ContentW + Edge * 2.0f;

	// a notification
	const ImVec4 Mint(0.24f, 0.84f, 0.64f, 1.0f), Gold(1.0f, 0.78f, 0.25f, 1.0f);
	const ImVec4 NoteColor = m_IslandNoteKind == 2 ? Gold : (m_IslandNoteKind == 1 ? t.m_Accent : Mint);
	const float NoteTitleW = pTitle->CalcTextSizeA(TextSize, FLT_MAX, 0.0f, m_aIslandNoteTitle).x;
	const float NoteSubW = pSmall->CalcTextSizeA(13.0f * U, FLT_MAX, 0.0f, m_aIslandNoteSub).x;
	const float NoteW = 16.0f * U + 22.0f * U + 10.0f * U + NoteTitleW + 10.0f * U + NoteSubW + 18.0f * U;

	// the player
	const float PlayerW = 380.0f * U;
	const bool ShowLyrics = pLines && !Approx && g_Config.m_XcHudIslandPlayerLyrics;
	const float ControlsH = 44.0f * U;
	const float PlayerH = 16.0f * U + 56.0f * U + 14.0f * U + 12.0f * U + (ShowLyrics ? 14.0f * U + 5.0f * 22.0f * U : 0.0f) + 10.0f * U + ControlsH + 10.0f * U;

	const float TargetW = Mode == 1 ? NoteW : (Mode == 2 ? PlayerW : CompactW);
	const float TargetH = Mode == 2 ? PlayerH : CompactH;
	if(m_IslandW <= 0.0f || m_Editing)
	{
		if(m_IslandW <= 0.0f)
		{
			m_IslandW = TargetW;
			m_IslandH = TargetH;
		}
	}
	const float SpringTime = AnimationSpeed() > 0.0f ? 0.32f / AnimationSpeed() : 0.001f;
	Spring(m_IslandW, m_IslandVelW, TargetW, Dt, SpringTime);
	Spring(m_IslandH, m_IslandVelH, TargetH, Dt, SpringTime);
	const float FadeSpeed = AnimationSpeed() > 0.0f ? 7.0f * AnimationSpeed() : 1000.0f;
	for(int i = 0; i < 3; i++)
		m_aIslandLayer[i] = Approach(m_aIslandLayer[i], i == Mode ? 1.0f : 0.0f, i == Mode ? FadeSpeed * 0.8f : FadeSpeed * 1.6f, Dt);

	// placed by its middle (the small island), it grows downward
	const ImVec2 C = Center(ELEMENT_ISLAND);
	const float W = std::max(m_IslandW, 40.0f * U), H = std::max(m_IslandH, 20.0f * U);
	const float Margin = 10.0f * Graphics()->ScreenHeight() / 1080.0f;
	ImVec2 Min(C.x - W * 0.5f, C.y - CompactH * 0.5f);
	Min.x = std::clamp(Min.x, Margin, std::max(Graphics()->ScreenWidth() - W - Margin, Margin));
	Min.y = std::clamp(Min.y, Margin, std::max(Graphics()->ScreenHeight() - H - Margin, Margin));
	const ImVec2 Max(Min.x + W, Min.y + H);
	m_aaRects[ELEMENT_ISLAND][0] = Min.x;
	m_aaRects[ELEMENT_ISLAND][1] = Min.y;
	m_aaRects[ELEMENT_ISLAND][2] = Max.x;
	m_aaRects[ELEMENT_ISLAND][3] = Max.y;
	const float Rounding = std::min(H * 0.5f, 26.0f * U);

	// glass: only around a notification and the player (the small island is loose parts); it grows out of the parts
	const float Shell = Smooth(std::max(m_aIslandLayer[1], m_aIslandLayer[2]));
	if(Shell > 0.01f)
	{
		if(Alpha * Shell > 0.5f && GameClient()->m_FxRenderer.ModuleActive())
			m_vChatBlurRects.push_back({Min.x, Min.y, Max.x, Max.y, Rounding});
		// the bigger it grows, the less shows through (the player lies over other elements)
		const float Solid = std::clamp((H - CompactH) / (60.0f * U), 0.0f, 1.0f);
		const float Base = GameClient()->m_FxRenderer.ModuleActive() ? 0.74f : 0.9f;
		pDraw->AddRectFilled(Min, Max, Col(ImVec4(t.m_Panel.x, t.m_Panel.y, t.m_Panel.z, Base + (0.97f - Base) * Solid), Alpha * Shell), Rounding);
	}
	pDraw->PushClipRect(Min, Max, true);
	const float Time = Now;
	const bool Playing = Music.HasTrack() && State.m_Playing;
	const auto Bars = [&](float x, float BaseY, float a) {
		for(int i = 0; i < 4; i++)
		{
			const float Wave = Playing ? 0.5f + 0.5f * std::sin(Time * (6.0f + i * 1.7f) + i * 1.3f) * std::sin(Time * (2.3f + i * 0.9f)) : 0.0f;
			const float BarH = (3.0f + 9.0f * std::abs(Wave)) * U;
			pDraw->AddRectFilled(ImVec2(x + i * 4.5f * U, BaseY - BarH), ImVec2(x + i * 4.5f * U + 2.2f * U, BaseY), Col(t.m_TextSecondary, a), 1.0f * U);
		}
	};
	const auto Cover = [&](ImVec2 CMin, float S, float Round, float a) {
		const IGraphics::CTextureHandle Art = Music.Art();
		if(Music.HasTrack() && Art.IsValid() && !Art.IsNullTexture())
			pDraw->AddImageRounded((ImTextureID)(uintptr_t)m_Backend.TextureId(Art), CMin, ImVec2(CMin.x + S, CMin.y + S), ImVec2(0, 0), ImVec2(1, 1), Col(ImVec4(1, 1, 1, 1), a), Round);
		else
		{
			pDraw->AddRectFilled(CMin, ImVec2(CMin.x + S, CMin.y + S), Col(Mix(t.m_Accent, t.m_Panel, 0.35f), a), Round);
			DrawIcon(pDraw, pSmall, S * 0.55f, ImVec2(CMin.x + S * 0.5f, CMin.y + S * 0.5f), Col(t.m_OnAccent, a), XC_ICON_MUSIC);
		}
	};

	// ---- layer 0: the small island, loose parts over the game
	if(m_aIslandLayer[0] > 0.01f)
	{
		const float a = Alpha * Smooth(m_aIslandLayer[0]);
		const float Lift = (1.0f - Smooth(m_aIslandLayer[0])) * -6.0f * U;
		const float MidY = Min.y + CompactH * 0.5f + Lift;
		const bool Glass = GameClient()->m_FxRenderer.ModuleActive() && a > 0.5f;
		const auto Plate = [&](ImVec2 PMin, ImVec2 PMax, const ImVec4 &Tint) {
			if(Glass)
				m_vChatBlurRects.push_back({PMin.x, PMin.y, PMax.x, PMax.y, (PMax.y - PMin.y) * 0.5f});
			pDraw->AddRectFilled(PMin, PMax, Col(ImVec4(t.m_Panel.x, t.m_Panel.y, t.m_Panel.z, Glass ? 0.62f : 0.82f), a), (PMax.y - PMin.y) * 0.5f);
			pDraw->AddRectFilled(PMin, PMax, Col(Tint, a), (PMax.y - PMin.y) * 0.5f);
		};
		// centered where the island is
		float x = (Min.x + Max.x) * 0.5f - ContentW * 0.5f;
		for(int i = 0; i < NumParts; i++)
		{
			const SPart &Part = aParts[i];
			if(i)
				x += PartGap;
			const ImVec2 PMin(x, MidY - PillH * 0.5f), PMax(x + Part.m_Width, MidY + PillH * 0.5f);
			if(Part.m_Kind == 't')
			{
				// plain text: a soft shadow keeps it readable on any map
				pDraw->AddText(pLabel, TextSize, ImVec2(x + 1.0f * U, MidY - TextSize * 0.55f + 1.0f * U), Col(ImVec4(0, 0, 0, 1), 0.55f * a), aTime);
				pDraw->AddText(pLabel, TextSize, ImVec2(x, MidY - TextSize * 0.55f), Col(t.m_Text, a), aTime);
			}
			else if(Part.m_Kind == 'm' && Part.m_Big)
			{
				Plate(PMin, PMax, ImVec4(1, 1, 1, 0.05f));
				Cover(ImVec2(PMin.x + 4.0f * U, MidY - CoverS * 0.5f), CoverS, 6.0f * U, a);
				const float TX = PMin.x + 4.0f * U + CoverS + 8.0f * U;
				const float TextY = MidY - TextSize * 0.55f;
				pDraw->PushClipRect(ImVec2(TX, PMin.y), ImVec2(PMax.x - EqW - 14.0f * U, PMax.y), true);
				const float Swap = Smooth(m_IslandPillSwap);
				if(Swap < 1.0f && m_aIslandPillOld[0])
					pDraw->AddText(pLabel, TextSize, ImVec2(TX, TextY - Swap * 8.0f * U), Col(t.m_Text, a * (1.0f - Swap)), m_aIslandPillOld);
				const ImVec2 TP(TX, TextY + (1.0f - Swap) * 8.0f * U);
				if(m_IslandPillLyric)
				{
					// karaoke: the part already sung bright, the rest dim
					pDraw->AddText(pLabel, TextSize, TP, Col(t.m_Text, 0.38f * a * Swap), m_aIslandPill);
					const float SungW = pLabel->CalcTextSizeA(TextSize, FLT_MAX, 0.0f, m_aIslandPill).x * LineFrac;
					pDraw->PushClipRect(ImVec2(TX, PMin.y), ImVec2(TX + SungW, PMax.y), true);
					pDraw->AddText(pLabel, TextSize, TP, Col(t.m_Text, a * Swap), m_aIslandPill);
					pDraw->PopClipRect();
				}
				else
					pDraw->AddText(pLabel, TextSize, TP, Col(t.m_Text, a * Swap), m_aIslandPill);
				pDraw->PopClipRect();
				Bars(PMax.x - EqW - 8.0f * U, MidY + 6.0f * U, a);
				m_aIslandPillRect[0] = PMin.x;
				m_aIslandPillRect[1] = PMin.y;
				m_aIslandPillRect[2] = PMax.x;
				m_aIslandPillRect[3] = PMax.y;
			}
			else if(Part.m_Kind == 'm')
			{
				Plate(PMin, PMax, ImVec4(1, 1, 1, 0.05f));
				Bars(PMin.x + PillH * 0.5f - 7.0f * U, MidY + 6.0f * U, a);
			}
			else if(Part.m_Kind == 'w' && Part.m_Big)
			{
				Plate(PMin, PMax, ImVec4(t.m_Accent.x, t.m_Accent.y, t.m_Accent.z, 0.18f));
				DrawLogo(pDraw, ImVec2(PMin.x + 10.0f * U + 7.0f * U, MidY), 15.0f * U, t.m_Accent, a, false);
				pDraw->AddText(pLabel, TextSize, ImVec2(PMin.x + 10.0f * U + 14.0f * U + 6.0f * U, MidY - TextSize * 0.55f), Col(t.m_Text, a), pMark);
			}
			else if(Part.m_Kind == 'w')
			{
				Plate(PMin, PMax, ImVec4(t.m_Accent.x, t.m_Accent.y, t.m_Accent.z, 0.18f));
				DrawLogo(pDraw, ImVec2(PMin.x + PillH * 0.5f, MidY), 16.0f * U, t.m_Accent, a, false);
			}
			else if(Part.m_Kind == 'c')
			{
				// four white bars filled by the ping, no plate, like the signal of a phone; a soft shadow keeps them seen
				const int Bars4 = Ping < 60 ? 4 : (Ping < 120 ? 3 : (Ping < 200 ? 2 : 1));
				const float BX = x + 1.0f * U;
				for(int b2 = 0; b2 < 4; b2++)
				{
					const float BH = (4.0f + b2 * 3.0f) * U;
					// round ends: the corners are half the width of a bar
					const float BarW = 3.0f * U, Round = BarW * 0.5f;
					const ImVec2 BMin(BX + b2 * 4.4f * U, MidY + 6.0f * U - BH), BMax(BX + b2 * 4.4f * U + BarW, MidY + 6.0f * U);
					pDraw->AddRectFilled(ImVec2(BMin.x + 1.0f * U, BMin.y + 1.0f * U), ImVec2(BMax.x + 1.0f * U, BMax.y + 1.0f * U), Col(ImVec4(0, 0, 0, 1), 0.4f * a), Round);
					pDraw->AddRectFilled(BMin, BMax, Col(ImVec4(1, 1, 1, b2 < Bars4 ? 1.0f : 0.3f), a), Round);
				}
				if(m_Editing && ImGui::IsMouseHoveringRect(PMin, PMax))
					ImGui::SetTooltip("%s %s", aPing, Tr("ms"));
			}
			x += Part.m_Width;
		}
	}

	// ---- layer 1: a notification
	if(m_aIslandLayer[1] > 0.01f)
	{
		const float a = Alpha * Smooth(m_aIslandLayer[1]);
		const float Drop = (1.0f - Smooth(m_aIslandLayer[1])) * 8.0f * U;
		const float MidY = Min.y + CompactH * 0.5f + Drop;
		float x = (Min.x + Max.x) * 0.5f - NoteW * 0.5f + 16.0f * U;
		pDraw->AddCircleFilled(ImVec2(x + 11.0f * U, MidY), 11.0f * U, Col(ImVec4(NoteColor.x, NoteColor.y, NoteColor.z, 0.18f), a), 24);
		DrawIcon(pDraw, pSmall, 11.0f * U, ImVec2(x + 11.0f * U, MidY), Col(NoteColor, a), XC_ICON_CHECK);
		x += 22.0f * U + 10.0f * U;
		pDraw->AddText(pTitle, TextSize, ImVec2(x, MidY - TextSize * 0.55f), Col(t.m_Text, a), m_aIslandNoteTitle);
		x += NoteTitleW + 10.0f * U;
		pDraw->AddText(pSmall, 13.0f * U, ImVec2(x, MidY - 13.0f * U * 0.55f), Col(t.m_TextMuted, a), m_aIslandNoteSub);
	}

	// ---- layer 2: the player
	if(m_aIslandLayer[2] > 0.01f)
	{
		const float a = Alpha * Smooth(m_aIslandLayer[2]);
		const float Grow = 0.94f + 0.06f * Smooth(m_aIslandLayer[2]);
		const float L = (Min.x + Max.x) * 0.5f - PlayerW * 0.5f * Grow + 16.0f * U;
		float y = Min.y + 16.0f * U;
		const float ArtS = 56.0f * U;
		Cover(ImVec2(L, y), ArtS, 13.0f * U, a);
		char aLine[256];
		XcFitText(pTitle, 15.5f * U, Music.HasTrack() ? State.m_Title.c_str() : Tr("Nothing playing"), PlayerW - ArtS - 90.0f * U, aLine, sizeof(aLine));
		pDraw->AddText(pTitle, 15.5f * U, ImVec2(L + ArtS + 12.0f * U, y + 10.0f * U), Col(t.m_Text, a), aLine);
		XcFitText(pSmall, 13.0f * U, !Music.HasTrack() ? Tr("Play music in the browser") : (State.m_Artist.empty() ? Music.AppName() : State.m_Artist.c_str()), PlayerW - ArtS - 90.0f * U, aLine, sizeof(aLine));
		pDraw->AddText(pSmall, 13.0f * U, ImVec2(L + ArtS + 12.0f * U, y + 31.0f * U), Col(t.m_TextMuted, a), aLine);
		Bars(L + PlayerW - 32.0f * U - 18.0f * U, y + 22.0f * U, a);
		y += ArtS + 14.0f * U;
		// progress with the times
		const float Duration = (float)State.m_Duration;
		char aPos[16], aDur[16];
		const int PosS = Position >= 0.0 ? (int)Position : 0, DurS = Duration > 0.0f ? (int)Duration : 0;
		str_format(aPos, sizeof(aPos), "%d:%02d", PosS / 60, PosS % 60);
		str_format(aDur, sizeof(aDur), "%d:%02d", DurS / 60, DurS % 60);
		const float TimeSize = 11.5f * U;
		const float DurW = pSmall->CalcTextSizeA(TimeSize, FLT_MAX, 0.0f, aDur).x;
		const float BarL = L + 36.0f * U, BarR = L + PlayerW - 32.0f * U - DurW - 10.0f * U;
		pDraw->AddText(pSmall, TimeSize, ImVec2(L, y), Col(t.m_TextMuted, a), aPos);
		pDraw->AddText(pSmall, TimeSize, ImVec2(BarR + 10.0f * U, y), Col(t.m_TextMuted, a), aDur);
		const float BarY = y + TimeSize * 0.5f;
		pDraw->AddRectFilled(ImVec2(BarL, BarY - 2.0f * U), ImVec2(BarR, BarY + 2.0f * U), Col(ImVec4(1, 1, 1, 0.1f), a), 2.0f * U);
		if(Duration > 0.0f && Position >= 0.0)
			pDraw->AddRectFilled(ImVec2(BarL, BarY - 2.0f * U), ImVec2(BarL + (BarR - BarL) * std::clamp((float)Position / Duration, 0.0f, 1.0f), BarY + 2.0f * U), Col(t.m_Text, a), 2.0f * U);
		// with a free mouse (the HUD editor) a click on the line jumps there
		if(m_Editing && a > 0.5f && State.m_CanSeek && Duration > 0.0f && ImGui::IsMouseHoveringRect(ImVec2(BarL, BarY - 8.0f * U), ImVec2(BarR, BarY + 8.0f * U)))
		{
			m_IslandOverControl = true;
			if(ImGui::IsMouseClicked(ImGuiMouseButton_Left))
				GameClient()->m_XcMusic.Seek(std::clamp((ImGui::GetIO().MousePos.x - BarL) / (BarR - BarL), 0.0f, 1.0f) * Duration);
		}
		y += 12.0f * U;
		// the lyrics: five lines, the one sung now bright in the middle, the list rolls softly
		if(ShowLyrics)
		{
			y += 14.0f * U;
			const float LineH = 22.0f * U;
			const float Target = (float)std::max(LineNow, 0);
			if(std::abs(m_IslandLyricRoll - Target) > 6.0f)
				m_IslandLyricRoll = Target;
			m_IslandLyricRoll += (Target - m_IslandLyricRoll) * (1.0f - std::exp(-Dt * 9.0f));
			const ImVec2 LMin(L, y), LMax(L + PlayerW - 32.0f * U, y + LineH * 5.0f);
			pDraw->PushClipRect(LMin, LMax, true);
			for(int i = std::max(LineNow - 3, 0); i < std::min(LineNow + 4, (int)pLines->size()); i++)
			{
				const float Offset = i - m_IslandLyricRoll; // 0 = the middle row
				const float Ly = y + (2.0f + Offset) * LineH;
				const bool IsNow = i == LineNow;
				const float Fade = std::clamp(1.0f - std::abs(Offset) * 0.32f, 0.0f, 1.0f);
				ImFont *pFont = IsNow ? pTitle : pLabel;
				const float Size = IsNow ? 15.0f * U : 13.0f * U;
				XcFitText(pFont, Size, (*pLines)[i].m_Text.c_str(), LMax.x - LMin.x, aLine, sizeof(aLine));
				const ImVec2 TP(L, Ly + (LineH - Size) * 0.5f);
				if(IsNow)
				{
					// karaoke: the part already sung bright
					pDraw->AddText(pFont, Size, TP, Col(t.m_Text, 0.4f * a), aLine);
					const float SungW = pFont->CalcTextSizeA(Size, FLT_MAX, 0.0f, aLine).x * LineFrac;
					pDraw->PushClipRect(ImVec2(L, Ly), ImVec2(L + SungW, Ly + LineH), true);
					pDraw->AddText(pFont, Size, TP, Col(t.m_Text, a), aLine);
					pDraw->PopClipRect();
				}
				else
					pDraw->AddText(pFont, Size, TP, Col(t.m_TextMuted, a * 0.55f * Fade), aLine);
			}
			pDraw->PopClipRect();
			y += 5.0f * 22.0f * U;
		}
		// the buttons: the line of the song in the small island, back, play / pause, forward, close
		{
			y += 10.0f * U;
			const float MidY = y + ControlsH * 0.5f;
			const float Right = L + PlayerW - 32.0f * U;
			struct SButton
			{
				const char *m_pIcon;
				float m_X;
				float m_Size;
				int m_Action;
			};
			const float Mid = (L + Right) * 0.5f;
			const SButton aButtons[] = {
				{XC_ICON_COMMENTS, L + 18.0f * U, 13.0f * U, 0},
				{XC_ICON_SKIP_BACK, Mid - 62.0f * U, 17.0f * U, 1},
				{Playing ? XC_ICON_PAUSE : XC_ICON_PLAY, Mid, 24.0f * U, 2},
				{XC_ICON_SKIP_FORWARD, Mid + 62.0f * U, 17.0f * U, 3},
				{"", Right - 18.0f * U, 13.0f * U, 4},
			};
			for(const SButton &Button : aButtons)
			{
				const float R = 20.0f * U;
				const bool Hover = m_Editing && a > 0.5f && ImGui::IsMouseHoveringRect(ImVec2(Button.m_X - R, MidY - R), ImVec2(Button.m_X + R, MidY + R));
				if(Hover)
				{
					m_IslandOverControl = true;
					pDraw->AddCircleFilled(ImVec2(Button.m_X, MidY), R, Col(ImVec4(1, 1, 1, 0.08f), a), 24);
				}
				const bool On = Button.m_Action != 0 || g_Config.m_XcHudIslandPlayerLyrics;
				if(Button.m_Action == 4)
				{
					// the app the music comes from: a round badge with its first letter
					const char *pApp = Music.AppName();
					char aLetter[8] = "?";
					if(pApp && pApp[0])
					{
						const int Len = str_utf8_forward(pApp, 0);
						str_copy(aLetter, pApp, std::min((int)sizeof(aLetter), Len + 1));
					}
					pDraw->AddCircleFilled(ImVec2(Button.m_X, MidY), 11.0f * U, Col(t.m_Accent, a), 24);
					const ImVec2 LD = pTitle->CalcTextSizeA(12.0f * U, FLT_MAX, 0.0f, aLetter);
					pDraw->AddText(pTitle, 12.0f * U, ImVec2(Button.m_X - LD.x * 0.5f, MidY - LD.y * 0.5f), Col(t.m_OnAccent, a), aLetter);
					if(Hover && pApp && pApp[0])
						ImGui::SetTooltip("%s", pApp);
					continue;
				}
				DrawIcon(pDraw, pTitle, Button.m_Size, ImVec2(Button.m_X, MidY), Col(Button.m_Action == 0 ? (On ? t.m_TextSecondary : t.m_TextMuted) : t.m_Text, a), Button.m_pIcon);
				if(!Hover || !ImGui::IsMouseClicked(ImGuiMouseButton_Left))
					continue;
				PlayUiSound(SOUND_CLICK);
				CXcMusic &MusicCtl = GameClient()->m_XcMusic;
				switch(Button.m_Action)
				{
				case 0: g_Config.m_XcHudIslandPlayerLyrics ^= 1; break;
				case 1: MusicCtl.Previous(); break;
				case 2: MusicCtl.PlayPause(); break;
				case 3: MusicCtl.Next(); break;
				default:
					m_IslandPlayerPinned = false;
					m_IslandPlayerUntil = -1.0f;
					break;
				}
			}
		}
	}
	pDraw->PopClipRect();
}

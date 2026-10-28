// The demo player bar of the new menu: a capsule at the bottom over the game (no blur), a seek bar with the
// markers and the cut, the buttons of the stock player in our style. Esc shows and hides it, it fades out when
// the mouse rests. The keyboard shortcuts stay those of the stock player (CMenus::RenderDemoPlayer runs them
// while its own menu is closed).
#include "feedback.h"
#include "icons.h"
#include "lang.h"
#include "menu.h"
#include "theme.h"
#include "widgets.h"

#include <base/str.h>

#include <engine/client.h>
#include <engine/demo.h>
#include <engine/external/imgui/imgui.h>
#include <engine/external/imgui/imgui_internal.h>
#include <engine/shared/config.h>
#include <engine/storage.h>

#include <game/client/components/menus.h>
#include <game/client/gameclient.h>

#include <algorithm>

using namespace XcUi;

namespace
{
	// seconds of the buttons and the arrow keys of the stock player
	constexpr float SKIP_SECONDS = 5.0f;
	// the bar fades out after the mouse rested this long (not while it is over the bar or the cut window is open)
	constexpr float HIDE_AFTER = 2.5f;

	void FormatTime(char *pBuf, int Size, int Ticks, int TickSpeed)
	{
		const int Seconds = std::max(Ticks, 0) / std::max(TickSpeed, 1);
		if(Seconds >= 3600)
			str_format(pBuf, Size, "%d:%02d:%02d", Seconds / 3600, (Seconds / 60) % 60, Seconds % 60);
		else
			str_format(pBuf, Size, "%d:%02d", Seconds / 60, Seconds % 60);
	}
}

bool CXcMenu::DemoBarInput(const IInput::CEvent &Event)
{
	const bool Press = Event.m_Flags & IInput::FLAG_PRESS;
	if(Press && Event.m_Key == KEY_ESCAPE)
	{
		if(m_DemoSliceOpen)
			m_DemoSliceOpen = false;
		else
			m_DemoBar = !m_DemoBar;
		m_DemoIdle = 0.0f;
		return true;
	}
	if(!m_DemoBar)
		return false;
	// the name of the cut is typed into our field
	if(ImGui::GetIO().WantTextInput)
	{
		m_Backend.OnInput(Event);
		return true;
	}
	if(Event.m_Key >= KEY_MOUSE_1 && Event.m_Key <= KEY_MOUSE_WHEEL_RIGHT)
	{
		m_DemoIdle = 0.0f;
		m_Backend.OnInput(Event);
		return true;
	}
	// other keys: the shortcuts of the stock player
	return false;
}

void CXcMenu::DemoSeek(float Ratio, float Seconds)
{
	// like CMenus::HandleDemoSeeking: what was shown before the jump goes away
	CMenus &Menus = GameClient()->m_Menus;
	Menus.HandleDemoSeeking(Ratio, Seconds);
}

void CXcMenu::RenderDemoOverlay(float Dt)
{
	if(m_DemoBar && !m_DemoSliceOpen)
		m_DemoIdle += Dt;
	const float Speed = AnimationSpeed() > 0.0f ? 7.0f * AnimationSpeed() : 1000.0f;
	const bool Show = m_DemoBar && (m_DemoIdle < HIDE_AFTER || m_DemoSliceOpen);
	m_DemoBarAnim = Show ? std::min(m_DemoBarAnim + Dt * Speed, 1.0f) : std::max(m_DemoBarAnim - Dt * Speed * 0.5f, 0.0f);
	if(m_DemoBarAnim <= 0.0f)
		return;

	UpdateStyle();
	m_Backend.NewFrame(Dt, Ui()->UpdatedMousePos());
	const float Alpha = m_DemoBarAnim * m_DemoBarAnim * (3.0f - 2.0f * m_DemoBarAnim);
	RenderDemoBar(Alpha);
	m_Backend.Render();

	// the mouse is only shown with the bar
	const float Width = (float)Graphics()->ScreenWidth();
	const float Height = (float)Graphics()->ScreenHeight();
	const vec2 Mouse = Ui()->UpdatedMousePos() * vec2(Width / std::max((float)Graphics()->WindowWidth(), 1.0f), Height / std::max((float)Graphics()->WindowHeight(), 1.0f));
	Graphics()->MapScreen(CScreenRect(0.0f, 0.0f, Width, Height));
	Graphics()->SetColor(1.0f, 1.0f, 1.0f, Alpha);
	RenderTools()->RenderCursor(Mouse, S(24.0f));
	Graphics()->SetColor(1.0f, 1.0f, 1.0f, 1.0f);
}

void CXcMenu::RenderDemoBar(float Alpha)
{
	const IDemoPlayer::CInfo *pInfo = DemoPlayer()->BaseInfo();
	const int TickSpeed = Client()->GameTickSpeed();
	const int TotalTicks = std::max(pInfo->m_LastTick - pInfo->m_FirstTick, 1);
	const int CurrentTick = pInfo->m_CurrentTick - pInfo->m_FirstTick;
	const STheme &t = Theme();
	const ImGuiIO &Io = ImGui::GetIO();
	ImGui::GetStyle().Alpha = Alpha;

	const float Pad = S(14.0f);
	const float BarWidth = std::min(S(980.0f), Io.DisplaySize.x - S(32.0f));
	const float BarHeight = S(104.0f);
	const ImVec2 BarPos((Io.DisplaySize.x - BarWidth) * 0.5f, Io.DisplaySize.y - BarHeight - S(22.0f));
	ImGui::SetNextWindowPos(BarPos);
	ImGui::SetNextWindowSize(ImVec2(BarWidth, BarHeight));
	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(Pad, Pad));
	ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(S(6.0f), S(8.0f)));
	ImGui::Begin("##demo_bar", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoScrollWithMouse);
	ImDrawList *pDraw = ImGui::GetWindowDrawList();
	// glass capsule: the panel color, a thin outline and a soft shadow
	pDraw->AddRectFilled(ImVec2(BarPos.x, BarPos.y + S(4.0f)), ImVec2(BarPos.x + BarWidth, BarPos.y + BarHeight + S(6.0f)), Col(t.m_Shadow, 0.5f * Alpha), S(18.0f));
	// solid, so the HUD under it does not show through
	const ImVec4 Solid(t.m_Panel.x, t.m_Panel.y, t.m_Panel.z, 0.97f);
	pDraw->AddRectFilled(BarPos, ImVec2(BarPos.x + BarWidth, BarPos.y + BarHeight), Col(Solid, Alpha), S(18.0f));
	pDraw->AddRect(BarPos, ImVec2(BarPos.x + BarWidth, BarPos.y + BarHeight), Col(t.m_Border, Alpha), S(18.0f));
	if(ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows | ImGuiHoveredFlags_AllowWhenBlockedByActiveItem))
		m_DemoIdle = 0.0f;

	// the seek bar: the cut, the markers, the time under the mouse
	{
		const float Width = ImGui::GetContentRegionAvail().x;
		const float Height = S(18.0f);
		const ImVec2 Pos = ImGui::GetCursorScreenPos();
		const ImRect Rect(Pos, ImVec2(Pos.x + Width, Pos.y + Height));
		const ImGuiID Id = ImGui::GetID("##seek");
		ImGui::ItemSize(Rect);
		ImGui::ItemAdd(Rect, Id);
		bool Hovered, Held;
		ImGui::ButtonBehavior(Rect, Id, &Hovered, &Held, ImGuiButtonFlags_PressedOnClick);
		const float Grow = Animate(Id, (Hovered || Held) ? 1.0f : 0.0f, 16.0f);
		const float TrackH = S(5.0f) + S(4.0f) * Grow;
		const float Mid = Rect.GetCenter().y;
		const ImVec2 TrackMin(Rect.Min.x, Mid - TrackH * 0.5f), TrackMax(Rect.Max.x, Mid + TrackH * 0.5f);
		const auto TickX = [&](int Tick) { return Rect.Min.x + Width * std::clamp((float)(Tick - pInfo->m_FirstTick) / TotalTicks, 0.0f, 1.0f); };
		pDraw->AddRectFilled(TrackMin, TrackMax, Col(t.m_Track, 1.6f), TrackH * 0.5f);
		// the cut
		if(g_Config.m_ClDemoSliceBegin != -1 || g_Config.m_ClDemoSliceEnd != -1)
		{
			const float x0 = g_Config.m_ClDemoSliceBegin != -1 ? TickX(g_Config.m_ClDemoSliceBegin) : Rect.Min.x;
			const float x1 = g_Config.m_ClDemoSliceEnd != -1 ? TickX(g_Config.m_ClDemoSliceEnd) : Rect.Max.x;
			pDraw->AddRectFilled(ImVec2(x0, TrackMin.y - S(3.0f)), ImVec2(x1, TrackMax.y + S(3.0f)), Col(ImVec4(1.0f, 0.62f, 0.25f, 0.28f)), S(3.0f));
			for(float x : {x0, x1})
				pDraw->AddRectFilled(ImVec2(x - S(1.0f), TrackMin.y - S(5.0f)), ImVec2(x + S(1.0f), TrackMax.y + S(5.0f)), Col(ImVec4(1.0f, 0.62f, 0.25f, 1.0f)));
		}
		// played part
		const float Shown = m_DemoSeekRatio >= 0.0f ? m_DemoSeekRatio : std::clamp((float)CurrentTick / TotalTicks, 0.0f, 1.0f);
		const float PlayedX = Rect.Min.x + Width * Shown;
		pDraw->AddRectFilled(TrackMin, ImVec2(std::max(PlayedX, TrackMin.x + TrackH), TrackMax.y), Col(t.m_Accent), TrackH * 0.5f);
		// markers
		for(int i = 0; i < pInfo->m_NumTimelineMarkers; i++)
		{
			const float x = TickX(pInfo->m_aTimelineMarkers[i]);
			pDraw->AddCircleFilled(ImVec2(x, Mid), S(3.0f) + S(1.0f) * Grow, Col(t.m_Text, 0.9f));
		}
		// the knob
		pDraw->AddCircleFilled(ImVec2(PlayedX, Mid), S(6.0f) + S(2.0f) * Grow, Col(ImVec4(1, 1, 1, 1)));
		pDraw->AddCircle(ImVec2(PlayedX, Mid), S(6.0f) + S(2.0f) * Grow, Col(t.m_Accent), 0, S(2.0f));
		const float Ratio = std::clamp((Io.MousePos.x - Rect.Min.x) / Width, 0.0f, 1.0f);
		if(Held)
		{
			// seek while dragging; the demo pauses meanwhile like in the stock player
			if(m_DemoSeekRatio < 0.0f || std::abs(Ratio - m_DemoSeekRatio) * Width > 1.0f)
			{
				m_DemoSeekRatio = Ratio;
				DemoSeek(Ratio, 0.0f);
			}
		}
		else
			m_DemoSeekRatio = -1.0f;
		if(Hovered || Held)
		{
			char aTime[32];
			FormatTime(aTime, sizeof(aTime), (int)(Ratio * TotalTicks), TickSpeed);
			ImFont *pFont = Fonts().m_pSmall ? Fonts().m_pSmall : ImGui::GetFont();
			const ImVec2 Size = pFont->CalcTextSizeA(pFont->FontSize, FLT_MAX, 0.0f, aTime);
			const float x = std::clamp(Io.MousePos.x, Rect.Min.x + Size.x * 0.5f + S(6.0f), Rect.Max.x - Size.x * 0.5f - S(6.0f));
			const ImVec2 Min(x - Size.x * 0.5f - S(6.0f), Rect.Min.y - Size.y - S(16.0f));
			ImDrawList *pFore = ImGui::GetForegroundDrawList();
			pFore->AddRectFilled(Min, ImVec2(Min.x + Size.x + S(12.0f), Min.y + Size.y + S(6.0f)), Col(t.m_Card, Alpha), S(6.0f));
			pFore->AddText(pFont, pFont->FontSize, ImVec2(Min.x + S(6.0f), Min.y + S(3.0f)), Col(t.m_Text, Alpha), aTime);
		}
	}

	// the buttons
	const auto Separator = [&]() {
		ImGui::SameLine(0.0f, S(10.0f));
		const ImVec2 p = ImGui::GetCursorScreenPos();
		pDraw->AddRectFilled(ImVec2(p.x, p.y + S(8.0f)), ImVec2(p.x + S(1.0f), p.y + S(26.0f)), Col(t.m_Border, 1.5f));
		ImGui::Dummy(ImVec2(S(1.0f), S(34.0f)));
		ImGui::SameLine(0.0f, S(10.0f));
	};
	if(IconButton(pInfo->m_Paused ? XC_ICON_PLAY : XC_ICON_PAUSE, pInfo->m_Paused ? Tr("Play (Space)") : Tr("Pause (Space)"), !pInfo->m_Paused))
	{
		if(pInfo->m_Paused)
		{
			// at the end it starts from the beginning
			if(CurrentTick >= TotalTicks)
				DemoSeek(0.0f, 0.0f);
			DemoPlayer()->Unpause();
		}
		else
			DemoPlayer()->Pause();
	}
	ImGui::SameLine();
	if(IconButton(XC_ICON_STOP, Tr("Stop: back to the start")))
	{
		DemoPlayer()->Pause();
		DemoSeek(0.0f, 0.0f);
	}
	Separator();
	if(IconButton(XC_ICON_REWIND, Tr("5 seconds back (Left)")))
		DemoSeek(-1.0f, -SKIP_SECONDS);
	ImGui::SameLine();
	if(IconButton(XC_ICON_FAST_FORWARD, Tr("5 seconds forward (Right)")))
		DemoSeek(-1.0f, SKIP_SECONDS);
	ImGui::SameLine();
	if(IconButton(XC_ICON_BACKWARD, Tr("One tick back (,)")))
		GameClient()->m_Menus.DemoSeekTick(IDemoPlayer::TICK_PREVIOUS);
	ImGui::SameLine();
	if(IconButton(XC_ICON_FORWARD, Tr("One tick forward (.)")))
		GameClient()->m_Menus.DemoSeekTick(IDemoPlayer::TICK_NEXT);
	if(pInfo->m_NumTimelineMarkers > 0)
	{
		ImGui::SameLine();
		if(IconButton(XC_ICON_SKIP_BACK, Tr("Previous marker")))
		{
			float Target = 0.0f;
			for(int i = pInfo->m_NumTimelineMarkers - 1; i >= 0; i--)
				if(pInfo->m_aTimelineMarkers[i] - pInfo->m_FirstTick < CurrentTick - 10)
				{
					Target = (float)(pInfo->m_aTimelineMarkers[i] - pInfo->m_FirstTick) / TotalTicks;
					break;
				}
			DemoSeek(Target, 0.0f);
		}
		ImGui::SameLine();
		if(IconButton(XC_ICON_SKIP_FORWARD, Tr("Next marker")))
		{
			for(int i = 0; i < pInfo->m_NumTimelineMarkers; i++)
				if(pInfo->m_aTimelineMarkers[i] - pInfo->m_FirstTick > CurrentTick + 10)
				{
					DemoSeek((float)(pInfo->m_aTimelineMarkers[i] - pInfo->m_FirstTick) / TotalTicks, 0.0f);
					break;
				}
		}
	}
	Separator();
	// the speed: minus, the value (click: normal speed), plus
	if(IconButton(XC_ICON_MINUS, Tr("Slower (Down)")))
		DemoPlayer()->AdjustSpeedIndex(-1);
	ImGui::SameLine();
	{
		char aSpeed[16];
		str_format(aSpeed, sizeof(aSpeed), "%gx", pInfo->m_Speed);
		ImGui::PushID("##speed");
		if(Button(aSpeed, pInfo->m_Speed != 1.0f, S(64.0f)))
			DemoPlayer()->SetSpeedIndex(DEMO_SPEED_INDEX_DEFAULT);
		if(ImGui::IsItemHovered())
			ImGui::SetTooltip("%s", Tr("Speed, click: normal"));
		ImGui::PopID();
	}
	ImGui::SameLine();
	if(IconButton(XC_ICON_PLUS, Tr("Faster (Up)")))
		DemoPlayer()->AdjustSpeedIndex(+1);

	// the time in the middle of what is left
	ImGui::SameLine(0.0f, S(14.0f));
	{
		char aCurrent[32], aTotal[32], aTime[80];
		FormatTime(aCurrent, sizeof(aCurrent), CurrentTick, TickSpeed);
		FormatTime(aTotal, sizeof(aTotal), TotalTicks, TickSpeed);
		str_format(aTime, sizeof(aTime), "%s / %s", aCurrent, aTotal);
		ImFont *pFont = Fonts().m_pLabel ? Fonts().m_pLabel : ImGui::GetFont();
		const ImVec2 p = ImGui::GetCursorScreenPos();
		const ImVec2 Size = pFont->CalcTextSizeA(pFont->FontSize, FLT_MAX, 0.0f, aTime);
		pDraw->AddText(pFont, pFont->FontSize, ImVec2(p.x, p.y + (S(34.0f) - Size.y) * 0.5f), Col(t.m_Text), aTime);
		ImGui::Dummy(ImVec2(Size.x, S(34.0f)));
	}

	// on the right: the cut, the camera, the menu, close
	const float RightWidth = S(34.0f) * 6 + S(6.0f) * 5 + S(21.0f) + (GameClient()->m_Camera.CanUseAutoSpecCamera() ? S(40.0f) : 0.0f);
	ImGui::SameLine(std::max(ImGui::GetCursorPosX(), ImGui::GetWindowWidth() - Pad - RightWidth));
	if(IconButton(XC_ICON_BRACKET_LEFT, Tr("Cut starts here (right click: reset)"), g_Config.m_ClDemoSliceBegin != -1))
	{
		Client()->DemoSliceBegin();
		if(g_Config.m_ClDemoSliceEnd != -1 && CurrentTick > g_Config.m_ClDemoSliceEnd - pInfo->m_FirstTick)
			g_Config.m_ClDemoSliceEnd = -1;
	}
	if(ImGui::IsItemClicked(ImGuiMouseButton_Right))
		g_Config.m_ClDemoSliceBegin = -1;
	ImGui::SameLine();
	if(IconButton(XC_ICON_BRACKET_RIGHT, Tr("Cut ends here (right click: reset)"), g_Config.m_ClDemoSliceEnd != -1))
	{
		Client()->DemoSliceEnd();
		if(g_Config.m_ClDemoSliceBegin != -1 && CurrentTick < g_Config.m_ClDemoSliceBegin - pInfo->m_FirstTick)
			g_Config.m_ClDemoSliceBegin = -1;
	}
	if(ImGui::IsItemClicked(ImGuiMouseButton_Right))
		g_Config.m_ClDemoSliceEnd = -1;
	ImGui::SameLine();
	if(IconButton(XC_ICON_SCISSORS, Tr("Save the cut as a demo"), m_DemoSliceOpen))
	{
		m_DemoSliceOpen = !m_DemoSliceOpen;
		if(m_DemoSliceOpen)
		{
			char aName[128];
			DemoPlayer()->GetDemoName(aName, sizeof(aName));
			str_format(m_aDemoSliceName, sizeof(m_aDemoSliceName), "%s_cut", aName);
		}
	}
	Separator();
	if(GameClient()->m_Camera.CanUseAutoSpecCamera())
	{
		if(IconButton(XC_ICON_CAMERA, Tr("Automatic camera"), GameClient()->m_Camera.m_AutoSpecCamera))
			GameClient()->m_Camera.m_AutoSpecCamera = !GameClient()->m_Camera.m_AutoSpecCamera;
		ImGui::SameLine();
	}
	if(IconButton(XC_ICON_KEYBOARD, Tr("Keyboard shortcuts: Space, arrows, comma and period"), g_Config.m_ClDemoKeyboardShortcuts != 0))
		g_Config.m_ClDemoKeyboardShortcuts ^= 1;
	ImGui::SameLine();
	if(IconButton(XC_ICON_GEAR, Tr("Menu")))
		SetActive(true);
	ImGui::SameLine();
	if(IconButton(XC_ICON_XMARK, Tr("Close the demo")))
		Client()->Disconnect();

	ImGui::End();
	ImGui::PopStyleVar(2);

	// the name of the demo above the bar
	{
		char aName[128];
		DemoPlayer()->GetDemoName(aName, sizeof(aName));
		ImFont *pFont = Fonts().m_pSmall ? Fonts().m_pSmall : ImGui::GetFont();
		ImDrawList *pBack = ImGui::GetBackgroundDrawList();
		const ImVec2 Size = pFont->CalcTextSizeA(pFont->FontSize, FLT_MAX, 0.0f, aName);
		const ImVec2 Min(BarPos.x + S(14.0f), BarPos.y - Size.y - S(14.0f));
		pBack->AddRectFilled(Min, ImVec2(Min.x + Size.x + S(28.0f), Min.y + Size.y + S(8.0f)), Col(t.m_Panel, 0.85f * Alpha), S(8.0f));
		pBack->AddText(Fonts().m_pIcons ? Fonts().m_pIcons : pFont, pFont->FontSize, ImVec2(Min.x + S(7.0f), Min.y + S(4.0f)), Col(t.m_Accent, Alpha), XC_ICON_FILM);
		pBack->AddText(pFont, pFont->FontSize, ImVec2(Min.x + S(22.0f), Min.y + S(4.0f)), Col(t.m_TextSecondary, Alpha), aName);
	}

	// saving the cut: a small window above the bar
	if(m_DemoSliceOpen)
	{
		const float Width = S(420.0f);
		ImGui::SetNextWindowPos(ImVec2(BarPos.x + BarWidth - Width, BarPos.y - S(188.0f)));
		ImGui::SetNextWindowSize(ImVec2(Width, S(176.0f)));
		ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(S(16.0f), S(14.0f)));
		ImGui::Begin("##demo_cut", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBackground);
		ImDrawList *pCut = ImGui::GetWindowDrawList();
		const ImVec2 Min = ImGui::GetWindowPos(), Max(Min.x + Width, Min.y + S(176.0f));
		pCut->AddRectFilled(Min, Max, Col(ImVec4(t.m_Panel.x, t.m_Panel.y, t.m_Panel.z, 0.97f), Alpha), S(14.0f));
		pCut->AddRect(Min, Max, Col(t.m_Border, Alpha), S(14.0f));
		const int Begin = g_Config.m_ClDemoSliceBegin == -1 ? 0 : g_Config.m_ClDemoSliceBegin - pInfo->m_FirstTick;
		const int End = g_Config.m_ClDemoSliceEnd == -1 ? TotalTicks : g_Config.m_ClDemoSliceEnd - pInfo->m_FirstTick;
		char aBegin[32], aEnd[32], aLength[32], aLine[128];
		FormatTime(aBegin, sizeof(aBegin), Begin, TickSpeed);
		FormatTime(aEnd, sizeof(aEnd), End, TickSpeed);
		FormatTime(aLength, sizeof(aLength), End - Begin, TickSpeed);
		str_format(aLine, sizeof(aLine), "%s: %s – %s (%s)", Tr("Cut"), aBegin, aEnd, aLength);
		ImGui::PushFont(Fonts().m_pLabel ? Fonts().m_pLabel : ImGui::GetFont());
		ImGui::TextUnformatted(aLine);
		ImGui::PopFont();
		InputText("##cut_name", m_aDemoSliceName, sizeof(m_aDemoSliceName), Tr("Name of the new demo"));
		ImGui::AlignTextToFramePadding();
		ImGui::TextUnformatted(Tr("Without the chat"));
		ImGui::SameLine(Width - S(16.0f) - S(52.0f));
		Toggle("##cut_no_chat", &m_DemoSliceRemoveChat);
		if(Button(Tr("Cancel"), false, S(120.0f)))
			m_DemoSliceOpen = false;
		ImGui::SameLine();
		if(Button(Tr("Save"), true, S(120.0f)))
		{
			char aName[128], aDemo[128];
			str_copy(aName, m_aDemoSliceName);
			if(str_endswith(aName, ".demo"))
				aName[str_length(aName) - 5] = '\0';
			DemoPlayer()->GetDemoName(aDemo, sizeof(aDemo));
			if(!aName[0] || !str_valid_filename(aName) || str_comp(aName, aDemo) == 0)
				Toast(Tr("This name cannot be used, choose another one"), TOAST_ERROR);
			else
			{
				CMenus &Menus = GameClient()->m_Menus;
				char aPath[IO_MAX_PATH_LENGTH];
				str_format(aPath, sizeof(aPath), "%s/%s.demo", Menus.m_aCurrentDemoFolder[0] ? Menus.m_aCurrentDemoFolder : "demos", aName);
				Client()->DemoSlice(aPath, CMenus::DemoFilterChat, &m_DemoSliceRemoveChat);
				char aMsg[256];
				str_format(aMsg, sizeof(aMsg), "%s: %s.demo", Tr("Cut saved"), aName);
				Toast(aMsg);
				m_DemoSliceOpen = false;
			}
		}
		ImGui::End();
		ImGui::PopStyleVar();
	}
	// the HUD steps back under the bar, so its messages (the cut saved) are shown here
	if(HasToasts())
		RenderToasts(ImGui::GetForegroundDrawList(), ImVec2(Io.DisplaySize.x, BarPos.y - S(40.0f)), Client()->GlobalTime(), 1.0f);
	ImGui::GetStyle().Alpha = 1.0f;
}

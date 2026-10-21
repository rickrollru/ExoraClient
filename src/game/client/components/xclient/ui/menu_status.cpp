// The connecting, map downloading and loading screens in the style of the menu, and the
// "disconnected" and "password" windows over the menu. The texts and the logic stay in CMenus.
#include "feedback.h"
#include "icons.h"
#include "lang.h"
#include "menu.h"
#include "theme.h"
#include "widgets.h"

#include <base/str.h>
#include <base/time.h>

#include <engine/client.h>
#include <engine/external/imgui/imgui.h>
#include <engine/external/imgui/imgui_internal.h>
#include <engine/keys.h>
#include <engine/shared/config.h>

#include <game/client/components/menus.h>
#include <game/client/gameclient.h>

#include <algorithm>
#include <cmath>

using namespace XcUi;

int CXcMenu::RenderStatusScreen(const char *pTitle, const char *pText, const char *pExtra, float Progress, const char *pButton)
{
	if(!g_Config.m_XcNewMenu || !m_Backend.IsReady())
		return STATUS_NOT_DRAWN;
	m_Backend.MakeCurrent();
	// called from inside an ImGui frame (something loads while the menu draws): the classic screen
	if(ImGui::GetCurrentContext()->WithinFrameScope)
		return STATUS_NOT_DRAWN;

	// the loading screens are drawn outside the normal frames, so the time is our own
	const float Now = (float)(time_get() / (double)time_freq());
	const float Dt = std::clamp(Now - m_StatusTime, 0.0f, 0.1f);
	m_StatusTime = Now;

	UpdateStyle();
	const STheme &t = Theme();
	const float Width = (float)Graphics()->ScreenWidth();
	const float Height = (float)Graphics()->ScreenHeight();
	const int Background = EffectiveBackground();
	if(Background == BG_CUSTOM)
		m_Backgrounds.Render(Width, Height, 1.0f);

	m_Backend.NewFrame(Dt, Ui()->UpdatedMousePos());
	// the menu itself may be faded out right now
	const float OldAlpha = ImGui::GetStyle().Alpha;
	ImGui::GetStyle().Alpha = 1.0f;
	const ImGuiIO &Io = ImGui::GetIO();
	ImDrawList *pDraw = ImGui::GetBackgroundDrawList();
	if(Background == BG_CUSTOM)
		pDraw->AddRectFilled(ImVec2(0, 0), Io.DisplaySize, Col(ImVec4(0, 0, 0, 1), g_Config.m_XcMenuBgDim / 100.0f));
	else if(Background == BG_MAP)
		pDraw->AddRectFilled(ImVec2(0, 0), Io.DisplaySize, Col(t.m_Background, 0.35f)); // the menu map stays behind
	else
		RenderAnimatedBackground(pDraw, 1.0f); // no game to blur while connecting

	ImFont *pHeader = Fonts().m_pHeader;
	ImFont *pBody = Fonts().m_pBody;
	ImFont *pSmall = Fonts().m_pSmall;
	ImFont *pLabel = Fonts().m_pLabel;
	const float Pad = S(28.0f);
	const float CardWidth = std::min(S(540.0f), Io.DisplaySize.x - S(40.0f));
	const float Inner = CardWidth - Pad * 2.0f;

	// measure first: the card grows with the text
	const float LogoRow = S(30.0f);
	const ImVec2 TitleSize = pHeader->CalcTextSizeA(pHeader->FontSize, FLT_MAX, Inner, pTitle);
	const bool HasText = pText && pText[0];
	const bool HasExtra = pExtra && pExtra[0];
	const ImVec2 ExtraSize = HasExtra ? pSmall->CalcTextSizeA(pSmall->FontSize, FLT_MAX, Inner, pExtra) : ImVec2(0, 0);
	const bool HasBar = Progress > -1.5f;
	const float ButtonHeight = S(40.0f);
	float ContentHeight = LogoRow + S(22.0f) + TitleSize.y;
	if(HasText)
		ContentHeight += S(8.0f) + pBody->FontSize;
	if(HasExtra)
		ContentHeight += S(10.0f) + ExtraSize.y;
	if(HasBar)
		ContentHeight += S(22.0f) + S(6.0f);
	if(pButton)
		ContentHeight += S(24.0f) + ButtonHeight;
	const ImVec2 CardMin((Io.DisplaySize.x - CardWidth) * 0.5f, (Io.DisplaySize.y - ContentHeight - Pad * 2.0f) * 0.5f);
	const ImVec2 CardMax(CardMin.x + CardWidth, CardMin.y + ContentHeight + Pad * 2.0f);
	pDraw = ImGui::GetForegroundDrawList();
	PanelRect(pDraw, CardMin, CardMax, S(18.0f), t.m_Panel);

	float y = CardMin.y + Pad;
	const float x = CardMin.x + Pad;
	// the logo and the name
	DrawLogo(pDraw, ImVec2(x + LogoRow * 0.5f, y + LogoRow * 0.5f), LogoRow, t.m_Accent);
	pDraw->AddText(pLabel, pLabel->FontSize, ImVec2(x + LogoRow + S(10.0f), y + (LogoRow - pLabel->FontSize) * 0.5f), Col(t.m_TextSecondary), "Exora Client");
	y += LogoRow + S(22.0f);

	pDraw->AddText(pHeader, pHeader->FontSize, ImVec2(x, y), Col(t.m_Text), pTitle, nullptr, Inner);
	y += TitleSize.y;
	if(HasText)
	{
		y += S(8.0f);
		const ImVec4 Clip(x, y, x + Inner, y + pBody->FontSize + S(4.0f));
		pDraw->AddText(pBody, pBody->FontSize, ImVec2(x, y), Col(t.m_TextSecondary), pText, nullptr, 0.0f, &Clip);
		y += pBody->FontSize;
	}
	if(HasExtra)
	{
		y += S(10.0f);
		pDraw->AddText(pSmall, pSmall->FontSize, ImVec2(x, y), Col(t.m_TextMuted), pExtra, nullptr, Inner);
		y += ExtraSize.y;
	}
	if(HasBar)
	{
		y += S(22.0f);
		const float BarHeight = S(6.0f);
		pDraw->AddRectFilled(ImVec2(x, y), ImVec2(x + Inner, y + BarHeight), Col(t.m_Track), BarHeight);
		if(Progress >= 0.0f)
		{
			const float Fill = std::clamp(Progress, 0.0f, 1.0f);
			if(Fill > 0.0f)
				pDraw->AddRectFilled(ImVec2(x, y), ImVec2(x + std::max(Inner * Fill, BarHeight), y + BarHeight), Col(t.m_Accent), BarHeight);
		}
		else
		{
			// no known amount: a piece of the bar runs from left to right
			const float Phase = std::fmod(Now * 0.7f, 1.4f) - 0.4f;
			const float Start = std::clamp(Phase, 0.0f, 1.0f);
			const float End = std::clamp(Phase + 0.4f, 0.0f, 1.0f);
			if(End > Start)
				pDraw->AddRectFilled(ImVec2(x + Inner * Start, y), ImVec2(x + Inner * End, y + BarHeight), Col(t.m_Accent), BarHeight);
		}
		y += BarHeight;
	}
	int Result = STATUS_DRAWN;
	if(pButton)
	{
		y += S(24.0f);
		const ImVec2 ButtonMin(x, y);
		const ImVec2 ButtonMax(x + Inner, y + ButtonHeight);
		const bool Hovered = ImGui::IsMouseHoveringRect(ButtonMin, ButtonMax, false);
		const float Hover = Animate(ImGui::GetID("##status_button"), Hovered ? 1.0f : 0.0f, 18.0f);
		pDraw->AddRectFilled(ButtonMin, ButtonMax, Col(t.m_Track, 1.0f + Hover * 0.8f), S(10.0f));
		pDraw->AddRect(ButtonMin, ButtonMax, Col(t.m_Border), S(10.0f));
		const ImVec2 TextSize = pLabel->CalcTextSizeA(pLabel->FontSize, FLT_MAX, 0.0f, pButton);
		pDraw->AddText(pLabel, pLabel->FontSize, ImVec2((ButtonMin.x + ButtonMax.x - TextSize.x) * 0.5f, (ButtonMin.y + ButtonMax.y - TextSize.y) * 0.5f), Col(t.m_Text), pButton);
		// the classic menu owns the input here, the click is read from the engine
		if(Hovered && Input()->KeyPress(KEY_MOUSE_1))
		{
			PlayUiSound(SOUND_CLICK);
			Result = STATUS_BUTTON;
		}
	}

	m_Backend.Render();
	ImGui::GetStyle().Alpha = OldAlpha;
	Ui()->MapScreen();
	return Result;
}

void CXcMenu::RenderConnectionPopup()
{
	CMenus &Menus = GameClient()->m_Menus;
	const bool Disconnected = Menus.m_Popup == CMenus::POPUP_DISCONNECTED;
	const bool Password = Menus.m_Popup == CMenus::POPUP_PASSWORD;
	if(!Disconnected && !Password)
	{
		m_PasswordFocus = true;
		return;
	}
	const STheme &t = Theme();
	const ImGuiIO &Io = ImGui::GetIO();
	// a veil over the menu, the window in the middle
	ImGui::SetNextWindowPos(ImVec2(0, 0));
	ImGui::SetNextWindowSize(Io.DisplaySize);
	ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.0f, 0.0f, 0.0f, 0.45f));
	ImGui::Begin("##xc_status_veil", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings);
	ImGui::End();
	ImGui::PopStyleColor();

	const float Width = std::min(S(480.0f), Io.DisplaySize.x - S(40.0f));
	ImGui::SetNextWindowPos(ImVec2(Io.DisplaySize.x * 0.5f, Io.DisplaySize.y * 0.5f), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
	ImGui::SetNextWindowSize(ImVec2(Width, 0.0f));
	ImGui::SetNextWindowFocus();
	ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(t.m_Card.x, t.m_Card.y, t.m_Card.z, 1.0f));
	ImGui::PushStyleColor(ImGuiCol_Border, t.m_Border);
	ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, S(16.0f));
	ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 1.0f);
	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(S(24.0f), S(22.0f)));
	ImGui::Begin("##xc_status", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_AlwaysAutoResize);
	ImGui::PushFont(Fonts().m_pBody);
	const float Inner = Width - S(48.0f);
	const float Half = (Inner - S(8.0f)) * 0.5f;
	const bool Enter = ImGui::IsKeyPressed(ImGuiKey_Enter, false) || ImGui::IsKeyPressed(ImGuiKey_KeypadEnter, false);
	const bool Escape = ImGui::IsKeyPressed(ImGuiKey_Escape, false);
	bool Close = false;
	if(Disconnected)
	{
		const bool Reconnecting = Client()->ReconnectTime() > 0;
		Header(Tr("Disconnected"));
		const char *pReason = Client()->ErrorString();
		if(pReason && pReason[0])
			Description(pReason, Inner);
		if(Reconnecting)
		{
			char aBuf[128];
			str_format(aBuf, sizeof(aBuf), Tr("Reconnecting in %d s"), (int)((Client()->ReconnectTime() - time_get()) / time_freq()) + 1);
			ImGui::Dummy(ImVec2(0.0f, S(4.0f)));
			ImGui::PushStyleColor(ImGuiCol_Text, t.m_Accent);
			ImGui::TextUnformatted(aBuf);
			ImGui::PopStyleColor();
		}
		ImGui::Dummy(ImVec2(0.0f, S(10.0f)));
		if(Button(Reconnecting ? Tr("Cancel") : Tr("Ok"), !Reconnecting, -1.0f) || Enter || Escape)
		{
			if(Reconnecting)
				Client()->SetReconnectTime(0);
			Close = true;
		}
	}
	else
	{
		Header(Tr("Password"));
		Description(Tr("This server is protected with a password"), Inner);
		ImGui::Dummy(ImVec2(0.0f, S(6.0f)));
		if(m_PasswordFocus && !ImGui::IsWindowAppearing())
		{
			ImGui::SetKeyboardFocusHere();
			m_PasswordFocus = false;
		}
		InputText("##password", g_Config.m_Password, sizeof(g_Config.m_Password), Tr("Password"), Inner, ImGuiInputTextFlags_Password);
		ImGui::Dummy(ImVec2(0.0f, S(10.0f)));
		if(Button(Tr("Cancel"), false, Half) || Escape)
			Close = true;
		ImGui::SameLine(0.0f, S(8.0f));
		if(Button(Tr("Join"), true, Half) || Enter)
		{
			char aAddr[NETADDR_MAXSTRSIZE];
			net_addr_str(&Client()->ServerAddress(), aAddr, sizeof(aAddr), true);
			Close = true;
			Client()->Connect(aAddr, g_Config.m_Password);
		}
	}
	ImGui::PopFont();
	ImGui::End();
	ImGui::PopStyleVar(3);
	ImGui::PopStyleColor(2);
	if(Close)
	{
		Menus.m_Popup = CMenus::POPUP_NONE;
		m_PasswordFocus = true;
	}
}

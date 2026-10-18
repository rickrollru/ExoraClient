#include "menu.h"

#include "icons.h"
#include "lang.h"
#include "theme.h"
#include "widgets.h"

#include <base/str.h>

#include <engine/client.h>
#include <engine/external/imgui/imgui.h>
#include <engine/external/imgui/imgui_internal.h>
#include <engine/graphics.h>
#include <engine/shared/config.h>
#include <engine/storage.h>

#include <game/client/gameclient.h>

#include <cmath>

using namespace XcUi;

static const char *const XC_CLIENT_NAME = "XClient"; // working name, the final one comes later

void CXcMenu::OnConsoleInit()
{
	Console()->Register("xc_menu", "", CFGFLAG_CLIENT, ConToggleMenu, this, "Open or close the new menu");
}

void CXcMenu::ConToggleMenu(IConsole::IResult *pResult, void *pUserData)
{
	CXcMenu *pSelf = static_cast<CXcMenu *>(pUserData);
	pSelf->SetActive(!pSelf->m_Active);
}

void CXcMenu::OnInit()
{
	m_Backend.Init(Graphics(), Input(), Storage());
	m_Active = g_Config.m_XcNewMenu && !IsOnline();
	m_OpenAnimation = m_Active ? 1.0f : 0.0f;
}

void CXcMenu::OnShutdown()
{
	m_Backend.Shutdown();
}

bool CXcMenu::IsOnline() const
{
	return Client()->State() == IClient::STATE_ONLINE || Client()->State() == IClient::STATE_DEMOPLAYBACK;
}

void CXcMenu::SetActive(bool Active)
{
	if(Active == m_Active)
		return;
	m_Active = Active;
	m_BindWaiting = -1;
	if(Active)
		GameClient()->m_Menus.SetActive(false);
}

void CXcMenu::OpenClassicMenu()
{
	SetActive(false);
	GameClient()->m_Menus.SetActive(true);
}

void CXcMenu::OnStateChange(int NewState, int OldState)
{
	if(!g_Config.m_XcNewMenu)
		return;
	// joining a server shows the game, leaving it shows the menu again;
	// while connecting the classic menu shows its progress popup
	if(NewState == IClient::STATE_OFFLINE)
		SetActive(true);
	else if(NewState == IClient::STATE_CONNECTING || NewState == IClient::STATE_LOADING || NewState == IClient::STATE_ONLINE)
		SetActive(false);
	(void)OldState;
}

bool CXcMenu::OnCursorMove(float x, float y, IInput::ECursorType CursorType)
{
	if(!m_Active)
		return false;
	Ui()->ConvertMouseMove(&x, &y, CursorType);
	Ui()->OnCursorMove(x, y);
	m_Backend.OnMouseMove(Ui()->UpdatedMousePos());
	return true;
}

bool CXcMenu::OnInput(const IInput::CEvent &Event)
{
	if(!g_Config.m_XcNewMenu || !m_Backend.IsReady())
		return false;

	const bool EscapePressed = (Event.m_Flags & IInput::FLAG_PRESS) && Event.m_Key == KEY_ESCAPE;
	if(!m_Active)
	{
		// Esc opens the new menu, unless the classic one is open in game (it closes itself then)
		if(EscapePressed && (!IsOnline() || !GameClient()->m_Menus.IsActive()))
		{
			SetActive(true);
			return true;
		}
		return false;
	}

	if(m_BindWaiting >= 0 && OnBindInput(Event))
		return true;

	if(EscapePressed)
	{
		// Esc first leaves a text field, then closes the menu in game
		if(ImGui::GetIO().WantTextInput)
		{
			m_Backend.OnInput(Event);
			return true;
		}
		if(IsOnline())
			SetActive(false);
		return true;
	}

	m_Backend.OnInput(Event);
	return true;
}

float CXcMenu::UiScale() const
{
	if(g_Config.m_XcUiScale > 0)
		return g_Config.m_XcUiScale / 100.0f;
	// automatic: designed for 1080p, grows on bigger screens
	return std::clamp(Graphics()->ScreenHeight() / 1080.0f, 0.75f, 3.0f);
}

void CXcMenu::UpdateStyle()
{
	static const float s_aAnimationSpeeds[] = {0.0f, 1.7f, 1.0f, 0.6f};
	SetAnimationSpeed(s_aAnimationSpeeds[std::clamp(g_Config.m_XcUiAnimations, 0, 3)]);

	// fonts are rasterized at the final size, so rounding the scale avoids rebuilding all the time
	const float Scale = std::round(UiScale() * 20.0f) / 20.0f;
	if(m_Backend.FontFamily() != g_Config.m_XcUiFont || m_Backend.FontScale() != Scale)
	{
		m_Backend.LoadFonts(g_Config.m_XcUiFont, Scale);
		const CImGuiBackend::SFonts &BackendFonts = m_Backend.Fonts();
		SFonts Fonts;
		Fonts.m_pBody = BackendFonts.m_pBody;
		Fonts.m_pLabel = BackendFonts.m_pLabel;
		Fonts.m_pTitle = BackendFonts.m_pTitle;
		Fonts.m_pHeader = BackendFonts.m_pHeader;
		Fonts.m_pSmall = BackendFonts.m_pSmall;
		Fonts.m_pIcons = BackendFonts.m_pIcons;
		SetFonts(Fonts);
	}
	if(m_AppliedTheme != g_Config.m_XcUiTheme || m_AppliedAccent != g_Config.m_XcUiAccent || m_AppliedScale != Scale)
	{
		m_AppliedTheme = g_Config.m_XcUiTheme;
		m_AppliedAccent = g_Config.m_XcUiAccent;
		m_AppliedScale = Scale;
		SetScale(Scale);
		SetTheme(g_Config.m_XcUiTheme, g_Config.m_XcUiAccent);
		ApplyStyle();
	}
}

void CXcMenu::RenderBackground()
{
	const float Width = (float)Graphics()->ScreenWidth();
	const float Height = (float)Graphics()->ScreenHeight();
	const STheme &t = Theme();
	if(!IsOnline())
	{
		// main menu: the classic animated map background, or a plain gradient
		if(!GameClient()->m_MenuBackground.Render())
		{
			Graphics()->MapScreen(CScreenRect(0.0f, 0.0f, Width, Height));
			Graphics()->TextureClear();
			Graphics()->QuadsBegin();
			const ColorRGBA Top(t.m_Background.x, t.m_Background.y, t.m_Background.z, 1.0f);
			const ColorRGBA Bottom(std::min(t.m_Background.x + t.m_Accent.x * 0.08f, 1.0f), std::min(t.m_Background.y + t.m_Accent.y * 0.08f, 1.0f), std::min(t.m_Background.z + t.m_Accent.z * 0.08f, 1.0f), 1.0f);
			Graphics()->SetColor4(Top, Top, Bottom, Bottom);
			const IGraphics::CQuadItem Item(0.0f, 0.0f, Width, Height);
			Graphics()->QuadsDrawTL(&Item, 1);
			Graphics()->QuadsEnd();
		}
	}
	if(g_Config.m_XcMenuBlur && g_Config.m_XcMenuBackground == 0)
		GameClient()->m_FxRenderer.BlurBackground(m_OpenAnimation);
}

void CXcMenu::OnRender()
{
	if(!m_Backend.IsReady() || !g_Config.m_XcNewMenu)
	{
		m_OpenAnimation = 0.0f;
		return;
	}
	// the classic menu was closed in the main menu: there must always be a menu
	if(!m_Active && !IsOnline() && !GameClient()->m_Menus.IsActive() && Client()->State() == IClient::STATE_OFFLINE)
		SetActive(true);

	// fade and slide in about 180 ms
	const float Dt = std::min(Client()->RenderFrameTime(), 0.1f);
	const float Target = m_Active ? 1.0f : 0.0f;
	const float Speed = AnimationSpeed() > 0.0f ? 6.0f * AnimationSpeed() : 1000.0f;
	m_OpenAnimation = m_OpenAnimation < Target ? std::min(m_OpenAnimation + Dt * Speed, Target) : std::max(m_OpenAnimation - Dt * Speed, Target);
	if(m_OpenAnimation <= 0.0f)
		return;

	UpdateStyle();
	RenderBackground();

	m_Backend.NewFrame(Dt, Ui()->UpdatedMousePos());
	RenderFrame();
	m_Backend.Render();

	if(m_Active)
	{
		const float Width = (float)Graphics()->ScreenWidth();
		const float Height = (float)Graphics()->ScreenHeight();
		const vec2 Mouse = Ui()->UpdatedMousePos() * vec2(Width / std::max((float)Graphics()->WindowWidth(), 1.0f), Height / std::max((float)Graphics()->WindowHeight(), 1.0f));
		Graphics()->MapScreen(CScreenRect(0.0f, 0.0f, Width, Height));
		RenderTools()->RenderCursor(Mouse, S(24.0f));
	}
}

void CXcMenu::RenderFrame()
{
	const ImGuiIO &Io = ImGui::GetIO();
	const STheme &t = Theme();
	// smoothstep makes the opening feel soft at both ends
	const float Open = m_OpenAnimation * m_OpenAnimation * (3.0f - 2.0f * m_OpenAnimation);
	ImGui::GetStyle().Alpha = Open;

	// darken the game a little behind the menu
	ImDrawList *pBackground = ImGui::GetBackgroundDrawList();
	pBackground->AddRectFilled(ImVec2(0, 0), Io.DisplaySize, Col(t.m_Background, (IsOnline() ? 0.35f : 0.15f) * Open));

	const float Margin = S(24.0f);
	const float Slide = (1.0f - Open) * S(14.0f);
	ImGui::SetNextWindowPos(ImVec2(0.0f, Slide));
	ImGui::SetNextWindowSize(Io.DisplaySize);
	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(Margin, S(20.0f)));
	ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
	ImGui::Begin("##xc_menu", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoNav);
	ImGui::PopStyleVar(2);
	ImGui::PushFont(Fonts().m_pBody);

	RenderTopBar(Io.DisplaySize.x - Margin * 2.0f);

	// big rounded panel with the page
	ImGui::SetCursorPosY(ImGui::GetCursorPosY() + S(6.0f));
	const ImVec2 PanelMin = ImGui::GetCursorScreenPos();
	const ImVec2 PanelMax(Io.DisplaySize.x - Margin, Io.DisplaySize.y - Margin + Slide);
	PanelRect(ImGui::GetWindowDrawList(), PanelMin, PanelMax, S(18.0f), t.m_Panel);
	ImGui::SetCursorScreenPos(ImVec2(PanelMin.x + S(20.0f), PanelMin.y + S(20.0f)));
	ImGui::BeginChild("##page", ImVec2(PanelMax.x - PanelMin.x - S(40.0f), PanelMax.y - PanelMin.y - S(40.0f)), ImGuiChildFlags_None, ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
	switch(m_Page)
	{
	case PAGE_HOME: RenderHome(); break;
	case PAGE_SERVERS: RenderServers(); break;
	case PAGE_VISUALS: RenderVisuals(); break;
	case PAGE_CONFIGS: RenderConfigs(); break;
	default: RenderSettings(); break;
	}
	ImGui::EndChild();

	ImGui::PopFont();
	ImGui::End();
}

void CXcMenu::RenderTopBar(float Width)
{
	const STheme &t = Theme();
	const float BarHeight = S(40.0f);
	const ImVec2 Start = ImGui::GetCursorScreenPos();
	ImDrawList *pDraw = ImGui::GetWindowDrawList();

	// logo: accent dot and the client name
	const float DotRadius = S(7.0f);
	pDraw->AddCircleFilled(ImVec2(Start.x + DotRadius + S(4.0f), Start.y + BarHeight * 0.5f), DotRadius, Col(t.m_Accent), 24);
	pDraw->AddCircleFilled(ImVec2(Start.x + DotRadius + S(4.0f), Start.y + BarHeight * 0.5f), DotRadius * 2.2f, Col(t.m_Accent, 0.12f), 32);
	ImFont *pTitle = Fonts().m_pTitle;
	pDraw->AddText(pTitle, pTitle->FontSize, ImVec2(Start.x + DotRadius * 2.0f + S(14.0f), Start.y + BarHeight * 0.5f - pTitle->FontSize * 0.5f), Col(t.m_Text), XC_CLIENT_NAME);

	// tabs in a capsule in the middle
	const char *apIcons[NUM_PAGES] = {XC_ICON_HOME, XC_ICON_SERVER, XC_ICON_WAND, XC_ICON_FOLDER, XC_ICON_GEAR};
	const char *apLabels[NUM_PAGES] = {Tr("Home"), Tr("Servers"), Tr("Visuals"), Tr("Configs"), Tr("Settings")};
	ImFont *pLabel = Fonts().m_pLabel;
	float CapsuleWidth = S(8.0f);
	for(int i = 0; i < NUM_PAGES; i++)
		CapsuleWidth += S(32.0f) + S(8.0f) + pLabel->CalcTextSizeA(pLabel->FontSize, FLT_MAX, 0.0f, apIcons[i]).x + pLabel->CalcTextSizeA(pLabel->FontSize, FLT_MAX, 0.0f, apLabels[i]).x;
	ImGui::SetCursorScreenPos(ImVec2(Start.x + (Width - CapsuleWidth) * 0.5f, Start.y));
	if(TabCapsule("##pages", &m_Page, apIcons, apLabels, NUM_PAGES))
		m_ScrollTarget = 0.0f;

	// quick buttons on the right
	const int NumButtons = 4;
	const float ButtonsWidth = NumButtons * S(34.0f) + (NumButtons - 1) * S(8.0f);
	float x = Start.x + Width - ButtonsWidth;
	const float y = Start.y + (BarHeight - S(34.0f)) * 0.5f;
	const auto Place = [&]() {
		ImGui::SetCursorScreenPos(ImVec2(x, y));
		x += S(34.0f) + S(8.0f);
	};
	Place();
	if(IconButton(XC_ICON_GLOBE, Tr("Language")))
		SetConfigString("cl_languagefile", IsRussian() ? "" : "languages/russian.txt");
	Place();
	if(IconButton(t.m_Dark ? XC_ICON_MOON : XC_ICON_SUN, Tr("Theme")))
		SetConfigInt("xc_ui_theme", g_Config.m_XcUiTheme ? 0 : 1);
	Place();
	if(IconButton(XC_ICON_WINDOW, Tr("Classic menu")))
		m_WantClassic = true;
	Place();
	if(IsOnline())
	{
		if(IconButton(XC_ICON_XMARK, Tr("Close")))
			SetActive(false);
	}
	else if(IconButton(XC_ICON_POWER, Tr("Quit")))
	{
		Client()->Quit();
	}

	ImGui::SetCursorScreenPos(ImVec2(Start.x, Start.y + BarHeight + S(12.0f)));
	if(m_WantClassic)
	{
		m_WantClassic = false;
		OpenClassicMenu();
	}
}

void CXcMenu::BeginSmoothScroll(const char *pId, ImVec2 Size)
{
	ImGui::BeginChild(pId, Size, ImGuiChildFlags_None, ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoScrollWithMouse);
	// the wheel sets a target, the view glides there
	const float MaxScroll = ImGui::GetScrollMaxY();
	if(ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows) && ImGui::GetIO().MouseWheel != 0.0f)
		m_ScrollTarget -= ImGui::GetIO().MouseWheel * S(90.0f);
	m_ScrollTarget = std::clamp(m_ScrollTarget, 0.0f, MaxScroll);
	const float Scroll = Animate(ImGui::GetID("##scroll"), m_ScrollTarget, 14.0f);
	ImGui::SetScrollY(std::clamp(Scroll, 0.0f, MaxScroll));
}

void CXcMenu::EndSmoothScroll()
{
	ImGui::EndChild();
}

void CXcMenu::RenderPlaceholder(const char *pIcon, const char *pTitle, const char *pText, bool ClassicButton)
{
	const STheme &t = Theme();
	const ImVec2 Avail = ImGui::GetContentRegionAvail();
	ImFont *pIcons = Fonts().m_pHeader;
	const float BlockHeight = S(200.0f);
	ImGui::SetCursorPosY(ImGui::GetCursorPosY() + std::max((Avail.y - BlockHeight) * 0.4f, 0.0f));
	ImDrawList *pDraw = ImGui::GetWindowDrawList();
	const ImVec2 Center(ImGui::GetCursorScreenPos().x + Avail.x * 0.5f, ImGui::GetCursorScreenPos().y + S(34.0f));
	pDraw->AddCircleFilled(Center, S(34.0f), Col(t.m_Accent, 0.14f), 48);
	const ImVec2 IconSize = pIcons->CalcTextSizeA(pIcons->FontSize, FLT_MAX, 0.0f, pIcon);
	pDraw->AddText(pIcons, pIcons->FontSize, ImVec2(Center.x - IconSize.x * 0.5f, Center.y - IconSize.y * 0.5f), Col(t.m_Accent), pIcon);
	ImGui::Dummy(ImVec2(0.0f, S(80.0f)));

	const auto Centered = [&](ImFont *pFont, const ImVec4 &Color, const char *pStr) {
		const float TextWidth = pFont->CalcTextSizeA(pFont->FontSize, FLT_MAX, 0.0f, pStr).x;
		ImGui::SetCursorPosX(std::max((Avail.x - TextWidth) * 0.5f, 0.0f));
		ImGui::PushFont(pFont);
		ImGui::TextColored(Color, "%s", pStr);
		ImGui::PopFont();
	};
	Centered(Fonts().m_pHeader, t.m_Text, pTitle);
	Centered(Fonts().m_pBody, t.m_TextMuted, pText);
	if(ClassicButton)
	{
		ImGui::Dummy(ImVec2(0.0f, S(8.0f)));
		const char *pLabel = Tr("Classic menu");
		const float ButtonWidth = Fonts().m_pLabel->CalcTextSizeA(Fonts().m_pLabel->FontSize, FLT_MAX, 0.0f, pLabel).x + S(28.0f);
		ImGui::SetCursorPosX((Avail.x - ButtonWidth) * 0.5f);
		if(Button(pLabel, true))
			m_WantClassic = true;
	}
	if(m_WantClassic)
	{
		m_WantClassic = false;
		OpenClassicMenu();
	}
}

void CXcMenu::RenderHome()
{
	const STheme &t = Theme();
	char aWelcome[128];
	str_format(aWelcome, sizeof(aWelcome), "%s, %s", Tr("Welcome back"), Client()->PlayerName());
	Header(aWelcome);
	Description(IsOnline() ? Tr("Connected") : Tr("Offline"));
	ImGui::Dummy(ImVec2(0.0f, S(12.0f)));

	BeginCardGrid("##home", 2);
	if(BeginCard("##actions", Tr("Quick actions"), nullptr))
	{
		if(IsOnline())
		{
			if(Button(Tr("Disconnect"), false, -1.0f))
				Client()->Disconnect();
		}
		else if(Button(Tr("Play"), true, -1.0f))
		{
			m_Page = PAGE_SERVERS;
		}
		if(Button(Tr("Classic menu"), false, -1.0f))
			m_WantClassic = true;
		EndCard();
	}
	if(BeginCard("##profile", Tr("Profile"), Tr("Name and clan of you and your dummy")))
	{
		ParamLabel(Tr("Name"));
		ImGui::PushFont(Fonts().m_pTitle);
		ImGui::TextColored(t.m_Text, "%s", Client()->PlayerName());
		ImGui::PopFont();
		if(g_Config.m_PlayerClan[0])
		{
			ParamLabel(Tr("Clan"));
			ImGui::TextColored(t.m_TextSecondary, "%s", g_Config.m_PlayerClan);
		}
		EndCard();
	}
	EndCardGrid();
	if(m_WantClassic)
	{
		m_WantClassic = false;
		OpenClassicMenu();
	}
}

void CXcMenu::RenderVisuals()
{
	RenderPlaceholder(XC_ICON_WAND, Tr("Visuals"), Tr("Visual effects are being redesigned."), false);
}

void CXcMenu::RenderConfigs()
{
	RenderPlaceholder(XC_ICON_FOLDER, Tr("Configs"), Tr("Local configs with import and export codes are coming."), false);
}

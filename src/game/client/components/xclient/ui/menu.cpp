#include "menu.h"

#include "feedback.h"
#include "icons.h"
#include "lang.h"
#include "theme.h"
#include "widgets.h"

#include <base/str.h>

#include <engine/client.h>
#include <engine/external/imgui/imgui.h>
#include <engine/external/imgui/imgui_internal.h>
#include <engine/friends.h>
#include <engine/graphics.h>
#include <engine/serverbrowser.h>
#include <engine/shared/config.h>
#include <engine/xc_render.h>
#include <engine/storage.h>

#include <game/client/animstate.h>
#include <game/client/gameclient.h>
#include <game/version.h>

#include <algorithm>
#include <cmath>

using namespace XcUi;

static const char *const XC_CLIENT_NAME = "Exora Client";

void CXcMenu::OnConsoleInit()
{
	Console()->Register("xc_menu", "", CFGFLAG_CLIENT, ConToggleMenu, this, "Open or close the new menu");
	Console()->Register("xc_visuals", "", CFGFLAG_CLIENT, ConToggleVisuals, this, "Open or close the visuals window (like the click menu of Minecraft clients)");
}

void CXcMenu::ConToggleVisuals(IConsole::IResult *pResult, void *pUserData)
{
	CXcMenu *pSelf = static_cast<CXcMenu *>(pUserData);
	// the same key closes it again
	if(pSelf->m_Active && pSelf->m_Page == PAGE_VISUALS)
	{
		if(pSelf->IsOnline())
			pSelf->SetActive(false);
		else
			pSelf->m_Page = PAGE_HOME;
		return;
	}
	pSelf->m_VisWinOpen = 0.0f;
	pSelf->OpenAt(PAGE_VISUALS, pSelf->m_VisualsSection);
}

void CXcMenu::ConToggleMenu(IConsole::IResult *pResult, void *pUserData)
{
	CXcMenu *pSelf = static_cast<CXcMenu *>(pUserData);
	pSelf->SetActive(!pSelf->m_Active);
}

void CXcMenu::OnInit()
{
	// the page and sections from the last time the game ran
	m_Page = std::clamp(g_Config.m_XcMenuPage, 0, NUM_PAGES - 1);
	m_SettingsSection = std::clamp(g_Config.m_XcMenuSettingsSection, 0, NUM_SETTINGS - 1);
	m_VisualsSection = std::clamp(g_Config.m_XcMenuVisualsSection, 0, NUM_VISUALS - 1);
	// the start clock: the client opens on it, a click goes to the home
	if(g_Config.m_XcStartClock)
	{
		m_StartLocked = true;
		m_Page = PAGE_HOME;
	}
	m_Backend.Init(Graphics(), Input(), Storage());
	m_Backgrounds.Init(Graphics(), Storage(), GameClient()->Engine());
	m_MapPreviews.Init(Graphics(), Storage(), GameClient()->Engine());

	// interface sounds: our own set generated for XClient and the Flower set (data/xclient/sounds)
	static const char *const s_aapSounds[NUM_UI_SOUND_PACKS][XcUi::NUM_SOUNDS] = {
		{"click", "tab", "toggle_on", "toggle_off", "notify", "error", "flower_tick"},
		{"flower_click", "flower_click", "flower_enable", "flower_disable", "notify", "error", "flower_tick"},
	};
	for(int Pack = 0; Pack < NUM_UI_SOUND_PACKS; Pack++)
	{
		for(int i = 0; i < XcUi::NUM_SOUNDS; i++)
		{
			char aPath[IO_MAX_PATH_LENGTH];
			str_format(aPath, sizeof(aPath), "xclient/sounds/%s.opus", s_aapSounds[Pack][i]);
			m_aaUiSounds[Pack][i] = Sound()->IsSoundEnabled() ? Sound()->LoadOpus(aPath) : -1;
		}
	}
	SetSoundPlayer(PlayUiSoundCallback, this);
	m_Active = g_Config.m_XcNewMenu && !IsOnline();
	m_OpenAnimation = m_Active ? 1.0f : 0.0f;
}

void CXcMenu::PlayUiSoundCallback(void *pUser, int Sound)
{
	CXcMenu *pSelf = static_cast<CXcMenu *>(pUser);
	if(!g_Config.m_XcUiSounds || Sound < 0 || Sound >= XcUi::NUM_SOUNDS)
		return;
	if(Sound == XcUi::SOUND_TICK && !g_Config.m_XcUiSoundTicks)
		return;
	const int Sample = pSelf->m_aaUiSounds[std::clamp(g_Config.m_XcUiSoundPack, 0, NUM_UI_SOUND_PACKS - 1)][Sound];
	if(Sample < 0)
		return;
	// one widget can report a change several frames in a row, play each sound at most every 40 ms
	const float Now = pSelf->Client()->GlobalTime();
	if(Now - pSelf->m_aUiSoundTime[Sound] < 0.04f)
		return;
	pSelf->m_aUiSoundTime[Sound] = Now;
	pSelf->Sound()->Play(CSounds::CHN_XC_UI, Sample, 0, g_Config.m_XcUiSoundVolume / 100.0f * (Sound == XcUi::SOUND_TICK ? 0.6f : 1.0f));
}

void CXcMenu::OnShutdown()
{
	UnloadAssetPreviews();
	if(m_pStatsTask)
		m_pStatsTask->Abort();
	m_pStatsTask = nullptr;
	SetSoundPlayer(nullptr, nullptr);
	for(auto &aSamples : m_aaUiSounds)
	{
		for(int &Sample : aSamples)
		{
			if(Sample >= 0)
				Sound()->UnloadSample(Sample);
			Sample = -1;
		}
	}
	Client()->XcSetDropCapture(false);
	m_Backgrounds.Shutdown();
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

void CXcMenu::OpenAt(int Page, int Section)
{
	g_Config.m_XcNewMenu = 1;
	m_Page = std::clamp(Page, 0, NUM_PAGES - 1);
	if(m_Page == PAGE_VISUALS)
		m_VisualsSection = std::clamp(Section, 0, NUM_VISUALS - 1);
	else if(m_Page == PAGE_SETTINGS)
		m_SettingsSection = std::clamp(Section, 0, NUM_SETTINGS - 1);
	SetActive(true);
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
	else if(NewState == IClient::STATE_CONNECTING || NewState == IClient::STATE_LOADING || NewState == IClient::STATE_ONLINE || NewState == IClient::STATE_DEMOPLAYBACK)
		SetActive(false);
	// a demo starts with the player bar shown, it hides by itself when the mouse rests
	m_DemoBar = NewState == IClient::STATE_DEMOPLAYBACK;
	m_DemoIdle = 0.0f;
	m_DemoSliceOpen = false;
	(void)OldState;
}

bool CXcMenu::OnCursorMove(float x, float y, IInput::ECursorType CursorType)
{
	const bool DemoBar = !m_Active && m_DemoBar && Client()->State() == IClient::STATE_DEMOPLAYBACK;
	if(DemoBar)
		m_DemoIdle = 0.0f;
	if(!m_Active && !DemoBar)
		return false;
	Ui()->ConvertMouseMove(&x, &y, CursorType);
	// a slider dragged with Shift: the mouse itself moves 10 times slower, for exact values
	if(AnyDragIsFine())
	{
		x *= 0.1f;
		y *= 0.1f;
	}
	Ui()->OnCursorMove(x, y);
	m_Backend.OnMouseMove(Ui()->UpdatedMousePos());
	return true;
}

bool CXcMenu::OnInput(const IInput::CEvent &Event)
{
	if(!g_Config.m_XcNewMenu || !m_Backend.IsReady())
		return false;
	m_Backend.MakeCurrent(); // the HUD has its own ImGui context

	const bool EscapePressed = (Event.m_Flags & IInput::FLAG_PRESS) && Event.m_Key == KEY_ESCAPE;
	if(!m_Active)
	{
		// in a demo Esc shows and hides our player bar; the keys of the stock player stay as they are
		if(Client()->State() == IClient::STATE_DEMOPLAYBACK)
			return DemoBarInput(Event);
		// Esc opens the new menu, unless the classic one is open in game (it closes itself then)
		if(EscapePressed && (!IsOnline() || !GameClient()->m_Menus.IsActive()))
		{
			// the menu opens where it was left last time (the map card stays on Home); the minimal home in game
			// is always the first thing Esc shows, its parts come in again
			if(GameHome())
			{
				m_Page = PAGE_HOME;
				m_HomeTime = 0.0001f;
				m_HomeLeaveTo = -1;
				m_GameMapCard = false;
				m_GamePopup = GAME_POPUP_NONE;
			}
			SetActive(true);
			return true;
		}
		return false;
	}

	if(m_BindWaiting >= 0 && OnBindInput(Event))
		return true;
	// the key of the visuals window closes it again (the menu takes the keys otherwise), not while typing
	if((Event.m_Flags & IInput::FLAG_PRESS) && !ImGui::GetIO().WantTextInput && Event.m_Key > KEY_FIRST && Event.m_Key < KEY_LAST && str_comp(GameClient()->m_Binds.Get(Event.m_Key, KeyModifier::NONE), "xc_visuals") == 0)
	{
		Console()->ExecuteLine("xc_visuals", IConsole::CLIENT_ID_UNSPECIFIED);
		return true;
	}

	if(EscapePressed)
	{
		// Esc first leaves a text field, then closes the menu in game
		if(m_SearchOpen)
		{
			m_SearchOpen = false;
			return true;
		}
		// a text field or the disconnected / password window takes the Esc
		if(ImGui::GetIO().WantTextInput || GameClient()->m_Menus.m_Popup == CMenus::POPUP_DISCONNECTED || GameClient()->m_Menus.m_Popup == CMenus::POPUP_PASSWORD)
		{
			m_Backend.OnInput(Event);
			return true;
		}
		if(m_Page == PAGE_HOME && GameHome() && GameHomeEscape())
			return true;
		if(IsOnline())
			SetActive(false);
		return true;
	}

	m_Backend.OnInput(Event);
	return true;
}

float CXcMenu::UiScale() const
{
	const float Size = g_Config.m_XcUiSize / 100.0f;
	// the layout needs about 1400 x 860 design pixels: never bigger than what fits the screen,
	// so small and narrow windows (1280 x 882, 4:3) get the whole menu instead of a cut one
	const float Fit = std::min(Graphics()->ScreenWidth() / 1400.0f, Graphics()->ScreenHeight() / 860.0f);
	if(g_Config.m_XcUiScale > 0)
		return std::max(std::min(g_Config.m_XcUiScale / 100.0f * Size, Fit), 0.5f);
	// automatic: designed for 1080p, grows on bigger screens
	const float Auto = Graphics()->ScreenHeight() / 1080.0f;
	return std::clamp(std::min(Auto * Size, Fit), 0.5f, 3.0f);
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
		m_FontsRebuilt = true;
		const CImGuiBackend::SFonts &BackendFonts = m_Backend.Fonts();
		SFonts Fonts;
		Fonts.m_pBody = BackendFonts.m_pBody;
		Fonts.m_pLabel = BackendFonts.m_pLabel;
		Fonts.m_pTitle = BackendFonts.m_pTitle;
		Fonts.m_pHeader = BackendFonts.m_pHeader;
		Fonts.m_pSmall = BackendFonts.m_pSmall;
		Fonts.m_pIcons = BackendFonts.m_pIcons;
		Fonts.m_pClock = BackendFonts.m_pClock;
		Fonts.m_pDisplay = BackendFonts.m_pDisplay;
		SetFonts(Fonts);
	}
	// everything the theme is made of, so a change shows at once
	const unsigned ThemeKey = g_Config.m_XcUiAccentColor ^ (g_Config.m_XcUiTintColor * 31u) ^ ((unsigned)g_Config.m_XcUiTint << 25);
	if(m_AppliedTheme != g_Config.m_XcUiTheme || m_AppliedAccent != g_Config.m_XcUiAccent || m_AppliedScale != Scale || m_AppliedThemeKey != ThemeKey)
	{
		m_AppliedThemeKey = ThemeKey;
		m_AppliedTheme = g_Config.m_XcUiTheme;
		m_AppliedAccent = g_Config.m_XcUiAccent;
		m_AppliedScale = Scale;
		SetScale(Scale);
		SetTheme(g_Config.m_XcUiTheme, g_Config.m_XcUiAccent);
		ApplyStyle();
	}
}

bool CXcMenu::AnimatedBackground() const
{
	return EffectiveBackground() == BG_ANIMATED;
}

void CXcMenu::RenderBackground()
{
	const float Width = (float)Graphics()->ScreenWidth();
	const float Height = (float)Graphics()->ScreenHeight();
	switch(EffectiveBackground())
	{
	case BG_CUSTOM:
		m_Backgrounds.Render(Width, Height, IsOnline() ? m_OpenAnimation : 1.0f);
		if(g_Config.m_XcMenuBgBlur)
			GameClient()->m_FxRenderer.BlurBackground(IsOnline() ? m_OpenAnimation : 1.0f, true);
		break;
	case BG_BLUR_GAME:
		GameClient()->m_FxRenderer.BlurBackground(m_OpenAnimation);
		break;
	case BG_MAP:
		if(!IsOnline() && !GameClient()->m_MenuBackground.Render())
		{
			// the map is not loaded: plain theme color
			const ImVec4 &Bg = Theme().m_Background;
			Graphics()->MapScreen(CScreenRect(0.0f, 0.0f, Width, Height));
			Graphics()->TextureClear();
			Graphics()->QuadsBegin();
			Graphics()->SetColor(Bg.x, Bg.y, Bg.z, 1.0f);
			const IGraphics::CQuadItem Item(0.0f, 0.0f, Width, Height);
			Graphics()->QuadsDrawTL(&Item, 1);
			Graphics()->QuadsEnd();
		}
		break;
	default: // animated, drawn by ImGui in RenderFrame
		break;
	}
}

// a filled circle whose color fades from the center to fully transparent at the edge
void XcSoftCircle(ImDrawList *pDraw, ImVec2 Center, float Radius, ImVec4 Color)
{
	const int Segments = 64;
	// center -> inner ring -> edge, the two steps make the falloff look round instead of conic
	const float InnerRadius = Radius * 0.45f;
	const ImU32 CenterCol = ImGui::ColorConvertFloat4ToU32(Color);
	const ImU32 InnerCol = ImGui::ColorConvertFloat4ToU32(ImVec4(Color.x, Color.y, Color.z, Color.w * 0.6f));
	const ImU32 EdgeCol = ImGui::ColorConvertFloat4ToU32(ImVec4(Color.x, Color.y, Color.z, 0.0f));
	const ImVec2 Uv = pDraw->_Data->TexUvWhitePixel;
	pDraw->PrimReserve(Segments * 9, 1 + Segments * 2);
	const ImDrawIdx Base = (ImDrawIdx)pDraw->_VtxCurrentIdx;
	pDraw->PrimWriteVtx(Center, Uv, CenterCol);
	for(int i = 0; i < Segments; i++)
	{
		const float a = (float)i / Segments * 2.0f * IM_PI;
		const ImVec2 Dir(std::cos(a), std::sin(a));
		pDraw->PrimWriteVtx(ImVec2(Center.x + Dir.x * InnerRadius, Center.y + Dir.y * InnerRadius), Uv, InnerCol);
		pDraw->PrimWriteVtx(ImVec2(Center.x + Dir.x * Radius, Center.y + Dir.y * Radius), Uv, EdgeCol);
	}
	for(int i = 0; i < Segments; i++)
	{
		const ImDrawIdx Inner = Base + 1 + i * 2, Outer = Inner + 1;
		const ImDrawIdx NextInner = Base + 1 + ((i + 1) % Segments) * 2, NextOuter = NextInner + 1;
		pDraw->PrimWriteIdx(Base);
		pDraw->PrimWriteIdx(Inner);
		pDraw->PrimWriteIdx(NextInner);
		pDraw->PrimWriteIdx(Inner);
		pDraw->PrimWriteIdx(Outer);
		pDraw->PrimWriteIdx(NextOuter);
		pDraw->PrimWriteIdx(Inner);
		pDraw->PrimWriteIdx(NextOuter);
		pDraw->PrimWriteIdx(NextInner);
	}
}

ImVec4 XcShiftHue(const ImVec4 &Color, float Shift)
{
	float h, s, v;
	ImGui::ColorConvertRGBtoHSV(Color.x, Color.y, Color.z, h, s, v);
	h = std::fmod(h + Shift + 1.0f, 1.0f);
	ImVec4 Out(0, 0, 0, Color.w);
	ImGui::ColorConvertHSVtoRGB(h, s, v, Out.x, Out.y, Out.z);
	return Out;
}

// cheap stable pseudo random number in [0, 1) for particle i
float XcHash(int i, int Salt)
{
	unsigned x = (unsigned)i * 747796405u + (unsigned)Salt * 2891336453u;
	x = ((x >> ((x >> 28u) + 4u)) ^ x) * 277803737u;
	x = (x >> 22u) ^ x;
	return (x & 0xffffff) / (float)0x1000000;
}

void CXcMenu::RenderMenuBackground(ImDrawList *pDraw, float Alpha)
{
	if(g_Config.m_XcMenuAnimStyle == 0 || g_Config.m_XcMenuAnimStyle == 1)
	{
		// time stands still when animations are off
		if(AnimationSpeed() > 0.0f)
			m_BackgroundTime += std::min(Client()->RenderFrameTime(), 0.1f);
		if(g_Config.m_XcMenuAnimStyle == 0)
			RenderTeesBackground(pDraw, Alpha);
		else
			RenderWavesBackground(pDraw, Alpha);
		return;
	}
	RenderAnimatedBackground(pDraw, Alpha);
}

void CXcMenu::RenderAnimatedBackground(ImDrawList *pDraw, float Alpha)
{
	const STheme &t = Theme();
	const ImVec2 Size = ImGui::GetIO().DisplaySize;
	// time stands still when animations are off
	if(AnimationSpeed() > 0.0f)
		m_BackgroundTime += std::min(Client()->RenderFrameTime(), 0.1f);
	const float Time = m_BackgroundTime;
	const bool Light = t.m_Background.x > 0.5f;

	// base: theme background with a slightly lighter, accent tinted bottom
	const ImVec4 Top(t.m_Background.x, t.m_Background.y, t.m_Background.z, Alpha);
	ImVec4 Bottom = Mix(t.m_Background, t.m_Accent, Light ? 0.06f : 0.05f);
	Bottom.w = Alpha;
	pDraw->AddRectFilledMultiColor(ImVec2(0, 0), Size, Col(Top), Col(Top), Col(Bottom), Col(Bottom));

	// big soft glows in the accent color and its neighbours drift slowly around the screen
	struct SGlow
	{
		float m_X, m_Y; // base position, 0..1
		float m_MoveX, m_MoveY; // how far it wanders
		float m_SpeedX, m_SpeedY;
		float m_Radius; // relative to the screen height
		float m_Hue; // shift from the accent
		float m_Strength;
	};
	static const SGlow s_aGlows[] = {
		{0.18f, 0.22f, 0.10f, 0.12f, 0.050f, 0.070f, 0.85f, 0.00f, 1.00f},
		{0.85f, 0.30f, 0.08f, 0.15f, 0.060f, 0.045f, 0.75f, 0.10f, 0.80f},
		{0.65f, 0.90f, 0.15f, 0.08f, 0.040f, 0.055f, 0.90f, -0.08f, 0.85f},
		{0.30f, 0.80f, 0.12f, 0.10f, 0.065f, 0.035f, 0.60f, 0.05f, 0.55f},
	};
	const float BaseAlpha = (Light ? 0.30f : 0.42f) * Alpha;
	for(const SGlow &Glow : s_aGlows)
	{
		const ImVec2 Center(Size.x * (Glow.m_X + Glow.m_MoveX * std::sin(Time * Glow.m_SpeedX * 2.0f * IM_PI)),
			Size.y * (Glow.m_Y + Glow.m_MoveY * std::cos(Time * Glow.m_SpeedY * 2.0f * IM_PI)));
		const float Breath = 1.0f + 0.06f * std::sin(Time * 0.35f + Glow.m_X * 10.0f);
		ImVec4 Color = XcShiftHue(t.m_Accent, Glow.m_Hue);
		Color.w = BaseAlpha * Glow.m_Strength;
		XcSoftCircle(pDraw, Center, Size.y * Glow.m_Radius * Breath, Color);
	}

	// faint dot grid, a bit brighter where the light falls
	const float Step = S(28.0f);
	const float Dot = std::max(S(1.2f), 1.0f);
	const ImU32 DotCol = Col(t.m_Text, (Light ? 0.07f : 0.045f) * Alpha);
	for(float y = std::fmod(Size.y * 0.5f, Step); y < Size.y; y += Step)
		for(float x = std::fmod(Size.x * 0.5f, Step); x < Size.x; x += Step)
			pDraw->AddRectFilled(ImVec2(x - Dot * 0.5f, y - Dot * 0.5f), ImVec2(x + Dot * 0.5f, y + Dot * 0.5f), DotCol);

	// small sparks rise slowly and twinkle
	const int NumSparks = 48;
	for(int i = 0; i < NumSparks; i++)
	{
		const float Speed = 0.012f + XcHash(i, 1) * 0.02f; // screen heights per second
		const float Phase = std::fmod(XcHash(i, 2) + Time * Speed, 1.0f);
		const float x = Size.x * (XcHash(i, 3) + 0.015f * std::sin(Time * (0.3f + XcHash(i, 4)) + i));
		const float y = Size.y * (1.05f - Phase * 1.1f);
		// fade in at the bottom, out at the top, and twinkle
		const float Fade = std::min(Phase * 6.0f, 1.0f) * std::min((1.0f - Phase) * 4.0f, 1.0f);
		const float Twinkle = 0.55f + 0.45f * std::sin(Time * (1.5f + XcHash(i, 5) * 2.0f) + i * 1.7f);
		const float Radius = S(1.0f + XcHash(i, 6) * 1.8f);
		const ImVec4 Color = XcHash(i, 7) < 0.6f ? t.m_Accent : XcShiftHue(t.m_Accent, 0.08f);
		const float SparkAlpha = Fade * Twinkle * Alpha * (Light ? 0.5f : 0.7f);
		XcSoftCircle(pDraw, ImVec2(x, y), Radius * 5.0f, ImVec4(Color.x, Color.y, Color.z, SparkAlpha * 0.25f));
		pDraw->AddCircleFilled(ImVec2(x, y), Radius, Col(Color, SparkAlpha), 12);
	}

	// vignette: darken the corners a little so the middle stands out
	if(!Light)
	{
		const ImU32 Clear = Col(t.m_Background, 0.0f);
		const ImU32 Edge = Col(t.m_Background, 0.55f * Alpha);
		const float Band = Size.y * 0.25f;
		pDraw->AddRectFilledMultiColor(ImVec2(0, 0), ImVec2(Size.x, Band), Edge, Edge, Clear, Clear);
		pDraw->AddRectFilledMultiColor(ImVec2(0, Size.y - Band), Size, Clear, Clear, Edge, Edge);
	}
}

void CXcMenu::OnRender()
{
	// once: a key for the visuals window, like the click menu of Minecraft clients. Right Shift is the spectator
	// mode in DDNet, so Insert (the other usual key of those menus) when it is free; it can be changed in the controls
	if(!g_Config.m_XcVisualsBindSet)
	{
		g_Config.m_XcVisualsBindSet = 1;
		if(!GameClient()->m_Binds.Get(KEY_INSERT, KeyModifier::NONE)[0])
			GameClient()->m_Binds.Bind(KEY_INSERT, "xc_visuals");
	}
	UpdateDemoRender();
	UpdateSelfUpdate();
	if(!m_Backend.IsReady() || !g_Config.m_XcNewMenu)
	{
		// switched to the classic menu (or no fonts): hand the screen over to it, otherwise nothing shows
		if(m_Active)
		{
			m_Active = false;
			m_SearchOpen = false;
			if(!IsOnline())
				GameClient()->m_Menus.SetActive(true);
		}
		m_OpenAnimation = 0.0f;
		Client()->XcSetDropCapture(false);
		return;
	}
	// the classic menu was closed in the main menu: there must always be a menu
	if(!m_Active && !IsOnline() && !GameClient()->m_Menus.IsActive() && Client()->State() == IClient::STATE_OFFLINE)
		SetActive(true);

	// fade and slide in about 180 ms
	const float Dt = std::min(Client()->RenderFrameTime(), 0.1f);

	// own background: loaded only while it is chosen, so it does not hold video memory otherwise
	m_Backgrounds.Update(g_Config.m_XcMenuBackground == 3 || g_Config.m_XcMenuBackgroundGame == 4 ? g_Config.m_XcMenuBgFile : "", Dt, g_Config.m_XcMenuBgGifSpeed / 100.0f);
	// files dropped on the window while the background settings are open become backgrounds
	// and while the cosmetics editor is open they become hats
	const bool CaptureBackgrounds = m_Active && m_Page == PAGE_SETTINGS && m_SettingsSection == SETTINGS_BACKGROUND;
	const bool CaptureHats = m_Active && m_Page == PAGE_VISUALS && m_VisualsSection == VISUALS_COSMETICS;
	// and on the textures page they become texture sets
	const bool CaptureAssets = m_Active && m_Page == PAGE_SETTINGS && m_SettingsSection == SETTINGS_TEXTURES;
	// and on the world page (weather) they fall as weather
	const bool CaptureWeather = m_Active && m_Page == PAGE_VISUALS && m_VisualsSection == VISUALS_WORLD;
	Client()->XcSetDropCapture(CaptureBackgrounds || CaptureHats || CaptureAssets || CaptureWeather);
	char aDropped[IO_MAX_PATH_LENGTH];
	if((CaptureBackgrounds || CaptureHats || CaptureAssets || CaptureWeather) && Client()->XcTakeDropFile(aDropped, sizeof(aDropped)))
	{
		if(CaptureWeather)
			HandleWeatherFile(aDropped);
		else if(CaptureHats)
			HandleHatFile(aDropped);
		else if(CaptureAssets)
			HandleAssetFile(aDropped);
		else
			HandleBackgroundFile(aDropped);
	}
	// the small texture previews stay loaded: opening the page again is instant
	const float Target = m_Active ? 1.0f : 0.0f;
	const float Speed = AnimationSpeed() > 0.0f ? 6.0f * AnimationSpeed() : 1000.0f;
	m_OpenAnimation = m_OpenAnimation < Target ? std::min(m_OpenAnimation + Dt * Speed, Target) : std::max(m_OpenAnimation - Dt * Speed, Target);
	if(m_OpenAnimation <= 0.0f)
	{
		if(Client()->State() == IClient::STATE_DEMOPLAYBACK)
			RenderDemoOverlay(Dt);
		return;
	}

	UpdateStyle();
	const vec2 Screen((float)Graphics()->ScreenWidth(), (float)Graphics()->ScreenHeight());
	if(Screen != m_SettleScreen || m_FontsRebuilt)
	{
		m_SettleScreen = Screen;
		m_FontsRebuilt = false;
		m_SettleTime = 0.0f;
	}
	else
		m_SettleTime += Dt;
	RenderBackground();

	m_Backend.NewFrame(Dt, Ui()->UpdatedMousePos());
	SetCursorSlowsDown(true); // OnCursorMove slows the mouse during Shift drags of the menu
	RenderFrame();
	SetCursorSlowsDown(false);
	m_Backend.Render();
	// the HUD island stays on the screen over the menu in game
	// (only where nothing of the menu is at the top: the home in game and the visuals window; the full menu has its bar)
	if(Client()->State() == IClient::STATE_ONLINE && ((m_Page == PAGE_HOME && GameHome()) || (m_Page == PAGE_VISUALS && g_Config.m_XcVisualsWindow)))
	{
		GameClient()->m_XcHud.RenderIslandOverMenu(m_OpenAnimation);
		m_Backend.MakeCurrent();
	}

	if(m_Active)
	{
		const float Width = (float)Graphics()->ScreenWidth();
		const float Height = (float)Graphics()->ScreenHeight();
		const vec2 Mouse = Ui()->UpdatedMousePos() * vec2(Width / std::max((float)Graphics()->WindowWidth(), 1.0f), Height / std::max((float)Graphics()->WindowHeight(), 1.0f));
		Graphics()->MapScreen(CScreenRect(0.0f, 0.0f, Width, Height));
		RenderTools()->RenderCursor(Mouse, S(24.0f));
	}
}

float CXcMenu::Settled() const
{
	// a few frames to measure the layout, then a soft fade in
	const float x = std::clamp((m_SettleTime - 0.12f) / 0.3f, 0.0f, 1.0f);
	return x * x * (3.0f - 2.0f * x);
}

void CXcMenu::RenderFrame()
{
	const ImGuiIO &Io = ImGui::GetIO();
	const STheme &t = Theme();
	// smoothstep makes the opening feel soft at both ends
	const float Open = m_OpenAnimation * m_OpenAnimation * (3.0f - 2.0f * m_OpenAnimation);
	ImGui::GetStyle().Alpha = Open;

	ImDrawList *pBackground = ImGui::GetBackgroundDrawList();
	const int Background = EffectiveBackground();
	// the visuals window in game lies over the game itself, like the click menu of Minecraft clients
	if(IsOnline() && m_Page == PAGE_VISUALS && g_Config.m_XcVisualsWindow)
		pBackground->AddRectFilled(ImVec2(0, 0), Io.DisplaySize, Col(t.m_Background, 0.25f * Open));
	else if(Background == BG_ANIMATED)
		RenderMenuBackground(pBackground, IsOnline() ? 0.94f * Open : 1.0f);
	else if(Background == BG_CUSTOM)
		pBackground->AddRectFilled(ImVec2(0, 0), Io.DisplaySize, Col(ImVec4(0, 0, 0, 1), g_Config.m_XcMenuBgDim / 100.0f * (IsOnline() ? Open : 1.0f)));
	else // darken the game or the map a little behind the menu
		pBackground->AddRectFilled(ImVec2(0, 0), Io.DisplaySize, Col(t.m_Background, (IsOnline() ? 0.35f : 0.15f) * Open));

	// the start clock and the minimal home take the whole screen, without the bar and the panel
	const float FrameDt = std::min(Client()->RenderFrameTime(), 0.1f);
	if(IsOnline())
		m_StartLocked = false;
	if(m_Page == PAGE_HOME && m_LastShownPage != PAGE_HOME && !m_StartLocked)
		m_HomeTime = 0.0001f; // back on the home: its parts come in again
	m_LastShownPage = m_Page;
	if(m_StartLocked || (m_Page == PAGE_HOME && (MinimalHome() || GameHome())))
	{
		ImGui::GetStyle().Alpha = Open * Settled();
		ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f));
		ImGui::SetNextWindowSize(Io.DisplaySize);
		ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(S(28.0f), S(24.0f)));
		ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
		ImGui::Begin("##xc_start", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoNav);
		ImGui::PopStyleVar(2);
		ImGui::PushFont(Fonts().m_pBody);
		if(GameHome() && !m_StartLocked)
			RenderGameHome(FrameDt);
		else
			RenderMinimalHome(FrameDt);
		ImGui::PopFont();
		ImGui::End();
		if(m_StartLocked)
			RenderStartScreen(FrameDt);
		else if(Io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_F, false))
			OpenSearch();
		RenderSearch();
		RenderConnectionPopup();
		RenderToasts(ImGui::GetForegroundDrawList(), Io.DisplaySize, Client()->GlobalTime(), Open);
		return;
	}
	// a page opened from the minimal home fades in and rises a little
	m_PageAppear = std::min(m_PageAppear + FrameDt / 0.5f, 1.0f);
	const float PageIn = 1.0f - std::pow(1.0f - m_PageAppear, 3.0f);

	// the visuals: a compact window over the game or the background
	if(m_Page == PAGE_VISUALS && g_Config.m_XcVisualsWindow)
	{
		ImGui::GetStyle().Alpha = Open * Settled();
		RenderVisualsWindow(FrameDt);
		if(Io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_F, false))
			OpenSearch();
		RenderSearch();
		RenderConnectionPopup();
		RenderToasts(ImGui::GetForegroundDrawList(), Io.DisplaySize, Client()->GlobalTime(), Open);
		return;
	}
	m_VisWinOpen = 0.0f;

	// the server list of the main menu with the minimal home: the whole screen too, over the background
	m_ServersFullScreen = m_Page == PAGE_SERVERS && MinimalHome();
	if(m_ServersFullScreen)
	{
		ImGui::GetStyle().Alpha = Open * Settled() * PageIn;
		const float Rise = (1.0f - PageIn) * S(14.0f);
		ImGui::SetNextWindowPos(ImVec2(0.0f, Rise));
		ImGui::SetNextWindowSize(Io.DisplaySize);
		ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(S(32.0f), S(28.0f)));
		ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
		ImGui::Begin("##xc_servers_full", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoNav);
		ImGui::PopStyleVar(2);
		ImGui::PushFont(Fonts().m_pBody);
		RenderServers();
		ImGui::PopFont();
		ImGui::End();
		if(Io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_F, false))
			OpenSearch();
		RenderSearch();
		RenderConnectionPopup();
		RenderToasts(ImGui::GetForegroundDrawList(), Io.DisplaySize, Client()->GlobalTime(), Open);
		return;
	}

	// the background is drawn, the content waits for the window and the fonts to settle
	const float Shown = Open * Settled() * PageIn;
	ImGui::GetStyle().Alpha = Shown;
	const float Margin = S(24.0f);
	const float Slide = (1.0f - Shown) * S(14.0f) + (1.0f - PageIn) * S(10.0f);
	ImGui::SetNextWindowPos(ImVec2(0.0f, Slide));
	ImGui::SetNextWindowSize(Io.DisplaySize);
	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(Margin, S(20.0f)));
	ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
	ImGui::Begin("##xc_menu", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoNav);
	ImGui::PopStyleVar(2);
	ImGui::PushFont(Fonts().m_pBody);

	// the side menu takes a column on the left, the bar and the page move right of it
	const float Rail = SideRailWidth();
	if(Rail > 0.0f)
	{
		const ImVec2 RailMin = ImGui::GetCursorScreenPos();
		RenderSideRail(RailMin, Io.DisplaySize.y - Margin + Slide - RailMin.y);
		ImGui::SetCursorScreenPos(ImVec2(RailMin.x + Rail + S(16.0f), RailMin.y));
	}
	RenderTopBar(Io.DisplaySize.x - Margin * 2.0f - (Rail > 0.0f ? Rail + S(16.0f) : 0.0f));

	// big rounded panel with the page
	ImGui::SetCursorPosY(ImGui::GetCursorPosY() + S(6.0f));
	const ImVec2 PanelMin = ImGui::GetCursorScreenPos();
	const ImVec2 PanelMax(Io.DisplaySize.x - Margin, Io.DisplaySize.y - Margin + Slide);
	PanelRect(ImGui::GetWindowDrawList(), PanelMin, PanelMax, S(18.0f), t.m_Panel);
	ImGui::SetCursorScreenPos(ImVec2(PanelMin.x + S(20.0f), PanelMin.y + S(20.0f)));
	// a new page or section: its cards appear one after another
	const int PageKey = m_Page * 1000 + (m_Page == PAGE_SETTINGS ? m_SettingsSection : m_Page == PAGE_VISUALS ? m_VisualsSection : 0);
	if(PageKey != m_LastPageKey || m_OpenAnimation < m_LastOpenAnimation - 0.5f || (m_LastOpenAnimation <= 0.0f && m_OpenAnimation > 0.0f))
	{
		m_LastPageKey = PageKey;
		PageEntered();
		// remembered for the next time the menu opens, also after a restart
		g_Config.m_XcMenuPage = m_Page;
		g_Config.m_XcMenuSettingsSection = m_SettingsSection;
		g_Config.m_XcMenuVisualsSection = m_VisualsSection;
	}
	m_LastOpenAnimation = m_OpenAnimation;
	ImGui::BeginChild("##page", ImVec2(PanelMax.x - PanelMin.x - S(40.0f), PanelMax.y - PanelMin.y - S(40.0f)), ImGuiChildFlags_None, ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
	switch(m_Page)
	{
	case PAGE_HOME: RenderHome(); break;
	case PAGE_SERVERS: RenderServers(); break;
	case PAGE_VISUALS: RenderVisuals(); break;
	case PAGE_CONFIGS: RenderConfigs(); break;
	case PAGE_DEMOS: RenderDemos(); break;
	case PAGE_VOTES: RenderVotes(); break;
	default: RenderSettings(); break;
	}
	ImGui::EndChild();

	ImGui::PopFont();
	ImGui::End();

	if(Io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_F, false))
		OpenSearch();
	RenderSearch();
	RenderConnectionPopup();
	RenderToasts(ImGui::GetForegroundDrawList(), Io.DisplaySize, Client()->GlobalTime(), Shown);
}

void CXcMenu::BeginSmoothScroll(const char *pId, ImVec2 Size)
{
	ImGui::BeginChild(pId, Size, ImGuiChildFlags_None, ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoScrollWithMouse);
	// the wheel sets a target, the view glides there
	const float MaxScroll = ImGui::GetScrollMaxY();
	const bool InnerWheel = m_InnerWheel;
	m_InnerWheel = false;
	if(ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows) && ImGui::GetIO().MouseWheel != 0.0f && !m_WheelCaptured && !InnerWheel)
		m_ScrollTarget -= ImGui::GetIO().MouseWheel * S(90.0f);
	m_ScrollTarget = std::clamp(m_ScrollTarget, 0.0f, MaxScroll);
	const float Scroll = Animate(ImGui::GetID("##scroll"), m_ScrollTarget, 14.0f);
	ImGui::SetScrollY(std::clamp(Scroll, 0.0f, MaxScroll));
}

void CXcMenu::EndSmoothScroll()
{
	ImGui::EndChild();
}

void CXcMenu::BeginInnerScroll(const char *pId, ImVec2 Size, int ExtraFlags)
{
	ImGui::BeginChild(pId, Size, ImGuiChildFlags_None, ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoScrollWithMouse | ExtraFlags);
	const ImGuiID Id = ImGui::GetID("##inner_scroll");
	float &Target = m_InnerScrollTargets.try_emplace(Id, ImGui::GetScrollY()).first->second;
	const float MaxScroll = ImGui::GetScrollMaxY();
	if(MaxScroll > 0.0f && ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows))
	{
		m_InnerWheel = true;
		if(ImGui::GetIO().MouseWheel != 0.0f)
			Target -= ImGui::GetIO().MouseWheel * S(60.0f);
	}
	Target = std::clamp(Target, 0.0f, MaxScroll);
	ImGui::SetScrollY(std::clamp(Animate(Id, Target, 16.0f), 0.0f, MaxScroll));
}

void CXcMenu::EndInnerScroll()
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
	pDraw->AddRectFilled(ImVec2(Center.x - (S(34.0f)), Center.y - (S(34.0f))), ImVec2(Center.x + (S(34.0f)), Center.y + (S(34.0f))), Col(t.m_Accent, 0.14f), S(14.0f));
	DrawIcon(pDraw, pIcons, pIcons->FontSize, Center, Col(t.m_Accent), pIcon);
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

void CXcMenu::TeePreviewCallback(const ImDrawList *pList, const ImDrawCmd *pCmd)
{
	(void)pList;
	const STeeDraw *pData = static_cast<const STeeDraw *>(pCmd->UserCallbackData);
	pData->m_pMenu->DrawTeePreview(*pData);
}

void CXcMenu::AddTeePreview(ImDrawList *pDraw, const ImVec2 &Min, const ImVec2 &Max, bool Still, int Who, const char *pSkin)
{
	STeeDraw Data = {this, {Min.x, Min.y, Max.x, Max.y}, Still, Who, ""};
	if(pSkin)
		str_copy(Data.m_aSkin, pSkin);
	pDraw->AddCallback(TeePreviewCallback, (void *)&Data, sizeof(Data));
	pDraw->AddCallback(ImDrawCallback_ResetRenderState, nullptr);
}

void CXcMenu::AddTeeSkin(ImDrawList *pDraw, const ImVec2 &Min, const ImVec2 &Max, const char *pSkin, bool CustomColors, int Body, int Feet)
{
	STeeDraw Data = {this, {Min.x, Min.y, Max.x, Max.y}, true, 0, ""};
	str_copy(Data.m_aSkin, pSkin);
	Data.m_ColorMode = CustomColors ? 1 : 0;
	Data.m_Body = Body;
	Data.m_Feet = Feet;
	pDraw->AddCallback(TeePreviewCallback, (void *)&Data, sizeof(Data));
	pDraw->AddCallback(ImDrawCallback_ResetRenderState, nullptr);
}

void CXcMenu::DrawTeePreview(const STeeDraw &Data)
{
	const float *pRect = Data.m_aRect;
	const bool Still = Data.m_Still;
	const vec2 Min(pRect[0], pRect[1]);
	const vec2 Max(pRect[2], pRect[3]);
	const vec2 Center = (Min + Max) * 0.5f;
	const bool Dummy = Data.m_Who == 1;
	CTeeRenderInfo Info;
	Info.Apply(GameClient()->m_Skins.Find(Data.m_aSkin[0] ? Data.m_aSkin : (Dummy ? g_Config.m_ClDummySkin : g_Config.m_ClPlayerSkin)));
	if(Data.m_ColorMode >= 0)
		Info.ApplyColors(Data.m_ColorMode == 1, Data.m_Body, Data.m_Feet);
	else if(Dummy)
		Info.ApplyColors(g_Config.m_ClDummyUseCustomColor, g_Config.m_ClDummyColorBody, g_Config.m_ClDummyColorFeet);
	else
		Info.ApplyColors(g_Config.m_ClPlayerUseCustomColor, g_Config.m_ClPlayerColorBody, g_Config.m_ClPlayerColorFeet);
	Info.m_Size = (Max.y - Min.y) * 0.62f;
	// the eyes follow the cursor
	const ImVec2 Mouse = ImGui::GetIO().MousePos;
	vec2 Dir = vec2(Mouse.x, Mouse.y) - Center;
	Dir = length(Dir) > 1.0f && !Still ? normalize(Dir) : vec2(1.0f, 0.0f);
	if(Data.m_Look != vec2(0.0f, 0.0f))
		Dir = normalize(Data.m_Look);
	const float Bob = Still || Data.m_Alpha >= 0.0f ? 0.0f : std::sin(Client()->GlobalTime() * 2.0f) * Info.m_Size * 0.03f;
	const float Alpha = Data.m_Alpha >= 0.0f ? Data.m_Alpha : ImGui::GetStyle().Alpha;
	// the tiny avatar in the top bar stays without the hat, it would stick out of the bar
	const bool Extras = !Dummy && !Data.m_aSkin[0];
	const bool Hat = Extras && g_Config.m_XcHat && g_Config.m_XcHatSelf && Max.y - Min.y > 64.0f;
	const bool Cloth = Extras && (g_Config.m_XcCapeSelf || g_Config.m_XcScarfSelf) && Max.y - Min.y > 64.0f;
	// the big preview of your own tee shows its neon outline and shader too (the render module captures the tee)
	XcRender::SGlowSettings GlowSettings;
	const bool Neon = Extras && Max.y - Min.y > 96.0f && GameClient()->m_TeeGlow.PreviewSettings(Info.m_Size / 64.0f, Alpha, GlowSettings);
	// the cape and the hat behind come first and stay as they are: the shader and the outline are only for the
	// tee itself (over the cape they made it a grey see-through sheet)
	if(Cloth)
		GameClient()->m_XcCape.RenderPreview(Center + vec2(0.0f, Bob), Info.m_Size, Dir.x < 0.0f, Alpha, Client()->GlobalTime());
	if(Hat && g_Config.m_XcHatBehind)
		GameClient()->m_XcHats.RenderOnTee(Center + vec2(0.0f, Bob), Info.m_Size, Dir.x < 0.0f, 0.0f, 0.0f, Alpha);
	if(Neon)
	{
		// on the stage only the crisp outline, no wide glow behind the tee
		GlowSettings.m_Strength = 0.0f;
		// the pattern stays on the tee, not on the screen
		GlowSettings.m_FillOriginX = Center.x;
		GlowSettings.m_FillOriginY = Center.y;
		const float Reach = Info.m_Size * 0.9f + GlowSettings.m_RimWidth * 4.0f;
		const float aRect[4] = {Center.x - Reach, Center.y - Reach, Center.x + Reach, Center.y + Reach};
		Graphics()->XcCaptureBegin(aRect);
	}
	RenderTools()->RenderTee(CAnimState::GetIdle(), &Info, EMOTE_NORMAL, Dir, Center + vec2(0.0f, Bob), Alpha);
	if(Neon)
		Graphics()->XcCaptureEnd(GlowSettings);
	if(Cloth && g_Config.m_XcScarfSelf)
		GameClient()->m_XcCape.RenderPreviewFront(Center + vec2(0.0f, Bob), Info.m_Size, Alpha);
	if(Hat && !g_Config.m_XcHatBehind)
		GameClient()->m_XcHats.RenderOnTee(Center + vec2(0.0f, Bob), Info.m_Size, Dir.x < 0.0f, 0.0f, 0.0f, Alpha);
	// the accessory (cone, halo, horns...) of the big preview, over the outline like in the game
	if(Extras && Max.y - Min.y > 96.0f && g_Config.m_XcAccessorySelf)
		GameClient()->m_Accessory.RenderPreview(Center + vec2(0.0f, Bob), Info.m_Size, Alpha, Client()->GlobalTime());
	Graphics()->WrapClamp();
}

bool CXcMenu::HomeTile(const char *pId, const char *pIconGlyph, const char *pLabel, const char *pNote, float Width, float Height, float Glow)
{
	const STheme &t = Theme();
	ImDrawList *pDraw = ImGui::GetWindowDrawList();
	const bool Disabled = ImGui::GetCurrentContext()->CurrentItemFlags & ImGuiItemFlags_Disabled;
	const ImVec2 Pos = ImGui::GetCursorScreenPos();
	const bool Clicked = ImGui::InvisibleButton(pId, ImVec2(Width, Height));
	const float Hover = Animate(ImGui::GetID(pId) + 1, ImGui::IsItemHovered() && !Disabled ? 1.0f : 0.0f, 16.0f);
	const float Fade = Disabled ? 0.45f : 1.0f;
	const float Lift = Hover * S(3.0f);
	const ImVec4 Accent(t.m_Accent.x, t.m_Accent.y, t.m_Accent.z, 1.0f);
	const ImVec4 Green(0.24f, 0.84f, 0.64f, 1.0f); // mint, "running"
	const ImVec2 Min(Pos.x, Pos.y - Lift);
	const ImVec2 Max(Pos.x + Width, Pos.y + Height - Lift);
	ImVec4 Fill = Mix(t.m_Track, Accent, 0.12f * Hover);
	Fill = Mix(Fill, Green, 0.14f * Glow);
	pDraw->AddRectFilled(Min, Max, Col(Fill, Fade), S(12.0f));
	pDraw->AddRect(Min, Max, Col(Mix(Mix(t.m_Border, t.m_Accent, Hover), Green, Glow), Fade), S(12.0f), 0, 1.0f + std::max(Hover, Glow));
	const ImVec4 IconColor = Mix(Mix(t.m_TextSecondary, t.m_Accent, Hover), Green, Glow);
	ImFont *pIcon = Fonts().m_pHeader;
	const float IconSize = S(26.0f);
	const float ContentHeight = IconSize + S(10.0f) + Fonts().m_pLabel->FontSize + (pNote ? S(4.0f) + Fonts().m_pSmall->FontSize : 0.0f);
	float y = (Min.y + Max.y - ContentHeight) * 0.5f;
	DrawIcon(pDraw, pIcon, IconSize, ImVec2((Min.x + Max.x) * 0.5f, y + IconSize * 0.5f), Col(IconColor, Fade), pIconGlyph);
	y += IconSize + S(10.0f);
	ImFont *pLabelFont = Fonts().m_pLabel;
	const ImVec2 LabelDim = pLabelFont->CalcTextSizeA(pLabelFont->FontSize, FLT_MAX, 0.0f, pLabel);
	pDraw->AddText(pLabelFont, pLabelFont->FontSize, ImVec2((Min.x + Max.x - LabelDim.x) * 0.5f, y), Col(t.m_Text, Fade), pLabel);
	if(pNote)
	{
		y += pLabelFont->FontSize + S(4.0f);
		ImFont *pSmall = Fonts().m_pSmall;
		const ImVec2 NoteDim = pSmall->CalcTextSizeA(pSmall->FontSize, FLT_MAX, 0.0f, pNote);
		pDraw->AddText(pSmall, pSmall->FontSize, ImVec2((Min.x + Max.x - NoteDim.x) * 0.5f, y), Col(Mix(t.m_TextMuted, Green, Glow), Fade), pNote);
	}
	if(Clicked)
		PlayUiSound(SOUND_CLICK);
	return Clicked;
}


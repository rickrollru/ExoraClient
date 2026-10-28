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
	m_Backend.MakeCurrent(); // the HUD has its own ImGui context

	const bool EscapePressed = (Event.m_Flags & IInput::FLAG_PRESS) && Event.m_Key == KEY_ESCAPE;
	if(!m_Active)
	{
		// in a demo Esc belongs to the demo player controls (pause, seek, exit)
		if(Client()->State() == IClient::STATE_DEMOPLAYBACK)
			return false;
		// Esc opens the new menu, unless the classic one is open in game (it closes itself then)
		if(EscapePressed && (!IsOnline() || !GameClient()->m_Menus.IsActive()))
		{
			// the menu opens where it was left last time (the map card stays on Home)
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
static void SoftCircle(ImDrawList *pDraw, ImVec2 Center, float Radius, ImVec4 Color)
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

static ImVec4 ShiftHue(const ImVec4 &Color, float Shift)
{
	float h, s, v;
	ImGui::ColorConvertRGBtoHSV(Color.x, Color.y, Color.z, h, s, v);
	h = std::fmod(h + Shift + 1.0f, 1.0f);
	ImVec4 Out(0, 0, 0, Color.w);
	ImGui::ColorConvertHSVtoRGB(h, s, v, Out.x, Out.y, Out.z);
	return Out;
}

// cheap stable pseudo random number in [0, 1) for particle i
static float Hash(int i, int Salt)
{
	unsigned x = (unsigned)i * 747796405u + (unsigned)Salt * 2891336453u;
	x = ((x >> ((x >> 28u) + 4u)) ^ x) * 277803737u;
	x = (x >> 22u) ^ x;
	return (x & 0xffffff) / (float)0x1000000;
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
		ImVec4 Color = ShiftHue(t.m_Accent, Glow.m_Hue);
		Color.w = BaseAlpha * Glow.m_Strength;
		SoftCircle(pDraw, Center, Size.y * Glow.m_Radius * Breath, Color);
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
		const float Speed = 0.012f + Hash(i, 1) * 0.02f; // screen heights per second
		const float Phase = std::fmod(Hash(i, 2) + Time * Speed, 1.0f);
		const float x = Size.x * (Hash(i, 3) + 0.015f * std::sin(Time * (0.3f + Hash(i, 4)) + i));
		const float y = Size.y * (1.05f - Phase * 1.1f);
		// fade in at the bottom, out at the top, and twinkle
		const float Fade = std::min(Phase * 6.0f, 1.0f) * std::min((1.0f - Phase) * 4.0f, 1.0f);
		const float Twinkle = 0.55f + 0.45f * std::sin(Time * (1.5f + Hash(i, 5) * 2.0f) + i * 1.7f);
		const float Radius = S(1.0f + Hash(i, 6) * 1.8f);
		const ImVec4 Color = Hash(i, 7) < 0.6f ? t.m_Accent : ShiftHue(t.m_Accent, 0.08f);
		const float SparkAlpha = Fade * Twinkle * Alpha * (Light ? 0.5f : 0.7f);
		SoftCircle(pDraw, ImVec2(x, y), Radius * 5.0f, ImVec4(Color.x, Color.y, Color.z, SparkAlpha * 0.25f));
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
	m_Backgrounds.Update(g_Config.m_XcMenuBackground == 3 ? g_Config.m_XcMenuBgFile : "", Dt, g_Config.m_XcMenuBgGifSpeed / 100.0f);
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

	ImDrawList *pBackground = ImGui::GetBackgroundDrawList();
	const int Background = EffectiveBackground();
	if(Background == BG_ANIMATED)
		RenderAnimatedBackground(pBackground, IsOnline() ? 0.94f * Open : 1.0f);
	else if(Background == BG_CUSTOM)
		pBackground->AddRectFilled(ImVec2(0, 0), Io.DisplaySize, Col(ImVec4(0, 0, 0, 1), g_Config.m_XcMenuBgDim / 100.0f * (IsOnline() ? Open : 1.0f)));
	else // darken the game or the map a little behind the menu
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
	RenderToasts(ImGui::GetForegroundDrawList(), Io.DisplaySize, Client()->GlobalTime(), Open);
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
	const float Bob = Still ? 0.0f : std::sin(Client()->GlobalTime() * 2.0f) * Info.m_Size * 0.03f;
	// the tiny avatar in the top bar stays without the hat, it would stick out of the bar
	const bool Extras = !Dummy && !Data.m_aSkin[0];
	const bool Hat = Extras && g_Config.m_XcHat && g_Config.m_XcHatSelf && Max.y - Min.y > 64.0f;
	const bool Cloth = Extras && (g_Config.m_XcCapeSelf || g_Config.m_XcScarfSelf) && Max.y - Min.y > 64.0f;
	if(Cloth)
		GameClient()->m_XcCape.RenderPreview(Center + vec2(0.0f, Bob), Info.m_Size, Dir.x < 0.0f, ImGui::GetStyle().Alpha, Client()->GlobalTime());
	if(Hat && g_Config.m_XcHatBehind)
		GameClient()->m_XcHats.RenderOnTee(Center + vec2(0.0f, Bob), Info.m_Size, Dir.x < 0.0f, 0.0f, 0.0f, ImGui::GetStyle().Alpha);
	RenderTools()->RenderTee(CAnimState::GetIdle(), &Info, EMOTE_NORMAL, Dir, Center + vec2(0.0f, Bob), ImGui::GetStyle().Alpha);
	if(Cloth && g_Config.m_XcScarfSelf)
		GameClient()->m_XcCape.RenderPreviewFront(Center + vec2(0.0f, Bob), Info.m_Size, ImGui::GetStyle().Alpha);
	if(Hat && !g_Config.m_XcHatBehind)
	{
		GameClient()->m_XcHats.RenderOnTee(Center + vec2(0.0f, Bob), Info.m_Size, Dir.x < 0.0f, 0.0f, 0.0f, ImGui::GetStyle().Alpha);
	}
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
	const ImVec2 IconDim = pIcon->CalcTextSizeA(IconSize, FLT_MAX, 0.0f, pIconGlyph);
	pDraw->AddText(pIcon, IconSize, ImVec2((Min.x + Max.x - IconDim.x) * 0.5f, y), Col(IconColor, Fade), pIconGlyph);
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

void CXcMenu::RenderHome()
{
	const STheme &t = Theme();
	if(!m_ServersRequested)
	{
		// the server list also tells which friends are online
		m_ServersRequested = true;
		ServerBrowser()->Refresh(IServerBrowser::TYPE_INTERNET);
	}

	BeginCardGrid("##home", 2);

	if(IsOnline() && g_Config.m_XcMenuMapInfo)
		RenderMapInfoCard();

	// profile with the tee
	if(BeginCard("##hero", Client()->PlayerName(), g_Config.m_PlayerClan[0] ? g_Config.m_PlayerClan : Tr("Profile")))
	{
		const float PreviewSize = S(112.0f);
		const ImVec2 Pos = ImGui::GetCursorScreenPos();
		ImDrawList *pDraw = ImGui::GetWindowDrawList();
		const ImVec2 Center(Pos.x + PreviewSize * 0.5f, Pos.y + PreviewSize * 0.5f);
		pDraw->AddCircleFilled(Center, PreviewSize * 0.46f, Col(t.m_Accent, 0.10f), 64);
		pDraw->AddCircle(Center, PreviewSize * 0.46f, Col(t.m_Accent, 0.35f), 64, S(1.5f));
		AddTeePreview(pDraw, Pos, ImVec2(Pos.x + PreviewSize, Pos.y + PreviewSize));
		ImGui::Dummy(ImVec2(PreviewSize, PreviewSize));

		ImGui::SameLine(0.0f, S(16.0f));
		ImGui::BeginGroup();
		ImGui::Dummy(ImVec2(0.0f, S(4.0f)));
		ImGui::PushFont(Fonts().m_pHeader);
		ImGui::TextColored(t.m_Text, "%s", Client()->PlayerName());
		ImGui::PopFont();
		char aStatus[160];
		if(IsOnline())
			str_format(aStatus, sizeof(aStatus), "%s: %s", Tr("Connected"), Client()->ServerInfo().m_aMap);
		else
			str_copy(aStatus, Tr("Offline"));
		Description(aStatus);
		ParamLabel(Tr("Skin"));
		ImGui::SameLine();
		ImGui::TextColored(t.m_Text, "%s", g_Config.m_ClPlayerSkin);
		ImGui::EndGroup();
		ImGui::Dummy(ImVec2(0.0f, S(6.0f)));
		RenderPlayerStats();

		ImGui::Dummy(ImVec2(0.0f, S(4.0f)));
		if(IsOnline())
		{
			if(Button(Tr("Disconnect"), false, -1.0f))
				Client()->Disconnect();
		}
		else if(Button(Tr("Play"), true, -1.0f))
		{
			m_Page = PAGE_SERVERS;
		}
		EndCard();
	}

	// the music right after the profile: the top of the second column, always in sight
	RenderMusicCard();

	// friends that are on a server right now
	if(BeginCard("##friends", Tr("Friends online"), nullptr))
	{
		int Shown = 0;
		IServerBrowser *pBrowser = ServerBrowser();
		for(int i = 0; i < pBrowser->NumServers() && Shown < 12; i++)
		{
			const CServerInfo *pInfo = pBrowser->Get(i);
			if(!pInfo || pInfo->m_FriendNum <= 0)
				continue;
			for(const CServerInfo::CClient &Player : pInfo->m_vClients)
			{
				if(Player.m_FriendState == IFriends::FRIEND_NO || Shown >= 12)
					continue;
				ImGui::PushFont(Fonts().m_pLabel);
				ImGui::TextColored(t.m_Accent, "%s", Player.m_aName);
				ImGui::PopFont();
				ImGui::SameLine();
				ImGui::PushFont(Fonts().m_pSmall);
				ImGui::TextColored(t.m_TextMuted, "%s", pInfo->m_aName);
				ImGui::PopFont();
				Shown++;
			}
		}
		if(Shown == 0)
			Description(GameClient()->Friends()->NumFriends() == 0 ? Tr("Add friends in the scoreboard or the classic menu") : Tr("None of your friends is online"));
		EndCard();
	}

	// map editor and the local server
	if(BeginCard("##tools", Tr("Tools"), Tr("Make your own maps and play them on a server right on your computer")))
	{
		const bool Running = GameClient()->m_LocalServer.IsServerRunning();
		const bool OnLocal = Running && IsOnline() && (str_startswith(Client()->ConnectAddressString(), "localhost") || str_startswith(Client()->ConnectAddressString(), "127.0.0.1"));
		const float Gap = S(10.0f);
		const float TileWidth = (AvailWidth() - Gap * 2.0f) / 3.0f;
		const float TileHeight = S(78.0f);
		const float RunGlow = Animate(ImGui::GetID("##server_glow"), Running ? 1.0f : 0.0f, 8.0f);
		if(HomeTile("##editor", XC_ICON_MAP, Tr("Map editor"), GameClient()->Editor()->HasUnsavedData() ? Tr("Unsaved changes") : Tr("Open"), TileWidth, TileHeight))
		{
			g_Config.m_ClEditor = 1;
			Input()->MouseModeRelative();
		}
		ImGui::SameLine(0.0f, Gap);
		if(HomeTile("##server", XC_ICON_SERVER, Tr("Local server"), Running ? Tr("Running, click to stop") : Tr("Start"), TileWidth, TileHeight, RunGlow))
		{
			if(Running)
			{
				GameClient()->m_LocalServer.KillServer();
				Toast(Tr("Local server stopped"), TOAST_INFO);
			}
			else if(GameClient()->m_LocalServer.RunServer({}))
				Toast(Tr("Local server started"));
			else
				Toast(Tr("Could not start the server"), TOAST_ERROR);
		}
		ImGui::SameLine(0.0f, Gap);
		ImGui::BeginDisabled(!Running || OnLocal);
		if(HomeTile("##join", XC_ICON_PLAY, Tr("Join"), OnLocal ? Tr("You are here") : (Running ? Tr("Your local server") : Tr("Start the server first")), TileWidth, TileHeight))
			Client()->Connect("localhost:8303");
		ImGui::EndDisabled();
		EndCard();
	}

	// versions and the way back to the classic menu
	if(BeginCard("##client", XC_CLIENT_NAME, Tr("Based on TClient and DDNet. The developer on Discord: rickroll.ru")))
	{
		// links of the project; empty links are placeholders for now
		{
			struct SLink
			{
				const char *m_pIcon;
				const char *m_pLabel;
				const char *m_pUrl;
				const char *m_pCopy; // copied to the clipboard instead of opened
			};
			const SLink aLinks[] = {
				{XC_ICON_CODE, "GitHub", g_Config.m_XcLinkGithub, nullptr},
				{XC_ICON_COMMENTS, "Discord", g_Config.m_XcLinkDiscord, nullptr},
				{XC_ICON_USER, "rickroll.ru", "", "rickroll.ru"},
			};
			const float Gap = S(10.0f);
			const float TileWidth = (AvailWidth() - Gap * 2.0f) / 3.0f;
			for(int i = 0; i < 3; i++)
			{
				const SLink &Link = aLinks[i];
				if(i > 0)
					ImGui::SameLine(0.0f, Gap);
				char aId[16];
				str_format(aId, sizeof(aId), "##link%d", i);
				if(HomeTile(aId, Link.m_pIcon, Link.m_pLabel, nullptr, TileWidth, S(64.0f)))
				{
					if(Link.m_pCopy)
					{
						Input()->SetClipboardText(Link.m_pCopy);
						Toast(Tr("Discord name copied"));
					}
					else if(Link.m_pUrl[0])
						Client()->ViewLink(Link.m_pUrl);
					else
						Toast(Tr("Coming soon"), TOAST_INFO);
				}
			}
			ImGui::Dummy(ImVec2(0.0f, S(4.0f)));
		}
		char aBuf[128];
		const auto VersionRow = [&](const char *pName, const char *pVersion) {
			const float StartX = ImGui::GetCursorPosX();
			const float Width = AvailWidth();
			ImGui::AlignTextToFramePadding();
			ParamLabel(pName);
			ImGui::SameLine();
			ImGui::SetCursorPosX(StartX + Width - ImGui::CalcTextSize(pVersion).x);
			ImGui::AlignTextToFramePadding();
			ImGui::TextColored(t.m_Text, "%s", pVersion);
		};
		VersionRow("DDNet", GAME_RELEASE_VERSION);
		VersionRow("TClient", TCLIENT_VERSION);
		const char *pLatest = Client()->LatestVersion();
		if(pLatest[0] && str_comp(pLatest, "0") != 0 && str_comp(pLatest, GAME_RELEASE_VERSION) != 0)
		{
			str_format(aBuf, sizeof(aBuf), "%s: DDNet %s", Tr("Update available"), pLatest);
			Description(aBuf);
		}
		if(Button(Tr("Classic menu"), false, -1.0f))
			m_WantClassic = true;
		EndCard();
	}
	// DDNet news: lines between | are headlines
	if(BeginCard("##news", Tr("DDNet news"), nullptr))
	{
		const char *pNews = Client()->News();
		if(!pNews[0])
			Description(Tr("No news loaded"));
		BeginInnerScroll("##news_text", ImVec2(0.0f, S(140.0f)));
		char aLine[256];
		const char *pStr = pNews;
		while((pStr = str_next_token(pStr, "\n", aLine, sizeof(aLine))))
		{
			const int Length = str_length(aLine);
			if(Length > 1 && aLine[0] == '|' && aLine[Length - 1] == '|')
			{
				aLine[Length - 1] = '\0';
				ImGui::Dummy(ImVec2(0.0f, S(4.0f)));
				ImGui::PushFont(Fonts().m_pLabel);
				ImGui::TextColored(t.m_Text, "%s", aLine + 1);
				ImGui::PopFont();
			}
			else
			{
				ImGui::PushFont(Fonts().m_pSmall);
				ImGui::PushTextWrapPos(0.0f);
				ImGui::TextColored(t.m_TextSecondary, "%s", aLine);
				ImGui::PopTextWrapPos();
				ImGui::PopFont();
			}
		}
		EndInnerScroll();
		EndCard();
	}

	EndCardGrid();

	if(m_WantClassic)
	{
		m_WantClassic = false;
		OpenClassicMenu();
	}
}

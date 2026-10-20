#include "ambience.h"

#include "sprite_fx.h"
#include "xc_helpers.h"

#include <base/math.h>

#include <engine/graphics.h>
#include <engine/shared/config.h>

#include <game/client/gameclient.h>

#include <algorithm>
#include <cmath>
#include <ctime>

using namespace XcHelpers;

// the light of the four times of day; the hours between are blended
static const CXcAmbience::SPalette s_Night = {
	ColorRGBA(0.04f, 0.07f, 0.2f, 0.4f),
	ColorRGBA(0.15f, 0.22f, 0.5f, 0.12f),
	ColorRGBA(0.1f, 0.12f, 0.3f, 0.06f),
	ColorRGBA(0.6f, 0.72f, 1.0f, 0.5f),
	ColorRGBA(0.22f, 0.27f, 0.42f, 1.0f),
	0.2f};
static const CXcAmbience::SPalette s_Dawn = {
	ColorRGBA(0.85f, 0.45f, 0.6f, 0.12f),
	ColorRGBA(0.6f, 0.45f, 0.8f, 0.1f),
	ColorRGBA(1.0f, 0.55f, 0.45f, 0.2f),
	ColorRGBA(1.0f, 0.72f, 0.58f, 0.8f),
	ColorRGBA(0.95f, 0.78f, 0.82f, 1.0f),
	0.1f};
static const CXcAmbience::SPalette s_Day = {
	ColorRGBA(1.0f, 0.97f, 0.9f, 0.0f),
	ColorRGBA(0.5f, 0.75f, 1.0f, 0.06f),
	ColorRGBA(1.0f, 0.95f, 0.8f, 0.05f),
	ColorRGBA(1.0f, 0.96f, 0.82f, 0.7f),
	ColorRGBA(0.82f, 0.88f, 0.95f, 1.0f),
	0.65f};
static const CXcAmbience::SPalette s_Sunset = {
	ColorRGBA(1.0f, 0.42f, 0.18f, 0.16f),
	ColorRGBA(0.75f, 0.35f, 0.5f, 0.1f),
	ColorRGBA(1.0f, 0.48f, 0.2f, 0.24f),
	ColorRGBA(1.0f, 0.6f, 0.3f, 0.9f),
	ColorRGBA(0.95f, 0.64f, 0.5f, 1.0f),
	0.9f};

static ColorRGBA MixColor(const ColorRGBA &a, const ColorRGBA &b, float t)
{
	return ColorRGBA(mix(a.r, b.r, t), mix(a.g, b.g, t), mix(a.b, b.b, t), mix(a.a, b.a, t));
}

static CXcAmbience::SPalette MixPalette(const CXcAmbience::SPalette &a, const CXcAmbience::SPalette &b, float t)
{
	t = t * t * (3.0f - 2.0f * t);
	return {MixColor(a.m_Overlay, b.m_Overlay, t), MixColor(a.m_Sky, b.m_Sky, t), MixColor(a.m_Horizon, b.m_Horizon, t), MixColor(a.m_Rays, b.m_Rays, t), MixColor(a.m_Fog, b.m_Fog, t), mix(a.m_RaysX, b.m_RaysX, t)};
}

CXcAmbience::SPalette CXcAmbience::PaletteAt(float Hour)
{
	struct SKey
	{
		float m_Hour;
		const SPalette *m_pPalette;
	};
	static const SKey s_aKeys[] = {{0.0f, &s_Night}, {4.5f, &s_Night}, {6.5f, &s_Dawn}, {8.5f, &s_Day}, {16.5f, &s_Day}, {18.5f, &s_Sunset}, {20.5f, &s_Night}, {24.0f, &s_Night}};
	Hour = std::fmod(std::fmod(Hour, 24.0f) + 24.0f, 24.0f);
	for(size_t i = 0; i + 1 < std::size(s_aKeys); i++)
	{
		if(Hour <= s_aKeys[i + 1].m_Hour)
		{
			const float t = (Hour - s_aKeys[i].m_Hour) / std::max(s_aKeys[i + 1].m_Hour - s_aKeys[i].m_Hour, 0.001f);
			return MixPalette(*s_aKeys[i].m_pPalette, *s_aKeys[i + 1].m_pPalette, std::clamp(t, 0.0f, 1.0f));
		}
	}
	return s_Night;
}

float CXcAmbience::CurrentHour() const
{
	switch(g_Config.m_XcAmbienceTime)
	{
	case TIME_DAWN: return 6.8f;
	case TIME_DAY: return 12.0f;
	case TIME_SUNSET: return 18.6f;
	case TIME_NIGHT: return 0.0f;
	case TIME_HOUR: return (float)g_Config.m_XcAmbienceHour;
	default:
	{
		const time_t Now = ::time(nullptr);
		tm Local;
#if defined(CONF_FAMILY_WINDOWS)
		localtime_s(&Local, &Now);
#else
		localtime_r(&Now, &Local);
#endif
		return Local.tm_hour + Local.tm_min / 60.0f;
	}
	}
}

void CXcAmbience::OnRender()
{
	if(Client()->State() != IClient::STATE_ONLINE && Client()->State() != IClient::STATE_DEMOPLAYBACK)
		return;
	if(!g_Config.m_XcAmbience)
		return;
	const float Width = (float)Graphics()->ScreenWidth();
	const float Height = (float)Graphics()->ScreenHeight();
	const float Strength = g_Config.m_XcAmbienceStrength / 100.0f;
	SPalette Palette = PaletteAt(CurrentHour());
	if(g_Config.m_XcAmbienceFogOwn)
	{
		const ColorRGBA Own = EffectColor(GameClient(), g_Config.m_XcAmbienceFogColorMode, g_Config.m_XcAmbienceFogColor, -1);
		Palette.m_Fog = Own.WithAlpha(1.0f);
	}

	const CScreenRect SavedScreen = Graphics()->GetScreen();
	Graphics()->MapScreen(CScreenRect(0.0f, 0.0f, Width, Height));
	Graphics()->TextureClear();

	// the light of the time of day: a thin layer of color, a glow from the sky and from the horizon
	if(Strength > 0.0f)
	{
		Graphics()->QuadsBegin();
		Graphics()->SetColor(Palette.m_Overlay.WithMultipliedAlpha(Strength));
		const IGraphics::CQuadItem Item(0.0f, 0.0f, Width, Height);
		Graphics()->QuadsDrawTL(&Item, 1);
		Graphics()->QuadsEnd();

		Graphics()->BlendAdditive();
		Graphics()->QuadsBegin();
		const ColorRGBA Sky = Palette.m_Sky.WithMultipliedAlpha(Strength);
		const ColorRGBA Horizon = Palette.m_Horizon.WithMultipliedAlpha(Strength);
		const ColorRGBA Clear(0.0f, 0.0f, 0.0f, 0.0f);
		Graphics()->SetColor4(Sky, Sky, Clear, Clear);
		const IGraphics::CFreeformItem Top(0.0f, 0.0f, Width, 0.0f, 0.0f, Height * 0.55f, Width, Height * 0.55f);
		Graphics()->QuadsDrawFreeform(&Top, 1);
		Graphics()->SetColor4(Clear, Clear, Horizon, Horizon);
		const IGraphics::CFreeformItem Bottom(0.0f, Height * 0.45f, Width, Height * 0.45f, 0.0f, Height, Width, Height);
		Graphics()->QuadsDrawFreeform(&Bottom, 1);
		Graphics()->QuadsEnd();
		Graphics()->BlendNormal();
	}
	if(g_Config.m_XcAmbienceRays > 0)
		RenderRays(Width, Height, Palette, g_Config.m_XcAmbienceRays / 100.0f);
	if(g_Config.m_XcAmbienceFog > 0)
		RenderFog(Width, Height, Palette, g_Config.m_XcAmbienceFog / 100.0f);
	if(g_Config.m_XcAmbienceMist > 0)
		RenderMist(Width, Height, Palette, g_Config.m_XcAmbienceMist / 100.0f);
	Graphics()->MapScreen(SavedScreen);
}

void CXcAmbience::RenderRays(float Width, float Height, const SPalette &Palette, float Strength)
{
	// long soft beams from a point above the screen that slowly sway and breathe
	const float Time = Client()->GlobalTime();
	const vec2 Origin(Width * Palette.m_RaysX, -Height * 0.25f);
	const vec2 Down = normalize(vec2(Width * 0.5f, Height) - Origin);
	const float BaseAngle = std::atan2(Down.y, Down.x);
	const ColorRGBA Color = Palette.m_Rays;
	const ColorRGBA Clear(Color.r, Color.g, Color.b, 0.0f);
	const float Length = std::sqrt(Width * Width + Height * Height) * 1.1f;
	Graphics()->BlendAdditive();
	Graphics()->TextureClear();
	Graphics()->QuadsBegin();
	constexpr int NUM_RAYS = 9;
	for(int i = 0; i < NUM_RAYS; i++)
	{
		const float Seed = i * 1.618f;
		const float Spread = (i / (float)(NUM_RAYS - 1) - 0.5f) * 0.9f;
		const float Angle = BaseAngle + Spread + 0.03f * std::sin(Time * 0.15f + Seed * 3.0f);
		const float HalfWidth = (0.025f + 0.02f * std::fmod(Seed * 7.3f, 1.0f)) * (0.8f + 0.2f * std::sin(Time * 0.3f + Seed));
		const float Breath = 0.55f + 0.45f * std::sin(Time * 0.4f + Seed * 2.0f);
		const float a = Color.a * Strength * 0.11f * Breath;
		const vec2 L = direction(Angle - HalfWidth) * Length;
		const vec2 R = direction(Angle + HalfWidth) * Length;
		// bright near the sky, fading to nothing down the screen
		Graphics()->SetColor4(Color.WithAlpha(a), Color.WithAlpha(a), Clear, Clear);
		const IGraphics::CFreeformItem Item(Origin, Origin + (R - L) * 0.02f, Origin + L, Origin + R);
		Graphics()->QuadsDrawFreeform(&Item, 1);
	}
	Graphics()->QuadsEnd();
	Graphics()->BlendNormal();
}

void CXcAmbience::RenderFog(float Width, float Height, const SPalette &Palette, float Density)
{
	// a haze over everything and thick fog rising from the bottom of the screen
	const ColorRGBA Fog = Palette.m_Fog;
	const ColorRGBA Clear = Fog.WithAlpha(0.0f);
	const float Start = 1.0f - g_Config.m_XcAmbienceFogHeight / 100.0f;
	Graphics()->TextureClear();
	Graphics()->QuadsBegin();
	Graphics()->SetColor(Fog.WithAlpha(0.12f * Density));
	const IGraphics::CQuadItem Haze(0.0f, 0.0f, Width, Height);
	Graphics()->QuadsDrawTL(&Haze, 1);
	const ColorRGBA Thick = Fog.WithAlpha(0.6f * Density);
	Graphics()->SetColor4(Clear, Clear, Thick, Thick);
	const IGraphics::CFreeformItem Bottom(0.0f, Height * Start, Width, Height * Start, 0.0f, Height, Width, Height);
	Graphics()->QuadsDrawFreeform(&Bottom, 1);
	Graphics()->QuadsEnd();
}

void CXcAmbience::RenderMist(float Width, float Height, const SPalette &Palette, float Amount)
{
	CXcSprites &Sprites = GameClient()->m_XcSprites;
	if(!Sprites.Loaded())
		return;
	if(!m_MistReady)
	{
		for(SMist &Mist : m_aMist)
		{
			Mist.m_Pos = vec2(random_float(), 0.35f + 0.65f * random_float());
			Mist.m_Size = 0.25f + 0.3f * random_float();
			Mist.m_Speed = 0.004f + 0.01f * random_float();
			Mist.m_Phase = random_float() * 10.0f;
		}
		m_MistReady = true;
	}
	// wide soft clouds that drift sideways and move a little with the camera, the lower the thicker
	const float Time = Client()->GlobalTime();
	const vec2 Camera = GameClient()->m_Camera.m_Center;
	const int Count = std::clamp((int)(std::size(m_aMist) * Amount) + 4, 4, (int)std::size(m_aMist));
	Sprites.Begin(CXcSprites::SPRITE_SOFT_BLOOM, false);
	for(int i = 0; i < Count; i++)
	{
		const SMist &Mist = m_aMist[i];
		const float Parallax = 0.15f + Mist.m_Size * 0.3f;
		float x = Mist.m_Pos.x + Time * Mist.m_Speed - Camera.x * Parallax / std::max(Width, 1.0f) * 0.5f;
		x = std::fmod(std::fmod(x, 1.4f) + 1.4f, 1.4f) - 0.2f;
		const float y = Mist.m_Pos.y + 0.02f * std::sin(Time * 0.2f + Mist.m_Phase);
		const float a = (0.05f + 0.1f * Mist.m_Pos.y) * Amount * (0.7f + 0.3f * std::sin(Time * 0.3f + Mist.m_Phase));
		Sprites.Draw(vec2(x * Width, y * Height), Mist.m_Size * Width, 0.0f, Palette.m_Fog.WithAlpha(a));
	}
	Sprites.End();
	Graphics()->TextureClear();
}

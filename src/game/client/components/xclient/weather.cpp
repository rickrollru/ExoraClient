#include "weather.h"

#include "fx_renderer.h"
#include "sprite_fx.h"
#include "xc_helpers.h"

#include <base/math.h>

#include <engine/client.h>
#include <engine/graphics.h>
#include <engine/shared/config.h>

#include <engine/engine.h>

#include <game/client/gameclient.h>

#include <cmath>

static constexpr float VIRTUAL_HEIGHT = 600.0f;
static constexpr int MAX_PARTICLES_PER_DENSITY = 6;

static const ColorRGBA s_aLeafColors[] = {
	ColorRGBA(0.95f, 0.55f, 0.15f, 1.0f),
	ColorRGBA(0.85f, 0.30f, 0.10f, 1.0f),
	ColorRGBA(0.95f, 0.75f, 0.20f, 1.0f),
	ColorRGBA(0.60f, 0.35f, 0.15f, 1.0f),
};

static const ColorRGBA s_aSakuraColors[] = {
	ColorRGBA(1.00f, 0.72f, 0.77f, 1.0f),
	ColorRGBA(1.00f, 0.62f, 0.71f, 1.0f),
	ColorRGBA(1.00f, 0.82f, 0.86f, 1.0f),
	ColorRGBA(0.98f, 0.55f, 0.68f, 1.0f),
};

static const ColorRGBA s_aHeartColors[] = {
	ColorRGBA(1.00f, 0.36f, 0.54f, 1.0f),
	ColorRGBA(1.00f, 0.25f, 0.35f, 1.0f),
	ColorRGBA(1.00f, 0.60f, 0.75f, 1.0f),
	ColorRGBA(0.90f, 0.20f, 0.50f, 1.0f),
};

static const ColorRGBA s_aStarColors[] = {
	ColorRGBA(1.00f, 0.92f, 0.55f, 1.0f),
	ColorRGBA(1.00f, 1.00f, 1.00f, 1.0f),
	ColorRGBA(0.70f, 0.85f, 1.00f, 1.0f),
};

static const ColorRGBA s_aCrystalColors[] = {
	ColorRGBA(0.45f, 0.95f, 1.00f, 1.0f),
	ColorRGBA(0.70f, 0.55f, 1.00f, 1.0f),
	ColorRGBA(1.00f, 0.55f, 0.95f, 1.0f),
	ColorRGBA(0.55f, 0.75f, 1.00f, 1.0f),
};

static const ColorRGBA s_aEmberColors[] = {
	ColorRGBA(1.00f, 0.55f, 0.15f, 1.0f),
	ColorRGBA(1.00f, 0.35f, 0.10f, 1.0f),
	ColorRGBA(1.00f, 0.80f, 0.30f, 1.0f),
};

static const ColorRGBA s_aConfettiColors[] = {
	ColorRGBA(0.31f, 0.55f, 1.00f, 1.0f),
	ColorRGBA(0.24f, 0.84f, 0.64f, 1.0f),
	ColorRGBA(1.00f, 0.54f, 0.24f, 1.0f),
	ColorRGBA(1.00f, 0.36f, 0.54f, 1.0f),
	ColorRGBA(0.71f, 0.91f, 0.24f, 1.0f),
	ColorRGBA(1.00f, 0.85f, 0.30f, 1.0f),
};

template<typename T, size_t N>
static const T &RandomPick(const T (&aItems)[N])
{
	return aItems[std::min((size_t)(random_float() * N), N - 1)];
}

CWeather::CWeather() :
	m_Images(FOLDER, 512, 256, false)
{
}

void CWeather::OnInit()
{
	m_Images.Init(Graphics(), Storage(), Kernel()->RequestInterface<IEngine>());
}

void CWeather::OnShutdown()
{
	m_Images.Shutdown();
}

bool CWeather::IconWeather() const
{
	return m_Type >= WEATHER_HEARTS && m_Type <= WEATHER_EMBERS;
}

void CWeather::OnReset()
{
	m_HasLastCamera = false;
}

void CWeather::OnRender()
{
	if(Client()->State() != IClient::STATE_ONLINE && Client()->State() != IClient::STATE_DEMOPLAYBACK)
		return;
	if(g_Config.m_XcWeather == WEATHER_OFF || !g_Config.m_XcWeatherInGame)
	{
		m_HasLastCamera = false;
		return;
	}

	// move the particles with the camera, so the weather feels like part of the world
	const vec2 Camera = GameClient()->m_Camera.m_Center;
	vec2 Shift = vec2(0.0f, 0.0f);
	if(m_HasLastCamera)
	{
		float WorldWidth, WorldHeight;
		Graphics()->CalcScreenParams(Graphics()->ScreenAspect(), GameClient()->m_Camera.m_Zoom, &WorldWidth, &WorldHeight);
		const vec2 Delta = Camera - m_LastCamera;
		// ignore teleports and spectator jumps
		if(WorldHeight > 0.0f && length(Delta) < WorldHeight * 0.5f)
			Shift = -Delta * (VIRTUAL_HEIGHT / WorldHeight);
	}
	m_LastCamera = Camera;
	m_HasLastCamera = true;

	const CScreenRect SavedScreen = Graphics()->GetScreen();
	Step(Shift);
	Draw();
	Graphics()->MapScreen(SavedScreen);
}

void CWeather::RenderMenu()
{
	m_HasLastCamera = false;
	if(g_Config.m_XcWeather == WEATHER_OFF || !g_Config.m_XcWeatherInMenu)
		return;

	const CScreenRect SavedScreen = Graphics()->GetScreen();
	Step(vec2(0.0f, 0.0f));
	Draw();
	Graphics()->MapScreen(SavedScreen);
}

void CWeather::Spawn(SParticle &Particle, bool Anywhere) const
{
	const float SizeScale = g_Config.m_XcWeatherSize / 100.0f;
	// more far away particles than close ones
	const float r = random_float();
	const float Depth = 0.3f + 0.7f * r * r;

	Particle.m_Depth = Depth;
	Particle.m_Rot = random_float(0.0f, 2.0f * pi);
	Particle.m_SwayPhase = random_float(0.0f, 2.0f * pi);
	Particle.m_Flicker = 0.0f;
	Particle.m_Sprite = false;
	Particle.m_Stretch = 1.0f;
	Particle.m_Icon = -1;
	Particle.m_Glowing = false;
	Particle.m_Hue = random_float();

	switch(m_Type)
	{
	case WEATHER_SNOW:
		Particle.m_Size = mix(3.0f, 13.0f, Depth) * random_float(0.8f, 1.2f);
		Particle.m_FallSpeed = mix(25.0f, 85.0f, Depth) * random_float(0.85f, 1.15f);
		Particle.m_SwayAmount = random_float(5.0f, 18.0f);
		Particle.m_SwaySpeed = random_float(0.4f, 1.2f);
		Particle.m_RotSpeed = random_float(-0.6f, 0.6f);
		Particle.m_Sprite = Depth > 0.6f;
		Particle.m_Color = ColorRGBA(1.0f, 1.0f, 1.0f, mix(0.45f, 1.0f, Depth));
		break;
	case WEATHER_LEAVES:
		Particle.m_Size = mix(7.0f, 18.0f, Depth) * random_float(0.8f, 1.2f);
		Particle.m_Stretch = 0.55f;
		Particle.m_FallSpeed = mix(35.0f, 80.0f, Depth) * random_float(0.85f, 1.15f);
		Particle.m_SwayAmount = random_float(20.0f, 45.0f);
		Particle.m_SwaySpeed = random_float(0.6f, 1.4f);
		Particle.m_RotSpeed = random_float(-2.5f, 2.5f);
		Particle.m_Color = RandomPick(s_aLeafColors).WithAlpha(mix(0.55f, 1.0f, Depth));
		break;
	case WEATHER_SAKURA:
		Particle.m_Size = mix(5.0f, 12.0f, Depth) * random_float(0.8f, 1.2f);
		Particle.m_Stretch = 0.7f;
		Particle.m_FallSpeed = mix(20.0f, 55.0f, Depth) * random_float(0.85f, 1.15f);
		Particle.m_SwayAmount = random_float(15.0f, 35.0f);
		Particle.m_SwaySpeed = random_float(0.8f, 1.8f);
		Particle.m_RotSpeed = random_float(-3.0f, 3.0f);
		Particle.m_Color = RandomPick(s_aSakuraColors).WithAlpha(mix(0.55f, 1.0f, Depth));
		break;
	case WEATHER_HEARTS:
	case WEATHER_STARS:
	case WEATHER_CRYSTALS:
	case WEATHER_MONEY:
	case WEATHER_HALLOWEEN:
	case WEATHER_CUSTOM:
		// pictures that float down, sway and turn
		Particle.m_Size = mix(7.0f, 19.0f, Depth) * random_float(0.85f, 1.15f);
		Particle.m_FallSpeed = mix(22.0f, 60.0f, Depth) * random_float(0.85f, 1.15f);
		Particle.m_SwayAmount = random_float(10.0f, 28.0f);
		Particle.m_SwaySpeed = random_float(0.5f, 1.3f);
		Particle.m_RotSpeed = random_float(-1.2f, 1.2f);
		Particle.m_Color = ColorRGBA(1.0f, 1.0f, 1.0f, mix(0.55f, 1.0f, Depth));
		if(m_Type == WEATHER_HEARTS)
		{
			Particle.m_Icon = CXcSprites::SPRITE_HEART;
			Particle.m_Color = RandomPick(s_aHeartColors).WithAlpha(Particle.m_Color.a);
			Particle.m_RotSpeed *= 0.4f;
		}
		else if(m_Type == WEATHER_STARS)
		{
			Particle.m_Icon = CXcSprites::SPRITE_STAR;
			Particle.m_Color = RandomPick(s_aStarColors).WithAlpha(Particle.m_Color.a);
			Particle.m_Glowing = true;
			Particle.m_Flicker = random_float(2.0f, 5.0f);
		}
		else if(m_Type == WEATHER_CRYSTALS)
		{
			Particle.m_Icon = CXcSprites::SPRITE_RHOMBUS;
			Particle.m_Color = RandomPick(s_aCrystalColors).WithAlpha(Particle.m_Color.a);
			Particle.m_Glowing = true;
			Particle.m_RotSpeed *= 2.0f;
		}
		else if(m_Type == WEATHER_MONEY)
		{
			Particle.m_Icon = CXcSprites::SPRITE_DOLLAR;
			Particle.m_Color = ColorRGBA(0.45f, random_float(0.85f, 1.0f), 0.45f, Particle.m_Color.a);
			Particle.m_SwayAmount *= 1.5f;
		}
		else if(m_Type == WEATHER_HALLOWEEN)
		{
			const bool Ghost = random_float() < 0.4f;
			Particle.m_Icon = Ghost ? CXcSprites::SPRITE_GHOST : CXcSprites::SPRITE_PUMPKIN;
			Particle.m_Color = Ghost ? ColorRGBA(0.92f, 0.95f, 1.0f, Particle.m_Color.a * 0.85f) : ColorRGBA(1.0f, 0.55f, 0.12f, Particle.m_Color.a);
			Particle.m_RotSpeed *= 0.3f;
		}
		else
		{
			// own pictures stay bigger, they usually have more details
			Particle.m_Size *= 1.6f;
			Particle.m_RotSpeed *= 0.6f;
		}
		break;
	case WEATHER_FIREFLIES:
		// little lights rising slowly and blinking
		Particle.m_Icon = CXcSprites::SPRITE_FIREFLY;
		Particle.m_Size = mix(5.0f, 12.0f, Depth) * random_float(0.8f, 1.2f);
		Particle.m_FallSpeed = -mix(6.0f, 22.0f, Depth) * random_float(0.7f, 1.3f);
		Particle.m_SwayAmount = random_float(12.0f, 30.0f);
		Particle.m_SwaySpeed = random_float(0.3f, 0.9f);
		Particle.m_RotSpeed = 0.0f;
		Particle.m_Glowing = true;
		Particle.m_Flicker = random_float(1.5f, 4.0f);
		Particle.m_Color = ColorRGBA(random_float(0.75f, 0.95f), 1.0f, random_float(0.35f, 0.55f), mix(0.6f, 1.0f, Depth));
		break;
	case WEATHER_EMBERS:
		// sparks rising from below
		Particle.m_Icon = CXcSprites::SPRITE_FIREPART;
		Particle.m_Size = mix(4.0f, 11.0f, Depth) * random_float(0.8f, 1.2f);
		Particle.m_FallSpeed = -mix(25.0f, 70.0f, Depth) * random_float(0.8f, 1.2f);
		Particle.m_SwayAmount = random_float(8.0f, 22.0f);
		Particle.m_SwaySpeed = random_float(0.8f, 2.0f);
		Particle.m_RotSpeed = random_float(-2.0f, 2.0f);
		Particle.m_Glowing = true;
		Particle.m_Flicker = random_float(4.0f, 9.0f);
		Particle.m_Color = RandomPick(s_aEmberColors).WithAlpha(mix(0.6f, 1.0f, Depth));
		break;
	case WEATHER_RAIN:
		Particle.m_Size = mix(10.0f, 26.0f, Depth); // length of the streak
		Particle.m_FallSpeed = mix(320.0f, 560.0f, Depth) * random_float(0.9f, 1.1f);
		Particle.m_SwayAmount = 0.0f;
		Particle.m_SwaySpeed = 0.0f;
		Particle.m_RotSpeed = 0.0f;
		Particle.m_Color = ColorRGBA(0.72f, 0.82f, 1.0f, mix(0.25f, 0.6f, Depth));
		break;
	case WEATHER_BUBBLES:
		Particle.m_Size = mix(5.0f, 16.0f, Depth) * random_float(0.8f, 1.2f);
		Particle.m_FallSpeed = -mix(12.0f, 35.0f, Depth) * random_float(0.8f, 1.2f);
		Particle.m_SwayAmount = random_float(6.0f, 16.0f);
		Particle.m_SwaySpeed = random_float(0.8f, 1.8f);
		Particle.m_RotSpeed = 0.0f;
		Particle.m_Color = ColorRGBA(0.75f, 0.92f, 1.0f, mix(0.45f, 0.9f, Depth));
		break;
	case WEATHER_CONFETTI:
		Particle.m_Size = mix(5.0f, 11.0f, Depth) * random_float(0.85f, 1.15f);
		Particle.m_Stretch = 0.55f;
		Particle.m_FallSpeed = mix(30.0f, 75.0f, Depth) * random_float(0.85f, 1.15f);
		Particle.m_SwayAmount = random_float(15.0f, 40.0f);
		Particle.m_SwaySpeed = random_float(1.0f, 2.2f);
		Particle.m_RotSpeed = random_float(-4.0f, 4.0f);
		Particle.m_Color = RandomPick(s_aConfettiColors).WithAlpha(mix(0.6f, 1.0f, Depth));
		break;
	case WEATHER_ASH:
	default:
		Particle.m_Size = mix(2.0f, 6.0f, Depth) * random_float(0.8f, 1.2f);
		Particle.m_FallSpeed = mix(12.0f, 35.0f, Depth) * random_float(0.85f, 1.15f);
		Particle.m_SwayAmount = random_float(4.0f, 12.0f);
		Particle.m_SwaySpeed = random_float(0.3f, 0.9f);
		Particle.m_RotSpeed = 0.0f;
		if(random_float() < 0.15f)
		{
			// glowing ember
			Particle.m_Color = ColorRGBA(1.0f, random_float(0.45f, 0.65f), 0.15f, mix(0.5f, 1.0f, Depth));
			Particle.m_Flicker = random_float(3.0f, 7.0f);
		}
		else
		{
			const float Grey = random_float(0.25f, 0.55f);
			Particle.m_Color = ColorRGBA(Grey, Grey, Grey, mix(0.4f, 0.9f, Depth));
		}
		break;
	}
	Particle.m_Size *= SizeScale;

	Particle.m_RotSpeed *= g_Config.m_XcWeatherSpin / 100.0f;
	Particle.m_Pos.x = random_float(0.0f, m_Width);
	if(Anywhere)
		Particle.m_Pos.y = random_float(0.0f, m_Height);
	else if(Particle.m_FallSpeed < 0.0f)
		Particle.m_Pos.y = m_Height + Particle.m_Size + random_float(0.0f, 50.0f); // rising ones start below
	else
		Particle.m_Pos.y = -Particle.m_Size - random_float(0.0f, 50.0f);
}

void CWeather::Step(vec2 Shift)
{
	const float Dt = std::min(Client()->RenderFrameTime(), 0.1f);
	m_Time += Dt;

	if(m_Type != g_Config.m_XcWeather)
	{
		m_Type = g_Config.m_XcWeather;
		m_vParticles.clear();
		// an own picture only takes video memory while it is the weather
		if(m_Type != WEATHER_CUSTOM)
			m_Images.Update("", 0.0f, 1.0f);
	}

	m_Height = VIRTUAL_HEIGHT;
	m_Width = VIRTUAL_HEIGHT * Graphics()->ScreenAspect();

	const size_t Target = (size_t)g_Config.m_XcWeatherDensity * MAX_PARTICLES_PER_DENSITY;
	if(m_vParticles.size() > Target)
		m_vParticles.resize(Target);
	while(m_vParticles.size() < Target)
	{
		SParticle Particle;
		Spawn(Particle, true);
		m_vParticles.push_back(Particle);
	}

	float WindStrength;
	switch(m_Type)
	{
	case WEATHER_SNOW: WindStrength = 15.0f; break;
	case WEATHER_LEAVES: WindStrength = 35.0f; break;
	case WEATHER_SAKURA: WindStrength = 25.0f; break;
	case WEATHER_RAIN: WindStrength = 60.0f; break;
	case WEATHER_CONFETTI: WindStrength = 30.0f; break;
	default: WindStrength = 15.0f; break;
	}
	WindStrength *= g_Config.m_XcWeatherWind / 100.0f;
	const float Wind = (std::sin(m_Time * 0.23f) * 0.6f + std::sin(m_Time * 0.071f) * 0.4f) * WindStrength;
	const float Speed = g_Config.m_XcWeatherSpeed / 100.0f;

	for(SParticle &Particle : m_vParticles)
	{
		Particle.m_SwayPhase += Particle.m_SwaySpeed * Dt;
		Particle.m_Rot += Particle.m_RotSpeed * Dt;

		const float SideSpeed = std::sin(Particle.m_SwayPhase) * Particle.m_SwayAmount + Wind * Particle.m_Depth;
		Particle.m_Pos += vec2(SideSpeed, Particle.m_FallSpeed) * Speed * Dt;
		Particle.m_Pos += Shift * Particle.m_Depth;

		const float Margin = Particle.m_Size * 2.0f + 10.0f;
		const bool Rising = Particle.m_FallSpeed < 0.0f;
		if((!Rising && Particle.m_Pos.y > m_Height + Margin) || (Rising && Particle.m_Pos.y < -Margin))
		{
			Spawn(Particle, false);
			continue;
		}
		if(!Rising && Particle.m_Pos.y < -Margin - 60.0f)
			Particle.m_Pos.y += m_Height + 2.0f * Margin;
		else if(Rising && Particle.m_Pos.y > m_Height + Margin + 60.0f)
			Particle.m_Pos.y -= m_Height + 2.0f * Margin;
		if(Particle.m_Pos.x < -Margin)
			Particle.m_Pos.x += m_Width + 2.0f * Margin;
		else if(Particle.m_Pos.x > m_Width + Margin)
			Particle.m_Pos.x -= m_Width + 2.0f * Margin;
	}
}

void CWeather::Draw()
{
	if(m_vParticles.empty())
		return;

	Graphics()->MapScreenToSize(m_Width, m_Height);
	if(m_Type == WEATHER_CUSTOM)
		DrawCustom();
	else if(IconWeather())
		DrawIcons();
	else if(m_Type >= WEATHER_RAIN || GameClient()->m_FxRenderer.ModuleActive())
		DrawShapes(); // the new shapes also work without the render module (drawn by IGraphics then)
	else
		DrawSprites();
}

float CWeather::ParticleAlpha(const SParticle &Particle) const
{
	float Alpha = Particle.m_Color.a * g_Config.m_XcWeatherAlpha / 100.0f;
	if(Particle.m_Flicker > 0.0f)
		Alpha *= 0.6f + 0.4f * std::sin(m_Time * Particle.m_Flicker + Particle.m_SwayPhase * 3.0f);
	return Alpha;
}

ColorRGBA CWeather::ParticleColor(const SParticle &Particle) const
{
	const float Alpha = ParticleAlpha(Particle);
	if(!g_Config.m_XcWeatherTint)
		return Particle.m_Color.WithAlpha(Alpha);
	// one color for everything; the rainbow gives each particle its own hue
	ColorRGBA Color = XcHelpers::EffectColor(GameClient(), g_Config.m_XcWeatherColorMode, g_Config.m_XcWeatherColor, -1, Particle.m_Hue, Alpha);
	// keep the light and dark variation of the weather's own colors
	const float Shade = 0.75f + 0.25f * std::max({Particle.m_Color.r, Particle.m_Color.g, Particle.m_Color.b});
	return ColorRGBA(Color.r * Shade, Color.g * Shade, Color.b * Shade, Alpha);
}

vec2 CWeather::ParticleSize(const SParticle &Particle) const
{
	float Height = Particle.m_Size * Particle.m_Stretch;
	// flat particles (leaves, petals) flip around while falling
	if(Particle.m_Stretch < 1.0f)
		Height *= 0.35f + 0.65f * std::abs(std::sin(Particle.m_SwayPhase * 1.3f));
	return vec2(Particle.m_Size, Height);
}

void CWeather::DrawShapes()
{
	const FxGlow::CScope GlowScope(FxGlow::WEATHER);
	m_vShapes.clear();
	for(const SParticle &Particle : m_vParticles)
	{
		const ColorRGBA Color = ParticleColor(Particle);
		const vec2 Half = ParticleSize(Particle) * 0.5f;
		switch(m_Type)
		{
		case WEATHER_RAIN:
		{
			// a thin streak along the way the drop falls
			const vec2 Dir = normalize(vec2(std::sin(m_Time * 0.23f) * 0.6f * 60.0f * g_Config.m_XcWeatherWind / 100.0f * Particle.m_Depth, Particle.m_FallSpeed));
			m_vShapes.push_back(FxShape::Line(Particle.m_Pos, Particle.m_Pos - Dir * Particle.m_Size, mix(0.6f, 1.3f, Particle.m_Depth), Color));
			break;
		}
		case WEATHER_BUBBLES:
			// a thin ring with a little shine on top
			m_vShapes.push_back(FxShape::Ring(Particle.m_Pos, Half, std::max(Half.x * 0.12f, 0.6f), Color));
			m_vShapes.push_back(FxShape::Circle(Particle.m_Pos + vec2(-Half.x * 0.35f, -Half.y * 0.35f), Half.x * 0.18f, Color.WithMultipliedAlpha(0.9f)).Soft(Half.x * 0.1f));
			break;
		case WEATHER_CONFETTI:
		{
			const float Flip = 0.25f + 0.75f * std::abs(std::sin(Particle.m_SwayPhase * 1.7f));
			m_vShapes.push_back(FxShape::RoundRect(Particle.m_Pos, vec2(Half.x, Half.x * 0.55f * Flip), Half.x * 0.15f, Color).Rotate(Particle.m_Rot));
			break;
		}
		case WEATHER_SNOW:
			if(Particle.m_Sprite)
			{
				// near flakes: six pointed star with a soft round core
				m_vShapes.push_back(FxShape::Star(Particle.m_Pos, Half.x, 6, 0.5f, Color).Rotate(Particle.m_Rot).Soft(Half.x * 0.1f));
				m_vShapes.push_back(FxShape::Circle(Particle.m_Pos, Half.x * 0.3f, Color).Soft(Half.x * 0.3f));
			}
			else
			{
				// far flakes: small discs with a soft edge
				m_vShapes.push_back(FxShape::Circle(Particle.m_Pos, Half.x * 0.6f, Color).Soft(Half.x * 0.5f));
			}
			break;
		case WEATHER_LEAVES:
		case WEATHER_SAKURA:
		{
			// darker towards one side, like light on a curved leaf or petal
			const float Shade = m_Type == WEATHER_LEAVES ? 0.7f : 1.12f;
			const ColorRGBA Other(std::min(Color.r * Shade, 1.0f), std::min(Color.g * Shade, 1.0f), std::min(Color.b * Shade, 1.0f), Color.a);
			m_vShapes.push_back(FxShape::Ellipse(Particle.m_Pos, Half, Color).Gradient(Other).Rotate(Particle.m_Rot));
			break;
		}
		default:
			if(Particle.m_Flicker > 0.0f)
				CFxRenderer::AppendGlowing(m_vShapes, FxShape::Circle(Particle.m_Pos, Half.x * 0.6f, Color), Half.x * 1.5f, 0.6f);
			else
				m_vShapes.push_back(FxShape::Blob(Particle.m_Pos, Half.x * 1.2f, Color));
			break;
		}
	}
	GameClient()->m_FxRenderer.Draw(m_vShapes, true);
}

void CWeather::DrawSprites() const
{
	const IGraphics::CTextureHandle aTextures[] = {
		GameClient()->m_ParticlesSkin.m_SpriteParticleBall,
		GameClient()->m_ExtrasSkin.m_SpriteParticleSnowflake,
	};
	for(int Pass = 0; Pass < 2; Pass++)
	{
		const bool SpritePass = Pass == 1;
		if(aTextures[Pass].IsValid())
			Graphics()->TextureSet(aTextures[Pass]);
		else
			Graphics()->TextureClear();

		Graphics()->QuadsBegin();
		for(const SParticle &Particle : m_vParticles)
		{
			if(Particle.m_Sprite != SpritePass)
				continue;

			const vec2 Size = ParticleSize(Particle);
			Graphics()->SetColor(ParticleColor(Particle));
			Graphics()->QuadsSetRotation(Particle.m_Rot);
			IGraphics::CQuadItem QuadItem(Particle.m_Pos.x, Particle.m_Pos.y, Size.x, Size.y);
			Graphics()->QuadsDraw(&QuadItem, 1);
		}
		Graphics()->QuadsEnd();
	}
	Graphics()->QuadsSetRotation(0.0f);
}

void CWeather::DrawIcons()
{
	CXcSprites &Sprites = GameClient()->m_XcSprites;
	if(!Sprites.Loaded())
	{
		DrawShapes();
		return;
	}
	const FxGlow::SScale Glow = FxGlow::For(FxGlow::WEATHER);
	// soft light under the glowing ones
	if(g_Config.m_XcWeatherGlow && Glow.m_Strength > 0.0f)
	{
		Sprites.Begin(CXcSprites::SPRITE_GLOW, true);
		for(const SParticle &Particle : m_vParticles)
			if(Particle.m_Glowing)
				Sprites.Draw(Particle.m_Pos, Particle.m_Size * 3.0f * Glow.m_Radius, 0.0f, ParticleColor(Particle).WithMultipliedAlpha(std::min(0.45f * Glow.m_Strength, 1.0f)));
		Sprites.End();
	}
	// the pictures, one batch per picture
	for(int Icon = CXcSprites::SPRITE_CROWN; Icon <= CXcSprites::SPRITE_STAR; Icon++)
	{
		bool Any = false;
		for(const SParticle &Particle : m_vParticles)
			Any |= Particle.m_Icon == Icon;
		if(!Any)
			continue;
		Sprites.Begin(Icon, false);
		for(const SParticle &Particle : m_vParticles)
			if(Particle.m_Icon == Icon)
				Sprites.Draw(Particle.m_Pos, Particle.m_Size * 1.4f, Particle.m_Rot, ParticleColor(Particle));
		Sprites.End();
	}
	// glowing pictures get a little light added on top
	Sprites.Begin(CXcSprites::SPRITE_CORE, true);
	for(const SParticle &Particle : m_vParticles)
		if(Particle.m_Glowing)
			Sprites.Draw(Particle.m_Pos, Particle.m_Size * 0.9f, 0.0f, ParticleColor(Particle).WithMultipliedAlpha(0.5f));
	Sprites.End();
}

void CWeather::DrawCustom()
{
	m_Images.Update(g_Config.m_XcWeatherFile, std::min(Client()->RenderFrameTime(), 0.1f), g_Config.m_XcWeatherGifSpeed / 100.0f);
	if(!m_Images.HasFrames())
		return;
	const float Aspect = std::max(m_Images.FrameAspect(), 0.05f);
	Graphics()->TextureSet(m_Images.CurrentFrame());
	Graphics()->QuadsBegin();
	for(const SParticle &Particle : m_vParticles)
	{
		// own pictures keep their colors unless the weather is painted
		ColorRGBA Color = g_Config.m_XcWeatherTint ? ParticleColor(Particle) : ColorRGBA(1.0f, 1.0f, 1.0f, ParticleAlpha(Particle));
		Graphics()->SetColor(Color);
		Graphics()->QuadsSetRotation(Particle.m_Rot);
		const float Width = Particle.m_Size * (Aspect >= 1.0f ? 1.0f : Aspect);
		const float Height = Width / Aspect;
		const IGraphics::CQuadItem Item(Particle.m_Pos.x - Width * 0.5f, Particle.m_Pos.y - Height * 0.5f, Width, Height);
		Graphics()->QuadsDrawTL(&Item, 1);
	}
	Graphics()->QuadsEnd();
	Graphics()->QuadsSetRotation(0.0f);
}

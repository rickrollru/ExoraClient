#include "weather.h"

#include <base/math.h>

#include <engine/client.h>
#include <engine/graphics.h>
#include <engine/shared/config.h>

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

template<typename T, size_t N>
static const T &RandomPick(const T (&aItems)[N])
{
	return aItems[std::min((size_t)(random_float() * N), N - 1)];
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

	Particle.m_Pos.x = random_float(0.0f, m_Width);
	Particle.m_Pos.y = Anywhere ? random_float(0.0f, m_Height) : -Particle.m_Size - random_float(0.0f, 50.0f);
}

void CWeather::Step(vec2 Shift)
{
	const float Dt = std::min(Client()->RenderFrameTime(), 0.1f);
	m_Time += Dt;

	if(m_Type != g_Config.m_XcWeather)
	{
		m_Type = g_Config.m_XcWeather;
		m_vParticles.clear();
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
	default: WindStrength = 10.0f; break;
	}
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
		if(Particle.m_Pos.y > m_Height + Margin)
		{
			Spawn(Particle, false);
			continue;
		}
		if(Particle.m_Pos.y < -Margin - 60.0f)
			Particle.m_Pos.y += m_Height + 2.0f * Margin;
		if(Particle.m_Pos.x < -Margin)
			Particle.m_Pos.x += m_Width + 2.0f * Margin;
		else if(Particle.m_Pos.x > m_Width + Margin)
			Particle.m_Pos.x -= m_Width + 2.0f * Margin;
	}
}

void CWeather::Draw() const
{
	if(m_vParticles.empty())
		return;

	Graphics()->MapScreenToSize(m_Width, m_Height);
	const float Alpha = g_Config.m_XcWeatherAlpha / 100.0f;

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

			float ParticleAlpha = Particle.m_Color.a * Alpha;
			if(Particle.m_Flicker > 0.0f)
				ParticleAlpha *= 0.6f + 0.4f * std::sin(m_Time * Particle.m_Flicker + Particle.m_SwayPhase * 3.0f);

			float Width = Particle.m_Size;
			float Height = Particle.m_Size * Particle.m_Stretch;
			// flat particles (leaves, petals) flip around while falling
			if(Particle.m_Stretch < 1.0f)
				Height *= 0.35f + 0.65f * std::abs(std::sin(Particle.m_SwayPhase * 1.3f));

			Graphics()->SetColor(Particle.m_Color.WithAlpha(ParticleAlpha));
			Graphics()->QuadsSetRotation(Particle.m_Rot);
			IGraphics::CQuadItem QuadItem(Particle.m_Pos.x, Particle.m_Pos.y, Width, Height);
			Graphics()->QuadsDraw(&QuadItem, 1);
		}
		Graphics()->QuadsEnd();
	}
	Graphics()->QuadsSetRotation(0.0f);
}

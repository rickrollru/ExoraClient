#include "hook_fx.h"

#include "xc_helpers.h"

#include <base/math.h>

#include <engine/client.h>
#include <engine/graphics.h>
#include <engine/shared/config.h>

#include <game/client/gameclient.h>
#include <game/gamecore.h>

#include <cmath>

using namespace XcHelpers;

void CHookFx::OnReset()
{
	for(auto &State : m_aStates)
		State = SState();
	m_Particles.Clear();
}

void CHookFx::OnMapLoad()
{
	OnReset();
}

int CHookFx::Style(int ClientId) const
{
	if(!in_range(ClientId, MAX_CLIENTS - 1))
		return STYLE_DEFAULT;
	if(!ShowFor(GameClient(), ClientId, g_Config.m_XcHookSelf, g_Config.m_XcHookDummy, g_Config.m_XcHookOthers))
		return STYLE_DEFAULT;
	return g_Config.m_XcHookStyle;
}

ColorRGBA CHookFx::Color(float Alpha) const
{
	if(g_Config.m_XcHookRainbow)
		return RainbowColor(Client()->GlobalTime() * 0.2f, Alpha);
	return HslConfigColor(g_Config.m_XcHookColor).WithAlpha(Alpha);
}

ColorRGBA CHookFx::SpriteColor(float Alpha) const
{
	const ColorRGBA Main = Color(Alpha);
	return ColorRGBA(mix(Main.r, 1.0f, 0.4f), mix(Main.g, 1.0f, 0.4f), mix(Main.b, 1.0f, 0.4f), Alpha);
}

void CHookFx::RenderLine(vec2 PlayerPos, vec2 HookPos, float Alpha) const
{
	const float Length = distance(PlayerPos, HookPos);
	if(Length < 1.0f)
		return;
	const vec2 Dir = (HookPos - PlayerPos) / Length;
	const vec2 Normal = vec2(-Dir.y, Dir.x);
	const vec2 End = HookPos;
	const float Width = 4.0f * g_Config.m_XcHookWidth / 100.0f;
	const int Style = g_Config.m_XcHookStyle;

	const ColorRGBA Main = Color(Alpha);
	const ColorRGBA Clear = Main.WithAlpha(0.0f);

	// band between two distances from the center line, with a color at each edge
	const auto Band = [&](float Inner, float Outer, ColorRGBA InnerColor, ColorRGBA OuterColor) {
		for(int Side = -1; Side <= 1; Side += 2)
		{
			const vec2 a = Normal * (Inner * Side);
			const vec2 b = Normal * (Outer * Side);
			Graphics()->SetColor4(InnerColor, InnerColor, OuterColor, OuterColor);
			const IGraphics::CFreeformItem Item(PlayerPos + a, End + a, PlayerPos + b, End + b);
			Graphics()->QuadsDrawFreeform(&Item, 1);
		}
	};

	Graphics()->QuadsSetRotation(0.0f);
	Graphics()->TextureClear();
	Graphics()->QuadsBegin();
	if(Style == STYLE_CHAIN)
	{
		// colored aura under the chain sprites, which are too dark to be tinted well
		Band(0.0f, Width * 2.5f, Main.WithMultipliedAlpha(0.8f), Clear);
	}
	else if(Style == STYLE_GLOW)
	{
		const float GlowSize = Width * 2.5f;
		Band(Width * 0.5f, Width * 0.5f + GlowSize, Main.WithMultipliedAlpha(0.45f), Clear);
		Band(0.0f, Width * 0.5f, ColorRGBA(mix(Main.r, 1.0f, 0.6f), mix(Main.g, 1.0f, 0.6f), mix(Main.b, 1.0f, 0.6f), Alpha), Main);

		// energy pulses running from the hook to the player
		const float Spacing = 48.0f;
		const float Offset = std::fmod(Client()->GlobalTime() * 260.0f, Spacing);
		for(float f = Offset; f < Length; f += Spacing)
		{
			const float Fade = std::min(std::min(f, Length - f) / 24.0f, 1.0f);
			if(Fade <= 0.0f)
				continue;
			DrawSoftCircle(Graphics(), HookPos - Dir * f, Width * 0.55f, Width * 0.9f, ColorRGBA(1.0f, 1.0f, 1.0f, Alpha * 0.85f * Fade));
		}
	}
	else
	{
		// solid line with a thin soft edge so it does not look jagged
		Band(0.0f, Width * 0.5f, Main, Main);
		Band(Width * 0.5f, Width * 0.5f + 1.2f, Main, Clear);
	}
	Graphics()->QuadsEnd();
}

void CHookFx::EmitHit(vec2 Pos, vec2 AwayDir)
{
	const ColorRGBA Main = Color(1.0f);
	switch(g_Config.m_XcHookHitFx)
	{
	case HIT_RING:
	{
		CXcParticles::SParticle Ring;
		Ring.m_Type = CXcParticles::EType::RING;
		Ring.m_Pos = Pos;
		Ring.m_LifeSpan = 0.35f;
		Ring.m_StartSize = 4.0f;
		Ring.m_EndSize = 30.0f;
		Ring.m_Thickness = 5.0f;
		Ring.m_Color = Main;
		m_Particles.Add(Ring);
		break;
	}
	case HIT_SPARKS:
	case HIT_STARS:
	{
		const bool Stars = g_Config.m_XcHookHitFx == HIT_STARS;
		const int Num = Stars ? 5 : 9;
		const float BaseAngle = angle(AwayDir);
		for(int i = 0; i < Num; i++)
		{
			CXcParticles::SParticle Particle;
			Particle.m_Type = Stars ? CXcParticles::EType::STAR : CXcParticles::EType::SPARK;
			Particle.m_Pos = Pos;
			// spray back towards the player, away from what was grabbed
			Particle.m_Vel = direction(BaseAngle + random_float(-1.2f, 1.2f)) * random_float(160.0f, 420.0f);
			Particle.m_Gravity = Stars ? 400.0f : 900.0f;
			Particle.m_LifeSpan = random_float(0.3f, 0.5f);
			Particle.m_StartSize = Stars ? random_float(12.0f, 18.0f) : random_float(5.0f, 8.0f);
			Particle.m_Rot = random_float(0.0f, 2.0f * pi);
			Particle.m_RotSpeed = Stars ? random_float(-6.0f, 6.0f) : 0.0f;
			Particle.m_Color = i % 3 == 0 ? ColorRGBA(1.0f, 1.0f, 1.0f, 1.0f) : Main;
			m_Particles.Add(Particle);
		}
		break;
	}
	}
}

void CHookFx::OnRender()
{
	if(Client()->State() != IClient::STATE_ONLINE && Client()->State() != IClient::STATE_DEMOPLAYBACK)
		return;

	const float Dt = std::min(Client()->RenderFrameTime(), 0.1f);
	for(int ClientId = 0; ClientId < MAX_CLIENTS; ClientId++)
	{
		SState &State = m_aStates[ClientId];
		if(g_Config.m_XcHookHitFx == HIT_OFF || !GameClient()->m_Snap.m_aCharacters[ClientId].m_Active ||
			!ShowFor(GameClient(), ClientId, g_Config.m_XcHookSelf, g_Config.m_XcHookDummy, g_Config.m_XcHookOthers) ||
			PlayerAlpha(GameClient(), ClientId) <= 0.0f)
		{
			State.m_Valid = false;
			continue;
		}

		const CGameClient::CClientData &Data = GameClient()->m_aClients[ClientId];
		const CNetObj_Character &Char = Data.m_RenderCur;
		State.m_Cooldown = std::max(State.m_Cooldown - Dt, 0.0f);
		if(State.m_Valid && Char.m_HookState == HOOK_GRABBED && State.m_HookState != HOOK_GRABBED && State.m_Cooldown <= 0.0f)
		{
			vec2 HookPos = vec2(Char.m_HookX, Char.m_HookY);
			if(in_range(Char.m_HookedPlayer, MAX_CLIENTS - 1))
				HookPos = GameClient()->m_aClients[Char.m_HookedPlayer].m_RenderPos;
			const vec2 ToPlayer = Data.m_RenderPos - HookPos;
			EmitHit(HookPos, length(ToPlayer) > 0.0f ? normalize(ToPlayer) : vec2(0.0f, -1.0f));
			// prediction can flip the hook state back and forth for a moment
			State.m_Cooldown = 0.15f;
		}
		State.m_Valid = true;
		State.m_HookState = Char.m_HookState;
	}

	m_Particles.Update(Dt);
	m_Particles.Render(GameClient()->m_FxRenderer);
}

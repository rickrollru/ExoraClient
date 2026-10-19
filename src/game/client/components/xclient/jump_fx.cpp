#include "jump_fx.h"

#include "sprite_fx.h"

#include "xc_helpers.h"

#include <base/math.h>

#include <engine/client.h>
#include <engine/graphics.h>
#include <engine/shared/config.h>

#include <game/client/gameclient.h>
#include <game/collision.h>

#include <cmath>

using namespace XcHelpers;

// rings lie flat on the ground, seen from the side
static constexpr float RING_SQUASH = 0.32f;

void CJumpFx::OnReset()
{
	for(auto &Player : m_aPlayers)
		Player = SPlayerState();
	m_Particles.Clear();
}

void CJumpFx::OnMapLoad()
{
	OnReset();
}

void CJumpFx::OnRender()
{
	if(Client()->State() != IClient::STATE_ONLINE && Client()->State() != IClient::STATE_DEMOPLAYBACK)
		return;

	const float Dt = std::min(Client()->RenderFrameTime(), 0.1f);
	UpdatePlayers(Dt);
	m_Particles.Update(Dt);
	m_Particles.Render(GameClient()->m_FxRenderer);
}

void CJumpFx::UpdatePlayers(float Dt)
{
	// the card toggle turns off everything in it: the jump, the landing ring and the dust
	const bool AnyEnabled = g_Config.m_XcJumpFx != JUMP_FX_OFF;
	for(int ClientId = 0; ClientId < MAX_CLIENTS; ClientId++)
	{
		SPlayerState &State = m_aPlayers[ClientId];
		if(!AnyEnabled || !GameClient()->m_Snap.m_aCharacters[ClientId].m_Active || !ShowFor(GameClient(), ClientId, g_Config.m_XcJumpFxSelf, g_Config.m_XcJumpFxDummy, g_Config.m_XcJumpFxOthers) || PlayerAlpha(GameClient(), ClientId) <= 0.0f)
		{
			State.m_Valid = false;
			continue;
		}

		const CGameClient::CClientData &Data = GameClient()->m_aClients[ClientId];
		m_ColorClient = ClientId;
		const CNetObj_Character &Char = Data.m_RenderCur;
		const vec2 Pos = Data.m_RenderPos;
		const vec2 Feet = Pos + vec2(0.0f, 14.0f);
		const vec2 Vel = CharacterVelocity(GameClient(), ClientId);
		const bool InAir = !Collision()->CheckPoint(Pos.x, Pos.y + 16.0f);

		// first frame for this player, or teleported / respawned
		if(!State.m_Valid || distance(State.m_Pos, Pos) > 200.0f)
		{
			State.m_Valid = true;
			State.m_InAir = InAir;
			State.m_Jumped = Char.m_Jumped;
			State.m_MaxFallSpeed = 0.0f;
			State.m_DustTimer = 0.0f;
			State.m_Pos = Pos;
			continue;
		}

		const bool JumpStarted = (Char.m_Jumped & 1) && !(State.m_Jumped & 1);
		const bool AirJumpStarted = (Char.m_Jumped & 2) && !(State.m_Jumped & 2);
		// a frozen tee does not jump: the jump flags it gets while frozen are not jumps
		const bool Frozen = Data.m_FreezeEnd != 0 || Data.m_RegularPredicted.m_FreezeEnd != 0 || Data.m_DeepFrozen;
		if(!Frozen)
		{
			if(AirJumpStarted)
			{
				if(g_Config.m_XcJumpFxAir)
					EmitJump(Feet, true);
			}
			else if(JumpStarted)
			{
				EmitJump(Feet, false);
			}
		}

		if(g_Config.m_XcLandFx && !Frozen && State.m_InAir && !InAir && State.m_MaxFallSpeed > 700.0f)
			EmitLanding(Feet + vec2(0.0f, 2.0f), State.m_MaxFallSpeed);

		if(InAir)
			State.m_MaxFallSpeed = std::max(State.m_MaxFallSpeed, Vel.y);
		else
			State.m_MaxFallSpeed = 0.0f;

		if(g_Config.m_XcRunDust && !InAir && std::abs(Vel.x) > 300.0f)
		{
			State.m_DustTimer += Dt;
			const float Interval = 0.06f;
			while(State.m_DustTimer > Interval)
			{
				State.m_DustTimer -= Interval;
				EmitDust(Feet, Vel.x > 0.0f ? 1.0f : -1.0f);
			}
		}
		else
		{
			State.m_DustTimer = 0.0f;
		}

		State.m_InAir = InAir;
		State.m_Jumped = Char.m_Jumped;
		State.m_Pos = Pos;
	}
}

ColorRGBA CJumpFx::FxColor() const
{
	return EffectColor(GameClient(), g_Config.m_XcJumpFxColorMode, g_Config.m_XcJumpFxColor, m_ColorClient);
}

void CJumpFx::EmitJump(vec2 Feet, bool AirJump)
{
	const float Scale = g_Config.m_XcJumpFxSize / 100.0f;
	const ColorRGBA Color = FxColor();

	switch(g_Config.m_XcJumpFx)
	{
	case JUMP_FX_RINGS:
	{
		if(GameClient()->m_XcSprites.Enabled())
		{
			// one glowing circle lying on the ground
			CXcParticles::SParticle Fx;
			Fx.m_Type = CXcParticles::EType::JUMP_CIRCLE;
			Fx.m_Sprite = CXcSprites::SPRITE_JUMP_RING;
			Fx.m_Squash = RING_SQUASH;
			Fx.m_Pos = Feet;
			Fx.m_LifeSpan = g_Config.m_XcJumpFxDuration / 1000.0f;
			const float Diameter = (AirJump ? 190.0f : 240.0f) * Scale; // the visible ring is half of the texture
			Fx.m_StartSize = Diameter * 0.2f;
			Fx.m_EndSize = Diameter;
			Fx.m_Color = Color;
			m_Particles.Add(Fx);
			break;
		}
		CXcParticles::SParticle Fx;
		Fx.m_Type = CXcParticles::EType::RING;
		Fx.m_Squash = RING_SQUASH;
		Fx.m_Thickness = 6.0f * Scale;
		Fx.m_Pos = Feet;
		Fx.m_LifeSpan = 0.45f;
		Fx.m_StartSize = 10.0f * Scale;
		Fx.m_EndSize = (AirJump ? 56.0f : 72.0f) * Scale;
		Fx.m_Color = Color;
		m_Particles.Add(Fx);
		if(!AirJump)
		{
			// second, smaller ring a bit later looks like a ripple
			Fx.m_LifeSpan = 0.35f;
			Fx.m_Life = -0.08f;
			Fx.m_EndSize = 44.0f * Scale;
			m_Particles.Add(Fx);
		}
		break;
	}
	case JUMP_FX_SPARKS:
	case JUMP_FX_STARS:
	{
		const bool Stars = g_Config.m_XcJumpFx == JUMP_FX_STARS;
		const int Num = Stars ? 6 : 10;
		for(int i = 0; i < Num; i++)
		{
			CXcParticles::SParticle Fx;
			Fx.m_Type = Stars ? CXcParticles::EType::STAR : CXcParticles::EType::SPARK;
			Fx.m_Pos = Feet + vec2(random_float(-10.0f, 10.0f), 0.0f);
			// mostly sideways and a bit up, away from the jump direction
			const float Angle = random_float(-pi * 0.95f, -pi * 0.05f);
			Fx.m_Vel = direction(Angle) * random_float(150.0f, 380.0f) * Scale;
			Fx.m_Gravity = Stars ? 500.0f : 1100.0f;
			Fx.m_LifeSpan = random_float(0.35f, 0.6f);
			Fx.m_StartSize = (Stars ? random_float(14.0f, 22.0f) : random_float(5.0f, 9.0f)) * Scale;
			Fx.m_EndSize = 0.0f;
			Fx.m_Rot = random_float(0.0f, 2.0f * pi);
			Fx.m_RotSpeed = Stars ? random_float(-6.0f, 6.0f) : 0.0f;
			Fx.m_Color = i % 3 == 0 ? ColorRGBA(1.0f, 1.0f, 1.0f, 1.0f) : Color;
			m_Particles.Add(Fx);
		}
		break;
	}
	}
}

void CJumpFx::EmitLanding(vec2 Feet, float FallSpeed)
{
	const float Scale = g_Config.m_XcJumpFxSize / 100.0f;
	const float Strength = std::clamp((FallSpeed - 700.0f) / 1500.0f, 0.0f, 1.0f);

	CXcParticles::SParticle Fx;
	if(GameClient()->m_XcSprites.Enabled())
	{
		// a soft thick ring that spreads over the ground, stronger after a long fall
		Fx.m_Type = CXcParticles::EType::JUMP_CIRCLE;
		Fx.m_Sprite = CXcSprites::SPRITE_JUMP_CIRCLE;
		Fx.m_Squash = RING_SQUASH;
		Fx.m_Pos = Feet;
		Fx.m_LifeSpan = 0.5f + 0.3f * Strength;
		Fx.m_EndSize = mix(110.0f, 190.0f, Strength) * Scale;
		Fx.m_StartSize = Fx.m_EndSize * 0.2f;
		Fx.m_Color = FxColor().WithMultipliedAlpha(mix(0.5f, 0.9f, Strength));
		m_Particles.Add(Fx);
		for(int i = 0; i < 4; i++)
			EmitDust(Feet, i % 2 == 0 ? 1.0f : -1.0f);
		return;
	}
	Fx.m_Type = CXcParticles::EType::RING;
	Fx.m_Squash = RING_SQUASH;
	Fx.m_Thickness = 6.0f * Scale;
	Fx.m_Pos = Feet;
	Fx.m_LifeSpan = 0.4f + 0.2f * Strength;
	Fx.m_StartSize = 16.0f * Scale;
	Fx.m_EndSize = mix(64.0f, 110.0f, Strength) * Scale;
	Fx.m_Color = ColorRGBA(1.0f, 1.0f, 1.0f, mix(0.5f, 0.9f, Strength));
	m_Particles.Add(Fx);

	// a few dust puffs to both sides
	for(int i = 0; i < 4; i++)
		EmitDust(Feet, i % 2 == 0 ? 1.0f : -1.0f);
}

void CJumpFx::EmitDust(vec2 Feet, float Direction)
{
	const float Scale = g_Config.m_XcJumpFxSize / 100.0f;
	CXcParticles::SParticle Fx;
	Fx.m_Type = CXcParticles::EType::DUST;
	Fx.m_Pos = Feet + vec2(-Direction * random_float(4.0f, 12.0f), random_float(-2.0f, 2.0f));
	Fx.m_Vel = vec2(-Direction * random_float(20.0f, 70.0f), random_float(-60.0f, -20.0f)) * Scale;
	Fx.m_Gravity = -20.0f;
	Fx.m_LifeSpan = random_float(0.4f, 0.7f);
	Fx.m_StartSize = random_float(10.0f, 14.0f) * Scale;
	Fx.m_EndSize = random_float(24.0f, 34.0f) * Scale;
	Fx.m_Rot = random_float(0.0f, 2.0f * pi);
	Fx.m_RotSpeed = random_float(-1.5f, 1.5f);
	const float Grey = random_float(0.75f, 0.9f);
	Fx.m_Color = ColorRGBA(Grey, Grey, Grey, 0.55f);
	m_Particles.Add(Fx);
}

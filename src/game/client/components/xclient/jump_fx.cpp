#include "jump_fx.h"

#include <base/math.h>

#include <engine/client.h>
#include <engine/graphics.h>
#include <engine/shared/config.h>

#include <game/client/gameclient.h>
#include <game/collision.h>

#include <cmath>

static constexpr size_t MAX_FX = 1024;
static constexpr int RING_SEGMENTS = 40;
// rings lie flat on the ground, seen from the side
static constexpr float RING_SQUASH = 0.32f;

static ColorRGBA AccentColor()
{
	return color_cast<ColorRGBA>(ColorHSLA(g_Config.m_XcJumpFxColor));
}

void CJumpFx::OnReset()
{
	for(auto &Player : m_aPlayers)
		Player = SPlayerState();
	m_vFx.clear();
}

void CJumpFx::OnMapLoad()
{
	OnReset();
}

bool CJumpFx::ShouldShow(int ClientId) const
{
	const int Self = GameClient()->m_aLocalIds[g_Config.m_ClDummy];
	const int Dummy = Client()->DummyConnected() ? GameClient()->m_aLocalIds[!g_Config.m_ClDummy] : -1;
	if(ClientId == Self)
		return g_Config.m_XcJumpFxSelf;
	if(ClientId == Dummy)
		return g_Config.m_XcJumpFxDummy;
	return g_Config.m_XcJumpFxOthers;
}

void CJumpFx::OnRender()
{
	if(Client()->State() != IClient::STATE_ONLINE && Client()->State() != IClient::STATE_DEMOPLAYBACK)
		return;

	const float Dt = std::min(Client()->RenderFrameTime(), 0.1f);
	UpdatePlayers(Dt);
	Simulate(Dt);
	Draw();
}

void CJumpFx::UpdatePlayers(float Dt)
{
	const bool AnyEnabled = g_Config.m_XcJumpFx != JUMP_FX_OFF || g_Config.m_XcLandFx || g_Config.m_XcRunDust;
	for(int ClientId = 0; ClientId < MAX_CLIENTS; ClientId++)
	{
		SPlayerState &State = m_aPlayers[ClientId];
		if(!AnyEnabled || !GameClient()->m_Snap.m_aCharacters[ClientId].m_Active || !ShouldShow(ClientId))
		{
			State.m_Valid = false;
			continue;
		}

		const CGameClient::CClientData &Data = GameClient()->m_aClients[ClientId];
		const CNetObj_Character &Char = Data.m_RenderCur;
		const vec2 Pos = Data.m_RenderPos;
		const vec2 Feet = Pos + vec2(0.0f, 14.0f);
		// netobj velocity is in 1/256 units per tick
		const vec2 Vel = vec2(Char.m_VelX, Char.m_VelY) / 256.0f * (float)Client()->GameTickSpeed();
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
		if(g_Config.m_XcJumpFx != JUMP_FX_OFF)
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

		if(g_Config.m_XcLandFx && State.m_InAir && !InAir && State.m_MaxFallSpeed > 700.0f)
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

void CJumpFx::Add(const SFx &Fx)
{
	if(m_vFx.size() >= MAX_FX)
		return;
	m_vFx.push_back(Fx);
}

void CJumpFx::EmitJump(vec2 Feet, bool AirJump)
{
	const float Scale = g_Config.m_XcJumpFxSize / 100.0f;
	const ColorRGBA Color = AccentColor();

	switch(g_Config.m_XcJumpFx)
	{
	case JUMP_FX_RINGS:
	{
		SFx Fx = {};
		Fx.m_Type = FX_RING;
		Fx.m_Pos = Feet;
		Fx.m_LifeSpan = 0.45f;
		Fx.m_StartSize = 10.0f * Scale;
		Fx.m_EndSize = (AirJump ? 56.0f : 72.0f) * Scale;
		Fx.m_Color = Color;
		Add(Fx);
		if(!AirJump)
		{
			// second, smaller ring a bit later looks like a ripple
			Fx.m_LifeSpan = 0.35f;
			Fx.m_Life = -0.08f;
			Fx.m_EndSize = 44.0f * Scale;
			Add(Fx);
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
			SFx Fx = {};
			Fx.m_Type = Stars ? FX_STAR : FX_SPARK;
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
			Add(Fx);
		}
		break;
	}
	}
}

void CJumpFx::EmitLanding(vec2 Feet, float FallSpeed)
{
	const float Scale = g_Config.m_XcJumpFxSize / 100.0f;
	const float Strength = std::clamp((FallSpeed - 700.0f) / 1500.0f, 0.0f, 1.0f);

	SFx Fx = {};
	Fx.m_Type = FX_RING;
	Fx.m_Pos = Feet;
	Fx.m_LifeSpan = 0.4f + 0.2f * Strength;
	Fx.m_StartSize = 16.0f * Scale;
	Fx.m_EndSize = mix(64.0f, 110.0f, Strength) * Scale;
	Fx.m_Color = ColorRGBA(1.0f, 1.0f, 1.0f, mix(0.5f, 0.9f, Strength));
	Add(Fx);

	// a few dust puffs to both sides
	for(int i = 0; i < 4; i++)
		EmitDust(Feet, i % 2 == 0 ? 1.0f : -1.0f);
}

void CJumpFx::EmitDust(vec2 Feet, float Direction)
{
	const float Scale = g_Config.m_XcJumpFxSize / 100.0f;
	SFx Fx = {};
	Fx.m_Type = FX_DUST;
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
	Add(Fx);
}

void CJumpFx::Simulate(float Dt)
{
	for(SFx &Fx : m_vFx)
	{
		Fx.m_Life += Dt;
		if(Fx.m_Life < 0.0f)
			continue;
		Fx.m_Vel.y += Fx.m_Gravity * Dt;
		Fx.m_Vel *= std::pow(0.15f, Dt); // air friction
		Fx.m_Pos += Fx.m_Vel * Dt;
		Fx.m_Rot += Fx.m_RotSpeed * Dt;
	}
	m_vFx.erase(std::remove_if(m_vFx.begin(), m_vFx.end(), [](const SFx &Fx) { return Fx.m_Life >= Fx.m_LifeSpan; }), m_vFx.end());
}

void CJumpFx::DrawRing(const SFx &Fx) const
{
	const float t = std::clamp(Fx.m_Life / Fx.m_LifeSpan, 0.0f, 1.0f);
	// ease out: fast at the start, slows down at the end
	const float Ease = 1.0f - (1.0f - t) * (1.0f - t) * (1.0f - t);
	const float Radius = mix(Fx.m_StartSize, Fx.m_EndSize, Ease);
	const float Alpha = Fx.m_Color.a * (1.0f - t);
	const float Thickness = mix(6.0f, 2.0f, t) * g_Config.m_XcJumpFxSize / 100.0f;
	const float Glow = Thickness * 3.0f;

	const ColorRGBA Core = Fx.m_Color.WithAlpha(Alpha);
	const ColorRGBA Clear = Fx.m_Color.WithAlpha(0.0f);
	const ColorRGBA Soft = Fx.m_Color.WithAlpha(Alpha * 0.35f);

	IGraphics::CFreeformItem aCore[RING_SEGMENTS];
	IGraphics::CFreeformItem aGlowOut[RING_SEGMENTS];
	IGraphics::CFreeformItem aGlowIn[RING_SEGMENTS];
	for(int i = 0; i < RING_SEGMENTS; i++)
	{
		const vec2 Dir1 = direction(2.0f * pi * i / RING_SEGMENTS);
		const vec2 Dir2 = direction(2.0f * pi * (i + 1) / RING_SEGMENTS);
		const auto Point = [&](vec2 Dir, float R) { return Fx.m_Pos + vec2(Dir.x * R, Dir.y * R * RING_SQUASH); };
		const float Inner = std::max(Radius - Thickness * 0.5f, 0.0f);
		const float Outer = Radius + Thickness * 0.5f;
		aCore[i] = IGraphics::CFreeformItem(Point(Dir1, Inner), Point(Dir2, Inner), Point(Dir1, Outer), Point(Dir2, Outer));
		aGlowOut[i] = IGraphics::CFreeformItem(Point(Dir1, Outer), Point(Dir2, Outer), Point(Dir1, Outer + Glow), Point(Dir2, Outer + Glow));
		aGlowIn[i] = IGraphics::CFreeformItem(Point(Dir1, Inner), Point(Dir2, Inner), Point(Dir1, std::max(Inner - Glow, 0.0f)), Point(Dir2, std::max(Inner - Glow, 0.0f)));
	}

	Graphics()->SetColor(Core);
	Graphics()->QuadsDrawFreeform(aCore, RING_SEGMENTS);
	Graphics()->SetColor4(Soft, Soft, Clear, Clear);
	Graphics()->QuadsDrawFreeform(aGlowOut, RING_SEGMENTS);
	Graphics()->QuadsDrawFreeform(aGlowIn, RING_SEGMENTS);
}

void CJumpFx::Draw() const
{
	if(m_vFx.empty())
		return;

	// rings: untextured, soft glow through vertex colors
	Graphics()->TextureClear();
	Graphics()->QuadsBegin();
	for(const SFx &Fx : m_vFx)
		if(Fx.m_Type == FX_RING && Fx.m_Life >= 0.0f)
			DrawRing(Fx);
	Graphics()->QuadsEnd();

	// sprite particles
	const struct
	{
		int m_Type;
		IGraphics::CTextureHandle m_Texture;
	} aPasses[] = {
		{FX_DUST, GameClient()->m_ParticlesSkin.m_SpriteParticleSmoke},
		{FX_SPARK, GameClient()->m_ParticlesSkin.m_SpriteParticleBall},
		{FX_STAR, GameClient()->m_ExtrasSkin.m_SpriteParticleSparkle},
	};
	for(const auto &Pass : aPasses)
	{
		if(Pass.m_Texture.IsValid())
			Graphics()->TextureSet(Pass.m_Texture);
		else
			Graphics()->TextureClear();
		Graphics()->QuadsBegin();
		for(const SFx &Fx : m_vFx)
		{
			if(Fx.m_Type != Pass.m_Type || Fx.m_Life < 0.0f)
				continue;
			const float t = std::clamp(Fx.m_Life / Fx.m_LifeSpan, 0.0f, 1.0f);
			const float Size = mix(Fx.m_StartSize, Fx.m_EndSize, t);
			Graphics()->SetColor(Fx.m_Color.WithAlpha(Fx.m_Color.a * (1.0f - t)));
			Graphics()->QuadsSetRotation(Fx.m_Rot);
			IGraphics::CQuadItem QuadItem(Fx.m_Pos.x, Fx.m_Pos.y, Size, Size);
			Graphics()->QuadsDraw(&QuadItem, 1);
		}
		Graphics()->QuadsEnd();
	}
	Graphics()->QuadsSetRotation(0.0f);
}

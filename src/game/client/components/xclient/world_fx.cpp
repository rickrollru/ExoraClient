#include "world_fx.h"

#include "fx_renderer.h"
#include "xc_helpers.h"

#include <base/math.h>

#include <engine/client.h>
#include <engine/graphics.h>
#include <engine/shared/config.h>

#include <game/client/components/xclient/ui/theme.h>
#include <game/client/gameclient.h>

#include <cmath>
#include <vector>

using namespace XcHelpers;

void CWorldFx::OnReset()
{
	for(auto &State : m_aFreeze)
		State = SFreezeState();
	m_Particles.Clear();
}

void CWorldFx::OnMapLoad()
{
	OnReset();
}

ColorRGBA CWorldFx::FreezeColor(int ClientId, float Alpha) const
{
	return EffectColor(GameClient(), g_Config.m_XcFreezeColorMode, g_Config.m_XcFreezeColor, ClientId, 0.0f, Alpha);
}

void CWorldFx::FreezeBurst(vec2 Pos, int ClientId, bool Unfreeze)
{
	const ColorRGBA Color = FreezeColor(ClientId);
	// ice shards fly out, more and faster when the ice breaks
	const int Count = Unfreeze ? 14 : 9;
	for(int i = 0; i < Count; i++)
	{
		const float Angle = 2.0f * pi * i / Count + random_float(-0.25f, 0.25f);
		CXcParticles::SParticle Shard;
		Shard.m_Type = CXcParticles::EType::SHARD;
		Shard.m_Pos = Pos + direction(Angle) * random_float(8.0f, 16.0f);
		Shard.m_Vel = direction(Angle) * random_float(Unfreeze ? 220.0f : 120.0f, Unfreeze ? 380.0f : 220.0f);
		Shard.m_Gravity = 500.0f;
		Shard.m_Friction = 0.25f;
		Shard.m_LifeSpan = random_float(0.45f, 0.8f);
		Shard.m_StartSize = random_float(9.0f, 15.0f);
		Shard.m_EndSize = 4.0f;
		Shard.m_Rot = Angle + pi * 0.5f;
		Shard.m_RotSpeed = random_float(-9.0f, 9.0f);
		Shard.m_Color = Color;
		m_Particles.Add(Shard);
	}
	CXcParticles::SParticle Ring;
	Ring.m_Type = CXcParticles::EType::RING;
	Ring.m_Pos = Pos;
	Ring.m_LifeSpan = Unfreeze ? 0.4f : 0.5f;
	Ring.m_StartSize = Unfreeze ? 20.0f : 60.0f;
	Ring.m_EndSize = Unfreeze ? 75.0f : 26.0f; // the ice closes in, or bursts open
	Ring.m_Squash = 1.0f;
	Ring.m_Thickness = 5.0f;
	Ring.m_Color = Color;
	m_Particles.Add(Ring);
}

void CWorldFx::HammerHit(vec2 Pos)
{
	const int Style = g_Config.m_XcHammerFx;
	if(Style == HAMMER_OFF)
		return;
	const ColorRGBA Color = EffectColor(GameClient(), g_Config.m_XcHammerColorMode, g_Config.m_XcHammerColor, -1, random_float(0.0f, 0.2f));
	CXcParticles::SParticle Ring;
	Ring.m_Type = CXcParticles::EType::RING;
	Ring.m_Pos = Pos;
	Ring.m_LifeSpan = 0.36f;
	Ring.m_StartSize = 10.0f;
	Ring.m_EndSize = 64.0f;
	Ring.m_Squash = 1.0f;
	Ring.m_Thickness = 9.0f;
	Ring.m_Color = Color;
	m_Particles.Add(Ring);
	// a second, quicker and whiter ring inside makes the hit snap
	Ring.m_LifeSpan = 0.2f;
	Ring.m_StartSize = 4.0f;
	Ring.m_EndSize = 34.0f;
	Ring.m_Thickness = 6.0f;
	Ring.m_Color = ColorRGBA(mix(Color.r, 1.0f, 0.6f), mix(Color.g, 1.0f, 0.6f), mix(Color.b, 1.0f, 0.6f), Color.a);
	m_Particles.Add(Ring);
	if(Style == HAMMER_RING)
		return;
	for(int i = 0; i < 14; i++)
	{
		const float Angle = random_float(0.0f, 2.0f * pi);
		CXcParticles::SParticle Particle;
		Particle.m_Type = Style == HAMMER_STARS ? CXcParticles::EType::STAR : CXcParticles::EType::SPARK;
		Particle.m_Pos = Pos;
		Particle.m_Vel = direction(Angle) * random_float(200.0f, 430.0f);
		Particle.m_Gravity = Style == HAMMER_STARS ? 60.0f : 300.0f;
		Particle.m_Friction = 0.12f;
		Particle.m_LifeSpan = random_float(0.3f, 0.55f);
		Particle.m_StartSize = Style == HAMMER_STARS ? random_float(13.0f, 19.0f) : random_float(9.0f, 13.0f);
		Particle.m_EndSize = 0.0f;
		Particle.m_Rot = Angle;
		Particle.m_RotSpeed = random_float(-6.0f, 6.0f);
		Particle.m_Color = Color;
		m_Particles.Add(Particle);
	}
}

void CWorldFx::OnRender()
{
	if(Client()->State() != IClient::STATE_ONLINE && Client()->State() != IClient::STATE_DEMOPLAYBACK)
		return;
	const float Dt = std::min(Client()->RenderFrameTime(), 0.1f);
	const float Time = Client()->GlobalTime();
	CFxRenderer &Fx = GameClient()->m_FxRenderer;

	for(int ClientId = 0; ClientId < MAX_CLIENTS; ClientId++)
	{
		SFreezeState &State = m_aFreeze[ClientId];
		const CGameClient::CClientData &Data = GameClient()->m_aClients[ClientId];
		const bool Visible = g_Config.m_XcFreezeFx && GameClient()->m_Snap.m_aCharacters[ClientId].m_Active &&
				     ShowFor(GameClient(), ClientId, g_Config.m_XcFreezeSelf, g_Config.m_XcFreezeDummy, g_Config.m_XcFreezeOthers);
		const float Alpha = Visible ? PlayerAlpha(GameClient(), ClientId) : 0.0f;
		const bool Frozen = Visible && (Data.m_FreezeEnd != 0 || Data.m_RegularPredicted.m_FreezeEnd != 0);
		const vec2 Pos = Data.m_RenderPos;
		if(Frozen != State.m_Frozen && Visible && Alpha > 0.0f && g_Config.m_XcFreezeBurst)
			FreezeBurst(Pos, ClientId, !Frozen);
		State.m_Frozen = Frozen;
		if(!Frozen || Alpha <= 0.0f)
			continue;

		// ice aura: cold light, a slowly turning frost ring and snow drifting down
		const ColorRGBA Color = FreezeColor(ClientId, Alpha);
		const float Breath = 0.85f + 0.15f * std::sin(Time * 2.0f + ClientId);
		Fx.AddWorld(FxShape::Circle(Pos, 40.0f, Color.WithAlpha(0.0f)).RadialGradient(Color.WithAlpha(0.32f * Breath * Alpha), 1.3f));
		Fx.AddWorldGlowing(FxShape::Ring(Pos, vec2(34.0f, 34.0f), 2.0f, Color.WithAlpha(0.7f * Alpha)), 8.0f, 0.4f * Breath);
		for(int i = 0; i < 6; i++)
		{
			const float Angle = Time * 0.6f + i * pi / 3.0f;
			Fx.AddWorldGlowing(FxShape::Star(Pos + direction(Angle) * 34.0f, 4.5f, 6, 0.45f, Color.WithAlpha(0.9f * Alpha)).Rotate(-Angle * 2.0f), 4.0f, 0.4f);
		}
		State.m_SnowTimer -= Dt;
		if(State.m_SnowTimer <= 0.0f)
		{
			State.m_SnowTimer = random_float(0.12f, 0.25f);
			CXcParticles::SParticle Flake;
			Flake.m_Type = CXcParticles::EType::STAR;
			Flake.m_Pos = Pos + vec2(random_float(-30.0f, 30.0f), random_float(-34.0f, -10.0f));
			Flake.m_Vel = vec2(random_float(-8.0f, 8.0f), random_float(18.0f, 36.0f));
			Flake.m_Friction = 0.8f;
			Flake.m_LifeSpan = random_float(0.8f, 1.3f);
			Flake.m_StartSize = random_float(6.0f, 9.0f);
			Flake.m_EndSize = 2.0f;
			Flake.m_Rot = random_float(0.0f, pi);
			Flake.m_RotSpeed = random_float(-1.5f, 1.5f);
			Flake.m_Color = Color;
			m_Particles.Add(Flake);
		}
	}

	m_Particles.Update(Dt);
	m_Particles.Render(Fx);
}

bool CWorldFx::RenderCrosshair(vec2 Pos, float Alpha, float Scale)
{
	const int Style = g_Config.m_XcCrosshair;
	if(Style == CROSSHAIR_DEFAULT)
		return false;
	const float Size = g_Config.m_XcCrosshairSize / 100.0f * Scale;
	const float Gap = g_Config.m_XcCrosshairGap / 100.0f * 8.0f * Size;
	const ColorRGBA Color = EffectColor(GameClient(), g_Config.m_XcCrosshairColorMode, g_Config.m_XcCrosshairColor, -1, 0.0f, Alpha);
	const ColorRGBA Shadow(0.0f, 0.0f, 0.0f, 0.55f * Alpha);
	const float Line = 3.0f * Size;
	const float Arm = 10.0f * Size;
	std::vector<SFxShape> vShapes;
	// a dark outline first keeps the crosshair visible on any background
	for(int Pass = g_Config.m_XcCrosshairOutline ? 0 : 1; Pass < 2; Pass++)
	{
		const bool Outline = Pass == 0;
		const ColorRGBA c = Outline ? Shadow : Color;
		const float Extra = Outline ? 2.0f * Size : 0.0f;
		const auto Add = [&](const SFxShape &Shape) {
			if(!Outline && g_Config.m_XcCrosshairGlow)
				CFxRenderer::AppendGlowing(vShapes, Shape, 6.0f * Size, 0.5f);
			else
				vShapes.push_back(Shape);
		};
		if(Style == CROSSHAIR_CROSS)
		{
			for(int i = 0; i < 4; i++)
			{
				const vec2 Dir = direction(i * pi * 0.5f);
				Add(FxShape::Line(Pos + Dir * Gap, Pos + Dir * (Gap + Arm), Line + Extra, c));
			}
		}
		if(Style == CROSSHAIR_CIRCLE || Style == CROSSHAIR_CIRCLE_DOT)
			Add(FxShape::Ring(Pos, vec2(Gap + Arm * 0.6f, Gap + Arm * 0.6f), Line * 0.8f + Extra, c));
		if(Style == CROSSHAIR_DOT || Style == CROSSHAIR_CIRCLE_DOT || Style == CROSSHAIR_CROSS)
			Add(FxShape::Circle(Pos, Line * (Style == CROSSHAIR_DOT ? 1.3f : 0.75f) + Extra * 0.5f, c));
	}
	GameClient()->m_FxRenderer.Draw(vShapes, false);
	return true;
}

void CScreenFx::OnRender()
{
	if(Client()->State() != IClient::STATE_ONLINE && Client()->State() != IClient::STATE_DEMOPLAYBACK)
		return;
	const int Tint = g_Config.m_XcWorldTint;
	const float Vignette = g_Config.m_XcVignette / 100.0f;
	if(Tint == TINT_OFF && Vignette <= 0.0f)
		return;
	const float Width = (float)Graphics()->ScreenWidth();
	const float Height = (float)Graphics()->ScreenHeight();
	const CScreenRect SavedScreen = Graphics()->GetScreen();
	Graphics()->MapScreen(CScreenRect(0.0f, 0.0f, Width, Height));
	Graphics()->TextureClear();

	if(Tint != TINT_OFF)
	{
		// a thin layer of color over the world
		const float Strength = g_Config.m_XcWorldTintStrength / 100.0f;
		ColorRGBA Color;
		switch(Tint)
		{
		case TINT_WARM: Color = ColorRGBA(1.0f, 0.62f, 0.25f, 0.22f); break;
		case TINT_COOL: Color = ColorRGBA(0.25f, 0.55f, 1.0f, 0.2f); break;
		case TINT_NIGHT: Color = ColorRGBA(0.05f, 0.07f, 0.2f, 0.45f); break;
		default:
		{
			const ImVec4 Accent = XcUi::AccentColor(g_Config.m_XcUiAccent, true);
			Color = ColorRGBA(Accent.x, Accent.y, Accent.z, 0.18f);
			break;
		}
		}
		Graphics()->QuadsBegin();
		Graphics()->SetColor(Color.WithMultipliedAlpha(Strength));
		const IGraphics::CQuadItem Item(0.0f, 0.0f, Width, Height);
		Graphics()->QuadsDrawTL(&Item, 1);
		Graphics()->QuadsEnd();
	}

	if(Vignette > 0.0f)
	{
		// dark corners: a ring of quads from clear inside to dark outside, around an ellipse
		const vec2 Center(Width * 0.5f, Height * 0.5f);
		const vec2 Inner = vec2(Width, Height) * 0.36f;
		const vec2 Outer = vec2(Width, Height) * 0.78f;
		const ColorRGBA Clear(0.0f, 0.0f, 0.0f, 0.0f);
		const ColorRGBA Dark(0.0f, 0.0f, 0.0f, 0.75f * Vignette);
		const int Segments = 64;
		Graphics()->QuadsBegin();
		for(int i = 0; i < Segments; i++)
		{
			const vec2 a = direction(2.0f * pi * i / Segments);
			const vec2 b = direction(2.0f * pi * (i + 1) / Segments);
			Graphics()->SetColor4(Clear, Clear, Dark, Dark);
			const IGraphics::CFreeformItem Item(Center + a * Inner, Center + b * Inner, Center + a * Outer, Center + b * Outer);
			Graphics()->QuadsDrawFreeform(&Item, 1);
		}
		Graphics()->QuadsEnd();
	}
	Graphics()->MapScreen(SavedScreen);
}

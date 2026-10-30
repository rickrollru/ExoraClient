#include "accessory.h"

#include "fx_renderer.h"
#include "sprite_fx.h"
#include "xc_helpers.h"

#include <base/math.h>

#include <engine/client.h>
#include <engine/graphics.h>
#include <engine/shared/config.h>

#include <game/client/gameclient.h>

#include <algorithm>
#include <cmath>
#include <iterator>

using namespace XcHelpers;

// distance from the tee center to the top of its head at the default tee size
static constexpr float HEAD_OFFSET = 27.0f;

// spring that lets the accessory lag behind sudden vertical speed changes
static constexpr float SPRING_STIFFNESS = 250.0f;
static constexpr float SPRING_DAMPING = 11.0f;
static constexpr float SPRING_IMPULSE = 0.12f;
static constexpr float SPRING_MAX = 8.0f;

void CAccessory::OnReset()
{
	for(auto &State : m_aStates)
		State = SState();
}

void CAccessory::OnMapLoad()
{
	OnReset();
}

void CAccessory::UpdateState(int ClientId, vec2 Pos, float Dt)
{
	SState &State = m_aStates[ClientId];
	const vec2 Vel = CharacterVelocity(GameClient(), ClientId);
	const float Physics = g_Config.m_XcAccessoryPhysics / 100.0f;

	if(!State.m_Valid || distance(State.m_Pos, Pos) > 200.0f)
	{
		State = SState();
		State.m_Valid = true;
		State.m_Pos = Pos;
		State.m_LastVel = Vel;
		return;
	}

	// lean into the running direction
	const float TargetTilt = std::clamp(Vel.x / 2000.0f, -0.3f, 0.3f) * Physics;
	State.m_Tilt = mix(State.m_Tilt, TargetTilt, SmoothFactor(8.0f, Dt));

	// a jump or a landing pushes the accessory down, then it springs back
	const float DeltaVelY = Vel.y - State.m_LastVel.y;
	State.m_SpringVel += -DeltaVelY * SPRING_IMPULSE * Physics;

	// fixed small steps keep the spring stable on any frame rate
	constexpr float STEP = 1.0f / 240.0f;
	float Remaining = Dt;
	while(Remaining > 0.0f)
	{
		const float h = std::min(Remaining, STEP);
		const float Accel = -SPRING_STIFFNESS * State.m_Spring - SPRING_DAMPING * State.m_SpringVel;
		State.m_SpringVel += Accel * h;
		State.m_Spring += State.m_SpringVel * h;
		Remaining -= h;
	}
	if(std::abs(State.m_Spring) > SPRING_MAX)
	{
		State.m_Spring = std::clamp(State.m_Spring, -SPRING_MAX, SPRING_MAX);
		State.m_SpringVel = 0.0f;
	}

	State.m_Pos = Pos;
	State.m_LastVel = Vel;
}

void CAccessory::OnRender()
{
	if(Client()->State() != IClient::STATE_ONLINE && Client()->State() != IClient::STATE_DEMOPLAYBACK)
		return;
	if(g_Config.m_XcAccessory == ACCESSORY_OFF)
	{
		OnReset();
		return;
	}

	const float Dt = std::min(Client()->RenderFrameTime(), 0.1f);
	m_Time += Dt;

	for(int ClientId = 0; ClientId < MAX_CLIENTS; ClientId++)
	{
		SState &State = m_aStates[ClientId];
		if(!GameClient()->m_Snap.m_aCharacters[ClientId].m_Active ||
			(!ShowFor(GameClient(), ClientId, g_Config.m_XcAccessorySelf, g_Config.m_XcAccessoryDummy, g_Config.m_XcAccessoryOthers) &&
				!(g_Config.m_XcAccessoryFriends && GameClient()->m_aClients[ClientId].m_Friend)))
		{
			State.m_Valid = false;
			continue;
		}
		const float Alpha = PlayerAlpha(GameClient(), ClientId) * g_Config.m_XcAccessoryAlpha / 100.0f;
		if(Alpha <= 0.0f)
		{
			State.m_Valid = false;
			continue;
		}

		const CGameClient::CClientData &Data = GameClient()->m_aClients[ClientId];
		const vec2 Pos = Data.m_RenderPos;
		UpdateState(ClientId, Pos, Dt);
		m_ColorClient = ClientId;

		const float TeeScale = Data.m_RenderInfo.m_Size / 64.0f;
		const float Scale = TeeScale * g_Config.m_XcAccessorySize / 100.0f;
		const float Spring = State.m_Spring * Scale;
		// pushed down = squashed, pulled up = stretched
		const float Squash = std::clamp(1.0f - State.m_Spring / SPRING_MAX * 0.2f, 0.8f, 1.2f);
		// the head moves around the tee center when tilting
		const vec2 Head = Rotate(Pos + vec2(0.0f, -HEAD_OFFSET * TeeScale + Spring), Pos, State.m_Tilt);

		switch(g_Config.m_XcAccessory)
		{
		case ACCESSORY_CONE: RenderCone(Head, Scale, State.m_Tilt, Squash, Alpha); break;
		case ACCESSORY_HALO: RenderHalo(Head + vec2(0.0f, std::sin(m_Time * 2.2f + ClientId) * 2.0f * Scale), Scale, State.m_Tilt, Squash, Alpha); break;
		case ACCESSORY_CROWN: RenderCrown(Head, Scale, State.m_Tilt, Squash, Alpha); break;
		}
	}
}

ColorRGBA CAccessory::BaseColor(float Offset, float Alpha) const
{
	return EffectColor(GameClient(), g_Config.m_XcAccessoryColorMode, g_Config.m_XcAccessoryColor, m_ColorClient, Offset, Alpha);
}

static ColorRGBA Shade(ColorRGBA Color, float Factor)
{
	return ColorRGBA(std::min(Color.r * Factor, 1.0f), std::min(Color.g * Factor, 1.0f), std::min(Color.b * Factor, 1.0f), Color.a);
}

// Seen a little from above: a circle around the head becomes a flat ellipse, its lower half is the front.
static constexpr float DEPTH = 0.3f;

void CAccessory::RenderCone(vec2 Head, float Scale, float Tilt, float Squash, float Alpha) const
{
	const FxGlow::CScope GlowScope(FxGlow::ACCESSORY);
	CFxRenderer &Fx = GameClient()->m_FxRenderer;
	const float Radius = 30.0f * Scale;
	const float Height = 24.0f * Scale * Squash * g_Config.m_XcAccessoryHeight / 100.0f;
	const auto P = [&](float x, float y) { return Rotate(Head + vec2(x, y), Head, Tilt); };
	const float BaseY = 4.0f * Scale;
	const vec2 Base = P(0.0f, BaseY), Tip = P(0.0f, BaseY - Height);
	const vec2 Brim = vec2(Radius, Radius * DEPTH);
	const ColorRGBA Main = BaseColor(0.0f, Alpha), Edge = BaseColor(0.5f, Alpha);
	const float Spin = m_Time * 0.9f * g_Config.m_XcAccessorySpin / 100.0f + m_ColorClient * 0.7f;

	// the far half of the rim, under everything
	Fx.AddWorldGlowing(FxShape::Ring(Base, Brim, 1.8f * Scale, Shade(Edge, 0.8f).WithMultipliedAlpha(0.7f)).Half(-1.0f).Rotate(Tilt), 4.0f * Scale, 0.3f);
	// see-through brim; the cone itself is only its stripes (a filled triangle showed hard edges)
	Fx.AddWorld(FxShape::Ellipse(Base, Brim, Shade(Main, 0.75f).WithMultipliedAlpha(0.3f)).Soft(2.0f * Scale).Rotate(Tilt));
	// stripes from the tip to the rim that turn round the cone; the ones in front are bright, the ones going away fade
	const int NumStripes = 10;
	for(int i = 0; i < NumStripes; i++)
	{
		const float a = Spin + 2.0f * pi * i / NumStripes;
		const float Front = std::sin(a); // 1 = straight at us
		if(Front < -0.15f)
			continue;
		const vec2 OnRim = P(std::cos(a) * Radius, BaseY + std::sin(a) * Radius * DEPTH);
		Fx.AddWorld(FxShape::Line(Tip, OnRim, 1.1f * Scale, Shade(Edge, 1.3f).WithMultipliedAlpha(0.15f + 0.5f * std::max(Front, 0.0f))).Additive());
	}
	// light from the upper left: a shine down the left flank
	const float ShineA = pi * 0.72f;
	Fx.AddWorld(FxShape::Line(P(0.0f, BaseY - Height + 2.0f * Scale), P(std::cos(ShineA) * Radius * 0.8f, BaseY + std::sin(ShineA) * Radius * DEPTH * 0.8f), 2.2f * Scale, ColorRGBA(1.0f, 1.0f, 1.0f, 0.22f * Alpha)).Additive());
	// the near half of the rim glows on top, and a small light at the tip
	Fx.AddWorldGlowing(FxShape::Ring(Base, Brim, 2.2f * Scale, Shade(Edge, 1.35f)).Half(1.0f).Rotate(Tilt), 6.0f * Scale, 0.6f);
	Fx.AddWorldGlowing(FxShape::Circle(Tip, 1.8f * Scale, Shade(Main, 1.6f)), 5.0f * Scale, 0.6f);
}

void CAccessory::RenderHalo(vec2 Head, float Scale, float Tilt, float Squash, float Alpha) const
{
	const FxGlow::CScope GlowScope(FxGlow::ACCESSORY);
	const float Pulse = 1.0f + 0.08f * std::sin(m_Time * 2.4f + m_ColorClient);
	const vec2 Center = Rotate(Head + vec2(0.0f, -14.0f * Scale * Squash), Head, Tilt);
	const vec2 Radius = vec2(15.0f, 15.0f * 0.32f * Squash) * Scale;
	const ColorRGBA Color = BaseColor(0.0f, Alpha);
	const ColorRGBA Core = ColorRGBA(mix(Color.r, 1.0f, 0.65f), mix(Color.g, 1.0f, 0.65f), mix(Color.b, 1.0f, 0.65f), Alpha);
	CFxRenderer &Fx = GameClient()->m_FxRenderer;
	const float Spin = m_Time * 1.6f * g_Config.m_XcAccessorySpin / 100.0f + m_ColorClient * 1.3f;
	const float c = std::cos(Tilt), s = std::sin(Tilt);
	const auto OnRing = [&](float a, float Grow) {
		const vec2 Local(std::cos(a) * Radius.x * Grow, std::sin(a) * Radius.y * Grow);
		return Center + vec2(Local.x * c - Local.y * s, Local.x * s + Local.y * c);
	};

	// a tube of light: the far half thinner and dimmer, the near half with a white-hot core
	Fx.AddWorld(FxShape::Ring(Center, Radius, 2.6f * Scale * Pulse, Color.WithMultipliedAlpha(0.65f)).Half(-1.0f).Rotate(Tilt));
	Fx.AddWorld(FxShape::Ring(Center, Radius, 3.4f * Scale * Pulse, Color).Half(1.0f).Rotate(Tilt));
	Fx.AddWorld(FxShape::Ring(Center, Radius, 1.2f * Scale, Core).Half(1.0f).Rotate(Tilt).Additive());

	const FxGlow::SScale Glow = FxGlow::g_Current;
	CXcSprites &Sprites = GameClient()->m_XcSprites;
	// the glow is built from soft lights along the ring, so it is equally wide all around;
	// the glow of the flattened ring itself would be flattened too: thin on top, thick at the sides
	if(Glow.m_Strength > 0.0f && Glow.m_Radius > 0.0f)
	{
		const int NumLights = 18;
		for(int i = 0; i < NumLights; i++)
		{
			const vec2 Pos = OnRing(2.0f * pi * i / NumLights, 1.0f);
			if(Sprites.Enabled())
			{
				// a colored layer keeps the glow visible on bright maps, the light is added on top
				Sprites.Queue(CXcSprites::SPRITE_GLOW, false, Pos, 13.0f * Scale * Glow.m_Radius * Pulse, 0.0f, Color.WithMultipliedAlpha(std::min(0.16f * Glow.m_Strength, 0.5f)));
				Sprites.Queue(CXcSprites::SPRITE_GLOW, true, Pos, 18.0f * Scale * Glow.m_Radius * Pulse, 0.0f, Color.WithMultipliedAlpha(0.4f * Glow.m_Strength));
			}
			else
				Fx.AddWorld(FxShape::Circle(Pos, 1.0f * Scale, Color).GlowOnly(5.0f * Scale, 0.18f).Additive()); // Glow() applies the scope
		}
	}
	// a spark that runs round the ring, bigger when it passes in front
	{
		const float Front = std::sin(Spin);
		const vec2 Pos = OnRing(Spin, 1.0f);
		Fx.AddWorldGlowing(FxShape::Circle(Pos, (1.6f + 0.8f * Front) * Scale, Core.WithMultipliedAlpha(0.55f + 0.45f * std::max(Front, 0.0f))).Additive(), 6.0f * Scale, 0.8f);
	}
	if(!g_Config.m_XcAccessorySparkles)
		return;
	// three sparkles flying round it, a little further out, bobbing and twinkling
	for(int i = 0; i < 3; i++)
	{
		const float a = -Spin * 0.6f + 2.0f * pi * i / 3.0f;
		const float Front = std::sin(a);
		const vec2 Pos = OnRing(a, 1.35f) + vec2(0.0f, std::sin(m_Time * 3.0f + i * 2.1f) * 2.5f * Scale);
		const float Twinkle = 0.55f + 0.45f * std::sin(m_Time * 7.0f + i * 1.7f);
		const float Size = (2.2f + 0.8f * Front) * Scale * Twinkle;
		Fx.AddWorldGlowing(FxShape::Star(Pos, Size, 4, 0.35f, Core.WithMultipliedAlpha((0.45f + 0.4f * std::max(Front, 0.0f)) * Twinkle)).Rotate(m_Time * 1.5f + i).Additive(), 3.0f * Scale, 0.5f);
	}
}

void CAccessory::RenderCrown(vec2 Head, float Scale, float Tilt, float Squash, float Alpha) const
{
	const FxGlow::CScope GlowScope(FxGlow::ACCESSORY);
	CFxRenderer &Fx = GameClient()->m_FxRenderer;
	// the crown is drawn a bit bigger than the other accessories to match their visual weight
	Scale *= 1.3f;
	// rich gold that goes dark at the bottom: stands out on yellow maps too
	const ColorRGBA GoldTop(1.0f, 0.84f, 0.32f, Alpha);
	const ColorRGBA GoldBottom(0.7f, 0.4f, 0.06f, Alpha);
	const ColorRGBA Outline(0.3f, 0.17f, 0.02f, Alpha);
	const float Radius = 12.5f * Scale;
	const float Depth = Radius * 0.35f;
	const float Bottom = 5.0f * Scale;
	const float Top = Bottom - 7.0f * Scale * Squash;
	const float Border = 1.3f * Scale;
	const auto P = [&](float x, float y) { return Rotate(Head + vec2(x, y), Head, Tilt); };
	const ColorRGBA Gem = Shade(BaseColor(0.0f, Alpha), 1.1f);
	// turns slowly with the spin setting, the spikes go round
	const float Turn = m_Time * 0.25f * g_Config.m_XcAccessorySpin / 100.0f + m_ColorClient * 0.5f;

	struct SSpike
	{
		float m_Front; // -1 far .. 1 near
		float m_X;
	};
	SSpike aSpikes[5];
	for(int i = 0; i < 5; i++)
	{
		const float a = Turn + 2.0f * pi * i / 5.0f;
		aSpikes[i] = {std::sin(a), std::cos(a) * Radius};
	}
	std::sort(std::begin(aSpikes), std::end(aSpikes), [](const SSpike &A, const SSpike &B) { return A.m_Front < B.m_Front; });
	const auto Spike = [&](const SSpike &S) {
		const float Near = (S.m_Front + 1.0f) * 0.5f; // 0 far, 1 near
		const float Width = (5.5f + 2.0f * Near) * Scale * (0.45f + 0.55f * std::sqrt(std::max(1.0f - (S.m_X / Radius) * (S.m_X / Radius), 0.0f)));
		const float Height = (9.5f + 3.5f * Near) * Scale * Squash;
		const float BaseY = Top + S.m_Front * Depth * 0.5f + 1.0f * Scale;
		const float Light = 0.7f + 0.3f * Near;
		Fx.AddWorld(FxShape::Triangle(P(S.m_X, BaseY - Height * 0.5f), vec2(Width, Height * 0.5f), Shade(GoldTop, Light))
				    .Gradient(Shade(GoldBottom, Light))
				    .Outline(Border, Outline)
				    .Rotate(Tilt));
		// light catches the left flank of the near spikes
		if(Near > 0.4f)
			Fx.AddWorld(FxShape::Line(P(S.m_X - Width * 0.45f, BaseY - 1.0f * Scale), P(S.m_X - 0.4f * Scale, BaseY - Height + 1.5f * Scale), 0.9f * Scale, ColorRGBA(1.0f, 1.0f, 0.9f, 0.5f * Alpha * Near)).Additive());
		// a pearl on the tip
		Fx.AddWorldGlowing(FxShape::Circle(P(S.m_X, BaseY - Height - 0.4f * Scale), (1.1f + 0.4f * Near) * Scale, Shade(Gem, 0.7f + 0.4f * Near)).Outline(0.5f * Scale, Outline), 2.5f * Scale, 0.4f * Near);
	};
	// the far spikes, the inside of the band, the band, its round bottom, the near spikes
	for(const SSpike &S : aSpikes)
		if(S.m_Front < 0.0f)
			Spike(S);
	Fx.AddWorld(FxShape::Ellipse(P(0.0f, Top), vec2(Radius, Depth * 0.5f), ColorRGBA(0.35f, 0.2f, 0.05f, Alpha)).Outline(Border, Outline).Rotate(Tilt));
	Fx.AddWorld(FxShape::Ellipse(P(0.0f, Bottom), vec2(Radius, Depth * 0.5f), GoldBottom).Outline(Border, Outline).Rotate(Tilt));
	Fx.AddWorld(FxShape::RoundRect(P(0.0f, (Top + Bottom) * 0.5f), vec2(Radius, (Bottom - Top) * 0.5f), 0.5f * Scale, GoldTop).Gradient(GoldBottom).Rotate(Tilt));
	// the front rim of the band as a line, and a shine along it
	Fx.AddWorld(FxShape::Ring(P(0.0f, Top), vec2(Radius, Depth * 0.5f), Border * 1.2f, Outline).Half(1.0f).Rotate(Tilt));
	Fx.AddWorld(FxShape::Line(P(-Radius * 0.75f, Top + 2.2f * Scale), P(Radius * 0.35f, Top + 2.2f * Scale), 1.0f * Scale, ColorRGBA(1.0f, 1.0f, 1.0f, 0.45f * Alpha)).Additive());
	for(const SSpike &S : aSpikes)
		if(S.m_Front >= 0.0f)
			Spike(S);
	// gems on the band: one big in the middle, two small at the sides
	const float GemY = (Top + Bottom) * 0.5f + Depth * 0.3f;
	const float aGemX[] = {0.0f, -Radius * 0.6f, Radius * 0.6f};
	for(int i = 0; i < 3; i++)
	{
		const vec2 At = P(aGemX[i], GemY - (i ? 0.4f * Scale : 0.0f));
		const float Size = (i == 0 ? 2.1f : 1.4f) * Scale;
		Fx.AddWorldGlowing(FxShape::Circle(At, Size, Gem).Outline(0.6f * Scale, Shade(Gem, 0.5f)), 3.5f * Scale, 0.5f);
		Fx.AddWorld(FxShape::Circle(At + vec2(-0.35f, -0.35f) * Size, Size * 0.3f, ColorRGBA(1.0f, 1.0f, 1.0f, Alpha * 0.85f)).Additive());
		// now and then a gem flashes with a little star
		if(g_Config.m_XcAccessorySparkles)
		{
			const float Flash = std::pow(std::max(std::sin(m_Time * 1.7f + i * 2.3f + m_ColorClient), 0.0f), 12.0f);
			if(Flash > 0.02f)
				Fx.AddWorldGlowing(FxShape::Star(At + vec2(0.6f, -0.6f) * Size, Size * 2.2f * Flash, 4, 0.25f, ColorRGBA(1.0f, 1.0f, 1.0f, Alpha * Flash)).Rotate(pi / 4.0f).Additive(), 3.0f * Scale, 0.6f * Flash);
		}
	}
}

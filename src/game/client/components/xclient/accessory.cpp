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
#include <vector>

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

		RenderOne(Head, Scale, State.m_Tilt, Squash, Alpha, ClientId);
	}
}

void CAccessory::RenderOne(vec2 Head, float Scale, float Tilt, float Squash, float Alpha, int Seed)
{
	switch(g_Config.m_XcAccessory)
	{
	case ACCESSORY_CONE: RenderCone(Head, Scale, Tilt, Squash, Alpha); break;
	case ACCESSORY_HALO: RenderHalo(Head + vec2(0.0f, std::sin(m_Time * 2.2f + Seed) * 2.0f * Scale), Scale, Tilt, Squash, Alpha); break;
	case ACCESSORY_CROWN: RenderCrown(Head, Scale, Tilt, Squash, Alpha); break;
	}
}

void CAccessory::RenderPreview(vec2 Pos, float TeeSize, float Alpha)
{
	if(g_Config.m_XcAccessory == ACCESSORY_OFF || !g_Config.m_XcAccessorySelf)
		return;
	// the menu has its own clock, the one of the game goes on unchanged
	const float GameTime = m_Time;
	m_Time = Client()->GlobalTime();
	m_ColorClient = -1;
	const float TeeScale = TeeSize / 64.0f;
	const float Scale = TeeScale * g_Config.m_XcAccessorySize / 100.0f;
	m_Preview = true;
	m_vPreviewShapes.clear();
	RenderOne(Pos + vec2(0.0f, -HEAD_OFFSET * TeeScale), Scale, 0.0f, 1.0f, Alpha * g_Config.m_XcAccessoryAlpha / 100.0f, 0);
	m_Preview = false;
	GameClient()->m_FxRenderer.Draw(m_vPreviewShapes, false);
	m_vPreviewShapes.clear();
	m_Time = GameTime;
}

ColorRGBA CAccessory::BaseColor(float Offset, float Alpha) const
{
	return EffectColor(GameClient(), g_Config.m_XcAccessoryColorMode, g_Config.m_XcAccessoryColor, m_ColorClient, Offset, Alpha);
}

// the color of the edges, rims and stripes: the main color made lighter, or an own one
ColorRGBA CAccessory::EdgeColor(float Alpha) const
{
	if(g_Config.m_XcAccessoryEdgeMode)
		return color_cast<ColorRGBA>(ColorHSLA(g_Config.m_XcAccessoryEdgeColor)).WithAlpha(Alpha);
	const ColorRGBA Main = BaseColor(0.4f, Alpha);
	return ColorRGBA(mix(Main.r, 1.0f, 0.45f), mix(Main.g, 1.0f, 0.45f), mix(Main.b, 1.0f, 0.45f), Alpha);
}

void CAccessory::Add(const SFxShape &Shape)
{
	if(m_Preview)
		m_vPreviewShapes.push_back(Shape);
	else
		GameClient()->m_FxRenderer.AddWorld(Shape);
}

void CAccessory::AddGlowing(const SFxShape &Shape, float Radius, float Strength)
{
	if(m_Preview)
		CFxRenderer::AppendGlowing(m_vPreviewShapes, Shape, Radius, Strength);
	else
		GameClient()->m_FxRenderer.AddWorldGlowing(Shape, Radius, Strength);
}

void CAccessory::Light(vec2 Pos, float Size, ColorRGBA Color, float Strength)
{
	const FxGlow::SScale Glow = FxGlow::g_Current;
	if(Glow.m_Strength <= 0.0f || Glow.m_Radius <= 0.0f || Strength <= 0.0f)
		return;
	CXcSprites &Sprites = GameClient()->m_XcSprites;
	if(!m_Preview && Sprites.Enabled())
	{
		// a colored layer keeps the light visible on bright maps, the light is added on top
		Sprites.Queue(CXcSprites::SPRITE_GLOW, false, Pos, Size * 0.72f * Glow.m_Radius, 0.0f, Color.WithMultipliedAlpha(std::min(0.16f * Glow.m_Strength * Strength, 0.5f)));
		Sprites.Queue(CXcSprites::SPRITE_GLOW, true, Pos, Size * Glow.m_Radius, 0.0f, Color.WithMultipliedAlpha(0.4f * Glow.m_Strength * Strength));
	}
	else
		Add(FxShape::Circle(Pos, Size * 0.06f, Color).GlowOnly(Size * 0.3f, 0.2f * Strength).Additive()); // Glow() applies the scope
}

static ColorRGBA Shade(ColorRGBA Color, float Factor)
{
	return ColorRGBA(std::min(Color.r * Factor, 1.0f), std::min(Color.g * Factor, 1.0f), std::min(Color.b * Factor, 1.0f), Color.a);
}

static ColorRGBA Lighter(ColorRGBA Color, float Amount)
{
	return ColorRGBA(mix(Color.r, 1.0f, Amount), mix(Color.g, 1.0f, Amount), mix(Color.b, 1.0f, Amount), Color.a);
}

// Seen a little from above: a circle around the head becomes a flat ellipse, its lower half is the front.
static constexpr float DEPTH = 0.3f;

void CAccessory::RenderCone(vec2 Head, float Scale, float Tilt, float Squash, float Alpha)
{
	const FxGlow::CScope GlowScope(FxGlow::ACCESSORY);
	const int Look = g_Config.m_XcAccessoryLook;
	const int Shape = g_Config.m_XcAccessoryShape;
	const int Pattern = g_Config.m_XcAccessoryPattern;
	// wide and flat, tall and narrow, or two tiers
	const float Radius = (Shape == 1 ? 20.0f : 30.0f) * Scale;
	const float Height = (Shape == 1 ? 40.0f : (Shape == 2 ? 30.0f : 24.0f)) * Scale * Squash * g_Config.m_XcAccessoryHeight / 100.0f;
	const auto P = [&](float x, float y) { return Rotate(Head + vec2(x, y), Head, Tilt); };
	const float BaseY = 4.0f * Scale;
	const vec2 Base = P(0.0f, BaseY), Tip = P(0.0f, BaseY - Height);
	const vec2 Brim = vec2(Radius, Radius * DEPTH);
	// a point on the cone: round angle a (sin > 0 is in front), height t from the brim (0) to the tip (1)
	const auto On = [&](float a, float t) { return P(std::cos(a) * Radius * (1.0f - t), BaseY - Height * t + std::sin(a) * Radius * DEPTH * (1.0f - t)); };
	const ColorRGBA Main = BaseColor(0.0f, Alpha), Edge = EdgeColor(Alpha);
	const float Spin = m_Time * 0.9f * g_Config.m_XcAccessorySpin / 100.0f + m_ColorClient * 0.7f;
	const ColorRGBA Dark = Look == LOOK_SOLID ? Shade(Main, 0.4f) : Edge;

	// the far half of the rim, under everything
	if(Look == LOOK_SOLID)
		Add(FxShape::Ring(Base, Brim, 1.4f * Scale, Dark).Half(-1.0f).Rotate(Tilt));
	else
		AddGlowing(FxShape::Ring(Base, Brim, 1.8f * Scale, Shade(Edge, 0.8f).WithMultipliedAlpha(Look == LOOK_NEON ? 0.9f : 0.7f)).Half(-1.0f).Rotate(Tilt), 4.0f * Scale, 0.3f);

	// the body: light in the brim only (energy), a dark shell (neon) or the material (solid)
	if(Look == LOOK_ENERGY)
		Add(FxShape::Ellipse(Base, Brim, Shade(Main, 0.75f).WithMultipliedAlpha(0.3f)).Soft(2.0f * Scale).Rotate(Tilt));
	else
	{
		const ColorRGBA Top = Look == LOOK_NEON ? Shade(Main, 0.28f) : (Pattern == PATTERN_GRADIENT ? Lighter(Edge, 0.2f) : Lighter(Main, 0.25f));
		const ColorRGBA Bottom = Look == LOOK_NEON ? Shade(Main, 0.12f) : Shade(Main, 0.7f);
		Add(FxShape::Ellipse(Base, Brim, Bottom.WithMultipliedAlpha(Look == LOOK_NEON ? 0.9f : 1.0f)).Rotate(Tilt));
		Add(FxShape::Triangle((Base + Tip) * 0.5f, vec2(Radius, Height * 0.5f), Top.WithMultipliedAlpha(Look == LOOK_NEON ? 0.9f : 1.0f)).Gradient(Bottom.WithMultipliedAlpha(Look == LOOK_NEON ? 0.9f : 1.0f)).Rotate(Tilt));
	}

	// the pattern on the front of the cone
	const auto PatternLine = [&](vec2 From, vec2 To, float Width, float Strength) {
		if(Look == LOOK_SOLID)
			Add(FxShape::Line(From, To, Width, Dark.WithMultipliedAlpha(0.55f * Strength)));
		else if(Look == LOOK_NEON)
			AddGlowing(FxShape::Line(From, To, Width, Edge.WithMultipliedAlpha(Strength)), 3.0f * Scale, 0.4f * Strength);
		else
			Add(FxShape::Line(From, To, Width, Shade(Edge, 1.3f).WithMultipliedAlpha(Strength)).Additive());
	};
	if(Pattern == PATTERN_STRIPES)
	{
		// stripes from the tip to the rim that turn round the cone; the ones in front are bright, the ones going away fade
		const int NumStripes = 10;
		for(int i = 0; i < NumStripes; i++)
		{
			const float a = Spin + 2.0f * pi * i / NumStripes;
			const float Front = std::sin(a);
			if(Front < (Look == LOOK_ENERGY ? -0.15f : 0.05f))
				continue;
			PatternLine(Tip, On(a, 0.0f), 1.1f * Scale, 0.15f + 0.5f * std::max(Front, 0.0f));
		}
	}
	else if(Pattern == PATTERN_RINGS)
	{
		// rings round the cone that climb to the tip and fade there
		const int NumRings = 4;
		for(int i = 0; i < NumRings; i++)
		{
			const float t = std::fmod((float)i / NumRings + Spin * 0.12f, 1.0f) * 0.9f;
			const float r = Radius * (1.0f - t);
			const float Fade = std::sin(t / 0.9f * pi);
			const vec2 At = P(0.0f, BaseY - Height * t);
			const ColorRGBA c = Look == LOOK_SOLID ? Dark.WithMultipliedAlpha(0.55f * Fade) : Edge.WithMultipliedAlpha(0.7f * Fade);
			if(Look == LOOK_NEON)
				AddGlowing(FxShape::Ring(At, vec2(r, r * DEPTH), 1.0f * Scale, c).Half(1.0f).Rotate(Tilt), 3.0f * Scale, 0.4f * Fade);
			else if(Look == LOOK_ENERGY)
				Add(FxShape::Ring(At, vec2(r, r * DEPTH), 1.2f * Scale, c).Half(1.0f).Rotate(Tilt).Additive());
			else
				Add(FxShape::Ring(At, vec2(r, r * DEPTH), 1.0f * Scale, c).Half(1.0f).Rotate(Tilt));
		}
	}
	else if(Pattern == PATTERN_GRADIENT && Look == LOOK_ENERGY)
	{
		// light that pours down from the tip
		for(int i = 0; i < 5; i++)
		{
			const float t = 0.15f + 0.17f * i;
			Light(P(0.0f, BaseY - Height * t), Radius * (1.2f - t) * 1.3f, Lighter(Main, t * 0.6f), 0.35f + 0.25f * t);
		}
	}

	// the outline of the cone: two lines from the tip down to the sides of the rim
	for(float Side : {-1.0f, 1.0f})
		PatternLine(Tip, P(Side * Radius, BaseY), (Look == LOOK_SOLID ? 1.0f : 1.4f) * Scale, Look == LOOK_SOLID ? 1.0f : 0.8f);
	if(Look == LOOK_SOLID)
	{
		// light from the upper left: a shine down the left flank
		const float ShineA = pi * 0.72f;
		Add(FxShape::Line(On(ShineA, 0.92f), On(ShineA, 0.12f), 2.2f * Scale, ColorRGBA(1.0f, 1.0f, 1.0f, 0.3f * Alpha)).Additive());
	}

	// two tiers: a second, small brim half way up
	if(Shape == 2)
	{
		const float t = 0.5f;
		const vec2 At = P(0.0f, BaseY - Height * t);
		const vec2 Small = vec2(Radius * 0.68f, Radius * 0.68f * DEPTH);
		if(Look == LOOK_SOLID)
		{
			Add(FxShape::Ellipse(At, Small, Shade(Main, 0.85f)).Outline(1.0f * Scale, Dark).Rotate(Tilt));
			Add(FxShape::Triangle(P(0.0f, BaseY - Height * 0.75f), vec2(Radius * 0.5f, Height * 0.25f), Lighter(Main, 0.25f)).Gradient(Shade(Main, 0.85f)).Rotate(Tilt));
		}
		else
			AddGlowing(FxShape::Ring(At, Small, 1.6f * Scale, Edge.WithMultipliedAlpha(0.9f)).Rotate(Tilt), 4.0f * Scale, 0.5f);
	}

	// the near half of the rim on top, and a small light at the tip
	if(Look == LOOK_SOLID)
	{
		Add(FxShape::Ring(Base, Brim, 1.6f * Scale, Dark).Half(1.0f).Rotate(Tilt));
		Add(FxShape::Circle(Tip, 1.4f * Scale, Lighter(Main, 0.5f)).Outline(0.6f * Scale, Dark));
	}
	else
	{
		AddGlowing(FxShape::Ring(Base, Brim, 2.2f * Scale, Shade(Edge, 1.35f)).Half(1.0f).Rotate(Tilt), (Look == LOOK_NEON ? 8.0f : 6.0f) * Scale, Look == LOOK_NEON ? 0.9f : 0.6f);
		AddGlowing(FxShape::Circle(Tip, 1.8f * Scale, Lighter(Edge, 0.4f)), 5.0f * Scale, 0.6f);
	}
	if(Look == LOOK_ENERGY)
		for(int i = 0; i < 4; i++)
			Light(On(Spin * 0.5f + pi * 0.5f * i, 0.0f), 12.0f * Scale, Main, 0.5f);
}

void CAccessory::RenderHalo(vec2 Head, float Scale, float Tilt, float Squash, float Alpha)
{
	const FxGlow::CScope GlowScope(FxGlow::ACCESSORY);
	const int Look = g_Config.m_XcAccessoryLook;
	const int Shape = g_Config.m_XcAccessoryShape;
	const float Pulse = 1.0f + 0.08f * std::sin(m_Time * 2.4f + m_ColorClient);
	const vec2 Center = Rotate(Head + vec2(0.0f, -14.0f * Scale * Squash), Head, Tilt);
	const vec2 Radius = vec2(15.0f, 15.0f * 0.32f * Squash) * Scale;
	const ColorRGBA Color = BaseColor(0.0f, Alpha), Edge = EdgeColor(Alpha);
	const ColorRGBA Core = Lighter(Color, 0.65f);
	const float Spin = m_Time * 1.6f * g_Config.m_XcAccessorySpin / 100.0f + m_ColorClient * 1.3f;
	const float c = std::cos(Tilt), s = std::sin(Tilt);
	const auto OnRing = [&](vec2 At, vec2 R, float a, float Grow) {
		const vec2 Local(std::cos(a) * R.x * Grow, std::sin(a) * R.y * Grow);
		return At + vec2(Local.x * c - Local.y * s, Local.x * s + Local.y * c);
	};

	// one ring in the look: the far half first, the near half over it
	const auto Ring = [&](vec2 At, vec2 R, float Thick, float SparkAngle) {
		if(Look == LOOK_ENERGY)
		{
			// a tube of light: the far half thinner and dimmer, the near half with a white-hot core
			Add(FxShape::Ring(At, R, Thick * 0.75f * Pulse, Color.WithMultipliedAlpha(0.65f)).Half(-1.0f).Rotate(Tilt));
			Add(FxShape::Ring(At, R, Thick * Pulse, Color).Half(1.0f).Rotate(Tilt));
			Add(FxShape::Ring(At, R, Thick * 0.35f, Core).Half(1.0f).Rotate(Tilt).Additive());
			// the glow from soft lights along the ring, equally wide all around (the glow of the flat ring would be
			// thin on top and thick at the sides)
			for(int i = 0; i < 18; i++)
				Light(OnRing(At, R, 2.0f * pi * i / 18, 1.0f), 18.0f * Scale * Pulse * R.x / Radius.x, Color, 1.0f);
		}
		else if(Look == LOOK_NEON)
		{
			// a dark tube with two bright edges
			Add(FxShape::Ring(At, R, Thick * 1.1f, Shade(Color, 0.18f).WithMultipliedAlpha(0.9f)).Rotate(Tilt));
			for(float Grow : {1.0f - Thick * 0.5f / R.x, 1.0f + Thick * 0.5f / R.x})
				AddGlowing(FxShape::Ring(At, R * Grow, 0.8f * Scale, Edge).Rotate(Tilt), 5.0f * Scale * Pulse, 0.8f);
		}
		else
		{
			// a solid ring, lit from above: light on the far half, a shine along the near half
			Add(FxShape::Ring(At, R, Thick * 1.1f, Shade(Color, 0.75f)).Outline(0.6f * Scale, Shade(Color, 0.35f)).Rotate(Tilt));
			Add(FxShape::Ring(At, R, Thick * 0.5f, Lighter(Color, 0.35f)).Half(-1.0f).Rotate(Tilt));
			Add(FxShape::Ring(OnRing(At, R, -pi * 0.5f, 0.0f) + vec2(0.0f, -0.6f * Scale), R, 0.7f * Scale, ColorRGBA(1.0f, 1.0f, 1.0f, 0.45f * Alpha)).Half(1.0f).Rotate(Tilt).Additive());
		}
		// a spark that runs round the ring, bigger when it passes in front
		if(Look != LOOK_SOLID)
		{
			const float Front = std::sin(SparkAngle);
			AddGlowing(FxShape::Circle(OnRing(At, R, SparkAngle, 1.0f), (1.6f + 0.8f * Front) * Scale, Lighter(Edge, 0.5f).WithMultipliedAlpha(0.55f + 0.45f * std::max(Front, 0.0f))).Additive(), 6.0f * Scale, 0.8f);
		}
	};

	if(Shape == 1)
	{
		// two rings, the small one above, the sparks run against each other
		Ring(Center, Radius, 3.4f * Scale, Spin);
		Ring(Center + vec2(0.0f, -4.5f * Scale), Radius * 0.66f, 2.4f * Scale, -Spin * 1.3f);
	}
	else if(Shape == 2)
	{
		// rays in the plane of the ring, turning slowly; the far ones under the ring, the near ones over it
		const int NumRays = 12;
		const auto Rays = [&](bool Near) {
			for(int i = 0; i < NumRays; i++)
			{
				const float a = Spin * 0.25f + 2.0f * pi * i / NumRays;
				const float Front = std::sin(a);
				if((Front >= 0.0f) != Near)
					continue;
				const float Long = i % 2 ? 1.35f : 1.6f;
				const vec2 From = OnRing(Center, Radius, a, 1.12f), To = OnRing(Center, Radius, a, Long);
				const float Strength = 0.45f + 0.45f * std::max(Front, 0.0f);
				if(Look == LOOK_SOLID)
					Add(FxShape::Line(From, To, 1.4f * Scale, Shade(Color, 0.6f + 0.4f * Strength)).Outline(0.4f * Scale, Shade(Color, 0.35f)));
				else if(Look == LOOK_NEON)
					AddGlowing(FxShape::Line(From, To, 0.9f * Scale, Edge.WithMultipliedAlpha(Strength)), 3.0f * Scale, 0.5f * Strength);
				else
					Add(FxShape::Line(From, To, 1.2f * Scale, Lighter(Color, 0.3f).WithMultipliedAlpha(Strength)).Additive());
			}
		};
		Rays(false);
		Ring(Center, Radius, 3.4f * Scale, Spin);
		Rays(true);
	}
	else
		Ring(Center, Radius, 3.4f * Scale, Spin);

	if(!g_Config.m_XcAccessorySparkles)
		return;
	// three sparkles flying round it, a little further out, bobbing and twinkling
	for(int i = 0; i < 3; i++)
	{
		const float a = -Spin * 0.6f + 2.0f * pi * i / 3.0f;
		const float Front = std::sin(a);
		const vec2 Pos = OnRing(Center, Radius, a, Shape == 2 ? 1.8f : 1.35f) + vec2(0.0f, std::sin(m_Time * 3.0f + i * 2.1f) * 2.5f * Scale);
		const float Twinkle = 0.55f + 0.45f * std::sin(m_Time * 7.0f + i * 1.7f);
		const float Size = (2.2f + 0.8f * Front) * Scale * Twinkle;
		AddGlowing(FxShape::Star(Pos, Size, 4, 0.35f, Lighter(Edge, 0.5f).WithMultipliedAlpha((0.45f + 0.4f * std::max(Front, 0.0f)) * Twinkle)).Rotate(m_Time * 1.5f + i).Additive(), 3.0f * Scale, 0.5f);
	}
}

void CAccessory::RenderCrown(vec2 Head, float Scale, float Tilt, float Squash, float Alpha)
{
	const FxGlow::CScope GlowScope(FxGlow::ACCESSORY);
	const int Look = g_Config.m_XcAccessoryLook;
	const int Shape = g_Config.m_XcAccessoryShape;
	// the crown is drawn a bit bigger than the other accessories to match their visual weight
	Scale *= 1.3f;
	const ColorRGBA Main = BaseColor(0.0f, Alpha), Edge = EdgeColor(Alpha);
	// solid: rich gold that goes dark at the bottom (stands out on yellow maps too), the main color is the gems
	ColorRGBA MetalTop(1.0f, 0.84f, 0.32f, Alpha), MetalBottom(0.7f, 0.4f, 0.06f, Alpha), Outline(0.3f, 0.17f, 0.02f, Alpha);
	if(Look == LOOK_NEON)
	{
		MetalTop = Shade(Main, 0.3f).WithMultipliedAlpha(0.92f);
		MetalBottom = Shade(Main, 0.12f).WithMultipliedAlpha(0.92f);
		Outline = Edge;
	}
	else if(Look == LOOK_ENERGY)
	{
		MetalTop = Lighter(Main, 0.3f).WithMultipliedAlpha(0.45f);
		MetalBottom = Main.WithMultipliedAlpha(0.2f);
		Outline = Lighter(Edge, 0.2f);
	}
	const ColorRGBA Gem = Look == LOOK_SOLID ? Shade(Main, 1.1f) : Lighter(Edge, 0.4f);
	const float Radius = 12.5f * Scale;
	const float Depth = Radius * 0.35f;
	const float Bottom = 5.0f * Scale;
	const float Top = Bottom - (Shape == 2 ? 5.0f : 7.0f) * Scale * Squash;
	const float Border = (Look == LOOK_SOLID ? 1.3f : 1.0f) * Scale;
	const auto P = [&](float x, float y) { return Rotate(Head + vec2(x, y), Head, Tilt); };
	// turns slowly with the spin setting, the spikes go round (the tiara stays facing forward)
	const float Turn = Shape == 1 ? 0.0f : m_Time * 0.25f * g_Config.m_XcAccessorySpin / 100.0f + m_ColorClient * 0.5f;

	// a glowing line in the energy and neon looks, an outline of the shape in the solid one
	const auto EdgeLine = [&](vec2 From, vec2 To, float Strength) {
		if(Look == LOOK_ENERGY)
			Add(FxShape::Line(From, To, 1.0f * Scale, Outline.WithMultipliedAlpha(Strength)).Additive());
		else
			AddGlowing(FxShape::Line(From, To, 0.9f * Scale, Outline.WithMultipliedAlpha(Strength)), 3.0f * Scale, 0.6f * Strength);
	};

	struct SSpike
	{
		float m_Front; // -1 far .. 1 near
		float m_X;
		float m_Tall;
	};
	std::vector<SSpike> vSpikes;
	if(Shape == 1)
	{
		// the tiara: spikes only across the front, the middle one the tallest
		for(int i = 0; i < 5; i++)
		{
			const float a = pi * (0.2f + 0.15f * i);
			vSpikes.push_back({std::sin(a), std::cos(a) * Radius, i == 2 ? 1.35f : (i == 1 || i == 3 ? 1.05f : 0.8f)});
		}
	}
	else
	{
		const int Num = Shape == 2 ? 9 : 5;
		for(int i = 0; i < Num; i++)
		{
			const float a = Turn + 2.0f * pi * i / Num;
			vSpikes.push_back({std::sin(a), std::cos(a) * Radius, Shape == 2 ? (i % 2 ? 1.15f : 1.45f) : 1.0f});
		}
	}
	std::sort(vSpikes.begin(), vSpikes.end(), [](const SSpike &A, const SSpike &B) { return A.m_Front < B.m_Front; });
	const auto Spike = [&](const SSpike &S) {
		const float Near = (S.m_Front + 1.0f) * 0.5f; // 0 far, 1 near
		const float Side = std::sqrt(std::max(1.0f - (S.m_X / Radius) * (S.m_X / Radius), 0.0f));
		const float Width = (Shape == 2 ? 3.2f : 5.5f + 2.0f * Near) * Scale * (0.45f + 0.55f * Side);
		const float Height = (9.5f + 3.5f * Near) * Scale * Squash * S.m_Tall;
		const float BaseY = Top + S.m_Front * Depth * 0.5f + 1.0f * Scale;
		const float Light = 0.7f + 0.3f * Near;
		const vec2 Apex = P(S.m_X, BaseY - Height);
		if(Look == LOOK_SOLID)
		{
			Add(FxShape::Triangle(P(S.m_X, BaseY - Height * 0.5f), vec2(Width, Height * 0.5f), Shade(MetalTop, Light)).Gradient(Shade(MetalBottom, Light)).Outline(Border, Outline).Rotate(Tilt));
			// light catches the left flank of the near spikes
			if(Near > 0.4f)
				Add(FxShape::Line(P(S.m_X - Width * 0.45f, BaseY - 1.0f * Scale), P(S.m_X - 0.4f * Scale, BaseY - Height + 1.5f * Scale), 0.9f * Scale, ColorRGBA(1.0f, 1.0f, 0.9f, 0.5f * Alpha * Near)).Additive());
		}
		else
		{
			SFxShape Fill = FxShape::Triangle(P(S.m_X, BaseY - Height * 0.5f), vec2(Width, Height * 0.5f), MetalTop.WithMultipliedAlpha(Light)).Gradient(MetalBottom.WithMultipliedAlpha(Light)).Rotate(Tilt);
			if(Look == LOOK_ENERGY)
				Fill.Additive();
			Add(Fill);
			EdgeLine(P(S.m_X - Width, BaseY), Apex, 0.4f + 0.6f * Near);
			EdgeLine(P(S.m_X + Width, BaseY), Apex, 0.4f + 0.6f * Near);
		}
		// a pearl on the tip
		if(Look == LOOK_SOLID)
			AddGlowing(FxShape::Circle(P(S.m_X, BaseY - Height - 0.4f * Scale), (1.1f + 0.4f * Near) * Scale, Shade(Gem, 0.7f + 0.4f * Near)).Outline(0.5f * Scale, Outline), 2.5f * Scale, 0.4f * Near);
		else
			AddGlowing(FxShape::Circle(Apex, (0.9f + 0.5f * Near) * Scale, Gem.WithMultipliedAlpha(0.5f + 0.5f * Near)).Additive(), 4.0f * Scale, 0.7f * Near);
		if(Look == LOOK_ENERGY && Near > 0.3f)
			this->Light(Apex, 10.0f * Scale, Main, 0.4f * Near);
	};

	// the far spikes, the inside of the band, the band, its round bottom, the near spikes
	for(const SSpike &S : vSpikes)
		if(S.m_Front < 0.0f)
			Spike(S);
	if(Shape == 1)
	{
		// the tiara is only a front arc
		if(Look == LOOK_SOLID)
			Add(FxShape::Ring(P(0.0f, (Top + Bottom) * 0.5f), vec2(Radius, Depth * 0.5f), Bottom - Top, MetalBottom).Outline(Border, Outline).Half(1.0f).Rotate(Tilt));
		else
		{
			SFxShape Band = FxShape::Ring(P(0.0f, (Top + Bottom) * 0.5f), vec2(Radius, Depth * 0.5f), (Bottom - Top) * 0.8f, MetalTop).Half(1.0f).Rotate(Tilt);
			if(Look == LOOK_ENERGY)
				Band.Additive();
			Add(Band);
			AddGlowing(FxShape::Ring(P(0.0f, Bottom), vec2(Radius, Depth * 0.5f), 0.9f * Scale, Outline).Half(1.0f).Rotate(Tilt), 4.0f * Scale, 0.6f);
		}
	}
	else
	{
		const ColorRGBA Inside = Look == LOOK_SOLID ? ColorRGBA(0.35f, 0.2f, 0.05f, Alpha) : Shade(Main, 0.1f).WithMultipliedAlpha(Look == LOOK_ENERGY ? 0.3f : 0.9f);
		Add(FxShape::Ellipse(P(0.0f, Top), vec2(Radius, Depth * 0.5f), Inside).Outline(Look == LOOK_SOLID ? Border : 0.0f, Outline).Rotate(Tilt));
		SFxShape BandBottom = FxShape::Ellipse(P(0.0f, Bottom), vec2(Radius, Depth * 0.5f), MetalBottom).Rotate(Tilt);
		SFxShape Band = FxShape::RoundRect(P(0.0f, (Top + Bottom) * 0.5f), vec2(Radius, (Bottom - Top) * 0.5f), 0.5f * Scale, MetalTop).Gradient(MetalBottom).Rotate(Tilt);
		if(Look == LOOK_SOLID)
			BandBottom.Outline(Border, Outline);
		if(Look == LOOK_ENERGY)
		{
			BandBottom.Additive();
			Band.Additive();
		}
		Add(BandBottom);
		Add(Band);
		if(Look == LOOK_SOLID)
		{
			// the front rim of the band as a line, and a shine along it
			Add(FxShape::Ring(P(0.0f, Top), vec2(Radius, Depth * 0.5f), Border * 1.2f, Outline).Half(1.0f).Rotate(Tilt));
			Add(FxShape::Line(P(-Radius * 0.75f, Top + 2.2f * Scale), P(Radius * 0.35f, Top + 2.2f * Scale), 1.0f * Scale, ColorRGBA(1.0f, 1.0f, 1.0f, 0.45f * Alpha)).Additive());
		}
		else
		{
			// glowing rims at the top and the bottom of the band
			AddGlowing(FxShape::Ring(P(0.0f, Top), vec2(Radius, Depth * 0.5f), 0.9f * Scale, Outline).Rotate(Tilt), 4.0f * Scale, 0.6f);
			AddGlowing(FxShape::Ring(P(0.0f, Bottom), vec2(Radius, Depth * 0.5f), 0.9f * Scale, Outline).Half(1.0f).Rotate(Tilt), 4.0f * Scale, 0.6f);
		}
	}
	for(const SSpike &S : vSpikes)
		if(S.m_Front >= 0.0f)
			Spike(S);
	if(Shape == 2)
		return; // the spiky crown has no gems on its thin band

	// gems on the band: one big in the middle, two small at the sides
	const float GemY = (Top + Bottom) * 0.5f + Depth * 0.3f;
	const float aGemX[] = {0.0f, -Radius * 0.6f, Radius * 0.6f};
	for(int i = 0; i < 3; i++)
	{
		const vec2 At = P(aGemX[i], GemY - (i ? 0.4f * Scale : 0.0f));
		const float Size = (i == 0 ? 2.1f : 1.4f) * Scale;
		if(Look == LOOK_SOLID)
		{
			AddGlowing(FxShape::Circle(At, Size, Gem).Outline(0.6f * Scale, Shade(Gem, 0.5f)), 3.5f * Scale, 0.5f);
			Add(FxShape::Circle(At + vec2(-0.35f, -0.35f) * Size, Size * 0.3f, ColorRGBA(1.0f, 1.0f, 1.0f, Alpha * 0.85f)).Additive());
		}
		else
			AddGlowing(FxShape::Star(At, Size * 1.2f, 4, 0.45f, Gem).Rotate(pi / 4.0f).Additive(), 4.0f * Scale, 0.7f);
		// now and then a gem flashes with a little star
		if(g_Config.m_XcAccessorySparkles)
		{
			const float Flash = std::pow(std::max(std::sin(m_Time * 1.7f + i * 2.3f + m_ColorClient), 0.0f), 12.0f);
			if(Flash > 0.02f)
				AddGlowing(FxShape::Star(At + vec2(0.6f, -0.6f) * Size, Size * 2.2f * Flash, 4, 0.25f, ColorRGBA(1.0f, 1.0f, 1.0f, Alpha * Flash)).Rotate(pi / 4.0f).Additive(), 3.0f * Scale, 0.6f * Flash);
		}
	}
}

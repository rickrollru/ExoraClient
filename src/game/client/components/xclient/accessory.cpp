#include "accessory.h"

#include "xc_helpers.h"

#include <base/math.h>

#include <engine/client.h>
#include <engine/graphics.h>
#include <engine/shared/config.h>

#include <game/client/gameclient.h>

#include <cmath>

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

	Graphics()->TextureClear();
	Graphics()->QuadsBegin();
	for(int ClientId = 0; ClientId < MAX_CLIENTS; ClientId++)
	{
		SState &State = m_aStates[ClientId];
		if(!GameClient()->m_Snap.m_aCharacters[ClientId].m_Active ||
			!ShowFor(GameClient(), ClientId, g_Config.m_XcAccessorySelf, g_Config.m_XcAccessoryDummy, g_Config.m_XcAccessoryOthers))
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
	Graphics()->QuadsEnd();
}

ColorRGBA CAccessory::BaseColor(float Offset, float Alpha) const
{
	if(g_Config.m_XcAccessoryRainbow)
		return RainbowColor(m_Time * 0.15f + Offset, Alpha);
	return HslConfigColor(g_Config.m_XcAccessoryColor).WithAlpha(Alpha);
}

static ColorRGBA Shade(ColorRGBA Color, float Factor)
{
	return ColorRGBA(std::min(Color.r * Factor, 1.0f), std::min(Color.g * Factor, 1.0f), std::min(Color.b * Factor, 1.0f), Color.a);
}

void CAccessory::RenderCone(vec2 Head, float Scale, float Tilt, float Squash, float Alpha) const
{
	constexpr int SEGMENTS = 32;
	constexpr float BRIM_SQUASH = 0.3f;
	const float Radius = 30.0f * Scale;
	const vec2 Base = Head + vec2(0.0f, 4.0f * Scale);
	const vec2 Apex = Base + vec2(0.0f, -24.0f * Scale * Squash);
	const auto Transform = [&](vec2 Point) { return Rotate(Point, Head, Tilt); };
	const vec2 ApexT = Transform(Apex);

	// cone surface: triangles from the apex to the brim, lit from the front
	for(int i = 0; i < SEGMENTS; i++)
	{
		const float a1 = 2.0f * pi * i / SEGMENTS;
		const float a2 = 2.0f * pi * (i + 1) / SEGMENTS;
		const vec2 p1 = Transform(Base + vec2(std::cos(a1) * Radius, std::sin(a1) * Radius * BRIM_SQUASH));
		const vec2 p2 = Transform(Base + vec2(std::cos(a2) * Radius, std::sin(a2) * Radius * BRIM_SQUASH));
		const float Light1 = 0.7f + 0.3f * std::sin(a1);
		const float Light2 = 0.7f + 0.3f * std::sin(a2);
		const ColorRGBA Top = Shade(BaseColor(0.0f, Alpha), 1.25f);
		Graphics()->SetColor4(Top, Top,
			Shade(BaseColor(i / (float)SEGMENTS, Alpha * 0.75f), Light1),
			Shade(BaseColor((i + 1) / (float)SEGMENTS, Alpha * 0.75f), Light2));
		const IGraphics::CFreeformItem Item(ApexT, ApexT, p1, p2);
		Graphics()->QuadsDrawFreeform(&Item, 1);
	}

	// glowing brim
	DrawGlowRing(Graphics(), Transform(Base), Radius, BRIM_SQUASH, 2.0f * Scale, 4.0f * Scale, Shade(BaseColor(0.5f, Alpha), 1.2f), Tilt);
}

void CAccessory::RenderHalo(vec2 Head, float Scale, float Tilt, float Squash, float Alpha) const
{
	const vec2 Center = Head + vec2(0.0f, -14.0f * Scale * Squash);
	DrawGlowRing(Graphics(), Rotate(Center, Head, Tilt), 15.0f * Scale, 0.32f * Squash, 3.0f * Scale, 6.0f * Scale, BaseColor(0.0f, Alpha), Tilt);
}

void CAccessory::RenderCrown(vec2 Head, float Scale, float Tilt, float Squash, float Alpha) const
{
	// the crown is drawn a bit bigger than the other accessories to match their visual weight
	Scale *= 1.3f;
	const ColorRGBA GoldTop(1.0f, 0.88f, 0.42f, Alpha);
	const ColorRGBA GoldBottom(0.9f, 0.62f, 0.16f, Alpha);
	const ColorRGBA Outline(0.42f, 0.27f, 0.05f, Alpha);
	const float HalfWidth = 13.0f * Scale;
	const float Bottom = 5.0f * Scale;
	const float BandTop = Bottom - 7.0f * Scale * Squash;
	const float Border = 1.5f * Scale;

	const auto P = [&](float x, float y) { return Rotate(Head + vec2(x, y), Head, Tilt); };

	struct SSpike
	{
		float m_Left, m_Right, m_Tip, m_Height;
	};
	const SSpike aSpikes[] = {
		{-HalfWidth, -HalfWidth / 3.0f, -HalfWidth + 2.0f * Scale, 9.0f * Scale},
		{-HalfWidth / 3.0f, HalfWidth / 3.0f, 0.0f, 13.0f * Scale},
		{HalfWidth / 3.0f, HalfWidth, HalfWidth - 2.0f * Scale, 9.0f * Scale},
	};

	// dark outline first, then the gold on top of it
	for(int Pass = 0; Pass < 2; Pass++)
	{
		const float Grow = Pass == 0 ? Border : 0.0f;
		if(Pass == 0)
			Graphics()->SetColor(Outline);
		else
			Graphics()->SetColor4(GoldTop, GoldTop, GoldBottom, GoldBottom);
		const IGraphics::CFreeformItem Band(
			P(-HalfWidth - Grow, BandTop - Grow), P(HalfWidth + Grow, BandTop - Grow),
			P(-HalfWidth - Grow, Bottom + Grow), P(HalfWidth + Grow, Bottom + Grow));
		Graphics()->QuadsDrawFreeform(&Band, 1);

		if(Pass == 1)
			Graphics()->SetColor4(GoldTop, GoldTop, GoldBottom, GoldBottom);
		for(const SSpike &Spike : aSpikes)
		{
			const float TipY = BandTop - Spike.m_Height * Squash - Grow * 1.5f;
			const IGraphics::CFreeformItem Item(
				P(Spike.m_Tip, TipY), P(Spike.m_Tip, TipY),
				P(Spike.m_Left - Grow, BandTop + 0.5f * Scale), P(Spike.m_Right + Grow, BandTop + 0.5f * Scale));
			Graphics()->QuadsDrawFreeform(&Item, 1);
		}
	}

	// gems on the spike tips and in the middle of the band
	const ColorRGBA Gem = Shade(BaseColor(0.0f, Alpha), 1.1f);
	const ColorRGBA Shine(1.0f, 1.0f, 1.0f, Alpha * 0.8f);
	for(const SSpike &Spike : aSpikes)
	{
		const vec2 Tip = P(Spike.m_Tip, BandTop - Spike.m_Height * Squash);
		DrawSoftCircle(Graphics(), Tip, 2.0f * Scale, 1.0f * Scale, Gem);
	}
	const vec2 Center = P(0.0f, (BandTop + Bottom) * 0.5f);
	DrawSoftCircle(Graphics(), Center, 2.5f * Scale, 1.0f * Scale, Gem);
	DrawSoftCircle(Graphics(), Center + vec2(-0.8f, -0.8f) * Scale, 0.8f * Scale, 0.5f * Scale, Shine);
}

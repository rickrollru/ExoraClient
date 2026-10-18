#include "accessory.h"

#include "fx_renderer.h"
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
	CFxRenderer &Fx = GameClient()->m_FxRenderer;
	constexpr float BRIM_SQUASH = 0.3f;
	const float Radius = 30.0f * Scale;
	const float Height = 24.0f * Scale * Squash;
	const vec2 Base = Rotate(Head + vec2(0.0f, 4.0f * Scale), Head, Tilt);
	const vec2 Body = Rotate(Head + vec2(0.0f, 4.0f * Scale - Height * 0.5f), Head, Tilt);
	const vec2 Brim = vec2(Radius, Radius * BRIM_SQUASH);

	// underside of the brim, then the cone, then the glowing rim on top
	Fx.AddWorld(FxShape::Ellipse(Base, Brim, Shade(BaseColor(0.5f, Alpha * 0.8f), 0.7f)).Rotate(Tilt));
	Fx.AddWorld(FxShape::Triangle(Body, vec2(Radius * 0.96f, Height * 0.5f), Shade(BaseColor(0.0f, Alpha * 0.9f), 1.3f))
			    .Gradient(Shade(BaseColor(0.3f, Alpha * 0.8f), 0.85f))
			    .Rotate(Tilt));
	Fx.AddWorldGlowing(FxShape::Ring(Base, Brim, 2.0f * Scale, Shade(BaseColor(0.5f, Alpha), 1.2f)).Rotate(Tilt), 5.0f * Scale, 0.5f);
}

void CAccessory::RenderHalo(vec2 Head, float Scale, float Tilt, float Squash, float Alpha) const
{
	const vec2 Center = Rotate(Head + vec2(0.0f, -14.0f * Scale * Squash), Head, Tilt);
	GameClient()->m_FxRenderer.AddWorldGlowing(FxShape::Ring(Center, vec2(15.0f, 15.0f * 0.32f * Squash) * Scale, 3.0f * Scale, BaseColor(0.0f, Alpha)).Rotate(Tilt), 7.0f * Scale, 0.5f);
}

void CAccessory::RenderCrown(vec2 Head, float Scale, float Tilt, float Squash, float Alpha) const
{
	CFxRenderer &Fx = GameClient()->m_FxRenderer;
	// the crown is drawn a bit bigger than the other accessories to match their visual weight
	Scale *= 1.3f;
	const ColorRGBA GoldTop(1.0f, 0.88f, 0.42f, Alpha);
	const ColorRGBA GoldBottom(0.9f, 0.62f, 0.16f, Alpha);
	const ColorRGBA Outline(0.42f, 0.27f, 0.05f, Alpha);
	const float HalfWidth = 13.0f * Scale;
	const float Bottom = 5.0f * Scale;
	const float BandTop = Bottom - 7.0f * Scale * Squash;
	const float Border = 1.2f * Scale;
	const auto P = [&](float x, float y) { return Rotate(Head + vec2(x, y), Head, Tilt); };

	struct SSpike
	{
		float m_Left, m_Right, m_Height;
	};
	const SSpike aSpikes[] = {
		{-HalfWidth, -HalfWidth / 3.0f, 9.0f * Scale},
		{-HalfWidth / 3.0f, HalfWidth / 3.0f, 13.0f * Scale},
		{HalfWidth / 3.0f, HalfWidth, 9.0f * Scale},
	};
	const ColorRGBA Gem = Shade(BaseColor(0.0f, Alpha), 1.1f);
	for(const SSpike &Spike : aSpikes)
	{
		const float Height = Spike.m_Height * Squash + 1.0f * Scale; // reaches into the band
		const float CenterX = (Spike.m_Left + Spike.m_Right) * 0.5f;
		const float CenterY = BandTop + 1.0f * Scale - Height * 0.5f;
		Fx.AddWorld(FxShape::Triangle(P(CenterX, CenterY), vec2((Spike.m_Right - Spike.m_Left) * 0.5f, Height * 0.5f), GoldTop)
				    .Gradient(GoldBottom)
				    .Outline(Border, Outline)
				    .Rotate(Tilt));
		Fx.AddWorldGlowing(FxShape::Circle(P(CenterX, BandTop - Spike.m_Height * Squash), 2.0f * Scale, Gem), 3.0f * Scale, 0.5f);
	}
	Fx.AddWorld(FxShape::RoundRect(P(0.0f, (BandTop + Bottom) * 0.5f), vec2(HalfWidth, (Bottom - BandTop) * 0.5f), 1.5f * Scale, GoldTop)
			    .Gradient(GoldBottom)
			    .Outline(Border, Outline)
			    .Rotate(Tilt));
	const vec2 Center = P(0.0f, (BandTop + Bottom) * 0.5f);
	Fx.AddWorldGlowing(FxShape::Circle(Center, 2.4f * Scale, Gem), 3.5f * Scale, 0.5f);
	Fx.AddWorld(FxShape::Circle(Center + vec2(-0.8f, -0.8f) * Scale, 0.7f * Scale, ColorRGBA(1.0f, 1.0f, 1.0f, Alpha * 0.8f)).Additive());
}

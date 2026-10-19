#include "cape.h"

#include "xc_helpers.h"

#include <base/math.h>

#include <engine/client.h>
#include <engine/graphics.h>
#include <engine/shared/config.h>

#include <game/client/gameclient.h>

#include <algorithm>
#include <cmath>

using namespace XcHelpers;

static constexpr float STEP = 1.0f / 120.0f;

void CXcCape::OnReset()
{
	for(SState &State : m_aStates)
		State.m_Valid = false;
}

bool CXcCape::LooksLeft(int ClientId) const
{
	// the player you control looks where the mouse is, the others where the server says
	if(Client()->State() != IClient::STATE_DEMOPLAYBACK && ClientId == GameClient()->m_aLocalIds[g_Config.m_ClDummy])
		return GameClient()->m_Controls.m_aMousePos[g_Config.m_ClDummy].x < 0.0f;
	const float Angle = GameClient()->m_aClients[ClientId].m_RenderCur.m_Angle / 256.0f;
	return std::cos(Angle) < 0.0f;
}

vec2 CXcCape::Anchor(vec2 Pos, float Scale, float Back) const
{
	// the neck: a bit behind the middle of the body, a little above it
	if(g_Config.m_XcCape == CAPE_SCARF)
		return Pos + vec2(Back * 15.0f, 5.0f) * Scale;
	return Pos + vec2(Back * 11.0f, -8.0f) * Scale;
}

void CXcCape::Simulate(SState &State, vec2 Anchor, float Scale, float Dt, float Time, bool Collide) const
{
	const bool Scarf = g_Config.m_XcCape == CAPE_SCARF;
	const float Length = (Scarf ? 68.0f : 44.0f) * Scale * g_Config.m_XcCapeLength / 100.0f;
	const float Segment = Length / (NUM_POINTS - 1);
	const float Wind = g_Config.m_XcCapeWind / 100.0f;
	if(!State.m_Valid || distance(State.m_aPoints[0], Anchor) > 300.0f)
	{
		// starts hanging behind the tee (also after a teleport)
		for(int i = 0; i < NUM_POINTS; i++)
		{
			State.m_aPoints[i] = Anchor + normalize(vec2(State.m_Back * 0.5f, 1.0f)) * Segment * i;
			State.m_aPrev[i] = State.m_aPoints[i];
		}
		State.m_Valid = true;
		State.m_Accumulator = 0.0f;
	}
	State.m_Accumulator = std::min(State.m_Accumulator + Dt, 0.1f);
	const vec2 Start = State.m_aPoints[0];
	int Steps = 0;
	const int NumSteps = (int)(State.m_Accumulator / STEP);
	while(State.m_Accumulator >= STEP)
	{
		State.m_Accumulator -= STEP;
		Steps++;
		// the neck follows the tee smoothly between the frames
		State.m_aPoints[0] = mix(Start, Anchor, (float)Steps / std::max(NumSteps, 1));
		State.m_aPrev[0] = State.m_aPoints[0];
		for(int i = 1; i < NUM_POINTS; i++)
		{
			const float t = (float)i / (NUM_POINTS - 1);
			vec2 Force(0.0f, 1500.0f * Scale);
			// the cloth likes to fly a bit backwards and flutters, more at the free end
			Force.x += State.m_Back * (Scarf ? 900.0f : 1300.0f) * Scale;
			Force.x += std::sin(Time * 2.3f + i * 0.7f) * 260.0f * Wind * t * Scale;
			Force.y += std::sin(Time * 3.1f + i * 1.1f) * 160.0f * Wind * t * Scale;
			const vec2 Velocity = (State.m_aPoints[i] - State.m_aPrev[i]) * 0.97f;
			State.m_aPrev[i] = State.m_aPoints[i];
			State.m_aPoints[i] += Velocity + Force * STEP * STEP;
		}
		for(int Iteration = 0; Iteration < 4; Iteration++)
		{
			for(int i = 1; i < NUM_POINTS; i++)
			{
				const vec2 Delta = State.m_aPoints[i] - State.m_aPoints[i - 1];
				const float Dist = length(Delta);
				if(Dist <= 0.0001f)
					continue;
				const vec2 Fix = Delta * ((Dist - Segment) / Dist);
				if(i == 1)
					State.m_aPoints[i] -= Fix; // the neck does not move
				else
				{
					State.m_aPoints[i] -= Fix * 0.5f;
					State.m_aPoints[i - 1] += Fix * 0.5f;
				}
			}
		}
		// the cloth lies on the ground and slides along walls instead of going into them: a point inside
		// a block moves out over the nearest edge of that block
		if(Collide)
		{
			for(int i = 1; i < NUM_POINTS; i++)
			{
				vec2 &Point = State.m_aPoints[i];
				if(!Collision()->CheckPoint(Point))
					continue;
				const float Left = std::floor(Point.x / 32.0f) * 32.0f - 0.5f;
				const float Top = std::floor(Point.y / 32.0f) * 32.0f - 0.5f;
				const vec2 aCandidates[] = {vec2(Point.x, Top), vec2(Point.x, Top + 33.0f), vec2(Left, Point.y), vec2(Left + 33.0f, Point.y)};
				float Best = 1e9f;
				vec2 Out = Point;
				for(const vec2 &Candidate : aCandidates)
					if(!Collision()->CheckPoint(Candidate) && distance(Candidate, Point) < Best)
					{
						Best = distance(Candidate, Point);
						Out = Candidate;
					}
				Point = Out;
				State.m_aPrev[i] = mix(State.m_aPrev[i], Point, 0.5f); // friction on the ground
			}
			// cloth does not stretch: a point stuck behind a corner pulls no further than a bit over its length
			for(int i = 1; i < NUM_POINTS; i++)
			{
				const vec2 Delta = State.m_aPoints[i] - State.m_aPoints[i - 1];
				const float Dist = length(Delta);
				if(Dist > Segment * 1.15f)
					State.m_aPoints[i] = State.m_aPoints[i - 1] + Delta * (Segment * 1.15f / Dist);
			}
		}
	}
}

void CXcCape::RenderCloth(const vec2 *pPoints, float Scale, float Back, int ClientId, float Alpha) const
{
	const bool Scarf = g_Config.m_XcCape == CAPE_SCARF;
	const float WidthScale = Scale * g_Config.m_XcCapeWidth / 100.0f;
	const float Opacity = Alpha * g_Config.m_XcCapeAlpha / 100.0f;
	vec2 aLeft[NUM_POINTS], aRight[NUM_POINTS];
	ColorRGBA aColors[NUM_POINTS];
	for(int i = 0; i < NUM_POINTS; i++)
	{
		const float t = (float)i / (NUM_POINTS - 1);
		const vec2 Dir = normalize(i == 0 ? pPoints[1] - pPoints[0] : pPoints[i] - pPoints[i - 1]);
		const vec2 Normal(-Dir.y, Dir.x);
		// a cape gets wider towards the bottom, a scarf stays narrow and ends in a point
		const float Half = Scarf ? 5.5f * WidthScale * (1.0f - 0.5f * t * t) : (7.0f + 8.0f * t) * WidthScale;
		aLeft[i] = pPoints[i] + Normal * Half;
		aRight[i] = pPoints[i] - Normal * Half;
		ColorRGBA Color = EffectColor(GameClient(), g_Config.m_XcCapeColorMode, g_Config.m_XcCapeColor, ClientId, t * 0.35f, 1.0f);
		// folds: the cloth gets lighter and darker along its length, darker at the free end
		const float Fold = 0.86f + 0.14f * std::sin(t * 9.0f + Back * 1.3f);
		const float Shade = Fold * (1.0f - 0.25f * t);
		aColors[i] = ColorRGBA(Color.r * Shade, Color.g * Shade, Color.b * Shade, Opacity);
	}

	Graphics()->TextureClear();
	Graphics()->QuadsBegin();
	// a thin dark border first, the cloth over it
	for(int Pass = 0; Pass < 2; Pass++)
	{
		const float Grow = Pass == 0 ? 1.4f * Scale : 0.0f;
		for(int i = 0; i + 1 < NUM_POINTS; i++)
		{
			const vec2 Dir0 = normalize(aLeft[i] - aRight[i]) * Grow;
			const vec2 Dir1 = normalize(aLeft[i + 1] - aRight[i + 1]) * Grow;
			const vec2 Down = i + 2 == NUM_POINTS ? normalize(pPoints[i + 1] - pPoints[i]) * Grow : vec2(0.0f, 0.0f);
			ColorRGBA A = aColors[i], B = aColors[i + 1];
			if(Pass == 0)
			{
				A = ColorRGBA(A.r * 0.35f, A.g * 0.35f, A.b * 0.35f, A.a);
				B = ColorRGBA(B.r * 0.35f, B.g * 0.35f, B.b * 0.35f, B.a);
			}
			// the inner side (towards the tee) is a little darker
			const ColorRGBA AIn(A.r * 0.8f, A.g * 0.8f, A.b * 0.8f, A.a), BIn(B.r * 0.8f, B.g * 0.8f, B.b * 0.8f, B.a);
			Graphics()->SetColor4(A, AIn, B, BIn);
			const IGraphics::CFreeformItem Item(aLeft[i] + Dir0, aRight[i] - Dir0, aLeft[i + 1] + Dir1 + Down, aRight[i + 1] - Dir1 + Down);
			Graphics()->QuadsDrawFreeform(&Item, 1);
		}
	}
	Graphics()->QuadsEnd();
}

void CXcCape::OnRender()
{
	if(!g_Config.m_XcCape || (Client()->State() != IClient::STATE_ONLINE && Client()->State() != IClient::STATE_DEMOPLAYBACK))
	{
		OnReset();
		return;
	}
	const float Dt = std::min(Client()->RenderFrameTime(), 0.1f);
	const float Time = Client()->GlobalTime();
	for(int ClientId = 0; ClientId < MAX_CLIENTS; ClientId++)
	{
		SState &State = m_aStates[ClientId];
		if(!GameClient()->m_Snap.m_aCharacters[ClientId].m_Active ||
			!ShowFor(GameClient(), ClientId, g_Config.m_XcCapeSelf, g_Config.m_XcCapeDummy, g_Config.m_XcCapeOthers))
		{
			State.m_Valid = false;
			continue;
		}
		const float Alpha = PlayerAlpha(GameClient(), ClientId);
		if(Alpha <= 0.0f)
		{
			State.m_Valid = false;
			continue;
		}
		const CGameClient::CClientData &Data = GameClient()->m_aClients[ClientId];
		const float Scale = Data.m_RenderInfo.m_Size / 64.0f;
		// turning around swings the cape over to the other side instead of jumping
		const float BackTarget = LooksLeft(ClientId) ? 1.0f : -1.0f;
		State.m_Back += (BackTarget - State.m_Back) * SmoothFactor(10.0f, Dt);
		Simulate(State, Anchor(Data.m_RenderPos, Scale, State.m_Back), Scale, Dt, Time + ClientId * 1.7f, true);
		RenderCloth(State.m_aPoints, Scale, State.m_Back, ClientId, Alpha);
	}
}

void CXcCape::RenderPreview(vec2 Pos, float TeeSize, bool LookingLeft, float Alpha, float Time)
{
	if(!g_Config.m_XcCape)
		return;
	// a short simulation from rest every frame would look stiff, so the preview keeps its own chain
	static SState s_State;
	const float Scale = TeeSize / 64.0f;
	const float BackTarget = LookingLeft ? 1.0f : -1.0f;
	s_State.m_Back += (BackTarget - s_State.m_Back) * SmoothFactor(10.0f, std::min(Client()->RenderFrameTime(), 0.1f));
	if(s_State.m_Valid && distance(s_State.m_aPoints[0], Anchor(Pos, Scale, s_State.m_Back)) > 40.0f * Scale)
		s_State.m_Valid = false;
	Simulate(s_State, Anchor(Pos, Scale, s_State.m_Back), Scale, std::min(Client()->RenderFrameTime(), 0.1f), Time, false);
	RenderCloth(s_State.m_aPoints, Scale, s_State.m_Back, -1, Alpha);
}

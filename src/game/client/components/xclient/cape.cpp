#include "cape.h"

#include "xc_helpers.h"

#include <base/math.h>

#include <engine/client.h>
#include <engine/engine.h>
#include <engine/graphics.h>
#include <engine/shared/config.h>
#include <engine/storage.h>

#include <game/client/gameclient.h>

#include <algorithm>
#include <cmath>

using namespace XcHelpers;

static constexpr float STEP = 1.0f / 120.0f;

static ColorRGBA Shaded(const ColorRGBA &Color, float Shade, float Alpha)
{
	return ColorRGBA(std::clamp(Color.r * Shade, 0.0f, 1.0f), std::clamp(Color.g * Shade, 0.0f, 1.0f), std::clamp(Color.b * Shade, 0.0f, 1.0f), Alpha);
}

static ColorRGBA ConfigColor(unsigned Value)
{
	return color_cast<ColorRGBA>(ColorHSLA(Value));
}

// a thick line as one quad, inside an open QuadsBegin
static void DrawSegment(IGraphics *pGraphics, vec2 a, vec2 b, float Width, const ColorRGBA &Color)
{
	const vec2 Dir = b - a;
	const float Len = length(Dir);
	if(Len < 0.001f)
		return;
	const vec2 Side = vec2(-Dir.y, Dir.x) / Len * (Width * 0.5f);
	const vec2 Along = Dir / Len * (Width * 0.35f); // overlaps the next segment so corners stay closed
	pGraphics->SetColor(Color);
	const IGraphics::CFreeformItem Item(a - Along + Side, b + Along + Side, a - Along - Side, b + Along - Side);
	pGraphics->QuadsDrawFreeform(&Item, 1);
}

// a filled circle as a fan of freeform quads, inside an open QuadsBegin
static void DrawDisc(IGraphics *pGraphics, vec2 Center, float Radius, const ColorRGBA &Color, int Segments = 10)
{
	pGraphics->SetColor(Color);
	for(int i = 0; i < Segments; i += 2)
	{
		const float a0 = 2.0f * pi * i / Segments, a1 = 2.0f * pi * (i + 1) / Segments, a2 = 2.0f * pi * (i + 2) / Segments;
		const IGraphics::CFreeformItem Item(Center, Center + direction(a0) * Radius, Center + direction(a1) * Radius, Center + direction(a2) * Radius);
		pGraphics->QuadsDrawFreeform(&Item, 1);
	}
}

CXcCape::CXcCape() :
	m_Pictures("xclient/capes", 512, 256, false)
{
	m_Front.m_pCape = this;
	for(vec2 &Pos : m_aPreviewPos)
		Pos = vec2(-1e6f, -1e6f);
}

void CXcCape::OnInit()
{
	m_Pictures.Init(Graphics(), Storage(), Kernel()->RequestInterface<IEngine>());
}

void CXcCape::OnShutdown()
{
	m_Pictures.Shutdown();
}

void CXcCape::OnReset()
{
	for(SState &State : m_aStates)
	{
		State.m_Cloth.m_Valid = false;
		for(STail &Tail : State.m_aTails)
			Tail.m_Valid = false;
		State.m_HasLast = false;
		State.m_DrawFront = false;
	}
}

bool CXcCape::LooksLeft(int ClientId) const
{
	// the player you control looks where the mouse is, the others where the server says
	if(Client()->State() != IClient::STATE_DEMOPLAYBACK && ClientId == GameClient()->m_aLocalIds[g_Config.m_ClDummy])
		return GameClient()->m_Controls.m_aMousePos[g_Config.m_ClDummy].x < 0.0f;
	const float Angle = GameClient()->m_aClients[ClientId].m_RenderCur.m_Angle / 256.0f;
	return std::cos(Angle) < 0.0f;
}

void CXcCape::UpdateMotion(SState &State, vec2 Pos, float Dt, bool Jumped) const
{
	const float React = g_Config.m_XcCapeReact / 100.0f;
	vec2 Vel = State.m_Vel;
	if(State.m_HasLast && Dt > 0.0f && distance(Pos, State.m_LastPos) < 200.0f)
		Vel = (Pos - State.m_LastPos) / Dt;
	// a sharp kick upwards is a jump: the cloth snaps down and out
	if(!Jumped && State.m_HasLast && Vel.y < -350.0f && Vel.y - State.m_Vel.y < -600.0f)
		Jumped = true;
	if(Jumped && React > 0.0f)
	{
		for(int r = 1; r < ROWS; r++)
		{
			const float t = (float)r / (ROWS - 1);
			for(int c = 0; c < COLS; c++)
				State.m_Cloth.m_aPrev[r][c] -= vec2(State.m_Back * 1.5f, 4.0f) * t * React;
		}
		for(STail &Tail : State.m_aTails)
			for(int i = 1; i < TAIL_POINTS; i++)
				Tail.m_aPrev[i] -= vec2(State.m_Back * 1.0f, 3.0f) * ((float)i / (TAIL_POINTS - 1)) * React;
	}
	State.m_Vel += (Vel - State.m_Vel) * SmoothFactor(12.0f, Dt);
	State.m_LastPos = Pos;
	State.m_HasLast = true;
}

float CXcCape::ColumnLength(int Col) const
{
	const float u = 2.0f * Col / (COLS - 1) - 1.0f;
	switch(g_Config.m_XcCapeShape)
	{
	case SHAPE_POINTED: return 1.0f - 0.32f * std::abs(u);
	case SHAPE_RAGGED:
	{
		static const float s_aLengths[COLS] = {0.9f, 1.04f, 0.84f, 1.0f, 0.88f};
		return s_aLengths[Col];
	}
	default: return 1.0f;
	}
}

void CXcCape::Collide(vec2 &Point, vec2 &Prev) const
{
	// lies on the ground and slides along walls: a point inside a block moves out over the nearest edge
	if(!Collision()->CheckPoint(Point))
		return;
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
	Prev = mix(Prev, Point, 0.5f); // friction on the ground
}

void CXcCape::SimulateCloth(SState &State, vec2 Pos, float Scale, float Dt, float Time, bool Collide) const
{
	SCloth &Cloth = State.m_Cloth;
	const bool Royal = g_Config.m_XcCapeShape == SHAPE_ROYAL;
	const float Width = g_Config.m_XcCapeWidth / 100.0f;
	const float Length = (Royal ? 56.0f : 46.0f) * Scale * g_Config.m_XcCapeLength / 100.0f;
	const float Segment = Length / (ROWS - 1);
	const float TopWidth = (Royal ? 20.0f : 14.0f) * Width * Scale;
	const float BottomWidth = (Royal ? 38.0f : 30.0f) * Width * Scale;
	const float Wind = g_Config.m_XcCapeWind / 100.0f;
	const float React = g_Config.m_XcCapeReact / 100.0f;
	const float Speed = std::min(length(State.m_Vel) / 900.0f, 1.5f) * React;
	// the shoulders: a bit behind the middle of the body, a little above it
	const vec2 Neck = Pos + vec2(State.m_Back * 11.0f, -8.0f) * Scale;
	auto TopPoint = [&](int c) { return Neck + vec2((2.0f * c / (COLS - 1) - 1.0f) * TopWidth * 0.5f, 0.0f); };

	if(!Cloth.m_Valid || distance(Cloth.m_aPoints[0][COLS / 2], Neck) > 300.0f)
	{
		// starts hanging behind the tee (also after a teleport)
		for(int r = 0; r < ROWS; r++)
			for(int c = 0; c < COLS; c++)
			{
				const float t = (float)r / (ROWS - 1);
				Cloth.m_aPoints[r][c] = TopPoint(c) + vec2(State.m_Back * 0.4f, 1.0f) * Segment * r + vec2((2.0f * c / (COLS - 1) - 1.0f) * (BottomWidth - TopWidth) * 0.5f * t, 0.0f);
				Cloth.m_aPrev[r][c] = Cloth.m_aPoints[r][c];
			}
		Cloth.m_Valid = true;
		Cloth.m_Accumulator = 0.0f;
	}

	float aRowWidth[ROWS];
	for(int r = 0; r < ROWS; r++)
		aRowWidth[r] = mix(TopWidth, BottomWidth, (float)r / (ROWS - 1)) / (COLS - 1);
	float aColumn[COLS];
	for(int c = 0; c < COLS; c++)
		aColumn[c] = Segment * ColumnLength(c);

	auto Relax = [](vec2 &a, vec2 &b, float Rest, bool PinnedA, float Stiffness) {
		const vec2 Delta = b - a;
		const float Dist = length(Delta);
		if(Dist <= 0.0001f)
			return;
		const vec2 Fix = Delta * ((Dist - Rest) / Dist) * Stiffness;
		if(PinnedA)
			b -= Fix;
		else
		{
			a += Fix * 0.5f;
			b -= Fix * 0.5f;
		}
	};

	Cloth.m_Accumulator = std::min(Cloth.m_Accumulator + Dt, 0.1f);
	vec2 aStart[COLS];
	for(int c = 0; c < COLS; c++)
		aStart[c] = Cloth.m_aPoints[0][c];
	const int NumSteps = (int)(Cloth.m_Accumulator / STEP);
	int Steps = 0;
	while(Cloth.m_Accumulator >= STEP)
	{
		Cloth.m_Accumulator -= STEP;
		Steps++;
		// the shoulders follow the tee smoothly between the frames
		const float Follow = (float)Steps / std::max(NumSteps, 1);
		for(int c = 0; c < COLS; c++)
		{
			Cloth.m_aPoints[0][c] = mix(aStart[c], TopPoint(c), Follow);
			Cloth.m_aPrev[0][c] = Cloth.m_aPoints[0][c];
		}
		for(int r = 1; r < ROWS; r++)
		{
			const float t = (float)r / (ROWS - 1);
			for(int c = 0; c < COLS; c++)
			{
				vec2 Force(0.0f, 1500.0f * Scale);
				// the cloth likes to fly a bit backwards and flutters, more at the free end and at speed
				Force.x += State.m_Back * 1300.0f * Scale;
				const float Flutter = Wind * t * Scale * (1.0f + Speed * 1.5f);
				Force.x += std::sin(Time * 2.3f + r * 0.7f + c * 0.4f) * 260.0f * Flutter;
				Force.y += std::sin(Time * 3.1f + r * 1.1f - c * 0.6f) * 180.0f * Flutter;
				// the hook pulls the tee, the cape streams away from it
				Force -= State.m_Pull * 1600.0f * React * t * Scale;
				vec2 &Point = Cloth.m_aPoints[r][c];
				const vec2 Velocity = (Point - Cloth.m_aPrev[r][c]) * 0.97f;
				Cloth.m_aPrev[r][c] = Point;
				Point += Velocity + Force * STEP * STEP;
			}
		}
		for(int Iteration = 0; Iteration < 3; Iteration++)
		{
			for(int r = 1; r < ROWS; r++)
			{
				for(int c = 0; c < COLS; c++)
				{
					Relax(Cloth.m_aPoints[r - 1][c], Cloth.m_aPoints[r][c], aColumn[c], r == 1, 1.0f);
					if(c > 0)
					{
						Relax(Cloth.m_aPoints[r][c - 1], Cloth.m_aPoints[r][c], aRowWidth[r], false, 1.0f);
						// shear: keeps the cells from folding flat
						const float Diagonal = std::sqrt(aRowWidth[r] * aRowWidth[r] + aColumn[c] * aColumn[c]);
						Relax(Cloth.m_aPoints[r - 1][c - 1], Cloth.m_aPoints[r][c], Diagonal, r == 1, 0.4f);
						Relax(Cloth.m_aPoints[r - 1][c], Cloth.m_aPoints[r][c - 1], Diagonal, r == 1, 0.4f);
					}
				}
			}
		}
		if(Collide)
		{
			for(int r = 1; r < ROWS; r++)
				for(int c = 0; c < COLS; c++)
					this->Collide(Cloth.m_aPoints[r][c], Cloth.m_aPrev[r][c]);
			// cloth does not stretch: a point stuck behind a corner pulls no further than a bit over its length
			for(int r = 1; r < ROWS; r++)
				for(int c = 0; c < COLS; c++)
				{
					const vec2 Delta = Cloth.m_aPoints[r][c] - Cloth.m_aPoints[r - 1][c];
					const float Dist = length(Delta);
					if(Dist > aColumn[c] * 1.2f)
						Cloth.m_aPoints[r][c] = Cloth.m_aPoints[r - 1][c] + Delta * (aColumn[c] * 1.2f / Dist);
				}
		}
	}
}

void CXcCape::RenderCloth(const SState &State, float Scale, int ClientId, float Alpha)
{
	const SCloth &Cloth = State.m_Cloth;
	const int Shape = g_Config.m_XcCapeShape;
	const int Pattern = g_Config.m_XcCapePattern;
	const float Opacity = Alpha * g_Config.m_XcCapeAlpha / 100.0f;
	const bool Ragged = Shape == SHAPE_RAGGED;
	const bool Picture = Pattern == PATTERN_PICTURE && m_Pictures.HasFrames();
	const float Width = g_Config.m_XcCapeWidth / 100.0f;
	const bool Royal = Shape == SHAPE_ROYAL;
	const float TopWidth = (Royal ? 20.0f : 14.0f) * Width * Scale;
	const float BottomWidth = (Royal ? 38.0f : 30.0f) * Width * Scale;

	// colors along the length
	ColorRGBA aOuter[ROWS], aInner[ROWS];
	const ColorRGBA OwnInner = ConfigColor(g_Config.m_XcCapeInnerColor);
	for(int r = 0; r < ROWS; r++)
	{
		const float t = (float)r / (ROWS - 1);
		const ColorRGBA Main = EffectColor(GameClient(), g_Config.m_XcCapeColorMode, g_Config.m_XcCapeColor, ClientId, t * 0.35f, 1.0f);
		const ColorRGBA Inner = g_Config.m_XcCapeInnerMode ? OwnInner : Shaded(Main, 0.45f, 1.0f);
		aOuter[r] = Main;
		aInner[r] = Inner;
		if(Pattern == PATTERN_GRADIENT)
			aOuter[r] = ColorRGBA(mix(Main.r, Inner.r, t * 0.85f), mix(Main.g, Inner.g, t * 0.85f), mix(Main.b, Inner.b, t * 0.85f), 1.0f);
		if(Picture)
			aOuter[r] = aInner[r] = ColorRGBA(1.0f, 1.0f, 1.0f, 1.0f);
	}

	// folds: where the cloth is squeezed sideways it gets darker, stretched parts catch the light
	float aShade[ROWS][COLS];
	for(int r = 0; r < ROWS; r++)
	{
		const float t = (float)r / (ROWS - 1);
		const float Rest = mix(TopWidth, BottomWidth, t) / (COLS - 1);
		for(int c = 0; c < COLS; c++)
		{
			float Stretch = 0.0f;
			int Count = 0;
			if(c > 0)
			{
				Stretch += distance(Cloth.m_aPoints[r][c], Cloth.m_aPoints[r][c - 1]);
				Count++;
			}
			if(c + 1 < COLS)
			{
				Stretch += distance(Cloth.m_aPoints[r][c], Cloth.m_aPoints[r][c + 1]);
				Count++;
			}
			const float Ratio = std::clamp(Stretch / std::max(Count * Rest, 0.001f), 0.0f, 1.2f);
			// the side towards the tee is a little darker
			const float Side = (2.0f * c / (COLS - 1) - 1.0f) * State.m_Back;
			aShade[r][c] = (0.62f + 0.38f * std::min(Ratio, 1.0f)) * (1.0f - 0.22f * t) * (0.92f + 0.08f * Side);
		}
	}

	// the corners of a cell; ragged capes end in teeth
	auto Corners = [&](int r, int c, vec2 *pOut) {
		pOut[0] = Cloth.m_aPoints[r][c];
		pOut[1] = Cloth.m_aPoints[r][c + 1];
		pOut[2] = Cloth.m_aPoints[r + 1][c];
		pOut[3] = Cloth.m_aPoints[r + 1][c + 1];
		if(Ragged && r + 2 == ROWS)
		{
			const vec2 Tip = (pOut[2] + pOut[3]) * 0.5f + ((pOut[2] + pOut[3]) * 0.5f - (pOut[0] + pOut[1]) * 0.5f) * 0.3f;
			pOut[2] = pOut[3] = Tip;
		}
	};
	// the edge of the cloth: both sides and the bottom
	const ColorRGBA Dark = Shaded(aOuter[ROWS / 2], Picture ? 0.25f : 0.32f, Opacity);
	auto ForEdges = [&](auto &&Fn) {
		for(int r = 0; r + 1 < ROWS; r++)
		{
			Fn(Cloth.m_aPoints[r][0], Cloth.m_aPoints[r + 1][0], (float)r / (ROWS - 1));
			Fn(Cloth.m_aPoints[r][COLS - 1], Cloth.m_aPoints[r + 1][COLS - 1], (float)r / (ROWS - 1));
		}
		for(int c = 0; c + 1 < COLS; c++)
		{
			vec2 aCell[4];
			Corners(ROWS - 2, c, aCell);
			if(Ragged)
			{
				// the teeth
				Fn(aCell[0], aCell[2], 1.0f);
				Fn(aCell[2], aCell[1], 1.0f);
			}
			else
				Fn(aCell[2], aCell[3], 1.0f);
		}
	};

	// a thin dark border first, the cloth over it
	Graphics()->TextureClear();
	Graphics()->QuadsBegin();
	ForEdges([&](vec2 a, vec2 b, float) { DrawSegment(Graphics(), a, b, 2.8f * Scale, Dark); });
	Graphics()->QuadsEnd();

	if(Picture)
		Graphics()->TextureSet(m_Pictures.CurrentFrame());
	else
		Graphics()->TextureClear();
	Graphics()->QuadsBegin();
	for(int r = 0; r + 1 < ROWS; r++)
	{
		const bool Stripe = Pattern == PATTERN_STRIPES && (r / 2) % 2 == 1;
		for(int c = 0; c + 1 < COLS; c++)
		{
			vec2 aCell[4];
			Corners(r, c, aCell);
			// a cell turned over shows the inner side of the cape
			const vec2 A = aCell[1] - aCell[0], B = aCell[2] - aCell[0];
			const bool Inside = A.x * B.y - A.y * B.x < 0.0f;
			const ColorRGBA *pTop = Inside ? aInner : aOuter;
			const ColorRGBA *pBottomSide = pTop;
			if(Stripe && !Inside)
				pTop = pBottomSide = aInner;
			const float InsideShade = Inside && Picture ? 0.45f : 1.0f;
			Graphics()->SetColor4(Shaded(pTop[r], aShade[r][c] * InsideShade, Opacity), Shaded(pTop[r], aShade[r][c + 1] * InsideShade, Opacity),
				Shaded(pBottomSide[r + 1], aShade[r + 1][c] * InsideShade, Opacity), Shaded(pBottomSide[r + 1], aShade[r + 1][c + 1] * InsideShade, Opacity));
			if(Picture)
			{
				const float u0 = (float)c / (COLS - 1), u1 = (float)(c + 1) / (COLS - 1);
				const float v0 = (float)r / (ROWS - 1), v1 = (float)(r + 1) / (ROWS - 1);
				const float Mid = (u0 + u1) * 0.5f;
				if(Ragged && r + 2 == ROWS)
					Graphics()->QuadsSetSubsetFree(u0, v0, u1, v0, Mid, 1.0f, Mid, 1.0f);
				else
					Graphics()->QuadsSetSubsetFree(u0, v0, u1, v0, u0, v1, u1, v1);
			}
			const IGraphics::CFreeformItem Item(aCell[0], aCell[1], aCell[2], aCell[3]);
			Graphics()->QuadsDrawFreeform(&Item, 1);
		}
	}
	Graphics()->QuadsEnd();

	// trim: a colored edge or fur, and the fur collar of the royal cape
	const int Trim = g_Config.m_XcCapeTrim;
	if(Trim == 0 && !Royal)
		return;
	Graphics()->TextureClear();
	Graphics()->QuadsBegin();
	if(Trim == 1)
	{
		const ColorRGBA TrimColor = ConfigColor(g_Config.m_XcCapeTrimColor);
		ForEdges([&](vec2 a, vec2 b, float t) { DrawSegment(Graphics(), a, b, 2.0f * Scale, Shaded(TrimColor, 1.0f - 0.2f * t, Opacity)); });
	}
	const ColorRGBA Fur(0.96f, 0.94f, 0.9f, Opacity);
	const ColorRGBA FurShade(0.78f, 0.76f, 0.72f, Opacity);
	const ColorRGBA Spot(0.08f, 0.08f, 0.1f, Opacity);
	if(Trim == 2)
	{
		// fluffy fur along the bottom with little dark spots
		for(int c = 0; c < COLS; c++)
		{
			const vec2 p = Cloth.m_aPoints[ROWS - 1][c];
			DrawDisc(Graphics(), p, 3.6f * Scale, FurShade);
			DrawDisc(Graphics(), p - vec2(0.0f, 0.6f) * Scale, 3.0f * Scale, Fur);
			if(c + 1 < COLS)
			{
				const vec2 m = (p + Cloth.m_aPoints[ROWS - 1][c + 1]) * 0.5f;
				DrawDisc(Graphics(), m, 3.2f * Scale, Fur);
				if(c % 2 == 0)
					DrawDisc(Graphics(), m + vec2(0.0f, 0.6f) * Scale, 0.9f * Scale, Spot, 6);
			}
		}
	}
	if(Royal || Trim == 2)
	{
		// the collar: a roll of fur over the shoulders
		for(int c = 0; c < COLS; c++)
		{
			const vec2 p = mix(Cloth.m_aPoints[0][c], Cloth.m_aPoints[1][c], 0.3f);
			DrawDisc(Graphics(), p, (Royal ? 5.2f : 3.8f) * Scale, FurShade);
			DrawDisc(Graphics(), p - vec2(0.0f, 0.8f) * Scale, (Royal ? 4.4f : 3.2f) * Scale, Fur);
			if(Royal && c % 2 == 1)
				DrawDisc(Graphics(), p + vec2(0.0f, 1.0f) * Scale, 1.1f * Scale, Spot, 6);
		}
	}
	Graphics()->QuadsEnd();
}

vec2 CXcCape::KnotPos(vec2 Pos, float Scale, float Back) const
{
	return Pos + vec2(Back * 17.0f, 8.0f) * Scale;
}

ColorRGBA CXcCape::ScarfColor(int ClientId, float t) const
{
	return EffectColor(GameClient(), g_Config.m_XcScarfColorMode, g_Config.m_XcScarfColor, ClientId, t * 0.3f, 1.0f);
}

void CXcCape::SimulateTail(STail &Tail, const SState &State, int Index, vec2 Anchor, float Scale, float Dt, float Time, bool Collide) const
{
	const float Length = (Index == 0 ? 40.0f : 32.0f) * Scale * g_Config.m_XcScarfLength / 100.0f;
	const float Segment = Length / (TAIL_POINTS - 1);
	const float Wind = g_Config.m_XcScarfWind / 100.0f;
	const float React = g_Config.m_XcCapeReact / 100.0f;
	const float Speed = std::min(length(State.m_Vel) / 900.0f, 1.5f) * React;
	const float Phase = Index * 1.9f;
	if(!Tail.m_Valid || distance(Tail.m_aPoints[0], Anchor) > 300.0f)
	{
		for(int i = 0; i < TAIL_POINTS; i++)
		{
			Tail.m_aPoints[i] = Anchor + normalize(vec2(State.m_Back * (0.6f + Index * 0.3f), 1.0f)) * Segment * i;
			Tail.m_aPrev[i] = Tail.m_aPoints[i];
		}
		Tail.m_Valid = true;
		Tail.m_Accumulator = 0.0f;
	}
	Tail.m_Accumulator = std::min(Tail.m_Accumulator + Dt, 0.1f);
	const vec2 Start = Tail.m_aPoints[0];
	const int NumSteps = (int)(Tail.m_Accumulator / STEP);
	int Steps = 0;
	while(Tail.m_Accumulator >= STEP)
	{
		Tail.m_Accumulator -= STEP;
		Steps++;
		Tail.m_aPoints[0] = mix(Start, Anchor, (float)Steps / std::max(NumSteps, 1));
		Tail.m_aPrev[0] = Tail.m_aPoints[0];
		for(int i = 1; i < TAIL_POINTS; i++)
		{
			const float t = (float)i / (TAIL_POINTS - 1);
			vec2 Force(0.0f, 1500.0f * Scale);
			Force.x += State.m_Back * (900.0f + Index * 250.0f) * Scale;
			const float Flutter = Wind * t * Scale * (1.0f + Speed * 1.5f);
			Force.x += std::sin(Time * 2.6f + i * 0.8f + Phase) * 280.0f * Flutter;
			Force.y += std::sin(Time * 3.4f + i * 1.2f + Phase) * 200.0f * Flutter;
			Force -= State.m_Pull * 1400.0f * React * t * Scale;
			const vec2 Velocity = (Tail.m_aPoints[i] - Tail.m_aPrev[i]) * 0.97f;
			Tail.m_aPrev[i] = Tail.m_aPoints[i];
			Tail.m_aPoints[i] += Velocity + Force * STEP * STEP;
		}
		for(int Iteration = 0; Iteration < 4; Iteration++)
		{
			for(int i = 1; i < TAIL_POINTS; i++)
			{
				const vec2 Delta = Tail.m_aPoints[i] - Tail.m_aPoints[i - 1];
				const float Dist = length(Delta);
				if(Dist <= 0.0001f)
					continue;
				const vec2 Fix = Delta * ((Dist - Segment) / Dist);
				if(i == 1)
					Tail.m_aPoints[i] -= Fix;
				else
				{
					Tail.m_aPoints[i] -= Fix * 0.5f;
					Tail.m_aPoints[i - 1] += Fix * 0.5f;
				}
			}
		}
		if(Collide)
		{
			for(int i = 1; i < TAIL_POINTS; i++)
				this->Collide(Tail.m_aPoints[i], Tail.m_aPrev[i]);
			for(int i = 1; i < TAIL_POINTS; i++)
			{
				const vec2 Delta = Tail.m_aPoints[i] - Tail.m_aPoints[i - 1];
				const float Dist = length(Delta);
				if(Dist > Segment * 1.15f)
					Tail.m_aPoints[i] = Tail.m_aPoints[i - 1] + Delta * (Segment * 1.15f / Dist);
			}
		}
	}
}

void CXcCape::RenderTails(const SState &State, float Scale, int ClientId, float Alpha) const
{
	const ColorRGBA StripeColor = ConfigColor(g_Config.m_XcScarfStripeColor);
	const float Half = 4.6f * Scale;
	Graphics()->TextureClear();
	Graphics()->QuadsBegin();
	// the second end hangs behind the first one
	for(int Index = g_Config.m_XcScarfTails - 1; Index >= 0; Index--)
	{
		const STail &Tail = State.m_aTails[Index];
		if(!Tail.m_Valid)
			continue;
		const float Behind = Index == 1 ? 0.8f : 1.0f;
		vec2 aLeft[TAIL_POINTS], aRight[TAIL_POINTS];
		for(int i = 0; i < TAIL_POINTS; i++)
		{
			const vec2 Dir = normalize(i == 0 ? Tail.m_aPoints[1] - Tail.m_aPoints[0] : Tail.m_aPoints[i] - Tail.m_aPoints[i - 1]);
			const vec2 Normal(-Dir.y, Dir.x);
			const float w = Half * (1.0f - 0.12f * i / (TAIL_POINTS - 1));
			aLeft[i] = Tail.m_aPoints[i] + Normal * w;
			aRight[i] = Tail.m_aPoints[i] - Normal * w;
		}
		for(int Pass = 0; Pass < 2; Pass++)
		{
			const float Grow = Pass == 0 ? 1.3f * Scale : 0.0f;
			for(int i = 0; i + 1 < TAIL_POINTS; i++)
			{
				const float t0 = (float)i / (TAIL_POINTS - 1), t1 = (float)(i + 1) / (TAIL_POINTS - 1);
				const bool Stripe = g_Config.m_XcScarfStripes && i % 3 == 1;
				ColorRGBA A = Stripe ? StripeColor : ScarfColor(ClientId, t0), B = Stripe ? StripeColor : ScarfColor(ClientId, t1);
				// knitted rows: every segment a bit lighter or darker
				const float Rib = (i % 2 ? 0.88f : 1.0f) * Behind;
				float ShadeA = Rib * (1.0f - 0.15f * t0), ShadeB = Rib * (1.0f - 0.15f * t1);
				if(Pass == 0)
					ShadeA = ShadeB = 0.3f;
				const ColorRGBA AL = Shaded(A, ShadeA, Alpha), AR = Shaded(A, ShadeA * 0.82f, Alpha);
				const ColorRGBA BL = Shaded(B, ShadeB, Alpha), BR = Shaded(B, ShadeB * 0.82f, Alpha);
				const vec2 Dir0 = normalize(aLeft[i] - aRight[i]) * Grow, Dir1 = normalize(aLeft[i + 1] - aRight[i + 1]) * Grow;
				Graphics()->SetColor4(AL, AR, BL, BR);
				const IGraphics::CFreeformItem Item(aLeft[i] + Dir0, aRight[i] - Dir0, aLeft[i + 1] + Dir1, aRight[i + 1] - Dir1);
				Graphics()->QuadsDrawFreeform(&Item, 1);
			}
		}
		if(g_Config.m_XcScarfFringe)
		{
			// fringe: short threads at the end
			const vec2 End = Tail.m_aPoints[TAIL_POINTS - 1];
			const vec2 Dir = normalize(End - Tail.m_aPoints[TAIL_POINTS - 2]);
			const ColorRGBA Thread = Shaded(ScarfColor(ClientId, 1.0f), 0.8f * Behind, Alpha);
			for(int k = 0; k < 4; k++)
			{
				const float u = (k + 0.5f) / 4.0f;
				const vec2 Base = mix(aLeft[TAIL_POINTS - 1], aRight[TAIL_POINTS - 1], u);
				const vec2 Tip = Base + Dir * 5.0f * Scale + vec2(-Dir.y, Dir.x) * std::sin(u * 7.0f) * 0.8f * Scale;
				DrawSegment(Graphics(), Base, Tip, 1.4f * Scale, Thread);
			}
		}
	}
	Graphics()->QuadsEnd();
}

void CXcCape::RenderWrap(vec2 Pos, float Scale, float Back, int ClientId, float Alpha) const
{
	// the band around the neck, bent down in the middle as it wraps around the round body
	static constexpr int NUM = 10;
	const ColorRGBA StripeColor = ConfigColor(g_Config.m_XcScarfStripeColor);
	const float Half = 4.4f * Scale;
	vec2 aCenter[NUM + 1];
	for(int i = 0; i <= NUM; i++)
	{
		const float u = 2.0f * i / NUM - 1.0f;
		aCenter[i] = Pos + vec2(u * 21.0f, 6.0f + 4.0f * (1.0f - u * u)) * Scale;
	}
	Graphics()->TextureClear();
	Graphics()->QuadsBegin();
	for(int Pass = 0; Pass < 2; Pass++)
	{
		const float Grow = Pass == 0 ? 1.3f * Scale : 0.0f;
		for(int i = 0; i < NUM; i++)
		{
			const vec2 Dir = normalize(aCenter[i + 1] - aCenter[i]);
			const vec2 Normal = vec2(-Dir.y, Dir.x) * (Half + Grow);
			const bool Stripe = g_Config.m_XcScarfStripes && (i == 3 || i == 6);
			const float t = (float)i / NUM;
			ColorRGBA Color = Stripe ? StripeColor : ScarfColor(ClientId, t);
			// the ends of the band go around the body, so they are darker
			const float Round = 1.0f - 0.3f * std::abs(2.0f * t - 1.0f);
			const float Shade = Pass == 0 ? 0.3f : (i % 2 ? 0.9f : 1.0f) * Round;
			Graphics()->SetColor4(Shaded(Color, Shade * 1.08f, Alpha), Shaded(Color, Shade * 1.08f, Alpha), Shaded(Color, Shade * 0.8f, Alpha), Shaded(Color, Shade * 0.8f, Alpha));
			const IGraphics::CFreeformItem Item(aCenter[i] - Normal, aCenter[i + 1] - Normal, aCenter[i] + Normal, aCenter[i + 1] + Normal);
			Graphics()->QuadsDrawFreeform(&Item, 1);
		}
	}
	// the knot at the back where the ends come out
	const vec2 Knot = KnotPos(Pos, Scale, Back);
	const ColorRGBA KnotColor = ScarfColor(ClientId, 0.0f);
	DrawDisc(Graphics(), Knot, 6.0f * Scale, Shaded(KnotColor, 0.3f, Alpha), 12);
	DrawDisc(Graphics(), Knot, 4.8f * Scale, Shaded(KnotColor, 0.85f, Alpha), 12);
	DrawDisc(Graphics(), Knot - vec2(0.8f, 1.0f) * Scale, 2.4f * Scale, Shaded(KnotColor, 1.1f, Alpha), 8);
	Graphics()->QuadsEnd();
}

void CXcCape::OnRender()
{
	// the scarf used to be the second style of the cape
	if(g_Config.m_XcCape == 2)
	{
		g_Config.m_XcCape = 0;
		g_Config.m_XcScarf = 1;
	}
	const bool Online = Client()->State() == IClient::STATE_ONLINE || Client()->State() == IClient::STATE_DEMOPLAYBACK;
	const float Dt = std::min(Client()->RenderFrameTime(), 0.1f);
	m_Pictures.Update(g_Config.m_XcCape && g_Config.m_XcCapePattern == PATTERN_PICTURE ? g_Config.m_XcCapeFile : "", Dt, 1.0f);
	if((!g_Config.m_XcCape && !g_Config.m_XcScarf) || !Online)
	{
		OnReset();
		return;
	}
	const float Time = Client()->GlobalTime();
	for(int ClientId = 0; ClientId < MAX_CLIENTS; ClientId++)
	{
		SState &State = m_aStates[ClientId];
		State.m_DrawFront = false;
		const bool Cape = g_Config.m_XcCape && ShowFor(GameClient(), ClientId, g_Config.m_XcCapeSelf, g_Config.m_XcCapeDummy, g_Config.m_XcCapeOthers);
		const bool Scarf = g_Config.m_XcScarf && ShowFor(GameClient(), ClientId, g_Config.m_XcScarfSelf, g_Config.m_XcScarfDummy, g_Config.m_XcScarfOthers);
		const float Alpha = GameClient()->m_Snap.m_aCharacters[ClientId].m_Active ? PlayerAlpha(GameClient(), ClientId) : 0.0f;
		if(!Cape)
			State.m_Cloth.m_Valid = false;
		if(!Scarf)
			for(STail &Tail : State.m_aTails)
				Tail.m_Valid = false;
		if((!Cape && !Scarf) || Alpha <= 0.0f)
		{
			State.m_Cloth.m_Valid = false;
			for(STail &Tail : State.m_aTails)
				Tail.m_Valid = false;
			State.m_HasLast = false;
			continue;
		}
		const CGameClient::CClientData &Data = GameClient()->m_aClients[ClientId];
		const float Scale = Data.m_RenderInfo.m_Size / 64.0f;
		const vec2 Pos = Data.m_RenderPos;
		// turning around swings the cloth over to the other side instead of jumping
		const float BackTarget = LooksLeft(ClientId) ? 1.0f : -1.0f;
		State.m_Back += (BackTarget - State.m_Back) * SmoothFactor(10.0f, Dt);
		State.m_Pull = vec2(0.0f, 0.0f);
		if(Data.m_RenderCur.m_HookState == HOOK_GRABBED)
		{
			const vec2 Hook((float)Data.m_RenderCur.m_HookX, (float)Data.m_RenderCur.m_HookY);
			if(distance(Hook, Pos) > 1.0f)
				State.m_Pull = normalize(Hook - Pos);
		}
		UpdateMotion(State, Pos, Dt, false);
		if(Cape)
		{
			SimulateCloth(State, Pos, Scale, Dt, Time + ClientId * 1.7f, true);
			RenderCloth(State, Scale, ClientId, Alpha);
		}
		if(Scarf)
		{
			for(int i = 0; i < g_Config.m_XcScarfTails; i++)
				SimulateTail(State.m_aTails[i], State, i, KnotPos(Pos, Scale, State.m_Back) + vec2(State.m_Back * 1.5f * i, 1.5f * i) * Scale, Scale, Dt, Time + ClientId * 1.7f, true);
			if(g_Config.m_XcScarfTails < MAX_TAILS)
				State.m_aTails[1].m_Valid = false;
			RenderTails(State, Scale, ClientId, Alpha);
			State.m_DrawFront = true;
			State.m_FrontPos = Pos;
			State.m_FrontScale = Scale;
			State.m_FrontAlpha = Alpha;
		}
	}
}

void CXcCape::RenderFront()
{
	if(!g_Config.m_XcScarf)
		return;
	for(int ClientId = 0; ClientId < MAX_CLIENTS; ClientId++)
	{
		const SState &State = m_aStates[ClientId];
		if(State.m_DrawFront)
			RenderWrap(State.m_FrontPos, State.m_FrontScale, State.m_Back, ClientId, State.m_FrontAlpha);
	}
}

void CXcCapeFront::OnRender()
{
	if(Client()->State() == IClient::STATE_ONLINE || Client()->State() == IClient::STATE_DEMOPLAYBACK)
		m_pCape->RenderFront();
}

CXcCape::SState &CXcCape::PreviewState(vec2 Pos, float Scale)
{
	// the same preview stays near the same place on the screen; a new place takes the slot used longest ago
	const float Now = Client()->GlobalTime();
	int Best = -1;
	for(int i = 0; i < NUM_PREVIEWS; i++)
		if(Now - m_aPreviewTime[i] < 0.5f && distance(m_aPreviewPos[i], Pos) < 40.0f * Scale)
			Best = i;
	if(Best < 0)
	{
		Best = 0;
		for(int i = 1; i < NUM_PREVIEWS; i++)
			if(m_aPreviewTime[i] < m_aPreviewTime[Best])
				Best = i;
		m_aPreviews[Best] = SState();
	}
	m_aPreviewPos[Best] = Pos;
	m_aPreviewTime[Best] = Now;
	return m_aPreviews[Best];
}

void CXcCape::RenderPreview(vec2 Pos, float TeeSize, bool LookingLeft, float Alpha, float Time)
{
	if(!g_Config.m_XcCape && !g_Config.m_XcScarf)
		return;
	const float Dt = std::min(Client()->RenderFrameTime(), 0.1f);
	if(Client()->State() != IClient::STATE_ONLINE && Client()->State() != IClient::STATE_DEMOPLAYBACK)
		m_Pictures.Update(g_Config.m_XcCape && g_Config.m_XcCapePattern == PATTERN_PICTURE ? g_Config.m_XcCapeFile : "", Dt, 1.0f);
	const float Scale = TeeSize / 64.0f;
	SState &State = PreviewState(Pos, Scale);
	const float BackTarget = LookingLeft ? 1.0f : -1.0f;
	State.m_Back += (BackTarget - State.m_Back) * SmoothFactor(10.0f, Dt);
	State.m_Pull = vec2(0.0f, 0.0f);
	if(g_Config.m_XcCape)
	{
		SimulateCloth(State, Pos, Scale, Dt, Time, false);
		RenderCloth(State, Scale, -1, Alpha);
	}
	if(g_Config.m_XcScarf)
	{
		for(int i = 0; i < g_Config.m_XcScarfTails; i++)
			SimulateTail(State.m_aTails[i], State, i, KnotPos(Pos, Scale, State.m_Back) + vec2(State.m_Back * 1.5f * i, 1.5f * i) * Scale, Scale, Dt, Time, false);
		RenderTails(State, Scale, -1, Alpha);
	}
}

void CXcCape::RenderPreviewFront(vec2 Pos, float TeeSize, float Alpha)
{
	if(!g_Config.m_XcScarf)
		return;
	const float Scale = TeeSize / 64.0f;
	const SState &State = PreviewState(Pos, Scale);
	RenderWrap(Pos, Scale, State.m_Back, -1, Alpha);
}

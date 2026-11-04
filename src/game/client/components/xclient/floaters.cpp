#include "floaters.h"

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

using namespace XcHelpers;

// a band around the view where floaters live, so they are already there when the camera moves
static constexpr float VIEW_MARGIN = 0.35f;

void CXcFloaters::OnReset()
{
	m_vFloaters.clear();
	m_HasCamera = false;
}

void CXcFloaters::OnMapLoad()
{
	OnReset();
}

void CXcFloaters::Spawn(SFloater &Floater, float X0, float Y0, float X1, float Y1, bool Anywhere)
{
	const float Width = X1 - X0, Height = Y1 - Y0;
	if(Anywhere)
		Floater.m_Pos = vec2(random_float(X0, X1), random_float(Y0, Y1));
	else
	{
		// on one edge of the living area, so nothing pops up in the middle of the screen
		switch(rand() % 4)
		{
		case 0: Floater.m_Pos = vec2(X0 + random_float(0.0f, Width * 0.12f), random_float(Y0, Y1)); break;
		case 1: Floater.m_Pos = vec2(X1 - random_float(0.0f, Width * 0.12f), random_float(Y0, Y1)); break;
		case 2: Floater.m_Pos = vec2(random_float(X0, X1), Y0 + random_float(0.0f, Height * 0.12f)); break;
		default: Floater.m_Pos = vec2(random_float(X0, X1), Y1 - random_float(0.0f, Height * 0.12f)); break;
		}
	}
	const float Angle = random_float(0.0f, 2.0f * pi);
	Floater.m_Drift = direction(Angle) * random_float(6.0f, 22.0f);
	Floater.m_Vel = Floater.m_Drift;
	Floater.m_Depth = random_float(0.4f, 1.0f);
	Floater.m_Size = random_float(15.0f, 28.0f);
	for(int i = 0; i < 3; i++)
	{
		Floater.m_aAngle[i] = random_float(0.0f, 2.0f * pi);
		Floater.m_aSpin[i] = random_float(-0.9f, 0.9f);
	}
	Floater.m_Hue = random_float(0.0f, 1.0f);
	Floater.m_Life = 0.0f;
	// one of the kinds that are on
	const int Mask = Kinds();
	int aKinds[NUM_KINDS], Num = 0;
	for(int k = 0; k < NUM_KINDS; k++)
		if(Mask & (1 << k))
			aKinds[Num++] = k;
	Floater.m_Kind = Num > 0 ? aKinds[rand() % Num] : KIND_ORB;
	Floater.m_Script = g_Config.m_XcFloatersRunes == 3 ? rand() % 3 : g_Config.m_XcFloatersRunes;
	Floater.m_Glyph = rand();
	Floater.m_Phase = random_float(0.0f, 100.0f);
	if(Floater.m_Kind == KIND_HEART)
	{
		// hearts rise slowly
		Floater.m_Drift = vec2(random_float(-6.0f, 6.0f), -random_float(10.0f, 22.0f));
		Floater.m_Vel = Floater.m_Drift;
	}
	else if(Floater.m_Kind == KIND_FIREFLY)
		Floater.m_Size = random_float(6.0f, 10.0f);
}

int CXcFloaters::Kinds()
{
	if(g_Config.m_XcFloatersKinds)
		return g_Config.m_XcFloatersKinds;
	switch(g_Config.m_XcFloaters)
	{
	case FLOATERS_CUBES: return 1 << KIND_CUBE;
	case FLOATERS_ORBS: return 1 << KIND_ORB;
	default: return (1 << KIND_CUBE) | (1 << KIND_ORB);
	}
}

namespace
{
	// the runes as strokes in a box of -1..1 wide and -1.5..1.5 high
	struct SStroke
	{
		float x0, y0, x1, y1;
	};
	struct SRune
	{
		int m_Num;
		SStroke m_aStrokes[6];
	};
	// Elder Futhark
	const SRune s_aNorse[] = {
		{3, {{0, -1.5f, 0, 1.5f}, {0, -0.6f, 0.8f, -1.3f}, {0, 0.1f, 0.8f, -0.6f}}}, // fehu
		{3, {{-0.5f, 1.5f, -0.5f, -1.5f}, {-0.5f, -1.5f, 0.6f, -0.6f}, {0.6f, -0.6f, 0.6f, 1.5f}}}, // uruz
		{3, {{0, -1.5f, 0, 1.5f}, {0, -0.6f, 0.7f, 0}, {0.7f, 0, 0, 0.6f}}}, // thurisaz
		{3, {{0, -1.5f, 0, 1.5f}, {0, -1.5f, 0.7f, -0.9f}, {0, -0.8f, 0.7f, -0.2f}}}, // ansuz
		{4, {{0, -1.5f, 0, 1.5f}, {0, -1.5f, 0.7f, -0.9f}, {0.7f, -0.9f, 0, -0.2f}, {0, -0.2f, 0.7f, 1.5f}}}, // raido
		{2, {{0.6f, -1.0f, -0.4f, 0}, {-0.4f, 0, 0.6f, 1.0f}}}, // kaunan
		{2, {{-0.7f, -1.2f, 0.7f, 1.2f}, {0.7f, -1.2f, -0.7f, 1.2f}}}, // gebo
		{3, {{0, -1.5f, 0, 1.5f}, {0, -1.5f, 0.7f, -0.9f}, {0.7f, -0.9f, 0, -0.3f}}}, // wunjo
		{3, {{-0.6f, -1.5f, -0.6f, 1.5f}, {0.6f, -1.5f, 0.6f, 1.5f}, {-0.6f, -0.4f, 0.6f, 0.4f}}}, // hagalaz
		{2, {{0, -1.5f, 0, 1.5f}, {-0.6f, -0.5f, 0.6f, 0.5f}}}, // naudiz
		{3, {{0, -1.5f, 0, 1.5f}, {0, -1.5f, 0.6f, -1.0f}, {0, 1.5f, -0.6f, 1.0f}}}, // eihwaz
		{3, {{0, -1.5f, 0, 1.5f}, {0, -0.4f, -0.7f, -1.3f}, {0, -0.4f, 0.7f, -1.3f}}}, // algiz
		{3, {{0.5f, -1.4f, -0.5f, -0.3f}, {-0.5f, -0.3f, 0.5f, 0.3f}, {0.5f, 0.3f, -0.5f, 1.4f}}}, // sowilo
		{3, {{0, -1.5f, 0, 1.5f}, {0, -1.5f, -0.7f, -0.7f}, {0, -1.5f, 0.7f, -0.7f}}}, // tiwaz
		{5, {{0, -1.5f, 0, 1.5f}, {0, -1.5f, 0.6f, -0.8f}, {0.6f, -0.8f, 0, 0}, {0, 0, 0.6f, 0.8f}, {0.6f, 0.8f, 0, 1.5f}}}, // berkanan
		{4, {{-0.6f, -1.5f, -0.6f, 1.5f}, {0.6f, -1.5f, 0.6f, 1.5f}, {-0.6f, -1.5f, 0.6f, -0.3f}, {0.6f, -1.5f, -0.6f, -0.3f}}}, // mannaz
		{6, {{0, -1.4f, 0.6f, -0.5f}, {0.6f, -0.5f, 0, 0.4f}, {0, 0.4f, -0.6f, -0.5f}, {-0.6f, -0.5f, 0, -1.4f}, {0, 0.4f, 0.7f, 1.5f}, {0, 0.4f, -0.7f, 1.5f}}}, // othala
	};
} // namespace

void CXcFloaters::DrawShape(const SFloater &Floater, float Size, ColorRGBA Color, float Alpha, float Time) const
{
	const FxGlow::CScope GlowScope(FxGlow::FLOATERS);
	CFxRenderer &Fx = GameClient()->m_FxRenderer;
	const vec2 Pos = Floater.m_Pos;
	const ColorRGBA c = Color.WithAlpha(Alpha);
	const ColorRGBA Hot(mix(Color.r, 1.0f, 0.55f), mix(Color.g, 1.0f, 0.55f), mix(Color.b, 1.0f, 0.55f), Alpha);
	const float u = Size * 0.32f; // the unit of the drawings
	// a slow sway instead of the full 3D turn of the cubes
	const float Angle = std::sin(Floater.m_aAngle[0]) * 0.35f;
	const auto P = [&](float x, float y) { return Rotate(Pos + vec2(x, y) * u, Pos, Angle); };
	const float Line = std::max(u * 0.22f, 0.8f);
	// a colored stroke with a light core: the color shows on bright maps, the light on dark ones
	const ColorRGBA Ink(Color.r * 0.8f, Color.g * 0.8f, Color.b * 0.8f, Alpha);
	const auto Stroke = [&](vec2 A, vec2 B) {
		Fx.AddWorldGlowing(FxShape::Line(A, B, Line * 1.7f, Ink), u * 1.2f, 0.4f);
		Fx.AddWorld(FxShape::Line(A, B, Line * 0.7f, Hot).Additive());
	};
	const auto Arc = [&](float cx, float cy, float r, float a0, float a1) {
		const int Num = 6;
		vec2 Prev = P(cx + std::cos(a0) * r, cy + std::sin(a0) * r);
		for(int i = 1; i <= Num; i++)
		{
			const float a = mix(a0, a1, (float)i / Num);
			const vec2 Next = P(cx + std::cos(a) * r, cy + std::sin(a) * r);
			Stroke(Prev, Next);
			Prev = Next;
		}
	};

	switch(Floater.m_Kind)
	{
	case KIND_FIREFLY:
	{
		// blinks: lit for a while, then nearly dark
		const float Blink = 0.5f + 0.5f * std::sin(Time * 1.6f + Floater.m_Phase);
		const float Lit = Blink * Blink * Blink;
		Fx.AddWorld(FxShape::Circle(Pos, std::max(u * 0.45f, 1.0f), Hot.WithMultipliedAlpha(0.25f + 0.75f * Lit)).GlowOnly(u * 4.0f, 0.9f * Lit + 0.1f).Additive());
		Fx.AddWorld(FxShape::Circle(Pos, std::max(u * 0.3f, 0.8f), ColorRGBA(1.0f, 1.0f, 0.9f, Alpha * (0.3f + 0.7f * Lit))).Additive());
		break;
	}
	case KIND_NOTE:
	{
		// one note or two joined ones; the head is a tilted oval, the stem goes up from its right side
		const bool Double = Floater.m_Glyph % 2;
		const int NumHeads = Double ? 2 : 1;
		for(int i = 0; i < NumHeads; i++)
		{
			const float x = Double ? (i ? 1.0f : -1.0f) : 0.0f;
			const float y = Double && i ? 0.6f : 1.0f;
			Fx.AddWorldGlowing(FxShape::Ellipse(P(x - 0.3f, y + 0.9f), vec2(0.85f, 0.6f) * u, c).Rotate(Angle - 0.4f), u * 1.5f, 0.5f);
			Stroke(P(x + 0.45f, y + 0.8f), P(x + 0.45f, y - 1.8f));
		}
		if(Double)
		{
			Fx.AddWorldGlowing(FxShape::Line(P(-0.55f, -0.8f), P(1.45f, -1.2f), Line * 2.2f, Hot).Additive(), u * 1.2f, 0.5f);
		}
		else
		{
			Stroke(P(0.45f, -0.8f), P(1.3f, -0.1f));
			Stroke(P(1.3f, -0.1f), P(1.0f, 0.6f));
		}
		break;
	}
	case KIND_HEART:
	{
		// two circles and a triangle pointing down
		const float Beat = 1.0f + 0.08f * std::sin(Time * 4.0f + Floater.m_Phase);
		const float h = u * Beat;
		const auto H = [&](float x, float y) { return Rotate(Pos + vec2(x, y) * h, Pos, Angle); };
		Fx.AddWorldGlowing(FxShape::Circle(H(-0.62f, -0.35f), 0.75f * h, c), h * 1.5f, 0.45f);
		Fx.AddWorldGlowing(FxShape::Circle(H(0.62f, -0.35f), 0.75f * h, c), h * 1.5f, 0.45f);
		Fx.AddWorld(FxShape::Triangle(H(0.0f, 0.45f), vec2(1.33f, 0.95f) * h, c).Rotate(Angle + pi));
		Fx.AddWorld(FxShape::Circle(H(-0.75f, -0.55f), 0.25f * h, ColorRGBA(1.0f, 1.0f, 1.0f, 0.6f * Alpha)).Additive());
		break;
	}
	case KIND_RUNE:
	{
		if(Floater.m_Script == 0)
		{
			const SRune &Rune = s_aNorse[(unsigned)Floater.m_Glyph % (sizeof(s_aNorse) / sizeof(s_aNorse[0]))];
			for(int i = 0; i < Rune.m_Num; i++)
				Stroke(P(Rune.m_aStrokes[i].x0, Rune.m_aStrokes[i].y0), P(Rune.m_aStrokes[i].x1, Rune.m_aStrokes[i].y1));
		}
		else if(Floater.m_Script == 1)
		{
			// Elvish in the manner of the tengwar: a stem up or down, one or two bows to one side, a mark above
			const unsigned g = (unsigned)Floater.m_Glyph;
			const bool StemUp = g & 1, TwoBows = g & 2, Left = g & 4, Mark = g & 8, Open = g & 16;
			const float Side = Left ? -1.0f : 1.0f;
			Stroke(P(-Side * 0.5f, StemUp ? -1.6f : -0.5f), P(-Side * 0.5f, StemUp ? 0.6f : 1.6f));
			for(int b = 0; b < (TwoBows ? 2 : 1); b++)
			{
				const float cx = -Side * 0.5f + Side * (0.45f + b * 0.75f);
				const float a0 = Left ? pi * 0.5f : -pi * 0.5f;
				Arc(cx, 0.05f, 0.5f, a0, a0 + (Left ? pi : -pi) * (Open && b == (TwoBows ? 1 : 0) ? -0.75f : -1.0f));
			}
			if(Mark)
				Arc(0.0f, -1.55f, 0.45f, pi * 1.15f, pi * 1.85f);
			else
				Fx.AddWorldGlowing(FxShape::Circle(P(0.1f, -1.35f), Line * 0.9f, Hot).Additive(), u, 0.4f);
		}
		else
		{
			// alchemy: the elements, the sun, salt, sulphur, mercury, the moon
			const auto Triangle = [&](bool Up, bool Bar) {
				const float t = Up ? -1.0f : 1.0f;
				const vec2 A = P(0.0f, t * 1.3f), B = P(-1.1f, -t * 0.7f), C = P(1.1f, -t * 0.7f);
				Stroke(A, B);
				Stroke(B, C);
				Stroke(C, A);
				if(Bar)
					Stroke(P(-0.9f, t * 0.35f), P(0.9f, t * 0.35f));
			};
			const auto Circle = [&](float cx, float cy, float r) {
				Fx.AddWorldGlowing(FxShape::Ring(P(cx, cy), vec2(r, r) * u, Line * 1.7f, Ink), u * 1.2f, 0.4f);
				Fx.AddWorld(FxShape::Ring(P(cx, cy), vec2(r, r) * u, Line * 0.7f, Hot).Additive());
			};
			switch((unsigned)Floater.m_Glyph % 10)
			{
			case 0: Triangle(true, false); break; // fire
			case 1: Triangle(false, false); break; // water
			case 2: Triangle(true, true); break; // air
			case 3: Triangle(false, true); break; // earth
			case 4: // the sun
				Circle(0.0f, 0.0f, 1.1f);
				Fx.AddWorldGlowing(FxShape::Circle(P(0.0f, 0.0f), Line * 1.4f, Hot).Additive(), u, 0.5f);
				break;
			case 5: // salt
				Circle(0.0f, 0.0f, 1.1f);
				Stroke(P(-1.1f, 0.0f), P(1.1f, 0.0f));
				break;
			case 6: // sulphur: a triangle on a cross
				Stroke(P(0.0f, -1.5f), P(-0.8f, -0.2f));
				Stroke(P(-0.8f, -0.2f), P(0.8f, -0.2f));
				Stroke(P(0.8f, -0.2f), P(0.0f, -1.5f));
				Stroke(P(0.0f, -0.2f), P(0.0f, 1.5f));
				Stroke(P(-0.6f, 0.75f), P(0.6f, 0.75f));
				break;
			case 7: // mercury: horns, a circle, a cross
				Arc(0.0f, -1.45f, 0.55f, pi * 0.1f, pi * 0.9f);
				Circle(0.0f, -0.35f, 0.6f);
				Stroke(P(0.0f, 0.25f), P(0.0f, 1.5f));
				Stroke(P(-0.5f, 0.9f), P(0.5f, 0.9f));
				break;
			case 8: // the moon: a crescent
				Arc(0.0f, 0.0f, 1.2f, pi * 0.35f, pi * 1.65f);
				Arc(0.55f, 0.0f, 0.95f, pi * 0.55f, pi * 1.45f);
				break;
			default: // copper (Venus): a circle on a cross
				Circle(0.0f, -0.55f, 0.75f);
				Stroke(P(0.0f, 0.2f), P(0.0f, 1.5f));
				Stroke(P(-0.6f, 0.85f), P(0.6f, 0.85f));
				break;
			}
		}
		break;
	}
	default:
		break;
	}
}

// soft round light, additive: bright middle, nothing at the edge
void CXcFloaters::DrawGlow(vec2 Pos, float Radius, ColorRGBA Color) const
{
	const FxGlow::SScale Glow = FxGlow::For(FxGlow::FLOATERS);
	if(Glow.m_Strength <= 0.0f)
		return;
	Radius *= Glow.m_Radius;
	Color.a = std::min(Color.a * Glow.m_Strength, 1.0f);
	constexpr int SEGMENTS = 20;
	const ColorRGBA Clear = Color.WithAlpha(0.0f);
	for(int i = 0; i < SEGMENTS; i++)
	{
		const vec2 A = Pos + direction(2.0f * pi * i / SEGMENTS) * Radius;
		const vec2 B = Pos + direction(2.0f * pi * (i + 1) / SEGMENTS) * Radius;
		Graphics()->SetColor4(Color, Color, Clear, Clear);
		const IGraphics::CFreeformItem Item(Pos, Pos, A, B);
		Graphics()->QuadsDrawFreeform(&Item, 1);
	}
}

void CXcFloaters::DrawCube(const SFloater &Floater, vec2 Pos, float Size, ColorRGBA Color, float Alpha) const
{
	// the eight corners, turned around all three axes
	const float Ca = std::cos(Floater.m_aAngle[0]), Sa = std::sin(Floater.m_aAngle[0]);
	const float Cb = std::cos(Floater.m_aAngle[1]), Sb = std::sin(Floater.m_aAngle[1]);
	const float Cc = std::cos(Floater.m_aAngle[2]), Sc = std::sin(Floater.m_aAngle[2]);
	const auto Turn = [&](vec3 v) {
		v = vec3(v.x, v.y * Ca - v.z * Sa, v.y * Sa + v.z * Ca);
		v = vec3(v.x * Cb + v.z * Sb, v.y, -v.x * Sb + v.z * Cb);
		return vec3(v.x * Cc - v.y * Sc, v.x * Sc + v.y * Cc, v.z);
	};
	vec3 aCorners[8];
	for(int i = 0; i < 8; i++)
		aCorners[i] = Turn(vec3(i & 1 ? 1.0f : -1.0f, i & 2 ? 1.0f : -1.0f, i & 4 ? 1.0f : -1.0f) * (Size * 0.5f));
	// faces as corner loops, with the outward normal before turning
	struct SFace
	{
		int m_aIndex[4];
		vec3 m_Normal;
	};
	static const SFace s_aFaces[6] = {
		{{0, 1, 3, 2}, vec3(0, 0, -1)},
		{{4, 6, 7, 5}, vec3(0, 0, 1)},
		{{0, 2, 6, 4}, vec3(-1, 0, 0)},
		{{1, 5, 7, 3}, vec3(1, 0, 0)},
		{{0, 4, 5, 1}, vec3(0, -1, 0)},
		{{2, 3, 7, 6}, vec3(0, 1, 0)},
	};
	// light from the top left and a bit in front
	const vec3 Light = normalize(vec3(-0.45f, -0.65f, -0.6f));
	for(const SFace &Face : s_aFaces)
	{
		const vec3 Normal = Turn(Face.m_Normal);
		if(Normal.z > 0.0f)
			continue; // facing away (the viewer looks along +z)
		const float Shade = 0.42f + 0.58f * std::max(0.0f, dot(Normal, Light));
		// the side facing the light gets a touch of white, like a glossy edge
		const float White = std::max(0.0f, dot(Normal, Light) - 0.6f) * 0.8f;
		const ColorRGBA FaceColor(std::min(Color.r * Shade + White, 1.0f), std::min(Color.g * Shade + White, 1.0f), std::min(Color.b * Shade + White, 1.0f), Alpha);
		const vec2 P0 = Pos + vec2(aCorners[Face.m_aIndex[0]].x, aCorners[Face.m_aIndex[0]].y);
		const vec2 P1 = Pos + vec2(aCorners[Face.m_aIndex[1]].x, aCorners[Face.m_aIndex[1]].y);
		const vec2 P2 = Pos + vec2(aCorners[Face.m_aIndex[2]].x, aCorners[Face.m_aIndex[2]].y);
		const vec2 P3 = Pos + vec2(aCorners[Face.m_aIndex[3]].x, aCorners[Face.m_aIndex[3]].y);
		Graphics()->SetColor(FaceColor);
		const IGraphics::CFreeformItem Item(P0, P1, P3, P2);
		Graphics()->QuadsDrawFreeform(&Item, 1);
		// bright neon edges around the face
		const ColorRGBA EdgeColor(std::min(Color.r * 0.5f + 0.6f, 1.0f), std::min(Color.g * 0.5f + 0.6f, 1.0f), std::min(Color.b * 0.5f + 0.6f, 1.0f), Alpha * 0.9f);
		Graphics()->SetColor(EdgeColor);
		const vec2 aPoints[4] = {P0, P1, P2, P3};
		const float Half = std::max(Size * 0.035f, 0.6f);
		for(int i = 0; i < 4; i++)
		{
			const vec2 A = aPoints[i], B = aPoints[(i + 1) % 4];
			const vec2 Side = B - A;
			const float Length = length(Side);
			if(Length < 0.01f)
				continue;
			const vec2 Normal = vec2(-Side.y, Side.x) / Length * Half;
			const IGraphics::CFreeformItem Edge(A - Normal, B - Normal, A + Normal, B + Normal);
			Graphics()->QuadsDrawFreeform(&Edge, 1);
		}
	}
}

void CXcFloaters::OnRender()
{
	if(Client()->State() != IClient::STATE_ONLINE && Client()->State() != IClient::STATE_DEMOPLAYBACK)
		return;
	if(g_Config.m_XcFloaters == FLOATERS_OFF)
	{
		OnReset();
		return;
	}
	const float Dt = std::min(Client()->RenderFrameTime(), 0.1f);
	const CScreenRect Screen = Graphics()->GetScreen();
	const vec2 Size = Screen.m_BottomRight - Screen.m_TopLeft;
	const float X0 = Screen.m_TopLeft.x - Size.x * VIEW_MARGIN, X1 = Screen.m_BottomRight.x + Size.x * VIEW_MARGIN;
	const float Y0 = Screen.m_TopLeft.y - Size.y * VIEW_MARGIN, Y1 = Screen.m_BottomRight.y + Size.y * VIEW_MARGIN;
	const vec2 Camera = GameClient()->m_Camera.m_Center;
	// a teleport or a new spectator target: fill the new place at once
	const bool Jump = m_HasCamera && distance(Camera, m_LastCamera) > Size.x * 0.8f;
	m_LastCamera = Camera;
	m_HasCamera = true;

	// the amount follows the screen size, so zooming out does not thin them out
	const int Wanted = std::clamp((int)(g_Config.m_XcFloatersCount * Size.x / 1400.0f), 1, 400);
	if((int)m_vFloaters.size() != Wanted || Jump)
	{
		const size_t Old = Jump ? 0 : m_vFloaters.size();
		m_vFloaters.resize(Wanted);
		for(size_t i = Old; i < m_vFloaters.size(); i++)
			Spawn(m_vFloaters[i], X0, Y0, X1, Y1, true);
	}

	// another mix picked in the menu: those that fly now change at once (before only new ones took it)
	if(Kinds() != m_LastKinds || g_Config.m_XcFloatersRunes != m_LastRunes)
	{
		const bool First = m_LastKinds < 0;
		m_LastKinds = Kinds();
		m_LastRunes = g_Config.m_XcFloatersRunes;
		if(!First)
		{
			for(SFloater &Floater : m_vFloaters)
			{
				const vec2 Pos = Floater.m_Pos;
				Spawn(Floater, X0, Y0, X1, Y1, true);
				Floater.m_Pos = Pos; // the same place, it only fades in as the new kind
			}
		}
	}

	// notes: only while the music plays (when asked), bobbing to a steady beat
	const bool Music = GameClient()->m_XcMusic.HasTrack() && GameClient()->m_XcMusic.PausedFor() <= 0.0f;
	const bool NotesShown = !g_Config.m_XcFloatersNotesMusic || Music;
	m_NotesFade = std::clamp(m_NotesFade + (NotesShown ? Dt : -Dt) * 1.5f, 0.0f, 1.0f);
	const float Speed = g_Config.m_XcFloatersSpeed / 100.0f;
	const float SizeScale = g_Config.m_XcFloatersSize / 100.0f;
	const float Alpha = g_Config.m_XcFloatersAlpha / 100.0f;
	const int Local = GameClient()->m_Snap.m_LocalClientId;
	const bool Push = g_Config.m_XcFloatersPush && Local >= 0 && GameClient()->m_Snap.m_aCharacters[Local].m_Active;
	const vec2 TeePos = Push ? GameClient()->m_aClients[Local].m_RenderPos : vec2(0.0f, 0.0f);
	const vec2 TeeVel = Push ? CharacterVelocity(GameClient(), Local) : vec2(0.0f, 0.0f);
	constexpr float PUSH_RADIUS = 110.0f;

	for(SFloater &Floater : m_vFloaters)
	{
		if(Floater.m_Pos.x < X0 || Floater.m_Pos.x > X1 || Floater.m_Pos.y < Y0 || Floater.m_Pos.y > Y1)
			Spawn(Floater, X0, Y0, X1, Y1, false);
		Floater.m_Life = std::min(Floater.m_Life + Dt * 0.7f, 1.0f);
		if(Push)
		{
			const vec2 Away = Floater.m_Pos - TeePos;
			const float Dist = length(Away);
			if(Dist < PUSH_RADIUS && Dist > 0.01f)
			{
				// a moving tee pushes harder and spins them
				const float Force = (1.0f - Dist / PUSH_RADIUS) * (260.0f + length(TeeVel) * 0.12f);
				Floater.m_Vel += Away / Dist * Force * Dt;
				for(float &Spin : Floater.m_aSpin)
					Spin += (Spin >= 0.0f ? 1.0f : -1.0f) * Force * Dt * 0.01f;
			}
		}
		// fireflies wander in zigzags
		if(Floater.m_Kind == KIND_FIREFLY)
			Floater.m_Drift = rotate(Floater.m_Drift, std::sin(Client()->GlobalTime() * 1.3f + Floater.m_Phase) * 2.5f * Dt);
		// back to the slow drift
		Floater.m_Vel = mix(Floater.m_Vel, Floater.m_Drift, SmoothFactor(1.2f, Dt));
		for(float &Spin : Floater.m_aSpin)
			Spin = std::clamp(Spin, -4.0f, 4.0f) * (1.0f - std::min(Dt * 0.4f, 1.0f));
		Floater.m_Pos += Floater.m_Vel * Speed * Dt;
		if(Floater.m_Kind == KIND_NOTE && Music)
			Floater.m_Pos.y += std::cos(Client()->GlobalTime() * 2.0f * pi * 2.0f + Floater.m_Phase) * 18.0f * Dt; // two beats a second
		for(int i = 0; i < 3; i++)
			Floater.m_aAngle[i] += (Floater.m_aSpin[i] + (Floater.m_aSpin[i] >= 0.0f ? 0.25f : -0.25f)) * Speed * Dt;
	}

	// far ones first
	std::vector<const SFloater *> vOrder;
	vOrder.reserve(m_vFloaters.size());
	for(const SFloater &Floater : m_vFloaters)
		vOrder.push_back(&Floater);
	std::sort(vOrder.begin(), vOrder.end(), [](const SFloater *a, const SFloater *b) { return a->m_Depth < b->m_Depth; });

	const float Time = Client()->GlobalTime();
	Graphics()->TextureClear();
	// the glow under everything, added on top of the map
	CXcSprites &Sprites = GameClient()->m_XcSprites;
	const bool Soft = Sprites.Enabled();
	const FxGlow::SScale GlowScale = FxGlow::For(FxGlow::FLOATERS);
	if(g_Config.m_XcFloatersGlow && Soft && GlowScale.m_Strength > 0.0f)
	{
		// soft light style: a wide bloom texture instead of the drawn gradient
		Sprites.Begin(CXcSprites::SPRITE_SOFT_BLOOM, true);
		for(const SFloater *pFloater : vOrder)
		{
			const ColorRGBA Color = EffectColor(GameClient(), g_Config.m_XcFloatersColorMode, g_Config.m_XcFloatersColor, -1, pFloater->m_Hue);
			const float Twinkle = 0.8f + 0.2f * std::sin(Time * 2.0f + pFloater->m_Hue * 40.0f);
			const float a = Alpha * pFloater->m_Life * pFloater->m_Depth * Twinkle;
			const float Radius = pFloater->m_Size * SizeScale * pFloater->m_Depth * (pFloater->m_Kind == KIND_CUBE ? 2.6f : (pFloater->m_Kind == KIND_ORB ? 3.2f : 1.3f)) * GlowScale.m_Radius;
			Sprites.Draw(pFloater->m_Pos, Radius * 2.4f, 0.0f, Color.WithAlpha(std::min(0.3f * a * GlowScale.m_Strength, 1.0f)));
		}
		Sprites.End();
		Graphics()->TextureClear();
	}
	else if(g_Config.m_XcFloatersGlow && GameClient()->m_FxRenderer.ModuleActive())
	{
		// the render module: a real soft light around each (blurred, it also feeds the bloom)
		CFxRenderer &Fx = GameClient()->m_FxRenderer;
		for(const SFloater *pFloater : vOrder)
		{
			const ColorRGBA Color = EffectColor(GameClient(), g_Config.m_XcFloatersColorMode, g_Config.m_XcFloatersColor, -1, pFloater->m_Hue);
			const float Twinkle = 0.8f + 0.2f * std::sin(Time * 2.0f + pFloater->m_Hue * 40.0f);
			const float a = Alpha * pFloater->m_Life * pFloater->m_Depth * Twinkle;
			const float Radius = pFloater->m_Size * SizeScale * pFloater->m_Depth * (pFloater->m_Kind == KIND_CUBE ? 2.6f : (pFloater->m_Kind == KIND_ORB ? 3.2f : 1.3f));
			if(a > 0.01f)
				Fx.AddWorld(FxShape::Circle(pFloater->m_Pos, Radius * 0.3f, Color.WithAlpha(std::min(0.6f * a, 1.0f))).GlowOnly(Radius * 0.8f, 0.7f).Additive());
		}
	}
	else if(g_Config.m_XcFloatersGlow)
	{
		Graphics()->BlendAdditive();
		Graphics()->QuadsBegin();
		for(const SFloater *pFloater : vOrder)
		{
			const ColorRGBA Color = EffectColor(GameClient(), g_Config.m_XcFloatersColorMode, g_Config.m_XcFloatersColor, -1, pFloater->m_Hue);
			const float Twinkle = 0.8f + 0.2f * std::sin(Time * 2.0f + pFloater->m_Hue * 40.0f);
			const float a = Alpha * pFloater->m_Life * pFloater->m_Depth * Twinkle;
			const float Radius = pFloater->m_Size * SizeScale * pFloater->m_Depth * (pFloater->m_Kind == KIND_CUBE ? 2.6f : (pFloater->m_Kind == KIND_ORB ? 3.2f : 1.3f));
			DrawGlow(pFloater->m_Pos, Radius, Color.WithAlpha(0.32f * a));
		}
		Graphics()->QuadsEnd();
		Graphics()->BlendNormal();
	}
	Graphics()->QuadsBegin();
	for(const SFloater *pFloater : vOrder)
	{
		const ColorRGBA Color = EffectColor(GameClient(), g_Config.m_XcFloatersColorMode, g_Config.m_XcFloatersColor, -1, pFloater->m_Hue);
		const float a = Alpha * pFloater->m_Life * (0.45f + 0.55f * pFloater->m_Depth);
		const float FloaterSize = pFloater->m_Size * SizeScale * pFloater->m_Depth;
		if(pFloater->m_Kind == KIND_CUBE)
			DrawCube(*pFloater, pFloater->m_Pos, FloaterSize, Color, a);
	}
	Graphics()->QuadsEnd();
	// fireflies, notes, runes and hearts go to the effect renderer
	for(const SFloater *pFloater : vOrder)
	{
		if(pFloater->m_Kind == KIND_CUBE || pFloater->m_Kind == KIND_ORB)
			continue;
		float a = Alpha * pFloater->m_Life * (0.45f + 0.55f * pFloater->m_Depth);
		if(pFloater->m_Kind == KIND_NOTE)
			a *= m_NotesFade;
		if(a <= 0.01f)
			continue;
		const ColorRGBA Color = EffectColor(GameClient(), g_Config.m_XcFloatersColorMode, g_Config.m_XcFloatersColor, -1, pFloater->m_Hue);
		DrawShape(*pFloater, pFloater->m_Size * SizeScale * pFloater->m_Depth, Color, a, Time);
	}
	// orbs: a bright core that is almost white in the middle
	if(Soft)
	{
		for(int Pass = 0; Pass < 2; Pass++)
		{
			Sprites.Begin(Pass == 0 ? CXcSprites::SPRITE_GLOW : CXcSprites::SPRITE_CORE, true);
			for(const SFloater *pFloater : vOrder)
			{
				if(pFloater->m_Kind != KIND_ORB)
					continue;
				const ColorRGBA Color = EffectColor(GameClient(), g_Config.m_XcFloatersColorMode, g_Config.m_XcFloatersColor, -1, pFloater->m_Hue);
				const float a = Alpha * pFloater->m_Life * (0.45f + 0.55f * pFloater->m_Depth);
				const float Radius = pFloater->m_Size * SizeScale * pFloater->m_Depth * 0.55f;
				if(Pass == 0)
					Sprites.Draw(pFloater->m_Pos, Radius * 4.2f, 0.0f, Color.WithAlpha(0.85f * a));
				else
					Sprites.Draw(pFloater->m_Pos, Radius * 1.5f, 0.0f, ColorRGBA(mix(Color.r, 1.0f, 0.7f), mix(Color.g, 1.0f, 0.7f), mix(Color.b, 1.0f, 0.7f), 0.9f * a));
			}
			Sprites.End();
		}
		return;
	}
	if(GameClient()->m_FxRenderer.ModuleActive())
	{
		// the render module: a white hot core with a colored glow around
		CFxRenderer &Fx = GameClient()->m_FxRenderer;
		for(const SFloater *pFloater : vOrder)
		{
			if(pFloater->m_Kind != KIND_ORB)
				continue;
			const ColorRGBA Color = EffectColor(GameClient(), g_Config.m_XcFloatersColorMode, g_Config.m_XcFloatersColor, -1, pFloater->m_Hue);
			const float a = Alpha * pFloater->m_Life * (0.45f + 0.55f * pFloater->m_Depth);
			if(a <= 0.01f)
				continue;
			const float Radius = pFloater->m_Size * SizeScale * pFloater->m_Depth * 0.55f;
			Fx.AddWorld(FxShape::Circle(pFloater->m_Pos, Radius * 0.5f, Color.WithAlpha(0.8f * a)).GlowOnly(Radius * 1.4f, 0.9f).Additive());
			Fx.AddWorld(FxShape::Circle(pFloater->m_Pos, Radius * 0.42f, ColorRGBA(mix(Color.r, 1.0f, 0.75f), mix(Color.g, 1.0f, 0.75f), mix(Color.b, 1.0f, 0.75f), 0.9f * a)).Additive());
		}
		return;
	}
	Graphics()->BlendAdditive();
	Graphics()->QuadsBegin();
	for(const SFloater *pFloater : vOrder)
	{
		if(pFloater->m_Kind != KIND_ORB)
			continue;
		const ColorRGBA Color = EffectColor(GameClient(), g_Config.m_XcFloatersColorMode, g_Config.m_XcFloatersColor, -1, pFloater->m_Hue);
		const float a = Alpha * pFloater->m_Life * (0.45f + 0.55f * pFloater->m_Depth);
		const float Radius = pFloater->m_Size * SizeScale * pFloater->m_Depth * 0.55f;
		DrawGlow(pFloater->m_Pos, Radius * 1.6f, Color.WithAlpha(0.7f * a));
		DrawGlow(pFloater->m_Pos, Radius * 0.6f, ColorRGBA(1.0f, 1.0f, 1.0f, 0.8f * a));
	}
	Graphics()->QuadsEnd();
	Graphics()->BlendNormal();
}

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
	const int Style = g_Config.m_XcFloaters;
	Floater.m_Cube = Style == FLOATERS_CUBES || (Style == FLOATERS_MIXED && rand() % 2 == 0);
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
		// back to the slow drift
		Floater.m_Vel = mix(Floater.m_Vel, Floater.m_Drift, SmoothFactor(1.2f, Dt));
		for(float &Spin : Floater.m_aSpin)
			Spin = std::clamp(Spin, -4.0f, 4.0f) * (1.0f - std::min(Dt * 0.4f, 1.0f));
		Floater.m_Pos += Floater.m_Vel * Speed * Dt;
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
			const float Radius = pFloater->m_Size * SizeScale * pFloater->m_Depth * (pFloater->m_Cube ? 2.6f : 3.2f) * GlowScale.m_Radius;
			Sprites.Draw(pFloater->m_Pos, Radius * 2.4f, 0.0f, Color.WithAlpha(std::min(0.3f * a * GlowScale.m_Strength, 1.0f)));
		}
		Sprites.End();
		Graphics()->TextureClear();
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
			const float Radius = pFloater->m_Size * SizeScale * pFloater->m_Depth * (pFloater->m_Cube ? 2.6f : 3.2f);
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
		if(pFloater->m_Cube)
			DrawCube(*pFloater, pFloater->m_Pos, FloaterSize, Color, a);
	}
	Graphics()->QuadsEnd();
	// orbs: a bright core that is almost white in the middle
	if(Soft)
	{
		for(int Pass = 0; Pass < 2; Pass++)
		{
			Sprites.Begin(Pass == 0 ? CXcSprites::SPRITE_GLOW : CXcSprites::SPRITE_CORE, true);
			for(const SFloater *pFloater : vOrder)
			{
				if(pFloater->m_Cube)
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
	Graphics()->BlendAdditive();
	Graphics()->QuadsBegin();
	for(const SFloater *pFloater : vOrder)
	{
		if(pFloater->m_Cube)
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

#include "trail_fx.h"

#include "fx_renderer.h"
#include "sprite_fx.h"
#include "xc_helpers.h"

#include <base/math.h>

#include <engine/client.h>
#include <engine/shared/config.h>

#include <game/client/gameclient.h>

#include <algorithm>
#include <cmath>
#include <vector>

using namespace XcHelpers;

// a tee jumps further than this between two frames only when it teleports
static constexpr float TELEPORT_DISTANCE = 300.0f;
// new points closer than this to the last one only move it, keeps the ribbon light
static constexpr float MIN_POINT_DISTANCE = 3.0f;
static constexpr float BASE_WIDTH = 26.0f;

static vec2 CatmullRom(vec2 P0, vec2 P1, vec2 P2, vec2 P3, float t)
{
	const float t2 = t * t;
	const float t3 = t2 * t;
	return ((P1 * 2.0f) + (P2 - P0) * t + (P0 * 2.0f - P1 * 5.0f + P2 * 4.0f - P3) * t2 + (P1 * 3.0f - P0 - P2 * 3.0f + P3) * t3) * 0.5f;
}

// Draws the strip in bands across its width, each band fades between two alphas, so the edges
// are soft without any texture: Profile is pairs of (offset from the middle -1..1, alpha factor).
void CTrailFx::DrawBands(const std::vector<SRibbonPoint> &vPoints, const std::vector<vec2> &vNormals, float WidthScale, const float *pProfile, int NumProfile, float AlphaScale, bool Core)
{
	IGraphics *pGraphics = Graphics();
	pGraphics->QuadsBegin();
	for(size_t i = 0; i + 1 < vPoints.size(); i++)
	{
		const SRibbonPoint &A = vPoints[i];
		const SRibbonPoint &B = vPoints[i + 1];
		ColorRGBA ColorA = A.m_Color;
		ColorRGBA ColorB = B.m_Color;
		if(Core)
		{
			ColorA = ColorRGBA(mix(ColorA.r, 1.0f, 0.75f), mix(ColorA.g, 1.0f, 0.75f), mix(ColorA.b, 1.0f, 0.75f), ColorA.a);
			ColorB = ColorRGBA(mix(ColorB.r, 1.0f, 0.75f), mix(ColorB.g, 1.0f, 0.75f), mix(ColorB.b, 1.0f, 0.75f), ColorB.a);
		}
		for(int b = 0; b + 1 < NumProfile; b++)
		{
			const float O0 = pProfile[b * 2], A0 = pProfile[b * 2 + 1];
			const float O1 = pProfile[b * 2 + 2], A1 = pProfile[b * 2 + 3];
			const vec2 Pa0 = A.m_Pos + vNormals[i] * (O0 * A.m_HalfWidth * WidthScale);
			const vec2 Pb0 = B.m_Pos + vNormals[i + 1] * (O0 * B.m_HalfWidth * WidthScale);
			const vec2 Pa1 = A.m_Pos + vNormals[i] * (O1 * A.m_HalfWidth * WidthScale);
			const vec2 Pb1 = B.m_Pos + vNormals[i + 1] * (O1 * B.m_HalfWidth * WidthScale);
			pGraphics->SetColor4(ColorA.WithMultipliedAlpha(A0 * AlphaScale), ColorB.WithMultipliedAlpha(A0 * AlphaScale), ColorA.WithMultipliedAlpha(A1 * AlphaScale), ColorB.WithMultipliedAlpha(A1 * AlphaScale));
			const IGraphics::CFreeformItem Item(Pa0, Pb0, Pa1, Pb1);
			pGraphics->QuadsDrawFreeform(&Item, 1);
		}
	}
	pGraphics->QuadsEnd();
}

void CTrailFx::DrawRibbon(const std::vector<SRibbonPoint> &vPoints, float Glow, float BodyAlpha)
{
	// normals from the neighbours, so the strip bends smoothly
	std::vector<vec2> vNormals(vPoints.size());
	for(size_t i = 0; i < vPoints.size(); i++)
	{
		const vec2 Prev = vPoints[i > 0 ? i - 1 : i].m_Pos;
		const vec2 Next = vPoints[i + 1 < vPoints.size() ? i + 1 : i].m_Pos;
		const vec2 Dir = Next - Prev;
		const float Length = length(Dir);
		vNormals[i] = Length > 0.001f ? vec2(-Dir.y, Dir.x) / Length : (i > 0 ? vNormals[i - 1] : vec2(0.0f, 1.0f));
	}
	Graphics()->TextureClear();
	if(Glow > 0.0f)
	{
		// wide soft light, added on top of the map
		static const float s_aGlow[] = {-1.0f, 0.0f, 0.0f, 1.0f, 1.0f, 0.0f};
		Graphics()->BlendAdditive();
		DrawBands(vPoints, vNormals, 2.4f, s_aGlow, 3, 0.45f * Glow, false);
		Graphics()->BlendNormal();
	}
	// the body with soft edges and a light core in the middle
	static const float s_aBody[] = {-1.0f, 0.0f, -0.65f, 1.0f, 0.65f, 1.0f, 1.0f, 0.0f};
	DrawBands(vPoints, vNormals, 1.0f, s_aBody, 4, BodyAlpha, false);
	static const float s_aCore[] = {-1.0f, 0.0f, 0.0f, 1.0f, 1.0f, 0.0f};
	DrawBands(vPoints, vNormals, 0.4f, s_aCore, 3, 0.95f, true);
}

void CTrailFx::DrawLightRibbon(const std::vector<SRibbonPoint> &vPoints, float Glow)
{
	// light sprites at even steps along the strip, three passes from the widest to the core
	struct SSample
	{
		vec2 m_Pos;
		float m_Width;
		ColorRGBA m_Color;
	};
	std::vector<SSample> vSamples;
	float Carry = 0.0f;
	for(size_t i = 0; i + 1 < vPoints.size(); i++)
	{
		const SRibbonPoint &A = vPoints[i];
		const SRibbonPoint &B = vPoints[i + 1];
		const float Segment = distance(A.m_Pos, B.m_Pos);
		const float Step = std::max(A.m_HalfWidth * 0.45f, 1.5f);
		float d = Carry;
		for(; d < Segment; d += Step)
		{
			const float u = d / std::max(Segment, 0.001f);
			const ColorRGBA Color(mix(A.m_Color.r, B.m_Color.r, u), mix(A.m_Color.g, B.m_Color.g, u), mix(A.m_Color.b, B.m_Color.b, u), mix(A.m_Color.a, B.m_Color.a, u));
			vSamples.push_back({mix(A.m_Pos, B.m_Pos, u), 2.0f * mix(A.m_HalfWidth, B.m_HalfWidth, u), Color});
		}
		Carry = d - Segment;
	}
	if(vSamples.empty())
		return;
	CXcSprites &Sprites = GameClient()->m_XcSprites;
	const float Reach = FxGlow::g_Current.m_Radius;
	const float Bright = FxGlow::g_Current.m_Strength * Glow;
	if(Bright > 0.0f)
	{
		Sprites.Begin(CXcSprites::SPRITE_BLOOM, true);
		for(const SSample &Sample : vSamples)
			Sprites.Draw(Sample.m_Pos, Sample.m_Width * 3.2f * Reach, 0.0f, Sample.m_Color.WithMultipliedAlpha(0.07f * Bright));
		Sprites.End();
	}
	// soft light over the smooth body: a glow and a hot whitish center made of the same soft texture
	Sprites.Begin(CXcSprites::SPRITE_GLOW, true);
	for(const SSample &Sample : vSamples)
		Sprites.Draw(Sample.m_Pos, Sample.m_Width * 1.6f * Reach, 0.0f, Sample.m_Color.WithMultipliedAlpha(0.14f * std::min(Bright, 1.5f)));
	for(const SSample &Sample : vSamples)
	{
		const ColorRGBA &c = Sample.m_Color;
		Sprites.Draw(Sample.m_Pos, Sample.m_Width * 0.55f, 0.0f, ColorRGBA(mix(c.r, 1.0f, 0.6f), mix(c.g, 1.0f, 0.6f), mix(c.b, 1.0f, 0.6f), c.a * 0.22f));
	}
	Sprites.End();
}

void CTrailFx::RenderDashes(float Length)
{
	CXcSprites &Sprites = GameClient()->m_XcSprites;
	m_vDashes.erase(std::remove_if(m_vDashes.begin(), m_vDashes.end(), [&](const SDash &Dash) { return m_Time - Dash.m_Born >= Length; }), m_vDashes.end());
	if(m_vDashes.empty())
		return;
	const int Sprite = CXcSprites::SPRITE_DASH_STRETCH + std::clamp(g_Config.m_XcTrailDash, 0, 4);
	const float Aspect = CXcSprites::Aspect(Sprite);
	const float Bright = FxGlow::g_Current.m_Strength * g_Config.m_XcTrailGlow / 100.0f;
	// a soft light under every dash, then the dash itself playing its frames
	if(Bright > 0.0f)
	{
		Sprites.Begin(CXcSprites::SPRITE_DASH_BLOOM, true);
		for(const SDash &Dash : m_vDashes)
		{
			const float t = (m_Time - Dash.m_Born) / Length;
			Sprites.Draw(Dash.m_Pos, 40.0f * Dash.m_Scale * FxGlow::g_Current.m_Radius, 0.0f, Dash.m_Color.WithMultipliedAlpha(0.35f * Bright * (1.0f - t)));
		}
		Sprites.End();
	}
	// the dash in its color, then the same frame added on top as light
	for(int Pass = 0; Pass < 2; Pass++)
	{
		Sprites.Begin(Sprite, Pass == 1);
		for(const SDash &Dash : m_vDashes)
		{
			const float t = (m_Time - Dash.m_Born) / Length;
			const ColorRGBA &c = Dash.m_Color;
			const ColorRGBA Color = Pass == 0 ? c : ColorRGBA(mix(c.r, 1.0f, 0.5f), mix(c.g, 1.0f, 0.5f), mix(c.b, 1.0f, 0.5f), c.a * 0.45f * std::min(Bright, 1.5f));
			Sprites.Draw(Dash.m_Pos, 16.0f * Aspect * Dash.m_Scale, Dash.m_Angle, Color, t);
		}
		Sprites.End();
	}
}

void CTrailFx::OnReset()
{
	for(STrail &Trail : m_aTrails)
		Trail = STrail();
	m_Particles.Clear();
	m_vDashes.clear();
}

void CTrailFx::OnMapLoad()
{
	OnReset();
}

void CTrailFx::Update(int ClientId, vec2 Pos, float Length)
{
	STrail &Trail = m_aTrails[ClientId];
	if(!Trail.m_vPoints.empty() && distance(Trail.m_vPoints.front().m_Pos, Pos) > TELEPORT_DISTANCE)
		Trail.m_vPoints.clear();
	if(!Trail.m_vPoints.empty() && distance(Trail.m_vPoints.front().m_Pos, Pos) < MIN_POINT_DISTANCE && Trail.m_vPoints.size() > 1)
	{
		// standing still: the head follows the tee, the old part fades away by itself
		Trail.m_vPoints.front().m_Pos = Pos;
	}
	else
		Trail.m_vPoints.push_front({Pos, m_Time});
	while(!Trail.m_vPoints.empty() && m_Time - Trail.m_vPoints.back().m_Time > Length)
		Trail.m_vPoints.pop_back();
}

void CTrailFx::Emit(int ClientId, vec2 Pos, vec2 Dir, float Scale)
{
	const int Style = g_Config.m_XcTrail;
	CXcParticles::SParticle Particle;
	Particle.m_Type = Style == TRAIL_STARS ? CXcParticles::EType::STAR : CXcParticles::EType::SPARK;
	const float Angle = random_float(0.0f, 2.0f * pi);
	Particle.m_Pos = Pos + vec2(std::cos(Angle), std::sin(Angle)) * random_float(0.0f, 8.0f) * Scale;
	// drift slightly backwards and sideways, sparks sink, stars float
	Particle.m_Vel = -Dir * random_float(10.0f, 40.0f) + vec2(random_float(-25.0f, 25.0f), random_float(-25.0f, 25.0f));
	Particle.m_Gravity = Style == TRAIL_STARS ? -20.0f : 120.0f;
	Particle.m_Friction = 0.3f;
	Particle.m_LifeSpan = random_float(0.35f, 0.7f) * g_Config.m_XcTrailLength / 500.0f;
	Particle.m_StartSize = (Style == TRAIL_STARS ? random_float(9.0f, 14.0f) : random_float(5.5f, 9.0f)) * Scale;
	Particle.m_EndSize = 0.0f;
	Particle.m_Rot = Angle;
	Particle.m_RotSpeed = Style == TRAIL_STARS ? random_float(-4.0f, 4.0f) : 0.0f;
	Particle.m_Color = EffectColor(GameClient(), g_Config.m_XcTrailColorMode, g_Config.m_XcTrailColor, ClientId, random_float(0.0f, 0.15f));
	m_Particles.Add(Particle);
}

void CTrailFx::OnRender()
{
	const FxGlow::CScope GlowScope(FxGlow::TRAIL);
	if(Client()->State() != IClient::STATE_ONLINE && Client()->State() != IClient::STATE_DEMOPLAYBACK)
		return;
	const float Dt = std::min(Client()->RenderFrameTime(), 0.1f);
	m_Time += Dt;
	const int Style = g_Config.m_XcTrail;
	const float Length = g_Config.m_XcTrailLength / 1000.0f;
	const float Scale = g_Config.m_XcTrailWidth / 100.0f;
	const float Glow = g_Config.m_XcTrailGlow / 100.0f;

	std::vector<SFxShape> vShapes;
	for(int ClientId = 0; ClientId < MAX_CLIENTS; ClientId++)
	{
		STrail &Trail = m_aTrails[ClientId];
		if(Style == TRAIL_OFF || !GameClient()->m_Snap.m_aCharacters[ClientId].m_Active ||
			!ShowFor(GameClient(), ClientId, g_Config.m_XcTrailSelf, g_Config.m_XcTrailDummy, g_Config.m_XcTrailOthers))
		{
			Trail.m_vPoints.clear();
			continue;
		}
		const float PlayerFade = PlayerAlpha(GameClient(), ClientId);
		if(PlayerFade <= 0.0f)
			continue;
		const vec2 Pos = GameClient()->m_aClients[ClientId].m_RenderPos;
		const vec2 OldHead = Trail.m_vPoints.empty() ? Pos : Trail.m_vPoints.front().m_Pos;
		Update(ClientId, Pos, Length);
		const size_t Count = Trail.m_vPoints.size();
		if(Count < 2)
			continue;

		if(Style == TRAIL_SPARKS || Style == TRAIL_STARS)
		{
			// particles every few units walked, so the amount follows the speed
			const float Walked = distance(OldHead, Pos);
			Trail.m_EmitDistance += Walked;
			const float Spacing = (Style == TRAIL_STARS ? 14.0f : 7.0f) * Scale;
			const vec2 Dir = Walked > 0.001f ? (Pos - OldHead) / Walked : vec2(0.0f, 0.0f);
			int Budget = 6;
			while(Trail.m_EmitDistance > Spacing && Budget-- > 0)
			{
				Trail.m_EmitDistance -= Spacing;
				Emit(ClientId, Pos - Dir * Trail.m_EmitDistance, Dir, Scale);
			}
			Trail.m_EmitDistance = std::min(Trail.m_EmitDistance, Spacing);
		}

		if(Style == TRAIL_DASHES && GameClient()->m_XcSprites.Enabled())
		{
			// a dash every few units walked, lying along the way
			const float Walked = distance(OldHead, Pos);
			Trail.m_EmitDistance += Walked;
			const float Spacing = 17.0f * Scale;
			const vec2 Dir = Walked > 0.001f ? (Pos - OldHead) / Walked : vec2(1.0f, 0.0f);
			int Budget = 8;
			while(Trail.m_EmitDistance > Spacing && Budget-- > 0)
			{
				Trail.m_EmitDistance -= Spacing;
				const ColorRGBA Color = EffectColor(GameClient(), g_Config.m_XcTrailColorMode, g_Config.m_XcTrailColor, ClientId, random_float(0.0f, 0.2f), PlayerFade);
				m_vDashes.push_back({Pos - Dir * Trail.m_EmitDistance, angle(Dir), m_Time, Color, Scale});
			}
			Trail.m_EmitDistance = std::min(Trail.m_EmitDistance, Spacing);
			continue;
		}

		// age 0 at the tee, 1 at the end of the trail
		const auto Age = [&](size_t i) { return std::clamp((m_Time - Trail.m_vPoints[i].m_Time) / Length, 0.0f, 1.0f); };
		if(Style == TRAIL_DOTS || Style == TRAIL_DASHES)
		{
			// comet: round dots at even distances, shrinking towards the end
			float Carry = 0.0f;
			const float Spacing = 12.0f * Scale;
			for(size_t i = 0; i + 1 < Count; i++)
			{
				const vec2 A = Trail.m_vPoints[i].m_Pos;
				const vec2 B = Trail.m_vPoints[i + 1].m_Pos;
				const float Segment = distance(A, B);
				float d = Spacing - Carry;
				for(; d <= Segment; d += Spacing)
				{
					const float t = mix(Age(i), Age(i + 1), d / std::max(Segment, 0.001f));
					const float Radius = BASE_WIDTH * 0.32f * Scale * (1.0f - t);
					const ColorRGBA Color = EffectColor(GameClient(), g_Config.m_XcTrailColorMode, g_Config.m_XcTrailColor, ClientId, t * 0.35f, PlayerFade * (1.0f - t * t));
					if(Radius > 0.4f)
						CFxRenderer::AppendGlowing(vShapes, FxShape::Circle(mix(A, B, d / std::max(Segment, 0.001f)), Radius, Color), Radius * 1.6f * Glow, 0.55f * Glow);
				}
				Carry = Segment - (d - Spacing);
			}
			continue;
		}

		// ribbon: one smooth strip along a Catmull-Rom curve through the points
		const float Width = BASE_WIDTH * Scale * (Style == TRAIL_RIBBON ? 1.0f : 0.35f);
		std::vector<SRibbonPoint> vRibbon;
		vRibbon.reserve(Count * 4);
		for(size_t i = 0; i + 1 < Count; i++)
		{
			const vec2 P0 = Trail.m_vPoints[i > 0 ? i - 1 : i].m_Pos;
			const vec2 P1 = Trail.m_vPoints[i].m_Pos;
			const vec2 P2 = Trail.m_vPoints[i + 1].m_Pos;
			const vec2 P3 = Trail.m_vPoints[i + 2 < Count ? i + 2 : i + 1].m_Pos;
			const int Steps = std::clamp((int)(distance(P1, P2) / 6.0f), 1, 12);
			for(int Step = 0; Step < Steps; Step++)
			{
				const float u = (float)Step / Steps;
				const float t = mix(Age(i), Age(i + 1), u);
				const float Fade = std::pow(1.0f - t, 1.6f) * PlayerFade;
				SRibbonPoint Point;
				Point.m_Pos = CatmullRom(P0, P1, P2, P3, u);
				Point.m_HalfWidth = Width * 0.5f * std::pow(1.0f - t, 0.8f);
				Point.m_Color = EffectColor(GameClient(), g_Config.m_XcTrailColorMode, g_Config.m_XcTrailColor, ClientId, t * 0.35f, Fade);
				vRibbon.push_back(Point);
			}
		}
		if(vRibbon.size() >= 2)
		{
			if(GameClient()->m_XcSprites.Enabled())
			{
				// the smooth body of the classic ribbon with soft textured light over it
				DrawRibbon(vRibbon, 0.0f, Style == TRAIL_RIBBON ? 0.85f : 0.6f);
				DrawLightRibbon(vRibbon, Glow);
			}
			else
				DrawRibbon(vRibbon, Glow, Style == TRAIL_RIBBON ? 0.95f : 0.7f);
		}
	}
	GameClient()->m_FxRenderer.Draw(vShapes, true);
	if(GameClient()->m_XcSprites.Enabled())
		RenderDashes(Length);
	else
		m_vDashes.clear();

	m_Particles.Update(Dt);
	m_Particles.Render(GameClient()->m_FxRenderer);
}

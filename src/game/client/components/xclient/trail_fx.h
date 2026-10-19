#ifndef GAME_CLIENT_COMPONENTS_XCLIENT_TRAIL_FX_H
#define GAME_CLIENT_COMPONENTS_XCLIENT_TRAIL_FX_H

#include "xc_particles.h"

#include <base/color.h>
#include <base/vmath.h>

#include <engine/shared/protocol.h>

#include <game/client/component.h>

#include <deque>
#include <vector>

// Trails behind the tees: a tapered glowing ribbon, sparks, stars or comet dots.
// Drawn before the players so the trail stays behind the tee.
class CTrailFx : public CComponent
{
public:
	enum
	{
		TRAIL_OFF = 0,
		TRAIL_RIBBON,
		TRAIL_SPARKS,
		TRAIL_STARS,
		TRAIL_DOTS,
		TRAIL_DASHES, // animated dash sprites of the new effect style
	};

	int Sizeof() const override { return sizeof(*this); }
	void OnRender() override;
	void OnReset() override;
	void OnMapLoad() override;

private:
	struct SPoint
	{
		vec2 m_Pos;
		float m_Time;
	};
	struct STrail
	{
		std::deque<SPoint> m_vPoints;
		float m_EmitDistance = 0.0f; // distance walked since the last particle
	};
	STrail m_aTrails[MAX_CLIENTS];
	// one dash of the dashes trail, plays its animation once
	struct SDash
	{
		vec2 m_Pos;
		float m_Angle;
		float m_Born;
		ColorRGBA m_Color;
		float m_Scale;
	};
	std::vector<SDash> m_vDashes;
	CXcParticles m_Particles;
	float m_Time = 0.0f;

	struct SRibbonPoint
	{
		vec2 m_Pos;
		float m_HalfWidth;
		ColorRGBA m_Color;
	};
	void DrawRibbon(const std::vector<SRibbonPoint> &vPoints, float Glow, float BodyAlpha);
	// the new style: soft layers of light along the ribbon
	void DrawLightRibbon(const std::vector<SRibbonPoint> &vPoints, float Glow);
	void RenderDashes(float Length);
	void DrawBands(const std::vector<SRibbonPoint> &vPoints, const std::vector<vec2> &vNormals, float WidthScale, const float *pProfile, int NumProfile, float AlphaScale, bool Core);
	void Update(int ClientId, vec2 Pos, float Length);
	void Emit(int ClientId, vec2 Pos, vec2 Dir, float Scale);
};

#endif

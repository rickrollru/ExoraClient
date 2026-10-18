#ifndef GAME_CLIENT_COMPONENTS_XCLIENT_WORLD_FX_H
#define GAME_CLIENT_COMPONENTS_XCLIENT_WORLD_FX_H

#include "xc_particles.h"

#include <base/vmath.h>

#include <engine/shared/protocol.h>

#include <game/client/component.h>

// Effects in the world: ice aura and shards for frozen tees, hammer hits, and the custom crosshair.
class CWorldFx : public CComponent
{
public:
	enum
	{
		HAMMER_OFF = 0,
		HAMMER_RING,
		HAMMER_SPARKS,
		HAMMER_STARS,
	};
	enum
	{
		CROSSHAIR_DEFAULT = 0,
		CROSSHAIR_DOT,
		CROSSHAIR_CROSS,
		CROSSHAIR_CIRCLE,
		CROSSHAIR_CIRCLE_DOT,
	};

	int Sizeof() const override { return sizeof(*this); }
	void OnRender() override;
	void OnReset() override;
	void OnMapLoad() override;

	// called by CEffects::HammerHit
	void HammerHit(vec2 Pos);
	// called by CHud::RenderCursor, true when the own crosshair was drawn instead of the weapon cursor
	bool RenderCrosshair(vec2 Pos, float Alpha, float Scale);

private:
	struct SFreezeState
	{
		bool m_Frozen = false;
		float m_SnowTimer = 0.0f;
	};
	SFreezeState m_aFreeze[MAX_CLIENTS];
	CXcParticles m_Particles;

	void FreezeBurst(vec2 Pos, int ClientId, bool Unfreeze);
	ColorRGBA FreezeColor(int ClientId, float Alpha = 1.0f) const;
};

// Over the whole world, under the HUD: color filter and vignette.
class CScreenFx : public CComponent
{
public:
	enum
	{
		TINT_OFF = 0,
		TINT_WARM,
		TINT_COOL,
		TINT_ACCENT,
		TINT_NIGHT,
	};

	int Sizeof() const override { return sizeof(*this); }
	void OnRender() override;
};

#endif

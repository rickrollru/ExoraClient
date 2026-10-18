#ifndef GAME_CLIENT_COMPONENTS_XCLIENT_JUMP_FX_H
#define GAME_CLIENT_COMPONENTS_XCLIENT_JUMP_FX_H

#include "xc_particles.h"

#include <base/color.h>
#include <base/vmath.h>

#include <engine/shared/protocol.h>

#include <game/client/component.h>

// Visual effects for jumps, double jumps, landings and running.
// Only reads the rendered character state, never touches input or physics.
class CJumpFx : public CComponent
{
public:
	enum
	{
		JUMP_FX_OFF = 0,
		JUMP_FX_RINGS,
		JUMP_FX_SPARKS,
		JUMP_FX_STARS,
	};

	int Sizeof() const override { return sizeof(*this); }
	void OnRender() override;
	void OnReset() override;
	void OnMapLoad() override;

private:
	struct SPlayerState
	{
		bool m_Valid = false;
		bool m_InAir = false;
		int m_Jumped = 0;
		float m_MaxFallSpeed = 0.0f;
		float m_DustTimer = 0.0f;
		vec2 m_Pos = vec2(0.0f, 0.0f);
	};

	SPlayerState m_aPlayers[MAX_CLIENTS];
	CXcParticles m_Particles;

	void UpdatePlayers(float Dt);
	ColorRGBA FxColor() const;
	int m_ColorClient = -1;
	void EmitJump(vec2 Feet, bool AirJump);
	void EmitLanding(vec2 Feet, float FallSpeed);
	void EmitDust(vec2 Feet, float Direction);
};

#endif

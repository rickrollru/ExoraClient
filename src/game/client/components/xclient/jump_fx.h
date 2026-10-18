#ifndef GAME_CLIENT_COMPONENTS_XCLIENT_JUMP_FX_H
#define GAME_CLIENT_COMPONENTS_XCLIENT_JUMP_FX_H

#include <base/color.h>
#include <base/vmath.h>

#include <engine/shared/protocol.h>

#include <game/client/component.h>

#include <vector>

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
	enum
	{
		FX_RING = 0,
		FX_SPARK,
		FX_STAR,
		FX_DUST,
	};

	struct SFx
	{
		int m_Type;
		vec2 m_Pos;
		vec2 m_Vel;
		float m_Gravity;
		float m_Life;
		float m_LifeSpan;
		float m_StartSize;
		float m_EndSize;
		float m_Rot;
		float m_RotSpeed;
		ColorRGBA m_Color;
	};

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
	std::vector<SFx> m_vFx;

	void UpdatePlayers(float Dt);
	void EmitJump(vec2 Feet, bool AirJump);
	void EmitLanding(vec2 Feet, float FallSpeed);
	void EmitDust(vec2 Feet, float Direction);
	void Add(const SFx &Fx);
	void Simulate(float Dt);
	void Draw() const;
	void DrawRing(const SFx &Fx) const;
};

#endif

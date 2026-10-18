#ifndef GAME_CLIENT_COMPONENTS_XCLIENT_HOOK_FX_H
#define GAME_CLIENT_COMPONENTS_XCLIENT_HOOK_FX_H

#include "xc_particles.h"

#include <base/color.h>
#include <base/vmath.h>

#include <engine/shared/protocol.h>

#include <game/client/component.h>

// Custom hook look (colored chain, solid or glowing line) and an effect when the hook grabs.
// CPlayers asks this component how to draw the hook, the grab effect is drawn here.
class CHookFx : public CComponent
{
public:
	enum
	{
		STYLE_DEFAULT = 0,
		STYLE_CHAIN,
		STYLE_LINE,
		STYLE_GLOW,
	};

	enum
	{
		HIT_OFF = 0,
		HIT_RING,
		HIT_SPARKS,
		HIT_STARS,
	};

	int Sizeof() const override { return sizeof(*this); }
	void OnRender() override;
	void OnReset() override;
	void OnMapLoad() override;

	// Hook style for this player, STYLE_DEFAULT when our settings do not apply to them.
	int Style(int ClientId) const;
	ColorRGBA Color(float Alpha) const;
	// Lighter tint for the hook sprites, they are dark by themselves.
	ColorRGBA SpriteColor(float Alpha) const;
	// Draws the line from the player to the hook head, or the aura under the chain for STYLE_CHAIN.
	void RenderLine(vec2 PlayerPos, vec2 HookPos, float Alpha) const;

private:
	struct SState
	{
		bool m_Valid = false;
		int m_HookState = 0;
		float m_Cooldown = 0.0f;
	};
	SState m_aStates[MAX_CLIENTS];
	CXcParticles m_Particles;

	void EmitHit(vec2 Pos, vec2 AwayDir);
};

#endif

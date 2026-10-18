#ifndef GAME_CLIENT_COMPONENTS_XCLIENT_EVENT_FX_H
#define GAME_CLIENT_COMPONENTS_XCLIENT_EVENT_FX_H

#include "xc_particles.h"

#include <base/vmath.h>

#include <engine/client/enums.h>
#include <engine/shared/protocol.h>

#include <game/client/component.h>

// Effects on game events: confetti or fireworks at the finish (and a banner for a new record), shards
// when a tee dies, a ring where one spawns, a flash on teleports, and an arrow to the dummy off screen.
class CXcEventFx : public CComponent
{
public:
	enum
	{
		FINISH_OFF = 0,
		FINISH_CONFETTI,
		FINISH_FIREWORKS,
	};
	enum
	{
		DEATH_OFF = 0,
		DEATH_SHARDS,
		DEATH_SPARKS,
		DEATH_ASH,
	};

	int Sizeof() const override { return sizeof(*this); }
	void OnRender() override;
	void OnReset() override;
	void OnMapLoad() override;
	void OnMessage(int MsgType, void *pRawMsg) override;

	// called by CEffects
	void PlayerDeath(vec2 Pos, int ClientId);
	void PlayerSpawn(vec2 Pos);

	// for the preview buttons in the menu
	void Finish(vec2 Pos, int ClientId);
	// called by CScreenFx, after the map foreground
	void RenderDummyArrow();

private:
	CXcParticles m_Particles;
	vec2 m_aLastPos[NUM_DUMMIES] = {};
	bool m_aLastValid[NUM_DUMMIES] = {};
	float m_aSinceDeath[NUM_DUMMIES] = {};
	vec2 m_DummyLastPos = vec2(0.0f, 0.0f);

	void Confetti(vec2 Pos, int ClientId);
	void Fireworks(vec2 Pos, int ClientId);
	void Burst(vec2 Pos, ColorRGBA Color, int Count, float Speed, float Delay);
	void TeleportFlash(vec2 From, vec2 To, int ClientId);
};

#endif

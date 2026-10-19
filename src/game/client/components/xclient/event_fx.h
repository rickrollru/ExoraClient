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

	enum
	{
		EVENT_SOUND_FINISH = 0,
		EVENT_SOUND_RECORD,
		EVENT_SOUND_FREEZE,
		EVENT_SOUND_UNFREEZE,
		EVENT_SOUND_DEATH,
		EVENT_SOUND_TELEPORT,
		NUM_EVENT_SOUNDS,
	};

	int Sizeof() const override { return sizeof(*this); }
	void OnInit() override;
	void OnShutdown() override;
	void OnRender() override;
	void OnReset() override;
	void OnMapLoad() override;
	void OnMessage(int MsgType, void *pRawMsg) override;

	// called by CEffects
	void PlayerDeath(vec2 Pos, int ClientId);
	void PlayerSpawn(vec2 Pos);

	// for the preview buttons in the menu
	void Finish(vec2 Pos, int ClientId);
	void PlayEventSound(int Sound);
	// called by CScreenFx, after the map foreground
	void RenderDummyArrow();

private:
	CXcParticles m_Particles;
	vec2 m_aLastPos[NUM_DUMMIES] = {};
	bool m_aLastValid[NUM_DUMMIES] = {};
	float m_aSinceDeath[NUM_DUMMIES] = {};
	vec2 m_DummyLastPos = vec2(0.0f, 0.0f);
	vec2 m_aLastSeenPos[MAX_CLIENTS] = {};
	float m_aLastSeenTime[MAX_CLIENTS] = {}; // 0 = never seen
	bool m_aWasFrozen[NUM_DUMMIES] = {};
	int m_aSounds[NUM_EVENT_SOUNDS] = {-1, -1, -1, -1, -1, -1};
	bool IsOwn(int ClientId) const;

	void Confetti(vec2 Pos, int ClientId);
	void Fireworks(vec2 Pos, int ClientId);
	void Burst(vec2 Pos, ColorRGBA Color, int Count, float Speed, float Delay);
	void TeleportFlash(vec2 From, vec2 To, int ClientId);
};

#endif

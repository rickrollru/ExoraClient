#include "event_fx.h"

#include "fx_renderer.h"
#include "sprite_fx.h"
#include "xc_helpers.h"

#include <base/math.h>

#include <engine/client.h>
#include <engine/graphics.h>
#include <engine/shared/config.h>
#include <engine/sound.h>

#include <generated/protocol.h>

#include <game/client/components/sounds.h>
#include <game/client/components/xclient/ui/lang.h>
#include <game/client/gameclient.h>
#include <game/collision.h>
#include <game/mapitems.h>

#include <algorithm>
#include <cmath>

using namespace XcHelpers;

void CXcEventFx::OnInit()
{
	static const char *const s_apNames[NUM_EVENT_SOUNDS] = {"finish", "record", "freeze", "unfreeze", "death", "teleport"};
	for(int i = 0; i < NUM_EVENT_SOUNDS; i++)
	{
		char aPath[128];
		str_format(aPath, sizeof(aPath), "xclient/sounds/%s.opus", s_apNames[i]);
		m_aSounds[i] = Sound()->IsSoundEnabled() ? Sound()->LoadOpus(aPath) : -1;
	}
}

void CXcEventFx::OnShutdown()
{
	for(int &Sample : m_aSounds)
	{
		if(Sample >= 0)
			Sound()->UnloadSample(Sample);
		Sample = -1;
	}
}

void CXcEventFx::PlayEventSound(int Sound)
{
	if(!g_Config.m_XcEventSounds || Sound < 0 || Sound >= NUM_EVENT_SOUNDS || m_aSounds[Sound] < 0)
		return;
	const int *apEnabled[NUM_EVENT_SOUNDS] = {&g_Config.m_XcEventSoundFinish, &g_Config.m_XcEventSoundFinish, &g_Config.m_XcEventSoundFreeze,
		&g_Config.m_XcEventSoundFreeze, &g_Config.m_XcEventSoundDeath, &g_Config.m_XcEventSoundTeleport};
	if(!*apEnabled[Sound])
		return;
	this->Sound()->Play(CSounds::CHN_GUI, m_aSounds[Sound], 0, g_Config.m_XcEventSoundVolume / 100.0f);
}

bool CXcEventFx::IsOwn(int ClientId) const
{
	return ClientId >= 0 && (ClientId == GameClient()->m_aLocalIds[0] || (Client()->DummyConnected() && ClientId == GameClient()->m_aLocalIds[1]));
}

void CXcEventFx::OnReset()
{
	m_Particles.Clear();
	m_vBolts.clear();
	m_DeathMarkerAge = -1.0f;
	for(int i = 0; i < NUM_DUMMIES; i++)
	{
		m_aLastValid[i] = false;
		m_aLastCheckpoint[i] = -1;
	}
	m_DummyLastPos = vec2(0.0f, 0.0f);
	for(float &Time : m_aLastSeenTime)
		Time = 0.0f;
}

void CXcEventFx::OnMapLoad()
{
	OnReset();
}

static ColorRGBA FinishColor(const CGameClient *pGameClient, int ClientId, float HueOffset)
{
	return EffectColor(pGameClient, g_Config.m_XcFinishColorMode, g_Config.m_XcFinishColor, ClientId, HueOffset);
}

void CXcEventFx::OnMessage(int MsgType, void *pRawMsg)
{
	if(MsgType != NETMSGTYPE_SV_RACEFINISH)
		return;
	const CNetMsg_Sv_RaceFinish *pMsg = (const CNetMsg_Sv_RaceFinish *)pRawMsg;
	const int ClientId = pMsg->m_ClientId;
	if(ClientId < 0 || ClientId >= MAX_CLIENTS)
		return;
	if(ShowFor(GameClient(), ClientId, g_Config.m_XcFinishSelf, g_Config.m_XcFinishDummy, g_Config.m_XcFinishOthers) &&
		GameClient()->m_Snap.m_aCharacters[ClientId].m_Active)
		Finish(GameClient()->m_aClients[ClientId].m_RenderPos, ClientId);

	// the banner and the sound only for the own tees
	if(!IsOwn(ClientId))
		return;
	PlayEventSound(pMsg->m_RecordPersonal || pMsg->m_RecordServer ? EVENT_SOUND_RECORD : EVENT_SOUND_FINISH);
	GameClient()->m_ScreenFx.Flash(pMsg->m_RecordServer || pMsg->m_RecordPersonal ? ColorRGBA(1.0f, 0.8f, 0.3f, 1.0f) : ColorRGBA(1.0f, 1.0f, 1.0f, 1.0f), 0.9f);
	if(!g_Config.m_XcFinishBanner)
		return;
	char aTime[32];
	const int Millis = pMsg->m_Time;
	if(Millis >= 3600000)
		str_format(aTime, sizeof(aTime), "%d:%02d:%02d.%03d", Millis / 3600000, (Millis / 60000) % 60, (Millis / 1000) % 60, Millis % 1000);
	else
		str_format(aTime, sizeof(aTime), "%02d:%02d.%03d", Millis / 60000, (Millis / 1000) % 60, Millis % 1000);
	char aSub[96];
	if(pMsg->m_Diff != 0 && (pMsg->m_RecordPersonal || pMsg->m_RecordServer || pMsg->m_Diff > 0))
		str_format(aSub, sizeof(aSub), "%s  (%s%.2f)", aTime, pMsg->m_Diff < 0 ? "-" : "+", std::abs(pMsg->m_Diff) / 1000.0f);
	else
		str_copy(aSub, aTime);
	const char *pTitle = pMsg->m_RecordServer ? XcUi::Tr("Server record!") : (pMsg->m_RecordPersonal ? XcUi::Tr("New personal record!") : XcUi::Tr("Finish"));
	GameClient()->m_XcHud.ShowBanner(pTitle, aSub, pMsg->m_RecordServer ? 2 : (pMsg->m_RecordPersonal ? 1 : 0));
}

void CXcEventFx::Finish(vec2 Pos, int ClientId)
{
	switch(g_Config.m_XcFinishFx)
	{
	case FINISH_CONFETTI: Confetti(Pos, ClientId); break;
	case FINISH_FIREWORKS: Fireworks(Pos, ClientId); break;
	}
}

void CXcEventFx::Confetti(vec2 Pos, int ClientId)
{
	const FxTune::CScope TuneScope(FxTune::FINISH);
	const float Amount = g_Config.m_XcFinishAmount / 100.0f;
	const int Count = (int)(110 * Amount);
	for(int i = 0; i < Count; i++)
	{
		// two fountains to the sides and up, like party poppers
		const float Side = i % 2 ? 1.0f : -1.0f;
		const float Angle = -pi / 2.0f + Side * random_float(0.15f, 0.75f);
		CXcParticles::SParticle Particle;
		Particle.m_Type = CXcParticles::EType::CONFETTI;
		Particle.m_Pos = Pos + vec2(Side * 10.0f, -10.0f);
		Particle.m_Vel = direction(Angle) * random_float(450.0f, 950.0f);
		Particle.m_Gravity = random_float(500.0f, 700.0f);
		Particle.m_Friction = 0.08f;
		Particle.m_LifeSpan = random_float(1.6f, 2.6f);
		Particle.m_Life = -random_float(0.0f, 0.12f);
		Particle.m_StartSize = random_float(10.0f, 16.0f);
		Particle.m_EndSize = Particle.m_StartSize * 0.8f;
		Particle.m_Rot = random_float(0.0f, 2.0f * pi);
		Particle.m_RotSpeed = random_float(-12.0f, 12.0f);
		Particle.m_Color = FinishColor(GameClient(), ClientId, random_float(0.0f, 1.0f));
		m_Particles.Add(Particle);
	}
	Burst(Pos, FinishColor(GameClient(), ClientId, 0.0f), 0, 0.0f, 0.0f);
}

// a ring and Count sparks flying out
void CXcEventFx::Burst(vec2 Pos, ColorRGBA Color, int Count, float Speed, float Delay)
{
	CXcParticles::SParticle Ring;
	Ring.m_Type = CXcParticles::EType::RING;
	Ring.m_Pos = Pos;
	Ring.m_Life = -Delay;
	Ring.m_LifeSpan = 0.5f;
	Ring.m_StartSize = 10.0f;
	Ring.m_EndSize = 90.0f + Speed * 0.1f;
	Ring.m_Thickness = 7.0f;
	Ring.m_Color = Color;
	m_Particles.Add(Ring);
	for(int i = 0; i < Count; i++)
	{
		const float Angle = 2.0f * pi * i / Count + random_float(-0.1f, 0.1f);
		CXcParticles::SParticle Particle;
		Particle.m_Type = i % 3 == 0 ? CXcParticles::EType::STAR : CXcParticles::EType::SPARK;
		Particle.m_Pos = Pos;
		Particle.m_Vel = direction(Angle) * Speed * random_float(0.75f, 1.1f);
		Particle.m_Gravity = 220.0f;
		Particle.m_Friction = 0.2f;
		Particle.m_Life = -Delay;
		Particle.m_LifeSpan = random_float(0.8f, 1.3f);
		Particle.m_StartSize = Particle.m_Type == CXcParticles::EType::STAR ? random_float(12.0f, 16.0f) : random_float(8.0f, 11.0f);
		Particle.m_EndSize = 0.0f;
		Particle.m_Rot = Angle;
		Particle.m_RotSpeed = random_float(-4.0f, 4.0f);
		Particle.m_Color = Color;
		m_Particles.Add(Particle);
	}
}

void CXcEventFx::Fireworks(vec2 Pos, int ClientId)
{
	const FxTune::CScope TuneScope(FxTune::FINISH);
	const float Amount = g_Config.m_XcFinishAmount / 100.0f;
	const int Shots = std::max(1, (int)std::round(4 * Amount));
	for(int i = 0; i < Shots; i++)
	{
		const vec2 Target = Pos + vec2(random_float(-220.0f, 220.0f), random_float(-330.0f, -190.0f));
		const float Delay = 0.25f + i * 0.28f;
		// the rocket: a few sparks rising to the burst point
		for(int k = 0; k < 6; k++)
		{
			CXcParticles::SParticle Trail;
			Trail.m_Type = CXcParticles::EType::SPARK;
			Trail.m_Pos = Pos + (Target - Pos) * (k / 6.0f);
			Trail.m_Life = -(Delay - 0.25f + k * 0.04f);
			Trail.m_LifeSpan = 0.3f;
			Trail.m_StartSize = 7.0f;
			Trail.m_EndSize = 0.0f;
			Trail.m_Color = ColorRGBA(1.0f, 0.95f, 0.8f, 1.0f);
			m_Particles.Add(Trail);
		}
		Burst(Target, FinishColor(GameClient(), ClientId, i * 0.27f), 34, 330.0f, Delay);
	}
}

void CXcEventFx::PlayerDeath(vec2 Pos, int ClientId)
{
	const FxTune::CScope TuneScope(FxTune::DEATH);
	if(ClientId >= 0 && ClientId == GameClient()->m_aLocalIds[g_Config.m_ClDummy])
	{
		GameClient()->m_ScreenFx.Flash(ColorRGBA(1.0f, 0.25f, 0.25f, 1.0f), 0.8f);
		GameClient()->m_ScreenFx.Shake(0.9f);
	}
	if(IsOwn(ClientId))
		PlayEventSound(EVENT_SOUND_DEATH);
	if(ClientId >= 0 && ClientId == GameClient()->m_aLocalIds[g_Config.m_ClDummy] && g_Config.m_XcDeathMarker)
	{
		m_DeathMarkerPos = Pos;
		m_DeathMarkerAge = 0.0f;
	}
	const int Style = g_Config.m_XcDeathFx;
	if(Style == DEATH_OFF || ClientId < 0 || ClientId >= MAX_CLIENTS)
		return;
	if(!ShowFor(GameClient(), ClientId, g_Config.m_XcDeathSelf, g_Config.m_XcDeathDummy, g_Config.m_XcDeathOthers))
		return;
	for(int i = 0; i < NUM_DUMMIES; i++)
		if(GameClient()->m_aLocalIds[i] == ClientId)
			m_aSinceDeath[i] = 0.0f;
	const ColorRGBA Color = EffectColor(GameClient(), g_Config.m_XcDeathColorMode, g_Config.m_XcDeathColor, ClientId);
	CXcParticles::SParticle Ring;
	Ring.m_Type = CXcParticles::EType::RING;
	Ring.m_Pos = Pos;
	Ring.m_LifeSpan = 0.35f;
	Ring.m_StartSize = 16.0f;
	Ring.m_EndSize = 70.0f;
	Ring.m_Thickness = 6.0f;
	Ring.m_Color = Color;
	m_Particles.Add(Ring);
	if(Style == DEATH_LIGHTNING)
	{
		// the bolt is drawn by RenderBolts, here only the flash on the ground and the sparks
		m_vBolts.push_back({Pos, 0.0f, Color, (unsigned)rand()});
		CXcParticles::SParticle Flash;
		Flash.m_Type = CXcParticles::EType::FLASH;
		Flash.m_Pos = Pos;
		Flash.m_LifeSpan = 0.3f;
		Flash.m_StartSize = 40.0f;
		Flash.m_EndSize = 110.0f;
		Flash.m_Color = Color;
		m_Particles.Add(Flash);
	}
	if(Style == DEATH_SOUL)
	{
		CXcParticles::SParticle Soul;
		Soul.m_Type = CXcParticles::EType::ICON;
		Soul.m_Sprite = CXcSprites::SPRITE_GHOST;
		Soul.m_Pos = Pos;
		Soul.m_Vel = vec2(0.0f, -90.0f);
		Soul.m_Gravity = -40.0f;
		Soul.m_Friction = 0.8f;
		Soul.m_LifeSpan = 1.6f;
		Soul.m_StartSize = 38.0f;
		Soul.m_EndSize = 46.0f;
		Soul.m_Color = Color;
		m_Particles.Add(Soul);
	}
	const int Count = Style == DEATH_ASH ? 26 : Style == DEATH_PIXELS ? 30 : Style == DEATH_SOUL ? 10 : 18;
	for(int i = 0; i < Count; i++)
	{
		const float Angle = random_float(0.0f, 2.0f * pi);
		CXcParticles::SParticle Particle;
		Particle.m_Pos = Pos + direction(Angle) * random_float(0.0f, 14.0f);
		Particle.m_Rot = Angle;
		Particle.m_Color = Color;
		switch(Style)
		{
		case DEATH_SHARDS:
			Particle.m_Type = CXcParticles::EType::SHARD;
			Particle.m_Vel = direction(Angle) * random_float(220.0f, 480.0f);
			Particle.m_Gravity = 900.0f;
			Particle.m_Friction = 0.3f;
			Particle.m_LifeSpan = random_float(0.6f, 1.0f);
			Particle.m_StartSize = random_float(9.0f, 15.0f);
			Particle.m_EndSize = 2.0f;
			Particle.m_RotSpeed = random_float(-10.0f, 10.0f);
			break;
		case DEATH_SPARKS:
			Particle.m_Type = i % 2 ? CXcParticles::EType::STAR : CXcParticles::EType::SPARK;
			Particle.m_Vel = direction(Angle) * random_float(250.0f, 500.0f);
			Particle.m_Gravity = 300.0f;
			Particle.m_Friction = 0.15f;
			Particle.m_LifeSpan = random_float(0.4f, 0.7f);
			Particle.m_StartSize = random_float(9.0f, 14.0f);
			Particle.m_EndSize = 0.0f;
			Particle.m_RotSpeed = random_float(-6.0f, 6.0f);
			break;
		case DEATH_LIGHTNING:
			// electric sparks that jump off the ground
			Particle.m_Type = CXcParticles::EType::SPARK;
			Particle.m_Vel = vec2(std::cos(Angle) * random_float(150.0f, 420.0f), -std::abs(std::sin(Angle)) * random_float(150.0f, 450.0f));
			Particle.m_Gravity = 1200.0f;
			Particle.m_Friction = 0.3f;
			Particle.m_LifeSpan = random_float(0.3f, 0.6f);
			Particle.m_StartSize = random_float(6.0f, 10.0f);
			Particle.m_EndSize = 0.0f;
			break;
		case DEATH_SOUL:
			// small lights that rise with the ghost
			Particle.m_Type = CXcParticles::EType::SPARK;
			Particle.m_Vel = vec2(random_float(-40.0f, 40.0f), random_float(-140.0f, -60.0f));
			Particle.m_Gravity = -30.0f;
			Particle.m_Friction = 0.6f;
			Particle.m_LifeSpan = random_float(0.8f, 1.4f);
			Particle.m_StartSize = random_float(5.0f, 9.0f);
			Particle.m_EndSize = 0.0f;
			break;
		case DEATH_HEARTS:
			Particle.m_Type = CXcParticles::EType::ICON;
			Particle.m_Sprite = CXcSprites::SPRITE_HEART;
			Particle.m_Vel = direction(Angle) * random_float(150.0f, 340.0f);
			Particle.m_Gravity = 250.0f;
			Particle.m_Friction = 0.2f;
			Particle.m_LifeSpan = random_float(0.6f, 1.0f);
			Particle.m_StartSize = random_float(14.0f, 22.0f);
			Particle.m_EndSize = 6.0f;
			Particle.m_RotSpeed = random_float(-4.0f, 4.0f);
			break;
		case DEATH_PIXELS:
			// square bits of the tee that fall down
			Particle.m_Type = CXcParticles::EType::CONFETTI;
			Particle.m_Pos = Pos + vec2(random_float(-20.0f, 20.0f), random_float(-20.0f, 20.0f));
			Particle.m_Vel = (Particle.m_Pos - Pos) * random_float(6.0f, 14.0f) + vec2(0.0f, -120.0f);
			Particle.m_Gravity = 1300.0f;
			Particle.m_Friction = 0.5f;
			Particle.m_LifeSpan = random_float(0.7f, 1.1f);
			Particle.m_StartSize = random_float(6.0f, 10.0f);
			Particle.m_EndSize = 5.0f;
			Particle.m_RotSpeed = random_float(-3.0f, 3.0f);
			break;
		default:
			// ash: dark flakes drifting up
			Particle.m_Type = CXcParticles::EType::DUST;
			Particle.m_Vel = vec2(random_float(-60.0f, 60.0f), random_float(-160.0f, -40.0f));
			Particle.m_Gravity = -40.0f;
			Particle.m_Friction = 0.5f;
			Particle.m_LifeSpan = random_float(0.9f, 1.6f);
			Particle.m_StartSize = random_float(10.0f, 18.0f);
			Particle.m_EndSize = 4.0f;
			Particle.m_RotSpeed = random_float(-2.0f, 2.0f);
			Particle.m_Color = ColorRGBA(Color.r * 0.35f, Color.g * 0.35f, Color.b * 0.35f, 0.85f);
			break;
		}
		m_Particles.Add(Particle);
	}
}

void CXcEventFx::PlayerSpawn(vec2 Pos)
{
	const FxTune::CScope TuneScope(FxTune::DEATH);
	if(!g_Config.m_XcSpawnFx)
		return;
	const ColorRGBA Color = EffectColor(GameClient(), g_Config.m_XcDeathColorMode == 3 ? 0 : g_Config.m_XcDeathColorMode, g_Config.m_XcDeathColor, -1);
	for(int i = 0; i < 2; i++)
	{
		CXcParticles::SParticle Ring;
		Ring.m_Type = CXcParticles::EType::RING;
		Ring.m_Pos = Pos;
		Ring.m_Life = -i * 0.12f;
		Ring.m_LifeSpan = 0.45f;
		Ring.m_StartSize = 70.0f;
		Ring.m_EndSize = 14.0f; // closes in on the new tee
		Ring.m_Thickness = 5.0f;
		Ring.m_Color = Color;
		m_Particles.Add(Ring);
	}
}

void CXcEventFx::CheckpointFlash(vec2 Pos, int ClientId, bool Controlled)
{
	const FxTune::CScope TuneScope(FxTune::CHECKPOINT);
	const ColorRGBA Color = EffectColor(GameClient(), g_Config.m_XcCheckpointColorMode, g_Config.m_XcCheckpointColor, ClientId);
	// a flat ring on the ground level of the tee and a soft flash, sparks rise like from a gate
	CXcParticles::SParticle Ring;
	Ring.m_Type = CXcParticles::EType::RING;
	Ring.m_Pos = Pos;
	Ring.m_LifeSpan = 0.5f;
	Ring.m_StartSize = 12.0f;
	Ring.m_EndSize = 110.0f;
	Ring.m_Thickness = 7.0f;
	Ring.m_Squash = 0.35f;
	Ring.m_Color = Color;
	m_Particles.Add(Ring);
	CXcParticles::SParticle Flash;
	Flash.m_Type = CXcParticles::EType::FLASH;
	Flash.m_Pos = Pos;
	Flash.m_LifeSpan = 0.25f;
	Flash.m_StartSize = 30.0f;
	Flash.m_EndSize = 70.0f;
	Flash.m_Color = Color.WithAlpha(0.7f);
	m_Particles.Add(Flash);
	for(int i = 0; i < 10; i++)
	{
		CXcParticles::SParticle Spark;
		Spark.m_Type = CXcParticles::EType::SPARK;
		Spark.m_Pos = Pos + vec2(random_float(-24.0f, 24.0f), random_float(-6.0f, 10.0f));
		Spark.m_Vel = vec2(random_float(-30.0f, 30.0f), random_float(-260.0f, -140.0f));
		Spark.m_Friction = 0.6f;
		Spark.m_LifeSpan = random_float(0.4f, 0.7f);
		Spark.m_StartSize = random_float(4.0f, 7.0f);
		Spark.m_EndSize = 0.0f;
		Spark.m_Color = Color;
		m_Particles.Add(Spark);
	}
	if(Controlled)
		GameClient()->m_ScreenFx.Flash(Color, 0.25f);
}

void CXcEventFx::TeleportFlash(vec2 From, vec2 To, int ClientId)
{
	const FxTune::CScope TuneScope(FxTune::TELEPORT);
	const ColorRGBA Color = EffectColor(GameClient(), g_Config.m_XcTeleportColorMode, g_Config.m_XcTeleportColor, ClientId);
	CXcParticles::SParticle Ring;
	Ring.m_Type = CXcParticles::EType::RING;
	Ring.m_Pos = From;
	Ring.m_LifeSpan = 0.3f;
	Ring.m_StartSize = 50.0f;
	Ring.m_EndSize = 6.0f;
	Ring.m_Thickness = 5.0f;
	Ring.m_Color = Color;
	m_Particles.Add(Ring);
	Ring.m_Pos = To;
	Ring.m_LifeSpan = 0.45f;
	Ring.m_StartSize = 8.0f;
	Ring.m_EndSize = 80.0f;
	Ring.m_Thickness = 7.0f;
	m_Particles.Add(Ring);
	for(int i = 0; i < 12; i++)
	{
		const float Angle = 2.0f * pi * i / 12.0f;
		CXcParticles::SParticle Particle;
		Particle.m_Type = CXcParticles::EType::STAR;
		Particle.m_Pos = To + direction(Angle) * 10.0f;
		Particle.m_Vel = direction(Angle) * random_float(140.0f, 260.0f);
		Particle.m_Friction = 0.1f;
		Particle.m_LifeSpan = random_float(0.35f, 0.55f);
		Particle.m_StartSize = random_float(10.0f, 14.0f);
		Particle.m_EndSize = 0.0f;
		Particle.m_RotSpeed = random_float(-5.0f, 5.0f);
		Particle.m_Color = Color;
		m_Particles.Add(Particle);
	}
}

void CXcEventFx::RenderDummyArrow()
{
	const FxGlow::CScope GlowScope(FxGlow::ARROW);
	if(Client()->State() != IClient::STATE_ONLINE)
		return;
	struct STarget
	{
		vec2 m_Pos;
		int m_ClientId;
		ColorRGBA m_Color;
		bool m_Name;
	};
	std::vector<STarget> vTargets;
	const int Own = GameClient()->m_aLocalIds[g_Config.m_ClDummy];
	int Other = -1;
	if(g_Config.m_XcDummyArrow && Client()->DummyConnected())
	{
		Other = GameClient()->m_aLocalIds[!g_Config.m_ClDummy];
		// far away the dummy drops out of the snapshot, then its last known place is used
		if(Other >= 0 && GameClient()->m_aClients[Other].m_Active)
		{
			if(GameClient()->m_Snap.m_aCharacters[Other].m_Active)
				m_DummyLastPos = GameClient()->m_aClients[Other].m_RenderPos;
			if(m_DummyLastPos != vec2(0.0f, 0.0f))
				vTargets.push_back({m_DummyLastPos, Other, EffectColor(GameClient(), g_Config.m_XcDummyArrowColorMode, g_Config.m_XcDummyArrowColor, Other), false});
		}
	}
	// team mates (the same DDRace team) and friends, while they are in the snapshot
	if((g_Config.m_XcTeamArrows || g_Config.m_XcFriendArrows) && Own >= 0)
	{
		const int OwnTeam = GameClient()->m_Teams.Team(Own);
		for(int ClientId = 0; ClientId < MAX_CLIENTS; ClientId++)
		{
			const CGameClient::CClientData &Data = GameClient()->m_aClients[ClientId];
			if(ClientId == Own || ClientId == Other || !Data.m_Active)
				continue;
			// far away players drop out of the snapshot: their last known place is used for a while
			if(GameClient()->m_Snap.m_aCharacters[ClientId].m_Active)
			{
				m_aLastSeenPos[ClientId] = Data.m_RenderPos;
				m_aLastSeenTime[ClientId] = Client()->GlobalTime();
			}
			else if(Client()->GlobalTime() - m_aLastSeenTime[ClientId] > 20.0f || m_aLastSeenTime[ClientId] <= 0.0f)
				continue;
			const bool Mate = g_Config.m_XcTeamArrows && OwnTeam != TEAM_FLOCK && OwnTeam != TEAM_SUPER && GameClient()->m_Teams.Team(ClientId) == OwnTeam;
			const bool Friend = g_Config.m_XcFriendArrows && Data.m_Friend;
			if(!Mate && !Friend)
				continue;
			// friends are mint, team mates wear their body color
			const ColorRGBA Color = Friend && !Mate ? ColorRGBA(0.24f, 0.84f, 0.64f, 1.0f) : EffectColor(GameClient(), COLOR_SKIN, 0, ClientId);
			vTargets.push_back({m_aLastSeenPos[ClientId], ClientId, GameClient()->m_Snap.m_aCharacters[ClientId].m_Active ? Color : Color.WithMultipliedAlpha(0.6f), true});
		}
	}
	if(vTargets.empty())
		return;

	// drawn over the map foreground, so it maps the world itself
	const CScreenRect Saved = Graphics()->GetScreen();
	const vec2 Camera = GameClient()->m_Camera.m_Center;
	const CScreenRect Screen = Graphics()->MapScreenToWorld(Camera.x, Camera.y, 100.0f, 100.0f, 100.0f, 0.0f, 0.0f, Graphics()->ScreenAspect(), GameClient()->m_Camera.m_Zoom);
	const float X0 = Screen.m_TopLeft.x, Y0 = Screen.m_TopLeft.y, X1 = Screen.m_BottomRight.x, Y1 = Screen.m_BottomRight.y;
	Graphics()->MapScreen(Screen);
	// world units per screen pixel, so the arrow keeps its size with any zoom
	const float Unit = (X1 - X0) / std::max((float)Graphics()->ScreenWidth(), 1.0f) * Graphics()->ScreenHeight() / 1080.0f;
	const vec2 Center((X0 + X1) * 0.5f, (Y0 + Y1) * 0.5f);
	const float Margin = 46.0f * Unit;
	const float Pulse = 0.85f + 0.15f * std::sin(Client()->GlobalTime() * 5.0f);
	std::vector<SFxShape> vShapes;
	struct SLabel
	{
		vec2 m_Tip;
		vec2 m_Dir;
		float m_Size;
		const char *m_pName;
	};
	std::vector<SLabel> vLabels;
	for(const STarget &Target : vTargets)
	{
		const vec2 Pos = Target.m_Pos;
		if(Pos.x > X0 && Pos.x < X1 && Pos.y > Y0 && Pos.y < Y1)
			continue;
		const vec2 Dir = normalize(Pos - Center);
		// where the line from the middle to the player leaves the screen
		const vec2 Half((X1 - X0) * 0.5f - Margin, (Y1 - Y0) * 0.5f - Margin);
		const float t = std::min(Dir.x != 0.0f ? Half.x / std::abs(Dir.x) : 1e9f, Dir.y != 0.0f ? Half.y / std::abs(Dir.y) : 1e9f);
		const vec2 Tip = Center + Dir * t;
		// the others get smaller arrows than the dummy
		const float Size = (Target.m_Name ? 15.0f : 20.0f) * Unit * g_Config.m_XcDummyArrowSize / 100.0f;
		const ColorRGBA Color = Target.m_Color;
		// a dark round plate keeps the arrow readable on bright maps
		vShapes.push_back(FxShape::Circle(Tip, Size * 1.45f, ColorRGBA(0.04f, 0.045f, 0.06f, 0.55f)));
		vShapes.push_back(FxShape::Ring(Tip, vec2(Size * 1.45f, Size * 1.45f), 1.5f * Unit, Color.WithAlpha(0.5f * Pulse)));
		const float Angle = std::atan2(Dir.y, Dir.x) + pi / 2.0f; // the triangle points up by default
		CFxRenderer::AppendGlowing(vShapes, FxShape::Triangle(Tip + Dir * Size * 0.12f, vec2(Size * 0.7f, Size * 0.75f), Color.WithAlpha(0.95f * Pulse)).Rotate(Angle), 10.0f * Unit, 0.6f);
		if(Target.m_Name)
			vLabels.push_back({Tip, Dir, Size, GameClient()->m_aClients[Target.m_ClientId].m_aName});
	}
	GameClient()->m_FxRenderer.Draw(vShapes, true);
	// the name next to the arrows of team mates and friends, towards the middle of the screen
	const float FontSize = 11.0f * Unit;
	for(const SLabel &Label : vLabels)
	{
		const float Width = TextRender()->TextWidth(FontSize, Label.m_pName);
		// next to the plate, on the side of the middle of the screen
		const vec2 Box(Width * 0.5f + 4.0f * Unit, FontSize * 0.6f + 2.0f * Unit);
		const float Reach = std::min(Label.m_Dir.x != 0.0f ? Box.x / std::abs(Label.m_Dir.x) : 1e9f, Label.m_Dir.y != 0.0f ? Box.y / std::abs(Label.m_Dir.y) : 1e9f);
		const vec2 Pos = Label.m_Tip - Label.m_Dir * (Label.m_Size * 1.6f + Reach);
		TextRender()->TextColor(ColorRGBA(1.0f, 1.0f, 1.0f, 0.9f));
		TextRender()->TextOutlineColor(ColorRGBA(0.0f, 0.0f, 0.0f, 0.6f));
		TextRender()->Text(Pos.x - Width * 0.5f, Pos.y - FontSize * 0.6f, FontSize, Label.m_pName);
	}
	TextRender()->TextColor(TextRender()->DefaultTextColor());
	TextRender()->TextOutlineColor(TextRender()->DefaultTextOutlineColor());
	Graphics()->MapScreen(Saved);
}

void CXcEventFx::OnRender()
{
	if(Client()->State() != IClient::STATE_ONLINE && Client()->State() != IClient::STATE_DEMOPLAYBACK)
		return;
	const float Dt = std::min(Client()->RenderFrameTime(), 0.1f);

	// teleports of the own tees: a big jump between two frames without dying
	for(int i = 0; i < NUM_DUMMIES; i++)
	{
		m_aSinceDeath[i] += Dt;
		const int ClientId = GameClient()->m_aLocalIds[i];
		if(ClientId < 0 || (i == 1 && !Client()->DummyConnected()) || !GameClient()->m_Snap.m_aCharacters[ClientId].m_Active)
		{
			m_aLastValid[i] = false;
			continue;
		}
		const vec2 Pos = GameClient()->m_aClients[ClientId].m_RenderPos;
		if(m_aLastValid[i] && m_aSinceDeath[i] > 0.6f && distance(Pos, m_aLastPos[i]) > 260.0f)
		{
			if(g_Config.m_XcTeleportFx)
				TeleportFlash(m_aLastPos[i], Pos, ClientId);
			PlayEventSound(EVENT_SOUND_TELEPORT);
		}
		// freeze sounds for the tee you control (the dummy would double them)
		const CGameClient::CClientData &Data = GameClient()->m_aClients[ClientId];
		const bool Frozen = Data.m_FreezeEnd != 0 || Data.m_RegularPredicted.m_FreezeEnd != 0;
		if(m_aLastValid[i] && Frozen != m_aWasFrozen[i] && i == g_Config.m_ClDummy && m_aSinceDeath[i] > 0.3f)
		{
			PlayEventSound(Frozen ? EVENT_SOUND_FREEZE : EVENT_SOUND_UNFREEZE);
			if(Frozen)
			{
				GameClient()->m_ScreenFx.Flash(ColorRGBA(0.45f, 0.8f, 1.0f, 1.0f), 0.7f);
				GameClient()->m_ScreenFx.Shake(0.35f);
			}
		}
		m_aWasFrozen[i] = Frozen;
		// checkpoints: the map says which time checkpoint tile is under the tee, a new one flashes
		const int MapIndex = Collision()->GetMapIndex(Pos);
		int Tile = MapIndex >= 0 ? Collision()->GetTileIndex(MapIndex) : 0;
		if(Tile < TILE_START || Tile > TILE_TIME_CHECKPOINT_LAST)
			Tile = MapIndex >= 0 ? Collision()->GetFrontTileIndex(MapIndex) : 0;
		if(Tile == TILE_START)
			m_aLastCheckpoint[i] = -1; // a new run
		else if(Tile >= TILE_TIME_CHECKPOINT_FIRST && Tile <= TILE_TIME_CHECKPOINT_LAST && Tile != m_aLastCheckpoint[i])
		{
			if(m_aLastValid[i] && g_Config.m_XcCheckpointFx)
				CheckpointFlash(Pos, ClientId, i == g_Config.m_ClDummy);
			m_aLastCheckpoint[i] = Tile;
		}
		m_aLastPos[i] = Pos;
		m_aLastValid[i] = true;
	}

	m_Particles.Update(Dt);
	m_Particles.Render(GameClient()->m_FxRenderer);
	RenderBolts(Dt);
	RenderDeathMarker(Dt);
}

void CXcEventFx::RenderBolts(float Dt)
{
	constexpr float BOLT_LIFE = 0.45f;
	for(SBolt &Bolt : m_vBolts)
	{
		Bolt.m_Life += Dt;
		const float t = Bolt.m_Life / BOLT_LIFE;
		// it flickers: on, off, on again and fades
		const float Flicker = t < 0.15f ? 1.0f : t < 0.25f ? 0.25f : 1.0f - (t - 0.25f) / 0.75f;
		if(Flicker <= 0.0f)
			continue;
		// a jagged line from high above down to the tee, the same shape every frame (the seed)
		unsigned Seed = Bolt.m_Seed;
		const auto Next = [&Seed]() {
			Seed = Seed * 1103515245u + 12345u;
			return ((Seed >> 16) & 0x7fff) / 32767.0f;
		};
		const float Height = 420.0f;
		constexpr int SEGMENTS = 9;
		vec2 Prev = Bolt.m_Pos - vec2((Next() - 0.5f) * 80.0f, Height);
		const float Width = 4.0f * Flicker + 1.0f;
		for(int i = 1; i <= SEGMENTS; i++)
		{
			const float k = i / (float)SEGMENTS;
			const vec2 Point = i == SEGMENTS ? Bolt.m_Pos : Bolt.m_Pos - vec2((Next() - 0.5f) * 70.0f * (1.0f - k), Height * (1.0f - k));
			GameClient()->m_FxRenderer.AddWorldGlowing(FxShape::Line(Prev, Point, Width, ColorRGBA(1.0f, 1.0f, 1.0f, Flicker)), 14.0f, 0.9f);
			GameClient()->m_FxRenderer.AddWorld(FxShape::Line(Prev, Point, Width * 2.2f, Bolt.m_Color.WithAlpha(0.45f * Flicker)).Glow(18.0f, 0.8f).Additive());
			// a small branch now and then
			if(Next() < 0.35f && i < SEGMENTS - 1)
			{
				const vec2 Branch = Point + vec2((Next() - 0.5f) * 90.0f, Next() * 50.0f + 15.0f);
				GameClient()->m_FxRenderer.AddWorld(FxShape::Line(Point, Branch, Width * 0.6f, Bolt.m_Color.WithAlpha(0.7f * Flicker)).Glow(10.0f, 0.6f).Additive());
			}
			Prev = Point;
		}
	}
	m_vBolts.erase(std::remove_if(m_vBolts.begin(), m_vBolts.end(), [](const SBolt &Bolt) { return Bolt.m_Life >= BOLT_LIFE; }), m_vBolts.end());
}

void CXcEventFx::RenderDeathMarker(float Dt)
{
	if(m_DeathMarkerAge < 0.0f)
		return;
	m_DeathMarkerAge += Dt;
	const float Duration = (float)g_Config.m_XcDeathMarkerTime;
	if(!g_Config.m_XcDeathMarker || m_DeathMarkerAge > Duration)
	{
		m_DeathMarkerAge = -1.0f;
		return;
	}
	// a small cross in a ring that fades in the last second
	const float Fade = std::min(std::min(m_DeathMarkerAge * 4.0f, 1.0f), Duration - m_DeathMarkerAge);
	const ColorRGBA Color = ColorRGBA(1.0f, 0.35f, 0.35f, 0.85f * Fade);
	const float Bob = std::sin(Client()->GlobalTime() * 3.0f) * 3.0f;
	const vec2 Pos = m_DeathMarkerPos + vec2(0.0f, -46.0f + Bob);
	GameClient()->m_FxRenderer.AddWorldGlowing(FxShape::Ring(Pos, vec2(11.0f, 11.0f), 2.5f, Color), 8.0f, 0.5f);
	GameClient()->m_FxRenderer.AddWorldGlowing(FxShape::Line(Pos + vec2(-5.0f, -5.0f), Pos + vec2(5.0f, 5.0f), 2.5f, Color), 6.0f, 0.4f);
	GameClient()->m_FxRenderer.AddWorldGlowing(FxShape::Line(Pos + vec2(5.0f, -5.0f), Pos + vec2(-5.0f, 5.0f), 2.5f, Color), 6.0f, 0.4f);
	// a thin line down to the place
	GameClient()->m_FxRenderer.AddWorld(FxShape::Line(Pos + vec2(0.0f, 13.0f), m_DeathMarkerPos + vec2(0.0f, -16.0f), 1.5f, Color.WithMultipliedAlpha(0.4f)));
}

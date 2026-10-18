#include "jelly.h"

#include "xc_helpers.h"

#include <base/math.h>

#include <engine/client.h>
#include <engine/shared/config.h>

#include <game/client/gameclient.h>

#include <algorithm>
#include <cmath>

using namespace XcHelpers;

static constexpr float STIFFNESS = 320.0f;
static constexpr float DAMPING = 9.0f;

void CXcJelly::OnReset()
{
	for(SState &State : m_aStates)
		State = SState();
}

void CXcJelly::OnMapLoad()
{
	OnReset();
}

bool CXcJelly::Begin(int ClientId)
{
	if(!g_Config.m_XcJelly || ClientId < 0 || ClientId >= MAX_CLIENTS ||
		!ShowFor(GameClient(), ClientId, g_Config.m_XcJellySelf, g_Config.m_XcJellyDummy, g_Config.m_XcJellyOthers))
		return false;
	SState &State = m_aStates[ClientId];
	const float Now = Client()->GlobalTime();
	const float Dt = State.m_Valid ? std::clamp(Now - State.m_LastTime, 0.0f, 0.1f) : 0.0f;
	State.m_LastTime = Now;
	const vec2 Vel = CharacterVelocity(GameClient(), ClientId);
	if(!State.m_Valid)
	{
		State.m_Valid = true;
		State.m_LastVel = Vel;
	}
	const float Strength = g_Config.m_XcJellyStrength / 100.0f;
	const float DeltaY = Vel.y - State.m_LastVel.y;
	// a sudden stop of a fall is a landing (squash), a sudden push up is a jump (stretch)
	if(DeltaY < -250.0f)
	{
		const float Impulse = std::min(-DeltaY / 1400.0f, 1.0f) * 9.0f;
		State.m_SquashVel += State.m_LastVel.y > 150.0f ? Impulse : -Impulse;
	}
	State.m_LastVel = Vel;
	// fixed small steps keep the spring stable on any frame rate
	constexpr float STEP = 1.0f / 240.0f;
	for(float Left = Dt; Left > 0.0f; Left -= STEP)
	{
		const float h = std::min(Left, STEP);
		State.m_SquashVel += (-STIFFNESS * State.m_Squash - DAMPING * State.m_SquashVel) * h;
		State.m_Squash += State.m_SquashVel * h;
	}
	State.m_Squash = std::clamp(State.m_Squash, -0.6f, 0.6f);
	const float Amount = State.m_Squash * 0.32f * Strength;
	if(std::abs(Amount) < 0.004f)
		return false;
	// keep the volume: wider when flatter
	const vec2 Scale(1.0f + Amount, 1.0f / (1.0f + Amount));

	// scale the view around the feet, so the tee stays on the ground
	const CGameClient::CClientData &Data = GameClient()->m_aClients[ClientId];
	const vec2 Pivot = Data.m_RenderPos + vec2(0.0f, 18.0f * Data.m_RenderInfo.m_Size / 64.0f);
	m_SavedScreen = Graphics()->GetScreen();
	const vec2 TopLeft = Pivot + (m_SavedScreen.m_TopLeft - Pivot) / Scale;
	const vec2 BottomRight = Pivot + (m_SavedScreen.m_BottomRight - Pivot) / Scale;
	Graphics()->MapScreen(CScreenRect(TopLeft, BottomRight));
	return true;
}

void CXcJelly::End()
{
	Graphics()->MapScreen(m_SavedScreen);
}

#include "fast_input.h"

#include <engine/client.h>
#include <engine/shared/config.h>

#include <game/client/gameclient.h>

#include <algorithm>
#include <cmath>

// one game tick in milliseconds
static constexpr float TICK_MS = 20.0f;

// beyond this the game physics is not run further ahead, the tee only glides on (a long physics prediction
// ahead wobbles a lot on every new press)
static constexpr int PHYSICS_HORIZON_MS = 40;

static int s_AutoMs = 20;
static int s_NewInputFrames = 0;
static float s_LastPreInput = -100.0f;
static float s_Now = 0.0f;

bool CXcFastInput::On()
{
	return g_Config.m_XcFastInput || g_Config.m_TcFastInput;
}

int CXcFastInput::AmountMs()
{
	if(!g_Config.m_XcFastInput)
		return g_Config.m_TcFastInputAmount;
	return g_Config.m_XcFastInputAuto ? s_AutoMs : g_Config.m_XcFastInputAmount;
}

int CXcFastInput::PhysicsMs()
{
	const int Amount = AmountMs();
	return g_Config.m_XcFastInput ? std::min(Amount, PHYSICS_HORIZON_MS) : Amount;
}

bool CXcFastInput::Others()
{
	if(!g_Config.m_XcFastInput)
		return g_Config.m_TcFastInputOthers != 0;
	// guessing the others ahead without their real inputs makes them wobble: only on servers that send them
	return g_Config.m_XcFastInputOthers && s_Now - s_LastPreInput < 2.0f;
}

int CXcFastInput::OthersMs()
{
	return g_Config.m_XcFastInput ? std::min(AmountMs(), (int)TICK_MS) : AmountMs();
}

void CXcFastInput::NotePreInput(float Now)
{
	s_LastPreInput = Now;
}

vec2 CXcFastInput::Extend(int ClientId, vec2 Pos)
{
	const int Extra = AmountMs() - PhysicsMs();
	if(!g_Config.m_XcFastInput || Extra <= 0 || ClientId < 0 || ClientId >= MAX_CLIENTS)
		return Pos;
	// the speed the prediction has now, per tick
	const vec2 Vel = GameClient()->m_aClients[ClientId].m_RegularPredicted.m_Vel;
	const vec2 Target = Pos + Vel * (Extra / TICK_MS);
	vec2 Before;
	if(Collision()->IntersectLine(Pos, Target, nullptr, &Before))
		return Before;
	return Target;
}

void CXcFastInput::NoteNewInput()
{
	// the positions of this and the next frame come from the new prediction
	s_NewInputFrames = 2;
}

void CXcFastInput::OnReset()
{
	for(SState &State : m_aState)
		State = SState();
	m_CorrectionAvg = 0.0f;
}

void CXcFastInput::OnRender()
{
	// automatic amount: a whole tick hides the wait for the next tick; when the prediction ahead keeps being
	// corrected a lot (jumpy servers, a lot of hooking), less of it looks calmer
	const float Target = std::clamp(TICK_MS - (m_CorrectionAvg - 2.0f) * 2.0f, 10.0f, TICK_MS);
	const float Dt = std::clamp(Client()->RenderFrameTime(), 0.0f, 0.1f);
	m_AutoMs += (Target - m_AutoMs) * (1.0f - std::exp(-Dt / 1.5f));
	s_Now = Client()->LocalTime();
	s_AutoMs = std::clamp((int)std::round(m_AutoMs), 1, 100);
	if(s_NewInputFrames > 0)
		s_NewInputFrames--;
}

vec2 CXcFastInput::Smooth(int ClientId, vec2 Target)
{
	if(!g_Config.m_XcFastInput || g_Config.m_XcFastInputSmooth <= 0 || ClientId < 0 || ClientId >= MAX_CLIENTS)
	{
		if(ClientId >= 0 && ClientId < MAX_CLIENTS)
			m_aState[ClientId].m_Valid = false;
		return Target;
	}
	SState &State = m_aState[ClientId];
	const float Now = Client()->LocalTime();
	const float Dt = Now - State.m_Time;
	if(!State.m_Valid || Dt < 0.0f || Dt > 0.25f || distance(Target, State.m_Last) > 120.0f)
	{
		// first frame, a pause or a teleport: start over
		State = SState();
		State.m_Valid = true;
		State.m_Last = Target;
		State.m_Time = Now;
		return Target;
	}
	if(Dt == 0.0f)
		return Target + State.m_Err;

	// where the tee would be now if nothing had changed
	const vec2 Expected = State.m_Last + State.m_Vel * Dt;
	const vec2 Jump = Target - Expected;
	if(s_NewInputFrames > 0 && length(Jump) > 0.5f)
	{
		// a new press moved the prediction ahead: keep showing the old path and let the difference melt
		State.m_Err -= Jump;
		m_CorrectionAvg += (length(Jump) - m_CorrectionAvg) * 0.1f;
	}
	else
		State.m_Vel = (Target - State.m_Last) / Dt;
	// never far from the truth
	const float MaxErr = 12.0f;
	if(length(State.m_Err) > MaxErr)
		State.m_Err = normalize(State.m_Err) * MaxErr;
	// 0..100 % = melts in 0..60 ms
	const float Tau = 0.06f * g_Config.m_XcFastInputSmooth / 100.0f;
	State.m_Err *= std::exp(-Dt / std::max(Tau, 0.001f));
	State.m_Last = Target;
	State.m_Time = Now;
	return Target + State.m_Err;
}

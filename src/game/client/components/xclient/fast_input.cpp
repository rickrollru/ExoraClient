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

static float s_AutoMs = 20.0f;
static int s_NewInputFrames = 0;
static int s_CorrectionFrames = 0;
static float s_LastPreInput = -100.0f;
static float s_Now = 0.0f;

bool CXcFastInput::On()
{
	return g_Config.m_XcFastInput || g_Config.m_TcFastInput;
}

float CXcFastInput::AmountMsF()
{
	if(!g_Config.m_XcFastInput)
		return (float)g_Config.m_TcFastInputAmount;
	return g_Config.m_XcFastInputAuto ? s_AutoMs : (float)g_Config.m_XcFastInputAmount;
}

float CXcFastInput::PhysicsMsF()
{
	const float Amount = AmountMsF();
	return g_Config.m_XcFastInput ? std::min(Amount, (float)PHYSICS_HORIZON_MS) : Amount;
}

int CXcFastInput::AmountMs()
{
	return (int)std::round(AmountMsF());
}

int CXcFastInput::PhysicsMs()
{
	return (int)std::round(PhysicsMsF());
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

bool CXcFastInput::HasPreInput(int ClientId, int Tick) const
{
	if(ClientId < 0 || ClientId >= MAX_CLIENTS || Tick < 0)
		return false;
	return GameClient()->m_aClients[ClientId].m_aPreInputs[Tick % 200].m_IntendedTick == Tick;
}

vec2 CXcFastInput::Extend(int ClientId, vec2 Pos)
{
	const float Extra = AmountMsF() - PhysicsMsF();
	if(!g_Config.m_XcFastInput || Extra <= 0.0f || ClientId < 0 || ClientId >= MAX_CLIENTS)
		return Pos;
	// the speed of the prediction, blended between its two ticks like the position: taken once a tick it would
	// make the glide jump on every tick while the tee speeds up or slows down
	const auto &Data = GameClient()->m_aClients[ClientId];
	const vec2 Vel = mix(Data.m_PrevPredicted.m_Vel, Data.m_Predicted.m_Vel, Client()->PredIntraGameTick(g_Config.m_ClDummy));
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

void CXcFastInput::NoteCorrection()
{
	s_CorrectionFrames = 2;
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
	// corrected a lot (jumpy servers, a lot of hooking), less of it looks calmer. The target must hold for two
	// seconds before it is followed, then the amount glides there at 4 ms a second: every change of the amount
	// moves the tee a little, so it changes rarely and never by steps
	const float Dt = std::clamp(Client()->RenderFrameTime(), 0.0f, 0.1f);
	s_Now = Client()->LocalTime();
	const float Target = std::round(std::clamp(TICK_MS - (m_CorrectionAvg - 2.0f) * 2.0f, 10.0f, TICK_MS) / 2.0f) * 2.0f;
	if(std::abs(Target - m_AutoTarget) >= 2.0f)
	{
		m_AutoTarget = Target;
		m_AutoTargetSince = s_Now;
	}
	if(s_Now - m_AutoTargetSince >= 2.0f)
	{
		const float Step = 4.0f * Dt;
		m_AutoMs = m_AutoMs < m_AutoTarget ? std::min(m_AutoMs + Step, m_AutoTarget) : std::max(m_AutoMs - Step, m_AutoTarget);
	}
	s_AutoMs = std::clamp(m_AutoMs, 1.0f, 100.0f);
	if(s_NewInputFrames > 0)
		s_NewInputFrames--;
	if(s_CorrectionFrames > 0)
		s_CorrectionFrames--;
}

vec2 CXcFastInput::Smooth(int ClientId, vec2 Target)
{
	// with Exora input, or for the corrections of the prediction alone (xc_pred_smooth)
	if(!(g_Config.m_XcFastInput || g_Config.m_XcPredSmooth) || g_Config.m_XcFastInputSmooth <= 0 || ClientId < 0 || ClientId >= MAX_CLIENTS)
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
	const float Strength = g_Config.m_XcFastInputSmooth / 100.0f;
	if((s_NewInputFrames > 0 || s_CorrectionFrames > 0) && length(Jump) > 0.5f)
	{
		// keep showing the old path and let the difference melt: a wrong guess slowly (up to 80 ms), the change of a
		// new press quickly (up to 25 ms), it is the answer to the key and must not lag
		State.m_Err -= Jump;
		State.m_Tau = s_CorrectionFrames > 0 ? 0.08f * Strength : 0.025f * Strength;
		if(s_NewInputFrames > 0)
			m_CorrectionAvg += (length(Jump) - m_CorrectionAvg) * 0.1f;
	}
	else
		State.m_Vel = (Target - State.m_Last) / Dt;
	// never far from the truth
	const float MaxErr = 12.0f;
	if(length(State.m_Err) > MaxErr)
		State.m_Err = normalize(State.m_Err) * MaxErr;
	State.m_Err *= std::exp(-Dt / std::max(State.m_Tau, 0.001f));
	State.m_Last = Target;
	State.m_Time = Now;
	return Target + State.m_Err;
}

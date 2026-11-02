#include "prediction.h"

#include "fast_input.h"
#include "fx_renderer.h"
#include "xc_helpers.h"
#include "ui/feedback.h"
#include "ui/lang.h"

#include <engine/client.h>
#include <engine/shared/config.h>

#include <game/client/gameclient.h>

#include <algorithm>
#include <cmath>

using namespace XcHelpers;

void CXcPrediction::OnReset()
{
	m_OffsetValid = false;
	m_JitterMs = 0.0f;
	m_CorrectionRate = 0.0f;
	m_CorrectionSize = 0.0f;
	m_CorrectionsWindow = 0;
	m_WasFrozen = false;
}

void CXcPrediction::NoteSnapshot(int Tick, bool HasPredicted, vec2 Predicted, vec2 Server)
{
	const float Now = Client()->LocalTime();
	// jitter: how far the arrival of a snapshot is from the steady pace of the ticks
	const float Offset = Now - Tick / (float)Client()->GameTickSpeed();
	if(!m_OffsetValid)
	{
		m_OffsetAvg = Offset;
		m_OffsetValid = true;
	}
	const float Deviation = std::abs(Offset - m_OffsetAvg) * 1000.0f;
	m_OffsetAvg += (Offset - m_OffsetAvg) * 0.05f;
	// rises fast, falls slowly: a few late snapshots count for a while
	m_JitterMs = Deviation > m_JitterMs ? mix(m_JitterMs, Deviation, 0.3f) : mix(m_JitterMs, Deviation, 0.02f);

	// a correction: the server has your tee somewhere else than the prediction had it (the positions are sent
	// as whole units, so a unit and a half is still the same spot)
	if(HasPredicted)
	{
		const float Error = distance(Predicted, Server);
		if(Error > 1.5f && Error < 400.0f)
		{
			m_CorrectionsWindow++;
			m_CorrectionSize = mix(m_CorrectionSize, Error, 0.2f);
			CXcFastInput::NoteCorrection(); // the jump it makes melts away
		}
	}
}

void CXcPrediction::OnRender()
{
	const float Now = Client()->LocalTime();
	const bool Online = Client()->State() == IClient::STATE_ONLINE;
	// corrections per second over a sliding second
	if(Now - m_WindowStart >= 1.0f || Now < m_WindowStart)
	{
		m_CorrectionRate = mix(m_CorrectionRate, (float)m_CorrectionsWindow / std::max(Now - m_WindowStart, 1.0f), 0.5f);
		m_CorrectionsWindow = 0;
		m_WindowStart = Now;
	}

	// leaving or entering the freeze moves the prediction a lot: let it melt
	const int Local = GameClient()->m_Snap.m_LocalClientId;
	if(Local >= 0 && GameClient()->m_Snap.m_aCharacters[Local].m_Active)
	{
		const bool Frozen = GameClient()->m_aClients[Local].m_Predicted.m_FreezeEnd != 0;
		if(Frozen != m_WasFrozen && g_Config.m_XcPredSmartFreeze)
			CXcFastInput::NoteCorrection();
		m_WasFrozen = Frozen;
	}

	// automatic margin: enough to cover the jitter, a bit more while the prediction keeps being corrected
	if(Online && g_Config.m_XcPredAutoMargin)
	{
		static const int s_aBias[] = {-2, 0, 6};
		const int Preset = std::clamp(g_Config.m_XcPredPreset, 0, NUM_PRESETS - 1);
		const float Target = 4.0f + m_JitterMs * 1.6f + (m_CorrectionRate > 1.0f ? 4.0f : 0.0f) + s_aBias[Preset];
		m_TargetMargin = std::clamp((int)std::round(Target), 4, 80);
		// one millisecond at a time, twice a second: the prediction never jumps because of it
		if(Now >= m_NextMarginStep)
		{
			m_NextMarginStep = Now + 0.5f;
			if(g_Config.m_ClPredictionMargin < m_TargetMargin)
				g_Config.m_ClPredictionMargin++;
			else if(g_Config.m_ClPredictionMargin > m_TargetMargin)
				g_Config.m_ClPredictionMargin--;
		}
	}

	TurnOffTClientTwins();
	if(g_Config.m_XcPredGhost && (Online || Client()->State() == IClient::STATE_DEMOPLAYBACK))
		RenderGhosts();
}

void CXcPrediction::TurnOffTClientTwins()
{
	struct STwin
	{
		bool m_OursOn;
		int *m_pTheirs;
		const char *m_pName;
	};
	const STwin aTwins[] = {
		{g_Config.m_XcFastInput != 0, &g_Config.m_TcFastInput, "fast input"},
		{g_Config.m_XcFastInput != 0, &g_Config.m_TcFastInputOthers, "fast input for others"},
		{g_Config.m_XcPredGhost != 0, &g_Config.m_TcShowOthersGhosts, "ghosts"},
		{g_Config.m_XcPredAutoMargin != 0, &g_Config.m_TcSmoothPredictionMargin, "smooth prediction margin"},
	};
	char aNames[256] = "";
	for(const STwin &Twin : aTwins)
	{
		if(!Twin.m_OursOn || !*Twin.m_pTheirs)
			continue;
		*Twin.m_pTheirs = 0;
		if(aNames[0])
			str_append(aNames, ", ");
		str_append(aNames, XcUi::Tr(Twin.m_pName));
	}
	if(aNames[0])
	{
		char aText[384];
		str_format(aText, sizeof(aText), XcUi::Tr("Turned off in TClient, ours does it: %s"), aNames);
		XcUi::Toast(aText, XcUi::TOAST_INFO);
	}
}

// where the server has a tee: a soft ring, only when it is noticeably away from where the tee is drawn
void CXcPrediction::RenderGhosts()
{
	CFxRenderer &Fx = GameClient()->m_FxRenderer;
	const float Intra = Client()->IntraGameTick(g_Config.m_ClDummy);
	for(int i = 0; i < MAX_CLIENTS; i++)
	{
		const CGameClient::CSnapState::CCharacterInfo &Char = GameClient()->m_Snap.m_aCharacters[i];
		if(!Char.m_Active)
			continue;
		const bool Own = i == GameClient()->m_aLocalIds[0] || (Client()->DummyConnected() && i == GameClient()->m_aLocalIds[1]);
		if(Own && g_Config.m_XcPredGhost < 2)
			continue;
		const vec2 Server = mix(vec2(Char.m_Prev.m_X, Char.m_Prev.m_Y), vec2(Char.m_Cur.m_X, Char.m_Cur.m_Y), Intra);
		const vec2 Drawn = GameClient()->m_aClients[i].m_RenderPos;
		const float Dist = distance(Server, Drawn);
		// near the drawn tee it would only clutter
		const float Alpha = std::clamp((Dist - 8.0f) / 24.0f, 0.0f, 1.0f) * PlayerAlpha(GameClient(), i);
		if(Alpha <= 0.0f)
			continue;
		const ColorRGBA Color = EffectColor(GameClient(), 0, 0, i);
		Fx.AddWorldGlowing(FxShape::Ring(Server, vec2(26.0f, 26.0f), 2.0f, Color.WithAlpha(0.75f * Alpha)), 10.0f, 0.5f * Alpha);
		Fx.AddWorld(FxShape::Circle(Server, 3.0f, Color.WithAlpha(0.8f * Alpha)).Additive());
		// a thin line to the drawn tee, fading towards it
		const vec2 Dir = normalize(Drawn - Server);
		if(Dist > 40.0f)
			Fx.AddWorld(FxShape::Line(Server + Dir * 28.0f, Drawn - Dir * 30.0f, 1.2f, Color.WithAlpha(0.35f * Alpha)).Gradient(Color.WithAlpha(0.0f)));
	}
}

void CXcPrediction::ApplyPreset(int Preset)
{
	g_Config.m_XcPredPreset = Preset;
	switch(Preset)
	{
	case PRESET_RESPONSIVE:
		// your tee answers at once, others guessed by their real inputs
		g_Config.m_XcFastInput = 1;
		g_Config.m_XcFastInputAuto = 1;
		g_Config.m_XcFastInputSmooth = 35;
		g_Config.m_ClAntiPing = 1;
		g_Config.m_ClAntiPingPreInput = 1;
		g_Config.m_XcPredAutoMargin = 1;
		g_Config.m_XcPredSmartFreeze = 1;
		g_Config.m_TcRemoveAnti = 0;
		break;
	case PRESET_BALANCED:
		g_Config.m_XcFastInput = 1;
		g_Config.m_XcFastInputAuto = 1;
		g_Config.m_XcFastInputSmooth = 55;
		g_Config.m_ClAntiPing = 1;
		g_Config.m_ClAntiPingPreInput = 1;
		g_Config.m_XcPredAutoMargin = 1;
		g_Config.m_XcPredSmartFreeze = 1;
		g_Config.m_TcRemoveAnti = 1;
		break;
	case PRESET_STABLE:
		// calm picture on a jumpy connection: no guessing ahead, more margin, everything smoothed
		g_Config.m_XcFastInput = 0;
		g_Config.m_XcFastInputSmooth = 80;
		g_Config.m_ClAntiPing = 0;
		g_Config.m_ClAntiPingPreInput = 1;
		g_Config.m_XcPredAutoMargin = 1;
		g_Config.m_XcPredSmartFreeze = 1;
		g_Config.m_TcRemoveAnti = 1;
		break;
	}
}

int CXcPrediction::CurrentPreset()
{
	return std::clamp(g_Config.m_XcPredPreset, 0, (int)PRESET_CUSTOM);
}

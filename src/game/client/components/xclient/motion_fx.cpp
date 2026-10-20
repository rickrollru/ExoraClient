#include "motion_fx.h"

#include "fx_renderer.h"
#include "xc_helpers.h"

#include <base/math.h>

#include <engine/client.h>
#include <engine/shared/config.h>

#include <game/client/animstate.h>
#include <game/client/gameclient.h>

#include <algorithm>
#include <cmath>

using namespace XcHelpers;

static constexpr float PATH_KEEP = 0.25f; // seconds of positions kept for the blur and the trail

void CXcMotion::OnReset()
{
	for(auto &vPath : m_avTeePath)
		vPath.clear();
	m_vCrosshairPath.clear();
	m_LastCrosshairTime = -1.0f;
	m_FireHeld = false;
	m_KickTime = -100.0f;
}

vec2 CXcMotion::PathAt(const std::vector<SSample> &vPath, float Time)
{
	if(vPath.empty())
		return vec2(0.0f, 0.0f);
	if(Time <= vPath.front().m_Time)
		return vPath.front().m_Pos;
	for(size_t i = vPath.size() - 1; i > 0; i--)
	{
		const SSample &a = vPath[i - 1];
		const SSample &b = vPath[i];
		if(Time >= a.m_Time)
		{
			const float Span = b.m_Time - a.m_Time;
			return Span > 0.0f ? mix(a.m_Pos, b.m_Pos, std::clamp((Time - a.m_Time) / Span, 0.0f, 1.0f)) : b.m_Pos;
		}
	}
	return vPath.front().m_Pos;
}

void CXcMotion::Prune(std::vector<SSample> &vPath, float Time, float Keep)
{
	// keep one sample older than the window, so the path reaches its start
	size_t First = 0;
	while(First + 1 < vPath.size() && vPath[First + 1].m_Time < Time - Keep)
		First++;
	if(First > 0)
		vPath.erase(vPath.begin(), vPath.begin() + First);
}

void CXcMotion::OnRender()
{
	const bool InGame = Client()->State() == IClient::STATE_ONLINE || Client()->State() == IClient::STATE_DEMOPLAYBACK;
	if(!InGame || (!g_Config.m_XcMotionBlur && !g_Config.m_XcSpeedLines))
	{
		for(auto &vPath : m_avTeePath)
			vPath.clear();
		return;
	}
	const float Time = Client()->GlobalTime();
	for(int ClientId = 0; ClientId < MAX_CLIENTS; ClientId++)
	{
		std::vector<SSample> &vPath = m_avTeePath[ClientId];
		const float Alpha = GameClient()->m_Snap.m_aCharacters[ClientId].m_Active &&
					    ShowFor(GameClient(), ClientId, g_Config.m_XcMotionBlurSelf, g_Config.m_XcMotionBlurDummy, g_Config.m_XcMotionBlurOthers) ?
					    PlayerAlpha(GameClient(), ClientId) :
					    0.0f;
		if(Alpha <= 0.0f)
		{
			vPath.clear();
			continue;
		}
		const vec2 Pos = GameClient()->m_aClients[ClientId].m_RenderPos;
		// a teleport or a respawn must not draw a blur across the map
		if(!vPath.empty() && distance(vPath.back().m_Pos, Pos) > 200.0f)
			vPath.clear();
		if(vPath.empty() || Time > vPath.back().m_Time)
			vPath.push_back({Time, Pos});
		Prune(vPath, Time, PATH_KEEP);
		if(g_Config.m_XcSpeedLines)
			RenderSpeedLines(ClientId, Time, Alpha);
		if(g_Config.m_XcMotionBlur)
			RenderTeeBlur(ClientId, Time, Alpha);
	}
}

void CXcMotion::RenderTeeBlur(int ClientId, float Time, float Alpha)
{
	const std::vector<SSample> &vPath = m_avTeePath[ClientId];
	const float Shutter = 0.015f + 0.065f * g_Config.m_XcMotionBlurStrength / 100.0f;
	const vec2 Pos = vPath.back().m_Pos;
	const float Moved = distance(Pos, PathAt(vPath, Time - Shutter));
	if(Moved < 6.0f)
		return;
	const CGameClient::CClientData &Data = GameClient()->m_aClients[ClientId];
	const vec2 Dir = direction(Data.m_RenderCur.m_Angle / 256.0f);
	// more copies for a longer smear, so it stays smooth
	const int Num = std::clamp((int)(Moved / 3.0f), 3, 18);
	// the fade-in of the blur with speed: slow tees get almost nothing
	const float SpeedFade = std::clamp((Moved - 6.0f) / 24.0f, 0.0f, 1.0f);
	// far copies first, the nearest one is drawn last
	for(int i = Num; i >= 1; i--)
	{
		const float k = (float)i / Num;
		const vec2 GhostPos = PathAt(vPath, Time - Shutter * k);
		// many faint copies blend into a smooth smear instead of separate steps
		const float GhostAlpha = Alpha * SpeedFade * std::min(2.0f / Num, 0.4f) * (1.0f - k * 0.9f);
		RenderTools()->RenderTee(CAnimState::GetIdle(), &Data.m_RenderInfo, EMOTE_NORMAL, Dir, GhostPos, GhostAlpha);
	}
}

void CXcMotion::RenderSpeedLines(int ClientId, float Time, float Alpha)
{
	const vec2 Vel = CharacterVelocity(GameClient(), ClientId);
	const float Speed = length(Vel);
	// lines appear from about the speed of a fast fall and are full at twice that
	const float Amount = std::clamp((Speed - 700.0f) / 900.0f, 0.0f, 1.0f);
	if(Amount <= 0.0f)
		return;
	const vec2 Back = -Vel / Speed;
	const vec2 Side(-Back.y, Back.x);
	const vec2 Pos = GameClient()->m_aClients[ClientId].m_RenderPos;
	const ColorRGBA Color(1.0f, 1.0f, 1.0f, Alpha * Amount * g_Config.m_XcSpeedLinesAlpha / 100.0f);
	std::vector<SFxShape> vShapes;
	constexpr int LANES = 6;
	for(int Lane = 0; Lane < LANES; Lane++)
	{
		// every lane streams backwards with its own phase and offset
		const float Seed = Lane * 0.618f + ClientId * 0.31f;
		const float Phase = std::fmod(Time * (2.6f + Lane * 0.23f) + Seed, 1.0f);
		const float Offset = (Lane - (LANES - 1) * 0.5f) * 8.0f + std::sin(Seed * 9.0f) * 3.0f;
		const float Start = 22.0f + Phase * 70.0f;
		const float Length = (30.0f + Speed * 0.035f) * (0.6f + 0.4f * std::sin(Seed * 5.0f + 1.0f));
		const float Fade = std::sin(Phase * pi);
		const vec2 From = Pos + Back * Start + Side * Offset;
		vShapes.push_back(FxShape::Line(From, From + Back * Length, 1.6f, Color.WithMultipliedAlpha(0.55f * Fade)).Additive());
	}
	GameClient()->m_FxRenderer.Draw(vShapes, false);
}

void CXcMotion::RenderCrosshair(vec2 Center, vec2 TargetPos, float Alpha, float Scale, bool Local, const std::function<void(vec2, float, float)> &Draw)
{
	if(!g_Config.m_XcCrosshairMotion)
	{
		m_vCrosshairPath.clear();
		Draw(TargetPos, Alpha, Scale);
		return;
	}
	const float Time = Client()->GlobalTime();
	// on the screen: offsets from the center, so a moving camera does not smear the crosshair
	const vec2 Offset = TargetPos - Center;
	if(!m_vCrosshairPath.empty() && distance(m_vCrosshairPath.back().m_Pos, Offset) > 1500.0f)
		m_vCrosshairPath.clear();
	if(Time > m_LastCrosshairTime)
	{
		m_vCrosshairPath.push_back({Time, Offset});
		m_LastCrosshairTime = Time;
	}
	Prune(m_vCrosshairPath, Time, PATH_KEEP);

	// kick: pops bigger on a shot and springs back with a small bounce
	float Kick = 1.0f;
	if(g_Config.m_XcCrosshairKick)
	{
		const bool Fire = Local && (GameClient()->m_Controls.m_aInputData[g_Config.m_ClDummy].m_Fire & 1);
		if(Fire && !m_FireHeld)
			m_KickTime = Time;
		m_FireHeld = Fire;
		const float t = Time - m_KickTime;
		if(t >= 0.0f && t < 0.6f)
			Kick += 0.5f * g_Config.m_XcCrosshairKickStrength / 100.0f * std::exp(-9.0f * t) * std::cos(14.0f * t);
	}

	if(g_Config.m_XcCrosshairTrail)
	{
		const FxGlow::CScope GlowScope(FxGlow::CROSSHAIR);
		const float TrailTime = 0.04f + 0.16f * g_Config.m_XcCrosshairTrailLength / 100.0f;
		const ColorRGBA Color = EffectColor(GameClient(), g_Config.m_XcCrosshairColorMode, g_Config.m_XcCrosshairColor, -1, 0.0f, Alpha);
		std::vector<SFxShape> vShapes;
		constexpr int PIECES = 14;
		vec2 Prev = Offset;
		for(int i = 1; i <= PIECES; i++)
		{
			const float k = (float)i / PIECES;
			const vec2 Next = PathAt(m_vCrosshairPath, Time - TrailTime * k);
			if(distance(Prev, Next) > 0.5f)
			{
				const float Width = mix(3.5f, 0.6f, k) * Scale;
				CFxRenderer::AppendGlowing(vShapes, FxShape::Line(Center + Prev, Center + Next, Width, Color.WithMultipliedAlpha((1.0f - k) * 0.8f)), 4.0f * Scale, 0.4f);
			}
			Prev = Next;
		}
		GameClient()->m_FxRenderer.Draw(vShapes, false);
	}

	if(g_Config.m_XcCrosshairBlur)
	{
		const float Shutter = 0.01f + 0.05f * g_Config.m_XcCrosshairBlurStrength / 100.0f;
		const float Moved = distance(Offset, PathAt(m_vCrosshairPath, Time - Shutter));
		if(Moved > 4.0f)
		{
			const int Num = std::clamp((int)(Moved / 3.0f), 3, 16);
			const float SpeedFade = std::clamp((Moved - 4.0f) / 20.0f, 0.0f, 1.0f);
			for(int i = Num; i >= 1; i--)
			{
				const float k = (float)i / Num;
				Draw(Center + PathAt(m_vCrosshairPath, Time - Shutter * k), Alpha * SpeedFade * std::min(2.4f / Num, 0.4f) * (1.0f - k * 0.9f), Scale * Kick);
			}
		}
	}
	Draw(TargetPos, Alpha, Scale * Kick);
}

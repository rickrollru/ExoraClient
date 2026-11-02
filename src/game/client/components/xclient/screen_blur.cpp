#include "screen_blur.h"

#include "xc_helpers.h"

#include <engine/client.h>
#include <engine/graphics.h>
#include <engine/shared/config.h>
#include <engine/xc_render.h>

#include <game/client/gameclient.h>

#include <algorithm>

using namespace XcHelpers;

// the time the shutter is open at 100 %: long enough to see at speed, short enough to keep the blocks readable
static constexpr float EXPOSURE = 0.022f;

bool CXcScreenBlur::Active() const
{
	return g_Config.m_XcMotionBlur && GameClient()->m_FxRenderer.ModuleActive() && !g_Config.m_Xc3d;
}

void CXcScreenBlur::OnReset()
{
	m_CameraValid = false;
	for(bool &Valid : m_aTeeValid)
		Valid = false;
}

// the velocity of a point from frame to frame, smoothed a little against uneven frames; a jump (a teleport,
// a respawn, a camera switch) starts again from rest
vec2 CXcScreenBlur::Track(vec2 Pos, vec2 &Prev, vec2 &Vel, bool &Valid, float Dt)
{
	if(!Valid || Dt <= 0.0f || distance(Pos, Prev) > 500.0f)
	{
		Valid = true;
		Prev = Pos;
		Vel = vec2(0.0f, 0.0f);
		return Vel;
	}
	const vec2 Now = (Pos - Prev) / Dt;
	Prev = Pos;
	// about 25 ms of smoothing, the same at any frame rate
	const float k = 1.0f - std::exp(-Dt / 0.025f);
	Vel += (Now - Vel) * k;
	return Vel;
}

// slow motion is not blurred: it fades in above the speed threshold
float CXcScreenBlur::Fade(float Speed) const
{
	const float Min = (float)g_Config.m_XcMotionBlurMin;
	return std::clamp((Speed - Min) / std::max(Min * 0.6f, 200.0f), 0.0f, 1.0f);
}

void CXcScreenBlur::OnRender()
{
	const bool InGame = Client()->State() == IClient::STATE_ONLINE || Client()->State() == IClient::STATE_DEMOPLAYBACK;
	if(!InGame || !Active())
	{
		OnReset();
		return;
	}
	const float Dt = Client()->RenderFrameTime();
	const vec2 Camera = GameClient()->m_Camera.m_Center;
	const vec2 CameraVel = Track(Camera, m_CameraPrev, m_CameraVel, m_CameraValid, Dt);
	for(int i = 0; i < MAX_CLIENTS; i++)
	{
		if(!GameClient()->m_Snap.m_aCharacters[i].m_Active)
		{
			m_aTeeValid[i] = false;
			continue;
		}
		Track(GameClient()->m_aClients[i].m_RenderPos, m_aTeePrev[i], m_aTeeVel[i], m_aTeeValid[i], Dt);
	}

	const CScreenRect Screen = Graphics()->MapScreenToWorld(Camera.x, Camera.y, 100.0f, 100.0f, 100.0f, 0.0f, 0.0f, Graphics()->ScreenAspect(), GameClient()->m_Camera.m_Zoom);
	const vec2 WorldSize = Screen.m_BottomRight - Screen.m_TopLeft;
	m_PixelsPerUnit = WorldSize.x > 0.0f ? Graphics()->ScreenWidth() / WorldSize.x : 1.0f;

	// the map: smeared along the camera motion
	m_WorldSmear = vec2(0.0f, 0.0f);
	if(g_Config.m_XcMotionBlurWorld <= 0 || WorldSize.x <= 0.0f || WorldSize.y <= 0.0f)
		return;
	const float Speed = length(CameraVel);
	const float Amount = Fade(Speed);
	if(Amount <= 0.0f)
		return;
	vec2 Smear = CameraVel * (EXPOSURE * g_Config.m_XcMotionBlurWorld / 100.0f * Amount);
	// never more than a tenth of the screen
	const float Max = WorldSize.x * 0.1f;
	if(length(Smear) > Max)
		Smear = normalize(Smear) * Max;
	if(length(Smear) * m_PixelsPerUnit < 1.0f)
		return;
	m_WorldSmear = Smear * m_PixelsPerUnit;
	static const int s_aSamples[] = {8, 16, 32};
	XcRender::SMotionBlurSettings Settings;
	Settings.m_DirX = Smear.x / WorldSize.x;
	Settings.m_DirY = Smear.y / WorldSize.y;
	Settings.m_Samples = s_aSamples[std::clamp(g_Config.m_XcMotionBlurQuality, 0, 2)];
	Graphics()->XcMotionBlur(Settings);
}

vec2 CXcScreenBlur::TeeSmear(int ClientId) const
{
	if(!Active() || !in_range(ClientId, MAX_CLIENTS - 1) || !m_aTeeValid[ClientId] || g_Config.m_XcMotionBlurStrength <= 0)
		return vec2(0.0f, 0.0f);
	if(!ShowFor(GameClient(), ClientId, g_Config.m_XcMotionBlurSelf, g_Config.m_XcMotionBlurDummy, g_Config.m_XcMotionBlurOthers))
		return vec2(0.0f, 0.0f);
	// on the screen a tee moves by its speed minus the camera's
	const vec2 Rel = m_aTeeVel[ClientId] - m_CameraVel;
	const float Amount = Fade(length(Rel));
	vec2 Smear = Rel * (EXPOSURE * 2.0f * g_Config.m_XcMotionBlurStrength / 100.0f * Amount) * m_PixelsPerUnit;
	const float Max = 64.0f * 1.6f * m_PixelsPerUnit;
	if(length(Smear) > Max)
		Smear = normalize(Smear) * Max;
	return length(Smear) < 1.5f ? vec2(0.0f, 0.0f) : Smear;
}

void CXcScreenBlur::CForegroundBegin::OnRender()
{
	CXcScreenBlur &Blur = GameClient()->m_XcScreenBlur;
	Blur.m_ForegroundCaptured = false;
	if(!Blur.Active() || Blur.m_WorldSmear == vec2(0.0f, 0.0f))
		return;
	// the whole screen in the world
	const vec2 Camera = GameClient()->m_Camera.m_Center;
	const CScreenRect Screen = Graphics()->MapScreenToWorld(Camera.x, Camera.y, 100.0f, 100.0f, 100.0f, 0.0f, 0.0f, Graphics()->ScreenAspect(), GameClient()->m_Camera.m_Zoom);
	Graphics()->MapScreen(Screen);
	const float aWorldRect[4] = {Screen.m_TopLeft.x, Screen.m_TopLeft.y, Screen.m_BottomRight.x, Screen.m_BottomRight.y};
	Graphics()->XcCaptureBegin(aWorldRect);
	Blur.m_ForegroundCaptured = true;
}

void CXcScreenBlur::CForegroundEnd::OnRender()
{
	CXcScreenBlur &Blur = GameClient()->m_XcScreenBlur;
	if(!Blur.m_ForegroundCaptured)
		return;
	Blur.m_ForegroundCaptured = false;
	XcRender::SGlowSettings Settings;
	Settings.m_DoGlow = false;
	Settings.m_RimWidth = 0.0f;
	Settings.m_Strength = 0.0f;
	Settings.m_MotionX = Blur.m_WorldSmear.x;
	Settings.m_MotionY = Blur.m_WorldSmear.y;
	Graphics()->XcCaptureEnd(Settings);
}

#include "spec_camera.h"

#include <engine/shared/config.h>

#include <algorithm>
#include <cmath>

vec2 CXcSpecCamera::Apply(vec2 Target, vec2 Mouse, float Dt, bool Active, bool FreeView)
{
	if(!Active || g_Config.m_XcSpecSmooth <= 0)
	{
		m_Valid = false;
		return Target;
	}
	// a jump of the watched player (a teleport, a respawn) is followed at once, not flown through
	const vec2 Base = Target - Mouse;
	const bool SameMode = m_Valid && FreeView == m_LastFreeView;
	if(!m_Valid || (!FreeView && SameMode && distance(Base, m_LastTarget - m_LastMouse) > 900.0f))
	{
		m_Valid = true;
		m_Pos = Target;
		m_Vel = vec2(0.0f, 0.0f);
		m_LastFreeView = FreeView;
		m_LastMouse = Mouse;
		m_LastTarget = Target;
		return Target;
	}
	// the mouse moves the view as it is: no lag, no glide, however fast it goes (switching between the free view
	// and following a player changes what the mouse part means, that switch glides instead)
	if(SameMode)
		m_Pos += Mouse - m_LastMouse;
	m_LastFreeView = FreeView;
	m_LastMouse = Mouse;
	m_LastTarget = Target;
	Dt = std::clamp(Dt, 0.0f, 0.1f);
	// the time the camera needs to catch up: 0.05 s (barely) .. 0.6 s (very soft)
	float SmoothTime = 0.05f + 0.55f * std::pow(g_Config.m_XcSpecSmooth / 100.0f, 1.5f);
	// the free view is moved by the mouse itself and glides as a whole, a bit shorter: a long glide there feels
	// like a broken mouse sensitivity (the view comes late)
	if(FreeView)
		SmoothTime = std::min(SmoothTime, 0.04f + 0.16f * g_Config.m_XcSpecSmooth / 100.0f);
	// a critically damped spring, exact for any frame time (the same glide at 30 and at 500 fps)
	const float Omega = 2.0f / SmoothTime;
	const float x = Omega * Dt;
	const float Exp = std::exp(-x);
	const vec2 Change = m_Pos - Target;
	const vec2 Temp = (m_Vel + Change * Omega) * Dt;
	m_Vel = (m_Vel - Temp * Omega) * Exp;
	m_Pos = Target + (Change + Temp) * Exp;

	return m_Pos;
}

#include "spec_camera.h"

#include <engine/shared/config.h>

#include <algorithm>
#include <cmath>

vec2 CXcSpecCamera::Apply(vec2 Target, float Dt, bool Active, bool FreeView)
{
	if(!Active || g_Config.m_XcSpecSmooth <= 0)
	{
		m_Valid = false;
		return Target;
	}
	// a jump of the watched player (a teleport, a respawn) is followed at once, not flown through; the free view
	// moved by the mouse never jumps, however fast the mouse goes
	if(!m_Valid || (!FreeView && distance(Target, m_LastTarget) > 900.0f))
	{
		m_Valid = true;
		m_Pos = Target;
		m_Vel = vec2(0.0f, 0.0f);
	}
	m_LastTarget = Target;
	Dt = std::clamp(Dt, 0.0f, 0.1f);
	// the time the camera needs to catch up: 0.05 s (barely) .. 0.6 s (very soft)
	const float SmoothTime = 0.05f + 0.55f * std::pow(g_Config.m_XcSpecSmooth / 100.0f, 1.5f);
	// a critically damped spring, exact for any frame time (the same glide at 30 and at 500 fps)
	const float Omega = 2.0f / SmoothTime;
	const float x = Omega * Dt;
	const float Exp = 1.0f / (1.0f + x + 0.48f * x * x + 0.235f * x * x * x);
	const vec2 Change = m_Pos - Target;
	const vec2 Temp = (m_Vel + Change * Omega) * Dt;
	m_Vel = (m_Vel - Temp * Omega) * Exp;
	m_Pos = Target + (Change + Temp) * Exp;
	// never too far behind: it is pulled along instead
	const float MaxLag = 1500.0f;
	if(distance(m_Pos, Target) > MaxLag)
		m_Pos = Target + normalize(m_Pos - Target) * MaxLag;
	return m_Pos;
}

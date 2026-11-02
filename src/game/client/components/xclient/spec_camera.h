#ifndef GAME_CLIENT_COMPONENTS_XCLIENT_SPEC_CAMERA_H
#define GAME_CLIENT_COMPONENTS_XCLIENT_SPEC_CAMERA_H

#include <base/vmath.h>

// A smooth camera while spectating (xc_spec_smooth): the view follows the camera the game wants (the free view
// moved by the mouse, or the watched player) on a critically damped spring, so it glides, speeds up and slows
// down softly instead of jerking. Only the view: the saved camera and the mouse stay the game's own.
class CXcSpecCamera
{
public:
	// Target: where the game puts the camera this frame. Returns where to show it.
	// FreeView: the camera moved by the mouse (never jumps, only a fast player follow may jump)
	vec2 Apply(vec2 Target, float Dt, bool Active, bool FreeView);
	void Reset() { m_Valid = false; }

private:
	bool m_Valid = false;
	vec2 m_Pos = vec2(0.0f, 0.0f);
	vec2 m_Vel = vec2(0.0f, 0.0f);
	vec2 m_LastTarget = vec2(0.0f, 0.0f);
};

#endif

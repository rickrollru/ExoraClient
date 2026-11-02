#ifndef GAME_CLIENT_COMPONENTS_XCLIENT_SCREEN_BLUR_H
#define GAME_CLIENT_COMPONENTS_XCLIENT_SCREEN_BLUR_H

#include <base/vmath.h>

#include <engine/shared/protocol.h>

#include <game/client/component.h>

// Real motion blur (xc_motion_blur), by the render module:
// - the map: right after the blocks are drawn, the frame is smeared along the motion of the camera, as a camera
//   with an open shutter sees it (the map moves on the screen only because the camera moves);
// - the tees: each tee is captured and drawn back smeared along its own motion on the screen (its speed minus
//   the camera's), see CTeeGlow. The tee the camera follows stays sharp, the world flies past it.
// Without the module (Vulkan, old OpenGL) the old copies behind the tees are drawn instead (CXcMotion).
class CXcScreenBlur : public CComponent
{
public:
	int Sizeof() const override { return sizeof(*this); }
	void OnRender() override;
	void OnReset() override;

	// the module draws the blur (the map and the tees)
	bool Active() const;
	// the smear of a tee on the screen in pixels (top left origin), zero when it has none
	vec2 TeeSmear(int ClientId) const;

	// the map layers in front of the tees are drawn after them: they are captured on their own and drawn back
	// smeared, so the tees under them are not blurred twice
	class CForegroundBegin : public CComponent
	{
	public:
		int Sizeof() const override { return sizeof(*this); }
		void OnRender() override;
	} m_ForegroundBegin;
	class CForegroundEnd : public CComponent
	{
	public:
		int Sizeof() const override { return sizeof(*this); }
		void OnRender() override;
	} m_ForegroundEnd;

private:
	friend class CForegroundBegin;
	friend class CForegroundEnd;
	vec2 m_WorldSmear = vec2(0.0f, 0.0f); // the smear of the map this frame in pixels
	bool m_ForegroundCaptured = false;
	vec2 m_CameraPrev = vec2(0.0f, 0.0f);
	vec2 m_CameraVel = vec2(0.0f, 0.0f);
	bool m_CameraValid = false;
	vec2 m_aTeePrev[MAX_CLIENTS];
	vec2 m_aTeeVel[MAX_CLIENTS];
	bool m_aTeeValid[MAX_CLIENTS] = {};
	float m_PixelsPerUnit = 1.0f;

	static vec2 Track(vec2 Pos, vec2 &Prev, vec2 &Vel, bool &Valid, float Dt);
	float Fade(float Speed) const;
};

#endif

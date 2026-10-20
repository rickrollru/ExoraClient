#ifndef GAME_CLIENT_COMPONENTS_XCLIENT_MOTION_FX_H
#define GAME_CLIENT_COMPONENTS_XCLIENT_MOTION_FX_H

#include <base/vmath.h>

#include <engine/shared/protocol.h>

#include <game/client/component.h>

#include <functional>
#include <vector>

// Motion effects, purely visual:
// - tees: motion blur (fading copies along the path of the last hundredths of a second) and speed lines;
// - crosshair: motion blur, a glowing trail and a kick on every shot (called by CHud::RenderCursor).
// The blur follows the real drawn positions over time, so it looks the same at any frame rate.
class CXcMotion : public CComponent
{
public:
	int Sizeof() const override { return sizeof(*this); }
	void OnReset() override;
	void OnRender() override;

	// draws the crosshair with its motion effects; Draw(Pos, Alpha, Scale) draws one crosshair
	// in the current mapping (the world around Center)
	void RenderCrosshair(vec2 Center, vec2 TargetPos, float Alpha, float Scale, bool Local, const std::function<void(vec2, float, float)> &Draw);

private:
	struct SSample
	{
		float m_Time;
		vec2 m_Pos;
	};
	// position on the path Ago seconds back, from the newest samples
	static vec2 PathAt(const std::vector<SSample> &vPath, float Time);
	static void Prune(std::vector<SSample> &vPath, float Time, float Keep);

	std::vector<SSample> m_avTeePath[MAX_CLIENTS];
	std::vector<SSample> m_vCrosshairPath; // offsets from the screen center
	float m_LastCrosshairTime = -1.0f;
	bool m_FireHeld = false;
	float m_KickTime = -100.0f;

	void RenderTeeBlur(int ClientId, float Time, float Alpha);
	void RenderSpeedLines(int ClientId, float Time, float Alpha);
};

#endif

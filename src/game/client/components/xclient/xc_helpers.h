#ifndef GAME_CLIENT_COMPONENTS_XCLIENT_XC_HELPERS_H
#define GAME_CLIENT_COMPONENTS_XCLIENT_XC_HELPERS_H

#include <base/color.h>
#include <base/vmath.h>

class CGameClient;
class IGraphics;

namespace XcHelpers
{

	enum class ETarget
	{
		SELF,
		DUMMY,
		OTHER,
	};

	// Who this client is from our point of view: us, our dummy or someone else.
	ETarget TargetOf(const CGameClient *pGameClient, int ClientId);

	// Picks the setting matching the target ("mine / dummy / others" toggles).
	bool ShowFor(const CGameClient *pGameClient, int ClientId, int Self, int Dummy, int Others);

	// Alpha the game uses for this player (players of other teams are faded or hidden).
	float PlayerAlpha(const CGameClient *pGameClient, int ClientId);

	// Velocity of a rendered character in world units per second.
	vec2 CharacterVelocity(const CGameClient *pGameClient, int ClientId);

	// Smooth factor for exponential smoothing that does not depend on the frame rate.
	float SmoothFactor(float Speed, float Dt);

	ColorRGBA HslConfigColor(unsigned PackedHsl);
	ColorRGBA RainbowColor(float Hue, float Alpha);

	vec2 Rotate(vec2 Point, vec2 Center, float Angle);

	// Ellipse ring with a soft glow on both sides. Must be called between QuadsBegin and QuadsEnd
	// with no texture set. Squash < 1 flattens the ring vertically.
	void DrawGlowRing(IGraphics *pGraphics, vec2 Center, float Radius, float Squash, float Thickness, float Glow, ColorRGBA Color, float Rotation = 0.0f);

	// Filled circle with a soft fading edge. Same requirements as DrawGlowRing.
	void DrawSoftCircle(IGraphics *pGraphics, vec2 Center, float Radius, float Feather, ColorRGBA Color);

} // namespace XcHelpers

#endif

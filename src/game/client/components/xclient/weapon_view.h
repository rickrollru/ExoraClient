#ifndef GAME_CLIENT_COMPONENTS_XCLIENT_WEAPON_VIEW_H
#define GAME_CLIENT_COMPONENTS_XCLIENT_WEAPON_VIEW_H

#include <base/vmath.h>

class CGameClient;

// How the weapon sits in the hand and how the hammer swings (from the Flower client's
// ViewModel and SwingAnimation). Only the picture: the hit itself is the server's.
namespace XcWeapon
{
	enum
	{
		SWING_DEFAULT = 0,
		SWING_WIDE, // a bigger arc
		SWING_SPIN, // a full turn
		SWING_SMOOTH, // soft start and end
		SWING_STAB, // a push forward instead of an arc
		SWING_BOUNCE, // swings past the hit and springs back
	};
	// true when the settings apply to this player
	bool Applies(const CGameClient *pGameClient, int ClientId);
	// how fast the hammer animation runs, 1 = normal
	float SwingSpeed(const CGameClient *pGameClient, int ClientId);
	// the turn of the hammer (in turns, like the animation's attach angle); Progress 0..1 of the swing
	float SwingAngle(const CGameClient *pGameClient, int ClientId, float AnimAngle, float Progress);
	// extra push of the hammer towards the aim during the swing (stab style)
	vec2 SwingPush(const CGameClient *pGameClient, int ClientId, vec2 Direction, float Progress);
	// where the weapon moves in the hand and its size
	vec2 Offset(const CGameClient *pGameClient, int ClientId, vec2 Direction);
	float Scale(const CGameClient *pGameClient, int ClientId);
}

#endif

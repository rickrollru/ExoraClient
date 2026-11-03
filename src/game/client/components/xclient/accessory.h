#ifndef GAME_CLIENT_COMPONENTS_XCLIENT_ACCESSORY_H
#define GAME_CLIENT_COMPONENTS_XCLIENT_ACCESSORY_H

#include <base/color.h>
#include <base/vmath.h>

#include <engine/shared/protocol.h>

#include <game/client/component.h>

// Cosmetic accessory drawn on top of the tee: cone hat ("China Hat"), halo or crown.
// It tilts with the running speed and springs on jumps and landings.
class CAccessory : public CComponent
{
public:
	enum
	{
		ACCESSORY_OFF = 0,
		ACCESSORY_CONE,
		ACCESSORY_HALO,
		ACCESSORY_CROWN,
		ACCESSORY_HORNS,
		ACCESSORY_SPROUT,
		ACCESSORY_STARS,
		ACCESSORY_FLAME,
		ACCESSORY_BOW,
		ACCESSORY_SIGN,
		ACCESSORY_CROSS,
	};

	int Sizeof() const override { return sizeof(*this); }
	void OnRender() override;
	void OnReset() override;
	void OnMapLoad() override;
	// the accessory on a tee drawn anywhere (the tee preview in the menu), with the current screen mapping
	void RenderPreview(vec2 TeePos, float TeeSize, float Alpha, float Time);

private:
	struct SState
	{
		bool m_Valid = false;
		vec2 m_Pos = vec2(0.0f, 0.0f);
		vec2 m_LastVel = vec2(0.0f, 0.0f);
		float m_Tilt = 0.0f;
		float m_Spring = 0.0f; // vertical offset of the accessory relative to the head
		float m_SpringVel = 0.0f;
	};
	SState m_aStates[MAX_CLIENTS];
	float m_Time = 0.0f;

	void UpdateState(int ClientId, vec2 Pos, float Dt);
	ColorRGBA BaseColor(float Offset, float Alpha) const;
	int m_ColorClient = -1; // player being drawn, for the skin color mode
	void RenderCone(vec2 Head, float Scale, float Tilt, float Squash, float Alpha) const;
	void RenderHalo(vec2 Head, float Scale, float Tilt, float Squash, float Alpha) const;
	void RenderCrown(vec2 Head, float Scale, float Tilt, float Squash, float Alpha) const;
	void RenderHorns(vec2 Head, float Scale, float Tilt, float Squash, float Alpha) const;
	void RenderSprout(vec2 Head, float Scale, float Tilt, float Squash, float Alpha, float Spring) const;
	void RenderStars(vec2 Head, float Scale, float Tilt, float Squash, float Alpha) const;
	void RenderFlame(vec2 Head, float Scale, float Tilt, float Squash, float Alpha, vec2 Vel) const;
	void RenderBow(vec2 Head, float Scale, float Tilt, float Squash, float Alpha) const;
	void RenderSign(vec2 Head, float Scale, float Tilt, float Squash, float Alpha, bool Frozen) const;
	void RenderCross(vec2 Head, float Scale, float Tilt, float Squash, float Alpha) const;
};

#endif

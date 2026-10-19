#ifndef GAME_CLIENT_COMPONENTS_XCLIENT_CAPE_H
#define GAME_CLIENT_COMPONENTS_XCLIENT_CAPE_H

#include <base/vmath.h>

#include <engine/shared/protocol.h>

#include <game/client/component.h>

// A cape or a scarf behind the tee. The cloth is a chain of points (verlet) hanging from the neck:
// it falls with gravity, trails behind when the tee moves and waves a little in the wind.
// Purely visual, only this client sees it.
class CXcCape : public CComponent
{
public:
	int Sizeof() const override { return sizeof(*this); }
	void OnRender() override;
	void OnReset() override;

	enum
	{
		CAPE_OFF = 0,
		CAPE_CLOTH,
		CAPE_SCARF,
	};

	// draws a cape that hangs still, for previews in the menu (Pos is the tee center)
	void RenderPreview(vec2 Pos, float TeeSize, bool LookingLeft, float Alpha, float Time);

private:
	static constexpr int NUM_POINTS = 10;
	struct SState
	{
		bool m_Valid = false;
		vec2 m_aPoints[NUM_POINTS];
		vec2 m_aPrev[NUM_POINTS];
		float m_Accumulator = 0.0f;
		float m_Back = 1.0f; // +1 when the back of the tee is on the right, eased while turning
	};
	SState m_aStates[MAX_CLIENTS];

	bool LooksLeft(int ClientId) const;
	vec2 Anchor(vec2 Pos, float Scale, float Back) const;
	void Simulate(SState &State, vec2 Anchor, float Scale, float Dt, float Time, bool Collide) const;
	void RenderCloth(const vec2 *pPoints, float Scale, float Back, int ClientId, float Alpha) const;
};

#endif

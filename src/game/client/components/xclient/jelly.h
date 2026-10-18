#ifndef GAME_CLIENT_COMPONENTS_XCLIENT_JELLY_H
#define GAME_CLIENT_COMPONENTS_XCLIENT_JELLY_H

#include <base/vmath.h>

#include <engine/graphics.h>
#include <engine/shared/protocol.h>

#include <game/client/component.h>

// "Jelly tee": the tee squashes when it lands, stretches when it jumps and wobbles back.
// Only the picture is scaled: CPlayers draws the tee between Begin and End with a stretched view.
class CXcJelly : public CComponent
{
public:
	int Sizeof() const override { return sizeof(*this); }
	void OnReset() override;
	void OnMapLoad() override;

	// true when the view was changed, then End must be called after drawing the tee
	bool Begin(int ClientId);
	void End();

private:
	struct SState
	{
		bool m_Valid = false;
		vec2 m_LastVel = vec2(0.0f, 0.0f);
		float m_Squash = 0.0f; // > 0 squashed, < 0 stretched
		float m_SquashVel = 0.0f;
		float m_LastTime = 0.0f;
	};
	SState m_aStates[MAX_CLIENTS];
	CScreenRect m_SavedScreen = CScreenRect(0.0f, 0.0f, 0.0f, 0.0f);
};

#endif

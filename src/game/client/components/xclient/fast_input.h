#ifndef GAME_CLIENT_COMPONENTS_XCLIENT_FAST_INPUT_H
#define GAME_CLIENT_COMPONENTS_XCLIENT_FAST_INPUT_H

#include <base/vmath.h>

#include <engine/shared/protocol.h>

#include <game/client/component.h>

#include <cmath>

// Exora input (xc_fast_input): your tee on the screen answers your keys sooner. Only the picture: the client
// already predicts your tee with the keys you hold, this shows that prediction a little further ahead, so a
// press shows at once instead of on the next game tick. What goes to the server does not change.
// Built on the fast input of TClient (the same prediction), with:
// - an automatic amount: one game tick, lowered by itself when the predictions keep being corrected;
// - smoothed corrections: when a new key press changes the prediction ahead, the tee does not snap a few
//   pixels, the difference melts away in a few hundredths of a second;
// - separate choice for the dummy and other players.
class CXcFastInput : public CComponent
{
public:
	int Sizeof() const override { return sizeof(*this); }
	void OnRender() override;
	void OnReset() override;

	// the switches the prediction code reads: ours when on, else TClient's
	static bool On();
	// the whole amount, and the part the game physics is run for (beyond two ticks the tee only glides on)
	static int AmountMs();
	static int PhysicsMs();
	// for other players: only with Exora input while the server sends their inputs ahead (pre-input), one tick
	static bool Others();
	static int OthersMs();
	// the server sent the inputs of other players ahead
	static void NotePreInput(float Now);
	// a new key press was just put into the prediction (called from the client before it predicts again)
	static void NoteNewInput();

	// the position to draw a predicted tee at, with the corrections smoothed
	vec2 Smooth(int ClientId, vec2 Target);
	// beyond the physics horizon: the tee glides on with its speed, stopping at walls
	vec2 Extend(int ClientId, vec2 Pos);
	int AutoMs() const { return (int)std::round(m_AutoMs); }

private:
	struct SState
	{
		bool m_Valid = false;
		vec2 m_Last = vec2(0.0f, 0.0f);
		vec2 m_Vel = vec2(0.0f, 0.0f);
		vec2 m_Err = vec2(0.0f, 0.0f);
		float m_Time = 0.0f;
	};
	SState m_aState[MAX_CLIENTS];
	float m_AutoMs = 20.0f;
	float m_CorrectionAvg = 0.0f; // how far the prediction ahead moves on new presses, units
};

#endif

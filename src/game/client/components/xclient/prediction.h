#ifndef GAME_CLIENT_COMPONENTS_XCLIENT_PREDICTION_H
#define GAME_CLIENT_COMPONENTS_XCLIENT_PREDICTION_H

#include <base/vmath.h>

#include <game/client/component.h>

// Prediction helpers, only the picture (what goes to the server stays the same):
// - measures how the prediction does: on every snapshot the predicted spot of your tee is compared with the one
//   the server sent (corrections and their size), and how unevenly the snapshots arrive (jitter);
// - automatic prediction margin (xc_pred_auto_margin): from that jitter and the corrections, changed slowly, so
//   a steady connection gets the least delay and a jumpy one stops snapping;
// - corrections of your tee (a wrong guess, leaving the freeze) melt away instead of snapping (with CXcFastInput);
// - ghosts in our style (xc_pred_ghost): a ring where the server has a tee, shown only when it is far from where
//   it is drawn, with a thin line between;
// - presets for all the prediction settings at once.
class CXcPrediction : public CComponent
{
public:
	enum
	{
		PRESET_RESPONSIVE = 0,
		PRESET_BALANCED,
		PRESET_STABLE,
		NUM_PRESETS,
		PRESET_CUSTOM = NUM_PRESETS,
	};

	int Sizeof() const override { return sizeof(*this); }
	void OnRender() override;
	void OnReset() override;

	// called by the game client when a snapshot comes, before the prediction is redone
	void NoteSnapshot(int Tick, bool HasPredicted, vec2 Predicted, vec2 Server);

	float JitterMs() const { return m_JitterMs; }
	float CorrectionsPerSecond() const { return m_CorrectionRate; }
	float CorrectionSize() const { return m_CorrectionSize; }
	int TargetMargin() const { return m_TargetMargin; }

	static void ApplyPreset(int Preset);
	static int CurrentPreset();

private:
	float m_JitterMs = 0.0f;
	float m_OffsetAvg = 0.0f;
	bool m_OffsetValid = false;
	float m_CorrectionRate = 0.0f;
	float m_CorrectionSize = 0.0f;
	int m_CorrectionsWindow = 0;
	float m_WindowStart = 0.0f;
	float m_NextMarginStep = 0.0f;
	int m_TargetMargin = 10;
	bool m_WasFrozen = false;

	void RenderGhosts();
};

#endif

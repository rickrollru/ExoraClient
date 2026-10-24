#ifndef GAME_CLIENT_COMPONENTS_XCLIENT_CAPE_H
#define GAME_CLIENT_COMPONENTS_XCLIENT_CAPE_H

#include "ui/backgrounds.h"

#include <base/color.h>
#include <base/vmath.h>

#include <engine/shared/protocol.h>

#include <game/client/component.h>

class CXcCape;

// The knot of the scarf is wrapped around the tee, so it is drawn after the players.
class CXcCapeFront : public CComponent
{
public:
	int Sizeof() const override { return sizeof(*this); }
	void OnRender() override;
	CXcCape *m_pCape = nullptr;
};

// A cape and a knitted scarf on the tee. The cape is a cloth grid (verlet) hanging from the shoulders:
// it falls with gravity, trails behind, flaps on jumps, streams away from the hook and lies on the ground.
// The scarf is wrapped around the neck with one or two ends flying behind.
// Purely visual, only this client sees them.
class CXcCape : public CComponent
{
public:
	CXcCape();
	int Sizeof() const override { return sizeof(*this); }
	void OnInit() override;
	void OnShutdown() override;
	void OnRender() override;
	void OnReset() override;

	enum
	{
		SHAPE_CLASSIC = 0,
		SHAPE_POINTED,
		SHAPE_RAGGED,
		SHAPE_ROYAL,
	};
	enum
	{
		PATTERN_PLAIN = 0,
		PATTERN_STRIPES,
		PATTERN_GRADIENT,
		PATTERN_PICTURE,
	};

	// previews in the menu (Pos is the tee center): the part behind the tee, then the part in front of it
	void RenderPreview(vec2 Pos, float TeeSize, bool LookingLeft, float Alpha, float Time);
	void RenderPreviewFront(vec2 Pos, float TeeSize, float Alpha);

	CXcBackgrounds &Pictures() { return m_Pictures; }
	CXcCapeFront m_Front;

private:
	static constexpr int COLS = 5;
	static constexpr int ROWS = 10;
	static constexpr int TAIL_POINTS = 9;
	static constexpr int MAX_TAILS = 2;

	struct SCloth
	{
		bool m_Valid = false;
		vec2 m_aPoints[ROWS][COLS];
		vec2 m_aPrev[ROWS][COLS];
		float m_Accumulator = 0.0f;
	};
	struct STail
	{
		bool m_Valid = false;
		vec2 m_aPoints[TAIL_POINTS];
		vec2 m_aPrev[TAIL_POINTS];
		float m_Accumulator = 0.0f;
	};
	struct SState
	{
		float m_Back = 1.0f; // +1 when the back of the tee is on the right, eased while turning
		vec2 m_LastPos = vec2(0.0f, 0.0f);
		vec2 m_Vel = vec2(0.0f, 0.0f); // smoothed speed of the tee
		bool m_HasLast = false;
		vec2 m_Pull = vec2(0.0f, 0.0f); // the hook pulls the tee this way
		SCloth m_Cloth;
		STail m_aTails[MAX_TAILS];
		// for the front pass of the scarf
		bool m_DrawFront = false;
		vec2 m_FrontPos = vec2(0.0f, 0.0f);
		float m_FrontScale = 1.0f;
		float m_FrontAlpha = 1.0f;
	};
	SState m_aStates[MAX_CLIENTS];
	// the menu can show a few previews at once, each keeps its own cloth
	static constexpr int NUM_PREVIEWS = 4;
	SState m_aPreviews[NUM_PREVIEWS];
	vec2 m_aPreviewPos[NUM_PREVIEWS];
	float m_aPreviewTime[NUM_PREVIEWS] = {};

	CXcBackgrounds m_Pictures;

	bool LooksLeft(int ClientId) const;
	void UpdateMotion(SState &State, vec2 Pos, float Dt, bool Jumped) const;

	// cape
	float ColumnLength(int Col) const;
	void SimulateCloth(SState &State, vec2 Pos, float Scale, float Dt, float Time, bool Collide) const;
	void RenderCloth(const SState &State, float Scale, int ClientId, float Alpha);

	// scarf
	vec2 KnotPos(vec2 Pos, float Scale, float Back) const;
	void SimulateTail(STail &Tail, const SState &State, int Index, vec2 Anchor, float Scale, float Dt, float Time, bool Collide) const;
	void RenderTails(const SState &State, float Scale, int ClientId, float Alpha) const;
	void RenderWrap(vec2 Pos, float Scale, float Back, int ClientId, float Alpha) const;

	ColorRGBA ScarfColor(int ClientId, float t) const;
	void Collide(vec2 &Point, vec2 &Prev) const;
	SState &PreviewState(vec2 Pos, float Scale);

	friend class CXcCapeFront;
	void RenderFront();
};

#endif

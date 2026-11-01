#ifndef GAME_CLIENT_COMPONENTS_XCLIENT_FLOATERS_H
#define GAME_CLIENT_COMPONENTS_XCLIENT_FLOATERS_H

#include <base/color.h>
#include <base/vmath.h>

#include <game/client/component.h>

#include <vector>

// Glowing cubes, light orbs, fireflies, notes, runes and hearts that slowly drift and turn around the camera and
// make room for your tee. The cubes are real 3D boxes, turned and lit, drawn with flat shaded faces; the runes,
// notes and hearts are drawn with lines and shapes of the effect renderer.
class CXcFloaters : public CComponent
{
public:
	enum
	{
		FLOATERS_OFF = 0,
		FLOATERS_CUBES,
		FLOATERS_ORBS,
		FLOATERS_MIXED,
	};
	enum
	{
		KIND_CUBE = 0,
		KIND_ORB,
		KIND_FIREFLY,
		KIND_NOTE,
		KIND_RUNE,
		KIND_HEART,
		NUM_KINDS,
	};
	// the kinds that fly, a mask of 1 << KIND_*
	static int Kinds();

	int Sizeof() const override { return sizeof(*this); }
	void OnRender() override;
	void OnReset() override;
	void OnMapLoad() override;

private:
	struct SFloater
	{
		vec2 m_Pos;
		vec2 m_Vel;
		vec2 m_Drift; // the speed it returns to after a push
		float m_Depth; // 0.4 .. 1, far ones are smaller and dimmer
		float m_Size;
		float m_aAngle[3];
		float m_aSpin[3];
		float m_Hue;
		float m_Life; // fades in after spawning
		int m_Kind;
		int m_Glyph; // which rune or note
		int m_Script; // the runes: 0 Norse, 1 Elvish, 2 alchemy
		float m_Phase; // the blink of a firefly, the bob of a note
	};
	std::vector<SFloater> m_vFloaters;
	vec2 m_LastCamera = vec2(0.0f, 0.0f);
	bool m_HasCamera = false;

	void Spawn(SFloater &Floater, float X0, float Y0, float X1, float Y1, bool Anywhere);
	void DrawGlow(vec2 Pos, float Radius, ColorRGBA Color) const;
	void DrawCube(const SFloater &Floater, vec2 Pos, float Size, ColorRGBA Color, float Alpha) const;
	// fireflies, notes, runes and hearts
	void DrawShape(const SFloater &Floater, float Size, ColorRGBA Color, float Alpha, float Time) const;
	float m_NotesFade = 0.0f; // the notes fade out when the music stops
};

#endif

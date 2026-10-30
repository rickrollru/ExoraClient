#ifndef GAME_CLIENT_COMPONENTS_XCLIENT_ACCESSORY_H
#define GAME_CLIENT_COMPONENTS_XCLIENT_ACCESSORY_H

#include <base/color.h>
#include <base/vmath.h>

#include <engine/shared/protocol.h>

#include <game/client/component.h>

#include <vector>

struct SFxShape;

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
	};

	enum
	{
		LOOK_ENERGY = 0, // made of light
		LOOK_NEON, // dark with glowing edges
		LOOK_SOLID, // a solid material
	};
	enum
	{
		PATTERN_PLAIN = 0,
		PATTERN_STRIPES,
		PATTERN_RINGS,
		PATTERN_GRADIENT,
	};

	int Sizeof() const override { return sizeof(*this); }
	// the accessory of your tee on a tee drawn in the menu (the cosmetics cards)
	void RenderPreview(vec2 Pos, float TeeSize, float Alpha);
	void OnRender() override;
	void OnReset() override;
	void OnMapLoad() override;

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
	void RenderOne(vec2 Head, float Scale, float Tilt, float Squash, float Alpha, int Seed);
	// shapes go to the world queue in game, to a list drawn at once in the menu
	bool m_Preview = false;
	std::vector<SFxShape> m_vPreviewShapes;
	void Add(const SFxShape &Shape);
	void AddGlowing(const SFxShape &Shape, float Radius, float Strength);
	// soft light (sprites in game, a glow of a dot in the menu)
	void Light(vec2 Pos, float Size, ColorRGBA Color, float Strength);
	ColorRGBA EdgeColor(float Alpha) const;
	ColorRGBA BaseColor(float Offset, float Alpha) const;
	int m_ColorClient = -1; // player being drawn, for the skin color mode
	void RenderCone(vec2 Head, float Scale, float Tilt, float Squash, float Alpha);
	void RenderHalo(vec2 Head, float Scale, float Tilt, float Squash, float Alpha);
	void RenderCrown(vec2 Head, float Scale, float Tilt, float Squash, float Alpha);
};

#endif

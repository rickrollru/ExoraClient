#ifndef GAME_CLIENT_COMPONENTS_XCLIENT_BLOCK_OUTLINE_H
#define GAME_CLIENT_COMPONENTS_XCLIENT_BLOCK_OUTLINE_H

#include <base/vmath.h>

#include <game/client/component.h>

// A soft frame around the block under the crosshair that glides from block to block.
// Can be colored by the kind of block (freeze, unfreeze, death...) and show its name.
// Only shows what the map already shows.
class CXcBlockOutline : public CComponent
{
public:
	enum
	{
		MODE_OUTLINE = 0,
		MODE_CORNERS,
		MODE_FILL,
	};

	int Sizeof() const override { return sizeof(*this); }
	void OnRender() override;
	void OnReset() override;
	void OnMapLoad() override { OnReset(); }

	// name of a block for the label, nullptr for air; Color gets its color
	static const char *TileName(int Tile, struct ColorRGBA *pColor);

private:
	vec2 m_Pos = vec2(0.0f, 0.0f); // top left of the frame, eased
	bool m_HasPos = false;
	float m_Shown = 0.0f;
};

#endif

#ifndef GAME_CLIENT_COMPONENTS_XCLIENT_TEE_GLOW_H
#define GAME_CLIENT_COMPONENTS_XCLIENT_TEE_GLOW_H

#include <base/color.h>

#include <game/client/component.h>

// Neon glow and outline around the silhouette of tees (body, feet, eyes and weapon),
// and the tee shader: a moving pattern (gradient, rainbow, galaxy...) painted over the skin.
// CPlayers wraps the drawing of a player with Begin/End, the render module does the rest.
class CTeeGlow : public CComponent
{
public:
	int Sizeof() const override { return sizeof(*this); }

	// Returns true when this player gets a glow, then End must be called after drawing it.
	bool Begin(int ClientId);
	void End(int ClientId);

private:
	ColorRGBA GlowColor(int ClientId) const;
	bool HasGlow(int ClientId) const;
	// the tee shader (xc_tee_shader) uses the same capture of the tee
	bool HasShader(int ClientId) const;
};

#endif

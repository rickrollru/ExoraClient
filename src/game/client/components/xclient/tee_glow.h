#ifndef GAME_CLIENT_COMPONENTS_XCLIENT_TEE_GLOW_H
#define GAME_CLIENT_COMPONENTS_XCLIENT_TEE_GLOW_H

#include <base/color.h>

#include <game/client/component.h>

// Neon glow and outline around the silhouette of tees (body, feet, eyes and weapon),
// and the tee shader: a moving pattern (gradient, rainbow, galaxy...) painted over the skin.
// CPlayers wraps the drawing of a player with Begin/End, the render module does the rest.
namespace XcRender
{
struct SGlowSettings;
}

class CTeeGlow : public CComponent
{
public:
	int Sizeof() const override { return sizeof(*this); }

	// Returns true when this player gets a glow, then End must be called after drawing it.
	bool Begin(int ClientId);
	// the neon outline and the tee shader of your own tee for a preview (the menu): false when neither is on
	bool PreviewSettings(float PixelsPerUnit, float Alpha, XcRender::SGlowSettings &Settings) const;
	void End(int ClientId);

	// the tee shader on single parts (xc_tee_shader_parts): CPlayers wraps the part with these;
	// PartBegin returns true when End must be called
	enum
	{
		PART_TEE = 1,
		PART_WEAPON = 2,
		PART_HOOK = 4,
	};
	bool PartBegin(int ClientId, int Part);
	void PartEnd(int ClientId);

private:
	// the shader fields of the settings for a player at a screen point (the tee's center, pixels from the top left)
	void ShaderSettings(int ClientId, float PixelsPerUnit, vec2 OriginPixels, XcRender::SGlowSettings &Settings) const;
	// tee and weapon both: the shader goes into the capture of the whole player, no parts needed
	bool ShaderWhole(int ClientId) const;
	vec2 ToPixels(vec2 WorldPos) const;
	float PixelsPerUnit() const;
	ColorRGBA GlowColor(int ClientId) const;
	bool HasGlow(int ClientId) const;
	// the tee shader (xc_tee_shader) uses the same capture of the tee
	bool HasShader(int ClientId) const;
};

#endif

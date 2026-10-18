#ifndef GAME_CLIENT_COMPONENTS_XCLIENT_ITEM_FX_H
#define GAME_CLIENT_COMPONENTS_XCLIENT_ITEM_FX_H

#include "fx_renderer.h"
#include "xc_particles.h"

#include <base/color.h>
#include <base/vmath.h>

#include <game/client/component.h>

#include <vector>

// Glow for pickups (halo and sparkles), projectiles (light and a comet tail) and lasers.
// CItems reports every drawn item, the glow is drawn in one batch right after the items.
class CItemFx : public CComponent
{
public:
	enum
	{
		COLOR_BY_TYPE = 0,
		COLOR_ACCENT,
		COLOR_OWN,
		COLOR_RAINBOW,
	};

	int Sizeof() const override { return sizeof(*this); }
	void OnRender() override;
	void OnReset() override;

	void Pickup(vec2 Pos, int Type, int Subtype);
	void Projectile(vec2 Pos, vec2 Vel, int Weapon, float Alpha);
	void Laser(vec2 From, vec2 To, ColorRGBA LaserColor, float Alpha);

private:
	std::vector<SFxShape> m_vShapes;
	CXcParticles m_Particles;

	ColorRGBA ItemColor(ColorRGBA ByType, float HueOffset) const;
};

#endif

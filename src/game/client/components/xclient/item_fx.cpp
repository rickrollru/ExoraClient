#include "item_fx.h"

#include "xc_helpers.h"

#include <base/math.h>

#include <engine/client.h>
#include <engine/shared/config.h>

#include <generated/protocol.h>

#include <game/client/components/xclient/ui/theme.h>
#include <game/client/gameclient.h>

#include <cmath>

using namespace XcHelpers;

static ColorRGBA Hex(unsigned Rgb)
{
	return ColorRGBA(((Rgb >> 16) & 0xFF) / 255.0f, ((Rgb >> 8) & 0xFF) / 255.0f, (Rgb & 0xFF) / 255.0f, 1.0f);
}

static ColorRGBA WeaponColor(int Weapon)
{
	switch(Weapon)
	{
	case WEAPON_HAMMER: return Hex(0xE8ECF5);
	case WEAPON_GUN: return Hex(0xFFD93D);
	case WEAPON_SHOTGUN: return Hex(0xFF8A3D);
	case WEAPON_GRENADE: return Hex(0xFF5A4D);
	case WEAPON_LASER: return Hex(0x4FC3FF);
	default: return Hex(0xB26BFF); // ninja
	}
}

void CItemFx::OnReset()
{
	m_vShapes.clear();
	m_Particles.Clear();
}

ColorRGBA CItemFx::ItemColor(ColorRGBA ByType, float HueOffset) const
{
	switch(g_Config.m_XcItemsColorMode)
	{
	case COLOR_ACCENT:
	{
		const ImVec4 Accent = XcUi::AccentColor(g_Config.m_XcUiAccent, g_Config.m_XcUiTheme == 0);
		return ColorRGBA(Accent.x, Accent.y, Accent.z, 1.0f);
	}
	case COLOR_OWN: return HslConfigColor(g_Config.m_XcItemsColor);
	case COLOR_RAINBOW: return RainbowColor(Client()->GlobalTime() * 0.15f + HueOffset, 1.0f);
	default: return ByType;
	}
}

bool CItemFx::Soft() const
{
	return GameClient()->m_XcSprites.Enabled();
}

void CItemFx::Pickup(vec2 Pos, int Type, int Subtype)
{
	const FxTune::CScope TuneScope(FxTune::ITEMS);
	if(!g_Config.m_XcItemsGlow)
		return;
	ColorRGBA ByType;
	switch(Type)
	{
	case POWERUP_HEALTH: ByType = Hex(0xFF4D6A); break;
	case POWERUP_ARMOR: ByType = Hex(0xFFB43D); break;
	case POWERUP_NINJA: ByType = Hex(0xB26BFF); break;
	case POWERUP_FREEZE: ByType = Hex(0x7FD8FF); break;
	case POWERUP_WEAPON: ByType = WeaponColor(Subtype); break;
	default: ByType = Hex(0xFFB43D); break; // weapon armors
	}
	const FxGlow::SScale Glow = FxGlow::For(FxGlow::ITEMS);
	const float Size = g_Config.m_XcItemsGlowSize / 100.0f * Glow.m_Radius;
	const float Strength = std::min(g_Config.m_XcItemsGlowStrength / 100.0f * Glow.m_Strength, 2.6f);
	const float Time = Client()->GlobalTime();
	// each pickup breathes with its own phase
	const float Phase = Pos.x * 0.013f + Pos.y * 0.007f;
	const float Breath = 0.8f + 0.2f * std::sin(Time * 2.2f + Phase);
	const ColorRGBA Color = ItemColor(ByType, Phase * 0.1f);
	const float Radius = 26.0f * Size * (0.95f + 0.05f * Breath);
	if(Soft())
	{
		// soft light: a wide faint bloom, a colored glow and a small hot center
		CXcSprites::Push(m_vSprites, CXcSprites::SPRITE_SOFT_BLOOM, true, Pos, Radius * 3.4f, 0.0f, Color.WithAlpha(0.22f * Strength * Breath));
		CXcSprites::Push(m_vSprites, CXcSprites::SPRITE_GLOW, false, Pos, Radius * 1.6f, 0.0f, Color.WithAlpha(0.3f * std::min(Strength, 1.0f) * Breath));
		CXcSprites::Push(m_vSprites, CXcSprites::SPRITE_GLOW, true, Pos, Radius * 1.3f, 0.0f, Color.WithAlpha(0.3f * Strength * Breath));
	}
	else
	{
	// halo: colored light in the middle fading to nothing; normal blending, so pickups standing
	// close together do not add up to white
	m_vShapes.push_back(FxShape::Circle(Pos, Radius, Color.WithAlpha(0.0f)).RadialGradient(Color.WithAlpha(0.38f * Strength * Breath), 1.4f));
	// a small additive center makes the item shine (and feeds the bloom)
	m_vShapes.push_back(FxShape::Circle(Pos, Radius * 0.3f, Color.WithAlpha(0.0f)).RadialGradient(Color.WithAlpha(0.22f * Strength * Breath), 2.0f).Additive());
	}

	// a sparkle now and then rises from the pickup
	if(g_Config.m_XcItemsSparkles && random_float() < Client()->RenderFrameTime() * 1.6f)
	{
		CXcParticles::SParticle Particle;
		Particle.m_Type = CXcParticles::EType::STAR;
		Particle.m_Pos = Pos + vec2(random_float(-14.0f, 14.0f), random_float(-10.0f, 10.0f)) * Size;
		Particle.m_Vel = vec2(random_float(-6.0f, 6.0f), random_float(-28.0f, -14.0f));
		Particle.m_Gravity = 0.0f;
		Particle.m_Friction = 0.6f;
		Particle.m_LifeSpan = random_float(0.6f, 1.0f);
		Particle.m_StartSize = random_float(6.0f, 9.0f) * Size;
		Particle.m_EndSize = 0.0f;
		Particle.m_Rot = random_float(0.0f, pi);
		Particle.m_RotSpeed = random_float(-2.0f, 2.0f);
		Particle.m_Color = Color;
		m_Particles.Add(Particle);
	}
}

void CItemFx::Projectile(vec2 Pos, vec2 Vel, int Weapon, float Alpha)
{
	const FxGlow::CScope GlowScope(FxGlow::ITEMS);
	if(!g_Config.m_XcProjectileGlow || Alpha <= 0.0f)
		return;
	const ColorRGBA Color = ItemColor(WeaponColor(Weapon), 0.0f);
	const bool Grenade = Weapon == WEAPON_GRENADE;
	const float Radius = Grenade ? 14.0f : 9.0f;
	const bool SoftLight = Soft();
	if(SoftLight)
		CXcSprites::PushLight(m_vSprites, Pos, Radius * 1.1f * FxGlow::g_Current.m_Radius, Color.WithAlpha(Alpha), std::min(FxGlow::g_Current.m_Strength, 2.0f) * 0.8f);
	else
		m_vShapes.push_back(FxShape::Circle(Pos, Radius, Color).GlowOnly(Radius * 2.2f, 0.55f * Alpha).Additive());
	if(g_Config.m_XcProjectileTrail && length(Vel) > 0.0001f && SoftLight)
	{
		// a tail of light that thins and fades against the flight direction
		const vec2 Dir = normalize(Vel);
		const float Length = Grenade ? 80.0f : 60.0f;
		const int Pieces = 10;
		for(int i = 1; i <= Pieces; i++)
		{
			const float a = (float)i / Pieces;
			const float Fade = (1.0f - a) * (1.0f - a);
			const float Width = (Grenade ? 26.0f : 18.0f) * (1.0f - a * 0.7f) * FxGlow::g_Current.m_Radius;
			CXcSprites::Push(m_vSprites, CXcSprites::SPRITE_GLOW, true, Pos - Dir * Length * a, Width, 0.0f, Color.WithAlpha(0.45f * Fade * Alpha));
		}
	}
	else if(g_Config.m_XcProjectileTrail && length(Vel) > 0.0001f)
	{
		// comet tail against the flight direction, thin and fading
		const vec2 Dir = normalize(Vel);
		const float Length = Grenade ? 70.0f : 55.0f;
		const int Pieces = 6;
		for(int i = 0; i < Pieces; i++)
		{
			const float a = (float)i / Pieces;
			const float b = (float)(i + 1) / Pieces;
			const float Fade = (1.0f - a) * (1.0f - a);
			m_vShapes.push_back(FxShape::Line(Pos - Dir * Length * a, Pos - Dir * Length * b, (Grenade ? 9.0f : 6.0f) * (1.0f - a * 0.8f), Color.WithAlpha(0.55f * Fade * Alpha)).Additive());
		}
	}
}

void CItemFx::Laser(vec2 From, vec2 To, ColorRGBA LaserColor, float Alpha)
{
	const FxGlow::CScope GlowScope(FxGlow::LASERS);
	if(!g_Config.m_XcLaserGlow || Alpha <= 0.0f || distance(From, To) < 1.0f)
		return;
	const ColorRGBA Color = ItemColor(LaserColor, 0.0f);
	const float Strength = g_Config.m_XcLaserGlowStrength / 100.0f;
	if(Soft())
		CXcSprites::PushLightLine(m_vSprites, From, To, 34.0f * FxGlow::g_Current.m_Radius, Color.WithAlpha(Alpha), std::min(Strength * FxGlow::g_Current.m_Strength, 2.0f));
	else
		m_vShapes.push_back(FxShape::Line(From, To, 7.0f, Color).GlowOnly(22.0f, 0.5f * Strength * Alpha).Additive());
}

void CItemFx::LaserBeam(vec2 From, vec2 To, ColorRGBA OuterColor, ColorRGBA InnerColor, float Fade, float Alpha)
{
	const FxGlow::CScope GlowScope(FxGlow::LASERS);
	if(Fade <= 0.0f || Alpha <= 0.0f)
		return;
	const ColorRGBA Color = ItemColor(OuterColor, 0.0f);
	const float Strength = g_Config.m_XcLaserGlowStrength / 100.0f;
	// smooth thinning instead of the stock linear one, the beam stays bright until the end
	const float Width = 1.0f - (1.0f - Fade) * (1.0f - Fade);
	const ColorRGBA Core(mix(InnerColor.r, 1.0f, 0.55f), mix(InnerColor.g, 1.0f, 0.55f), mix(InnerColor.b, 1.0f, 0.55f), Alpha);
	if(Soft())
	{
		// the beam is a clean line, its light and the hot spot are soft textures
		const float Reach = FxGlow::g_Current.m_Radius;
		const float Light = std::min(Strength * FxGlow::g_Current.m_Strength, 2.0f);
		if(distance(From, To) >= 1.0f)
		{
			CXcSprites::PushLightLine(m_vSprites, From, To, 40.0f * Width * Reach, Color.WithAlpha(Alpha), Light);
			m_vShapes.push_back(FxShape::Line(From, To, 9.0f * Width, Color.WithAlpha(0.9f * Alpha)).Additive());
			m_vShapes.push_back(FxShape::Line(From, To, 4.0f * Width, Core).Additive());
		}
		CXcSprites::PushLight(m_vSprites, To, 12.0f * Width * Reach, Color.WithAlpha(Alpha), Light);
		return;
	}
	if(distance(From, To) >= 1.0f)
	{
		m_vShapes.push_back(FxShape::Line(From, To, 11.0f * Width, Color.WithAlpha(0.9f * Alpha)).Glow(18.0f * Width, 0.55f * Strength).Additive());
		m_vShapes.push_back(FxShape::Line(From, To, 4.5f * Width, Core).Additive());
	}
	// a hot spot where the beam ends
	m_vShapes.push_back(FxShape::Circle(To, 7.0f * Width, Core).Glow(16.0f * Width, 0.7f * Strength).Additive());
}

void CItemFx::OnRender()
{
	if(Client()->State() != IClient::STATE_ONLINE && Client()->State() != IClient::STATE_DEMOPLAYBACK)
	{
		m_vShapes.clear();
		m_vSprites.clear();
		return;
	}
	// the soft light goes under the shapes
	GameClient()->m_XcSprites.DrawList(m_vSprites);
	GameClient()->m_FxRenderer.Draw(m_vShapes, true);
	m_vShapes.clear();
	m_Particles.Update(std::min(Client()->RenderFrameTime(), 0.1f));
	m_Particles.Render(GameClient()->m_FxRenderer);
}

#ifndef GAME_CLIENT_COMPONENTS_XCLIENT_WEATHER_H
#define GAME_CLIENT_COMPONENTS_XCLIENT_WEATHER_H

#include "fx_renderer.h"
#include "ui/backgrounds.h"

#include <base/color.h>
#include <base/vmath.h>

#include <game/client/component.h>

#include <vector>

// Screen-space weather (snow, leaves, sakura, ash) drawn over the game world
// and over the main menu background.
class CWeather : public CComponent
{
public:
	enum
	{
		WEATHER_OFF = 0,
		WEATHER_SNOW,
		WEATHER_LEAVES,
		WEATHER_SAKURA,
		WEATHER_ASH,
		WEATHER_HEARTS,
		WEATHER_STARS,
		WEATHER_CRYSTALS,
		WEATHER_FIREFLIES,
		WEATHER_MONEY,
		WEATHER_HALLOWEEN,
		WEATHER_EMBERS,
		WEATHER_RAIN,
		WEATHER_BUBBLES,
		WEATHER_CONFETTI,
		WEATHER_CUSTOM, // own picture or gif from xclient/weather
		NUM_WEATHERS,
	};
	static constexpr const char *FOLDER = "xclient/weather";

	CWeather();
	int Sizeof() const override { return sizeof(*this); }
	void OnInit() override;
	void OnShutdown() override;
	void OnRender() override;
	void OnReset() override;

	// own weather pictures, for the gallery in the menu
	CXcBackgrounds &Images() { return m_Images; }

	// Called by the menus between the menu background and the menu UI.
	void RenderMenu();

private:
	struct SParticle
	{
		vec2 m_Pos;
		float m_FallSpeed;
		float m_Depth; // 0..1, closer particles are bigger, faster and move more with the camera
		float m_Size;
		float m_Stretch; // height / width
		float m_Rot;
		float m_RotSpeed;
		float m_SwayPhase;
		float m_SwaySpeed;
		float m_SwayAmount;
		float m_Flicker; // ash embers only, otherwise 0
		bool m_Sprite; // use the snowflake sprite instead of the soft ball
		int m_Icon; // CXcSprites picture of the icon weathers, -1 for shapes
		bool m_Glowing; // stars, fireflies, embers: soft light around them
		float m_Hue; // own place on the rainbow when the weather is painted in rainbow colors
		ColorRGBA m_Color;
	};
	CXcBackgrounds m_Images;

	std::vector<SParticle> m_vParticles;
	int m_Type = WEATHER_OFF;
	float m_Width = 0.0f;
	float m_Height = 0.0f;
	float m_Time = 0.0f;
	vec2 m_LastCamera = vec2(0.0f, 0.0f);
	bool m_HasLastCamera = false;

	void Spawn(SParticle &Particle, bool Anywhere) const;
	void Step(vec2 Shift);
	std::vector<SFxShape> m_vShapes;

	float ParticleAlpha(const SParticle &Particle) const;
	// the particle's own color, or the chosen one when the weather is painted
	ColorRGBA ParticleColor(const SParticle &Particle) const;
	bool IconWeather() const;
	void DrawIcons();
	void DrawCustom();
	vec2 ParticleSize(const SParticle &Particle) const;
	void Draw();
	// render module: smooth SDF shapes
	void DrawShapes();
	// fallback: textured sprites through IGraphics
	void DrawSprites() const;
};

#endif

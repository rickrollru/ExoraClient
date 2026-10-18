#ifndef GAME_CLIENT_COMPONENTS_XCLIENT_WEATHER_H
#define GAME_CLIENT_COMPONENTS_XCLIENT_WEATHER_H

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
		NUM_WEATHERS,
	};

	int Sizeof() const override { return sizeof(*this); }
	void OnRender() override;
	void OnReset() override;

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
		ColorRGBA m_Color;
	};

	std::vector<SParticle> m_vParticles;
	int m_Type = WEATHER_OFF;
	float m_Width = 0.0f;
	float m_Height = 0.0f;
	float m_Time = 0.0f;
	vec2 m_LastCamera = vec2(0.0f, 0.0f);
	bool m_HasLastCamera = false;

	void Spawn(SParticle &Particle, bool Anywhere) const;
	void Step(vec2 Shift);
	void Draw() const;
};

#endif

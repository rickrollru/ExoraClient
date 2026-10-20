#ifndef GAME_CLIENT_COMPONENTS_XCLIENT_AMBIENCE_H
#define GAME_CLIENT_COMPONENTS_XCLIENT_AMBIENCE_H

#include <base/color.h>
#include <base/vmath.h>

#include <game/client/component.h>

// Atmosphere over the map (from the Flower client's Ambience and CustomFog): the light of the
// time of day (real clock or chosen), sun or moon rays from the sky, fog from below and
// drifting mist. Only a picture over the world, under the HUD.
class CXcAmbience : public CComponent
{
public:
	enum
	{
		TIME_CLOCK = 0, // the computer clock
		TIME_DAWN,
		TIME_DAY,
		TIME_SUNSET,
		TIME_NIGHT,
		TIME_HOUR, // xc_ambience_hour
	};

	int Sizeof() const override { return sizeof(*this); }
	void OnRender() override;

	struct SPalette
	{
		ColorRGBA m_Overlay; // over everything, alpha blended
		ColorRGBA m_Sky; // additive glow from the top
		ColorRGBA m_Horizon; // additive glow from the bottom
		ColorRGBA m_Rays;
		ColorRGBA m_Fog;
		float m_RaysX; // where the rays come from, 0 = left, 1 = right
	};
	// the light of a time of day, Hour 0..24
	static SPalette PaletteAt(float Hour);
	float CurrentHour() const;

private:
	struct SMist
	{
		vec2 m_Pos; // 0..1 of the screen, before the parallax
		float m_Size;
		float m_Speed;
		float m_Phase;
	};
	SMist m_aMist[24];
	bool m_MistReady = false;

	void RenderRays(float Width, float Height, const SPalette &Palette, float Strength);
	void RenderFog(float Width, float Height, const SPalette &Palette, float Density);
	void RenderMist(float Width, float Height, const SPalette &Palette, float Amount);
};

#endif

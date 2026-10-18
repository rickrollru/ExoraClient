// Visuals page: every effect is a card with its style, color, sizes and "mine / dummy / others".
#include "icons.h"
#include "lang.h"
#include "menu.h"
#include "theme.h"
#include "widgets.h"

#include <engine/external/imgui/imgui.h>
#include <engine/graphics.h>
#include <engine/shared/config.h>

#include <game/client/gameclient.h>

using namespace XcUi;

// Cards for effects that have several styles and "off" as 0: the header toggle switches between off
// and the last used style, the styles are a segmented control in the body.
bool CXcMenu::StyleCard(const char *pId, const char *pTitle, const char *pDescription, const char *pConfig, int *pValue, const char *const *ppStyles, int NumStyles)
{
	int &LastStyle = m_aLastFxStyle[ImGui::GetID(pId) % std::size(m_aLastFxStyle)];
	if(*pValue > 0)
		LastStyle = *pValue;
	if(LastStyle <= 0 || LastStyle > NumStyles)
		LastStyle = 1;
	int Enabled = *pValue > 0;
	const int WasEnabled = Enabled;
	if(!BeginCard(pId, pTitle, pDescription, &Enabled))
		return false;
	if(Enabled != WasEnabled)
		SetConfigInt(pConfig, Enabled ? LastStyle : 0);
	int Style = std::clamp((Enabled ? *pValue : LastStyle) - 1, 0, NumStyles - 1);
	if(Segmented("##style", &Style, ppStyles, NumStyles))
	{
		LastStyle = Style + 1;
		SetConfigInt(pConfig, LastStyle);
	}
	return true;
}

void CXcMenu::RenderVisualsTee()
{
	BeginCardGrid("##vis_tee", 2);
	if(BeginCard("##fx_glow", Tr("Neon glow"), Tr("Soft light and a crisp outline around the tee"), &g_Config.m_XcGlow))
	{
		if(!Graphics()->XcRenderAvailable())
			Description(Tr("Needs OpenGL 3.3, see Settings > Graphics"));
		EffectColorPicker("##glow_color", &g_Config.m_XcGlowColorMode, &g_Config.m_XcGlowColor);
		ConfigSlider(Tr("Size"), "xc_glow_size", &g_Config.m_XcGlowSize, 25, 300, "%d%%");
		ConfigSlider(Tr("Glow strength"), "xc_glow_strength", &g_Config.m_XcGlowStrength, 10, 300, "%d%%");
		ConfigSlider(Tr("Outline"), "xc_glow_outline", &g_Config.m_XcGlowOutline, 0, 300, "%d%%");
		ToggleRow(Tr("Pulse"), "xc_glow_pulse", &g_Config.m_XcGlowPulse);
		TargetChips("##glow_targets", &g_Config.m_XcGlowSelf, &g_Config.m_XcGlowDummy, &g_Config.m_XcGlowOthers);
		EndCard();
	}

	const char *apTrails[] = {Tr("Ribbon"), Tr("Sparks"), Tr("Stars"), Tr("Comet")};
	if(StyleCard("##fx_trail", Tr("Trail"), Tr("A glowing ribbon behind the tee"), "xc_trail", &g_Config.m_XcTrail, apTrails, 4))
	{
		EffectColorPicker("##trail_color", &g_Config.m_XcTrailColorMode, &g_Config.m_XcTrailColor);
		ConfigSlider(Tr("Length"), "xc_trail_length", &g_Config.m_XcTrailLength, 100, 2000, "%d ms");
		ConfigSlider(Tr("Width"), "xc_trail_width", &g_Config.m_XcTrailWidth, 25, 300, "%d%%");
		ConfigSlider(Tr("Glow"), "xc_trail_glow", &g_Config.m_XcTrailGlow, 0, 200, "%d%%");
		TargetChips("##trail_targets", &g_Config.m_XcTrailSelf, &g_Config.m_XcTrailDummy, &g_Config.m_XcTrailOthers);
		EndCard();
	}

	const char *apAccessories[] = {Tr("Cone hat"), Tr("Halo"), Tr("Crown")};
	if(StyleCard("##fx_accessory", Tr("Accessory"), Tr("Something on the head that sways with the tee"), "xc_accessory", &g_Config.m_XcAccessory, apAccessories, 3))
	{
		EffectColorPicker("##accessory_color", &g_Config.m_XcAccessoryColorMode, &g_Config.m_XcAccessoryColor);
		ConfigSlider(Tr("Size"), "xc_accessory_size", &g_Config.m_XcAccessorySize, 50, 200, "%d%%");
		ConfigSlider(Tr("Opacity"), "xc_accessory_alpha", &g_Config.m_XcAccessoryAlpha, 10, 100, "%d%%");
		ConfigSlider(Tr("Sway"), "xc_accessory_physics", &g_Config.m_XcAccessoryPhysics, 0, 200, "%d%%");
		TargetChips("##accessory_targets", &g_Config.m_XcAccessorySelf, &g_Config.m_XcAccessoryDummy, &g_Config.m_XcAccessoryOthers);
		EndCard();
	}
	EndCardGrid();
}

void CXcMenu::RenderVisualsMovement()
{
	BeginCardGrid("##vis_move", 2);
	const char *apJumps[] = {Tr("Rings"), Tr("Sparks"), Tr("Stars")};
	if(StyleCard("##fx_jump", Tr("Jumps"), Tr("Effect under the feet on every jump"), "xc_jump_fx", &g_Config.m_XcJumpFx, apJumps, 3))
	{
		EffectColorPicker("##jump_color", &g_Config.m_XcJumpFxColorMode, &g_Config.m_XcJumpFxColor);
		ConfigSlider(Tr("Size"), "xc_jump_fx_size", &g_Config.m_XcJumpFxSize, 25, 300, "%d%%");
		ToggleRow(Tr("Also on double jumps"), "xc_jump_fx_air", &g_Config.m_XcJumpFxAir);
		ToggleRow(Tr("Ring on landing"), "xc_land_fx", &g_Config.m_XcLandFx);
		ToggleRow(Tr("Dust when running"), "xc_run_dust", &g_Config.m_XcRunDust);
		TargetChips("##jump_targets", &g_Config.m_XcJumpFxSelf, &g_Config.m_XcJumpFxDummy, &g_Config.m_XcJumpFxOthers);
		EndCard();
	}
	EndCardGrid();
}

void CXcMenu::RenderVisualsWeapons()
{
	BeginCardGrid("##vis_weapons", 2);
	if(BeginCard("##fx_projectiles", Tr("Bullets and lasers"), Tr("Light around shots, comet tails and glowing lasers")))
	{
		ToggleRow(Tr("Glow around bullets and grenades"), "xc_projectile_glow", &g_Config.m_XcProjectileGlow);
		ToggleRow(Tr("Comet tail"), "xc_projectile_trail", &g_Config.m_XcProjectileTrail);
		ToggleRow(Tr("Laser glow"), "xc_laser_glow", &g_Config.m_XcLaserGlow);
		ConfigSlider(Tr("Laser glow strength"), "xc_laser_glow_strength", &g_Config.m_XcLaserGlowStrength, 10, 250, "%d%%");
		Description(Tr("The color is set in the Pickups card under World"));
		EndCard();
	}
	const char *apHooks[] = {Tr("Chain"), Tr("Line"), Tr("Glow")};
	if(StyleCard("##fx_hook", Tr("Hook"), Tr("Colored chain, clean line or a glowing beam"), "xc_hook_style", &g_Config.m_XcHookStyle, apHooks, 3))
	{
		EffectColorPicker("##hook_color", &g_Config.m_XcHookColorMode, &g_Config.m_XcHookColor);
		ConfigSlider(Tr("Width"), "xc_hook_width", &g_Config.m_XcHookWidth, 25, 300, "%d%%");
		ParamLabel(Tr("When the hook grabs"));
		const char *apHits[] = {Tr("Nothing"), Tr("Ring"), Tr("Sparks"), Tr("Stars")};
		ConfigSegmented("##hook_hit", "xc_hook_hit_fx", &g_Config.m_XcHookHitFx, apHits, 4);
		TargetChips("##hook_targets", &g_Config.m_XcHookSelf, &g_Config.m_XcHookDummy, &g_Config.m_XcHookOthers);
		EndCard();
	}
	EndCardGrid();
}

void CXcMenu::RenderVisualsWorld()
{
	BeginCardGrid("##vis_world", 2);
	if(BeginCard("##fx_items", Tr("Pickups"), Tr("Hearts, armor, weapons and ninja softly glow"), &g_Config.m_XcItemsGlow))
	{
		ParamLabel(Tr("Color"));
		const char *apModes[] = {Tr("By type"), Tr("Accent"), Tr("Own"), Tr("Rainbow")};
		ConfigSegmented("##items_color_mode", "xc_items_color_mode", &g_Config.m_XcItemsColorMode, apModes, 4);
		if(g_Config.m_XcItemsColorMode == 2)
		{
			int Mode = 1; // the picker's own color part
			EffectColorPicker("##items_color", &Mode, &g_Config.m_XcItemsColor);
		}
		ConfigSlider(Tr("Size"), "xc_items_glow_size", &g_Config.m_XcItemsGlowSize, 25, 250, "%d%%");
		ConfigSlider(Tr("Brightness"), "xc_items_glow_strength", &g_Config.m_XcItemsGlowStrength, 10, 250, "%d%%");
		ToggleRow(Tr("Sparkles"), "xc_items_sparkles", &g_Config.m_XcItemsSparkles);
		Description(Tr("Bullets, grenades and lasers use the same colors"));
		EndCard();
	}
	const char *apWeather[] = {Tr("Snow"), Tr("Leaves"), Tr("Sakura"), Tr("Ash")};
	if(StyleCard("##fx_weather", Tr("Weather"), Tr("Falling particles over the map and the menu"), "xc_weather", &g_Config.m_XcWeather, apWeather, 4))
	{
		ConfigSlider(Tr("Amount"), "xc_weather_density", &g_Config.m_XcWeatherDensity, 1, 100, "%d%%");
		ConfigSlider(Tr("Size"), "xc_weather_size", &g_Config.m_XcWeatherSize, 25, 300, "%d%%");
		ConfigSlider(Tr("Speed"), "xc_weather_speed", &g_Config.m_XcWeatherSpeed, 10, 300, "%d%%");
		ConfigSlider(Tr("Opacity"), "xc_weather_alpha", &g_Config.m_XcWeatherAlpha, 5, 100, "%d%%");
		ToggleRow(Tr("In game"), "xc_weather_ingame", &g_Config.m_XcWeatherInGame);
		ToggleRow(Tr("In the main menu"), "xc_weather_menu", &g_Config.m_XcWeatherInMenu);
		EndCard();
	}
	EndCardGrid();
}

void CXcMenu::RenderVisuals()
{
	const char *apIcons[NUM_VISUALS] = {XC_ICON_USER, XC_ICON_BOLT, XC_ICON_CROSSHAIR, XC_ICON_GLOBE};
	const char *apLabels[NUM_VISUALS] = {Tr("Tee"), Tr("Movement"), Tr("Hook and weapons"), Tr("World")};
	const float NavWidth = S(210.0f);
	const ImVec2 Start = ImGui::GetCursorScreenPos();
	if(SideNav("##visual_sections", &m_VisualsSection, apIcons, apLabels, NUM_VISUALS, NavWidth))
		m_ScrollTarget = 0.0f;
	ImGui::SetCursorScreenPos(ImVec2(Start.x + NavWidth + S(20.0f), Start.y));
	const ImVec2 Avail = ImGui::GetContentRegionAvail();
	BeginSmoothScroll("##visuals_scroll", ImVec2(Avail.x, Avail.y));
	Header(apLabels[m_VisualsSection]);
	ImGui::Dummy(ImVec2(0.0f, S(4.0f)));
	switch(m_VisualsSection)
	{
	case VISUALS_TEE: RenderVisualsTee(); break;
	case VISUALS_MOVEMENT: RenderVisualsMovement(); break;
	case VISUALS_WEAPONS: RenderVisualsWeapons(); break;
	default: RenderVisualsWorld(); break;
	}
	if(m_ScrollToHighlight)
	{
		const float CardY = HighlightedCardY();
		if(CardY >= 0.0f)
		{
			m_ScrollTarget = std::max(CardY - S(12.0f), 0.0f);
			m_ScrollToHighlight = false;
		}
	}
	ImGui::Dummy(ImVec2(0.0f, S(12.0f)));
	EndSmoothScroll();
}

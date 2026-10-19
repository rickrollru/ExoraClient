// Visuals page: every effect is a card with its style, color, sizes and "mine / dummy / others".
#include "feedback.h"
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

	const char *apGradients[] = {Tr("Accent"), Tr("Rainbow"), Tr("Skin"), Tr("Two colors")};
	if(StyleCard("##fx_name_gradient", Tr("Gradient name"), Tr("The name above the tee in a smooth gradient that can flow"), "xc_name_gradient", &g_Config.m_XcNameGradient, apGradients, 4))
	{
		if(g_Config.m_XcNameGradient == 4)
		{
			// the own part of the picker for each of the two colors
			int OwnMode = 1;
			ParamLabel(Tr("First color"));
			EffectColorPicker("##gradient_color1", &OwnMode, &g_Config.m_XcNameGradientColor1);
			ParamLabel(Tr("Second color"));
			EffectColorPicker("##gradient_color2", &OwnMode, &g_Config.m_XcNameGradientColor2);
		}
		ToggleRow(Tr("Flowing"), "xc_name_gradient_flow", &g_Config.m_XcNameGradientFlow);
		TargetChips("##gradient_targets", &g_Config.m_XcNameGradientSelf, &g_Config.m_XcNameGradientDummy, &g_Config.m_XcNameGradientOthers);
		EndCard();
	}
	if(BeginCard("##fx_flying_names", Tr("Flying names"), Tr("Names follow the tees on a soft spring, like kites"), &g_Config.m_XcFlyingNames))
	{
		ConfigSlider(Tr("Lag"), "xc_flying_names_lag", &g_Config.m_XcFlyingNamesLag, 30, 300, "%d%%");
		EndCard();
	}
	if(BeginCard("##fx_jelly", Tr("Jelly tee"), Tr("Squashes when landing, stretches when jumping and wobbles back"), &g_Config.m_XcJelly))
	{
		ConfigSlider(Tr("Softness"), "xc_jelly_strength", &g_Config.m_XcJellyStrength, 10, 300, "%d%%");
		TargetChips("##jelly_targets", &g_Config.m_XcJellySelf, &g_Config.m_XcJellyDummy, &g_Config.m_XcJellyOthers);
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
	const char *apCapes[] = {Tr("Cape"), Tr("Scarf")};
	if(StyleCard("##fx_cape", Tr("Cape"), Tr("A cloth cape or a scarf that flies behind the tee"), "xc_cape", &g_Config.m_XcCape, apCapes, 2))
	{
		EffectColorPicker("##cape_color", &g_Config.m_XcCapeColorMode, &g_Config.m_XcCapeColor);
		ConfigSlider(Tr("Length"), "xc_cape_length", &g_Config.m_XcCapeLength, 50, 200, "%d%%");
		ConfigSlider(Tr("Width"), "xc_cape_width", &g_Config.m_XcCapeWidth, 50, 200, "%d%%");
		ConfigSlider(Tr("Wind"), "xc_cape_wind", &g_Config.m_XcCapeWind, 0, 200, "%d%%");
		ConfigSlider(Tr("Opacity"), "xc_cape_alpha", &g_Config.m_XcCapeAlpha, 10, 100, "%d%%");
		TargetChips("##cape_targets", &g_Config.m_XcCapeSelf, &g_Config.m_XcCapeDummy, &g_Config.m_XcCapeOthers);
		EndCard();
	}

	if(BeginCard("##fx_freeze", Tr("Freeze"), Tr("An icy aura around frozen tees and shards when the ice breaks"), &g_Config.m_XcFreezeFx))
	{
		EffectColorPicker("##freeze_color", &g_Config.m_XcFreezeColorMode, &g_Config.m_XcFreezeColor);
		ToggleRow(Tr("Shards on freeze and unfreeze"), "xc_freeze_burst", &g_Config.m_XcFreezeBurst);
		TargetChips("##freeze_targets", &g_Config.m_XcFreezeSelf, &g_Config.m_XcFreezeDummy, &g_Config.m_XcFreezeOthers);
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
		ToggleRow(Tr("Own laser beam instead of the stock one"), "xc_replace_laser", &g_Config.m_XcReplaceLaser);
		ToggleRow(Tr("Hide the stock smoke trails"), "xc_replace_trails", &g_Config.m_XcReplaceTrails);
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
	const char *apHammer[] = {Tr("Ring"), Tr("Sparks"), Tr("Stars")};
	if(StyleCard("##fx_hammer", Tr("Hammer hit"), Tr("A flash where the hammer lands"), "xc_hammer_fx", &g_Config.m_XcHammerFx, apHammer, 3))
	{
		EffectColorPicker("##hammer_color", &g_Config.m_XcHammerColorMode, &g_Config.m_XcHammerColor);
		ToggleRow(Tr("Hide the stock hit"), "xc_replace_hammer", &g_Config.m_XcReplaceHammer);
		EndCard();
	}
	const char *apExplosions[] = {Tr("Shockwave"), Tr("Neon"), Tr("Sparks")};
	if(StyleCard("##fx_explosion", Tr("Grenade explosion"), Tr("A shockwave, neon rings or hot sparks where a grenade explodes"), "xc_explosion_fx", &g_Config.m_XcExplosionFx, apExplosions, 3))
	{
		EffectColorPicker("##explosion_color", &g_Config.m_XcExplosionColorMode, &g_Config.m_XcExplosionColor);
		ConfigSlider(Tr("Size"), "xc_explosion_size", &g_Config.m_XcExplosionSize, 25, 250, "%d%%");
		ToggleRow(Tr("Hide the stock explosion"), "xc_replace_explosion", &g_Config.m_XcReplaceExplosion);
		EndCard();
	}
	const char *apCrosshairs[] = {Tr("Dot"), Tr("Cross"), Tr("Circle"), Tr("Circle + dot")};
	if(StyleCard("##fx_crosshair", Tr("Crosshair"), Tr("Your own aim instead of the weapon cursor"), "xc_crosshair", &g_Config.m_XcCrosshair, apCrosshairs, 4))
	{
		EffectColorPicker("##crosshair_color", &g_Config.m_XcCrosshairColorMode, &g_Config.m_XcCrosshairColor);
		ConfigSlider(Tr("Size"), "xc_crosshair_size", &g_Config.m_XcCrosshairSize, 30, 300, "%d%%");
		ConfigSlider(Tr("Gap"), "xc_crosshair_gap", &g_Config.m_XcCrosshairGap, 0, 300, "%d%%");
		ToggleRow(Tr("Glow"), "xc_crosshair_glow", &g_Config.m_XcCrosshairGlow);
		ToggleRow(Tr("Dark outline"), "xc_crosshair_outline", &g_Config.m_XcCrosshairOutline);
		EndCard();
	}
	EndCardGrid();
}

void CXcMenu::RenderVisualsWorld()
{
	BeginCardGrid("##vis_world", 2);
	if(BeginCard("##fx_glow_tune", Tr("Glow"), Tr("How bright and how wide every effect glows")))
	{
		ConfigSlider(Tr("Bloom halo"), "xc_bloom", &g_Config.m_XcBloom, 0, 200, "%d%%");
		ConfigSlider(Tr("Bloom threshold"), "xc_bloom_threshold", &g_Config.m_XcBloomThreshold, 0, 100, "%d%%");
		ConfigSlider(Tr("All effects: strength"), "xc_fxglow_all", &g_Config.m_XcFxglowAll, 0, 300, "%d%%");
		ConfigSlider(Tr("All effects: size"), "xc_fxglow_all_size", &g_Config.m_XcFxglowAllSize, 25, 300, "%d%%");

		// one effect at a time: its chip, then its two sliders; a dot marks effects that differ from 100 %
		struct SGroup
		{
			const char *m_pName;
			const char *m_pStrength;
			int *m_pStrengthValue;
			const char *m_pSize;
			int *m_pSizeValue;
		};
		const SGroup aGroups[] = {
			{Tr("Trail"), "xc_fxglow_trail", &g_Config.m_XcFxglowTrail, "xc_fxglow_trail_size", &g_Config.m_XcFxglowTrailSize},
			{Tr("Hook"), "xc_fxglow_hook", &g_Config.m_XcFxglowHook, "xc_fxglow_hook_size", &g_Config.m_XcFxglowHookSize},
			{Tr("Particles"), "xc_fxglow_particles", &g_Config.m_XcFxglowParticles, "xc_fxglow_particles_size", &g_Config.m_XcFxglowParticlesSize},
			{Tr("Pickups and shots"), "xc_fxglow_items", &g_Config.m_XcFxglowItems, "xc_fxglow_items_size", &g_Config.m_XcFxglowItemsSize},
			{Tr("Lasers"), "xc_fxglow_lasers", &g_Config.m_XcFxglowLasers, "xc_fxglow_lasers_size", &g_Config.m_XcFxglowLasersSize},
			{Tr("Accessory"), "xc_fxglow_accessory", &g_Config.m_XcFxglowAccessory, "xc_fxglow_accessory_size", &g_Config.m_XcFxglowAccessorySize},
			{Tr("Freeze"), "xc_fxglow_freeze", &g_Config.m_XcFxglowFreeze, "xc_fxglow_freeze_size", &g_Config.m_XcFxglowFreezeSize},
			{Tr("Crosshair"), "xc_fxglow_crosshair", &g_Config.m_XcFxglowCrosshair, "xc_fxglow_crosshair_size", &g_Config.m_XcFxglowCrosshairSize},
			{Tr("Weather"), "xc_fxglow_weather", &g_Config.m_XcFxglowWeather, "xc_fxglow_weather_size", &g_Config.m_XcFxglowWeatherSize},
			{Tr("Floating lights"), "xc_fxglow_floaters", &g_Config.m_XcFxglowFloaters, "xc_fxglow_floaters_size", &g_Config.m_XcFxglowFloatersSize},
			{Tr("Dummy arrow"), "xc_fxglow_arrow", &g_Config.m_XcFxglowArrow, "xc_fxglow_arrow_size", &g_Config.m_XcFxglowArrowSize},
		};
		constexpr int NUM_GROUPS = (int)std::size(aGroups);
		const char *apNames[NUM_GROUPS];
		bool aChanged[NUM_GROUPS];
		bool AnyChanged = g_Config.m_XcFxglowAll != 100 || g_Config.m_XcFxglowAllSize != 100;
		for(int i = 0; i < NUM_GROUPS; i++)
		{
			apNames[i] = aGroups[i].m_pName;
			aChanged[i] = *aGroups[i].m_pStrengthValue != 100 || *aGroups[i].m_pSizeValue != 100;
			AnyChanged |= aChanged[i];
		}
		static int s_Group = 0;
		ParamLabel(Tr("Effect"));
		ChoiceChips("##glow_groups", &s_Group, apNames, NUM_GROUPS, aChanged);
		const SGroup &Group = aGroups[std::clamp(s_Group, 0, NUM_GROUPS - 1)];
		ConfigSlider(Tr("Strength"), Group.m_pStrength, Group.m_pStrengthValue, 0, 300, "%d%%");
		ConfigSlider(Tr("Size"), Group.m_pSize, Group.m_pSizeValue, 25, 300, "%d%%");
		if(AnyChanged && Button(Tr("Reset all glow")))
		{
			SetConfigInt("xc_fxglow_all", 100);
			SetConfigInt("xc_fxglow_all_size", 100);
			for(const SGroup &Reset : aGroups)
			{
				SetConfigInt(Reset.m_pStrength, 100);
				SetConfigInt(Reset.m_pSize, 100);
			}
			Toast(Tr("Glow reset"), TOAST_INFO);
		}
		EndCard();
	}
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
	const char *apFloaters[] = {Tr("Cubes"), Tr("Orbs"), Tr("Both")};
	if(StyleCard("##fx_floaters", Tr("Floating lights"), Tr("Glowing cubes and orbs drift around, your tee pushes them away"), "xc_floaters", &g_Config.m_XcFloaters, apFloaters, 3))
	{
		EffectColorPicker("##floaters_color", &g_Config.m_XcFloatersColorMode, &g_Config.m_XcFloatersColor);
		ConfigSlider(Tr("Amount"), "xc_floaters_count", &g_Config.m_XcFloatersCount, 5, 150);
		ConfigSlider(Tr("Size"), "xc_floaters_size", &g_Config.m_XcFloatersSize, 30, 300, "%d%%");
		ConfigSlider(Tr("Speed"), "xc_floaters_speed", &g_Config.m_XcFloatersSpeed, 0, 300, "%d%%");
		ConfigSlider(Tr("Opacity"), "xc_floaters_alpha", &g_Config.m_XcFloatersAlpha, 10, 100, "%d%%");
		ToggleRow(Tr("Glow"), "xc_floaters_glow", &g_Config.m_XcFloatersGlow);
		ToggleRow(Tr("Your tee pushes them"), "xc_floaters_push", &g_Config.m_XcFloatersPush);
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
	const char *apTints[] = {Tr("Warm"), Tr("Cool"), Tr("Accent"), Tr("Night")};
	if(StyleCard("##fx_tint", Tr("World color"), Tr("A soft color filter over the map"), "xc_world_tint", &g_Config.m_XcWorldTint, apTints, 4))
	{
		ConfigSlider(Tr("Strength"), "xc_world_tint_strength", &g_Config.m_XcWorldTintStrength, 5, 100, "%d%%");
		EndCard();
	}
	if(BeginCard("##fx_over_tiles", Tr("Over the blocks"), Tr("Effects and hats can be drawn over the map blocks, they stay visible behind walls")))
	{
		ToggleRow(Tr("Effects over the blocks"), "xc_fx_over_tiles", &g_Config.m_XcFxOverTiles);
		ToggleRow(Tr("Hats over the blocks"), "xc_hats_over_tiles", &g_Config.m_XcHatsOverTiles);
		Description(Tr("Jumps, rings, freeze, death, finish and accessories. Hats placed behind the tee stay behind"));
		EndCard();
	}
	if(BeginCard("##fx_vignette", Tr("Vignette"), Tr("Darker screen edges to focus on the center")))
	{
		ConfigSlider(Tr("Strength"), "xc_vignette", &g_Config.m_XcVignette, 0, 100, "%d%%");
		EndCard();
	}
	EndCardGrid();
}

void CXcMenu::RenderVisualsEvents()
{
	BeginCardGrid("##vis_events", 2);
	const char *apFinish[] = {Tr("Confetti"), Tr("Fireworks")};
	if(StyleCard("##fx_finish", Tr("Finish"), Tr("Confetti or fireworks when you finish the map"), "xc_finish_fx", &g_Config.m_XcFinishFx, apFinish, 2))
	{
		EffectColorPicker("##finish_color", &g_Config.m_XcFinishColorMode, &g_Config.m_XcFinishColor);
		ConfigSlider(Tr("Amount"), "xc_finish_amount", &g_Config.m_XcFinishAmount, 25, 300, "%d%%");
		ToggleRow(Tr("Banner with the time and records"), "xc_finish_banner", &g_Config.m_XcFinishBanner);
		TargetChips("##finish_targets", &g_Config.m_XcFinishSelf, &g_Config.m_XcFinishDummy, &g_Config.m_XcFinishOthers);
		const int Local = GameClient()->m_Snap.m_LocalClientId;
		if(IsOnline() && Local >= 0 && GameClient()->m_Snap.m_aCharacters[Local].m_Active)
		{
			if(Button(Tr("Show"), true, -1.0f))
			{
				// closes the menu so the effect is seen right on the tee
				SetActive(false);
				GameClient()->m_XcEventFx.Finish(GameClient()->m_aClients[Local].m_RenderPos, Local);
				if(g_Config.m_XcFinishBanner)
					GameClient()->m_XcHud.ShowBanner(Tr("New personal record!"), "00:42.137  (-0.52)", 1);
			}
		}
		else
			Description(Tr("Join a server to try it"));
		EndCard();
	}
	const char *apDeath[] = {Tr("Shards"), Tr("Sparks"), Tr("Ash")};
	if(StyleCard("##fx_death", Tr("Death and spawn"), Tr("A tee bursts into pieces, rings close in where it appears again"), "xc_death_fx", &g_Config.m_XcDeathFx, apDeath, 3))
	{
		EffectColorPicker("##death_color", &g_Config.m_XcDeathColorMode, &g_Config.m_XcDeathColor);
		ToggleRow(Tr("Rings on spawn"), "xc_spawn_fx", &g_Config.m_XcSpawnFx);
		TargetChips("##death_targets", &g_Config.m_XcDeathSelf, &g_Config.m_XcDeathDummy, &g_Config.m_XcDeathOthers);
		EndCard();
	}
	if(BeginCard("##fx_teleport", Tr("Teleports"), Tr("A flash where your tee or dummy leaves and arrives"), &g_Config.m_XcTeleportFx))
	{
		EffectColorPicker("##teleport_color", &g_Config.m_XcTeleportColorMode, &g_Config.m_XcTeleportColor);
		EndCard();
	}
	if(BeginCard("##fx_juice", Tr("Juice"), Tr("Screen flashes and camera shake on freeze, death, finish and explosions")))
	{
		ToggleRow(Tr("Screen flash"), "xc_screen_flash", &g_Config.m_XcScreenFlash);
		ConfigSlider(Tr("Flash strength"), "xc_screen_flash_strength", &g_Config.m_XcScreenFlashStrength, 5, 100, "%d%%");
		ToggleRow(Tr("Camera shake"), "xc_camera_shake", &g_Config.m_XcCameraShake);
		ConfigSlider(Tr("Shake strength"), "xc_camera_shake_strength", &g_Config.m_XcCameraShakeStrength, 10, 300, "%d%%");
		Description(Tr("Only the picture moves, aim and movement stay the same"));
		EndCard();
	}
	if(BeginCard("##fx_event_sounds", Tr("Event sounds"), Tr("Short sounds when you freeze, finish, set a record or teleport"), &g_Config.m_XcEventSounds))
	{
		ConfigSlider(Tr("Volume"), "xc_event_sound_volume", &g_Config.m_XcEventSoundVolume, 0, 100, "%d%%");
		ToggleRow(Tr("Freeze and unfreeze"), "xc_event_sound_freeze", &g_Config.m_XcEventSoundFreeze);
		ToggleRow(Tr("Finish and records"), "xc_event_sound_finish", &g_Config.m_XcEventSoundFinish);
		ToggleRow(Tr("Teleports"), "xc_event_sound_teleport", &g_Config.m_XcEventSoundTeleport);
		ToggleRow(Tr("Death"), "xc_event_sound_death", &g_Config.m_XcEventSoundDeath);
		// listen without playing
		const char *apNames[] = {Tr("Finish"), Tr("Record"), Tr("Freeze"), Tr("Unfreeze"), Tr("Death"), Tr("Teleport")};
		ParamLabel(Tr("Listen"));
		const float Gap = S(6.0f);
		const float Width = (AvailWidth() - Gap * 2.0f) / 3.0f;
		for(int i = 0; i < CXcEventFx::NUM_EVENT_SOUNDS; i++)
		{
			if(i % 3)
				ImGui::SameLine(0.0f, Gap);
			ImGui::PushID(i);
			if(Button(apNames[i], false, Width))
			{
				// the preview plays even when this sound is switched off
				const int Old = g_Config.m_XcEventSounds;
				int *apEnabled[] = {&g_Config.m_XcEventSoundFinish, &g_Config.m_XcEventSoundFreeze, &g_Config.m_XcEventSoundDeath, &g_Config.m_XcEventSoundTeleport};
				int aOld[4];
				for(int k = 0; k < 4; k++)
				{
					aOld[k] = *apEnabled[k];
					*apEnabled[k] = 1;
				}
				g_Config.m_XcEventSounds = 1;
				GameClient()->m_XcEventFx.PlayEventSound(i);
				g_Config.m_XcEventSounds = Old;
				for(int k = 0; k < 4; k++)
					*apEnabled[k] = aOld[k];
			}
			ImGui::PopID();
		}
		EndCard();
	}
	if(BeginCard("##fx_stock", Tr("Stock effects"), Tr("Hide the standard game effects so only ours stay, sounds remain")))
	{
		ToggleRow(Tr("Hammer hit"), "xc_replace_hammer", &g_Config.m_XcReplaceHammer);
		ToggleRow(Tr("Grenade explosion and smoke"), "xc_replace_explosion", &g_Config.m_XcReplaceExplosion);
		ToggleRow(Tr("Rifle and shotgun lasers"), "xc_replace_laser", &g_Config.m_XcReplaceLaser);
		ToggleRow(Tr("Smoke trails of bullets and grenades"), "xc_replace_trails", &g_Config.m_XcReplaceTrails);
		ToggleRow(Tr("Double jump puffs"), "xc_replace_air_jump", &g_Config.m_XcReplaceAirJump);
		ToggleRow(Tr("Skid smoke when turning"), "xc_replace_skid", &g_Config.m_XcReplaceSkid);
		ToggleRow(Tr("Purple spawn smoke"), "xc_replace_spawn", &g_Config.m_XcReplaceSpawn);
		ToggleRow(Tr("Blood on death"), "xc_replace_death", &g_Config.m_XcReplaceDeath);
		ToggleRow(Tr("Snowflakes on frozen tees"), "xc_replace_freeze", &g_Config.m_XcReplaceFreeze);
		Description(Tr("Turn on our effect for the same event in its card, otherwise nothing is shown"));
		EndCard();
	}
	if(BeginCard("##fx_dummy_arrow", Tr("Arrows to players"), Tr("Glowing arrows at the screen edge show where the dummy, team mates and friends are")))
	{
		ToggleRow(Tr("Dummy"), "xc_dummy_arrow", &g_Config.m_XcDummyArrow);
		ToggleRow(Tr("Team mates"), "xc_team_arrows", &g_Config.m_XcTeamArrows);
		ToggleRow(Tr("Friends"), "xc_friend_arrows", &g_Config.m_XcFriendArrows);
		ParamLabel(Tr("Dummy arrow color"));
		EffectColorPicker("##arrow_color", &g_Config.m_XcDummyArrowColorMode, &g_Config.m_XcDummyArrowColor);
		ConfigSlider(Tr("Size"), "xc_dummy_arrow_size", &g_Config.m_XcDummyArrowSize, 50, 250, "%d%%");
		EndCard();
	}
	EndCardGrid();
}

void CXcMenu::RenderVisuals()
{
	const char *apIcons[NUM_VISUALS] = {XC_ICON_USER, XC_ICON_BOLT, XC_ICON_CROSSHAIR, XC_ICON_GLOBE, XC_ICON_STAR, XC_ICON_HAT};
	const char *apLabels[NUM_VISUALS] = {Tr("Tee"), Tr("Movement"), Tr("Hook and weapons"), Tr("World"), Tr("Events"), Tr("Cosmetics")};
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
	case VISUALS_EVENTS: RenderVisualsEvents(); break;
	case VISUALS_COSMETICS: RenderVisualsCosmetics(); break;
	default: RenderVisualsWorld(); break;
	}
	if(m_VisualsSection != VISUALS_COSMETICS)
		m_WheelCaptured = false;
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

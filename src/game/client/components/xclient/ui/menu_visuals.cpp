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
	const char *apTints[] = {Tr("Warm"), Tr("Cool"), Tr("Accent"), Tr("Night")};
	if(StyleCard("##fx_tint", Tr("World color"), Tr("A soft color filter over the map"), "xc_world_tint", &g_Config.m_XcWorldTint, apTints, 4))
	{
		ConfigSlider(Tr("Strength"), "xc_world_tint_strength", &g_Config.m_XcWorldTintStrength, 5, 100, "%d%%");
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
	if(BeginCard("##fx_dummy_arrow", Tr("Dummy arrow"), Tr("A glowing arrow at the screen edge shows where the dummy is"), &g_Config.m_XcDummyArrow))
	{
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

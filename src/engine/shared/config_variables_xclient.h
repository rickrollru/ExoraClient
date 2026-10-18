// This file can be included several times.

// Settings of our own client (working prefix "xc_", the project name is not final).
// Saved to settings_xclient.cfg.

#ifndef MACRO_CONFIG_INT
#error "The config macros must be defined"
#define MACRO_CONFIG_INT(Name, ScriptName, Def, Min, Max, Save, Desc) ;
#define MACRO_CONFIG_COL(Name, ScriptName, Def, Save, Desc) ;
#define MACRO_CONFIG_STR(Name, ScriptName, Len, Def, Save, Desc) ;
#endif

// Render module
MACRO_CONFIG_INT(XcRender, xc_render, 1, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Use the XClient render module for effects (needs OpenGL 3.3)")
MACRO_CONFIG_INT(XcBloom, xc_bloom, 100, 0, 200, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Bloom strength of glowing effects in percent")
MACRO_CONFIG_INT(XcBloomThreshold, xc_bloom_threshold, 10, 0, 100, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Only parts brighter than this (percent) glow")
MACRO_CONFIG_INT(XcFxQuality, xc_fx_quality, 2, 0, 2, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Effect quality (0 = low, no bloom, 1 = medium, 2 = high)")
MACRO_CONFIG_INT(XcMenuBlur, xc_menu_blur, 1, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Blur the game behind the menu")

// New menu (ImGui)
MACRO_CONFIG_INT(XcNewMenu, xc_new_menu, 1, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Use the new XClient menu instead of the classic one")
MACRO_CONFIG_INT(XcUiTheme, xc_ui_theme, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Menu theme (0 = dark, 1 = light)")
MACRO_CONFIG_INT(XcUiAccent, xc_ui_accent, 0, 0, 6, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Menu accent color (0 blue, 1 mint, 2 orange, 3 pink, 4 lime, 5 red, 6 white)")
MACRO_CONFIG_INT(XcUiFont, xc_ui_font, 0, 0, 2, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Menu font (0 = Manrope, 1 = Onest, 2 = Unbounded)")
MACRO_CONFIG_INT(XcUiScale, xc_ui_scale, 0, 0, 300, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Menu scale in percent (0 = automatic by screen height)")
MACRO_CONFIG_INT(XcUiAnimations, xc_ui_animations, 2, 0, 3, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Menu animations (0 = off, 1 = fast, 2 = normal, 3 = smooth)")
MACRO_CONFIG_INT(XcUiSounds, xc_ui_sounds, 1, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Play interface sounds in the menu")
MACRO_CONFIG_INT(XcUiSoundVolume, xc_ui_sound_volume, 60, 0, 100, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Volume of interface sounds in percent")
MACRO_CONFIG_STR(XcStatsUrl, xc_stats_url, 128, "https://ddnet.org/players/?json2=%s", CFGFLAG_CLIENT | CFGFLAG_SAVE, "Where the home page gets player stats from, %s is the player name")
MACRO_CONFIG_INT(XcMenuBackground, xc_menu_background, 1, 0, 3, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Menu background (0 = blurred game, 1 = animated, 2 = map / plain game, 3 = own picture or gif)")
MACRO_CONFIG_STR(XcMenuBgFile, xc_menu_bg_file, 128, "", CFGFLAG_CLIENT | CFGFLAG_SAVE, "Own menu background, a file in xclient/backgrounds")
MACRO_CONFIG_INT(XcMenuBgDim, xc_menu_bg_dim, 25, 0, 90, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Darken the own menu background (percent)")
MACRO_CONFIG_INT(XcMenuBgBlur, xc_menu_bg_blur, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Blur the own menu background (needs OpenGL 3.3)")
MACRO_CONFIG_INT(XcMenuBgGifSpeed, xc_menu_bg_gif_speed, 100, 25, 300, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Speed of gif backgrounds in percent")

// New HUD (positions are the element centers in 1/1000 of the screen, scale in percent)
MACRO_CONFIG_INT(XcHud, xc_hud, 1, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Show the XClient HUD")
MACRO_CONFIG_INT(XcHudWatermark, xc_hud_watermark, 1, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "HUD: watermark with name, FPS, ping and time")
MACRO_CONFIG_INT(XcHudWatermarkX, xc_hud_watermark_x, 110, 0, 1000, CFGFLAG_CLIENT | CFGFLAG_SAVE, "HUD: watermark position x")
MACRO_CONFIG_INT(XcHudWatermarkY, xc_hud_watermark_y, 32, 0, 1000, CFGFLAG_CLIENT | CFGFLAG_SAVE, "HUD: watermark position y")
MACRO_CONFIG_INT(XcHudWatermarkScale, xc_hud_watermark_scale, 100, 50, 200, CFGFLAG_CLIENT | CFGFLAG_SAVE, "HUD: watermark size")
MACRO_CONFIG_INT(XcHudTimer, xc_hud_timer, 1, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "HUD: race timer (replaces the classic one)")
MACRO_CONFIG_INT(XcHudTimerX, xc_hud_timer_x, 500, 0, 1000, CFGFLAG_CLIENT | CFGFLAG_SAVE, "HUD: timer position x")
MACRO_CONFIG_INT(XcHudTimerY, xc_hud_timer_y, 38, 0, 1000, CFGFLAG_CLIENT | CFGFLAG_SAVE, "HUD: timer position y")
MACRO_CONFIG_INT(XcHudTimerScale, xc_hud_timer_scale, 100, 50, 200, CFGFLAG_CLIENT | CFGFLAG_SAVE, "HUD: timer size")
MACRO_CONFIG_INT(XcHudSpeed, xc_hud_speed, 1, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "HUD: speed")
MACRO_CONFIG_INT(XcHudSpeedX, xc_hud_speed_x, 455, 0, 1000, CFGFLAG_CLIENT | CFGFLAG_SAVE, "HUD: speed position x")
MACRO_CONFIG_INT(XcHudSpeedY, xc_hud_speed_y, 905, 0, 1000, CFGFLAG_CLIENT | CFGFLAG_SAVE, "HUD: speed position y")
MACRO_CONFIG_INT(XcHudSpeedScale, xc_hud_speed_scale, 100, 50, 200, CFGFLAG_CLIENT | CFGFLAG_SAVE, "HUD: speed size")
MACRO_CONFIG_INT(XcHudWeapon, xc_hud_weapon, 1, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "HUD: current weapon")
MACRO_CONFIG_INT(XcHudWeaponX, xc_hud_weapon_x, 565, 0, 1000, CFGFLAG_CLIENT | CFGFLAG_SAVE, "HUD: weapon position x")
MACRO_CONFIG_INT(XcHudWeaponY, xc_hud_weapon_y, 905, 0, 1000, CFGFLAG_CLIENT | CFGFLAG_SAVE, "HUD: weapon position y")
MACRO_CONFIG_INT(XcHudWeaponScale, xc_hud_weapon_scale, 100, 50, 200, CFGFLAG_CLIENT | CFGFLAG_SAVE, "HUD: weapon size")
MACRO_CONFIG_INT(XcHudToasts, xc_hud_toasts, 1, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "HUD: show notifications in game")
MACRO_CONFIG_INT(XcHudPlates, xc_hud_plates, 1, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Dark rounded plates behind name plates")
MACRO_CONFIG_INT(XcHudWheels, xc_hud_wheels, 1, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "New look for the emote wheel and the spectator menu")
// Weather
MACRO_CONFIG_INT(XcWeather, xc_weather, 0, 0, 4, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Weather effect (0 = off, 1 = snow, 2 = leaves, 3 = sakura, 4 = ash)")
MACRO_CONFIG_INT(XcWeatherDensity, xc_weather_density, 50, 1, 100, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Amount of weather particles")
MACRO_CONFIG_INT(XcWeatherSize, xc_weather_size, 100, 25, 300, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Size of weather particles in percent")
MACRO_CONFIG_INT(XcWeatherSpeed, xc_weather_speed, 100, 10, 300, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Falling speed of weather particles in percent")
MACRO_CONFIG_INT(XcWeatherAlpha, xc_weather_alpha, 80, 5, 100, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Opacity of weather particles in percent")
MACRO_CONFIG_INT(XcWeatherInGame, xc_weather_ingame, 1, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Show weather in game")
MACRO_CONFIG_INT(XcWeatherInMenu, xc_weather_menu, 1, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Show weather in the main menu")

// Jump and run effects
MACRO_CONFIG_INT(XcJumpFx, xc_jump_fx, 1, 0, 3, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Effect on jump (0 = off, 1 = rings, 2 = sparks, 3 = stars)")
MACRO_CONFIG_INT(XcJumpFxAir, xc_jump_fx_air, 1, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Also show the jump effect on double jumps")
MACRO_CONFIG_INT(XcLandFx, xc_land_fx, 1, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Ring when landing after a fall")
MACRO_CONFIG_INT(XcRunDust, xc_run_dust, 1, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Dust under the feet when running")
MACRO_CONFIG_INT(XcJumpFxSize, xc_jump_fx_size, 100, 25, 300, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Size of jump and run effects in percent")
MACRO_CONFIG_COL(XcJumpFxColor, xc_jump_fx_color, 10223527, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Color of jump effects") // #4F8CFF
MACRO_CONFIG_INT(XcJumpFxColorMode, xc_jump_fx_color_mode, 0, 0, 3, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Jump effect color (0 = menu accent, 1 = own color, 2 = rainbow, 3 = skin color)")
MACRO_CONFIG_INT(XcJumpFxSelf, xc_jump_fx_self, 1, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Show jump and run effects for yourself")
MACRO_CONFIG_INT(XcJumpFxDummy, xc_jump_fx_dummy, 1, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Show jump and run effects for your dummy")
MACRO_CONFIG_INT(XcJumpFxOthers, xc_jump_fx_others, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Show jump and run effects for other players")

// Trail behind the tee
MACRO_CONFIG_INT(XcTrail, xc_trail, 0, 0, 4, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Trail (0 = off, 1 = ribbon, 2 = sparks, 3 = stars, 4 = comet dots)")
MACRO_CONFIG_INT(XcTrailLength, xc_trail_length, 450, 100, 2000, CFGFLAG_CLIENT | CFGFLAG_SAVE, "How long the trail stays, in milliseconds")
MACRO_CONFIG_INT(XcTrailWidth, xc_trail_width, 100, 25, 300, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Trail width in percent")
MACRO_CONFIG_INT(XcTrailGlow, xc_trail_glow, 100, 0, 200, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Trail glow in percent")
MACRO_CONFIG_INT(XcTrailColorMode, xc_trail_color_mode, 0, 0, 3, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Trail color (0 = menu accent, 1 = own color, 2 = rainbow, 3 = skin color)")
MACRO_CONFIG_COL(XcTrailColor, xc_trail_color, 10223527, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Trail color") // #4F8CFF
MACRO_CONFIG_INT(XcTrailSelf, xc_trail_self, 1, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Trail behind yourself")
MACRO_CONFIG_INT(XcTrailDummy, xc_trail_dummy, 1, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Trail behind your dummy")
MACRO_CONFIG_INT(XcTrailOthers, xc_trail_others, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Trail behind other players")

// Pickups, projectiles and lasers
MACRO_CONFIG_INT(XcItemsGlow, xc_items_glow, 1, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Soft halo around pickups (hearts, armor, weapons, ninja)")
MACRO_CONFIG_INT(XcItemsGlowSize, xc_items_glow_size, 100, 25, 250, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Size of the pickup halo in percent")
MACRO_CONFIG_INT(XcItemsGlowStrength, xc_items_glow_strength, 100, 10, 250, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Brightness of the pickup halo in percent")
MACRO_CONFIG_INT(XcItemsSparkles, xc_items_sparkles, 1, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Small sparkles rising from pickups")
MACRO_CONFIG_INT(XcItemsColorMode, xc_items_color_mode, 0, 0, 3, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Color of item glows (0 = by type, 1 = menu accent, 2 = own color, 3 = rainbow)")
MACRO_CONFIG_COL(XcItemsColor, xc_items_color, 10223527, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Own color of item glows") // #4F8CFF
MACRO_CONFIG_INT(XcProjectileGlow, xc_projectile_glow, 1, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Light around bullets and grenades")
MACRO_CONFIG_INT(XcProjectileTrail, xc_projectile_trail, 1, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Comet tail behind bullets and grenades")
MACRO_CONFIG_INT(XcLaserGlow, xc_laser_glow, 1, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Glow along lasers")
MACRO_CONFIG_INT(XcLaserGlowStrength, xc_laser_glow_strength, 100, 10, 250, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Brightness of the laser glow in percent")

// Accessory on the tee
MACRO_CONFIG_INT(XcAccessory, xc_accessory, 0, 0, 3, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Accessory on the tee (0 = off, 1 = cone hat, 2 = halo, 3 = crown)")
MACRO_CONFIG_INT(XcAccessorySize, xc_accessory_size, 100, 50, 200, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Size of the accessory in percent")
MACRO_CONFIG_INT(XcAccessoryAlpha, xc_accessory_alpha, 85, 10, 100, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Opacity of the accessory in percent")
MACRO_CONFIG_COL(XcAccessoryColor, xc_accessory_color, 10223527, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Color of the accessory (crown gems for the crown)") // #4F8CFF
MACRO_CONFIG_INT(XcAccessoryColorMode, xc_accessory_color_mode, 1, 0, 3, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Accessory color (0 = menu accent, 1 = own color, 2 = rainbow, 3 = skin color)")
MACRO_CONFIG_INT(XcAccessoryPhysics, xc_accessory_physics, 100, 0, 200, CFGFLAG_CLIENT | CFGFLAG_SAVE, "How much the accessory tilts and bounces with movement in percent (0 = rigid)")
MACRO_CONFIG_INT(XcAccessorySelf, xc_accessory_self, 1, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Show the accessory on yourself")
MACRO_CONFIG_INT(XcAccessoryDummy, xc_accessory_dummy, 1, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Show the accessory on your dummy")
MACRO_CONFIG_INT(XcAccessoryOthers, xc_accessory_others, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Show the accessory on other players")

// Hook
MACRO_CONFIG_INT(XcHookStyle, xc_hook_style, 0, 0, 3, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Hook look (0 = default, 1 = colored chain, 2 = solid line, 3 = glowing line)")
MACRO_CONFIG_COL(XcHookColor, xc_hook_color, 10223527, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Hook color") // #4F8CFF
MACRO_CONFIG_INT(XcHookColorMode, xc_hook_color_mode, 0, 0, 3, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Hook color (0 = menu accent, 1 = own color, 2 = rainbow, 3 = skin color)")
MACRO_CONFIG_INT(XcHookWidth, xc_hook_width, 100, 25, 300, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Width of the hook line in percent")
MACRO_CONFIG_INT(XcHookHitFx, xc_hook_hit_fx, 1, 0, 3, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Effect when the hook grabs something (0 = off, 1 = ring, 2 = sparks, 3 = stars)")
MACRO_CONFIG_INT(XcHookSelf, xc_hook_self, 1, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Use the hook look and effects for yourself")
MACRO_CONFIG_INT(XcHookDummy, xc_hook_dummy, 1, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Use the hook look and effects for your dummy")
MACRO_CONFIG_INT(XcHookOthers, xc_hook_others, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Use the hook look and effects for other players")

// Neon outline around tees
MACRO_CONFIG_INT(XcGlow, xc_glow, 1, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Neon glow and outline around tees (needs the render module)")
MACRO_CONFIG_COL(XcGlowColor, xc_glow_color, 10223527, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Glow color") // #4F8CFF
MACRO_CONFIG_INT(XcGlowColorMode, xc_glow_color_mode, 0, 0, 3, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Glow color (0 = menu accent, 1 = own color, 2 = rainbow, 3 = skin color)")
MACRO_CONFIG_INT(XcGlowSize, xc_glow_size, 100, 25, 300, CFGFLAG_CLIENT | CFGFLAG_SAVE, "How far the glow reaches in percent")
MACRO_CONFIG_INT(XcGlowStrength, xc_glow_strength, 100, 10, 300, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Glow brightness in percent")
MACRO_CONFIG_INT(XcGlowOutline, xc_glow_outline, 100, 0, 300, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Width of the crisp outline in percent (0 = only the soft glow)")
MACRO_CONFIG_INT(XcGlowPulse, xc_glow_pulse, 1, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Glow softly pulses")
MACRO_CONFIG_INT(XcGlowSelf, xc_glow_self, 1, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Glow around your tee")
MACRO_CONFIG_INT(XcGlowDummy, xc_glow_dummy, 1, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Glow around your dummy")
MACRO_CONFIG_INT(XcGlowOthers, xc_glow_others, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Glow around other players")

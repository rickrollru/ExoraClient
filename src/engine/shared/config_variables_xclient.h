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
MACRO_CONFIG_INT(XcHudMusic, xc_hud_music, 1, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "HUD: what is playing in the browser or a music app")
MACRO_CONFIG_INT(XcHudMusicX, xc_hud_music_x, 880, 0, 1000, CFGFLAG_CLIENT | CFGFLAG_SAVE, "HUD: music position x")
MACRO_CONFIG_INT(XcHudMusicY, xc_hud_music_y, 60, 0, 1000, CFGFLAG_CLIENT | CFGFLAG_SAVE, "HUD: music position y")
MACRO_CONFIG_INT(XcHudMusicScale, xc_hud_music_scale, 100, 50, 200, CFGFLAG_CLIENT | CFGFLAG_SAVE, "HUD: music size")
MACRO_CONFIG_INT(XcHudMusicAutohide, xc_hud_music_autohide, 1, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "HUD: hide the music a few seconds after it is paused")
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
MACRO_CONFIG_INT(XcTrail, xc_trail, 1, 0, 4, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Trail (0 = off, 1 = ribbon, 2 = sparks, 3 = stars, 4 = comet dots)")
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

// Freeze, hammer, crosshair, world color
MACRO_CONFIG_INT(XcFreezeFx, xc_freeze_fx, 1, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Ice aura around frozen tees")
MACRO_CONFIG_INT(XcFreezeBurst, xc_freeze_burst, 1, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Ice shards when a tee freezes and when the ice breaks")
MACRO_CONFIG_INT(XcFreezeColorMode, xc_freeze_color_mode, 1, 0, 3, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Ice color (0 = menu accent, 1 = own color, 2 = rainbow, 3 = skin color)")
MACRO_CONFIG_COL(XcFreezeColor, xc_freeze_color, 9895812, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Ice color") // light blue
MACRO_CONFIG_INT(XcFreezeSelf, xc_freeze_self, 1, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Ice effects for yourself")
MACRO_CONFIG_INT(XcFreezeDummy, xc_freeze_dummy, 1, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Ice effects for your dummy")
MACRO_CONFIG_INT(XcFreezeOthers, xc_freeze_others, 1, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Ice effects for other players")
MACRO_CONFIG_INT(XcHammerFx, xc_hammer_fx, 2, 0, 3, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Effect when the hammer hits (0 = off, 1 = ring, 2 = sparks, 3 = stars)")
MACRO_CONFIG_INT(XcHammerColorMode, xc_hammer_color_mode, 0, 0, 3, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Hammer effect color (0 = menu accent, 1 = own color, 2 = rainbow, 3 = skin color)")
MACRO_CONFIG_COL(XcHammerColor, xc_hammer_color, 10223527, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Hammer effect color")
MACRO_CONFIG_INT(XcCrosshair, xc_crosshair, 0, 0, 4, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Crosshair (0 = weapon cursor, 1 = dot, 2 = cross, 3 = circle, 4 = circle with dot)")
MACRO_CONFIG_INT(XcCrosshairSize, xc_crosshair_size, 100, 30, 300, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Crosshair size in percent")
MACRO_CONFIG_INT(XcCrosshairGap, xc_crosshair_gap, 100, 0, 300, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Gap in the middle of the crosshair in percent")
MACRO_CONFIG_INT(XcCrosshairColorMode, xc_crosshair_color_mode, 0, 0, 3, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Crosshair color (0 = menu accent, 1 = own color, 2 = rainbow, 3 = skin color)")
MACRO_CONFIG_COL(XcCrosshairColor, xc_crosshair_color, 10223527, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Crosshair color")
MACRO_CONFIG_INT(XcCrosshairGlow, xc_crosshair_glow, 1, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Glowing crosshair")
MACRO_CONFIG_INT(XcCrosshairOutline, xc_crosshair_outline, 1, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Dark outline around the crosshair")
MACRO_CONFIG_INT(XcWorldTint, xc_world_tint, 0, 0, 4, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Color filter over the world (0 = off, 1 = warm, 2 = cool, 3 = accent, 4 = night)")
MACRO_CONFIG_INT(XcWorldTintStrength, xc_world_tint_strength, 60, 5, 100, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Strength of the color filter in percent")
MACRO_CONFIG_INT(XcVignette, xc_vignette, 0, 0, 100, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Dark corners around the screen in percent")

// Finish, records, death, spawn, teleports, dummy arrow
MACRO_CONFIG_INT(XcFinishFx, xc_finish_fx, 1, 0, 2, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Effect at the finish (0 = off, 1 = confetti, 2 = fireworks)")
MACRO_CONFIG_INT(XcFinishAmount, xc_finish_amount, 100, 25, 300, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Amount of confetti or fireworks in percent")
MACRO_CONFIG_INT(XcFinishColorMode, xc_finish_color_mode, 2, 0, 3, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Finish effect color (0 = menu accent, 1 = own color, 2 = rainbow, 3 = skin color)")
MACRO_CONFIG_COL(XcFinishColor, xc_finish_color, 10223527, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Finish effect color")
MACRO_CONFIG_INT(XcFinishBanner, xc_finish_banner, 1, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Banner with the time and new records at the finish")
MACRO_CONFIG_INT(XcFinishSelf, xc_finish_self, 1, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Finish effect for yourself")
MACRO_CONFIG_INT(XcFinishDummy, xc_finish_dummy, 1, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Finish effect for your dummy")
MACRO_CONFIG_INT(XcFinishOthers, xc_finish_others, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Finish effect for other players")
MACRO_CONFIG_INT(XcDeathFx, xc_death_fx, 1, 0, 3, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Effect when a tee dies (0 = off, 1 = shards, 2 = sparks, 3 = ash)")
MACRO_CONFIG_INT(XcDeathColorMode, xc_death_color_mode, 3, 0, 3, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Death effect color (0 = menu accent, 1 = own color, 2 = rainbow, 3 = skin color)")
MACRO_CONFIG_COL(XcDeathColor, xc_death_color, 10223527, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Death effect color")
MACRO_CONFIG_INT(XcDeathSelf, xc_death_self, 1, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Death effect for yourself")
MACRO_CONFIG_INT(XcDeathDummy, xc_death_dummy, 1, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Death effect for your dummy")
MACRO_CONFIG_INT(XcDeathOthers, xc_death_others, 1, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Death effect for other players")
MACRO_CONFIG_INT(XcSpawnFx, xc_spawn_fx, 1, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Rings closing in where a tee spawns")
MACRO_CONFIG_INT(XcTeleportFx, xc_teleport_fx, 1, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Flash when your tee or dummy is teleported")
MACRO_CONFIG_INT(XcTeleportColorMode, xc_teleport_color_mode, 0, 0, 3, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Teleport flash color (0 = menu accent, 1 = own color, 2 = rainbow, 3 = skin color)")
MACRO_CONFIG_COL(XcTeleportColor, xc_teleport_color, 10223527, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Teleport flash color")
MACRO_CONFIG_INT(XcDummyArrow, xc_dummy_arrow, 1, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Arrow at the screen edge pointing to the dummy when it is off screen")
MACRO_CONFIG_INT(XcDummyArrowSize, xc_dummy_arrow_size, 100, 50, 250, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Size of the dummy arrow in percent")
MACRO_CONFIG_INT(XcDummyArrowColorMode, xc_dummy_arrow_color_mode, 0, 0, 3, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Dummy arrow color (0 = menu accent, 1 = own color, 2 = rainbow, 3 = skin color)")
MACRO_CONFIG_COL(XcDummyArrowColor, xc_dummy_arrow_color, 10223527, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Dummy arrow color")

// Juice: screen flashes and camera shake (visual only)
MACRO_CONFIG_INT(XcScreenFlash, xc_screen_flash, 1, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Colored light from the screen edges on freeze, death and finish")
MACRO_CONFIG_INT(XcScreenFlashStrength, xc_screen_flash_strength, 45, 5, 100, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Strength of the screen flashes in percent")
MACRO_CONFIG_INT(XcCameraShake, xc_camera_shake, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Shake the camera on death, freeze and near explosions")
MACRO_CONFIG_INT(XcCameraShakeStrength, xc_camera_shake_strength, 100, 10, 300, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Strength of the camera shake in percent")

// Event sounds (only for your own tee and dummy)
MACRO_CONFIG_INT(XcEventSounds, xc_event_sounds, 1, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "XClient sounds on game events of your own tees")
MACRO_CONFIG_INT(XcEventSoundVolume, xc_event_sound_volume, 60, 0, 100, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Volume of the event sounds in percent")
MACRO_CONFIG_INT(XcEventSoundFreeze, xc_event_sound_freeze, 1, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Sound when you freeze and unfreeze")
MACRO_CONFIG_INT(XcEventSoundFinish, xc_event_sound_finish, 1, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Sound at the finish and for a new record")
MACRO_CONFIG_INT(XcEventSoundDeath, xc_event_sound_death, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Sound when you die")
MACRO_CONFIG_INT(XcEventSoundTeleport, xc_event_sound_teleport, 1, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Sound when you are teleported")

// Own hats: a picture or gif from xclient/hats on top of the tee (only you see it)
MACRO_CONFIG_INT(XcHat, xc_hat, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Show your own hat picture on the tee")
MACRO_CONFIG_STR(XcHatFile, xc_hat_file, 128, "", CFGFLAG_CLIENT | CFGFLAG_SAVE, "Hat picture or gif in xclient/hats")
MACRO_CONFIG_INT(XcHatX, xc_hat_x, 0, -96, 96, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Horizontal hat offset from the tee center (tee size is 64)")
MACRO_CONFIG_INT(XcHatY, xc_hat_y, -34, -128, 96, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Vertical hat offset from the tee center (tee size is 64)")
MACRO_CONFIG_INT(XcHatSize, xc_hat_size, 64, 8, 256, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Hat width (tee size is 64)")
MACRO_CONFIG_INT(XcHatRotation, xc_hat_rotation, 0, -180, 180, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Hat rotation in degrees")
MACRO_CONFIG_INT(XcHatFlip, xc_hat_flip, 1, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Mirror the hat when the tee looks to the left")
MACRO_CONFIG_INT(XcHatSway, xc_hat_sway, 100, 0, 200, CFGFLAG_CLIENT | CFGFLAG_SAVE, "How much the hat tilts and bounces with movement in percent (0 = rigid)")
MACRO_CONFIG_INT(XcHatAlpha, xc_hat_alpha, 100, 10, 100, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Hat opacity in percent")
MACRO_CONFIG_INT(XcHatGifSpeed, xc_hat_gif_speed, 100, 25, 300, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Speed of an animated hat in percent")
MACRO_CONFIG_INT(XcHatSelf, xc_hat_self, 1, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Show the hat on yourself")
MACRO_CONFIG_INT(XcHatDummy, xc_hat_dummy, 1, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Show the hat on your dummy")
MACRO_CONFIG_INT(XcHatOthers, xc_hat_others, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Show the hat on other players too")

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
MACRO_CONFIG_INT(XcHookStyle, xc_hook_style, 3, 0, 3, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Hook look (0 = default, 1 = colored chain, 2 = solid line, 3 = glowing line)")
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

// Music player (system media sessions: browser tabs, Spotify...)
MACRO_CONFIG_INT(XcMusic, xc_music, 1, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Music player: show and control what plays in the browser or a music app")
MACRO_CONFIG_INT(XcMusicToasts, xc_music_toasts, 1, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Music player: a short note when the next track starts")

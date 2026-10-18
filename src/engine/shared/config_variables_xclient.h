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
MACRO_CONFIG_INT(XcJumpFxSelf, xc_jump_fx_self, 1, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Show jump and run effects for yourself")
MACRO_CONFIG_INT(XcJumpFxDummy, xc_jump_fx_dummy, 1, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Show jump and run effects for your dummy")
MACRO_CONFIG_INT(XcJumpFxOthers, xc_jump_fx_others, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Show jump and run effects for other players")

// Accessory on the tee
MACRO_CONFIG_INT(XcAccessory, xc_accessory, 0, 0, 3, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Accessory on the tee (0 = off, 1 = cone hat, 2 = halo, 3 = crown)")
MACRO_CONFIG_INT(XcAccessorySize, xc_accessory_size, 100, 50, 200, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Size of the accessory in percent")
MACRO_CONFIG_INT(XcAccessoryAlpha, xc_accessory_alpha, 85, 10, 100, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Opacity of the accessory in percent")
MACRO_CONFIG_COL(XcAccessoryColor, xc_accessory_color, 10223527, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Color of the accessory (crown gems for the crown)") // #4F8CFF
MACRO_CONFIG_INT(XcAccessoryRainbow, xc_accessory_rainbow, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Animated rainbow color for the accessory")
MACRO_CONFIG_INT(XcAccessoryPhysics, xc_accessory_physics, 100, 0, 200, CFGFLAG_CLIENT | CFGFLAG_SAVE, "How much the accessory tilts and bounces with movement in percent (0 = rigid)")
MACRO_CONFIG_INT(XcAccessorySelf, xc_accessory_self, 1, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Show the accessory on yourself")
MACRO_CONFIG_INT(XcAccessoryDummy, xc_accessory_dummy, 1, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Show the accessory on your dummy")
MACRO_CONFIG_INT(XcAccessoryOthers, xc_accessory_others, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Show the accessory on other players")

// Hook
MACRO_CONFIG_INT(XcHookStyle, xc_hook_style, 0, 0, 3, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Hook look (0 = default, 1 = colored chain, 2 = solid line, 3 = glowing line)")
MACRO_CONFIG_COL(XcHookColor, xc_hook_color, 10223527, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Hook color") // #4F8CFF
MACRO_CONFIG_INT(XcHookRainbow, xc_hook_rainbow, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Animated rainbow hook color")
MACRO_CONFIG_INT(XcHookWidth, xc_hook_width, 100, 25, 300, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Width of the hook line in percent")
MACRO_CONFIG_INT(XcHookHitFx, xc_hook_hit_fx, 1, 0, 3, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Effect when the hook grabs something (0 = off, 1 = ring, 2 = sparks, 3 = stars)")
MACRO_CONFIG_INT(XcHookSelf, xc_hook_self, 1, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Use the hook look and effects for yourself")
MACRO_CONFIG_INT(XcHookDummy, xc_hook_dummy, 1, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Use the hook look and effects for your dummy")
MACRO_CONFIG_INT(XcHookOthers, xc_hook_others, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Use the hook look and effects for other players")

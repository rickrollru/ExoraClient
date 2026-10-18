// This file can be included several times.

// Settings of our own client (working prefix "xc_", the project name is not final).
// Saved to settings_xclient.cfg.

#ifndef MACRO_CONFIG_INT
#error "The config macros must be defined"
#define MACRO_CONFIG_INT(Name, ScriptName, Def, Min, Max, Save, Desc) ;
#define MACRO_CONFIG_COL(Name, ScriptName, Def, Save, Desc) ;
#define MACRO_CONFIG_STR(Name, ScriptName, Len, Def, Save, Desc) ;
#endif

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

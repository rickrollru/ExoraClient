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

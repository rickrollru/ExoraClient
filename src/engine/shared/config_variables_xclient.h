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
MACRO_CONFIG_INT(XcMenuBackground, xc_menu_background, 1, 0, 3, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Menu background (0 = blurred game, 1 = animated, 2 = map / plain game, 3 = own picture or gif)")
MACRO_CONFIG_STR(XcMenuBgFile, xc_menu_bg_file, 128, "", CFGFLAG_CLIENT | CFGFLAG_SAVE, "Own menu background, a file in xclient/backgrounds")
MACRO_CONFIG_INT(XcMenuBgDim, xc_menu_bg_dim, 25, 0, 90, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Darken the own menu background (percent)")
MACRO_CONFIG_INT(XcMenuBgBlur, xc_menu_bg_blur, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Blur the own menu background (needs OpenGL 3.3)")
MACRO_CONFIG_INT(XcMenuBgGifSpeed, xc_menu_bg_gif_speed, 100, 25, 300, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Speed of gif backgrounds in percent")

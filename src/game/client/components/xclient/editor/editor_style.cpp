#include "editor_style.h"

#include <engine/shared/config.h>

#include <algorithm>

#include <game/client/components/xclient/ui/theme.h>

namespace XcEditorStyle
{
bool Enabled()
{
	return g_Config.m_XcEditorStyle != 0;
}

ColorRGBA Accent(float Alpha)
{
	const ImVec4 Color = XcUi::AccentColor(g_Config.m_XcUiAccent, true);
	return ColorRGBA(Color.x, Color.y, Color.z, Alpha);
}

// interface brightness like in Photoshop: darkest, dark, medium gray
static float Lift()
{
	switch(g_Config.m_XcEditorBrightness)
	{
	case 0: return -0.035f;
	case 2: return 0.09f;
	default: return 0.0f;
	}
}

ColorRGBA Window()
{
	const float l = Lift();
	return ColorRGBA(0.106f + l, 0.110f + l, 0.122f + l, 1.0f); // #1B1C1F
}

ColorRGBA Panel()
{
	const float l = Lift();
	return ColorRGBA(0.137f + l, 0.145f + l, 0.161f + l, 1.0f); // #232529
}

float CheckerSize()
{
	if(!Enabled())
		return 32.0f;
	static const float s_aSizes[] = {16.0f, 32.0f, 64.0f};
	return s_aSizes[std::clamp(g_Config.m_XcEditorCheckerSize, 0, 2)];
}

float CheckerBrightness()
{
	if(!Enabled())
		return 1.0f;
	static const float s_aShades[] = {0.25f, 0.38f, 0.62f};
	return s_aShades[std::clamp(g_Config.m_XcEditorCheckerShade, 0, 2)];
}

ColorRGBA PanelLine()
{
	return ColorRGBA(1.0f, 1.0f, 1.0f, 0.07f);
}

static ColorRGBA Gray(float Value, float Alpha = 1.0f)
{
	Value += Lift();
	return ColorRGBA(Value, Value * 1.02f, Value * 1.08f, Alpha);
}

ColorRGBA ButtonColor(int Checked, bool Hot)
{
	switch(Checked)
	{
	case -1: return ColorRGBA(0.0f, 0.0f, 0.0f, 0.35f); // disabled
	case 8: return ColorRGBA(0.0f, 0.0f, 0.0f, 0.0f); // invisible
	case 11: return Hot ? Gray(0.24f) : ColorRGBA(0.0f, 0.0f, 0.0f, 0.0f); // flat menu item
	case 9: return Hot ? ColorRGBA(0.90f, 0.28f, 0.30f, 1.0f) : ColorRGBA(0.80f, 0.24f, 0.27f, 0.9f); // dangerous action
	case 10: return Hot ? ColorRGBA(0.24f, 0.84f, 0.64f, 1.0f) : ColorRGBA(0.20f, 0.70f, 0.54f, 0.9f); // positive action
	case 7: return Accent(Hot ? 0.75f : 0.55f); // selected + game layer
	case 6: return Hot ? Gray(0.30f) : Gray(0.25f); // game layer
	case 5: return Hot ? ColorRGBA(1.0f, 0.62f, 0.32f, 1.0f) : ColorRGBA(1.0f, 0.54f, 0.24f, 0.85f); // selected + should be embedded
	case 4: return Hot ? ColorRGBA(1.0f, 0.54f, 0.24f, 0.8f) : ColorRGBA(1.0f, 0.54f, 0.24f, 0.55f); // should be embedded
	case 3: return Accent(Hot ? 0.6f : 0.4f); // selected + unused
	case 2: return Hot ? Gray(0.24f, 0.8f) : Gray(0.20f, 0.6f); // unused
	case 1: return Accent(Hot ? 1.0f : 0.85f); // selected
	default: return Hot ? Gray(0.27f) : Gray(0.21f); // regular
	}
}

const char *FontFace()
{
	// a copy of Manrope SemiBold named "XClient UI" (data/xclient/fonts/editor), loaded with the TClient fonts
	return Enabled() ? "XClient UI" : "";
}
} // namespace XcEditorStyle

#include "tee_glow.h"

#include "xc_helpers.h"

#include <base/math.h>

#include <engine/client.h>
#include <engine/graphics.h>
#include <engine/shared/config.h>
#include <engine/xc_render.h>

#include <game/client/gameclient.h>

#include <cmath>

using namespace XcHelpers;

// glow reach and outline width at 100 %, in world units (a tee is 64 units wide)
static constexpr float GLOW_RADIUS = 26.0f;
static constexpr float OUTLINE_WIDTH = 2.5f;

ColorRGBA CTeeGlow::GlowColor() const
{
	if(g_Config.m_XcGlowRainbow)
		return RainbowColor(Client()->GlobalTime() * 0.15f, 1.0f);
	return HslConfigColor(g_Config.m_XcGlowColor);
}

bool CTeeGlow::Begin(int ClientId)
{
	if(!g_Config.m_XcGlow || !GameClient()->m_FxRenderer.ModuleActive())
		return false;
	if(!in_range(ClientId, MAX_CLIENTS - 1) || !ShowFor(GameClient(), ClientId, g_Config.m_XcGlowSelf, g_Config.m_XcGlowDummy, g_Config.m_XcGlowOthers))
		return false;
	if(PlayerAlpha(GameClient(), ClientId) <= 0.0f)
		return false;

	// the tee, its weapon and room for the glow
	const vec2 Pos = GameClient()->m_aClients[ClientId].m_RenderPos;
	const float Extent = 80.0f + GLOW_RADIUS * g_Config.m_XcGlowSize / 100.0f;
	const float aWorldRect[4] = {Pos.x - Extent, Pos.y - Extent, Pos.x + Extent, Pos.y + Extent};
	Graphics()->XcCaptureBegin(aWorldRect);
	return true;
}

void CTeeGlow::End(int ClientId)
{
	// world units to pixels with the current zoom
	const CScreenRect Screen = Graphics()->GetScreen();
	const float WorldWidth = Screen.m_BottomRight.x - Screen.m_TopLeft.x;
	const float PixelsPerUnit = WorldWidth > 0.0f ? Graphics()->ScreenWidth() / WorldWidth : 1.0f;

	const ColorRGBA Color = GlowColor();
	float Pulse = 1.0f;
	if(g_Config.m_XcGlowPulse)
		Pulse = 1.0f + 0.12f * std::sin(Client()->GlobalTime() * 2.4f + ClientId);
	const float Alpha = PlayerAlpha(GameClient(), ClientId);

	XcRender::SGlowSettings Settings;
	Settings.m_Color = Color.WithAlpha(Alpha);
	// the outline is a lighter version of the glow, like the hot core of a neon tube
	Settings.m_RimColor = ColorRGBA(mix(Color.r, 1.0f, 0.2f), mix(Color.g, 1.0f, 0.2f), mix(Color.b, 1.0f, 0.2f), Alpha);
	Settings.m_Radius = GLOW_RADIUS * g_Config.m_XcGlowSize / 100.0f * PixelsPerUnit;
	Settings.m_Strength = 2.4f * g_Config.m_XcGlowStrength / 100.0f * Pulse;
	Settings.m_RimWidth = OUTLINE_WIDTH * g_Config.m_XcGlowOutline / 100.0f * PixelsPerUnit;
	Graphics()->XcCaptureEnd(Settings);
}

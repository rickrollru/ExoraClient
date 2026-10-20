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

ColorRGBA CTeeGlow::GlowColor(int ClientId) const
{
	return EffectColor(GameClient(), g_Config.m_XcGlowColorMode, g_Config.m_XcGlowColor, ClientId);
}

bool CTeeGlow::HasGlow(int ClientId) const
{
	return g_Config.m_XcGlow && ShowFor(GameClient(), ClientId, g_Config.m_XcGlowSelf, g_Config.m_XcGlowDummy, g_Config.m_XcGlowOthers);
}

bool CTeeGlow::HasShader(int ClientId) const
{
	return g_Config.m_XcTeeShader && ShowFor(GameClient(), ClientId, g_Config.m_XcTeeShaderSelf, g_Config.m_XcTeeShaderDummy, g_Config.m_XcTeeShaderOthers);
}

bool CTeeGlow::Begin(int ClientId)
{
	if(!GameClient()->m_FxRenderer.ModuleActive() || !in_range(ClientId, MAX_CLIENTS - 1))
		return false;
	if(!HasGlow(ClientId) && !HasShader(ClientId))
		return false;
	if(PlayerAlpha(GameClient(), ClientId) <= 0.0f)
		return false;

	// the tee, its weapon and room for the glow
	const vec2 Pos = GameClient()->m_aClients[ClientId].m_RenderPos;
	const float Extent = 80.0f + GLOW_RADIUS * g_Config.m_XcGlowSize / 100.0f * FxGlow::For(-1).m_Radius;
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

	const ColorRGBA Color = GlowColor(ClientId);
	float Pulse = 1.0f;
	if(g_Config.m_XcGlowPulse)
		Pulse = 1.0f + 0.12f * std::sin(Client()->GlobalTime() * 2.4f + ClientId);
	const float Alpha = PlayerAlpha(GameClient(), ClientId);

	XcRender::SGlowSettings Settings;
	Settings.m_Color = Color.WithAlpha(Alpha);
	// the outline is a lighter version of the glow, like the hot core of a neon tube
	Settings.m_RimColor = ColorRGBA(mix(Color.r, 1.0f, 0.2f), mix(Color.g, 1.0f, 0.2f), mix(Color.b, 1.0f, 0.2f), Alpha);
	Settings.m_Radius = GLOW_RADIUS * g_Config.m_XcGlowSize / 100.0f * FxGlow::For(-1).m_Radius * PixelsPerUnit;
	Settings.m_Strength = 2.4f * g_Config.m_XcGlowStrength / 100.0f * FxGlow::For(-1).m_Strength * Pulse;
	Settings.m_RimWidth = OUTLINE_WIDTH * g_Config.m_XcGlowOutline / 100.0f * PixelsPerUnit;
	Settings.m_DoGlow = HasGlow(ClientId);
	if(HasShader(ClientId))
	{
		// the tee shader: a moving pattern painted over the skin
		Settings.m_FillMode = g_Config.m_XcTeeShader;
		Settings.m_FillColor = EffectColor(GameClient(), g_Config.m_XcTeeShaderColorMode, g_Config.m_XcTeeShaderColor, ClientId);
		Settings.m_FillColor2 = HslConfigColor(g_Config.m_XcTeeShaderColor2);
		Settings.m_FillAlpha = g_Config.m_XcTeeShaderAlpha / 100.0f;
		Settings.m_FillScale = 64.0f * PixelsPerUnit * g_Config.m_XcTeeShaderScale / 100.0f;
		Settings.m_FillTime = Client()->GlobalTime() * g_Config.m_XcTeeShaderSpeed / 100.0f + ClientId * 0.37f;
		Settings.m_KeepShade = g_Config.m_XcTeeShaderShade / 100.0f;
	}
	Graphics()->XcCaptureEnd(Settings);
}

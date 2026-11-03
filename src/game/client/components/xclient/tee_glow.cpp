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
	return g_Config.m_XcGlow && !GameClient()->m_FxRenderer.Reduced() && ShowFor(GameClient(), ClientId, g_Config.m_XcGlowSelf, g_Config.m_XcGlowDummy, g_Config.m_XcGlowOthers);
}

bool CTeeGlow::HasShader(int ClientId) const
{
	return g_Config.m_XcTeeShader && ShowFor(GameClient(), ClientId, g_Config.m_XcTeeShaderSelf, g_Config.m_XcTeeShaderDummy, g_Config.m_XcTeeShaderOthers);
}

bool CTeeGlow::ShaderWhole(int ClientId) const
{
	// always by parts: the whole capture also held the emote bubble, the shader painted it too
	(void)ClientId;
	return false;
}

float CTeeGlow::PixelsPerUnit() const
{
	// world units to pixels with the current zoom
	const CScreenRect Screen = Graphics()->GetScreen();
	const float WorldWidth = Screen.m_BottomRight.x - Screen.m_TopLeft.x;
	return WorldWidth > 0.0f ? Graphics()->ScreenWidth() / WorldWidth : 1.0f;
}

vec2 CTeeGlow::ToPixels(vec2 WorldPos) const
{
	const CScreenRect Screen = Graphics()->GetScreen();
	const vec2 Size = Screen.m_BottomRight - Screen.m_TopLeft;
	if(Size.x == 0.0f || Size.y == 0.0f)
		return vec2(0.0f, 0.0f);
	return vec2((WorldPos.x - Screen.m_TopLeft.x) / Size.x * Graphics()->ScreenWidth(), (WorldPos.y - Screen.m_TopLeft.y) / Size.y * Graphics()->ScreenHeight());
}

void CTeeGlow::ShaderSettings(int ClientId, float Ppu, vec2 OriginPixels, XcRender::SGlowSettings &Settings) const
{
	// the tee shader: a moving pattern painted over the skin, pinned to the tee so it moves and zooms with it
	Settings.m_FillMode = g_Config.m_XcTeeShader;
	Settings.m_FillColor = EffectColor(GameClient(), g_Config.m_XcTeeShaderColorMode, g_Config.m_XcTeeShaderColor, ClientId);
	Settings.m_FillColor2 = HslConfigColor(g_Config.m_XcTeeShaderColor2);
	Settings.m_FillAlpha = g_Config.m_XcTeeShaderAlpha / 100.0f;
	Settings.m_FillScale = 64.0f * Ppu * g_Config.m_XcTeeShaderScale / 100.0f;
	Settings.m_FillTime = Client()->GlobalTime() * g_Config.m_XcTeeShaderSpeed / 100.0f + std::max(ClientId, 0) * 0.37f;
	Settings.m_KeepShade = g_Config.m_XcTeeShaderShade / 100.0f;
	Settings.m_FillOriginX = std::max(OriginPixels.x, 0.0f);
	Settings.m_FillOriginY = std::max(OriginPixels.y, 0.0f);
}

bool CTeeGlow::Begin(int ClientId)
{
	// a player with a shared set gets his own neon and shader
	const CXcCosmeticsSync::CScope Shared(GameClient()->m_XcCosmetics, ClientId);
	if(!GameClient()->m_FxRenderer.ModuleActive() || !in_range(ClientId, MAX_CLIENTS - 1))
		return false;
	const vec2 Smear = GameClient()->m_XcScreenBlur.TeeSmear(ClientId);
	const bool Motion = Smear != vec2(0.0f, 0.0f);
	if(!HasGlow(ClientId) && !ShaderWhole(ClientId) && !Motion)
		return false;
	if(PlayerAlpha(GameClient(), ClientId) <= 0.0f)
		return false;

	// the tee, its weapon and room for the glow
	const vec2 Pos = GameClient()->m_aClients[ClientId].m_RenderPos;
	// the blur reaches up to twice the radius (its levels are powers of two)
	float Extent = 80.0f + (HasGlow(ClientId) ? 2.0f * GLOW_RADIUS * g_Config.m_XcGlowSize / 100.0f * FxGlow::For(-1).m_Radius : 0.0f);
	// room for the motion smear (pixels to world units)
	if(Motion)
		Extent += length(Smear) * 0.5f / std::max(PixelsPerUnit(), 0.001f) + 8.0f;
	const float aWorldRect[4] = {Pos.x - Extent, Pos.y - Extent, Pos.x + Extent, Pos.y + Extent};
	Graphics()->XcCaptureBegin(aWorldRect);
	return true;
}

void CTeeGlow::End(int ClientId)
{
	// a player with a shared set gets his own neon and shader
	const CXcCosmeticsSync::CScope Shared(GameClient()->m_XcCosmetics, ClientId);
	const float Ppu = PixelsPerUnit();
	const ColorRGBA Color = GlowColor(ClientId);
	float Pulse = 1.0f;
	if(g_Config.m_XcGlowPulse)
		Pulse = 1.0f + 0.12f * std::sin(Client()->GlobalTime() * 2.4f + ClientId);
	const float Alpha = PlayerAlpha(GameClient(), ClientId);

	XcRender::SGlowSettings Settings;
	Settings.m_Color = Color.WithAlpha(Alpha);
	// the outline is a lighter version of the glow, like the hot core of a neon tube
	Settings.m_RimColor = ColorRGBA(mix(Color.r, 1.0f, 0.2f), mix(Color.g, 1.0f, 0.2f), mix(Color.b, 1.0f, 0.2f), Alpha);
	Settings.m_Radius = GLOW_RADIUS * g_Config.m_XcGlowSize / 100.0f * FxGlow::For(-1).m_Radius * Ppu;
	Settings.m_Strength = 2.4f * g_Config.m_XcGlowStrength / 100.0f * FxGlow::For(-1).m_Strength * Pulse;
	Settings.m_RimWidth = OUTLINE_WIDTH * g_Config.m_XcGlowOutline / 100.0f * Ppu;
	Settings.m_DoGlow = HasGlow(ClientId);
	if(ShaderWhole(ClientId))
		ShaderSettings(ClientId, Ppu, ToPixels(GameClient()->m_aClients[ClientId].m_RenderPos), Settings);
	const vec2 Smear = GameClient()->m_XcScreenBlur.TeeSmear(ClientId);
	Settings.m_MotionX = Smear.x;
	Settings.m_MotionY = Smear.y;
	Graphics()->XcCaptureEnd(Settings);
}

bool CTeeGlow::PartBegin(int ClientId, int Part)
{
	// a player with a shared set gets his own neon and shader
	const CXcCosmeticsSync::CScope Shared(GameClient()->m_XcCosmetics, ClientId);
	if(!GameClient()->m_FxRenderer.ModuleActive() || !in_range(ClientId, MAX_CLIENTS - 1) || !HasShader(ClientId))
		return false;
	if(!(g_Config.m_XcTeeShaderParts & Part) || (Part != PART_HOOK && ShaderWhole(ClientId)))
		return false;
	if(PlayerAlpha(GameClient(), ClientId) <= 0.0f)
		return false;
	// the tee and its weapon; the hook reaches from the tee to its head
	const CGameClient::CClientData &Data = GameClient()->m_aClients[ClientId];
	vec2 Min = Data.m_RenderPos - vec2(80.0f, 80.0f);
	vec2 Max = Data.m_RenderPos + vec2(80.0f, 80.0f);
	if(Part == PART_HOOK)
	{
		const vec2 Hook(Data.m_RenderCur.m_HookX, Data.m_RenderCur.m_HookY);
		Min = vec2(std::min(Min.x, Hook.x - 40.0f), std::min(Min.y, Hook.y - 40.0f));
		Max = vec2(std::max(Max.x, Hook.x + 40.0f), std::max(Max.y, Hook.y + 40.0f));
	}
	const float aWorldRect[4] = {Min.x, Min.y, Max.x, Max.y};
	Graphics()->XcCaptureBegin(aWorldRect);
	return true;
}

void CTeeGlow::PartEnd(int ClientId)
{
	// a player with a shared set gets his own neon and shader
	const CXcCosmeticsSync::CScope Shared(GameClient()->m_XcCosmetics, ClientId);
	// only the pattern: no glow and no outline (those are around the whole player)
	XcRender::SGlowSettings Settings;
	Settings.m_DoGlow = false;
	Settings.m_Strength = 0.0f;
	Settings.m_RimWidth = 0.0f;
	ShaderSettings(ClientId, PixelsPerUnit(), ToPixels(GameClient()->m_aClients[ClientId].m_RenderPos), Settings);
	Graphics()->XcCaptureEnd(Settings);
}

bool CTeeGlow::PreviewSettings(float PixelsPerUnit, float Alpha, XcRender::SGlowSettings &Settings) const
{
	const bool Glow = g_Config.m_XcGlow && g_Config.m_XcGlowSelf;
	// the preview shows the tee only: its shader when the tee is one of the parts
	const bool Shader = g_Config.m_XcTeeShader && g_Config.m_XcTeeShaderSelf && (g_Config.m_XcTeeShaderParts & PART_TEE);
	if(!GameClient()->m_FxRenderer.ModuleActive() || (!Glow && !Shader))
		return false;
	const int Local = GameClient()->m_Snap.m_LocalClientId;
	const ColorRGBA Color = EffectColor(GameClient(), g_Config.m_XcGlowColorMode, g_Config.m_XcGlowColor, Local);
	Settings.m_Color = Color.WithAlpha(Alpha);
	Settings.m_RimColor = ColorRGBA(mix(Color.r, 1.0f, 0.2f), mix(Color.g, 1.0f, 0.2f), mix(Color.b, 1.0f, 0.2f), Alpha);
	Settings.m_Radius = GLOW_RADIUS * g_Config.m_XcGlowSize / 100.0f * FxGlow::For(-1).m_Radius * PixelsPerUnit;
	Settings.m_Strength = 2.4f * g_Config.m_XcGlowStrength / 100.0f * FxGlow::For(-1).m_Strength;
	Settings.m_RimWidth = OUTLINE_WIDTH * g_Config.m_XcGlowOutline / 100.0f * PixelsPerUnit;
	Settings.m_DoGlow = Glow;
	if(Shader)
	{
		Settings.m_FillMode = g_Config.m_XcTeeShader;
		Settings.m_FillColor = EffectColor(GameClient(), g_Config.m_XcTeeShaderColorMode, g_Config.m_XcTeeShaderColor, Local);
		Settings.m_FillColor2 = HslConfigColor(g_Config.m_XcTeeShaderColor2);
		Settings.m_FillAlpha = g_Config.m_XcTeeShaderAlpha / 100.0f;
		Settings.m_FillScale = 64.0f * PixelsPerUnit * g_Config.m_XcTeeShaderScale / 100.0f;
		Settings.m_FillTime = Client()->GlobalTime() * g_Config.m_XcTeeShaderSpeed / 100.0f;
		Settings.m_KeepShade = g_Config.m_XcTeeShaderShade / 100.0f;
	}
	return true;
}

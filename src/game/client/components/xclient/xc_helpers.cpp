#include "xc_helpers.h"

#include <base/math.h>

#include <engine/client.h>
#include <engine/graphics.h>
#include <engine/shared/config.h>

#include <game/client/components/xclient/ui/theme.h>
#include <game/client/gameclient.h>

#include <cmath>

namespace XcHelpers
{

	ETarget TargetOf(const CGameClient *pGameClient, int ClientId)
	{
		if(ClientId == pGameClient->m_aLocalIds[g_Config.m_ClDummy])
			return ETarget::SELF;
		if(pGameClient->Client()->DummyConnected() && ClientId == pGameClient->m_aLocalIds[!g_Config.m_ClDummy])
			return ETarget::DUMMY;
		return ETarget::OTHER;
	}

	bool ShowFor(const CGameClient *pGameClient, int ClientId, int Self, int Dummy, int Others)
	{
		switch(TargetOf(pGameClient, ClientId))
		{
		case ETarget::SELF: return Self;
		case ETarget::DUMMY: return Dummy;
		default: return Others;
		}
	}

	float PlayerAlpha(const CGameClient *pGameClient, int ClientId)
	{
		if(!pGameClient->IsOtherTeam(ClientId))
			return 1.0f;
		if(!g_Config.m_ClShowOthers)
			return 0.0f;
		return g_Config.m_ClShowOthersAlpha / 100.0f;
	}

	vec2 CharacterVelocity(const CGameClient *pGameClient, int ClientId)
	{
		const CNetObj_Character &Char = pGameClient->m_aClients[ClientId].m_RenderCur;
		// netobj velocity is in 1/256 units per tick
		return vec2(Char.m_VelX, Char.m_VelY) / 256.0f * (float)pGameClient->Client()->GameTickSpeed();
	}

	float SmoothFactor(float Speed, float Dt)
	{
		return 1.0f - std::exp(-Speed * Dt);
	}

	ColorRGBA HslConfigColor(unsigned PackedHsl)
	{
		return color_cast<ColorRGBA>(ColorHSLA(PackedHsl));
	}

	ColorRGBA EffectColor(const CGameClient *pGameClient, int Mode, unsigned CustomColor, int ClientId, float HueOffset, float Alpha)
	{
		switch(Mode)
		{
		case COLOR_ACCENT:
		{
			const ImVec4 Accent = XcUi::AccentColor(g_Config.m_XcUiAccent, g_Config.m_XcUiTheme == 0);
			return ColorRGBA(Accent.x, Accent.y, Accent.z, Alpha);
		}
		case COLOR_RAINBOW:
			return RainbowColor(pGameClient->Client()->GlobalTime() * 0.15f + HueOffset, Alpha);
		case COLOR_SKIN:
		{
			if(ClientId < 0)
				ClientId = pGameClient->m_aLocalIds[g_Config.m_ClDummy];
			if(ClientId < 0 || ClientId >= MAX_CLIENTS)
				break;
			const CTeeRenderInfo &Info = pGameClient->m_aClients[ClientId].m_RenderInfo;
			ColorRGBA Color = Info.m_CustomColoredSkin ? Info.m_ColorBody : Info.m_BloodColor;
			// effects glow, so very dark skins are lifted a little
			ColorHSLA Hsl = color_cast<ColorHSLA>(Color);
			Hsl.l = std::max(Hsl.l, 0.45f);
			Hsl.s = std::max(Hsl.s, 0.35f);
			return color_cast<ColorRGBA>(Hsl).WithAlpha(Alpha);
		}
		default:
			break;
		}
		return HslConfigColor(CustomColor).WithAlpha(Alpha);
	}

	ColorRGBA RainbowColor(float Hue, float Alpha)
	{
		Hue -= std::floor(Hue);
		return color_cast<ColorRGBA>(ColorHSLA(Hue, 0.85f, 0.62f, Alpha));
	}

	vec2 Rotate(vec2 Point, vec2 Center, float Angle)
	{
		const float s = std::sin(Angle);
		const float c = std::cos(Angle);
		const vec2 d = Point - Center;
		return Center + vec2(d.x * c - d.y * s, d.x * s + d.y * c);
	}

	void DrawGlowRing(IGraphics *pGraphics, vec2 Center, float Radius, float Squash, float Thickness, float Glow, ColorRGBA Color, float Rotation)
	{
		constexpr int SEGMENTS = 40;
		const ColorRGBA Clear = Color.WithAlpha(0.0f);
		const ColorRGBA Soft = Color.WithMultipliedAlpha(0.35f);

		const float Inner = std::max(Radius - Thickness * 0.5f, 0.0f);
		const float Outer = Radius + Thickness * 0.5f;
		const float GlowInner = std::max(Inner - Glow, 0.0f);
		const float GlowOuter = Outer + Glow;
		const auto Point = [&](vec2 Dir, float R) {
			return Rotate(Center + vec2(Dir.x * R, Dir.y * R * Squash), Center, Rotation);
		};

		IGraphics::CFreeformItem aCore[SEGMENTS];
		IGraphics::CFreeformItem aGlowOut[SEGMENTS];
		IGraphics::CFreeformItem aGlowIn[SEGMENTS];
		for(int i = 0; i < SEGMENTS; i++)
		{
			const vec2 Dir1 = direction(2.0f * pi * i / SEGMENTS);
			const vec2 Dir2 = direction(2.0f * pi * (i + 1) / SEGMENTS);
			aCore[i] = IGraphics::CFreeformItem(Point(Dir1, Inner), Point(Dir2, Inner), Point(Dir1, Outer), Point(Dir2, Outer));
			aGlowOut[i] = IGraphics::CFreeformItem(Point(Dir1, Outer), Point(Dir2, Outer), Point(Dir1, GlowOuter), Point(Dir2, GlowOuter));
			aGlowIn[i] = IGraphics::CFreeformItem(Point(Dir1, Inner), Point(Dir2, Inner), Point(Dir1, GlowInner), Point(Dir2, GlowInner));
		}

		pGraphics->SetColor(Color);
		pGraphics->QuadsDrawFreeform(aCore, SEGMENTS);
		if(Glow > 0.0f)
		{
			pGraphics->SetColor4(Soft, Soft, Clear, Clear);
			pGraphics->QuadsDrawFreeform(aGlowOut, SEGMENTS);
			pGraphics->QuadsDrawFreeform(aGlowIn, SEGMENTS);
		}
	}

	void DrawSoftCircle(IGraphics *pGraphics, vec2 Center, float Radius, float Feather, ColorRGBA Color)
	{
		constexpr int SEGMENTS = 24;
		const ColorRGBA Clear = Color.WithAlpha(0.0f);
		IGraphics::CFreeformItem aFill[SEGMENTS];
		IGraphics::CFreeformItem aEdge[SEGMENTS];
		for(int i = 0; i < SEGMENTS; i++)
		{
			const vec2 Dir1 = direction(2.0f * pi * i / SEGMENTS);
			const vec2 Dir2 = direction(2.0f * pi * (i + 1) / SEGMENTS);
			aFill[i] = IGraphics::CFreeformItem(Center, Center, Center + Dir1 * Radius, Center + Dir2 * Radius);
			aEdge[i] = IGraphics::CFreeformItem(Center + Dir1 * Radius, Center + Dir2 * Radius, Center + Dir1 * (Radius + Feather), Center + Dir2 * (Radius + Feather));
		}
		pGraphics->SetColor(Color);
		pGraphics->QuadsDrawFreeform(aFill, SEGMENTS);
		pGraphics->SetColor4(Color, Color, Clear, Clear);
		pGraphics->QuadsDrawFreeform(aEdge, SEGMENTS);
	}

} // namespace XcHelpers

#include "weapon_view.h"

#include "xc_helpers.h"

#include <base/math.h>

#include <engine/shared/config.h>

#include <game/client/gameclient.h>

#include <cmath>

namespace XcWeapon
{
	bool Applies(const CGameClient *pGameClient, int ClientId)
	{
		if(!g_Config.m_XcWeaponView)
			return false;
		if(ClientId < 0)
			return true;
		return XcHelpers::ShowFor(pGameClient, ClientId, 1, 1, g_Config.m_XcWeaponViewOthers);
	}

	float SwingSpeed(const CGameClient *pGameClient, int ClientId)
	{
		return Applies(pGameClient, ClientId) ? g_Config.m_XcSwingSpeed / 100.0f : 1.0f;
	}

	float SwingAngle(const CGameClient *pGameClient, int ClientId, float AnimAngle, float Progress)
	{
		if(!Applies(pGameClient, ClientId))
			return AnimAngle;
		// the stock swing rests at -0.1 turns and goes up to 0.3; the styles change the way from the rest
		constexpr float REST = -0.1f;
		const float p = std::clamp(Progress, 0.0f, 1.0f);
		float Away;
		switch(g_Config.m_XcSwingStyle)
		{
		case SWING_WIDE: Away = (AnimAngle - REST) * 1.7f; break;
		case SWING_SPIN: Away = p > 0.0f && p < 1.0f ? 1.0f - (1.0f - p) * (1.0f - p) : 0.0f; break;
		case SWING_SMOOTH:
		{
			const float Ease = p < 0.5f ? 4.0f * p * p * p : 1.0f - std::pow(-2.0f * p + 2.0f, 3.0f) / 2.0f;
			Away = 0.4f * std::sin(Ease * pi);
			break;
		}
		case SWING_STAB: Away = 0.0f; break;
		case SWING_BOUNCE:
		{
			// fast up to the hit, then a damped spring back to the rest
			const float Late = std::max(p - 0.3f, 0.0f);
			Away = 0.5f * std::sin(std::min(p / 0.3f, 1.0f) * pi * 0.5f) * std::exp(-6.0f * Late) * std::cos(Late * 14.0f);
			break;
		}
		default: Away = AnimAngle - REST; break;
		}
		return REST + Away * g_Config.m_XcSwingStrength / 100.0f + g_Config.m_XcSwingTilt / 360.0f;
	}

	vec2 SwingPush(const CGameClient *pGameClient, int ClientId, vec2 Direction, float Progress)
	{
		if(!Applies(pGameClient, ClientId) || g_Config.m_XcSwingStyle != SWING_STAB || Progress <= 0.0f || Progress >= 1.0f)
			return vec2(0.0f, 0.0f);
		return Direction * std::sin(Progress * pi) * 18.0f * g_Config.m_XcSwingStrength / 100.0f;
	}

	vec2 Offset(const CGameClient *pGameClient, int ClientId, vec2 Direction)
	{
		if(!Applies(pGameClient, ClientId))
			return vec2(0.0f, 0.0f);
		// x moves forward in the direction the tee looks
		const float Side = Direction.x < 0.0f ? -1.0f : 1.0f;
		return vec2(g_Config.m_XcWeaponX * Side, g_Config.m_XcWeaponY);
	}

	float Scale(const CGameClient *pGameClient, int ClientId)
	{
		return Applies(pGameClient, ClientId) ? g_Config.m_XcWeaponSize / 100.0f : 1.0f;
	}
}

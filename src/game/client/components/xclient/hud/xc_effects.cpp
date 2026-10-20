// The effects list of the HUD: what the tee has right now, like the potion effects of Minecraft.
// Freeze and ninja run out, so they get a timer and a bar; the rest (jetpack, endless hook, solo...)
// stay until a tile takes them away. Only shows what the classic HUD already knows.

#include "xc_hud.h"

#include <base/math.h>
#include <base/str.h>

#include <engine/client.h>
#include <engine/graphics.h>
#include <engine/shared/config.h>

#include <generated/client_data.h>
#include <generated/protocol.h>

#include <game/client/components/xclient/ui/feedback.h>
#include <game/client/components/xclient/ui/icons.h>
#include <game/client/components/xclient/ui/lang.h>
#include <game/client/components/xclient/ui/theme.h>
#include <game/client/components/xclient/ui/widgets.h>
#include <game/client/gameclient.h>

#include <algorithm>
#include <cmath>

using namespace XcUi;

static const char *const s_apEffectNames[] = {
	"Frozen",
	"Deep freeze",
	"Live freeze",
	"Ninja",
	"Jetpack",
	"Endless hook",
	"Endless jump",
	"Super",
	"Invincible",
	"Solo",
	"No collision",
	"No hook",
	"No hammer",
	"Weapons off",
	"Teleport gun",
	"Practice",
	"Locked",
	"Team 0",
};

// the color of an effect: good ones mint and blue, bad ones ice and red
static ImVec4 EffectColor(int Effect)
{
	switch(Effect)
	{
	case 0: return ImVec4(0.55f, 0.85f, 1.0f, 1.0f); // frozen
	case 1: return ImVec4(0.35f, 0.6f, 1.0f, 1.0f);
	case 2: return ImVec4(0.5f, 0.75f, 0.95f, 1.0f);
	case 3: return ImVec4(0.75f, 0.45f, 1.0f, 1.0f); // ninja
	case 4: return ImVec4(1.0f, 0.62f, 0.3f, 1.0f); // jetpack
	case 5:
	case 6: return ImVec4(0.24f, 0.84f, 0.64f, 1.0f); // endless
	case 7:
	case 8: return ImVec4(1.0f, 0.84f, 0.3f, 1.0f); // super, invincible
	case 9: return ImVec4(0.7f, 0.72f, 0.78f, 1.0f); // solo
	case 10:
	case 11:
	case 12:
	case 13: return ImVec4(1.0f, 0.42f, 0.42f, 1.0f); // something turned off
	case 14: return ImVec4(0.3f, 0.75f, 1.0f, 1.0f);
	default: return ImVec4(0.8f, 0.8f, 0.85f, 1.0f);
	}
}

void CXcHud::DrawEffectIcon(ImDrawList *pDraw, int Effect, ImVec2 Min, float Size, float Alpha)
{
	const auto &Hud = GameClient()->m_HudSkin;
	IGraphics::CTextureHandle Texture;
	switch(Effect)
	{
	case EFFECT_DEEP_FREEZE: Texture = Hud.m_SpriteHudDeepFrozen; break;
	case EFFECT_LIVE_FREEZE: Texture = Hud.m_SpriteHudLiveFrozen; break;
	case EFFECT_NINJA: Texture = GameClient()->m_GameSkin.m_aSpritePickupWeapons[WEAPON_NINJA]; break;
	case EFFECT_JETPACK: Texture = Hud.m_SpriteHudJetpack; break;
	case EFFECT_ENDLESS_HOOK: Texture = Hud.m_SpriteHudEndlessHook; break;
	case EFFECT_ENDLESS_JUMP: Texture = Hud.m_SpriteHudEndlessJump; break;
	case EFFECT_SOLO: Texture = Hud.m_SpriteHudSolo; break;
	case EFFECT_NO_COLLISION: Texture = Hud.m_SpriteHudCollisionDisabled; break;
	case EFFECT_NO_HOOK: Texture = Hud.m_SpriteHudHookHitDisabled; break;
	case EFFECT_NO_HAMMER: Texture = Hud.m_SpriteHudHammerHitDisabled; break;
	case EFFECT_NO_SHOOTING: Texture = Hud.m_SpriteHudShotgunHitDisabled; break;
	case EFFECT_TELEGUN: Texture = Hud.m_SpriteHudTeleportGun; break;
	case EFFECT_PRACTICE: Texture = Hud.m_SpriteHudPracticeMode; break;
	case EFFECT_LOCK: Texture = Hud.m_SpriteHudLockMode; break;
	case EFFECT_TEAM0: Texture = Hud.m_SpriteHudTeam0Mode; break;
	default: break;
	}
	const ImVec4 Color = EffectColor(Effect);
	const ImVec2 C(Min.x + Size * 0.5f, Min.y + Size * 0.5f);
	// a soft colored disc behind every icon
	pDraw->AddCircleFilled(C, Size * 0.5f, Col(Color, 0.18f * Alpha), 24);
	if(Texture.IsValid() && !Texture.IsNullTexture())
	{
		const float Pad = Size * 0.14f;
		pDraw->AddImage((ImTextureID)(uintptr_t)m_Backend.TextureId(Texture), ImVec2(Min.x + Pad, Min.y + Pad), ImVec2(Min.x + Size - Pad, Min.y + Size - Pad), ImVec2(0, 0), ImVec2(1, 1), Col(ImVec4(1, 1, 1, 1), Alpha));
		return;
	}
	if(Effect == EFFECT_FREEZE)
	{
		// a snowflake: three crossing lines with small branches
		const float R = Size * 0.32f;
		const float W = std::max(1.0f, Size * 0.07f);
		for(int i = 0; i < 3; i++)
		{
			const float A = i * pi / 3.0f + pi / 2.0f;
			const ImVec2 D(std::cos(A) * R, std::sin(A) * R);
			pDraw->AddLine(ImVec2(C.x - D.x, C.y - D.y), ImVec2(C.x + D.x, C.y + D.y), Col(Color, Alpha), W);
			for(int Side = -1; Side <= 1; Side += 2)
			{
				const ImVec2 Tip(C.x + D.x * Side * 0.62f, C.y + D.y * Side * 0.62f);
				for(int b = -1; b <= 1; b += 2)
				{
					const float Ab = A + (Side > 0 ? 0.0f : pi) + b * 0.7f;
					pDraw->AddLine(Tip, ImVec2(Tip.x + std::cos(Ab) * R * 0.38f, Tip.y + std::sin(Ab) * R * 0.38f), Col(Color, Alpha), W);
				}
			}
		}
		return;
	}
	// super and invincible: a glyph
	const char *pGlyph = Effect == EFFECT_SUPER ? XC_ICON_BOLT : XC_ICON_STAR;
	ImFont *pFont = m_Backend.Fonts().m_pLabel;
	const float TextSize = Size * 0.55f;
	const ImVec2 Dim = pFont->CalcTextSizeA(TextSize, FLT_MAX, 0.0f, pGlyph);
	pDraw->AddText(pFont, TextSize, ImVec2(C.x - Dim.x * 0.5f, C.y - Dim.y * 0.5f), Col(Color, Alpha), pGlyph);
}

void CXcHud::RenderEffects(ImDrawList *pDraw, float Alpha, float Dt)
{
	const STheme &t = Theme();
	const float U = Unit(ELEMENT_EFFECTS);
	const int ClientId = LocalClientId();
	bool aActive[NUM_EFFECTS] = {};
	float aProgress[NUM_EFFECTS]; // 0..1 time left of the effects that run out, -1 = no timer
	float aSeconds[NUM_EFFECTS];
	std::fill(std::begin(aProgress), std::end(aProgress), -1.0f);
	std::fill(std::begin(aSeconds), std::end(aSeconds), 0.0f);
	const bool HasCharacter = ClientId >= 0 && GameClient()->m_Snap.m_aCharacters[ClientId].m_Active;
	if(HasCharacter)
	{
		const CCharacterCore &Core = GameClient()->m_aClients[ClientId].m_Predicted;
		const auto &Snap = GameClient()->m_Snap.m_aCharacters[ClientId];
		const bool Extended = Snap.m_HasExtendedDisplayInfo;
		const int Tick = Client()->GameTick(g_Config.m_ClDummy);
		const float TickSpeed = Client()->GameTickSpeed();
		if(Extended && Core.m_FreezeEnd > 0 && Core.m_FreezeStart > 0 && Core.m_FreezeEnd > Tick)
		{
			aActive[EFFECT_FREEZE] = true;
			const float Max = (float)std::max(Core.m_FreezeEnd - Core.m_FreezeStart, 1);
			aProgress[EFFECT_FREEZE] = std::clamp((Core.m_FreezeEnd - Tick - Client()->IntraGameTick(g_Config.m_ClDummy)) / Max, 0.0f, 1.0f);
			aSeconds[EFFECT_FREEZE] = (Core.m_FreezeEnd - Tick) / TickSpeed;
		}
		aActive[EFFECT_DEEP_FREEZE] = Core.m_DeepFrozen;
		aActive[EFFECT_LIVE_FREEZE] = Core.m_LiveFrozen;
		if(Core.m_aWeapons[WEAPON_NINJA].m_Got)
		{
			const int Max = g_pData->m_Weapons.m_Ninja.m_Duration * Client()->GameTickSpeed() / 1000;
			const int Left = std::clamp(Core.m_Ninja.m_ActivationTick + Max - Tick, 0, Max);
			if(Left > 0 && Extended)
			{
				aActive[EFFECT_NINJA] = true;
				aProgress[EFFECT_NINJA] = Left / (float)std::max(Max, 1);
				aSeconds[EFFECT_NINJA] = Left / TickSpeed;
			}
		}
		aActive[EFFECT_JETPACK] = Core.m_Jetpack;
		aActive[EFFECT_ENDLESS_HOOK] = Core.m_EndlessHook;
		aActive[EFFECT_ENDLESS_JUMP] = Core.m_EndlessJump;
		aActive[EFFECT_SUPER] = Core.m_Super;
		aActive[EFFECT_INVINCIBLE] = Core.m_Invincible;
		aActive[EFFECT_SOLO] = Core.m_Solo;
		aActive[EFFECT_NO_COLLISION] = Core.m_CollisionDisabled;
		aActive[EFFECT_NO_HOOK] = Core.m_HookHitDisabled;
		aActive[EFFECT_NO_HAMMER] = Core.m_HammerHitDisabled;
		aActive[EFFECT_NO_SHOOTING] = (Core.m_ShotgunHitDisabled && Core.m_aWeapons[WEAPON_SHOTGUN].m_Got) || (Core.m_GrenadeHitDisabled && Core.m_aWeapons[WEAPON_GRENADE].m_Got) || (Core.m_LaserHitDisabled && Core.m_aWeapons[WEAPON_LASER].m_Got);
		aActive[EFFECT_TELEGUN] = (Core.m_HasTelegunGun && Core.m_aWeapons[WEAPON_GUN].m_Got) || (Core.m_HasTelegunGrenade && Core.m_aWeapons[WEAPON_GRENADE].m_Got) || (Core.m_HasTelegunLaser && Core.m_aWeapons[WEAPON_LASER].m_Got);
		if(Extended)
		{
			aActive[EFFECT_PRACTICE] = Snap.m_ExtendedData.m_Flags & CHARACTERFLAG_PRACTICE_MODE;
			aActive[EFFECT_LOCK] = Snap.m_ExtendedData.m_Flags & CHARACTERFLAG_LOCK_MODE;
			aActive[EFFECT_TEAM0] = Snap.m_ExtendedData.m_Flags & CHARACTERFLAG_TEAM0_MODE;
		}
	}

	// notifications when the own tee gets or loses an effect (not for the freeze, it comes all the time)
	const bool Notify = g_Config.m_XcHudEffectsNotify && !m_Editing && HasCharacter && ClientId == GameClient()->m_Snap.m_LocalClientId;
	if(ClientId != m_EffectsClientId || !HasCharacter)
	{
		// another tee or a respawn: no notifications for what it already had
		m_EffectsClientId = HasCharacter ? ClientId : -1;
		for(int i = 0; i < NUM_EFFECTS; i++)
			m_aEffectWasActive[i] = aActive[i];
	}
	for(int i = 0; i < NUM_EFFECTS; i++)
	{
		if(aActive[i] != m_aEffectWasActive[i] && Notify && i != EFFECT_FREEZE && i != EFFECT_NINJA)
		{
			char aBuf[128];
			str_format(aBuf, sizeof(aBuf), aActive[i] ? Tr("Effect: %s") : Tr("Effect ended: %s"), Tr(s_apEffectNames[i]));
			Toast(aBuf, TOAST_INFO);
		}
		m_aEffectWasActive[i] = aActive[i];
	}

	// in the editor show a few examples so the element can be placed
	if(m_Editing)
	{
		bool Any = false;
		for(bool b : aActive)
			Any |= b;
		if(!Any)
		{
			aActive[EFFECT_FREEZE] = aActive[EFFECT_JETPACK] = aActive[EFFECT_ENDLESS_HOOK] = true;
			aProgress[EFFECT_FREEZE] = 0.6f;
			aSeconds[EFFECT_FREEZE] = 1.8f;
		}
	}
	const float Speed = AnimationSpeed() > 0.0f ? 10.0f * AnimationSpeed() : 1000.0f;
	for(int i = 0; i < NUM_EFFECTS; i++)
		m_aEffectShown[i] = aActive[i] ? std::min(m_aEffectShown[i] + Dt * Speed, 1.0f) : std::max(m_aEffectShown[i] - Dt * Speed, 0.0f);

	// the effects that run out first, then in the order of the list
	int aOrder[NUM_EFFECTS];
	int Count = 0;
	for(int Pass = 0; Pass < 2; Pass++)
		for(int i = 0; i < NUM_EFFECTS; i++)
			if(m_aEffectShown[i] > 0.0f && ((i == EFFECT_FREEZE || i == EFFECT_NINJA) == (Pass == 0)))
				aOrder[Count++] = i;
	if(Count == 0)
	{
		m_aaRects[ELEMENT_EFFECTS][0] = m_aaRects[ELEMENT_EFFECTS][2] = -1.0f;
		return;
	}

	const bool Compact = g_Config.m_XcHudEffectsCompact;
	ImFont *pLabel = m_Backend.Fonts().m_pLabel;
	ImFont *pSmall = m_Backend.Fonts().m_pSmall;
	const float Icon = 26.0f * U;
	const float Pad = 8.0f * U;
	const float RowHeight = Icon + 8.0f * U;
	const float NameSize = 13.0f * U;
	const float TimeSize = 11.5f * U;
	float TextWidth = 0.0f;
	if(!Compact)
		for(int n = 0; n < Count; n++)
			TextWidth = std::max(TextWidth, pLabel->CalcTextSizeA(NameSize, FLT_MAX, 0.0f, Tr(s_apEffectNames[aOrder[n]])).x);
	TextWidth = std::max(TextWidth, pSmall->CalcTextSizeA(TimeSize, FLT_MAX, 0.0f, "0.0s").x);
	const float Width = Compact ? Pad * 2.0f + Icon + 6.0f * U + pSmall->CalcTextSizeA(TimeSize, FLT_MAX, 0.0f, "00.0").x : Pad * 2.0f + Icon + 10.0f * U + std::max(TextWidth, 70.0f * U);
	float Height = Pad * 2.0f - 8.0f * U;
	for(int n = 0; n < Count; n++)
		Height += RowHeight * m_aEffectShown[aOrder[n]];
	Height = std::max(Height, Icon + Pad * 2.0f - 8.0f * U);
	const ImVec2 Min = PlaceRect(ELEMENT_EFFECTS, ImVec2(Width, Height));
	RenderPlate(pDraw, Min, ImVec2(Min.x + Width, Min.y + Height), U, Alpha);

	float y = Min.y + Pad - 4.0f * U;
	for(int n = 0; n < Count; n++)
	{
		const int e = aOrder[n];
		const float Shown = m_aEffectShown[e];
		const float a = Alpha * Shown;
		const float x = Min.x + Pad;
		const float RowY = y + 4.0f * U;
		DrawEffectIcon(pDraw, e, ImVec2(x, RowY), Icon, a);
		const ImVec4 Color = EffectColor(e);
		const float TextX = x + Icon + (Compact ? 6.0f : 10.0f) * U;
		char aTime[16] = "";
		if(aProgress[e] >= 0.0f)
			str_format(aTime, sizeof(aTime), aSeconds[e] < 10.0f ? "%.1fs" : "%.0fs", std::max(aSeconds[e], 0.0f));
		if(Compact)
		{
			if(aTime[0])
				pDraw->AddText(pSmall, TimeSize, ImVec2(TextX, RowY + (Icon - TimeSize) * 0.5f), Col(t.m_TextSecondary, a), aTime);
		}
		else
		{
			const bool Timed = aProgress[e] >= 0.0f;
			pDraw->AddText(pLabel, NameSize, ImVec2(TextX, RowY + (Timed ? 0.0f : (Icon - NameSize) * 0.5f - 1.0f * U)), Col(t.m_Text, a), Tr(s_apEffectNames[e]));
			if(Timed)
			{
				// the time left and a thin bar that runs out
				const float BarY = RowY + Icon - 5.0f * U;
				const float BarW = Min.x + Width - Pad - TextX;
				const float TimeW = pSmall->CalcTextSizeA(TimeSize, FLT_MAX, 0.0f, aTime).x;
				pDraw->AddText(pSmall, TimeSize, ImVec2(Min.x + Width - Pad - TimeW, RowY + 1.0f * U), Col(t.m_TextMuted, a), aTime);
				pDraw->AddRectFilled(ImVec2(TextX, BarY), ImVec2(TextX + BarW, BarY + 3.0f * U), Col(t.m_Track, a), 2.0f * U);
				pDraw->AddRectFilled(ImVec2(TextX, BarY), ImVec2(TextX + BarW * aProgress[e], BarY + 3.0f * U), Col(Color, a), 2.0f * U);
			}
		}
		y += RowHeight * Shown;
	}
}

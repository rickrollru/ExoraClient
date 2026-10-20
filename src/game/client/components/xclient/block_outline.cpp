#include "block_outline.h"

#include "fx_renderer.h"
#include "xc_helpers.h"

#include <base/color.h>
#include <base/math.h>
#include <base/str.h>

#include <engine/graphics.h>
#include <engine/shared/config.h>
#include <engine/textrender.h>

#include <game/client/components/xclient/ui/lang.h>
#include <game/client/gameclient.h>
#include <game/collision.h>
#include <game/mapitems.h>

#include <algorithm>
#include <cmath>
#include <vector>

using namespace XcHelpers;

void CXcBlockOutline::OnReset()
{
	m_HasPos = false;
	m_Shown = 0.0f;
}

const char *CXcBlockOutline::TileName(int Tile, ColorRGBA *pColor)
{
	ColorRGBA Color(1.0f, 1.0f, 1.0f, 1.0f);
	const char *pName = nullptr;
	switch(Tile)
	{
	case TILE_SOLID: pName = "Hookable"; Color = ColorRGBA(0.85f, 0.87f, 0.92f, 1.0f); break;
	case TILE_NOHOOK: pName = "Unhookable"; Color = ColorRGBA(0.6f, 0.62f, 0.7f, 1.0f); break;
	case TILE_DEATH: pName = "Kill"; Color = ColorRGBA(1.0f, 0.3f, 0.3f, 1.0f); break;
	case TILE_FREEZE: pName = "Freeze"; Color = ColorRGBA(0.55f, 0.85f, 1.0f, 1.0f); break;
	case TILE_DFREEZE: pName = "Deep freeze"; Color = ColorRGBA(0.35f, 0.55f, 1.0f, 1.0f); break;
	case TILE_LFREEZE: pName = "Live freeze"; Color = ColorRGBA(0.5f, 0.75f, 0.95f, 1.0f); break;
	case TILE_UNFREEZE: pName = "Unfreeze"; Color = ColorRGBA(0.24f, 0.84f, 0.64f, 1.0f); break;
	case TILE_DUNFREEZE: pName = "Deep unfreeze"; Color = ColorRGBA(0.24f, 0.84f, 0.64f, 1.0f); break;
	case TILE_LUNFREEZE: pName = "Live unfreeze"; Color = ColorRGBA(0.24f, 0.84f, 0.64f, 1.0f); break;
	case TILE_START: pName = "Start"; Color = ColorRGBA(0.3f, 0.9f, 0.4f, 1.0f); break;
	case TILE_FINISH: pName = "Finish"; Color = ColorRGBA(1.0f, 0.84f, 0.3f, 1.0f); break;
	case TILE_STOP:
	case TILE_STOPS:
	case TILE_STOPA: pName = "Stopper"; Color = ColorRGBA(1.0f, 0.6f, 0.3f, 1.0f); break;
	case TILE_THROUGH:
	case TILE_THROUGH_ALL:
	case TILE_THROUGH_DIR:
	case TILE_THROUGH_CUT: pName = "Shoot through"; Color = ColorRGBA(0.75f, 0.75f, 0.8f, 1.0f); break;
	case TILE_WALLJUMP: pName = "Wall jump"; Color = ColorRGBA(0.7f, 0.9f, 0.4f, 1.0f); break;
	case TILE_JUMP: pName = "Jumps"; Color = ColorRGBA(0.7f, 0.9f, 0.4f, 1.0f); break;
	case TILE_REFILL_JUMPS: pName = "Refill jumps"; Color = ColorRGBA(0.7f, 0.9f, 0.4f, 1.0f); break;
	case TILE_EHOOK_ENABLE: pName = "Endless hook"; Color = ColorRGBA(0.24f, 0.84f, 0.64f, 1.0f); break;
	case TILE_EHOOK_DISABLE: pName = "Endless hook off"; Color = ColorRGBA(1.0f, 0.42f, 0.42f, 1.0f); break;
	case TILE_JETPACK_ENABLE: pName = "Jetpack"; Color = ColorRGBA(1.0f, 0.62f, 0.3f, 1.0f); break;
	case TILE_JETPACK_DISABLE: pName = "Jetpack off"; Color = ColorRGBA(1.0f, 0.42f, 0.42f, 1.0f); break;
	case TILE_SOLO_ENABLE: pName = "Solo"; Color = ColorRGBA(0.7f, 0.72f, 0.78f, 1.0f); break;
	case TILE_SOLO_DISABLE: pName = "Solo off"; Color = ColorRGBA(0.7f, 0.72f, 0.78f, 1.0f); break;
	case TILE_HIT_ENABLE: pName = "Hit on"; Color = ColorRGBA(0.24f, 0.84f, 0.64f, 1.0f); break;
	case TILE_HIT_DISABLE: pName = "Hit off"; Color = ColorRGBA(1.0f, 0.42f, 0.42f, 1.0f); break;
	case TILE_NPC_ENABLE: pName = "Collision on"; Color = ColorRGBA(0.24f, 0.84f, 0.64f, 1.0f); break;
	case TILE_NPC_DISABLE: pName = "Collision off"; Color = ColorRGBA(1.0f, 0.42f, 0.42f, 1.0f); break;
	case TILE_NPH_ENABLE: pName = "Hook on"; Color = ColorRGBA(0.24f, 0.84f, 0.64f, 1.0f); break;
	case TILE_NPH_DISABLE: pName = "Hook off"; Color = ColorRGBA(1.0f, 0.42f, 0.42f, 1.0f); break;
	case TILE_UNLIMITED_JUMPS_ENABLE: pName = "Endless jump"; Color = ColorRGBA(0.24f, 0.84f, 0.64f, 1.0f); break;
	case TILE_UNLIMITED_JUMPS_DISABLE: pName = "Endless jump off"; Color = ColorRGBA(1.0f, 0.42f, 0.42f, 1.0f); break;
	case TILE_TELE_GUN_ENABLE:
	case TILE_TELE_GRENADE_ENABLE:
	case TILE_TELE_LASER_ENABLE: pName = "Teleport gun"; Color = ColorRGBA(0.3f, 0.75f, 1.0f, 1.0f); break;
	case TILE_TELE_GUN_DISABLE:
	case TILE_TELE_GRENADE_DISABLE:
	case TILE_TELE_LASER_DISABLE: pName = "Teleport gun off"; Color = ColorRGBA(1.0f, 0.42f, 0.42f, 1.0f); break;
	case TILE_CP:
	case TILE_CP_F: pName = "Speeder"; Color = ColorRGBA(1.0f, 0.6f, 0.3f, 1.0f); break;
	case TILE_TUNE: pName = "Tune zone"; Color = ColorRGBA(0.75f, 0.45f, 1.0f, 1.0f); break;
	default:
		if(Tile >= TILE_TIME_CHECKPOINT_FIRST && Tile <= TILE_TIME_CHECKPOINT_LAST)
		{
			pName = "Time checkpoint";
			Color = ColorRGBA(1.0f, 0.84f, 0.3f, 1.0f);
		}
		break;
	}
	if(pColor)
		*pColor = Color;
	return pName;
}

void CXcBlockOutline::OnRender()
{
	if(Client()->State() != IClient::STATE_ONLINE && Client()->State() != IClient::STATE_DEMOPLAYBACK)
		return;
	const float Dt = std::min(Client()->RenderFrameTime(), 0.1f);
	const bool Active = g_Config.m_XcBlockOutline && !GameClient()->m_Menus.IsActive() && !GameClient()->m_XcMenu.IsActive() && Client()->State() == IClient::STATE_ONLINE;
	if(!Active && m_Shown <= 0.0f)
		return;

	const int Dummy = g_Config.m_ClDummy;
	const int Local = GameClient()->m_Snap.m_LocalClientId;
	const bool Playing = Local >= 0 && GameClient()->m_Snap.m_aCharacters[Local].m_Active && !GameClient()->m_Snap.m_SpecInfo.m_Active;
	const vec2 Target = GameClient()->m_Controls.m_aTargetPos[Dummy];
	const CCollision *pCollision = Collision();
	const int Index = pCollision->GetPureMapIndex(Target);
	int Tile = pCollision->GetTileIndex(Index);
	const int Front = pCollision->GetFrontTileIndex(Index);
	// the front layer wins when the game layer is empty
	if((Tile == TILE_AIR || TileName(Tile, nullptr) == nullptr) && Front != TILE_AIR)
		Tile = Front;
	int Tele = 0;
	if(pCollision->TeleLayer())
	{
		const CTeleTile &TeleTile = pCollision->TeleLayer()[Index];
		if(TeleTile.m_Type == TILE_TELEIN || TeleTile.m_Type == TILE_TELEINEVIL || TeleTile.m_Type == TILE_TELEOUT || TeleTile.m_Type == TILE_TELECHECKIN || TeleTile.m_Type == TILE_TELECHECKINEVIL || TeleTile.m_Type == TILE_TELECHECKOUT || TeleTile.m_Type == TILE_TELECHECK || TeleTile.m_Type == TILE_TELEINWEAPON || TeleTile.m_Type == TILE_TELEINHOOK)
			Tele = TeleTile.m_Type * 1000 + TeleTile.m_Number;
	}
	ColorRGBA TileColor;
	const char *pName = TileName(Tile, &TileColor);
	if(Tele)
	{
		pName = (Tele / 1000 == TILE_TELEOUT || Tele / 1000 == TILE_TELECHECKOUT) ? "Teleport exit" : "Teleport";
		TileColor = ColorRGBA(0.75f, 0.45f, 1.0f, 1.0f);
	}
	const bool OnBlock = pName != nullptr;
	const bool Wanted = Active && Playing && (OnBlock || g_Config.m_XcBlockOutlineAir);

	const vec2 Cell = vec2(std::floor(Target.x / 32.0f) * 32.0f, std::floor(Target.y / 32.0f) * 32.0f);
	if(!m_HasPos || !g_Config.m_XcBlockOutlineSmooth || m_Shown <= 0.0f)
		m_Pos = Cell;
	else if(Wanted)
		m_Pos = mix(m_Pos, Cell, SmoothFactor(6.0f + g_Config.m_XcBlockOutlineSpeed * 0.3f, Dt));
	m_HasPos = true;
	m_Shown = Wanted ? std::min(m_Shown + Dt * 8.0f, 1.0f) : std::max(m_Shown - Dt * 5.0f, 0.0f);
	if(m_Shown <= 0.0f)
		return;

	ColorRGBA Color = g_Config.m_XcBlockOutlineTileColor && OnBlock ? TileColor : EffectColor(GameClient(), g_Config.m_XcBlockOutlineColorMode, g_Config.m_XcBlockOutlineColor, -1);
	const float Pulse = 0.85f + 0.15f * std::sin(Client()->GlobalTime() * 4.0f);
	const float Alpha = m_Shown * Pulse;
	const float Width = g_Config.m_XcBlockOutlineWidth * 0.75f;
	const bool Glow = g_Config.m_XcBlockOutlineGlow;
	const float GlowRadius = Width * 3.0f + 4.0f;
	const float Fill = g_Config.m_XcBlockOutlineFill / 100.0f;
	const vec2 Min = m_Pos;
	const vec2 Max = m_Pos + vec2(32.0f, 32.0f);
	const vec2 Center = (Min + Max) * 0.5f;

	std::vector<SFxShape> vShapes;
	const auto Line = [&](vec2 a, vec2 b) {
		SFxShape Shape = FxShape::Line(a, b, Width, Color.WithAlpha(Alpha));
		if(Glow)
			CFxRenderer::AppendGlowing(vShapes, Shape, GlowRadius, 0.7f);
		else
			vShapes.push_back(Shape);
	};
	if(Fill > 0.0f || g_Config.m_XcBlockOutlineMode == MODE_FILL)
	{
		const float FillAlpha = g_Config.m_XcBlockOutlineMode == MODE_FILL ? std::max(Fill, 0.35f) : Fill;
		SFxShape Shape = FxShape::RoundRect(Center, vec2(16.0f, 16.0f), 3.0f, Color.WithAlpha(FillAlpha * m_Shown));
		if(Glow && g_Config.m_XcBlockOutlineMode == MODE_FILL)
			CFxRenderer::AppendGlowing(vShapes, Shape, GlowRadius, 0.5f);
		else
			vShapes.push_back(Shape);
	}
	if(g_Config.m_XcBlockOutlineMode == MODE_OUTLINE)
	{
		Line(Min, vec2(Max.x, Min.y));
		Line(vec2(Max.x, Min.y), Max);
		Line(Max, vec2(Min.x, Max.y));
		Line(vec2(Min.x, Max.y), Min);
	}
	else if(g_Config.m_XcBlockOutlineMode == MODE_CORNERS)
	{
		// short brackets in the corners that breathe a little
		const float Len = 9.0f + 1.5f * std::sin(Client()->GlobalTime() * 4.0f);
		const vec2 aCorners[4] = {Min, vec2(Max.x, Min.y), Max, vec2(Min.x, Max.y)};
		for(int i = 0; i < 4; i++)
		{
			const vec2 C = aCorners[i];
			const float Sx = C.x == Min.x ? 1.0f : -1.0f;
			const float Sy = C.y == Min.y ? 1.0f : -1.0f;
			Line(C, C + vec2(Len * Sx, 0.0f));
			Line(C, C + vec2(0.0f, Len * Sy));
		}
	}
	Graphics()->TextureClear();
	GameClient()->m_FxRenderer.Draw(vShapes, Glow);

	// the name of the block above it
	if(g_Config.m_XcBlockOutlineLabel && OnBlock && pName)
	{
		char aLabel[64];
		if(Tele)
			str_format(aLabel, sizeof(aLabel), "%s %d", XcUi::Tr(pName), Tele % 1000);
		else
			str_copy(aLabel, XcUi::Tr(pName));
		const float FontSize = 9.0f;
		const float TextWidth = TextRender()->TextWidth(FontSize, aLabel, -1, -1.0f);
		const vec2 TextPos(Center.x - TextWidth * 0.5f, Min.y - FontSize - 6.0f);
		Graphics()->DrawRect(TextPos.x - 4.0f, TextPos.y - 1.5f, TextWidth + 8.0f, FontSize + 4.0f, ColorRGBA(0.04f, 0.05f, 0.06f, 0.65f * m_Shown), IGraphics::CORNER_ALL, 3.0f);
		TextRender()->TextColor(Color.r * 0.4f + 0.6f, Color.g * 0.4f + 0.6f, Color.b * 0.4f + 0.6f, m_Shown);
		TextRender()->Text(TextPos.x, TextPos.y, FontSize, aLabel, -1.0f);
		TextRender()->TextColor(TextRender()->DefaultTextColor());
	}
}

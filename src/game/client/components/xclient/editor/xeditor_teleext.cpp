// Teleporter numbers above 255. The map keeps one byte per number, but the check teleporters have their own 255
// outs: a tele checkpoint tile right in front of a check tele in sends the tee to the check out of that number, like
// a plain teleporter. The brush paints a number N above 255 as such a pair: the in becomes a check tele in with the
// number N - 255 (the server does not read it, it marks the pair here), a ring of tele checkpoints N - 255 goes round
// it, the out becomes a check out N - 255. Standard tiles only: works on every server.
#include <base/math.h>
#include <base/str.h>
#include <base/time.h>

#include <engine/graphics.h>
#include <engine/shared/config.h>
#include <engine/textrender.h>

#include <game/editor/editor.h>
#include <game/editor/mapitems/layer_tele.h>
#include <game/mapitems.h>

#include <algorithm>
#include <bitset>
#include <map>

namespace
{
	bool IsCheckIn(int Type)
	{
		return Type == TILE_TELECHECKIN || Type == TILE_TELECHECKINEVIL;
	}

	// the number of the pair (1..254), 0 for a check tele in without the ring: the old editor writes 255 there
	int ExtNumber(const CTeleTile *pTele, int w, int h, int x, int y)
	{
		const CTeleTile &Tile = pTele[y * w + x];
		if(!IsCheckIn(Tile.m_Type) || Tile.m_Number < 1 || Tile.m_Number > CLayerTele::XC_TELE_EXT_MAX)
			return 0;
		for(int dy = -1; dy <= 1; dy++)
			for(int dx = -1; dx <= 1; dx++)
			{
				const int nx = x + dx, ny = y + dy;
				if((dx || dy) && nx >= 0 && ny >= 0 && nx < w && ny < h && pTele[ny * w + nx].m_Type == TILE_TELECHECK && pTele[ny * w + nx].m_Number == Tile.m_Number)
					return Tile.m_Number;
			}
		return 0;
	}

	// the pair whose ring this tele checkpoint is, 0 if it is a real one
	int RingNumber(const CTeleTile *pTele, int w, int h, int x, int y)
	{
		const CTeleTile &Tile = pTele[y * w + x];
		if(Tile.m_Type != TILE_TELECHECK)
			return 0;
		for(int dy = -1; dy <= 1; dy++)
			for(int dx = -1; dx <= 1; dx++)
			{
				const int nx = x + dx, ny = y + dy;
				if((dx || dy) && nx >= 0 && ny >= 0 && nx < w && ny < h && IsCheckIn(pTele[ny * w + nx].m_Type) && pTele[ny * w + nx].m_Number == Tile.m_Number && Tile.m_Number <= CLayerTele::XC_TELE_EXT_MAX)
					return Tile.m_Number;
			}
		return 0;
	}

	struct SExtCache
	{
		int64_t m_Time = 0;
		std::bitset<256> m_Used;
	};
	std::map<const CLayerTele *, SExtCache> s_ExtCache;
}

void CLayerTele::XcConvertExt(int Index)
{
	const int Number = Editor()->m_XcTeleExt;
	if(Number < 1 || Number > XC_TELE_EXT_MAX)
		return;
	int Type = m_pTeleTile[Index].m_Type;
	if(Type == TILE_TELEIN)
		Type = TILE_TELECHECKIN;
	else if(Type == TILE_TELEINEVIL)
		Type = TILE_TELECHECKINEVIL;
	else if(Type == TILE_TELEOUT)
		Type = TILE_TELECHECKOUT;
	else
		return; // weapon and hook teleporters only know the plain outs
	m_pTeleTile[Index].m_Type = Type;
	m_pTeleTile[Index].m_Number = Number;
	m_pTiles[Index].m_Index = Type;
}

void CLayerTele::XcSyncRings(int sx, int sy, int w, int h)
{
	const int x0 = std::max(sx - 1, 0), y0 = std::max(sy - 1, 0);
	const int x1 = std::min(sx + w + 1, m_Width), y1 = std::min(sy + h + 1, m_Height);
	if(x0 >= x1 || y0 >= y1)
		return;
	bool Changed = false;
	for(int y = y0; y < y1; y++)
		for(int x = x0; x < x1; x++)
		{
			const int i = y * m_Width + x;
			const STeleTileStateChange::SData Previous{m_pTeleTile[i].m_Number, m_pTeleTile[i].m_Type, m_pTiles[i].m_Index};
			// the pair next to this cell, now and before this stroke
			int Want = 0, Before = 0;
			for(int dy = -1; dy <= 1; dy++)
				for(int dx = -1; dx <= 1; dx++)
				{
					const int nx = x + dx, ny = y + dy;
					if(!(dx || dy) || nx < 0 || ny < 0 || nx >= m_Width || ny >= m_Height)
						continue;
					const CTeleTile &Near = m_pTeleTile[ny * m_Width + nx];
					if(!Want && IsCheckIn(Near.m_Type) && Near.m_Number >= 1 && Near.m_Number <= XC_TELE_EXT_MAX)
					{
						// only pairs: a check tele in the brush did not make keeps its plain look
						const auto RowIt = m_History.find(ny);
						const bool Painted = RowIt != m_History.end() && RowIt->second.contains(nx);
						if(Painted || ExtNumber(m_pTeleTile, m_Width, m_Height, nx, ny))
							Want = Near.m_Number;
					}
					const auto RowIt = m_History.find(ny);
					if(RowIt != m_History.end())
					{
						const auto CellIt = RowIt->second.find(nx);
						if(CellIt != RowIt->second.end() && IsCheckIn(CellIt->second.m_Previous.m_Type) && CellIt->second.m_Previous.m_Number <= XC_TELE_EXT_MAX)
							Before = CellIt->second.m_Previous.m_Number;
					}
				}
			if(Want && m_pTiles[i].m_Index == TILE_AIR)
			{
				m_pTeleTile[i].m_Type = TILE_TELECHECK;
				m_pTeleTile[i].m_Number = Want;
				m_pTiles[i].m_Index = TILE_TELECHECK;
			}
			else if(!Want && Before && m_pTeleTile[i].m_Type == TILE_TELECHECK && m_pTeleTile[i].m_Number == Before)
			{
				// the in of the pair is gone, its ring goes with it
				m_pTeleTile[i].m_Type = 0;
				m_pTeleTile[i].m_Number = 0;
				m_pTiles[i].m_Index = TILE_AIR;
			}
			else
				continue;
			RecordStateChange(x, y, Previous, {m_pTeleTile[i].m_Number, m_pTeleTile[i].m_Type, m_pTiles[i].m_Index});
			Changed = true;
		}
	if(Changed)
		FlagModified(x0, y0, x1 - x0, y1 - y0);
}

int CLayerTele::XcFindFreeExt() const
{
	for(int i = 1; i <= XC_TELE_EXT_MAX; i++)
		if(!ContainsElementWithId(i, true))
			return i;
	return -1;
}

bool CLayerTele::XcRenderOverlay(int OverlayRenderFlags)
{
	if(!(OverlayRenderFlags & OVERLAYRENDERFLAG_TEXT))
		return false;
	// which pairs the map has, looked up again twice a second (a whole layer is too much for every frame)
	SExtCache &Cache = s_ExtCache[this];
	const int64_t Now = time_get();
	if(Now - Cache.m_Time > time_freq() / 2)
	{
		Cache.m_Time = Now;
		Cache.m_Used.reset();
		for(int y = 0; y < m_Height; y++)
			for(int x = 0; x < m_Width; x++)
				if(const int Number = ExtNumber(m_pTeleTile, m_Width, m_Height, x, y))
					Cache.m_Used.set(Number);
	}
	if(Cache.m_Used.none())
		return false;

	IGraphics *pGraphics = Editor()->Graphics();
	ITextRender *pTextRender = Editor()->TextRender();
	const CScreenRect ScreenRect = pGraphics->GetScreen();
	const float Scale = 32.0f;
	int StartY = (int)(ScreenRect.m_TopLeft.y / Scale) - 1;
	int StartX = (int)(ScreenRect.m_TopLeft.x / Scale) - 1;
	int EndY = (int)(ScreenRect.m_BottomRight.y / Scale) + 1;
	int EndX = (int)(ScreenRect.m_BottomRight.x / Scale) + 1;
	if(EndX - StartX > pGraphics->ScreenWidth() / g_Config.m_GfxTextOverlay || EndY - StartY > pGraphics->ScreenHeight() / g_Config.m_GfxTextOverlay)
		return true;
	StartY = std::max(0, StartY);
	StartX = std::max(0, StartX);
	EndY = std::min(m_Height, EndY);
	EndX = std::min(m_Width, EndX);

	const float Size = g_Config.m_ClTextEntitiesSize / 100.f;
	char aBuf[16];
	pTextRender->TextColor(1.0f, 1.0f, 1.0f, 1.0f);
	for(int y = StartY; y < EndY; y++)
		for(int x = StartX; x < EndX; x++)
		{
			const CTeleTile &Tile = m_pTeleTile[y * m_Width + x];
			int Shown = 0;
			if(IsCheckIn(Tile.m_Type))
				Shown = ExtNumber(m_pTeleTile, m_Width, m_Height, x, y) ? Tile.m_Number + 255 : 0;
			else if(Tile.m_Type == TILE_TELECHECKOUT && Tile.m_Number && Cache.m_Used.test(Tile.m_Number))
				Shown = Tile.m_Number + 255;
			else if(Tile.m_Type == TILE_TELECHECK && RingNumber(m_pTeleTile, m_Width, m_Height, x, y))
				Shown = 0; // the ring is part of the in, the number is written there
			else if(Tile.m_Number && IsTeleTileNumberUsedAny(Tile.m_Type))
				Shown = Tile.m_Number;
			if(!Shown)
				continue;
			str_format(aBuf, sizeof(aBuf), "%d", Shown);
			const float ScaledWidth = pTextRender->TextWidth(Size * Scale, aBuf, -1);
			const float Factor = std::clamp(Scale / ScaledWidth, 0.0f, 1.0f);
			const float LocalSize = Size * Factor;
			const float ToCenterOffset = (1 - LocalSize) / 2.f;
			pTextRender->Text((x + 0.5f) * Scale - (ScaledWidth * Factor) / 2.0f, (y + ToCenterOffset) * Scale, LocalSize * Scale, aBuf);
		}
	pTextRender->TextColor(pTextRender->DefaultTextColor());
	pGraphics->MapScreen(ScreenRect);
	return true;
}

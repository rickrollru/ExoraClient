#include "xc_live_edit.h"

#include <engine/map.h>
#include <engine/shared/packer.h>

#include <game/layers.h>
#include <game/mapitems.h>

#include <cstring>

namespace XcLiveEdit
{
static CMapItemLayerTilemap *Tilemap(CLayers *pLayers, int Group, int Layer)
{
	if(Group < 0 || Group >= pLayers->NumGroups())
		return nullptr;
	const CMapItemGroup *pGroup = pLayers->GetGroup(Group);
	if(!pGroup || Layer < 0 || Layer >= pGroup->m_NumLayers)
		return nullptr;
	CMapItemLayer *pLayer = pLayers->GetLayer(pGroup->m_StartLayer + Layer);
	if(!pLayer || pLayer->m_Type != LAYERTYPE_TILES)
		return nullptr;
	return reinterpret_cast<CMapItemLayerTilemap *>(pLayer);
}

// the array of the extra data and its cell size
static int ExtraData(const CMapItemLayerTilemap *pTilemap, int *pDataIndex)
{
	*pDataIndex = -1;
	if(pTilemap->m_Version < 3)
		return 0;
	if(pTilemap->m_Flags & TILESLAYERFLAG_TELE)
	{
		*pDataIndex = pTilemap->m_Tele;
		return sizeof(CTeleTile);
	}
	if(pTilemap->m_Flags & TILESLAYERFLAG_SPEEDUP)
	{
		*pDataIndex = pTilemap->m_Speedup;
		return sizeof(CSpeedupTile);
	}
	if(pTilemap->m_Flags & TILESLAYERFLAG_SWITCH)
	{
		*pDataIndex = pTilemap->m_Switch;
		return sizeof(CSwitchTile);
	}
	if(pTilemap->m_Flags & TILESLAYERFLAG_TUNE)
	{
		*pDataIndex = pTilemap->m_Tune;
		return sizeof(CTuneTile);
	}
	return 0;
}

int ExtraSize(CLayers *pLayers, int Group, int Layer)
{
	const CMapItemLayerTilemap *pTilemap = Tilemap(pLayers, Group, Layer);
	if(!pTilemap)
		return -1;
	int DataIndex;
	return ExtraData(pTilemap, &DataIndex);
}

bool Read(CUnpacker *pUnpacker, int *pGroup, int *pLayer, int *pCellSize, std::vector<SRun> *pvRuns)
{
	*pGroup = pUnpacker->GetInt();
	*pLayer = pUnpacker->GetInt();
	*pCellSize = pUnpacker->GetInt();
	const int NumRuns = pUnpacker->GetInt();
	if(pUnpacker->Error() || *pCellSize < (int)sizeof(CTile) || *pCellSize > 16 || NumRuns <= 0 || NumRuns > MAX_MESSAGE_DATA)
		return false;
	pvRuns->clear();
	for(int i = 0; i < NumRuns; i++)
	{
		SRun Run;
		Run.m_Start = pUnpacker->GetInt();
		Run.m_Count = pUnpacker->GetInt();
		if(pUnpacker->Error() || Run.m_Start < 0 || Run.m_Count <= 0 || Run.m_Count * *pCellSize > MAX_MESSAGE_DATA * 2)
			return false;
		Run.m_pCells = pUnpacker->GetRaw(Run.m_Count * *pCellSize);
		if(!Run.m_pCells || pUnpacker->Error())
			return false;
		pvRuns->push_back(Run);
	}
	return true;
}

void Write(CPacker *pPacker, int Group, int Layer, int CellSize, const std::vector<SRun> &vRuns)
{
	pPacker->AddInt(Group);
	pPacker->AddInt(Layer);
	pPacker->AddInt(CellSize);
	pPacker->AddInt((int)vRuns.size());
	for(const SRun &Run : vRuns)
	{
		pPacker->AddInt(Run.m_Start);
		pPacker->AddInt(Run.m_Count);
		pPacker->AddRaw(Run.m_pCells, Run.m_Count * CellSize);
	}
}

bool Apply(CLayers *pLayers, int Group, int Layer, int CellSize, const std::vector<SRun> &vRuns, std::vector<SChange> *pvChanges)
{
	CMapItemLayerTilemap *pTilemap = Tilemap(pLayers, Group, Layer);
	if(!pTilemap)
		return false;
	int ExtraIndex;
	const int Extra = ExtraData(pTilemap, &ExtraIndex);
	if(CellSize != (int)sizeof(CTile) + Extra)
		return false;
	IMap *pMap = pLayers->Map();
	const int Cells = pTilemap->m_Width * pTilemap->m_Height;
	const bool Front = pTilemap->m_Version >= 3 && (pTilemap->m_Flags & TILESLAYERFLAG_FRONT);
	const bool Game = pTilemap->m_Flags & TILESLAYERFLAG_GAME;

	// the tiles: the front layer keeps them in its own data
	const int TileIndex = Front ? pTilemap->m_Front : pTilemap->m_Data;
	CTile *pTiles = nullptr;
	if(TileIndex >= 0 && pMap->GetDataSize(TileIndex) >= Cells * (int)sizeof(CTile))
		pTiles = static_cast<CTile *>(pMap->GetData(TileIndex));
	uint8_t *pExtra = nullptr;
	if(Extra && ExtraIndex >= 0 && pMap->GetDataSize(ExtraIndex) >= Cells * Extra)
		pExtra = static_cast<uint8_t *>(pMap->GetData(ExtraIndex));
	if(!pTiles && !pExtra)
		return false;

	for(const SRun &Run : vRuns)
	{
		if(Run.m_Start + Run.m_Count > Cells)
			continue;
		for(int i = 0; i < Run.m_Count; i++)
		{
			const int Cell = Run.m_Start + i;
			const uint8_t *pCell = Run.m_pCells + (size_t)i * CellSize;
			if(pTiles)
			{
				CTile Tile;
				std::memcpy(&Tile, pCell, sizeof(CTile));
				if(pvChanges && (Game || Front) && pTiles[Cell].m_Index != Tile.m_Index)
					pvChanges->push_back({Cell % pTilemap->m_Width, Cell / pTilemap->m_Width, pTiles[Cell].m_Index, Tile.m_Index, false, Front});
				pTiles[Cell] = Tile;
			}
			if(pExtra)
			{
				if(pvChanges && (pTilemap->m_Flags & TILESLAYERFLAG_SWITCH))
				{
					CSwitchTile Old, New;
					std::memcpy(&Old, pExtra + (size_t)Cell * Extra, sizeof(CSwitchTile));
					std::memcpy(&New, pCell + sizeof(CTile), sizeof(CSwitchTile));
					if(Old.m_Type != New.m_Type)
						pvChanges->push_back({Cell % pTilemap->m_Width, Cell / pTilemap->m_Width, Old.m_Type, New.m_Type, true, false});
				}
				std::memcpy(pExtra + (size_t)Cell * Extra, pCell + sizeof(CTile), Extra);
			}
		}
	}
	return true;
}
} // namespace XcLiveEdit

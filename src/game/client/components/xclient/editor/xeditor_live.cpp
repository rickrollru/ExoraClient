#include "xeditor_live.h"

#include "xeditor_coop.h"

#include <base/log.h>
#include <base/net.h>
#include <base/str.h>

#include <engine/client.h>
#include <engine/serverbrowser.h>
#include <engine/shared/packer.h>

#include <game/editor/editor.h>
#include <game/editor/mapitems/layer_group.h>
#include <game/editor/mapitems/layer_speedup.h>
#include <game/editor/mapitems/layer_switch.h>
#include <game/editor/mapitems/layer_tele.h>
#include <game/editor/mapitems/layer_tiles.h>
#include <game/editor/mapitems/layer_tune.h>
#include <game/xc_live_edit.h>

#include <generated/protocol.h>

#include <cstring>

namespace
{
// the extra data of a physics layer next to its CTile array
int ExtraSize(const CLayerTiles *pLayer)
{
	if(pLayer->m_HasTele)
		return sizeof(CTeleTile);
	if(pLayer->m_HasSpeedup)
		return sizeof(CSpeedupTile);
	if(pLayer->m_HasSwitch)
		return sizeof(CSwitchTile);
	if(pLayer->m_HasTune)
		return sizeof(CTuneTile);
	return 0;
}

const uint8_t *ExtraData(CLayerTiles *pLayer)
{
	if(pLayer->m_HasTele)
		return (const uint8_t *)static_cast<CLayerTele *>(pLayer)->m_pTeleTile;
	if(pLayer->m_HasSpeedup)
		return (const uint8_t *)static_cast<CLayerSpeedup *>(pLayer)->m_pSpeedupTile;
	if(pLayer->m_HasSwitch)
		return (const uint8_t *)static_cast<CLayerSwitch *>(pLayer)->m_pSwitchTile;
	if(pLayer->m_HasTune)
		return (const uint8_t *)static_cast<CLayerTune *>(pLayer)->m_pTuneTile;
	return nullptr;
}

int LayerFlags(const CLayerTiles *pLayer)
{
	return (pLayer->m_HasGame ? 1 : 0) | (pLayer->m_HasTele ? 2 : 0) | (pLayer->m_HasSpeedup ? 4 : 0) | (pLayer->m_HasFront ? 8 : 0) | (pLayer->m_HasSwitch ? 16 : 0) | (pLayer->m_HasTune ? 32 : 0);
}

CLayerTiles *TileLayer(CEditorMap *pMap, int Group, int Layer)
{
	if(Group < 0 || Group >= (int)pMap->m_vpGroups.size())
		return nullptr;
	const auto &vpLayers = pMap->m_vpGroups[Group]->m_vpLayers;
	if(Layer < 0 || Layer >= (int)vpLayers.size() || vpLayers[Layer]->m_Type != LAYERTYPE_TILES)
		return nullptr;
	return static_cast<CLayerTiles *>(vpLayers[Layer].get());
}

// the cells of a layer as they travel: CTile, then the extra data
void ReadCells(CLayerTiles *pLayer, std::vector<uint8_t> &vOut)
{
	const int Extra = ExtraSize(pLayer);
	const int CellSize = sizeof(CTile) + Extra;
	const size_t Cells = (size_t)pLayer->m_Width * pLayer->m_Height;
	vOut.resize(Cells * CellSize);
	const uint8_t *pExtra = Extra ? ExtraData(pLayer) : nullptr;
	for(size_t i = 0; i < Cells; i++)
	{
		std::memcpy(&vOut[i * CellSize], &pLayer->m_pTiles[i], sizeof(CTile));
		if(Extra)
		{
			if(pExtra)
				std::memcpy(&vOut[i * CellSize + sizeof(CTile)], pExtra + i * Extra, Extra);
			else
				std::memset(&vOut[i * CellSize + sizeof(CTile)], 0, Extra);
		}
	}
}
} // namespace

void CXcLiveEdit::Start(CEditorMap *pMap, uint32_t CoopMapId, const char *pMapName)
{
	Stop();
	m_pMap = CoopMapId ? nullptr : pMap;
	m_CoopMapId = CoopMapId;
	m_MapName = pMapName;
	TakeSnapshot(pMap);
	log_info("xc_live", "live editing of '%s' started", pMapName);
}

void CXcLiveEdit::Stop()
{
	m_pMap = nullptr;
	m_CoopMapId = 0;
	m_vSnaps.clear();
	m_vQueue.clear();
	m_StructureChanged = false;
}

CEditorMap *CXcLiveEdit::CurrentMap()
{
	if(m_CoopMapId)
	{
		// editing together: the tab may have been loaded again from the room, find it by its id
		for(const auto &pMap : m_pEditor->m_vpMaps)
			if(m_pCoop->MapId(pMap.get()) == m_CoopMapId)
				return pMap.get();
		return nullptr;
	}
	for(const auto &pMap : m_pEditor->m_vpMaps)
		if(pMap.get() == m_pMap)
			return m_pMap;
	return nullptr;
}

void CXcLiveEdit::TakeSnapshot(CEditorMap *pMap)
{
	m_vSnaps.clear();
	m_StructureChanged = false;
	for(int g = 0; g < (int)pMap->m_vpGroups.size(); g++)
	{
		for(int l = 0; l < (int)pMap->m_vpGroups[g]->m_vpLayers.size(); l++)
		{
			CLayerTiles *pLayer = TileLayer(pMap, g, l);
			if(!pLayer || !pLayer->m_pTiles)
				continue;
			SSnap Snap;
			Snap.m_Group = g;
			Snap.m_Layer = l;
			Snap.m_Width = pLayer->m_Width;
			Snap.m_Height = pLayer->m_Height;
			Snap.m_CellSize = sizeof(CTile) + ExtraSize(pLayer);
			Snap.m_Flags = LayerFlags(pLayer);
			Snap.m_Image = pLayer->m_Image;
			Snap.m_Name = pLayer->m_aName;
			ReadCells(pLayer, Snap.m_vCells);
			m_vSnaps.push_back(std::move(Snap));
		}
	}
}

void CXcLiveEdit::Diff(CEditorMap *pMap)
{
	std::vector<uint8_t> vNow;
	for(SSnap &Snap : m_vSnaps)
	{
		CLayerTiles *pLayer = TileLayer(pMap, Snap.m_Group, Snap.m_Layer);
		// another layer at this place, or a new size: the server does not have it
		if(!pLayer || !pLayer->m_pTiles || pLayer->m_Width != Snap.m_Width || pLayer->m_Height != Snap.m_Height ||
			(int)sizeof(CTile) + ExtraSize(pLayer) != Snap.m_CellSize || LayerFlags(pLayer) != Snap.m_Flags || pLayer->m_Image != Snap.m_Image || Snap.m_Name != pLayer->m_aName)
		{
			m_StructureChanged = true;
			continue;
		}
		ReadCells(pLayer, vNow);
		if(std::memcmp(vNow.data(), Snap.m_vCells.data(), vNow.size()) == 0)
			continue;

		// runs of changed cells, packed into messages under the size limit
		const int CellSize = Snap.m_CellSize;
		const int MaxCells = XcLiveEdit::MAX_MESSAGE_DATA / CellSize;
		const int Cells = Snap.m_Width * Snap.m_Height;
		std::vector<XcLiveEdit::SRun> vRuns;
		int Bytes = 0;
		const auto FlushMessage = [&]() {
			if(vRuns.empty())
				return;
			CPacker Packer;
			Packer.Reset();
			XcLiveEdit::Write(&Packer, Snap.m_Group, Snap.m_Layer, CellSize, vRuns);
			if(!Packer.Error())
				m_vQueue.emplace_back(Packer.Data(), Packer.Data() + Packer.Size());
			vRuns.clear();
			Bytes = 0;
		};
		int i = 0;
		while(i < Cells)
		{
			if(std::memcmp(&vNow[(size_t)i * CellSize], &Snap.m_vCells[(size_t)i * CellSize], CellSize) == 0)
			{
				i++;
				continue;
			}
			int End = i + 1;
			while(End < Cells && End - i < MaxCells && std::memcmp(&vNow[(size_t)End * CellSize], &Snap.m_vCells[(size_t)End * CellSize], CellSize) != 0)
				End++;
			const int Count = End - i;
			if(Bytes + Count * CellSize + 16 > XcLiveEdit::MAX_MESSAGE_DATA)
				FlushMessage();
			// the snapshot keeps the sent cells, the run points into it
			std::memcpy(&Snap.m_vCells[(size_t)i * CellSize], &vNow[(size_t)i * CellSize], (size_t)Count * CellSize);
			vRuns.push_back({i, Count, &Snap.m_vCells[(size_t)i * CellSize]});
			Bytes += Count * CellSize + 8;
			i = End;
		}
		FlushMessage();
	}
}

void CXcLiveEdit::Update()
{
	if(!Active())
		return;
	CEditor &E = *m_pEditor;
	CEditorMap *pMap = CurrentMap();
	if(!pMap)
	{
		log_info("xc_live", "the live tab is gone");
		Stop();
		return;
	}
	// only into the map that is played, on the local server
	IClient *pClient = E.Client();
	const bool OnServer = pClient->State() == IClient::STATE_ONLINE && net_addr_is_local(&pClient->ServerAddress()) && str_comp(pClient->ServerInfo().m_aMap, m_MapName.c_str()) == 0;
	if(!OnServer)
		return;
	const float Now = pClient->GlobalTime();
	if(Now >= m_NextDiff)
	{
		m_NextDiff = Now + 0.1f;
		if(m_vQueue.size() < 64)
			Diff(pMap);
	}
	// a big fill goes out over a few frames, the server must not take it for a flood
	for(int Sent = 0; Sent < 12 && !m_vQueue.empty(); Sent++)
	{
		CMsgPacker Msg(NETMSGTYPE_CL_XCLIVETILES);
		Msg.AddRaw(m_vQueue.front().data(), (int)m_vQueue.front().size());
		pClient->SendMsgActive(&Msg, MSGFLAG_VITAL);
		m_vQueue.pop_front();
	}
}

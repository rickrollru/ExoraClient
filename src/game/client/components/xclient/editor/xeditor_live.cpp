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
#include <engine/shared/config.h>

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

// the cells of a layer, kept apart: the tiles and the extra data
void ReadLayer(CLayerTiles *pLayer, std::vector<uint8_t> &vTiles, std::vector<uint8_t> &vExtra)
{
	const int Extra = ExtraSize(pLayer);
	const size_t Cells = (size_t)pLayer->m_Width * pLayer->m_Height;
	const uint8_t *pTiles = (const uint8_t *)pLayer->m_pTiles;
	vTiles.assign(pTiles, pTiles + Cells * sizeof(CTile));
	vExtra.clear();
	if(Extra)
	{
		if(const uint8_t *pExtra = ExtraData(pLayer))
			vExtra.assign(pExtra, pExtra + Cells * Extra);
		else
			vExtra.assign(Cells * Extra, 0);
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
			ReadLayer(pLayer, Snap.m_vTiles, Snap.m_vExtra);
			m_vSnaps.push_back(std::move(Snap));
		}
	}
}

// the changed cells since the last look, straight from the layers: whole rows are compared first, a map that did not
// change costs one memcmp per row
void CXcLiveEdit::Diff(CEditorMap *pMap)
{
	std::vector<uint8_t> vPacked;
	vPacked.reserve(XcLiveEdit::MAX_MESSAGE_DATA);
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
		const int Extra = Snap.m_CellSize - (int)sizeof(CTile);
		const uint8_t *pTiles = (const uint8_t *)pLayer->m_pTiles;
		const uint8_t *pExtra = Extra ? ExtraData(pLayer) : nullptr;
		if(Extra && !pExtra)
			continue;
		const size_t RowTiles = (size_t)Snap.m_Width * sizeof(CTile);
		const size_t RowExtra = (size_t)Snap.m_Width * Extra;

		// runs of changed cells, packed into messages under the size limit
		const int CellSize = Snap.m_CellSize;
		const int MaxCells = XcLiveEdit::MAX_MESSAGE_DATA / CellSize;
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
			vPacked.clear();
			Bytes = 0;
		};
		const auto Changed = [&](size_t i) {
			return std::memcmp(pTiles + i * sizeof(CTile), Snap.m_vTiles.data() + i * sizeof(CTile), sizeof(CTile)) != 0 ||
			       (Extra && std::memcmp(pExtra + i * Extra, Snap.m_vExtra.data() + i * Extra, Extra) != 0);
		};
		for(int y = 0; y < Snap.m_Height; y++)
		{
			if(std::memcmp(pTiles + y * RowTiles, Snap.m_vTiles.data() + y * RowTiles, RowTiles) == 0 &&
				(!Extra || std::memcmp(pExtra + y * RowExtra, Snap.m_vExtra.data() + y * RowExtra, RowExtra) == 0))
				continue;
			int x = 0;
			while(x < Snap.m_Width)
			{
				const size_t Start = (size_t)y * Snap.m_Width + x;
				if(!Changed(Start))
				{
					x++;
					continue;
				}
				int End = x + 1;
				while(End < Snap.m_Width && End - x < MaxCells && Changed((size_t)y * Snap.m_Width + End))
					End++;
				const int Count = End - x;
				if(Bytes + Count * CellSize + 16 > XcLiveEdit::MAX_MESSAGE_DATA)
					FlushMessage();
				// the cells as they travel: CTile, then the extra data; the snapshot takes them
				const size_t PackedAt = vPacked.size();
				for(size_t i = Start; i < Start + Count; i++)
				{
					vPacked.insert(vPacked.end(), pTiles + i * sizeof(CTile), pTiles + (i + 1) * sizeof(CTile));
					std::memcpy(Snap.m_vTiles.data() + i * sizeof(CTile), pTiles + i * sizeof(CTile), sizeof(CTile));
					if(Extra)
					{
						vPacked.insert(vPacked.end(), pExtra + i * Extra, pExtra + (i + 1) * Extra);
						std::memcpy(Snap.m_vExtra.data() + i * Extra, pExtra + i * Extra, Extra);
					}
				}
				vRuns.push_back({(int)Start, Count, vPacked.data() + PackedAt});
				Bytes += Count * CellSize + 8;
				x = End;
			}
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
	// with the editor closed (playing) the map only changes by the blocks of the friends, which mark it as changed:
	// it is only looked at then; with the editor open every 0.1 s
	const bool EditorOpen = g_Config.m_ClEditor;
	if(Now >= m_NextDiff && (EditorOpen || pMap->m_LastModifiedTime != m_DiffedModified))
	{
		m_NextDiff = Now + 0.1f;
		if(m_vQueue.size() < 64)
		{
			m_DiffedModified = pMap->m_LastModifiedTime;
			Diff(pMap);
		}
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

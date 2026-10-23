// Editing maps together (see xeditor_coop.h). A small TCP protocol of its own: [u32 size][u8 type][data].
// The host is the authority: he gives every shared tab its id, every whole map a number (epoch), and blocks
// are only applied to the map with the same number, so a late block never lands on a layer that moved.
#include "xeditor_coop.h"

#include <base/io.h>
#include <base/log.h>
#include <base/net.h>
#include <base/str.h>

#include <engine/client.h>
#include <engine/input.h>
#include <engine/keys.h>
#include <engine/shared/config.h>
#include <engine/storage.h>

#include <game/client/gameclient.h>
#include <game/editor/editor.h>
#include <game/editor/mapitems/image.h>
#include <game/editor/mapitems/layer_group.h>
#include <game/editor/mapitems/layer_quads.h>
#include <game/editor/mapitems/layer_sounds.h>
#include <game/editor/mapitems/layer_speedup.h>
#include <game/editor/mapitems/layer_switch.h>
#include <game/editor/mapitems/layer_tele.h>
#include <game/editor/mapitems/layer_tiles.h>
#include <game/editor/mapitems/layer_tune.h>
#include <game/editor/mapitems/sound.h>

#include <algorithm>
#include <atomic>
#include <cstring>
#include <thread>

namespace
{
enum
{
	MSG_HELLO = 1,
	MSG_WELCOME, // [id][maps]
	MSG_MEMBERS,
	MSG_MAP, // [map][epoch][base][flags][author][token][name][file]
	MSG_EPOCH, // [map][epoch]: the host took the change of the guest
	MSG_TILES, // [map][epoch][author][diff]
	MSG_CURSOR, // [member][visible][x][y][group][layer][map]
	MSG_BYE,
	MSG_CLOSE, // [map][member]: the tab was closed
	MSG_ASSIGN, // [token][map][epoch]: the id of a guest's new tab
	MSG_CHECK, // [map][epoch][hash]: the host's state of the map
	MSG_RESYNC, // [map]: a guest is out of sync and asks for the whole map
	MSG_PLAY_REQ, // [map]: a guest asks the host to play this map together
	MSG_PLAY, // [port][map]: the host's local server is up, join it
	MSG_TEE, // [member][visible][x][y][angle][map][skin][body][feet][custom]: the tee of a member playing the map
};
enum
{
	MAPFLAG_ANSWER = 1, // the map answers the change of the guest who gets it
	MAPFLAG_REPLACE = 2, // take it as it is: the copy of the guest was out of sync
	MAPFLAG_NEW = 4, // a guest opened a new tab (the map id is 0, the token names it)
};
constexpr int PROTOCOL_VERSION = 2;
constexpr float VERIFY_INTERVAL = 2.0f;
constexpr int PLAY_PORT = 8303;
const char *const PLAY_MAP = "xc_coop_play"; // maps/xc_coop_play.map on the host
constexpr uint32_t MAX_MESSAGE = 256 * 1024 * 1024;
const char *const COOP_FOLDER = "xclient/coop";

// little endian packing
class CCoopPacker
{
public:
	std::vector<uint8_t> m_vData;
	void U8(int v) { m_vData.push_back((uint8_t)v); }
	void U16(int v)
	{
		U8(v & 0xff);
		U8((v >> 8) & 0xff);
	}
	void U32(uint32_t v)
	{
		for(int i = 0; i < 4; i++)
			U8((v >> (i * 8)) & 0xff);
	}
	void F32(float f)
	{
		uint32_t v;
		std::memcpy(&v, &f, 4);
		U32(v);
	}
	void Str(const char *p)
	{
		const int Len = std::min(str_length(p), 255);
		U8(Len);
		Raw(p, Len);
	}
	void Raw(const void *p, size_t Size)
	{
		const uint8_t *b = (const uint8_t *)p;
		m_vData.insert(m_vData.end(), b, b + Size);
	}
};
class CCoopUnpacker
{
public:
	const uint8_t *m_pData;
	size_t m_Size;
	size_t m_Pos = 0;
	bool m_Error = false;
	CCoopUnpacker(const uint8_t *pData, size_t Size) :
		m_pData(pData), m_Size(Size) {}
	bool Need(size_t n)
	{
		if(m_Error || m_Pos + n > m_Size)
		{
			m_Error = true;
			return false;
		}
		return true;
	}
	int U8() { return Need(1) ? m_pData[m_Pos++] : 0; }
	int U16()
	{
		const int a = U8();
		return a | (U8() << 8);
	}
	uint32_t U32()
	{
		uint32_t v = 0;
		for(int i = 0; i < 4; i++)
			v |= (uint32_t)U8() << (i * 8);
		return v;
	}
	float F32()
	{
		const uint32_t v = U32();
		float f;
		std::memcpy(&f, &v, 4);
		return f;
	}
	std::string Str()
	{
		const int Len = U8();
		if(!Need(Len))
			return "";
		std::string s((const char *)m_pData + m_Pos, Len);
		m_Pos += Len;
		return s;
	}
	const uint8_t *Raw(size_t n)
	{
		if(!Need(n))
			return nullptr;
		const uint8_t *p = m_pData + m_Pos;
		m_Pos += n;
		return p;
	}
	size_t Left() const { return m_Error ? 0 : m_Size - m_Pos; }
};

uint64_t Hash(uint64_t h, const void *p, size_t Size)
{
	const uint8_t *b = (const uint8_t *)p;
	for(size_t i = 0; i < Size; i++)
	{
		h ^= b[i];
		h *= 1099511628211ull;
	}
	return h;
}
uint64_t HashInt(uint64_t h, int v) { return Hash(h, &v, sizeof(v)); }
uint64_t HashStr(uint64_t h, const char *p) { return Hash(h, p, str_length(p) + 1); }

// the special data of a tile layer next to its CTile array
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
uint8_t *ExtraData(CLayerTiles *pLayer)
{
	if(pLayer->m_HasTele)
		return (uint8_t *)static_cast<CLayerTele *>(pLayer)->m_pTeleTile;
	if(pLayer->m_HasSpeedup)
		return (uint8_t *)static_cast<CLayerSpeedup *>(pLayer)->m_pSpeedupTile;
	if(pLayer->m_HasSwitch)
		return (uint8_t *)static_cast<CLayerSwitch *>(pLayer)->m_pSwitchTile;
	if(pLayer->m_HasTune)
		return (uint8_t *)static_cast<CLayerTune *>(pLayer)->m_pTuneTile;
	return nullptr;
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
}


// a quick hash of a big block of memory (the tiles of a layer), 8 bytes at a time
static uint64_t HashBlock(uint64_t h, const uint8_t *p, size_t Size)
{
	size_t i = 0;
	for(; i + 8 <= Size; i += 8)
	{
		uint64_t v;
		std::memcpy(&v, p + i, 8);
		h = (h ^ v) * 1099511628211ull;
		h ^= h >> 29;
	}
	return Hash(h, p + i, Size - i);
}

// a guest connects in its own thread: a wrong address can take many seconds to fail
struct CXcCoop::SConnectJob
{
	NETSOCKET m_Sock = nullptr;
	NETADDR m_Addr;
	int m_Result = -1;
	std::atomic<bool> m_Finished{false};
};

CXcCoop::~CXcCoop()
{
	Leave();
}

unsigned CXcCoop::MemberColor(int Id)
{
	static const unsigned s_aColors[] = {0x4F8CFF, 0x3DD6A4, 0xFF8A3D, 0xFF5C8A, 0xB6E83C, 0xB98CFF, 0xFFD43D, 0x3DC8FF};
	return s_aColors[(Id < 0 ? 0 : Id) % (int)std::size(s_aColors)];
}

float CXcCoop::Now() const
{
	return m_pEditor->Client()->GlobalTime();
}

void CXcCoop::OnActivity()
{
	if(m_Role != ROLE_NONE)
		m_LastActivity = Now();
}

std::string CXcCoop::TakeNotice()
{
	std::string Notice;
	std::swap(Notice, m_Notice);
	return Notice;
}

CXcCoop::SMember *CXcCoop::FindMember(int Id)
{
	for(SMember &Member : m_vMembers)
		if(Member.m_Id == Id)
			return &Member;
	return nullptr;
}

CXcCoop::SConn *CXcCoop::ConnOf(int MemberId)
{
	for(auto &pConn : m_vpConns)
		if(pConn->m_MemberId == MemberId && !pConn->m_Dead)
			return pConn.get();
	return nullptr;
}

CXcCoop::SShared *CXcCoop::FindShared(uint32_t Id)
{
	if(Id == 0)
		return nullptr;
	for(auto &pShared : m_vpShared)
		if(pShared->m_Id == Id)
			return pShared.get();
	return nullptr;
}

CXcCoop::SShared *CXcCoop::FindSharedMap(const CEditorMap *pMap)
{
	for(auto &pShared : m_vpShared)
		if(pShared->m_pMap == pMap)
			return pShared.get();
	return nullptr;
}

CXcCoop::SShared *CXcCoop::FindToken(uint32_t Token)
{
	for(auto &pShared : m_vpShared)
		if(pShared->m_Token == Token && pShared->m_Id == 0)
			return pShared.get();
	return nullptr;
}

uint32_t CXcCoop::MapId(const CEditorMap *pMap) const
{
	for(const auto &pShared : m_vpShared)
		if(pShared->m_pMap == pMap && pMap)
			return pShared->m_Id;
	return 0;
}

int CXcCoop::MapIndex(const CEditorMap *pMap) const
{
	for(size_t i = 0; i < m_pEditor->m_vpMaps.size(); i++)
		if(m_pEditor->m_vpMaps[i].get() == pMap)
			return (int)i;
	return -1;
}

static const char *OwnName()
{
	return g_Config.m_PlayerName[0] ? g_Config.m_PlayerName : "Player";
}

bool CXcCoop::Host(int Port, char *pError, int ErrorSize)
{
	Leave();
	NETADDR Bind = {};
	Bind.type = NETTYPE_IPV4;
	Bind.port = Port;
	m_Listen = net_tcp_create(Bind);
	if(!m_Listen || net_tcp_listen(m_Listen, 8) != 0)
	{
		if(m_Listen)
			net_tcp_close(m_Listen);
		m_Listen = nullptr;
		str_copy(pError, "The port is busy or blocked", ErrorSize);
		return false;
	}
	net_set_non_blocking(m_Listen);
	m_Role = ROLE_HOST;
	m_State = STATE_ONLINE;
	m_SelfId = 0;
	m_Port = Port;
	SMember Self;
	Self.m_Id = 0;
	Self.m_Name = OwnName();
	m_vMembers = {Self};
	m_LastActivity = Now();
	// every open tab is shared
	for(const auto &pMap : m_pEditor->m_vpMaps)
	{
		auto pShared = std::make_unique<SShared>();
		pShared->m_Id = m_NextMapId++;
		pShared->m_pMap = pMap.get();
		pShared->m_Epoch = 1;
		ResetSync(*pShared);
		m_vpShared.push_back(std::move(pShared));
	}
	log_info("xc_coop", "room open on port %d, %d maps", Port, (int)m_vpShared.size());
	return true;
}

bool CXcCoop::Join(const char *pAddress, char *pError, int ErrorSize)
{
	Leave();
	char aAddress[128];
	str_copy(aAddress, pAddress);
	str_utf8_trim_right(aAddress);
	const char *pStart = aAddress;
	while(*pStart == ' ')
		pStart++;
	NETADDR Addr;
	if(net_addr_from_str(&Addr, pStart) != 0)
	{
		// a name instead of an IP, or an IP without the port
		char aHost[128];
		str_copy(aHost, pStart);
		int Port = 8314;
		const char *pColon = str_rchr(aHost, ':');
		if(pColon && pColon == str_find(aHost, ":"))
		{
			Port = str_toint(pColon + 1);
			aHost[pColon - aHost] = '\0';
		}
		if(net_addr_from_str(&Addr, aHost) != 0 && net_host_lookup(aHost, &Addr, NETTYPE_IPV4) != 0)
		{
			str_copy(pError, "Wrong address. Write it like 26.12.34.56:8314", ErrorSize);
			return false;
		}
		Addr.port = Port;
	}
	if(Addr.port == 0)
		Addr.port = 8314;
	Addr.type = NETTYPE_IPV4;

	NETADDR Bind = {};
	Bind.type = NETTYPE_IPV4;
	NETSOCKET Sock = net_tcp_create(Bind);
	if(!Sock)
	{
		str_copy(pError, "Could not create a network socket", ErrorSize);
		return false;
	}
	m_pConnect = std::make_shared<SConnectJob>();
	m_pConnect->m_Sock = Sock;
	m_pConnect->m_Addr = Addr;
	std::shared_ptr<SConnectJob> pJob = m_pConnect;
	std::thread([pJob]() {
		pJob->m_Result = net_tcp_connect(pJob->m_Sock, &pJob->m_Addr);
		// the editor gave up on this connection: the socket is ours to close
		if(pJob->m_Finished.exchange(true))
			net_tcp_close(pJob->m_Sock);
	}).detach();

	m_Role = ROLE_GUEST;
	m_State = STATE_CONNECTING;
	m_SelfId = -1;
	net_addr_str(&Addr, m_aAddress, sizeof(m_aAddress), true);
	log_info("xc_coop", "connecting to %s", m_aAddress);
	m_vMembers.clear();
	// the tabs open now stay this player's own, the room's tabs come next to them
	m_PrivateMaps.clear();
	for(const auto &pMap : m_pEditor->m_vpMaps)
		m_PrivateMaps.insert(pMap.get());
	m_LastActivity = Now();
	return true;
}

void CXcCoop::Leave(const char *pReason)
{
	if(m_Role == ROLE_NONE)
		return;
	for(auto &pConn : m_vpConns)
	{
		if(pConn->m_Sock)
		{
			CCoopPacker Bye;
			Bye.Str("left");
			Queue(*pConn, MSG_BYE, Bye.m_vData.data(), Bye.m_vData.size(), false);
			// one last try to send what is left, without waiting
			if(pConn->m_vOut.size() > pConn->m_OutPos)
				net_tcp_send(pConn->m_Sock, pConn->m_vOut.data() + pConn->m_OutPos, (int)std::min<size_t>(pConn->m_vOut.size() - pConn->m_OutPos, 1 << 20));
			net_tcp_close(pConn->m_Sock);
			pConn->m_Sock = nullptr;
		}
	}
	m_vpConns.clear();
	if(m_Listen)
		net_tcp_close(m_Listen);
	m_Listen = nullptr;
	if(m_pConnect)
	{
		// the thread is done: close here; else the thread closes when it is done
		if(m_pConnect->m_Finished.exchange(true))
			net_tcp_close(m_pConnect->m_Sock);
		m_pConnect = nullptr;
	}
	m_Role = ROLE_NONE;
	m_State = STATE_IDLE;
	m_SelfId = -1;
	m_vpShared.clear();
	m_PrivateMaps.clear();
	m_vMembers.clear();
	m_vSaves.clear();
	m_Progress = -1.0f;
	if(pReason)
		m_Notice = pReason;
	log_info("xc_coop", "session ended%s%s", pReason ? ": " : "", pReason ? pReason : "");
}

void CXcCoop::ResetSync(SShared &Shared)
{
	Shared.m_vIncoming.clear();
	Shared.m_HasIncoming = false;
	Shared.m_pIncomingFrom = nullptr;
	Shared.m_ProposalPending = false;
	Shared.m_Mismatches = 0;
	Shared.m_NextVerify = Now() + VERIFY_INTERVAL;
	if(Shared.m_pMap)
	{
		TakeSnapshot(Shared);
		Shared.m_SyncedHash = Shared.m_LastHash = StateHash(Shared.m_pMap);
	}
}

bool CXcCoop::BroadcastSavePending(uint32_t MapId) const
{
	for(const SPendingSave &Save : m_vSaves)
		if(Save.m_MapId == MapId && (Save.m_Kind == SAVE_CHANGE || Save.m_Kind == SAVE_NEW))
			return true;
	return false;
}

bool CXcCoop::SendingBlocked(const SShared &Shared) const
{
	return Shared.m_Id == 0 || Shared.m_ProposalPending || Shared.m_HasIncoming || Shared.m_SyncedHash != Shared.m_LastHash || BroadcastSavePending(Shared.m_Id);
}

static bool MouseBusy(CEditor &E)
{
	return E.Input()->KeyIsPressed(KEY_MOUSE_1) || E.Input()->KeyIsPressed(KEY_MOUSE_2) || E.Input()->KeyIsPressed(KEY_MOUSE_3) || !E.Ui()->CheckActiveItem(nullptr);
}

// tabs opened or closed by this player: shared with the room or closed for everyone
void CXcCoop::TrackTabs()
{
	CEditor &E = *m_pEditor;
	for(auto It = m_PrivateMaps.begin(); It != m_PrivateMaps.end();)
		It = MapIndex(*It) < 0 ? m_PrivateMaps.erase(It) : std::next(It);

	for(size_t i = 0; i < m_vpShared.size();)
	{
		SShared &Shared = *m_vpShared[i];
		// a whole new tab from the room that is not loaded yet has no map
		if(!Shared.m_pMap || MapIndex(Shared.m_pMap) >= 0)
		{
			i++;
			continue;
		}
		if(Shared.m_Id != 0)
		{
			CCoopPacker Close;
			Close.U32(Shared.m_Id);
			Close.U8(m_SelfId < 0 ? 0 : m_SelfId);
			Broadcast(MSG_CLOSE, Close.m_vData);
			log_info("xc_coop", "map %u closed", Shared.m_Id);
		}
		const uint32_t Id = Shared.m_Id;
		m_vSaves.erase(std::remove_if(m_vSaves.begin(), m_vSaves.end(), [&](const SPendingSave &Save) { return Id != 0 && Save.m_MapId == Id; }), m_vSaves.end());
		m_vpShared.erase(m_vpShared.begin() + i);
	}

	for(const auto &pMap : E.m_vpMaps)
	{
		if(FindSharedMap(pMap.get()) || m_PrivateMaps.count(pMap.get()))
			continue;
		auto pShared = std::make_unique<SShared>();
		pShared->m_pMap = pMap.get();
		SShared &Shared = *pShared;
		m_vpShared.push_back(std::move(pShared));
		ResetSync(Shared);
		if(m_Role == ROLE_HOST)
		{
			// a new tab of the host: everyone gets it
			Shared.m_Id = m_NextMapId++;
			Shared.m_Epoch = 1;
			StartSave(Shared, SAVE_CHANGE);
		}
		else
		{
			// a new tab of a guest: the host gives it an id and shows it to the others
			Shared.m_Token = m_NextToken++;
			if(StartSave(Shared, SAVE_NEW))
				Shared.m_ProposalPending = true;
		}
		log_info("xc_coop", "sharing a new tab");
	}
}

void CXcCoop::CloseSharedTab(uint32_t Id, int ByMember)
{
	CEditor &E = *m_pEditor;
	for(size_t i = 0; i < m_vpShared.size(); i++)
	{
		if(m_vpShared[i]->m_Id != Id)
			continue;
		CEditorMap *pMap = m_vpShared[i]->m_pMap;
		m_vpShared.erase(m_vpShared.begin() + i);
		m_vSaves.erase(std::remove_if(m_vSaves.begin(), m_vSaves.end(), [&](const SPendingSave &Save) { return Save.m_MapId == Id; }), m_vSaves.end());
		const int Index = MapIndex(pMap);
		if(Index >= 0)
		{
			// the editor keeps at least one tab: then the closed map stays as a tab of this player
			if(E.m_vpMaps.size() > 1)
			{
				E.CloseMap(Index, false);
				m_MapReplaced = true;
			}
			else
				m_PrivateMaps.insert(pMap);
		}
		const SMember *pMember = FindMember(ByMember);
		log_info("xc_coop", "map %u closed by '%s'", Id, pMember ? pMember->m_Name.c_str() : "?");
		return;
	}
}

void CXcCoop::Update()
{
	if(m_Role == ROLE_NONE)
		return;
	CEditor &E = *m_pEditor;
	Poll();
	if(m_Role == ROLE_NONE)
		return;
	FinishSaves();
	// one whole map at a time, when the mouse is free
	if(!MouseBusy(E))
	{
		for(auto &pShared : m_vpShared)
		{
			if(pShared->m_HasIncoming)
			{
				LoadIncomingMap(*pShared);
				break;
			}
		}
	}
	if(m_Role == ROLE_NONE)
		return;

	if(m_PlayRunAt >= 0.0f && Now() >= m_PlayRunAt)
	{
		m_PlayRunAt = -1.0f;
		RunPlayServer();
	}

	if(m_State == STATE_ONLINE)
	{
		TrackTabs();
		const float Time = Now();
		if(Time >= m_NextCheck)
		{
			m_NextCheck = Time + (Time - m_LastActivity < 1.5f ? 0.1f : 0.5f);
			for(auto &pShared : m_vpShared)
				if(pShared->m_pMap)
					UpdateMap(*pShared, Time);
		}
		SendCursor();
		SendTee();
	}
	if(m_State == STATE_ONLINE)
	{
		const float Time = Now();
		EaseTees(m_LastEase > 0.0f ? std::clamp(Time - m_LastEase, 0.0f, 0.1f) : 0.0f);
		m_LastEase = Time;
	}
	Flush();
}

void CXcCoop::UpdateMap(SShared &Shared, float Time)
{
	CEditor &E = *m_pEditor;
	// with the editor closed (playing together) nobody here changes the map, only the blocks of the others come in:
	// no need to look for own changes, which costs a pass over every layer
	if(!g_Config.m_ClEditor && Shared.m_pMap->m_LastModifiedTime == Shared.m_SeenModified && Shared.m_LastHash == Shared.m_SyncedHash)
	{
		SendVerify(Shared, Time);
		return;
	}
	Shared.m_SeenModified = Shared.m_pMap->m_LastModifiedTime;
	const uint64_t Hash = StateHash(Shared.m_pMap);
	if(Hash != Shared.m_LastHash)
	{
		Shared.m_LastHash = Hash;
		Shared.m_HashChangeTime = Time;
	}
	if(Shared.m_Id == 0 || Shared.m_HasIncoming)
		return;
	if(Hash != Shared.m_SyncedHash)
	{
		// layers, quads, envelopes... changed: the whole map goes out when the user pauses
		if(Time - Shared.m_HashChangeTime > 0.35f && !MouseBusy(E) && !Shared.m_ProposalPending && !BroadcastSavePending(Shared.m_Id))
		{
			const bool Started = StartSave(Shared, SAVE_CHANGE);
			Shared.m_SyncedHash = Hash;
			Shared.m_LastHash = Hash;
			TakeSnapshot(Shared);
			if(Started && m_Role == ROLE_GUEST)
				Shared.m_ProposalPending = true;
		}
		return;
	}
	if(SendingBlocked(Shared))
		return;
	std::vector<uint8_t> vDiff;
	if(DiffTiles(Shared, vDiff))
	{
		CCoopPacker Tiles;
		Tiles.U32(Shared.m_Id);
		Tiles.U32(Shared.m_Epoch);
		Tiles.U8(m_SelfId < 0 ? 0 : m_SelfId);
		Tiles.Raw(vDiff.data(), vDiff.size());
		Broadcast(MSG_TILES, Tiles.m_vData);
		// a guest's blocks are on the way: its copy differs from the host's for a moment
		Shared.m_NextVerify = std::max(Shared.m_NextVerify, Time + 1.5f);
	}
	SendVerify(Shared, Time);
}

// the host: the state of the map for everyone to compare (after the blocks, so they agree)
void CXcCoop::SendVerify(SShared &Shared, float Time)
{
	if(m_Role != ROLE_HOST || Time < Shared.m_NextVerify || m_vpConns.empty() || Shared.m_Id == 0 || Shared.m_HasIncoming || SendingBlocked(Shared))
		return;
	Shared.m_NextVerify = Time + VERIFY_INTERVAL;
	CCoopPacker Check;
	Check.U32(Shared.m_Id);
	Check.U32(Shared.m_Epoch);
	const uint64_t Full = FullHash(Shared);
	Check.U32((uint32_t)Full);
	Check.U32((uint32_t)(Full >> 32));
	Broadcast(MSG_CHECK, Check.m_vData);
}

// ---- network

void CXcCoop::Queue(SConn &Conn, int Type, const void *pData, size_t Size, bool Hold)
{
	std::vector<uint8_t> &vTarget = (Hold && Conn.m_AwaitMaps > 0 && m_Role == ROLE_HOST) ? Conn.m_vHeld : Conn.m_vOut;
	const uint32_t Length = (uint32_t)Size + 1;
	for(int i = 0; i < 4; i++)
		vTarget.push_back((Length >> (i * 8)) & 0xff);
	vTarget.push_back((uint8_t)Type);
	const uint8_t *b = (const uint8_t *)pData;
	vTarget.insert(vTarget.end(), b, b + Size);
}

void CXcCoop::Broadcast(int Type, const std::vector<uint8_t> &vData, const SConn *pExcept)
{
	for(auto &pConn : m_vpConns)
		if(pConn.get() != pExcept && !pConn->m_Dead && (m_Role == ROLE_GUEST || pConn->m_Hello))
			Queue(*pConn, Type, vData.data(), vData.size());
}

void CXcCoop::SendMembers()
{
	CCoopPacker List;
	List.U8((int)m_vMembers.size());
	for(const SMember &Member : m_vMembers)
	{
		List.U8(Member.m_Id);
		List.Str(Member.m_Name.c_str());
	}
	for(auto &pConn : m_vpConns)
		if(pConn->m_Hello && !pConn->m_Dead)
			Queue(*pConn, MSG_MEMBERS, List.m_vData.data(), List.m_vData.size(), false);
}

void CXcCoop::CloseConn(SConn &Conn)
{
	if(Conn.m_Sock)
		net_tcp_close(Conn.m_Sock);
	Conn.m_Sock = nullptr;
	Conn.m_Dead = true;
}

void CXcCoop::Poll()
{
	// a guest: is the connection there yet?
	if(m_pConnect && m_pConnect->m_Finished.load())
	{
		std::shared_ptr<SConnectJob> pJob = m_pConnect;
		m_pConnect = nullptr;
		if(pJob->m_Result != 0)
		{
			net_tcp_close(pJob->m_Sock);
			Leave("Could not connect. Check the IP and that the room is open");
			return;
		}
		net_set_non_blocking(pJob->m_Sock);
		auto pConn = std::make_unique<SConn>();
		pConn->m_Sock = pJob->m_Sock;
		pConn->m_Hello = true;
		CCoopPacker Hello;
		Hello.Raw("EXCO", 4);
		Hello.U16(PROTOCOL_VERSION);
		Hello.Str(OwnName());
		Queue(*pConn, MSG_HELLO, Hello.m_vData.data(), Hello.m_vData.size(), false);
		m_vpConns.push_back(std::move(pConn));
		m_State = STATE_JOINING;
	}

	// the host: new guests
	if(m_Listen)
	{
		for(int i = 0; i < 4; i++)
		{
			NETSOCKET NewSock = nullptr;
			NETADDR Addr;
			if(net_tcp_accept(m_Listen, &NewSock, &Addr) < 0 || !NewSock)
				break;
			net_set_non_blocking(NewSock);
			auto pConn = std::make_unique<SConn>();
			pConn->m_Sock = NewSock;
			m_vpConns.push_back(std::move(pConn));
			char aAddr[64];
			net_addr_str(&Addr, aAddr, sizeof(aAddr), true);
			log_info("xc_coop", "connection from %s", aAddr);
		}
	}

	const auto IncomingWaits = [&]() {
		for(const auto &pShared : m_vpShared)
			if(pShared->m_HasIncoming)
				return true;
		return false;
	};
	for(size_t c = 0; c < m_vpConns.size(); c++)
	{
		SConn &Conn = *m_vpConns[c];
		if(Conn.m_Dead || !Conn.m_Sock)
			continue;
		uint8_t aBuf[65536];
		for(int Reads = 0; Reads < 256; Reads++)
		{
			const int Got = net_tcp_recv(Conn.m_Sock, aBuf, sizeof(aBuf));
			if(Got > 0)
			{
				Conn.m_vIn.insert(Conn.m_vIn.end(), aBuf, aBuf + Got);
				continue;
			}
			if(Got == 0 || !net_would_block())
				CloseConn(Conn);
			break;
		}
		// whole messages; a map waiting to be loaded stops everything after it until it is in
		size_t Pos = 0;
		while(!IncomingWaits() && Conn.m_vIn.size() - Pos >= 4)
		{
			const uint8_t *p = Conn.m_vIn.data() + Pos;
			const uint32_t Length = p[0] | (p[1] << 8) | (p[2] << 16) | ((uint32_t)p[3] << 24);
			if(Length == 0 || Length > MAX_MESSAGE)
			{
				CloseConn(Conn);
				break;
			}
			if(Conn.m_vIn.size() - Pos < 4 + (size_t)Length)
				break;
			Pos += 4 + Length;
			Handle(Conn, p[4], p + 5, Length - 1);
			if(m_Role == ROLE_NONE)
				return;
		}
		if(Pos > 0)
			Conn.m_vIn.erase(Conn.m_vIn.begin(), Conn.m_vIn.begin() + Pos);
		// how far a big map has come
		m_Progress = -1.0f;
		if(m_Role == ROLE_GUEST && Conn.m_vIn.size() >= 5)
		{
			const uint8_t *p = Conn.m_vIn.data();
			const uint32_t Length = p[0] | (p[1] << 8) | (p[2] << 16) | ((uint32_t)p[3] << 24);
			if(p[4] == MSG_MAP && Length > 256 * 1024)
				m_Progress = std::clamp((Conn.m_vIn.size() - 4) / (float)Length, 0.0f, 1.0f);
		}
	}

	// gone connections
	for(size_t c = 0; c < m_vpConns.size();)
	{
		SConn &Conn = *m_vpConns[c];
		if(!Conn.m_Dead)
		{
			c++;
			continue;
		}
		if(m_Role == ROLE_GUEST)
		{
			Leave(m_State == STATE_ONLINE ? "The connection to the room was lost" : "The room did not answer");
			return;
		}
		for(auto &pShared : m_vpShared)
			if(pShared->m_pIncomingFrom == &Conn)
				pShared->m_pIncomingFrom = nullptr;
		if(Conn.m_MemberId >= 0)
		{
			const SMember *pMember = FindMember(Conn.m_MemberId);
			if(pMember)
				log_info("xc_coop", "'%s' left", pMember->m_Name.c_str());
			m_vMembers.erase(std::remove_if(m_vMembers.begin(), m_vMembers.end(), [&](const SMember &Member) { return Member.m_Id == Conn.m_MemberId; }), m_vMembers.end());
		}
		const bool WasMember = Conn.m_MemberId >= 0;
		const NETSOCKET Sock = Conn.m_Sock;
		m_vSaves.erase(std::remove_if(m_vSaves.begin(), m_vSaves.end(), [&](const SPendingSave &Save) { return Save.m_Kind == SAVE_TARGET && Sock && Save.m_TargetSock == Sock; }), m_vSaves.end());
		m_vpConns.erase(m_vpConns.begin() + c);
		if(WasMember)
			SendMembers();
	}
}

void CXcCoop::Flush()
{
	for(auto &pConn : m_vpConns)
	{
		SConn &Conn = *pConn;
		if(Conn.m_Dead || !Conn.m_Sock)
			continue;
		while(Conn.m_OutPos < Conn.m_vOut.size())
		{
			const int Size = (int)std::min<size_t>(Conn.m_vOut.size() - Conn.m_OutPos, 1 << 20);
			const int Sent = net_tcp_send(Conn.m_Sock, Conn.m_vOut.data() + Conn.m_OutPos, Size);
			if(Sent > 0)
			{
				Conn.m_OutPos += Sent;
				continue;
			}
			if(Sent < 0 && !net_would_block())
				CloseConn(Conn);
			break;
		}
		if(Conn.m_OutPos >= Conn.m_vOut.size())
		{
			Conn.m_vOut.clear();
			Conn.m_OutPos = 0;
		}
		else if(Conn.m_OutPos > (1 << 20) && Conn.m_OutPos > Conn.m_vOut.size() / 2)
		{
			Conn.m_vOut.erase(Conn.m_vOut.begin(), Conn.m_vOut.begin() + Conn.m_OutPos);
			Conn.m_OutPos = 0;
		}
	}
}

void CXcCoop::HandleMap(SConn &Conn, const uint8_t *pData, size_t Size)
{
	CCoopUnpacker Msg(pData, Size);
	const uint32_t Id = Msg.U32();
	const uint32_t Epoch = Msg.U32();
	const uint32_t Base = Msg.U32();
	const int Flags = Msg.U8();
	Msg.U8(); // the author
	const uint32_t Token = Msg.U32();
	const std::string Name = Msg.Str();
	if(Msg.m_Error)
		return;

	SShared *pShared = nullptr;
	if(m_Role == ROLE_HOST)
	{
		if(Flags & MAPFLAG_NEW)
		{
			// a guest opened a new tab: it gets an id now, the map loads when the mouse is free
			auto pNew = std::make_unique<SShared>();
			pNew->m_Id = m_NextMapId++;
			pNew->m_Token = Token;
			pNew->m_Epoch = 1;
			pShared = pNew.get();
			m_vpShared.push_back(std::move(pNew));
		}
		else
			pShared = FindShared(Id);
		if(!pShared)
			return;
		pShared->m_pIncomingFrom = &Conn;
		pShared->m_IncomingBase = Base;
		pShared->m_IncomingAnswer = false;
		pShared->m_IncomingReplace = false;
	}
	else
	{
		pShared = FindShared(Id);
		if(!pShared)
		{
			// a tab the room has and this player not yet
			auto pNew = std::make_unique<SShared>();
			pNew->m_Id = Id;
			pShared = pNew.get();
			m_vpShared.push_back(std::move(pNew));
		}
		pShared->m_IncomingEpoch = Epoch;
		pShared->m_IncomingAnswer = (Flags & MAPFLAG_ANSWER) != 0;
		pShared->m_IncomingReplace = (Flags & MAPFLAG_REPLACE) != 0;
		m_Progress = -1.0f;
	}
	pShared->m_IncomingName = Name;
	pShared->m_vIncoming.assign(pData + Msg.m_Pos, pData + Size);
	pShared->m_HasIncoming = true;
}

void CXcCoop::Handle(SConn &Conn, int Type, const uint8_t *pData, size_t Size)
{
	CCoopUnpacker Msg(pData, Size);
	if(m_Role == ROLE_HOST)
	{
		if(Type == MSG_HELLO)
		{
			const uint8_t *pMagic = Msg.Raw(4);
			const int Version = Msg.U16();
			const std::string Name = Msg.Str();
			if(Conn.m_Hello)
				return;
			const char *pRefuse = nullptr;
			if(!pMagic || std::memcmp(pMagic, "EXCO", 4) != 0)
				pRefuse = "This is not an Exora Client room";
			else if(Version != PROTOCOL_VERSION)
				pRefuse = "The room has another version of Exora Client. Update both clients";
			else if(m_vMembers.size() >= MAX_MEMBERS)
				pRefuse = "The room is full";
			if(pRefuse)
			{
				CCoopPacker Bye;
				Bye.Str(pRefuse);
				Queue(Conn, MSG_BYE, Bye.m_vData.data(), Bye.m_vData.size(), false);
				Flush();
				CloseConn(Conn);
				return;
			}
			int Id = 1;
			while(FindMember(Id))
				Id++;
			SMember Member;
			Member.m_Id = Id;
			Member.m_Name = Name.empty() ? "Player" : Name;
			m_vMembers.push_back(Member);
			Conn.m_MemberId = Id;
			Conn.m_Hello = true;
			int NumMaps = 0;
			for(const auto &pShared : m_vpShared)
				NumMaps += pShared->m_pMap && pShared->m_Id != 0 ? 1 : 0;
			CCoopPacker Welcome;
			Welcome.U8(Id);
			Welcome.U16(NumMaps);
			Queue(Conn, MSG_WELCOME, Welcome.m_vData.data(), Welcome.m_vData.size(), false);
			SendMembers();
			// the blocks drawn so far go to the others first, so the map files and the diffs agree
			for(auto &pShared : m_vpShared)
			{
				if(!pShared->m_pMap || pShared->m_Id == 0)
					continue;
				if(!SendingBlocked(*pShared))
				{
					std::vector<uint8_t> vDiff;
					if(DiffTiles(*pShared, vDiff))
					{
						CCoopPacker Tiles;
						Tiles.U32(pShared->m_Id);
						Tiles.U32(pShared->m_Epoch);
						Tiles.U8(m_SelfId);
						Tiles.Raw(vDiff.data(), vDiff.size());
						Broadcast(MSG_TILES, Tiles.m_vData, &Conn);
					}
				}
				if(StartSave(*pShared, SAVE_TARGET, Conn.m_Sock))
					Conn.m_AwaitMaps++;
			}
			log_info("xc_coop", "'%s' joined, %d maps", Member.m_Name.c_str(), NumMaps);
			return;
		}
		if(!Conn.m_Hello)
			return;
		if(Type == MSG_MAP)
		{
			HandleMap(Conn, pData, Size);
			return;
		}
		if(Type == MSG_TILES)
		{
			const uint32_t Id = Msg.U32();
			const uint32_t Epoch = Msg.U32();
			Msg.U8();
			SShared *pShared = FindShared(Id);
			// blocks for an older map are dropped; the check later brings this guest back in sync
			if(Msg.m_Error || !pShared || !pShared->m_pMap || Epoch != pShared->m_Epoch || pShared->m_HasIncoming)
				return;
			ApplyTiles(*pShared, pData + Msg.m_Pos, Msg.Left(), true);
			// to the others, as from this guest
			std::vector<uint8_t> vRelay(pData, pData + Size);
			vRelay[8] = (uint8_t)Conn.m_MemberId;
			Broadcast(MSG_TILES, vRelay, &Conn);
			return;
		}
		if(Type == MSG_CURSOR || Type == MSG_TEE)
		{
			if(Size < 1)
				return;
			std::vector<uint8_t> vRelay(pData, pData + Size);
			vRelay[0] = (uint8_t)Conn.m_MemberId;
			Handle(Conn, -Type, vRelay.data(), vRelay.size());
			Broadcast(Type, vRelay, &Conn);
			return;
		}
		if(Type == MSG_CLOSE)
		{
			const uint32_t Id = Msg.U32();
			if(Msg.m_Error || !FindShared(Id))
				return;
			CCoopPacker Close;
			Close.U32(Id);
			Close.U8(Conn.m_MemberId);
			Broadcast(MSG_CLOSE, Close.m_vData, &Conn);
			CloseSharedTab(Id, Conn.m_MemberId);
			return;
		}
		if(Type == MSG_PLAY_REQ)
		{
			const uint32_t Id = Msg.U32();
			if(!Msg.m_Error)
				StartPlay(Id);
			return;
		}
		if(Type == MSG_RESYNC)
		{
			SShared *pShared = FindShared(Msg.U32());
			if(!Msg.m_Error && pShared && pShared->m_pMap)
			{
				log_info("xc_coop", "map %u is out of sync for member %d, sending it again", pShared->m_Id, Conn.m_MemberId);
				StartSave(*pShared, SAVE_TARGET, Conn.m_Sock, true);
			}
			return;
		}
		if(Type == -MSG_CURSOR || Type == -MSG_TEE)
			Type = -Type; // the host shows the cursor and the tee too
		else if(Type == MSG_BYE)
		{
			CloseConn(Conn);
			return;
		}
		else
			return;
	}

	// a guest, from the host (and the cursors of everyone, the host too)
	if(Type == MSG_WELCOME)
	{
		m_SelfId = Msg.U8();
		const int NumMaps = Msg.U16();
		if(NumMaps == 0)
			m_State = STATE_ONLINE;
	}
	else if(Type == MSG_MEMBERS)
	{
		std::vector<SMember> vMembers;
		const int Count = Msg.U8();
		for(int i = 0; i < Count && !Msg.m_Error; i++)
		{
			SMember Member;
			Member.m_Id = Msg.U8();
			const std::string Name = Msg.Str();
			// the cursor stays where it was
			if(const SMember *pOld = FindMember(Member.m_Id))
				Member = *pOld;
			Member.m_Name = Name;
			vMembers.push_back(Member);
		}
		if(!Msg.m_Error)
			m_vMembers = vMembers;
	}
	else if(Type == MSG_MAP)
		HandleMap(Conn, pData, Size);
	else if(Type == MSG_EPOCH)
	{
		SShared *pShared = FindShared(Msg.U32());
		const uint32_t Epoch = Msg.U32();
		if(!Msg.m_Error && pShared)
		{
			pShared->m_Epoch = Epoch;
			pShared->m_ProposalPending = false;
		}
	}
	else if(Type == MSG_ASSIGN)
	{
		const uint32_t Token = Msg.U32();
		const uint32_t Id = Msg.U32();
		const uint32_t Epoch = Msg.U32();
		SShared *pShared = FindToken(Token);
		if(!Msg.m_Error && pShared)
		{
			pShared->m_Id = Id;
			pShared->m_Epoch = Epoch;
			pShared->m_ProposalPending = false;
		}
	}
	else if(Type == MSG_TILES)
	{
		SShared *pShared = FindShared(Msg.U32());
		const uint32_t Epoch = Msg.U32();
		Msg.U8();
		if(!Msg.m_Error && pShared && pShared->m_pMap && Epoch == pShared->m_Epoch && !pShared->m_ProposalPending && !pShared->m_HasIncoming)
			ApplyTiles(*pShared, pData + Msg.m_Pos, Msg.Left(), true);
	}
	else if(Type == MSG_CHECK)
	{
		SShared *pShared = FindShared(Msg.U32());
		const uint32_t Epoch = Msg.U32();
		const uint32_t Low = Msg.U32();
		const uint64_t HostHash = Low | ((uint64_t)Msg.U32() << 32);
		if(Msg.m_Error || !pShared || !pShared->m_pMap)
			return;
		// only a copy at rest can be compared: own changes on the way would differ anyway
		if(Epoch != pShared->m_Epoch || SendingBlocked(*pShared) || StateHash(pShared->m_pMap) != pShared->m_SyncedHash || TilesDirty(*pShared) || Now() < pShared->m_NextVerify)
			return;
		if(FullHash(*pShared) == HostHash)
		{
			pShared->m_Mismatches = 0;
			return;
		}
		if(++pShared->m_Mismatches >= 2)
		{
			log_info("xc_coop", "map %u is out of sync, asking the host for it", pShared->m_Id);
			pShared->m_Mismatches = -3; // give the map time to come
			CCoopPacker Resync;
			Resync.U32(pShared->m_Id);
			Broadcast(MSG_RESYNC, Resync.m_vData);
		}
	}
	else if(Type == MSG_PLAY)
	{
		const int Port = Msg.U16();
		if(Msg.Left() >= 4)
			m_PlayingMapId = Msg.U32();
		if(!Msg.m_Error && Port > 0)
			JoinPlay(Port);
	}
	else if(Type == MSG_TEE)
	{
		const int Id = Msg.U8();
		const bool Visible = Msg.U8() != 0;
		const float x = Msg.F32();
		const float y = Msg.F32();
		const float Angle = Msg.F32();
		const uint32_t MapId = Msg.U32();
		const std::string Skin = Msg.Str();
		const int Body = (int)Msg.U32();
		const int Feet = (int)Msg.U32();
		const bool Custom = Msg.U8() != 0;
		SMember *pMember = FindMember(Id);
		if(Msg.m_Error || !pMember || Id == m_SelfId)
			return;
		// a tee that shows up jumps to its place, a moving one glides there
		if(!pMember->m_HasTee || distance(pMember->m_TeeDrawPos, vec2(x, y)) > 600.0f)
			pMember->m_TeeDrawPos = vec2(x, y);
		pMember->m_HasTee = Visible;
		pMember->m_TeeMapId = MapId;
		pMember->m_TeePos = vec2(x, y);
		pMember->m_TeeAngle = Angle;
		pMember->m_Skin = Skin;
		pMember->m_ColorBody = Body;
		pMember->m_ColorFeet = Feet;
		pMember->m_CustomColor = Custom;
		pMember->m_TeeSeen = Now();
	}
	else if(Type == MSG_CLOSE)
	{
		const uint32_t Id = Msg.U32();
		const int By = Msg.U8();
		if(!Msg.m_Error)
			CloseSharedTab(Id, By);
	}
	else if(Type == MSG_CURSOR)
	{
		const int Id = Msg.U8();
		const bool Visible = Msg.U8() != 0;
		const float x = Msg.F32();
		const float y = Msg.F32();
		const int Group = (int16_t)Msg.U16();
		const int Layer = (int16_t)Msg.U16();
		const uint32_t MapId = Msg.U32();
		SMember *pMember = FindMember(Id);
		if(Msg.m_Error || !pMember || Id == m_SelfId)
			return;
		pMember->m_HasCursor = Visible;
		pMember->m_Cursor = vec2(x, y);
		pMember->m_Group = Group;
		pMember->m_Layer = Layer;
		pMember->m_MapId = MapId;
		pMember->m_LastSeen = Now();
	}
	else if(Type == MSG_BYE)
	{
		const std::string Reason = Msg.Str();
		Leave(Reason == "left" ? "The host closed the room" : Reason.c_str());
	}
}

void CXcCoop::SendCursor()
{
	CEditor &E = *m_pEditor;
	const float Time = Now();
	if(Time < m_NextCursor)
		return;
	const CEditorMap *pMap = E.Map();
	const uint32_t Id = MapId(pMap);
	const bool Visible = Id != 0;
	const vec2 Pos = Visible ? pMap->m_MapViewState.m_MouseWorldNoParaPos : vec2(-1.0f, -1.0f);
	const int Group = pMap->m_SelectedGroup;
	const int Layer = pMap->m_vSelectedLayers.empty() ? -1 : pMap->m_vSelectedLayers[0];
	if(distance(Pos, m_LastCursor) < 0.5f && Group == m_LastCursorGroup && Layer == m_LastCursorLayer && Id == m_LastCursorMap)
		return;
	m_NextCursor = Time + 0.05f;
	m_LastCursor = Pos;
	m_LastCursorGroup = Group;
	m_LastCursorLayer = Layer;
	m_LastCursorMap = Id;
	CCoopPacker Cursor;
	Cursor.U8(m_SelfId < 0 ? 0 : m_SelfId);
	Cursor.U8(Visible ? 1 : 0);
	Cursor.F32(Pos.x);
	Cursor.F32(Pos.y);
	Cursor.U16(Group & 0xffff);
	Cursor.U16(Layer & 0xffff);
	Cursor.U32(Id);
	Broadcast(MSG_CURSOR, Cursor.m_vData);
}

// playing together: where our tee is, for the editors of the others
bool CXcCoop::PlayingNow() const
{
	CEditor &E = *m_pEditor;
	return m_PlayingMapId != 0 && !g_Config.m_ClEditor && E.Client()->State() == IClient::STATE_ONLINE &&
	       str_comp(E.Client()->ServerInfo().m_aMap, PLAY_MAP) == 0;
}

void CXcCoop::SendTee()
{
	CEditor &E = *m_pEditor;
	const float Time = Now();
	if(Time < m_NextTee)
		return;
	const CGameClient *pGameClient = (const CGameClient *)E.Kernel()->RequestInterface<IGameClient>();
	const bool Visible = PlayingNow() && pGameClient->m_Snap.m_pLocalCharacter;
	if(!Visible && !m_TeeShown)
		return;
	const vec2 Pos = Visible ? pGameClient->m_LocalCharacterPos : vec2(0.0f, 0.0f);
	// still: once a second, so a late editor sees it too
	if(Visible == m_TeeShown && distance(Pos, m_LastTee) < 0.5f && Time - m_LastTeeSent < 1.0f)
		return;
	m_NextTee = Time + 0.05f;
	m_LastTee = Pos;
	m_LastTeeSent = Time;
	m_TeeShown = Visible;
	const vec2 Aim = pGameClient->m_Controls.m_aMousePos[g_Config.m_ClDummy];
	CCoopPacker Tee;
	Tee.U8(m_SelfId < 0 ? 0 : m_SelfId);
	Tee.U8(Visible ? 1 : 0);
	Tee.F32(Pos.x);
	Tee.F32(Pos.y);
	Tee.F32(std::atan2(Aim.y, Aim.x));
	Tee.U32(m_PlayingMapId);
	Tee.Str(g_Config.m_ClPlayerSkin);
	Tee.U32((uint32_t)g_Config.m_ClPlayerColorBody);
	Tee.U32((uint32_t)g_Config.m_ClPlayerColorFeet);
	Tee.U8(g_Config.m_ClPlayerUseCustomColor ? 1 : 0);
	Broadcast(MSG_TEE, Tee.m_vData);
}

void CXcCoop::EaseTees(float Dt)
{
	const float k = 1.0f - std::exp(-Dt * 18.0f);
	for(SMember &Member : m_vMembers)
	{
		if(!Member.m_HasTee)
			continue;
		Member.m_TeeDrawPos += (Member.m_TeePos - Member.m_TeeDrawPos) * k;
		// a member who stopped sending (left the game without a word) fades out
		if(Now() - Member.m_TeeSeen > 5.0f)
			Member.m_HasTee = false;
	}
}

// ---- playing together

void CXcCoop::RequestPlay()
{
	if(m_State != STATE_ONLINE)
		return;
	const uint32_t Id = MapId(m_pEditor->Map());
	if(Id == 0)
		return;
	if(m_Role == ROLE_HOST)
	{
		StartPlay(Id);
		return;
	}
	CCoopPacker Request;
	Request.U32(Id);
	Broadcast(MSG_PLAY_REQ, Request.m_vData);
	m_Notice = "The host starts the server, you join it in a moment";
}

void CXcCoop::StartPlay(uint32_t MapId)
{
	SShared *pShared = FindShared(MapId);
	if(!pShared || !pShared->m_pMap || m_PlayMapId != 0)
		return;
	CEditor &E = *m_pEditor;
	E.Storage()->CreateFolder("maps", IStorage::TYPE_SAVE);
	char aPath[IO_MAX_PATH_LENGTH];
	str_format(aPath, sizeof(aPath), "maps/%s.map", PLAY_MAP);
	if(E.IsSaving(aPath))
		return;
	const size_t Before = E.m_WriterFinishJobs.size();
	if(!pShared->m_pMap->Save(aPath, [&E](const char *pError) { E.ShowFileDialogError("%s", pError); }) || E.m_WriterFinishJobs.size() == Before)
		return;
	SPendingSave Save;
	Save.m_pJob = E.m_WriterFinishJobs.back();
	Save.m_Path = aPath;
	Save.m_MapId = MapId;
	Save.m_Token = 0;
	Save.m_Kind = SAVE_PLAY;
	Save.m_TargetSock = nullptr;
	Save.m_Replace = false;
	m_vSaves.push_back(Save);
	m_PlayMapId = MapId;
	log_info("xc_coop", "saving map %u to play together", MapId);
}

// the map is written: change the map of the running local server, or start one
void CXcCoop::RunPlayServer()
{
	CEditor &E = *m_pEditor;
	CGameClient *pGameClient = (CGameClient *)E.Kernel()->RequestInterface<IGameClient>();
	char aMapChange[128];
	str_format(aMapChange, sizeof(aMapChange), "change_map %s", PLAY_MAP);
	const bool OnLocal = E.Client()->State() == IClient::STATE_ONLINE && net_addr_is_local(&E.Client()->ServerAddress());
	if(OnLocal && E.Client()->RconAuthed())
	{
		// everyone on it reloads the new map by himself
		E.Client()->Rcon("sv_xc_live_edit 1");
		E.Client()->Rcon(aMapChange);
	}
	else if(pGameClient->m_LocalServer.IsServerRunning() && m_PlayRunAt < -1.5f)
	{
		// a server we cannot control: stop it and start a new one in a moment
		pGameClient->m_LocalServer.KillServer();
		m_PlayRunAt = Now() + 1.5f;
		return;
	}
	else
	{
		char aPort[32];
		str_format(aPort, sizeof(aPort), "sv_port %d", PLAY_PORT);
		if(!pGameClient->m_LocalServer.RunServer({"sv_register 0", "sv_xc_live_edit 1", aPort, aMapChange}))
		{
			m_Notice = "The local server could not be started";
			return;
		}
	}
	// from now on the blocks of the tab go to the server live
	if(SShared *pPlaying = FindShared(m_PlayingMapId); pPlaying && pPlaying->m_pMap && m_fnOnPlay)
		m_fnOnPlay(pPlaying->m_pMap, m_PlayingMapId, PLAY_MAP);
	CCoopPacker Play;
	Play.U16(PLAY_PORT);
	Play.U32(m_PlayingMapId);
	Broadcast(MSG_PLAY, Play.m_vData);
	Flush();
	log_info("xc_coop", "playing together on port %d", PLAY_PORT);
	if(!OnLocal)
		JoinPlay(-1);
	else
	{
		E.OnClose();
		g_Config.m_ClEditor = 0;
	}
}

// Port -1: the host joins his own server
void CXcCoop::JoinPlay(int Port)
{
	CEditor &E = *m_pEditor;
	char aAddress[160];
	if(Port < 0)
		str_format(aAddress, sizeof(aAddress), "localhost:%d", PLAY_PORT);
	else
	{
		// the host's address, the server's port
		str_copy(aAddress, m_aAddress);
		char *pColon = (char *)str_rchr(aAddress, ':');
		if(pColon)
			*pColon = '\0';
		str_format(aAddress + str_length(aAddress), sizeof(aAddress) - str_length(aAddress), ":%d", Port);
		// already there: the server changes the map by itself
		char aCurrent[NETADDR_MAXSTRSIZE] = "";
		if(E.Client()->State() == IClient::STATE_ONLINE)
			net_addr_str(&E.Client()->ServerAddress(), aCurrent, sizeof(aCurrent), true);
		if(aCurrent[0] && str_comp(aCurrent, aAddress) == 0)
		{
			E.OnClose();
			g_Config.m_ClEditor = 0;
			return;
		}
	}
	E.OnClose();
	g_Config.m_ClEditor = 0;
	E.Client()->Connect(aAddress);
}

// ---- map state

uint64_t CXcCoop::StateHash(const CEditorMap *pMap) const
{
	uint64_t h = 14695981039346656037ull;
	h = HashStr(h, pMap->m_MapInfo.m_aAuthor);
	h = HashStr(h, pMap->m_MapInfo.m_aVersion);
	h = HashStr(h, pMap->m_MapInfo.m_aCredits);
	h = HashStr(h, pMap->m_MapInfo.m_aLicense);
	h = HashInt(h, (int)pMap->m_vSettings.size());
	for(const auto &Setting : pMap->m_vSettings)
		h = HashStr(h, Setting.m_aCommand);
	h = HashInt(h, (int)pMap->m_vpImages.size());
	for(const auto &pImage : pMap->m_vpImages)
	{
		h = HashStr(h, pImage->m_aName);
		h = HashInt(h, pImage->m_External);
		h = HashInt(h, (int)pImage->m_Width);
		h = HashInt(h, (int)pImage->m_Height);
		// a few pixels, enough to notice a color correction
		if(pImage->m_pData)
		{
			const size_t Size = pImage->DataSize();
			for(size_t i = 0; i < Size; i += 997)
				h = Hash(h, pImage->m_pData + i, 1);
		}
	}
	h = HashInt(h, (int)pMap->m_vpSounds.size());
	for(const auto &pSound : pMap->m_vpSounds)
	{
		h = HashStr(h, pSound->m_aName);
		h = HashInt(h, (int)pSound->m_DataSize);
	}
	h = HashInt(h, (int)pMap->m_vpEnvelopes.size());
	for(const auto &pEnvelope : pMap->m_vpEnvelopes)
	{
		h = HashInt(h, (int)pEnvelope->Type());
		h = HashStr(h, pEnvelope->m_aName);
		h = HashInt(h, pEnvelope->m_Synchronized);
		if(!pEnvelope->m_vPoints.empty())
			h = Hash(h, pEnvelope->m_vPoints.data(), pEnvelope->m_vPoints.size() * sizeof(pEnvelope->m_vPoints[0]));
	}
	h = HashInt(h, (int)pMap->m_vpGroups.size());
	for(const auto &pGroup : pMap->m_vpGroups)
	{
		const int aGroup[] = {pGroup->m_OffsetX, pGroup->m_OffsetY, pGroup->m_ParallaxX, pGroup->m_ParallaxY, pGroup->m_UseClipping, pGroup->m_ClipX, pGroup->m_ClipY, pGroup->m_ClipW, pGroup->m_ClipH, pGroup->m_GameGroup, (int)pGroup->m_vpLayers.size()};
		h = Hash(h, aGroup, sizeof(aGroup));
		h = HashStr(h, pGroup->m_aName);
		for(const auto &pLayer : pGroup->m_vpLayers)
		{
			h = HashInt(h, pLayer->m_Type);
			h = HashInt(h, pLayer->m_Flags);
			h = HashStr(h, pLayer->m_aName);
			if(pLayer->m_Type == LAYERTYPE_TILES)
			{
				const CLayerTiles *pTiles = static_cast<const CLayerTiles *>(pLayer.get());
				const int aTiles[] = {pTiles->m_Width, pTiles->m_Height, pTiles->m_Image, pTiles->m_Color.r, pTiles->m_Color.g, pTiles->m_Color.b, pTiles->m_Color.a, pTiles->m_ColorEnv, pTiles->m_ColorEnvOffset, pTiles->m_AutomapperConfig, pTiles->m_AutomapperReference, pTiles->m_Seed, pTiles->m_AutoAutomapper, pTiles->m_HasGame, pTiles->m_HasTele, pTiles->m_HasSpeedup, pTiles->m_HasFront, pTiles->m_HasSwitch, pTiles->m_HasTune};
				h = Hash(h, aTiles, sizeof(aTiles));
			}
			else if(pLayer->m_Type == LAYERTYPE_QUADS)
			{
				const CLayerQuads *pQuads = static_cast<const CLayerQuads *>(pLayer.get());
				h = HashInt(h, pQuads->m_Image);
				h = HashInt(h, (int)pQuads->m_vQuads.size());
				if(!pQuads->m_vQuads.empty())
					h = Hash(h, pQuads->m_vQuads.data(), pQuads->m_vQuads.size() * sizeof(CQuad));
			}
			else if(pLayer->m_Type == LAYERTYPE_SOUNDS)
			{
				const CLayerSounds *pSounds = static_cast<const CLayerSounds *>(pLayer.get());
				h = HashInt(h, pSounds->m_Sound);
				h = HashInt(h, (int)pSounds->m_vSources.size());
				if(!pSounds->m_vSources.empty())
					h = Hash(h, pSounds->m_vSources.data(), pSounds->m_vSources.size() * sizeof(CSoundSource));
			}
		}
	}
	return h;
}

// everything, the blocks too: what the host and the guests compare. Both only ask when the snapshot holds the
// blocks as they are, so the blocks are hashed from it, and a layer only again when its blocks changed
uint64_t CXcCoop::FullHash(SShared &Shared) const
{
	uint64_t h = StateHash(Shared.m_pMap);
	for(STileSnap &Snap : Shared.m_vSnaps)
	{
		if(!Snap.m_HashValid)
		{
			uint64_t LayerHash = HashBlock(14695981039346656037ull, Snap.m_vTiles.data(), Snap.m_vTiles.size());
			if(!Snap.m_vExtra.empty())
				LayerHash = HashBlock(LayerHash, Snap.m_vExtra.data(), Snap.m_vExtra.size());
			Snap.m_Hash = LayerHash;
			Snap.m_HashValid = true;
		}
		const int aKey[] = {Snap.m_Group, Snap.m_Layer, Snap.m_Width, Snap.m_Height, Snap.m_CellSize};
		h = Hash(h, aKey, sizeof(aKey));
		h = Hash(h, &Snap.m_Hash, sizeof(Snap.m_Hash));
	}
	return h;
}

void CXcCoop::TakeSnapshot(SShared &Shared)
{
	Shared.m_vSnaps.clear();
	CEditorMap *pMap = Shared.m_pMap;
	if(!pMap)
		return;
	for(int g = 0; g < (int)pMap->m_vpGroups.size(); g++)
	{
		const auto &vpLayers = pMap->m_vpGroups[g]->m_vpLayers;
		for(int l = 0; l < (int)vpLayers.size(); l++)
		{
			CLayerTiles *pLayer = TileLayer(pMap, g, l);
			if(!pLayer || !pLayer->m_pTiles)
				continue;
			STileSnap Snap;
			Snap.m_Group = g;
			Snap.m_Layer = l;
			Snap.m_Width = pLayer->m_Width;
			Snap.m_Height = pLayer->m_Height;
			const int Extra = ExtraSize(pLayer);
			Snap.m_CellSize = sizeof(CTile) + Extra;
			const size_t Cells = (size_t)pLayer->m_Width * pLayer->m_Height;
			const uint8_t *pTiles = (const uint8_t *)pLayer->m_pTiles;
			Snap.m_vTiles.assign(pTiles, pTiles + Cells * sizeof(CTile));
			if(Extra)
			{
				const uint8_t *pExtra = ExtraData(pLayer);
				if(!pExtra)
					continue;
				Snap.m_vExtra.assign(pExtra, pExtra + Cells * Extra);
			}
			Shared.m_vSnaps.push_back(std::move(Snap));
		}
	}
}

// are there own blocks not sent yet
bool CXcCoop::TilesDirty(const SShared &Shared) const
{
	for(const STileSnap &Snap : Shared.m_vSnaps)
	{
		CLayerTiles *pLayer = TileLayer(Shared.m_pMap, Snap.m_Group, Snap.m_Layer);
		if(!pLayer || !pLayer->m_pTiles || pLayer->m_Width != Snap.m_Width || pLayer->m_Height != Snap.m_Height)
			return true;
		if(std::memcmp(pLayer->m_pTiles, Snap.m_vTiles.data(), Snap.m_vTiles.size()) != 0)
			return true;
		if(!Snap.m_vExtra.empty())
		{
			const uint8_t *pExtra = ExtraData(pLayer);
			if(!pExtra || std::memcmp(pExtra, Snap.m_vExtra.data(), Snap.m_vExtra.size()) != 0)
				return true;
		}
	}
	return false;
}

// the changed blocks since the snapshot: [u16 group][u16 layer][u16 cell size][u32 runs] runs: [u32 start][u32 count][cells]
bool CXcCoop::DiffTiles(SShared &Shared, std::vector<uint8_t> &vOut)
{
	CCoopPacker Out;
	for(STileSnap &Snap : Shared.m_vSnaps)
	{
		CLayerTiles *pLayer = TileLayer(Shared.m_pMap, Snap.m_Group, Snap.m_Layer);
		if(!pLayer || !pLayer->m_pTiles || pLayer->m_Width != Snap.m_Width || pLayer->m_Height != Snap.m_Height || (int)sizeof(CTile) + ExtraSize(pLayer) != Snap.m_CellSize)
			continue;
		const int Extra = Snap.m_CellSize - (int)sizeof(CTile);
		uint8_t *pTiles = (uint8_t *)pLayer->m_pTiles;
		uint8_t *pExtra = Extra ? ExtraData(pLayer) : nullptr;
		if(Extra && !pExtra)
			continue;
		const size_t RowTiles = (size_t)Snap.m_Width * sizeof(CTile);
		const size_t RowExtra = (size_t)Snap.m_Width * Extra;
		size_t HeaderPos = 0;
		uint32_t Runs = 0;
		for(int y = 0; y < Snap.m_Height; y++)
		{
			const bool SameTiles = std::memcmp(pTiles + y * RowTiles, Snap.m_vTiles.data() + y * RowTiles, RowTiles) == 0;
			const bool SameExtra = !Extra || std::memcmp(pExtra + y * RowExtra, Snap.m_vExtra.data() + y * RowExtra, RowExtra) == 0;
			if(SameTiles && SameExtra)
				continue;
			int x = 0;
			while(x < Snap.m_Width)
			{
				auto Changed = [&](int cx) {
					const size_t i = (size_t)y * Snap.m_Width + cx;
					return std::memcmp(pTiles + i * sizeof(CTile), Snap.m_vTiles.data() + i * sizeof(CTile), sizeof(CTile)) != 0 ||
					       (Extra && std::memcmp(pExtra + i * Extra, Snap.m_vExtra.data() + i * Extra, Extra) != 0);
				};
				if(!Changed(x))
				{
					x++;
					continue;
				}
				int End = x + 1;
				while(End < Snap.m_Width && Changed(End))
					End++;
				if(Runs == 0)
				{
					Out.U16(Snap.m_Group);
					Out.U16(Snap.m_Layer);
					Out.U16(Snap.m_CellSize);
					HeaderPos = Out.m_vData.size();
					Out.U32(0);
				}
				Runs++;
				Snap.m_HashValid = false;
				const size_t Start = (size_t)y * Snap.m_Width + x;
				Out.U32((uint32_t)Start);
				Out.U32((uint32_t)(End - x));
				for(size_t i = Start; i < Start + (End - x); i++)
				{
					Out.Raw(pTiles + i * sizeof(CTile), sizeof(CTile));
					std::memcpy(Snap.m_vTiles.data() + i * sizeof(CTile), pTiles + i * sizeof(CTile), sizeof(CTile));
					if(Extra)
					{
						Out.Raw(pExtra + i * Extra, Extra);
						std::memcpy(Snap.m_vExtra.data() + i * Extra, pExtra + i * Extra, Extra);
					}
				}
				x = End;
			}
		}
		if(Runs)
		{
			for(int i = 0; i < 4; i++)
				Out.m_vData[HeaderPos + i] = (Runs >> (i * 8)) & 0xff;
		}
	}
	vOut = std::move(Out.m_vData);
	return !vOut.empty();
}

void CXcCoop::ApplyTiles(SShared &Shared, const uint8_t *pData, size_t Size, bool UpdateSnap)
{
	CEditorMap *pMap = Shared.m_pMap;
	if(!pMap)
		return;
	CCoopUnpacker In(pData, Size);
	bool Applied = false;
	while(In.Left() > 0)
	{
		const int Group = In.U16();
		const int Layer = In.U16();
		const int CellSize = In.U16();
		const uint32_t Runs = In.U32();
		if(In.m_Error)
			break;
		CLayerTiles *pLayer = TileLayer(pMap, Group, Layer);
		const bool Fits = pLayer && pLayer->m_pTiles && (int)sizeof(CTile) + ExtraSize(pLayer) == CellSize && (ExtraSize(pLayer) == 0 || ExtraData(pLayer));
		const size_t Cells = pLayer ? (size_t)pLayer->m_Width * pLayer->m_Height : 0;
		STileSnap *pSnap = nullptr;
		if(Fits && UpdateSnap)
			for(STileSnap &Snap : Shared.m_vSnaps)
				if(Snap.m_Group == Group && Snap.m_Layer == Layer && Snap.m_Width == pLayer->m_Width && Snap.m_Height == pLayer->m_Height && Snap.m_CellSize == CellSize)
					pSnap = &Snap;
		const int Extra = CellSize - (int)sizeof(CTile);
		for(uint32_t r = 0; r < Runs && !In.m_Error; r++)
		{
			const uint32_t Start = In.U32();
			const uint32_t Count = In.U32();
			const uint8_t *pCells = In.Raw((size_t)Count * CellSize);
			if(!pCells || !Fits || (size_t)Start + Count > Cells)
				continue;
			uint8_t *pTiles = (uint8_t *)pLayer->m_pTiles;
			uint8_t *pExtra = Extra ? ExtraData(pLayer) : nullptr;
			for(uint32_t i = 0; i < Count; i++)
			{
				const size_t Cell = Start + i;
				const uint8_t *pCell = pCells + (size_t)i * CellSize;
				std::memcpy(pTiles + Cell * sizeof(CTile), pCell, sizeof(CTile));
				if(Extra)
					std::memcpy(pExtra + Cell * Extra, pCell + sizeof(CTile), Extra);
				if(pSnap)
				{
					pSnap->m_HashValid = false;
					std::memcpy(pSnap->m_vTiles.data() + Cell * sizeof(CTile), pCell, sizeof(CTile));
					if(Extra)
						std::memcpy(pSnap->m_vExtra.data() + Cell * Extra, pCell + sizeof(CTile), Extra);
				}
			}
			Applied = true;
		}
	}
	if(Applied)
		pMap->OnModify();
}

// ---- whole maps

bool CXcCoop::StartSave(SShared &Shared, int Kind, NETSOCKET TargetSock, bool Replace)
{
	CEditor &E = *m_pEditor;
	if(!Shared.m_pMap)
		return false;
	E.Storage()->CreateFolder("xclient", IStorage::TYPE_SAVE);
	E.Storage()->CreateFolder(COOP_FOLDER, IStorage::TYPE_SAVE);
	char aPath[IO_MAX_PATH_LENGTH];
	for(int Try = 0; Try < 32; Try++)
	{
		str_format(aPath, sizeof(aPath), "%s/out_%d.map", COOP_FOLDER, m_SaveCounter++ % 32);
		if(!E.IsSaving(aPath))
			break;
	}
	const size_t Before = E.m_WriterFinishJobs.size();
	const bool Saved = Shared.m_pMap->Save(aPath, [&E](const char *pError) { E.ShowFileDialogError("%s", pError); });
	if(!Saved || E.m_WriterFinishJobs.size() == Before)
		return false;
	SPendingSave Save;
	Save.m_pJob = E.m_WriterFinishJobs.back();
	Save.m_Path = aPath;
	Save.m_MapId = Shared.m_Id;
	Save.m_Token = Shared.m_Token;
	Save.m_Kind = Kind;
	Save.m_TargetSock = TargetSock;
	Save.m_Replace = Replace;
	m_vSaves.push_back(Save);
	return true;
}

void CXcCoop::FinishSaves()
{
	CEditor &E = *m_pEditor;
	// in order: an older map must not overtake a newer one
	while(!m_vSaves.empty() && m_vSaves.front().m_pJob->Done())
	{
		SPendingSave Save = m_vSaves.front();
		m_vSaves.erase(m_vSaves.begin());
		if(Save.m_Kind == SAVE_PLAY)
		{
			m_PlayingMapId = m_PlayMapId;
			m_PlayMapId = 0;
			if(Save.m_pJob->ErrorMessage()[0] == '\0')
			{
				m_PlayRunAt = -2.0f; // a running server may be stopped once
				RunPlayServer();
				if(m_PlayRunAt < -1.5f)
					m_PlayRunAt = -1.0f;
			}
			continue;
		}
		SShared *pShared = Save.m_Kind == SAVE_NEW ? FindToken(Save.m_Token) : FindShared(Save.m_MapId);
		if(!pShared && Save.m_Kind == SAVE_NEW)
			pShared = FindShared(Save.m_MapId);
		void *pFile = nullptr;
		unsigned FileSize = 0;
		const bool Read = Save.m_pJob->ErrorMessage()[0] == '\0' && E.Storage()->ReadFile(Save.m_Path.c_str(), IStorage::TYPE_SAVE, &pFile, &FileSize);
		E.Storage()->RemoveFile(Save.m_Path.c_str(), IStorage::TYPE_SAVE);
		if(!Read || !pShared)
		{
			free(pFile);
			if(pShared && Save.m_Kind != SAVE_TARGET && m_Role == ROLE_GUEST)
				pShared->m_ProposalPending = false;
			if(Save.m_Kind == SAVE_TARGET)
			{
				// the guest must not wait for a map that never comes
				for(auto &pConn : m_vpConns)
				{
					if(pConn->m_Sock != Save.m_TargetSock || pConn->m_AwaitMaps <= 0 || Save.m_Replace)
						continue;
					if(--pConn->m_AwaitMaps == 0)
					{
						pConn->m_vOut.insert(pConn->m_vOut.end(), pConn->m_vHeld.begin(), pConn->m_vHeld.end());
						pConn->m_vHeld.clear();
					}
				}
			}
			continue;
		}

		if(Save.m_Kind == SAVE_CHANGE && m_Role == ROLE_HOST)
			pShared->m_Epoch++;
		int Flags = 0;
		if(Save.m_Kind == SAVE_NEW)
			Flags |= MAPFLAG_NEW;
		if(Save.m_Replace)
			Flags |= MAPFLAG_REPLACE;
		CCoopPacker Map;
		Map.U32(pShared->m_Id);
		Map.U32(m_Role == ROLE_HOST ? pShared->m_Epoch : 0);
		Map.U32(pShared->m_Epoch); // the base the change was made on
		Map.U8(Flags);
		Map.U8(m_SelfId < 0 ? 0 : m_SelfId);
		Map.U32(pShared->m_Token);
		Map.Str(pShared->m_pMap ? pShared->m_pMap->m_aDisplayName : "");
		Map.Raw(pFile, FileSize);
		free(pFile);

		if(Save.m_Kind != SAVE_TARGET)
		{
			// a change or a new tab: the host gives it to everyone, a guest gives it to the host
			Broadcast(MSG_MAP, Map.m_vData);
			continue;
		}
		for(auto &pConn : m_vpConns)
		{
			if(pConn->m_Sock != Save.m_TargetSock || pConn->m_Dead)
				continue;
			// the maps of a new guest go first, then what happened while they were written
			Queue(*pConn, MSG_MAP, Map.m_vData.data(), Map.m_vData.size(), false);
			if(!Save.m_Replace && pConn->m_AwaitMaps > 0 && --pConn->m_AwaitMaps == 0)
			{
				pConn->m_vOut.insert(pConn->m_vOut.end(), pConn->m_vHeld.begin(), pConn->m_vHeld.end());
				pConn->m_vHeld.clear();
			}
		}
	}
}

void CXcCoop::LoadIncomingMap(SShared &Shared)
{
	CEditor &E = *m_pEditor;
	Shared.m_HasIncoming = false;
	std::vector<uint8_t> vData;
	std::swap(vData, Shared.m_vIncoming);

	// the blocks drawn here and not sent yet go onto the new map again (not when the host says it is out of sync)
	std::vector<uint8_t> vLocal;
	if(Shared.m_pMap && !Shared.m_IncomingReplace && StateHash(Shared.m_pMap) == Shared.m_SyncedHash)
		DiffTiles(Shared, vLocal);

	E.Storage()->CreateFolder("xclient", IStorage::TYPE_SAVE);
	E.Storage()->CreateFolder(COOP_FOLDER, IStorage::TYPE_SAVE);
	char aPath[IO_MAX_PATH_LENGTH];
	str_format(aPath, sizeof(aPath), "%s/in.map", COOP_FOLDER);
	IOHANDLE File = E.Storage()->OpenFile(aPath, IOFLAG_WRITE, IStorage::TYPE_SAVE);
	if(!File)
	{
		Leave("Could not write the map of the room");
		return;
	}
	io_write(File, vData.data(), vData.size());
	io_close(File);

	std::unique_ptr<CEditorMap> pNew = std::make_unique<CEditorMap>(&E);
	std::string Error;
	const bool Loaded = pNew->Load(aPath, IStorage::TYPE_SAVE, [&Error](const char *pError) { Error = pError; });
	E.Storage()->RemoveFile(aPath, IStorage::TYPE_SAVE);
	if(!Loaded)
	{
		log_error("xc_coop", "could not load the map: %s", Error.c_str());
		if(m_Role == ROLE_GUEST)
		{
			Leave("A map of the room could not be opened");
			return;
		}
		// the host keeps his map and sends it to everyone instead
		if(Shared.m_pMap)
			Shared.m_SyncedHash = 0;
		else
			m_vpShared.erase(std::remove_if(m_vpShared.begin(), m_vpShared.end(), [&](const auto &p) { return p.get() == &Shared; }), m_vpShared.end());
		return;
	}

	const int Index = MapIndex(Shared.m_pMap);
	if(Index < 0)
	{
		// a new tab from the room; the first one of a joining guest is opened
		char aName[IO_MAX_PATH_LENGTH];
		str_format(aName, sizeof(aName), "maps/%s.map", Shared.m_IncomingName.empty() ? "coop" : Shared.m_IncomingName.c_str());
		str_copy(pNew->m_aFilename, aName);
		pNew->m_ValidSaveFilename = false;
		const bool First = m_Role == ROLE_GUEST && m_State == STATE_JOINING;
		E.m_vpMaps.push_back(std::move(pNew));
		Shared.m_pMap = E.m_vpMaps.back().get();
		if(First)
		{
			E.m_SelectedMap = E.m_vpMaps.size() - 1;
			E.m_MapTabsRevealSelected = true;
			E.Reset();
		}
	}
	else
	{
		// the same tab, the same view, the same selection
		CEditorMap *pOld = E.m_vpMaps[Index].get();
		str_copy(pNew->m_aFilename, pOld->m_aFilename);
		pNew->m_ValidSaveFilename = pOld->m_ValidSaveFilename;
		pNew->m_MapViewState = pOld->m_MapViewState;
		pNew->m_MapViewState.m_ActiveOp = CMapView::EActiveOp::NONE;
		if(pOld->m_SelectedGroup >= 0 && pOld->m_SelectedGroup < (int)pNew->m_vpGroups.size())
		{
			pNew->m_SelectedGroup = pOld->m_SelectedGroup;
			pNew->m_vSelectedLayers.clear();
			const int NumLayers = pNew->m_vpGroups[pNew->m_SelectedGroup]->m_vpLayers.size();
			for(int Layer : pOld->m_vSelectedLayers)
				if(Layer >= 0 && Layer < NumLayers)
					pNew->m_vSelectedLayers.push_back(Layer);
			if(pNew->m_vSelectedLayers.empty() && NumLayers > 0)
				pNew->m_vSelectedLayers.push_back(0);
		}
		pNew->m_SelectedEnvelope = std::clamp(pOld->m_SelectedEnvelope, 0, std::max(0, (int)pNew->m_vpEnvelopes.size() - 1));
		pNew->m_Modified = true;
		if(Index == (int)E.m_SelectedMap)
		{
			E.Ui()->ClosePopupMenus();
			E.Ui()->SetActiveItem(nullptr);
		}
		E.m_vpMaps[Index] = std::move(pNew);
		Shared.m_pMap = E.m_vpMaps[Index].get();
	}
	E.UpdateMapDisplayNames();
	if(E.Map() == Shared.m_pMap)
		for(CEditorComponent &Component : E.m_vComponents)
			Component.OnMapLoad();
	m_MapReplaced = true;

	if(m_Role == ROLE_HOST)
	{
		SConn *pFrom = Shared.m_pIncomingFrom;
		Shared.m_pIncomingFrom = nullptr;
		if(Index < 0)
		{
			// a guest's new tab: its id to him, the tab to the others
			if(pFrom)
			{
				CCoopPacker Assign;
				Assign.U32(Shared.m_Token);
				Assign.U32(Shared.m_Id);
				Assign.U32(Shared.m_Epoch);
				Queue(*pFrom, MSG_ASSIGN, Assign.m_vData.data(), Assign.m_vData.size());
			}
			Shared.m_Token = 0;
			CCoopPacker Map;
			Map.U32(Shared.m_Id);
			Map.U32(Shared.m_Epoch);
			Map.U32(Shared.m_Epoch);
			Map.U8(0);
			Map.U8(pFrom ? pFrom->m_MemberId : 0);
			Map.U32(0);
			Map.Str(Shared.m_pMap->m_aDisplayName);
			Map.Raw(vData.data(), vData.size());
			Broadcast(MSG_MAP, Map.m_vData, pFrom);
		}
		else
		{
			// a change of a guest: everyone gets the new map, its author only the new number (or the map,
			// if the host changed something in the meantime)
			const uint32_t OldEpoch = Shared.m_Epoch;
			Shared.m_Epoch++;
			CCoopPacker Map;
			Map.U32(Shared.m_Id);
			Map.U32(Shared.m_Epoch);
			Map.U32(Shared.m_Epoch);
			Map.U8(0);
			Map.U8(pFrom ? pFrom->m_MemberId : 0);
			Map.U32(0);
			Map.Str(Shared.m_pMap->m_aDisplayName);
			Map.Raw(vData.data(), vData.size());
			const bool Fresh = pFrom && Shared.m_IncomingBase == OldEpoch;
			for(auto &pConn : m_vpConns)
			{
				if(!pConn->m_Hello || pConn->m_Dead)
					continue;
				if(pConn.get() == pFrom && Fresh)
				{
					CCoopPacker Epoch;
					Epoch.U32(Shared.m_Id);
					Epoch.U32(Shared.m_Epoch);
					Queue(*pConn, MSG_EPOCH, Epoch.m_vData.data(), Epoch.m_vData.size());
					continue;
				}
				Map.m_vData[12] = pConn.get() == pFrom ? MAPFLAG_ANSWER : 0;
				Queue(*pConn, MSG_MAP, Map.m_vData.data(), Map.m_vData.size());
			}
		}
	}
	else
	{
		Shared.m_Epoch = Shared.m_IncomingEpoch;
		if(Shared.m_IncomingAnswer || Shared.m_IncomingReplace)
			Shared.m_ProposalPending = false;
		// all the maps of the room are here
		if(m_State == STATE_JOINING)
		{
			bool AllLoaded = true;
			for(const auto &pOther : m_vpShared)
				AllLoaded &= pOther->m_pMap != nullptr && !pOther->m_HasIncoming;
			if(AllLoaded)
				m_State = STATE_ONLINE;
		}
	}

	TakeSnapshot(Shared);
	Shared.m_SyncedHash = Shared.m_LastHash = StateHash(Shared.m_pMap);
	Shared.m_Mismatches = 0;
	Shared.m_NextVerify = Now() + VERIFY_INTERVAL;
	// own blocks on top; the snapshot does not have them, so they go out with the new number
	if(!vLocal.empty())
		ApplyTiles(Shared, vLocal.data(), vLocal.size(), false);
	log_info("xc_coop", "map %u (epoch %u) loaded", Shared.m_Id, Shared.m_Epoch);
}

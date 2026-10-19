// Editing one map together (see xeditor_coop.h). A small TCP protocol of its own: [u32 size][u8 type][data].
// The host is the authority: every whole map gets a number (epoch) from him, and blocks are only applied to
// the map with the same number, so a late block never lands on a layer that moved.
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
	MSG_WELCOME,
	MSG_MEMBERS,
	MSG_MAP,
	MSG_EPOCH,
	MSG_TILES,
	MSG_CURSOR,
	MSG_BYE,
};
enum
{
	MAPFLAG_ANSWER = 1, // the map answers the change of the guest who gets it
};
constexpr int PROTOCOL_VERSION = 1;
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
	m_pMap = m_pEditor->Map();
	m_Epoch = 1;
	SMember Self;
	Self.m_Id = 0;
	Self.m_Name = OwnName();
	m_vMembers = {Self};
	ResetSync();
	log_info("xc_coop", "room open on port %d", Port);
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
	m_pMap = nullptr;
	m_SelfId = -1;
	net_addr_str(&Addr, m_aAddress, sizeof(m_aAddress), true);
	log_info("xc_coop", "connecting to %s", m_aAddress);
	m_vMembers.clear();
	ResetSync();
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
	m_pMap = nullptr;
	m_vMembers.clear();
	m_vSaves.clear();
	m_vSnaps.clear();
	m_vIncomingMap.clear();
	m_HasIncomingMap = false;
	m_pIncomingFrom = nullptr;
	m_ProposalPending = false;
	m_Progress = -1.0f;
	if(pReason)
		m_Notice = pReason;
	log_info("xc_coop", "session ended%s%s", pReason ? ": " : "", pReason ? pReason : "");
}

void CXcCoop::ResetSync()
{
	m_vSaves.clear();
	m_vSnaps.clear();
	m_vIncomingMap.clear();
	m_HasIncomingMap = false;
	m_pIncomingFrom = nullptr;
	m_ProposalPending = false;
	m_NextCheck = 0.0f;
	m_LastActivity = Now();
	m_LastCursor = vec2(-1.0f, -1.0f);
	if(m_pMap)
	{
		TakeSnapshot();
		m_SyncedHash = m_LastHash = StateHash(m_pMap);
	}
}

bool CXcCoop::MapAlive()
{
	if(!m_pMap)
		return true;
	for(const auto &pMap : m_pEditor->m_vpMaps)
		if(pMap.get() == m_pMap)
			return true;
	return false;
}

bool CXcCoop::SendingBlocked() const
{
	if(m_ProposalPending || m_HasIncomingMap || m_SyncedHash != m_LastHash)
		return true;
	for(const SPendingSave &Save : m_vSaves)
		if(Save.m_TargetSock == nullptr)
			return true;
	return false;
}

static bool MouseBusy(CEditor &E)
{
	return E.Input()->KeyIsPressed(KEY_MOUSE_1) || E.Input()->KeyIsPressed(KEY_MOUSE_2) || E.Input()->KeyIsPressed(KEY_MOUSE_3) || !E.Ui()->CheckActiveItem(nullptr);
}

void CXcCoop::Update()
{
	if(m_Role == ROLE_NONE)
		return;
	CEditor &E = *m_pEditor;
	if(!MapAlive())
	{
		Leave("The shared map tab was closed");
		return;
	}
	Poll();
	if(m_Role == ROLE_NONE)
		return;
	FinishSaves();
	if(m_HasIncomingMap && !MouseBusy(E))
		LoadIncomingMap();
	if(m_Role == ROLE_NONE)
		return;

	if(m_State == STATE_ONLINE && m_pMap)
	{
		const float Time = Now();
		if(Time >= m_NextCheck)
		{
			m_NextCheck = Time + (Time - m_LastActivity < 1.5f ? 0.1f : 0.5f);
			const uint64_t Hash = StateHash(m_pMap);
			if(Hash != m_LastHash)
			{
				m_LastHash = Hash;
				m_HashChangeTime = Time;
			}
			bool BroadcastSave = false;
			for(const SPendingSave &Save : m_vSaves)
				BroadcastSave |= Save.m_TargetSock == nullptr;
			if(Hash != m_SyncedHash)
			{
				// layers, quads, envelopes... changed: the whole map goes out when the user pauses
				if(Time - m_HashChangeTime > 0.35f && !MouseBusy(E) && !m_ProposalPending && !m_HasIncomingMap && !BroadcastSave)
				{
					const bool Started = StartSave(nullptr);
					m_SyncedHash = Hash;
					m_LastHash = Hash;
					TakeSnapshot();
					if(Started && m_Role == ROLE_GUEST)
						m_ProposalPending = true;
				}
			}
			else if(!SendingBlocked())
			{
				CCoopPacker Tiles;
				Tiles.U32(m_Epoch);
				Tiles.U8(m_SelfId);
				std::vector<uint8_t> vDiff;
				if(DiffTiles(vDiff))
				{
					Tiles.Raw(vDiff.data(), vDiff.size());
					Broadcast(MSG_TILES, Tiles.m_vData);
				}
			}
		}
		SendCursor();
	}
	Flush();
}

// ---- network

void CXcCoop::Queue(SConn &Conn, int Type, const void *pData, size_t Size, bool Hold)
{
	std::vector<uint8_t> &vTarget = (Hold && Conn.m_AwaitMap && m_Role == ROLE_HOST) ? Conn.m_vHeld : Conn.m_vOut;
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
		pConn->m_AwaitMap = false;
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
		while(!m_HasIncomingMap && Conn.m_vIn.size() - Pos >= 4)
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
		if(m_State == STATE_JOINING && Conn.m_vIn.size() >= 5)
		{
			const uint8_t *p = Conn.m_vIn.data();
			const uint32_t Length = p[0] | (p[1] << 8) | (p[2] << 16) | ((uint32_t)p[3] << 24);
			if(p[4] == MSG_MAP && Length > 0)
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
		if(m_pIncomingFrom == &Conn)
			m_pIncomingFrom = nullptr;
		if(Conn.m_MemberId >= 0)
		{
			const SMember *pMember = FindMember(Conn.m_MemberId);
			if(pMember)
				log_info("xc_coop", "'%s' left", pMember->m_Name.c_str());
			m_vMembers.erase(std::remove_if(m_vMembers.begin(), m_vMembers.end(), [&](const SMember &Member) { return Member.m_Id == Conn.m_MemberId; }), m_vMembers.end());
		}
		const bool WasMember = Conn.m_MemberId >= 0;
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
			Conn.m_AwaitMap = true;
			CCoopPacker Welcome;
			Welcome.U8(Id);
			Welcome.U32(m_Epoch);
			Queue(Conn, MSG_WELCOME, Welcome.m_vData.data(), Welcome.m_vData.size(), false);
			SendMembers();
			// the blocks drawn so far go to the others first, so the map file and the diffs agree
			if(!SendingBlocked())
			{
				std::vector<uint8_t> vDiff;
				if(DiffTiles(vDiff))
				{
					CCoopPacker Tiles;
					Tiles.U32(m_Epoch);
					Tiles.U8(m_SelfId);
					Tiles.Raw(vDiff.data(), vDiff.size());
					Broadcast(MSG_TILES, Tiles.m_vData, &Conn);
				}
			}
			if(!StartSave(Conn.m_Sock))
			{
				CCoopPacker Bye;
				Bye.Str("The host could not save the map");
				Queue(Conn, MSG_BYE, Bye.m_vData.data(), Bye.m_vData.size(), false);
				CloseConn(Conn);
			}
			log_info("xc_coop", "'%s' joined", Member.m_Name.c_str());
			return;
		}
		if(!Conn.m_Hello)
			return;
		if(Type == MSG_MAP)
		{
			Msg.U32(); // a guest sends no epoch
			const uint32_t Base = Msg.U32();
			Msg.U8();
			Msg.U8();
			Msg.Str();
			if(Msg.m_Error)
				return;
			m_vIncomingMap.assign(pData + Msg.m_Pos, pData + Size);
			m_HasIncomingMap = true;
			m_pIncomingFrom = &Conn;
			m_IncomingBase = Base;
			m_IncomingAnswer = false;
			return;
		}
		if(Type == MSG_TILES)
		{
			const uint32_t Epoch = Msg.U32();
			Msg.U8();
			if(Msg.m_Error || Epoch != m_Epoch)
				return;
			ApplyTiles(pData + Msg.m_Pos, Msg.Left(), true);
			// to the others, as from this guest
			std::vector<uint8_t> vRelay(pData, pData + Size);
			vRelay[4] = (uint8_t)Conn.m_MemberId;
			Broadcast(MSG_TILES, vRelay, &Conn);
			return;
		}
		if(Type == MSG_CURSOR)
		{
			if(Size < 1)
				return;
			std::vector<uint8_t> vRelay(pData, pData + Size);
			vRelay[0] = (uint8_t)Conn.m_MemberId;
			Handle(Conn, -MSG_CURSOR, vRelay.data(), vRelay.size());
			Broadcast(MSG_CURSOR, vRelay, &Conn);
			return;
		}
		if(Type == -MSG_CURSOR)
			Type = MSG_CURSOR; // the host shows the cursor too
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
		m_Epoch = Msg.U32();
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
	{
		const uint32_t Epoch = Msg.U32();
		Msg.U32();
		const int Flags = Msg.U8();
		Msg.U8();
		const std::string Name = Msg.Str();
		if(Msg.m_Error)
			return;
		m_vIncomingMap.assign(pData + Msg.m_Pos, pData + Size);
		m_HasIncomingMap = true;
		m_IncomingEpoch = Epoch;
		m_IncomingAnswer = (Flags & MAPFLAG_ANSWER) != 0;
		m_Progress = -1.0f;
		m_IncomingName = Name;
	}
	else if(Type == MSG_EPOCH)
	{
		m_Epoch = Msg.U32();
		m_ProposalPending = false;
	}
	else if(Type == MSG_TILES)
	{
		const uint32_t Epoch = Msg.U32();
		Msg.U8();
		if(!Msg.m_Error && Epoch == m_Epoch && !m_ProposalPending && m_pMap)
			ApplyTiles(pData + Msg.m_Pos, Msg.Left(), true);
	}
	else if(Type == MSG_CURSOR)
	{
		const int Id = Msg.U8();
		const bool Visible = Msg.U8() != 0;
		const float x = Msg.F32();
		const float y = Msg.F32();
		const int Group = (int16_t)Msg.U16();
		const int Layer = (int16_t)Msg.U16();
		SMember *pMember = FindMember(Id);
		if(Msg.m_Error || !pMember || Id == m_SelfId)
			return;
		pMember->m_HasCursor = Visible;
		pMember->m_Cursor = vec2(x, y);
		pMember->m_Group = Group;
		pMember->m_Layer = Layer;
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
	const bool Visible = E.Map() == m_pMap;
	const vec2 Pos = Visible ? m_pMap->m_MapViewState.m_MouseWorldNoParaPos : vec2(-1.0f, -1.0f);
	const int Group = m_pMap->m_SelectedGroup;
	const int Layer = m_pMap->m_vSelectedLayers.empty() ? -1 : m_pMap->m_vSelectedLayers[0];
	if(distance(Pos, m_LastCursor) < 0.5f && Group == m_LastCursorGroup && Layer == m_LastCursorLayer)
		return;
	m_NextCursor = Time + 0.05f;
	m_LastCursor = Pos;
	m_LastCursorGroup = Group;
	m_LastCursorLayer = Layer;
	CCoopPacker Cursor;
	Cursor.U8(m_SelfId < 0 ? 0 : m_SelfId);
	Cursor.U8(Visible ? 1 : 0);
	Cursor.F32(Pos.x);
	Cursor.F32(Pos.y);
	Cursor.U16(Group & 0xffff);
	Cursor.U16(Layer & 0xffff);
	Broadcast(MSG_CURSOR, Cursor.m_vData);
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

void CXcCoop::TakeSnapshot()
{
	m_vSnaps.clear();
	if(!m_pMap)
		return;
	for(int g = 0; g < (int)m_pMap->m_vpGroups.size(); g++)
	{
		const auto &vpLayers = m_pMap->m_vpGroups[g]->m_vpLayers;
		for(int l = 0; l < (int)vpLayers.size(); l++)
		{
			CLayerTiles *pLayer = TileLayer(m_pMap, g, l);
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
			m_vSnaps.push_back(std::move(Snap));
		}
	}
}

// the changed blocks since the snapshot: [u16 group][u16 layer][u16 cell size][u32 runs] runs: [u32 start][u32 count][cells]
bool CXcCoop::DiffTiles(std::vector<uint8_t> &vOut)
{
	CCoopPacker Out;
	for(STileSnap &Snap : m_vSnaps)
	{
		CLayerTiles *pLayer = TileLayer(m_pMap, Snap.m_Group, Snap.m_Layer);
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

void CXcCoop::ApplyTiles(const uint8_t *pData, size_t Size, bool UpdateSnap)
{
	if(!m_pMap)
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
		CLayerTiles *pLayer = TileLayer(m_pMap, Group, Layer);
		const bool Fits = pLayer && pLayer->m_pTiles && (int)sizeof(CTile) + ExtraSize(pLayer) == CellSize && (ExtraSize(pLayer) == 0 || ExtraData(pLayer));
		const size_t Cells = pLayer ? (size_t)pLayer->m_Width * pLayer->m_Height : 0;
		STileSnap *pSnap = nullptr;
		if(Fits && UpdateSnap)
			for(STileSnap &Snap : m_vSnaps)
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
					std::memcpy(pSnap->m_vTiles.data() + Cell * sizeof(CTile), pCell, sizeof(CTile));
					if(Extra)
						std::memcpy(pSnap->m_vExtra.data() + Cell * Extra, pCell + sizeof(CTile), Extra);
				}
			}
			Applied = true;
		}
	}
	if(Applied)
		m_pMap->OnModify();
}

// ---- whole maps

bool CXcCoop::StartSave(NETSOCKET TargetSock)
{
	CEditor &E = *m_pEditor;
	E.Storage()->CreateFolder("xclient", IStorage::TYPE_SAVE);
	E.Storage()->CreateFolder(COOP_FOLDER, IStorage::TYPE_SAVE);
	char aPath[IO_MAX_PATH_LENGTH];
	for(int Try = 0; Try < 16; Try++)
	{
		str_format(aPath, sizeof(aPath), "%s/out_%d.map", COOP_FOLDER, m_SaveCounter++ % 16);
		if(!E.IsSaving(aPath))
			break;
	}
	const size_t Before = E.m_WriterFinishJobs.size();
	const bool Saved = m_pMap->Save(aPath, [&E](const char *pError) { E.ShowFileDialogError("%s", pError); });
	if(!Saved || E.m_WriterFinishJobs.size() == Before)
		return false;
	SPendingSave Save;
	Save.m_pJob = E.m_WriterFinishJobs.back();
	Save.m_Path = aPath;
	Save.m_TargetSock = TargetSock;
	Save.m_Hash = StateHash(m_pMap);
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
		void *pFile = nullptr;
		unsigned FileSize = 0;
		if(Save.m_pJob->ErrorMessage()[0] != '\0' || !E.Storage()->ReadFile(Save.m_Path.c_str(), IStorage::TYPE_SAVE, &pFile, &FileSize))
		{
			if(Save.m_TargetSock == nullptr && m_Role == ROLE_GUEST)
				m_ProposalPending = false;
			continue;
		}
		E.Storage()->RemoveFile(Save.m_Path.c_str(), IStorage::TYPE_SAVE);

		CCoopPacker Map;
		if(Save.m_TargetSock == nullptr && m_Role == ROLE_HOST)
			m_Epoch++;
		Map.U32(m_Role == ROLE_HOST ? m_Epoch : 0);
		Map.U32(m_Epoch); // the base the change was made on
		Map.U8(0);
		Map.U8(m_SelfId < 0 ? 0 : m_SelfId);
		Map.Str(m_pMap ? m_pMap->m_aDisplayName : "");
		Map.Raw(pFile, FileSize);
		free(pFile);

		if(Save.m_TargetSock == nullptr)
		{
			// a change of the structure: the host gives it to everyone, a guest gives it to the host
			Broadcast(MSG_MAP, Map.m_vData);
			continue;
		}
		for(auto &pConn : m_vpConns)
		{
			if(pConn->m_Sock != Save.m_TargetSock || pConn->m_Dead)
				continue;
			// the first map of a new guest, then what happened while it was written
			Queue(*pConn, MSG_MAP, Map.m_vData.data(), Map.m_vData.size(), false);
			pConn->m_vOut.insert(pConn->m_vOut.end(), pConn->m_vHeld.begin(), pConn->m_vHeld.end());
			pConn->m_vHeld.clear();
			pConn->m_AwaitMap = false;
		}
	}
}

void CXcCoop::LoadIncomingMap()
{
	CEditor &E = *m_pEditor;
	m_HasIncomingMap = false;
	std::vector<uint8_t> vData;
	std::swap(vData, m_vIncomingMap);

	// the blocks drawn here and not sent yet go onto the new map again
	std::vector<uint8_t> vLocal;
	if(m_pMap && StateHash(m_pMap) == m_SyncedHash)
		DiffTiles(vLocal);

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
			Leave("The map of the room could not be opened");
			return;
		}
		// the host keeps his map and sends it to everyone instead
		m_SyncedHash = 0;
		return;
	}

	auto It = std::find_if(E.m_vpMaps.begin(), E.m_vpMaps.end(), [&](const std::unique_ptr<CEditorMap> &pMap) { return pMap.get() == m_pMap; });
	if(It == E.m_vpMaps.end())
	{
		// a guest gets his first map: a new tab
		char aName[IO_MAX_PATH_LENGTH];
		str_format(aName, sizeof(aName), "maps/%s.map", m_IncomingName.empty() ? "coop" : m_IncomingName.c_str());
		str_copy(pNew->m_aFilename, aName);
		pNew->m_ValidSaveFilename = false;
		E.Reset();
		E.m_vpMaps.push_back(std::move(pNew));
		E.m_SelectedMap = E.m_vpMaps.size() - 1;
		E.m_MapTabsRevealSelected = true;
		m_pMap = E.m_vpMaps.back().get();
		m_State = STATE_ONLINE;
		m_Progress = -1.0f;
	}
	else
	{
		// the same tab, the same view, the same selection
		CEditorMap *pOld = It->get();
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
		if(It - E.m_vpMaps.begin() == (std::ptrdiff_t)E.m_SelectedMap)
		{
			E.Ui()->ClosePopupMenus();
			E.Ui()->SetActiveItem(nullptr);
		}
		*It = std::move(pNew);
		m_pMap = It->get();
	}
	E.UpdateMapDisplayNames();
	if(E.Map() == m_pMap)
		for(CEditorComponent &Component : E.m_vComponents)
			Component.OnMapLoad();
	m_MapReplaced = true;

	const uint32_t OldEpoch = m_Epoch;
	if(m_Role == ROLE_HOST)
	{
		// a change of a guest: everyone gets the new map, its author only the new number (or the map, if
		// the host changed something in the meantime)
		m_Epoch++;
		CCoopPacker Map;
		Map.U32(m_Epoch);
		Map.U32(m_Epoch);
		Map.U8(0);
		Map.U8(m_pIncomingFrom ? m_pIncomingFrom->m_MemberId : 0);
		Map.Str(m_pMap->m_aDisplayName);
		Map.Raw(vData.data(), vData.size());
		const bool Fresh = m_pIncomingFrom && m_IncomingBase == OldEpoch;
		for(auto &pConn : m_vpConns)
		{
			if(!pConn->m_Hello || pConn->m_Dead)
				continue;
			if(pConn.get() == m_pIncomingFrom && Fresh)
			{
				CCoopPacker Epoch;
				Epoch.U32(m_Epoch);
				Queue(*pConn, MSG_EPOCH, Epoch.m_vData.data(), Epoch.m_vData.size());
				continue;
			}
			Map.m_vData[8] = pConn.get() == m_pIncomingFrom ? MAPFLAG_ANSWER : 0;
			Queue(*pConn, MSG_MAP, Map.m_vData.data(), Map.m_vData.size());
		}
		m_pIncomingFrom = nullptr;
	}
	else
	{
		m_Epoch = m_IncomingEpoch;
		if(m_IncomingAnswer)
			m_ProposalPending = false;
	}

	TakeSnapshot();
	m_SyncedHash = m_LastHash = StateHash(m_pMap);
	// own blocks on top; the snapshot does not have them, so they go out with the new number
	if(!vLocal.empty())
		ApplyTiles(vLocal.data(), vLocal.size(), false);
	log_info("xc_coop", "map %u loaded", m_Epoch);
}

// Editing maps together over the network (LAN, Radmin VPN, Hamachi...). One player opens a room, the
// others connect to his IP. Every map tab of the host is shared, and so is every tab anyone opens later:
// each player works in whichever tab he likes, like tabs in a browser.
// A map goes to a new player as a whole map file; after that only the changed blocks travel (a diff of all
// tile layers a few times a second), so drawing feels live. Everything else (layers, quads, envelopes,
// pictures, settings) sends the whole map again after a short pause. The host checks every map now and
// then against everyone (a hash of everything, blocks included), a map that went out of sync loads again.
#ifndef GAME_CLIENT_COMPONENTS_XCLIENT_EDITOR_XEDITOR_COOP_H
#define GAME_CLIENT_COMPONENTS_XCLIENT_EDITOR_XEDITOR_COOP_H

#include <base/types.h>
#include <base/vmath.h>

#include <cstdint>
#include <functional>
#include <memory>
#include <set>
#include <string>
#include <vector>

class CEditor;
class CEditorMap;
class CDataFileWriterFinishJob;

class CXcCoop
{
public:
	enum
	{
		ROLE_NONE = 0,
		ROLE_HOST,
		ROLE_GUEST,
	};
	enum
	{
		STATE_IDLE = 0,
		STATE_CONNECTING, // a guest waits for the TCP connection
		STATE_JOINING, // a guest waits for the maps
		STATE_ONLINE,
	};
	enum
	{
		MAX_MEMBERS = 16,
	};
	struct SMember
	{
		int m_Id = -1;
		std::string m_Name;
		vec2 m_Cursor = vec2(0.0f, 0.0f);
		bool m_HasCursor = false;
		uint32_t m_MapId = 0; // the tab the member is in
		int m_Group = -1;
		int m_Layer = -1;
		float m_LastSeen = 0.0f; // when the cursor moved
	};

	void Init(CEditor *pEditor) { m_pEditor = pEditor; }
	~CXcCoop();

	bool Host(int Port, char *pError, int ErrorSize);
	bool Join(const char *pAddress, char *pError, int ErrorSize);
	void Leave(const char *pReason = nullptr);

	// every frame of the editor, before its panels
	void Update();
	void OnActivity(); // the user pressed something: look for changes more often
	// play the map of the current tab together: the host starts his local server, everyone joins it
	void RequestPlay();
	bool PlayStarting() const { return m_PlayMapId != 0; }
	// the host started playing a tab: live editing sends its blocks to the server (tab, its room id, map name)
	std::function<void(CEditorMap *, uint32_t, const char *)> m_fnOnPlay;

	int Role() const { return m_Role; }
	int State() const { return m_State; }
	int SelfId() const { return m_SelfId; }
	int Port() const { return m_Port; }
	const char *Address() const { return m_aAddress; }
	const std::vector<SMember> &Members() const { return m_vMembers; }
	// the room id of a shared map tab, 0 for a tab that is not shared (yet)
	uint32_t MapId(const CEditorMap *pMap) const;
	int NumSharedMaps() const { return (int)m_vpShared.size(); }
	// a message for the user after the session ended (or empty)
	std::string TakeNotice();
	// what happens right now, for the status line ("getting the map 40%")
	float Progress() const { return m_Progress; }
	static unsigned MemberColor(int Id); // 0xRRGGBB
	// true once after a map came from the room: pointers into the old map are gone
	bool TakeMapReplaced()
	{
		const bool Replaced = m_MapReplaced;
		m_MapReplaced = false;
		return Replaced;
	}

private:
	struct SConn;
	struct STileSnap
	{
		int m_Group;
		int m_Layer;
		int m_Width;
		int m_Height;
		int m_CellSize;
		std::vector<uint8_t> m_vTiles; // CTile per cell
		std::vector<uint8_t> m_vExtra; // the tele/speedup/switch/tune data per cell
	};
	// one shared map tab and how far it is in sync
	struct SShared
	{
		uint32_t m_Id = 0; // given by the host; 0 while a guest waits for it
		uint32_t m_Token = 0; // a guest's own number for a new tab until the host gives the id
		CEditorMap *m_pMap = nullptr;
		// every whole map has a number given by the host; blocks are only applied to the map they were drawn on
		uint32_t m_Epoch = 0;
		std::vector<STileSnap> m_vSnaps; // the tile layers as they were last sent or received
		uint64_t m_SyncedHash = 0;
		uint64_t m_LastHash = 0;
		float m_HashChangeTime = 0.0f;
		bool m_ProposalPending = false; // a guest sent its change and waits for the host to take it
		// a whole map that came in while the user was dragging something: loaded when the mouse is free
		bool m_HasIncoming = false;
		std::vector<uint8_t> m_vIncoming;
		uint32_t m_IncomingEpoch = 0;
		uint32_t m_IncomingBase = 0;
		bool m_IncomingAnswer = false; // the map answers our own change
		bool m_IncomingReplace = false; // the host says: take this one as it is (out of sync)
		SConn *m_pIncomingFrom = nullptr; // the host: the guest whose change it is
		std::string m_IncomingName;
		float m_NextVerify = 0.0f; // the host: when to send the check
		int m_Mismatches = 0; // a guest: checks in a row that did not match
	};

	CEditor *m_pEditor = nullptr;
	int m_Role = ROLE_NONE;
	int m_State = STATE_IDLE;
	int m_SelfId = -1;
	int m_Port = 0;
	char m_aAddress[128] = {};
	std::vector<std::unique_ptr<SShared>> m_vpShared;
	std::set<const CEditorMap *> m_PrivateMaps; // a guest's tabs from before joining stay his own
	uint32_t m_NextMapId = 1;
	uint32_t m_NextToken = 1;
	std::vector<SMember> m_vMembers;
	std::string m_Notice;
	float m_Progress = -1.0f;
	bool m_MapReplaced = false;

	struct SConn
	{
		NETSOCKET m_Sock = nullptr;
		int m_MemberId = -1;
		bool m_Hello = false;
		int m_AwaitMaps = 0; // a new guest: whole maps still being written for it; the blocks wait until then
		std::vector<uint8_t> m_vIn;
		std::vector<uint8_t> m_vOut;
		size_t m_OutPos = 0;
		std::vector<uint8_t> m_vHeld;
		bool m_Dead = false;
	};
	NETSOCKET m_Listen = nullptr;
	std::vector<std::unique_ptr<SConn>> m_vpConns; // the host: one per guest; a guest: the host

	// a guest connects in a thread so the editor does not hang on a wrong address
	struct SConnectJob;
	std::shared_ptr<SConnectJob> m_pConnect;

	float m_NextCheck = 0.0f;
	float m_LastActivity = 0.0f;
	float m_NextCursor = 0.0f;
	vec2 m_LastCursor = vec2(-1.0f, -1.0f);
	int m_LastCursorGroup = -2;
	int m_LastCursorLayer = -2;
	uint32_t m_LastCursorMap = 0;

	// maps being written for sending (the editor saves in a job)
	enum
	{
		SAVE_CHANGE = 0, // a change for everyone (a guest: for the host)
		SAVE_TARGET, // the map for one guest (joining, or out of sync)
		SAVE_NEW, // a guest's new tab, for the host
		SAVE_PLAY, // the map for the local server
	};
	struct SPendingSave
	{
		std::shared_ptr<CDataFileWriterFinishJob> m_pJob;
		std::string m_Path;
		uint32_t m_MapId;
		uint32_t m_Token;
		int m_Kind;
		NETSOCKET m_TargetSock;
		bool m_Replace; // SAVE_TARGET: the guest replaces its copy
	};
	std::vector<SPendingSave> m_vSaves;
	int m_SaveCounter = 0;

	// playing together
	uint32_t m_PlayMapId = 0; // the tab being saved for the server
	uint32_t m_PlayingMapId = 0; // the tab the server plays
	float m_PlayRunAt = -1.0f; // the old server was stopped: start the new one then
	void StartPlay(uint32_t MapId);
	void RunPlayServer();
	void JoinPlay(int Port);

	float Now() const;
	SShared *FindShared(uint32_t Id);
	SShared *FindSharedMap(const CEditorMap *pMap);
	SShared *FindToken(uint32_t Token);
	int MapIndex(const CEditorMap *pMap) const;
	void ResetSync(SShared &Shared);
	void TrackTabs();
	void CloseConn(SConn &Conn);
	void Poll();
	void Flush();
	void Queue(SConn &Conn, int Type, const void *pData, size_t Size, bool Hold = true);
	void Broadcast(int Type, const std::vector<uint8_t> &vData, const SConn *pExcept = nullptr);
	void Handle(SConn &Conn, int Type, const uint8_t *pData, size_t Size);
	void HandleMap(SConn &Conn, const uint8_t *pData, size_t Size);
	void SendMembers();
	SMember *FindMember(int Id);
	SConn *ConnOf(int MemberId);

	uint64_t StateHash(const CEditorMap *pMap) const;
	uint64_t FullHash(SShared &Shared) const;
	void TakeSnapshot(SShared &Shared);
	bool DiffTiles(SShared &Shared, std::vector<uint8_t> &vOut);
	bool TilesDirty(const SShared &Shared) const;
	void ApplyTiles(SShared &Shared, const uint8_t *pData, size_t Size, bool UpdateSnap);
	bool StartSave(SShared &Shared, int Kind, NETSOCKET TargetSock = nullptr, bool Replace = false);
	bool BroadcastSavePending(uint32_t MapId) const;
	bool SendingBlocked(const SShared &Shared) const;
	void FinishSaves();
	void LoadIncomingMap(SShared &Shared);
	void CloseSharedTab(uint32_t Id, int ByMember);
	void SendCursor();
	void UpdateMap(SShared &Shared, float Time);
};

#endif

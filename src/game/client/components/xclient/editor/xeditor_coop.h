// Editing one map together over the network (LAN, Radmin VPN, Hamachi...). One player opens a room, the
// others connect to his IP. The map goes to a new player as a whole map file; after that only the changed
// blocks travel (a diff of all tile layers a few times a second), so drawing feels live. Everything else
// (layers, quads, envelopes, pictures, settings) sends the whole map again after a short pause.
#ifndef GAME_CLIENT_COMPONENTS_XCLIENT_EDITOR_XEDITOR_COOP_H
#define GAME_CLIENT_COMPONENTS_XCLIENT_EDITOR_XEDITOR_COOP_H

#include <base/types.h>
#include <base/vmath.h>

#include <cstdint>
#include <memory>
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
		STATE_JOINING, // a guest waits for the map
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

	int Role() const { return m_Role; }
	int State() const { return m_State; }
	int SelfId() const { return m_SelfId; }
	int Port() const { return m_Port; }
	const char *Address() const { return m_aAddress; }
	const std::vector<SMember> &Members() const { return m_vMembers; }
	const CEditorMap *SharedMap() const { return m_pMap; }
	// a message for the user after the session ended (or empty)
	std::string TakeNotice();
	// what happens right now, for the status line ("sending the map 40%")
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
	CEditor *m_pEditor = nullptr;
	int m_Role = ROLE_NONE;
	int m_State = STATE_IDLE;
	int m_SelfId = -1;
	int m_Port = 0;
	char m_aAddress[128] = {};
	CEditorMap *m_pMap = nullptr; // the map tab that is shared
	std::vector<SMember> m_vMembers;
	std::string m_Notice;
	float m_Progress = -1.0f;

	struct SConn
	{
		NETSOCKET m_Sock = nullptr;
		int m_MemberId = -1;
		bool m_Hello = false;
		bool m_AwaitMap = true; // the guest has no map yet: hold the blocks until the map is queued
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

	// the tile layers as they were last sent or received
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
	std::vector<STileSnap> m_vSnaps;
	uint64_t m_SyncedHash = 0;
	uint64_t m_LastHash = 0;
	float m_HashChangeTime = 0.0f;
	float m_NextCheck = 0.0f;
	float m_LastActivity = 0.0f;
	float m_NextCursor = 0.0f;
	vec2 m_LastCursor = vec2(-1.0f, -1.0f);
	int m_LastCursorGroup = -2;
	int m_LastCursorLayer = -2;

	// maps being written for sending (the editor saves in a job)
	struct SPendingSave
	{
		std::shared_ptr<CDataFileWriterFinishJob> m_pJob;
		std::string m_Path;
		NETSOCKET m_TargetSock; // nullptr: a change for everyone, else the map for a new guest
		uint64_t m_Hash;
	};
	std::vector<SPendingSave> m_vSaves;
	int m_SaveCounter = 0;
	// a map that came in while the user was dragging something: loaded when the mouse is free
	std::vector<uint8_t> m_vIncomingMap;
	bool m_HasIncomingMap = false;
	bool m_IncomingAnswer = false; // the map answers our own change
	std::string m_IncomingName; // the name of the map tab of the host
	bool m_MapReplaced = false;
	uint32_t m_IncomingEpoch = 0;
	SConn *m_pIncomingFrom = nullptr; // the host: the guest whose change it is
	uint32_t m_IncomingBase = 0;
	// every whole map has a number given by the host; blocks are only applied to the map they were drawn on
	uint32_t m_Epoch = 0;
	bool m_ProposalPending = false; // a guest sent its change and waits for the host to take it

	float Now() const;
	bool MapAlive();
	void ResetSync();
	void CloseConn(SConn &Conn);
	void Poll();
	void Flush();
	void Queue(SConn &Conn, int Type, const void *pData, size_t Size, bool Hold = true);
	void Broadcast(int Type, const std::vector<uint8_t> &vData, const SConn *pExcept = nullptr);
	void Handle(SConn &Conn, int Type, const uint8_t *pData, size_t Size);
	void SendMembers();
	SMember *FindMember(int Id);

	uint64_t StateHash(const CEditorMap *pMap) const;
	void TakeSnapshot();
	bool DiffTiles(std::vector<uint8_t> &vOut);
	void ApplyTiles(const uint8_t *pData, size_t Size, bool UpdateSnap);
	bool StartSave(NETSOCKET TargetSock);
	bool SendingBlocked() const;
	void FinishSaves();
	void LoadIncomingMap();
	void SendCursor();
};

#endif

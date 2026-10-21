#include "discord_rpc.h"

#include "ui/lang.h"

#include <base/log.h>
#include <base/process.h>
#include <base/str.h>
#include <base/detect.h>
#include <base/time.h>

#include <engine/client.h>
#include <engine/external/json-parser/json.h>
#include <engine/http.h>
#include <engine/shared/config.h>
#include <engine/shared/jsonwriter.h>

#include <game/client/gameclient.h>

#include <chrono>
#include <cstring>
#include <functional>

#if defined(CONF_FAMILY_WINDOWS)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#else
#include <poll.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#endif

// ---------------------------------------------------------------------------------------------
// The IPC pipe of the Discord app. Every message is a frame: opcode and length (little endian
// 32 bit each), then JSON.

namespace
{
enum
{
	OP_HANDSHAKE = 0,
	OP_FRAME = 1,
	OP_CLOSE = 2,
	OP_PING = 3,
	OP_PONG = 4,
};

class CIpc
{
public:
	~CIpc() { Close(); }

	bool Open()
	{
		Close();
		// Discord takes the first free pipe of discord-ipc-0..9
		for(int i = 0; i < 10; i++)
		{
#if defined(CONF_FAMILY_WINDOWS)
			wchar_t aName[64];
			swprintf(aName, 64, L"\\\\.\\pipe\\discord-ipc-%d", i);
			m_Pipe = CreateFileW(aName, GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, 0, nullptr);
			if(m_Pipe != INVALID_HANDLE_VALUE)
				return true;
#else
			// the regular app, then the Flatpak and Snap builds
			const char *pBase = getenv("XDG_RUNTIME_DIR");
			if(!pBase || !pBase[0])
				pBase = getenv("TMPDIR");
			if(!pBase || !pBase[0])
				pBase = "/tmp";
			static const char *const s_apSub[] = {"", "app/com.discordapp.Discord/", "snap.discord/"};
			for(const char *pSub : s_apSub)
			{
				sockaddr_un Addr = {};
				Addr.sun_family = AF_UNIX;
				str_format(Addr.sun_path, sizeof(Addr.sun_path), "%s/%sdiscord-ipc-%d", pBase, pSub, i);
				const int Fd = socket(AF_UNIX, SOCK_STREAM, 0);
				if(Fd < 0)
					return false;
				if(connect(Fd, (sockaddr *)&Addr, sizeof(Addr)) == 0)
				{
					m_Fd = Fd;
					return true;
				}
				close(Fd);
			}
#endif
		}
		return false;
	}

	void Close()
	{
#if defined(CONF_FAMILY_WINDOWS)
		if(m_Pipe != INVALID_HANDLE_VALUE)
			CloseHandle(m_Pipe);
		m_Pipe = INVALID_HANDLE_VALUE;
#else
		if(m_Fd >= 0)
			close(m_Fd);
		m_Fd = -1;
#endif
	}

	bool Write(int Op, const std::string &Json)
	{
		std::string Frame(8 + Json.size(), '\0');
		const uint32_t aHeader[2] = {(uint32_t)Op, (uint32_t)Json.size()};
		for(int i = 0; i < 2; i++)
			for(int b = 0; b < 4; b++)
				Frame[i * 4 + b] = (char)((aHeader[i] >> (8 * b)) & 0xff);
		std::memcpy(&Frame[8], Json.data(), Json.size());
		return WriteAll(Frame.data(), Frame.size());
	}

	// 1 got a frame, 0 nothing within the timeout, -1 the pipe broke
	int Read(int *pOp, std::string *pJson, int TimeoutMs)
	{
		unsigned char aHeader[8];
		const int Got = ReadAll(aHeader, sizeof(aHeader), TimeoutMs);
		if(Got <= 0)
			return Got;
		*pOp = aHeader[0] | (aHeader[1] << 8) | (aHeader[2] << 16) | (aHeader[3] << 24);
		const uint32_t Size = aHeader[4] | (aHeader[5] << 8) | (aHeader[6] << 16) | ((uint32_t)aHeader[7] << 24);
		if(Size > 1024 * 1024)
			return -1;
		pJson->assign(Size, '\0');
		// the rest of a frame follows right away
		if(Size > 0 && ReadAll(pJson->data(), Size, 3000) <= 0)
			return -1;
		return 1;
	}

private:
#if defined(CONF_FAMILY_WINDOWS)
	HANDLE m_Pipe = INVALID_HANDLE_VALUE;

	bool WriteAll(const char *pData, size_t Size)
	{
		while(Size > 0)
		{
			DWORD Written = 0;
			if(!WriteFile(m_Pipe, pData, (DWORD)Size, &Written, nullptr) || Written == 0)
				return false;
			pData += Written;
			Size -= Written;
		}
		return true;
	}

	int ReadAll(void *pData, size_t Size, int TimeoutMs)
	{
		// the pipe is synchronous: only read what is already there, so a silent Discord never blocks us
		const auto Until = std::chrono::steady_clock::now() + std::chrono::milliseconds(TimeoutMs);
		size_t Done = 0;
		while(Done < Size)
		{
			DWORD Available = 0;
			if(!PeekNamedPipe(m_Pipe, nullptr, 0, nullptr, &Available, nullptr))
				return -1;
			if(Available == 0)
			{
				if(std::chrono::steady_clock::now() >= Until)
					return Done == 0 ? 0 : -1;
				Sleep(10);
				continue;
			}
			DWORD Read = 0;
			const DWORD Want = (DWORD)std::min<size_t>(Size - Done, Available);
			if(!ReadFile(m_Pipe, (char *)pData + Done, Want, &Read, nullptr) || Read == 0)
				return -1;
			Done += Read;
		}
		return 1;
	}
#else
	int m_Fd = -1;

	bool WriteAll(const char *pData, size_t Size)
	{
		while(Size > 0)
		{
			const ssize_t Written = send(m_Fd, pData, Size, MSG_NOSIGNAL);
			if(Written <= 0)
				return false;
			pData += Written;
			Size -= Written;
		}
		return true;
	}

	int ReadAll(void *pData, size_t Size, int TimeoutMs)
	{
		size_t Done = 0;
		while(Done < Size)
		{
			pollfd Poll = {m_Fd, POLLIN, 0};
			const int Ready = poll(&Poll, 1, TimeoutMs);
			if(Ready < 0)
				return -1;
			if(Ready == 0)
				return Done == 0 ? 0 : -1;
			const ssize_t Read = recv(m_Fd, (char *)pData + Done, Size - Done, 0);
			if(Read <= 0)
				return -1;
			Done += Read;
		}
		return 1;
	}
#endif
};
} // namespace

// ---------------------------------------------------------------------------------------------
// The worker thread

CXcDiscordRpc::~CXcDiscordRpc()
{
	Stop();
}

void CXcDiscordRpc::OnShutdown()
{
	Stop();
}

void CXcDiscordRpc::Start()
{
	if(m_Thread.joinable())
		return;
	m_Quit = false;
	m_Thread = std::thread([this]() { Run(); });
}

void CXcDiscordRpc::Stop()
{
	if(!m_Thread.joinable())
		return;
	{
		std::lock_guard<std::mutex> Lock(m_Mutex);
		m_Quit = true;
	}
	m_Wake.notify_all();
	m_Thread.join();
	m_Connected = false;
	m_LastActivity.clear();
}

void CXcDiscordRpc::Run()
{
	CIpc Ipc;
	bool Connected = false;
	std::string ConnectedAppId;
	std::string Sent; // the last activity Discord has, sent again after a reconnect
	int Nonce = 0;
	auto Disconnect = [&]() {
		Ipc.Close();
		Connected = false;
		m_Connected = false;
	};

	std::unique_lock<std::mutex> Lock(m_Mutex);
	while(!m_Quit)
	{
		const std::string AppId = m_AppId;
		if(Connected && AppId != ConnectedAppId)
			Disconnect();

		if(!Connected)
		{
			// Discord may start later than the game: try again now and then
			Lock.unlock();
			bool Ok = !AppId.empty() && Ipc.Open();
			if(Ok)
			{
				char aHello[128];
				str_format(aHello, sizeof(aHello), "{\"v\":1,\"client_id\":\"%s\"}", AppId.c_str());
				int Op;
				std::string Reply;
				Ok = Ipc.Write(OP_HANDSHAKE, aHello) && Ipc.Read(&Op, &Reply, 5000) == 1 && Op == OP_FRAME && Reply.find("\"READY\"") != std::string::npos;
				if(!Ok)
				{
					if(!Reply.empty())
						log_info("discord", "handshake failed: %s", Reply.substr(0, 200).c_str());
					Ipc.Close();
				}
			}
			Lock.lock();
			if(!Ok)
			{
				m_Wake.wait_for(Lock, std::chrono::seconds(15), [this]() { return m_Quit; });
				continue;
			}
			Connected = true;
			ConnectedAppId = AppId;
			m_Connected = true;
			log_info("discord", "connected");
			if(!m_HasPending && !Sent.empty())
			{
				m_Pending = Sent;
				m_HasPending = true;
			}
		}

		if(!m_HasPending)
			m_Wake.wait_for(Lock, std::chrono::seconds(5), [this]() { return m_Quit || m_HasPending; });
		if(m_Quit)
			break;
		std::string Activity;
		const bool Send = m_HasPending;
		if(Send)
		{
			Activity = m_Pending;
			m_HasPending = false;
		}
		Lock.unlock();

		bool Ok = true;
		if(Send)
		{
			char aHead[96];
			str_format(aHead, sizeof(aHead), "{\"cmd\":\"SET_ACTIVITY\",\"nonce\":\"%d\",\"args\":{\"pid\":%d,\"activity\":", ++Nonce, process_id());
			Ok = Ipc.Write(OP_FRAME, aHead + Activity + "}}");
			if(Ok)
				Sent = Activity;
		}
		// answers and pings; Discord answers every command, so after a send wait a bit for it
		int Op;
		std::string Reply;
		for(int Tries = 0; Ok && Tries < 8; Tries++)
		{
			const int Got = Ipc.Read(&Op, &Reply, Send && Tries == 0 ? 3000 : 0);
			if(Got == 0)
				break;
			if(Got < 0 || Op == OP_CLOSE)
				Ok = false;
			else if(Op == OP_PING)
				Ok = Ipc.Write(OP_PONG, Reply);
			else if(Reply.find("\"evt\":\"ERROR\"") != std::string::npos)
				log_info("discord", "error: %s", Reply.substr(0, 300).c_str());
		}
		Lock.lock();
		if(!Ok)
		{
			log_info("discord", "disconnected");
			Disconnect();
		}
		else if(Send)
		{
			// Discord takes at most 5 updates in 20 seconds, newer ones wait and replace each other
			m_Wake.wait_for(Lock, std::chrono::seconds(4), [this]() { return m_Quit; });
		}
	}
	Lock.unlock();
	if(Connected)
	{
		// leave no stale status behind
		char aHead[96];
		str_format(aHead, sizeof(aHead), "{\"cmd\":\"SET_ACTIVITY\",\"nonce\":\"%d\",\"args\":{\"pid\":%d}}", ++Nonce, process_id());
		Ipc.Write(OP_FRAME, aHead);
	}
	Ipc.Close();
	m_Connected = false;
}

// ---------------------------------------------------------------------------------------------
// The game side

enum
{
	STATE_MENU = 0,
	STATE_CONNECTING,
	STATE_PLAYING,
	STATE_DEMO,
	STATE_EDITOR,
};

void CXcDiscordRpc::UpdateThumbnail(const char *pMap)
{
	if(str_comp(m_aThumbMap, pMap) != 0)
	{
		str_copy(m_aThumbMap, pMap);
		m_Thumbnail.clear();
		if(m_pThumbTask)
			m_pThumbTask->Abort();
		m_pThumbTask = nullptr;
		const char *pPlaceholder = str_find(g_Config.m_XcMapInfoUrl, "%s");
		if(!pMap[0] || !pPlaceholder)
			return;
		char aEscaped[256];
		EscapeUrl(aEscaped, pMap);
		char aUrl[512];
		str_format(aUrl, sizeof(aUrl), "%.*s%s%s", (int)(pPlaceholder - g_Config.m_XcMapInfoUrl), g_Config.m_XcMapInfoUrl, aEscaped, pPlaceholder + 2);
		m_pThumbTask = HttpGet(aUrl);
		m_pThumbTask->Timeout(CTimeout{10000, 0, 500, 10});
		m_pThumbTask->MaxResponseSize(8 * 1024 * 1024);
		m_pThumbTask->LogProgress(HTTPLOG::FAILURE);
		Http()->Run(m_pThumbTask);
	}
	if(!m_pThumbTask || !m_pThumbTask->Done())
		return;
	std::shared_ptr<IHttpRequest> pTask = m_pThumbTask;
	m_pThumbTask = nullptr;
	if(pTask->State() != EHttpState::DONE)
		return;
	json_value *pJson = pTask->ResultJson();
	if(!pJson)
		return;
	const json_value &Thumb = (*pJson)["thumbnail"];
	// only pictures from https addresses, Discord shows nothing else
	if(Thumb.type == json_string && str_startswith(Thumb.u.string.ptr, "https://"))
		m_Thumbnail = Thumb.u.string.ptr;
	json_value_free(pJson);
}

std::string CXcDiscordRpc::BuildActivity()
{
	const int ClientState = Client()->State();
	int State = STATE_MENU;
	if(g_Config.m_ClEditor)
		State = STATE_EDITOR;
	else if(ClientState == IClient::STATE_DEMOPLAYBACK)
		State = STATE_DEMO;
	else if(ClientState == IClient::STATE_ONLINE)
		State = STATE_PLAYING;
	else if(ClientState == IClient::STATE_CONNECTING || ClientState == IClient::STATE_LOADING)
		State = STATE_CONNECTING;

	const CServerInfo &Info = Client()->ServerInfo();
	const char *pMap = State == STATE_PLAYING || State == STATE_DEMO ? Info.m_aMap : "";
	if(g_Config.m_XcDiscordMapImage)
		UpdateThumbnail(pMap);

	// the clock restarts with a new activity: another server, another map, menu or game
	char aKey[160];
	str_format(aKey, sizeof(aKey), "%s|%s", State == STATE_PLAYING ? Info.m_aAddress : "", pMap);
	if(State != m_LastState || str_comp(aKey, m_aStartServer) != 0)
	{
		if(!(State == STATE_PLAYING && m_LastState == STATE_CONNECTING))
			m_StartTime = time_timestamp();
		m_LastState = State;
		str_copy(m_aStartServer, aKey);
	}

	char aDetails[128];
	char aState[128] = "";
	switch(State)
	{
	case STATE_EDITOR: str_copy(aDetails, XcUi::Tr("Editing a map")); break;
	case STATE_DEMO: str_copy(aDetails, XcUi::Tr("Watching a demo")); break;
	case STATE_CONNECTING: str_copy(aDetails, XcUi::Tr("Connecting")); break;
	case STATE_PLAYING:
		if(g_Config.m_XcDiscordServer && Info.m_aName[0])
			str_copy(aDetails, Info.m_aName);
		else
			str_copy(aDetails, XcUi::Tr("Playing"));
		break;
	default: str_copy(aDetails, XcUi::Tr("In the menu"));
	}
	if(pMap[0] && g_Config.m_XcDiscordMap)
		str_format(aState, sizeof(aState), "%s: %s", XcUi::Tr("Map"), pMap);
	else if(State == STATE_PLAYING)
		str_copy(aState, XcUi::Tr("On a server"));

	CJsonStringWriter Writer;
	Writer.BeginObject();
	Writer.WriteAttribute("details");
	Writer.WriteStrValue(aDetails);
	if(aState[0])
	{
		Writer.WriteAttribute("state");
		Writer.WriteStrValue(aState);
	}
	if(State == STATE_PLAYING && g_Config.m_XcDiscordPlayers && Info.m_MaxClients > 0)
	{
		Writer.WriteAttribute("party");
		Writer.BeginObject();
		// the party needs an id; a hash, so the server address does not go to Discord
		char aPartyId[32];
		str_format(aPartyId, sizeof(aPartyId), "exora-%08x", (unsigned)std::hash<std::string>{}(Info.m_aAddress));
		Writer.WriteAttribute("id");
		Writer.WriteStrValue(aPartyId);
		Writer.WriteAttribute("size");
		Writer.BeginArray();
		Writer.WriteIntValue(std::max(Info.m_NumClients, 1));
		Writer.WriteIntValue(Info.m_MaxClients);
		Writer.EndArray();
		Writer.EndObject();
	}
	if(g_Config.m_XcDiscordTime)
	{
		Writer.WriteAttribute("timestamps");
		Writer.BeginObject();
		Writer.WriteAttribute("start");
		Writer.WriteIntValue((int)m_StartTime);
		Writer.EndObject();
	}

	// the map picture with the client logo in the corner, or the logo alone
	Writer.WriteAttribute("assets");
	Writer.BeginObject();
	const bool Thumb = pMap[0] && g_Config.m_XcDiscordMapImage && !m_Thumbnail.empty();
	Writer.WriteAttribute("large_image");
	Writer.WriteStrValue(Thumb ? m_Thumbnail.c_str() : g_Config.m_XcDiscordLogo);
	Writer.WriteAttribute("large_text");
	Writer.WriteStrValue(Thumb ? pMap : "Exora Client");
	if(Thumb && g_Config.m_XcDiscordLogo[0])
	{
		Writer.WriteAttribute("small_image");
		Writer.WriteStrValue(g_Config.m_XcDiscordLogo);
		Writer.WriteAttribute("small_text");
		Writer.WriteStrValue("Exora Client");
	}
	Writer.EndObject();

	if(g_Config.m_XcDiscordButton && str_startswith(g_Config.m_XcLinkDiscord, "https://"))
	{
		Writer.WriteAttribute("buttons");
		Writer.BeginArray();
		Writer.BeginObject();
		Writer.WriteAttribute("label");
		Writer.WriteStrValue("Exora Client");
		Writer.WriteAttribute("url");
		Writer.WriteStrValue(g_Config.m_XcLinkDiscord);
		Writer.EndObject();
		Writer.EndArray();
	}
	Writer.EndObject();
	return Writer.GetOutputString();
}

void CXcDiscordRpc::OnRender()
{
	if(!g_Config.m_XcDiscord)
	{
		Stop();
		return;
	}
	const float Now = Client()->GlobalTime();
	if(Now < m_NextUpdate && m_Thread.joinable())
		return;
	m_NextUpdate = Now + 1.0f;

	std::string Activity = BuildActivity();
	{
		std::lock_guard<std::mutex> Lock(m_Mutex);
		if(m_AppId != g_Config.m_XcDiscordAppId)
		{
			m_AppId = g_Config.m_XcDiscordAppId;
			m_LastActivity.clear();
		}
		if(Activity != m_LastActivity)
		{
			m_LastActivity = Activity;
			m_Pending = std::move(Activity);
			m_HasPending = true;
		}
	}
	m_Wake.notify_all();
	Start();
}

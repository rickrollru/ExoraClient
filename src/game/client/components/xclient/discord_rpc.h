#ifndef GAME_CLIENT_COMPONENTS_XCLIENT_DISCORD_RPC_H
#define GAME_CLIENT_COMPONENTS_XCLIENT_DISCORD_RPC_H

#include <game/client/component.h>

#include <atomic>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

class IHttpRequest;

// Discord Rich Presence: "Playing Exora Client" with the server, the map and its picture in the
// Discord profile. Talks to the running Discord app over its local IPC pipe (no SDK, no library):
// a worker thread connects, shakes hands and sends the activity whenever it changes.
class CXcDiscordRpc : public CComponent
{
public:
	~CXcDiscordRpc() override;
	int Sizeof() const override { return sizeof(*this); }
	void OnRender() override;
	void OnShutdown() override;

	// for the settings card: is Discord connected right now
	bool Connected() const { return m_Connected; }

private:
	// the game side: builds the activity once a second
	float m_NextUpdate = 0.0f;
	std::string m_LastActivity;
	char m_aStartServer[128] = "";
	int64_t m_StartTime = 0;
	int m_LastState = -1;

	// the map picture from ddnet.org (the thumbnail field of the map info)
	char m_aThumbMap[128] = "";
	std::string m_Thumbnail;
	std::shared_ptr<IHttpRequest> m_pThumbTask;
	void UpdateThumbnail(const char *pMap);
	std::string BuildActivity();

	// the worker thread
	std::thread m_Thread;
	std::mutex m_Mutex;
	std::condition_variable m_Wake;
	bool m_Quit = false;
	std::string m_AppId;
	std::string m_Pending; // activity JSON waiting to be sent, "null" clears it
	bool m_HasPending = false;
	std::atomic<bool> m_Connected = false;
	void Start();
	void Stop();
	void Run();
};

#endif

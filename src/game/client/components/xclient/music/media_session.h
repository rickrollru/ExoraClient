#ifndef GAME_CLIENT_COMPONENTS_XCLIENT_MUSIC_MEDIA_SESSION_H
#define GAME_CLIENT_COMPONENTS_XCLIENT_MUSIC_MEDIA_SESSION_H

#include <condition_variable>
#include <cstdint>
#include <deque>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

// What the system says is playing right now: a browser tab with YouTube, SoundCloud or Yandex Music,
// Spotify, a desktop player... Windows: the system media sessions (the same as the volume overlay).
// Linux: MPRIS through playerctl. Everything runs in a worker thread, the game only takes snapshots.
struct SXcMediaState
{
	bool m_Available = false; // some player is there
	bool m_Playing = false;
	bool m_CanSeek = false;
	std::string m_Title;
	std::string m_Artist;
	std::string m_Album;
	std::string m_App; // "chrome.exe", "firefox", "Spotify"...
	double m_Position = -1.0; // seconds, -1 unknown
	double m_Duration = -1.0;
	double m_SampleTime = 0.0; // steady clock seconds when m_Position was true
	float m_Volume = -1.0f; // the volume of the player 0..1, -1 when it cannot be changed
	uint64_t m_TrackId = 0; // changes with every new track

	// cover art, decoded to RGBA in the worker
	uint64_t m_ArtId = 0;
	int m_ArtWidth = 0;
	int m_ArtHeight = 0;
	std::vector<unsigned char> m_vArtRgba;
};

class CXcMediaSession
{
public:
	enum ECommand
	{
		COMMAND_PLAY_PAUSE = 0,
		COMMAND_NEXT,
		COMMAND_PREVIOUS,
		COMMAND_SEEK, // to m_Seconds from the start of the track
		COMMAND_VOLUME, // to m_Seconds (0..1)
	};
	struct SCommand
	{
		ECommand m_Command;
		double m_Seconds;
	};

	CXcMediaSession() = default;
	~CXcMediaSession();
	CXcMediaSession(const CXcMediaSession &) = delete;
	CXcMediaSession &operator=(const CXcMediaSession &) = delete;

	static bool Supported();
	void Start();
	void Stop();
	bool Running() const { return m_Thread.joinable(); }

	// copies the state; the art is copied only when ArtId differs from KnownArtId
	void Snapshot(SXcMediaState *pState, uint64_t KnownArtId);
	void Send(ECommand Command, double Seconds = 0.0);

	static double Now();

private:
	std::thread m_Thread;
	std::mutex m_Mutex;
	std::condition_variable m_Wake;
	bool m_Quit = false;
	std::deque<SCommand> m_Commands;
	SXcMediaState m_State;

	void Run();
};

// platform parts (media_session_windows.cpp, media_session_linux.cpp)
namespace XcMediaPlatform
{
	bool Supported();
	void ThreadInit();
	void ThreadShutdown();
	// fills everything but the art; pArtBytes gets the encoded cover when WantArt is set and there is one
	bool Poll(SXcMediaState *pState, bool WantArt, std::vector<unsigned char> *pArtBytes);
	void Command(CXcMediaSession::ECommand Command, double Seconds);
	// how often to ask, in seconds
	double PollInterval();
}

// Windows: the volume of the app in the system mixer (media_volume_windows.cpp), found by the session app id
bool XcAppVolumeGet(const std::string &AppId, float *pVolume);
void XcAppVolumeSet(const std::string &AppId, float Volume);

#endif

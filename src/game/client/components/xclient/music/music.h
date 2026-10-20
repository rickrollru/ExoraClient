#ifndef GAME_CLIENT_COMPONENTS_XCLIENT_MUSIC_MUSIC_H
#define GAME_CLIENT_COMPONENTS_XCLIENT_MUSIC_MUSIC_H

#include "media_session.h"

#include <engine/console.h>
#include <engine/graphics.h>

#include <game/client/component.h>

// Music player: shows and controls what plays in the browser or a music app (see CXcMediaSession).
// The HUD widget and the card on the home page read from here.
class CXcMusic : public CComponent
{
public:
	int Sizeof() const override { return sizeof(*this); }
	void OnConsoleInit() override;
	void OnInit() override;
	void OnShutdown() override;
	void OnRender() override;

	bool Supported() const { return m_Supported; }
	bool Enabled() const;
	const SXcMediaState &State() const { return m_State; }
	bool HasTrack() const { return Enabled() && m_State.m_Available && !m_State.m_Title.empty(); }
	IGraphics::CTextureHandle Art() const { return m_Art; }
	// the position moves on smoothly between the polls; -1 when the player does not tell
	double Position() const;
	// seconds since the music was paused or stopped (0 while playing)
	float PausedFor() const { return m_PausedFor; }
	// "Chrome", "Firefox", "Spotify"... from the app id
	const char *AppName() const;
	// short text of the track for notifications: "Title — Artist"
	void TrackText(char *pBuf, int BufSize) const;

	void PlayPause();
	void Next();
	void Previous();
	// jump inside the track, when the player allows it (State().m_CanSeek)
	void Seek(double Seconds);

private:
	CXcMediaSession m_Session;
	SXcMediaState m_State;
	bool m_Supported = false;
	IGraphics::CTextureHandle m_Art;
	uint64_t m_ArtId = 0;
	uint64_t m_LastTrackId = 0;
	float m_PausedFor = 0.0f;
	// a play / pause press shows right away, before the player answers
	int m_PendingPlaying = -1;
	float m_PendingTime = 0.0f;
	// a seek shows right away too, until the player reports the new position
	double m_SeekTarget = -1.0;
	double m_SeekStart = 0.0;

	static void ConPlayPause(IConsole::IResult *pResult, void *pUserData);
	static void ConNext(IConsole::IResult *pResult, void *pUserData);
	static void ConPrevious(IConsole::IResult *pResult, void *pUserData);
};

#endif

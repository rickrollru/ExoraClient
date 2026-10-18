#include "music.h"

#include <base/str.h>

#include <engine/client.h>
#include <engine/image.h>
#include <engine/shared/config.h>

#include <game/client/components/xclient/ui/feedback.h>
#include <game/client/components/xclient/ui/lang.h>
#include <game/client/gameclient.h>

#include <algorithm>

using namespace XcUi;

void CXcMusic::OnConsoleInit()
{
	Console()->Register("xc_music_play_pause", "", CFGFLAG_CLIENT, ConPlayPause, this, "Music player: play or pause");
	Console()->Register("xc_music_next", "", CFGFLAG_CLIENT, ConNext, this, "Music player: next track");
	Console()->Register("xc_music_prev", "", CFGFLAG_CLIENT, ConPrevious, this, "Music player: previous track");
}

void CXcMusic::ConPlayPause(IConsole::IResult *pResult, void *pUserData)
{
	static_cast<CXcMusic *>(pUserData)->PlayPause();
}

void CXcMusic::ConNext(IConsole::IResult *pResult, void *pUserData)
{
	static_cast<CXcMusic *>(pUserData)->Next();
}

void CXcMusic::ConPrevious(IConsole::IResult *pResult, void *pUserData)
{
	static_cast<CXcMusic *>(pUserData)->Previous();
}

void CXcMusic::OnInit()
{
	m_Supported = CXcMediaSession::Supported();
}

void CXcMusic::OnShutdown()
{
	m_Session.Stop();
	Graphics()->UnloadTexture(&m_Art);
}

bool CXcMusic::Enabled() const
{
	return m_Supported && g_Config.m_XcMusic;
}

void CXcMusic::PlayPause()
{
	if(!Enabled())
		return;
	m_Session.Send(CXcMediaSession::COMMAND_PLAY_PAUSE);
	m_PendingPlaying = m_State.m_Playing ? 0 : 1;
	m_PendingTime = 1.5f;
}

void CXcMusic::Next()
{
	if(Enabled())
		m_Session.Send(CXcMediaSession::COMMAND_NEXT);
}

void CXcMusic::Previous()
{
	if(Enabled())
		m_Session.Send(CXcMediaSession::COMMAND_PREVIOUS);
}

double CXcMusic::Position() const
{
	if(m_State.m_Position < 0.0 || m_State.m_Duration <= 0.0)
		return -1.0;
	double Position = m_State.m_Position;
	if(m_State.m_Playing)
		Position += CXcMediaSession::Now() - m_State.m_SampleTime;
	return std::clamp(Position, 0.0, m_State.m_Duration);
}

const char *CXcMusic::AppName() const
{
	struct SApp
	{
		const char *m_pNeedle;
		const char *m_pName;
	};
	static const SApp s_aApps[] = {
		{"yandex", "Yandex"},
		{"msedge", "Edge"},
		{"edge", "Edge"},
		{"opera", "Opera"},
		{"brave", "Brave"},
		{"vivaldi", "Vivaldi"},
		{"chrom", "Chrome"},
		{"firefox", "Firefox"},
		{"spotify", "Spotify"},
		{"vlc", "VLC"},
		{"aimp", "AIMP"},
		{"foobar", "foobar2000"},
		{"zunemusic", "Media Player"},
		{"telegram", "Telegram"},
		{"discord", "Discord"},
	};
	for(const SApp &App : s_aApps)
	{
		if(str_find_nocase(m_State.m_App.c_str(), App.m_pNeedle))
			return App.m_pName;
	}
	return m_State.m_App.c_str();
}

void CXcMusic::TrackText(char *pBuf, int BufSize) const
{
	if(m_State.m_Artist.empty())
		str_copy(pBuf, m_State.m_Title.c_str(), BufSize);
	else
		str_format(pBuf, BufSize, "%s — %s", m_State.m_Title.c_str(), m_State.m_Artist.c_str());
}

void CXcMusic::OnRender()
{
	if(!Enabled())
	{
		if(m_Session.Running())
		{
			m_Session.Stop();
			m_State = SXcMediaState();
			Graphics()->UnloadTexture(&m_Art);
			m_ArtId = 0;
		}
		return;
	}
	if(!m_Session.Running())
		m_Session.Start();

	const float Dt = std::min(Client()->RenderFrameTime(), 0.1f);
	m_Session.Snapshot(&m_State, m_ArtId);
	if(m_PendingTime > 0.0f)
	{
		m_PendingTime -= Dt;
		if(m_PendingPlaying >= 0 && (m_State.m_Playing ? 1 : 0) != m_PendingPlaying)
			m_State.m_Playing = m_PendingPlaying == 1;
		else
			m_PendingTime = 0.0f;
	}
	m_PausedFor = m_State.m_Available && m_State.m_Playing ? 0.0f : m_PausedFor + Dt;

	// a new cover: upload it, the old one goes away
	if(m_State.m_ArtId != m_ArtId)
	{
		m_ArtId = m_State.m_ArtId;
		Graphics()->UnloadTexture(&m_Art);
		if(!m_State.m_vArtRgba.empty() && m_State.m_ArtWidth > 0)
		{
			CImageInfo Image;
			Image.m_Width = m_State.m_ArtWidth;
			Image.m_Height = m_State.m_ArtHeight;
			Image.m_Format = CImageInfo::FORMAT_RGBA;
			Image.Allocate();
			std::copy(m_State.m_vArtRgba.begin(), m_State.m_vArtRgba.end(), Image.m_pData);
			m_Art = Graphics()->LoadTextureRawMove(Image, 0, "xclient_music_art");
		}
	}

	// a short note when the next song starts
	if(m_State.m_TrackId != m_LastTrackId)
	{
		const bool First = m_LastTrackId == 0;
		m_LastTrackId = m_State.m_TrackId;
		if(!First && HasTrack() && m_State.m_Playing && g_Config.m_XcMusicToasts)
		{
			char aTrack[256];
			TrackText(aTrack, sizeof(aTrack));
			Toast(aTrack, TOAST_MUSIC);
		}
	}
}

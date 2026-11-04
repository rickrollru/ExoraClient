#include "music.h"

#include <base/str.h>

#include <engine/client.h>
#include <engine/demo.h>
#include <engine/gfx/image_loader.h>
#include <engine/image.h>
#include <engine/shared/config.h>
#include <engine/shared/packer.h>
#include <engine/shared/protocol.h>

#include <generated/protocol.h>

#include <game/client/components/xclient/ui/feedback.h>
#include <game/client/components/xclient/ui/lang.h>
#include <game/client/gameclient.h>

#include <algorithm>
#include <cmath>

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
	return (m_Supported || m_DemoActive) && g_Config.m_XcMusic;
}

void CXcMusic::PlayPause()
{
	if(!Enabled() || m_DemoActive)
		return;
	m_Session.Send(CXcMediaSession::COMMAND_PLAY_PAUSE);
	m_PendingPlaying = m_State.m_Playing ? 0 : 1;
	m_PendingTime = 1.5f;
}

void CXcMusic::Next()
{
	if(Enabled() && !m_DemoActive)
		m_Session.Send(CXcMediaSession::COMMAND_NEXT);
}

void CXcMusic::Previous()
{
	if(Enabled() && !m_DemoActive)
		m_Session.Send(CXcMediaSession::COMMAND_PREVIOUS);
}

void CXcMusic::Seek(double Seconds)
{
	if(!Enabled() || m_DemoActive || !m_State.m_CanSeek || m_State.m_Duration <= 0.0)
		return;
	Seconds = std::clamp(Seconds, 0.0, m_State.m_Duration);
	m_Session.Send(CXcMediaSession::COMMAND_SEEK, Seconds);
	m_SeekTarget = Seconds;
	m_SeekStart = CXcMediaSession::Now();
}

float CXcMusic::Volume() const
{
	if(m_State.m_Volume < 0.0f)
		return -1.0f;
	// the player answers with the new level after the next poll
	if(m_VolumeTarget >= 0.0f && CXcMediaSession::Now() - m_VolumeStart < 1.5)
		return m_VolumeTarget;
	return m_State.m_Volume;
}

void CXcMusic::SetVolume(float Volume)
{
	if(!Enabled() || m_DemoActive || m_State.m_Volume < 0.0f)
		return;
	m_VolumeTarget = std::clamp(Volume, 0.0f, 1.0f);
	m_VolumeStart = CXcMediaSession::Now();
	m_Session.Send(CXcMediaSession::COMMAND_VOLUME, m_VolumeTarget);
}

double CXcMusic::Position() const
{
	if(m_State.m_Position < 0.0 || m_State.m_Duration <= 0.0)
		return -1.0;
	if(m_DemoActive)
	{
		const double Passed = m_State.m_Playing ? std::max(DemoTime() - m_DemoPositionTime, 0.0) : 0.0;
		return std::clamp(m_DemoPosition + Passed, 0.0, m_State.m_Duration);
	}
	// right after a seek the player still reports the old place for a moment
	const double SinceSeek = CXcMediaSession::Now() - m_SeekStart;
	if(m_SeekTarget >= 0.0 && SinceSeek < 2.0 && m_State.m_SampleTime < m_SeekStart + 0.3)
		return std::clamp(m_SeekTarget + (m_State.m_Playing ? SinceSeek : 0.0), 0.0, m_State.m_Duration);
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
	// a demo with recorded music: its state comes from the demo messages
	if(m_DemoActive)
	{
		const float Dt = std::min(Client()->RenderFrameTime(), 0.1f);
		m_PausedFor = m_State.m_Available && m_State.m_Playing ? 0.0f : m_PausedFor + Dt;
		return;
	}
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
		// a small copy of the cover for demos (64 px, PNG)
		m_vCoverPng.clear();
		const int W = m_State.m_ArtWidth, H = m_State.m_ArtHeight;
		if(W > 0 && H > 0 && (int)m_State.m_vArtRgba.size() >= W * H * 4)
		{
			const int Size = 64;
			CImageInfo Small;
			Small.m_Width = Size;
			Small.m_Height = Size;
			Small.m_Format = CImageInfo::FORMAT_RGBA;
			Small.Allocate();
			// the middle square, each pixel the average of its area
			const int Side = std::min(W, H);
			const int X0 = (W - Side) / 2, Y0 = (H - Side) / 2;
			for(int y = 0; y < Size; y++)
			{
				for(int x = 0; x < Size; x++)
				{
					const int Ax = X0 + x * Side / Size, Bx = std::max(X0 + (x + 1) * Side / Size, Ax + 1);
					const int Ay = Y0 + y * Side / Size, By = std::max(Y0 + (y + 1) * Side / Size, Ay + 1);
					unsigned aSum[4] = {0, 0, 0, 0};
					for(int Py = Ay; Py < By; Py++)
						for(int Px = Ax; Px < Bx; Px++)
							for(int c = 0; c < 4; c++)
								aSum[c] += m_State.m_vArtRgba[((size_t)Py * W + Px) * 4 + c];
					const unsigned Count = (unsigned)((Bx - Ax) * (By - Ay));
					for(int c = 0; c < 4; c++)
						Small.m_pData[((size_t)y * Size + x) * 4 + c] = (unsigned char)(aSum[c] / Count);
				}
			}
			CByteBufferWriter Writer;
			if(CImageLoader::SavePng(Writer, Small))
				m_vCoverPng.assign(Writer.Data(), Writer.Data() + Writer.Size());
			Small.Free();
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

	RecordDemo();
}

// demos: the music goes in as a message of our own (other clients skip it), recorded only, never sent
void CXcMusic::SendDemo(CMsgPacker &Msg)
{
	Client()->SendMsg(g_Config.m_ClDummy, &Msg, MSGFLAG_RECORD | MSGFLAG_NOSEND);
}

void CXcMusic::RecordDemo()
{
	if(Client()->State() != IClient::STATE_ONLINE)
	{
		m_RecordMask = 0;
		return;
	}
	unsigned Mask = 0;
	for(int i = 0; i < RECORDER_MAX; i++)
	{
		if(Client()->DemoRecorder(i)->IsRecording())
			Mask |= 1u << i;
	}
	const bool NewRecorder = (Mask & ~m_RecordMask) != 0;
	m_RecordMask = Mask;
	if(!Mask)
		return;

	const bool Has = HasTrack();
	if(!Has && !m_RecordHad && !NewRecorder)
		return;
	const float Now = Client()->GlobalTime();
	bool Lyrics = false;
	const std::string Key = CXcLyrics::TrackKey(m_State.m_Artist, m_State.m_Title);
	const std::vector<CXcLyrics::SLine> *pLines = nullptr;
	bool Approx = false;
	if(Has)
		pLines = GameClient()->m_XcLyrics.FoundLines(Key, Approx);
	// everything again from time to time: the replay recorder keeps only the last seconds
	const bool Full = NewRecorder || m_State.m_TrackId != m_RecordTrack || Now - m_RecordFullTime >= 20.0f;
	const bool SendState = Full || Has != m_RecordHad || (m_State.m_Playing ? 1 : 0) != m_RecordPlaying || Now - m_RecordStateTime >= 1.0f;
	if(!SendState && m_State.m_ArtId == m_RecordArt && (pLines ? pLines->size() : 0) == m_RecordLyrics)
		return;

	if(SendState)
	{
		CMsgPacker Msg(NETMSGTYPE_SV_XCDEMOMUSIC, false, true);
		Msg.AddInt(DEMO_STATE);
		Msg.AddString(Has ? m_State.m_Title.c_str() : "", 256);
		Msg.AddString(Has ? m_State.m_Artist.c_str() : "", 256);
		Msg.AddString(Has ? m_State.m_App.c_str() : "", 64);
		Msg.AddInt(Has && m_State.m_Playing ? 1 : 0);
		const double TrackPos = Has ? Position() : -1.0;
		Msg.AddInt(TrackPos < 0.0 ? -1 : (int)std::lround(TrackPos * 1000.0));
		Msg.AddInt(Has && m_State.m_Duration > 0.0 ? (int)std::lround(m_State.m_Duration * 1000.0) : -1);
		SendDemo(Msg);
		m_RecordStateTime = Now;
		m_RecordHad = Has;
		m_RecordPlaying = m_State.m_Playing ? 1 : 0;
	}
	if(!Has)
		return;
	if(Full || m_State.m_ArtId != m_RecordArt)
	{
		m_RecordArt = m_State.m_ArtId;
		CMsgPacker Msg(NETMSGTYPE_SV_XCDEMOMUSIC, false, true);
		Msg.AddInt(DEMO_COVER);
		const int Size = m_vCoverPng.size() <= 40000 ? (int)m_vCoverPng.size() : 0;
		Msg.AddInt(Size);
		if(Size > 0)
			Msg.AddRaw(m_vCoverPng.data(), Size);
		SendDemo(Msg);
	}
	if(pLines && (Full || pLines->size() != m_RecordLyrics))
		Lyrics = true;
	m_RecordLyrics = pLines ? pLines->size() : 0;
	if(Lyrics)
	{
		// in parts, each well under the size of a demo chunk
		const int Total = (int)pLines->size();
		const int Part = 32;
		for(int First = 0; First < Total; First += Part)
		{
			CMsgPacker Msg(NETMSGTYPE_SV_XCDEMOMUSIC, false, true);
			Msg.AddInt(DEMO_LYRICS);
			Msg.AddString(m_State.m_Title.c_str(), 256);
			Msg.AddString(m_State.m_Artist.c_str(), 256);
			Msg.AddInt(Approx ? 1 : 0);
			Msg.AddInt(First);
			Msg.AddInt(Total);
			const int Count = std::min(Part, Total - First);
			Msg.AddInt(Count);
			for(int i = First; i < First + Count; i++)
			{
				Msg.AddInt((int)std::lround((*pLines)[i].m_Time * 1000.0));
				Msg.AddString((*pLines)[i].m_Text.c_str(), 256);
			}
			SendDemo(Msg);
		}
	}
	if(Full)
	{
		m_RecordFullTime = Now;
		m_RecordTrack = m_State.m_TrackId;
	}
}

double CXcMusic::DemoTime() const
{
	return (Client()->GameTick(0) + (double)Client()->IntraGameTick(0)) / Client()->GameTickSpeed();
}

void CXcMusic::LeaveDemo()
{
	if(!m_DemoActive)
		return;
	m_DemoActive = false;
	m_State = SXcMediaState();
	Graphics()->UnloadTexture(&m_Art);
	m_ArtId = 0;
	m_LastTrackId = 0;
	m_vDemoLyrics.clear();
}

void CXcMusic::OnStateChange(int NewState, int OldState)
{
	if(NewState != IClient::STATE_DEMOPLAYBACK || OldState != IClient::STATE_DEMOPLAYBACK)
		LeaveDemo();
	m_RecordMask = 0;
}

void CXcMusic::OnDemoMessage(CUnpacker *pUnpacker)
{
	const int Type = pUnpacker->GetInt();
	if(pUnpacker->Error())
		return;
	if(!m_DemoActive)
	{
		// the music that plays now goes away while the demo shows its own
		m_DemoActive = true;
		m_State = SXcMediaState();
		Graphics()->UnloadTexture(&m_Art);
		m_ArtId = 0;
		m_vDemoLyrics.clear();
	}
	if(Type == DEMO_STATE)
	{
		const std::string Title = pUnpacker->GetString();
		const std::string Artist = pUnpacker->GetString();
		const std::string App = pUnpacker->GetString();
		const int Playing = pUnpacker->GetInt();
		const int Position = pUnpacker->GetInt();
		const int Duration = pUnpacker->GetInt();
		if(pUnpacker->Error())
			return;
		const bool NewTrack = Title != m_State.m_Title || Artist != m_State.m_Artist;
		m_State.m_Available = !Title.empty();
		m_State.m_Playing = Playing != 0;
		m_State.m_CanSeek = false;
		m_State.m_Title = Title;
		m_State.m_Artist = Artist;
		m_State.m_App = App;
		m_State.m_Duration = Duration > 0 ? Duration / 1000.0 : -1.0;
		m_State.m_Position = Position >= 0 ? Position / 1000.0 : -1.0;
		m_State.m_Volume = -1.0f;
		m_DemoPosition = m_State.m_Position;
		m_DemoPositionTime = DemoTime();
		if(NewTrack)
		{
			m_State.m_TrackId++;
			m_LastTrackId = m_State.m_TrackId;
			Graphics()->UnloadTexture(&m_Art);
		}
	}
	else if(Type == DEMO_COVER)
	{
		const int Size = pUnpacker->GetInt();
		if(pUnpacker->Error() || Size <= 0 || Size > 40000)
			return;
		const unsigned char *pData = pUnpacker->GetRaw(Size);
		if(pUnpacker->Error() || !pData)
			return;
		// the same cover comes again every few seconds: it is loaded once per track
		if(m_Art.IsValid())
			return;
		CByteBufferReader Reader(pData, Size);
		CImageInfo Image;
		int PngliteIncompatible = 0;
		if(!CImageLoader::LoadPng(Reader, "demo music cover", Image, PngliteIncompatible))
			return;
		if(Image.m_Format != CImageInfo::FORMAT_RGBA)
		{
			Image.Free();
			return;
		}
		m_Art = Graphics()->LoadTextureRawMove(Image, 0, "xclient_music_art");
	}
	else if(Type == DEMO_LYRICS)
	{
		const std::string Title = pUnpacker->GetString();
		const std::string Artist = pUnpacker->GetString();
		const bool Approx = pUnpacker->GetInt() != 0;
		const int First = pUnpacker->GetInt();
		const int Total = pUnpacker->GetInt();
		const int Count = pUnpacker->GetInt();
		if(pUnpacker->Error() || First < 0 || Count < 0 || Total <= 0 || Total > 4000 || First + Count > Total)
			return;
		if(First == 0)
			m_vDemoLyrics.clear();
		if((int)m_vDemoLyrics.size() != First)
			return; // a part was missed (the demo was cut), wait for the next round
		for(int i = 0; i < Count; i++)
		{
			const int Time = pUnpacker->GetInt();
			const char *pText = pUnpacker->GetString();
			if(pUnpacker->Error())
				return;
			m_vDemoLyrics.emplace_back(Time / 1000.0, pText);
		}
		if((int)m_vDemoLyrics.size() == Total)
		{
			std::vector<CXcLyrics::SLine> vLines;
			vLines.reserve(m_vDemoLyrics.size());
			for(const auto &[Time, Text] : m_vDemoLyrics)
				vLines.push_back({Time, Text});
			GameClient()->m_XcLyrics.AddRecorded(CXcLyrics::TrackKey(Artist, Title), std::move(vLines), Approx);
			m_vDemoLyrics.clear();
		}
	}
}

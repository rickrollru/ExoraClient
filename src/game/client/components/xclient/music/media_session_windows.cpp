// Windows: the system media sessions (SMTC) through C++/WinRT. Every browser tab that plays
// sound with the Media Session API (YouTube, SoundCloud, Yandex Music...) and players like Spotify
// show up here, the same list as in the Windows volume overlay.
#include "media_session.h"

#include <base/detect.h>

#if defined(CONF_FAMILY_WINDOWS)

// set by CMake when a test program with the same calls builds and links
#if defined(CONF_XC_MEDIA_WINRT)
#define XC_MEDIA_WINRT 1
#endif

#if defined(XC_MEDIA_WINRT)

#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Media.Control.h>
#include <winrt/Windows.Storage.Streams.h>
#include <winrt/base.h>

#include <chrono>

using namespace winrt::Windows::Media::Control;
using namespace winrt::Windows::Storage::Streams;

using SessionManager = GlobalSystemMediaTransportControlsSessionManager;
using Session = GlobalSystemMediaTransportControlsSession;
using PlaybackStatus = GlobalSystemMediaTransportControlsSessionPlaybackStatus;

static SessionManager gs_Manager{nullptr};

bool XcMediaPlatform::Supported()
{
	return true;
}

void XcMediaPlatform::ThreadInit()
{
	try
	{
		winrt::init_apartment(winrt::apartment_type::multi_threaded);
	}
	catch(...)
	{
	}
}

void XcMediaPlatform::ThreadShutdown()
{
	gs_Manager = nullptr;
	winrt::uninit_apartment();
}

double XcMediaPlatform::PollInterval()
{
	return 0.5;
}

static Session CurrentSession()
{
	if(!gs_Manager)
		gs_Manager = SessionManager::RequestAsync().get();
	return gs_Manager.GetCurrentSession();
}

static double Seconds(winrt::Windows::Foundation::TimeSpan Time)
{
	return std::chrono::duration<double>(Time).count();
}

bool XcMediaPlatform::Poll(SXcMediaState *pState, bool WantArt, std::vector<unsigned char> *pArtBytes)
{
	try
	{
		Session Current = CurrentSession();
		if(!Current)
		{
			pState->m_Available = false;
			return true;
		}
		const auto Properties = Current.TryGetMediaPropertiesAsync().get();
		pState->m_Available = true;
		pState->m_Title = winrt::to_string(Properties.Title());
		pState->m_Artist = winrt::to_string(Properties.Artist());
		pState->m_Album = winrt::to_string(Properties.AlbumTitle());
		pState->m_App = winrt::to_string(Current.SourceAppUserModelId());
		XcAppVolumeGet(pState->m_App, &pState->m_Volume);
		const auto Playback = Current.GetPlaybackInfo();
		pState->m_Playing = Playback.PlaybackStatus() == PlaybackStatus::Playing;
		pState->m_CanSeek = Playback.Controls().IsPlaybackPositionEnabled();

		// browsers report the position only now and then; it was true at LastUpdatedTime
		const auto Timeline = Current.GetTimelineProperties();
		const double Duration = Seconds(Timeline.EndTime() - Timeline.StartTime());
		if(Duration > 0.5)
		{
			double Position = Seconds(Timeline.Position() - Timeline.StartTime());
			const double Since = Seconds(winrt::clock::now() - Timeline.LastUpdatedTime());
			if(pState->m_Playing && Since > 0.0 && Since < 24.0 * 3600.0)
				Position += Since;
			pState->m_Duration = Duration;
			pState->m_Position = Position < Duration ? Position : Duration;
			pState->m_SampleTime = CXcMediaSession::Now();
		}

		if(WantArt)
		{
			const auto Thumbnail = Properties.Thumbnail();
			if(Thumbnail)
			{
				const auto Stream = Thumbnail.OpenReadAsync().get();
				const uint64_t Size = Stream.Size();
				if(Size > 0 && Size < 8 * 1024 * 1024)
				{
					Buffer Data((uint32_t)Size);
					const auto Result = Stream.ReadAsync(Data, (uint32_t)Size, InputStreamOptions::ReadAhead).get();
					const DataReader Reader = DataReader::FromBuffer(Result);
					pArtBytes->resize(Result.Length());
					Reader.ReadBytes(*pArtBytes);
				}
			}
		}
		return true;
	}
	catch(...)
	{
		// the session went away in the middle, ask the manager again next time
		gs_Manager = nullptr;
		return false;
	}
}

void XcMediaPlatform::Command(CXcMediaSession::ECommand Command, double Seconds)
{
	try
	{
		Session Current = CurrentSession();
		if(!Current)
			return;
		switch(Command)
		{
		case CXcMediaSession::COMMAND_NEXT: Current.TrySkipNextAsync().get(); break;
		case CXcMediaSession::COMMAND_PREVIOUS: Current.TrySkipPreviousAsync().get(); break;
		case CXcMediaSession::COMMAND_SEEK:
		{
			// the position is in 100 ns ticks on the timeline of the session
			const auto Timeline = Current.GetTimelineProperties();
			const int64_t Ticks = Timeline.StartTime().count() + (int64_t)(Seconds * 10000000.0);
			Current.TryChangePlaybackPositionAsync(Ticks).get();
			break;
		}
		case CXcMediaSession::COMMAND_VOLUME: XcAppVolumeSet(winrt::to_string(Current.SourceAppUserModelId()), (float)Seconds); break;
		default: Current.TryTogglePlayPauseAsync().get(); break;
		}
	}
	catch(...)
	{
		gs_Manager = nullptr;
	}
}

#else

// built without C++/WinRT (MinGW, an old Windows SDK): no music player
bool XcMediaPlatform::Supported() { return false; }
void XcMediaPlatform::ThreadInit() {}
void XcMediaPlatform::ThreadShutdown() {}
double XcMediaPlatform::PollInterval() { return 5.0; }
bool XcMediaPlatform::Poll(SXcMediaState *pState, bool WantArt, std::vector<unsigned char> *pArtBytes)
{
	(void)WantArt;
	(void)pArtBytes;
	pState->m_Available = false;
	return true;
}
void XcMediaPlatform::Command(CXcMediaSession::ECommand Command, double Seconds)
{
	(void)Command;
	(void)Seconds;
}

#endif
#endif

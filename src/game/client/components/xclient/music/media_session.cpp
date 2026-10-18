#include "media_session.h"

#include <engine/external/stb/stb_image.h>
#include <engine/gfx/image_manipulation.h>

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <functional>

// covers are small on screen
static constexpr int MAX_ART_SIZE = 256;

CXcMediaSession::~CXcMediaSession()
{
	Stop();
}

bool CXcMediaSession::Supported()
{
	return XcMediaPlatform::Supported();
}

double CXcMediaSession::Now()
{
	return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

void CXcMediaSession::Start()
{
	if(m_Thread.joinable() || !Supported())
		return;
	m_Quit = false;
	m_Thread = std::thread([this]() { Run(); });
}

void CXcMediaSession::Stop()
{
	if(!m_Thread.joinable())
		return;
	{
		std::lock_guard<std::mutex> Lock(m_Mutex);
		m_Quit = true;
	}
	m_Wake.notify_all();
	m_Thread.join();
	std::lock_guard<std::mutex> Lock(m_Mutex);
	m_State = SXcMediaState();
	m_Commands.clear();
}

void CXcMediaSession::Snapshot(SXcMediaState *pState, uint64_t KnownArtId)
{
	std::lock_guard<std::mutex> Lock(m_Mutex);
	std::vector<unsigned char> vKeep;
	if(m_State.m_ArtId == KnownArtId)
		vKeep.swap(pState->m_vArtRgba); // the caller already has these pixels
	const bool SkipArt = m_State.m_ArtId == KnownArtId;
	pState->m_Available = m_State.m_Available;
	pState->m_Playing = m_State.m_Playing;
	pState->m_CanSeek = m_State.m_CanSeek;
	pState->m_Title = m_State.m_Title;
	pState->m_Artist = m_State.m_Artist;
	pState->m_Album = m_State.m_Album;
	pState->m_App = m_State.m_App;
	pState->m_Position = m_State.m_Position;
	pState->m_Duration = m_State.m_Duration;
	pState->m_SampleTime = m_State.m_SampleTime;
	pState->m_TrackId = m_State.m_TrackId;
	pState->m_ArtId = m_State.m_ArtId;
	pState->m_ArtWidth = m_State.m_ArtWidth;
	pState->m_ArtHeight = m_State.m_ArtHeight;
	if(SkipArt)
		pState->m_vArtRgba.swap(vKeep);
	else
		pState->m_vArtRgba = m_State.m_vArtRgba;
}

void CXcMediaSession::Send(ECommand Command)
{
	{
		std::lock_guard<std::mutex> Lock(m_Mutex);
		if(m_Commands.size() < 8)
			m_Commands.push_back(Command);
	}
	m_Wake.notify_all();
}

static bool DecodeArt(const std::vector<unsigned char> &vBytes, std::vector<unsigned char> *pRgba, int *pWidth, int *pHeight)
{
	int Width, Height, Comp;
	stbi_uc *pPixels = stbi_load_from_memory(vBytes.data(), (int)vBytes.size(), &Width, &Height, &Comp, 4);
	if(!pPixels)
		return false;
	// a square crop from the middle: video thumbnails are 16:9, covers are square
	const int Side = std::min(Width, Height);
	const int X0 = (Width - Side) / 2;
	const int Y0 = (Height - Side) / 2;
	std::vector<unsigned char> vSquare((size_t)Side * Side * 4);
	for(int y = 0; y < Side; y++)
		std::memcpy(&vSquare[(size_t)y * Side * 4], &pPixels[((size_t)(y + Y0) * Width + X0) * 4], (size_t)Side * 4);
	stbi_image_free(pPixels);
	const int Target = std::min(Side, MAX_ART_SIZE);
	if(Target != Side)
	{
		uint8_t *pResized = ResizeImage(vSquare.data(), Side, Side, Target, Target, 4);
		pRgba->assign(pResized, pResized + (size_t)Target * Target * 4);
		free(pResized);
	}
	else
		pRgba->swap(vSquare);
	*pWidth = Target;
	*pHeight = Target;
	return true;
}

void CXcMediaSession::Run()
{
	XcMediaPlatform::ThreadInit();
	std::string LastKey;
	uint64_t TrackId = 0;
	uint64_t ArtId = 0;
	int ArtTries = 0;
	bool HaveArt = false;
	while(true)
	{
		// commands first, then a fresh look so the button press shows right away
		std::deque<ECommand> Commands;
		{
			std::lock_guard<std::mutex> Lock(m_Mutex);
			if(m_Quit)
				break;
			Commands.swap(m_Commands);
		}
		for(ECommand Command : Commands)
			XcMediaPlatform::Command(Command);

		SXcMediaState State;
		std::vector<unsigned char> vArtBytes;
		// browsers often send the cover a moment after the title, so ask a few times
		const bool WantArt = !HaveArt && ArtTries < 6;
		const bool Ok = XcMediaPlatform::Poll(&State, WantArt, &vArtBytes);
		if(!Ok)
			State = SXcMediaState();
		const std::string Key = State.m_Available ? State.m_App + "\n" + State.m_Title + "\n" + State.m_Artist : std::string();
		if(Key != LastKey)
		{
			LastKey = Key;
			TrackId++;
			HaveArt = false;
			ArtTries = 0;
		}
		State.m_TrackId = TrackId;

		std::vector<unsigned char> vRgba;
		int ArtWidth = 0, ArtHeight = 0;
		bool NewArt = false;
		if(WantArt && State.m_Available)
		{
			ArtTries++;
			if(!vArtBytes.empty() && DecodeArt(vArtBytes, &vRgba, &ArtWidth, &ArtHeight))
			{
				HaveArt = true;
				NewArt = true;
				ArtId++;
			}
		}

		{
			std::lock_guard<std::mutex> Lock(m_Mutex);
			const bool TrackChanged = m_State.m_TrackId != State.m_TrackId;
			std::vector<unsigned char> vOldArt;
			vOldArt.swap(m_State.m_vArtRgba);
			const int OldWidth = m_State.m_ArtWidth, OldHeight = m_State.m_ArtHeight;
			const uint64_t OldArtId = m_State.m_ArtId;
			m_State = State;
			if(NewArt)
			{
				m_State.m_vArtRgba.swap(vRgba);
				m_State.m_ArtWidth = ArtWidth;
				m_State.m_ArtHeight = ArtHeight;
				m_State.m_ArtId = ArtId;
			}
			else if(!TrackChanged)
			{
				m_State.m_vArtRgba.swap(vOldArt);
				m_State.m_ArtWidth = OldWidth;
				m_State.m_ArtHeight = OldHeight;
				m_State.m_ArtId = OldArtId;
			}
			else
			{
				// a new track without a cover yet: no picture rather than the old one
				ArtId++;
				m_State.m_ArtId = ArtId;
			}
		}

		std::unique_lock<std::mutex> Lock(m_Mutex);
		m_Wake.wait_for(Lock, std::chrono::duration<double>(XcMediaPlatform::PollInterval()), [this]() { return m_Quit || !m_Commands.empty(); });
		if(m_Quit)
			break;
	}
	XcMediaPlatform::ThreadShutdown();
}

// Windows: the volume of the app that plays the music, the same slider as in the system volume mixer.
// The media sessions (SMTC) have no volume, so the audio session of the app's process is found by
// its exe name: "chrome.exe" for the session "Chrome", "spotify.exe" for "SpotifyAB.SpotifyMusic_...".
#include "media_session.h"

#include <base/detect.h>

#if defined(CONF_FAMILY_WINDOWS)

// the project builds with NOGDI, but the audio headers (mmreg.h) need the GDI bitmap types
#undef NOGDI
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include <audiopolicy.h>
#include <mmdeviceapi.h>
#include <wrl/client.h>

#include <algorithm>
#include <cctype>
#include <iterator>
#include <string>
#include <vector>

using Microsoft::WRL::ComPtr;

static std::string Lower(std::string Text)
{
	std::transform(Text.begin(), Text.end(), Text.begin(), [](unsigned char c) { return (char)std::tolower(c); });
	return Text;
}

// "chrome" for C:\...\chrome.exe
static std::string ExeStem(DWORD ProcessId)
{
	HANDLE Process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, ProcessId);
	if(!Process)
		return std::string();
	wchar_t aPath[MAX_PATH];
	DWORD Size = MAX_PATH;
	std::string Stem;
	if(QueryFullProcessImageNameW(Process, 0, aPath, &Size))
	{
		std::wstring Path(aPath, Size);
		const size_t Slash = Path.find_last_of(L"\\/");
		if(Slash != std::wstring::npos)
			Path = Path.substr(Slash + 1);
		const size_t Dot = Path.find_last_of(L'.');
		if(Dot != std::wstring::npos)
			Path = Path.substr(0, Dot);
		for(wchar_t c : Path)
			Stem += c < 128 ? (char)std::tolower((int)c) : '_';
	}
	CloseHandle(Process);
	return Stem;
}

// the letters and digits only: "yandex music" and "Yandex.Music" both become "yandexmusic"
static std::string Squash(const std::string &Text)
{
	std::string Out;
	for(char c : Text)
		if(std::isalnum((unsigned char)c))
			Out += c;
	return Out;
}

// the words of a name, "ru.yandex.desktop.music" -> ru, yandex, desktop, music
static std::vector<std::string> Words(const std::string &Text)
{
	std::vector<std::string> vWords;
	std::string Word;
	for(char c : Text + ".")
	{
		if(std::isalnum((unsigned char)c))
			Word += c;
		else if(!Word.empty())
		{
			vWords.push_back(Word);
			Word.clear();
		}
	}
	return vWords;
}

// does the media session app id belong to this exe
static bool AppMatches(const std::string &App, const std::string &Stem)
{
	if(Stem.empty())
		return false;
	if(App.find(Stem) != std::string::npos)
		return true;
	const std::string SquashedStem = Squash(Stem);
	if(SquashedStem.size() >= 3 && Squash(App).find(SquashedStem) != std::string::npos)
		return true;
	// apps whose session id does not carry the exe name
	static const char *const s_aaKnown[][2] = {
		{"308046b0af4a39cb", "firefox"}, // Firefox
		{"6f193ccc56814779", "firefox"}, // Firefox Developer Edition
		{"yandex", "browser"}, // Yandex Browser
		{"opera", "launcher"},
		{"msedge", "msedgewebview2"},
		{"applemusic", "applemusic"},
		{"zunemusic", "music.ui"}, // Media Player
		{"zunemusic", "microsoft.media.player"},
	};
	for(const auto &aKnown : s_aaKnown)
		if(App.find(aKnown[0]) != std::string::npos && Stem == aKnown[1])
			return true;
	// a distinctive word of the exe name in the app id ("yandexmusic.exe" for "ru.yandex.desktop.music" does
	// not match, "yandex music.exe" does by "yandex")
	static const char *const s_apGeneric[] = {"music", "player", "desktop", "app", "media", "microsoft", "windows", "client", "launcher", "update"};
	for(const std::string &Word : Words(Stem))
	{
		if(Word.size() < 4 || std::find_if(std::begin(s_apGeneric), std::end(s_apGeneric), [&](const char *p) { return Word == p; }) != std::end(s_apGeneric))
			continue;
		if(App.find(Word) != std::string::npos)
			return true;
	}
	return false;
}

// calls Fn for every audio session of the app on the default output device; false when there is none
template<typename F>
static bool ForEachAppSession(const std::string &AppId, F &&Fn)
{
	const std::string App = Lower(AppId);
	if(App.empty())
		return false;
	ComPtr<IMMDeviceEnumerator> pEnumerator;
	if(FAILED(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL, IID_PPV_ARGS(&pEnumerator))))
		return false;
	ComPtr<IMMDevice> pDevice;
	if(FAILED(pEnumerator->GetDefaultAudioEndpoint(eRender, eMultimedia, &pDevice)))
		return false;
	ComPtr<IAudioSessionManager2> pManager;
	if(FAILED(pDevice->Activate(__uuidof(IAudioSessionManager2), CLSCTX_ALL, nullptr, (void **)pManager.GetAddressOf())))
		return false;
	ComPtr<IAudioSessionEnumerator> pSessions;
	if(FAILED(pManager->GetSessionEnumerator(&pSessions)))
		return false;
	int Count = 0;
	pSessions->GetCount(&Count);
	bool Found = false;
	// when no session carries the app's name: the app that plays sound right now, if it is the only one
	std::vector<ComPtr<ISimpleAudioVolume>> vpPlaying;
	std::string PlayingStem;
	bool PlayingSeveral = false;
	for(int i = 0; i < Count; i++)
	{
		ComPtr<IAudioSessionControl> pControl;
		ComPtr<IAudioSessionControl2> pControl2;
		if(FAILED(pSessions->GetSession(i, &pControl)) || FAILED(pControl.As(&pControl2)))
			continue;
		DWORD ProcessId = 0;
		if(FAILED(pControl2->GetProcessId(&ProcessId)) || ProcessId == 0 || ProcessId == GetCurrentProcessId())
			continue;
		const std::string Stem = ExeStem(ProcessId);
		ComPtr<ISimpleAudioVolume> pVolume;
		if(FAILED(pControl.As(&pVolume)))
			continue;
		if(AppMatches(App, Stem))
		{
			Found = true;
			Fn(pVolume.Get());
			continue;
		}
		AudioSessionState SessionState;
		if(Stem.empty() || FAILED(pControl->GetState(&SessionState)) || SessionState != AudioSessionStateActive)
			continue;
		if(!PlayingStem.empty() && PlayingStem != Stem)
			PlayingSeveral = true;
		PlayingStem = Stem;
		vpPlaying.push_back(pVolume);
	}
	if(!Found && !PlayingSeveral)
	{
		for(auto &pVolume : vpPlaying)
			Fn(pVolume.Get());
		Found = !vpPlaying.empty();
	}
	return Found;
}

bool XcAppVolumeGet(const std::string &AppId, float *pVolume)
{
	float Max = -1.0f;
	ForEachAppSession(AppId, [&](ISimpleAudioVolume *pVolumeControl) {
		float Level;
		if(SUCCEEDED(pVolumeControl->GetMasterVolume(&Level)))
			Max = std::max(Max, Level);
	});
	*pVolume = Max;
	return Max >= 0.0f;
}

void XcAppVolumeSet(const std::string &AppId, float Volume)
{
	Volume = std::clamp(Volume, 0.0f, 1.0f);
	// a browser plays from several processes: all of them get the same level
	ForEachAppSession(AppId, [&](ISimpleAudioVolume *pVolumeControl) {
		pVolumeControl->SetMasterVolume(Volume, nullptr);
	});
}

#endif

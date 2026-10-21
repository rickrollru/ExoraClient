// Linux (and other unix systems): MPRIS players through the playerctl tool. Browsers expose
// YouTube, SoundCloud, Yandex Music and others over MPRIS on their own.
#include "media_session.h"

#include <base/detect.h>

#if !defined(CONF_FAMILY_WINDOWS)

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#if defined(CONF_PLATFORM_LINUX) || defined(CONF_PLATFORM_FREEBSD) || defined(CONF_PLATFORM_OPENBSD) || defined(CONF_PLATFORM_NETBSD)
#define XC_MEDIA_PLAYERCTL 1
#endif

#if defined(XC_MEDIA_PLAYERCTL)

// output of a fixed command, at most MaxSize bytes
static bool RunCommand(const char *pCommand, std::string *pOutput, size_t MaxSize = 1 << 16)
{
	pOutput->clear();
	FILE *pPipe = popen(pCommand, "r");
	if(!pPipe)
		return false;
	char aBuf[4096];
	size_t Read;
	while((Read = fread(aBuf, 1, sizeof(aBuf), pPipe)) > 0)
	{
		if(pOutput->size() + Read > MaxSize)
		{
			pOutput->clear();
			pclose(pPipe);
			return false;
		}
		pOutput->append(aBuf, Read);
	}
	return pclose(pPipe) == 0;
}

static int gs_Supported = -1;

bool XcMediaPlatform::Supported()
{
	if(gs_Supported < 0)
	{
		std::string Output;
		gs_Supported = RunCommand("playerctl --version 2>/dev/null", &Output) && !Output.empty() ? 1 : 0;
	}
	return gs_Supported == 1;
}

void XcMediaPlatform::ThreadInit() {}
void XcMediaPlatform::ThreadShutdown() {}

double XcMediaPlatform::PollInterval()
{
	return 1.0;
}

static std::string UrlDecode(const std::string &In)
{
	std::string Out;
	for(size_t i = 0; i < In.size(); i++)
	{
		if(In[i] == '%' && i + 2 < In.size())
		{
			const char aHex[3] = {In[i + 1], In[i + 2], '\0'};
			Out += (char)std::strtol(aHex, nullptr, 16);
			i += 2;
		}
		else
			Out += In[i];
	}
	return Out;
}

static void ReadArt(const std::string &Url, std::vector<unsigned char> *pBytes)
{
	constexpr size_t MAX_ART_BYTES = 8 * 1024 * 1024;
	if(Url.rfind("file://", 0) == 0)
	{
		const std::string Path = UrlDecode(Url.substr(7));
		FILE *pFile = fopen(Path.c_str(), "rb");
		if(!pFile)
			return;
		unsigned char aBuf[8192];
		size_t Read;
		while((Read = fread(aBuf, 1, sizeof(aBuf), pFile)) > 0 && pBytes->size() < MAX_ART_BYTES)
			pBytes->insert(pBytes->end(), aBuf, aBuf + Read);
		fclose(pFile);
		if(pBytes->size() >= MAX_ART_BYTES)
			pBytes->clear();
	}
	else if(Url.rfind("https://", 0) == 0 || Url.rfind("http://", 0) == 0)
	{
		// only plain URL characters go to the shell
		for(char c : Url)
		{
			if(!(std::isalnum((unsigned char)c) || std::strchr(":/._-~%?=&+,@", c)))
				return;
		}
		std::string Output;
		const std::string Command = "curl -sfL --max-time 4 --max-filesize 8000000 '" + Url + "' 2>/dev/null";
		if(RunCommand(Command.c_str(), &Output, MAX_ART_BYTES))
			pBytes->assign(Output.begin(), Output.end());
	}
}

bool XcMediaPlatform::Poll(SXcMediaState *pState, bool WantArt, std::vector<unsigned char> *pArtBytes)
{
	std::string Output;
	// one line, tab separated; times are microseconds
	RunCommand("playerctl metadata --format '{{status}}\t{{playerName}}\t{{xesam:title}}\t{{xesam:artist}}\t{{xesam:album}}\t{{position}}\t{{mpris:length}}\t{{mpris:artUrl}}\t{{volume}}' 2>/dev/null", &Output);
	while(!Output.empty() && (Output.back() == '\n' || Output.back() == '\r'))
		Output.pop_back();
	std::vector<std::string> vFields;
	size_t Start = 0;
	while(true)
	{
		const size_t Tab = Output.find('\t', Start);
		vFields.push_back(Output.substr(Start, Tab == std::string::npos ? std::string::npos : Tab - Start));
		if(Tab == std::string::npos)
			break;
		Start = Tab + 1;
	}
	if(vFields.size() < 8 || (vFields[0] != "Playing" && vFields[0] != "Paused"))
	{
		pState->m_Available = false;
		return true;
	}
	pState->m_Available = true;
	pState->m_Playing = vFields[0] == "Playing";
	pState->m_App = vFields[1];
	pState->m_Title = vFields[2];
	pState->m_Artist = vFields[3];
	pState->m_Album = vFields[4];
	if(!vFields[6].empty() && std::atoll(vFields[6].c_str()) > 0)
	{
		pState->m_Duration = std::atoll(vFields[6].c_str()) / 1000000.0;
		pState->m_Position = std::atoll(vFields[5].c_str()) / 1000000.0;
		pState->m_SampleTime = CXcMediaSession::Now();
		pState->m_CanSeek = true; // MPRIS players take "playerctl position"
	}
	if(vFields.size() >= 9 && !vFields[8].empty())
		pState->m_Volume = std::clamp((float)std::atof(vFields[8].c_str()), 0.0f, 1.0f);
	if(WantArt && !vFields[7].empty())
		ReadArt(vFields[7], pArtBytes);
	return true;
}

void XcMediaPlatform::Command(CXcMediaSession::ECommand Command, double Seconds)
{
	if(Command == CXcMediaSession::COMMAND_VOLUME)
	{
		char aVolume[64];
		std::snprintf(aVolume, sizeof(aVolume), "playerctl volume %.2f 2>/dev/null", std::clamp(Seconds, 0.0, 1.0));
		std::string Output;
		RunCommand(aVolume, &Output);
		return;
	}
	if(Command == CXcMediaSession::COMMAND_SEEK)
	{
		char aSeek[64];
		std::snprintf(aSeek, sizeof(aSeek), "playerctl position %.2f 2>/dev/null", Seconds < 0.0 ? 0.0 : Seconds);
		std::string Output;
		RunCommand(aSeek, &Output);
		return;
	}
	const char *pCommand = Command == CXcMediaSession::COMMAND_NEXT ? "playerctl next 2>/dev/null" : (Command == CXcMediaSession::COMMAND_PREVIOUS ? "playerctl previous 2>/dev/null" : "playerctl play-pause 2>/dev/null");
	std::string Output;
	RunCommand(pCommand, &Output);
}

#else

// no media session support on this system (macOS, Android, web)
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

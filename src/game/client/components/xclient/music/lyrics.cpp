#include "lyrics.h"

#include "music.h"

#include <base/math.h>
#include <base/str.h>

#include <engine/client.h>
#include <engine/graphics.h>
#include <engine/http.h>
#include <engine/shared/config.h>
#include <engine/shared/json.h>
#include <engine/textrender.h>

#include <game/client/components/xclient/xc_helpers.h>
#include <game/client/gameclient.h>

#include <algorithm>
#include <cmath>

using namespace XcHelpers;

// the words on the map: how long one stays, its size in world units
static constexpr float WORD_LIFE = 2.6f;
static constexpr float WORD_SIZE = 22.0f;

// LRCLIB asks the programs that use it to say who they are
static const char *const LRCLIB_AGENT = "ExoraClient (https://github.com/rickrollru/ExoraClient)";

std::vector<CXcLyrics::SLine> CXcLyrics::ParseLrc(const char *pLrc)
{
	std::vector<SLine> vLines;
	const char *p = pLrc;
	while(p && *p)
	{
		const char *pEnd = str_find(p, "\n");
		std::string Line(p, pEnd ? (size_t)(pEnd - p) : str_length(p));
		p = pEnd ? pEnd + 1 : nullptr;
		if(!Line.empty() && Line.back() == '\r')
			Line.pop_back();
		// all the times in front, then the text
		std::vector<double> vTimes;
		size_t Pos = 0;
		while(Pos < Line.size() && Line[Pos] == '[')
		{
			const size_t Close = Line.find(']', Pos);
			if(Close == std::string::npos)
				break;
			const std::string Tag = Line.substr(Pos + 1, Close - Pos - 1);
			int Minutes = 0;
			float Seconds = 0.0f;
			// tags like [ar:...] or [length: 3:20] have letters: not a time
			if(!Tag.empty() && Tag[0] >= '0' && Tag[0] <= '9' && sscanf(Tag.c_str(), "%d:%f", &Minutes, &Seconds) == 2)
				vTimes.push_back(Minutes * 60.0 + Seconds);
			Pos = Close + 1;
		}
		std::string Text = Line.substr(Pos);
		while(!Text.empty() && Text[0] == ' ')
			Text.erase(0, 1);
		for(double Time : vTimes)
			vLines.push_back({Time, Text});
	}
	std::stable_sort(vLines.begin(), vLines.end(), [](const SLine &a, const SLine &b) { return a.m_Time < b.m_Time; });
	return vLines;
}

void CXcLyrics::CleanTitle(const std::string &Artist, const std::string &Title, std::string &OutArtist, std::string &OutTitle)
{
	std::string Clean;
	// whatever is in brackets: (Official Video), [4K], (Lyrics)...
	int Depth = 0;
	for(char c : Title)
	{
		if(c == '(' || c == '[')
			Depth++;
		else if((c == ')' || c == ']') && Depth > 0)
			Depth--;
		else if(Depth == 0)
			Clean += c;
	}
	OutArtist = Artist;
	// the channels of YouTube: "Artist - Topic", "ArtistVEVO"
	bool Channel = false;
	for(const char *pSuffix : {" - Topic", "VEVO", "Vevo"})
		if(OutArtist.size() > (size_t)str_length(pSuffix) && OutArtist.compare(OutArtist.size() - str_length(pSuffix), std::string::npos, pSuffix) == 0)
		{
			OutArtist.erase(OutArtist.size() - str_length(pSuffix));
			Channel = true;
		}
	// "Artist - Title" in one line, as the browser tabs of YouTube have it
	const size_t Dash = Clean.find(" - ");
	if(Dash != std::string::npos)
	{
		const std::string Before = Clean.substr(0, Dash);
		// the channel of YouTube is often "Artist - Topic" or "ArtistVEVO": the part of the title is better
		std::string BeforeBare, ArtistBare;
		for(char c : Before)
			if(c != ' ')
				BeforeBare += c;
		for(char c : OutArtist)
			if(c != ' ')
				ArtistBare += c;
		if(OutArtist.empty() || Channel || str_find_nocase(BeforeBare.c_str(), ArtistBare.c_str()) || str_find_nocase(ArtistBare.c_str(), BeforeBare.c_str()))
			OutArtist = Before;
		Clean = Clean.substr(Dash + 3);
	}
	// "feat. Someone" is not part of the title in the database
	for(const char *pFeat : {" feat.", " ft.", " Feat.", " Ft."})
	{
		const size_t At = Clean.find(pFeat);
		if(At != std::string::npos)
			Clean = Clean.substr(0, At);
	}
	const auto Trim = [](std::string &s) {
		while(!s.empty() && (s.back() == ' ' || s.back() == '-' || s.back() == '|'))
			s.pop_back();
		while(!s.empty() && s.front() == ' ')
			s.erase(0, 1);
	};
	Trim(Clean);
	Trim(OutArtist);
	OutTitle = Clean;
}

void CXcLyrics::OnShutdown()
{
	if(m_pRequest)
		m_pRequest->Abort();
	m_pRequest = nullptr;
}

void CXcLyrics::Request(bool Search)
{
	char aArtist[256], aTitle[256], aUrl[768];
	EscapeUrl(aArtist, m_Artist.c_str());
	EscapeUrl(aTitle, m_Title.c_str());
	if(Search)
		str_format(aUrl, sizeof(aUrl), "https://lrclib.net/api/search?track_name=%s&artist_name=%s", aTitle, aArtist);
	else if(m_Duration > 0.0)
		str_format(aUrl, sizeof(aUrl), "https://lrclib.net/api/get?track_name=%s&artist_name=%s&duration=%d", aTitle, aArtist, (int)std::round(m_Duration));
	else
		str_format(aUrl, sizeof(aUrl), "https://lrclib.net/api/get?track_name=%s&artist_name=%s", aTitle, aArtist);
	m_pRequest = HttpGet(aUrl);
	m_pRequest->HeaderString("User-Agent", LRCLIB_AGENT);
	m_pRequest->Timeout(CTimeout{10000, 0, 500, 10});
	m_pRequest->MaxResponseSize(2 * 1024 * 1024);
	m_pRequest->LogProgress(HTTPLOG::NONE);
	Http()->Run(m_pRequest);
	m_Searching = Search;
	m_State = STATE_LOADING;
}

void CXcLyrics::TakeAnswer()
{
	std::shared_ptr<IHttpRequest> pRequest = m_pRequest;
	m_pRequest = nullptr;
	json_value *pJson = pRequest->State() == EHttpState::DONE ? pRequest->ResultJson() : nullptr;
	const char *pLrc = nullptr;
	if(pJson && pJson->type == json_object)
	{
		const json_value *pSynced = json_object_get(pJson, "syncedLyrics");
		if(pSynced && pSynced->type == json_string)
			pLrc = json_string_get(pSynced);
	}
	else if(pJson && pJson->type == json_array)
	{
		// the first result that has timed lyrics
		for(int i = 0; i < json_array_length(pJson) && !pLrc; i++)
		{
			const json_value *pSynced = json_object_get(json_array_get(pJson, i), "syncedLyrics");
			if(pSynced && pSynced->type == json_string && json_string_get(pSynced)[0])
				pLrc = json_string_get(pSynced);
		}
	}
	if(pLrc)
	{
		m_vLines = ParseLrc(pLrc);
		m_Cache[m_Key] = m_vLines;
		m_State = m_vLines.empty() ? STATE_NOT_FOUND : STATE_FOUND;
	}
	if(pJson)
		json_value_free(pJson);
	if(m_State == STATE_FOUND)
		return;
	// not found exactly: search once more by the words
	if(!m_Searching)
		Request(true);
	else
	{
		m_State = STATE_NOT_FOUND;
		m_Cache[m_Key] = {};
	}
}

void CXcLyrics::OnRender()
{
	const float Dt = std::min(Client()->RenderFrameTime(), 0.1f);
	const CXcMusic &Music = GameClient()->m_XcMusic;
	const bool InGame = Client()->State() == IClient::STATE_ONLINE || Client()->State() == IClient::STATE_DEMOPLAYBACK;
	if(!g_Config.m_XcLyrics || !Music.HasTrack())
	{
		m_Fade = std::max(m_Fade - Dt * 3.0f, 0.0f);
		m_vWords.clear();
		return;
	}

	// a new track: the lyrics from the memory, or ask for them
	const SXcMediaState &State = Music.State();
	const std::string Key = State.m_Artist + "\x1f" + State.m_Title;
	if(Key != m_Key)
	{
		if(m_pRequest)
			m_pRequest->Abort();
		m_pRequest = nullptr;
		m_Key = Key;
		m_vLines.clear();
		m_vWords.clear();
		m_LastLine = -1;
		CleanTitle(State.m_Artist, State.m_Title, m_Artist, m_Title);
		m_Duration = State.m_Duration;
		auto It = m_Cache.find(Key);
		if(It != m_Cache.end())
		{
			m_vLines = It->second;
			m_State = m_vLines.empty() ? STATE_NOT_FOUND : STATE_FOUND;
		}
		else if(!m_Title.empty())
			Request(false);
		else
			m_State = STATE_NOT_FOUND;
	}
	if(m_pRequest && m_pRequest->Done())
		TakeAnswer();

	const double Position = Music.Position() + g_Config.m_XcLyricsOffset / 1000.0;
	const bool Playing = Music.PausedFor() <= 0.0f;
	m_Fade = std::clamp(m_Fade + (m_State == STATE_FOUND && Playing ? Dt : -Dt) * 3.0f, 0.0f, 1.0f);
	if(m_State != STATE_FOUND || Position < 0.0 || !InGame)
		return;

	// the line sung now: the last one that has started
	int Line = -1;
	for(int i = 0; i < (int)m_vLines.size() && m_vLines[i].m_Time <= Position; i++)
		Line = i;
	if(Line != m_LastLine)
	{
		m_LastLine = Line;
		// the words of the new line pop up one by one over the time of the line
		if(Line >= 0 && Playing && (g_Config.m_XcLyricsShow == SHOW_MAP || g_Config.m_XcLyricsShow == SHOW_BOTH))
		{
			const double Next = Line + 1 < (int)m_vLines.size() ? m_vLines[Line + 1].m_Time : m_vLines[Line].m_Time + 4.0;
			const float Length = (float)std::clamp(Next - m_vLines[Line].m_Time, 0.5, 8.0);
			std::vector<std::string> vWords;
			const std::string &Text = m_vLines[Line].m_Text;
			size_t Start = 0;
			while(Start < Text.size())
			{
				size_t End = Text.find(' ', Start);
				if(End == std::string::npos)
					End = Text.size();
				if(End > Start)
					vWords.push_back(Text.substr(Start, End - Start));
				Start = End + 1;
			}
			// the words of the line before go away quickly
			for(SWord &Word : m_vWords)
				Word.m_Age = std::max(Word.m_Age, Word.m_Delay + WORD_LIFE - 0.3f);
			// in a row over the tee, as the line is written, each word comes in its turn
			const float Size = WORD_SIZE * g_Config.m_XcLyricsSize / 100.0f;
			const float Space = TextRender()->TextWidth(Size, " ");
			float Total = 0.0f;
			std::vector<float> vWidths;
			for(const std::string &Word : vWords)
			{
				vWidths.push_back(TextRender()->TextWidth(Size, Word.c_str()));
				Total += vWidths.back() + Space;
			}
			float x = -(Total - Space) * 0.5f;
			for(size_t i = 0; i < vWords.size(); i++)
			{
				m_vWords.push_back({vWords[i], vec2(x + vWidths[i] * 0.5f, -78.0f + random_float(-5.0f, 5.0f)), 0.0f, Length * 0.8f * i / std::max<size_t>(vWords.size(), 1)});
				x += vWidths[i] + Space;
			}
		}
	}
	if(g_Config.m_XcLyricsShow == SHOW_MAP || g_Config.m_XcLyricsShow == SHOW_BOTH)
		RenderWords(Dt);
	if(g_Config.m_XcLyricsShow == SHOW_PLATE || g_Config.m_XcLyricsShow == SHOW_BOTH)
		RenderPlate(Line, Position);
}

// three lines at the bottom: the one before (dim), the one sung now (bright, filled as it goes), the next one (dim)
void CXcLyrics::RenderPlate(int Line, double Position)
{
	if(m_Fade <= 0.0f)
		return;
	const float Width = (float)Graphics()->ScreenWidth(), Height = (float)Graphics()->ScreenHeight();
	Graphics()->MapScreen(CScreenRect(0.0f, 0.0f, Width, Height));
	const float k = Height / 1080.0f * g_Config.m_XcLyricsSize / 100.0f;
	const float Size = 30.0f * k, SmallSize = 21.0f * k;
	const float y = Height * g_Config.m_XcLyricsY / 100.0f;
	const ColorRGBA Accent = EffectColor(GameClient(), 0, 0, -1);
	const char *pNow = Line >= 0 ? m_vLines[Line].m_Text.c_str() : "";
	const char *pBefore = Line >= 1 ? m_vLines[Line - 1].m_Text.c_str() : "";
	const char *pAfter = Line + 1 < (int)m_vLines.size() ? m_vLines[Line + 1].m_Text.c_str() : "";
	const float NowWidth = TextRender()->TextWidth(Size, pNow);
	const float PlateWidth = std::max({NowWidth, TextRender()->TextWidth(SmallSize, pBefore), TextRender()->TextWidth(SmallSize, pAfter), 200.0f * k}) + 48.0f * k;
	const float PlateHeight = Size + SmallSize * 2.0f + 36.0f * k;
	const float x0 = (Width - PlateWidth) * 0.5f;
	Graphics()->DrawRect(x0, y - PlateHeight * 0.5f, PlateWidth, PlateHeight, ColorRGBA(0.05f, 0.06f, 0.08f, 0.55f * m_Fade), IGraphics::CORNER_ALL, 16.0f * k);

	const auto Centered = [&](const char *pText, float TextSize, float Ty, ColorRGBA Color) {
		if(!pText[0])
			return;
		TextRender()->TextColor(Color);
		TextRender()->Text((Width - TextRender()->TextWidth(TextSize, pText)) * 0.5f, Ty, TextSize, pText);
	};
	const float Top = y - PlateHeight * 0.5f + 12.0f * k;
	Centered(pBefore, SmallSize, Top, ColorRGBA(1.0f, 1.0f, 1.0f, 0.4f * m_Fade));
	Centered(pAfter, SmallSize, Top + SmallSize + Size + 12.0f * k, ColorRGBA(1.0f, 1.0f, 1.0f, 0.4f * m_Fade));
	// the line sung now: white, then the part already sung once more in the accent color, cut at the progress
	if(pNow[0])
	{
		const float NowY = Top + SmallSize + 6.0f * k;
		Centered(pNow, Size, NowY, ColorRGBA(1.0f, 1.0f, 1.0f, 0.95f * m_Fade));
		const double Start = m_vLines[Line].m_Time;
		const double End = Line + 1 < (int)m_vLines.size() ? m_vLines[Line + 1].m_Time : Start + 4.0;
		const float Progress = (float)std::clamp((Position - Start) / std::max(End - Start, 0.1), 0.0, 1.0);
		const float NowX = (Width - NowWidth) * 0.5f;
		Graphics()->ClipEnable((int)NowX, (int)NowY, (int)(NowWidth * Progress) + 1, (int)(Size * 1.4f));
		TextRender()->TextColor(Accent.WithAlpha(m_Fade));
		TextRender()->Text(NowX, NowY, Size, pNow);
		Graphics()->ClipDisable();
	}
	TextRender()->TextColor(TextRender()->DefaultTextColor());
}

// the words of the line fly up from next to your tee and fade
void CXcLyrics::RenderWords(float Dt)
{
	const int Local = GameClient()->m_Snap.m_LocalClientId;
	const bool HasTee = Local >= 0 && GameClient()->m_Snap.m_aCharacters[Local].m_Active;
	const vec2 Tee = HasTee ? GameClient()->m_aClients[Local].m_RenderPos : GameClient()->m_Camera.m_Center;
	for(SWord &Word : m_vWords)
		Word.m_Age += Dt;
	m_vWords.erase(std::remove_if(m_vWords.begin(), m_vWords.end(), [](const SWord &Word) { return Word.m_Age - Word.m_Delay > WORD_LIFE; }), m_vWords.end());
	if(m_vWords.empty())
		return;

	const vec2 Camera = GameClient()->m_Camera.m_Center;
	const CScreenRect Screen = Graphics()->MapScreenToWorld(Camera.x, Camera.y, 100.0f, 100.0f, 100.0f, 0.0f, 0.0f, Graphics()->ScreenAspect(), GameClient()->m_Camera.m_Zoom);
	Graphics()->MapScreen(Screen);
	const float Size = WORD_SIZE * g_Config.m_XcLyricsSize / 100.0f;
	for(const SWord &Word : m_vWords)
	{
		const float t = Word.m_Age - Word.m_Delay;
		if(t < 0.0f)
			continue;
		const float Life = t / WORD_LIFE;
		// pops in a little bigger, rises, fades at the end
		const float Pop = t < 0.15f ? 1.0f + (0.15f - t) * 2.0f : 1.0f;
		const float Alpha = std::min(t / 0.12f, 1.0f) * (1.0f - std::max(Life - 0.6f, 0.0f) / 0.4f);
		const float TextSize = Size * Pop;
		const vec2 Pos = Tee + Word.m_Offset + vec2(0.0f, -40.0f * Life);
		const float TextWidth = TextRender()->TextWidth(TextSize, Word.m_Text.c_str());
		const ColorRGBA Color = EffectColor(GameClient(), 0, 0, -1, Word.m_Offset.x * 0.003f);
		TextRender()->TextOutlineColor(ColorRGBA(0.0f, 0.0f, 0.0f, 0.5f * Alpha));
		TextRender()->TextColor(ColorRGBA(mix(Color.r, 1.0f, 0.5f), mix(Color.g, 1.0f, 0.5f), mix(Color.b, 1.0f, 0.5f), Alpha));
		TextRender()->Text(Pos.x - TextWidth * 0.5f, Pos.y - TextSize * 0.5f, TextSize, Word.m_Text.c_str());
	}
	TextRender()->TextColor(TextRender()->DefaultTextColor());
	TextRender()->TextOutlineColor(TextRender()->DefaultTextOutlineColor());
}
